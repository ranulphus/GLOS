/* A PC's serial port as a line for bytes (uart.h): an 8250, 16450 or 16550A, by interrupts. The 16550A's FIFOs
 * are used where the chip has them (a byte every 87 microseconds at 115,200 baud: without them the kernel has
 * to answer each one, and a chip that loses bytes says so in `overruns`).
 *
 * Two lines at most: the cable link's (kernel/net/slip.c) and an app's (API 2.10, NET's SERIAL: a serial
 * session's; kernel/net/sock.c). The app's has what a terminal on a wire wants beside: 5 to 8 bits, a parity,
 * one or two stop bits, any speed the chip's divisor comes within a hundredth of, and flow control, by the
 * RTS and CTS wires or by XON and XOFF. */
#include <string.h>

#include "arch.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "uart.h"
#include "vm.h"

#define RX 8192u
#define TX 32768u                               /* (a TCP connection's whole send buffer, escaped: slip.c) */
#define APP_RING 4096u                          /* the app's line: each way (its own rings are sock.c's) */
#define XON 0x11
#define XOFF 0x13

struct line {
    u16 base;
    u8 irq, on, fifo, wake_on;
    u8 any;                                     /* kick at every byte that comes (a terminal's) */
    u8 flow;                                    /* UART_FLOW_ */
    u8 held;                                    /* the far side says wait: CTS is off, or it sent XOFF */
    u8 stopped;                                 /* this side said wait: RTS off, or XOFF sent */
    u8 owe;                                     /* an XON or XOFF to send before anything else (0: none) */
    u8 mcr;
    u32 baud;
    void (*kick)(void);
    u8 was_ier, was_lcr, was_mcr, was_dll, was_dlm;
    volatile u32 rh;                            /* (the interrupt's) */
    u32 rt;
    u32 th;
    volatile u32 tt;                            /* (the interrupt's) */
    u32 rsize, tsize;                           /* the rings' sizes: powers of two */
    u8 *rx, *tx;
    struct uart_counts n;
};

static struct line lines[2];                    /* 0: the cable link's; 1: an app's */
static u8 rx0[RX], tx0[TX];
static u8 claimed[2];                           /* IRQ 4's and IRQ 3's users */

/* The chip's transmitter fed (interrupts off): its FIFO's 16 bytes, or one; it interrupts again when it is
   empty, while there is more. Nothing goes while the far side says wait, but the word that tells it to. */
static void feed(struct line *u)
{
    int n = u->fifo ? 16 : 1;
    if (inb((u16)(u->base + 5)) & 0x20) {
        if (u->owe) {
            outb(u->base, u->owe);
            u->owe = 0;
            n--;
        }
        if (!u->held)
            for (; n > 0 && u->tt != u->th; n--, u->n.tx++)
                outb(u->base, u->tx[u->tt++ & (u->tsize - 1)]);
    }
    outb((u16)(u->base + 1), (u8)((u->owe || (!u->held && u->tt != u->th) ? 0x02 : 0) | (u->flow == UART_FLOW_RTS ? 0x0D : 0x05)));
}

/* The reader is behind (three quarters of the ring wait) or has caught up (a quarter): the far side is told,
   where the line has a way to tell it. (Interrupts off.) */
static void pace(struct line *u)
{
    u32 waiting = u->rh - u->rt;
    if (!u->flow)
        return;
    if (!u->stopped && waiting > u->rsize / 4 * 3) {
        u->stopped = 1;
        if (u->flow == UART_FLOW_RTS)
            outb((u16)(u->base + 4), u->mcr &= (u8)~0x02);
        else
            u->owe = XOFF;
    } else if (u->stopped && waiting < u->rsize / 4) {
        u->stopped = 0;
        if (u->flow == UART_FLOW_RTS)
            outb((u16)(u->base + 4), u->mcr |= 0x02);
        else
            u->owe = XON;
    }
}

