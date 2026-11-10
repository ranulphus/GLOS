/* API 2.0's WIN and DRAW (include/glos/api2.h; docs/v1/phase-b.md, step 2),
 * and the desktop's events for a 2.0 app.
 *
 * A window's handle holds the window's number on the desktop (wm's id), not
 * memory. The desktop (kernel/wm/deskapp.c) does the work, on the app's own
 * thread; it sends its events as it sends 1.x's (app_event), and they are
 * given their 2.0 form here, the window's handle found by its number. That
 * happens on the desktop's thread, so the app's table of handles changes
 * with interrupts off where a window's handle is made or closed. */
#include "api2int.h"
#include "desk.h"
#include "io.h"
#include "kprintf.h"
#include "wm.h"

static u32 win_id(struct app *a, u32 handle)
{
    return (u32)handle_obj(&a->v2->h, handle, GLOS_HK_WINDOW);
}

/* The call's answer for a handle that is no window's: no such handle, or one of another kind. */
static int not_window(struct app *a, struct trapframe *tf, u32 handle)
{
    return a2_fail(tf, handle_kind(&a->v2->h, handle) ? GLOS_E_KIND : GLOS_E_HANDLE);
}

/* The handle of the app's window with that number (interrupts off), or 0. */
static u32 win_handle(struct app *a, u32 id)
{
    u32 i = 0, h;
    void *obj;
    while (handle_next(&a->v2->h, &i, &h, &obj) == 0)
        if (GLOS_HANDLE_KIND(h) == GLOS_HK_WINDOW && (u32)obj == id)
            return h;
    return 0;
}

static u32 state_of(int ws)                     /* the desktop's WS_ (or -1) as GLOS_WS_ */
{
    return ws == WS_MIN ? GLOS_WS_MIN : ws == WS_MAX ? GLOS_WS_MAX : ws == WS_FULL ? GLOS_WS_FULL
         : ws == WS_SNAP_L ? GLOS_WS_LEFT : ws == WS_SNAP_R ? GLOS_WS_RIGHT : ws < 0 ? GLOS_WS_HIDDEN : GLOS_WS_NORMAL;
}

/* ---- the desktop's events */

void api2_event_1x(struct app *a, const struct glos_event *e)
{
    union {
        struct glos_ev ev;
        struct glos_ev_key key;
        struct glos_ev_mouse mouse;
        struct glos_ev_wheel wheel;
        struct glos_ev_focus focus;
        struct glos_ev_size size;
        struct glos_ev_state state;
    } u;
    u32 f = irq_save(), h = a->v2 ? win_handle(a, e->window) : 0;
    int merge = 0;
    irq_restore(f);
    if (!h) {                                   /* (not a window's, or one whose handle is closed already) */
        if (a->v2)
            a->v2->ndrop1x++;
        return;
    }
    memset(&u, 0, sizeof u);
    u.ev.handle = h;
    switch (e->type) {
    case V1_EV_KEY:
        u.ev.size = sizeof u.key, u.ev.kind = GLOS_EV_KEY;
        u.key.key = e->key, u.key.ch = e->ch, u.key.mods = e->mods, u.key.down = e->down;
        break;
    case V1_EV_MOUSE:
        u.ev.size = sizeof u.mouse, u.ev.kind = GLOS_EV_MOUSE;
        u.mouse.x = e->x, u.mouse.y = e->y;
        u.mouse.dx = (s32)e->size, u.mouse.dy = (s32)e->time;   /* (the desktop's: how far it moved, deskin.c) */
        u.mouse.buttons = e->buttons, u.mouse.button = e->key, u.mouse.down = e->down, u.mouse.mods = e->mods;
        merge = !e->key;
        break;
    case V1_EV_WHEEL:
        u.ev.size = sizeof u.wheel, u.ev.kind = GLOS_EV_WHEEL;
        u.wheel.x = e->x, u.wheel.y = e->y, u.wheel.notches = (s32)e->key;
        u.wheel.buttons = e->buttons, u.wheel.mods = e->mods;
        break;
    case V1_EV_FOCUS:
        u.ev.size = sizeof u.focus, u.ev.kind = GLOS_EV_FOCUS;
        u.focus.got = e->down;
        break;
    case V1_EV_RESIZE:
        u.ev.size = sizeof u.size, u.ev.kind = GLOS_EV_SIZE;
        u.size.w = e->x, u.size.h = e->y;
        break;
    case V1_EV_CLOSE:
        u.ev.size = sizeof u.ev, u.ev.kind = GLOS_EV_CLOSE;
        break;
    case DESK_EV_STATE:
        u.ev.size = sizeof u.state, u.ev.kind = GLOS_EV_STATE;
        u.state.state = state_of((int)e->key);
        break;
    default:
        a->v2->ndrop1x++;                       /* (jobs, watches, the shell's: with their areas' steps) */
        return;
    }
    a2_post(a, &u.ev, merge);
}

