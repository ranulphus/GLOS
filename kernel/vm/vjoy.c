/* The virtual game port (docs/gamepad.md J1).
 *
 * While a joystick source is on (glos joy; USB pads later), or [joystick] port = virtual, port 201h is trapped
 * everywhere (the system VM, sessions, DPMI clients; apps through app/dev.c) and answered here, as the IBM Game
 * Control Adapter answers:
 *   a write          fires the four one-shots, one an axis: GLOS notes the time
 *   a read           bits 0-3: 1 while each axis's one-shot runs, 24 µs + RANGE µs * axis / 255 after the write
 *                    (RANGE 1100 by default: 24-1124 µs, a 0-100 kΩ stick); an axis with nothing connected never
 *                    times out (1 after a write, 0 before any). Bits 4-7: the buttons A1 A2 B1 B2, 0 while pressed
 *   INT 15h AH=84h   DX=0: the buttons in AL bits 4-7; DX=1: AX, BX, CX, DX the axes' one-shots in units of 4 µs
 *                    (0 for an axis with nothing connected)
 * Otherwise 201h is passed through as before (PRD §6.4, D48): the PC's own game port.
 *
 * The sources: glos joy's state as it is set (it overrides the pads while it is on), else the USB pads, mapped as
 * docs/gamepad.md says: one pad drives the whole port (its left stick joystick A's X and Y, its right stick B's,
 * buttons 1-4 A1, A2, B1, B2); with two, the second is joystick B (its left stick B's axes, its buttons 1 and 2 B1
 * and B2) and the first keeps A's.
 *
 * The time since the write, at each read:
 *   TSC              on a Pentium and later: its rate measured against the kernel's tick, from the first tick on
 *   PIT channel 0    without a TSC, while the PIT is as GLOS keeps it (mode 2, count FFFFh: its read-back status,
 *                    and IRQ 0 at 18.2 a second): latched in the program's own trapped access, never from an
 *                    interrupt, so a program part-way through its own reads of the PIT is never disturbed
 *   reads            otherwise (a 486 whose PIT a program has set): each read counts as long as a trapped read
 *                    took when the PIT could be read */
#include <string.h>
#include "glos/bootinfo.h"
#include "arch.h"
#include "io.h"
#include "idle.h"
#include "joy.h"
#include "kcfg.h"
#include "kprintf.h"
#include "timer.h"
#include "vm.h"

#define LONG_AGO_TICKS  64              /* a read this long after the write: every one-shot is over */

static struct {
    int cfg_port;                       /* 0 auto, 1 virtual, 2 off */
    u32 range_us;                       /* an axis at 255: 24 + range_us */
    int on;                             /* 201h trapped and answered here */
    int have[JOY_SOURCES];
    struct joy_state src[JOY_SOURCES], cur;
    int have_pad[JOY_PADS];
    struct joy_pad pad[JOY_PADS];
    /* the one-shots */
    int fired;
    u32 fire_tick, fire_tsc, fire_reads;
    u16 fire_pit;
    int how;                            /* the time source of the last write: 0 TSC, 1 PIT, 2 reads */
    u32 reads, writes;
    /* the time sources */
    int tsc;                            /* the CPU has one (-1 until the first tick) */
    u32 tsc_mult;                       /* µs = (cycles * tsc_mult) >> 32; 0 until measured */
    u32 tsc_mhz;
    u32 cal_tick, cal_irq0;             /* IRQ 0's rate: a window a second */
    u32 base_tick;                      /* the TSC's rate: measured from the first tick on, so that a tick late or */
    unsigned long long base_tsc;        /* caught up at a window's end costs less the longer GLOS runs */
    int pit_rate_ok;                    /* IRQ 0 came at 18.2 a second over the last second (GLOS set it so at
                                           the VM's start: true until a second says otherwise) */
    u32 us_per_read;                    /* a trapped read's cost, in 1/16 µs, measured while the PIT was readable */
    u32 last_read_us;
    int last_read_ok;
} j = { .range_us = 1100, .us_per_read = 16 * 15, .pit_rate_ok = 1, .tsc = -1 };

static const char *const how_name[] = { "tsc", "pit", "reads" };

/* The USB pads onto the port. */
static void from_pads(struct joy_state *s)
{
    const struct joy_pad *a = j.have_pad[0] ? &j.pad[0] : &j.pad[1], *b = j.have_pad[0] && j.have_pad[1] ? &j.pad[1] : 0;
    s->axis[0] = a->lx, s->axis[1] = a->ly;
    s->present = 0x3;
    if (b) {
        s->axis[2] = b->lx, s->axis[3] = b->ly;
        s->present |= 0xC;
        s->buttons = (u8)((a->buttons & 3) | (b->buttons & 3) << 2);
    } else {
        s->axis[2] = a->rx, s->axis[3] = a->ry;
        if (a->right)
            s->present |= 0xC;
        s->buttons = (u8)(a->buttons & 0xF);
    }
}

