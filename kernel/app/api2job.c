/* API 2.0's JOB (include/glos/api.h; docs/v1/phase-b.md step 9): programs run for an app by the agent
 * (kernel/dos/agent.c), each a handle. RUN starts one; READ takes what it has written to its standard output or
 * error (kept by the agent as DOS's console calls bring it); WRITE is its standard input (answered by the agent
 * in place of DOS's console input calls) or, for a job in the app's window, its keys; SIGNAL is Ctrl+C or
 * Ctrl+Break as from the keyboard, or its end; STATUS says how it is doing. The app is told by GLOS_EV_JOB when
 * the job moves on and when it writes where it had written nothing; no call here waits. */
#include <string.h>

#include "agent.h"
#include "api2int.h"
#include "dpmi.h"
#include "glos/bootinfo.h"
#include "io.h"
#include "vm.h"

static u32 seq_of(struct app *a, struct trapframe *tf, u32 h, int *fail)
{
    void *obj = handle_obj(&a->v2->h, h, GLOS_HK_JOB);
    if (!obj) {
        *fail = a2_fail(tf, handle_kind(&a->v2->h, h) ? GLOS_E_KIND : GLOS_E_HANDLE);
        return 0;
    }
    return (u32)obj;
}

static int run(struct app *a, struct trapframe *tf)
{
    static struct krun r;                   /* (what the agent takes; no wait while it is used) */
    static char cmd[1024];                      /* (a command line may be longer than that structure's) */
    struct glos_job_run q;
    struct agent_opts o;
    char profile[16], file[3][80];
    u32 size, seq, i, f, off[3];
    int e, detach;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || q.how > GLOS_RUN_APP
        || (q.flags & ~(u32)(GLOS_RUNF_DETACH | GLOS_RUNF_STDIN | GLOS_RUNF_DIRECT | GLOS_RUNF_APPEND_OUT | GLOS_RUNF_APPEND_ERR
                               | GLOS_RUNF_DEBUG))
        || ((q.flags & GLOS_RUNF_STDIN) && q.in))
        return a2_fail(tf, GLOS_E_ARGUMENT);
    detach = (q.flags & GLOS_RUNF_DETACH) != 0;
    if ((q.flags & GLOS_RUNF_STDIN) && (detach || q.how != GLOS_RUN_HIDDEN))
        return a2_fail(tf, GLOS_E_ARGUMENT);    /* (a window's job is typed to; a session's has the keyboard and stops
                                                   the app; nobody writes to nobody's) */
    memset(&r, 0, sizeof r);
    memset(&o, 0, sizeof o);
    r.size = sizeof r;
    if (a2_text_in(tf, q.command, cmd, sizeof cmd) <= 0 || (q.dir && a2_text_in(tf, q.dir, r.dir, sizeof r.dir) < 0)
        || (q.profile && a2_text_in(tf, q.profile, profile, sizeof profile) <= 0))
        return a2_fail(tf, GLOS_E_ARGUMENT);
    for (i = 0; q.env && i < sizeof r.env - 1; i++) {   /* its strings, to the NUL after the last one's */
        if (user_rd((u16)tf->ds, q.env + i, &r.env[i], 1) != 0)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        if (!r.env[i] && (!i || !r.env[i - 1]))
            break;
    }
    if (q.env && i == sizeof r.env - 1)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(vm.bi->flags & BI_F_AGENT))           /* (GLOS /RUN: there is nobody to run it) */
        return a2_fail(tf, GLOS_E_REFUSED);
    r.flags = (detach ? KRUN_DETACH : 0u) | (q.how <= GLOS_RUN_FULL ? KRUN_CONSOLE : 0u)
            | (q.how == GLOS_RUN_FULL ? KRUN_FULL : 0u);
    o.hidden = q.how == GLOS_RUN_HIDDEN;        /* (a console job without a window: the desktop stays as it is) */
    o.direct = (q.flags & GLOS_RUNF_DIRECT) != 0;
    o.exclusive = q.how == GLOS_RUN_EXCLUSIVE ? 1 : q.how == GLOS_RUN_APP ? 2 : 0;
    o.stdin_pipe = (q.flags & GLOS_RUNF_STDIN) != 0;
    o.debug = (q.flags & GLOS_RUNF_DEBUG) != 0 && !detach;
    o.keep = (u8)!detach;
    o.command = cmd;
    o.profile = q.profile ? profile : 0;
    off[0] = q.in, off[1] = q.out, off[2] = q.err;
    for (i = 0; i < 3; i++) {                   /* (in DOS's bytes: the stub opens them) */
        if (off[i] && api2_fs_path(tf, off[i], file[i], sizeof file[i]) <= 0)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        o.file[i] = off[i] ? file[i] : 0;
    }
    o.append = (u8)((q.flags & GLOS_RUNF_APPEND_OUT ? 2 : 0) | (q.flags & GLOS_RUNF_APPEND_ERR ? 4 : 0));
    if ((e = agent_post_job(detach ? 0 : a, &r, &o, &seq)) != 0)
        return a2_fail(tf, (u32)e);
    q.id = seq;
    q.handle = 0;
    if (!detach) {
        f = irq_save();                         /* (the desktop's thread looks handles up for its events) */
        q.handle = handle_new(&a->v2->h, GLOS_HK_JOB, (void *)seq);
        irq_restore(f);
    }
    if ((!detach && !q.handle) || a2_wr(tf, &q, sizeof q, size) != 0) {
        if (q.handle) {
            f = irq_save();
            handle_close(&a->v2->h, q.handle);
            irq_restore(f);
        }
        if (!detach) {                          /* (it is not to run for nobody) */
            agent_app_kill(a, seq);
            agent_app_forget(a, seq);
        }
        return a2_fail(tf, !detach && !q.handle ? GLOS_E_MEMORY : GLOS_E_ARGUMENT);
    }
    return a2_ok(tf, 0);
}

