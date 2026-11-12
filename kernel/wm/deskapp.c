/* Apps' windows (kernel/include/desk.h; PRD §10; include/glos/api.h): the
 * window functions of the API, on the app's own thread, and what the
 * desktop does to an app's window.
 *
 * A window's pixels are the manager's memory (whole pages: mm_win_alloc),
 * mapped into the app as well, so it draws in them itself and says where
 * (GLOS_FN_PRESENT). The mapping is made and undone on the app's thread, in
 * its own address space, and only there. The manager may give a window new
 * pixels behind the app's back: the user gave it another size, the screen
 * got another format. The app is told (GLOS_EV_RESIZE) and asks where they
 * are now; until it has, it draws in the old ones, which nothing shows any
 * more, but which stay its own to write on: memory an app has mapped is
 * never given to anyone else (`pins`). It lets go of them at its next call
 * here, and at its end. */
#include <string.h>

#define GLOS_KERNEL
#include "glos/api.h"
#include "app.h"
#include "deskint.h"
#include "dpmi.h"
#include "io.h"
#include "display.h"
#include "keymap.h"
#include "kprintf.h"
#include "mgadisp.h"
#include "mm.h"
#include "timer.h"

/* Pixels an app has mapped. */
struct pin {
    void *mem;                                  /* 0: free */
    u32 bytes;
    struct app *a;
    u32 lin;                                    /* where, in the app */
    u8 stale;                                   /* no window's any more: to be let go of and freed */
};
static struct pin pins[96];

static struct pin *pin_of(const void *mem)
{
    u32 i;
    for (i = 0; i < ARRAY_SIZE(pins); i++)
        if (pins[i].mem == mem)
            return &pins[i];
    return 0;
}

int deskapp_keep(void *mem)
{
    struct pin *p = pin_of(mem);
    if (p)
        p->stale = 1;
    return p != 0;
}

/* On the app's thread: what it has mapped and no window has. */
static void let_go(struct app *a)
{
    u32 i;
    for (i = 0; i < ARRAY_SIZE(pins); i++)
        if (pins[i].mem && pins[i].a == a && pins[i].stale) {
            phys_unmap(pins[i].lin);
            mm_win_free(pins[i].mem, pins[i].bytes);
            pins[i].mem = 0;
        }
}

/* On the app's thread: where it has the window's pixels, mapped now if they
   weren't. 0, or a GLOS_E_. */
static int map(struct app *a, const struct wm_window *w, u32 *lin)
{
    struct pin *p = pin_of(w->mem);
    u32 bytes = w->store.pitch * (u32)w->store.h;
    if (p) {
        *lin = p->lin;
        return 0;
    }
    if (!(p = pin_of(0)) || win_map(w->mem, bytes, lin) != 0)
        return GLOS_E_MEMORY;
    p->mem = w->mem;
    p->bytes = bytes;
    p->a = a;
    p->lin = *lin;
    p->stale = 0;
    return 0;
}

static struct wm_window *own(struct wm *m, const struct app *a, u32 id)
{
    struct wm_window *w = wm_find(m, id);
    return w && w->owner == a ? w : 0;
}

static void describe(const struct wm *m, const struct wm_window *w, u32 lin, struct glos_window *g)
{
    memset(g, 0, sizeof *g);
    g->size = sizeof *g;
    g->bpp = m->fmt.bpp;
    g->rpos = m->fmt.rpos, g->rsize = m->fmt.rsize;
    g->gpos = m->fmt.gpos, g->gsize = m->fmt.gsize;
    g->bpos = m->fmt.bpos, g->bsize = m->fmt.bsize;
    if (!w) {
        g->w = m->w;
        g->h = m->h;
        return;
    }
    g->id = w->id;
    {                                           /* (1.2: the client area's corner) */
        struct gfx_rect c;
        wm_client(m, w, &c);
        g->x = c.x;
        g->y = c.y;
    }
    g->w = w->store.w;
    g->h = w->store.h;
    g->flags = (w->flags & WF_FIXED ? GLOS_WIN_FIXED : 0u) | (w->flags & WF_FRAME ? 0u : GLOS_WIN_BARE)
             | (w->flags & WF_POPUP ? GLOS_WIN_POPUP : 0u) | (w->flags & WF_DESKTOP ? GLOS_WIN_DESKTOP : 0u);
    memcpy(g->title, w->title, sizeof g->title < sizeof w->title ? sizeof g->title : sizeof w->title);
    g->title[sizeof g->title - 1] = 0;
    g->pixels = lin;
    g->pitch = w->store.pitch;
}

