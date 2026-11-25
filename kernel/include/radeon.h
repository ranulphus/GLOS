/* An ATI Radeon (R100 family) settled before the BIOS sets a mode (kernel/gfx/radeon.c). */
#ifndef K_RADEON_H
#define K_RADEON_H
#include "types.h"

enum {
    RADEON_IDLE,                /* idle at once */
    RADEON_SETTLED,             /* idle after a wait */
    RADEON_WEDGED,              /* busy and still: the engine reset */
    RADEON_NEVER_IDLE,          /* busy, moving, never idle: the engine reset */
    RADEON_DEAD                 /* reset twice, and its FIFO still full: no BIOS mode set may be made */
};

/* r: the card's registers (BAR 2, 16 KB mapped); wait: about 55 ms; bm: bus mastering off (0) and on (1) at the
   PCI command register (may be 0). surfaces: SURFACE0's registers cleared too. */
int radeon_settle_regs(volatile u32 *r, void (*wait)(void), void (*bm)(int on), int surfaces);

#endif
