/* Intel 8255x (82557, 82558, 82559: PRD §8.1), through its registers in I/O
 * space.
 *
 * The card works from blocks in memory it reads and writes itself, from
 * dma_alloc(): command blocks for its command unit (configure, set the
 * address, transmit) and a ring of receive frame descriptors for its
 * receive unit, each with room for a frame after its header ("simplified
 * mode"). The last descriptor of the ring carries EL; when the card gets
 * there it stops (receive not ready), and the driver, which has by then
 * handed the earlier ones back, starts it again.
 *
 * One transmit block: a frame goes out when the one before has left, which
 * the driver waits for briefly.
 *
 * The address is in the card's serial EEPROM (93C46 or 93C66: the width of
 * its addresses is found as the part itself tells it, by the zero it puts
 * out after the last address bit).
 *
 * Written from Intel's "8255x Open Source Software Developer Manual". */
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "nic.h"
#include "pci.h"
#include "vm.h"

#define SCB_STATUS 0x00                 /* bits 5-2: the receive unit's state */
#define SCB_ACK    0x01                 /* what interrupts; written back to acknowledge */
#define SCB_CMD    0x02
#define SCB_MASK   0x03                 /* bit 0: no interrupts */
#define SCB_PTR    0x04
#define PORT       0x08
#define EEPROM     0x0E

#define CU_START   0x10
#define CU_BASE    0x60
#define RU_START   0x01
#define RU_BASE    0x06
#define RU_READY   4

#define ACK_RNR    0x10

#define CB_C       0x8000u              /* status: done */
#define CB_OK      0x2000u
#define CB_EL      0x8000u              /* command: the last of its list */
#define CMD_IA     1
#define CMD_CONFIG 2
#define CMD_TX     4

#define EE_SK 1
#define EE_CS 2
#define EE_DI 4
#define EE_DO 8

#define NRFD     16
#define SLOT     1536u                  /* a descriptor or command block with its frame */
#define RFD_DATA (SLOT - 16)

struct block {                          /* the head of a command block or a receive frame descriptor */
    volatile u16 status;
    u16 command;
    u32 link;
    u32 ptr;                            /* transmit: the buffer list (none: FFFFFFFFh); receive: the same */
    volatile u16 count;                 /* transmit: bytes that follow, and EOF; receive: bytes received */
    u16 size;                           /* transmit: threshold and buffer count; receive: room */
};

static struct nic e;
static u8 *rfds, *cb;
static u32 rfds_phys, cb_phys;
static u32 rx_next, rx_last;

static struct block *rfd(u32 i) { return (struct block *)(rfds + i * SLOT); }

static void pause(u32 n)
{
    while (n--)
        inb(0x80);
}

/* The command register takes the next command when it reads zero. */
static void scb(u8 cmd, u32 ptr)
{
    u32 n;
    for (n = 0; n < 100000 && inb((u16)(e.base + SCB_CMD)); n++) ;
    outl((u16)(e.base + SCB_PTR), ptr);
    outb((u16)(e.base + SCB_CMD), cmd);
}

/* The command block at cb, started and waited for: 0 if the card did it. */
static int run_cb(void)
{
    struct block *b = (struct block *)cb;
    u32 n;
    b->status = 0;
    scb(CU_START, cb_phys);
    for (n = 0; n < 200000 && !(b->status & CB_C); n++)
        pause(1);
    return (b->status & (CB_C | CB_OK)) == (CB_C | CB_OK) ? 0 : -1;
}

/* ---- the EEPROM: bits clocked through the control register */

static void ee_out(u8 v)
{
    outb((u16)(e.base + EEPROM), v);
    pause(4);
}

static int ee_bit(int di)
{
    u8 v = (u8)(EE_CS | (di ? EE_DI : 0));
    ee_out(v);
    ee_out((u8)(v | EE_SK));
    v = inb((u16)(e.base + EEPROM));
    ee_out((u8)(EE_CS | (di ? EE_DI : 0)));
    return (v & EE_DO) != 0;
}

static u16 ee_read(u32 addr, u32 bits)
{
    u32 i;
    u16 v = 0;
    ee_out(EE_CS);
    ee_bit(1);                                  /* start, then "read": 1 0 */
    ee_bit(1);
    ee_bit(0);
    for (i = bits; i-- > 0;)
        ee_bit((addr >> i) & 1);
    for (i = 0; i < 16; i++)
        v = (u16)((v << 1) | ee_bit(0));
    ee_out(0);
    return v;
}

/* The address bits the part takes: it puts out a zero after the last. */
static u32 ee_width(void)
{
    u32 bits = 0;
    ee_out(EE_CS);
    ee_bit(1);
    ee_bit(1);
    ee_bit(0);
    while (bits < 12) {
        bits++;
        if (!ee_bit(0))
            break;
    }
    for (u32 i = 0; i < 16; i++)
        ee_bit(0);
    ee_out(0);
    return bits >= 12 ? 6 : bits;
}

/* ---- */

static void rx_start(void)
{
    scb(RU_START, rfds_phys + rx_next * SLOT);
}

