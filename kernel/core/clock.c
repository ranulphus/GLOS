/* The wall clock (docs/milestones-m10.md step A; PRD §11.3).
 *
 * GLOS keeps UTC: a time at a tick, run on by the tick (1024 a second).
 * At the start it is the RTC's, read as local time in [time] zone (UTC0
 * until GLOS.CFG has been read, then again in the zone it gives). SNTP
 * (lwIP's client, on the net thread: kernel/net/net.c) or `glos time set`
 * sets it; a step of a second or more is written to the RTC and to DOS,
 * both of which keep local time, as DOS always has: the RTC directly (its
 * updates held off with register B's SET bit, interrupts off so that the
 * tick's own reads of register C don't move the index), DOS through the
 * DOS server (INT 21h 2Bh and 2Dh; without one, in GLOS's other start
 * modes, the BIOS's count of ticks since midnight). When summer time
 * begins or ends, the RTC and DOS are set again. */
#include <string.h>

#include "glos/api.h"
#include "glos/tz.h"
#include "clock.h"
#include "dos.h"
#include "io.h"
#include "kprintf.h"
#include "timer.h"
#include "vm.h"

static struct {
    u32 base, at;                       /* UTC at tick `at` */
    struct tz z;
    char zone[48];
    int synced;                         /* set since GLOS started */
    char how[8];                        /* "rtc", "sntp", "hand" */
    char server[64];
    u32 set_tick;                       /* when last set */
    s32 step;                           /* the last step: milliseconds, or seconds if step_s */
    int step_s;
    long written_off;                   /* the zone's offset the RTC and DOS were last set in */
    int dos_due;                        /* DOS's date and time to be set again */
    u32 sets;                           /* times set (glos time sync waits for the next) */
    struct dos_req q;
    volatile u8 busy;
} k;

u16 clock_sntp_port = 123;
static u32 interval_h = 24;

/* ---- the RTC */

static u8 from_rtc(u8 v, int bcd) { return bcd ? (u8)((v >> 4) * 10 + (v & 15)) : v; }
static u8 to_rtc(u32 v, int bcd) { return bcd ? (u8)(((v / 10) << 4) | (v % 10)) : (u8)v; }

static int rtc_get(struct tz_tm *tm)
{
    u32 f, tries;
    u8 r[8], b, cent;
    for (tries = 0; tries < 100000; tries++) {
        f = irq_save();
        if (!(rtc_read(0x0A) & 0x80)) {         /* (no update under way: the next takes 244 us at least) */
            r[0] = rtc_read(0x00), r[2] = rtc_read(0x02), r[4] = rtc_read(0x04);
            r[7] = rtc_read(0x07), r[1] = rtc_read(0x08), r[3] = rtc_read(0x09);
            cent = rtc_read(0x32);
            b = rtc_read(0x0B);
            irq_restore(f);
            {
                int bcd = !(b & 0x04), h = from_rtc((u8)(r[4] & 0x7F), bcd), c = from_rtc(cent, bcd);
                if (!(b & 0x02))                /* (12-hour mode: bit 7 the afternoon) */
                    h = h % 12 + ((r[4] & 0x80) ? 12 : 0);
                tm->sec = from_rtc(r[0], bcd), tm->min = from_rtc(r[2], bcd), tm->hour = h;
                tm->day = from_rtc(r[7], bcd), tm->mon = from_rtc(r[1], bcd);
                tm->year = from_rtc(r[3], bcd) + (c >= 19 && c <= 21 ? c * 100 : from_rtc(r[3], bcd) < 80 ? 2000 : 1900);
            }
            return tm->mon >= 1 && tm->mon <= 12 && tm->day >= 1 && tm->day <= 31 && tm->year >= 1970 ? 0 : -1;
        }
        irq_restore(f);
    }
    return -1;
}

