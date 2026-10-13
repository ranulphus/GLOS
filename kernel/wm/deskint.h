/* The desktop: what kernel/wm's kernel-side files share (desk.c, deskin.c,
 * deskapp.c). Everything here is called with the manager locked (desk_lock)
 * unless it says otherwise. */
#ifndef K_DESKINT_H
#define K_DESKINT_H
#include "desk.h"

struct app;

/* deskin.c: the keyboard and the mouse. */
void deskin_turn(struct wm *m);         /* what has come in since the last turn */
void deskin_focus(struct wm *m);        /* the focus may have changed: its old owner and its new one are told */
void deskin_gone(struct app *a);        /* an app has ended: nothing more for it */
void deskin_init(void);

/* deskapp.c: apps' windows. */
int deskapp_keep(void *mem);            /* a window's pixels being freed: 1 if an app still has them mapped (they
                                           are freed when it has let go) */
int deskapp_resize(struct wm *m, struct wm_window *w, s32 cw, s32 ch); /* the user's new size for a window: its
                                                                           owner is told; -1: no memory */
void deskapp_close(struct wm *m, struct wm_window *w);  /* the user wants it closed: its owner is asked (the
                                                           kernel's own just goes) */
void deskapp_screen(struct wm *m);      /* the screen has another format: every app's pixels are elsewhere */
void desk_refocus(struct wm *m);        /* no window has the focus: the topmost that can */

#endif
