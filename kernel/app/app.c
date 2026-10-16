/* GLOS apps (PRD §5.4, §6.8; docs/milestones-m6.md step A).
 *
 * An app is a DPMI client with a thread of its own. It starts as any DOS
 * program does: the resident stub EXECs it, its own stub switches to
 * protected mode, and it runs as a session on the VM thread. When it calls
 * the GLOS API's init (api.c) its context moves to a new thread, which goes
 * on from that call's return; the VM thread goes back to the stub, which
 * asks for its next job as if the program had ended. DOS still has the
 * program as a process, the stub's child.
 *
 * On its thread the app runs its protected-mode code, preempted like any
 * thread, and the DPMI host serves what needs no real mode there: memory,
 * descriptors, handlers, exceptions. What needs real mode needs the system
 * VM: every real-mode call the host would make for it (rm_call(): a DOS or
 * BIOS call reflected or through 0300h, a DOS memory block) is made by the
 * VM thread instead, when the stub asks for its next job: nothing else is
 * in DOS then. The app's thread waits. For the length of the call the VM
 * thread is the app's (it "adopts" it): the app's context is loaded, so its
 * real-mode callbacks and whatever they call run as they would in a
 * session, and DOS is given the app's state:
 *   - its PSP and DTA, in DOS's swappable data area;
 *   - its current drive and directory, set only when DOS has another;
 *   - the real-mode vectors it changed: the table is the system's while the
 *     app is out of the VM, compared with it when the app leaves and its
 *     changes put back when it returns;
 *   - a parent's stack frame for DOS to end it through. DOS ends a process
 *     by going back into its parent's last INT 21h frame, kept in the
 *     parent's PSP; the parent is the stub, which has made other calls
 *     since, so for the call that frame is a spare one on the stub's kill
 *     stack, and DOS goes on at the program's terminate address, which the
 *     mode switch pointed at the host (host.c terminated()).
 *
 * An app's end (INT 21h 4Ch, a kill, a crash) frees its context on its own
 * thread, which then has the VM thread make one last call for it, INT 21h
 * 4Ch as the program; an end inside a call the VM thread is making (in a
 * callback, in real mode, DOS's own abort) ends that call. Either way DOS
 * has ended the process when the VM thread sees the terminate address, and
 * the thread ends.
 *
 * While a session runs (a DOS command, a program that has yet to become an
 * app, an app's own child program) every app is paused at its next trap:
 * the session owns the machine (PRD §6.9), and calls into the VM wait for
 * the stub anyway.
 *
 * An app reaches no port (the TSS's I/O map is out of reach while its
 * thread runs): dev.c answers, with a timer and a PIC of the app's own. Its
 * interrupts are raised there and given to its protected-mode handlers
 * here, at a return to its code with its virtual interrupt flag on; a wait
 * in the kernel for it (its sleep, the BIOS's wait) ends early for one, and
 * the call is made again when the handler has returned.
 *
 * glos run --app makes an app of a program that never asks: at its first
 * return to its own protected-mode code where init would be allowed. */
#include <string.h>

#include "glos/api.h"
#include "glos/bootinfo.h"
#include "agent.h"
#include "app.h"
#include "appint.h"
#include "desk.h"
#include "dpmi.h"
#include "io.h"
#include "kprintf.h"
#include "mixer.h"
#include "mm.h"
#include "session.h"
#include "timer.h"
#include "vm.h"
#include "watch.h"

static struct app *apps, *pending;
static struct app *callq, *callq_tail;
static struct app *adopted;                     /* the VM thread is making a call for it ... */
static int base;                                /* ... which is the nested call at this depth ... */
static u8 ending;                               /* ... its last */
static u8 exclusive;                            /* ... or one that runs a program: the other apps wait */
static struct waitq pauseq;
static u32 next_id = 1, napps, last_code;
static struct cwd dos_has, sys_cwd;             /* DOS's drive and directory now; the system VM's own */
static struct {                                 /* the system VM's own, while the VM thread is an app's */
    u8 vif;
    void *job;
    u16 psp;
    u32 dta, ps_stack;
    u32 ivt[256];
} sys;

