/* The window manager's core (kernel/wm/wm.c; PRD §9.4; docs/graphics.md §3):
 * windows that keep their pixels, their order, the focus, and the composer
 * that puts them on the screen. Pure code on gfx surfaces, like the 2D
 * library: tests/host/wm_test.c runs it on the host. The kernel's side
 * (kernel/wm/desk.c) gives it memory, the screen, a thread and input. */
#ifndef K_WM_H
#define K_WM_H
#include "gfx.h"

#define WM_TITLE_MAX 64
#define WM_TRAY 12                      /* icons in the task bar's tray at once */

/* A window's flags. */
#define WF_SHOWN   0x0001               /* on the screen (when nothing covers it) */
#define WF_FRAME   0x0002               /* it has a title bar and a border, which the manager draws */
#define WF_FIXED   0x0004               /* its size is not the user's to change */
#define WF_TOPMOST 0x0008               /* above every window that isn't (the task bar) */
#define WF_NOFOCUS 0x0010               /* never the focus (the task bar) */
#define WF_POPUP   0x0020               /* an app's menu (KWIN_POPUP): topmost and bare; never given the focus
                                           when another window's goes, and a press outside it takes it away */
#define WF_DESKTOP 0x0040               /* the shell's desktop (KWIN_DESKTOP): under every other window, always */
#define WF_MIN     0x0080               /* minimised: off the screen (WF_SHOWN clear), its button still on the task bar */
#define WF_MAX     0x0100               /* maximised: it fills the work area (the screen less the task bar) */
#define WF_FULL    0x0200               /* full screen: no frame, the whole screen; the task bar goes while it or
                                           another window of its owner's has the focus */
#define WF_SNAP_L  0x0400               /* snapped to the work area's left half ... */
#define WF_SNAP_R  0x0800               /* ... or its right */
#define WF_NOBAR   0x1000               /* no button on the task bar (a tool window) */
#define WF_NOMIN   0x2000               /* no minimise button (a dialog) */
#define WF_MODAL   0x4000               /* while it is shown, its parent takes no input and no focus */
#define WF_WITH    0x8000               /* hidden because its parent was minimised: back with it */
#define WF_HUNG    0x10000              /* its owner isn't taking its events (the kernel's side says) */
#define WF_EMBED   0x20000              /* shown inside its parent's client area (wm_embed): bare, with no button of
                                           its own, where and as large as its parent has it */
#define WF_PLACED  (WF_MAX | WF_FULL | WF_SNAP_L | WF_SNAP_R)   /* its place is the manager's, not its own */

/* A window's state (wm_state). */
enum { WS_NORMAL, WS_MIN, WS_MAX, WS_FULL, WS_SNAP_L, WS_SNAP_R, WS_UNMIN };

/* Where a point is in a window (wm_hit). WH_SIZE with WD_ bits: an edge or a corner the user sizes it by, which
   reaches WM_GRAB pixels outside the frame; WH_GROW is the bottom right corner, the client's own pixels there too. */
enum { WH_NONE, WH_CLIENT, WH_TITLE, WH_CLOSE, WH_BORDER, WH_MIN, WH_MAX, WH_SIZE = 16 };
#define WD_N 1
#define WD_S 2
#define WD_W 4
#define WD_E 8
#define WH_GROW (WH_SIZE | WD_S | WD_E)
#define WM_GRAB 3

/* The pointer's shapes (wm_cursor). */
enum { WC_ARROW, WC_BUSY, WC_TEXT, WC_HAND, WC_CROSS, WC_SIZE_NS, WC_SIZE_EW, WC_SIZE_NWSE, WC_SIZE_NESW, WC_MOVE,
       WC_COUNT, WC_CUSTOM = WC_COUNT,          /* WC_CUSTOM: a window's own picture (its cursor_img) */
       WC_DROP_COPY, WC_DROP_MOVE, WC_DROP_LINK, WC_DROP_NONE,  /* a drag's (the desktop's own: no window asks for them) */
       WC_ALL };
