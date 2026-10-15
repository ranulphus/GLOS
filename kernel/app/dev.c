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
 *   60h, 64h           the keyboard, with keys only while a window of the
 *                      app's has the focus (the desktop gives them:
 *                      appdev_key): scan codes one at a time at 60h, each
 *                      with IRQ 1 if the app has a handler for it. A key
 *                      nobody's handler took, or that its handler passed
 *                      on, goes into a buffer as the BIOS's would, for
 *                      INT 16h (answered here: appdev_int16), and INT 33h
 *                      is the mouse in the app's window (appdev_int33)
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
#include "kprintf.h"
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
static void kb_feed(struct app *a);

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
    if (!a->kb_full && a->kb_qh != a->kb_qt)    /* the keyboard's next byte */
        kb_feed(a);
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

/* ---- the keyboard and the mouse, as a DOS program reads them */

/* A key as the BIOS keeps it (scan code, character): into the buffer INT 16h reads. */
static void cook(struct app *a, u16 key)
{
    if (key && a->kb_kt - a->kb_kh < ARRAY_SIZE(a->kb_keys)) {
        a->kb_keys[a->kb_kt++ % ARRAY_SIZE(a->kb_keys)] = key;
        thread_wake(&a->wq);
    }
}

/* The next scan code into port 60h, with IRQ 1, for an app that has a
   handler for it: when the last has been read and its interrupt has ended.
   Not before: a handler reads the port and passes the interrupt on to the
   one it replaced, which reads the port again and must find the same byte
   (DJGPP's own does, looking for Ctrl-C, and the BIOS's keeps the key).
   For an app with no handler the keys go straight to the BIOS's buffer, as
   the BIOS's own handler would put them. */
static void kb_feed(struct app *a)
{
    u32 n = ARRAY_SIZE(a->kb_q);
    if (!(a->ctx && a->ctx->vidt[0x09].sel)) {
        while (a->kb_qh != a->kb_qt) {
            a->kb_out = a->kb_q[a->kb_qh % n].byte;
            cook(a, a->kb_q[a->kb_qh++ % n].key);
        }
        a->kb_full = 0;
        return;
    }
    if (a->kb_full || a->kb_qh == a->kb_qt || (a->pic.p[0].isr & 2))
        return;
    a->kb_out = a->kb_q[a->kb_qh % n].byte;
    a->kb_out_key = a->kb_q[a->kb_qh++ % n].key;
    a->kb_full = 1;
    raise(a, 1);
}

/* A key that went down as the BIOS would keep it: its scan code and its
   character (E0h for the grey keys' none); 0 for the keys it keeps
   nothing of (the shifts, the locks). The function keys' codes change with
   a shift, as the BIOS has them. */
static u16 bios_key(u32 key, u32 ch, u32 mods)
{
    u32 scan = key & 0x7F;
    switch (key) {
    case 0x2A: case 0x36: case 0x1D: case 0x11D: case 0x38: case 0x138: case 0x3A: case 0x45: case 0x46:
        return 0;
    }
    if (key >= 0x3B && key <= 0x44)
        scan += mods & GLOS_MOD_ALT ? 0x2D : mods & GLOS_MOD_CTRL ? 0x23 : mods & GLOS_MOD_SHIFT ? 0x19 : 0;
    return (u16)(scan << 8 | ((key & 0x100) && !ch ? 0xE0 : ch & 0xFF));
}

/* The desktop: a key, for the app whose window has the focus. */
void appdev_key(struct app *a, u32 key, int down, u32 ch, u32 mods)
{
    u32 f = irq_save(), n = ARRAY_SIZE(a->kb_q);
    a->kb_shift = (u8)((mods & GLOS_MOD_SHIFT ? 2 : 0) | (mods & GLOS_MOD_CTRL ? 4 : 0) | (mods & GLOS_MOD_ALT ? 8 : 0));
    if (n - (a->kb_qt - a->kb_qh) >= 2) {
        if (key & 0x100) {
            a->kb_q[a->kb_qt % n].byte = 0xE0;
            a->kb_q[a->kb_qt++ % n].key = 0;
        }
        a->kb_q[a->kb_qt % n].byte = (u8)((key & 0x7F) | (down ? 0 : 0x80));
        a->kb_q[a->kb_qt++ % n].key = down ? bios_key(key, ch, mods) : 0;
    }
    kb_feed(a);
    irq_restore(f);
}

void appdev_mouse(struct app *a, s32 x, s32 y, s32 dx, s32 dy, u32 buttons)
{
    a->ms_x = x;
    a->ms_y = y;
    a->ms_dx += dx;
    a->ms_dy += dy;
    a->ms_buttons = (u8)buttons;
}

/* A key as INT 16h's old functions give it (00h, 01h): the grey keys as
   the number pad's, with no character. */
static u16 old(u32 ah, u16 k) { return ah < 0x10 && (k & 0xFF) == 0xE0 && (k >> 8) ? (u16)(k & 0xFF00) : k; }

/* INT 16h from the app (reflected, or through 0300h): its own keys. 1:
   answered here. The wait for a key (00h, 10h) ends early for an interrupt
   of the app's, and the INT is made again after the handler. */
