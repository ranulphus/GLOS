/* The display (kernel/gfx/display.c; PRD §9.1): the screen as a surface for
 * the 2D library. */
#ifndef K_DISPLAY_H
#define K_DISPLAY_H
#include "gfx.h"

struct bootinfo;

enum { DISPLAY_OFF, DISPLAY_ON, DISPLAY_CARD };

void display_init(const struct bootinfo *bi);   /* GLOS.CFG's [display] mode: asked for at the start */
int display_card_settle(void);           /* a stuck Radeon engine reset before a BIOS mode set (gfx/radeon.c): 0, or
                                           -1 (stuck past two resets: no mode set may be made) */
void display_refresh_changed(void);
u32 display_measure_hz(void);            /* the refresh the card makes, measured: hundredths of a Hz (0: none seen) */      /* [display] refresh: the desktop's mode set again at the new rate */
int display_ask(int what, u32 w, u32 h, u32 bpp);       /* from any thread: DISPLAY_ (w 0: the mode as it is, or
                                                   the configured one); done by the VM thread when the stub
                                                   next asks. -1: one is waiting already */
int display_vm_pending(void);
void display_vm_turn(void);                     /* the VM thread, at the stub's NEXT */
void display_session(int begin);                /* the VM thread: a session takes the screen (text mode), or has
                                                   ended (the desktop's mode again, and its picture) */
struct gfx_surface *display_fb(void);           /* the screen, or NULL while it isn't the desktop's */
void display_painter(void (*paint)(struct gfx_surface *fb));    /* who draws the whole screen when it comes (back) */
u32 display_stat(char *buf, u32 max);           /* glos display: a line */
void display_sync(void);                        /* a drawing engine's work on the screen finished: before reading it */
void display_cursor_image(const u8 *and_mask, const u8 *xor_mask);      /* the pointer, for a hardware cursor */
int display_cursor(int on, s32 x, s32 y);       /* the pointer by the card's cursor: 1, or 0 if it must be drawn */
int display_scanout(const struct gfx_surface *s);       /* the display on these pixels in the card's memory instead of
                                                           the screen's own (NULL: the screen's again); 1 if so */
u32 display_scanout_phys(void);                 /* the first pixel shown while it is so (physical); else 0 */
int display_app_mode(u32 w, u32 h, s32 x, s32 y, void (*done)(void), const void *who);  /* the card in the mode of w by h, its memory
                                                   kept, for a full-screen window of that size at (x, y) on the
                                                   desktop (w 0: the desktop's mode again): 1 it is so, 0 asked for
                                                   (done() when it is), -1 it cannot be */
/* 1 while the card is changing to or from a mode of the app's (who: display_app_mode's) for it: the app waits on
   GLOS meanwhile, a BIOS's mode set on the system VM's thread, its own sound not fed (the mixer holds its streams:
   mixer.c). Any context. */
int display_app_waits(const void *app);
int display_app_busy(void);                     /* the card is in such a mode, or changing: nothing is drawn on the
                                                   screen */
void display_app_refuse(void);                  /* the mode it is in is no use for the window: not asked for again */
void display_evicter(void (*evict)(void));      /* who moves pixels out of the card's memory before it changes hands */
void *display_vram_alloc(u32 bytes);            /* the card's memory for pixels (a drawing engine's), or NULL */
int display_vram_free(void *p);                 /* 1 if p was such, now free */
const struct gfx_dev *display_vram_dev(const void *p);  /* the engine at such pixels, or NULL */
void display_vram_limit(u32 bytes);             /* at most this much of it for them from now on (0: no limit) */
void display_leave(void);                       /* GLOS is leaving: text mode */
void display_counts(u32 *w, u32 *h, u32 *bpp, u32 *accel, u32 *vram_kb);     /* GLOS_FN_STAT's (M9) */
void display_console_trap(int on);      /* a console job runs in its window: the VGA out of reach (M9) */
int display_vga_fault(u32 linear);       /* 1: a console job touched the VGA's memory (now full-screen) */
int display_vga_port(u32 port);         /* 1: ... or its port */
int display_quiet(u32 *eax, u32 *ebx, u32 *ecx, u32 *edx, int app);    /* a program's INT 10h while the desktop
                                                   has the screen: 1 if answered here, not by the BIOS */

#define DISPLAY_MODES 64
struct display_mode {
    u16 w, h, bits;
};
int display_modes(struct display_mode *out, u32 max);   /* the modes the card has (listed by the VM thread the first
                                                   time: may wait); how many, or -1 if not known yet */
enum { DISPLAY_POWER_ON, DISPLAY_POWER_STANDBY, DISPLAY_POWER_SUSPEND, DISPLAY_POWER_OFF };
int display_power(int state, u32 *can);         /* the screen's power (VBE/PM) set (-1: only asked); the state now, and
                                                   in *can the states the card has, a bit each (0: none). May wait */
void display_power_post(int state);             /* ... asked for without waiting */
int display_power_now(void);                    /* ... as it is */

int display_desktop_bpp(void);                  /* the desktop's depth, the screen its or held by a session; 0: none */
int display_dac_bits(void);                     /* the DAC's bits a colour at 8 bits a pixel: 8, or 6 (VNC: M10) */

#endif