static void rtc_put(u32 local)
{
    struct tz_tm tm;
    u32 f = irq_save();
    u8 b = rtc_read(0x0B), cent = rtc_read(0x32);
    int bcd = !(b & 0x04);
    tz_split(local, &tm);
    rtc_write(0x0B, (u8)(b | 0x80));            /* SET: the chip's updates held off */
    rtc_write(0x00, to_rtc((u32)tm.sec, bcd));
    rtc_write(0x02, to_rtc((u32)tm.min, bcd));
    if (b & 0x02)
        rtc_write(0x04, to_rtc((u32)tm.hour, bcd));
    else
        rtc_write(0x04, (u8)(to_rtc((u32)(tm.hour % 12 ? tm.hour % 12 : 12), bcd) | (tm.hour >= 12 ? 0x80 : 0)));
    rtc_write(0x06, to_rtc((u32)tm.wday + 1, bcd));
    rtc_write(0x07, to_rtc((u32)tm.day, bcd));
    rtc_write(0x08, to_rtc((u32)tm.mon, bcd));
    rtc_write(0x09, to_rtc((u32)(tm.year % 100), bcd));
    if (from_rtc(cent, bcd) >= 19 && from_rtc(cent, bcd) <= 21)        /* (a century register: the IBM AT's 32h) */
        rtc_write(0x32, to_rtc((u32)(tm.year / 100), bcd));
    rtc_write(0x0B, b);
    irq_restore(f);
}

/* ---- now */

u32 clock_utc(void)
{
    u32 f = irq_save(), t = k.base + (timer_ticks() - k.at) / 1024;
    irq_restore(f);
    return t;
}

void clock_get_us(u32 *sec, u32 *us)
{
    u32 f = irq_save(), d = timer_ticks() - k.at;
    *sec = k.base + d / 1024;
    *us = (d % 1024) * 1000000u / 1024;
    irq_restore(f);
}

u32 clock_local(void)
{
    u32 t = clock_utc();
    return t + (u32)tz_offset(&k.z, t, 0);
}

int clock_synced(void) { return k.synced; }

int clock_how(void) { return !k.synced ? GLOS_TIME_RTC : !strcmp(k.how, "sntp") ? GLOS_TIME_SNTP : GLOS_TIME_HAND; }

long clock_offset(void) { return tz_offset(&k.z, clock_utc(), 0); }
u32 clock_sets(void) { return k.sets; }

/* Local time (a zone's) to UTC, for glos time set without a Z. */
u32 clock_from_local(u32 local) { return tz_local_to_utc(&k.z, local); }

/* ---- DOS's date and time */

static void dos_now(void)
{
    struct tz_tm tm;
    if (k.q.state == DR_QUEUED || k.q.state == DR_RUNNING) {
        k.dos_due = 1;                          /* (the DOS server is at it: again once it has done) */
        return;
    }
    k.dos_due = 0;
    tz_split(clock_local(), &tm);
    memset(&k.q, 0, sizeof k.q);
    k.q.n = 2;
    k.q.r[0].ax = 0x2B00, k.q.r[0].cx = (u16)tm.year, k.q.r[0].dx = (u16)(tm.mon << 8 | tm.day);
    k.q.r[1].ax = 0x2D00, k.q.r[1].cx = (u16)(tm.hour << 8 | tm.min), k.q.r[1].dx = (u16)(tm.sec << 8);
    if (dos_post(&k.q) != 0) {                  /* no DOS server (/RUN, /SHELL): the BIOS's ticks since midnight */
        u32 secs = (u32)(tm.hour * 3600 + tm.min * 60 + tm.sec), f;
        u32 n = secs * 18 + secs * 13533u / 65536u;     /* (1193182 / 65536 = 18.2065 a second) */
        f = irq_save();
        vm_wr8(0x46C, (u8)n), vm_wr8(0x46D, (u8)(n >> 8)), vm_wr8(0x46E, (u8)(n >> 16)), vm_wr8(0x46F, (u8)(n >> 24));
        irq_restore(f);
        k.q.state = DR_DONE;
    }
}

/* UTC is now t: the RTC and DOS told if it moved a second or more, or `force`. */
static void set_to(u32 t, u32 us, const char *how, int force)
{
    u32 f, old_us, old;
    s32 secs, step;
    int big;
    clock_get_us(&old, &old_us);
    secs = (s32)(t - old);
    big = secs > 2000000 || secs < -2000000;    /* (23 days: more, in milliseconds, is past 32 bits) */
    step = big ? secs : secs * 1000 + ((s32)us - (s32)old_us) / 1000;
    f = irq_save();
    k.base = t;
    k.at = timer_ticks() - us * 1024 / 1000000;
    irq_restore(f);
    k.synced = 1;
    k.sets++;
    strncpy(k.how, how, sizeof k.how - 1);
    k.set_tick = timer_ticks();
    k.step = step;
    k.step_s = big;
    if (force || big || step >= 1000 || step <= -1000) {
        struct tz_tm tm;
        u32 local = clock_local();
        rtc_put(local);
        k.written_off = tz_offset(&k.z, t, 0);
        dos_now();
        tz_split(local, &tm);
        kprintf("GLOS-TIME set by=%s step=%d%s local=%04d-%02d-%02dT%02d:%02d:%02d zone=%s\n", how, (int)step,
                big ? "s" : "ms", tm.year, tm.mon, tm.day, tm.hour, tm.min, tm.sec, k.zone);
    }
}