static int io(struct app *a, struct trapframe *tf, int write)
{
    struct glos_job_io q;
    u8 buf[512];
    u16 keys[64];
    u32 size, seq, done = 0, i;
    int n, fail = 0;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(seq = seq_of(a, tf, q.handle, &fail)))
        return fail;
    if (!write && q.stream == GLOS_JOB_KEYS) {  /* what was typed to it and it ended without reading */
        if ((n = agent_app_keys(a, seq, keys, q.length / 2 < 64 ? q.length / 2 : 64)) < 0)
            return a2_fail(tf, GLOS_E_REFUSED);
        if (n && user_wr((u16)tf->ds, q.buffer, keys, (u32)n * 2) != 0)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        done = (u32)n * 2;
    } else if (!write) {
        if (q.stream != GLOS_JOB_OUT && q.stream != GLOS_JOB_ERR)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        while (done < q.length) {
            u32 want = q.length - done < sizeof buf ? q.length - done : sizeof buf;
            if ((n = agent_app_read1(a, seq, (int)q.stream, buf, want)) < 0)
                return a2_fail(tf, GLOS_E_HANDLE);
            if (n && user_wr((u16)tf->ds, q.buffer + done, buf, (u32)n) != 0)
                return a2_fail(tf, GLOS_E_ARGUMENT);
            done += (u32)n;
            if ((u32)n < want)
                break;
        }
    } else if (q.stream == GLOS_JOB_KEYS) {
        if (q.length & 1)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        for (i = 0; i < q.length / 2 && i < 64; i++)
            if (user_rd((u16)tf->ds, q.buffer + i * 2, &keys[i], 2) != 0)
                return a2_fail(tf, GLOS_E_ARGUMENT);
        if ((n = agent_app_type(a, seq, keys, i)) < 0)
            return a2_fail(tf, GLOS_E_REFUSED); /* (not a job in a window of the app's) */
        done = (u32)n * 2;
    } else if (q.stream == GLOS_JOB_IN) {
        if (!q.length && agent_app_write(a, seq, buf, 0, 1) < 0)
            return a2_fail(tf, GLOS_E_REFUSED);
        while (done < q.length) {
            u32 want = q.length - done < sizeof buf ? q.length - done : sizeof buf;
            if (user_rd((u16)tf->ds, q.buffer + done, buf, want) != 0)
                return a2_fail(tf, GLOS_E_ARGUMENT);
            if ((n = agent_app_write(a, seq, buf, want, 0)) < 0)
                return a2_fail(tf, GLOS_E_REFUSED);     /* (its input is not the app's: no GLOS_RUNF_STDIN) */
            done += (u32)n;
            if ((u32)n < want)
                break;                          /* (full: the rest when its program has read some) */
        }
    } else {
        return a2_fail(tf, GLOS_E_ARGUMENT);
    }
    q.done = done;
    return a2_wr(tf, &q, sizeof q, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

static int signal_call(struct app *a, struct trapframe *tf)
{
    struct glos_job_signal q;
    u32 size, seq;
    int r, fail = 0;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || q.what < GLOS_SIGNAL_INT || q.what > GLOS_SIGNAL_END)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(seq = seq_of(a, tf, q.handle, &fail)))
        return fail;
    r = q.what == GLOS_SIGNAL_END ? agent_app_kill(a, seq) : agent_app_signal(a, seq, q.what == GLOS_SIGNAL_BREAK);
    return r != 0 ? a2_fail(tf, GLOS_E_REFUSED) : a2_ok(tf, 0);    /* (it is not running: waiting, ended, or an app) */
}

