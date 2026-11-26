/* EHCI handed over (GLOS v1, phase H tier 1, 2026-11-23; docs/v1/hardware-audit.md). A PC of USB 2.0's time has an
 * EHCI controller beside its UHCI (or OHCI) companions, and its BIOS has usually routed every port to EHCI (its
 * CONFIGFLAG set) and drives the keyboard there by SMI. GLOS drives USB 1.1 by UHCI alone (uhci.c), so before it
 * takes the companions each EHCI is:
 *   - taken from the BIOS by its legacy-support capability (USBLEGSUP, in PCI configuration space at EECP):
 *     the OS's semaphore set, the BIOS's awaited (a second; then cleared by force), its SMIs turned off;
 *   - stopped (Run/Stop cleared, halted awaited) and its interrupts turned off;
 *   - told that no software configured it (CONFIGFLAG 0): every port goes back to its companion controller, where
 *     the devices plugged in are found again as UHCI's own. USB 2.0's speed is not used (out of v1's scope).
 * At glos exit the BIOS gets it back as it was: CONFIGFLAG and the SMI enables put back, the OS's semaphore given
 * up (a BIOS that wants its keyboard again takes it then, as it took it at its start).
 *
 * From the EHCI specification (1.0, sections 2.1.7, 2.3, 5.1). Log:
 *   GLOS-USB ehci 00:1d.7 handed bios=1|0 forced=0|1 ports=N   (taken: whether the BIOS had it, by force)
 *   GLOS-USB ehci 00:1d.7 refused no-memory|io-bar             (left alone)
 *   GLOS-USB ehci 00:1d.7 given back                           (glos exit)
 *
 * (U4, docs/v1/usb-storage.md; [usb] ehci = on, off by default until a real PC has shown it) Driven: after its
 * companions are taken, reset, its asynchronous schedule made (a head QH, and a QH for each endpoint used, linked in
 * after it and never changed; their toggles from the qTDs, so one QH serves an endpoint both ways), CONFIGFLAG set
 * (every port its now) and the ports powered. A device on a port is kept only if it is a high-speed disk: a
 * low-speed one (its line's K state) and a full-speed one (not enabled after the reset's chirp) are given to the
 * companion (PORT_OWNER) at once, and a high-speed one that isn't mass storage (a USB 2.0 hub too: no split
 * transactions here) once its configuration is read; the companion finds each again at full speed, as before.
 * No periodic schedule, no interrupts: transfers are polled, one at a time, as on UHCI and OHCI.
 *   GLOS-USB ehci 00:1d.7 driven ports=N (high-speed disks; the rest to the companions)
 *   GLOS-USB ehci port=N speed=low|full: to its companion */
#include <string.h>
#include "devlist.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "pci.h"
#include "sched.h"
#include "kcfg.h"
#include "msc.h"
#include "timer.h"
#include "usb.h"
#include "usbhc.h"

#define MAX_EHCI 4

#define CAP_LENGTH   0x00               /* (byte) where the operational registers begin */
#define CAP_HCSPARAMS 0x04
#define CAP_HCCPARAMS 0x08
#define OP_USBCMD    0x00
#define OP_USBSTS    0x04
#define OP_USBINTR   0x08
#define OP_CONFIGFLAG 0x40
#define CMD_RUN      0x01
#define STS_HALTED   0x1000

#define LEGSUP_BIOS  (1u << 16)         /* USBLEGSUP: the BIOS owns it */
#define LEGSUP_OS    (1u << 24)         /* ... the OS asks for it */

static struct ehci {
    struct pci_dev *pci;
    volatile u8 *cap;
    volatile u32 *op;
    u32 eecp;                           /* the legacy-support capability's place (0: none) */
    u32 legctl, configflag, intr;       /* as the BIOS left them: given back at exit */
} eh[MAX_EHCI];
static int neh;

static void pause_ms(u32 ms, int can_sleep)
{
    if (!can_sleep) {                   /* (glos exit: the tick has stopped; a port read takes about a microsecond) */
        u32 n = ms * 1000;
        while (n--)
            inb(0x80);
        return;
    }
    thread_sleep_until(timer_ticks() + (ms * 1024 + 999) / 1000 + 1);
}

