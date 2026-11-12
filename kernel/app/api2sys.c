/* API 2.0's SYS (include/glos/api.h; docs/v1/phase-b.md step 9): what GLOS knows of the system, for System
 * Monitor and Task Manager: STAT (figures, a kind at a time: structures, or the lines GLOS's own commands
 * print), PROGRAMS (apps and jobs, with who started each), LOG (GLOS's log from a place on, and an event when
 * it has more), KILL. */
#include <string.h>

#include "agent.h"
#include "api2int.h"
#include "cache.h"
#include "desk.h"
#include "display.h"
#include "dpmi.h"
#include "glos/bootinfo.h"
#include "gpu.h"
#include "io.h"
#include "kprintf.h"
#include "mixer.h"
#include "mm.h"
#include "nic.h"
#include "pci.h"
#include "sched.h"
#include "session.h"
#include "stat.h"
#include "timer.h"
#include "usb.h"
#include "vm.h"

#define TEXT_ROOM 4096u

static void put_str(char *to, u32 room, const char *from)
{
    u32 i;
    for (i = 0; from[i] && i < room - 1; i++)
        to[i] = from[i];
    to[i] = 0;
}

/* The structures or the text in buf (len bytes; each structure `each` bytes, 0 for text), as far as the app has
   room: whole structures only. */