static int service(struct line *u)
{
    int turns, wake = 0;
    for (turns = 0; turns < 256; turns++) {
        u8 lsr;
        if (inb((u16)(u->base + 2)) & 1)        /* nothing pending */
            break;
        lsr = inb((u16)(u->base + 5));
        while (lsr & 0x01) {
            u8 c = inb(u->base);
            if (lsr & 0x02)
                u->n.overruns++;
            if (lsr & 0x1C)
                u->n.errors++;
            if (u->flow == UART_FLOW_XON && (c == XON || c == XOFF)) {  /* (the line's own words: no text) */
                u->held = c == XOFF;
            } else {
                if (u->rh - u->rt < u->rsize)
                    u->rx[u->rh++ & (u->rsize - 1)] = c, u->n.rx++;
                else
                    u->n.dropped++;
                if (c == u->wake_on || u->any)
                    wake = 1;
            }
            lsr = inb((u16)(u->base + 5));
        }
        if (lsr & 0x02)
            u->n.overruns++;
        if (u->flow == UART_FLOW_RTS)           /* the modem's lines: CTS says whether the far side takes bytes */
            u->held = !(inb((u16)(u->base + 6)) & 0x10);
        else
            inb((u16)(u->base + 6));            /* (a change is nothing here) */
        pace(u);
        if (lsr & 0x20 || u->owe)
            feed(u);
    }
    if (u->any && u->th - u->tt < u->tsize / 2)
        wake = 1;                               /* (room to send again: its writer may be waiting for it) */
    return wake || u->rh - u->rt > u->rsize / 2;
}

static void irq_of(int which)                   /* IRQ 4 (which 0: COM1, COM3) or IRQ 3 (COM2, COM4) */
{
    int i;
    for (i = 0; i < 2; i++) {
        struct line *u = &lines[i];
        if (u->on && u->irq == (which ? 3 : 4) && service(u) && u->kick)
            u->kick();
    }
}

static void irq4(struct trapframe *tf) { (void)tf; irq_of(0); }
static void irq3(struct trapframe *tf) { (void)tf; irq_of(1); }

static u16 base_of(u32 com)                     /* the BIOS's port for it, else the usual one */
{
    static const u16 usual[4] = { 0x3F8, 0x2F8, 0x3E8, 0x2E8 };
    u16 base = (u16)(vm_rd8(0x400 + 2 * (com - 1)) | (u16)vm_rd8(0x401 + 2 * (com - 1)) << 8);
    return base < 0x100 || base > 0x3F8 || (base & 7) ? usual[com - 1] : base;
}

/* The line opened: 0, or -1 (no chip answers there), -2 (it is open, or the other line has that port), -3 (a
   speed the chip can't be set to). lcr: the line control register's bits (the frame). */
static int open_line(struct line *u, u32 com, u32 baud, u8 lcr, u8 flow, u8 wake_on, int any, void (*kick)(void))
{
    const struct line *other = u == &lines[0] ? &lines[1] : &lines[0];
    u32 f, div, got;
    u16 base;
    if (u->on)
        return -2;
    if (com < 1 || com > 4)
        return -1;
    if (!baud || baud > 115200)
        return -3;
    div = (115200 + baud / 2) / baud;           /* (the nearest: 110 baud is 110.03) */
    got = 115200 / div;
    if ((got > baud ? got - baud : baud - got) * 100 > baud)
        return -3;
    base = base_of(com);
    if (other->on && other->base == base)
        return -2;
    f = irq_save();
    u->was_lcr = inb((u16)(base + 3));
    outb((u16)(base + 3), 0x1B);                /* is a chip there? Its line control register keeps what is written */
    if (inb((u16)(base + 3)) != 0x1B) {
        irq_restore(f);
        return -1;
    }
    outb((u16)(base + 3), 0x03);
    if (inb((u16)(base + 3)) != 0x03) {
        irq_restore(f);
        return -1;
    }
    u->was_ier = inb((u16)(base + 1));
    u->was_mcr = inb((u16)(base + 4));
    outb((u16)(base + 1), 0);
    outb((u16)(base + 3), 0x83);
    u->was_dll = inb(base), u->was_dlm = inb((u16)(base + 1));
    outb(base, (u8)div);
    outb((u16)(base + 1), (u8)(div >> 8));
    outb((u16)(base + 3), lcr);
    outb((u16)(base + 2), 0x07);                /* FIFOs on and emptied ... */
    outb((u16)(base + 2), 0x47);                /* ... an interrupt at 4 bytes, or when the line goes quiet (at 8
                                                   the 486 bench lost a byte in 30,000 at 115,200 baud: 12 more
                                                   bytes' time, a millisecond, is the kernel's to answer in) */
    u->fifo = (inb((u16)(base + 2)) & 0xC0) == 0xC0;    /* (a 16550A says so; an 8250 or 16450 ignores it) */
    if (!u->fifo)
        outb((u16)(base + 2), 0);
    inb((u16)(base + 5)), inb(base), inb((u16)(base + 2)), inb((u16)(base + 6));
    u->base = base, u->irq = (u8)(com & 1 ? 4 : 3), u->baud = baud, u->wake_on = wake_on, u->kick = kick;
    u->any = (u8)any, u->flow = flow, u->stopped = 0, u->owe = 0;
    u->rh = u->rt = u->th = u->tt = 0;
    memset(&u->n, 0, sizeof u->n);
    u->on = 1;
    if (base == COM1)
        kprintf_com1_off = 1;                   /* (the log stays in memory: glos log) */
    vdev_hide(base, 8);
    if (!claimed[u->irq == 3]++)
        vm_claim_irq(u->irq, u->irq == 3 ? irq3 : irq4);
    u->mcr = 0x0B;                              /* DTR, RTS, and OUT2, which lets the chip's interrupt through */
    outb((u16)(base + 4), u->mcr);
    u->held = flow == UART_FLOW_RTS && !(inb((u16)(base + 6)) & 0x10);
    outb((u16)(base + 1), flow == UART_FLOW_RTS ? 0x0D : 0x05); /* received bytes, the line's status; CTS's changes */
    irq_restore(f);
    return 0;
}