/* USBLEGSUP's place among the controller's extended capabilities (in PCI configuration space): 0 if none. */
static u32 legsup_at(const struct pci_dev *pd, u32 hccparams)
{
    u32 at = (hccparams >> 8) & 0xFF, n;
    for (n = 0; at >= 0x40 && n < 16; n++) {
        u32 v = pci_read32(pd->bus, pd->dev, pd->fn, at);
        if ((v & 0xFF) == 1)
            return at;
        at = (v >> 8) & 0xFF;
    }
    return 0;
}

static void take(struct pci_dev *pd)
{
    struct ehci *e = &eh[neh];
    char w[8];
    u32 bar = pd->bar[0], hcc, v, cmd, i;
    volatile u8 *cap;
    int bios = 0, forced = 0;
    if (bar & 1) {                      /* (EHCI's registers are memory, never I/O) */
        kprintf("GLOS-USB ehci %s refused io-bar\n", pci_where(pd, w));
        dev_note(pci_where(pd, w), "ehci", DEVS_REFUSED, "io-bar", 0);
        return;
    }
    bar &= ~0xFu;
    cmd = pci_read32(pd->bus, pd->dev, pd->fn, 0x04);
    pci_write32(pd->bus, pd->dev, pd->fn, 0x04, (cmd & 0xFFFF) | 0x02);       /* (its memory decoded) */
    if (!bar || !(cap = mm_dev_map(bar & ~0xFFFu, 0x1000))) {
        kprintf("GLOS-USB ehci %s refused no-memory\n", pci_where(pd, w));
        dev_note(pci_where(pd, w), "ehci", DEVS_REFUSED, "no-memory", 0);
        return;
    }
    cap += bar & 0xFFFu;
    hcc = *(volatile u32 *)(cap + CAP_HCCPARAMS);
    memset(e, 0, sizeof *e);
    e->pci = pd;
    e->cap = cap;
    e->op = (volatile u32 *)(cap + cap[CAP_LENGTH]);
    if ((e->eecp = legsup_at(pd, hcc)) != 0) {          /* the BIOS's: asked for, awaited, then taken */
        v = pci_read32(pd->bus, pd->dev, pd->fn, e->eecp);
        bios = (v & LEGSUP_BIOS) != 0;
        pci_write32(pd->bus, pd->dev, pd->fn, e->eecp, v | LEGSUP_OS);         /* (asked; the BIOS lets go) */
        if (bios) {
            for (i = 0; i < 100 && (pci_read32(pd->bus, pd->dev, pd->fn, e->eecp) & LEGSUP_BIOS); i++)
                pause_ms(10, 1);
            if (pci_read32(pd->bus, pd->dev, pd->fn, e->eecp) & LEGSUP_BIOS) {
                forced = 1;             /* (a BIOS that never lets go: its bit cleared, as Linux and the BSDs do) */
                v = pci_read32(pd->bus, pd->dev, pd->fn, e->eecp);
                pci_write32(pd->bus, pd->dev, pd->fn, e->eecp, (v & ~LEGSUP_BIOS) | LEGSUP_OS);
            }
        }
        e->legctl = pci_read32(pd->bus, pd->dev, pd->fn, e->eecp + 4);
        pci_write32(pd->bus, pd->dev, pd->fn, e->eecp + 4, 0xE0000000u);     /* (its SMIs off; their causes cleared) */
    }
    e->configflag = e->op[OP_CONFIGFLAG / 4];
    e->intr = e->op[OP_USBINTR / 4];
    e->op[OP_USBINTR / 4] = 0;
    e->op[OP_USBCMD / 4] &= ~CMD_RUN;           /* stopped, ... */
    for (i = 0; i < 20 && !(e->op[OP_USBSTS / 4] & STS_HALTED); i++)
        pause_ms(1, 1);
    e->op[OP_CONFIGFLAG / 4] = 0;               /* ... and every port its companion's */
    e->op[OP_USBSTS / 4] = e->op[OP_USBSTS / 4];         /* (what it had to say, cleared) */
    neh++;
    kprintf("GLOS-USB ehci %s handed bios=%d forced=%d ports=%u\n", pci_where(pd, w), bios, forced,
            *(volatile u32 *)(cap + CAP_HCSPARAMS) & 0xF);
    dev_note(pci_where(pd, w), "ehci", DEVS_ON, "ports-to-companions", 0);
    pause_ms(20, 1);                            /* (the ports settle on the companions before UHCI looks at them) */
}