static void update(void)
{
    int i, any = 0, on;
    u32 f = irq_save();                         /* (a pad's report comes on the usb thread; reads on the VM's) */
    memset(&j.cur, 0, sizeof j.cur);
    if (j.have_pad[0] || j.have_pad[1]) {
        from_pads(&j.cur);
        any = 1;
    }
    for (i = 0; i < JOY_SOURCES; i++)
        if (j.have[i]) {
            j.cur = j.src[i];                   /* (glos joy: over the pads while it is on) */
            any = 1;
        }
    on = j.cfg_port == 1 || (j.cfg_port == 0 && any);
    irq_restore(f);
    if (on != j.on) {
        cpu_io_trap(0x201, on);
        j.on = on;
        j.fired = 0;
        kprintf("GLOS-JOY port=%s\n", on ? "virtual" : "real");
    }
}

void joy_config(void)
{
    char v[16];
    v[0] = 0;
    kcfg_get("joystick", "port", v, sizeof v);
    j.cfg_port = !strcmp(v, "virtual") ? 1 : !strcmp(v, "off") ? 2 : 0;
    v[0] = 0;
    kcfg_get("joystick", "range", v, sizeof v);
    if (v[0]) {
        u32 n = 0;
        const char *p;
        for (p = v; *p >= '0' && *p <= '9'; p++)
            n = n * 10 + (u32)(*p - '0');
        if (!*p && n >= 100 && n <= 10000)
            j.range_us = n - 24;
    }
    update();
}

void joy_source(int src, const struct joy_state *s)
{
    if (src < 0 || src >= JOY_SOURCES)
        return;
    j.have[src] = s != 0;
    if (s)
        j.src[src] = *s;
    update();
}

static void (*pad_hook)(int n, const struct joy_pad *p);

void joy_pad_hook(void (*fn)(int n, const struct joy_pad *p)) { pad_hook = fn; }

void joy_pad(int n, const struct joy_pad *p)
{
    if (n < 0 || n >= JOY_PADS)
        return;
    j.have_pad[n] = p != 0;
    if (p)
        j.pad[n] = *p;
    update();
    idle_touch();                               /* (somebody is there: kernel/wm/idle.c) */
    if (pad_hook)
        pad_hook(n, p);
}

int joy_pad_get(int n, struct joy_pad *out)
{
    u32 f = irq_save();
    int have = n >= 0 && n < JOY_PADS && j.have_pad[n];
    if (have)
        *out = j.pad[n];
    irq_restore(f);
    return have;
}

void joy_info(int *on, int *mode, u32 *range_us, u32 *pads)
{
    *on = j.on;
    *mode = j.cfg_port;
    *range_us = j.range_us;
    *pads = (j.have_pad[0] ? 1u : 0u) | (j.have_pad[1] ? 2u : 0u);
}

int joy_virtual(void) { return j.on; }

/* ---- time */

/* (edx:eax) / d with the quotient in 32 bits. */
static u32 div64_32(u32 hi, u32 lo, u32 d)
{
    u32 q, r;
    __asm__("divl %4" : "=a"(q), "=d"(r) : "a"(lo), "d"(hi), "rm"(d));
    (void)r;
    return q;
}

static u16 pit_latch(void)
{
    u8 lo;
    outb(0x43, 0x00);
    lo = inb(0x40);
    return (u16)(lo | inb(0x40) << 8);
}

static int pit_ours(void)
{
    if (!j.pit_rate_ok)
        return 0;
    outb(0x43, 0xE2);                           /* read-back: channel 0's status */
    return (inb(0x40) & 0x3F) == 0x34;          /* both bytes, mode 2, binary */
}

/* Microseconds since the write; 0xFFFFFFFF long ago. */
static u32 since_fire(int counting_read)
{
    if (timer_ticks() - j.fire_tick > LONG_AGO_TICKS)
        return 0xFFFFFFFFu;
    switch (j.how) {
    case 0: {
        u32 d = (u32)rdtsc() - j.fire_tsc;
        return (u32)(((unsigned long long)d * j.tsc_mult) >> 32);
    }
    case 1: {
        u32 c = (u16)(j.fire_pit - pit_latch());              /* mode 2 counts down from FFFFh: 0.838 µs a count */
        return c * 838u / 1000u;
    }
    default:
        if (counting_read)
            j.fire_reads++;
        return j.fire_reads * j.us_per_read / 16;
    }
}

