/* API 2.0's WIN and DRAW (include/glos/api.h; docs/v1/phase-b.md, step 2),
 * and the desktop's events for a 2.0 app.
 *
 * A window's handle holds the window's number on the desktop (wm's id), not
 * memory. The desktop (kernel/wm/deskapp.c) does the work, on the app's own
 * thread; it sends its events as the kernel's own (struct kevent, app_event), and they
 * are given the API's form here, the window's handle found by its number. That
 * happens on the desktop's thread, so the app's table of handles changes
 * with interrupts off where a window's handle is made or closed. */
#include "api2int.h"
#include "display.h"
#include "desk.h"
#include "gpu.h"
#include "mgadisp.h"
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

void api2_event_1x(struct app *a, const struct kevent *e)
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
    if (a->v2 && e->type == KEV_FILES) {    /* a watch's (kernel/dos/watch.c): FS's event, for its handle */
        u32 i = 0, wh;
        void *obj;
        struct glos_ev ev;
        memset(&ev, 0, sizeof ev);
        while (handle_next(&a->v2->h, &i, &wh, &obj) == 0)
            if (GLOS_HANDLE_KIND(wh) == GLOS_HK_WATCH && (u32)obj == e->key)
                ev.handle = wh;
        irq_restore(f);
        if (ev.handle) {
            ev.size = sizeof ev;
            ev.kind = GLOS_EV_FS;
            a2_post(a, &ev, 1);                 /* (one waiting already stands for this change too) */
        }
        return;
    }
    irq_restore(f);
    if (a->v2 && e->type == KEV_JOB) {        /* a job's news (kernel/dos/agent.c): JOB's event */
        api2_job_event(a, e->key, (u32)e->x, (u32)e->y);
        return;
    }
    if (a->v2 && (e->type == KEV_LAUNCH || e->type == KEV_SYSKEY)) {    /* the shell's: SHELL's event */
        api2_shell_1x(a, e->type, e->x, e->y);
        return;
    }
    if (!h) {                                   /* (not a window's, or one whose handle is closed already) */
        if (a->v2)
            a->v2->ndrop1x++;
        return;
    }
    memset(&u, 0, sizeof u);
    u.ev.handle = h;
    switch (e->type) {
    case KEV_KEY:
        u.ev.size = sizeof u.key, u.ev.kind = GLOS_EV_KEY;
        u.key.key = e->key, u.key.ch = e->ch, u.key.mods = e->mods, u.key.down = e->down;
        u.key.repeat = e->buttons;              /* (the desktop's: the keyboard repeating a held key, deskin.c) */
        if (a->v2->in_flags & GLOS_INPUT_RAW) {
            if (u.key.repeat)
                return;                         /* (raw: only what the key itself did) */
        } else if (e->down && e->ch >= 0x20 && e->ch != 0x7F) {
            struct glos_ev_text t;              /* what it typed, after the key */
            a2_post(a, &u.ev, 0);
            memset(&t, 0, sizeof t);
            t.ev.size = sizeof t, t.ev.kind = GLOS_EV_TEXT, t.ev.handle = h;
            t.ch = e->ch, t.mods = e->mods;
            a2_post(a, &t.ev, 0);
            return;
        }
        break;
    case KEV_MOUSE:
        u.ev.size = sizeof u.mouse, u.ev.kind = GLOS_EV_MOUSE;
        u.mouse.x = e->x, u.mouse.y = e->y;
        u.mouse.dx = (s32)e->size, u.mouse.dy = (s32)e->time;   /* (the desktop's: how far it moved, deskin.c) */
        u.mouse.buttons = e->buttons, u.mouse.button = e->key, u.mouse.down = e->down, u.mouse.mods = e->mods;
        u.mouse.clicks = e->ch;                 /* (the desktop's count of presses in a row) */
        merge = !e->key;
        break;
    case KEV_WHEEL:
        u.ev.size = sizeof u.wheel, u.ev.kind = GLOS_EV_WHEEL;
        u.wheel.x = e->x, u.wheel.y = e->y, u.wheel.notches = (s32)e->key;
        u.wheel.buttons = e->buttons, u.wheel.mods = e->mods;
        break;
    case KEV_FOCUS:
        u.ev.size = sizeof u.focus, u.ev.kind = GLOS_EV_FOCUS;
        u.focus.got = e->down;
        break;
    case KEV_RESIZE:
        u.ev.size = sizeof u.size, u.ev.kind = GLOS_EV_SIZE;
        u.size.w = e->x, u.size.h = e->y;
        break;
    case KEV_CLOSE:
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
    if (q.flags & GLOS_WF_MODAL) {              /* (a dialog with an owner) */
        if (q.kind != GLOS_WK_DIALOG || !q.owner)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        n.wf |= WF_MODAL;
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
    if (!q.handle) {                            /* any window: for GLOS's own programs (the shell, Task Manager) */
        if (!app_own(a))
            return a2_fail(tf, GLOS_E_REFUSED);
        return (r = desk_win_focus_id(q.id)) != 0 ? a2_fail(tf, (u32)r) : a2_ok(tf, 0);
    }
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
    if (d->wf & WF_HUNG)
        o->flags |= GLOS_WF_HUNG;
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

static int embed(struct app *a, struct trapframe *tf)
{
    struct glos_win_embed q;
    struct gfx_rect at;
    u32 size, id;
    int r;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || q.w < 0 || q.h < 0 || q.w > 4096 || q.h > 4096 || (q.w && !q.h))
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(id = win_id(a, q.handle)))
        return not_window(a, tf, q.handle);
    if ((r = api2_started_window(a, q.window)) != 0)    /* (its own, or a program's it started: CAPTURE's rule) */
        return a2_fail(tf, (u32)r);
    at.x = q.x, at.y = q.y, at.w = q.w, at.h = q.h;
    r = desk_win_embed(a, id, q.window, q.w ? &at : 0);
    return r ? a2_fail(tf, r == DESK_E_REFUSED ? GLOS_E_REFUSED : GLOS_E_NOT_FOUND) : a2_ok(tf, 0);
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
    case GLOS_WIN_EMBED & 0xFF: return embed(a, tf);
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
       : which == 10 ? (s32)d.id : which == 11 ? (d.wf & WF_TOPMOST) != 0 : which == 12 ? !(d.wf & WF_NOBAR)
       : which == 13 ? d.flash : d.progress;
    return 0;
}
#define W_GET(name, which) \
    static int name(struct app *a, void *o, s32 *v, char *x, u32 *l) { (void)x; (void)l; return w_num(a, o, v, which); }