/* ---- the API's functions (app/api.c): 0, or a GLOS_E_ */

int desk_win_create(struct app *a, struct glos_window *g)
{
    struct wm *m;
    struct wm_window *w;
    u32 lin, flags = (g->flags & (GLOS_WIN_BARE | GLOS_WIN_POPUP | GLOS_WIN_DESKTOP) ? 0u : WF_FRAME)
                   | (g->flags & GLOS_WIN_FIXED ? WF_FIXED : 0u) | (g->flags & GLOS_WIN_POPUP ? WF_POPUP | WF_TOPMOST : 0u)
                   | (g->flags & GLOS_WIN_DESKTOP ? WF_DESKTOP | WF_FIXED : 0u);
    s32 x = g->x, y = g->y;
    int r;
    if (g->w < 1 || g->h < 1 || g->w > 4096 || g->h > 4096)
        return GLOS_E_ARGUMENT;
    g->title[sizeof g->title - 1] = 0;
    m = desk_lock();
    let_go(a);
    if (g->flags & GLOS_WIN_DESKTOP) {          /* the shell's desktop (M9): the screen less the task bar, one */
        struct wm_window *o;
        for (o = m->top; o; o = o->below)
            if (o->flags & WF_DESKTOP) {
                desk_unlock();
                return GLOS_E_ARGUMENT;
            }
        x = 0, y = 0;
        g->w = m->w;
        g->h = m->h - (m->bar ? m->bar->frame.h : 0);
    } else if (g->flags & GLOS_WIN_ANYWHERE) {         /* down and right of the last, a few times, then from the top again; */
        s32 fw = g->w + (flags & WF_FRAME ? 2 * m->look.border : 0);    /* up and left as far as it takes to be */
        s32 fh = g->h + (flags & WF_FRAME ? 3 * m->look.border + m->look.title_h : 0);     /* on the screen (M9) */
        s32 room = m->h - (m->bar ? m->bar->frame.h : 0);
        x = 24 + (s32)(m->next_id % 8) * 24;
        y = 16 + (s32)(m->next_id % 8) * 24;
        if (x + fw > m->w)
            x = m->w - fw > 0 ? m->w - fw : 0;
        if (y + fh > room)
            y = room - fh > 0 ? room - fh : 0;
    }
    if (!(w = wm_create(m, x, y, g->w, g->h, g->title, flags, a))) {
        desk_unlock();
        return GLOS_E_MEMORY;
    }
    if ((r = map(a, w, &lin)) != 0)
        wm_destroy(m, w);
    else
        describe(m, w, lin, g);
    desk_unlock();
    return r;
}

int desk_win_close(struct app *a, u32 id)
{
    struct wm *m = desk_lock();
    struct wm_window *w = own(m, a, id);
    if (w) {
        wm_destroy(m, w);
        desk_refocus(m);
    }
    let_go(a);                                  /* (its pixels, which wm_destroy() left to this) */
    desk_unlock();
    return w ? 0 : GLOS_E_WINDOW;
}

int desk_win_title(struct app *a, u32 id, const char *title)
{
    struct wm *m = desk_lock();
    struct wm_window *w = own(m, a, id);
    if (w)
        wm_title(m, w, title);
    desk_unlock();
    return w ? 0 : GLOS_E_WINDOW;
}

int desk_win_pixels(struct app *a, u32 id, struct glos_window *g)
{
    struct wm *m = desk_lock();
    struct wm_window *w = own(m, a, id);
    u32 lin;
    int r = GLOS_E_WINDOW;
    let_go(a);
    if (w && (r = map(a, w, &lin)) == 0)
        describe(m, w, lin, g);
    desk_unlock();
    return r;
}

