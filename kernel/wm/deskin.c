/* The desktop's input (kernel/include/desk.h; PRD §9.4, D34; docs/graphics.md
 * §4): the keyboard and the mouse while the desktop has the screen.
 *
 * The 8042's code (kernel/vm/vkbc.c) hands every byte over from its
 * interrupt (desk_in), real or injected by the agent (glos key, glos
 * mouse); the composer's thread takes them from a ring in its turn, with
 * the manager locked. Only the kill hotkey is carried out at once, from the
 * interrupt: it has to work when nothing else does.
 *
 * Keys are set 1 scan codes, as the controller translates them. A key goes
 * to the app whose window has the focus, as an event (and as the keyboard
 * a DOS program reads: kernel/app/dev.c), with its character by the US
 * layout. The manager keeps a few for itself, with Alt:
 *     Alt-Tab   the focus to the next window (a minimised one comes back)
 *     Alt-F4    the window is asked to close
 *     Alt-F7    the window is moved with the arrow keys
 *     Alt-F8    ... or given another size
 *     Alt-F9    the window is minimised
 *     Alt-F10   ... maximised, or put back
 * In Alt-F7 and Alt-F8 an outline follows the arrows (eight pixels a step,
 * one with Shift), Enter makes it so and Esc leaves things alone. The mouse
 * does the same by the title bar, and by any edge or corner (the pointer
 * shows which way), with the same outline: a window's pixels are not moved
 * until the button is let go, which a 486 would be slow at. A press
 * anywhere on a window gives it the focus; the title bar's boxes (minimise,
 * maximise or restore, close) act when the button is let go on the one it
 * went down on; a double click on the title bar maximises or restores. A
 * window dragged by its title to the screen's left or right edge takes that
 * half of the work area, and to the top all of it (the outline shows it);
 * dragged away again it has its own size back. A button on the task bar
 * gives its window the focus, brings a minimised one back, and minimises
 * the one that has the focus.
 *
 * An app gets the keys that went down while it had the focus, and the up
 * of each, whatever happens between: when the focus leaves, the keys still
 * down are let go for it first. It gets the mouse while the pointer is in
 * the client area of its window with the focus, and, from a button's press
 * there, until every button is up again, wherever the pointer goes. */
#include <string.h>

#include "kapi.h"
#include "app.h"
#include "deskint.h"
#include "idle.h"
#include "io.h"
#include "keymap.h"
#include "kprintf.h"
#include "timer.h"
#include "vm.h"
#include "vnc.h"

struct in {
    u8 what, c;
    short a, b;
};
static struct in ring[256];
static volatile u32 rh, rt;

enum { KM_LSHIFT = 1, KM_RSHIFT = 2, KM_LCTRL = 4, KM_RCTRL = 8, KM_LALT = 16, KM_RALT = 32 };
#define KM_SHIFT (KM_LSHIFT | KM_RSHIFT)
#define KM_CTRL  (KM_LCTRL | KM_RCTRL)
#define KM_ALT   (KM_LALT | KM_RALT)

static u8 e0, e1;                               /* a prefix came; bytes of a Pause still to come */
static u8 kmods, caps, num;
static u8 isdown[64];                           /* keys that are down, a bit each (200h of them) */
static u8 held[64];                             /* ... and those the focus's owner saw go down */
static u8 pkt[4], npkt;                         /* the mouse's packet so far */
static u32 pkt_at;
static u8 buttons;

static u32 foc_id;                              /* the window the focus's owner was last told of */
static struct app *volatile foc_app;
static u32 grab_id;                             /* the window the mouse's events go to while a button is down */
static u32 cap_id;                              /* the window that has captured the mouse (API 2.0's CAPTURE) */
static u32 press_at, press_id, press_bit, press_n;      /* the last press in a client area: when, whose, which
                                                           button, and how many in a row (a double click's 2) */
static s32 press_x, press_y;
static u32 box_id;                              /* the window whose title-bar box the button went down on ... */
static u8 box_part;                             /* ... and which (WH_CLOSE, WH_MIN, WH_MAX) */
static u32 click_at, click_id;                  /* the last press on a title bar: when, and whose */
static s32 click_x, click_y;

enum { D_NONE, D_MOVE, D_SIZE, D_KEY_MOVE, D_KEY_SIZE };
static u8 drag;
static u8 drag_dir;                             /* D_SIZE: the edges that follow the pointer (WD_) */
static u8 drag_snap;                            /* D_MOVE: the state the outline shows now (WS_; 0: its own place) */
static u8 snap = 1;                             /* dragging to an edge snaps ([desktop] snap) */
static u32 drag_id;
static s32 drag_x, drag_y;                      /* D_MOVE: the pointer from the outline's corner; D_SIZE: where it began */
static struct gfx_rect drag_from;               /* the outline when it began */

static u32 double_ticks = 512;                  /* two presses this close are a double click ([input] double_click) ... */
#define DOUBLE_PX    4                          /* ... and this near */

u32 deskin_double_ticks(void) { return double_ticks; }
void deskin_double_click(u32 ms) { double_ticks = ms >= 100 && ms <= 2000 ? ms + ms * 24 / 1000 : 512; }

