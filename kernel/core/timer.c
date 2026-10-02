/* The kernel clock: the RTC periodic interrupt (IRQ 8), 1024 Hz. Its handler
 * reads register C every time: a real MC146818 stops interrupting until C is
 * read, 86Box does not (supervisor.md §8.1, V86TEST case Q). PIT channel 0 is
 * never touched; channel 2 times the measurement. */
#include "arch.h"
#include "io.h"
#include "timer.h"

static volatile u32 ticks, spurious_c;
static u8 rtc_a, rtc_b;

void pic_init(u8 master_base, u8 slave_base, u16 mask)
{
    outb(0x20, 0x11); outb(0xA0, 0x11);         /* ICW1: edge, cascade, ICW4 follows */
    outb(0x21, master_base); outb(0xA1, slave_base);
    outb(0x21, 0x04); outb(0xA1, 0x02);         /* slave on IRQ 2 */
    outb(0x21, 0x01); outb(0xA1, 0x01);         /* 8086 mode, normal EOI */
    pic_set_mask(mask);
}

void pic_set_mask(u16 mask)
{
    outb(0x21, (u8)mask);
    outb(0xA1, (u8)(mask >> 8));
}

static u8 cmos(u8 reg) { outb(0x70, reg); return inb(0x71); }
static void cmos_set(u8 reg, u8 v) { outb(0x70, reg); outb(0x71, v); }

static void rtc_irq(struct trapframe *tf)
{
    (void)tf;
    if (cmos(0x0C) & 0x40)                      /* PF: a periodic interrupt */
        ticks++;
    else
        spurious_c++;
}

void timer_start(void)
{
    rtc_a = cmos(0x0A);
    rtc_b = cmos(0x0B);
    set_irq_handler(8, rtc_irq);
    cmos_set(0x0A, 0x26);                       /* 32.768 kHz base, rate 6: 1024 Hz */
    cmos_set(0x0B, (u8)(rtc_b | 0x40));         /* PIE */
    (void)cmos(0x0C);
}

void timer_stop(void)
{
    cmos_set(0x0B, rtc_b);
    cmos_set(0x0A, rtc_a);
    (void)cmos(0x0C);
    set_irq_handler(8, NULL);
}

u32 timer_ticks(void) { return ticks; }
u32 timer_spurious(void) { return spurious_c; }

void pit2_wait(void)
{
    u8 g = inb(0x61);
    u32 spin = 0;
    outb(0x61, (u8)((g & ~0x02) | 0x01));       /* gate on, speaker off */
    outb(0x43, 0xB0);                           /* channel 2, lo/hi, mode 0 */
    outb(0x42, 0xFF);
    outb(0x42, 0xFF);
    while (!(inb(0x61) & 0x20) && ++spin < 200000000u) ;
    outb(0x61, g);
}
