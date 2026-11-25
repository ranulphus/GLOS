/* An ATI Radeon (R100 family) settled before the BIOS sets a mode (kernel/gfx/radeon.c). */
#ifndef K_RADEON_H
#define K_RADEON_H
#include "types.h"

enum {
    RADEON_IDLE,                /* idle at once */
    RADEON_SETTLED,             /* idle after a wait */
    RADEON_WEDGED,              /* busy and still: the engine reset */
    RADEON_NEVER_IDLE           /* busy, moving, never idle: the engine reset */
};

/* r: the card's registers (BAR 2, 16 KB mapped); wait: about 55 ms. surfaces: SURFACE0's registers cleared too. */
int radeon_settle_regs(volatile u32 *r, void (*wait)(void), int surfaces);

#endif