int appdev_int16(struct app *a, struct trapframe *from, struct rmregs *r)
{
    u32 ah = (r->eax >> 8) & 0xFF;
    u16 k;
    switch (ah) {
    case 0x00: case 0x10:
        while (a->kb_kh == a->kb_kt) {
            if (app_sleep(a, 1024)) {           /* (an interrupt, or a key: look again) */
                if (a->kb_kh != a->kb_kt)
                    break;
                if (from && (from->cs & 0xFFFF) != SEL_TRAMP)
                    from->eip -= 2;
                return 1;
            }
        }
        k = a->kb_keys[a->kb_kh++ % ARRAY_SIZE(a->kb_keys)];
        r->eax = (r->eax & ~0xFFFFu) | old(ah, k);
        return 1;
    case 0x01: case 0x11:
        if (a->kb_kh == a->kb_kt) {
            r->flags |= FL_ZF;
        } else {
            r->flags &= (u16)~FL_ZF;
            r->eax = (r->eax & ~0xFFFFu) | old(ah, a->kb_keys[a->kb_kh % ARRAY_SIZE(a->kb_keys)]);
        }
        return 1;
    case 0x02: case 0x12:
        r->eax = (r->eax & ~0xFFu) | a->kb_shift;
        return 1;
    case 0x05:                                  /* a key put into the buffer */
        cook(a, (u16)r->ecx);
        r->eax &= ~0xFFu;
        return 1;
    default:
        return 1;                               /* (the rate, the LEDs: not an app's to set) */
    }
}

/* INT 33h from the app: the mouse in its window. 1: answered here. */
int appdev_int33(struct app *a, struct rmregs *r)
{
    u32 ax = r->eax & 0xFFFF;
    switch (ax) {
    case 0x00: case 0x21:                       /* is there a mouse: yes, with two buttons */
        r->eax = (r->eax & ~0xFFFFu) | 0xFFFF;
        r->ebx = (r->ebx & ~0xFFFFu) | 2;
        a->ms_dx = a->ms_dy = 0;
        break;
    case 0x03:                                  /* where, and the buttons */
        r->ebx = (r->ebx & ~0xFFFFu) | a->ms_buttons;
        r->ecx = (r->ecx & ~0xFFFFu) | (u16)a->ms_x;
        r->edx = (r->edx & ~0xFFFFu) | (u16)a->ms_y;
        break;
    case 0x0B:                                  /* how far it moved since last asked */
        r->ecx = (r->ecx & ~0xFFFFu) | (u16)a->ms_dx;
        r->edx = (r->edx & ~0xFFFFu) | (u16)a->ms_dy;
        a->ms_dx = a->ms_dy = 0;
        break;
    case 0x24:                                  /* the driver: version 8.0, a PS/2 mouse */
        r->ebx = (r->ebx & ~0xFFFFu) | 0x0800;
        r->ecx = (r->ecx & ~0xFFFFu) | 0x0400;
        break;
    }
    return 1;                                   /* (the rest: showing it, its range, handlers: nothing to do) */
}

/* COM1 (M8): a UART that is always ready to send, whose lines go into GLOS's
   log as they are, so what an app writes there (DOS-GL's DGL- lines, a test's
   HX- lines) is seen as a session's is; it receives nothing. */
static void com_out(struct app *a, u16 port, u8 v)
{
    switch (port - 0x3F8) {
    case 0:
        if (a->com.lcr & 0x80)                  /* (the divisor: no matter) */
            break;
        if (v == '\n' || a->com.len == sizeof a->com.line - 1) {
            a->com.line[a->com.len] = 0;
            kprintf("%s\n", a->com.line);
            a->com.len = 0;
        }
        if (v != '\n' && v != '\r')
            a->com.line[a->com.len++] = (char)v;
        break;
    case 1: if (!(a->com.lcr & 0x80)) a->com.ier = v; break;
    case 3: a->com.lcr = v; break;
    case 4: a->com.mcr = v; break;
    case 7: a->com.scr = v; break;
    }
}

static u8 com_in(struct app *a, u16 port)
{
    switch (port - 0x3F8) {
    case 1: return a->com.lcr & 0x80 ? 0 : a->com.ier;
    case 2: return 0x01;                        /* no interrupt waits */
    case 3: return a->com.lcr;
    case 4: return a->com.mcr;
    case 5: return 0x60;                        /* the transmitter empty: ready */
    case 6: return 0xB0;                        /* carrier, DSR, CTS */
    case 7: return a->com.scr;
    default: return 0;
    }
}

static u8 in8(struct app *a, u16 port)
{
    if (port >= 0x3F8 && port <= 0x3FF)
        return com_in(a, port);
    switch (port) {
    case 0x201:                                 /* the gameport: the PC's own (M8: SDL's joystick) */
        return inb(0x201);
    case 0x3DA:                                 /* the VGA's status (M8): a retrace a millisecond long 70 times
                                                   a second, so a program's wait for one ends as on a card (each
                                                   read here is a trap: millions of them took minutes) */
        return timer_ticks() % 15 == 0 ? 0x09 : 0x00;
    case 0x20: case 0x21: case 0xA0: case 0xA1:
        return vpic_read(&a->pic, port);
    case 0x40:
        return pit_read(a);
    case 0x60:                                  /* the scan code; the same again until the next has come, */
        if (!a->gui)                            /* which is at a tick after this one's interrupt has ended */
            return 0xFF;
        a->kb_full = 0;
        return a->kb_out;
    case 0x64:                                  /* a byte waits; the self-test passed, the keyboard is not locked */
        return a->gui ? (u8)(0x14 | a->kb_full) : 0xFF;
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
    if (port >= 0x3F8 && port <= 0x3FF) {
        com_out(a, port, v);
        return;
    }
    switch (port) {
    case 0x201:
        outb(0x201, v);
        break;
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
    if (vec == 0x09) {                          /* the keyboard's: the BIOS takes the byte and keeps the key */
        cook(a, a->kb_out_key);
        a->kb_out_key = 0;
        a->kb_full = 0;
        kb_feed(a);
    }
    return 1;
}
