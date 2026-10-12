/* A GLOS app's own devices (PRD §5.5, §6.5; docs/milestones-m6.md step B).
 * An app reaches no port: every IN and OUT traps (its thread runs with the
 * I/O map out of reach), and is answered here.
 *
 *   20h/21h, A0h/A1h   a virtual PIC pair of its own (kernel/vm/vpic.c), as
 *                      the system VM has one. Its lines: 0 from its timer,
 *                      13 from the FPU when the app is the one that made
 *                      the error. Its interrupts go to the handlers it
 *                      hooked in protected mode, at a return to its code
 *                      with its virtual interrupt flag on (app.c).
 *   40h, 43h           PIT channel 0, virtual. GLOS keeps the real one in
 *                      mode 2 with count FFFFh (supervisor.md §8.2), which
 *                      is what DJGPP's uclock() programs. While the app's
 *                      is the same (untouched, or programmed so), a read
 *                      is the real counter's, latched here, and its IRQ 0
 *                      comes when the real one does: coherent with the BIOS
 *                      tick at 0040:006C, which is the system VM's, and
 *                      with ticks the app counts itself. Programmed
 *                      otherwise, the count and IRQ 0 run off the kernel's
 *                      tick: right on average, in steps of about a
 *                      millisecond (1,165 counts), and no faster than 1024
 *                      interrupts a second.
 *   61h                bit 4 toggles at every read (delay loops watch it);
 *                      writes are dropped: no speaker for an app
 *   70h/71h            the RTC and CMOS, to read: the time from the chip,
 *                      registers A to D as the PC has them at rest
 *   F0h                the FPU's error latch: the kernel has cleared it
 *   the rest           reads FFh, writes dropped
 *
 * The real PIT is read only while no session runs: a session has its ports
 * to itself. While one does (apps run on beside a program's first half
 * second, session.c), a read is reckoned from the kernel's ticks since the
 * real timer last interrupted. Either way a count never goes back while the
 * BIOS's tick stays the same: uclock() pairs the two, and the tick moves
 * only when the system VM has run the BIOS's handler, a moment after the
 * counter began again. */
#include "appint.h"
#include "io.h"
#include "session.h"
#include "timer.h"
#include "vm.h"

#define PIT_HZ 1193182u

void appdev_init(struct app *a)
{
    vpic_reset(&a->pic, 0x08, 0x70, vpic_imr(&vm.pic));    /* the masks its program found */
    a->pit.mode = 2;
    a->pit.access = 3;
    a->pit.reload = 0xFFFF;
    a->pit.at = timer_ticks();
    a->rtc_index = 0x0D;
}

static u32 irq0_at;                             /* the tick the PC's timer last interrupted at */

void appdev_irq0_at(u32 tick) { irq0_at = tick; }

/* Its timer is the PC's, as GLOS keeps it: reads and IRQ 0 come from there. */
static int pit_real(const struct app *a) { return a->pit.reload == 0xFFFF || a->pit.reload == 0x10000; }

/* It wants its timer's interrupt: a handler for it, or for the BIOS's INT
   1Ch that follows from it. */
static int wants_irq0(const struct app *a)
{
    return a->ctx && (a->ctx->vidt[0x08].sel || a->ctx->vidt[0x1C].sel);
}

static void raise(struct app *a, int irq)
{
    vpic_raise(&a->pic, irq);
    a->nirq[irq]++;
    if (!thread_wake(&a->wq))                   /* (its HLT, its sleep) */
        sched_nudge(a->thread);
}

void appdev_irq0(struct app *a)
{
    if (pit_real(a) && wants_irq0(a))
        raise(a, 0);
}

/* By the kernel's clock, not by the calls: ticks the clock was given all at
   once (core/timer.c: lost while interrupts were off) count, and a pause
   (no calls) is cut to 60. */
void appdev_tick(struct app *a)
{
    u32 now = timer_ticks(), n = now - a->pit.at;
    a->pit.at = now;
    if (n > 60)
        n = 60;
    a->pit.acc += n * (PIT_HZ / 1024);
    a->pit.frac += n * (PIT_HZ % 1024);
    a->pit.acc += a->pit.frac >> 10;
    a->pit.frac &= 1023;
    if (a->pit.acc >= a->pit.reload) {          /* (periods that ended meanwhile: one interrupt for them all) */
        a->pit.acc %= a->pit.reload;
        if (!pit_real(a) && wants_irq0(a))
            raise(a, 0);
    }
}

/* The FPU reported an error while the app's code ran (IRQ 13, CR0.NE
   clear): its own line 13, the latch cleared as the BIOS's handler would. */
void app_fpu_irq(struct app *a)
{
    outb(0xF0, 0);
    raise(a, 13);
}