struct app *app_cur(void) { return current->user == TU_APP ? current->arg : 0; }

static void wr16(u32 lin, u32 v)
{
    vm_wr8(lin, (u8)v);
    vm_wr8(lin + 1, (u8)(v >> 8));
}

static void wr32(u32 lin, u32 v)
{
    wr16(lin, v);
    wr16(lin + 2, v >> 16);
}

static int mine(const struct app *a, u32 v) { return (a->ivt_mine[v >> 3] >> (v & 7)) & 1; }

/* ---- DOS's current drive and directory (the VM thread, as the system) */

static int dos21(struct rmregs *r)
{
    r->ss = vm.loader_cs;
    r->sp = (u16)vm.bi->kill_sp;
    r->flags = 2;
    return rm_call(0, r, RM_INT, 0x21, 0, 0);
}

/* (The stub's EXEC path is free between the stub's jobs: 80 bytes.) */
static u32 scratch(void) { return ((u32)vm.loader_cs << 4) + __builtin_offsetof(struct stub_data, path); }

static void cwd_read(struct cwd *c)
{
    struct rmregs r;
    u32 buf = scratch(), i;
    memset(&r, 0, sizeof r);
    r.eax = 0x1900;
    dos21(&r);
    c->drive = (u8)r.eax;
    memset(&r, 0, sizeof r);
    r.eax = 0x4700;
    r.ds = vm.loader_cs;
    r.esi = __builtin_offsetof(struct stub_data, path);
    vm_wr8(buf, 0);
    dos21(&r);
    for (i = 0; i < sizeof c->dir - 1 && vm_rd8(buf + i); i++)
        c->dir[i] = (char)vm_rd8(buf + i);
    c->dir[i] = 0;
    c->known = 1;
}

static void cwd_want(const struct cwd *w)
{
    struct rmregs r;
    u32 buf = scratch(), i;
    if (!dos_has.known)
        cwd_read(&dos_has);
    if (dos_has.drive == w->drive && !strcmp(dos_has.dir, w->dir))
        return;
    if (dos_has.drive != w->drive) {
        memset(&r, 0, sizeof r);
        r.eax = 0x0E00;
        r.edx = w->drive;
        dos21(&r);
    }
    vm_wr8(buf, '\\');
    for (i = 0; w->dir[i]; i++)
        vm_wr8(buf + 1 + i, (u8)w->dir[i]);
    vm_wr8(buf + 1 + i, 0);
    memset(&r, 0, sizeof r);
    r.eax = 0x3B00;
    r.ds = vm.loader_cs;
    r.edx = __builtin_offsetof(struct stub_data, path);
    if (dos21(&r) != 0 || (r.flags & FL_CF))
        kprintf("GLOS-APP warn=directory-gone drive=%c dir=\\%s\n", 'A' + w->drive, w->dir);
    dos_has = *w;
}

/* ---- becoming an app (the VM thread, in the program's trap) */

static void app_main(void *arg);

int app_init(struct trapframe *tf, u32 *id)
{
    struct dpmi_ctx *c = dctx;
    struct app *a;
    u16 psp = vm_current_psp();
    u32 i;
    (void)tf;
    if (app_cur() || c->app)
        return GLOS_INIT_ALREADY;
    if (c->nlv != 1 || c->npe || c->lstack_use || rm_nesting())
        return GLOS_INIT_NESTED;                /* a child program's, or from a handler or a callback */
    if (!c->bits32)
        return GLOS_INIT_16BIT;
    if (session_exclusive() || vm.direct)
        return GLOS_INIT_EXCLUSIVE;             /* glos run --exclusive or --direct, a direct-mode profile */
    if (!vm.sda || !vm.bi->detached_off || (vm.bi->flags & BI_F_SHELL) || !session_active() || psp != c->psp
        || vm_rd16(psp * 16u + 0x16) != vm.loader_psp)
        return GLOS_INIT_PARENT;                /* GLOS itself didn't start it (a program's child, a batch file's) */
    if (napps >= MAX_APPS || !(a = kmalloc(sizeof *a)))
        return GLOS_INIT_MEMORY;
    memset(a, 0, sizeof *a);
    a->id = next_id++;
    ksnprintf(a->tname, sizeof a->tname, "app%u", a->id);
    if (!(a->thread = thread_new(a->tname, PRIO_NORMAL, app_main, a, TU_APP))) {
        kfree(a);
        return GLOS_INIT_MEMORY;
    }
    a->ctx = c;
    a->psp = psp;
    for (i = 0; i < sizeof a->prog - 1 && session_prog()[i]; i++)
        a->prog[i] = session_prog()[i];
    pending = a;
    *id = a->id;
    return 0;
}