void clock_sntp(u32 sec, u32 us) { set_to(sec, us, "sntp", 0); }

int clock_set(u32 utc, const char *how)
{
    set_to(utc, 0, how, 1);
    return 0;
}

void clock_sntp_asked(const char *server) { strncpy(k.server, server, sizeof k.server - 1); }

u32 clock_sntp_interval(void) { return interval_h * 3600u * 1000u; }

void clock_set_interval(u32 hours) { interval_h = hours ? hours : 24; }

/* ---- the zone */

void clock_zone(const char *tz)
{
    struct tz z;
    if (tz_parse(tz, &z) != 0 || !strcmp(tz, k.zone))
        return;
    strncpy(k.zone, tz, sizeof k.zone - 1);
    k.z = z;
    if (k.synced) {                             /* UTC known: the RTC and DOS in the new zone */
        set_to(clock_utc(), 0, k.how, 1);
    } else {                                    /* the RTC's time is local time in the new zone */
        struct tz_tm tm;
        if (rtc_get(&tm) == 0) {
            u32 f = irq_save();
            k.base = tz_local_to_utc(&k.z, tz_join(&tm));
            k.at = timer_ticks();
            irq_restore(f);
        }
        k.written_off = tz_offset(&k.z, clock_utc(), 0);
    }
    kprintf("GLOS-TIME zone=%s\n", k.zone);
}

void clock_init(void)
{
    struct tz_tm tm;
    strcpy(k.zone, "UTC0");
    strcpy(k.how, "rtc");
    tz_parse(k.zone, &k.z);
    memset(&tm, 0, sizeof tm);
    if (rtc_get(&tm) == 0) {
        k.base = tz_join(&tm);
        k.at = timer_ticks();
    }
    kprintf("GLOS-TIME rtc=%04d-%02d-%02dT%02d:%02d:%02d\n", tm.year, tm.mon, tm.day, tm.hour, tm.min, tm.sec);
}

void clock_second(void)
{
    u32 f = irq_save();
    long off;
    if (k.busy) {
        irq_restore(f);
        return;
    }
    k.busy = 1;
    irq_restore(f);
    off = tz_offset(&k.z, clock_utc(), 0);
    if (off != k.written_off) {                 /* summer time began or ended */
        rtc_put(clock_local());
        k.written_off = off;
        dos_now();
        kprintf("GLOS-TIME offset=%d\n", (int)off);
    } else if (k.dos_due) {
        dos_now();                              /* (the DOS server was busy with the last: again) */
    }
    k.busy = 0;
}

u32 clock_text(char *buf, u32 max)
{
    struct tz_tm u, l, r;
    int dst, have_rtc = rtc_get(&r) == 0;
    u32 t = clock_utc(), n;
    long off = tz_offset(&k.z, t, &dst);
    tz_split(t, &u);
    tz_split(t + (u32)off, &l);
    n = (u32)ksnprintf(buf, max, "time utc=%04d-%02d-%02dT%02d:%02d:%02dZ local=%04d-%02d-%02dT%02d:%02d:%02d %s "
                       "zone=%s\n", u.year, u.mon, u.day, u.hour, u.min, u.sec, l.year, l.mon, l.day, l.hour, l.min,
                       l.sec, dst ? k.z.dst_name : k.z.std_name, k.zone);
    if (k.synced)
        n += (u32)ksnprintf(buf + n, max - n, "set by=%s%s%s ago=%us step=%d%s\n", k.how,
                            !strcmp(k.how, "sntp") ? " server=" : "", !strcmp(k.how, "sntp") ? k.server : "",
                            (timer_ticks() - k.set_tick) / 1024, (int)k.step, k.step_s ? "s" : "ms");
    else
        n += (u32)ksnprintf(buf + n, max - n, "set by=rtc\n");
    if (have_rtc)
        n += (u32)ksnprintf(buf + n, max - n, "rtc=%04d-%02d-%02dT%02d:%02d:%02d\n", r.year, r.mon, r.day, r.hour, r.min,
                            r.sec);
    return n;
}