void ehci_take_all(void)
{
    struct pci_dev *pd;
    int i;
    for (i = 0; (pd = pci_at(i)) != 0 && neh < MAX_EHCI; i++)
        if (pd->class == 0x0C && pd->subclass == 0x03 && pd->progif == 0x20 && !dev_pci_off(pd, "ehci"))
            take(pd);
}

void ehci_leave(void)
{
    int i;
    char w[8];
    for (i = 0; i < neh; i++) {
        struct ehci *e = &eh[i];
        struct pci_dev *pd = e->pci;
        e->op[OP_CONFIGFLAG / 4] = e->configflag;       /* the ports as the BIOS had them */
        pause_ms(5, 0);
        e->op[OP_USBINTR / 4] = e->intr;
        if (e->eecp) {
            u32 v = pci_read32(pd->bus, pd->dev, pd->fn, e->eecp);
            pci_write32(pd->bus, pd->dev, pd->fn, e->eecp + 4, e->legctl & 0x0000E03Fu);    /* (its SMI enables) */
            pci_write32(pd->bus, pd->dev, pd->fn, e->eecp, v & ~LEGSUP_OS);
        }
        kprintf("GLOS-USB ehci %s given back\n", pci_where(pd, w));
    }
}

/* ---- (U4) driven */

#define OP_CTRLDSSEG 0x10
#define OP_ASYNC     0x18
#define OP_PORTSC(p) (0x44 + 4 * (p))
#define CMD_HCRESET  0x02
#define CMD_ASE      0x20
#define CMD_ITC8     (8u << 16)
#define STS_ASS      (1u << 15)
#define PS_CCS       0x0001u
#define PS_CSC       0x0002u
#define PS_PE        0x0004u
#define PS_PEC       0x0008u
#define PS_OCC       0x0020u
#define PS_PR        0x0100u
#define PS_LS        0x0C00u
#define PS_LS_K      0x0400u            /* (the line's K state at rest: a low-speed device) */
#define PS_PP        0x1000u
#define PS_PO        0x2000u
#define PS_W1C       (PS_CSC | PS_PEC | PS_OCC)

#define NQH 16                          /* the head and fifteen endpoints' */
#define NQTD 8

struct ehd {
    struct ehci *e;
    u8 *pool;
    u32 pool_phys;
    struct ehci_qh *qh;                 /* [0] the head */
    u32 key[NQH];                       /* an endpoint's QH: its address, endpoint and max packet, + 1 (0: free) */
    struct ehci_qtd *qtd;
    u8 *setup, *data;
};
static struct ehd ehd[MAX_EHCI];

#define EP(x, p) ((x)->pool_phys + (u32)((const u8 *)(p) - (x)->pool))

static void wmb(void) { __asm__ __volatile__("" ::: "memory"); }

/* The asynchronous schedule on or off, and the controller seen to follow (a few milliseconds at most). */
static void async_on(struct ehd *x, int on)
{
    volatile u32 *op = x->e->op;
    u32 i;
    if (on)
        op[OP_USBCMD / 4] |= CMD_ASE;
    else
        op[OP_USBCMD / 4] &= ~CMD_ASE;
    for (i = 0; i < 100 && ((op[OP_USBSTS / 4] & STS_ASS) != 0) != (on != 0); i++)
        usb_wait_ms(1);
}

/* An endpoint's QH: found, or made and linked in after the head (the list stays whole for a controller on it). All
   fifteen taken (devices come and go): the schedule stopped, every endpoint's let go, and begun again. */