void app_detach(struct trapframe *tf)
{
    struct app *a = pending;
    struct dpmi_ctx *c = a->ctx;
    const u32 *sivt = session_ivt();
    u32 v, nvec = 0;

    pending = 0;
    c->app = a;
    a->start = *tf;
    a->thread->vif = current->vif;
    a->thread->ctx = c;
    fpu_fork(a->thread);
    current->ctx = 0;                           /* the VM thread has no client any more */
    dpmi_ctx_load(0);
    a->dta = vm_rd32(vm.sda + 0x0C);
    for (v = 0; v < 256; v++) {                 /* its vectors out of the table: as its session found it */
        u32 cur = vm_rd32(v * 4);
        if (cur != sivt[v]) {
            a->ivt[v] = cur;
            a->ivt_mine[v >> 3] |= (u8)(1u << (v & 7));
            wr32(v * 4, sivt[v]);
            nvec++;
        }
    }
    wr16(vm.sda + 0x10, vm.loader_psp);         /* DOS's current program: the stub again */
    wr32(vm.sda + 0x0C, (u32)vm.loader_psp << 16 | 0x80);
    cwd_read(&dos_has);                         /* where it is, it stays; so does the system VM */
    sys_cwd = a->cwd = dos_has;
    a->job = agent_vm_job();
    a->t0 = timer_ticks();
    appdev_init(a);
    a->next = apps;
    apps = a;
    napps++;
    dpmi_to_v86(tf, vm.loader_cs, vm.bi->detached_off, vm.loader_cs, vm.bi->kill_sp, vm.loader_cs, vm.loader_cs);
    current->vif = 0;
    kprintf("GLOS-APP start id=%u prog=%s psp=%04x thread=%s vectors=%u\n", a->id, a->prog, a->psp, a->tname, nvec);
    thread_go(a->thread);
}

/* The app's thread: its frame to the top of the stack, where a trap from
   its code would have left it, and out through the trap's way back. */
static void app_main(void *arg)
{
    struct app *a = arg;
    cli();
    current->esp0 = current->stack_top - 16;    /* a ring-3 frame's (arch/cpu.c trap_dispatch) */
    cpu_set_esp0(current->esp0);
    dpmi_return(&a->start);                     /* (its wait for the session it began in to end) */
    frame_start(&a->start, current->stack_top);
}

/* ---- on the app's thread */

static void app_thread_end(struct app *a) __attribute__((noreturn));
static void app_thread_end(struct app *a)
{
    struct app **pp;
    for (pp = &apps; *pp && *pp != a; pp = &(*pp)->next) ;
    if (*pp)
        *pp = a->next;
    napps--;
    fpu_drop(current);
    desk_app_gone(a);                           /* (its windows; none may point at it after this) */
    gpu_app_gone(a);                            /* (the engine, if it had it; its blocks of VRAM) */
    mixer_app_gone(a);                          /* (its streams) */
    agent_app_gone(a);                          /* (its jobs, M9: those in its console with it) */
    watch_app_gone(a);
    desk_shell(a, 0);
    current->user = TU_NONE;
    current->name = "app-gone";
    kfree(a);
    vm_kick();                                  /* (the stub may be waiting for the last app to go) */
    thread_exit();
}

