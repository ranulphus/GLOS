/* UHCI host controllers (docs/gamepad.md J2, PRD D74): GLOS takes each one from the BIOS and DOS, and finds what is
 * on its root ports.
 *
 * Sources (clean room): Intel's Universal Host Controller Interface Design Guide rev 1.1 (the registers, the frame
 * list, TDs and QHs); the PIIX4 datasheet (the legacy support register at configuration C0h); the USB 1.1
 * specification (port reset, enumeration's requests, the descriptors).
 *
 * The take-over: the legacy support register cleared (no SMI, no PCI interrupt line, its status bits cleared), the
 * controller stopped, globally reset (the bus too) and reset, then run with GLOS's frame list. No interrupt is ever
 * enabled: GLOS polls what it queued, so the line the controller shares (bench-g400: IRQ 11, the e100's) stays the
 * other device's. Its PCI function and ports then read as absent to DOS (pci_claim, vdev_hide). A controller the
 * BIOS was using for its USB keyboard emulation (the register's SMI enable) is taken too: GLOS's own keyboard and
 * mouse drivers stand in. At glos exit (usb_leave) each is stopped and reset, and the register put back as found
 * (whether a BIOS's emulation then picks up again is to be seen on a PC that has it on).
 *
 * The schedule: every frame of the 1024 points at the same chain of queue heads, interrupt pipes' first (J4) and
 * the control QH last. A control transfer's TDs hang from the control QH; the "usb" thread waits for them a tick
 * at a time.
 *
 * Root ports: the thread looks every 100 ms. A device that connects: the port reset (50 ms) and enabled, its speed
 * read; then at address 0 the first 8 bytes of its device descriptor (the control endpoint's packet size), its
 * address, its whole device descriptor, its configuration with interfaces and endpoints, and SET_CONFIGURATION.
 *     GLOS-USB uhci 00:02.2 io=ff00 ports=2 legsup=0000 [reset=slow]
 *     GLOS-USB port=1 speed=low id=081f:e401 class=00/00/00 if=03/00/00 in=81/8/10 out=- is=hid
 *     GLOS-USB port=1 gone
 *     GLOS-USB port=2 error=STEP status=XXXXXXXX           (a transfer that failed: the step, the TD's status) */
#include <string.h>
#include "glos/bootinfo.h"
#include "hid.h"
#include "io.h"
#include "joy.h"
#include "kcfg.h"
#include "kprintf.h"
#include "mm.h"
#include "pci.h"
#include "sched.h"
#include "timer.h"
#include "usb.h"
#include "usbkbd.h"
#include "vm.h"
#include "xpad.h"

#define USBCMD          0x00
#define USBSTS          0x02
#define USBINTR         0x04
#define FRNUM           0x06
#define FRBASE          0x08
#define SOFMOD          0x0C
#define PORTSC(p)       (0x10 + 2 * (p))

#define CMD_RS          0x0001
#define CMD_HCRESET     0x0002
#define CMD_GRESET      0x0004
#define CMD_CF          0x0040
#define CMD_MAXP        0x0080
#define STS_HCH         0x0020

#define PS_CCS          0x0001          /* a device is there */
#define PS_CSC          0x0002          /* ... which changed (write 1 to clear) */
#define PS_PE           0x0004          /* the port is enabled */
#define PS_PEC          0x0008          /* ... which changed (write 1 to clear) */
#define PS_LSDA         0x0100          /* a low-speed device */
#define PS_PR           0x0200          /* reset */
#define PS_RW           0x1244          /* the bits a write keeps (PE, RD, PR, SUSP); CSC and PEC are cleared by 1s */

#define UHCI_LEGSUP     0xC0
#define LEGSUP_SMIEN    0x0010
#define LEGSUP_CLEAR    0x8F00          /* its status bits: a 1 clears them */

#define MAX_HC          2
#define MAX_DEV         16
#define NTD             40
#define NPIPE           8               /* interrupt IN pipes, a controller */

struct hc {
    struct pci_dev *pci;
    u16 io;
    u32 legsup;                         /* as found, for usb_leave */
    u32 *frames;                        /* the frame list (1024 links) */
    u32 frames_phys;
    u8 *pool;                           /* a page: the control QH, the TDs, the setup packet and a data buffer */
    u32 pool_phys;
    struct uhci_qh *qh_ctrl, *qh_int;   /* the control QH; the pipes' QHs (NPIPE), in the frames' chain first */
    struct uhci_td *td, *td_int;        /* a control transfer's TDs; a TD a pipe */
    u8 *setup, *data, *int_buf;         /* the setup packet, a control transfer's data, 64 bytes a pipe */
    u8 ports, slow_reset;
    u8 port_dev[2];                     /* the device on each root port: its index + 1, 0 none */
    u8 tries[2];                        /* attempts at the device there since it connected (3 at most) */
};

static struct hc hc[MAX_HC];
static int nhc;
static struct usb_dev dev[MAX_DEV];
static struct hid_pad pads[MAX_DEV];    /* a pad's report layout, by its device's index */
static u8 pad_used[JOY_PADS];
static u8 last_report[MAX_DEV][64];     /* a pipe's last report, so that only a change goes on (some pads, the */
static u8 last_len[MAX_DEV];            /* owner's 081f:e401 among them, answer every poll despite SET_IDLE(0)) */
static u8 next_addr = 1;
static u32 rep_code, rep_next, rep_delay = 512, rep_every = 102;      /* a USB keyboard's repeat (GLOS's: in ticks) */
static int started;

#define PHYS(h, p) ((h)->pool_phys + (u32)((const u8 *)(p) - (h)->pool))

/* ---- registers, waits */