struct wm_cursor {
    u8 w, h, hx, hy;                    /* its size (32 by 32 at most) and its hot spot */
    u8 edge[32][4], fill[32][4];        /* a bit a pixel: black, and white; neither: the screen shows through */
};

struct wm_window {
    struct wm_window *above, *below;
    u32 id;
    void *owner;                        /* whose it is (the kernel's side says: an app, or 0) */
    u32 flags;
    struct gfx_rect frame;              /* on the screen, title bar and border included */
    struct gfx_rect normal;             /* while WF_PLACED: the frame's corner and the client's size to go back to */
    s32 min_w, min_h, max_w, max_h;     /* the client area's limits for the user (0: the manager's) */
    u8 cursor;                          /* WC_: the pointer's shape over its client area */
    u32 tag;                            /* its owner's own word for it (the API's kind and flags) */
    u32 parent;                         /* the window it belongs to (a dialog's owner), by id; 0: none. It stays
                                           in front of its parent, and is minimised and brought back with it */
    u8 flash;                           /* its task-bar button asks for attention until it has the focus */
    u8 progress;                        /* 1 to 101: a bar along its task-bar button, 0 to 100 in a hundred; 0: none */
    u8 has_icon;
    u32 icon[16 * 16];                  /* its picture, 16 by 16: 0xAARRGGBB, drawn where AA is 80h or more */
    struct wm_cursor cursor_img;        /* its own pointer (cursor == WC_CUSTOM) */
    char help[32];                      /* its owner's page in Help */
    struct gfx_surface store;           /* the client area's pixels, in the screen's format */
    void *mem;                          /* ... and their memory, from wm_host.alloc */
    struct gfx_surface ext;             /* what it shows instead, if has_ext: pixels that aren't the manager's (an
                                           app's in the card's memory, M8), of the client area's size and format */
    u8 has_ext;
    struct gfx_rect embed_at;           /* WF_EMBED: where in its parent's client area */
    u32 embed_flags;                    /* ... and what it was before: its flags, its corner, its client's size */
    s32 embed_x, embed_y, embed_w, embed_h;
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
    u8 cursor;                          /* WC_: its shape now */
    struct wm_cursor custom;            /* ... WC_CUSTOM's picture */
    u32 cursor_gen;                     /* counts its changes of shape (the card's cursor is loaded again) */
    u8 pointer;                         /* shown */
    u8 hw_pointer;                      /* the card shows it (its hardware cursor): not drawn here */
    u8 ghost_on;
    struct gfx_rect ghost;              /* an outline above every window: where one is being moved or sized to */
    u32 next_id, count;
    char clock[12], note[16];            /* the task bar's right end: the time, and a note (a remote session's, D35) */
    struct {                            /* ... and the tray's icons, left of them (API 2.0's SHELL TRAY) */
        u32 id;
        u32 icon[16 * 16];              /* 0xAARRGGBB, drawn where AA is 80h or more */
    } tray[WM_TRAY];
    u32 ntray, tray_next;
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
/* A window shown inside another's client area (API 2.0's EMBED): it loses its frame and its button, belongs to
   the host (in front of it, minimised with it), and is never the user's to move or size. -1: not these two (one
   is the other, the host is embedded itself, w has windows of its own or a place of the manager's). The caller
   then puts it where wm_embed_where says, and again whenever the host has moved or changed size. */
int wm_embed(struct wm *m, struct wm_window *w, struct wm_window *host, const struct gfx_rect *at);
void wm_unembed(struct wm *m, struct wm_window *w, s32 *cw, s32 *ch);   /* its own window again, at its old corner;
                                                   *cw, *ch: its old size, the caller's to give it back */
int wm_embed_where(const struct wm *m, const struct wm_window *w, struct gfx_rect *out, int *host_shown); /* 1: its
                                                   corner on the screen and its size now (what of its rectangle
                                                   lies in the host's client area; a pixel at least); 0: no host */
struct wm_window *wm_find(const struct wm *m, u32 id);
struct wm_window *wm_hit(const struct wm *m, s32 x, s32 y, int *part);  /* the window at a point, and where in it */
struct wm_window *wm_next(const struct wm *m, const struct wm_window *w);       /* the next to give the focus (Alt-Tab) */

/* The window's state: WS_NORMAL, WS_MAX, WS_FULL and the snaps are its place (one of them); WS_MIN takes it off
   the screen as it is, and WS_UNMIN puts it back. 0, or -1: no memory for its pixels at the new size (nothing
   changed). Its size may have changed: its owner is to be told. */
int wm_state(struct wm *m, struct wm_window *w, int state);
int wm_state_of(const struct wm_window *w);     /* WS_MIN while minimised, else its place */
void wm_work(const struct wm *m, struct gfx_rect *out);         /* the screen less the task bar */
void wm_limits(const struct wm *m, const struct wm_window *w, s32 *min_w, s32 *min_h, s32 *max_w, s32 *max_h);
int wm_can(const struct wm_window *w, int part);        /* it has that box or edge: WH_MIN, WH_MAX, WH_SIZE */
void wm_cursor(struct wm *m, int shape, const struct wm_cursor *custom);        /* the pointer's shape: WC_ (custom:
                                                                                    WC_CUSTOM's picture) */
struct wm_window *wm_modal_for(const struct wm *m, const struct wm_window *w);  /* the shown modal window that keeps
                                                   w from input (its child, or a child's), or NULL */
void wm_bar_again(struct wm *m);                /* a window's button changed (flash, progress, icon): drawn again */
void wm_frame_again(struct wm *m, struct wm_window *w); /* its title bar changed (icon, WF_HUNG): drawn again */
void wm_topmost(struct wm *m, struct wm_window *w, int on);     /* above the ordinary windows, or one of them again */
int wm_cursor_for(const struct wm_window *w, int part); /* the shape for a part of a window (wm_hit's) */
const struct wm_cursor *wm_cursor_shape(int shape);

void wm_pointer(struct wm *m, s32 x, s32 y, int shown);
void wm_hw_pointer(struct wm *m, int on);       /* the card shows the pointer from now on (1), or this does (0) */
int wm_store_again(struct wm *m, struct wm_window *w, int kind);        /* the window's pixels moved into memory of
                                                   that kind (WM_MEM_DESK or WM_MEM_RAM; not an app's): 0, or -1 (none) */
void wm_pointer_masks(const struct wm *m, u8 and_mask[512], u8 xor_mask[512], s32 *hx, s32 *hy);
                                                /* the pointer's shape now as a 64 x 64 AND/XOR cursor (colour 0
                                                   black, colour 1 white), and its hot spot in it */
void wm_ghost(struct wm *m, const struct gfx_rect *r);  /* the outline, two pixels wide; NULL: none */
void wm_bar(struct wm *m, int on);                      /* the task bar: made, or taken away */
void wm_bar_text(struct wm *m, const char *clock, const char *note);
int wm_bar_mark(const struct wm *m, s32 x, s32 y);     /* GLOS's mark at the task bar's start (the launcher) */
struct wm_window *wm_bar_hit(const struct wm *m, s32 x, s32 y); /* the window whose button is at a point of the bar */
/* The tray: icons at the task bar's right, left of the note and the clock, in the order they came. */
u32 wm_tray_add(struct wm *m, const u32 *icon);         /* its id, or 0: the tray is full */
void wm_tray_icon(struct wm *m, u32 id, const u32 *icon);
void wm_tray_remove(struct wm *m, u32 id);
u32 wm_tray_hit(const struct wm *m, s32 x, s32 y);      /* the icon at a point of the screen, 0 none */
enum { WB_START = 1, WB_BAR, WB_TRAY, WB_CLOCK, WB_WORK };
int wm_bar_part(const struct wm *m, int part, u32 tray_id, struct gfx_rect *r);  /* where a part of the bar is on the
                                                           screen (WB_; WB_TRAY with an id: that icon): 1, or 0 if
                                                           it isn't there */

void wm_damage_all(struct wm *m);
int wm_compose(struct wm *m, struct gfx_surface *screen);       /* what is dirty, drawn; 1 if anything was */

#endif
