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
 *     Alt-Tab   the focus to the next window
 *     Alt-F4    the window is asked to close
 *     Alt-F7    the window is moved with the arrow keys
 *     Alt-F8    ... or given another size
 * In the last two an outline follows the arrows (eight pixels a step, one
 * with Shift), Enter makes it so and Esc leaves things alone. The mouse
 * does the same by the title bar and the bottom right corner, with the
 * same outline: a window's pixels are not moved until the button is let
 * go, which a 486 would be slow at. A press anywhere on a window gives it
 * the focus; the close box closes when the button is let go on it; a
 * button on the task bar gives its window the focus.
 *
 * An app gets the keys that went down while it had the focus, and the up
 * of each, whatever happens between: when the focus leaves, the keys still
 * down are let go for it first. It gets the mouse while the pointer is in
 * the client area of its window with the focus, and, from a button's press
 * there, until every button is up again, wherever the pointer goes. */
#include <string.h>

#define GLOS_KERNEL
#include "glos/api.h"
#include "app.h"
#include "deskint.h"
#include "io.h"
#include "kprintf.h"
#include "timer.h"
#include "vm.h"

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
static u32 close_id;                            /* the window whose close box the button went down on */

enum { D_NONE, D_MOVE, D_SIZE, D_KEY_MOVE, D_KEY_SIZE };
static u8 drag;
static u32 drag_id;
static s32 drag_x, drag_y;                      /* the pointer from the outline's corner (D_SIZE: its far corner) */

#define MIN_W 80                                /* the smallest client area the user can make */
#define MIN_H 40

/* From the 8042's code, interrupts off. */
void desk_in(int what, int a, int b, int c)
{
    u32 f = irq_save();
    if (what == DESK_KILL) {
        if (foc_app)
            app_kill(foc_app, "hotkey");
    } else if (rt - rh < ARRAY_SIZE(ring)) {
        struct in *e = &ring[rt++ % ARRAY_SIZE(ring)];
        e->what = (u8)what;
        e->a = (short)a;
        e->b = (short)b;
        e->c = (u8)c;
    }
    irq_restore(f);
    desk_kick();
}

/* ---- events to apps */

static u32 mods_now(void)
{
    return (kmods & KM_SHIFT ? GLOS_MOD_SHIFT : 0u) | (kmods & KM_CTRL ? GLOS_MOD_CTRL : 0u) | (kmods & KM_ALT ? GLOS_MOD_ALT : 0u);
}

static void send_key(struct app *a, u32 id, u32 key, int down, u32 ch, u32 mods)
{
    struct glos_event e;
    memset(&e, 0, sizeof e);
    e.type = GLOS_EV_KEY;
    e.window = id;
    e.key = key;
    e.ch = ch;
    e.mods = mods;
    e.down = (unsigned)down;
    app_event(a, &e);
}

static void send_focus(struct app *a, u32 id, int got)
{
    struct glos_event e;
    memset(&e, 0, sizeof e);
    e.type = GLOS_EV_FOCUS;
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
        if ((w->flags & WF_SHOWN) && !(w->flags & WF_NOFOCUS)) {
            wm_set_focus(m, w);
            return;
        }
}

/* ---- moving and sizing: the outline */

static void drag_stop(struct wm *m)
{
    wm_ghost(m, 0);
    drag = D_NONE;
}

/* The outline kept to what a window may be: no smaller than the least, no
   larger than the screen, its title bar never out of reach. */
static void outline(struct wm *m, const struct wm_window *w, struct gfx_rect g)
{
    s32 ew = w->frame.w - w->store.w, eh = w->frame.h - w->store.h;     /* the frame's own part */
    s32 floor = m->h - (m->bar ? m->bar->frame.h : 0) - m->look.title_h;
    if (g.w < MIN_W + ew)
        g.w = MIN_W + ew;
    if (g.h < MIN_H + eh)
        g.h = MIN_H + eh;
    if (g.w > m->w)
        g.w = m->w;
    if (g.h > m->h)
        g.h = m->h;
    if (g.y < 0)
        g.y = 0;
    if (g.y > floor)
        g.y = floor;
    if (g.x > m->w - 40)
        g.x = m->w - 40;
    if (g.x < 40 - g.w)
        g.x = 40 - g.w;
    wm_ghost(m, &g);
}

