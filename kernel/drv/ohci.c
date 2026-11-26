/* OHCI host controllers (docs/v1/usb-storage.md, U3): ALi's, SiS's, NEC's, OPTi's and their add-in cards' USB 1.1,
 * driven through the calls uhci.c's devices, hubs, pads and disks use (usbhc.h's hcd_ops).
 *
 * Sources (clean room): the OpenHCI specification rev 1.0a (the registers, §7; the HCCA, the EDs and the general
 * TDs, §4; taking the controller from the BIOS's SMM by OwnershipChangeRequest, §5.1.1.3; the root hub's ports,
 * §7.4), the USB 1.1 specification (port reset, the stages of a control transfer).
 *
 * The take-over: the BIOS's ownership asked for (HcControl's IR: OCR set, then IR seen clear) or a running
 * controller stopped; then reset (HCR), its frame interval as found, the HCCA, the control and bulk lists' heads
 * and every list enabled, operational. No interrupt is ever enabled: as uhci.c, GLOS polls what it queued (a TD
 * whose condition code is no longer "not accessed"), so the done queue is never read and no TD asks to be
 * written there (DI 7). The ports powered (each, or all), their power's settle time waited.
 *
 * The memory (one block, five pages): the HCCA at 0 (its 32 interrupt-table entries all the first interrupt ED:
 * every frame passes the pipes, as uhci.c's schedule does), the EDs at 256 (control, bulk, then NPIPE pipes', in a
 * chain), the TDs at 512 (control three and a dummy; two a pipe, its TD and dummy; the bulk TD and dummy), the setup
 * packet at 1536, a control transfer's data at 2048, the pipes' 64 bytes each at 3072; the bulk buffer (16 KB) in
 * pages 1-4. Each ED's queue is its TDs and a dummy at its tail; the controller stops at the dummy.
 *     GLOS-USB ohci 00:02.0 mem=e0001000 ports=2 rev=10 [owner=bios]
 *
 * A bulk piece goes one TD (4 KB at most) at a time: a short packet ends a TD and the controller would go on to the
 * next one, which on an IN would then take the CSW meant for the status stage; one at a time, the piece stops there.
 * The ED's toggle carry is the endpoint's: one ED serves a stick's IN and OUT, so each transfer starts from the
 * endpoint's toggle (usb_dev's tog_bin, tog_bout) and leaves it there. */
#include <string.h>
#include "devlist.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "msc.h"
#include "timer.h"
#include "usbhc.h"

#define HcRevision          0x00
#define HcControl           0x04
#define   CTL_CBSR4         0x3u        /* control to bulk 4:1 */
#define   CTL_PLE           (1u << 2)
#define   CTL_CLE           (1u << 4)
#define   CTL_BLE           (1u << 5)
#define   CTL_HCFS          (3u << 6)
#define   HCFS_RESET        (0u << 6)
#define   HCFS_RESUME       (1u << 6)
#define   HCFS_OPER         (2u << 6)
#define   CTL_IR            (1u << 8)
#define HcCommandStatus     0x08
#define   CS_HCR            (1u << 0)
#define   CS_CLF            (1u << 1)
#define   CS_BLF            (1u << 2)
#define   CS_OCR            (1u << 3)
#define HcInterruptStatus   0x0C
#define HcInterruptDisable  0x14
#define HcHCCA              0x18
#define HcControlHeadED     0x20
#define HcControlCurrentED  0x24
#define HcBulkHeadED        0x28
#define HcBulkCurrentED     0x2C
#define HcFmInterval        0x34
#define HcFmNumber          0x3C
#define HcPeriodicStart     0x40
#define HcLSThreshold       0x44
#define HcRhDescriptorA     0x48
#define   RHA_PSM           (1u << 8)
#define   RHA_NPS           (1u << 9)
#define HcRhStatus          0x50
#define   RHS_LPSC          (1u << 16)  /* (write) global power on */
#define HcRhPortStatus(p)   (0x54 + 4 * (p))
#define   PS_CCS            (1u << 0)
#define   PS_PES            (1u << 1)
#define   PS_PRS            (1u << 4)   /* (write) its reset */
#define   PS_PPS            (1u << 8)   /* (write) its power on */
#define   PS_LSDA           (1u << 9)
#define   PS_CSC            (1u << 16)
#define   PS_PESC           (1u << 17)
#define   PS_PRSC           (1u << 20)