static struct ehci_qh *qh_for(struct ehd *x, u32 addr, u32 ep, u32 maxp)
{
    u32 key = (addr | ep << 8 | maxp << 12) + 1, i, at = 0;
    struct ehci_qh *q;
    for (i = 1; i < NQH; i++) {
        if (x->key[i] == key)
            return &x->qh[i];
        if (!x->key[i] && !at)
            at = i;
    }
    if (!at) {
        async_on(x, 0);
        x->qh[0].link = EP(x, &x->qh[0]) | EHCI_LINK_QH;
        memset(x->key, 0, sizeof x->key);
        async_on(x, 1);
        at = 1;
    }
    q = &x->qh[at];
    memset((void *)q, 0, sizeof *q);
    q->ep1 = ehci_qh_ep1(addr, ep, maxp, 0);
    q->ep2 = ehci_qh_ep2();
    q->next = q->alt = EHCI_T;
    q->link = x->qh[0].link;
    wmb();
    x->qh[0].link = EP(x, q) | EHCI_LINK_QH;
    x->key[at] = key;
    return q;
}

/* qTDs t[0..n-1] given to q and waited for: 0 done (stop_short: an IN one that came in short ended it), MSC_STALL
   (halted, no other error: the endpoint's stall), -1 an error or no end in time (the schedule stopped meanwhile and
   the QH emptied). */
static int run(struct ehd *x, struct ehci_qh *q, struct ehci_qtd *t, u32 n, u32 limit, int stop_short)
{
    u32 t0 = timer_ticks(), i;
    q->alt = EHCI_T;
    q->token = 0;                       /* (a halt of the last transfer's cleared) */
    wmb();
    q->next = EP(x, t);                 /* (the controller takes the first qTD at its next visit) */
    for (;;) {
        int all = 1;
        for (i = 0; i < n; i++) {
            u32 tok = t[i].token;
            if (tok & EHCI_TOK_HALTED)
                return (tok & EHCI_TOK_ERRORS & ~EHCI_TOK_HALTED) ? -1 : MSC_STALL;
            if (tok & EHCI_TOK_ACTIVE) {
                all = 0;
                break;
            }
            if (stop_short && EHCI_TOK_LEFT(tok))
                return 0;
        }
        if (all)
            return 0;
        if (timer_ticks() - t0 > limit) {
            async_on(x, 0);
            q->next = q->alt = EHCI_T;
            q->token = 0;
            for (i = 0; i < n; i++)
                t[i].token = 0;
            async_on(x, 1);
            return -1;
        }
        usb_nap();
    }
}

static int ehci_control(struct hc *h, u32 addr, u32 maxp, int low, const u8 setup[8], void *data, u32 *got,
                        u32 *status)
{
    struct ehd *x = h->priv;
    u32 len = setup[6] | (u32)setup[7] << 8, n, moved = 0;
    int in = setup[0] & 0x80, r;
    (void)low, (void)maxp;              /* (high speed: endpoint 0's packets are 64 bytes, always) */
    if (len > 1024)
        return -1;
    memcpy(x->setup, setup, 8);
    if (!in && len)
        memcpy(x->data, data, len);
    if (!(n = ehci_control_qtds(x->qtd, NQTD, EP(x, x->qtd), setup, EP(x, x->setup), EP(x, x->data))))
        return -1;
    r = run(x, qh_for(x, addr, 0, 64), x->qtd, n, 512, 0);
    if (r != 0) {
        *status = (u32)(r == MSC_STALL ? 0x40 : 0x80000000u) | (x->qtd[n - 1].token & 0xFF);
        return -1;
    }
    if (len)
        moved = ehci_qtd_moved(&x->qtd[1], len);
    if (in && data)
        memcpy(data, x->data, moved < len ? moved : len);
    if (got)
        *got = moved;
    return 0;
}

static void ehci_intr_arm(struct hc *h, struct usb_dev *d) { (void)h, (void)d; }    /* (no pipes: given away) */
static int ehci_intr_done(struct hc *h, struct usb_dev *d, const u8 **r, u32 *len, u32 *status)
{
    (void)h, (void)d, (void)r, (void)len, (void)status;
    return 0;
}
static void ehci_intr_off(struct hc *h, struct usb_dev *d) { (void)h, (void)d; }
static int ehci_intr_out(struct hc *h, struct usb_dev *d, const u8 *data, u32 len, u32 *status)
{
    (void)h, (void)d, (void)data, (void)len, (void)status;
    return -1;
}

static int ehci_bulk_ready(struct hc *h) { return h->bulk_buf ? 0 : -1; }