void joy_out(void)
{
    j.writes++;
    j.fired = 1;
    j.fire_tick = timer_ticks();
    j.fire_reads = 0;
    j.last_read_ok = 0;
    if (j.tsc && j.tsc_mult) {
        j.how = 0;
        j.fire_tsc = (u32)rdtsc();
    } else if (pit_ours()) {
        j.how = 1;
        j.fire_pit = pit_latch();
    } else {
        j.how = 2;
    }
}

static u32 axis_us(int i) { return 24 + j.cur.axis[i] * j.range_us / 255; }

u8 joy_in(void)
{
    u8 v = (u8)(0xF0 & ~(j.cur.buttons << 4));
    int i;
    j.reads++;
    if (j.fired) {
        u32 us = since_fire(1);
        if (j.how == 1 && us != 0xFFFFFFFFu) { /* the cost of a trapped read, for when the PIT is a program's */
            if (j.last_read_ok && us > j.last_read_us && us - j.last_read_us < 200)
                j.us_per_read = (j.us_per_read * 15 + (us - j.last_read_us) * 16 + 8) / 16;
            j.last_read_us = us;
            j.last_read_ok = 1;
        }
        for (i = 0; i < 4; i++)
            if (!(j.cur.present >> i & 1) || us < axis_us(i))
                v |= (u8)(1u << i);
    }
    return v;
}

/* ---- INT 15h AH=84h, the BIOS's joystick support */

int joy_int15(struct trapframe *tf, u32 ret_ip)
{
    u32 a[4];
    int i;
    if (!j.on)
        return 0;
    if ((u16)tf->edx == 0) {
        tf->eax = (tf->eax & 0xFFFFFF00u) | (u8)(0xF0 & ~(j.cur.buttons << 4));
    } else if ((u16)tf->edx == 1) {
        for (i = 0; i < 4; i++)
            a[i] = j.cur.present >> i & 1 ? axis_us(i) / 4 : 0;
        tf->eax = (tf->eax & 0xFFFF0000u) | a[0];
        tf->ebx = (tf->ebx & 0xFFFF0000u) | a[1];
        tf->ecx = (tf->ecx & 0xFFFF0000u) | a[2];
        tf->edx = (tf->edx & 0xFFFF0000u) | a[3];
    } else {
        tf->eflags |= FL_CF;
        tf->eip = ret_ip;
        return 1;
    }
    tf->eflags &= ~FL_CF;
    tf->eip = ret_ip;
    return 1;
}

/* ---- every kernel tick: once a second, the TSC's rate (since the first tick) and IRQ 0's (over the second); no
   port is touched here */

void joy_tick(u32 now)
{
    u32 irq0, n, span;
    if (j.tsc < 0) {                            /* the first tick: the windows start here */
        j.tsc = vm.bi && (vm.bi->cpuid_edx & 0x10);
        j.cal_tick = j.base_tick = now;
        j.base_tsc = j.tsc ? rdtsc() : 0;
        j.cal_irq0 = cpu_irq_count(0);
        return;
    }
    if (now - j.cal_tick < 1024)
        return;
    irq0 = cpu_irq_count(0);
    n = irq0 - j.cal_irq0;
    span = now - j.cal_tick;                    /* (1024 a second) */
    j.pit_rate_ok = n * 1024 >= span * 17 && n * 1024 <= span * 20;
    j.cal_tick = now;
    j.cal_irq0 = irq0;
    if (j.tsc) {                                /* cycles * 1024 / ticks since the first tick: the quotient is */
        unsigned long long d = rdtsc() - j.base_tsc;  /* below 2^32 for a TSC slower than 4 GHz */
        u32 hi = (u32)(d >> 22), lo = (u32)(d << 10), per_sec;
        span = now - j.base_tick;
        per_sec = hi < span ? div64_32(hi, lo, span) : 0;
        if (per_sec > 1000000u) {
            j.tsc_mult = div64_32(1000000u, 0, per_sec);  /* 2^32 * 10^6 / per_sec */
            j.tsc_mhz = (per_sec + 500000u) / 1000000u;
        }
    }
}

/* ---- glos joy */

int joy_stat(char *b, u32 room)
{
    char ax[4][4];
    int i;
    for (i = 0; i < 4; i++)
        if (j.cur.present >> i & 1)
            ksnprintf(ax[i], sizeof ax[i], "%u", j.cur.axis[i]);
        else
            strcpy(ax[i], "-");
    return ksnprintf(b, room, "joy port=%s source=%s a=%s,%s b=%s,%s buttons=%u%u%u%u range=%u time=%s mhz=%u "
                     "read_us=%u.%u writes=%u reads=%u\n", j.on ? "virtual" : "real",
                     j.have[JOY_SRC_SSH] ? "ssh" : j.have_pad[0] || j.have_pad[1] ? "usb" : "none", ax[0], ax[1],
                     ax[2], ax[3], j.cur.buttons & 1,
                     j.cur.buttons >> 1 & 1, j.cur.buttons >> 2 & 1, j.cur.buttons >> 3 & 1, 24 + j.range_us,
                     j.writes ? how_name[j.how] : (j.tsc && j.tsc_mult ? "tsc" : j.pit_rate_ok ? "pit" : "reads"),
                     j.tsc_mhz, j.us_per_read / 16, (j.us_per_read % 16) * 10 / 16, j.writes, j.reads);
}