struct ohci {
    volatile u32 *r;
    u32 base;
    u8 *pool;
    u32 pool_phys;
    u32 *hcca;
    struct ohci_ed *ed_ctrl, *ed_bulk, *ed_int;
    struct ohci_td *td_ctrl, *td_int, *td_bulk;
    u8 *setup, *data, *int_buf;
    u32 rev, bios;                      /* bios: the BIOS had it (IR, or running) */
};
static struct ohci ohcis[MAX_HC];

#define R(o, off) ((o)->r[(off) / 4])
#define P(o, p) ((o)->pool_phys + (u32)((const u8 *)(p) - (o)->pool))

static void nap(void) { usb_nap(); }

/* A queue's TDs given to its ED (skipped meanwhile), from DATA1 if carry: the ED's queue runs to tail. */
static void queue(struct ohci_ed *ed, u32 info, u32 first, u32 tail, int carry)
{
    ed->info = info | OHCI_ED_SKIP;
    ed->tailp = tail;
    ed->headp = first | (carry ? OHCI_ED_CARRY : 0);
    ed->info = info;
}

/* The queue emptied (its head the dummy, not halted), the ED skipped. */
static void unqueue(struct ohci_ed *ed)
{
    ed->info |= OHCI_ED_SKIP;
    ed->headp = ed->tailp | (ed->headp & OHCI_ED_CARRY);
}

/* TDs t[0..n-1] done: 0 all with no error, or the first error's condition code; -1 not all done yet. */
static int done(const struct ohci_td *t, u32 n)
{
    u32 i;
    for (i = 0; i < n; i++) {
        u32 cc = OHCI_TD_CC(t[i].info);
        if (cc >= OHCI_CC_NOT_YET)
            return -1;
        if (cc != OHCI_CC_NONE)
            return (int)cc;
    }
    return 0;
}

static int ohci_control(struct hc *h, u32 addr, u32 maxp, int low, const u8 setup[8], void *data, u32 *got,
                        u32 *status)
{
    struct ohci *o = h->priv;
    u32 len = setup[6] | (u32)setup[7] << 8, n, t0 = timer_ticks(), moved = 0;
    int in = setup[0] & 0x80, r;
    if (len > 1024)
        return -1;
    memcpy(o->setup, setup, 8);
    if (!in && len)
        memcpy(o->data, data, len);
    n = ohci_control_tds(o->td_ctrl, 3, P(o, o->td_ctrl), o->setup, P(o, o->setup), P(o, o->data), P(o, &o->td_ctrl[3]));
    if (!n)
        return -1;
    queue(o->ed_ctrl, ohci_ed_info(addr, 0, 0, low, maxp ? maxp : 8), P(o, o->td_ctrl), P(o, &o->td_ctrl[3]), 0);
    R(o, HcCommandStatus) = CS_CLF;
    while ((r = done(o->td_ctrl, n)) < 0) {
        if (o->ed_ctrl->headp & OHCI_ED_HALTED)
            break;
        if (timer_ticks() - t0 > 512) {
            unqueue(o->ed_ctrl);
            *status = 0x80000000u | o->td_ctrl[n - 1].info >> 28;   /* (a timeout: bit 31) */
            return -1;
        }
        nap();
    }
    unqueue(o->ed_ctrl);
    if (r != 0) {
        *status = (u32)(r < 0 ? 0xF : r) | (o->ed_ctrl->headp & OHCI_ED_HALTED) << 8;
        return -1;
    }
    if (len)
        moved = ohci_td_moved(&o->td_ctrl[1]);
    if (in && data)
        memcpy(data, o->data, moved < len ? moved : len);
    if (got)
        *got = moved;
    return 0;
}

/* ---- interrupt pipes: an ED a pipe slot on the periodic list, its TD and dummy */

static void ohci_intr_arm(struct hc *h, struct usb_dev *d)
{
    struct ohci *o = h->priv;
    u32 k = d->pipe - 1u, maxp = d->ep_in_maxp ? d->ep_in_maxp : 8;
    struct ohci_ed *ed = &o->ed_int[k];
    struct ohci_td *t = &o->td_int[2 * k];
    int carry = (ed->headp & OHCI_ED_CARRY) != 0;
    if (maxp > 64)
        maxp = 64;
    ohci_bulk_tds(t, 1, P(o, t), 1, P(o, o->int_buf + k * 64), maxp, P(o, &t[1]));
    queue(ed, ohci_ed_info(d->addr, d->ep_in & 0xF, 2, d->low, d->ep_in_maxp ? d->ep_in_maxp : 8), P(o, t), P(o, &t[1]),
          carry);
}