static int status(struct app *a, struct trapframe *tf)
{
    struct glos_job_status q;
    u32 size, seq, ticks = 0;
    int fail = 0;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(seq = seq_of(a, tf, q.handle, &fail)))
        return fail;
    if (agent_app_status(a, seq, &q.state, &q.code, &q.out, &q.err, &ticks) != 0)
        return a2_fail(tf, GLOS_E_HANDLE);
    q.ms = a2_ms_of(ticks);
    q.id = seq;
    return a2_wr(tf, &q, sizeof q, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

int api2_job(struct app *a, struct trapframe *tf, u32 n)
{
    switch (GLOS_FN(GLOS_AREA_JOB, n)) {
    case GLOS_JOB_RUN:
        return run(a, tf);
    case GLOS_JOB_READ:
        return io(a, tf, 0);
    case GLOS_JOB_WRITE:
        return io(a, tf, 1);
    case GLOS_JOB_SIGNAL:
        return signal_call(a, tf);
    case GLOS_JOB_STATUS:
        return status(a, tf);
    }
    return a2_fail(tf, GLOS_E_FUNCTION);
}

/* CLOSE of a job's handle: it is nobody's from now. One still running runs on, what it writes dropped (SIGNAL's
   END first ends it). */
void api2_job_close(struct app *a, void *obj) { agent_app_forget(a, (u32)obj); }

/* KEV_JOB (the agent's tell and put): JOB's event, for the job's handle. */
void api2_job_event(struct app *a, u32 seq, u32 state, u32 code)
{
    struct glos_ev_job e;
    u32 i = 0, h, f = irq_save();
    void *obj;
    memset(&e, 0, sizeof e);
    while (a->v2 && handle_next(&a->v2->h, &i, &h, &obj) == 0)
        if (GLOS_HANDLE_KIND(h) == GLOS_HK_JOB && (u32)obj == seq)
            e.ev.handle = h;
    irq_restore(f);
    if (!e.ev.handle)
        return;
    e.ev.size = sizeof e;
    e.ev.kind = GLOS_EV_JOB;
    e.state = state;                            /* (the agent's states are GLOS_JS_) */
    e.code = code;
    a2_post(a, &e.ev, 0);
}