static void drag_start(struct wm *m, struct wm_window *w, int how)
{
    drag = (u8)how;
    drag_id = w->id;
    drag_x = how == D_SIZE ? w->frame.x + w->frame.w - m->px : m->px - w->frame.x;
    drag_y = how == D_SIZE ? w->frame.y + w->frame.h - m->py : m->py - w->frame.y;
    outline(m, w, w->frame);
}

/* The move or the new size is made (or not). */
static void drag_end(struct wm *m, int make)
{
    struct wm_window *w = wm_find(m, drag_id);
    struct gfx_rect g = m->ghost;
    int size = drag == D_SIZE || drag == D_KEY_SIZE;
    drag_stop(m);
    if (!w || !make)
        return;
    if (size)
        deskapp_resize(m, w, g.w - (w->frame.w - w->store.w), g.h - (w->frame.h - w->store.h));
    else
        wm_move(m, w, g.x, g.y);
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
    outline(m, w, g);
}

/* ---- the keyboard */

static u32 key_char(u32 key)
{
    static const char plain[0x3A] = {
        0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 8, 9,
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 13, 0, 'a', 's',
        'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
        'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ' };
    static const char shifted[0x3A] = {
        0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 8, 9,
        'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 13, 0, 'A', 'S',
        'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
        'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ' };
    static const char pad[] = "789-456+1230.";
    int shift = (kmods & KM_SHIFT) != 0;
    u32 c = 0;
    if (key < 0x3A) {
        c = (u8)plain[key];
        if (c >= 'a' && c <= 'z')
            c -= (shift != (caps != 0)) ? 32u : 0u;
        else if (shift)
            c = (u8)shifted[key];
    } else if (key >= 0x47 && key <= 0x53) {    /* the number pad: digits with Num Lock, or Shift without */
        c = (u8)pad[key - 0x47];
        if (c != '-' && c != '+' && shift == (num != 0))
            c = 0;
    } else if (key == 0x11C) {
        c = 13;
    } else if (key == 0x135) {
        c = '/';
    }
    if (kmods & KM_ALT)
        return 0;
    if (kmods & KM_CTRL) {
        if (c >= 'a' && c <= 'z')
            return c - 0x60;
        if (c >= 0x40 && c < 0x60)
            return c - 0x40;
        return c == 13 ? 10 : c == 8 ? 127 : c == ' ' || c == 27 ? c : 0;
    }
    return c;
}

