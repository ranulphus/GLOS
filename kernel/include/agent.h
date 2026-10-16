/* The agent shell and output capture (kernel/dos/agent.c; supervisor.md
 * §17.3, §17.4). The ssh thread posts DOS commands and empties their
 * output; the VM thread runs them through the resident stub. */
#ifndef K_AGENT_H
#define K_AGENT_H
#include "types.h"
#include "dos.h"

struct trapframe;
struct glos_run;

/* The ssh thread's side. owner is its channel. */
void agent_init(void (*kick_ssh)(void));       /* kick_ssh: output or an end is waiting */
int agent_post(void *owner, const char *cmd);   /* 0, or -1 when every job slot is taken */
int agent_post_run(void *owner, const char *cmd, int direct, int how, const char *profile);    /* glos run's
                                                   (how: 0, 1 --exclusive, 2 --app) */
void agent_drop(void *owner);                   /* the channel has gone */
void agent_service(u32 (*write)(void *owner, int stream, const u8 *d, u32 n),
                   void (*done)(void *owner, u32 code));       /* code: the exit status */
void agent_exit(u32 delay_ticks);               /* glos exit: leave once idle, this many ticks on */
int agent_kill(void);                           /* glos kill: end the running job; -1 if none */
int agent_kill_seq(u32 seq);                    /* glos kill SEQ: that job, running or waiting; -1 if none */
int agent_busy(void);                           /* a job runs or waits */
u32 agent_ps(char *buf, u32 max);               /* "job SEQ STATE COMMAND" lines */

/* Apps' jobs (GLOS_FN_RUN, GLOS_FN_JOB; M9 step C), on the app's thread. app: its struct app. */
int agent_post_app(void *app, const struct glos_run *r, u32 *seq);    /* 0, or a GLOS_E_ */
int agent_app_read(void *app, u32 seq, u8 *out, u32 room, u32 wait_ticks, u32 *state, u32 *code);   /* bytes, or -1 */
int agent_app_type(void *app, u32 seq, const u16 *keys, u32 n);       /* the keys taken, or -1 */
int agent_app_kill(void *app, u32 seq);
int agent_app_forget(void *app, u32 seq);
void agent_app_gone(void *app);                 /* the app has ended: its jobs killed (or let go, if apps now) */
u32 agent_jobs_stat(void *out, u32 room);       /* struct glos_stat_app for each job */

/* The VM thread's side (trap context). */
struct agent_exec {
    const char *path, *t1, *t2;                 /* for the stub: EXEC path with the tail t1 t2 */
    struct dos_regs dos;                        /* or an INT 21h call to make first (a job's drive and folder) */
    const char *dos_path;                       /* ... with this string at DS:DX (the stub's path buffer) */
};
int agent_vm_next(u32 result, const char *comspec, struct agent_exec *x);  /* 1 EXEC, 0 wait, -1 leave, 3 the
                                                   DOS call in x->dos (its answer comes back as result FFFFFFFEh) */
int agent_vm_console(void);                     /* a console job's program runs, in its window still */
void agent_vm_console_full(void);               /* ... and wants the screen: it is a session now */
void agent_vm_console_char(u8 c);               /* its BIOS teletype output */
int agent_vm_wake_pending(void);                /* a job or an exit is waiting for the halted stub */
int agent_vm_capturing(void);
void *agent_vm_job(void);                       /* the running job, or 0 */
void *agent_vm_as(void *job);                   /* output is this job's from now (an app's); returns the one before */
void agent_app_done(void *job, u32 result);     /* the app that job became has ended */
void agent_vm_tick(u32 now);                    /* every kernel tick: the kills of a job being ended */
void agent_vm_int21(const struct trapframe *tf);
void agent_vm_int29(const struct trapframe *tf);

#endif
