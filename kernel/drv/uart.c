/* A PC's serial port as a line for bytes (uart.h): an 8250, 16450 or 16550A, by interrupts. The 16550A's FIFOs
 * are used where the chip has them (a byte every 87 microseconds at 115,200 baud: without them the kernel has
 * to answer each one, and a chip that loses bytes says so in `overruns`). */
#include <string.h>

#include "arch.h"
#include "io.h"
#include "kprintf.h"
#include "uart.h"
#include "vm.h"

#define RX 8192u
#define TX 32768u                               /* (a TCP connection's whole send buffer, escaped: slip.c) */

static struct {
    u16 base;
    u8 irq, on, fifo, wake_on;
    u32 baud;
    void (*kick)(void);
    u8 was_ier, was_lcr, was_mcr, was_dll, was_dlm;
    volatile u32 rh;                            /* (the interrupt's) */
    u32 rt;
    u32 th;
    volatile u32 tt;                            /* (the interrupt's) */
    struct uart_counts n;
    u8 rx[RX], tx[TX];
} u;

/* The chip's transmitter fed (interrupts off): its FIFO's 16 bytes, or one; it interrupts again when it is
   empty, while there is more. */
static void feed(void)
{
    int n = u.fifo ? 16 : 1;
    if (inb((u16)(u.base + 5)) & 0x20)
        for (; n && u.tt != u.th; n--, u.n.tx++)
            outb(u.base, u.tx[u.tt++ & (TX - 1)]);
    outb((u16)(u.base + 1), u.tt != u.th ? 0x07 : 0x05);
}

static void irq(struct trapframe *tf)
{
    int turns, wake = 0;
    (void)tf;
    for (turns = 0; turns < 256; turns++) {
        u8 lsr;
        if (inb((u16)(u.base + 2)) & 1)         /* nothing pending */
            break;
        lsr = inb((u16)(u.base + 5));
        while (lsr & 0x01) {
            u8 c = inb(u.base);
            if (lsr & 0x02)
                u.n.overruns++;
            if (lsr & 0x1C)
                u.n.errors++;
            if (u.rh - u.rt < RX)
                u.rx[u.rh++ & (RX - 1)] = c, u.n.rx++;
            else
                u.n.dropped++;
            if (c == u.wake_on)
                wake = 1;
            lsr = inb((u16)(u.base + 5));
        }
        if (lsr & 0x02)
            u.n.overruns++;
        if (lsr & 0x20)
            feed();
        inb((u16)(u.base + 6));                 /* (the modem's lines: a change is nothing here) */
    }
    if ((wake || u.rh - u.rt > RX / 2) && u.kick)
        u.kick();
}

int uart_open(u32 com, u32 baud, u8 wake_on, void (*kick)(void))
{
    static const u16 usual[4] = { 0x3F8, 0x2F8, 0x3E8, 0x2E8 };
    u32 f, div;
    u16 base;
    if (u.on)
        return -2;
    if (com < 1 || com > 4)
        return -1;
    if (!baud || baud > 115200 || 115200 % baud)
        return -3;
    div = 115200 / baud;
    base = (u16)(vm_rd8(0x400 + 2 * (com - 1)) | (u16)vm_rd8(0x401 + 2 * (com - 1)) << 8);     /* the BIOS's */
    if (base < 0x100 || base > 0x3F8 || (base & 7))
        base = usual[com - 1];
    f = irq_save();
    u.was_lcr = inb((u16)(base + 3));
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
    u.was_ier = inb((u16)(base + 1));
    u.was_mcr = inb((u16)(base + 4));
    outb((u16)(base + 1), 0);
    outb((u16)(base + 3), 0x83);
    u.was_dll = inb(base), u.was_dlm = inb((u16)(base + 1));
    outb(base, (u8)div);
    outb((u16)(base + 1), (u8)(div >> 8));
    outb((u16)(base + 3), 0x03);                /* 8 bits, no parity, one stop bit */
    outb((u16)(base + 2), 0x07);                /* FIFOs on and emptied ... */
    outb((u16)(base + 2), 0x47);                /* ... an interrupt at 4 bytes, or when the line goes quiet (at 8
                                                   the 486 bench lost a byte in 30,000 at 115,200 baud: 12 more
                                                   bytes' time, a millisecond, is the kernel's to answer in) */
    u.fifo = (inb((u16)(base + 2)) & 0xC0) == 0xC0;     /* (a 16550A says so; an 8250 or 16450 ignores it) */
    if (!u.fifo)
        outb((u16)(base + 2), 0);
    inb((u16)(base + 5)), inb(base), inb((u16)(base + 2)), inb((u16)(base + 6));
    u.base = base, u.irq = (u8)(com & 1 ? 4 : 3), u.baud = baud, u.wake_on = wake_on, u.kick = kick;
    u.rh = u.rt = u.th = u.tt = 0;
    memset(&u.n, 0, sizeof u.n);
    u.on = 1;
    if (base == COM1)
        kprintf_com1_off = 1;                   /* (the log stays in memory: glos log) */
    vdev_hide(base, 8);
    vm_claim_irq(u.irq, irq);
    outb((u16)(base + 4), 0x0B);                /* DTR, RTS, and OUT2, which lets the chip's interrupt through */
    outb((u16)(base + 1), 0x05);                /* received bytes, and the line's status */
    irq_restore(f);
    return 0;
}

void uart_close(void)
{
    u32 f = irq_save();
    if (u.on) {
        outb((u16)(u.base + 1), 0);
        vm_unclaim_irq(u.irq);
        outb((u16)(u.base + 2), 0);
        outb((u16)(u.base + 3), 0x83);
        outb(u.base, u.was_dll);
        outb((u16)(u.base + 1), u.was_dlm);
        outb((u16)(u.base + 3), u.was_lcr);
        outb((u16)(u.base + 4), u.was_mcr);
        outb((u16)(u.base + 1), u.was_ier);
        vdev_unhide(u.base);
        u.on = 0;
        if (u.base == COM1) {
            serial_init(COM1);                  /* (as the kernel's log has it) */
            kprintf_com1_off = 0;
        }
    }
    irq_restore(f);
}

int uart_is_open(u16 port) { return u.on && port >= u.base && port < u.base + 8; }

u32 uart_read(u8 *buf, u32 room)
{
    u32 n = 0;
    while (n < room && u.rt != u.rh)
        buf[n++] = u.rx[u.rt++ & (RX - 1)];
    return n;
}

u32 uart_room(void) { return u.on ? TX - (u.th - u.tt) : 0; }

void uart_write(const u8 *d, u32 n)
{
    u32 f;
    if (!u.on)
        return;
    for (; n && u.th - u.tt < TX; n--)
        u.tx[u.th++ & (TX - 1)] = *d++;
    f = irq_save();
    feed();
    irq_restore(f);
}

int uart_info(u16 *base, u32 *irq_line, u32 *baud, int *fifo, struct uart_counts *c)
{
    if (!u.on)
        return -1;
    *base = u.base, *irq_line = u.irq, *baud = u.baud, *fifo = u.fifo, *c = u.n;
    return 0;
}