static int start(void)
{
    /* The configuration the manual gives as the default, with every
       multicast frame taken (byte 21, bit 3) for mDNS. Byte 6 keeps the
       standard transmit block; byte 18 pads short frames. */
    static const u8 config[22] = { 22, 0x08, 0x00, 0x00, 0x00, 0x00, 0x32, 0x03, 0x01, 0x00, 0x2E,
                                   0x00, 0x60, 0x00, 0xF2, 0x48, 0x00, 0x40, 0xF2, 0x80, 0x3F, 0x0D };
    struct block *b = (struct block *)cb;
    u32 i;

    outl((u16)(e.base + PORT), 0);              /* reset */
    pause(100);
    outb((u16)(e.base + SCB_MASK), 1);
    scb(CU_BASE, 0);
    scb(RU_BASE, 0);

    b->command = CMD_CONFIG | CB_EL;
    b->link = 0xFFFFFFFFu;
    memcpy(cb + 8, config, sizeof config);
    if (run_cb() != 0)
        return -1;
    b->command = CMD_IA | CB_EL;
    memcpy(cb + 8, e.mac, 6);
    if (run_cb() != 0)
        return -1;

    for (i = 0; i < NRFD; i++) {
        struct block *r = rfd(i);
        r->status = 0;
        r->command = i == NRFD - 1 ? CB_EL : 0;
        r->link = rfds_phys + ((i + 1) % NRFD) * SLOT;
        r->ptr = 0xFFFFFFFFu;
        r->count = 0;
        r->size = RFD_DATA;
    }
    rx_next = 0;
    rx_last = NRFD - 1;
    b->status = CB_C;                           /* (nothing waits to go out) */
    rx_start();
    outb((u16)(e.base + SCB_ACK), 0xFF);
    outb((u16)(e.base + SCB_MASK), 0);
    return 0;
}

static int e100_send(struct nic *c, const void *frame, u32 len)
{
    struct block *b = (struct block *)cb;
    u32 n;
    (void)c;
    if (len > ETH_MAX)
        return -1;
    for (n = 0; n < 3000 && !(b->status & CB_C); n++)   /* the frame before: a millisecond or so at 10 Mbit/s */
        pause(1);
    if (!(b->status & CB_C))
        return -1;
    b->status = 0;
    b->command = CMD_TX | CB_EL;
    b->link = 0xFFFFFFFFu;
    b->ptr = 0xFFFFFFFFu;                       /* the frame follows the block */
    b->count = (u16)(len | 0x8000);             /* and this is all of it */
    b->size = 0x0001;                           /* no buffers; start sending once 8 bytes are in the FIFO */
    memcpy(cb + 16, frame, len);
    scb(CU_START, cb_phys);
    e.tx_frames++;
    return 0;
}

static void e100_service(struct nic *c)
{
    u8 ack = inb((u16)(e.base + SCB_ACK));
    u32 guard = 0;
    (void)c;
    if (ack)
        outb((u16)(e.base + SCB_ACK), ack);
    while ((rfd(rx_next)->status & CB_C) && guard++ < 2 * NRFD) {
        struct block *r = rfd(rx_next);
        u32 len = r->count & 0x3FFF;
        if ((r->status & CB_OK) && len >= 14 && len <= ETH_MAX) {
            e.rx_frames++;
            net_rx(&e, (const u8 *)r + 16, len);
        } else {
            e.rx_errors++;
        }
        r->status = 0;                          /* back to the card, as the ring's new end */
        r->count = 0;
        r->command = CB_EL;
        rfd(rx_last)->command = 0;
        rx_last = rx_next;
        rx_next = (rx_next + 1) % NRFD;
    }
    if (((inb((u16)(e.base + SCB_STATUS)) >> 2) & 15) != RU_READY) {    /* it reached the end: again, from */
        if (ack & ACK_RNR)                                              /* the first one it hasn't filled */
            e.overruns++;
        rx_start();
    }
}

int e100_probe(void)
{
    static const u16 ids[] = { 0x1229, 0x1209, 0x2449, 0x1030, 0x1031, 0x1038, 0x1039, 0x103A };
    struct pci_dev *d = 0;
    u32 i, bits;

    for (i = 0; i < ARRAY_SIZE(ids) && !d; i++)
        d = pci_find(0x8086, ids[i], 0);
    if (!d)
        return -1;
    if (!(d->bar[1] & 1) || !(d->bar[1] & ~3u)) {
        kprintf("GLOS-NET refuse card=i8255x reason=no-io\n");
        return -1;
    }
    if (nic_pci(d, "i8255x") != 0)
        return -1;
    if (!(rfds = dma_alloc(NRFD * SLOT / 4096, &rfds_phys)) || !(cb = dma_alloc(1, &cb_phys))) {
        kprintf("GLOS-NET refuse card=i8255x reason=no-memory\n");
        return -1;
    }
    pci_write32(d->bus, d->dev, d->fn, 4, pci_read32(d->bus, d->dev, d->fn, 4) | 0x05);  /* I/O, bus master */
    e.name = "i8255x";
    e.base = (u16)(d->bar[1] & ~3u);
    e.irq = d->irq;
    outl((u16)(e.base + PORT), 0);
    pause(100);
    outb((u16)(e.base + SCB_MASK), 1);
    bits = ee_width();
    for (i = 0; i < 3; i++) {
        u16 w = ee_read(i, bits);
        e.mac[i * 2] = (u8)w;
        e.mac[i * 2 + 1] = (u8)(w >> 8);
    }
    if (start() != 0) {
        kprintf("GLOS-NET refuse card=i8255x base=%x reason=no-response\n", e.base);
        return -1;
    }
    e.send = e100_send;
    e.service = e100_service;
    vdev_hide(e.base, 0x40);
    nic_claimed(&e, d);
    return 0;
}