static void close_line(struct line *u)          /* the port as it was found, DOS's again */
{
    u32 f = irq_save();
    if (u->on) {
        outb((u16)(u->base + 1), 0);
        if (!--claimed[u->irq == 3])
            vm_unclaim_irq(u->irq);
        outb((u16)(u->base + 2), 0);
        outb((u16)(u->base + 3), 0x83);
        outb(u->base, u->was_dll);
        outb((u16)(u->base + 1), u->was_dlm);
        outb((u16)(u->base + 3), u->was_lcr);
        outb((u16)(u->base + 4), u->was_mcr);
        outb((u16)(u->base + 1), u->was_ier);
        vdev_unhide(u->base);
        u->on = 0;
        if (u->base == COM1) {
            serial_init(COM1);                  /* (as the kernel's log has it) */
            kprintf_com1_off = 0;
        }
    }
    irq_restore(f);
}

static u32 read_line(struct line *u, u8 *buf, u32 room)
{
    u32 n = 0, f;
    while (n < room && u->rt != u->rh)
        buf[n++] = u->rx[u->rt++ & (u->rsize - 1)];
    if (n && u->flow && u->stopped) {           /* (caught up: the far side may send again) */
        f = irq_save();
        pace(u);
        if (u->owe)
            feed(u);
        irq_restore(f);
    }
    return n;
}

static void write_line(struct line *u, const u8 *d, u32 n)
{
    u32 f;
    if (!u->on)
        return;
    for (; n && u->th - u->tt < u->tsize; n--)
        u->tx[u->th++ & (u->tsize - 1)] = *d++;
    f = irq_save();
    feed(u);
    irq_restore(f);
}

/* ---- the cable link's line */

int uart_open(u32 com, u32 baud, u8 wake_on, void (*kick)(void))
{
    struct line *u = &lines[0];
    if (baud && 115200 % baud)
        return -3;                              /* (a link's speed is one both ends can have exactly) */
    u->rx = rx0, u->tx = tx0, u->rsize = RX, u->tsize = TX;
    return open_line(u, com, baud, 0x03, UART_FLOW_NONE, wake_on, 0, kick);    /* 8 bits, no parity, one stop bit */
}

void uart_close(void) { close_line(&lines[0]); }

int uart_is_open(u16 port)
{
    int i;
    for (i = 0; i < 2; i++)
        if (lines[i].on && port >= lines[i].base && port < lines[i].base + 8)
            return 1;
    return 0;
}

u32 uart_read(u8 *buf, u32 room) { return read_line(&lines[0], buf, room); }
u32 uart_room(void) { return lines[0].on ? TX - (lines[0].th - lines[0].tt) : 0; }
void uart_write(const u8 *d, u32 n) { write_line(&lines[0], d, n); }

