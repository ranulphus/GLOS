/* API 2.0's CLIP and DND (include/glos/api.h; docs/v1/phase-b.md step 6).
 *
 * CLIP: transactions on the kernel's clipboard (clip.c over clipstore.c). A writing transaction fills a content
 * of its own, which becomes the clipboard at its CLOSE; a reading one holds the content that was the clipboard
 * at its OPEN. Data is copied to and from the app a piece at a time, with interrupts let in between pieces: a
 * picture's megabytes are not one long stretch with the clock stopped.
 *
 * DND: one drag at a time (kernel/wm/dnd.c's states). Its data is a writing transaction's content, taken at
 * DRAG and never the clipboard's. The desktop's input says where the pointer is (api2_dnd_at) and when the drag
 * ends (api2_dnd_end), on the desktop's thread; what is over a target is found in the apps' own lists of
 * targets, and each note of the states is an event for one app. At a drop the target is given a reading
 * transaction on the drag's content.
 *
 * Who touches what: an app's handle table and its targets are touched by its own threads' calls and, here, by
 * the desktop's thread; no call waits while it has them half changed, and kernel work is not preempted, so each
 * sees them whole. */
#include <string.h>

#include "api2int.h"
#include "clip.h"
#include "clipstore.h"
#include "desk.h"
#include "dnd.h"
#include "dpmi.h"
#include "io.h"
#include "mm.h"
#include "sched.h"

#define PIECE     65536u                /* bytes copied between two looks at the interrupts */
#define DATA_MAX  0x1000000u            /* one format's data at most: 16 MB (the windows' region has 32) */

struct clip_tx {                        /* a transaction: a handle's object */
    struct clip_content *c;             /* its content (a reader's may be NULL: the clipboard was empty) */
    u8 write, puts, commit;
};

/* Interrupts in for a moment (a long copy), with nothing else given the processor. */
static void breathe(void)
{
    preempt_disable();
    sti();
    cli();
    preempt_enable();
}

static int copy_in(struct trapframe *tf, u32 off, u8 *dst, u32 n)
{
    u32 done, k;
    for (done = 0; done < n; done += k) {
        k = n - done < PIECE ? n - done : PIECE;
        if (user_rd((u16)tf->ds, off + done, dst + done, k) != 0)
            return -1;
        if (done + k < n)
            breathe();
    }
    return 0;
}

static int copy_out(struct trapframe *tf, u32 off, const u8 *src, u32 n)
{
    u32 done, k;
    for (done = 0; done < n; done += k) {
        k = n - done < PIECE ? n - done : PIECE;
        if (user_wr((u16)tf->ds, off + done, src + done, k) != 0)
            return -1;
        if (done + k < n)
            breathe();
    }
    return 0;
}

static int format_ok(u32 f) { return (f >= GLOS_CLIP_TEXT && f <= GLOS_CLIP_RICH) || f >= GLOS_CLIP_PRIVATE; }

/* ---- CLIP */

static struct clip_tx *tx_new(int write, struct clip_content *c)
{
    struct clip_tx *t = kmalloc(sizeof *t);
    if (!t)
        return 0;
    t->c = c;
    t->write = write != 0;
    t->puts = 0;
    t->commit = 1;
    return t;
}

static int open_call(struct app *a, struct trapframe *tf)
{
    struct glos_clip_open q;
    struct clip_content *c;
    struct clip_tx *t;
    u32 size;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || q.write > 1)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    c = q.write ? clip_content_new() : clip_content_now();
    if ((q.write && !c) || !(t = tx_new((int)q.write, c))) {
        clip_content_drop(c);
        return a2_fail(tf, GLOS_E_MEMORY);
    }
    q.serial = clip_serial();
    if (!(q.handle = handle_new(&a->v2->h, GLOS_HK_CLIP, t)) || a2_wr(tf, &q, sizeof q, size) != 0) {
        int full = !q.handle;
        if (q.handle)
            handle_close(&a->v2->h, q.handle);
        clip_content_drop(c);
        kfree(t);
        return a2_fail(tf, full ? GLOS_E_MEMORY : GLOS_E_ARGUMENT);
    }
    return a2_ok(tf, 0);
}