/* The call waits for the VM thread, and the thread for the call. */
static int app_wait(struct app *a, int what)
{
    a->call = (u8)what;
    a->qnext = 0;
    if (callq_tail)
        callq_tail->qnext = a;
    else
        callq = a;
    callq_tail = a;
    vm_kick();
    while (a->call)
        thread_block(&a->wq);
    if (a->dead)
        app_thread_end(a);
    return a->result;
}

static int rm_call_app(struct app *a, struct trapframe *from, struct rmregs *r, int kind, u8 vec, const u16 *words,
                       u32 nwords);

int app_rm_call(struct trapframe *from, struct rmregs *r, int kind, u8 vec, const u16 *words, u32 nwords)
{
    struct app *a = app_cur();
    int res;
    if (!a)
        return -1;                              /* (a kernel thread: only the VM's and apps' make them) */
    gpu_safe_begin(a);                          /* (a DOS or BIOS call: its turn with the engine may end) */
    res = rm_call_app(a, from, r, kind, vec, words, nwords);
    gpu_safe_end(a);
    return res;
}

static int rm_call_app(struct app *a, struct trapframe *from, struct rmregs *r, int kind, u8 vec, const u16 *words,
                       u32 nwords)
{
    if (vec == 0x15 && kind != RM_FAR && ((r->eax >> 8) & 0xFF) == 0x86) {
        /* The BIOS's wait (DJGPP's delay()): a sleep on its own thread, not the system VM held for as long. An
           interrupt of its own ends it early: the handler runs, and the INT is made again for what is left. */
        u32 us = (r->ecx & 0xFFFF) << 16 | (r->edx & 0xFFFF), left;
        left = app_sleep(a, us / 977 + 1);
        if (left && from && (from->cs & 0xFFFF) != SEL_TRAMP) {
            us = left * 977;
            r->ecx = (r->ecx & ~0xFFFFu) | us >> 16;
            r->edx = (r->edx & ~0xFFFFu) | (us & 0xFFFF);
            from->eip -= 2;                     /* (INT 31h or INT 15h: two bytes either way) */
        }
        r->flags &= (u16)~FL_CF;
        return 0;
    }
    if (a->gui && vec == 0x16 && kind != RM_FAR && appdev_int16(a, from, r))
        return 0;                               /* an app with a window: its own keyboard (dev.c) */
    if (a->gui && vec == 0x33 && kind != RM_FAR && appdev_int33(a, r))
        return 0;                               /* ... and its own mouse */
    if (vec == 0x21 && kind != RM_FAR && ((r->eax >> 8) & 0xFF) == 0x4D && a->child_have) {
        a->child_have = 0;                      /* how its child ended: asked of DOS when the child did */
        r->eax = (r->eax & ~0xFFFFu) | a->child;
        r->flags &= (u16)~FL_CF;
        return 0;
    }
    a->c.from = from;
    a->c.r = r;
    a->c.kind = kind;
    a->c.vec = vec;
    a->c.words = words;
    a->c.nwords = nwords;
    return app_wait(a, AC_RM);
}

void app_end(struct app *a, u32 code)
{
    gpu_app_unlock(a);                          /* (its turn with the engine ends with it: nobody waits for an end) */
    a->ctx = 0;
    a->code = code;
    app_wait(a, AC_END);
    panic("app-end", NULL);
}

/* Apps wait: a session has the machine (session.c), or an app's own child
   program does. */
static int paused(void) { return exclusive || session_pauses(); }

int apps_paused(void) { return paused(); }