void deskin_snap(int on) { snap = on != 0; }

/* From the 8042's code, interrupts off. */
void desk_in(int what, int a, int b, int c)
{
    u32 f = irq_save();
    if (what == DESK_KILL) {
        if (foc_app)
            app_kill(foc_app, "hotkey");
    } else if (rt - rh < ARRAY_SIZE(ring)) {
        struct in *e = &ring[rt++ % ARRAY_SIZE(ring)];
        if (what != DESK_RESET)
            idle_touch();                       /* (somebody is there: kernel/wm/idle.c) */
        e->what = (u8)what;
        e->a = (short)a;
        e->b = (short)b;
        e->c = (u8)c;
    }
    irq_restore(f);
    desk_kick();
}

/* An injected key waits (glos key and glos type, through vkbc_key) while the input waiting here and the events
   the app with the keyboard hasn't read come to half its queue (256): a slow app (on a 486) lost keys. */
int desk_in_busy(void)
{
    struct app *a = foc_app;
    return rt - rh + (a ? app_event_backlog(a) : 0) >= 128;
}

/* What desk_in_busy() saw, for the log. */
const char *desk_focus_line(void)
{
    static char b[64];
    u32 f = irq_save();
    struct app *a = foc_app;
    ksnprintf(b, sizeof b, "app=%u window=%u backlog=%u input=%u", a ? app_id(a) : 0, foc_id,
              a ? app_event_backlog(a) : 0, rt - rh);
    irq_restore(f);
    return b;
}

/* ---- events to apps */

static u32 mods_now(void)
{
    return (kmods & KM_SHIFT ? GLOS_MOD_SHIFT : 0u) | (kmods & KM_CTRL ? GLOS_MOD_CTRL : 0u) | (kmods & KM_ALT ? GLOS_MOD_ALT : 0u);
}

static void send_key2(struct app *a, u32 id, u32 key, int down, u32 ch, u32 mods, int again)
{
    struct kevent e;
    memset(&e, 0, sizeof e);
    e.type = KEV_KEY;
    e.window = id;
    e.buttons = (unsigned)again;                /* (for API 2.0's apps: the keyboard repeating a held key) */
    e.key = key;
    e.ch = ch;
    e.mods = mods;
    e.down = (unsigned)down;
    app_event(a, &e);
}

static void send_key(struct app *a, u32 id, u32 key, int down, u32 ch, u32 mods) { send_key2(a, id, key, down, ch, mods, 0); }

/* ---- the shell (GLOS_FN_SHELL, M9): the launcher and the task list are its */

static struct app *shell;

void desk_shell(struct app *a, int on)
{
    if (on)
        shell = a;
    else if (shell == a)
        shell = 0;
}

struct app *desk_shell_app(void) { return shell; }

/* API 2.0's DESKTOP: one shell at a time. */
int desk_shell_set(struct app *a, int on)
{
    if (on && shell && shell != a)
        return -1;
    desk_shell(a, on);
    return 0;
}

static int to_shell(struct wm *m, unsigned type)
{
    struct kevent e;
    if (!shell)
        return 0;
    memset(&e, 0, sizeof e);
    e.type = type;
    e.x = 0;
    e.y = m->bar ? m->bar->frame.y : m->h;      /* (the launcher's bottom left: the task bar's top) */
    app_event(shell, &e);
    return 1;
}

static void send_focus(struct app *a, u32 id, int got)
{
    struct kevent e;
    memset(&e, 0, sizeof e);
    e.type = KEV_FOCUS;
    e.window = id;
    e.down = (unsigned)got;
    app_event(a, &e);
}

void deskin_focus(struct wm *m)
{
    struct wm_window *w = m->focus;
    u32 id = w ? w->id : 0, k, f;
    if (id == foc_id)
        return;
    if (foc_app) {
        for (k = 0; k < 0x200; k++)             /* the keys it still has down: let go, as far as it knows */
            if (held[k >> 3] & (1u << (k & 7)))
                send_key(foc_app, foc_id, k, 0, 0, 0);
        send_focus(foc_app, foc_id, 0);
    }
    memset(held, 0, sizeof held);
    f = irq_save();
    foc_id = id;
    foc_app = w ? w->owner : 0;
    irq_restore(f);
    if (cap_id) {                               /* the focus has left the app that captured the mouse: let go */
        struct wm_window *c = wm_find(m, cap_id);
        if (!c || c->owner != foc_app)
            cap_id = 0;
    }
    if (foc_app)
        send_focus(foc_app, id, 1);
}

void deskin_gone(struct app *a)
{
    u32 f = irq_save();
    if (foc_app == a) {
        foc_app = 0;
        foc_id = 0;
        memset(held, 0, sizeof held);
    }
    irq_restore(f);
}

void desk_refocus(struct wm *m)
{
    struct wm_window *w;
    if (m->focus)
        return;
    for (w = m->top; w; w = w->below)
        if ((w->flags & WF_SHOWN) && !(w->flags & (WF_NOFOCUS | WF_POPUP))) {
            wm_set_focus(m, w);
            return;
        }
}

static void pointer_shape(struct wm *m);
static void dnd_finish(struct wm *m, int drop);
static void toggle_max(struct wm *m, struct wm_window *w);
static void activate(struct wm *m, struct wm_window *w);