static int ehci_bulk(struct hc *h, struct usb_dev *d, int in, u32 n, u32 *got)
{
    struct ehd *x = h->priv;
    u32 ep = (u32)(in ? d->ep_bin : d->ep_bout) & 0xF, maxp = in ? d->ep_bin_maxp : d->ep_bout_maxp, k, i, moved = 0;
    u8 *tog = in ? &d->tog_bin : &d->tog_bout;
    int r;
    if (!maxp || !(k = ehci_bulk_qtds(x->qtd, NQTD, EP(x, x->qtd), in, EP(x, h->bulk_buf), n, maxp, *tog))) {
        *got = 0;
        return -1;
    }
    r = run(x, qh_for(x, d->addr, ep, maxp), x->qtd, k, 5 * 1024, in);
    for (i = 0; i < k && !(x->qtd[i].token & EHCI_TOK_ACTIVE); i++) {
        u32 asked = n - i * 16384u < 16384u ? n - i * 16384u : 16384u, m = ehci_qtd_moved(&x->qtd[i], asked);
        moved += m;
        if (m < asked)
            break;
    }
    *got = moved;
    if (r == 0)
        *tog = (u8)ehci_toggle_after(*tog, moved, n, maxp);
    return r;
}

static u32 portsc(struct ehd *x, int p) { return x->e->op[OP_PORTSC(p) / 4]; }

/* A port's register written with bits set and cleared, its write-1-to-clear changes left alone. */
static void portsc_set(struct ehd *x, int p, u32 set, u32 clear)
{
    x->e->op[OP_PORTSC(p) / 4] = ((portsc(x, p) & ~PS_W1C) | set) & ~clear;
}

static int ehci_port_changed(struct hc *h, int p)
{
    struct ehd *x = h->priv;
    u32 v = portsc(x, p);
    if (!(v & PS_CSC))
        return 0;
    x->e->op[OP_PORTSC(p) / 4] = (v & ~PS_W1C) | PS_CSC;
    return 1;
}

static int ehci_port_connected(struct hc *h, int p)
{
    struct ehd *x = h->priv;
    return (portsc(x, p) & (PS_CCS | PS_PO)) == PS_CCS;
}

static void ehci_give_away(struct hc *h, int p)
{
    struct ehd *x = h->priv;
    portsc_set(x, p, PS_PO, 0);
}

static int ehci_port_reset(struct hc *h, int p, int *low)
{
    struct ehd *x = h->priv;
    u32 v = portsc(x, p), i;
    if (!(v & PS_CCS) || (v & PS_PO))
        return -1;
    if ((v & PS_LS) == PS_LS_K) {               /* low speed: its companion's at once */
        kprintf("GLOS-USB ehci port=%d speed=low: to its companion\n", p + 1);
        ehci_give_away(h, p);
        return -1;
    }
    portsc_set(x, p, PS_PR, PS_PE);             /* the reset, in which a high-speed device chirps: 50 ms */
    usb_wait_ms(50);
    portsc_set(x, p, 0, PS_PR);
    for (i = 0; i < 20 && (portsc(x, p) & PS_PR); i++)
        usb_wait_ms(1);
    usb_wait_ms(10);                            /* (the reset's recovery, USB 2.0's TRSTRCY) */
    v = portsc(x, p);
    if (!(v & PS_CCS))
        return -1;
    if (!(v & PS_PE)) {                         /* not enabled: a full-speed device, its companion's */
        kprintf("GLOS-USB ehci port=%d speed=full: to its companion\n", p + 1);
        ehci_give_away(h, p);
        return -1;
    }
    *low = 0;
    return 0;
}

static int ehci_keeps(struct hc *h, const struct usb_dev *d)
{
    (void)h;
    return d->iclass == 0x08 && d->dclass != 0x09;
}

static void ehci_drive_leave(struct hc *h)
{
    struct ehd *x = h->priv;
    volatile u32 *op = x->e->op;
    u32 i;
    op[OP_USBCMD / 4] &= ~(CMD_ASE | CMD_RUN);  /* (stopped; ehci_leave then gives the ports and SMIs back) */
    for (i = 0; i < 20000 && !(op[OP_USBSTS / 4] & STS_HALTED); i++)
        inb(0x80);
}

static u32 ehci_stat(struct hc *h, char *b, u32 room)
{
    return (u32)ksnprintf(b, room, "ehci %02x:%02x.%u ports=%u driven (high-speed disks)\n", h->pci->bus, h->pci->dev,
                          h->pci->fn, h->ports);
}

