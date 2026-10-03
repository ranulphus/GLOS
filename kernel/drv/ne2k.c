/* NE2000 (DP8390 core): the RTL8029 on PCI, else an ISA card at a common
 * base (PRD §4.4). 16 KB of card memory: transmit buffer at 4000h (pages
 * 40h-45h), receive ring 4600h-7FFFh. Word-wide remote DMA throughout.
 *
 * Refused, with a GLOS-NET line, when a packet driver is loaded (it owns the
 * card) or when the card's PCI IRQ line is shared with another function
 * (the IRQ would no longer reach the VM's device). An ISA card's IRQ is found
 * by making it interrupt and reading the PICs' request registers. */
#include "io.h"
#include "kprintf.h"
#include "nic.h"
#include "pci.h"
#include "timer.h"
#include "vm.h"

#define CR      0x00
#define PSTART  0x01
#define PSTOP   0x02
#define BNRY    0x03
#define TPSR    0x04
#define TBCR0   0x05
#define TBCR1   0x06
#define ISR     0x07
#define RSAR0   0x08
#define RSAR1   0x09
#define RBCR0   0x0A
#define RBCR1   0x0B
#define RCR     0x0C
#define TCR     0x0D
#define DCR     0x0E
#define IMR     0x0F
#define CURR    0x07                    /* page 1 */
#define DATA    0x10
#define RESET   0x1F

#define TX_PAGE   0x40
#define RX_START  0x46
#define RX_STOP   0x80

#define ISR_PRX 0x01
#define ISR_PTX 0x02
#define ISR_RXE 0x04
#define ISR_TXE 0x08
#define ISR_OVW 0x10
#define ISR_CNT 0x20
#define ISR_RDC 0x40
#define ISR_RST 0x80

struct nic *nic0;
static struct nic ne;
static u8 frame[ETH_MAX + 6];
static int tx_busy;

static u8 rd(u8 reg) { return inb((u16)(ne.base + reg)); }
static void wr(u8 reg, u8 v) { outb((u16)(ne.base + reg), v); }

static int wait_rdc(void)
{
    u32 n;
    for (n = 0; n < 100000; n++)
        if (rd(ISR) & ISR_RDC) {
            wr(ISR, ISR_RDC);
            return 0;
        }
    return -1;
}

/* Remote DMA: card memory to dst, len bytes (word transfers, len rounded up). */
static int dma_read(u32 src, u8 *dst, u32 len)
{
    u32 i, words = (len + 1) / 2;
    u16 w;
    wr(CR, 0x22);
    wr(RBCR0, (u8)(words * 2));
    wr(RBCR1, (u8)((words * 2) >> 8));
    wr(RSAR0, (u8)src);
    wr(RSAR1, (u8)(src >> 8));
    wr(CR, 0x0A);                               /* start, remote read */
    for (i = 0; i < words; i++) {
        w = inw((u16)(ne.base + DATA));
        dst[2 * i] = (u8)w;
        if (2 * i + 1 < len)
            dst[2 * i + 1] = (u8)(w >> 8);
    }
    return wait_rdc();
}

static int dma_write(u32 dst, const u8 *src, u32 len)
{
    u32 i, words = (len + 1) / 2;
    wr(ISR, ISR_RDC);
    wr(CR, 0x22);
    wr(RBCR0, (u8)(words * 2));
    wr(RBCR1, (u8)((words * 2) >> 8));
    wr(RSAR0, (u8)dst);
    wr(RSAR1, (u8)(dst >> 8));
    wr(CR, 0x12);                               /* start, remote write */
    for (i = 0; i < words; i++)
        outw((u16)(ne.base + DATA), (u16)(src[2 * i] | ((2 * i + 1 < len ? src[2 * i + 1] : 0) << 8)));
    return wait_rdc();
}

static int reset(void)
{
    u32 n;
    wr(RESET, rd(RESET));
    for (n = 0; n < 100000; n++)
        if (rd(ISR) & ISR_RST)
            break;
    if (!(rd(ISR) & ISR_RST))
        return -1;
    wr(ISR, 0xFF);
    return 0;
}