int uart_info(u16 *base, u32 *irq_line, u32 *baud, int *fifo, struct uart_counts *c)
{
    const struct line *u = &lines[0];
    if (!u->on)
        return -1;
    *base = u->base, *irq_line = u->irq, *baud = u->baud, *fifo = u->fifo, *c = u->n;
    return 0;
}

/* ---- an app's line (API 2.10) */

int uart_app_open(const struct uart_setup *su, void (*kick)(void))
{
    struct line *u = &lines[1];
    u8 lcr;
    int r;
    if (u->on)
        return -2;
    if (su->bits < 5 || su->bits > 8 || su->parity > UART_PARITY_EVEN || su->stop < 1 || su->stop > 2
        || su->flow > UART_FLOW_XON)
        return -3;
    lcr = (u8)((su->bits - 5) | (su->stop == 2 ? 0x04 : 0)
               | (su->parity == UART_PARITY_ODD ? 0x08 : su->parity == UART_PARITY_EVEN ? 0x18 : 0));
    if (!u->rx && (!(u->rx = kmalloc(APP_RING)) || !(u->tx = kmalloc(APP_RING)))) {
        kfree(u->rx);
        u->rx = u->tx = 0;
        return -4;
    }
    u->rsize = u->tsize = APP_RING;
    r = open_line(u, su->com, su->baud, lcr, (u8)su->flow, 0, 1, kick);
    if (r == 0)
        kprintf("GLOS-UART app com=%u base=%x irq=%u baud=%u bits=%u parity=%u stop=%u flow=%u fifo=%u\n", su->com, u->base,
                u->irq, su->baud, su->bits, su->parity, su->stop, su->flow, u->fifo);
    return r;
}

void uart_app_close(void)
{
    struct line *u = &lines[1];
    if (u->on)
        kprintf("GLOS-UART app closed rx=%u tx=%u overruns=%u errors=%u dropped=%u\n", u->n.rx, u->n.tx, u->n.overruns,
                u->n.errors, u->n.dropped);
    close_line(u);
}

int uart_app_on(void) { return lines[1].on; }
u32 uart_app_read(u8 *buf, u32 room) { return lines[1].on ? read_line(&lines[1], buf, room) : 0; }
u32 uart_app_room(void) { return lines[1].on ? lines[1].tsize - (lines[1].th - lines[1].tt) : 0; }
void uart_app_write(const u8 *d, u32 n) { write_line(&lines[1], d, n); }
int uart_app_idle(void) { return !lines[1].on || (lines[1].th == lines[1].tt && (inb((u16)(lines[1].base + 5)) & 0x40)); }

/* The modem's lines: the far side's (UART_LINE_CTS and the rest), and this side's two set. */
u32 uart_app_lines(void)
{
    const struct line *u = &lines[1];
    u8 msr;
    if (!u->on)
        return 0;
    msr = inb((u16)(u->base + 6));
    return (msr & 0x10 ? UART_LINE_CTS : 0) | (msr & 0x20 ? UART_LINE_DSR : 0) | (msr & 0x40 ? UART_LINE_RI : 0)
           | (msr & 0x80 ? UART_LINE_DCD : 0) | (u->mcr & 1 ? UART_LINE_DTR : 0) | (u->mcr & 2 ? UART_LINE_RTS : 0);
}

void uart_app_set(u32 dtr, u32 rts)             /* (RTS is the flow control's own while the line has it) */
{
    struct line *u = &lines[1];
    u32 f = irq_save();
    if (u->on) {
        u->mcr = (u8)((u->mcr & ~1u) | (dtr ? 1 : 0));
        if (u->flow != UART_FLOW_RTS)
            u->mcr = (u8)((u->mcr & ~2u) | (rts ? 2 : 0));
        outb((u16)(u->base + 4), u->mcr);
    }
    irq_restore(f);
}

void uart_app_break(int on)                     /* the line held at a space (a break), or let go */
{
    struct line *u = &lines[1];
    u32 f = irq_save();
    if (u->on) {
        u8 lcr = inb((u16)(u->base + 3));
        outb((u16)(u->base + 3), (u8)(on ? lcr | 0x40 : lcr & ~0x40));
    }
    irq_restore(f);
}
