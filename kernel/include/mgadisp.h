/* The Matrox card's drawing engine under the desktop (kernel/gfx/mga.c;
 * docs/milestones-m7.md step C). */
#ifndef K_MGADISP_H
#define K_MGADISP_H
#include "types.h"

struct bootinfo;

void mga_display_init(const struct bootinfo *bi);       /* the card on the PCI bus, unless [display] accel = off */
int mga_display_up(u32 lfb, u32 pitch, u32 bpp, u32 h); /* the VM thread, after the desktop's mode set: 1 if the engine
                                                           is up on that framebuffer (the card's), 0 if not */
void mga_display_down(void);                    /* drained and let go of: a session has the screen, or no desktop */
int mga_display_on(void);
u32 mga_display_stat(char *buf, u32 max);       /* glos display: a line */

#endif
