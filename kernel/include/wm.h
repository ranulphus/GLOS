/* The window manager's core (kernel/wm/wm.c; PRD §9.4; docs/graphics.md §3):
 * windows that keep their pixels, their order, the focus, and the composer
 * that puts them on the screen. Pure code on gfx surfaces, like the 2D
 * library: tests/host/wm_test.c runs it on the host. The kernel's side
 * (kernel/wm/desk.c) gives it memory, the screen, a thread and input. */
#ifndef K_WM_H
#define K_WM_H
#include "gfx.h"

#define WM_TITLE_MAX 48

/* A window's flags. */
#define WF_SHOWN   0x0001               /* on the screen (when nothing covers it) */
#define WF_FRAME   0x0002               /* it has a title bar and a border, which the manager draws */
#define WF_FIXED   0x0004               /* its size is not the user's to change */
#define WF_TOPMOST 0x0008               /* above every window that isn't (the task bar) */
#define WF_NOFOCUS 0x0010               /* never the focus (the task bar) */
#define WF_POPUP   0x0020               /* an app's menu (GLOS_WIN_POPUP): topmost and bare; never given the focus
                                           when another window's goes, and a press outside it takes it away */
#define WF_DESKTOP 0x0040               /* the shell's desktop (GLOS_WIN_DESKTOP): under every other window, always */

/* Where a point is in a window (wm_hit). */
enum { WH_NONE, WH_CLIENT, WH_TITLE, WH_CLOSE, WH_GROW, WH_BORDER };

struct wm_window {
    struct wm_window *above, *below;
    u32 id;
    void *owner;                        /* whose it is (the kernel's side says: an app, or 0) */
    u16 flags;
    struct gfx_rect frame;              /* on the screen, title bar and border included */
    struct gfx_surface store;           /* the client area's pixels, in the screen's format */
    void *mem;                          /* ... and their memory, from wm_host.alloc */
    struct gfx_surface ext;             /* what it shows instead, if has_ext: pixels that aren't the manager's (an
                                           app's in the card's memory, M8), of the client area's size and format */
    u8 has_ext;
    char title[WM_TITLE_MAX];
};

/* The look: sizes, colours and fonts the manager draws with. Greys, and one
   colour that isn't (`mark`), which says which window is in front. */
struct wm_look {
    s32 border, title_h, box;           /* the border's width, the title bar's height, the close box's side */
    u32 desk;                           /* the desktop */
    u32 edge, face, text;               /* a window's border, its title bar and title */
    u32 face_off, text_off;             /* ... when it isn't the focus */
    u32 mark;                           /* the focus's close box, filled (GFX_NONE: an outline, as the others') */
    u32 bar, bar_text, bar_on, bar_on_text;     /* the task bar; its button for the focus */
    const struct gfx_font *font, *bold;
};

/* What the core needs from its host. */
struct wm_host {
    void *(*alloc)(u32 bytes, int kind);        /* memory (WM_MEM_), zeroed, or NULL */
    void (*free)(void *mem, u32 bytes, int kind);
    const struct gfx_dev *(*dev)(const void *mem);      /* a drawing engine at a window's pixels, or NULL (NULL: none) */
};
enum {
    WM_MEM_SMALL,                       /* the manager's own things */
    WM_MEM_APP,                         /* an app's window's pixels (whole pages, which the app can have mapped) */
    WM_MEM_DESK,                        /* the desktop's own window's: anywhere, the card's memory too (M7 step E) */
    WM_MEM_RAM                          /* ... but in the system's memory */
};

struct wm {
    struct wm_host host;
    struct wm_look look;
    struct gfx_fmt fmt;                 /* the screen's */
    s32 w, h;
    struct wm_window *top, *bottom, *focus;
    struct wm_window *bar;              /* the task bar, or NULL */
    struct gfx_region dirty;            /* what of the screen is to be drawn again */
    s32 px, py;                         /* the pointer's hot spot */
    u8 pointer;                         /* shown */
    u8 hw_pointer;                      /* the card shows it (its hardware cursor): not drawn here */
    u8 ghost_on;
    struct gfx_rect ghost;              /* an outline above every window: where one is being moved or sized to */
    u32 next_id, count;
    char clock[8], note[16];            /* the task bar's right end: the time, and a note (a remote session's, D35) */
};

extern const struct wm_look wm_look_default;

void wm_init(struct wm *m, const struct wm_host *host, s32 w, s32 h, const struct gfx_fmt *fmt);
void wm_screen(struct wm *m, s32 w, s32 h, const struct gfx_fmt *fmt); /* another screen: every window's pixels are
                                                                           kept if the format is the same, cleared if not */

/* A window with a client area of cw x ch at (x, y) on the screen (its frame's
   corner), or NULL: no memory. Hidden until wm_show(). */
struct wm_window *wm_create(struct wm *m, s32 x, s32 y, s32 cw, s32 ch, const char *title, u32 flags, void *owner);
void wm_destroy(struct wm *m, struct wm_window *w);
void wm_show(struct wm *m, struct wm_window *w, int on);
void wm_move(struct wm *m, struct wm_window *w, s32 x, s32 y);
int wm_resize(struct wm *m, struct wm_window *w, s32 cw, s32 ch);       /* its pixels kept from the top left; -1: no memory */
void wm_raise(struct wm *m, struct wm_window *w);       /* above the others of its kind */
void wm_set_focus(struct wm *m, struct wm_window *w);   /* (and raised); NULL: none */
void wm_title(struct wm *m, struct wm_window *w, const char *title);
void wm_ext(struct wm *m, struct wm_window *w, const struct gfx_surface *s);   /* it shows s from now on (NULL: its
                                                   own pixels again); dropped when its size or the screen's format changes */
void wm_damage(struct wm *m, struct wm_window *w, const struct gfx_rect *r);    /* its owner drew there (client
                                                                                    coordinates; NULL: all of it) */
void wm_client(const struct wm *m, const struct wm_window *w, struct gfx_rect *out);    /* its client area on the screen */
struct wm_window *wm_find(const struct wm *m, u32 id);
struct wm_window *wm_hit(const struct wm *m, s32 x, s32 y, int *part);  /* the window at a point, and where in it */
struct wm_window *wm_next(const struct wm *m, const struct wm_window *w);       /* the next to give the focus (Alt-Tab) */

void wm_pointer(struct wm *m, s32 x, s32 y, int shown);
void wm_hw_pointer(struct wm *m, int on);       /* the card shows the pointer from now on (1), or this does (0) */
int wm_store_again(struct wm *m, struct wm_window *w, int kind);        /* the window's pixels moved into memory of
                                                   that kind (WM_MEM_DESK or WM_MEM_RAM; not an app's): 0, or -1 (none) */
void wm_pointer_masks(u8 and_mask[512], u8 xor_mask[512]);     /* the pointer as a 64 x 64 AND/XOR cursor, its hot
                                                   spot the top left: colour 0 black, colour 1 white */
void wm_ghost(struct wm *m, const struct gfx_rect *r);  /* the outline, two pixels wide; NULL: none */
void wm_bar(struct wm *m, int on);                      /* the task bar: made, or taken away */
void wm_bar_text(struct wm *m, const char *clock, const char *note);
int wm_bar_mark(const struct wm *m, s32 x, s32 y);     /* GLOS's mark at the task bar's start (the launcher) */
struct wm_window *wm_bar_hit(const struct wm *m, s32 x, s32 y); /* the window whose button is at a point of the bar */

void wm_damage_all(struct wm *m);
int wm_compose(struct wm *m, struct gfx_surface *screen);       /* what is dirty, drawn; 1 if anything was */

#endif
