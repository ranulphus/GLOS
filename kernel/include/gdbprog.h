/* glos gdb: gdb's remote protocol for the program in front (kernel/dbg/gdbprog.c). */
#ifndef K_GDBPROG_H
#define K_GDBPROG_H
#include "types.h"

struct trapframe;
struct thread;

#define GDBP_PORT 2345                  /* the port an ssh -L or -W asks for */

/* The thread the program runs on (the VM thread, or an app's first), interrupts off. */
int gdbp_trap(struct trapframe *tf);    /* a client's #DB or INT3: 1 if the debugger took it */
void gdbp_return(struct trapframe *tf); /* on the way back to a client: stops it when asked to */
void gdbp_became_app(struct thread *t, void *app, u32 job);     /* the VM thread's program is an app now */
void gdbp_app_end(void *app, u32 code); /* an app has ended */
void gdbp_job_end(u32 job, u32 code);   /* a job has ended (the agent's word, with its exit code) */
/* API 2.0's DEBUG (kernel/app/api2dbg.c). */
int gdbp_arm_job(u32 job);              /* the job stops at its program's first 32-bit code, for its debugger */
int gdbp_attach_job(u32 job, struct thread *t, void *app, void (*send)(const u8 *d, u32 n), void (*wake)(void));
void gdbp_forget_job(u32 job);          /* its owner let go without attaching: it runs on */
int gdbp_stopped(void);                 /* the program waits for gdb */
void gdbp_program_end(u32 code);        /* its program has ended */

/* The ssh thread. */
void gdbp_arm(void (*wake)(void));      /* glos gdb: stop the program when it next runs its 32-bit code */
int gdbp_armed(void);                   /* armed, stopped or attached */
int gdbp_attach(void (*send)(const u8 *d, u32 n));      /* gdb's channel has opened: 0, or -1 if there is no one to debug */
void gdbp_input(const u8 *d, u32 n);    /* its bytes */
void gdbp_turn(void);                   /* each turn: a stop gdb waits to hear of */
void gdbp_detach(void);                 /* the channel has gone, or gdb let go: the program runs on */

#endif
