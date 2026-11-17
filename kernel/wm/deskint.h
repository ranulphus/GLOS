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
int deskpass_key(struct wm *m, u32 key, int down, u32 ch);      /* (deskpass.c) a key for the focus: 1 if the secrets'
                                                                   password dialog took it */
void deskpass_turn(struct wm *m, int again);    /* ... each turn: drawn again when the screen changed under it */
int deskshell_press(struct wm *m, u32 bit);     /* a button went down: 1 if on the tray or a notification (used up) */
int deskshell_wheel(struct wm *m, int notches); /* the wheel: 1 if over one of GLOS's own icons (used up) */
u32 deskshell_list(struct wm *m, char *buf, u32 max);   /* glos desk's lines for the tray's icons */
u32 deskshell_own_add(struct wm *m, const u32 *icon, const char *tip);  /* one of GLOS's own tray icons: its id */
void deskshell_own_set(struct wm *m, u32 id, const u32 *icon, const char *tip);        /* (NULL: as it was) */
void deskshell_own_remove(struct wm *m, u32 id);
u32 deskshell_note(const char *title, const char *text);        /* one of GLOS's own notifications */

/* desktray.c: GLOS's own tray icons (phase D, D2): the volume, the network, the pads, a remote session. */
void desktray_turn(struct wm *m, int screen, int again);        /* each turn (again: another theme: drawn again) */
void desktray_press(struct wm *m, u32 id, int what, const struct gfx_rect *r);  /* 1 a press, 2 twice, 3 right */
void desktray_wheel(struct wm *m, u32 id, int notches);
void desktray_keep(void);               /* (after the turn, the manager unlocked) what is to be kept in GLOS.CFG */
const char *desk_note_text(void);       /* a remote session's note (desk_note), or "" */

/* deskpop.c: the desktop's own pop-up menus (phase D). */
enum { DP_NONE, DP_WINDOW, DP_GROUP, DP_CUSTOM };       /* a window's menu (restore, minimise, maximise, close); a
                                                           group's windows; one made by deskpop_begin and _show */
struct popmenu;
struct popmenu *deskpop_begin(struct wm *m);    /* a menu of the desktop's own making, to fill (pm_add) ... */
void deskpop_show(struct wm *m, s32 x, s32 y, int above, void (*chosen)(struct wm *m, u32 data));  /* ... and show */
void deskpop_open(struct wm *m, int kind, u32 target, s32 x, s32 y, int above); /* at x, y: its bottom left corner
                                                                                    (above 1) or its top left */
int deskpop_key(struct wm *m, u32 key, int down, u32 ch);       /* a key while it has the keyboard: 1 if taken */
int deskpop_press(struct wm *m, u32 bit);       /* a button went down: 1 if on the pop-up (used up); outside, it goes */
void deskpop_move(struct wm *m, u32 buttons);   /* the pointer moved: the item under it is the current one; the
                                                   volume's slider follows a held button */
void deskvol_open(struct wm *m, const struct gfx_rect *icon, s32 edge, int below, void (*get)(u32 *v, int *mute),
                  void (*set)(u32 v, int mute), void (*done)(void));    /* the volume's slider by its icon (v: 0 to
                                                   256); done: closed */
int deskvol_up(void);
void deskvol_close(struct wm *m);
void deskvol_again(struct wm *m);       /* (the volume changed elsewhere: drawn again) */
void deskpop_turn(struct wm *m, int again);     /* each turn (again: another screen or theme): it goes if it must */
int deskpop_up(void);
void deskswitch_step(struct wm *m, int back);   /* Alt+Tab (back: Shift too): the strip, or its next window */
void deskswitch_end(struct wm *m, int go);      /* Alt let go (go 1), Esc (0): the strip goes, its window in front */
int deskswitch_up(void);
void deskcal_open(struct wm *m, s32 right, s32 edge, int below, int year, int month, int day, int first_day);
                                        /* the clock's calendar, its right at right, above edge (below: under it);
                                           first_day: 0 Sunday */
int deskcal_up(void);
void deskcal_close(struct wm *m);

#endif
