/* AMD PCnet-PCI (GLOS v1, phase H tier 2, 2026-11-23; docs/v1/hardware-audit.md): the Am79C970A PCnet-PCI II
 * (1022:2000) and the cards of its family that answer to the same ID (PCnet-FAST and later), common on Pentium
 * boards and in VirtualBox's and VMware's emulated PCs; 86Box's `pcnetpci`.
 *
 * Written from AMD's Am79C970A data sheet (Rev D, March 1999; publication 19436; the copy at
 * people.freebsd.org/~wpaul/AMD/Am79C970A.pdf, sha256 a79f5992...74fcc8c9):
 *   - its I/O space in 16-bit mode (as it comes out of reset): 00h-0Fh the address PROM (the station's address,
 *     its first six bytes), 10h RDP (the data of the CSR RAP names), 12h RAP, 14h RESET (a read resets it), 16h BDP
 *     (the data of the BCR RAP names);
 *   - BCR20 (SWSTYLE) 2: 32-bit software structures (SSIZE32), the PCnet-PCI's own descriptors;
 *   - the initialisation block (28 bytes, its address in CSR1 and CSR2): the mode (0), the rings' sizes as powers
 *     of two (the high nibbles of bytes 2 and 3), the station's address (4-9), the multicast filter (12-19: all
 *     ones), the rings' addresses (20, 24);
 *   - a descriptor of 16 bytes: the buffer's address, then OWN (31), ERR (30), STP (25), ENP (24), ONES (15:12)
 *     and the buffer's length negated (11:0); a received frame's length (with its CRC) in the third word;
 *   - CSR0: INIT (1), STRT (2), STOP (4), TDMD (8: look at the transmit ring), IENA (40h), IDON (100h), TINT (200h),
 *     RINT (400h), MERR (800h), MISS (1000h), CERR (2000h), BABL (4000h), ERR (8000h); the status bits cleared by
 *     writing them back.
 * Proof: Loop A (86Box's pcnetpci); no bench has one. */
#include <string.h>

#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "nic.h"
#include "pci.h"
#include "vm.h"

#define APROM  0x00
#define RDP    0x10
#define RAP    0x12
#define RESET  0x14
#define BDP    0x16

#define C0_INIT 0x0001u
#define C0_STRT 0x0002u
#define C0_STOP 0x0004u
#define C0_TDMD 0x0008u
#define C0_IENA 0x0040u
#define C0_IDON 0x0100u
#define C0_TINT 0x0200u
#define C0_RINT 0x0400u
#define C0_MERR 0x0800u
#define C0_MISS 0x1000u
#define C0_STATUS 0x7F00u               /* (IDON to BABL: cleared by writing them back) */

#define OWN    0x80000000u
#define D_ERR  0x40000000u
#define D_STP  0x02000000u
#define D_ENP  0x01000000u
#define ONES   0x0000F000u

#define RX_LOG 4                        /* 16 receive descriptors */
#define TX_LOG 3                        /* 8 transmit */
#define RX_N   (1u << RX_LOG)
#define TX_N   (1u << TX_LOG)
#define BUF    2048u
#define RX_LEN 1536u

struct desc {
    volatile u32 addr, flags, misc, user;
};

struct init {                           /* SWSTYLE 2's, 28 bytes */
    u16 mode;
    u8 rlen, tlen;
    u8 padr[6];
    u16 res;
    u8 ladrf[8];
    u32 rdra, tdra;
} __attribute__((packed));

static struct nic pc;
static struct desc *rxd, *txd;
static struct init *ib;
static u8 *rxb, *txb;
static u32 ring_phys, rxb_phys, txb_phys;
static u32 rx_at, tx_head, tx_tail;

static u16 csr_rd(u32 n) { outw((u16)(pc.base + RAP), (u16)n); return inw((u16)(pc.base + RDP)); }
static void csr_wr(u32 n, u16 v) { outw((u16)(pc.base + RAP), (u16)n); outw((u16)(pc.base + RDP), v); }
static void bcr_wr(u32 n, u16 v) { outw((u16)(pc.base + RAP), (u16)n); outw((u16)(pc.base + BDP), v); }

static void pause(u32 n)
{
    while (n--)
        inb(0x80);
}

static void rings(void)
{
    u32 i;
    for (i = 0; i < RX_N; i++) {
        rxd[i].addr = rxb_phys + i * BUF;
        rxd[i].misc = 0, rxd[i].user = 0;
        rxd[i].flags = OWN | ONES | ((0u - RX_LEN) & 0x0FFF);
    }
    for (i = 0; i < TX_N; i++) {
        txd[i].addr = txb_phys + i * BUF;
        txd[i].flags = 0, txd[i].misc = 0, txd[i].user = 0;
    }
    rx_at = tx_head = tx_tail = 0;
}