static int leaving;                     /* glos exit: the kernel's tick has stopped, so nothing may sleep */

static void wait_ms(u32 ms)
{
    if (leaving) {                              /* (a port read takes about a microsecond, as e100.c's pause) */
        u32 n = ms * 1000;
        while (n--)
            inb(0x80);
        return;
    }
    thread_sleep_until(timer_ticks() + (ms * 1024 + 999) / 1000 + 1);
}

static void port_write(struct hc *h, int p, u16 v) { outw((u16)(h->io + PORTSC(p)), v); }
static u16 port_read(struct hc *h, int p) { return inw((u16)(h->io + PORTSC(p))); }

static int stop(struct hc *h)
{
    int i;
    outw((u16)(h->io + USBCMD), 0);
    for (i = 0; i < 20 && !(inw((u16)(h->io + USBSTS)) & STS_HCH); i++)
        wait_ms(1);
    return (inw((u16)(h->io + USBSTS)) & STS_HCH) != 0;
}

/* HCRESET clears itself when done (86Box's model never clears it: slow_reset, and on after 10 ms). */
static void reset(struct hc *h)
{
    int i;
    outw((u16)(h->io + USBCMD), CMD_HCRESET);
    for (i = 0; i < 10 && (inw((u16)(h->io + USBCMD)) & CMD_HCRESET); i++)
        wait_ms(1);
    if (inw((u16)(h->io + USBCMD)) & CMD_HCRESET)
        h->slow_reset = 1;
    outw((u16)(h->io + USBCMD), 0);
}

/* ---- control transfers */

/* One control transfer to a device: 0, or -1 (the step's TD status logged by the caller in *status). data: in, the
   buffer filled (up to setup's length; *got the bytes that came); out, its bytes sent. */
static int control(struct hc *h, u32 addr, u32 maxp, int low, const u8 setup[8], void *data, u32 *got, u32 *status)
{
    u32 len = setup[6] | (u32)setup[7] << 8, n, i, t0 = timer_ticks(), moved = 0;
    int in = setup[0] & 0x80;
    if (len > 1024)
        return -1;
    memcpy(h->setup, setup, 8);
    if (!in && len)
        memcpy(h->data, data, len);
    n = uhci_control_tds(h->td, NTD, PHYS(h, h->td), h->setup, PHYS(h, h->setup), PHYS(h, h->data), addr, maxp,
                         low);
    if (!n)
        return -1;
    h->qh_ctrl->elem = PHYS(h, h->td);          /* (the controller takes it from the next frame on) */
    for (;;) {
        u32 last = h->td[n - 1].ctrl;
        if (!(last & UHCI_TD_ACTIVE))
            break;
        for (i = 0; i + 1 < n; i++) {           /* the TDs done so far: an error ends it; a short IN packet */
            u32 c = h->td[i].ctrl;              /* (SPD) leaves the queue stopped at that TD: on to the status */
            if (c & UHCI_TD_ACTIVE)             /* stage */
                break;
            if (c & UHCI_TD_ERRORS) {
                h->qh_ctrl->elem = UHCI_LP_T;
                *status = c;
                return -1;
            }
            if (in && i > 0 && UHCI_TD_ACTLEN(c) < (((h->td[i].token >> 21) + 1) & 0x7FFu)) {
                h->qh_ctrl->elem = PHYS(h, &h->td[n - 1]);
                break;
            }
        }
        if (timer_ticks() - t0 > 512) {
            h->qh_ctrl->elem = UHCI_LP_T;
            *status = last | 0x80000000u;       /* (a timeout: bit 31) */
            return -1;
        }
        thread_sleep_until(timer_ticks() + 1);
    }
    h->qh_ctrl->elem = UHCI_LP_T;
    if (h->td[n - 1].ctrl & UHCI_TD_ERRORS) {
        *status = h->td[n - 1].ctrl;
        return -1;
    }
    for (i = 1; i + 1 < n; i++)
        if (!(h->td[i].ctrl & UHCI_TD_ACTIVE))
            moved += UHCI_TD_ACTLEN(h->td[i].ctrl);
    if (in && data)
        memcpy(data, h->data, moved < len ? moved : len);
    if (got)
        *got = moved;
    return 0;
}

static int get_desc(struct hc *h, u32 addr, u32 maxp, int low, u8 type, u8 index, u16 len, u8 *out, u32 *got,
                    u32 *status)
{
    u8 s[8] = { 0x80, 6, index, type, 0, 0, (u8)len, (u8)(len >> 8) };
    return control(h, addr, maxp, low, s, out, got, status);
}

/* ---- devices */

static const char *what(const struct usb_dev *d)
{
    u8 c = d->dclass ? d->dclass : d->iclass, s = d->dclass ? d->dsub : d->isub, p = d->dclass ? d->dproto : d->iproto;
    if (c == 0x09)
        return "hub";
    if (c == 0x03 && s == 0x01 && p == 0x01)
        return "hid-keyboard";
    if (c == 0x03 && s == 0x01 && p == 0x02)
        return "hid-mouse";
    if (c == 0x03)
        return "hid";
    if (c == 0xFF && s == 0x5D && p == 0x01)
        return "xbox360";
    if (c == 0xFF && s == 0x47 && p == 0xD0)
        return "xbox-gip";
    return "other";
}

