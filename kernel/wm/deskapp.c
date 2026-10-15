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
#include "kprintf.h"
#include "mgadisp.h"
#include "mm.h"

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
    g->x = w->frame.x;
    g->y = w->frame.y;
    g->w = w->store.w;
    g->h = w->store.h;
    g->flags = (w->flags & WF_FIXED ? GLOS_WIN_FIXED : 0u) | (w->flags & WF_FRAME ? 0u : GLOS_WIN_BARE);
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
    u32 lin, flags = (g->flags & GLOS_WIN_BARE ? 0u : WF_FRAME) | (g->flags & GLOS_WIN_FIXED ? WF_FIXED : 0u);
    s32 x = g->x, y = g->y;
    int r;
    if (g->w < 1 || g->h < 1 || g->w > 4096 || g->h > 4096)
        return GLOS_E_ARGUMENT;
    g->title[sizeof g->title - 1] = 0;
    m = desk_lock();
    let_go(a);
    if (g->flags & GLOS_WIN_ANYWHERE) {         /* down and right of the last, a few times, then from the top again */
        x = 24 + (s32)(m->next_id % 8) * 24;
        y = 16 + (s32)(m->next_id % 8) * 24;
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

int desk_win_show(struct app *a, u32 id, int on)
{
    struct wm *m = desk_lock();
    struct wm_window *w = own(m, a, id);
    if (w) {
        wm_show(m, w, on);
        if (on)
            wm_set_focus(m, w);
        else
            desk_refocus(m);
    }
    desk_unlock();
    return w ? 0 : GLOS_E_WINDOW;
}

int desk_win_move(struct app *a, u32 id, s32 x, s32 y)
{
    struct wm *m = desk_lock();
    struct wm_window *w = own(m, a, id);
    if (w)
        wm_move(m, w, x, y);
    desk_unlock();
    return w ? 0 : GLOS_E_WINDOW;
}

int desk_win_resize(struct app *a, u32 id, s32 cw, s32 ch)
{
    struct wm *m;
    struct wm_window *w;
    int r = 0;
    if (cw < 1 || ch < 1 || cw > 4096 || ch > 4096)
        return GLOS_E_ARGUMENT;
    m = desk_lock();
    if (!(w = own(m, a, id)))
        r = GLOS_E_WINDOW;
    else if (wm_resize(m, w, cw, ch) != 0)
        r = GLOS_E_MEMORY;
    let_go(a);                                  /* (the old ones, which wm_resize() left to this) */
    desk_unlock();
    return r;
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
    } else if (m->fmt.bpp != 16 || m->fmt.gsize != 6 || !mga_card()) {
        r = GLOS_E_NO_GPU;                      /* (GLOS_CAP_GLWIN: the app's pixels are RGB565) */
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