/* Reset, the 32-bit structures, the rings and the block read, started: 0, or -1 if it never finished. */
static int start(void)
{
    u32 n, ib_phys = ring_phys + (RX_N + TX_N) * (u32)sizeof(struct desc);
    inw((u16)(pc.base + RESET));                /* (a read resets it: 16-bit I/O again) */
    pause(10);
    csr_wr(0, C0_STOP);
    bcr_wr(20, 2);                              /* SWSTYLE 2: 32-bit structures */
    rings();
    memset(ib, 0, sizeof *ib);
    ib->rlen = RX_LOG << 4, ib->tlen = TX_LOG << 4;
    memcpy(ib->padr, pc.mac, 6);
    memset(ib->ladrf, 0xFF, sizeof ib->ladrf);  /* (every multicast: mDNS's group among them) */
    ib->rdra = ring_phys;
    ib->tdra = ring_phys + RX_N * (u32)sizeof(struct desc);
    csr_wr(1, (u16)ib_phys);
    csr_wr(2, (u16)(ib_phys >> 16));
    csr_wr(3, 0);                               /* (no interrupt masked) */
    csr_wr(0, C0_INIT);
    for (n = 0; n < 20000 && !(csr_rd(0) & C0_IDON); n++)
        pause(10);
    if (!(csr_rd(0) & C0_IDON))
        return -1;
    csr_wr(0, C0_IDON | C0_STRT | C0_IENA);
    return 0;
}

static void pcnet_stop(struct nic *c)
{
    (void)c;
    csr_wr(0, C0_STOP);
    inw((u16)(pc.base + RESET));
}

static int pcnet_send(struct nic *c, const void *frame, u32 len)
{
    u32 n = len < 60 ? 60 : len, i;
    u8 *b = txb + tx_head * BUF;
    (void)c;
    if (len > ETH_MAX || (txd[tx_head].flags & OWN) || (tx_head + 1) % TX_N == tx_tail)
        return -1;                              /* (the ring is full) */
    memcpy(b, frame, len);
    for (i = len; i < n; i++)                   /* (a short frame padded here) */
        b[i] = 0;
    txd[tx_head].misc = 0;
    txd[tx_head].flags = OWN | D_STP | D_ENP | ONES | ((0u - n) & 0x0FFF);
    tx_head = (tx_head + 1) % TX_N;
    csr_wr(0, C0_TDMD | C0_IENA);
    pc.tx_frames++;
    return 0;
}

static void pcnet_service(struct nic *c)
{
    u16 c0 = csr_rd(0);
    u32 guard;
    (void)c;
    if (!(c0 & C0_STATUS))
        return;
    csr_wr(0, (u16)((c0 & C0_STATUS) | C0_IENA));       /* (its status cleared, interrupts left on) */
    if (c0 & C0_MISS)
        pc.overruns++;
    for (guard = 0; guard < RX_N && !(rxd[rx_at].flags & OWN); guard++) {
        u32 f = rxd[rx_at].flags, len = rxd[rx_at].misc & 0x0FFF;
        if (!(f & D_ERR) && (f & (D_STP | D_ENP)) == (D_STP | D_ENP) && len >= 4 + 14 && len <= ETH_MAX + 4) {
            pc.rx_frames++;
            net_rx(&pc, rxb + rx_at * BUF, len - 4);    /* without its CRC */
        } else {
            pc.rx_errors++;
        }
        rxd[rx_at].misc = 0;
        rxd[rx_at].flags = OWN | ONES | ((0u - RX_LEN) & 0x0FFF);
        rx_at = (rx_at + 1) % RX_N;
    }
    while (tx_tail != tx_head && !(txd[tx_tail].flags & OWN)) {
        if (txd[tx_tail].flags & D_ERR)
            pc.rx_errors++;                     /* (the card's errors are one count: glos net) */
        tx_tail = (tx_tail + 1) % TX_N;
    }
    if (c0 & C0_MERR)
        kprintf("GLOS-WARN pcnet bus-error csr0=%04x\n", c0);
}

int pcnet_probe(void)
{
    struct pci_dev *d = pci_find(0x1022, 0x2000, 0);
    u32 i;
    if (!d)
        return -1;
    if (!(d->bar[0] & 1) || !(d->bar[0] & ~3u)) {
        kprintf("GLOS-NET refuse card=pcnet reason=no-io\n");
        return -1;
    }
    if (nic_pci(d, "pcnet") != 0)
        return -1;
    if (!rxd) {
        u8 *ring = dma_alloc(1, &ring_phys);    /* both rings and the initialisation block in one page */
        if (ring) {
            rxd = (struct desc *)ring;
            txd = (struct desc *)(ring + RX_N * sizeof(struct desc));
            ib = (struct init *)(ring + (RX_N + TX_N) * sizeof(struct desc));
        }
        rxb = dma_alloc(RX_N * BUF / 4096, &rxb_phys);
        txb = dma_alloc(TX_N * BUF / 4096, &txb_phys);
    }
    if (!rxd || !rxb || !txb) {
        kprintf("GLOS-NET refuse card=pcnet reason=no-memory\n");
        return -1;
    }
    nic_pci_on(d);
    pc.name = "pcnet";
    pc.base = (u16)(d->bar[0] & ~3u);
    pc.irq = d->irq;
    inw((u16)(pc.base + RESET));
    for (i = 0; i < 6; i++)
        pc.mac[i] = inb((u16)(pc.base + APROM + i));
    if (start() != 0) {
        kprintf("GLOS-NET refuse card=pcnet base=%x reason=no-response\n", pc.base);
        return -1;
    }
    pc.send = pcnet_send;
    pc.service = pcnet_service;
    pc.stop = pcnet_stop;
    vdev_hide(pc.base, 0x20);
    nic_claimed(&pc, d);
    return 0;
}