static u32 ehci_detail(struct hc *h, char *b, u32 room)
{
    return (u32)ksnprintf(b, room, "%u ports, USB 2.0's speed for disks (the rest at full speed on the companions)\n",
                          h->ports);
}

static const struct hcd_ops ehci_ops = {
    "ehci", ehci_control, ehci_intr_arm, ehci_intr_done, ehci_intr_off, ehci_intr_out, ehci_bulk_ready, ehci_bulk,
    ehci_port_changed, ehci_port_connected, ehci_port_reset, ehci_drive_leave, ehci_stat, ehci_detail,
    ehci_keeps, ehci_give_away, 1,
};

int ehci_drive(struct hc *h, int i)
{
    struct ehci *e = &eh[i];
    struct ehd *x = &ehd[i];
    volatile u32 *op;
    u32 hcs, ports, k;
    char v[8], w[8];
    if (i >= neh)
        return -1;
    v[0] = 0;
    kcfg_get("usb", "ehci", v, sizeof v);
    if (strcmp(v, "on"))
        return -1;
    memset(x, 0, sizeof *x);
    x->e = e;
    if (!(x->pool = dma_alloc(5, &x->pool_phys))) {
        kprintf("GLOS-USB ehci %s not driven: no memory\n", pci_where(e->pci, w));
        return -1;
    }
    x->qh = (struct ehci_qh *)x->pool;
    x->qtd = (struct ehci_qtd *)(x->pool + NQH * sizeof(struct ehci_qh));
    x->setup = x->pool + 1536;
    x->data = x->pool + 2048;
    op = e->op;
    op[OP_USBCMD / 4] &= ~CMD_RUN;              /* (stopped already by the hand-over: reset now) */
    for (k = 0; k < 20 && !(op[OP_USBSTS / 4] & STS_HALTED); k++)
        usb_wait_ms(1);
    op[OP_USBCMD / 4] = CMD_HCRESET;
    for (k = 0; k < 250 && (op[OP_USBCMD / 4] & CMD_HCRESET); k++)
        usb_wait_ms(1);
    if (*(volatile u32 *)(e->cap + CAP_HCCPARAMS) & 1)
        op[OP_CTRLDSSEG / 4] = 0;               /* (64-bit addressing: our structures are below 4 GB) */
    op[OP_USBINTR / 4] = 0;
    x->qh[0].link = EP(x, &x->qh[0]) | EHCI_LINK_QH;    /* the head: its own next, never to be run */
    x->qh[0].ep1 = ehci_qh_ep1(0, 0, 64, 1);
    x->qh[0].ep2 = ehci_qh_ep2();
    x->qh[0].next = x->qh[0].alt = EHCI_T;
    x->qh[0].token = EHCI_TOK_HALTED;
    wmb();
    op[OP_ASYNC / 4] = EP(x, &x->qh[0]);
    op[OP_USBCMD / 4] = CMD_ITC8 | CMD_ASE | CMD_RUN;
    for (k = 0; k < 20 && (op[OP_USBSTS / 4] & STS_HALTED); k++)
        usb_wait_ms(1);
    op[OP_CONFIGFLAG / 4] = 1;                  /* every port EHCI's: its devices come and go from the companions */
    usb_wait_ms(5);
    hcs = *(volatile u32 *)(e->cap + CAP_HCSPARAMS);
    ports = hcs & 0xF;
    if (ports > MAX_PORTS)
        ports = MAX_PORTS;
    if (hcs & 0x10)                             /* (PPC: the ports' power is software's) */
        for (k = 0; k < ports; k++)
            portsc_set(x, (int)k, PS_PP, 0);
    usb_wait_ms(20);
    memset(h, 0, sizeof *h);
    h->ops = &ehci_ops;
    h->priv = x;
    h->pci = e->pci;
    h->ports = (u8)ports;
    h->bulk_buf = x->pool + 4096;
    kprintf("GLOS-USB ehci %s driven ports=%u (high-speed disks; the rest to the companions)\n", pci_where(e->pci, w),
            ports);
    dev_note(pci_where(e->pci, w), "ehci", DEVS_ON, "driven", 0);
    return 0;
}
