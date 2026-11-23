/* DEC's Tulip: the 21140 (1011:0009) and the 21142/21143 (1011:0019) (GLOS v1, phase H tier 2, 2026-11-23;
 * docs/v1/hardware-audit.md), the chips of many 10/100 cards of 1996-2000 and of Hyper-V's and Virtual PC's
 * emulated PCs; 86Box's `dec_21140_tulip` and `dec_21143_tulip`. (The Tulip's clones, from Macronix, Lite-On,
 * ADMtek and Davicom, differ in their address ROMs and PHYs: not driven here.)
 *
 * Written from Intel's "21143 PCI/CardBus 10/100Mb/s Ethernet LAN Controller Hardware Reference Manual"
 * (278074-001, October 1998; the copy at pcmcia-cs.sourceforge.net/specs/21143.pdf, sha256 fdfbe7c3...1d7095d):
 *   - its CSRs in I/O space, eight bytes apart: CSR0 bus mode (bit 0 software reset; 13:8 the burst length),
 *     CSR1 and CSR2 the transmit and receive poll demands, CSR3 and CSR4 the rings' addresses, CSR5 the status
 *     (cleared by writing it back), CSR6 the operating mode (1 start receive, 7 every multicast, 13 start
 *     transmit, 18 the MII/SYM port, 19 no heartbeat, 21 store and forward, 25 must be one), CSR7 the interrupt
 *     enables, CSR9 the serial ROM's pins (11 its select, 14 read, 3:0 data out, data in, clock, chip select);
 *   - descriptors of four longwords in ring mode (the last of a ring marked): the status (31 OWN; a received
 *     frame's length 29:16, its error 15, first and last 9 and 8), the control (a transmit: 31 an interrupt when
 *     sent, 30 last, 29 first, 27 a setup frame, 25 the ring's end; a receive: 25 the ring's end; 10:0 the
 *     buffer's size), the buffer's address;
 *   - the setup frame (192 bytes, sent first): sixteen addresses for the perfect filter, each in the low halves of
 *     three longwords (the station's own, the broadcast, the own again).
 * The station's address is in its serial ROM, bytes 20 to 25 (the 21X4 SROM format's), read a word at a time as a
 * 93C46 is (an 8-bit address, a 93C66's, if the 6-bit one gives none). Media: the MII/SYM port at 100 Mbit, half
 * duplex; the SIA's 10 Mbit ports and autosensing are not done (a bench Tulip would want them).
 * Proof: Loop A (86Box's models); no bench has one. */
#include <string.h>

#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "nic.h"
#include "pci.h"
#include "vm.h"

#define CSR(n) ((n) * 8)

#define C0_SWR   0x00000001u
#define C0_PBL16 0x00001000u
#define C5_TI    0x00000001u
#define C5_TU    0x00000004u
#define C5_RI    0x00000040u
#define C5_RU    0x00000080u
#define C5_FBE   0x00002000u
#define C5_AIS   0x00008000u
#define C5_NIS   0x00010000u
#define C6_SR    0x00000002u
#define C6_PM    0x00000080u
#define C6_ST    0x00002000u
#define C6_PS    0x00040000u
#define C6_HBD   0x00080000u
#define C6_SF    0x00200000u
#define C6_MBO   0x02000000u
#define C9_SR    0x00000800u
#define C9_RD    0x00004000u
#define C9_CS    0x00000001u
#define C9_CLK   0x00000002u
#define C9_DI    0x00000004u
#define C9_DO    0x00000008u

#define OWN      0x80000000u
#define R_ES     0x00008000u
#define R_FS     0x00000200u
#define R_LS     0x00000100u
#define D_END    0x02000000u            /* the ring's last descriptor (RER, TER) */
#define T_IC     0x80000000u
#define T_LS     0x40000000u
#define T_FS     0x20000000u
#define T_SET    0x08000000u

#define RX_N   16
#define TX_N   8
#define BUF    2048u
#define RX_LEN 1536u

struct desc {
    volatile u32 status, ctl, buf1, buf2;
};

static struct nic tu;
static struct desc *rxd, *txd;
static u8 *rxb, *txb;
static u32 ring_phys, rxb_phys, txb_phys;
static u32 rx_at, tx_head, tx_tail, mode;

static u32 rd(u32 n) { return inl((u16)(tu.base + CSR(n))); }
static void wr(u32 n, u32 v) { outl((u16)(tu.base + CSR(n)), v); }

