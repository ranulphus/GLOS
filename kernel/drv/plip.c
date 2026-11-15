/* The parallel link's wire (plip.h; docs/v1/cable-network.md, step N2): Ethernet frames over a LapLink cable
 * between two parallel ports, as Crynwr's PLIP packet driver and Linux's plip speak it (mode 0).
 *
 * The cable crosses five lines each way: this port's data bits 0 to 4 are the other port's status bits 3 to 7
 * (ERROR, SELECT, PAPER END, ACK, and BUSY, which reads inverted). A byte goes as two nibbles on data bits 0 to
 * 3, each clocked by bit 4 and acknowledged by the other end's bit 4:
 *
 *   sender   data = low nibble | 10h      receiver sees status bit 7 clear: takes bits 3-6, writes 10h
 *   sender   sees bit 7 clear; data = high nibble (bit 4 clear)
 *            receiver sees bit 7 set: takes bits 3-6, writes 00h; sender sees bit 7 set: the next byte
 *
 * A packet: the sender, finding the line idle (status F8h bits = 80h), writes 08h, which raises the receiver's
 * ACK line and so its interrupt; the receiver answers 01h (the sender's ERROR bit); then the length (low byte,
 * high byte), the frame's bytes, and their sum as one byte; both ends write 00h.
 *
 * Every wait here is a busy one, and a packet of 1500 bytes is 6000 of them, so the wire has a thread of its
 * own that runs beside the PC's programs, not ahead of them, and the waits are long (a quarter of a second):
 * two GLOS PCs keep a packet going while either is busy with something else. Linux and Crynwr wait about three
 * milliseconds for a nibble: a GLOS PC cabled to one of those answers in time only while it has nothing else
 * to do. 86Box's PLIP device waits a second.
 *
 * Frames wait in two small rings between this thread and the net thread (plip_send, plip_recv).
 *
 * Written from the protocol as Linux's Documentation/networking/plip.rst and Crynwr's PLIP.TXT describe it,
 * and checked against 86Box's device (src/network/net_plip.c), which was itself tested against both. */
#include <string.h>

#include "io.h"
#include "kprintf.h"
#include "plip.h"
#include "sched.h"
#include "timer.h"
#include "vm.h"

#define NFRAMES  8u                     /* frames that wait each way (a power of two): TCP's window is eight */
#define FRAME    1600u
#define NIBBLE_TICKS  256u              /* how long one nibble's answer is waited for */
#define TRIGGER_TICKS 64u               /* ... and the answer to "a packet is coming" */
#define CTRL_IRQ  0x1Cu                 /* the control port: not INIT, SELECT IN, the ACK line's interrupt on */
#define CTRL_QUIET 0x0Cu                /* ... off */

static struct {
    u16 base;
    u8 irq, on, lpt;
    u8 was_ctrl, was_data;
    volatile u8 woke;
    struct waitq wq;
    void (*kick)(void);
    u8 tx[NFRAMES][FRAME], rx[NFRAMES][FRAME];
    u16 txlen[NFRAMES], rxlen[NFRAMES];
    volatile u32 txh, txt, rxh, rxt;    /* (h: put by the one side; t: taken by the other) */
    u8 tx_tries;
    struct plip_counts n;
} pl;
static u8 started;

static u8 status(void) { return inb((u16)(pl.base + 1)); }
static void data(u8 v) { outb(pl.base, v); }

/* Wait until status bit 7 is `want` (0 or 80h), the same on two reads; the status then, or -1 at the timeout. */
static int wait_bit7(u8 want, u32 ticks)
{
    u32 t0 = timer_ticks(), n = 0;
    for (;;) {
        u8 a = status();
        if ((a & 0x80) == want && status() == a)
            return a;
        if ((++n & 0xFF) == 0 && timer_ticks() - t0 > ticks)
            return -1;
    }
}

static int rx_byte(void)
{
    int a, b;
    if ((a = wait_bit7(0x00, NIBBLE_TICKS)) < 0)
        return -1;
    data(0x10);
    if ((b = wait_bit7(0x80, NIBBLE_TICKS)) < 0)
        return -1;
    data(0x00);
    return ((a >> 3) & 0x0F) | ((b << 1) & 0xF0);
}

