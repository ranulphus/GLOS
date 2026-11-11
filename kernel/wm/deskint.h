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
int deskin_capture(struct wm *m, struct wm_window *w, int on);  /* the mouse to w from now (or no longer): -1 if it
                                                                   can't have it (hidden, a window is being moved) */
void deskin_keys(u8 down[64], u32 *mods, u32 *locks);   /* the keyboard as it is */
void deskapp_hung(struct wm *m);        /* who isn't answering, each turn: their windows say so */
void deskapp_embeds(struct wm *m);      /* each turn: windows shown inside others go where their hosts are now */
int deskin_drag(struct wm *m, struct wm_window *w, int part);  /* the outline from now, for a window whose app asks
                                                                   (WH_TITLE, or WH_SIZE with WD_): -1 if no button is down */

/* deskapp.c: apps' windows. */
int deskapp_keep(void *mem);            /* a window's pixels being freed: 1 if an app still has them mapped (they
                                           are freed when it has let go) */
int deskapp_resize(struct wm *m, struct wm_window *w, s32 cw, s32 ch); /* the user's new size for a window: its
                                                                           owner is told; -1: no memory */
int deskapp_state(struct wm *m, struct wm_window *w, int state);        /* the user's new state for it (WS_): its
                                                                           owner is told of a new size; -1: no memory */
void deskapp_close(struct wm *m, struct wm_window *w);  /* the user wants it closed: its owner is asked (the
                                                           kernel's own just goes) */
void deskapp_screen(struct wm *m);      /* the screen has another format: every app's pixels are elsewhere */
void desk_refocus(struct wm *m);        /* no window has the focus: the topmost that can */

u32 deskin_double_ticks(void);          /* two presses this close are a double click ([input] double_click) */

/* deskshell.c: the tray and notifications. */
u32 deskshell_turn(struct wm *m, int screen);   /* each turn: the tick to wake at, at the latest (0: whenever) */
int deskshell_press(struct wm *m, u32 bit);     /* a button went down: 1 if on the tray or a notification (used up) */

#endif
