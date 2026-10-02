/* Threads and the scheduler (supervisor.md §7). Every thread has its own
 * kernel stack; the system VM is one of them, entered and left through
 * traps on its stack. A switch happens only in schedule(), with interrupts
 * off: from a thread that blocks, sleeps or yields, or on the way out of an
 * interrupt or a trap from V86 mode when a reschedule is due. Kernel
 * threads run with interrupts on and are preemptible unless they raise the
 * preempt count; the trap paths of the system VM run with interrupts off
 * and are never preempted midway. */
#ifndef K_SCHED_H
#define K_SCHED_H
#include "types.h"

struct trapframe;

#define THREAD_STACK 8192u

enum { PRIO_URGENT, PRIO_NORMAL, PRIO_BULK, PRIO_IDLE };
enum { T_READY, T_RUNNING, T_BLOCKED, T_DEAD };

struct thread {
    u32 esp;                            /* saved by switch_to */
    u8 *stack;                          /* the lowest address; a canary lives there */
    u32 stack_top;
    u8 state, prio;
    u16 slice;                          /* ticks left in this turn */
    const char *name;
    struct thread *next;                /* in a run queue, a wait queue or the sleepers */
    struct thread *all;                 /* every thread, for reports */
    void (*fn)(void *);
    void *arg;
    u32 wake_at;                        /* sleeping: the tick to wake at */
    u32 ticks;                          /* ticks spent running */
};

struct waitq {
    struct thread *head;
};

extern struct thread *current;

void sched_init(void);                  /* the caller becomes the idle thread */
void sched_idle(void) __attribute__((noreturn));
struct thread *thread_create(const char *name, int prio, void (*fn)(void *), void *arg);
void thread_exit(void) __attribute__((noreturn));

/* With interrupts off. */
void schedule(void);
void thread_block(struct waitq *q);     /* until thread_wake(q) */
int thread_wake(struct waitq *q);       /* every waiter; 1 if any */
void sched_resched(void);               /* switch at the next safe point */

/* With interrupts on or off; they come back as they were. */
void thread_sleep_until(u32 tick);
void thread_yield(void);
void preempt_disable(void);
void preempt_enable(void);

void sched_tick(void);                  /* every kernel tick, from the clock's IRQ */
void sched_trap_exit(struct trapframe *tf);     /* the end of an IRQ or a trap from V86 */
void sched_report(void);                /* GLOS-SCHED lines */

#endif
