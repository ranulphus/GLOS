/* VIA Rhine (VT86C100A; the VT6102 Rhine II and VT6105 Rhine III answer to the same registers for what is
 * used here): the D-Link DFE-530TX's chip, on the P3 bench (docs/v1-route.md, phase H; the owner lifted the
 * hold for this driver alone, 2026-11-13).
 *
 * The card reads and writes memory itself, through two rings of 16-byte descriptors (four words each: status,
 * control and length, the buffer's address, the next descriptor's), one for frames received and one for frames
 * to send. A descriptor is the card's while its first word has OWN; the driver gives a receive descriptor back
 * when it has taken the frame, and gives a transmit one with the frame copied into its buffer. The card is told
 * to look at the transmit ring for each frame (its own polling is off).
 *
 * Whole frames only: the receive FIFO's threshold is "the whole frame" and so is the transmit one, so that a
 * slow bus never makes an underrun. Duplex is the MAC's to be told: it is read from the PHY (what both ends
 * advertised) when the card starts and whenever the link changes.
 *
 * Written from VIA's VT86C100A and VT6102 data sheets' register descriptions. Proven on the P3 bench's VT86C100A
 * alone (2026-11-14: 100 Mbit full duplex, 4 MB each way); 86Box has no Rhine, so Loop A never runs this file. */
#include <string.h>

#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "nic.h"
#include "pci.h"
#include "vm.h"

#define PAR0   0x00                     /* the station's address, six bytes */
#define RCR    0x06                     /* receive: what is taken, the FIFO's threshold */
#define TCR    0x07                     /* transmit: loopback (off), the FIFO's threshold */
#define CR0    0x08
#define CR1    0x09
#define ISR    0x0C                     /* 16 bits; written back to acknowledge */
#define IMR    0x0E
#define MAR0   0x10                     /* the multicast hash, eight bytes */
#define RDA    0x18                     /* the receive ring's current descriptor */
#define TDA    0x1C                     /* the transmit ring's */
#define PHYADR 0x6C
#define MIICR  0x70
#define MIIAD  0x71
#define MIIDATA 0x72
#define EECSR  0x74

#define CR0_STRT 0x02
#define CR0_STOP 0x04
#define CR0_RXON 0x08
#define CR0_TXON 0x10
#define CR0_RDMD 0x40                   /* look at the receive ring again */
#define CR1_FDX  0x04
#define CR1_DPOLL 0x08                  /* no polling of the transmit ring: TDMD for each frame */
#define CR1_TDMD 0x20                   /* look at the transmit ring */
#define CR1_SFRST 0x80

#define RCR_AM   0x04                   /* multicasts (the hash is all ones: mDNS's group among them) */
#define RCR_AB   0x08                   /* broadcasts */
#define RCR_WHOLE 0xE0                  /* the FIFO hands over whole frames */
#define TCR_WHOLE 0xE0

#define INT_PRX  0x0001                 /* a frame received */
#define INT_PTX  0x0002                 /* a frame sent */
#define INT_RXE  0x0004
#define INT_TXE  0x0008
#define INT_TU   0x0010                 /* the transmit ring ran dry in a frame */
#define INT_RU   0x0020                 /* no receive descriptor was free */
#define INT_BE   0x0040                 /* a PCI bus error */
#define INT_UDFI 0x0200                 /* the transmit FIFO underran */
#define INT_OVFI 0x0400                 /* the receive FIFO overran */
#define INT_PKTRACE 0x0800
#define INT_NORBF 0x1000
#define INT_ABTI 0x2000                 /* a frame given up after 16 collisions */
#define INT_SRCI 0x4000                 /* the link changed */
#define INTS (INT_PRX | INT_PTX | INT_RXE | INT_TXE | INT_TU | INT_RU | INT_BE | INT_UDFI | INT_OVFI | INT_PKTRACE \
              | INT_NORBF | INT_ABTI | INT_SRCI)

#define OWN      0x80000000u            /* a descriptor's first word: the card's */
#define RX_OK    0x00008000u
#define RX_WHOLE 0x00000300u            /* the frame's start and its end in this one buffer */
#define TX_ERR   0x00008000u
#define TX_FRAME 0x00E08000u            /* the control word: interrupt when sent, first and last buffer, chained */

#define RX_N     16
#define TX_N     8
#define BUF      2048u                  /* a buffer; 1536 of it is told to the card */

struct desc {
    volatile u32 status, ctl, buf, next;
};

static struct nic rh;
static struct desc *rxd, *txd;
static u8 *rxb, *txb;
static u32 rxd_phys, txd_phys, rxb_phys, txb_phys;
static u32 rx_at;                       /* the receive descriptor the next frame comes in */
static u32 tx_head, tx_tail;            /* the transmit descriptor to use next; the oldest not yet seen sent */
static u8 cr1;                          /* CR1 as it is kept: DPOLL, and FDX with a full-duplex link */
static u8 full;