int desk_win_present(struct app *a, u32 id, const struct glos_rect *r)
{
    struct wm *m = desk_lock();
    struct wm_window *w = own(m, a, id);
    struct gfx_rect part;
    if (w && r) {
        part.x = r->x, part.y = r->y, part.w = r->w, part.h = r->h;
        wm_damage(m, w, &part);
    } else if (w) {
        wm_damage(m, w, 0);
    }
    desk_unlock();
    return w ? 0 : GLOS_E_WINDOW;
}

int desk_win_vram(struct app *a, u32 id, u32 off, u32 pitch)
{
    struct wm *m = desk_lock();
    struct wm_window *w = own(m, a, id);
    struct gfx_surface s;
    u32 bpp_b = m->fmt.bpp / 8u;
    int r = 0;
    if (!w) {
        r = GLOS_E_WINDOW;
    } else if (off == 0xFFFFFFFFu) {
        wm_ext(m, w, 0);
    } else if (m->fmt.bpp != 16 || m->fmt.gsize != 6 || !mga_desktop_card()) {
        r = GLOS_E_NO_GPU;                      /* (GLOS_CAP_GLWIN: the app's pixels are RGB565; the desktop's card,
                                                   also while another program's session has it, as the capability) */
    } else if ((off & 63) || (pitch & 63) || pitch < (u32)w->store.w * bpp_b
               || !mga_app_block(app_id(a), off, pitch * (u32)(w->store.h - 1) + (u32)w->store.w * bpp_b, 0, 0)) {
        r = GLOS_E_ARGUMENT;                    /* (only its own blocks) */
    } else {
        gfx_init(&s, mga_vram_ptr(off), w->store.w, w->store.h, pitch, &m->fmt);
        s.dev = display_vram_dev(s.pix);
        wm_ext(m, w, &s);
    }
    desk_unlock();
    return r;
}

void desk_app_vram_gone(struct app *a, u32 off, u32 bytes)
{
    struct wm *m = desk_lock();
    struct wm_window *w;
    for (w = m->bottom; w; w = w->above)
        if (w->owner == a && w->has_ext) {
            u32 at = (u32)((u8 *)w->ext.pix - (u8 *)mga_vram_ptr(0));
            if (at >= off && at - off < bytes)
                wm_ext(m, w, 0);
        }
    desk_unlock();
}

int desk_win_focus(struct app *a, u32 id)
{
    struct wm *m = desk_lock();
    struct wm_window *w = own(m, a, id);
    if (w)
        wm_set_focus(m, w);
    desk_unlock();
    return w ? 0 : GLOS_E_WINDOW;
}

void desk_screen(struct glos_window *g)
{
    struct wm *m = desk_lock();
    describe(m, 0, 0, g);
    desk_unlock();
}

/* ---- what the desktop does to an app's window */

static void told(struct wm_window *w, u32 type)
{
    struct glos_event e;
    memset(&e, 0, sizeof e);
    e.type = type;
    e.window = w->id;
    e.x = w->store.w;
    e.y = w->store.h;
    app_event(w->owner, &e);
}

int deskapp_resize(struct wm *m, struct wm_window *w, s32 cw, s32 ch)
{
    if (cw == w->store.w && ch == w->store.h)
        return 0;
    if (wm_resize(m, w, cw, ch) != 0)
        return -1;
    if (w->owner)
        told(w, GLOS_EV_RESIZE);
    return 0;
}

int deskapp_state(struct wm *m, struct wm_window *w, int state)
{
    s32 cw = w->store.w, ch = w->store.h;
    int was = wm_state_of(w);
    if (wm_state(m, w, state) != 0)
        return -1;
    if (w->owner && (cw != w->store.w || ch != w->store.h))
        told(w, GLOS_EV_RESIZE);
    if (w->owner && wm_state_of(w) != was) {
        struct glos_event e;
        memset(&e, 0, sizeof e);
        e.type = DESK_EV_STATE;
        e.window = w->id;
        e.key = (unsigned)wm_state_of(w);
        app_event(w->owner, &e);
    }
    return 0;
}

/* ---- for API 2.0 (app/api2win.c): on the app's thread; 0, or a GLOS_E_ */