/* Stopped, loopback, the station address from the PROM, then running. */
static int setup(void)
{
    u8 prom[32];
    int i, doubled = 1;

    wr(CR, 0x21);                               /* page 0, stopped, no DMA */
    wr(DCR, 0x49);                              /* word-wide, normal, 8-byte FIFO */
    wr(RBCR0, 0);
    wr(RBCR1, 0);
    wr(IMR, 0);
    wr(ISR, 0xFF);
    wr(RCR, 0x20);                              /* monitor */
    wr(TCR, 0x02);                              /* internal loopback */
    if (dma_read(0, prom, 32) != 0)
        return -1;
    for (i = 0; i < 12; i += 2)                 /* word-wide PROM reads repeat each byte */
        if (prom[i] != prom[i + 1])
            doubled = 0;
    for (i = 0; i < 6; i++)
        ne.mac[i] = doubled ? prom[2 * i] : prom[i];
    wr(PSTART, RX_START);
    wr(PSTOP, RX_STOP);
    wr(BNRY, RX_START);
    wr(CR, 0x61);                               /* page 1 */
    for (i = 0; i < 6; i++)
        wr((u8)(1 + i), ne.mac[i]);
    wr(CURR, RX_START + 1);
    for (i = 0; i < 8; i++)
        wr((u8)(8 + i), 0xFF);                  /* multicast: all (IPv4 multicast later) */
    wr(CR, 0x21);
    wr(ISR, 0xFF);
    wr(IMR, ISR_PRX | ISR_PTX | ISR_RXE | ISR_TXE | ISR_OVW);
    wr(CR, 0x22);                               /* started */
    wr(TCR, 0x00);
    wr(RCR, 0x04);                              /* broadcasts too */
    return 0;
}

static int ne_send(struct nic *n, const void *f, u32 len)
{
    u32 spin;
    (void)n;
    if (len > ETH_MAX)
        return -1;
    for (spin = 0; tx_busy && spin < 200000; spin++)    /* the last frame is still going */
        if (rd(ISR) & (ISR_PTX | ISR_TXE)) {
            wr(ISR, ISR_PTX | ISR_TXE);
            tx_busy = 0;
        }
    if (tx_busy)
        return -1;
    memcpy(frame, f, len);
    if (len < 60) {                             /* the minimum Ethernet frame */
        memset(frame + len, 0, 60 - len);
        len = 60;
    }
    if (dma_write(TX_PAGE << 8, frame, len) != 0)
        return -1;
    wr(TPSR, TX_PAGE);
    wr(TBCR0, (u8)len);
    wr(TBCR1, (u8)(len >> 8));
    wr(CR, 0x26);                               /* transmit */
    tx_busy = 1;
    ne.tx_frames++;
    return 0;
}

/* Receive-buffer overflow: the DP8390's recovery sequence. */
static void overflow(void)
{
    u32 n;
    ne.overruns++;
    wr(CR, 0x21);
    for (n = 0; n < 2000; n++)
        (void)rd(ISR);
    wr(RBCR0, 0);
    wr(RBCR1, 0);
    wr(TCR, 0x02);
    wr(CR, 0x22);
    wr(BNRY, RX_START);
    wr(CR, 0x62);
    wr(CURR, RX_START + 1);
    wr(CR, 0x22);
    wr(ISR, ISR_OVW);
    wr(TCR, 0x00);
}

static void receive(void)
{
    u8 hdr[4];
    u32 next, curr, len, at, first;
    for (;;) {
        wr(CR, 0x62);
        curr = rd(CURR);
        wr(CR, 0x22);
        next = rd(BNRY) + 1u;
        if (next >= RX_STOP)
            next = RX_START;
        if (next == curr)
            return;
        if (dma_read(next << 8, hdr, 4) != 0)
            return;
        len = (u32)(hdr[2] | (hdr[3] << 8));
        if (hdr[1] < RX_START || hdr[1] >= RX_STOP || len < 4 + 14 || len > ETH_MAX + 4 + 4) {
            ne.rx_errors++;                     /* a broken header: start the ring again */
            overflow();
            return;
        }
        len -= 4;
        if (len > ETH_MAX)
            len = ETH_MAX;                      /* the CRC, if the card counts it */
        at = (next << 8) + 4;
        first = (RX_STOP << 8) - at;
        if (len <= first) {
            dma_read(at, frame, len);
        } else {                                /* the frame wraps round the ring */
            dma_read(at, frame, first);
            dma_read(RX_START << 8, frame + first, len - first);
        }
        if (hdr[0] & 0x01) {                    /* received intact */
            ne.rx_frames++;
            net_rx(&ne, frame, len);
        } else {
            ne.rx_errors++;
        }
        wr(BNRY, (u8)(hdr[1] == RX_START ? RX_STOP - 1 : hdr[1] - 1));
    }
}

/* Until the card has nothing left to report: an event arriving while we work
   keeps its interrupt line high, and on an edge-triggered line that would
   never interrupt again. */
static void ne_service(struct nic *n)
{
    u8 isr;
    int rounds;
    (void)n;
    wr(CR, 0x22);
    for (rounds = 0; rounds < 16; rounds++) {
        isr = rd(ISR) & (ISR_PRX | ISR_PTX | ISR_RXE | ISR_TXE | ISR_OVW | ISR_CNT);
        if (!isr)
            return;
        if (isr & ISR_OVW)
            overflow();
        if (isr & (ISR_PTX | ISR_TXE))
            tx_busy = 0;
        wr(ISR, isr);
        if (isr & (ISR_PRX | ISR_RXE | ISR_OVW))
            receive();
    }
}