static int pair(const char **p, u8 *x, u8 *y, int *present)
{
    u32 a = 0, b = 0;
    const char *q = *p;
    if (!strncmp(q, "none", 4)) {
        *p = q + 4;
        *present = 0;
        return 0;
    }
    if (*q < '0' || *q > '9')
        return -1;
    while (*q >= '0' && *q <= '9')
        a = a * 10 + (u32)(*q++ - '0');
    if (*q++ != ',' || *q < '0' || *q > '9')
        return -1;
    while (*q >= '0' && *q <= '9')
        b = b * 10 + (u32)(*q++ - '0');
    if (a > 255 || b > 255)
        return -1;
    *x = (u8)a;
    *y = (u8)b;
    *present = 1;
    *p = q;
    return 0;
}

int joy_command(const char *args, char *why, u32 room)
{
    struct joy_state s = j.have[JOY_SRC_SSH] ? j.src[JOY_SRC_SSH]
                                              : (struct joy_state){ { 128, 128, 128, 128 }, 0x3, 0 };
    const char *p = args;
    int i, present;
    while (*p == ' ')
        p++;
    if (!strcmp(p, "off")) {
        joy_source(JOY_SRC_SSH, 0);
        return 0;
    }
    if (!strncmp(p, "pad=", 4)) {               /* a pad as USB would give it (for tests: Loop A has no USB pads): */
        struct joy_pad jp;                      /* pad=N,LX,LY,RX,RY,BUTTONS (buttons in hex) or pad=N,off */
        u32 v[6] = { 0 }, n = 0;
        p += 4;
        if (*p >= '0' && *p < '0' + JOY_PADS && !strcmp(p + 1, ",off")) {
            joy_pad(*p - '0', 0);
            return 0;
        }
        while (n < 6) {
            u32 x = 0, digits = 0;
            for (; (*p >= '0' && *p <= '9') || (n == 5 && ((*p | 32) >= 'a' && (*p | 32) <= 'f')); p++, digits++)
                x = x * (n == 5 ? 16 : 10) + (u32)(*p <= '9' ? *p - '0' : (*p | 32) - 'a' + 10);
            if (!digits)
                break;
            v[n++] = x;
            if (*p != ',')
                break;
            p++;
        }
        if (n != 6 || *p || v[0] >= JOY_PADS || v[1] > 255 || v[2] > 255 || v[3] > 255 || v[4] > 255 || v[5] > 0xFFFF) {
            ksnprintf(why, room, "pad= takes N,LX,LY,RX,RY,BUTTONS (0-1, four of 0-255, hex) or N,off");
            return -1;
        }
        jp.lx = (u8)v[1], jp.ly = (u8)v[2], jp.rx = (u8)v[3], jp.ry = (u8)v[4];
        jp.right = 1;
        jp.buttons = (u16)v[5];
        joy_pad((int)v[0], &jp);
        return 0;
    }
    while (*p) {
        if ((p[0] == 'a' || p[0] == 'b') && p[1] == '=') {
            int k = p[0] == 'a' ? 0 : 2;
            p += 2;
            if (pair(&p, &s.axis[k], &s.axis[k + 1], &present) != 0) {
                ksnprintf(why, room, "%c= takes X,Y (0-255) or none", k ? 'b' : 'a');
                return -1;
            }
            s.present = (u8)(present ? s.present | 3u << k : s.present & ~(3u << k));
        } else if (!strncmp(p, "buttons=", 8)) {
            p += 8;
            for (i = 0; i < 4; i++, p++) {
                if (*p != '0' && *p != '1') {
                    ksnprintf(why, room, "buttons= takes four of 0 and 1 (A1 A2 B1 B2, 1 pressed)");
                    return -1;
                }
                s.buttons = (u8)(*p == '1' ? s.buttons | 1u << i : s.buttons & ~(1u << i));
            }
        } else {
            ksnprintf(why, room, "usage: glos joy [a=X,Y|none] [b=X,Y|none] [buttons=NNNN] | off");
            return -1;
        }
        if (*p && *p != ' ') {
            ksnprintf(why, room, "usage: glos joy [a=X,Y|none] [b=X,Y|none] [buttons=NNNN] | off");
            return -1;
        }
        while (*p == ' ')
            p++;
    }
    joy_source(JOY_SRC_SSH, &s);
    return 0;
}