/* ---- moving and sizing: the outline */

static void drag_stop(struct wm *m)
{
    wm_ghost(m, 0);
    drag = D_NONE;
}

/* The outline kept to what a window may be: within its limits and the
   screen, its title bar never out of reach. dir: the edges being dragged
   (WD_; 0: it is being moved), which give way; the others stay. */
static void outline(struct wm *m, const struct wm_window *w, struct gfx_rect g, int dir)
{
    s32 ew = w->frame.w - w->store.w, eh = w->frame.h - w->store.h;     /* the frame's own part */
    s32 floor = m->h - (m->bar ? m->bar->frame.h : 0) - m->look.title_h;
    s32 right = g.x + g.w, bottom = g.y + g.h, lw, lh, hw, hh;
    wm_limits(m, w, &lw, &lh, &hw, &hh);
    if (hw + ew > m->w)
        hw = m->w - ew;
    if (hh + eh > m->h)
        hh = m->h - eh;
    if (g.w < lw + ew)
        g.w = lw + ew;
    if (g.h < lh + eh)
        g.h = lh + eh;
    if (g.w > hw + ew && hw >= lw)
        g.w = hw + ew;
    if (g.h > hh + eh && hh >= lh)
        g.h = hh + eh;
    if (dir & WD_W)
        g.x = right - g.w;
    if (dir & WD_N)
        g.y = bottom - g.h;
    if (g.y < 0) {
        if (dir & WD_N)
            g.h = bottom > lh + eh ? bottom : lh + eh;
        g.y = 0;
    }
    if (!dir) {
        if (g.y > floor)
            g.y = floor;
        if (g.x > m->w - 40)
            g.x = m->w - 40;
        if (g.x < 40 - g.w)
            g.x = 40 - g.w;
    }
    wm_ghost(m, &g);
}

/* The frame the window would have in its own place. */
static void normal_frame(const struct wm_window *w, struct gfx_rect *f)
{
    *f = w->frame;
    if (w->flags & WF_PLACED) {
        f->w = w->normal.w + (w->frame.w - w->store.w);
        f->h = w->normal.h + (w->frame.h - w->store.h);
    }
}

static void drag_start(struct wm *m, struct wm_window *w, int how, int dir)
{
    struct gfx_rect g = w->frame;
    drag = (u8)how;
    drag_id = w->id;
    drag_dir = (u8)dir;
    drag_snap = 0;
    if (how == D_MOVE || how == D_KEY_MOVE) {
        if (w->flags & (WF_MAX | WF_SNAP_L | WF_SNAP_R)) {      /* dragged out of its place: its own size again, */
            normal_frame(w, &g);                                /* under the pointer where the pointer was in it */
            g.x = m->px - (m->px - w->frame.x) * g.w / (w->frame.w > 0 ? w->frame.w : 1);
        }
        drag_x = m->px - g.x;
        drag_y = m->py - g.y;
    } else {
        drag_x = m->px;
        drag_y = m->py;
    }
    drag_from = g;
    outline(m, w, g, dir);
}

/* Where the pointer is at an edge of the screen with a window being moved: the state it would snap to. */
static int snap_at(const struct wm *m, const struct wm_window *w)
{
    if (!snap || !wm_can(w, WH_MAX))
        return 0;
    return m->py <= 0 ? WS_MAX : m->px <= 0 ? WS_SNAP_L : m->px >= m->w - 1 ? WS_SNAP_R : 0;
}

/* The outline follows the pointer. */
static void drag_to(struct wm *m, struct wm_window *w)
{
    struct gfx_rect g = drag_from;
    if (drag == D_MOVE) {
        drag_snap = (u8)snap_at(m, w);
        if (drag_snap) {
            wm_work(m, &g);
            if (drag_snap == WS_SNAP_L)
                g.w /= 2;
            else if (drag_snap == WS_SNAP_R)
                g.x = g.w / 2, g.w -= g.x;
            wm_ghost(m, &g);
            return;
        }
        g.x = m->px - drag_x, g.y = m->py - drag_y;
    } else {
        s32 dx = m->px - drag_x, dy = m->py - drag_y;
        if (drag_dir & WD_E)
            g.w += dx;
        if (drag_dir & WD_W)
            g.x += dx, g.w -= dx;
        if (drag_dir & WD_S)
            g.h += dy;
        if (drag_dir & WD_N)
            g.y += dy, g.h -= dy;
    }
    outline(m, w, g, drag == D_MOVE ? 0 : drag_dir);
}

