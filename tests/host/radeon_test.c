/* Host test for the Radeon's settling before a mode set (kernel/gfx/radeon.c), on a register file in memory: an
 * idle engine left alone (nothing written but the surfaces when asked); an engine that drains after two waits
 * left alone; a stuck one (busy, the FIFO full, the CP's pointers still: the P3's RBBM_STATUS 80116100) reset after
 * STILL waits, the CP stopped; one that moves but never idles reset after WAITS waits. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "radeon.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static u32 regs[0x4000 / 4];
static int waits, idle_after = -1, moving;
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

static void setup(u32 status, int idle_at, int move)
{
    memset(regs, 0, sizeof regs);
    R(0x0E40) = status;
    R(0x0740) = 4u << 28;                       /* (the CP in bus-master mode) */
    R(0x0710) = 0x123;
    R(0x0B0C) = 0x00030050, R(0x0B04) = 0x100000, R(0x0B08) = 0x1FFFFF;
    R(0x0130) = 0x5;
    waits = 0, idle_after = idle_at, moving = move;
}

int main(void)
{
    setup(0x40, -1, 0);                         /* idle */
    CHECK(radeon_settle_regs(regs, wait, 0) == RADEON_IDLE);
    CHECK(waits == 0 && R(0x0740) == 4u << 28 && R(0x0B0C) == 0x00030050);
    CHECK(radeon_settle_regs(regs, wait, 1) == RADEON_IDLE);
    CHECK(R(0x0B0C) == 0 && R(0x0B04) == 0 && R(0x0B08) == 0);

    setup(0x80000020, 2, 1);                    /* busy, moving, idle at the second wait */
    CHECK(radeon_settle_regs(regs, wait, 0) == RADEON_SETTLED);
    CHECK(waits == 2 && R(0x0740) == 4u << 28);

    setup(0x80116100, -1, 0);                   /* the P3's: stuck */
    CHECK(radeon_settle_regs(regs, wait, 1) == RADEON_WEDGED);
    CHECK(waits == 5);                          /* (STILL) */
    CHECK(R(0x0740) == 0);                      /* the CP stopped */
    CHECK(R(0x0130) == 0x5 && R(0x00F0) == 0);  /* the host path and the reset register as they were */
    CHECK(R(0x0B0C) == 0);

    setup(0x80000010, -1, 1);                   /* moving, never idle */
    CHECK(radeon_settle_regs(regs, wait, 0) == RADEON_NEVER_IDLE);
    CHECK(waits == 20);                         /* (WAITS) */
    CHECK(R(0x0740) == 0);
    CHECK(R(0x0B0C) == 0x00030050);             /* (surfaces not asked for) */

    printf("radeon_test: %d failures\n", fails);
    return fails != 0;
}
