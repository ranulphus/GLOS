/* API 2.0's THREAD (include/glos/api.h; docs/v1/phase-b.md step 4;
 * supervisor.md 12.7): an app's own threads, its locks and signals, and the
 * waits for them.
 *
 * The threads themselves are app.c's (a record each in the app, a kernel
 * thread in its context). A lock and a signal are tsync.c's state behind a
 * handle. Every wait here is a look, then a sleep on the app's queue that
 * any change wakes (app_thread_news), then a look again: a waiter keeps no
 * pointer over its sleep, only handles, since another thread may close what
 * it waits for meanwhile. A wait in the app's first thread is cut short by
 * an interrupt of the app's, and made again when its handler has returned
 * (as CORE's EVENT and SLEEP are). */
#include <string.h>

#include "api2int.h"
#include "dpmi.h"
#include "io.h"
#include "mm.h"
#include "timer.h"
#include "tsync.h"

struct lock2 {
    struct tlock l;
};

struct sig2 {
    struct tsignal s;
};

static u32 who(struct app *a) { return app_thr_number(a) + 1; }

/* A wait: ready() says 1 (what was waited for is there), 0 (not yet) or a negative GLOS_E_. The call's answer:
   1 ready, 0 the time is up (or the app is ending), 2 again after a handler of the app's, or a negative GLOS_E_. */
static int wait_for(struct app *a, struct trapframe *tf, u32 ms, int (*ready)(struct app *a, void *arg), void *arg)
{
    u32 until;
    int r;
    ms = a2_wait_ms(tf, ms);
    until = ms == GLOS_FOREVER ? 0 : timer_ticks() + a2_ticks_of(ms);
    if (ms != GLOS_FOREVER && !until)
        until = 1;
    for (;;) {
        if ((r = ready(a, arg)) != 0)
            return r;
        if (!ms)
            return 0;
        switch (app_thread_wait(a, until)) {
        case 1:
            ms = 0;                             /* (it has waited that long: one more look) */
            break;
        case 2:
            return 0;
        case 3:
            tf->ecx = ms == GLOS_FOREVER ? GLOS_FOREVER : a2_ms_of(until - timer_ticks());
            tf->edx = A2_RESUME;
            return 2;
        }
    }
}

/* ---- threads */

static int create(struct app *a, struct trapframe *tf)
{
    struct glos_thread q;
    struct app_thr *r = 0;
    u32 size, i, ret[2];
    int e;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || !q.entry || !q.stack)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (user_rd((u16)tf->ss, sp_of(tf), ret, 8) != 0)   /* (the call's far return address: the caller's code segment) */
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (q.data) {                               /* its DS, ES and SS: a writable data segment of the app's, 32-bit, */
        u32 hi = cpu_desc_hi(q.data);           /* at the caller's base */
        if (q.data > 0xFFFF || !ldt_valid((u16)q.data) || (hi & 0xFA00) != 0xF200 || !(hi & 0x400000)
            || sel_base((u16)q.data) != sel_base((u16)tf->ds))
            return a2_fail(tf, GLOS_E_ARGUMENT);
    }
    for (i = 0; i < APP_THREADS && !r; i++)
        if (!a->thr[i].used)
            r = &a->thr[i];
    if (!r)
        return a2_fail(tf, GLOS_E_MEMORY);
    memset(r, 0, sizeof *r);
    if (!(r->handle = handle_new(&a->v2->h, GLOS_HK_THREAD, r)))
        return a2_fail(tf, GLOS_E_MEMORY);
    r->used = 1;
    q.handle = r->handle;
    q.number = (u32)(r - a->thr) + 1;
    if (a2_wr(tf, &q, sizeof q, size) != 0)     /* (before it runs: it may look at what its maker was given) */
        e = -2;
    else
        e = app_thread_start(a, r, tf, ret[1] & 0xFFFF, q.data, q.entry, q.arg, q.stack,
                             (q.flags & GLOS_THREAD_BACKGROUND) != 0);
    if (e) {
        handle_close(&a->v2->h, r->handle);
        r->used = 0;
        return a2_fail(tf, e == -2 ? GLOS_E_ARGUMENT : e == -1 ? GLOS_E_MEMORY : GLOS_E_REFUSED);
    }
    return a2_ok(tf, 0);
}

static int exit_call(struct app *a, struct trapframe *tf)
{
    struct glos_thread_exit q;
    u32 size;
    if (app_first())
        return a2_fail(tf, GLOS_E_REFUSED);     /* (the first thread's end is the program's) */
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        q.code = 0;
    app_thread_exit(a, q.code);
}

static int join_ready(struct app *a, void *arg)
{
    struct app_thr *r = handle_obj(&a->v2->h, *(u32 *)arg, GLOS_HK_THREAD);
    if (!r)
        return -GLOS_E_HANDLE;
    return !r->live;
}

