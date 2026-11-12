/* JOYTEST (docs/gamepad.md J1): what a DOS program sees of the game port at 201h, three ways at once.
 *     JOYTEST [TIMES] [app]     TIMES readings (5 by default), the median of each reported; app: the DJGPP build
 *                               becomes a GLOS app first (glos_init), so the apps' path is the one read
 *     JOYTEST TIMES pit N       PIT channel 0 set to mode 2 with count N first, as a game sets it, and two seconds
 *                               waited (GLOS checks IRQ 0's rate once a second); its time then reckoned with that
 *                               count; mode 2, count FFFFh again at the end, as GLOS keeps it
 * Each reading writes 201h, then reads it until every axis bit has dropped (or 20 ms have gone by), noting for
 * each axis the reads it took (as most games count) and the time by PIT channel 0, which GLOS keeps in mode 2
 * with count FFFFh (0.838 µs a count); then INT 15h AH=84h, the BIOS's joystick support. One line on COM1 and
 * stdout:
 *     HX-JOY buttons=NNNN count=A,B,C,D us=A,B,C,D bios=CF:BTN:A,B,C,D reads=N
 * buttons: A1 A2 B1 B2 as read at 201h (1 pressed); an axis whose bit never dropped is "-"; bios: CF, AL's
 * buttons (1 pressed) and the four axes, or "cf" alone when the BIOS says there is no game port.
 * Builds with Open Watcom (real mode: V86 under GLOS) and DJGPP (a DPMI client). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __DJGPP__
#include <dpmi.h>
#include <pc.h>
#include "glos/glos.h"
static int app_error;
#define INB(p) inportb(p)
#define OUTB(p, v) outportb(p, v)
#else
#include <conio.h>
#include <dos.h>
#include <i86.h>
#define INB(p) ((unsigned char)inp(p))
#define OUTB(p, v) outp(p, v)
#endif

#define MAX_TIMES 15
#define LIMIT_COUNTS 23864UL                    /* 20 ms of PIT counts */

static void ser(const char *s)
{
    for (; *s; s++) {
        unsigned n = 0;
        while (!(INB(0x3FD) & 0x20) && ++n < 60000u) ;
        OUTB(0x3F8, *s);
    }
}

static unsigned reload = 0xFFFFu;               /* PIT channel 0's count (mode 2) */

static unsigned pit(void)
{
    unsigned lo;
    OUTB(0x43, 0x00);
    lo = INB(0x40);
    return lo | (unsigned)INB(0x40) << 8;
}

/* One reading: per axis the reads and the PIT counts until its bit dropped (0xFFFFFFFF: never). */
static unsigned long reading(unsigned long count[4], unsigned long counts[4], unsigned *buttons)
{
    unsigned long reads = 0, elapsed = 0;
    unsigned last, now, v, live = 0x0F;
    int i;
    for (i = 0; i < 4; i++)
        count[i] = counts[i] = 0xFFFFFFFFUL;
    *buttons = ~(unsigned)INB(0x201) >> 4 & 0x0F;
    last = pit();
    OUTB(0x201, 0xFF);
    while (live && elapsed < LIMIT_COUNTS) {
        v = INB(0x201);
        reads++;
        now = pit();
        elapsed += last >= now ? last - now : last + reload - now;  /* (mode 2 counts down; one wrap at most) */
        last = now;
        for (i = 0; i < 4; i++)
            if ((live >> i & 1) && !(v >> i & 1)) {
                count[i] = reads;
                counts[i] = elapsed;
                live &= ~(1u << i);
            }
    }
    return reads;
}

static void pit_set(unsigned n)
{
    OUTB(0x43, 0x34);
    OUTB(0x40, n & 0xFF);
    OUTB(0x40, n >> 8);
    reload = n;
}

/* PIT counts of channel 0 (as set) for about ms milliseconds. */
static void pit_wait(unsigned long ms)
{
    unsigned long want = ms * 1193UL, got = 0;
    unsigned last = pit(), now;
    while (got < want) {
        now = pit();
        got += last >= now ? last - now : last + reload - now;
        last = now;
    }
}

static int cmp(const void *a, const void *b)
{
    unsigned long x = *(const unsigned long *)a, y = *(const unsigned long *)b;
    return x < y ? -1 : x > y;
}