static void pause(u32 n)
{
    while (n--)
        inb(0x80);
}

/* ---- the serial ROM, by its pins in CSR9 */
static void srom_out(u32 bit)
{
    u32 v = C9_SR | C9_RD | C9_CS | (bit ? C9_DI : 0);
    wr(9, v);
    pause(2);
    wr(9, v | C9_CLK);
    pause(2);
    wr(9, v);
    pause(2);
}

static u16 srom_word(u32 addr, u32 abits)
{
    u32 cmd = (6u << abits) | addr, i, v = 0;     /* the start bit and READ (110), then the address */
    wr(9, C9_SR | C9_RD);
    pause(2);
    wr(9, C9_SR | C9_RD | C9_CS);
    pause(2);
    for (i = abits + 3; i-- > 0; )
        srom_out((cmd >> i) & 1);
    for (i = 0; i < 16; i++) {
        wr(9, C9_SR | C9_RD | C9_CS | C9_CLK);
        pause(2);
        v = (v << 1) | ((rd(9) & C9_DO) ? 1 : 0);
        wr(9, C9_SR | C9_RD | C9_CS);
        pause(2);
    }
    wr(9, C9_SR | C9_RD);                       /* (chip select off) */
    return (u16)v;
}

static int mac_from_srom(u32 abits)
{
    u32 i, all = 0, none = 0xFF;
    for (i = 0; i < 3; i++) {
        u16 w = srom_word(10 + i, abits);       /* (bytes 20 to 25: words 10 to 12, low byte first) */
        tu.mac[i * 2] = (u8)w, tu.mac[i * 2 + 1] = (u8)(w >> 8);
    }
    for (i = 0; i < 6; i++)
        all |= tu.mac[i], none &= tu.mac[i];
    return all && none != 0xFF && !(tu.mac[0] & 1);
}

static void rings(void)
{
    u32 i;
    for (i = 0; i < RX_N; i++) {
        rxd[i].buf1 = rxb_phys + i * BUF, rxd[i].buf2 = 0;
        rxd[i].ctl = RX_LEN | (i == RX_N - 1 ? D_END : 0);
        rxd[i].status = OWN;
    }
    for (i = 0; i < TX_N; i++) {
        txd[i].buf1 = txb_phys + i * BUF, txd[i].buf2 = 0;
        txd[i].ctl = i == TX_N - 1 ? D_END : 0;
        txd[i].status = 0;
    }
    rx_at = tx_head = tx_tail = 0;
}

/* The setup frame: the perfect filter's sixteen addresses (own, broadcast, own ...). Sent as the first frame. */
static void setup_frame(void)
{
    u32 *f = (u32 *)(txb + tx_head * BUF), i;
    static const u8 bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    for (i = 0; i < 16; i++) {
        const u8 *a = i == 1 ? bcast : tu.mac;
        f[i * 3] = (u32)a[0] | (u32)a[1] << 8;
        f[i * 3 + 1] = (u32)a[2] | (u32)a[3] << 8;
        f[i * 3 + 2] = (u32)a[4] | (u32)a[5] << 8;
    }
    txd[tx_head].ctl = T_SET | 192 | (tx_head == TX_N - 1 ? D_END : 0);
    txd[tx_head].status = OWN;
    tx_head = (tx_head + 1) % TX_N;
    wr(1, 1);
}

static int start(void)
{
    u32 n;
    wr(0, C0_SWR);
    for (n = 0; n < 1000 && (rd(0) & C0_SWR); n++)
        pause(10);
    pause(100);
    wr(0, C0_PBL16);
    rings();
    wr(3, ring_phys);
    wr(4, ring_phys + RX_N * (u32)sizeof(struct desc));
    wr(5, 0xFFFFFFFFu);
    wr(7, C5_NIS | C5_AIS | C5_RI | C5_RU | C5_TI | C5_TU | C5_FBE);
    mode = C6_MBO | C6_SF | C6_HBD | C6_PS | C6_PM;
    wr(6, mode);
    mode |= C6_ST;
    wr(6, mode);                                /* transmit on: the setup frame first, ... */
    setup_frame();
    for (n = 0; n < 1000 && (txd[0].status & OWN); n++)
        pause(10);
    if (txd[0].status & OWN)
        return -1;
    tx_tail = tx_head;
    mode |= C6_SR;
    wr(6, mode);                                /* ... then receive */
    wr(2, 1);
    return 0;
}