static int put(struct app *a, struct trapframe *tf)
{
    struct glos_clip_data q;
    struct clip_tx *t;
    u32 size;
    u8 *p;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || !format_ok(q.format))
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(t = handle_obj(&a->v2->h, q.handle, GLOS_HK_CLIP)))
        return a2_fail(tf, handle_kind(&a->v2->h, q.handle) ? GLOS_E_KIND : GLOS_E_HANDLE);
    if (!t->write)
        return a2_fail(tf, GLOS_E_REFUSED);
    if (q.length > DATA_MAX || !(p = clip_content_add(t->c, q.format, q.length)))
        return a2_fail(tf, GLOS_E_MEMORY);
    if (copy_in(tf, q.data, p, q.length) != 0) {
        clip_content_add(t->c, q.format, 0);    /* (nothing of it: not half of it) */
        return a2_fail(tf, GLOS_E_ARGUMENT);
    }
    t->puts = 1;
    return a2_ok(tf, 0);
}

static int get(struct app *a, struct trapframe *tf)
{
    struct glos_clip_data q;
    const struct clip_item *it;
    struct clip_content *c;
    struct clip_tx *t;
    u32 size, n;
    int r;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(t = handle_obj(&a->v2->h, q.handle, GLOS_HK_CLIP)))
        return a2_fail(tf, handle_kind(&a->v2->h, q.handle) ? GLOS_E_KIND : GLOS_E_HANDLE);
    if (!(it = clip_find(t->c, q.format)))
        return a2_fail(tf, GLOS_E_ARGUMENT);    /* (it has no such format: FORMATS says which it has) */
    q.total = it->len;
    n = q.offset >= it->len ? 0 : it->len - q.offset < q.length ? it->len - q.offset : q.length;
    c = clip_content_keep(t->c);                /* (another thread may close the transaction during the copy) */
    r = copy_out(tf, q.data, (const u8 *)it->data + q.offset, n);
    clip_content_drop(c);
    if (r != 0 || a2_wr(tf, &q, sizeof q, size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    return a2_ok(tf, n);
}

static int formats(struct app *a, struct trapframe *tf)
{
    struct glos_clip_formats q;
    struct clip_content *c;
    struct clip_tx *t = 0;
    u32 size, i;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (q.handle && !(t = handle_obj(&a->v2->h, q.handle, GLOS_HK_CLIP)))
        return a2_fail(tf, handle_kind(&a->v2->h, q.handle) ? GLOS_E_KIND : GLOS_E_HANDLE);
    c = t ? clip_content_keep(t->c) : clip_content_now();
    q.count = c ? c->n : 0;
    q.serial = c ? c->serial : 0;
    for (i = 0; i < GLOS_CLIP_ITEMS; i++) {
        q.format[i] = c && i < c->n ? c->item[i].format : 0;
        q.length[i] = c && i < c->n ? c->item[i].len : 0;
    }
    clip_content_drop(c);
    return a2_wr(tf, &q, sizeof q, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

static int format(struct trapframe *tf)
{
    struct glos_clip_format q;
    char name[CLIP_NAME_MAX];
    u32 size, i;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || !q.name)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    for (i = 0; i < sizeof name; i++) {
        if (user_rd((u16)tf->ds, q.name + i, &name[i], 1) != 0)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        if (!name[i])
            break;
    }
    if (i == sizeof name || !(q.format = clip_private(name)))
        return a2_fail(tf, i == sizeof name || !name[0] ? GLOS_E_ARGUMENT : GLOS_E_MEMORY);
    return a2_wr(tf, &q, sizeof q, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

/* CLOSE of a transaction's handle (commit: not when its app is ending). */
void api2_clip_close(struct app *a, void *obj, int commit)
{
    struct clip_tx *t = obj;
    (void)a;
    if (t->write && t->puts && t->commit && commit)
        clip_content_commit(t->c);              /* (its reference is the store's now) */
    else
        clip_content_drop(t->c);
    kfree(t);
}

int api2_clip(struct app *a, struct trapframe *tf, u32 n)
{
    switch (n) {
    case GLOS_CLIP_OPEN & 0xFF: return open_call(a, tf);
    case GLOS_CLIP_PUT & 0xFF: return put(a, tf);
    case GLOS_CLIP_GET & 0xFF: return get(a, tf);
    case GLOS_CLIP_FORMATS & 0xFF: return formats(a, tf);
    case GLOS_CLIP_FORMAT & 0xFF: return format(tf);
    }
    return a2_fail(tf, GLOS_E_FUNCTION);
}

static int tx_commit_get(struct app *a, void *o, s32 *v, char *x, u32 *l)
{
    (void)a; (void)x; (void)l;
    *v = ((struct clip_tx *)o)->commit;
    return 0;
}

static int tx_commit_set(struct app *a, void *o, s32 v, const char *x, u32 l)
{
    struct clip_tx *t = o;
    (void)a; (void)x; (void)l;
    if (!t->write)
        return GLOS_E_REFUSED;
    t->commit = v != 0;
    return 0;
}

const struct propdef api2_clip_props[] = {
    { GLOS_PROP_CLIP_COMMIT, P_NUM, tx_commit_get, tx_commit_set },
};
const u32 api2_clip_nprops = ARRAY_SIZE(api2_clip_props);

/* The clipboard changed (clip.c's hook: whoever changed it): every app is told. */
static void clip_tell(struct app *a, void *arg)
{
    struct glos_ev_clip ev;
    if (!a->v2)
        return;
    memset(&ev, 0, sizeof ev);
    ev.ev.size = sizeof ev;
    ev.ev.kind = GLOS_EV_CLIP;
    ev.serial = *(u32 *)arg;
    a2_post(a, &ev.ev, 0);
}

static void clip_changed(u32 serial) { app_each(clip_tell, &serial); }

void api2_clip_init(void) { clip_hook(clip_changed); }

/* ---- DND */

static struct dnd drag;                 /* the one drag */
static struct clip_content *drag_data;  /* ... its data ... */
static struct app *drag_src;            /* ... and its source */
static s32 drag_ox, drag_oy;            /* the target's client area's corner on the screen (its events' x and y) */

/* Which effects a window takes for what the drag offers: its app's TARGET for it, a format in common. */
static u32 takes(const struct app *a, u32 win)
{
    const struct api2 *s = a->v2;
    u32 i, k, f;
    for (i = 0; s && i < s->ntargets; i++)
        if (s->target[i].win == win)
            for (k = 0; k < s->target[i].n; k++)
                for (f = 0; drag_data && f < drag_data->n; f++)
                    if (s->target[i].format[k] == drag_data->item[f].format)
                        return s->target[i].effects;
    return 0;
}

struct desk_find {
    struct app *a;
};

static void desk_target(struct app *a, void *arg)      /* the app that takes the bare desktop's drops (the shell) */
{
    struct desk_find *d = arg;
    const struct api2 *s = a->v2;
    u32 i;
    for (i = 0; s && i < s->ntargets && !d->a; i++)
        if (s->target[i].win == 0)
            d->a = a;
}

struct find_app {
    u32 id;
    struct app *a;
};

static void by_id(struct app *a, void *arg)
{
    struct find_app *f = arg;
    if (app_id(a) == f->id)
        f->a = a;
}

static u32 win_handle(struct app *a, u32 id)
{
    u32 i = 0, h;
    void *obj;
    while (id && handle_next(&a->v2->h, &i, &h, &obj) == 0)
        if (GLOS_HANDLE_KIND(h) == GLOS_HK_WINDOW && (u32)obj == id)
            return h;
    return 0;
}

/* The states' notes, each an event for its app (still there, and a 2.0 app). */
static void tell(const struct dnd_note *o, u32 n)
{
    static const u16 kinds[] = { 0, GLOS_EV_DND_ENTER, GLOS_EV_DND_OVER, GLOS_EV_DND_LEAVE, GLOS_EV_DND_DROP,
                                 GLOS_EV_DND_DONE };
    u32 i, k;
    for (i = 0; i < n; i++) {
        struct find_app f = { o[i].app, 0 };
        struct glos_ev_dnd ev;
        app_each(by_id, &f);
        if (!f.a || !f.a->v2)
            continue;
        memset(&ev, 0, sizeof ev);
        ev.ev.size = sizeof ev;
        ev.ev.kind = kinds[o[i].kind];
        ev.ev.handle = win_handle(f.a, o[i].win);
        ev.effects = o[i].effect;
        if (o[i].kind == DND_DONE) {
            ev.x = o[i].x, ev.y = o[i].y;       /* (the source: on the screen) */
        } else {
            ev.x = o[i].x - drag_ox, ev.y = o[i].y - drag_oy;
        }
        if ((o[i].kind == DND_ENTER || o[i].kind == DND_DROP) && drag_data) {
            ev.count = drag_data->n;
            for (k = 0; k < drag_data->n; k++)
                ev.format[k] = drag_data->item[k].format;
        }
        if (o[i].kind == DND_DROP && drag_data) {       /* the data: a reading transaction of the target's */
            struct clip_tx *t = tx_new(0, clip_content_keep(drag_data));
            if (t && !(ev.data = handle_new(&f.a->v2->h, GLOS_HK_CLIP, t))) {
                clip_content_drop(t->c);
                kfree(t);
            } else if (!t) {
                clip_content_drop(drag_data);
            }
        }
        a2_post(f.a, &ev.ev, o[i].kind == DND_OVER);
    }
}

static void drag_over(void)             /* the drag has ended: its data is whoever's still holds it */
{
    clip_content_drop(drag_data);
    drag_data = 0;
    drag_src = 0;
}

void api2_dnd_at(struct app *owner, u32 win, s32 sx, s32 sy, s32 ox, s32 oy)
{
    struct dnd_note o[DND_NOTES];
    struct dnd_target t = { 0, 0, 0 };
    u32 f = irq_save(), n;
    if (!owner && !win) {               /* the bare desktop: the shell's, if it has asked for it */
        struct desk_find d = { 0 };
        app_each(desk_target, &d);
        owner = d.a;
    } else if (win == ~0u) {
        owner = 0;
    }
    if (owner && owner->v2) {
        t.app = app_id(owner);
        t.win = win;
        t.effects = takes(owner, win);
    }
    if (t.effects && (t.app != drag.t_app || t.win != drag.t_win)) {
        n = dnd_move(&drag, 0, sx, sy, o);      /* (the one it leaves: told with its own corner) */
        tell(o, n);
        drag_ox = ox, drag_oy = oy;
    } else if (t.effects) {
        drag_ox = ox, drag_oy = oy;
    }
    n = dnd_move(&drag, t.effects ? &t : 0, sx, sy, o);
    tell(o, n);
    irq_restore(f);
}

void api2_dnd_end(int drop)
{
    struct dnd_note o[DND_NOTES];
    u32 f = irq_save(), n = dnd_end(&drag, drop, o);
    tell(o, n);
    drag_over();
    irq_restore(f);
}

int api2_dnd_effect(void) { return drag.on ? (int)drag.answer : 0; }

/* An app, or one of its windows (its number; 0: the app), is no more. */
void api2_dnd_gone(struct app *a, u32 win)
{
    struct dnd_note o[DND_NOTES];
    struct api2 *s = a->v2;
    u32 f = irq_save(), n, i;
    int was = drag.on;
    if (win && s)                       /* (its targets go with a window; with the app, everything of its does) */
        for (i = 0; i < s->ntargets; i++)
            if (s->target[i].win == win)
                s->target[i--] = s->target[--s->ntargets];
    n = dnd_gone(&drag, app_id(a), win, o);
    tell(o, n);
    if (was && !drag.on)
        drag_over();
    irq_restore(f);
    if (was && !drag.on)
        desk_dnd_stop();                /* (the pointer is the desktop's again) */
}

static int drag_call(struct app *a, struct trapframe *tf)
{
    static u32 pixels[GLOS_DND_PICTURE * GLOS_DND_PICTURE];     /* (under the app's lock; one drag at a time) */
    struct glos_dnd q;
    struct clip_tx *t;
    u32 size, win;
    int have_pic, r = 0;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    t = handle_obj(&a->v2->h, q.data, GLOS_HK_CLIP);
    win = (u32)handle_obj(&a->v2->h, q.window, GLOS_HK_WINDOW);
    have_pic = q.picture && q.pw && q.ph;
    if (!t || !t->write || !t->puts || !win || !(q.effects & (GLOS_DND_COPY | GLOS_DND_MOVE | GLOS_DND_LINK))
        || q.effects & ~(u32)(GLOS_DND_COPY | GLOS_DND_MOVE | GLOS_DND_LINK)
        || (have_pic && (q.pw > GLOS_DND_PICTURE || q.ph > GLOS_DND_PICTURE
                         || user_rd((u16)tf->ds, q.picture, pixels, q.pw * q.ph * 4) != 0)))
        r = GLOS_E_ARGUMENT;
    else if (drag.on || dnd_begin(&drag, app_id(a), win, q.effects) != 0)
        r = GLOS_E_REFUSED;
    if (!r) {
        drag_data = clip_content_keep(t->c);
        drag_src = a;
        drag_ox = drag_oy = 0;
        if (desk_dnd_begin(a, win, have_pic ? pixels : 0, q.pw, q.ph) != 0) {   /* (no button is down there) */
            drag.on = 0;
            drag_over();
            r = GLOS_E_REFUSED;
        }
    }
    if (t) {                            /* its handle is closed here, done or refused: the data is the drag's */
        handle_close(&a->v2->h, q.data);
        api2_clip_close(a, t, 0);
    }
    return r ? a2_fail(tf, (u32)r) : a2_ok(tf, 0);
}

static int target_call(struct app *a, struct trapframe *tf)
{
    struct api2 *s = a->v2;
    struct glos_dnd_target q;
    u32 size, win = 0, i, k;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || q.effects & ~(u32)(GLOS_DND_COPY | GLOS_DND_MOVE | GLOS_DND_LINK)
        || (q.effects && (!q.count || q.count > GLOS_CLIP_ITEMS)))
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (q.window && !(win = (u32)handle_obj(&s->h, q.window, GLOS_HK_WINDOW)))
        return a2_fail(tf, handle_kind(&s->h, q.window) ? GLOS_E_KIND : GLOS_E_HANDLE);
    if (!q.window && q.effects && desk_shell_app() != a)
        return a2_fail(tf, GLOS_E_REFUSED);     /* (the bare desktop's drops are the shell's: SHELL's DESKTOP) */
    for (i = 0; i < s->ntargets && s->target[i].win != win; i++) ;
    if (!q.effects) {
        if (i < s->ntargets)
            s->target[i] = s->target[--s->ntargets];
        return a2_ok(tf, 0);
    }
    if (i == s->ntargets) {
        if (s->ntargets == A2_TARGETS)
            return a2_fail(tf, GLOS_E_MEMORY);
        s->ntargets++;
    }
    s->target[i].win = win;
    s->target[i].effects = q.effects;
    s->target[i].n = q.count;
    for (k = 0; k < q.count; k++)
        s->target[i].format[k] = q.format[k];
    return a2_ok(tf, 0);
}

static int answer_call(struct app *a, struct trapframe *tf)
{
    struct glos_dnd_answer q;
    u32 size, win = 0;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (q.window && !(win = (u32)handle_obj(&a->v2->h, q.window, GLOS_HK_WINDOW)))
        return a2_fail(tf, handle_kind(&a->v2->h, q.window) ? GLOS_E_KIND : GLOS_E_HANDLE);
    if (dnd_answer(&drag, app_id(a), win, q.effect) != 0)
        return a2_fail(tf, GLOS_E_REFUSED);
    desk_kick();                        /* (the pointer's shape says what a drop would do) */
    return a2_ok(tf, 0);
}

int api2_dnd(struct app *a, struct trapframe *tf, u32 n)
{
    switch (n) {
    case GLOS_DND_DRAG & 0xFF: return drag_call(a, tf);
    case GLOS_DND_TARGET & 0xFF: return target_call(a, tf);
    case GLOS_DND_ANSWER & 0xFF: return answer_call(a, tf);
    }
    return a2_fail(tf, GLOS_E_FUNCTION);
}