/* A packet driver (a "PKT DRVR" signature at INT 60h-80h) owns a card. */
static int packet_driver(void)
{
    u32 v, x, lin;
    for (v = 0x60; v <= 0x80; v++) {
        x = vm_rd32(v * 4);
        lin = (x >> 16) * 16 + (x & 0xFFFF) + 3;
        if (x && lin + 8 < 0x110000 && !memcmp(vm_ptr(lin), "PKT DRVR", 8))
            return (int)v;
    }
    return 0;
}

/* An ISA card's IRQ: have it interrupt (remote DMA complete) with every line
   masked, and see which request appears. */
static int isa_irq(void)
{
    u8 before[2], after[2];
    u16 saved = (u16)(inb(0x21) | (inb(0xA1) << 8));
    int i;
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);
    outb(0x20, 0x0A);
    outb(0xA0, 0x0A);
    before[0] = inb(0x20);
    before[1] = inb(0xA0);
    wr(IMR, ISR_RDC);                           /* a 2-byte remote read: RDC */
    wr(CR, 0x22);
    wr(RBCR0, 2);
    wr(RBCR1, 0);
    wr(RSAR0, 0);
    wr(RSAR1, 0);
    wr(CR, 0x0A);
    (void)inw((u16)(ne.base + DATA));
    for (i = 0; i < 100000 && !(rd(ISR) & ISR_RDC); i++) ;
    after[0] = inb(0x20);                       /* before acknowledging: a request that */
    after[1] = inb(0xA0);                       /* drops early can leave the 8259's IRR */
    wr(ISR, 0xFF);
    wr(IMR, ISR_PRX | ISR_PTX | ISR_RXE | ISR_TXE | ISR_OVW);    /* as setup() left it */
    outb(0x21, (u8)saved);
    outb(0xA1, (u8)(saved >> 8));
    for (i = 0; i < 16; i++)
        if (i != 2 && !(before[i >> 3] & (1u << (i & 7))) && (after[i >> 3] & (1u << (i & 7))))
            return i;
    return -1;
}

static int claim(const char *name, u16 base, int irq)
{
    ne.name = name;
    ne.base = base;
    if (reset() != 0 || setup() != 0) {
        kprintf("GLOS-NET refuse card=%s base=%x reason=no-response\n", name, base);
        return -1;
    }
    if (irq < 0)
        irq = isa_irq();
    if (irq <= 0 || irq == 2 || irq >= 16) {
        kprintf("GLOS-NET refuse card=%s base=%x reason=no-irq\n", name, base);
        return -1;
    }
    ne.irq = (u8)irq;
    ne.send = ne_send;
    ne.service = ne_service;
    vdev_hide(base, 0x20);
    vm_claim_irq(irq, net_irq);
    nic0 = &ne;
    kprintf("GLOS-NET card=%s base=%x irq=%u mac=%02x:%02x:%02x:%02x:%02x:%02x\n", name, base, irq, ne.mac[0],
            ne.mac[1], ne.mac[2], ne.mac[3], ne.mac[4], ne.mac[5]);
    return 0;
}

int ne2k_probe(void)
{
    static const u16 isa[] = { 0x300, 0x280, 0x320, 0x340, 0x360 };
    struct pci_dev *d = pci_find(0x10EC, 0x8029, 0);
    int pd = packet_driver();
    u32 i;

    if (pd) {
        kprintf("GLOS-NET refuse reason=packet-driver int=%02x\n", pd);
        return -1;
    }
    if (d) {
        if (!(d->bar[0] & 1) || pci_irq_shared(d)) {
            kprintf("GLOS-NET refuse card=rtl8029 reason=%s irq=%u\n", (d->bar[0] & 1) ? "shared-irq" : "no-io",
                    d->irq);
            return -1;
        }
        pci_write32(d->bus, d->dev, d->fn, 4, pci_read32(d->bus, d->dev, d->fn, 4) | 0x05);  /* I/O, bus master */
        if (claim("rtl8029", (u16)(d->bar[0] & ~3u), d->irq) != 0)
            return -1;
        pci_claim(d);
        return 0;
    }
    for (i = 0; i < ARRAY_SIZE(isa); i++) {
        u8 cr = inb(isa[i]);
        if (cr == 0xFF)
            continue;
        ne.base = isa[i];
        if (reset() == 0)
            return claim("ne2000", isa[i], -1);
    }
    return -1;
}