static void tulip_stop(struct nic *c)
{
    (void)c;
    wr(7, 0);
    wr(6, rd(6) & ~(C6_SR | C6_ST));
    wr(0, C0_SWR);
}

static int tulip_send(struct nic *c, const void *frame, u32 len)
{
    u32 n = len < 60 ? 60 : len, i;
    u8 *b = txb + tx_head * BUF;
    (void)c;
    if (len > ETH_MAX || (txd[tx_head].status & OWN) || (tx_head + 1) % TX_N == tx_tail)
        return -1;                              /* (the ring is full) */
    memcpy(b, frame, len);
    for (i = len; i < n; i++)                   /* (a short frame padded here) */
        b[i] = 0;
    txd[tx_head].ctl = T_IC | T_LS | T_FS | n | (tx_head == TX_N - 1 ? D_END : 0);
    txd[tx_head].status = OWN;
    tx_head = (tx_head + 1) % TX_N;
    wr(1, 1);                                   /* (look at the ring) */
    tu.tx_frames++;
    return 0;
}

static void tulip_service(struct nic *c)
{
    u32 st = rd(5), guard;
    (void)c;
    if (!(st & 0x0001FFFFu))
        return;
    wr(5, st & 0x0001FFFFu);                    /* (cleared by writing them back) */
    for (guard = 0; guard < RX_N && !(rxd[rx_at].status & OWN); guard++) {
        u32 s = rxd[rx_at].status, len = (s >> 16) & 0x3FFF;
        if (!(s & R_ES) && (s & (R_FS | R_LS)) == (R_FS | R_LS) && len >= 4 + 14 && len <= ETH_MAX + 4) {
            tu.rx_frames++;
            net_rx(&tu, rxb + rx_at * BUF, len - 4);    /* without its CRC */
        } else {
            tu.rx_errors++;
        }
        rxd[rx_at].status = OWN;
        rx_at = (rx_at + 1) % RX_N;
    }
    if (st & C5_RU) {                           /* it stopped for want of a descriptor: look again */
        tu.overruns++;
        wr(2, 1);
    }
    while (tx_tail != tx_head && !(txd[tx_tail].status & OWN)) {
        if (txd[tx_tail].status & 0x8000u)
            tu.rx_errors++;                     /* (the card's errors are one count: glos net) */
        tx_tail = (tx_tail + 1) % TX_N;
    }
    if (st & C5_FBE)
        kprintf("GLOS-WARN tulip bus-error csr5=%08x\n", st);
}

int tulip_probe(void)
{
    struct pci_dev *d = pci_find(0x1011, 0x0019, 0);
    if (!d)
        d = pci_find(0x1011, 0x0009, 0);
    if (!d)
        return -1;
    if (!(d->bar[0] & 1) || !(d->bar[0] & ~3u)) {
        kprintf("GLOS-NET refuse card=tulip reason=no-io\n");
        return -1;
    }
    if (nic_pci(d, "tulip") != 0)
        return -1;
    if (!rxd) {
        u8 *ring = dma_alloc(1, &ring_phys);    /* both rings in one page */
        if (ring) {
            rxd = (struct desc *)ring;
            txd = (struct desc *)(ring + RX_N * sizeof(struct desc));
        }
        rxb = dma_alloc(RX_N * BUF / 4096, &rxb_phys);
        txb = dma_alloc(TX_N * BUF / 4096, &txb_phys);
    }
    if (!rxd || !rxb || !txb) {
        kprintf("GLOS-NET refuse card=tulip reason=no-memory\n");
        return -1;
    }
    nic_pci_on(d);
    tu.name = "tulip";
    tu.base = (u16)(d->bar[0] & ~3u);
    tu.irq = d->irq;
    wr(0, C0_SWR);
    pause(1000);
    if (!mac_from_srom(6) && !mac_from_srom(8)) {
        kprintf("GLOS-NET refuse card=tulip base=%x reason=no-address\n", tu.base);
        return -1;
    }
    if (start() != 0) {
        kprintf("GLOS-NET refuse card=tulip base=%x reason=no-response\n", tu.base);
        return -1;
    }
    tu.send = tulip_send;
    tu.service = tulip_service;
    tu.stop = tulip_stop;
    vdev_hide(tu.base, 0x80);
    nic_claimed(&tu, d);
    return 0;
}