/* The configuration's interfaces and endpoints: the first interface's class, its first interrupt IN and OUT. */
static void parse_config(struct usb_dev *d, const u8 *c, u32 len)
{
    u32 at = 0, ifaces = 0;
    while (at + 2 <= len && c[at] >= 2 && at + c[at] <= len) {
        if (c[at + 1] == 2 && c[at] >= 9) {                         /* configuration */
            d->config = c[at + 5];
        } else if (c[at + 1] == 4 && c[at] >= 9) {                  /* interface */
            if (!ifaces++) {
                d->iclass = c[at + 5], d->isub = c[at + 6], d->iproto = c[at + 7];
                d->ifnum = c[at + 2];
            }
        } else if (c[at + 1] == 0x21 && c[at] >= 9 && ifaces == 1 && c[at + 6] == 0x22) {   /* HID: its report */
            d->hid_rlen = (u16)(c[at + 7] | c[at + 8] << 8);
        } else if (c[at + 1] == 5 && c[at] >= 7 && ifaces == 1 && (c[at + 3] & 3) == 3) {     /* endpoint: interrupt */
            if (c[at + 2] & 0x80) {
                if (!d->ep_in)
                    d->ep_in = c[at + 2], d->ep_in_maxp = c[at + 4], d->ep_in_ival = c[at + 6];
            } else if (!d->ep_out) {
                d->ep_out = c[at + 2], d->ep_out_maxp = c[at + 4];
            }
        }
        at += c[at];
    }
    d->nif = (u8)ifaces;
}

static void fail(const struct usb_dev *d, const char *step, u32 status);
static char *loc(const struct usb_dev *d, char *b);

/* ---- interrupt IN pipes: one TD a pipe, hung from its QH, which every frame passes (the controller retries a
   NAKed TD each frame, so a device is asked every millisecond however long its interval); re-armed with the data
   toggle flipped each time it has brought a report */

static void pipe_arm(struct hc *h, struct usb_dev *d)
{
    u32 k = d->pipe - 1u;
    struct uhci_td *t = &h->td_int[k];
    t->link = UHCI_LP_T;
    t->buf = PHYS(h, h->int_buf + k * 64);
    t->token = USB_PID_IN | (u32)d->addr << 8 | (u32)(d->ep_in & 0xF) << 15 | (u32)(d->toggle & 1) << 19
               | ((u32)(d->ep_in_maxp ? d->ep_in_maxp - 1 : 0x7FF) & 0x7FF) << 21;
    t->ctrl = UHCI_TD_ACTIVE | UHCI_TD_CERR3 | (d->low ? UHCI_TD_LS : 0);
    h->qh_int[k].elem = PHYS(h, t);
}

static void pipe_stop(struct hc *h, struct usb_dev *d)
{
    if (d->pipe) {
        h->qh_int[d->pipe - 1].elem = UHCI_LP_T;
        d->pipe = 0;
    }
    if (d->pad) {
        pad_used[d->pad - 1] = 0;
        joy_pad(d->pad - 1, 0);
        d->pad = 0;
    }
}

/* A pad's report to the virtual game port: X and Y the left stick, the hat folded in; Z and Rz, else Rx and Ry,
   the right. */
static void pad_report(struct usb_dev *d, const u8 *r, u32 len)
{
    struct hid_pad_state s;
    struct joy_pad jp;
    const struct hid_pad *hp = &pads[d - dev];
    static const signed char hx[8] = { 0, 1, 1, 1, 0, -1, -1, -1 }, hy[8] = { -1, -1, 0, 1, 1, 1, 0, -1 };
    if (hid_pad_read(hp, r, len, &s) != 0)
        return;
    jp.lx = s.axis[0], jp.ly = s.axis[1];
    if (s.hat < 8) {
        if (hx[s.hat])
            jp.lx = hx[s.hat] < 0 ? 0 : 255;
        if (hy[s.hat])
            jp.ly = hy[s.hat] < 0 ? 0 : 255;
    }
    if ((hp->axes & 0x24) == 0x24)              /* Z and Rz */
        jp.rx = s.axis[2], jp.ry = s.axis[5], jp.right = 1;
    else if ((hp->axes & 0x18) == 0x18)         /* Rx and Ry */
        jp.rx = s.axis[3], jp.ry = s.axis[4], jp.right = 1;
    else
        jp.rx = jp.ry = 128, jp.right = 0;
    jp.buttons = s.buttons;
    joy_pad(d->pad - 1, &jp);
}

static void pipes_poll(struct hc *h)
{
    int i;
    for (i = 0; i < MAX_DEV; i++) {
        struct usb_dev *d = &dev[i];
        u32 c;
        if (!d->used || !d->pipe || &hc[d->hc_i] != h)
            continue;
        c = h->td_int[d->pipe - 1].ctrl;
        if (c & UHCI_TD_ACTIVE)
            continue;
        if (c & UHCI_TD_ERRORS) {               /* (unplugged, or a stall: the port's change will tell) */
            char b[16];
            if (++d->errors == 1)
                kprintf("GLOS-USB port=%s pipe-error status=%08x\n", loc(d, b), c);
            if (d->errors > 20) {
                pipe_stop(h, d);
                continue;
            }
        } else {
            const u8 *r = h->int_buf + (d->pipe - 1) * 64;
            u32 len = UHCI_TD_ACTLEN(c);
            d->reports++;
            d->toggle ^= 1;
            if (d->nports && len)               /* a hub's status pipe: a bit for each port that changed */
                d->hub_dirty = 1;
            if (d->kind == USB_KIND_MOUSE && len >= 3) {        /* (every report counts: no comparing) */
                vkbc_mouse((signed char)r[1], (signed char)r[2], r[0] & 7);
            } else if (d->kind == USB_KIND_KBD && len >= 8 && memcmp(r, last_report[i], 8)) {
                struct usbkbd_event ev[16];
                u32 k, n, rep = rep_code;
                n = usbkbd_diff(last_report[i], r, ev, 16, &rep);
                for (k = 0; k < n; k++)
                    vkbc_key(ev[k].code, ev[k].down);
                if (rep != rep_code)
                    rep_next = timer_ticks() + rep_delay;
                rep_code = rep;
                memcpy(last_report[i], r, 8);
            } else if (d->pad && (len != last_len[i] || memcmp(r, last_report[i], len))) {
                struct joy_pad jp;
                memcpy(last_report[i], r, len);
                last_len[i] = (u8)len;
                if (!d->kind)
                    pad_report(d, r, len);
                else if (xpad_read(d->kind, r, len, &jp) == 0)
                    joy_pad(d->pad - 1, &jp);
            }
        }
        pipe_arm(h, d);
    }
}