void app_return(struct app *a, struct trapframe *tf)
{
    for (;;) {
        u32 f = irq_save();                     /* (looked at and waited for in one piece: the session's end clears
                                                   it and wakes whoever waits, once) */
        int wait = !a->kill && paused() && !gpu_app_holds(a);  /* (with the engine: on to its next GLOS or DOS
                                                   call, where its turn ends: gpu.c) */
        if (wait)
            thread_block(&pauseq);              /* a session has the machine */
        irq_restore(f);
        if (a->kill)
            dpmi_end(tf, 0xFF, 1);              /* (doesn't come back) */
        if (!wait)
            break;
    }
    while (current->vif && vpic_pending(&a->pic)) {     /* its own interrupts: to its handlers */
        int vec = vpic_ack(&a->pic);
        if (vec < 0)
            break;
        if (dpmi_irq(tf, (u8)vec))
            break;                              /* (its virtual IF is off until the handler returns) */
        app_irq_default(a, (u8)vec);            /* nobody's: ended, as the BIOS would */
        if (vec == 0x08 && dpmi_irq(tf, 0x1C))  /* ... and the timer's goes on to INT 1Ch, as the BIOS's does */
            break;
    }
}

/* An interrupt of its own that could be given to it now. */
static int irq_due(const struct app *a) { return current->vif && vpic_pending(&a->pic); }

/* Sleeps `ticks`, or until something is there for it: an interrupt of its
   own that is due, an event, a key for INT 16h, its kill. The ticks left
   then (0: slept them all). */
u32 app_sleep(struct app *a, u32 ticks)
{
    u32 until = timer_ticks() + ticks, ev = a->evt, key = a->kb_kt;
    s32 left;
    if (!until)
        until = 1;
    while ((left = (s32)(until - timer_ticks())) > 0 && !a->kill && !irq_due(a) && a->evt == ev && a->kb_kt == key)
        thread_wait(&a->wq, until);
    return left > 0 ? (u32)left : 0;
}

/* ---- events (the desktop gives them, the API takes them) */

void app_event(struct app *a, const struct glos_event *e)
{
    u32 f = irq_save(), n = ARRAY_SIZE(a->evq);
    struct glos_event *last = a->evt != a->evh ? &a->evq[(a->evt - 1) % n] : 0;
    if (last && e->type == GLOS_EV_MOUSE && !e->key && last->type == GLOS_EV_MOUSE && !last->key
        && last->window == e->window) {
        *last = *e;                             /* (a movement after a movement: the newer one) */
    } else if (a->evt - a->evh < n) {
        a->evq[a->evt++ % n] = *e;
    }
    if (e->type == GLOS_EV_KEY)
        appdev_key(a, e->key, (int)e->down, e->ch, e->mods);
    else if (e->type == GLOS_EV_MOUSE)
        appdev_mouse(a, e->x, e->y, (s32)e->size, (s32)e->time, e->buttons);    /* (the desktop's: dx, dy there) */
    a->evq[(a->evt - 1) % n].size = sizeof *e;
    a->evq[(a->evt - 1) % n].time = (u32)(((unsigned long long)timer_ticks() * 125) >> 7);
    thread_wake(&a->wq);
    irq_restore(f);
}

int app_event_get(struct app *a, struct glos_event *e)
{
    u32 f = irq_save();
    int got = a->evt != a->evh;
    if (got)
        *e = a->evq[a->evh++ % ARRAY_SIZE(a->evq)];
    irq_restore(f);
    return got;
}

int app_event_waiting(const struct app *a) { return a->evt != a->evh; }

/* Its HLT: until an interrupt of its own. */
void app_idle(struct app *a)
{
    gpu_safe_begin(a);
    while (!a->kill && !vpic_pending(&a->pic))
        thread_block(&a->wq);
    gpu_safe_end(a);
}

/* Every kernel tick, and every interrupt of the PC's timer (interrupts
   off): the apps' own timers. Not while a session has the machine: their
   time stands still with them. */
void app_tick(void)
{
    struct app *a;
    if (!apps || paused())
        return;
    for (a = apps; a; a = a->next)
        if (a->ctx)
            appdev_tick(a);
}

void app_irq0(void)
{
    struct app *a;
    appdev_irq0_at(timer_ticks());
    if (!apps || paused())
        return;
    for (a = apps; a; a = a->next)
        if (a->ctx)
            appdev_irq0(a);
}

/* glos run --app: the session's program becomes an app without asking, at a
   return to its protected-mode code (the VM thread). 1: it has, and the
   frame is the stub's now. */