int desk_win_new(struct app *a, const struct desk_new *n, u32 *id)
{
    struct wm *m;
    struct wm_window *w, *o;
    s32 x = n->x, y = n->y, cw = n->w, ch = n->h;
    u32 lin;
    int r;
    if (cw < 1 || ch < 1 || cw > 4096 || ch > 4096)
        return GLOS_E_ARGUMENT;
    m = desk_lock();
    let_go(a);
    if (n->parent && !own(m, a, n->parent)) {
        desk_unlock();
        return GLOS_E_ARGUMENT;
    }
    if (n->desktop) {                           /* the shell's: the work area, one */
        struct gfx_rect work;
        for (o = m->top; o; o = o->below)
            if (o->flags & WF_DESKTOP) {
                desk_unlock();
                return GLOS_E_ARGUMENT;
            }
        wm_work(m, &work);
        x = y = 0, cw = work.w, ch = work.h;
    } else if (n->anywhere) {                   /* (as desk_win_create) */
        struct gfx_rect work;
        s32 fw = cw + (n->wf & WF_FRAME ? 2 * m->look.border : 0);
        s32 fh = ch + (n->wf & WF_FRAME ? 3 * m->look.border + m->look.title_h : 0);
        wm_work(m, &work);
        x = 24 + (s32)(m->next_id % 8) * 24;
        y = 16 + (s32)(m->next_id % 8) * 24;
        if (x + fw > m->w)
            x = m->w - fw > 0 ? m->w - fw : 0;
        if (y + fh > work.h)
            y = work.h - fh > 0 ? work.h - fh : 0;
    }
    if (!(w = wm_create(m, x, y, cw, ch, n->title, n->wf, a))) {
        desk_unlock();
        return GLOS_E_MEMORY;
    }
    w->tag = n->tag;
    w->parent = n->parent;
    if ((r = map(a, w, &lin)) != 0)
        wm_destroy(m, w);
    else
        *id = w->id;
    desk_unlock();
    return r;
}

int desk_win_state(struct app *a, u32 id, int state, int focus)
{
    struct wm *m = desk_lock();
    struct wm_window *w = own(m, a, id);
    int r = 0;
    if (!w) {
        r = GLOS_E_WINDOW;
    } else if (state == -1) {
        int was = wm_state_of(w);
        wm_show(m, w, 0);
        desk_refocus(m);
        (void)was;
    } else {
        if (state == -2) {                      /* as it last was */
            if (w->flags & WF_MIN)
                r = deskapp_state(m, w, WS_UNMIN);
            else
                wm_show(m, w, 1);
        } else if (state == WS_MIN) {
            wm_show(m, w, 1);                   /* (a hidden one minimised: its button is there) */
            r = deskapp_state(m, w, WS_MIN);
        } else {
            if (!(w->flags & (WF_SHOWN | WF_MIN)))
                wm_show(m, w, 1);
            r = deskapp_state(m, w, state);
        }
        if (r)
            r = GLOS_E_MEMORY;
        else if (state == WS_MIN)
            desk_refocus(m);
        else if (focus)
            wm_set_focus(m, w);
    }
    let_go(a);                                  /* (pixels of its old size) */
    desk_unlock();
    return r;
}

int desk_win_place(struct app *a, u32 id, int move, s32 x, s32 y, int size, s32 cw, s32 ch)
{
    struct wm *m;
    struct wm_window *w;
    int r = 0;
    if (size && (cw < 1 || ch < 1 || cw > 4096 || ch > 4096))
        return GLOS_E_ARGUMENT;
    m = desk_lock();
    if (!(w = own(m, a, id))) {
        r = GLOS_E_WINDOW;
    } else if (w->flags & WF_PLACED) {          /* the manager has it somewhere: this is where it goes back to */
        if (move)
            w->normal.x = x, w->normal.y = y;
        if (size)
            w->normal.w = cw, w->normal.h = ch;
    } else {
        if (size && deskapp_resize(m, w, cw, ch) != 0)
            r = GLOS_E_MEMORY;
        if (!r && move)
            wm_move(m, w, x, y);
    }
    let_go(a);
    desk_unlock();
    return r;
}

