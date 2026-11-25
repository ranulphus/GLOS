/* Host test for the Radeon's settling before a mode set (kernel/gfx/radeon.c), on a register file in memory whose
 * RBBM_STATUS the test changes at a chosen wait (the engine going idle then) or at bus mastering's turning off
 * (the harder reset working): an idle engine left alone (nothing written but the surfaces when asked); an engine
 * that drains after two waits left alone; a stuck one (busy, the FIFO full, the CP's pointers still: the P3's
 * RBBM_STATUS 80116100) reset after STILL waits, the CP stopped, the first reset enough; one the first reset leaves
 * with its FIFO full, which the harder one (bus mastering off and back) frees; one nothing frees, dead, its
 * surfaces left alone; one that moves but never idles reset after WAITS waits. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "radeon.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static u32 regs[0x4000 / 4];
static int waits, idle_after = -1, moving, bm_off, bm_on, bm_frees;
#define R(o) regs[(o) / 4]

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

static void wait(void)
{
    waits++;
    if (waits == idle_after)
        R(0x0E40) = 0x40;                       /* (64 free, not active) */
    else if (moving)
        R(0x0710) = R(0x0710) + 3;              /* (the CP's read pointer on) */
}

static void bm(int on)
{
    if (on)
        bm_on++;
    else
        bm_off++;
    if (!on && bm_frees)
        R(0x0E40) = 0x40;
}

static void setup(u32 status, int idle_at, int move, int frees)
{
    memset(regs, 0, sizeof regs);
    R(0x0E40) = status;
    R(0x0740) = 4u << 28;                       /* (the CP in bus-master mode) */
    R(0x0710) = 0x123;
    R(0x0704) = 0x0803;                         /* (CP_RB_CNTL) */
    R(0x0030) = 0x11;                           /* (BUS_CNTL) */
    R(0x0B0C) = 0x00030050, R(0x0B04) = 0x100000, R(0x0B08) = 0x1FFFFF;
    R(0x0130) = 0x5;
    waits = 0, idle_after = idle_at, moving = move, bm_off = bm_on = 0, bm_frees = frees;
}

int main(void)
{
    setup(0x40, -1, 0, 0);                      /* idle */
    CHECK(radeon_settle_regs(regs, wait, bm, 0) == RADEON_IDLE);
    CHECK(waits == 0 && R(0x0740) == 4u << 28 && R(0x0B0C) == 0x00030050);
    CHECK(radeon_settle_regs(regs, wait, bm, 1) == RADEON_IDLE);
    CHECK(R(0x0B0C) == 0 && R(0x0B04) == 0 && R(0x0B08) == 0);

    setup(0x80000020, 2, 1, 0);                 /* busy, moving, idle at the second wait */
    CHECK(radeon_settle_regs(regs, wait, bm, 0) == RADEON_SETTLED);
    CHECK(waits == 2 && R(0x0740) == 4u << 28);

    setup(0x80116100, 6, 0, 0);                 /* the P3's: stuck; the first reset (its wait, the 6th) frees it */
    CHECK(radeon_settle_regs(regs, wait, bm, 1) == RADEON_WEDGED);
    CHECK(waits == 6);                          /* (STILL, and the reset's own) */
    CHECK(R(0x0740) == 0);                      /* the CP stopped */
    CHECK(R(0x0130) == 0x5 && R(0x00F0) == 0);  /* the host path and the reset register as they were */
    CHECK(bm_off == 0 && bm_on == 0);           /* (no harder reset) */
    CHECK(R(0x0B0C) == 0);

    setup(0x80116100, -1, 0, 1);                /* the first reset leaves the FIFO full; the harder one frees it */
    CHECK(radeon_settle_regs(regs, wait, bm, 1) == RADEON_WEDGED);
    CHECK(bm_off == 1 && bm_on == 1 && R(0x0030) == 0x11 && R(0x0704) == 0x0803 && R(0x00F0) == 0);
    CHECK(waits == 5 + 1 + 2 * 9 + 2);          /* (still; the first reset; the harder one's holds and pauses) */
    CHECK(R(0x0B0C) == 0);

    setup(0x80116100, -1, 0, 0);                /* nothing frees it: dead, its surfaces not touched */
    CHECK(radeon_settle_regs(regs, wait, bm, 1) == RADEON_DEAD);
    CHECK(bm_off == 1 && bm_on == 1 && R(0x0B0C) == 0x00030050);

    setup(0x80000010, 21, 1, 0);                /* moving, never idle; the reset (its wait, the 21st) frees it */
    CHECK(radeon_settle_regs(regs, wait, bm, 0) == RADEON_NEVER_IDLE);
    CHECK(waits == 21 && bm_off == 0);          /* (WAITS, and the reset's own) */
    CHECK(R(0x0740) == 0);
    CHECK(R(0x0B0C) == 0x00030050);             /* (surfaces not asked for) */

    setup(0x80000010, -1, 1, 0);                /* (no bus-mastering hook: the harder reset without it) */
    CHECK(radeon_settle_regs(regs, wait, 0, 0) == RADEON_DEAD);

    printf("radeon_test: %d failures\n", fails);
    return fails != 0;
}