static int ohci_intr_done(struct hc *h, struct usb_dev *d, const u8 **r, u32 *len, u32 *status)
{
    struct ohci *o = h->priv;
    u32 k = d->pipe - 1u;
    struct ohci_td *t = &o->td_int[2 * k];
    u32 cc = OHCI_TD_CC(t->info);
    if (cc >= OHCI_CC_NOT_YET)
        return 0;
    if (cc != OHCI_CC_NONE) {
        *status = cc;
        unqueue(&o->ed_int[k]);                 /* (the ED halted: emptied, re-armed by the caller) */
        return -1;
    }
    *r = o->int_buf + k * 64;
    *len = ohci_td_moved(t);
    return 1;                                   /* (the toggle is the ED's carry: nothing to flip) */
}

static void ohci_intr_off(struct hc *h, struct usb_dev *d) { unqueue(&((struct ohci *)h->priv)->ed_int[d->pipe - 1]); }

/* One report out on the device's interrupt OUT endpoint (the Xbox One pad's power-on), on the bulk ED (a USB 1.1
   controller doesn't care what kind of endpoint an ED's TD is for), the toggle the device's: 0, or -1. */
static int ohci_intr_out(struct hc *h, struct usb_dev *d, const u8 *data, u32 len, u32 *status)
{
    struct ohci *o = h->priv;
    struct ohci_td *t = o->td_bulk;
    u32 t0 = timer_ticks();
    int r;
    if (!len || len > 64)
        return -1;
    memcpy(o->int_buf + (d->pipe - 1) * 64, data, len);
    ohci_bulk_tds(t, 1, P(o, t), 0, P(o, o->int_buf + (d->pipe - 1) * 64), len, P(o, &t[1]));
    queue(o->ed_bulk, ohci_ed_info(d->addr, d->ep_out & 0xF, 0, d->low, d->ep_out_maxp ? d->ep_out_maxp : 8), P(o, t),
          P(o, &t[1]), d->toggle_out & 1);
    R(o, HcCommandStatus) = CS_BLF;
    while ((r = done(t, 1)) < 0 && timer_ticks() - t0 < 100)
        nap();
    unqueue(o->ed_bulk);
    if (r != 0) {
        *status = (u32)(r < 0 ? 0xF : r);
        return -1;
    }
    d->toggle_out ^= 1;
    return 0;
}

/* ---- bulk: one TD at a time (see above) */

static int ohci_bulk_ready(struct hc *h) { return h->bulk_buf ? 0 : -1; }

static int ohci_bulk(struct hc *h, struct usb_dev *d, int in, u32 n, u32 *got)
{
    struct ohci *o = h->priv;
    struct ohci_td *t = o->td_bulk;
    u32 ep = (u32)(in ? d->ep_bin : d->ep_bout) & 0xF, maxp = in ? d->ep_bin_maxp : d->ep_bout_maxp, moved = 0;
    u8 *tog = in ? &d->tog_bin : &d->tog_bout;
    int r = 0;
    while (moved < n) {
        u32 now = n - moved < 4096 ? n - moved : 4096, t0 = timer_ticks(), m;
        ohci_bulk_tds(t, 1, P(o, t), in, P(o, h->bulk_buf + moved), now, P(o, &t[1]));
        queue(o->ed_bulk, ohci_ed_info(d->addr, ep, 0, d->low, maxp), P(o, t), P(o, &t[1]), *tog & 1);
        R(o, HcCommandStatus) = CS_BLF;
        while ((r = done(t, 1)) < 0) {
            if (timer_ticks() - t0 > 5 * 1024)
                break;
            nap();
        }
        *tog = (o->ed_bulk->headp & OHCI_ED_CARRY) ? 1 : 0;     /* (where the controller left the toggle) */
        unqueue(o->ed_bulk);
        if (r != 0) {
            *got = moved;
            return r == OHCI_CC_STALL ? MSC_STALL : -1;
        }
        m = ohci_td_moved(t);
        moved += m;
        if (m < now)
            break;                              /* (a short packet: the device has no more) */
    }
    *got = moved;
    return 0;
}