static int tx_byte(u8 v)
{
    data(v & 0x0F);
    data((u8)(0x10 | (v & 0x0F)));
    if (wait_bit7(0x00, NIBBLE_TICKS) < 0)
        return -1;
    data((u8)(0x10 | (v >> 4)));
    data(v >> 4);
    return wait_bit7(0x80, NIBBLE_TICKS) < 0 ? -1 : 0;
}

/* The other end has said a packet is coming (its 08h: our ACK line): the packet taken, into the ring. */
static void receive(void)
{
    u8 *f = pl.rx[pl.rxh % NFRAMES];
    u32 len, i;
    int lo, hi, c, sum = 0, room = pl.rxh - pl.rxt < NFRAMES;
    outb((u16)(pl.base + 2), CTRL_QUIET);
    data(0x01);                                 /* go on */
    if ((lo = rx_byte()) < 0 || (hi = rx_byte()) < 0)
        goto bad;
    len = (u32)lo | (u32)hi << 8;
    if (len < 14 || len > FRAME)
        goto bad;
    for (i = 0; i < len; i++) {
        if ((c = rx_byte()) < 0)
            goto bad;
        if (room)
            f[i] = (u8)c;
        sum += c;
    }
    if ((c = rx_byte()) < 0 || c != (sum & 0xFF))
        goto bad;
    data(0x00);
    if (room) {
        u32 fl = irq_save();
        pl.rxlen[pl.rxh % NFRAMES] = (u16)len;
        pl.rxh++;
        irq_restore(fl);
        pl.n.rx++;
        if (pl.kick)
            pl.kick();
    } else {
        pl.n.rx_dropped++;                      /* (the net thread is behind: TCP sends it again) */
    }
    outb((u16)(pl.base + 2), CTRL_IRQ);
    return;
bad:
    pl.n.rx_errors++;
    data(0x00);
    outb((u16)(pl.base + 2), CTRL_IRQ);
}

/* The oldest frame that waits, sent: 1 sent (or given up), 0 not now (the other end is sending, or isn't there). */
static int send(void)
{
    const u8 *f = pl.tx[pl.txt % NFRAMES];
    u32 len = pl.txlen[pl.txt % NFRAMES], i, t0, n = 0;
    int sum = 0;
    u8 st;
    if ((status() & 0xF8) != 0x80)
        return 0;                               /* (not idle: its packet first) */
    outb((u16)(pl.base + 2), CTRL_QUIET);
    data(0x08);                                 /* a packet is coming */
    for (t0 = timer_ticks();;) {
        st = status();
        if (st & 0x08)
            break;                              /* go on, it says */
        if ((st & 0xF8) == 0xC0 && n > 16) {    /* it says the same to us, at the same moment: ours waits */
            data(0x00);
            pl.n.collisions++;
            receive();
            return 0;
        }
        if ((++n & 0xFF) == 0 && timer_ticks() - t0 > TRIGGER_TICKS) {
            data(0x00);
            outb((u16)(pl.base + 2), CTRL_IRQ);
            if (++pl.tx_tries < 8)
                return 0;                       /* (nobody answered: again in a moment, a few times) */
            pl.n.tx_dropped++;
            goto done;
        }
    }
    if (tx_byte((u8)len) < 0 || tx_byte((u8)(len >> 8)) < 0)
        goto bad;
    for (i = 0; i < len; i++) {
        if (tx_byte(f[i]) < 0)
            goto bad;
        sum += f[i];
    }
    if (tx_byte((u8)sum) < 0)
        goto bad;
    pl.n.tx++;
    goto done;
bad:
    pl.n.tx_errors++;
done:
    data(0x00);
    outb((u16)(pl.base + 2), CTRL_IRQ);
    pl.tx_tries = 0;
    {
        u32 fl = irq_save();
        pl.txt++;
        irq_restore(fl);
    }
    return 1;
}

static void irq(struct trapframe *tf)           /* the ACK line: the other end's "a packet is coming" */
{
    (void)tf;
    pl.n.irqs++;
    pl.woke = 1;
    thread_wake(&pl.wq);
}