static u8 rd8(u32 r) { return inb((u16)(rh.base + r)); }
static u16 rd16(u32 r) { return inw((u16)(rh.base + r)); }
static void wr8(u32 r, u8 v) { outb((u16)(rh.base + r), v); }
static void wr16(u32 r, u16 v) { outw((u16)(rh.base + r), v); }
static void wr32(u32 r, u32 v) { outl((u16)(rh.base + r), v); }

static void pause(u32 n)                /* about n microseconds: a read of port 80h each */
{
    while (n--)
        inb(0x80);
}

/* A PHY register through the chip's MII port: its value, or -1 if the chip never finishes. */
static int mii_read(u32 reg)
{
    u32 n;
    wr8(MIICR, 0);                              /* (its own polling of the link off while this reads) */
    pause(100);
    wr8(MIIAD, (u8)reg);
    wr8(MIICR, 0x40);                           /* read */
    for (n = 0; n < 20000 && (rd8(MIICR) & 0x40); n++)
        pause(1);
    return rd8(MIICR) & 0x40 ? -1 : (int)rd16(MIIDATA);
}

/* The duplex both ends agreed on, from what each advertised (the PHY's registers 4 and 5): the MAC is told. */
static void duplex(void)
{
    int adv = mii_read(4), lpa = mii_read(5), bmcr = mii_read(0);
    int both = adv >= 0 && lpa >= 0 ? adv & lpa : 0;
    u8 fdx = (u8)((both & 0x0100) || ((both & 0x0180) == 0 && (both & 0x0040)));   /* 100 full; or 10 full with no 100 */
    if (bmcr >= 0 && !(bmcr & 0x1000))          /* (no negotiation: as the PHY is set) */
        fdx = (bmcr & 0x0100) != 0;
    if (fdx != full || !(cr1 & CR1_DPOLL)) {
        full = fdx;
        cr1 = (u8)(CR1_DPOLL | (fdx ? CR1_FDX : 0));
        wr8(CR1, cr1);
        kprintf("GLOS-NET rhine link adv=%04x partner=%04x duplex=%s\n", adv & 0xFFFF, lpa & 0xFFFF, fdx ? "full" : "half");
    }
}

static void rings(void)
{
    u32 i;
    for (i = 0; i < RX_N; i++) {
        rxd[i].status = OWN;
        rxd[i].ctl = 1536;
        rxd[i].buf = rxb_phys + i * BUF;
        rxd[i].next = rxd_phys + ((i + 1) % RX_N) * (u32)sizeof(struct desc);
    }
    for (i = 0; i < TX_N; i++) {
        txd[i].status = 0;
        txd[i].ctl = TX_FRAME;
        txd[i].buf = txb_phys + i * BUF;
        txd[i].next = txd_phys + ((i + 1) % TX_N) * (u32)sizeof(struct desc);
    }
    rx_at = tx_head = tx_tail = 0;
}

/* The chip reset and set going: 0, or -1 if it doesn't come out of reset. */
static int start(void)
{
    u32 n;
    wr8(CR1, CR1_SFRST);
    for (n = 0; n < 20000 && (rd8(CR1) & CR1_SFRST); n++)
        pause(10);
    if (rd8(CR1) & CR1_SFRST)
        return -1;
    wr8(EECSR, (u8)(rd8(EECSR) | 0x20));        /* its address and settings again from its EEPROM */
    for (n = 0; n < 20000 && (rd8(EECSR) & 0x20); n++)
        pause(10);
    rings();
    wr32(RDA, rxd_phys);
    wr32(TDA, txd_phys);
    wr8(RCR, RCR_WHOLE | RCR_AB | RCR_AM);
    wr8(TCR, TCR_WHOLE);
    for (n = 0; n < 8; n++)
        wr8(MAR0 + n, 0xFF);
    wr16(ISR, 0xFFFF);
    wr16(IMR, INTS);
    cr1 = 0;                                    /* (duplex() writes it: DPOLL, and FDX if the link has it) */
    full = 0;
    duplex();
    wr8(CR0, CR0_STRT | CR0_RXON | CR0_TXON);
    wr8(CR0, CR0_STRT | CR0_RXON | CR0_TXON | CR0_RDMD);
    return 0;
}

/* At leave: nothing more received or sent, its interrupts masked, the chip reset. */
static void rhine_stop(struct nic *c)
{
    u32 n;
    (void)c;
    wr16(IMR, 0);
    wr8(CR0, CR0_STOP);
    wr8(CR1, CR1_SFRST);
    for (n = 0; n < 20000 && (rd8(CR1) & CR1_SFRST); n++)
        pause(10);
}