/* ---- WIN */

static int create(struct app *a, struct trapframe *tf)
{
    struct glos_win_create q;
    struct desk_new n;
    u32 size, id, f, h;
    int r;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || q.kind > GLOS_WK_DESKTOP
        || (q.flags & ~(u32)(GLOS_WF_FIXED | GLOS_WF_BARE | GLOS_WF_ANYWHERE | GLOS_WF_MODAL)))
        return a2_fail(tf, GLOS_E_ARGUMENT);
    q.title[sizeof q.title - 1] = 0;
    memset(&n, 0, sizeof n);
    n.x = q.x, n.y = q.y, n.w = q.w, n.h = q.h;
    n.title = q.title;
    n.tag = q.kind | (q.flags << 8);
    n.anywhere = (q.flags & GLOS_WF_ANYWHERE) != 0;
    n.wf = q.flags & GLOS_WF_FIXED ? WF_FIXED : 0;
    if (q.kind == GLOS_WK_POPUP)
        n.wf |= WF_POPUP | WF_TOPMOST;
    else if (q.kind == GLOS_WK_DESKTOP)
        n.wf |= WF_DESKTOP | WF_FIXED, n.desktop = 1;
    else {
        if (!(q.flags & GLOS_WF_BARE))
            n.wf |= WF_FRAME;
        if (q.kind == GLOS_WK_DIALOG || q.kind == GLOS_WK_TOOL)
            n.wf |= WF_NOBAR | WF_NOMIN;
    }
    if (q.owner && !(n.parent = win_id(a, q.owner)))
        return not_window(a, tf, q.owner);
    if ((r = desk_win_new(a, &n, &id)) != 0)
        return a2_fail(tf, (u32)r);
    f = irq_save();
    h = handle_new(&a->v2->h, GLOS_HK_WINDOW, (void *)id);
    irq_restore(f);
    q.handle = h;
    if (!h || a2_wr(tf, &q, sizeof q, size) != 0) {
        if (h) {
            f = irq_save();
            handle_close(&a->v2->h, h);
            irq_restore(f);
        }
        desk_win_close(a, id);
        return a2_fail(tf, h ? GLOS_E_ARGUMENT : GLOS_E_MEMORY);
    }
    a->gui = 1;                                 /* (its keyboard and its mouse are the desktop's from now on) */
    return a2_ok(tf, 0);
}

int api2_win_close(struct app *a, u32 id) { return desk_win_close(a, id); }

static int show(struct app *a, struct trapframe *tf)
{
    struct glos_win_show q;
    u32 size, id;
    int st, r;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || q.state > GLOS_WS_SHOWN)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(id = win_id(a, q.handle)))
        return not_window(a, tf, q.handle);
    st = q.state == GLOS_WS_HIDDEN ? -1 : q.state == GLOS_WS_SHOWN ? -2 : q.state == GLOS_WS_MIN ? WS_MIN
       : q.state == GLOS_WS_MAX ? WS_MAX : q.state == GLOS_WS_FULL ? WS_FULL : WS_NORMAL;
    r = desk_win_state(a, id, st, !(q.flags & GLOS_SHOW_NOFOCUS));
    return r ? a2_fail(tf, (u32)r) : a2_ok(tf, 0);
}

