/* API 2.0's calls (include/glos/api2.h; docs/v1/phase-b.md, step 1: CORE).
 * glos_api (api.c) hands over every call whose number has major 2. An app is
 * 1.x's or 2.0's by the major version it gave INIT (app.c's api_major), and
 * makes that API's calls only.
 *
 * An app's 2.0 state (struct api2): its handles (handle.c), its timers, and
 * its events: a ring of whole events, each with its size first, so that an
 * EVENT that filters by area can take one from the middle (the ones queued
 * before it move up). The app's thread and the kernel's tick (interrupts
 * off) share it, so changes are made with interrupts off. */
#include "api2int.h"
#include "clock.h"
#include "display.h"
#include "dpmi.h"
#include "gpu.h"
#include "io.h"
#include "kprintf.h"
#include "mgadisp.h"
#include "mixer.h"
#include "mm.h"
#include "nic.h"
#include "randombytes.h"
#include "sched.h"
#include "timer.h"
#include "vm.h"

#ifndef GLOS_BUILD
#define GLOS_BUILD "unknown"
#endif

#define RESUME  0x52455355u             /* EDX: a wait cut short by an interrupt of the app's, ECX its ms left */

/* ---- helpers */

int a2_fail(struct trapframe *tf, u32 e)
{
    tf->eax = e;
    tf->eflags |= FL_CF;
    return 0;
}

int a2_ok(struct trapframe *tf, u32 v)
{
    tf->eax = v;
    return 0;
}

static u32 ticks_of(u32 ms)             /* milliseconds as ticks, 1024 to the second (as api.c) */
{
    if (ms > 0x03FFFFFFu)
        ms = 0x03FFFFFFu;
    return ms + (ms * 24 + 999) / 1000;
}

static u32 ms_of(u32 ticks) { return (u32)(((unsigned long long)ticks * 125 + 127) >> 7); }
static u32 now_ms(void) { return (u32)(((unsigned long long)timer_ticks() * 125) >> 7); }

/* The call's structure at DS:EBX: what GLOS knows of it read (n bytes at most), the rest zero; *size the
   caller's size. -1 if there is none, or its size can't even hold itself. */
int a2_rd(struct trapframe *tf, void *s, u32 n, u32 *size)
{
    u32 sz;
    memset(s, 0, n);
    if (!tf->ebx || user_rd((u16)tf->ds, tf->ebx, &sz, 4) != 0 || sz < 4)
        return -1;
    if (user_rd((u16)tf->ds, tf->ebx, s, sz < n ? sz : n) != 0)
        return -1;
    *size = sz;
    return 0;
}

/* ... and back: no more than the caller's size. */
int a2_wr(struct trapframe *tf, const void *s, u32 n, u32 size)
{
    return user_wr((u16)tf->ds, tf->ebx, s, size < n ? size : n);
}

/* ---- events */

static void qput(struct api2 *s, u32 at, const void *src, u32 n)
{
    const u8 *p = src;
    u32 i;
    for (i = 0; i < n; i++)
        s->q[(at + i) % A2_QBYTES] = p[i];
}

static void qget(const struct api2 *s, u32 at, void *dst, u32 n)
{
    u8 *p = dst;
    u32 i;
    for (i = 0; i < n; i++)
        p[i] = s->q[(at + i) % A2_QBYTES];
}

/* An event for the app (any thread, or the tick): its time set here. 0, or -1 if the queue is full. */
int a2_post(struct app *a, struct glos_ev *ev, int merge)
{
    struct api2 *s = a->v2;
    u32 f = irq_save();
    int r = -1;
    ev->time = now_ms();
    if (merge && s->last_merge && s->qt != s->qh) {     /* (a movement after a movement: the newer one) */
        struct glos_ev last;
        qget(s, s->last_at, &last, sizeof last);
        if (last.kind == ev->kind && last.handle == ev->handle && last.size == ev->size) {
            qput(s, s->last_at, ev, ev->size);
            irq_restore(f);
            return 0;
        }
    }
    if (ev->size >= sizeof *ev && ev->size <= GLOS_EV_MAX && s->qt - s->qh + ev->size <= A2_QBYTES) {
        if (s->qt == s->qh)
            a->ev_since = timer_ticks();        /* (waiting from now: app_stalled) */
        qput(s, s->qt, ev, ev->size);
        s->last_at = s->qt;
        s->last_merge = merge != 0;
        s->qt += ev->size;
        a->wake++;
        thread_wake(&a->wq);
        r = 0;
    } else {
        s->ndrop++;
    }
    irq_restore(f);
    return r;
}

