/* GLOS apps (kernel/app/app.c, api.c; PRD §5.4, §6.8, §10; M6). */
#ifndef K_APP_H
#define K_APP_H
#include "arch.h"
#include "sched.h"

struct app;
struct rmregs;

#define NEXT_DETACHED 0xFFFFFFFDu       /* the stub's NEXT: the program it EXECed runs on as an app */

/* The running thread's app, or 0 (the VM thread, a kernel thread). */
struct app *app_cur(void);
void app_each(void (*fn)(struct app *a, void *arg), void *arg); /* every app that runs, interrupts off: fn is brief */
int app_stalled(const struct app *a);    /* events have waited 5 seconds for an app that reads its queue */
struct app *app_pending(void);          /* the app app_init made, until its program's call returns */

/* api.c: the entry behind DPMI 0A00h "GLOS". 1: the program becomes an app
   (app_detach() once its far call has returned). */
int glos_api(struct trapframe *tf);

/* On the VM thread, from the program's trap. */
int app_init(struct trapframe *tf, u32 major, u32 minor, u32 *id);      /* the checks and the memory: 0, or a
                                           GLOS_INIT_ reason (the same numbers as 2.0's GLOS_WHY_) */
void app_detach(struct trapframe *tf);  /* the frame goes to the app's thread; this one to the stub */
int app_force(struct trapframe *tf);    /* glos run --app: at a return to the session's client; 1: it is an app now */

/* On an app's thread, from its traps (the DPMI host). */
int app_rm_call(struct trapframe *from, struct rmregs *r, int kind, u8 vec, const u16 *words, u32 nwords);
                                        /* rm_call(), made by the VM thread when the system VM is idle */
void app_end(struct app *a, u32 code) __attribute__((noreturn));
                                        /* its context has gone: DOS ends the process, the thread ends */
void app_return(struct app *a, struct trapframe *tf);   /* before every return to its code */
/* An app's own threads (API 2.0's THREAD; supervisor.md 12.7). */
int app_first(void);                    /* the running thread is an app's first (not one the app made) */
u32 app_thread_number(void);            /* the running thread's number in its app: 0 the first (or no app's) */
int app_exception(struct trapframe *tf);        /* an exception with a handler of the app's: 1 to give it now */
void app_stop_threads(struct app *a);   /* its first thread, before its context goes: the others leave */
void app_ctx_ending(struct app *a);     /* its context is about to be freed (the VM thread: the others out first) */
void app_end_from_thread(struct app *a, u8 code, int killed) __attribute__((noreturn));
                                        /* a thread the app made ends the app (its first thread does), and is gone */
u32 app_sleep(struct app *a, u32 ticks); /* the ticks left if an interrupt of its own (or a kill) ended it early */
void app_idle(struct app *a);           /* its HLT: until an interrupt of its own */
int app_irq_default(struct app *a, u8 vec);     /* an interrupt of its own nobody took: ended; 0: not one's vector */
u32 app_in(struct app *a, u16 port, int width);
void app_out(struct app *a, u16 port, u32 val, int width);
u32 app_ivt_get(u8 n);                  /* a real-mode vector as the running thread's program has it */
void app_ivt_set(u8 n, u32 v);

/* On the VM thread. */
void app_vm_turn(void);                 /* the stub's NEXT: the calls apps wait with */
void app_vm_idle_turn(int in_dos);      /* a console job waits for a key: the calls that are safe then */
int app_vm_pending(void);               /* one waits (the stub's halt ends) */
void app_vm_system(void);               /* before the system VM's own DOS work: DOS's state its own again */
void app_vm_base(int *depth, struct trapframe **f);     /* where an app's level 1 began, for the VM: its call */
int app_vm_ending(void);                /* the call under way is an app's end */
void app_vm_ended(struct trapframe *tf) __attribute__((noreturn));      /* DOS has ended the app's process */
u16 app_vm_psp(void);                   /* the PSP of the app the VM thread makes a call for, or 0 */

/* Events (app.c): the desktop gives them (e->size and e->time carry the
   pointer's movement for a mouse event on the way in), the API takes them. */
struct glos_event;
void app_event(struct app *a, const struct glos_event *e);
int app_event_get(struct app *a, struct glos_event *e);         /* 1 and the oldest, or 0 */
int app_event_waiting(const struct app *a);
u32 app_event_backlog(const struct app *a);     /* its events not yet read */

/* From interrupts. */
void app_tick(void);                    /* every kernel tick */
void app_irq0(void);                    /* the PC's timer's interrupt */
void app_fpu_irq(struct app *a);        /* IRQ 13 while the app's code ran: the FPU error is its own */

/* From anywhere (interrupts off). */
void app_kill(struct app *a, const char *why);
void app_kill_id(u32 id, const char *why);      /* the app with that id, if it still runs */
u32 app_id(const struct app *a);
int apps_paused(void);                          /* a session (or an app's child program) has the machine */
void app_kill_job(void *job);           /* the app that agent job began, if it still runs */
void app_kill_all(const char *why);
void app_session_end(void);             /* a session has ended: paused apps run on */
u32 app_count(void);
u32 app_stat_list(void *out, u32 room);         /* struct glos_stat_app for each (M9) */
u32 app_last_code(void);                /* how the last app to end did: INT 21h 4Dh's AX */
u32 app_ps(char *buf, u32 max);         /* glos ps: "app ID PROGRAM ..." lines */
u32 app_stat(char *buf, u32 max);       /* glos stat: a JSON array */

struct app *app_by_id(u32 id);          /* (interrupts off) the live app with this id, or NULL */
int app_own(const struct app *a);       /* its program is one of GLOS's own (in GLOS.EXE's folder) */

#endif