int app_force(struct trapframe *tf)
{
    u32 id;
    int r;
    if (!session_app() || !dctx || dctx->app || dctx->no_app)
        return 0;
    if ((r = app_init(tf, &id)) == GLOS_INIT_NESTED)
        return 0;                               /* not here: at a later return */
    if (r) {
        dctx->no_app = 1;
        kprintf("GLOS-APP refuse psp=%04x why=%u forced=1\n", dctx->psp, (u32)r);
        return 0;
    }
    app_detach(tf);
    return 1;
}

u32 app_ivt_get(u8 n)
{
    const struct app *a = app_cur();
    return a && mine(a, n) ? a->ivt[n] : vm_rd32(n * 4u);
}

void app_ivt_set(u8 n, u32 v)
{
    struct app *a = app_cur();
    if (a) {                                    /* into the table when the VM thread next makes a call for it */
        a->ivt[n] = v;
        a->ivt_mine[n >> 3] |= (u8)(1u << (n & 7));
    } else {
        wr32(n * 4u, v);
    }
}

/* ---- on the VM thread: the calls */

int app_vm_pending(void) { return callq != 0; }

u16 app_vm_psp(void) { return adopted ? adopted->psp : 0; }

int app_vm_ending(void) { return adopted && ending; }

void app_vm_base(int *depth, struct trapframe **f)
{
    if (adopted && current == vm.thread) {
        *depth = base;
        *f = rm_nest_frame(base);
    }
}

/* DOS has ended the app's process and gone to its terminate address, in
   the call under way or one nested in it: that call is over. */
void app_vm_ended(struct trapframe *tf)
{
    (void)tf;
    adopted->dead = 1;
    adopted->ctx = 0;
    rm_nest_end(base);
}

static void adopt(struct app *a)
{
    u32 lp = (u32)vm.loader_psp << 4, spare = vm.bi->kill_sp - SPARE, v;

    if (!sys_cwd.known) {                       /* (a session may have left another) */
        cwd_read(&dos_has);
        sys_cwd = dos_has;
    }
    if (a->ctx)
        cwd_want(&a->cwd);
    sys.vif = current->vif;
    sys.psp = vm_current_psp();
    sys.dta = vm_rd32(vm.sda + 0x0C);
    sys.ps_stack = vm_rd32(lp + 0x2E);
    memcpy(sys.ivt, vm_ptr(0), sizeof sys.ivt);
    sys.job = agent_vm_as(a->job);
    for (v = 0; v < 256; v++)
        if (mine(a, v))
            wr32(v * 4, a->ivt[v]);
    wr16(vm.sda + 0x10, a->psp);
    wr32(vm.sda + 0x0C, a->dta);
    memset(vm_ptr(((u32)vm.loader_cs << 4) + spare), 0, 48);
    wr32(lp + 0x2E, (u32)vm.loader_cs << 16 | spare);
    current->vif = a->ctx ? a->thread->vif : 1;
    current->ctx = a->ctx;
    dpmi_ctx_load(a->ctx);
    base = rm_nesting() + 1;
    adopted = a;
    a->adopted = 1;
}

/* cwd: the call may have changed the drive or the directory. */
static void unadopt(struct app *a, int cwd)
{
    u32 lp = (u32)vm.loader_psp << 4, v;

    adopted = 0;
    a->adopted = 0;
    vm.kill_req = 0;                            /* (a kill asked for during the call was the app's: its flag has it) */
    agent_vm_as(sys.job);
    if (!a->dead) {
        a->dta = vm_rd32(vm.sda + 0x0C);
        a->thread->vif = current->vif;
        for (v = 0; v < 256; v++) {
            u32 cur = vm_rd32(v * 4);
            if (cur != sys.ivt[v]) {
                a->ivt[v] = cur;
                a->ivt_mine[v >> 3] |= (u8)(1u << (v & 7));
            } else {
                a->ivt_mine[v >> 3] &= (u8)~(1u << (v & 7));
            }
        }
    }
    memcpy(vm_ptr(0), sys.ivt, sizeof sys.ivt);
    wr16(vm.sda + 0x10, sys.psp);
    wr32(vm.sda + 0x0C, sys.dta);
    wr32(lp + 0x2E, sys.ps_stack);
    current->ctx = 0;
    dpmi_ctx_load(0);
    current->vif = sys.vif;
    if (cwd && !a->dead) {
        cwd_read(&dos_has);
        a->cwd = dos_has;
    }
}