/* The first queued event of the areas asked for (0: any) into ev (GLOS_EV_MAX bytes), taken from the queue if
   it fits room: 1 taken, 0 none, -1 too large for room (*need its size; it stays). */
static int take(struct app *a, u32 areas, void *ev, u32 room, u32 *need)
{
    struct api2 *s = a->v2;
    u32 f = irq_save(), at = s->qh, size = 0, i;
    u16 kind = 0;
    int r = 0;
    while (at != s->qt) {
        qget(s, at, &size, 4);
        qget(s, at + 4, &kind, 2);
        if (!areas || (areas >> GLOS_EV_AREA(kind) & 1))
            break;
        at += size;
    }
    if (at != s->qt) {
        if (size > room) {
            *need = size;
            r = -1;
        } else {
            qget(s, at, ev, size);
            a->ev_taken = 1;
            a->ev_since = timer_ticks();
            if (at == s->last_at)
                s->last_merge = 0;              /* (the one put last is gone: nothing takes its place) */
            for (i = at; i != s->qh; i--)       /* (those before it move up into its place) */
                s->q[(i - 1 + size) % A2_QBYTES] = s->q[(i - 1) % A2_QBYTES];
            s->qh += size;
            *need = size;
            r = 1;
        }
    }
    irq_restore(f);
    return r;
}

int api2_pending(const struct app *a) { return a->v2 && a->v2->qt != a->v2->qh; }

/* ---- timers */

static void timer_drop(struct api2 *s, struct timer2 *t)
{
    u32 f = irq_save(), i;
    for (i = 0; i < s->ntimers; i++)
        if (s->timers[i] == t) {
            s->timers[i] = s->timers[--s->ntimers];
            break;
        }
    irq_restore(f);
}

void api2_tick(struct app *a)
{
    struct api2 *s = a->v2;
    u32 now = timer_ticks(), i;
    for (i = 0; i < s->ntimers; i++) {
        struct timer2 *t = s->timers[i];
        if (!t->live || (s32)(now - t->next) < 0)
            continue;
        t->count++;
        if (t->pending) {
            t->missed++;                        /* (the one queued stands for this one too) */
        } else {
            struct glos_ev_timer ev;
            memset(&ev, 0, sizeof ev);
            ev.ev.size = sizeof ev;
            ev.ev.kind = GLOS_EV_TIMER;
            ev.ev.handle = t->handle;
            ev.count = t->count;
            ev.missed = t->missed;
            if (a2_post(a, &ev.ev, 0) == 0) {
                t->pending = 1;
                t->missed = 0;
            }
        }
        if (!t->repeat)
            t->live = 0;
        else if ((s32)(now - t->next) > (s32)t->period)
            t->next = now + t->period;          /* (far behind: from now, not a burst to catch up) */
        else
            t->next += t->period;
    }
}

/* ---- properties: by number, for each kind of handle */