/* The counter now. */
static u16 pit_count(struct app *a)
{
    u32 c, tick;
    if (pit_real(a)) {
        if (!session_active() && !app_vm_psp()) {       /* (nor an app's own call under way: its child program) */
            u8 lo;
            outb(0x43, 0x00);                   /* latch channel 0 */
            lo = inb(0x40);
            c = lo | (u32)inb(0x40) << 8;
        } else {                                /* (the ports are a session's: from the tick) */
            c = (timer_ticks() - irq0_at) * (PIT_HZ / 1024);
            c = c < 0xFFFF ? 0xFFFF - c : 1;
        }
        tick = vm_rd32(0x46C);
        if (tick == a->pit.last_tick && c > a->pit.last)
            c = a->pit.last;                    /* (the counter is round, the BIOS's tick has yet to follow) */
        a->pit.last = (u16)c;
        a->pit.last_tick = tick;
        return (u16)c;
    }
    c = a->pit.acc;
    if (a->pit.mode == 3) {                     /* a square wave counts down twice a period, by twos */
        c *= 2;
        if (c >= a->pit.reload)
            c -= a->pit.reload;
        return (u16)((a->pit.reload - c) & ~1u);
    }
    return (u16)(a->pit.reload - c);
}

static u8 pit_read(struct app *a)
{
    u8 v;
    if (!a->pit.latched) {                      /* not latched: the count as it is, for both bytes */
        a->pit.latch = pit_count(a);
        a->pit.latched = a->pit.access == 3 ? 2 : 1;
        a->pit.rd_hi = a->pit.access == 2;
    }
    v = a->pit.rd_hi ? (u8)(a->pit.latch >> 8) : (u8)a->pit.latch;
    a->pit.rd_hi = !a->pit.rd_hi && a->pit.access == 3;
    a->pit.latched--;
    return v;
}

static void pit_load(struct app *a, u32 count)
{
    a->pit.reload = count ? count : 0x10000;
    a->pit.acc = a->pit.frac = 0;
    a->pit.at = timer_ticks();
    a->pit.own = 1;
}

static void pit_write(struct app *a, u8 v)
{
    switch (a->pit.access) {
    case 1:
        pit_load(a, v);
        break;
    case 2:
        pit_load(a, (u32)v << 8);
        break;
    default:
        if (!a->pit.wr_hi) {
            a->pit.wr_lo = v;
            a->pit.wr_hi = 1;
        } else {
            a->pit.wr_hi = 0;
            pit_load(a, a->pit.wr_lo | (u32)v << 8);
        }
    }
}

static void pit_control(struct app *a, u8 v)
{
    if (v >> 6)                                 /* channels 1 and 2, the read-back command: not an app's */
        return;
    if (!(v & 0x30)) {                          /* the counter latch */
        if (!a->pit.latched) {
            a->pit.latch = pit_count(a);
            a->pit.latched = a->pit.access == 3 ? 2 : 1;
            a->pit.rd_hi = a->pit.access == 2;
        }
        return;
    }
    a->pit.access = (v >> 4) & 3;
    a->pit.mode = ((v >> 1) & 3) == 3 ? 3 : 2;  /* (modes 0, 1, 4 and 5 count as mode 2) */
    a->pit.wr_hi = a->pit.rd_hi = 0;
    a->pit.latched = 0;
}

static u8 rtc_in(const struct app *a)
{
    switch (a->rtc_index) {
    case 0x0A: return (u8)((rtc_read(0x0A) & 0x80) | 0x26);    /* the update flag from the chip */
    case 0x0B: return 0x02;                     /* 24 hours, BCD, no interrupts */
    case 0x0C: return 0x00;
    case 0x0D: return 0x80;
    default: return rtc_read(a->rtc_index);
    }
}

static u8 in8(struct app *a, u16 port)
{
    switch (port) {
    case 0x20: case 0x21: case 0xA0: case 0xA1:
        return vpic_read(&a->pic, port);
    case 0x40:
        return pit_read(a);
    case 0x61:
        a->p61 ^= 0x10;
        return a->p61;
    case 0x71:
        return rtc_in(a);
    default:
        return 0xFF;
    }
}

static void out8(struct app *a, u16 port, u8 v)
{
    switch (port) {
    case 0x20: case 0x21: case 0xA0: case 0xA1:
        vpic_write(&a->pic, port, v);
        break;
    case 0x40:
        pit_write(a, v);
        break;
    case 0x43:
        pit_control(a, v);
        break;
    case 0x70:
        a->rtc_index = v & 0x7F;
        break;
    }
}

u32 app_in(struct app *a, u16 port, int width)
{
    u32 v = 0;
    int i;
    for (i = 0; i < width; i++)
        v |= (u32)in8(a, (u16)(port + i)) << (8 * i);
    return v;
}

void app_out(struct app *a, u16 port, u32 val, int width)
{
    int i;
    for (i = 0; i < width; i++)
        out8(a, (u16)(port + i), (u8)(val >> (8 * i)));
}

/* An interrupt of the app's that no handler of its own took, or that its
   handler passed on to the one it replaced (the host's): the BIOS's work
   for it, on the app's own PIC. Never real mode's handler: that one is the
   system VM's, with the system's PIC behind it. 1: the vector was one of
   its interrupts' (08h-0Fh, 70h-77h). */
int app_irq_default(struct app *a, u8 vec)
{
    int slave = vec >= 0x70 && vec < 0x78;
    if (!slave && !(vec >= 0x08 && vec < 0x10))
        return 0;
    if (slave)                                  /* the end of that interrupt, by its level (one that has */
        vpic_write(&a->pic, 0xA0, (u8)(0x60 | (vec & 7)));     /* ended already: nothing) */
    vpic_write(&a->pic, 0x20, (u8)(0x60 | (slave ? 2 : vec & 7)));
    return 1;
}