static int place(struct app *a, struct trapframe *tf)
{
    struct glos_win_place q;
    u32 size, id;
    int r;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || !q.what || (q.what & ~(u32)(GLOS_PLACE_MOVE | GLOS_PLACE_SIZE)))
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(id = win_id(a, q.handle)))
        return not_window(a, tf, q.handle);
    r = desk_win_place(a, id, q.what & GLOS_PLACE_MOVE, q.x, q.y, q.what & GLOS_PLACE_SIZE, q.w, q.h);
    return r ? a2_fail(tf, (u32)r) : a2_ok(tf, 0);
}

static int focus(struct app *a, struct trapframe *tf)
{
    struct glos_win_focus q;
    u32 size, id;
    int r;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!q.handle)                              /* (any window: the shell's and Task Manager's; SHELL's step says who) */
        return (r = desk_win_focus_id(q.id)) != 0 ? a2_fail(tf, (u32)r) : a2_ok(tf, 0);
    if (!(id = win_id(a, q.handle)))
        return not_window(a, tf, q.handle);
    return (r = desk_win_focus(a, id)) != 0 ? a2_fail(tf, (u32)r) : a2_ok(tf, 0);
}

static void describe(const struct desk_info *d, struct glos_win_info *o)
{
    memset(o, 0, sizeof *o);
    o->size = sizeof *o;
    o->id = d->id;
    o->app = d->owner ? app_id(d->owner) : 0;
    o->kind = d->tag & 0xFF, o->flags = d->tag >> 8;
    if (!d->owner || !d->owner->v2) {           /* (not made through this API: what its flags say) */
        o->kind = d->wf & WF_DESKTOP ? GLOS_WK_DESKTOP : d->wf & WF_POPUP ? GLOS_WK_POPUP : GLOS_WK_NORMAL;
        o->flags = (d->wf & WF_FIXED ? GLOS_WF_FIXED : 0u) | (d->wf & WF_FRAME ? 0u : GLOS_WF_BARE);
    }
    o->state = state_of(d->state);
    o->focus = (unsigned)d->focus;
    o->x = d->x, o->y = d->y, o->w = d->w, o->h = d->h;
    memcpy(o->title, d->title, sizeof o->title < sizeof d->title ? sizeof o->title : sizeof d->title);
    o->title[sizeof o->title - 1] = 0;
}

#define ENUM_MAX 96                             /* windows listed at once */