static void key_event(struct wm *m, u32 key, int down)
{
    struct wm_window *w = m->focus;
    u8 bit = (u8)(1u << (key & 7)), mod = 0;
    int again = down && (isdown[key >> 3] & bit);       /* the keyboard repeating a key held down */

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
    if (down && (kmods & KM_ALT) && !(kmods & KM_CTRL)) {
        switch (key) {
        case 0x0F:
            wm_set_focus(m, wm_next(m, w));
            return;
        case 0x3E:
            if (w && !again)
                deskapp_close(m, w);
            return;
        case 0x41: case 0x42:
            if (w && !again && !drag && !(key == 0x42 && (w->flags & WF_FIXED)))
                drag_start(m, w, key == 0x41 ? D_KEY_MOVE : D_KEY_SIZE);
            return;
        }
    }
    deskin_focus(m);                            /* (a key just before this one may have moved it) */
    w = m->focus;
    if (!w || !w->owner)
        return;
    if (!down && !(held[key >> 3] & bit))       /* (the up of a key it never saw go down) */
        return;
    held[key >> 3] = (u8)(down ? held[key >> 3] | bit : held[key >> 3] & ~bit);
    send_key(foc_app, foc_id, key, down, down ? key_char(key) : 0, mods_now());
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
    struct glos_event e;
    struct gfx_rect c;
    wm_client(m, w, &c);
    memset(&e, 0, sizeof e);
    e.type = GLOS_EV_MOUSE;
    e.window = w->id;
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
    if (!down) {
        if (bit == 1 && close_id) {             /* let go on the close box it went down on */
            w = wm_hit(m, m->px, m->py, &part);
            if (w && w->id == close_id && part == WH_CLOSE)
                deskapp_close(m, w);
            close_id = 0;
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
    if (!(w = wm_hit(m, m->px, m->py, &part)))
        return;
    if (w == m->bar) {
        if ((v = wm_bar_hit(m, m->px, m->py)) != 0)
            wm_set_focus(m, v);
        return;
    }
    wm_set_focus(m, w);
    deskin_focus(m);                            /* (its owner hears of the focus before the press) */
    if (bit == 1 && part == WH_TITLE) {
        drag_start(m, w, D_MOVE);
    } else if (bit == 1 && part == WH_GROW) {
        drag_start(m, w, D_SIZE);
    } else if (bit == 1 && part == WH_CLOSE) {
        close_id = w->id;
    } else if (part == WH_CLIENT && w->owner && w == m->focus) {
        grab_id = w->id;
        send_mouse(m, w, 0, 0, bit, 1);
    }
}

/* The mouse moved by dx, dy (down and right positive); its buttons now. */
static void mouse(struct wm *m, s32 dx, s32 dy, u32 now)
{
    struct wm_window *w;
    u32 was = buttons, bit;
    s32 x0 = m->px, y0 = m->py;

    wm_pointer(m, m->px + dx, m->py + dy, 1);
    buttons = (u8)now;
    if (drag == D_MOVE || drag == D_SIZE) {
        struct gfx_rect g = m->ghost;
        if (!(w = wm_find(m, drag_id))) {
            drag_stop(m);
        } else {
            if (drag == D_MOVE)
                g.x = m->px - drag_x, g.y = m->py - drag_y;
            else
                g.w = m->px + drag_x - g.x, g.h = m->py + drag_y - g.y;
            outline(m, w, g);
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

/* A byte from the mouse itself: three make a packet (four with a wheel). */
static void aux_byte(struct wm *m, u8 b, int wheel)
{
    u32 now = timer_ticks();
    if (npkt && now - pkt_at > 100)             /* (half a packet, long ago: lost) */
        npkt = 0;
    if (!npkt && !(b & 0x08))                   /* (not a packet's first byte) */
        return;
    pkt[npkt++] = b;
    pkt_at = now;
    if (npkt < (wheel ? 4 : 3))
        return;
    npkt = 0;
    if (pkt[0] & 0xC0)                          /* (it overflowed: a movement that means nothing) */
        return;
    mouse(m, (s32)pkt[1] - (pkt[0] & 0x10 ? 256 : 0), -((s32)pkt[2] - (pkt[0] & 0x20 ? 256 : 0)), pkt[0] & 7u);
}

/* The keyboard and the mouse have changed hands (a session took them, or
   gave them back): what was down is no longer known to be. The app that
   saw keys or buttons go down sees them let go. */
static void forget(struct wm *m)
{
    struct wm_window *w;
    u32 k, bit;
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
    grab_id = close_id = 0;
    deskin_init();
}

void deskin_turn(struct wm *m)
{
    struct in e;
    u32 f;
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
        case DESK_RESET: forget(m); break;
        }
    }
    if (drag && !wm_find(m, drag_id))           /* (the window went while it was being moved) */
        drag_stop(m);
}

void deskin_init(void)
{
    u8 leds = vm_rd8(0x417);                    /* the BIOS's locks, as DOS left them */
    caps = (leds & 0x40) != 0;
    num = (leds & 0x20) != 0;
}