static void plip_thread(void *arg)
{
    (void)arg;
    for (;;) {
        u32 f = irq_save();
        if (!pl.woke && (!pl.on || pl.txh == pl.txt || pl.tx_tries))
            thread_wait(&pl.wq, timer_ticks() + (pl.on && pl.txh != pl.txt ? 32 : pl.on ? 64 : 1024));     /* (a
                                                   look now and then all the same: a port whose interrupt
                                                   never comes still moves packets) */
        pl.woke = 0;
        irq_restore(f);
        while (pl.on) {
            if ((status() & 0xF8) == 0xC0) {
                receive();
            } else if (pl.txh != pl.txt) {
                if (!send())
                    break;
            } else {
                break;
            }
        }
    }
}

int plip_open(u32 lpt, void (*kick)(void))
{
    static const u16 usual[3] = { 0x378, 0x278, 0x3BC };
    u16 base;
    u32 f;
    if (pl.on)
        return -2;
    if (lpt < 1 || lpt > 3)
        return -1;
    base = (u16)(vm_rd8(0x408 + 2 * (lpt - 1)) | (u16)vm_rd8(0x409 + 2 * (lpt - 1)) << 8);     /* the BIOS's */
    if (base != 0x378 && base != 0x278 && base != 0x3BC)
        base = usual[lpt - 1];
    f = irq_save();
    pl.was_data = inb(base), pl.was_ctrl = inb((u16)(base + 2));
    outb(base, 0x55);                           /* is a port there? Its data register keeps what is written */
    if (inb(base) != 0x55) {
        irq_restore(f);
        return -1;
    }
    outb(base, 0x00);
    if (inb(base) != 0x00) {
        irq_restore(f);
        return -1;
    }
    pl.base = base, pl.lpt = (u8)lpt, pl.irq = base == 0x278 ? 5 : 7, pl.kick = kick;
    pl.txh = pl.txt = pl.rxh = pl.rxt = 0;
    pl.tx_tries = 0;
    memset(&pl.n, 0, sizeof pl.n);
    outb((u16)(base + 2), CTRL_QUIET);
    vdev_hide(base, 3);
    vm_claim_irq(pl.irq, irq);
    pl.on = 1;
    outb((u16)(base + 2), CTRL_IRQ);
    irq_restore(f);
    if (!started) {
        started = 1;
        thread_create("plip", PRIO_NORMAL, plip_thread, 0);
    }
    return 0;
}

void plip_close(void)
{
    u32 f = irq_save();
    if (pl.on) {
        pl.on = 0;
        outb((u16)(pl.base + 2), CTRL_QUIET);
        vm_unclaim_irq(pl.irq);
        outb(pl.base, pl.was_data);
        outb((u16)(pl.base + 2), (u8)(pl.was_ctrl & ~0x10u));
        vdev_unhide(pl.base);
    }
    irq_restore(f);
}

int plip_send(const u8 *frame, u32 len)
{
    u32 f;
    if (!pl.on || len < 14 || len > FRAME)
        return -1;
    f = irq_save();
    if (pl.txh - pl.txt >= NFRAMES) {
        irq_restore(f);
        pl.n.tx_full++;
        return -1;                              /* (the wire is behind: TCP sends it again) */
    }
    memcpy(pl.tx[pl.txh % NFRAMES], frame, len);
    pl.txlen[pl.txh % NFRAMES] = (u16)len;
    pl.txh++;
    pl.woke = 1;
    thread_wake(&pl.wq);
    irq_restore(f);
    return 0;
}

u32 plip_recv(u8 *frame, u32 room)
{
    u32 f = irq_save(), len = 0;
    if (pl.rxt != pl.rxh) {
        len = pl.rxlen[pl.rxt % NFRAMES];
        if (len > room)
            len = 0;
        else
            memcpy(frame, pl.rx[pl.rxt % NFRAMES], len);
        pl.rxt++;
    }
    irq_restore(f);
    return len;
}

int plip_info(u32 *lpt, u16 *base, u32 *irq_line, struct plip_counts *c)
{
    if (!pl.on)
        return -1;
    *lpt = pl.lpt, *base = pl.base, *irq_line = pl.irq;
    *c = pl.n;
    return 0;
}
