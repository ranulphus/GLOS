/* Threads and the scheduler (supervisor.md §7).
 *   urgent   first, always (network receive, input, short replies)
 *   normal   round-robin, 20-tick turns (the system VM, GLOS apps)
 *   bulk     takes turns with normal threads until it has used 30 ticks of
 *            the current 100-tick window, then runs only when no normal
 *            thread is ready (crypto, SFTP, PNG)
 *   idle     HLT with interrupts on
 * The normal class is shared by running time (M6): each thread counts the
 * ticks it has run, and the ready one that has run least goes next, for 20
 * ticks at most before the choice is made again. A thread woken having run
 * less than the running one takes the CPU from it. So one that mostly
 * waits (an app back from a DOS call, or with a timer interrupt to take)
 * runs at once beside busy ones, and busy ones share what is left equally;
 * a long sleep earns no more than 10 ticks of advance. The VM thread goes
 * first whenever it is woken, whatever it has run: it wakes for an
 * interrupt.
 * A thread's stack has a canary word at its base, checked at each switch. */
#include "arch.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "sched.h"
#include "timer.h"

#define SLICE       20                  /* ticks per turn: about 20 ms */
#define WINDOW      100                 /* the bulk budget's window, in ticks */
#define BULK_BUDGET 30                  /* bulk ticks per window while others are ready */
#define CANARY      0x4B435453u
#define EFLAGS_IF   0x200u
#define EFLAGS_VM   0x20000u

extern char boot_stack[], boot_stack_top[];
extern void switch_to(u32 *save_esp, u32 new_esp);

struct thread *current;
void (*sched_switch)(struct thread *next);
static struct thread idle_thread;
static struct thread *rq_head[3], *rq_tail[3];
static struct thread *sleepers, *dead, *all_threads;
static u32 need_resched, preempt_count, n_switches;
static u32 bulk_used, window_start;
static u32 bulk_contended, normal_contended;    /* ticks run while the other class was ready */
static u8 last_bulk;
static u32 min_vrun;                    /* the normal class's running times start from here (it only grows) */
static struct thread *held_for;         /* sched_urgent_turn(): the thread that goes on when the urgent ones are done */

#define SLEEPER 10                      /* ticks of advance a thread wakes with at most */
#define GRAN    2                       /* a woken thread takes the CPU when it is this far behind */

static void enqueue(struct thread *t)
{
    int p = t->prio;
    t->state = T_READY;
    t->next = NULL;
    if (rq_tail[p])
        rq_tail[p]->next = t;
    else
        rq_head[p] = t;
    rq_tail[p] = t;
}

static struct thread *dequeue(int p)
{
    struct thread *t = rq_head[p];
    if (t) {
        rq_head[p] = t->next;
        if (!rq_head[p])
            rq_tail[p] = NULL;
        t->next = NULL;
        t->ahead = 0;
    }
    return t;
}

/* The normal class's next: one that went first when woken (the VM thread),
   or the one that has run least; among equals, the one that has waited
   longest. */
static struct thread *pick_normal(void)
{
    struct thread *t, *prev = NULL, *best = NULL, *bprev = NULL;
    for (t = rq_head[PRIO_NORMAL]; t; prev = t, t = t->next) {
        if (t->ahead) {
            best = t;
            bprev = prev;
            break;
        }
        if (!best || (s32)(t->vrun - best->vrun) < 0) {
            best = t;
            bprev = prev;
        }
    }
    if (!best)
        return NULL;
    if (bprev)
        bprev->next = best->next;
    else
        rq_head[PRIO_NORMAL] = best->next;
    if (rq_tail[PRIO_NORMAL] == best)
        rq_tail[PRIO_NORMAL] = bprev;
    best->next = NULL;
    best->ahead = 0;
    if ((s32)(best->vrun - min_vrun) > 0)
        min_vrun = best->vrun;
    return best;
}

static struct thread *pick(void)
{
    struct thread *t, **pp;
    if ((t = dequeue(PRIO_URGENT)))
        return t;
    if ((t = held_for)) {                       /* (the idle thread is in no queue: NULL is it) */
        if (t == &idle_thread)
            return NULL;
        for (pp = &rq_head[t->prio]; *pp && *pp != t; pp = &(*pp)->next) ;
        if (*pp) {
            struct thread *q;
            *pp = t->next;
            for (q = rq_head[t->prio]; q && q->next; q = q->next) ;
            rq_tail[t->prio] = q;
            t->next = NULL;
            t->ahead = 0;
            return t;
        }
    }
    if (rq_head[PRIO_BULK] && (!rq_head[PRIO_NORMAL] || (bulk_used < BULK_BUDGET && !last_bulk))) {
        last_bulk = 1;
        return dequeue(PRIO_BULK);
    }
    if ((t = pick_normal())) {
        last_bulk = 0;
        return t;
    }
    return NULL;
}