/* The move or the new size is made (or not). */
static void drag_end(struct wm *m, int make)
{
    struct wm_window *w = wm_find(m, drag_id);
    struct gfx_rect g = m->ghost;
    int size = drag == D_SIZE || drag == D_KEY_SIZE, to = drag_snap;
    drag_stop(m);
    drag_snap = 0;
    if (!w || !make)
        return;
    if (size) {
        w->flags &= ~(u32)(WF_SNAP_L | WF_SNAP_R);     /* (a snapped one sized by hand is in a place of its own) */
        deskapp_resize(m, w, g.w - (w->frame.w - w->store.w), g.h - (w->frame.h - w->store.h));
        wm_move(m, w, g.x, g.y);
    } else if (to) {
        deskapp_state(m, w, to);
    } else {
        struct gfx_rect old = w->frame;
        struct wm_window *o;
        int clear;
        if (w->flags & WF_PLACED) {             /* out of its place: its own size, where the outline is */
            deskapp_state(m, w, WS_NORMAL);
            wm_move(m, w, g.x, g.y);
            return;
        }
        clear = old.x + old.w <= g.x || g.x + old.w <= old.x || old.y + old.h <= g.y || g.y + old.h <= old.y;
        for (o = w->above; o && clear; o = o->above)    /* (VNC's CopyRect: nothing above it at either place) */
            if ((o->flags & WF_SHOWN) && ((o->frame.x < old.x + old.w && old.x < o->frame.x + o->frame.w && o->frame.y < old.y + old.h
                              && old.y < o->frame.y + o->frame.h)
                             || (o->frame.x < g.x + old.w && g.x < o->frame.x + o->frame.w && o->frame.y < g.y + old.h
                                 && g.y < o->frame.y + o->frame.h)))
                clear = 0;
        if (clear && (w->flags & WF_SHOWN))
            vnc_moved(old.x, old.y, old.w, old.h, g.x - old.x, g.y - old.y);
        wm_move(m, w, g.x, g.y);
    }
}

/* A key while the arrows move the outline. */
static void drag_key(struct wm *m, u32 key)
{
    struct wm_window *w = wm_find(m, drag_id);
    struct gfx_rect g = m->ghost;
    s32 step = kmods & KM_SHIFT ? 1 : 8, dx = 0, dy = 0;
    if (!w) {
        drag_stop(m);
        return;
    }
    switch (key & 0xFF) {
    case 0x48: dy = -step; break;
    case 0x50: dy = step; break;
    case 0x4B: dx = -step; break;
    case 0x4D: dx = step; break;
    case 0x1C: drag_end(m, 1); return;
    case 0x01: drag_end(m, 0); return;
    default: return;
    }
    if (drag == D_KEY_MOVE)
        g.x += dx, g.y += dy;
    else
        g.w += dx, g.h += dy;
    outline(m, w, g, drag == D_KEY_MOVE ? 0 : WD_S | WD_E);
}

/* ---- what the title bar's boxes, the task bar's buttons and their keys do */

static void toggle_max(struct wm *m, struct wm_window *w)
{
    if (wm_can(w, WH_MAX))
        deskapp_state(m, w, w->flags & (WF_MAX | WF_SNAP_L | WF_SNAP_R) ? WS_NORMAL : WS_MAX);
}

/* The focus to w: a minimised one comes back first. */
static void activate(struct wm *m, struct wm_window *w)
{
    if (w && (w->flags & WF_MIN))
        deskapp_state(m, w, WS_UNMIN);
    wm_set_focus(m, w);
}

/* ---- the keyboard */

static u32 key_char(u32 key)
{
    return keymap_char(key, (kmods & KM_SHIFT) != 0, (kmods & KM_CTRL) != 0, (kmods & KM_ALT) != 0, caps != 0, num != 0);
}

/* The saver is on the screen (idle.c): the input that ends it is used up here, so that the key that wakes the
   screen types nothing and the press that wakes it presses nothing. A key that went down then is nobody's until
   it has come up, and the mouse's buttons until they are all up. */
static u8 dnd_on;                               /* an app's drag has the pointer (API 2.0's DND) */
static u32 dnd_pic;                             /* ... and this window is its picture (0: none) */
#define DND_PX 14                               /* the picture's corner from the pointer's point: never under it */
#define DND_PY 20

static u8 asleep;                               /* this turn began with the saver on */
static u8 woke_key[0x200 / 8];                  /* keys whose press was used up so */
static u8 woke_buttons;                         /* ... and the mouse's buttons */

