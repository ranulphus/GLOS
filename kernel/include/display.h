/* The display (kernel/gfx/display.c; PRD §9.1): the screen as a surface for
 * the 2D library. */
#ifndef K_DISPLAY_H
#define K_DISPLAY_H
#include "gfx.h"

struct bootinfo;

enum { DISPLAY_OFF, DISPLAY_ON, DISPLAY_CARD };

void display_init(const struct bootinfo *bi);   /* GLOS.CFG's [display] mode: asked for at the start */
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
void display_leave(void);                       /* GLOS is leaving: text mode */
int display_quiet(u32 *eax, u32 *ebx, u32 *ecx, u32 *edx, int app);    /* a program's INT 10h while the desktop
                                                   has the screen: 1 if answered here, not by the BIOS */

#endif