static void unsleep(struct thread *t)
{
    struct thread **pp;
    for (pp = &sleepers; *pp; pp = &(*pp)->snext)
        if (*pp == t) {
            *pp = t->snext;
            break;
        }
}

static void unwait(struct thread *t)
{
    struct thread **pp;
    if (!t->waiting_on)
        return;
    for (pp = &t->waiting_on->head; *pp; pp = &(*pp)->next)
        if (*pp == t) {
            *pp = t->next;
            break;
        }
    t->waiting_on = NULL;
}

/* A woken thread that outranks the running one gets the CPU at the next safe
   point; so does the system VM over bulk work (supervisor.md §7), and in
   the normal class a thread that has run less than the running one, and
   the VM thread always (it wakes for an interrupt DOS or the BIOS must not
   wait a round of busy apps for; M6). */
static void wake(struct thread *t)
{
    if (t->prio == PRIO_NORMAL) {
        if ((s32)(t->vrun - (min_vrun - SLEEPER)) < 0)
            t->vrun = min_vrun - SLEEPER;
        t->ahead = t->front;
    }
    enqueue(t);
    if (t->prio < current->prio)
        need_resched = 1;
    else if (t->prio == PRIO_NORMAL && current->prio == PRIO_NORMAL && current != t
             && (t->front ? !current->front : (s32)(t->vrun + GRAN - current->vrun) < 0))
        need_resched = 1;
}

void schedule(void)
{
    struct thread *prev = current, *next;

    need_resched = 0;
    if (prev->state == T_RUNNING && prev != &idle_thread)
        enqueue(prev);
    next = pick();
    if (!next)
        next = &idle_thread;
    next->state = T_RUNNING;
    next->slice = SLICE;
    if (next == prev)
        return;
    if (*(u32 *)prev->stack != CANARY)
        panic("stack-overflow", NULL);
    n_switches++;
    current = next;
    if (sched_switch)
        sched_switch(next);
    cpu_set_esp0(next->esp0);
    switch_to(&prev->esp, next->esp);
}

void sched_resched(void) { need_resched = 1; }

/* Urgent threads that are ready run now, and then the caller goes on, before
   any other thread: for a caller that must not be overtaken (the log's
   writer in the middle of a line, lib/kprintf.c) but shouldn't keep network
   receive waiting. Interrupts off. 1 if any ran. */
int sched_urgent_turn(void)
{
    if (!rq_head[PRIO_URGENT] || held_for)
        return 0;
    held_for = current;
    schedule();
    held_for = NULL;
    return 1;
}

/* A ready thread goes first in its class (the VM thread, for an interrupt
   raised while it waits its turn), and gets the CPU from a thread of its
   class or a lower one at the next safe point. */
void sched_front(struct thread *t)
{
    if (t->state != T_READY || t == current)
        return;
    t->ahead = 1;
    if (t->prio <= current->prio)
        need_resched = 1;
}

/* Something came for a thread that may be ready and waiting its turn (an
   app's own interrupt): if it has run less than the running one, the
   choice is made again now. */
void sched_nudge(struct thread *t)
{
    if (t->state == T_READY && t->prio == PRIO_NORMAL && current->prio == PRIO_NORMAL
        && (s32)(t->vrun + GRAN - current->vrun) < 0)
        need_resched = 1;
}

/* ---- threads */

static void thread_start(void) __attribute__((noreturn, used));
static void thread_start(void)
{
    sti();
    current->fn(current->arg);
    thread_exit();
}

