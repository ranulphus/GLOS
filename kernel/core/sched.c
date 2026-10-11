/* Threads and the scheduler (supervisor.md §7).
 *   urgent   first, always (network receive, input, short replies)
 *   normal   round-robin, 20-tick turns (the system VM, GLOS apps)
 *   bulk     takes turns with normal threads until it has used 30 ticks of
 *            the current 100-tick window, then runs only when no normal
 *            thread is ready (crypto, SFTP, PNG)
 *   idle     HLT with interrupts on
 * A turn is 20 ticks of running, however often the thread waits meanwhile:
 * woken with some of its turn left, it goes ahead of its class's queue and
 * takes the CPU from a thread of its class (M6: an app back from a DOS call
 * doesn't wait out a busy app's turn; nor can it starve one, its turn runs
 * out too). A thread that loses the CPU that way keeps the rest of its
 * turn, and goes on after those that went ahead. The VM thread goes ahead
 * whenever it is woken: it wakes for an interrupt.
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

/* Ahead of the queue: woken (ahead: before those that went ahead earlier),
   or having lost the CPU with turn left (after them all). */
static void enqueue_front(struct thread *t, int woken)
{
    struct thread *q, *prev = NULL;
    int p = t->prio;
    t->state = T_READY;
    t->ahead = (u8)woken;
    if (!woken)
        for (q = rq_head[p]; q && q->ahead; q = q->next)
            prev = q;
    if (prev) {
        t->next = prev->next;
        prev->next = t;
    } else {
        t->next = rq_head[p];
        rq_head[p] = t;
    }
    if (!t->next)
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

static struct thread *pick(void)
{
    struct thread *t;
    if ((t = dequeue(PRIO_URGENT)))
        return t;
    if (rq_head[PRIO_BULK] && (!rq_head[PRIO_NORMAL] || (bulk_used < BULK_BUDGET && !last_bulk))) {
        last_bulk = 1;
        return dequeue(PRIO_BULK);
    }
    if ((t = dequeue(PRIO_NORMAL))) {
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
   point; so does the system VM over bulk work (supervisor.md §7), and over
   an app: it wakes for an interrupt DOS or the BIOS must not wait a round
   of busy apps for (M6). */
static void wake(struct thread *t)
{
    if (t->front || t->slice) {
        enqueue_front(t, 1);
        if (t->prio < current->prio || (t->prio == current->prio && !current->front && current != t))
            need_resched = 1;
        return;
    }
    enqueue(t);
    if (t->prio < current->prio)
        need_resched = 1;
}

void schedule(void)
{
    struct thread *prev = current, *next;

    need_resched = 0;
    if (prev->state == T_RUNNING && prev != &idle_thread) {
        if (prev->slice)
            enqueue_front(prev, 0);             /* (it lost the CPU: the rest of its turn comes) */
        else
            enqueue(prev);
    }
    next = pick();
    if (!next)
        next = &idle_thread;
    next->state = T_RUNNING;
    if (!next->slice)
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

/* A thread that waits its turn in a run queue goes to the head of it, and
   gets the CPU from a thread of its class or a lower one at the next safe
   point (the VM thread, for an interrupt raised while it is ready). */
void sched_front(struct thread *t)
{
    struct thread *q, *prev = NULL;
    int p = t->prio;
    if (t->state != T_READY || t == current)
        return;
    for (q = rq_head[p]; q && q != t; q = q->next)
        prev = q;
    if (!q)
        return;
    if (prev) {
        prev->next = t->next;
        if (rq_tail[p] == t)
            rq_tail[p] = prev;
        t->next = rq_head[p];
        rq_head[p] = t;
    }
    t->ahead = 1;
    if (p <= current->prio)
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
    current->slice = 0;                         /* the rest of its turn given up */
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