/* ---- root ports */

static int ohci_port_changed(struct hc *h, int p)
{
    struct ohci *o = h->priv;
    if (!(R(o, HcRhPortStatus(p)) & PS_CSC))
        return 0;
    R(o, HcRhPortStatus(p)) = PS_CSC;
    return 1;
}

static int ohci_port_connected(struct hc *h, int p) { return (R(((struct ohci *)h->priv), HcRhPortStatus(p)) & PS_CCS) != 0; }

static int ohci_port_reset(struct hc *h, int p, int *low)
{
    struct ohci *o = h->priv;
    u32 v, i;
    R(o, HcRhPortStatus(p)) = PS_PRS;           /* the reset: the root hub times it (10 ms), and says so (PRSC) */
    for (i = 0; i < 50; i++) {
        usb_wait_ms(2);
        if (R(o, HcRhPortStatus(p)) & PS_PRSC)
            break;
    }
    v = R(o, HcRhPortStatus(p));
    R(o, HcRhPortStatus(p)) = PS_PRSC | PS_PESC;
    if (!(v & PS_CCS))
        return -1;
    if (!(v & PS_PES)) {
        kprintf("GLOS-USB port=%d error=enable status=%08x\n", p + 1, v);
        return -1;
    }
    *low = (v & PS_LSDA) != 0;
    return 0;
}

/* ---- the controller */

static void ohci_leave(struct hc *h)
{
    struct ohci *o = h->priv;
    R(o, HcInterruptDisable) = 0xC000007Fu;
    R(o, HcControl) &= ~(CTL_PLE | CTL_CLE | CTL_BLE);
    usb_wait_ms(2);
    R(o, HcCommandStatus) = CS_HCR;             /* (reset, as GLOS took it; a BIOS's SMM that had it is not given it
                                                   back: its USB keyboard after GLOS waits for the next start) */
}

static u32 ohci_stat(struct hc *h, char *b, u32 room)
{
    struct ohci *o = h->priv;
    return (u32)ksnprintf(b, room, "ohci %02x:%02x.%u mem=%x ports=%u frame=%u\n", h->pci->bus, h->pci->dev, h->pci->fn,
                          o->base, h->ports, R(o, HcFmNumber) & 0xFFFF);
}

static u32 ohci_detail(struct hc *h, char *b, u32 room)
{
    struct ohci *o = h->priv;
    return (u32)ksnprintf(b, room, "%u ports, its registers at %x\n", h->ports, o->base);
}

static const struct hcd_ops ohci_ops = {
    "ohci", ohci_control, ohci_intr_arm, ohci_intr_done, ohci_intr_off, ohci_intr_out, ohci_bulk_ready, ohci_bulk,
    ohci_port_changed, ohci_port_connected, ohci_port_reset, ohci_leave, ohci_stat, ohci_detail,
};