/* A pipe slot on the device's controller: 0, or -1 if none is free. */
static int pipe_slot(struct usb_dev *d)
{
    int n, i, taken;
    for (n = 0; n < NPIPE; n++) {
        for (taken = 0, i = 0; i < MAX_DEV; i++)
            if (dev[i].used && dev[i].hc_i == d->hc_i && dev[i].pipe == n + 1 && &dev[i] != d)
                taken = 1;
        if (!taken)
            break;
    }
    if (n == NPIPE)
        return -1;
    d->pipe = (u8)(n + 1);
    d->toggle = 0;
    last_len[d - dev] = 0xFF;                   /* (the first report always goes on) */
    return 0;
}

/* A joystick pad and a pipe slot for a device: 0, or -1 if either is short. */
static int pad_take(struct usb_dev *d)
{
    int k;
    for (k = 0; k < JOY_PADS && pad_used[k]; k++) ;
    if (k == JOY_PADS || pipe_slot(d) != 0)
        return -1;
    pad_used[k] = 1;
    d->pad = (u8)(k + 1);
    return 0;
}

/* A boot keyboard or mouse (HID subclass 1): the boot protocol (SET_PROTOCOL 0: the reports of HID 1.11 appendix
   B), reports on a change only (SET_IDLE 0); a keyboard starts with nothing held, and repeats as [input] says. */
static void boot_start(struct hc *h, struct usb_dev *d)
{
    u8 proto[8] = { 0x21, 0x0B, 0, 0, 0, 0, 0, 0 }, idle[8] = { 0x21, 0x0A, 0, 0, 0, 0, 0, 0 };
    char v[16];
    u32 status = 0;
    if (!d->ep_in || pipe_slot(d) != 0)
        return;
    proto[4] = idle[4] = d->ifnum;
    control(h, d->addr, d->maxp0, d->low, proto, 0, 0, &status);
    control(h, d->addr, d->maxp0, d->low, idle, 0, 0, &status);
    d->kind = (u8)(d->iproto == 1 ? USB_KIND_KBD : USB_KIND_MOUSE);
    d->is = d->kind == USB_KIND_KBD ? "hid-keyboard" : "hid-mouse";
    if (d->kind == USB_KIND_KBD) {
        memset(last_report[d - dev], 0, 8);
        last_len[d - dev] = 8;
        v[0] = 0;
        kcfg_get("input", "repeat_delay", v, sizeof v);
        if (v[0] >= '1' && v[0] <= '9') {
            u32 ms = 0;
            const char *c;
            for (c = v; *c >= '0' && *c <= '9'; c++)
                ms = ms * 10 + (u32)(*c - '0');
            rep_delay = ms * 1024 / 1000;
        }
        v[0] = 0;
        kcfg_get("input", "repeat_rate", v, sizeof v);
        if (v[0] >= '1' && v[0] <= '9') {
            u32 hz = 0;
            const char *c;
            for (c = v; *c >= '0' && *c <= '9'; c++)
                hz = hz * 10 + (u32)(*c - '0');
            if (hz)
                rep_every = 1024 / hz;
        }
    }
    pipe_arm(h, d);
}

/* A HID device that is a joystick or gamepad: its report descriptor read and parsed, its pipe started. */
static void hid_start(struct hc *h, struct usb_dev *d)
{
    u8 idle[8] = { 0x21, 0x0A, 0, 0, 0, 0, 0, 0 }, get[8] = { 0x81, 6, 0, 0x22, 0, 0, 0, 0 }, *buf;
    u32 got = 0, status = 0;
    int n;
    if (d->isub == 1 && (d->iproto == 1 || d->iproto == 2)) {
        boot_start(h, d);
        return;
    }
    if (!d->ep_in || !d->hid_rlen || d->hid_rlen > 1024)
        return;
    idle[4] = d->ifnum;                         /* SET_IDLE(0): reports on a change only (a stall is fine) */
    control(h, d->addr, d->maxp0, d->low, idle, 0, 0, &status);
    get[4] = d->ifnum;
    get[6] = (u8)d->hid_rlen, get[7] = (u8)(d->hid_rlen >> 8);
    if (!(buf = kmalloc(1024)))
        return;
    if (control(h, d->addr, d->maxp0, d->low, get, buf, &got, &status) != 0) {
        kfree(buf);
        fail(d, "report-descriptor", status);
        return;
    }
    n = hid_pad_parse(buf, got, &pads[d - dev]);
    kfree(buf);
    if (n != 0 || pad_take(d) != 0)
        return;
    d->is = "hid-pad";
    pipe_arm(h, d);
}

/* An Xbox pad: GIP's are silent until the power-on packet (docs/gamepad-j0/: 05 20 00 01 00) goes to their
   interrupt OUT endpoint; a 360's report at once. */