static void serve(struct app *a)
{
    struct rmregs r;
    int cwd = 0;

    adopt(a);
    if (a->call == AC_RM) {
        if (a->c.vec == 0x21 && a->c.kind != RM_FAR) {
            u32 ah = (a->c.r->eax >> 8) & 0xFF;
            cwd = ah == 0x0E || ah == 0x3B || ah == 0x4B || ah == 0x71;
            exclusive = ah == 0x4B;             /* its child program is a session of its own (PRD Q6) */
        }
        a->ncalls++;
        a->result = rm_call(a->c.from, a->c.r, a->c.kind, a->c.vec, a->c.words, a->c.nwords);
        if (exclusive) {
            exclusive = 0;
            thread_wake(&pauseq);
            if (!a->dead && a->result == 0 && !(a->c.r->flags & FL_CF)) {
                memset(&r, 0, sizeof r);        /* the child's exit code, before anyone else's end changes it */
                r.eax = 0x4D00;
                if (dos21(&r) == 0) {
                    a->child = (u16)r.eax;
                    a->child_have = 1;
                }
            }
        }
    } else {                                    /* its end: INT 21h 4Ch as the program; DOS doesn't come back, */
        memset(&r, 0, sizeof r);                /* terminated() ends the call */
        r.eax = 0x4C00 | (a->code & 0xFF);
        ending = 1;
        dos21(&r);
        ending = 0;
        if (!a->dead) {
            kprintf("GLOS-APP error=end-returned id=%u psp=%04x\n", a->id, a->psp);
            a->dead = 1;
        }
    }
    unadopt(a, cwd);
    if (a->dead) {                              /* how DOS says it ended: AH the way, AL the code */
        memset(&r, 0, sizeof r);
        r.eax = 0x4D00;
        a->code = dos21(&r) == 0 ? r.eax & 0xFFFF : 0xFF;
        last_code = a->code;
        kprintf("GLOS-APP end id=%u prog=%s code=%u how=%u calls=%u ticks=%u t=%u irq0=%u irq13=%u\n", a->id, a->prog,
                a->code & 0xFF, a->code >> 8, a->ncalls, a->thread->ticks, timer_ticks() - a->t0, a->nirq[0],
                a->nirq[13]);
        if (a->job)
            agent_app_done(a->job, a->code);
    }
    a->call = AC_NONE;
    thread_wake(&a->wq);
}

void app_vm_turn(void)
{
    u32 t0 = timer_ticks(), n = 0;
    struct app *a;
    if (current != vm.thread || adopted)
        return;
    while ((a = callq) && n++ < 16 && timer_ticks() - t0 < 8) {        /* (then the system VM's own work has a turn) */
        callq = a->qnext;
        if (!callq)
            callq_tail = 0;
        serve(a);
    }
    if (sys_cwd.known)
        cwd_want(&sys_cwd);                     /* DOS's directory is the system VM's again */
    if (!apps)
        sys_cwd.known = dos_has.known = 0;
}

/* ---- from anywhere */

void app_kill(struct app *a, const char *why)
{
    u32 f = irq_save();
    if (!a->kill) {
        a->kill = 1;
        kprintf("GLOS-APP kill id=%u reason=%s\n", a->id, why);
    }
    if (a->adopted) {                           /* in the system VM: the kill there (v86.c), which ends its call */
        vm.kill_req = 1;
        vm.kill_reason = why;
        vm.kill_since = timer_ticks();
        vm_kick();
    }
    thread_wake(&a->wq);
    thread_wake(&pauseq);
    irq_restore(f);
}