struct thread *thread_new(const char *name, int prio, void (*fn)(void *), void *arg, int user)
{
    struct thread *t = kmalloc(sizeof *t);
    u32 *sp, f;
    if (!t)
        return NULL;
    memset(t, 0, sizeof *t);
    if (!(t->stack = kmalloc(THREAD_STACK)) || (user && !(t->fpu = kmalloc(108)))) {
        if (t->stack)
            kfree(t->stack);
        kfree(t);
        return NULL;
    }
    *(u32 *)t->stack = CANARY;
    t->stack_top = (u32)t->stack + THREAD_STACK;
    t->esp0 = t->stack_top;
    t->prio = (u8)prio;
    t->name = name;
    t->fn = fn;
    t->arg = arg;
    t->user = (u8)user;
    t->state = T_BLOCKED;
    t->vrun = min_vrun;
    sp = (u32 *)t->stack_top;
    *--sp = 0;                                  /* thread_start never returns */
    *--sp = (u32)thread_start;                  /* switch_to's RET */
    *--sp = 0;                                  /* ebp */
    *--sp = 0;                                  /* ebx */
    *--sp = 0;                                  /* esi */
    *--sp = 0;                                  /* edi */
    t->esp = (u32)sp;
    f = irq_save();
    t->all = all_threads;
    all_threads = t;
    irq_restore(f);
    return t;
}

void thread_go(struct thread *t) { wake(t); }

struct thread *thread_create(const char *name, int prio, void (*fn)(void *), void *arg)
{
    struct thread *t = thread_new(name, prio, fn, arg, TU_NONE);
    u32 f;
    if (!t)
        panic("thread-create", NULL);
    f = irq_save();
    wake(t);
    irq_restore(f);
    return t;
}

void thread_exit(void)
{
    cli();
    current->state = T_DEAD;
    current->next = dead;
    dead = current;
    schedule();
    panic("dead-thread-ran", NULL);
}

void sched_init(void)
{
    idle_thread.stack = (u8 *)boot_stack;
    idle_thread.stack_top = (u32)boot_stack_top;
    idle_thread.esp0 = idle_thread.stack_top;
    *(u32 *)idle_thread.stack = CANARY;
    idle_thread.prio = PRIO_IDLE;
    idle_thread.state = T_RUNNING;
    idle_thread.name = "idle";
    idle_thread.all = NULL;
    all_threads = &idle_thread;
    current = &idle_thread;
    window_start = timer_ticks();
}

/* The boot stack's own thread: HLT until an interrupt makes work, which its
   exit path switches to. Dead threads' memory is freed here. */
void sched_idle(void)
{
    for (;;) {
        cli();
        while (dead) {
            struct thread *t = dead, **pp;
            dead = t->next;
            for (pp = &all_threads; *pp; pp = &(*pp)->all)
                if (*pp == t) {
                    *pp = t->all;
                    break;
                }
            kfree(t->stack);
            if (t->fpu)
                kfree(t->fpu);
            kfree(t);
        }
        if (need_resched || rq_head[0] || rq_head[1] || rq_head[2])
            schedule();
        if (kprintf_pump(1))                    /* a log line to COM1 while there is nothing else to do, then a */
            continue;                           /* look at the queues again */
        __asm__ volatile("sti; hlt" ::: "memory");
    }
}

/* ---- waiting */

void thread_block(struct waitq *q)
{
    u32 f;
    __asm__ volatile("pushfl; popl %0" : "=r"(f));
    if (f & EFLAGS_IF)
        panic("thread_block-with-interrupts-on", NULL);
    current->state = T_BLOCKED;
    current->next = q->head;
    current->waiting_on = q;
    q->head = current;
    schedule();
}

int thread_wake(struct waitq *q)
{
    struct thread *t = q->head, *n;
    u32 f;
    __asm__ volatile("pushfl; popl %0" : "=r"(f));
    if (f & EFLAGS_IF)                          /* the lists would race with IRQs that wake threads */
        panic("thread_wake-with-interrupts-on", NULL);
    q->head = NULL;
    if (!t)
        return 0;
    for (; t; t = n) {
        n = t->next;
        t->waiting_on = NULL;
        unsleep(t);
        wake(t);
    }
    return 1;
}

void thread_sleep_until(u32 tick)
{
    u32 f = irq_save();
    if ((s32)(tick - timer_ticks()) > 0) {
        current->wake_at = tick;
        current->state = T_BLOCKED;
        current->snext = sleepers;
        sleepers = current;
        schedule();
    }
    irq_restore(f);
}

int thread_wait(struct waitq *q, u32 tick)
{
    u32 f = irq_save();
    int woken = 1;
    if (!tick || (s32)(tick - timer_ticks()) > 0) {
        current->timed_out = 0;
        if (tick) {
            current->wake_at = tick;
            current->snext = sleepers;
            sleepers = current;
        }
        thread_block(q);
        woken = !current->timed_out;
    } else {
        woken = 0;
    }
    irq_restore(f);
    return woken;
}

