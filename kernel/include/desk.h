/* The desktop (kernel/wm/desk.c): the window manager's core (wm.h) in the
 * kernel, with memory, the screen, a thread of its own, and (steps E and F)
 * input and apps. */
#ifndef K_DESK_H
#define K_DESK_H
#include "wm.h"

void desk_init(void);                   /* the manager, its thread, and the display's painter */
struct wm *desk_lock(void);             /* the manager, for the caller alone until desk_unlock() (it may wait) */
void desk_unlock(void);                 /* ... and the composer told to look */
void desk_kick(void);                   /* something changed (from anywhere, interrupts on or off) */
void desk_note(const char *note);       /* the task bar's note: a remote session's (PRD D35), or "" */
u32 desk_frames(void);                  /* how many composes have drawn something (glos rec: M10) */
/* Input, from the 8042's code (vm/vkbc.c; interrupts off): a keyboard byte
   (set 1); a byte of the mouse's (b: its packets have a fourth byte, a
   wheel's); a movement already told apart (dx, dy, the buttons now); the
   kill hotkey; or word that the keyboard has changed hands (whatever was
   held is forgotten). DESK_POINT (M10): the pointer to (a, b), the buttons c: VNC's, absolute. */
enum { DESK_KEY, DESK_AUX, DESK_MOUSE, DESK_KILL, DESK_RESET, DESK_SYSKEY, DESK_POINT };
void desk_in(int what, int a, int b, int c);
int desk_in_busy(void);                         /* injected keys should wait: the focused app is behind */
const char *desk_focus_line(void);              /* "app=N window=N backlog=N input=N", for the log */
/* Apps' windows (wm/deskapp.c), for the API (app/api.c), on the app's own
   thread: 0, or a GLOS_E_ code. */
struct app;
struct glos_window;
struct glos_rect;
int desk_win_create(struct app *a, struct glos_window *g);      /* g: what is asked for, then what it is */
int desk_win_close(struct app *a, u32 id);
int desk_win_show(struct app *a, u32 id, int on);
int desk_win_move(struct app *a, u32 id, s32 x, s32 y);
int desk_win_resize(struct app *a, u32 id, s32 cw, s32 ch);
int desk_win_title(struct app *a, u32 id, const char *title);
int desk_win_pixels(struct app *a, u32 id, struct glos_window *g);
int desk_win_present(struct app *a, u32 id, const struct glos_rect *r);        /* NULL: all of it */
int desk_win_vram(struct app *a, u32 id, u32 off, u32 pitch);         /* GLOS_FN_WINVRAM */
void desk_app_vram_gone(struct app *a, u32 off, u32 bytes);     /* its windows stop showing that block */
int desk_win_focus(struct app *a, u32 id);
void desk_screen(struct glos_window *g);
void desk_app_gone(struct app *a);      /* an app has ended (on its thread): its windows go */
int desk_demo(int on);                  /* glos desk demo|clear: wm/demo.c's windows; -1: no memory */
u32 desk_stat(char *buf, u32 max);      /* glos desk: its windows, a line each */

void deskin_pointer_speed(int speed);   /* [input] pointer_speed (M9) */
struct app;
void desk_shell(struct app *a, int on);
u32 desk_windows(const void *owner);    /* an app's windows (NULL: every one): GLOS_FN_STAT's */        /* the desktop's shell: the launcher's and the task list's (M9) */

#endif