static void info(const struct wm *m, const struct wm_window *w, struct desk_info *o)
{
    struct gfx_rect c;
    memset(o, 0, sizeof *o);
    wm_client(m, w, &c);
    o->id = w->id, o->tag = w->tag, o->wf = w->flags, o->parent = w->parent;
    o->owner = w->owner;
    o->state = (w->flags & (WF_SHOWN | WF_MIN)) ? wm_state_of(w) : -1;
    o->focus = w == m->focus;
    o->x = w->frame.x, o->y = w->frame.y, o->w = w->store.w, o->h = w->store.h;
    o->cx = c.x, o->cy = c.y;
    o->min_w = w->min_w, o->min_h = w->min_h, o->max_w = w->max_w, o->max_h = w->max_h;
    o->cursor = w->cursor;
    o->flash = w->flash;
    o->progress = w->progress ? w->progress - 1 : -1;
    memcpy(o->help, w->help, sizeof o->help);
    memcpy(o->title, w->title, sizeof o->title);
}

int desk_win_info(struct app *a, u32 id, struct desk_info *out)
{
    struct wm *m = desk_lock();
    struct wm_window *w = own(m, a, id);
    if (w)
        info(m, w, out);
    desk_unlock();
    return w ? 0 : GLOS_E_WINDOW;
}

u32 desk_win_list(struct desk_info *out, u32 max)
{
    struct wm *m = desk_lock();
    const struct wm_window *w;
    u32 n = 0;
    for (w = m->top; w; w = w->below) {
        if (w == m->bar)
            continue;
        if (n < max)
            info(m, w, &out[n]);
        n++;
    }
    desk_unlock();
    return n;
}

int desk_win_limit(struct app *a, u32 id, int which, s32 v)
{
    struct wm *m;
    struct wm_window *w;
    if (v < 0 || v > 4096)
        return GLOS_E_ARGUMENT;
    m = desk_lock();
    if ((w = own(m, a, id)) != 0) {
        if (which == 0) w->min_w = v;
        else if (which == 1) w->min_h = v;
        else if (which == 2) w->max_w = v;
        else w->max_h = v;
    }
    desk_unlock();
    return w ? 0 : GLOS_E_WINDOW;
}

int desk_win_cursor(struct app *a, u32 id, int shape)
{
    struct wm *m;
    struct wm_window *w;
    if (shape < 0 || shape > WC_CUSTOM)
        return GLOS_E_ARGUMENT;
    m = desk_lock();
    if ((w = own(m, a, id)) != 0) {
        if (shape == WC_CUSTOM && !w->cursor_img.w) {   /* (no picture of its own given yet) */
            desk_unlock();
            return GLOS_E_ARGUMENT;
        }
        w->cursor = (u8)shape;
    }
    desk_unlock();                              /* (the composer's turn gives the pointer its shape) */
    return w ? 0 : GLOS_E_WINDOW;
}

int desk_win_set(struct app *a, u32 id, int what, s32 v, const void *data, u32 len)
{
    struct wm *m = desk_lock();
    struct wm_window *w = own(m, a, id);
    int r = w ? 0 : GLOS_E_WINDOW;
    if (!w) {
        ;
    } else if (what == DW_TOPMOST) {
        wm_topmost(m, w, v != 0);
    } else if (what == DW_TASKBAR) {
        w->flags = v ? w->flags & ~(u32)WF_NOBAR : w->flags | WF_NOBAR;
        wm_bar_again(m);
    } else if (what == DW_FLASH) {
        w->flash = v && w != m->focus;          /* (the one with the focus has the attention) */
        wm_bar_again(m);
    } else if (what == DW_PROGRESS) {
        if (v < -1 || v > 100)
            r = GLOS_E_ARGUMENT;
        else {
            w->progress = (u8)(v + 1);
            wm_bar_again(m);
        }
    } else if (what == DW_ICON) {
        if (len != 0 && len != sizeof w->icon)
            r = GLOS_E_ARGUMENT;
        else {
            w->has_icon = len != 0;
            if (len)
                memcpy(w->icon, data, sizeof w->icon);
            wm_frame_again(m, w);
        }
    } else if (what == DW_CURSOR_IMG) {
        const struct wm_cursor *c = data;
        if (len != sizeof *c || !c->w || !c->h || c->w > 32 || c->h > 32 || c->hx >= c->w || c->hy >= c->h)
            r = GLOS_E_ARGUMENT;
        else {
            w->cursor_img = *c;
            w->cursor = WC_CUSTOM;
        }
    } else if (what == DW_HELP) {
        if (len >= sizeof w->help)
            r = GLOS_E_ARGUMENT;
        else {
            memcpy(w->help, data, len);
            w->help[len] = 0;
        }
    }
    desk_unlock();
    return r;
}

