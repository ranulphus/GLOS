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
void *mga_store_alloc(u32 bytes);               /* VRAM for the desktop's windows' pixels (64-byte aligned), or NULL */
int mga_store_free(void *p);                    /* 1 if p was such */
const struct gfx_dev *mga_store_dev(const void *p);     /* mga_dev for such pixels, else NULL */
void mga_store_limit(u32 bytes);                /* at most this much of VRAM for them (0: no limit) */
extern const struct gfx_dev mga_dev;

/* For the GPU lock (gpu.c) and apps (M8). */
typedef struct {
    u32 device, revision, vram, bpp, fb_phys, regs_phys, option, subsys;
} mga_card_info;
const mga_card_info *mga_card(void);            /* the card while its engine draws the desktop, else NULL */
int mga_desktop_card(void);                     /* the desktop's last mode was on the card (a session may have it now) */
u32 mga_regs_phys(void);
int mga_handover(int kernel, int reset);        /* the engine changes hands: drained (reset: reset first); kernel:
                                                   the kernel's HAL resynchronised. -1 if it had hung (reset) */
u32 mga_display_stat(char *buf, u32 max);       /* glos display: a line */


/* Apps' blocks of VRAM (M8 step C), at the same offsets for their lives: kept in the system's memory while the
   card is another's (mga_display_down) and put back after (mga_display_up), or the app is ended. */
int mga_app_vram(u32 id, u32 min, u32 max, u32 align, u32 *off, u32 *bytes);   /* from the top: 0, or -1 */
u32 mga_app_vram_free(u32 id, u32 off);         /* its size, or 0 if it was no block of the app's */
int mga_app_block(u32 id, u32 off, u32 len, u32 *start, u32 *bytes);   /* [off, off + len) in a block of the app's:
                                                   1 (and the block), or 0 */
void mga_app_gone(u32 id);
void *mga_vram_ptr(u32 off);                    /* the kernel's address of a VRAM offset */
u32 mga_vram_size(void);
int mga_app_next(u32 id, u32 from, u32 *start, u32 *bytes);    /* the app's first block at or after from: 1, or 0 */

#endif
