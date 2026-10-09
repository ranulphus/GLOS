/* The kernel clock: the RTC periodic interrupt (IRQ 8), 1024 Hz. Its handler
 * reads register C every time: a real MC146818 stops interrupting until C is
 * read, 86Box does not (supervisor.md §8.1, V86TEST case Q). PIT channel 0 is
 * never touched; channel 2 times the measurement. */
#include "arch.h"
#include "io.h"
#include "sched.h"
#include "timer.h"

static volatile u32 ticks, spurious_c;
static u8 rtc_a, rtc_b;
static void (*tick_hook)(struct trapframe *tf, u8 c);

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
    u8 c = cmos(0x0C);
    if (c & 0x40) {                             /* PF: a periodic interrupt */
        ticks++;
        sched_tick();
    } else {
        spurious_c++;
    }
    if (tick_hook)
        tick_hook(tf, c);
}

void timer_set_hook(void (*fn)(struct trapframe *tf, u8 c)) { tick_hook = fn; }
u8 rtc_read(u8 reg) { return cmos(reg); }
void rtc_write(u8 reg, u8 v) { cmos_set(reg, v); }
u8 timer_found_a(void) { return rtc_a; }
u8 timer_found_b(void) { return rtc_b; }

/* Register B as the program wants it but with the kernel's PIE: only SET,
   SQWE, DM, 24/12 and DSE reach the chip (supervisor.md §10). */
void timer_write_b(u8 b) { cmos_set(0x0B, (u8)((b & 0x8F) | 0x40)); }

/* Direct mode (supervisor.md §9.7): protected-mode code at IOPL 3 reaches
   the RTC itself. When register A's rate or B's PIE is no longer the
   kernel's, the kernel's go back (B's other bits as the program left them)
   and 1 is returned, with what the program had written in *a and *b. */
int timer_reclaim(u8 *a, u8 *b)
{
    u8 ra = cmos(0x0A), rb = cmos(0x0B);
    if ((ra & 0x7F) == 0x26 && (rb & 0x40))
        return 0;
    *a = (u8)(ra & 0x7F);
    *b = rb;
    cmos_set(0x0A, 0x26);
    timer_write_b(rb);
    (void)cmos(0x0C);
    return 1;
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

void timer_stop(void) { timer_stop_to(rtc_a, rtc_b); }

void timer_stop_to(u8 a, u8 b)
{
    cmos_set(0x0B, b);
    cmos_set(0x0A, a);
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