static int enumerate(struct app *a, struct trapframe *tf)
{
    static struct desk_info list[ENUM_MAX];     /* (one call at a time: the lock below) */
    static struct waitq q;
    static struct thread *owner;
    struct glos_win_enum e;
    struct glos_win_info one;
    u32 size, n, i, f, count = 0, fit = 0;
    int bad = 0;
    if (a2_rd(tf, &e, sizeof e, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    f = irq_save();
    while (owner && owner != current)
        thread_block(&q);
    owner = current;
    irq_restore(f);
    n = desk_win_list(list, ENUM_MAX);
    if (n > ENUM_MAX)
        n = ENUM_MAX;
    for (i = 0; i < n && !bad; i++) {
        u32 app = list[i].owner ? app_id(list[i].owner) : 0;
        if (e.app != GLOS_ENUM_ALL && app != (e.app ? e.app : app_id(a)))
            continue;
        count++;
        if ((fit + 1) * sizeof one > e.room)
            continue;
        describe(&list[i], &one);
        bad = user_wr((u16)tf->ds, e.buffer + fit * sizeof one, &one, sizeof one) != 0;
        fit++;
    }
    f = irq_save();
    owner = 0;
    thread_wake(&q);
    irq_restore(f);
    e.count = count;
    if (bad || a2_wr(tf, &e, sizeof e, size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    return fit < count ? a2_fail(tf, GLOS_E_ROOM) : a2_ok(tf, 0);
}

static int drag(struct app *a, struct trapframe *tf)
{
    struct glos_win_drag q;
    u32 size, id;
    int r;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || (q.part & ~31u) || (q.part & 15 && !(q.part & GLOS_DRAG_SIZE)))
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(id = win_id(a, q.handle)))
        return not_window(a, tf, q.handle);
    r = desk_win_drag(a, id, q.part & GLOS_DRAG_SIZE ? WH_SIZE | (int)(q.part & 15) : WH_TITLE);
    return r ? a2_fail(tf, (u32)r) : a2_ok(tf, 0);
}

int api2_win(struct app *a, struct trapframe *tf, u32 n)
{
    switch (n) {
    case GLOS_WIN_CREATE & 0xFF: return create(a, tf);
    case GLOS_WIN_SHOW & 0xFF: return show(a, tf);
    case GLOS_WIN_PLACE & 0xFF: return place(a, tf);
    case GLOS_WIN_FOCUS & 0xFF: return focus(a, tf);
    case GLOS_WIN_ENUM & 0xFF: return enumerate(a, tf);
    case GLOS_WIN_DRAG & 0xFF: return drag(a, tf);
    }
    return a2_fail(tf, GLOS_E_FUNCTION);
}

/* ---- a window's properties */

static int w_title_get(struct app *a, void *o, s32 *v, char *x, u32 *l)
{
    struct desk_info d;
    (void)v;
    if (desk_win_info(a, (u32)o, &d) != 0)
        return GLOS_E_HANDLE;
    *l = (u32)strlen(d.title);
    memcpy(x, d.title, *l);
    return 0;
}

static int w_title_set(struct app *a, void *o, s32 v, const char *x, u32 l)
{
    char t[64];
    (void)v;
    if (l >= sizeof t)
        return GLOS_E_ARGUMENT;
    memcpy(t, x, l);
    t[l] = 0;
    return desk_win_title(a, (u32)o, t) ? GLOS_E_HANDLE : 0;
}

/* The numbers: which of desk_info's. */
static int w_num(struct app *a, void *o, s32 *v, int which)
{
    struct desk_info d;
    if (desk_win_info(a, (u32)o, &d) != 0)
        return GLOS_E_HANDLE;
    *v = which == 0 ? (s32)state_of(d.state) : which == 1 ? d.cx : which == 2 ? d.cy : which == 3 ? d.w : which == 4 ? d.h
       : which == 5 ? d.min_w : which == 6 ? d.min_h : which == 7 ? d.max_w : which == 8 ? d.max_h : which == 9 ? d.cursor
       : (s32)d.id;
    return 0;
}
#define W_GET(name, which) \
    static int name(struct app *a, void *o, s32 *v, char *x, u32 *l) { (void)x; (void)l; return w_num(a, o, v, which); }
W_GET(w_state_get, 0) W_GET(w_x_get, 1) W_GET(w_y_get, 2) W_GET(w_w_get, 3) W_GET(w_h_get, 4)
W_GET(w_minw_get, 5) W_GET(w_minh_get, 6) W_GET(w_maxw_get, 7) W_GET(w_maxh_get, 8) W_GET(w_cursor_get, 9)
W_GET(w_id_get, 10)
#define W_LIMIT(name, which) \
    static int name(struct app *a, void *o, s32 v, const char *x, u32 l) \
    { (void)x; (void)l; return desk_win_limit(a, (u32)o, which, v); }
W_LIMIT(w_minw_set, 0) W_LIMIT(w_minh_set, 1) W_LIMIT(w_maxw_set, 2) W_LIMIT(w_maxh_set, 3)

static int w_cursor_set(struct app *a, void *o, s32 v, const char *x, u32 l)
{
    (void)x; (void)l;
    return desk_win_cursor(a, (u32)o, v);       /* (GLOS_CURSOR_ are the desktop's WC_) */
}

const struct propdef api2_win_props[] = {
    { GLOS_PROP_WIN_TITLE, P_TEXT, w_title_get, w_title_set },
    { GLOS_PROP_WIN_STATE, P_NUM | P_RO, w_state_get, 0 },
    { GLOS_PROP_WIN_X, P_NUM | P_RO, w_x_get, 0 },
    { GLOS_PROP_WIN_Y, P_NUM | P_RO, w_y_get, 0 },
    { GLOS_PROP_WIN_W, P_NUM | P_RO, w_w_get, 0 },
    { GLOS_PROP_WIN_H, P_NUM | P_RO, w_h_get, 0 },
    { GLOS_PROP_WIN_MIN_W, P_NUM, w_minw_get, w_minw_set },
    { GLOS_PROP_WIN_MIN_H, P_NUM, w_minh_get, w_minh_set },
    { GLOS_PROP_WIN_MAX_W, P_NUM, w_maxw_get, w_maxw_set },
    { GLOS_PROP_WIN_MAX_H, P_NUM, w_maxh_get, w_maxh_set },
    { GLOS_PROP_WIN_CURSOR, P_NUM, w_cursor_get, w_cursor_set },
    { GLOS_PROP_WIN_ID, P_NUM | P_RO, w_id_get, 0 },
};
const u32 api2_win_nprops = ARRAY_SIZE(api2_win_props);

/* ---- DRAW */

static void format_of(const struct glos_window *g, struct glos_format *f)
{
    f->bpp = g->bpp;
    f->rpos = g->rpos, f->rsize = g->rsize, f->gpos = g->gpos, f->gsize = g->gsize, f->bpos = g->bpos, f->bsize = g->bsize;
    f->pad[0] = f->pad[1] = 0;
}

static int pixels(struct app *a, struct trapframe *tf)
{
    struct glos_pixels q;
    struct glos_window g;
    u32 size, id;
    int r;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(id = win_id(a, q.handle)))
        return not_window(a, tf, q.handle);
    if ((r = desk_win_pixels(a, id, &g)) != 0)
        return a2_fail(tf, (u32)r);
    q.pixels = g.pixels, q.pitch = g.pitch, q.w = g.w, q.h = g.h;
    format_of(&g, &q.format);
    return a2_wr(tf, &q, sizeof q, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

static int present(struct app *a, struct trapframe *tf)
{
    struct glos_present q;
    struct glos_rect r;
    u32 size, id;
    int e;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || q.w < 0 || q.h < 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(id = win_id(a, q.handle)))
        return not_window(a, tf, q.handle);
    r.x = q.x, r.y = q.y, r.w = q.w, r.h = q.h;
    e = desk_win_present(a, id, q.w && q.h ? &r : 0);
    return e ? a2_fail(tf, (u32)e) : a2_ok(tf, 0);
}

int api2_screen(struct trapframe *tf)
{
    struct glos_screen q;
    struct glos_window g;
    u32 size;
    s32 x, y, w, h;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    desk_screen(&g);
    desk_work(&x, &y, &w, &h);
    q.w = g.w, q.h = g.h;
    format_of(&g, &q.format);
    q.work_x = x, q.work_y = y, q.work_w = w, q.work_h = h;
    return a2_wr(tf, &q, sizeof q, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

int api2_draw(struct app *a, struct trapframe *tf, u32 n)
{
    switch (n) {
    case GLOS_DRAW_PIXELS & 0xFF: return pixels(a, tf);
    case GLOS_DRAW_PRESENT & 0xFF: return present(a, tf);
    case GLOS_DRAW_SCREEN & 0xFF: return api2_screen(tf);
    }
    return a2_fail(tf, GLOS_E_FUNCTION);
}