static int join(struct app *a, struct trapframe *tf)
{
    struct glos_thread_join q;
    struct app_thr *r;
    u32 size;
    int e;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    r = handle_obj(&a->v2->h, q.handle, GLOS_HK_THREAD);
    if (r && r->live && r->t == current)
        return a2_fail(tf, GLOS_E_REFUSED);     /* (itself) */
    if ((e = wait_for(a, tf, q.ms, join_ready, &q.handle)) == 2)
        return 2;
    if (e < 0)
        return a2_fail(tf, (u32)-e);
    r = handle_obj(&a->v2->h, q.handle, GLOS_HK_THREAD);
    q.ended = e == 1;
    q.code = e == 1 && r ? r->code : 0;
    return a2_wr(tf, &q, sizeof q, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

/* ---- locks */

static int lock_new(struct app *a, struct trapframe *tf)
{
    struct glos_lock q;
    struct lock2 *l;
    u32 size;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(l = kmalloc(sizeof *l)))
        return a2_fail(tf, GLOS_E_MEMORY);
    memset(l, 0, sizeof *l);
    if (!(q.handle = handle_new(&a->v2->h, GLOS_HK_LOCK, l))) {
        kfree(l);
        return a2_fail(tf, GLOS_E_MEMORY);
    }
    if (a2_wr(tf, &q, sizeof q, size) != 0) {
        kfree(handle_close(&a->v2->h, q.handle));
        return a2_fail(tf, GLOS_E_ARGUMENT);
    }
    return a2_ok(tf, 0);
}

static int take_ready(struct app *a, void *arg)
{
    struct lock2 *l = handle_obj(&a->v2->h, *(u32 *)arg, GLOS_HK_LOCK);
    if (!l)
        return -GLOS_E_HANDLE;
    return tlock_take(&l->l, who(a));
}

static int take(struct app *a, struct trapframe *tf)
{
    struct glos_take q;
    u32 size;
    int e;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if ((e = wait_for(a, tf, q.ms, take_ready, &q.handle)) == 2)
        return 2;
    if (e < 0)
        return a2_fail(tf, (u32)-e);
    q.taken = (u32)e;
    if (a2_wr(tf, &q, sizeof q, size) != 0) {
        struct lock2 *l = handle_obj(&a->v2->h, q.handle, GLOS_HK_LOCK);
        if (e && l && tlock_give(&l->l, who(a)) == 1)
            app_thread_news(a);
        return a2_fail(tf, GLOS_E_ARGUMENT);
    }
    return a2_ok(tf, 0);
}

static int give(struct app *a, struct trapframe *tf)
{
    struct glos_give q;
    struct lock2 *l;
    u32 size;
    int e;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(l = handle_obj(&a->v2->h, q.handle, GLOS_HK_LOCK)))
        return a2_fail(tf, GLOS_E_HANDLE);
    if ((e = tlock_give(&l->l, who(a))) < 0)
        return a2_fail(tf, GLOS_E_REFUSED);
    if (e)
        app_thread_news(a);
    return a2_ok(tf, 0);
}

/* ---- signals */

static int signal_new(struct app *a, struct trapframe *tf)
{
    struct glos_signal q;
    struct sig2 *s;
    u32 size;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(s = kmalloc(sizeof *s)))
        return a2_fail(tf, GLOS_E_MEMORY);
    memset(s, 0, sizeof *s);
    s->s.one = (q.flags & GLOS_SIGNAL_ONE) != 0;
    if (!(q.handle = handle_new(&a->v2->h, GLOS_HK_SIGNAL, s))) {
        kfree(s);
        return a2_fail(tf, GLOS_E_MEMORY);
    }
    if (a2_wr(tf, &q, sizeof q, size) != 0) {
        kfree(handle_close(&a->v2->h, q.handle));
        return a2_fail(tf, GLOS_E_ARGUMENT);
    }
    return a2_ok(tf, 0);
}

static int signal_set(struct app *a, struct trapframe *tf)
{
    struct glos_signal_set q;
    struct sig2 *s;
    u32 size;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(s = handle_obj(&a->v2->h, q.handle, GLOS_HK_SIGNAL)))
        return a2_fail(tf, GLOS_E_HANDLE);
    tsignal_set(&s->s, q.set != 0);
    if (q.set)
        app_thread_news(a);
    return a2_ok(tf, 0);
}

/* ---- WAIT: the first of several handles that is ready */

struct any {
    const struct glos_wait_any *q;
    int which;
};

static int any_ready(struct app *a, void *arg)
{
    struct any *w = arg;
    u32 i;
    for (i = 0; i < w->q->count; i++) {
        u32 h = w->q->handles[i];
        int kind = handle_kind(&a->v2->h, h), ok = 0;
        void *obj = kind ? handle_obj(&a->v2->h, h, (u8)kind) : 0;
        if (kind == GLOS_HK_THREAD) {
            ok = !((struct app_thr *)obj)->live;
        } else if (kind == GLOS_HK_SIGNAL) {
            ok = tsignal_take(&((struct sig2 *)obj)->s);
        } else if (kind == GLOS_HK_TIMER) {
            struct timer2 *t = obj;
            ok = t->fired;
            t->fired = 0;
        } else {
            return kind ? -GLOS_E_KIND : -GLOS_E_HANDLE;
        }
        if (ok) {
            w->which = (int)i;
            return 1;
        }
    }
    return 0;
}