int desk_win_icon(struct app *a, u32 id, u32 *out)
{
    struct wm *m = desk_lock();
    struct wm_window *w = own(m, a, id);
    int r = !w ? -1 : w->has_icon;
    if (r > 0)
        memcpy(out, w->icon, sizeof w->icon);
    desk_unlock();
    return r;
}

int desk_win_focus_id(u32 id)
{
    struct wm *m = desk_lock();
    struct wm_window *w = wm_find(m, id);
    if (w && w != m->bar) {
        if (w->flags & WF_MIN)
            deskapp_state(m, w, WS_UNMIN);
        wm_set_focus(m, w);
    }
    desk_unlock();
    return w && w != m->bar ? 0 : GLOS_E_ARGUMENT;
}

int desk_win_drag(struct app *a, u32 id, int part)
{
    struct wm *m = desk_lock();
    struct wm_window *w = own(m, a, id);
    int r = !w ? GLOS_E_WINDOW : deskin_drag(m, w, part) != 0 ? DESK_E_REFUSED : 0;
    desk_unlock();
    return r;
}

void desk_work(s32 *x, s32 *y, s32 *w, s32 *h)
{
    struct wm *m = desk_lock();
    struct gfx_rect r;
    wm_work(m, &r);
    *x = r.x, *y = r.y, *w = r.w, *h = r.h;
    desk_unlock();
}

void deskapp_close(struct wm *m, struct wm_window *w)
{
    if (w->owner) {
        told(w, GLOS_EV_CLOSE);
    } else {
        wm_destroy(m, w);
        desk_refocus(m);
    }
}

void deskapp_screen(struct wm *m)
{
    struct wm_window *w;
    for (w = m->bottom; w; w = w->above)
        if (w->owner)
            told(w, GLOS_EV_RESIZE);
}

/* An app has ended, its address space gone already: its windows go, and
   the pixels it had mapped are free. On its thread, as it ends. */
void desk_app_gone(struct app *a)
{
    struct wm *m = desk_lock();
    struct wm_window *w, *next;
    u32 i;
    deskin_gone(a);
    for (i = 0; i < ARRAY_SIZE(pins); i++)
        if (pins[i].mem && pins[i].a == a) {
            if (pins[i].stale)
                mm_win_free(pins[i].mem, pins[i].bytes);
            pins[i].mem = 0;                    /* (a window's own: freed with the window, below) */
        }
    for (w = m->bottom; w; w = next) {
        next = w->above;
        if (w->owner == a)
            wm_destroy(m, w);
    }
    desk_refocus(m);
    desk_unlock();
}

/* Each turn of the desktop's thread: the windows of an app that isn't taking its events say so (their title bars,
   the pointer over them), and no longer when it does again. Not while apps are held (a session has the machine),
   nor for 5 seconds after. */
void deskapp_hung(struct wm *m)
{
    static u32 held_at;
    struct wm_window *w;
    u32 now = timer_ticks();
    int quiet = apps_paused();
    if (quiet)
        held_at = now ? now : 1;
    else if (held_at && now - held_at < 5 * 1024u)
        quiet = 1;
    for (w = m->bottom; w; w = w->above) {
        int hung = w->owner && !quiet && app_stalled(w->owner);
        if (hung != ((w->flags & WF_HUNG) != 0)) {
            w->flags ^= WF_HUNG;
            wm_frame_again(m, w);
            kprintf("GLOS-DESK window=%u app=%u %s\n", w->id, w->owner ? app_id(w->owner) : 0,
                    hung ? "not-answering" : "answering");
            if (desk_shell_app() && desk_shell_app() != w->owner)       /* (the shell is told: SHELL's event) */
                api2_shell_hung(desk_shell_app(), w->id, w->owner, hung);
        }
    }
}

int desk_win_capture(struct app *a, u32 id, int on)
{
    struct wm *m = desk_lock();
    struct wm_window *w = own(m, a, id);
    int r = !w ? GLOS_E_WINDOW : deskin_capture(m, w, on) != 0 ? DESK_E_REFUSED : 0;
    desk_unlock();
    return r;
}

