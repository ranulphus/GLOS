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
   held is forgotten). DESK_POINT (M10): the pointer to (a, b), the buttons c: VNC's, absolute. DESK_WHEEL: the
   wheel turned a notches (positive away from the user: up). */
enum { DESK_KEY, DESK_AUX, DESK_MOUSE, DESK_KILL, DESK_RESET, DESK_SYSKEY, DESK_POINT, DESK_WHEEL };
void desk_in(int what, int a, int b, int c);
int desk_in_busy(void);                         /* injected keys should wait: the focused app is behind */
const char *desk_focus_line(void);              /* "app=N window=N backlog=N input=N", for the log */
/* Apps' windows (wm/deskapp.c), for the API (app/api.c), on the app's own
   thread: 0, or a GLOS_E_ code. */
struct app;
struct kwindow;
struct krect;
int desk_win_create(struct app *a, struct kwindow *g);      /* g: what is asked for, then what it is */
int desk_win_close(struct app *a, u32 id);
int desk_win_title(struct app *a, u32 id, const char *title);
int desk_win_pixels(struct app *a, u32 id, struct kwindow *g);
int desk_win_present(struct app *a, u32 id, const struct krect *r);        /* NULL: all of it */
int desk_win_vram(struct app *a, u32 id, u32 off, u32 pitch);         /* GLOS_FN_WINVRAM */
void desk_app_vram_gone(struct app *a, u32 off, u32 bytes);     /* its windows stop showing that block */
int desk_win_focus(struct app *a, u32 id);
void desk_screen(struct kwindow *g);
void desk_app_gone(struct app *a);      /* an app has ended (on its thread): its windows go */
/* ... and for API 2.0 (app/api2win.c). */
struct desk_new {
    s32 x, y, w, h;                     /* the frame's corner, the client area's size */
    u32 wf;                             /* WF_ */
    u8 anywhere, desktop;               /* the desktop chooses the place; the shell's desktop window */
    u32 parent;                         /* the window it belongs to, by id, or 0 */
    u32 tag;
    const char *title;
};
struct desk_info {
    u32 id, tag, wf, parent;
    struct app *owner;
    int state;                          /* WS_, or -1: hidden */
    int focus;
    s32 x, y, w, h;                     /* the frame's corner; the client area's size */
    s32 cx, cy;                         /* the client area's corner on the screen */
    s32 min_w, min_h, max_w, max_h;
    int cursor;
    int flash, progress;                /* progress: 0 to 100, or -1 */
    char help[32];
    char title[WM_TITLE_MAX];
};
int desk_win_new(struct app *a, const struct desk_new *n, u32 *id);
int desk_win_state(struct app *a, u32 id, int state, int focus);       /* WS_; -1: hidden; -2: shown as it last was */
int desk_win_place(struct app *a, u32 id, int move, s32 x, s32 y, int size, s32 w, s32 h);
int desk_win_info(struct app *a, u32 id, struct desk_info *out);
u32 desk_win_list(struct desk_info *out, u32 max);      /* every window but the task bar, front to back: how many
                                                           there are (as many as fit given) */
int desk_win_limit(struct app *a, u32 id, int which, s32 v);   /* 0 min_w, 1 min_h, 2 max_w, 3 max_h */
int desk_win_cursor(struct app *a, u32 id, int shape);
int desk_win_focus_id(u32 id);          /* any window, by its id */
enum { DW_TOPMOST, DW_TASKBAR, DW_FLASH, DW_PROGRESS, DW_ICON, DW_CURSOR_IMG, DW_HELP };
int desk_win_set(struct app *a, u32 id, int what, s32 v, const void *data, u32 len);
int desk_win_icon(struct app *a, u32 id, u32 *out);     /* its picture (256 pixels): 1 if it has one, 0, or -1 */
int desk_win_drag(struct app *a, u32 id, int part);     /* the user moves or sizes it from here on (a button is down) */
void desk_work(s32 *x, s32 *y, s32 *w, s32 *h);         /* the screen less the task bar */
struct desk_blit {
    u16 ds;                             /* the app's data selector: where src and palette are */
    u32 src, pitch;                     /* the source's top left pixel, and its rows' distance */
    struct gfx_fmt fmt;                 /* its format */
    s32 w, h, x, y;                     /* its size; where it goes in the client area */
    u32 palette;                        /* an 8-bit source's 256 colours (0x00RRGGBB), or 0: GLOS's palette */
};
int desk_win_capture(struct app *a, u32 id, int on);    /* (API 2.0's INPUT CAPTURE) */
void desk_keys(u8 down[64], u32 *mods, u32 *locks);     /* the keyboard as the desktop has it: GLOS_MOD_, GLOS_LOCK_ */
struct app *deskin_focus_app(void);     /* the app whose window has the focus, or 0 (any thread: a look) */
int desk_win_blit(struct app *a, u32 id, const struct desk_blit *b);    /* (API 2.0's DRAW BLIT) */
int desk_win_embed(struct app *a, u32 host, u32 child, const struct gfx_rect *at);     /* (API 2.0's WIN EMBED) the
                                                   window `child` shown inside a's window `host` (at NULL: let go):
                                                   0, GLOS_E_HANDLE, DESK_E_REFUSED */