static void xbox_start(struct hc *h, struct usb_dev *d)
{
    static const u8 power_on[5] = { 0x05, 0x20, 0x00, 0x01, 0x00 };
    d->kind = (u8)(d->iproto == 0xD0 ? XPAD_GIP : XPAD_360);
    if (!d->ep_in || pad_take(d) != 0)
        return;
    if (d->kind == XPAD_GIP) {
        struct uhci_td *t = &h->td_int[d->pipe - 1];
        u32 t0 = timer_ticks();
        if (!d->ep_out) {
            pipe_stop(h, d);
            return;
        }
        memcpy(h->int_buf + (d->pipe - 1) * 64, power_on, sizeof power_on);
        t->link = UHCI_LP_T;
        t->buf = PHYS(h, h->int_buf + (d->pipe - 1) * 64);
        t->token = USB_PID_OUT | (u32)d->addr << 8 | (u32)(d->ep_out & 0xF) << 15 | (u32)(d->toggle_out & 1) << 19
                   | (u32)(sizeof power_on - 1) << 21;
        t->ctrl = UHCI_TD_ACTIVE | UHCI_TD_CERR3;
        h->qh_int[d->pipe - 1].elem = PHYS(h, t);
        while ((t->ctrl & UHCI_TD_ACTIVE) && timer_ticks() - t0 < 100)
            thread_sleep_until(timer_ticks() + 1);
        h->qh_int[d->pipe - 1].elem = UHCI_LP_T;
        if ((t->ctrl & (UHCI_TD_ACTIVE | UHCI_TD_ERRORS))) {
            fail(d, "power-on", t->ctrl);
            pipe_stop(h, d);
            return;
        }
        d->toggle_out ^= 1;
    }
    d->is = d->kind == XPAD_GIP ? "xbox-gip-pad" : "xbox360-pad";
    pipe_arm(h, d);
}

static void fail(const struct usb_dev *d, const char *step, u32 status)
{
    char b[16];
    kprintf("GLOS-USB port=%s error=%s status=%08x\n", loc(d, b), step, status);
}

/* Where a device is: its root port, then each hub port on the way ("2.3"). b: 16 bytes. */
static char *loc(const struct usb_dev *d, char *b)
{
    u32 n;
    if (d->parent && d->parent <= MAX_DEV) {
        loc(&dev[d->parent - 1], b);
        n = (u32)strlen(b);
        if (n < 13)
            ksnprintf(b + n, 16 - n, ".%u", d->hport);
    } else {
        ksnprintf(b, 16, "%u", d->port);
    }
    return b;
}

static void hub_start(struct hc *h, struct usb_dev *d);

/* A device just reset on a port (a root port, or port hport of the hub parent - 1): its address, descriptors and
   configuration; then what it is gets started. Its index, or -1. */
static int enumerate(struct hc *h, int root, int parent, int hport, int low)
{
    struct usb_dev *d;
    u8 buf[256];
    u32 got = 0, status = 0, total;
    int i;
    char b[16];
    u8 s_addr[8] = { 0x00, 5, 0, 0, 0, 0, 0, 0 }, s_conf[8] = { 0x00, 9, 0, 0, 0, 0, 0, 0 };

    for (i = 0; i < MAX_DEV && dev[i].used; i++) ;
    if (i == MAX_DEV) {
        kprintf("GLOS-USB port=%d error=devices status=%08x\n", root + 1, MAX_DEV);
        return -1;
    }
    d = &dev[i];
    memset(d, 0, sizeof *d);
    d->hc_i = (u8)(h - hc);
    d->port = (u8)(root + 1);
    d->parent = (u8)parent;
    d->hport = (u8)hport;
    d->low = (u8)low;
    if (get_desc(h, 0, 8, low, 1, 0, 8, buf, &got, &status) != 0 || got < 8) {
        fail(d, "device8", status);
        return -1;
    }
    d->maxp0 = buf[7] ? buf[7] : 8;
    d->addr = next_addr;
    next_addr = (u8)(next_addr % 127 + 1);
    s_addr[2] = d->addr;
    if (control(h, 0, d->maxp0, low, s_addr, 0, 0, &status) != 0) {
        fail(d, "address", status);
        return -1;
    }
    wait_ms(2);                                 /* (SET_ADDRESS's recovery: 2 ms) */
    if (get_desc(h, d->addr, d->maxp0, low, 1, 0, 18, buf, &got, &status) != 0 || got < 18) {
        fail(d, "device", status);
        return -1;
    }
    d->vendor = (u16)(buf[8] | buf[9] << 8);
    d->product = (u16)(buf[10] | buf[11] << 8);
    d->release = (u16)(buf[12] | buf[13] << 8);
    d->dclass = buf[4], d->dsub = buf[5], d->dproto = buf[6];
    if (get_desc(h, d->addr, d->maxp0, low, 2, 0, 9, buf, &got, &status) != 0 || got < 9) {
        fail(d, "config9", status);
        return -1;
    }
    total = buf[2] | (u32)buf[3] << 8;
    if (total > sizeof buf)
        total = sizeof buf;
    if (get_desc(h, d->addr, d->maxp0, low, 2, 0, (u16)total, buf, &got, &status) != 0) {
        fail(d, "config", status);
        return -1;
    }
    parse_config(d, buf, got);
    s_conf[2] = d->config;
    if (control(h, d->addr, d->maxp0, low, s_conf, 0, 0, &status) != 0) {
        fail(d, "configure", status);
        return -1;
    }
    d->is = what(d);
    d->used = 1;
    if (d->dclass == 0x09 || d->iclass == 0x09)
        hub_start(h, d);
    else if (d->iclass == 0x03)
        hid_start(h, d);
    else if (d->iclass == 0xFF && ((d->isub == 0x47 && d->iproto == 0xD0) || (d->isub == 0x5D && d->iproto == 0x01)))
        xbox_start(h, d);
    if (d->nports)
        kprintf("GLOS-USB port=%s speed=%s id=%04x:%04x is=hub ports=%u\n", loc(d, b), low ? "low" : "full",
                d->vendor, d->product, d->nports);
    else if (d->ep_in)
        kprintf("GLOS-USB port=%s speed=%s id=%04x:%04x class=%02x/%02x/%02x if=%02x/%02x/%02x in=%02x/%u/%u%s is=%s\n",
                loc(d, b), low ? "low" : "full", d->vendor, d->product, d->dclass, d->dsub, d->dproto, d->iclass,
                d->isub, d->iproto, d->ep_in, d->ep_in_maxp, d->ep_in_ival, d->ep_out ? " out=yes" : "", d->is);
    else
        kprintf("GLOS-USB port=%s speed=%s id=%04x:%04x class=%02x/%02x/%02x if=%02x/%02x/%02x is=%s\n", loc(d, b),
                low ? "low" : "full", d->vendor, d->product, d->dclass, d->dsub, d->dproto, d->iclass, d->isub,
                d->iproto, d->is);
    return i;
}