static unsigned long median(unsigned long *v, int n)
{
    qsort(v, (size_t)n, sizeof *v, cmp);
    return v[n / 2];
}

/* INT 15h AH=84h: DX=0 the buttons, DX=1 the axes. Returns CF. */
static int bios(unsigned *btn, unsigned ax[4])
{
#ifdef __DJGPP__
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.h.ah = 0x84;
    r.x.dx = 0;
    __dpmi_int(0x15, &r);
    if (r.x.flags & 1)
        return 1;
    *btn = ~(unsigned)r.h.al >> 4 & 0x0F;
    memset(&r, 0, sizeof r);
    r.h.ah = 0x84;
    r.x.dx = 1;
    __dpmi_int(0x15, &r);
    ax[0] = r.x.ax, ax[1] = r.x.bx, ax[2] = r.x.cx, ax[3] = r.x.dx;
    return r.x.flags & 1;
#else
    union REGS r;
    memset(&r, 0, sizeof r);
    r.h.ah = 0x84;
    r.x.dx = 0;
    int86(0x15, &r, &r);
    if (r.x.cflag)
        return 1;
    *btn = ~(unsigned)r.h.al >> 4 & 0x0F;
    memset(&r, 0, sizeof r);
    r.h.ah = 0x84;
    r.x.dx = 1;
    int86(0x15, &r, &r);
    ax[0] = r.x.ax, ax[1] = r.x.bx, ax[2] = r.x.cx, ax[3] = r.x.dx;
    return r.x.cflag;
#endif
}

static void num(char *out, unsigned long v)
{
    if (v == 0xFFFFFFFFUL)
        strcpy(out, "-");
    else
        sprintf(out, "%lu", v);
}

int main(int argc, char **argv)
{
    static unsigned long cnt[4][MAX_TIMES], us[4][MAX_TIMES];
    unsigned long count[4], counts[4], reads = 0;
    unsigned buttons = 0, bbtn = 0, bax[4];
    char line[200], c[4][12], u[4][12];
    int times = argc > 1 ? atoi(argv[1]) : 5, t, i, cf;

    if (times < 1 || times > MAX_TIMES)
        times = 5;
    if (argc > 3 && !strcmp(argv[2], "pit")) {
        unsigned n = (unsigned)atoi(argv[3]);
        if (n < 100) {
            printf("HX-JOY bad-pit\n");
            return 2;
        }
        pit_set(n);
        pit_wait(2200);
    }
#ifdef __DJGPP__
    if (argc > 2 && !strcmp(argv[2], "app") && (app_error = glos_init("JOYTEST", "", "")) != 0) {
        printf("HX-JOY no-app error=%d\n", app_error);
        return 2;
    }
#endif
    for (t = 0; t < times; t++) {
        reads += reading(count, counts, &buttons);
        for (i = 0; i < 4; i++) {
            cnt[i][t] = count[i];
            us[i][t] = counts[i] == 0xFFFFFFFFUL ? counts[i] : counts[i] * 838UL / 1000UL;
        }
    }
    for (i = 0; i < 4; i++) {
        num(c[i], median(cnt[i], times));
        num(u[i], median(us[i], times));
    }
    if (reload != 0xFFFFu)
        pit_set(0xFFFFu);
    cf = bios(&bbtn, bax);
    sprintf(line, "HX-JOY buttons=%u%u%u%u count=%s,%s,%s,%s us=%s,%s,%s,%s ", buttons & 1, buttons >> 1 & 1,
            buttons >> 2 & 1, buttons >> 3 & 1, c[0], c[1], c[2], c[3], u[0], u[1], u[2], u[3]);
    if (cf)
        sprintf(line + strlen(line), "bios=cf reads=%lu\r\n", reads);
    else
        sprintf(line + strlen(line), "bios=0:%u%u%u%u:%u,%u,%u,%u reads=%lu\r\n", bbtn & 1, bbtn >> 1 & 1,
                bbtn >> 2 & 1, bbtn >> 3 & 1, bax[0], bax[1], bax[2], bax[3], reads);
    ser(line);
    fputs(line, stdout);
    return 0;
}
