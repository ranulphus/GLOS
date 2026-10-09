/* The agent shell and output capture (kernel/dos/agent.c; supervisor.md
 * §17.3, §17.4). The ssh thread posts DOS commands and empties their
 * output; the VM thread runs them through the resident stub. */
#ifndef K_AGENT_H
#define K_AGENT_H
#include "types.h"

struct trapframe;

/* The ssh thread's side. owner is its channel. */
void agent_init(void (*kick_ssh)(void));       /* kick_ssh: output or an end is waiting */
int agent_post(void *owner, const char *cmd);   /* 0, or -1 when every job slot is taken */
int agent_post_run(void *owner, const char *cmd, int direct, const char *profile);     /* glos run's */
void agent_drop(void *owner);                   /* the channel has gone */
void agent_service(u32 (*write)(void *owner, int stream, const u8 *d, u32 n),
                   void (*done)(void *owner, u32 code));       /* code: the exit status */
void agent_exit(u32 delay_ticks);               /* glos exit: leave once idle, this many ticks on */
int agent_kill(void);                           /* glos kill: end the running job; -1 if none */
u32 agent_ps(char *buf, u32 max);               /* "job SEQ STATE COMMAND" lines */

/* The VM thread's side (trap context). */
struct agent_exec {
    const char *path, *t1, *t2;                 /* for the stub: EXEC path with the tail t1 t2 */
};
int agent_vm_next(u32 result, const char *comspec, struct agent_exec *x);  /* 1 EXEC, 0 wait, -1 leave */
int agent_vm_wake_pending(void);                /* a job or an exit is waiting for the halted stub */
int agent_vm_capturing(void);
void agent_vm_tick(u32 now);                    /* every kernel tick: the kills of a job being ended */
void agent_vm_int21(const struct trapframe *tf);
void agent_vm_int29(const struct trapframe *tf);

#endif