/* A device gone, and everything behind it if it is a hub. */
static void detach_dev(struct hc *h, int i)
{
    struct usb_dev *d = &dev[i];
    char b[16];
    int k;
    if (!d->used)
        return;
    for (k = 0; k < MAX_DEV; k++)
        if (dev[k].used && dev[k].parent == i + 1)
            detach_dev(h, k);
    pipe_stop(h, d);
    if (d->kind == USB_KIND_KBD) {              /* (its keys let go, so nothing stays held) */
        static const u8 none[8];
        struct usbkbd_event ev[16];
        u32 k, n = usbkbd_diff(last_report[i], none, ev, 16, 0);
        for (k = 0; k < n; k++)
            vkbc_key(ev[k].code, 0);
        rep_code = 0;
    }
    kprintf("GLOS-USB port=%s gone\n", loc(d, b));
    if (d->parent && d->hport >= 1 && d->hport <= 8)
        dev[d->parent - 1].child[d->hport - 1] = 0;
    d->used = 0;
}

/* ---- root ports */

static void attach(struct hc *h, int p)
{
    u16 v;
    int i, low;
    v = port_read(h, p);                        /* the port reset: 50 ms (USB 1.1 §7.1.7.3, a root port's 50) */
    port_write(h, p, (u16)((v & PS_RW & ~PS_PE) | PS_PR));
    wait_ms(50);
    port_write(h, p, (u16)(port_read(h, p) & PS_RW & ~(PS_PR | PS_PE)));
    wait_ms(1);
    for (i = 0; i < 10; i++) {                  /* enabled, its change bits cleared */
        v = port_read(h, p);
        if (!(v & PS_CCS))
            return;
        if (v & PS_PE)
            break;
        port_write(h, p, (u16)((v & PS_RW) | PS_PE | PS_CSC | PS_PEC));
        wait_ms(10);
    }
    if (!(port_read(h, p) & PS_PE)) {
        kprintf("GLOS-USB port=%d error=enable status=%08x\n", p + 1, port_read(h, p));
        return;
    }
    port_write(h, p, (u16)((port_read(h, p) & PS_RW) | PS_CSC | PS_PEC));
    low = (port_read(h, p) & PS_LSDA) != 0;
    wait_ms(10);                                /* (the recovery a reset is owed) */
    if ((i = enumerate(h, p, 0, 0, low)) >= 0)
        h->port_dev[p] = (u8)(i + 1);
}

static void detach(struct hc *h, int p)
{
    if (h->port_dev[p]) {
        detach_dev(h, h->port_dev[p] - 1);
        h->port_dev[p] = 0;
    }
}

/* ---- hubs (USB 1.1 chapter 11): the hub descriptor (29h), each port powered (SET_FEATURE PORT_POWER), the status
   pipe (a bit for each port that changed) on one of the controller's pipes; on a change, each port's status
   (GET_STATUS: wPortStatus, wPortChange): a connect cleared and looked at, a reset (SET_FEATURE PORT_RESET, until
   C_PORT_RESET) and the device enumerated as a root port's is. A low-speed device behind a full-speed hub: UHCI's
   LS bit, and the controller sends the preamble. */

#define HUB_PORT_CONNECTION     0x0001
#define HUB_PORT_ENABLE         0x0002
#define HUB_PORT_LOW_SPEED      0x0200
#define HUB_C_CONNECTION        0x0001
#define HUB_C_ENABLE            0x0002
#define HUB_C_OVER_CURRENT      0x0008
#define HUB_C_RESET             0x0010
#define FEAT_PORT_RESET         4
#define FEAT_PORT_POWER         8
#define FEAT_C_PORT_CONNECTION  16
#define FEAT_C_PORT_ENABLE      17
#define FEAT_C_PORT_OVER_CURRENT 19
#define FEAT_C_PORT_RESET       20

static int hub_feature(struct hc *h, struct usb_dev *d, int set, u32 feature, u32 port)
{
    u8 s[8] = { 0x23, (u8)(set ? 3 : 1), (u8)feature, 0, (u8)port, 0, 0, 0 };
    u32 st;
    return control(h, d->addr, d->maxp0, d->low, s, 0, 0, &st);
}

static int hub_status(struct hc *h, struct usb_dev *d, u32 port, u16 *status, u16 *change)
{
    u8 s[8] = { 0xA3, 0, 0, 0, (u8)port, 0, 4, 0 }, b[4];
    u32 got = 0, st;
    if (control(h, d->addr, d->maxp0, d->low, s, b, &got, &st) != 0 || got < 4)
        return -1;
    *status = (u16)(b[0] | b[1] << 8);
    *change = (u16)(b[2] | b[3] << 8);
    return 0;
}