static int t_ms_get(struct app *a, void *o, s32 *v, char *x, u32 *l) { (void)a; (void)x; (void)l; *v = (s32)((struct timer2 *)o)->ms; return 0; }
static int t_ms_set(struct app *a, void *o, s32 v, const char *x, u32 l)
{
    struct timer2 *t = o;
    u32 f;
    (void)a; (void)x; (void)l;
    if (v < 1)
        return GLOS_E_ARGUMENT;
    f = irq_save();
    t->ms = (u32)v;
    t->period = ticks_of((u32)v);
    t->next = timer_ticks() + t->period;
    irq_restore(f);
    return 0;
}
static int t_rep_get(struct app *a, void *o, s32 *v, char *x, u32 *l) { (void)a; (void)x; (void)l; *v = ((struct timer2 *)o)->repeat; return 0; }
static int t_rep_set(struct app *a, void *o, s32 v, const char *x, u32 l)
{
    struct timer2 *t = o;
    (void)a; (void)x; (void)l;
    t->repeat = v != 0;
    return 0;
}
static int t_count_get(struct app *a, void *o, s32 *v, char *x, u32 *l) { (void)a; (void)x; (void)l; *v = (s32)((struct timer2 *)o)->count; return 0; }
static int t_name_get(struct app *a, void *o, s32 *v, char *x, u32 *l)
{
    struct timer2 *t = o;
    (void)a; (void)v;
    *l = (u32)strlen(t->name);
    memcpy(x, t->name, *l);
    return 0;
}
static int t_name_set(struct app *a, void *o, s32 v, const char *x, u32 l)
{
    struct timer2 *t = o;
    (void)a; (void)v;
    if (l >= sizeof t->name)
        return GLOS_E_ARGUMENT;
    memcpy(t->name, x, l);
    t->name[l] = 0;
    return 0;
}

static const struct propdef props[] = {
    { GLOS_PROP_TIMER_MS, P_NUM, t_ms_get, t_ms_set },
    { GLOS_PROP_TIMER_REPEAT, P_NUM, t_rep_get, t_rep_set },
    { GLOS_PROP_TIMER_COUNT, P_NUM | P_RO, t_count_get, 0 },
    { GLOS_PROP_TIMER_NAME, P_TEXT, t_name_get, t_name_set },
};