static void key_event(struct wm *m, u32 key, int down)
{
    struct wm_window *w = m->focus;
    u8 bit = (u8)(1u << (key & 7)), mod = 0;
    int again;

    if (asleep || (woke_key[key >> 3] & bit)) {
        woke_key[key >> 3] = (u8)(down && (asleep || (woke_key[key >> 3] & bit)) ? woke_key[key >> 3] | bit
                                                                                  : woke_key[key >> 3] & ~bit);
        return;
    }
    if (dnd_on && key == 0x01 && down) {        /* Esc: the drag is given up (and the key is nobody's) */
        woke_key[key >> 3] |= bit;
        dnd_finish(m, 0);
        return;
    }
    again = down && (isdown[key >> 3] & bit);           /* the keyboard repeating a key held down */

    isdown[key >> 3] = (u8)(down ? isdown[key >> 3] | bit : isdown[key >> 3] & ~bit);
    switch (key) {
    case 0x2A: mod = KM_LSHIFT; break;
    case 0x36: mod = KM_RSHIFT; break;
    case 0x1D: mod = KM_LCTRL; break;
    case 0x11D: mod = KM_RCTRL; break;
    case 0x38: mod = KM_LALT; break;
    case 0x138: mod = KM_RALT; break;
    case 0x3A: caps ^= (u8)(down && !again); break;
    case 0x45: num ^= (u8)(down && !again); break;
    }
    kmods = (u8)(down ? kmods | mod : kmods & ~mod);

    /* Every key is the outline's meanwhile (one let go still goes to whoever saw it go down). */
    if (down && (drag == D_KEY_MOVE || drag == D_KEY_SIZE)) {
        drag_key(m, key);
        return;
    }
    if (down && !again && ((key == 0x01 && (kmods & KM_CTRL) && !(kmods & KM_ALT)) || key == 0x15B)
        && to_shell(m, KEV_LAUNCH))
        return;                                 /* Ctrl-Esc, the Windows key: the launcher (the shell's, M9) */
    if (down && (kmods & KM_ALT) && !(kmods & KM_CTRL)) {
        switch (key) {
        case 0x0F:
            activate(m, wm_next(m, w));
            return;
        case 0x3E:
            if (w && !again)
                deskapp_close(m, w);
            return;
        case 0x41: case 0x42:
            if (w && !again && !drag && !(w->flags & WF_FULL) && !(key == 0x42 && !wm_can(w, WH_SIZE)))
                drag_start(m, w, key == 0x41 ? D_KEY_MOVE : D_KEY_SIZE, key == 0x41 ? 0 : WD_S | WD_E);
            return;
        case 0x43:                              /* Alt-F9: minimised */
            if (w && !again && !drag && wm_can(w, WH_MIN)) {
                deskapp_state(m, w, WS_MIN);
                desk_refocus(m);
            }
            return;
        case 0x44:                              /* Alt-F10: maximised, or back */
            if (w && !again && !drag)
                toggle_max(m, w);
            return;
        }
    }
    deskin_focus(m);                            /* (a key just before this one may have moved it) */
    w = m->focus;
    if (deskpass_key(m, key, down, down ? key_char(key) : 0))
        return;                                 /* (the secrets' password dialog has the keyboard: deskpass.c) */
    if (!w || !w->owner)
        return;
    if (!down && !(held[key >> 3] & bit))       /* (the up of a key it never saw go down) */
        return;
    held[key >> 3] = (u8)(down ? held[key >> 3] | bit : held[key >> 3] & ~bit);
    send_key2(foc_app, foc_id, key, down, down ? key_char(key) : 0, mods_now(), again);
}

static void key_byte(struct wm *m, u8 b)
{
    u32 key;
    if (e1) {                                   /* Pause: E1 1D 45, E1 9D C5 */
        e1--;
        return;
    }
    if (b == 0xE1) {
        e1 = 2;
        return;
    }
    if (b == 0xE0) {
        e0 = 1;
        return;
    }
    key = (b & 0x7Fu) | (e0 ? 0x100u : 0u);
    e0 = 0;
    if (!(b & 0x7F) || (b & 0x7F) > 0x5D)       /* (an answer to a command, an overrun: not a key) */
        return;
    if (key == 0x12A || key == 0x136)           /* (the shifts an extended key brings with it) */
        return;
    key_event(m, key, !(b & 0x80));
}

/* ---- the mouse */

/* A mouse event for the app with window w: the pointer in its client area. */
static void send_mouse(struct wm *m, struct wm_window *w, s32 dx, s32 dy, u32 button, int down)
{
    struct kevent e;
    struct gfx_rect c;
    wm_client(m, w, &c);
    memset(&e, 0, sizeof e);
    e.type = KEV_MOUSE;
    e.window = w->id;
    if (button && down) {                       /* presses in a row, close in time and place (for API 2.0's apps) */
        u32 now = timer_ticks();
        s32 ax = m->px - press_x, ay = m->py - press_y;
        if (press_id == w->id && press_bit == button && now - press_at < double_ticks && ax >= -DOUBLE_PX && ax <= DOUBLE_PX
            && ay >= -DOUBLE_PX && ay <= DOUBLE_PX && press_n < 3)
            press_n++;
        else
            press_n = 1;
        press_id = w->id, press_bit = button, press_at = now, press_x = m->px, press_y = m->py;
        e.ch = press_n;
    }
    e.size = (unsigned)dx;                      /* (how far it moved: app_event() takes these two out) */
    e.time = (unsigned)dy;
    e.x = m->px - c.x;
    e.y = m->py - c.y;
    e.buttons = buttons;
    e.key = button;
    e.mods = mods_now();
    e.down = (unsigned)down;
    app_event(w->owner, &e);
}

/* The window the mouse is an app's business in now, or 0. */
static struct wm_window *mouse_for(struct wm *m)
{
    struct wm_window *w;
    int part;
    if (cap_id) {                               /* captured: that window's, wherever the pointer is */
        if ((w = wm_find(m, cap_id)) != 0 && w->owner && (w->flags & WF_SHOWN))
            return w;
        cap_id = 0;
    }
    if (grab_id) {
        if ((w = wm_find(m, grab_id)) != 0 && w->owner)
            return w;
        grab_id = 0;
    }
    w = wm_hit(m, m->px, m->py, &part);
    return w && part == WH_CLIENT && w->owner && w == m->focus ? w : 0;
}

