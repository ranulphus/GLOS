/* Threads and the scheduler (supervisor.md §7). Every thread has its own
 * kernel stack; the system VM is one of them, entered and left through
 * traps on its stack. A switch happens only in schedule(), with interrupts
 * off: from a thread that blocks, sleeps or yields, or on the way out of an
 * interrupt or a trap from V86 mode when a reschedule is due. Kernel
 * threads run with interrupts on and are preemptible unless they raise the
 * preempt count; the trap paths of the system VM run with interrupts off
 * and are never preempted midway.
 *
 * Threads that run user-mode code (M6): the system VM's, and one for each
 * GLOS app (kernel/app/app.c). Each has a virtual interrupt flag, may have a
 * DPMI context (an address space, an LDT), and has the FPU's state while
 * another of them has the FPU. The switch hook loads what the next one
 * needs (kernel/dpmi/host.c). */
#ifndef K_SCHED_H
#define K_SCHED_H
#include "types.h"

struct trapframe;

#define THREAD_STACK 16384u                 /* the ssh thread peaked at 5,420 bytes of 8 KB, and IRQs nest on it */

enum { PRIO_URGENT, PRIO_NORMAL, PRIO_BULK, PRIO_IDLE };
enum { T_READY, T_RUNNING, T_BLOCKED, T_DEAD };

struct thread {
    u32 esp;                            /* saved by switch_to */
    u8 *stack;                          /* the lowest address; a canary lives there */
    u32 stack_top;
    u8 state, prio;
    u16 slice;                          /* ticks left in this turn */
    const char *name;
    struct thread *next;                /* in a run queue or a wait queue */
    struct thread *snext;               /* in the sleepers (thread_sleep_until, thread_wait) */
    struct waitq *waiting_on;           /* thread_wait: the queue it may be woken from */
    u8 timed_out;
    struct thread *all;                 /* every thread, for reports */
    void (*fn)(void *);
    void *arg;
    u32 wake_at;                        /* sleeping: the tick to wake at */
    u32 ticks;                          /* ticks spent running */
    u32 esp0;                           /* where a trap from user mode builds its frame (TSS.ESP0) */
    u8 user;                            /* TU_: it runs user-mode code */
    u8 vif;                             /* ... whose virtual interrupt flag this is (EFLAGS.VIF in V86 mode with VME) */
    u8 front;                           /* woken, it goes first in its class, whatever it has run (the VM thread) */
    u8 ahead;                           /* in a run queue: it goes first */
    u32 vrun;                           /* the normal class: ticks it has run, from the class's floor when it began */
    void *ctx;                          /* ... in this DPMI context (struct dpmi_ctx), or 0 */
    u8 *fpu;                            /* ... and its FPU state (FNSAVE's 108 bytes) while it isn't in the FPU */
};

enum { TU_NONE, TU_VM, TU_APP };

struct waitq {
    struct thread *head;
};

extern struct thread *current;

void sched_init(void);                  /* the caller becomes the idle thread */
void sched_idle(void) __attribute__((noreturn));
struct thread *thread_create(const char *name, int prio, void (*fn)(void *), void *arg);
struct thread *thread_new(const char *name, int prio, void (*fn)(void *), void *arg, int user);
                                        /* the same, 0 without memory; user: TU_ (a suspended start: thread_go) */
void thread_go(struct thread *t);       /* a thread_new() thread becomes ready (interrupts off) */
extern void (*sched_switch)(struct thread *next);       /* called before every switch to another thread */
void thread_exit(void) __attribute__((noreturn));

/* With interrupts off. */
void schedule(void);
void thread_block(struct waitq *q);     /* until thread_wake(q) */
int thread_wake(struct waitq *q);       /* every waiter; 1 if any */
void sched_resched(void);               /* switch at the next safe point */
void sched_front(struct thread *t);     /* a ready thread goes first in its class, and gets the CPU soon */
int sched_urgent_turn(void);            /* urgent threads run now, then the caller goes on before anyone else */
void sched_nudge(struct thread *t);     /* something came for it: the CPU now, if it has run less than the running one */

/* With interrupts on or off; they come back as they were. */
void thread_sleep_until(u32 tick);
int thread_wait(struct waitq *q, u32 tick);     /* thread_wake(q) (1) or tick reached (0); tick 0: none */
void thread_yield(void);
void preempt_disable(void);
void preempt_enable(void);

void sched_tick(void);                  /* every kernel tick, from the clock's IRQ */
void sched_trap_exit(struct trapframe *tf);     /* the end of an IRQ or a trap from V86 */
void sched_report(void);                /* GLOS-SCHED lines */
void sched_panic_report(void);          /* GLOS-PANIC thread= lines */
u32 sched_ps(char *buf, u32 max);       /* glos ps: a line per thread */
u32 sched_stat(char *buf, u32 max);     /* glos stat: "NAME":ticks for each thread */

u32 sched_idle_ticks(void);             /* ticks the processor was halted (M9) */
u32 sched_threads(void);

#endif