W_GET(w_state_get, 0) W_GET(w_x_get, 1) W_GET(w_y_get, 2) W_GET(w_w_get, 3) W_GET(w_h_get, 4)
W_GET(w_minw_get, 5) W_GET(w_minh_get, 6) W_GET(w_maxw_get, 7) W_GET(w_maxh_get, 8) W_GET(w_cursor_get, 9)
W_GET(w_id_get, 10) W_GET(w_top_get, 11) W_GET(w_bar_get, 12) W_GET(w_flash_get, 13) W_GET(w_prog_get, 14)
#define W_SET(name, what) \
    static int name(struct app *a, void *o, s32 v, const char *x, u32 l) \
    { (void)x; (void)l; return desk_win_set(a, (u32)o, what, v, 0, 0); }
W_SET(w_top_set, DW_TOPMOST) W_SET(w_bar_set, DW_TASKBAR) W_SET(w_flash_set, DW_FLASH) W_SET(w_prog_set, DW_PROGRESS)
#define W_PUT(name, what) \
    static int name(struct app *a, void *o, s32 v, const char *x, u32 l) \
    { (void)v; return desk_win_set(a, (u32)o, what, 0, x, l); }
W_PUT(w_icon_set, DW_ICON) W_PUT(w_pointer_set, DW_CURSOR_IMG) W_PUT(w_help_set, DW_HELP)

static int w_icon_get(struct app *a, void *o, s32 *v, char *x, u32 *l)
{
    int r = desk_win_icon(a, (u32)o, (u32 *)(void *)x);
    (void)v;
    if (r < 0)
        return GLOS_E_HANDLE;
    *l = r ? 1024 : 0;
    return 0;
}

static int w_pointer_get(struct app *a, void *o, s32 *v, char *x, u32 *l)
{
    (void)a; (void)o; (void)v; (void)x;
    *l = 0;                                     /* (write only: nothing comes back) */
    return 0;
}

static int w_help_get(struct app *a, void *o, s32 *v, char *x, u32 *l)
{
    struct desk_info d;
    (void)v;
    if (desk_win_info(a, (u32)o, &d) != 0)
        return GLOS_E_HANDLE;
    *l = (u32)strlen(d.help);
    memcpy(x, d.help, *l);
    return 0;
}
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
    { GLOS_PROP_WIN_TOPMOST, P_NUM, w_top_get, w_top_set },
    { GLOS_PROP_WIN_TASKBAR, P_NUM, w_bar_get, w_bar_set },
    { GLOS_PROP_WIN_FLASH, P_NUM, w_flash_get, w_flash_set },
    { GLOS_PROP_WIN_PROGRESS, P_NUM, w_prog_get, w_prog_set },
    { GLOS_PROP_WIN_ICON, P_DATA, w_icon_get, w_icon_set },
    { GLOS_PROP_WIN_POINTER, P_DATA, w_pointer_get, w_pointer_set },
    { GLOS_PROP_WIN_HELP, P_TEXT, w_help_get, w_help_set },
};
const u32 api2_win_nprops = ARRAY_SIZE(api2_win_props);

/* ---- DRAW */

static void format_of(const struct kwindow *g, struct glos_format *f)
{
    f->bpp = g->bpp;
    f->rpos = g->rpos, f->rsize = g->rsize, f->gpos = g->gpos, f->gsize = g->gsize, f->bpos = g->bpos, f->bsize = g->bsize;
    f->pad[0] = f->pad[1] = 0;
}

