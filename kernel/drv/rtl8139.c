/* Realtek RTL8139 (PRD §8.1; 86Box's "RTL8139C+" answers to it too), in the
 * mode every member of the family has: one receive ring the card fills, and
 * four transmit slots.
 *
 * The card reads and writes memory itself: a 32 KB receive ring (with 16
 * bytes more, and room for the frame it runs past the end with: WRAP; 8 KB
 * overran in every 1 MB transfer in 86Box, a frame or two each time) and
 * four 2 KB transmit buffers, from dma_alloc(). In the ring each frame has
 * a header of two words, its status and its length with the CRC; the next
 * starts on the next multiple of four. CAPR, where the driver has read to,
 * is kept 16 bytes behind, as the card wants it.
 *
 * Written from the RTL8139 datasheet's register descriptions. */
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "nic.h"
#include "pci.h"
#include "vm.h"

#define IDR0     0x00
#define MAR0     0x08
#define TSD0     0x10                   /* transmit status of slot n: + 4n */
#define TSAD0    0x20                   /* its buffer's address */
#define RBSTART  0x30
#define CR       0x37
#define CAPR     0x38
#define IMR      0x3C
#define ISR      0x3E
#define TCR      0x40
#define RCR      0x44
#define CONFIG1  0x52

#define CR_BUFE  0x01                   /* the receive ring is empty */
#define CR_TE    0x04
#define CR_RE    0x08
#define CR_RST   0x10

#define INT_ROK  0x0001
#define INT_RER  0x0002
#define INT_TOK  0x0004
#define INT_TER  0x0008
#define INT_RXOVW 0x0010
#define INT_FOVW 0x0040
#define INTS     (INT_ROK | INT_RER | INT_TOK | INT_TER | INT_RXOVW | INT_FOVW)

#define TSD_OWN  0x2000                 /* the card has taken the frame (or the slot was never used) */

#define RING     32768u
#define RING_FRAMES 9                   /* 32768 + 16 + a frame, in pages */
#define TX_SLOTS 4
#define TX_SIZE  2048u

static struct nic rtl;
static u8 *ring, *txmem;
static u32 ring_phys, tx_phys;
static u32 cur;                         /* where the next frame starts in the ring */
static u32 slot;                        /* the transmit slot to use next */

static u8 rd8(u32 r) { return inb((u16)(rtl.base + r)); }
static u16 rd16(u32 r) { return inw((u16)(rtl.base + r)); }
static u32 rd32(u32 r) { return inl((u16)(rtl.base + r)); }
static void wr8(u32 r, u8 v) { outb((u16)(rtl.base + r), v); }
static void wr16(u32 r, u16 v) { outw((u16)(rtl.base + r), v); }
static void wr32(u32 r, u32 v) { outl((u16)(rtl.base + r), v); }

/* The card reset and set going: 0, or -1 if it doesn't come out of reset. */
static int start(void)
{
    u32 n, i;
    wr8(CONFIG1, 0x00);                         /* out of its low-power state */
    wr8(CR, CR_RST);
    for (n = 0; n < 100000 && (rd8(CR) & CR_RST); n++) ;
    if (rd8(CR) & CR_RST)
        return -1;
    wr32(RBSTART, ring_phys);
    for (i = 0; i < TX_SLOTS; i++)
        wr32(TSAD0 + 4 * i, tx_phys + i * TX_SIZE);
    wr8(CR, CR_RE | CR_TE);                     /* (the two below take no writes before this) */
    wr32(MAR0, 0xFFFFFFFF);                     /* every multicast group (mDNS's is one) */
    wr32(MAR0 + 4, 0xFFFFFFFF);
    wr32(RCR, 0x0000F78E);                      /* frames for us, multicasts and broadcasts; WRAP; a 32 KB
                                                   ring; whole frames, in DMA bursts of any length */
    wr32(TCR, 0x03000700);                      /* the standard gap between frames; DMA bursts of 2048 */
    wr16(CAPR, 0xFFF0);                         /* read to the start (16 behind) */
    wr16(ISR, 0xFFFF);
    wr16(IMR, INTS);
    cur = 0;
    slot = 0;
    return 0;
}

static int rtl_send(struct nic *c, const void *frame, u32 len)
{
    u32 n = len < 60 ? 60 : len, i;
    u8 *b = txmem + slot * TX_SIZE;
    (void)c;
    if (len > ETH_MAX || !(rd32(TSD0 + 4 * slot) & TSD_OWN))     /* (the slot's last frame hasn't left) */
        return -1;
    memcpy(b, frame, len);
    for (i = len; i < n; i++)                   /* the card doesn't pad a short frame */
        b[i] = 0;
    wr32(TSD0 + 4 * slot, n);                   /* the length, OWN clear: go */
    slot = (slot + 1) % TX_SLOTS;
    rtl.tx_frames++;
    return 0;
}

static void rtl_service(struct nic *c)
{
    u16 isr = rd16(ISR);
    u32 guard = 0;
    (void)c;
    if (!isr)
        return;
    wr16(ISR, isr);
    if (isr & (INT_RXOVW | INT_FOVW))
        rtl.overruns++;
    while (!(rd8(CR) & CR_BUFE) && guard++ < 64) {
        const u8 *h = ring + cur;
        u32 status = h[0] | (h[1] << 8), len = h[2] | (h[3] << 8);
        if (!(status & 1) || len < 8 || len > ETH_MAX + 4) {    /* not a good frame: the ring can't be trusted */
            rtl.rx_errors++;
            start();
            return;
        }
        rtl.rx_frames++;
        net_rx(&rtl, h + 4, len - 4);           /* without its CRC */
        cur = (cur + len + 4 + 3) & ~3u;
        cur %= RING;
        wr16(CAPR, (u16)(cur - 16));
    }
    if (isr & INT_RER)
        rtl.rx_errors++;
}

int rtl8139_probe(void)
{
    struct pci_dev *d = pci_find(0x10EC, 0x8139, 0);
    u32 i;

    if (!d)
        return -1;
    if (!(d->bar[0] & 1) || !(d->bar[0] & ~3u)) {
        kprintf("GLOS-NET refuse card=rtl8139 reason=no-io\n");
        return -1;
    }
    if (nic_pci(d, "rtl8139") != 0)
        return -1;
    if (!(ring = dma_alloc(RING_FRAMES, &ring_phys)) || !(txmem = dma_alloc(TX_SLOTS * TX_SIZE / 4096, &tx_phys))) {
        kprintf("GLOS-NET refuse card=rtl8139 reason=no-memory\n");
        return -1;
    }
    pci_write32(d->bus, d->dev, d->fn, 4, pci_read32(d->bus, d->dev, d->fn, 4) | 0x05);  /* I/O, bus master */
    rtl.name = "rtl8139";
    rtl.base = (u16)(d->bar[0] & ~3u);
    rtl.irq = d->irq;
    if (start() != 0) {
        kprintf("GLOS-NET refuse card=rtl8139 base=%x reason=no-response\n", rtl.base);
        return -1;
    }
    for (i = 0; i < 6; i++)
        rtl.mac[i] = rd8(IDR0 + i);
    rtl.send = rtl_send;
    rtl.service = rtl_service;
    vdev_hide(rtl.base, 0x100);
    nic_claimed(&rtl, d);
    return 0;
}
