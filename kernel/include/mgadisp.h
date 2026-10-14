/* The Matrox card's drawing engine under the desktop (kernel/gfx/mga.c;
 * docs/milestones-m7.md step C). */
#ifndef K_MGADISP_H
#define K_MGADISP_H
#include "gfx.h"

struct bootinfo;

void mga_display_init(const struct bootinfo *bi);       /* the card on the PCI bus, unless [display] accel = off */
int mga_display_up(u8 *pix, u32 lfb, u32 pitch, u32 bpp, u32 h);   /* the VM thread, after the desktop's mode set (pix:
                                                   the screen's pixels): 1 if the engine is up on that framebuffer (the
                                                   card's), and the screen's surface may have mga_dev; 0 if not */
void mga_display_down(void);                    /* drained and let go of: a session has the screen, or no desktop */
int mga_display_on(void);
void mga_display_sync(void);                    /* the engine finished: the CPU may read the screen */
extern const struct gfx_dev mga_dev;
u32 mga_display_stat(char *buf, u32 max);       /* glos display: a line */

#endif
