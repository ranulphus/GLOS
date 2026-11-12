/* API 2.0's DEBUG (include/glos/api.h; docs/v1/phase-b.md step 12): a debugger app's channel to a program it
 * started, carrying gdb's remote protocol to and from GLOS's stub for programs (kernel/dbg/gdbprog.c, which
 * `glos gdb` reaches over SSH).
 *
 * One channel at a time, as the stub has one program at a time: its app, its job, and a ring of what the stub
 * has to say, which the stub's own calls fill (answers, while WRITE hands it a packet; a stop's reply, from READ
 * or from the SSH thread's turn) and READ empties. The stub tells of a stop from the program's own thread,
 * interrupts off: an event for the app, and its wait in READ woken. These calls are made without the app's lock
 * (api2.c): READ waits. */
#include <string.h>

#include "api2int.h"
#include "agent.h"
#include "dpmi.h"
#include "gdbprog.h"
#include "io.h"
#include "kprintf.h"
#include "timer.h"

#define RING 4096u

static struct {
    struct app *app;                            /* whose channel; 0: none */
    u32 job;                                    /* the job's number */
    u32 handle;
    u8 ring[RING];
    volatile u32 in, out;                       /* counts, never wrapped by hand */
} d;

static void dbg_send(const u8 *b, u32 n)        /* the stub's bytes (any thread) */
{
    u32 f = irq_save(), i;
    for (i = 0; i < n && d.in - d.out < RING; i++, d.in++)
        d.ring[d.in % RING] = b[i];
    if (d.app)
        thread_wake(&d.app->wq);
    irq_restore(f);
}

static void dbg_wake(void)                      /* the program stopped or ended (its thread; interrupts off) */
{
    struct glos_ev ev;
    if (!d.app)
        return;
    memset(&ev, 0, sizeof ev);
    ev.size = sizeof ev;
    ev.kind = GLOS_EV_DEBUG;
    ev.handle = d.handle;
    a2_post(d.app, &ev, 1);
    thread_wake(&d.app->wq);
}

static int attach(struct app *a, struct trapframe *tf)
{
    struct glos_debug_attach q;
    struct app *target;
    u32 size, seq, f;
    int r;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    f = irq_save();
    seq = (u32)handle_obj(&a->v2->h, q.job, GLOS_HK_JOB);
    if (!seq || agent_job_by(seq) != app_id(a)) {
        irq_restore(f);
        return a2_fail(tf, seq ? GLOS_E_REFUSED : GLOS_E_HANDLE);
    }
    if (d.app) {
        irq_restore(f);
        return a2_fail(tf, GLOS_E_REFUSED);     /* (one channel) */
    }
    target = app_of_job(seq);
    d.in = d.out = 0;
    d.job = seq;
    d.handle = 0;
    d.app = a;
    r = gdbp_attach_job(seq, target ? target->thread : 0, target, dbg_send, dbg_wake);
    if (r == 0 && !(d.handle = handle_new(&a->v2->h, GLOS_HK_DEBUG, (void *)seq))) {
        gdbp_detach();
        r = -2;
    }
    if (r != 0)
        d.app = 0;
    irq_restore(f);
    if (r != 0)
        return a2_fail(tf, r == -2 ? GLOS_E_MEMORY : GLOS_E_REFUSED);  /* (the stub has another program) */
    q.handle = d.handle;
    if (a2_wr(tf, &q, sizeof q, size) != 0) {
        f = irq_save();
        handle_close(&a->v2->h, q.handle);
        irq_restore(f);
        api2_debug_close(a, (void *)seq);
        return a2_fail(tf, GLOS_E_ARGUMENT);
    }
    kprintf("GLOS-DEBUG attach app=%u job=%u%s\n", app_id(a), seq, target ? " as-app" : "");
    return a2_ok(tf, 0);
}

void api2_debug_close(struct app *a, void *obj)
{
    u32 f = irq_save();
    int mine = d.app == a && d.job == (u32)obj;
    if (mine)
        d.app = 0;
    irq_restore(f);
    if (mine) {
        gdbp_detach();
        kprintf("GLOS-DEBUG detach app=%u job=%u\n", app_id(a), (u32)obj);
    }
}

static int io(struct app *a, struct trapframe *tf, int write)
{
    struct glos_debug_io q;
    u8 buf[256];
    u32 size, f, n, until;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    f = irq_save();
    n = d.app == a && d.handle == q.handle && handle_obj(&a->v2->h, q.handle, GLOS_HK_DEBUG);
    irq_restore(f);
    if (!n)
        return a2_fail(tf, GLOS_E_HANDLE);
    q.done = 0;
    if (write) {
        while (q.done < q.count) {              /* (the stub answers as it is fed: into the ring) */
            n = q.count - q.done < sizeof buf ? q.count - q.done : sizeof buf;
            if (user_rd((u16)tf->ds, q.buffer + q.done, buf, n) != 0)
                return a2_fail(tf, GLOS_E_ARGUMENT);
            gdbp_input(buf, n);
            q.done += n;
        }
    } else {
        until = q.ms == 0xFFFFFFFFu ? 0 : timer_ticks() + a2_ticks_of(q.ms);
        for (;;) {
            gdbp_turn();                        /* (a stop the stub hasn't told of yet) */
            f = irq_save();
            if (d.in != d.out || d.app != a || a->kill || !q.ms || (until && (s32)(timer_ticks() - until) >= 0)) {
                irq_restore(f);
                break;
            }
            thread_wait(&a->wq, until ? until : timer_ticks() + 1024);
            irq_restore(f);
        }
        while (q.done < q.count && d.in != d.out) {
            f = irq_save();
            for (n = 0; n < sizeof buf && q.done + n < q.count && d.in != d.out; n++, d.out++)
                buf[n] = d.ring[d.out % RING];
            irq_restore(f);
            if (user_wr((u16)tf->ds, q.buffer + q.done, buf, n) != 0)
                return a2_fail(tf, GLOS_E_ARGUMENT);
            q.done += n;
        }
    }
    return a2_wr(tf, &q, sizeof q, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

int api2_debug(struct app *a, struct trapframe *tf, u32 n)
{
    switch (GLOS_FN(GLOS_AREA_DEBUG, n)) {
    case GLOS_DEBUG_ATTACH:
        return attach(a, tf);
    case GLOS_DEBUG_READ:
        return io(a, tf, 0);
    case GLOS_DEBUG_WRITE:
        return io(a, tf, 1);
    }
    return a2_fail(tf, GLOS_E_FUNCTION);
}