static int wait_any(struct app *a, struct trapframe *tf)
{
    struct glos_wait_any q;
    struct any w;
    u32 size;
    int e;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || !q.count || q.count > GLOS_WAIT_MAX)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    w.q = &q;
    w.which = -1;
    if ((e = wait_for(a, tf, q.ms, any_ready, &w)) == 2)
        return 2;
    if (e < 0)
        return a2_fail(tf, (u32)-e);
    q.which = e == 1 ? w.which : -1;
    return a2_wr(tf, &q, sizeof q, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

/* ---- the handles' ends */

void api2_thread_close(struct app *a, int kind, void *obj)
{
    if (kind == GLOS_HK_THREAD) {               /* (it runs on: its record is free once it has ended) */
        struct app_thr *r = obj;
        r->handle = 0;
        if (!r->live)
            r->used = 0;
    } else {
        kfree(obj);
    }
    app_thread_news(a);                         /* (whoever waited for it: the handle is no more) */
}

void api2_thread_gone(struct app *a, u32 number)
{
    u32 i = 0, h;
    void *obj;
    while (handle_next(&a->v2->h, &i, &h, &obj) == 0)
        if (GLOS_HANDLE_KIND(h) == GLOS_HK_LOCK)
            tlock_drop(&((struct lock2 *)obj)->l, number + 1);
}

/* ---- properties */

#define NOPE (void)a; (void)x; (void)l
static int th_number(struct app *a, void *o, s32 *v, char *x, u32 *l) { NOPE; *v = (s32)((struct app_thr *)o - a->thr) + 1; return 0; }
static int th_ended(struct app *a, void *o, s32 *v, char *x, u32 *l) { NOPE; *v = !((struct app_thr *)o)->live; return 0; }
static int th_code(struct app *a, void *o, s32 *v, char *x, u32 *l) { NOPE; *v = (s32)((struct app_thr *)o)->code; return 0; }
static int th_flags(struct app *a, void *o, s32 *v, char *x, u32 *l) { NOPE; *v = ((struct app_thr *)o)->bulk ? GLOS_THREAD_BACKGROUND : 0; return 0; }
static int th_ms(struct app *a, void *o, s32 *v, char *x, u32 *l)
{
    struct app_thr *r = o;
    NOPE;
    *v = (s32)a2_ms_of(r->live ? r->t->ticks : r->ticks);
    return 0;
}
static int lk_held(struct app *a, void *o, s32 *v, char *x, u32 *l) { NOPE; *v = (s32)((struct lock2 *)o)->l.owner; return 0; }
static int sg_set(struct app *a, void *o, s32 *v, char *x, u32 *l) { NOPE; *v = ((struct sig2 *)o)->s.set; return 0; }

const struct propdef api2_thr_props[] = {
    { GLOS_PROP_THREAD_NUMBER, P_NUM | P_RO, th_number, 0 },
    { GLOS_PROP_THREAD_ENDED, P_NUM | P_RO, th_ended, 0 },
    { GLOS_PROP_THREAD_CODE, P_NUM | P_RO, th_code, 0 },
    { GLOS_PROP_THREAD_FLAGS, P_NUM | P_RO, th_flags, 0 },
    { GLOS_PROP_THREAD_MS, P_NUM | P_RO, th_ms, 0 },
    { GLOS_PROP_LOCK_HELD, P_NUM | P_RO, lk_held, 0 },
    { GLOS_PROP_SIGNAL_SET, P_NUM | P_RO, sg_set, 0 },
};
const u32 api2_thr_nprops = ARRAY_SIZE(api2_thr_props);

/* ---- the calls */

int api2_thread(struct app *a, struct trapframe *tf, u32 n)
{
    switch (n) {
    case GLOS_THREAD_CREATE & 0xFF:
        return create(a, tf);
    case GLOS_THREAD_EXIT & 0xFF:
        return exit_call(a, tf);
    case GLOS_THREAD_JOIN & 0xFF:
        return join(a, tf);
    case GLOS_THREAD_LOCK & 0xFF:
        return lock_new(a, tf);
    case GLOS_THREAD_TAKE & 0xFF:
        return take(a, tf);
    case GLOS_THREAD_GIVE & 0xFF:
        return give(a, tf);
    case GLOS_THREAD_SIGNAL & 0xFF:
        return signal_new(a, tf);
    case GLOS_THREAD_SET & 0xFF:
        return signal_set(a, tf);
    case GLOS_THREAD_WAIT & 0xFF:
        return wait_any(a, tf);
    case GLOS_THREAD_SELF & 0xFF:
        return a2_ok(tf, app_thr_number(a));
    }
    return a2_fail(tf, GLOS_E_FUNCTION);
}