void desk_keys(u8 down[64], u32 *mods, u32 *locks)
{
    desk_lock();
    deskin_keys(down, mods, locks);
    desk_unlock();
}

/* API 2.0's DRAW BLIT: the app's pixels, in a format of its own, into the window's (the screen's format) and to
   the screen. On the app's thread: its memory is read a row at a time. */
int desk_win_blit(struct app *a, u32 id, const struct desk_blit *b)
{
    static u8 row[4096 * 4];                    /* (the manager's lock is held: one at a time) */
    static u32 pal[256], lut[256];
    struct wm *m;
    struct wm_window *w;
    struct gfx_rect part;
    s32 x0 = b->x, y0 = b->y, sx = 0, sy = 0, cw = b->w, ch = b->h, y;
    u32 sb = b->fmt.bpp / 8u, i;
    int r = 0;
    if (!gfx_fmt_ok(&b->fmt) || b->w < 1 || b->h < 1 || b->w > 4096 || b->h > 4096 || b->pitch < (u32)b->w * sb)
        return GLOS_E_ARGUMENT;
    m = desk_lock();
    if (!(w = own(m, a, id))) {
        desk_unlock();
        return GLOS_E_WINDOW;
    }
    if (x0 < 0) sx = -x0, cw += x0, x0 = 0;     /* what of it lies in the client area */
    if (y0 < 0) sy = -y0, ch += y0, y0 = 0;
    if (x0 + cw > w->store.w) cw = w->store.w - x0;
    if (y0 + ch > w->store.h) ch = w->store.h - y0;
    if (cw > 0 && ch > 0) {
        const u32 *table = 0;
        if (b->fmt.bpp == 8 && b->palette) {    /* its own colours: each one's pixel on this screen */
            if (user_rd(b->ds, b->palette, pal, sizeof pal) != 0)
                r = GLOS_E_ARGUMENT;
            for (i = 0; i < 256 && !r; i++)
                lut[i] = m->fmt.bpp == 8 ? gfx_pal_nearest(pal[i] & 0xFFFFFF) : gfx_px(&m->fmt, pal[i] & 0xFFFFFF);
            table = lut;
        }
        if (w->store.dev)
            w->store.dev->sync(&w->store);      /* (an engine's work on these pixels done first) */
        for (y = 0; y < ch && !r; y++) {
            if (user_rd(b->ds, b->src + (u32)(sy + y) * b->pitch + (u32)sx * sb, row, (u32)cw * sb) != 0) {
                r = GLOS_E_ARGUMENT;
                break;
            }
            gfx_convert(&m->fmt, w->store.pix + (u32)(y0 + y) * w->store.pitch + (u32)x0 * (m->fmt.bpp / 8u), &b->fmt, row,
                        cw, table, x0, y0 + y);
        }
        part.x = x0, part.y = y0, part.w = cw, part.h = y;
        if (y > 0)
            wm_damage(m, w, &part);
    }
    desk_unlock();
    return r;
}

/* ---- API 2.0's CAPTURE: a window seen and driven from outside (api2cap.c says who may) */

int desk_win_who(u32 id, u32 *owner, s32 *w, s32 *h)
{
    struct wm *m = desk_lock();
    struct wm_window *win = wm_find(m, id);
    int r = win ? 0 : GLOS_E_WINDOW;
    if (win)
        *owner = win->owner ? app_id(win->owner) : 0, *w = win->store.w, *h = win->store.h;
    desk_unlock();
    return r;
}

int desk_win_rows(u32 id, s32 y, s32 n, s32 w, u8 *rgb)
{
    struct wm *m = desk_lock();
    struct wm_window *win = wm_find(m, id);
    const struct gfx_surface *s;
    s32 x, got = 0;
    u32 bytes = m->fmt.bpp / 8u;
    if (!win || win->store.w != w) {
        desk_unlock();
        return -1;
    }
    s = win->has_ext ? &win->ext : &win->store;        /* (what it shows: an app's own pixels in the card's memory) */
    if (s->dev)
        s->dev->sync((struct gfx_surface *)s);          /* (an engine's work on them done first) */
    for (; got < n && y + got < s->h; got++) {
        const u8 *p = s->pix + (u32)(y + got) * s->pitch;
        for (x = 0; x < w; x++, p += bytes) {
            u32 px = bytes == 1 ? p[0] : bytes == 2 ? (u32)(p[0] | p[1] << 8) : (u32)(p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24);
            u32 c = gfx_rgb(&m->fmt, px);
            *rgb++ = (u8)(c >> 16), *rgb++ = (u8)(c >> 8), *rgb++ = (u8)c;
        }
    }
    desk_unlock();
    return got;
}

