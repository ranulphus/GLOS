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
 * other device's. Its PCI function and ports then read as absent to DOS (pci_claim, vdev_hide). Until GLOS's own
 * keyboard and mouse drivers (J4), a controller the BIOS is using for its USB keyboard emulation (the register's
 * SMI enable) is left to the BIOS. At glos exit (usb_leave) each is stopped and reset, and the register put back.
 *
 * The schedule: every frame of the 1024 points at the same chain of queue heads, interrupt pipes' first (J4) and
 * the control QH last. A control transfer's TDs hang from the control QH; the "usb" thread waits for them a tick
 * at a time.
 *
 * Root ports: the thread looks every 100 ms. A device that connects: the port reset (50 ms) and enabled, its speed
 * read; then at address 0 the first 8 bytes of its device descriptor (the control endpoint's packet size), its
 * address, its whole device descriptor, its configuration with interfaces and endpoints, and SET_CONFIGURATION.
 *     GLOS-USB uhci 00:02.2 io=ff00 ports=2 legsup=0000 [reset=slow]
 *     GLOS-USB uhci 00:02.2 left reason=bios-legacy       (the BIOS's keyboard emulation is on)
 *     GLOS-USB port=1 speed=low id=081f:e401 class=00/00/00 if=03/00/00 in=81/8/10 out=- is=hid
 *     GLOS-USB port=1 gone
 *     GLOS-USB port=2 error=STEP status=XXXXXXXX           (a transfer that failed: the step, the TD's status) */
#include <string.h>
#include "glos/bootinfo.h"
#include "io.h"
#include "kcfg.h"
#include "kprintf.h"
#include "mm.h"
#include "pci.h"
#include "sched.h"
#include "timer.h"
#include "usb.h"
#include "vm.h"

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
#define MAX_DEV         8
#define NTD             40

struct hc {
    struct pci_dev *pci;
    u16 io;
    u32 legsup;                         /* as found, for usb_leave */
    u32 *frames;                        /* the frame list (1024 links) */
    u32 frames_phys;
    u8 *pool;                           /* a page: the control QH, the TDs, the setup packet and a data buffer */
    u32 pool_phys;
    struct uhci_qh *qh_ctrl;
    struct uhci_td *td;
    u8 *setup, *data;
    u8 ports, slow_reset;
    u8 port_dev[2];                     /* the device on each root port: its index + 1, 0 none */
    u8 tries[2];                        /* attempts at the device there since it connected (3 at most) */
};

static struct hc hc[MAX_HC];
static int nhc;
static struct usb_dev dev[MAX_DEV];
static u8 next_addr = 1;
static int started;

#define PHYS(h, p) ((h)->pool_phys + (u32)((const u8 *)(p) - (h)->pool))

/* ---- registers, waits */

static void wait_ms(u32 ms)
{
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
            if (!ifaces++ || (!d->iclass && c[at + 3] != 0xFF)) {
                d->iclass = c[at + 5], d->isub = c[at + 6], d->iproto = c[at + 7];
            }
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

static void fail(int port, const char *step, u32 status)
{
    kprintf("GLOS-USB port=%d error=%s status=%08x\n", port, step, status);
}

static void attach(struct hc *h, int p)
{
    struct usb_dev *d = 0;
    u8 buf[256];
    u32 got = 0, status = 0, total;
    u16 v;
    int i, low;
    u8 s_addr[8] = { 0x00, 5, 0, 0, 0, 0, 0, 0 }, s_conf[8] = { 0x00, 9, 0, 0, 0, 0, 0, 0 };

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
        fail(p + 1, "enable", port_read(h, p));
        return;
    }
    port_write(h, p, (u16)((port_read(h, p) & PS_RW) | PS_CSC | PS_PEC));
    low = (port_read(h, p) & PS_LSDA) != 0;
    wait_ms(10);                                /* (the recovery a reset is owed) */
    for (i = 0; i < MAX_DEV && dev[i].used; i++) ;
    if (i == MAX_DEV) {
        fail(p + 1, "devices", MAX_DEV);
        return;
    }
    d = &dev[i];
    memset(d, 0, sizeof *d);
    d->port = (u8)(p + 1);
    d->low = (u8)low;
    if (get_desc(h, 0, 8, low, 1, 0, 8, buf, &got, &status) != 0 || got < 8) {
        fail(p + 1, "device8", status);
        return;
    }
    d->maxp0 = buf[7] ? buf[7] : 8;
    d->addr = next_addr;
    next_addr = (u8)(next_addr % 127 + 1);
    s_addr[2] = d->addr;
    if (control(h, 0, d->maxp0, low, s_addr, 0, 0, &status) != 0) {
        fail(p + 1, "address", status);
        return;
    }
    wait_ms(2);                                 /* (SET_ADDRESS's recovery: 2 ms) */
    if (get_desc(h, d->addr, d->maxp0, low, 1, 0, 18, buf, &got, &status) != 0 || got < 18) {
        fail(p + 1, "device", status);
        return;
    }
    d->vendor = (u16)(buf[8] | buf[9] << 8);
    d->product = (u16)(buf[10] | buf[11] << 8);
    d->release = (u16)(buf[12] | buf[13] << 8);
    d->dclass = buf[4], d->dsub = buf[5], d->dproto = buf[6];
    if (get_desc(h, d->addr, d->maxp0, low, 2, 0, 9, buf, &got, &status) != 0 || got < 9) {
        fail(p + 1, "config9", status);
        return;
    }
    total = buf[2] | (u32)buf[3] << 8;
    if (total > sizeof buf)
        total = sizeof buf;
    if (get_desc(h, d->addr, d->maxp0, low, 2, 0, (u16)total, buf, &got, &status) != 0) {
        fail(p + 1, "config", status);
        return;
    }
    parse_config(d, buf, got);
    s_conf[2] = d->config;
    if (control(h, d->addr, d->maxp0, low, s_conf, 0, 0, &status) != 0) {
        fail(p + 1, "configure", status);
        return;
    }
    d->is = what(d);
    d->used = 1;
    h->port_dev[p] = (u8)(i + 1);
    if (d->ep_in)
        kprintf("GLOS-USB port=%d speed=%s id=%04x:%04x class=%02x/%02x/%02x if=%02x/%02x/%02x in=%02x/%u/%u%s is=%s\n",
                p + 1, low ? "low" : "full", d->vendor, d->product, d->dclass, d->dsub, d->dproto, d->iclass,
                d->isub, d->iproto, d->ep_in, d->ep_in_maxp, d->ep_in_ival, d->ep_out ? " out=yes" : "", d->is);
    else
        kprintf("GLOS-USB port=%d speed=%s id=%04x:%04x class=%02x/%02x/%02x if=%02x/%02x/%02x is=%s\n", p + 1,
                low ? "low" : "full", d->vendor, d->product, d->dclass, d->dsub, d->dproto, d->iclass, d->isub,
                d->iproto, d->is);
}

static void detach(struct hc *h, int p)
{
    if (h->port_dev[p]) {
        dev[h->port_dev[p] - 1].used = 0;
        h->port_dev[p] = 0;
        kprintf("GLOS-USB port=%d gone\n", p + 1);
    }
}

static void usb_thread(void *arg)
{
    int i, p;
    (void)arg;
    for (;;) {
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
        wait_ms(100);
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
    if (h->legsup & LEGSUP_SMIEN) {             /* (until J4: the BIOS's USB keyboard may be the PC's only one) */
        kprintf("GLOS-USB uhci %02x:%02x.%u left reason=bios-legacy legsup=%04x\n", pd->bus, pd->dev, pd->fn,
                h->legsup);
        return -1;
    }
    if (!(h->frames = dma_alloc(1, &h->frames_phys)) || !(h->pool = dma_alloc(1, &h->pool_phys)))
        return -1;
    h->qh_ctrl = (struct uhci_qh *)h->pool;
    h->td = (struct uhci_td *)(h->pool + 256);
    h->setup = h->pool + 256 + NTD * sizeof(struct uhci_td);
    h->data = h->pool + 3072;
    h->qh_ctrl->head = UHCI_LP_T;
    h->qh_ctrl->elem = UHCI_LP_T;
    for (p = 0; p < 1024; p++)
        h->frames[p] = h->pool_phys | UHCI_LP_Q;
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
        if (dev[i].used)
            n += (u32)ksnprintf(b + n, room - n, "port=%u addr=%u speed=%s id=%04x:%04x is=%s\n", dev[i].port,
                                dev[i].addr, dev[i].low ? "low" : "full", dev[i].vendor, dev[i].product, dev[i].is);
    return (int)n;
}