int ohci_take(struct hc *h, struct pci_dev *pd)
{
    struct ohci *o;
    u32 ctl, fi, cmd, i, a, ports, pwr;
    for (i = 0; i < MAX_HC && ohcis[i].r; i++) ;
    if (i == MAX_HC)
        return -2;
    o = &ohcis[i];
    memset(h, 0, sizeof *h);
    memset(o, 0, sizeof *o);
    if ((pd->bar[0] & 1) || !(pd->bar[0] & ~0xFFFu))
        return -1;
    o->base = pd->bar[0] & ~0xFFFu;
    if (!(o->r = mm_dev_map(o->base, 4096)))
        return -1;
    if (!(o->pool = dma_alloc(5, &o->pool_phys))) {
        o->r = 0;
        return -2;
    }
    h->ops = &ohci_ops, h->priv = o, h->pci = pd;
    o->hcca = (u32 *)o->pool;
    o->ed_ctrl = (struct ohci_ed *)(o->pool + 256);
    o->ed_bulk = o->ed_ctrl + 1;
    o->ed_int = o->ed_ctrl + 2;
    o->td_ctrl = (struct ohci_td *)(o->pool + 512);
    o->td_int = o->td_ctrl + 4;
    o->td_bulk = o->td_int + 2 * NPIPE;
    o->setup = o->pool + 1536;
    o->data = o->pool + 2048;
    o->int_buf = o->pool + 3072;
    h->bulk_buf = o->pool + 4096;
    o->ed_ctrl->info = OHCI_ED_SKIP;
    o->ed_ctrl->headp = o->ed_ctrl->tailp = P(o, &o->td_ctrl[3]);
    o->ed_bulk->info = OHCI_ED_SKIP;
    o->ed_bulk->headp = o->ed_bulk->tailp = P(o, &o->td_bulk[1]);
    for (i = 0; i < NPIPE; i++) {               /* the pipes' EDs in a chain, every frame's */
        o->ed_int[i].info = OHCI_ED_SKIP;
        o->ed_int[i].headp = o->ed_int[i].tailp = P(o, &o->td_int[2 * i + 1]);
        o->ed_int[i].next = i + 1 < NPIPE ? P(o, &o->ed_int[i + 1]) : 0;
    }
    for (i = 0; i < 32; i++)
        o->hcca[i] = P(o, &o->ed_int[0]);
    cmd = pci_read32(pd->bus, pd->dev, pd->fn, 0x04);
    pci_write32(pd->bus, pd->dev, pd->fn, 0x04, (cmd & 0xFFFF) | 0x0006);       /* memory space, bus master */
    o->rev = R(o, HcRevision) & 0xFF;
    ctl = R(o, HcControl);
    R(o, HcInterruptDisable) = 0xC000007Fu;
    if (ctl & CTL_IR) {                         /* the BIOS's SMM has it: asked to let go, a second at most */
        o->bios = 1;
        R(o, HcCommandStatus) = CS_OCR;
        for (i = 0; i < 100 && (R(o, HcControl) & CTL_IR); i++)
            usb_wait_ms(10);
        if (R(o, HcControl) & CTL_IR)
            kprintf("GLOS-USB ohci %02x:%02x.%u owner=bios-kept: taken all the same\n", pd->bus, pd->dev, pd->fn);
    } else if ((ctl & CTL_HCFS) != HCFS_RESET) {        /* a BIOS's driver without SMM: stopped */
        o->bios = 1;
        if ((ctl & CTL_HCFS) != HCFS_OPER) {
            R(o, HcControl) = (ctl & ~CTL_HCFS) | HCFS_RESUME;
            usb_wait_ms(20);
        }
    }
    fi = R(o, HcFmInterval) & 0x3FFF;
    if (fi < 11000 || fi > 13000)
        fi = 11999;
    R(o, HcCommandStatus) = CS_HCR;
    for (i = 0; i < 100 && (R(o, HcCommandStatus) & CS_HCR); i++)
        usb_wait_ms(1);
    /* (After a reset the controller is suspended, and wants to be operational within 2 ms: everything at once.) */
    R(o, HcFmInterval) = ((R(o, HcFmInterval) & (1u << 31)) ^ (1u << 31)) | (((fi - 210) * 6 / 7) << 16) | fi;
    R(o, HcPeriodicStart) = fi * 9 / 10;
    R(o, HcLSThreshold) = 0x628;
    R(o, HcHCCA) = o->pool_phys;
    R(o, HcControlHeadED) = P(o, o->ed_ctrl);
    R(o, HcControlCurrentED) = 0;
    R(o, HcBulkHeadED) = P(o, o->ed_bulk);
    R(o, HcBulkCurrentED) = 0;
    R(o, HcInterruptStatus) = 0xC000007Fu;
    R(o, HcControl) = HCFS_OPER | CTL_PLE | CTL_CLE | CTL_BLE | CTL_CBSR4;
    a = R(o, HcRhDescriptorA);
    ports = a & 0xFF;
    if (ports > MAX_PORTS)
        ports = MAX_PORTS;
    h->ports = (u8)ports;
    if (!(a & RHA_NPS)) {                       /* power switched: each port's, or all of them */
        if (a & RHA_PSM)
            for (i = 0; i < ports; i++)
                R(o, HcRhPortStatus(i)) = PS_PPS;
        else
            R(o, HcRhStatus) = RHS_LPSC;
    }
    pwr = (a >> 24) * 2;                        /* its power-on-to-power-good, in 2 ms units */
    usb_wait_ms(pwr < 20 ? 20 : pwr);
    pd->keeper = DEV_GLOS;
    pci_claim(pd);
    kprintf("GLOS-USB ohci %02x:%02x.%u mem=%x ports=%u rev=%x%s\n", pd->bus, pd->dev, pd->fn, o->base, ports, o->rev,
            o->bios ? " owner=bios" : "");
    return 0;
}
