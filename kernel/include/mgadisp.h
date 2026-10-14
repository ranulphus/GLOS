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
void mga_cursor_image(const u8 *and_mask, const u8 *xor_mask);  /* the pointer's (64 x 64 AND/XOR, wm_pointer_masks) */
int mga_cursor(int on, s32 x, s32 y);           /* the hardware cursor shown (or not) there: 1, or 0 if the card
                                                   can't (no engine up), and the pointer is software's */
int mga_cursor_state(s32 *x, s32 *y, const u8 **and_mask, const u8 **xor_mask);   /* 1 while it shows */
extern const struct gfx_dev mga_dev;
u32 mga_display_stat(char *buf, u32 max);       /* glos display: a line */

#endif