static int rhine_send(struct nic *c, const void *frame, u32 len)
{
    u32 n = len < 60 ? 60 : len, i;
    u8 *b = txb + tx_head * BUF;
    (void)c;
    if (len > ETH_MAX || (txd[tx_head].status & OWN) || (tx_head + 1) % TX_N == tx_tail)
        return -1;                              /* (the ring is full: the frames before haven't left) */
    memcpy(b, frame, len);
    for (i = len; i < n; i++)                   /* (a short frame padded here, with zeros) */
        b[i] = 0;
    txd[tx_head].ctl = TX_FRAME | n;
    txd[tx_head].status = OWN;
    tx_head = (tx_head + 1) % TX_N;
    wr8(CR1, (u8)(cr1 | CR1_TDMD));
    rh.tx_frames++;
    return 0;
}

static void rhine_service(struct nic *c)
{
    u16 isr = rd16(ISR);
    u32 guard;
    (void)c;
    if (!isr)
        return;
    wr16(ISR, isr);
    if (isr & INT_OVFI)
        rh.overruns++;
    for (guard = 0; guard < RX_N && !(rxd[rx_at].status & OWN); guard++) {
        u32 st = rxd[rx_at].status, len = (st >> 16) & 0x7FF;
        if ((st & RX_OK) && (st & RX_WHOLE) == RX_WHOLE && len >= 4 + 14 && len <= ETH_MAX + 4) {
            rh.rx_frames++;
            net_rx(&rh, rxb + rx_at * BUF, len - 4);    /* without its CRC */
        } else {
            rh.rx_errors++;
        }
        rxd[rx_at].status = OWN;
        rx_at = (rx_at + 1) % RX_N;
    }
    if (isr & (INT_RU | INT_NORBF | INT_OVFI))          /* it stopped for want of a descriptor: on again */
        wr8(CR0, CR0_STRT | CR0_RXON | CR0_TXON | CR0_RDMD);
    while (tx_tail != tx_head && !(txd[tx_tail].status & OWN)) {
        if (txd[tx_tail].status & TX_ERR)
            rh.rx_errors++;                     /* (the card's errors are one count: glos net) */
        tx_tail = (tx_tail + 1) % TX_N;
    }
    if (isr & (INT_ABTI | INT_UDFI | INT_TU | INT_TXE)) {      /* a frame given up: the chip has stopped
                                                   sending. From the oldest frame still its own, on again */
        if (tx_tail != tx_head) {
            wr32(TDA, txd_phys + tx_tail * (u32)sizeof(struct desc));
            wr8(CR0, CR0_STRT | CR0_RXON | CR0_TXON);
            wr8(CR1, (u8)(cr1 | CR1_TDMD));
        } else {
            wr8(CR0, CR0_STRT | CR0_RXON | CR0_TXON);
        }
    }
    if (isr & INT_SRCI)
        duplex();
    if (isr & INT_BE)
        kprintf("GLOS-WARN rhine bus-error isr=%04x\n", isr);
}

int rhine_probe(void)
{
    static const u16 ids[] = { 0x3043, 0x3065, 0x3106 };        /* VT86C100A, VT6102, VT6105 */
    struct pci_dev *d = 0;
    u32 i;
    for (i = 0; i < ARRAY_SIZE(ids) && !d; i++)
        d = pci_find(0x1106, ids[i], 0);
    if (!d)
        return -1;
    if (!(d->bar[0] & 1) || !(d->bar[0] & ~3u)) {
        kprintf("GLOS-NET refuse card=rhine reason=no-io (the BIOS, or Windows before DOS, left it without an address)\n");
        return -1;
    }
    if (nic_pci(d, "rhine") != 0)
        return -1;
    if (!rxd) {
        u8 *ring = dma_alloc(1, &rxd_phys);     /* both rings in one page */
        if (ring) {
            rxd = (struct desc *)ring;
            txd = (struct desc *)(ring + RX_N * sizeof(struct desc));
            txd_phys = rxd_phys + RX_N * (u32)sizeof(struct desc);
        }
        rxb = dma_alloc(RX_N * BUF / 4096, &rxb_phys);
        txb = dma_alloc(TX_N * BUF / 4096, &txb_phys);
    }
    if (!rxd || !rxb || !txb) {
        kprintf("GLOS-NET refuse card=rhine reason=no-memory\n");
        return -1;
    }
    nic_pci_on(d);
    rh.name = "rhine";
    rh.base = (u16)(d->bar[0] & ~3u);
    rh.irq = d->irq;
    if (start() != 0) {
        kprintf("GLOS-NET refuse card=rhine base=%x reason=no-response\n", rh.base);
        return -1;
    }
    for (i = 0; i < 6; i++)
        rh.mac[i] = rd8(PAR0 + i);
    rh.send = rhine_send;
    rh.service = rhine_service;
    rh.stop = rhine_stop;
    vdev_hide(rh.base, d->device == 0x3043 ? 0x80 : 0x100);     /* (the first Rhine has 128 bytes of registers) */
    nic_claimed(&rh, d);
    return 0;
}