static int give(struct trapframe *tf, struct glos_sys_stat *q, u32 size, const void *buf, u32 len, u32 each)
{
    u32 n = len <= q->room ? len : q->room;
    if (each)
        n -= n % each;
    q->length = len;
    q->count = each ? n / each : 0;
    if ((n && user_wr((u16)tf->ds, q->buffer, buf, n) != 0) || a2_wr(tf, q, sizeof *q, size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    return n < len ? a2_fail(tf, GLOS_E_ROOM) : a2_ok(tf, 0);
}

int api2_sys_stat(struct trapframe *tf)         /* (for any program: api2.c) */
{
    struct glos_sys_stat q;
    u32 size, n = 0, vram = 0, x;
    char *b;
    int r, i;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(b = kmalloc(TEXT_ROOM)))
        return a2_fail(tf, GLOS_E_MEMORY);
    switch (q.kind) {
    case GLOS_STAT_SYSTEM: {
        struct glos_sys_system *s = (struct glos_sys_system *)b;
        u32 now = timer_ticks();
        memset(s, 0, sizeof *s);
        s->size = sizeof *s;
        s->uptime_ms = a2_ms_of(now);
        s->idle_ms = a2_ms_of(sched_idle_ticks());
        s->busy_ms = s->uptime_ms > s->idle_ms ? s->uptime_ms - s->idle_ms : 0;
        s->cpu_family = vm.bi->cpu_family;
        s->threads = sched_threads();
        s->apps = app_count();
        s->windows = desk_windows(0);
        net_counts(s->ip, &s->net_rx, &s->net_tx, s->nic, sizeof s->nic);
        display_counts(&s->screen_w, &s->screen_h, &s->screen_bpp, &s->accel, &vram);
        mixer_counts(&s->mixer_rate, &s->streams, &s->underruns);
        s->agent = (vm.bi->flags & BI_F_AGENT) != 0;
        put_str(s->hostname, sizeof s->hostname, vm.bi->hostname[0] ? vm.bi->hostname : "glos");
        put_str(s->card, sizeof s->card, s->accel ? "Matrox" : s->screen_w ? "VESA" : "");
        if (session_active())
            put_str(s->session, sizeof s->session, session_prog());
        r = give(tf, &q, size, b, sizeof *s, sizeof *s);
        break;
    }
    case GLOS_STAT_MEMORY: {
        struct glos_sys_memory *m = (struct glos_sys_memory *)b;
        memset(m, 0, sizeof *m);
        m->size = sizeof *m;
        m->total_kb = pmm_total_frames() * 4;
        m->free_kb = mm_avail_frames() * 4;
        m->kernel_kb = kernel_bytes() / 1024;
        m->heap_kb = kheap_bytes() / 1024;
        m->heap_max_kb = HEAP_MAX / 1024;
        m->cache_kb = cache_kb();
        display_counts(&x, &x, &x, &x, &m->vram_kb);
        r = give(tf, &q, size, b, sizeof *m, sizeof *m);
        break;
    }
    case GLOS_STAT_PCI: {
        struct glos_sys_pci *p = (struct glos_sys_pci *)b;
        const struct pci_dev *d;
        for (i = 0; (d = pci_at(i)) != 0 && (u32)(i + 1) * sizeof *p <= TEXT_ROOM; i++, p++) {
            memset(p, 0, sizeof *p);
            p->size = sizeof *p;
            p->bus = d->bus, p->dev = d->dev, p->fn = d->fn, p->irq = d->irq;
            p->vendor = d->vendor, p->device = d->device;
            p->class_code = d->class, p->subclass = d->subclass, p->progif = d->progif;
            p->keeper = d->keeper;              /* (GLOS_KEEPER_ are pci.h's DEV_) */
        }
        r = give(tf, &q, size, b, (u32)i * sizeof *p, sizeof *p);
        break;
    }
    case GLOS_STAT_NET:
        n = net_stat(b, TEXT_ROOM, 0);
        r = give(tf, &q, size, b, n, 0);
        break;
    case GLOS_STAT_AUDIO:
        n = mixer_stat(b, TEXT_ROOM);
        r = give(tf, &q, size, b, n, 0);
        break;
    case GLOS_STAT_USB:
        n = (u32)usb_stat(b, TEXT_ROOM);
        r = give(tf, &q, size, b, n, 0);
        break;
    case GLOS_STAT_DISPLAY:
        n = display_stat(b, TEXT_ROOM);
        n += gpu_stat(b + n, TEXT_ROOM - n);
        r = give(tf, &q, size, b, n, 0);
        break;
    case GLOS_STAT_CACHE:
        n = cache_stat(b, TEXT_ROOM);
        r = give(tf, &q, size, b, n, 0);
        break;
    case GLOS_STAT_THREADS:
        n = sched_stat(b, TEXT_ROOM);
        r = give(tf, &q, size, b, n, 0);
        break;
    default:
        r = a2_fail(tf, GLOS_E_ARGUMENT);
    }
    kfree(b);
    return r;
}

static int programs(struct trapframe *tf)
{
    struct glos_sys_stat q;
    struct kprog *old;                  /* (the records the kernel's lists give: stat.c) */
    struct glos_program *p;
    u32 size, n, i, k;
    char *b;
    int r;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(b = kmalloc(2 * TEXT_ROOM)))
        return a2_fail(tf, GLOS_E_MEMORY);
    old = (struct kprog *)(b + TEXT_ROOM);
    n = stat_apps(old, TEXT_ROOM) / sizeof *old;
    p = (struct glos_program *)b;
    for (i = k = 0; i < n && (k + 1) * sizeof *p <= TEXT_ROOM; i++) {
        if (old[i].kind > 1)
            continue;
        memset(p, 0, sizeof *p);
        p->size = sizeof *p;
        p->kind = old[i].kind;
        p->id = old[i].id;
        p->state = old[i].state;
        p->busy_ms = old[i].busy_ms;
        p->mem_kb = old[i].mem_kb;
        p->windows = old[i].windows;
        memcpy(p->name, old[i].name, sizeof p->name);
        if (p->kind == GLOS_PROG_APP) {
            p->job = app_began_as(p->id);
            p->by = p->job ? agent_job_by(p->job) : 0;
        } else {
            p->by = agent_job_by(p->id);
        }
        p++, k++;
    }
    r = give(tf, &q, size, b, k * sizeof *p, sizeof *p);
    kfree(b);
    return r;
}