static int prop_call(struct app *a, struct trapframe *tf, int set)
{
    struct glos_prop p;
    const struct propdef *d = 0;
    char text[1040];                            /* (on the app's own kernel stack, 16 KB: a setter may wait for the
                                                   desktop with it in use) */
    u32 size, i, len = 0;
    int kind, e;
    void *obj;
    if (a2_rd(tf, &p, sizeof p, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    kind = handle_kind(&a->v2->h, p.handle);
    if (!kind)
        return a2_fail(tf, GLOS_E_HANDLE);
    for (i = 0; i < ARRAY_SIZE(props); i++)
        if (props[i].prop == p.prop)
            d = &props[i];
    for (i = 0; i < api2_win_nprops; i++)
        if (api2_win_props[i].prop == p.prop)
            d = &api2_win_props[i];
    if (!d || (p.prop >> 8) != (u32)kind || (set && (d->flags & P_RO)))
        return a2_fail(tf, (p.prop >> 8) != (u32)kind && d ? GLOS_E_KIND : GLOS_E_PROPERTY);
    obj = handle_obj(&a->v2->h, p.handle, (u8)kind);
    if (set) {
        if (d->flags & (P_TEXT | P_DATA)) {
            if (p.room >= sizeof text || (p.room && user_rd((u16)tf->ds, p.data, text, p.room) != 0))
                return a2_fail(tf, GLOS_E_ARGUMENT);
            len = p.room;
        }
        e = d->set(a, obj, p.value, text, len);
        return e ? a2_fail(tf, (u32)e) : a2_ok(tf, 0);
    }
    e = d->get(a, obj, &p.value, text, &len);
    if (e)
        return a2_fail(tf, (u32)e);
    if (d->flags & P_DATA) {                    /* bytes as they are: all of them, or as many as there is room for */
        p.length = len;
        if (p.room && len && user_wr((u16)tf->ds, p.data, text, len <= p.room ? len : p.room) != 0)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        if (a2_wr(tf, &p, sizeof p, size) != 0)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        return p.room < len ? a2_fail(tf, GLOS_E_ROOM) : a2_ok(tf, 0);
    }
    if (d->flags & P_TEXT) {
        u32 n = len + 1 <= p.room ? len + 1 : p.room;     /* (its NUL too, where there is room for it) */
        text[len] = 0;
        p.length = len;
        if (n && user_wr((u16)tf->ds, p.data, text, n) != 0)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        if (a2_wr(tf, &p, sizeof p, size) != 0)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        return p.room <= len ? a2_fail(tf, GLOS_E_ROOM) : a2_ok(tf, 0);
    }
    return a2_wr(tf, &p, sizeof p, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

/* ---- the app's state */

int api2_app_new(struct app *a)
{
    struct api2 *s = kmalloc(sizeof *s);
    if (!s)
        return -1;
    memset(s, 0, sizeof *s);
    htab_init(&s->h);
    a->v2 = s;
    return 0;
}

void api2_app_gone(struct app *a)
{
    struct api2 *s = a->v2;
    u32 i = 0, h;
    void *obj;
    if (!s)
        return;
    s->ntimers = 0;                             /* (the tick looks at these no more) */
    while (handle_next(&s->h, &i, &h, &obj) == 0) {
        handle_close(&s->h, h);
        if (GLOS_HANDLE_KIND(h) == GLOS_HK_TIMER)
            kfree(obj);                         /* (a window's is its number: the desktop took the windows away) */
    }
    if (s->ndrop || s->ndrop1x)
        kprintf("GLOS-API2 app=%u drops=%u unmapped=%u\n", a->id, s->ndrop, s->ndrop1x);
    a->v2 = 0;
    kfree(s);
}

/* ---- the calls */

static u32 machine(void)                        /* (as api.c's caps(): the desktop's, even under a session) */
{
    u32 bpp = (u32)display_desktop_bpp();
    u32 m = (mixer_on() || mixer_found() ? GLOS_MACHINE_AUDIO : 0u) | (net_up() ? GLOS_MACHINE_NET : 0u);
    if (bpp && mga_desktop_card())
        m |= GLOS_MACHINE_GPU | (bpp == 16 ? GLOS_MACHINE_GLWIN : 0u);
    return m;
}

static int version(struct trapframe *tf)
{
    struct glos_version v;
    u32 size;
    if (!tf->ebx)
        return a2_ok(tf, (GLOS_MAJOR << 16) | GLOS_MINOR);
    if (a2_rd(tf, &v, sizeof v, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    v.major = GLOS_MAJOR;
    v.minor = GLOS_MINOR;
    v.machine = machine();
    memset(v.build, 0, sizeof v.build);
    memcpy(v.build, GLOS_BUILD, strlen(GLOS_BUILD) < sizeof v.build ? strlen(GLOS_BUILD) : sizeof v.build - 1);
    memset(v.functions, 0, sizeof v.functions);
    v.functions[GLOS_AREA_CORE][0] = (1u << 13) - 1;    /* VERSION to RANDOM */
    v.functions[GLOS_AREA_WIN][0] = 0x5F;               /* CREATE, SHOW, PLACE, FOCUS, ENUM, DRAG */
    v.functions[GLOS_AREA_DRAW][0] = 0x1FF;             /* PIXELS, PRESENT, SCREEN, the GPU's five, BLIT */
    return a2_wr(tf, &v, sizeof v, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, (GLOS_MAJOR << 16) | GLOS_MINOR);
}

static int init(struct trapframe *tf)
{
    struct glos_init in;
    struct app *a;
    u32 size, id;
    int r;
    if (a2_rd(tf, &in, sizeof in, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (in.major != GLOS_MAJOR)
        return a2_fail(tf, GLOS_E_VERSION);
    if ((r = app_init(tf, GLOS_MAJOR, in.minor, &id)) != 0) {
        kprintf("GLOS-APP refuse api=2 psp=%04x why=%d\n", dctx ? dctx->psp : 0, r);
        in.why = (u32)r;
        a2_wr(tf, &in, sizeof in, size);
        return a2_fail(tf, GLOS_E_INIT);
    }
    a = app_pending();                          /* (its name, Help page and Start menu group, for later steps) */
    if (a && a->v2) {
        in.name[sizeof in.name - 1] = in.help[sizeof in.help - 1] = in.category[sizeof in.category - 1] = 0;
        memcpy(a->v2->name, in.name, sizeof in.name);
        memcpy(a->v2->help, in.help, sizeof in.help);
        memcpy(a->v2->category, in.category, sizeof in.category);
    }
    in.app = id;
    in.why = 0;
    a2_wr(tf, &in, sizeof in, size);
    tf->eax = 0;
    return 1;                                   /* (the program is an app from its return: api.c, app.c) */
}

static int time_call(struct trapframe *tf)
{
    struct glos_time t;
    u32 size;
    if (a2_rd(tf, &t, sizeof t, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    t.utc = clock_utc();
    t.how = (u32)clock_how();
    t.offset = (int)clock_offset();
    return a2_wr(tf, &t, sizeof t, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

static int random_call(struct trapframe *tf)
{
    struct glos_random q;
    u8 buf[256];
    u32 size;
    int r;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || q.length > sizeof buf)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    randombytes(buf, q.length);
    r = q.length && user_wr((u16)tf->ds, q.buffer, buf, q.length) != 0;
    memset(buf, 0, sizeof buf);
    return r ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

/* A wait's milliseconds: the structure's, or, made again after an interrupt of the app's cut it short, what
   was left then (ECX, with EDX the mark). */
static u32 wait_ms(struct trapframe *tf, u32 ms)
{
    if (tf->edx == RESUME) {
        tf->edx = 0;
        return tf->ecx;
    }
    return ms;
}

static int event_call(struct app *a, struct trapframe *tf)
{
    struct glos_wait w;
    u8 ev[GLOS_EV_MAX];
    u32 size, need = 0, ms, left;
    int r;
    if (a2_rd(tf, &w, sizeof w, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    ms = wait_ms(tf, w.ms);
    for (;;) {
        r = take(a, w.areas, ev, w.room, &need);
        if (r)
            break;
        if (!ms) {
            w.length = 0;
            return a2_wr(tf, &w, sizeof w, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
        }
        left = app_sleep(a, ms == GLOS_FOREVER ? 0x7FFFFFFFu : ticks_of(ms));
        if (a->v2->qt != a->v2->qh)
            continue;
        if (!left) {
            ms = 0;                             /* (it has waited that long: one more look) */
            continue;
        }
        if (a->kill)
            return a2_ok(tf, 0);
        tf->ecx = ms == GLOS_FOREVER ? GLOS_FOREVER : ms_of(left);     /* an interrupt of its own: again after */
        tf->edx = RESUME;                                               /* its handler, for the rest */
        return 2;
    }
    w.length = need;
    if (r < 0) {
        a2_wr(tf, &w, sizeof w, size);
        return a2_fail(tf, GLOS_E_ROOM);
    }
    if (((struct glos_ev *)ev)->kind == GLOS_EV_TIMER) {          /* its timer may queue another now */
        struct timer2 *t = handle_obj(&a->v2->h, ((struct glos_ev *)ev)->handle, GLOS_HK_TIMER);
        if (t)
            t->pending = 0;
    }
    if (user_wr((u16)tf->ds, w.buffer, ev, need) != 0 || a2_wr(tf, &w, sizeof w, size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    return a2_ok(tf, 1);
}

static int sleep_call(struct app *a, struct trapframe *tf)
{
    struct glos_sleep q;
    u32 size, ms, left;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    ms = wait_ms(tf, q.ms);
    left = app_sleep(a, ticks_of(ms));
    if (left && !a->kill) {
        tf->ecx = ms_of(left);
        tf->edx = RESUME;
        return 2;
    }
    q.left = 0;
    return a2_wr(tf, &q, sizeof q, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

static int timer_call(struct app *a, struct trapframe *tf)
{
    struct api2 *s = a->v2;
    struct glos_timer q;
    struct timer2 *t;
    u32 size, f;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || !q.ms)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (s->ntimers >= A2_TIMERS || !(t = kmalloc(sizeof *t)))
        return a2_fail(tf, GLOS_E_MEMORY);
    memset(t, 0, sizeof *t);
    if (!(t->handle = handle_new(&s->h, GLOS_HK_TIMER, t))) {
        kfree(t);
        return a2_fail(tf, GLOS_E_MEMORY);
    }
    t->ms = q.ms;
    t->period = ticks_of(q.ms);
    t->repeat = q.repeat != 0;
    f = irq_save();
    t->next = timer_ticks() + t->period;
    t->live = 1;
    s->timers[s->ntimers++] = t;
    irq_restore(f);
    q.handle = t->handle;
    return a2_wr(tf, &q, sizeof q, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

static int close_call(struct app *a, struct trapframe *tf)
{
    struct api2 *s = a->v2;
    struct glos_close q;
    u32 size;
    void *obj;
    int kind;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    kind = handle_kind(&s->h, q.handle);
    if (!kind)
        return a2_fail(tf, GLOS_E_HANDLE);
    if (kind == GLOS_HK_TIMER)
        timer_drop(s, handle_obj(&s->h, q.handle, GLOS_HK_TIMER));
    {
        u32 f = irq_save();                     /* (the desktop's thread looks windows' handles up for its events) */
        obj = handle_close(&s->h, q.handle);
        irq_restore(f);
    }
    if (kind == GLOS_HK_TIMER)
        kfree(obj);
    else if (kind == GLOS_HK_WINDOW)
        api2_win_close(a, (u32)obj);
    return a2_ok(tf, 0);
}

static int post_call(struct app *a, struct trapframe *tf)
{
    struct glos_post q;
    struct glos_ev_post ev;
    u32 size;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    memset(&ev, 0, sizeof ev);
    ev.ev.size = sizeof ev;
    ev.ev.kind = GLOS_EV_POST;
    ev.code = q.code;
    ev.a = q.a;
    ev.b = q.b;
    ev.c = q.c;
    return a2_post(a, &ev.ev, 0) != 0 ? a2_fail(tf, GLOS_E_MEMORY) : a2_ok(tf, 0);
}

static int core(struct app *a, struct trapframe *tf, u32 n)
{
    switch (n) {
    case GLOS_CORE_CLOSE & 0xFF:
        return close_call(a, tf);
    case GLOS_CORE_GET & 0xFF:
        return prop_call(a, tf, 0);
    case GLOS_CORE_SET & 0xFF:
        return prop_call(a, tf, 1);
    case GLOS_CORE_EVENT & 0xFF:
        return event_call(a, tf);
    case GLOS_CORE_POST & 0xFF:
        return post_call(a, tf);
    case GLOS_CORE_TIMER & 0xFF:
        return timer_call(a, tf);
    case GLOS_CORE_SLEEP & 0xFF:
        return sleep_call(a, tf);
    case GLOS_CORE_YIELD & 0xFF:
        thread_yield();
        return a2_ok(tf, 0);
    }
    return a2_fail(tf, GLOS_E_FUNCTION);
}

int api2_call(struct trapframe *tf)
{
    struct app *a = app_cur();
    u32 fn = tf->eax, area = GLOS_FN_AREA(fn), n = fn & 0xFFu;
    int r;
    if (fn == GLOS_CORE_VERSION)                /* (for any program, as the others below) */
        return version(tf);
    if (fn == GLOS_CORE_INIT)
        return init(tf);
    if (fn == GLOS_CORE_TICKS)
        return a2_ok(tf, now_ms());
    if (fn == GLOS_CORE_TIME)
        return time_call(tf);
    if (fn == GLOS_CORE_RANDOM)
        return random_call(tf);
    if (fn == GLOS_DRAW_SCREEN)
        return api2_screen(tf);
    if (!a)
        return a2_fail(tf, GLOS_E_NOT_APP);
    if (a->api_major != 2 || !a->v2)
        return a2_fail(tf, GLOS_E_VERSION);       /* (a 1.x app makes 1.x's calls only) */
    if (area > GLOS_AREA_DRAW)
        return a2_fail(tf, GLOS_E_FUNCTION);       /* (the other areas come with phase B's later steps) */
    if (fn == GLOS_DRAW_GPU_LOCK)               /* (not a safe point: the lock is being taken, not left) */
        return gpu_app_lock(a) == 0 ? a2_ok(tf, 0) : a2_fail(tf, GLOS_E_NO_GPU);
    gpu_safe_begin(a);                          /* (as api.c: the app's turn with the engine may end in a wait) */
    r = area == GLOS_AREA_WIN ? api2_win(a, tf, n) : area == GLOS_AREA_DRAW ? api2_draw(a, tf, n) : core(a, tf, n);
    gpu_safe_end(a);
    return r;
}