int desk_win_key(u32 id, u32 key, int down, u32 mods)
{
    struct wm *m = desk_lock();
    struct wm_window *win = wm_find(m, id);
    struct glos_event e;
    int r = win && win->owner ? 0 : GLOS_E_WINDOW;
    if (!r) {
        memset(&e, 0, sizeof e);
        e.type = GLOS_EV_KEY;
        e.window = id;
        e.key = key;
        e.ch = down ? keymap_char(key, (mods & GLOS_MOD_SHIFT) != 0, (mods & GLOS_MOD_CTRL) != 0, (mods & GLOS_MOD_ALT) != 0, 0, 0) : 0;
        e.mods = mods;
        e.down = (unsigned)(down != 0);
        app_event(win->owner, &e);
    }
    desk_unlock();
    return r;
}

int desk_win_mouse(u32 id, s32 x, s32 y, u32 buttons, u32 button, int down, u32 clicks)
{
    struct wm *m = desk_lock();
    struct wm_window *win = wm_find(m, id);
    struct glos_event e;
    int r = win && win->owner ? 0 : GLOS_E_WINDOW;
    if (!r) {
        memset(&e, 0, sizeof e);
        e.type = GLOS_EV_MOUSE;
        e.window = id;
        e.x = x, e.y = y;
        e.buttons = buttons;
        e.key = button;
        e.down = (unsigned)(down != 0);
        e.ch = button && down ? (clicks ? clicks : 1) : 0;
        app_event(win->owner, &e);
    }
    desk_unlock();
    return r;
}

/* ---- API 2.0's EMBED: a window shown inside another app's (api2win.c says who may) */

/* One such window put where its host has it now, its app told of a new size; hidden while its host is, and its
   own window again once its host is gone. */
static void embed_fit(struct wm *m, struct wm_window *w)
{
    struct gfx_rect r;
    s32 cw, ch;
    int host_shown = 0;
    if (!wm_embed_where(m, w, &r, &host_shown)) {
        wm_unembed(m, w, &cw, &ch);
        deskapp_resize(m, w, cw, ch);
        return;
    }
    wm_move(m, w, r.x, r.y);
    deskapp_resize(m, w, r.w, r.h);
    if (!host_shown && (w->flags & WF_SHOWN)) {
        wm_show(m, w, 0);
        w->flags |= WF_WITH;
    } else if (host_shown && (w->flags & WF_WITH) && !(w->flags & WF_SHOWN)) {
        wm_show(m, w, 1);
    }
}

void deskapp_embeds(struct wm *m)
{
    struct wm_window *w;
    u32 ids[16], n = 0, i;
    for (w = m->bottom; w && n < 16; w = w->above)     /* (their numbers first: showing one changes the order) */
        if (w->flags & WF_EMBED)
            ids[n++] = w->id;
    for (i = 0; i < n; i++)
        if ((w = wm_find(m, ids[i])) != 0)
            embed_fit(m, w);
}

int desk_win_embed(struct app *a, u32 host, u32 child, const struct gfx_rect *at)
{
    struct wm *m = desk_lock();
    struct wm_window *h = own(m, a, host), *w = wm_find(m, child);
    s32 cw, ch;
    int r = 0;
    if (!h || !w || !w->owner) {
        r = GLOS_E_WINDOW;
    } else if (at) {
        if (wm_embed(m, w, h, at) != 0)
            r = DESK_E_REFUSED;
        else
            embed_fit(m, w);
    } else if ((w->flags & WF_EMBED) && w->parent == h->id) {
        wm_unembed(m, w, &cw, &ch);
        deskapp_resize(m, w, cw, ch);
    } else {
        r = DESK_E_REFUSED;                     /* (not one of this window's) */
    }
    desk_unlock();
    return r;
}