static void hub_start(struct hc *h, struct usb_dev *d)
{
    u8 get[8] = { 0xA0, 6, 0, 0x29, 0, 0, 8, 0 }, buf[8];
    u32 got = 0, status = 0, k;
    if (control(h, d->addr, d->maxp0, d->low, get, buf, &got, &status) != 0 || got < 7) {
        fail(d, "hub-descriptor", status);
        return;
    }
    d->nports = (u8)(buf[2] > 8 ? 8 : buf[2]);
    for (k = 1; k <= d->nports; k++)
        hub_feature(h, d, 1, FEAT_PORT_POWER, k);
    wait_ms(buf[5] * 2u + 100);                 /* (bPwrOn2PwrGood, in 2 ms, then a connect's 100 ms) */
    d->is = "hub";
    d->hub_dirty = 1;                           /* (its ports looked at once, then on its status pipe's word) */
    if (d->ep_in && pipe_slot(d) == 0)
        pipe_arm(h, d);
}

static void hub_poll(struct hc *h, int hi)
{
    struct usb_dev *hub = &dev[hi];
    u32 p;
    int k, c;
    hub->hub_dirty = 0;
    for (p = 1; p <= hub->nports; p++) {
        u16 st, ch;
        if (hub_status(h, hub, p, &st, &ch) != 0)
            return;                             /* (the hub gone: its own port's change will say) */
        if (ch & HUB_C_CONNECTION) {
            hub_feature(h, hub, 0, FEAT_C_PORT_CONNECTION, p);
            if (hub->child[p - 1])
                detach_dev(h, hub->child[p - 1] - 1);
            hub->ctries[p - 1] = 0;
        }
        if (ch & HUB_C_ENABLE)
            hub_feature(h, hub, 0, FEAT_C_PORT_ENABLE, p);
        if (ch & HUB_C_OVER_CURRENT)
            hub_feature(h, hub, 0, FEAT_C_PORT_OVER_CURRENT, p);
        if (!(st & HUB_PORT_CONNECTION) || hub->child[p - 1] || hub->ctries[p - 1] >= 3)
            continue;
        hub->ctries[p - 1]++;
        wait_ms(100);                           /* (USB 1.1 §7.1.7.3: 100 ms for the power to settle) */
        hub_feature(h, hub, 1, FEAT_PORT_RESET, p);
        for (k = 0; k < 20; k++) {
            wait_ms(10);
            if (hub_status(h, hub, p, &st, &ch) == 0 && (ch & HUB_C_RESET))
                break;
        }
        hub_feature(h, hub, 0, FEAT_C_PORT_RESET, p);
        if (hub_status(h, hub, p, &st, &ch) != 0 || !(st & HUB_PORT_ENABLE)) {
            char b[16];
            kprintf("GLOS-USB port=%s.%u error=hub-reset status=%04x%04x\n", loc(hub, b), p, ch, st);
            continue;
        }
        wait_ms(10);
        if ((c = enumerate(h, hub->port - 1, hi + 1, (int)p, (st & HUB_PORT_LOW_SPEED) != 0)) >= 0)
            hub->child[p - 1] = (u8)(c + 1);
    }
}

static void usb_thread(void *arg)
{
    int i, p;
    u32 last_scan = timer_ticks() - 1024, last_hubs = timer_ticks();
    (void)arg;
    for (;;) {
        for (i = 0; i < nhc; i++)
            pipes_poll(&hc[i]);
        if (rep_code && (s32)(timer_ticks() - rep_next) >= 0) {     /* a USB keyboard's held key repeats */
            vkbc_key(rep_code, 1);
            rep_next += rep_every;
            if ((s32)(timer_ticks() - rep_next) > 0)
                rep_next = timer_ticks() + rep_every;
        }
        for (i = 0; i < MAX_DEV; i++)           /* a hub whose status pipe spoke (or, a second on, any hub) */
            if (dev[i].used && dev[i].nports && (dev[i].hub_dirty || timer_ticks() - last_hubs > 1024))
                hub_poll(&hc[dev[i].hc_i], i);
        if (timer_ticks() - last_hubs > 1024)
            last_hubs = timer_ticks();
        if (timer_ticks() - last_scan < 102) {  /* (the pipes every 2 ms; the root ports every 100) */
            thread_sleep_until(timer_ticks() + 2);
            continue;
        }
        last_scan = timer_ticks();
        for (i = 0; i < nhc; i++)
            for (p = 0; p < hc[i].ports; p++) {
                u16 v = port_read(&hc[i], p);
                if (v & PS_CSC) {               /* a change: whatever was there is gone, attempts start again */
                    port_write(&hc[i], p, (u16)((v & PS_RW) | PS_CSC));
                    detach(&hc[i], p);
                    hc[i].tries[p] = 0;
                }
                if ((v & PS_CCS) && !hc[i].port_dev[p] && hc[i].tries[p] < 3) {
                    hc[i].tries[p]++;
                    wait_ms(100);               /* (USB 1.1 §7.1.7.3: 100 ms for the power to settle) */
                    attach(&hc[i], p);
                }
            }
    }
}

/* ---- the controllers */