static int kill_call(struct app *a, struct trapframe *tf)
{
    struct glos_sys_kill q;
    u32 size;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || q.what > GLOS_KILL_ASK)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (q.what == GLOS_KILL_ASK)                /* (anybody may ask) */
        return app_ask_end(q.id) == 0 ? a2_ok(tf, 0) : a2_fail(tf, GLOS_E_NOT_FOUND);
    if (!app_own(a))
        return a2_fail(tf, GLOS_E_REFUSED);     /* (ending others' programs is for GLOS's own: Task Manager) */
    if (q.what == GLOS_KILL_APP) {
        if (!app_alive(q.id))
            return a2_fail(tf, GLOS_E_NOT_FOUND);
        app_kill_id(q.id, "sys");
        return a2_ok(tf, 0);
    }
    if ((q.what == GLOS_KILL_JOB ? agent_kill_seq(q.id) : agent_kill()) != 0)
        return a2_fail(tf, GLOS_E_NOT_FOUND);
    return a2_ok(tf, 0);
}

/* LOG: for any program (GLOSLOG's: a program a batch file started can't be an app); following it is an app's. */
int api2_sys_log(struct app *a, struct trapframe *tf)
{
    struct glos_sys_log q;
    u32 size, pos, n, want;
    char *b;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || q.follow > 1)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (q.follow && !a)
        return a2_fail(tf, GLOS_E_NOT_APP);
    want = q.room < TEXT_ROOM ? q.room : TEXT_ROOM;
    if (!(b = kmalloc(TEXT_ROOM)))
        return a2_fail(tf, GLOS_E_MEMORY);
    pos = q.pos;
    if (klog_pos() - pos > 0x80000000u)         /* (a place that is not one of the log's: its end) */
        pos = klog_pos();
    n = klog_from(&pos, b, want);               /* (a place the log has gone past: from the oldest kept) */
    q.length = n;
    q.next = pos;
    if (a) {
        a->v2->log_follow = (u8)q.follow;
        a->v2->log_told = pos;                  /* (the event is for what comes after this) */
    }
    if ((n && user_wr((u16)tf->ds, q.buffer, b, n) != 0) || a2_wr(tf, &q, sizeof q, size) != 0) {
        kfree(b);
        return a2_fail(tf, GLOS_E_ARGUMENT);
    }
    kfree(b);
    return a2_ok(tf, 0);
}

/* Each tick, for an app that follows the log: told once when it has more than the app last read. */
void api2_sys_tick(struct app *a)
{
    struct api2 *s = a->v2;
    struct glos_ev ev;
    if (!s->log_follow || klog_pos() == s->log_told)
        return;
    memset(&ev, 0, sizeof ev);
    ev.size = sizeof ev;
    ev.kind = GLOS_EV_LOG;
    if (a2_post(a, &ev, 1) == 0)
        s->log_told = klog_pos();               /* (not again until LOG has been called and more has come) */
}

/* GLOS asks an app to end (glos exit; KILL's ASK): GLOS_EV_END. */
void api2_end_event(struct app *a)
{
    struct glos_ev ev;
    if (!a->v2)
        return;
    memset(&ev, 0, sizeof ev);
    ev.size = sizeof ev;
    ev.kind = GLOS_EV_END;
    a2_post(a, &ev, 1);
}

int api2_sys(struct app *a, struct trapframe *tf, u32 n)
{
    switch (GLOS_FN(GLOS_AREA_SYS, n)) {
    case GLOS_SYS_STAT:                         /* (answered before this, in api2_call) */
        return api2_sys_stat(tf);
    case GLOS_SYS_KILL:
        return kill_call(a, tf);
    case GLOS_SYS_LOG:
        return api2_sys_log(a, tf);
    case GLOS_SYS_PROGRAMS:
        return programs(tf);
    }
    return a2_fail(tf, GLOS_E_FUNCTION);
}