static void button(struct wm *m, u32 bit, int down)
{
    struct wm_window *w, *v;
    int part;
    if (cap_id && (w = mouse_for(m)) != 0 && cap_id) {  /* captured: every button is that window's, and nothing else */
        send_mouse(m, w, 0, 0, bit, down);
        return;
    }
    if (!down) {
        if (bit == 1 && box_id) {               /* let go on the box it went down on */
            w = wm_hit(m, m->px, m->py, &part);
            if (w && w->id == box_id && part == box_part) {
                if (part == WH_CLOSE) {
                    deskapp_close(m, w);
                } else if (part == WH_MAX) {
                    toggle_max(m, w);
                } else {
                    deskapp_state(m, w, WS_MIN);
                    desk_refocus(m);
                }
            }
            box_id = 0;
        } else if ((w = mouse_for(m)) != 0) {
            send_mouse(m, w, 0, 0, bit, 0);
        }
        if (!buttons)
            grab_id = 0;
        return;
    }
    if (grab_id && (w = mouse_for(m)) != 0) {   /* another button, in the same hold */
        send_mouse(m, w, 0, 0, bit, 1);
        return;
    }
    w = wm_hit(m, m->px, m->py, &part);
    if (m->focus && m->focus->flags & WF_POPUP && w != m->focus && (!w || w == m->bar)) {
        wm_set_focus(m, 0);                     /* a press outside a pop-up: it loses the keyboard (its app takes it */
        desk_refocus(m);                        /* away), even where no window would take the focus instead */
        deskin_focus(m);
    }
    if (deskshell_press(m, bit))                /* a tray icon's, or a notification's */
        return;
    if (!w)
        return;
    if ((v = wm_modal_for(m, w)) != 0) {        /* a modal window of its own is up: that one, and nothing else */
        wm_set_focus(m, v);
        deskin_focus(m);
        return;
    }
    if (w == m->bar) {
        if ((v = wm_bar_hit(m, m->px, m->py)) != 0) {
            if (bit != 1) {
                ;
            } else if (v == m->focus && wm_can(v, WH_MIN)) {    /* the one in front: out of the way */
                deskapp_state(m, v, WS_MIN);
                desk_refocus(m);
            } else {
                activate(m, v);
            }
        } else if (bit == 1 && wm_bar_mark(m, m->px, m->py))
            to_shell(m, KEV_LAUNCH);        /* GLOS's mark: the launcher (M9) */
        return;
    }
    wm_set_focus(m, w);
    deskin_focus(m);                            /* (its owner hears of the focus before the press) */
    if (bit == 1 && part == WH_TITLE) {
        u32 now = timer_ticks();
        s32 dx = m->px - click_x, dy = m->py - click_y;
        if (click_id == w->id && now - click_at < double_ticks && dx >= -DOUBLE_PX && dx <= DOUBLE_PX && dy >= -DOUBLE_PX
            && dy <= DOUBLE_PX) {
            click_id = 0;                       /* the second press of two: maximised, or back */
            toggle_max(m, w);
        } else {
            click_id = w->id, click_at = now, click_x = m->px, click_y = m->py;
            if (!(w->flags & WF_FULL))
                drag_start(m, w, D_MOVE, 0);
        }
    } else if (bit == 1 && (part & WH_SIZE)) {
        drag_start(m, w, D_SIZE, part & 15);
    } else if (bit == 1 && (part == WH_CLOSE || part == WH_MIN || part == WH_MAX)) {
        box_id = w->id;
        box_part = (u8)part;
    } else if (part == WH_CLIENT && w->owner && w == m->focus) {
        grab_id = w->id;
        send_mouse(m, w, 0, 0, bit, 1);
    }
}

/* The mouse moved by dx, dy (down and right positive); its buttons now. */
static s32 speed = 1;                           /* [input] pointer_speed (M9) */

void deskin_pointer_speed(int s) { speed = s >= 1 && s <= 3 ? s : 1; }

/* The pointer to (x, y) with the buttons `now`: a movement and a press or release, as the mouse makes them. */
/* ---- an app's drag (API 2.0's DND; the states are kernel/wm/dnd.c's, the events kernel/app/api2clip.c's) */

/* Where the pointer is, for the drag: over an app's window's client area, the bare desktop, or neither. Its
   picture goes with it. */
static void dnd_point(struct wm *m)
{
    struct wm_window *w, *pic = dnd_pic ? wm_find(m, dnd_pic) : 0;
    struct gfx_rect c;
    int part;
    if (pic)
        wm_move(m, pic, m->px + DND_PX, m->py + DND_PY);
    w = wm_hit(m, m->px, m->py, &part);
    if (w && w->owner && part == WH_CLIENT) {
        wm_client(m, w, &c);
        api2_dnd_at(w->owner, w->id, m->px, m->py, c.x, c.y);
    } else if (!w || (w->flags & WF_DESKTOP)) {
        api2_dnd_at(0, 0, m->px, m->py, 0, 0);
    } else {
        api2_dnd_at(0, ~0u, m->px, m->py, 0, 0);
    }
}

static void dnd_clear(struct wm *m)
{
    struct wm_window *pic = dnd_pic ? wm_find(m, dnd_pic) : 0;
    if (pic)
        wm_destroy(m, pic);
    dnd_pic = 0;
    dnd_on = 0;
}