void thread_yield(void)
{
    u32 f = irq_save();
    struct thread *t;
    if (current->prio == PRIO_NORMAL)           /* behind every ready thread of its class */
        for (t = rq_head[PRIO_NORMAL]; t; t = t->next)
            if ((s32)(t->vrun - current->vrun) > 0)
                current->vrun = t->vrun;
    schedule();
    irq_restore(f);
}

void preempt_disable(void) { preempt_count++; }

void preempt_enable(void)
{
    u32 f = irq_save();
    if (preempt_count)
        preempt_count--;
    if (!preempt_count && need_resched && (f & EFLAGS_IF))
        schedule();
    irq_restore(f);
}

/* ---- the clock */

void sched_tick(void)
{
    u32 now = timer_ticks();
    struct thread **pp = &sleepers, *t;

    if (!current)
        return;
    current->ticks++;
    if (current->prio == PRIO_NORMAL)
        current->vrun++;
    if (current->prio == PRIO_BULK) {
        bulk_used++;
        if (rq_head[PRIO_NORMAL])
            bulk_contended++;
    } else if (current->prio == PRIO_NORMAL && rq_head[PRIO_BULK]) {
        normal_contended++;
    }
    if (now - window_start >= WINDOW) {
        window_start = now;
        bulk_used = 0;
    }
    while ((t = *pp)) {
        if ((s32)(now - t->wake_at) >= 0) {
            *pp = t->snext;
            if (t->waiting_on) {
                unwait(t);
                t->timed_out = 1;
            }
            wake(t);
        } else {
            pp = &t->snext;
        }
    }
    if (current != &idle_thread && current->slice && --current->slice == 0)
        need_resched = 1;
    if (current->prio == PRIO_BULK && bulk_used >= BULK_BUDGET && rq_head[PRIO_NORMAL])
        need_resched = 1;
}

/* A switch only where the interrupted code may lose the CPU: V86 mode, or a
   thread that had interrupts on (never a trap path, which runs with them
   off), and not inside a preempt_disable() section. */
void sched_trap_exit(struct trapframe *tf)
{
    if (!need_resched || preempt_count || !current)
        return;
    if ((tf->eflags & EFLAGS_VM) || (tf->eflags & EFLAGS_IF))
        schedule();
}

/* For the #DF report: each thread's stack, saved ESP and canary. */
void sched_panic_report(void)
{
    struct thread *t;
    for (t = all_threads; t; t = t->all)
        kprintf("GLOS-PANIC thread=%s cur=%u state=%u stack=%p top=%p esp=%p canary=%s\n", t->name, t == current,
                t->state, (u32)t->stack, t->stack_top, t->esp, *(u32 *)t->stack == CANARY ? "ok" : "bad");
}

/* glos ps: "thread NAME CLASS STATE ticks=N" lines. */
u32 sched_ps(char *buf, u32 max)
{
    static const char *const prio[] = { "urgent", "normal", "bulk", "idle" };
    static const char *const state[] = { "ready", "running", "blocked", "dead" };
    struct thread *t;
    u32 n = 0, f = irq_save();
    for (t = all_threads; t && n < max; t = t->all)
        n += (u32)ksnprintf(buf + n, max - n, "thread %s %s %s ticks=%u\n", t->name, prio[t->prio & 3],
                            state[t->state & 3], t->ticks);
    irq_restore(f);
    return n < max ? n : max;
}

/* glos stat: "NAME":ticks for each thread, commas between. */
u32 sched_stat(char *buf, u32 max)
{
    struct thread *t;
    u32 n = 0, f = irq_save();
    for (t = all_threads; t && n < max; t = t->all)
        n += (u32)ksnprintf(buf + n, max - n, "%s\"%s\":%u", n ? "," : "", t->name, t->ticks);
    irq_restore(f);
    return n < max ? n : max;
}

void sched_report(void)
{
    struct thread *t;
    kprintf("GLOS-SCHED switches=%u contended_bulk=%u contended_normal=%u", n_switches, bulk_contended,
            normal_contended);
    for (t = all_threads; t; t = t->all)
        kprintf(" %s=%u", t->name, t->ticks);
    kprintf("\n");
}