u32 app_id(const struct app *a) { return a->id; }

void app_kill_id(u32 id, const char *why)
{
    struct app *a;
    u32 f = irq_save();
    for (a = apps; a; a = a->next)
        if (a->id == id)
            app_kill(a, why);
    irq_restore(f);
}

void app_kill_job(void *job)
{
    struct app *a;
    u32 f = irq_save();
    for (a = apps; a; a = a->next)
        if (a->job == job)
            app_kill(a, "agent");
    irq_restore(f);
}

void app_kill_all(const char *why)
{
    struct app *a;
    u32 f = irq_save();
    for (a = apps; a; a = a->next)
        app_kill(a, why);
    irq_restore(f);
}

void app_session_end(void)
{
    sys_cwd.known = dos_has.known = 0;          /* (the session may have left DOS somewhere else) */
    thread_wake(&pauseq);
}

u32 app_count(void) { return napps; }

u32 app_last_code(void) { return last_code; }

static const char *state_of(const struct app *a)
{
    return !a->ctx ? "ending" : a->adopted ? "in-dos" : a->call ? "waiting" : a->kill ? "killed" : "running";
}

u32 app_ps(char *buf, u32 max)
{
    const struct app *a;
    u32 n = 0, f = irq_save();
    for (a = apps; a && n < max; a = a->next)
        n += (u32)ksnprintf(buf + n, max - n, "app %u %s %s psp=%04x thread=%s held_kb=%u calls=%u ticks=%u irq0=%u\n",
                            a->id, a->prog, state_of(a), a->psp, a->tname, a->ctx ? a->ctx->frames * 4 : 0, a->ncalls,
                            a->thread->ticks, a->nirq[0]);
    irq_restore(f);
    return n < max ? n : max;
}

/* GLOS_FN_STAT's GLOS_STAT_APPS (M9): a struct glos_stat_app for each app. */
u32 app_stat_list(void *out, u32 room)
{
    struct app *a;
    const struct app *list[MAX_APPS];
    u32 n = 0, k = 0, i, f = irq_save();
    for (a = apps; a && k < MAX_APPS; a = a->next)
        list[k++] = a;
    irq_restore(f);
    for (i = 0; i < k && n + sizeof(struct glos_stat_app) <= room; i++) {
        struct glos_stat_app *s = (struct glos_stat_app *)((u8 *)out + n);
        memset(s, 0, sizeof *s);
        s->size = sizeof *s;
        s->kind = 0;
        s->id = list[i]->id;
        s->busy_ms = (u32)(((unsigned long long)list[i]->thread->ticks * 125) >> 7);
        s->mem_kb = list[i]->ctx ? list[i]->ctx->frames * 4 : 0;
        s->windows = desk_windows(list[i]);
        s->state = session_pauses() ? 1 : 0;
        memcpy(s->name, list[i]->prog, sizeof list[i]->prog < sizeof s->name ? sizeof list[i]->prog : sizeof s->name);
        s->name[sizeof s->name - 1] = 0;
        n += sizeof *s;
    }
    return n;
}

u32 app_stat(char *buf, u32 max)
{
    const struct app *a;
    u32 n = 0, f = irq_save();
    n += (u32)ksnprintf(buf + n, max - n, "[");
    for (a = apps; a && n < max; a = a->next)
        n += (u32)ksnprintf(buf + n, max - n, "%s{\"id\":%u,\"program\":\"%s\",\"state\":\"%s\",\"psp\":%u,"
                            "\"held_kb\":%u,\"calls\":%u,\"ticks\":%u,\"irq0\":%u,\"irq13\":%u}", a == apps ? "" : ",",
                            a->id, a->prog, state_of(a), a->psp, a->ctx ? a->ctx->frames * 4 : 0, a->ncalls,
                            a->thread->ticks, a->nirq[0], a->nirq[13]);
    irq_restore(f);
    n += (u32)ksnprintf(buf + n, max > n ? max - n : 0, "]");
    return n < max ? n : max;
}