static void dnd_finish(struct wm *m, int drop)
{
    if (!dnd_on)
        return;
    dnd_clear(m);
    if (buttons) {                              /* given up with the button still down: its release is nobody's */
        buttons = 0;
        woke_buttons = 1;
    }
    api2_dnd_end(drop);
}

int desk_dnd_begin(struct app *a, u32 win_id, const u32 *picture, u32 pw, u32 ph)
{
    struct wm *m = desk_lock();
    struct wm_window *w = wm_find(m, win_id), *pic;
    if (!w || w->owner != a || !buttons || grab_id != win_id || drag || dnd_on) {
        desk_unlock();
        return -1;
    }
    grab_id = 0;                                /* (the hold is the drag's now: the app hears no more of it) */
    dnd_on = 1;
    dnd_pic = 0;
    if (picture && pw && ph && (pic = wm_create(m, m->px + DND_PX, m->py + DND_PY, (s32)pw + 2, (s32)ph + 2, "",
                                                 WF_TOPMOST | WF_NOFOCUS | WF_NOBAR, 0)) != 0) {
        u32 x, y;                               /* a line round it; where the picture is clear, the field's colour */
        gfx_fill(&pic->store, 0, 0, (s32)pw + 2, (s32)ph + 2, 0x000000);
        gfx_fill(&pic->store, 1, 1, (s32)pw, (s32)ph, 0xFFFFFF);
        for (y = 0; y < ph; y++)
            for (x = 0; x < pw; x++)
                if (picture[y * pw + x] >> 24)
                    gfx_fill(&pic->store, 1 + (s32)x, 1 + (s32)y, 1, 1, picture[y * pw + x] & 0xFFFFFF);
        wm_show(m, pic, 1);
        dnd_pic = pic->id;
    }
    dnd_point(m);
    pointer_shape(m);
    desk_unlock();
    return 0;
}

void desk_dnd_stop(void)
{
    struct wm *m = desk_lock();
    dnd_clear(m);
    pointer_shape(m);
    desk_unlock();
}

static void mouse_to(struct wm *m, s32 x, s32 y, u32 now)
{
    struct wm_window *w;
    u32 was = buttons, bit;
    s32 x0 = m->px, y0 = m->py, dx, dy;

    if (asleep) {                               /* (the movement or press that ended the saver) */
        woke_buttons = now != 0;
        return;
    }
    if (woke_buttons) {                         /* (the pointer still moves: nothing is told of it) */
        if (now) {
            wm_pointer(m, x, y, 1);
            return;
        }
        woke_buttons = 0;
    }

    wm_pointer(m, x, y, 1);
    dx = m->px - x0, dy = m->py - y0;
    buttons = (u8)now;
    if (dnd_on) {                               /* an app's drag: the pointer is its until every button is up */
        dnd_point(m);
        if (!now)
            dnd_finish(m, 1);
        return;
    }
    if (drag == D_MOVE || drag == D_SIZE) {
        if (!(w = wm_find(m, drag_id))) {
            drag_stop(m);
        } else {
            drag_to(m, w);
            if (!(now & 1))
                drag_end(m, 1);
        }
        return;
    }
    if (drag)                                   /* (the arrows have the outline: the mouse only points) */
        return;
    if ((m->px != x0 || m->py != y0) && (w = mouse_for(m)) != 0)
        send_mouse(m, w, dx, dy, 0, 0);
    for (bit = 1; bit < 8; bit <<= 1)
        if ((was ^ now) & bit)
            button(m, bit, (now & bit) != 0);
}

static void mouse(struct wm *m, s32 dx, s32 dy, u32 now) { mouse_to(m, m->px + dx * speed, m->py + dy * speed, now); }

/* The wheel turned: the window under the pointer hears of it, whether it has the focus or not (KEV_WHEEL, API
   1.5; notches positive away from the user). */
static void wheel(struct wm *m, int notches)
{
    struct wm_window *w;
    struct kevent e;
    struct gfx_rect c;
    int part;
    if (asleep || !notches || !(w = wm_hit(m, m->px, m->py, &part)) || part != WH_CLIENT || !w->owner)
        return;
    wm_client(m, w, &c);
    memset(&e, 0, sizeof e);
    e.type = KEV_WHEEL;
    e.window = w->id;
    e.time = timer_ticks() * 1000 / 1024;
    e.x = m->px - c.x;
    e.y = m->py - c.y;
    e.buttons = buttons;
    e.key = (unsigned)notches;
    e.mods = mods_now();
    app_event(w->owner, &e);
}