static int take(struct pci_dev *pd)
{
    struct hc *h = &hc[nhc];
    u32 cmd;
    int p;
    memset(h, 0, sizeof *h);
    h->pci = pd;
    h->io = (pd->bar[4] & 1) ? (u16)(pd->bar[4] & 0xFFE0u) : 0;
    if (!h->io)
        return -1;
    h->legsup = pd->vendor == 0x8086 ? pci_read32(pd->bus, pd->dev, pd->fn, UHCI_LEGSUP) & 0xFFFF : 0;
    if (!(h->frames = dma_alloc(1, &h->frames_phys)) || !(h->pool = dma_alloc(1, &h->pool_phys)))
        return -1;
    h->qh_ctrl = (struct uhci_qh *)h->pool;     /* the page: the control QH at 0, the pipes' QHs at 16-143, the */
    h->qh_int = (struct uhci_qh *)(h->pool + 16);      /* control TDs at 256, the setup packet at 1536, the */
    h->td = (struct uhci_td *)(h->pool + 256);         /* pipes' TDs at 1600, their buffers at 2048, a control */
    h->setup = h->pool + 1536;                         /* transfer's data at 3072 */
    h->td_int = (struct uhci_td *)(h->pool + 1600);
    h->int_buf = h->pool + 2048;
    h->data = h->pool + 3072;
    h->qh_ctrl->head = UHCI_LP_T;
    h->qh_ctrl->elem = UHCI_LP_T;
    for (p = 0; p < NPIPE; p++) {               /* each frame: the pipes' QHs in turn, then the control QH */
        h->qh_int[p].head = (p + 1 < NPIPE ? PHYS(h, &h->qh_int[p + 1]) : PHYS(h, h->qh_ctrl)) | UHCI_LP_Q;
        h->qh_int[p].elem = UHCI_LP_T;
    }
    for (p = 0; p < 1024; p++)
        h->frames[p] = PHYS(h, &h->qh_int[0]) | UHCI_LP_Q;
    if (pd->vendor == 0x8086)                   /* no SMI, no PCI interrupt, its status cleared */
        pci_write32(pd->bus, pd->dev, pd->fn, UHCI_LEGSUP, LEGSUP_CLEAR);
    cmd = pci_read32(pd->bus, pd->dev, pd->fn, 0x04);
    pci_write32(pd->bus, pd->dev, pd->fn, 0x04, (cmd & 0xFFFF) | 0x0005);       /* I/O space, bus master */
    stop(h);
    outw((u16)(h->io + USBINTR), 0);
    outw((u16)(h->io + USBCMD), CMD_GRESET);    /* the bus reset too: 10 ms (the design guide's) */
    wait_ms(11);
    outw((u16)(h->io + USBCMD), 0);
    wait_ms(1);
    reset(h);
    outw((u16)(h->io + USBINTR), 0);
    outw((u16)(h->io + USBSTS), 0x3F);
    outw((u16)(h->io + FRNUM), 0);
    outl((u16)(h->io + FRBASE), h->frames_phys);
    outb((u16)(h->io + SOFMOD), 0x40);
    outw((u16)(h->io + USBCMD), CMD_RS | CMD_CF | CMD_MAXP);
    for (p = 0; p < 2; p++)                     /* root ports: bit 7 reads 1 (the design guide's way to count) */
        if (port_read(h, p) & 0x0080)
            h->ports = (u8)(p + 1);
    pd->keeper = DEV_GLOS;
    pci_claim(pd);
    vdev_hide(h->io, 32);
    kprintf("GLOS-USB uhci %02x:%02x.%u io=%x ports=%u legsup=%04x%s\n", pd->bus, pd->dev, pd->fn, h->io, h->ports,
            h->legsup, h->slow_reset ? " reset=slow" : "");
    nhc++;
    return 0;
}

void usb_late(void)
{
    char v[8];
    int i;
    struct pci_dev *pd;
    if (started)
        return;
    started = 1;
    v[0] = 0;
    kcfg_get("usb", "enabled", v, sizeof v);
    if (!strcmp(v, "off")) {
        kprintf("GLOS-USB off\n");
        return;
    }
    for (i = 0; (pd = pci_at(i)) != 0 && nhc < MAX_HC; i++)
        if (pd->class == 0x0C && pd->subclass == 0x03 && pd->progif == 0x00)
            take(pd);
    if (nhc)
        thread_create("usb", PRIO_NORMAL, usb_thread, 0);
}

void usb_leave(void)
{
    int i;
    leaving = 1;                                /* (86Box's HCRESET never clears: reset() waits its 10 ms) */
    for (i = 0; i < nhc; i++) {
        struct hc *h = &hc[i];
        stop(h);
        reset(h);
        outw((u16)(h->io + USBINTR), 0);
        if (h->pci->vendor == 0x8086)
            pci_write32(h->pci->bus, h->pci->dev, h->pci->fn, UHCI_LEGSUP, h->legsup | LEGSUP_CLEAR);
    }
}

int usb_stat(char *b, u32 room)
{
    u32 n = 0;
    int i;
    if (!started || !nhc)
        return ksnprintf(b, room, "usb %s\n", !started ? "not-started" : "none");
    for (i = 0; i < nhc && n < room; i++)
        n += (u32)ksnprintf(b + n, room - n, "uhci %02x:%02x.%u io=%x ports=%u frame=%u\n", hc[i].pci->bus,
                            hc[i].pci->dev, hc[i].pci->fn, hc[i].io, hc[i].ports,
                            inw((u16)(hc[i].io + FRNUM)) & 0x7FF);
    for (i = 0; i < MAX_DEV && n < room; i++)
        if (dev[i].used) {
            char w[16];
            n += (u32)ksnprintf(b + n, room - n, "port=%s addr=%u speed=%s id=%04x:%04x is=%s%s reports=%u errors=%u\n",
                                loc(&dev[i], w), dev[i].addr, dev[i].low ? "low" : "full", dev[i].vendor,
                                dev[i].product, dev[i].is, dev[i].pad ? (dev[i].pad == 1 ? " pad=1" : " pad=2") : "",
                                dev[i].reports, dev[i].errors);
        }
    return (int)n;
}