static int pixels(struct app *a, struct trapframe *tf)
{
    struct glos_pixels q;
    struct kwindow g;
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
    struct krect r;
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
    struct kwindow g;
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

static int blit(struct app *a, struct trapframe *tf)
{
    struct glos_blit q;
    struct desk_blit b;
    u32 size, id;
    int r;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || q.format.bpp > 32)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(id = win_id(a, q.handle)))
        return not_window(a, tf, q.handle);
    b.ds = (u16)tf->ds;
    b.src = q.pixels, b.pitch = q.pitch;
    b.fmt.bpp = (u8)q.format.bpp;
    b.fmt.rpos = q.format.rpos, b.fmt.rsize = q.format.rsize, b.fmt.gpos = q.format.gpos, b.fmt.gsize = q.format.gsize;
    b.fmt.bpos = q.format.bpos, b.fmt.bsize = q.format.bsize;
    b.w = q.w, b.h = q.h, b.x = q.x, b.y = q.y;
    b.palette = q.palette;
    r = desk_win_blit(a, id, &b);
    return r ? a2_fail(tf, (u32)r) : a2_ok(tf, 0);
}

/* The Matrox card for an app with a HAL of its own, as API 1.x had it (kernel/gfx/gpu.c). */
static int gpu(struct app *a, struct trapframe *tf)
{
    struct glos_gpu g;
    u32 size;
    int r;
    if (a2_rd(tf, &g, sizeof g, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if ((r = gpu_app_open(a, &g)) != 0)
        return a2_fail(tf, (u32)r);
    return a2_wr(tf, &g, sizeof g, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

static int vram(struct app *a, struct trapframe *tf)
{
    struct glos_vram q;
    u32 size, off, bytes;
    int r;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || q.op > 1)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (q.op == 1) {
        if (mga_app_block(app_id(a), q.offset, 1, &off, &bytes) && off == q.offset)
            desk_app_vram_gone(a, off, bytes);  /* (no window may show it after) */
        return (r = gpu_vram_free(a, q.offset)) != 0 ? a2_fail(tf, (u32)r) : a2_ok(tf, 0);
    }
    if ((r = gpu_vram_alloc(a, q.least, q.most, q.align, &off, &bytes)) != 0)
        return a2_fail(tf, (u32)r);
    q.offset = off, q.bytes = bytes;
    if (a2_wr(tf, &q, sizeof q, size) != 0) {
        gpu_vram_free(a, off);
        return a2_fail(tf, GLOS_E_ARGUMENT);
    }
    return a2_ok(tf, 0);
}

static int winvram(struct app *a, struct trapframe *tf)
{
    struct glos_winvram q;
    u32 size, id;
    int r;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(id = win_id(a, q.handle)))
        return not_window(a, tf, q.handle);
    return (r = desk_win_vram(a, id, q.offset, q.pitch)) != 0 ? a2_fail(tf, (u32)r) : a2_ok(tf, 0);
}

/* MODES: the screen modes the card has (the display lists them once, by the VM thread: this may wait for it). */
static int modes(struct trapframe *tf)
{
    static struct display_mode list[DISPLAY_MODES];     /* (under the app's lock, and filled in one piece) */
    struct glos_modes q;
    u32 size, i, give;
    int n;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if ((n = display_modes(list, DISPLAY_MODES)) < 0)
        return a2_fail(tf, GLOS_E_REFUSED);     /* (the system VM is busy: again later) */
    q.count = (u32)n;
    give = q.room < (u32)n ? q.room : (u32)n;
    for (i = 0; i < give; i++) {
        struct glos_mode m = { list[i].w, list[i].h, list[i].bits };
        if (user_wr((u16)tf->ds, q.buffer + i * sizeof m, &m, sizeof m) != 0)
            return a2_fail(tf, GLOS_E_ARGUMENT);
    }
    if (a2_wr(tf, &q, sizeof q, size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    return q.room < (u32)n ? a2_fail(tf, GLOS_E_ROOM) : a2_ok(tf, 0);
}

int api2_draw(struct app *a, struct trapframe *tf, u32 n)
{
    switch (n) {
    case GLOS_DRAW_PIXELS & 0xFF: return pixels(a, tf);
    case GLOS_DRAW_PRESENT & 0xFF: return present(a, tf);
    case GLOS_DRAW_SCREEN & 0xFF: return api2_screen(tf);
    case GLOS_DRAW_GPU & 0xFF: return gpu(a, tf);
    case GLOS_DRAW_GPU_UNLOCK & 0xFF:
        gpu_app_unlock(a);
        return a2_ok(tf, 0);
    case GLOS_DRAW_VRAM & 0xFF: return vram(a, tf);
    case GLOS_DRAW_WINVRAM & 0xFF: return winvram(a, tf);
    case GLOS_DRAW_BLIT & 0xFF: return blit(a, tf);
    case GLOS_DRAW_MODES & 0xFF: return modes(tf);
    }
    return a2_fail(tf, GLOS_E_FUNCTION);
}