/* A byte from the mouse itself: three make a packet (four with a wheel). */
static void aux_byte(struct wm *m, u8 b, int wheel_mouse)
{
    u32 now = timer_ticks();
    if (npkt && now - pkt_at > 100)             /* (half a packet, long ago: lost) */
        npkt = 0;
    if (!npkt && !(b & 0x08))                   /* (not a packet's first byte) */
        return;
    pkt[npkt++] = b;
    pkt_at = now;
    if (npkt < (wheel_mouse ? 4 : 3))
        return;
    npkt = 0;
    if (pkt[0] & 0xC0)                          /* (it overflowed: a movement that means nothing) */
        return;
    mouse(m, (s32)pkt[1] - (pkt[0] & 0x10 ? 256 : 0), -((s32)pkt[2] - (pkt[0] & 0x20 ? 256 : 0)), pkt[0] & 7u);
    if (wheel_mouse && (pkt[3] & 0x0F))         /* (the fourth byte's Z: 4 bits, negative for a turn away) */
        wheel(m, -((int)(pkt[3] & 0x0F) - ((pkt[3] & 0x08) ? 16 : 0)));
}

/* The keyboard and the mouse have changed hands (a session took them, or
   gave them back): what was down is no longer known to be. The app that
   saw keys or buttons go down sees them let go. */
static void forget(struct wm *m)
{
    struct wm_window *w;
    u32 k, bit;
    dnd_finish(m, 0);
    if (drag)
        drag_end(m, 0);
    if (foc_app)
        for (k = 0; k < 0x200; k++)
            if (held[k >> 3] & (1u << (k & 7)))
                send_key(foc_app, foc_id, k, 0, 0, 0);
    if (grab_id && (w = wm_find(m, grab_id)) != 0 && w->owner)
        for (bit = 1; bit < 8; bit <<= 1)
            if (buttons & bit) {
                buttons &= (u8)~bit;
                send_mouse(m, w, 0, 0, bit, 0);
            }
    memset(held, 0, sizeof held);
    memset(isdown, 0, sizeof isdown);
    kmods = e0 = e1 = npkt = buttons = 0;
    grab_id = box_id = click_id = cap_id = press_id = 0;
    deskin_init();
}

void deskin_turn(struct wm *m)
{
    struct in e;
    u32 f;
    asleep = idle_saver() != 0;
    for (;;) {
        f = irq_save();
        if (rh == rt) {
            irq_restore(f);
            break;
        }
        e = ring[rh++ % ARRAY_SIZE(ring)];
        irq_restore(f);
        switch (e.what) {
        case DESK_KEY: key_byte(m, (u8)e.a); break;
        case DESK_AUX: aux_byte(m, (u8)e.a, e.b); break;
        case DESK_MOUSE: mouse(m, e.a, e.b, e.c); break;
        case DESK_POINT: mouse_to(m, e.a, e.b, e.c); break;
        case DESK_WHEEL: wheel(m, e.a); break;
        case DESK_RESET: forget(m); break;
        case DESK_SYSKEY:
            if (!asleep)
                to_shell(m, KEV_SYSKEY);
            break;
        }
    }
    asleep = 0;
    if (drag && !wm_find(m, drag_id))           /* (the window went while it was being moved) */
        drag_stop(m);
    pointer_shape(m);
}

int deskin_capture(struct wm *m, struct wm_window *w, int on)
{
    (void)m;
    if (!on) {
        if (cap_id == w->id)
            cap_id = 0;
        return 0;
    }
    if (!(w->flags & WF_SHOWN) || drag)
        return -1;
    cap_id = w->id;
    return 0;
}

void deskin_keys(u8 down[64], u32 *mods, u32 *locks)
{
    memcpy(down, isdown, 64);
    *mods = mods_now();
    *locks = (caps ? 1u : 0u) | (num ? 2u : 0u);
}

struct app *deskin_focus_app(void) { return foc_app; }

int deskin_drag(struct wm *m, struct wm_window *w, int part)
{
    if (!(buttons & 1) || drag || (w->flags & WF_FULL))
        return -1;
    grab_id = 0;                                /* (the hold is the manager's now: the app hears no more of it) */
    drag_start(m, w, part & WH_SIZE ? D_SIZE : D_MOVE, part & 15);
    return 0;
}

/* The pointer's shape for where it is and what it is doing: the edge a window is being sized by, the window's
   own over its client area (and while a button pressed there is held), the arrow elsewhere. */
static void pointer_shape(struct wm *m)
{
    struct wm_window *w = 0;
    int part, shape = WC_ARROW;
    if (dnd_on) {                               /* a drag: what a drop here would do */
        int e = api2_dnd_effect();
        shape = e == 1 ? WC_DROP_COPY : e == 2 ? WC_DROP_MOVE : e == 4 ? WC_DROP_LINK : WC_DROP_NONE;
    } else if (drag == D_SIZE)
        shape = wm_cursor_for(0, WH_SIZE | drag_dir);
    else if (drag)
        shape = WC_ARROW;
    else if ((cap_id && (w = wm_find(m, cap_id)) != 0) || (grab_id && (w = wm_find(m, grab_id)) != 0))
        shape = w->cursor;
    else if ((w = wm_hit(m, m->px, m->py, &part)) != 0)
        shape = wm_modal_for(m, w) ? WC_ARROW : wm_cursor_for(w, part);
    wm_cursor(m, shape, shape == WC_CUSTOM && w ? &w->cursor_img : 0);
}

void deskin_init(void)
{
    u8 leds = vm_rd8(0x417);                    /* the BIOS's locks, as DOS left them */
    caps = (leds & 0x40) != 0;
    num = (leds & 0x20) != 0;
}