int desk_drawing(void);                 /* the desktop is painting, or owes the screen something (glos shot waits) */
int desk_password(struct app *a);       /* (kernel/wm/deskpass.c) the secrets store's master password asked of the
                                           user in a dialog of the desktop's, the calling app's thread waiting: 0
                                           once the store is open; -1: no desktop to ask on, the user said no, or
                                           the app is ending */
/* API 2.0's CAPTURE (kernel/app/api2cap.c): any window by its number. */
int desk_win_who(u32 id, u32 *owner, s32 *w, s32 *h);   /* whose it is (the app's id; 0: GLOS's own) and its client
                                                            area's size: 0, or GLOS_E_HANDLE */
int desk_win_rows(u32 id, s32 y, s32 n, s32 w, u8 *rgb);        /* n rows of its client area from row y, as R, G, B
                                                                   bytes (w pixels a row): the rows given; -1: it
                                                                   is gone, or no longer w wide */
int desk_win_key(u32 id, u32 key, int down, u32 mods);          /* a key into it, as if typed there: 0, GLOS_E_HANDLE */
int desk_win_mouse(u32 id, s32 x, s32 y, u32 buttons, u32 button, int down, u32 clicks);       /* ... the mouse */
#define DESK_E_REFUSED 19               /* (API 2.0's GLOS_E_REFUSED; its GLOS_E_HANDLE is 1.x's GLOS_E_HANDLE, 6) */
#define DESK_EV_STATE 100               /* (to app_event, for API 2.0's apps alone) a window's state changed: key WS_ */
int desk_demo(int on);                  /* glos desk demo|clear: wm/demo.c's windows; -1: no memory */
u32 desk_stat(char *buf, u32 max);      /* glos desk: its windows, a line each */

void deskin_pointer_speed(int speed);   /* [input] pointer_speed (M9) */
void deskin_double_click(u32 ms);       /* [input] double_click: the title bar's (the toolkit has its own) */
struct theme;
const struct theme *desk_theme(void);   /* the theme in force (kernel/wm/theme.c) */
int desk_theme_set(const char *name);   /* [desktop] theme: 0, or -1 no such theme */
void deskin_snap(int on);               /* a window dragged to the screen's edge snaps there ([desktop] snap) */
struct app;
void desk_shell(struct app *a, int on);
u32 desk_windows(const void *owner);    /* an app's windows (NULL: every one): GLOS_FN_STAT's */        /* the desktop's shell: the launcher's and the task list's (M9) */

/* Drag and drop (API 2.0's DND). The desktop's input gives the pointer to a drag (deskin.c) and tells the API
   where it is (kernel/app/api2clip.c, which knows the targets and sends the events). */
struct app;
int desk_dnd_begin(struct app *a, u32 win_id, const u32 *picture, u32 pw, u32 ph);  /* DRAG: a button is down in
                                                   that window of a's: the pointer is the drag's from now, with a
                                                   picture (0xAARRGGBB, or NULL). 0, or -1: no button is down there */
void desk_dnd_stop(void);               /* the drag is over for another reason than the pointer's (its source went) */
/* api2clip.c: the pointer is at sx, sy over the client area of owner's window win (its corner at ox, oy), or over
   the bare desktop (owner NULL, win 0), or over nothing that could be a target (win ~0). */
void api2_dnd_at(struct app *owner, u32 win, s32 sx, s32 sy, s32 ox, s32 oy);
void api2_dnd_end(int drop);            /* the button was let go (1), or the drag given up (0: Esc, a session) */
int api2_dnd_effect(void);              /* the effect a drop would have now (GLOS_DND_: 1, 2, 4), 0 none */

/* The tray and notifications (API 2.0's SHELL; kernel/wm/deskshell.c). */
struct note;
struct note_news;
struct gfx_rect;
u32 desk_tray_add(struct app *a, const u32 *icon, const char *tip);    /* an icon of a's: its id, 0 if the tray is full */
void desk_tray_set(u32 id, const u32 *icon, const char *tip);   /* its picture or its tooltip changed (NULL: as it is) */
int desk_tray_tip(u32 id, char *out, u32 room);         /* its tooltip: its length, -1 no such icon */
void desk_tray_remove(struct app *a, u32 id);           /* gone (id 0: every one of a's) */
u32 desk_notify_add(struct app *a, const struct note *n);       /* a notification of a's: its id, 0 if too many wait */
void desk_notify_remove(struct app *a, u32 id);         /* taken away, shown or waiting (id 0: every one of a's) */
int desk_point(int what, struct gfx_rect *r);           /* where a part of the task bar is (wm.h's WB_): 1, or 0 */
struct app *desk_shell_app(void);       /* the desktop's shell, or NULL */
int desk_shell_set(struct app *a, int on);      /* a is the shell (0), or no longer; -1: another app is */
/* kernel/app/api2shell.c: what the desktop tells apps (the desktop's thread). what: 1 a press, 2 the second of two,
   3 the right button. */
void api2_tray_event(struct app *a, u32 tray, int what, const struct gfx_rect *r);
void api2_note_event(const struct note_news *n);
void api2_shell_hung(struct app *shell, u32 window, struct app *owner, int hung);

#endif
