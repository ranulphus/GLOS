/* GLOS apps: what kernel/app's files share (app.c, dev.c). */
#ifndef K_APPINT_H
#define K_APPINT_H
#define GLOS_KERNEL
#include "glos/api.h"
#include "app.h"
#include "dpmi.h"
#include "gpu.h"
#include "vpic.h"

#define MAX_APPS 16
#define APP_THREADS 16                          /* threads an app may make beside its first (GLOS_THREADS) */
#define SPARE    160u                           /* the spare parent frame: this far below the kill stack's top */

enum { AC_NONE, AC_RM, AC_END, AC_BATCH };

struct cwd {
    u8 known, drive;
    char dir[66];                               /* as INT 21h 47h gives it: no drive, no first backslash */
};

/* A thread the app made (API 2.0's THREAD: app.c, api2thr.c; supervisor.md 12.7). */
struct app_thr {
    struct thread *t;                           /* while it lives */
    u8 used;                                    /* the record is taken: until it has ended and its handle is closed */
    u8 live;                                    /* its kernel thread is there */
    u8 bulk;                                    /* the background class */
    u8 fpu_err;                                 /* an FPU error of its own waits (IRQ 13): the app's end */
    u32 handle;
    u32 code;                                   /* EXIT's */
    u32 ticks;                                  /* what it ran, kept at its end */
    struct trapframe start;                     /* where it begins */
    char name[12];                              /* app<id>.<number> */
};

struct app {
    struct app *next;                           /* every app */
    struct app *qnext;                          /* those waiting for the VM thread */
    u32 id;
    char tname[8];                              /* its thread's name: app<id> */
    char prog[13];
    struct dpmi_ctx *ctx;                       /* 0 once it has ended */
    struct thread *thread;
    struct trapframe start;                     /* where its thread begins: the return of its init call */
    void *job;                                  /* the agent's job it began as, or 0 */
    u16 psp;
    u16 api_minor;                              /* the API's minor version it was built for (INIT's) */
    u8 api_major;                               /* 1 (glos/api.h) or 2 (glos/api2.h): the calls it may make */
    u8 own;                                     /* its program is in GLOS's own folder: it may be the shell */
    char folder[80];                            /* the folder its file is in ("C:\\GLOS") */
    u32 wake;                                   /* counts every event given it, of either API (app_sleep's) */
    u32 ev_since;                               /* the tick its queue last got its first event, or it last took one */
    u8 ev_taken;                                /* it has taken an event at least once (it reads its queue) */
    struct api2 *v2;                            /* API 2.0's: its handles, timers and events (api2.c) */
    u32 dta;
    struct cwd cwd;
    u32 ivt[256];                               /* the real-mode vectors it changed ... */
    u8 ivt_mine[32];                            /* ... a bit for each */
    volatile u8 kill;
    u8 adopted;                                 /* the VM thread is making a call for it */
    u8 dead;                                    /* DOS has ended its process */
    u8 call;                                    /* AC_: what it waits for the VM thread with */
    struct {
        struct trapframe *from;
        struct rmregs *r;
        int kind;
        u8 vec;
        const u16 *words;
        u32 nwords;
        void (*batch)(struct app *a, void *arg);        /* AC_BATCH: what the VM thread runs for it ... */
        void *batch_arg;
        u8 bios;                                /* ... which makes BIOS calls too, not DOS's alone */
    } c;
    int result;
    u8 child_have;                              /* a program it ran has ended: INT 21h 4Dh's answer is kept for it */
    u16 child;                                  /* (DOS has one for everybody, and another app may end first) */
    u32 code;                                   /* its exit code; then INT 21h 4Dh's AX */
    struct waitq wq;                            /* its threads: for the call, in a sleep */
    /* its own threads (12.7) */
    struct app_thr thr[APP_THREADS];
    u8 nthr;                                    /* those alive */
    u8 parking;                                 /* the VM thread is about to free its context, in a call it made for one
                                                   thread: the others leave, or wait where they touch it no more */
    u8 parked;                                  /* its first thread is waiting so */
    u8 end_asked, end_killed;                   /* another thread asked for the app's end (its exit, its crash) ... */
    u8 end_code;                                /* ... with this code */
    struct thread *dos_owner;                   /* the thread whose DOS or BIOS call is under way (the DOS lock) */
    u32 first_wants;                            /* the first thread waits for such a call's end (an interrupt of its
                                                   own, an exception): since this tick, 0 not. No other is begun */
    struct thread *api_owner;                   /* the thread making a GLOS call (the API lock) */
    struct waitq tq;                            /* its threads' waits for one another: the locks, a thread's end */
    u32 t0, ncalls;
    /* its devices (dev.c) */
    struct vpic pic;
    struct {                                    /* PIT channel 0 */
        u8 mode;                                /* 2 or 3 */
        u8 access;                              /* 1 low byte, 2 high, 3 low then high */
        u8 wr_hi, rd_hi;                        /* the byte a write or a read is at */
        u8 latched;                             /* bytes of `latch` still to be read */
        u8 own;                                 /* it has programmed it: no longer the PC's, as GLOS keeps that */
        u8 wr_lo;
        u16 latch;
        u16 last;                               /* the count it last read, in the BIOS tick `last_tick` */
        u32 last_tick;
        u32 reload;                             /* 1 to 65536 */
        u32 acc, frac;                          /* counts into the period (and 1024ths of one) ... */
        u32 at;                                 /* ... at this tick */
    } pit;
    u8 p61, rtc_index;
    u32 nirq[16];                               /* interrupts given to it, by line */
    /* its input (dev.c): what the desktop gave it while a window of its own had the focus. An app that has
       made a window reads this keyboard and this mouse; one that never has reads the PC's, with DOS. */
    u8 gui;
    struct glos_event evq[256];                 /* events, for the API (32 lost a 486's typed keys, M9) */
    u32 evh, evt;
    u32 ndrop;                                  /* events lost to a full queue */
    struct {
        u8 byte;                                /* a scan code, for port 60h ... */
        u16 key;                                /* ... and with a key's last byte, the key as the BIOS keeps it (0: none) */
    } kb_q[32];
    u32 kb_qh, kb_qt;
    u8 kb_out, kb_full;                         /* the byte port 60h holds, and whether it has been read */
    u16 kb_out_key;
    u16 kb_keys[16];                            /* keys as the BIOS has them (scan code, character), for INT 16h */
    u32 kb_kh, kb_kt;
    u8 kb_shift;                                /* the BIOS's shift flags: right shift, left shift, Ctrl, Alt */
    s32 ms_x, ms_y, ms_dx, ms_dy;               /* the mouse, for INT 33h: where, and how far since last asked */
    u8 ms_buttons;
    struct gpu_app gpu;                         /* its use of the Matrox card (kernel/gfx/gpu.c, M8) */
    struct {                                    /* COM1 as it sees it (dev.c, M8): its lines into GLOS's log */
        char line[160];
        u8 len, lcr, ier, mcr, scr;
    } com;
};

/* api2.c: API 2.0 (include/glos/api2.h) beside 1.x (phase B, docs/v1/phase-b.md). */
int api2_call(struct trapframe *tf);            /* a 2.0 call (EAX's major 2): glos_api's answer */
int api2_app_new(struct app *a);                /* a 2.0 app's state, before its thread runs: 0, or -1 */
void api2_app_gone(struct app *a);              /* its handles closed, its state freed (its thread, at its end) */
void api2_tick(struct app *a);                  /* its timers, every kernel tick (interrupts off) */
void api2_end_event(struct app *a);             /* (api2sys.c) GLOS_EV_END: the app is asked to end */
void api2_net_event(struct app *a, u32 id, u32 state, u32 waiting, u32 room);  /* (api2net.c) GLOS_EV_NET for a
                                                   connection of a 2.0 app's (the net thread; interrupts off) */

void api2_event_1x(struct app *a, const struct glos_event *e);  /* the desktop's events for a 2.0 app */
int api2_pending(const struct app *a);          /* events wait in its 2.0 queue */

/* app.c: an app's own threads. */
int app_first(void);                            /* the running thread is an app's first */
struct app_thr *app_thr_cur(struct app *a);     /* the running thread's record, or 0 (the first has none) */
u32 app_thr_number(struct app *a);              /* its number: 0 the first, 1 to APP_THREADS */
int app_thread_start(struct app *a, struct app_thr *r, const struct trapframe *from, u32 cs, u32 data, u32 entry,
                     u32 arg, u32 stack, int bulk);     /* data: its DS, ES and SS (0: the caller's). 0, or -1 no
                                                   memory, -2 the stack can't be written, -3 the app is ending */
void app_thread_exit(struct app *a, u32 code) __attribute__((noreturn));    /* the running thread (not the first) */
int app_thread_wait(struct app *a, u32 until);  /* a wait for another thread's doing: 0 look again, 1 the tick
                                                   `until` came (0: none), 2 the app is ending, 3 an interrupt of
                                                   the first thread's is due */
void app_thread_news(struct app *a);            /* ... which this wakes: a lock given, a signal set, a handle closed */
/* A batch: DOS and BIOS calls made for the app by the VM thread in one turn, in the app's own DOS context (its
   PSP, its drive and directory), so that a call of GLOS's that needs many of DOS's costs one turn of the VM
   thread, not one each (FS's LIST: api2fs.c). On one of the app's threads: fn has run when this returns 0; -1:
   the app is ending. bios: fn makes BIOS calls too (it then waits for a moment when DOS isn't in its idle call). */
int app_batch(struct app *a, void (*fn)(struct app *a, void *arg), void *arg, int bios);
struct rmregs;
int app_batch_int(u8 vec, struct rmregs *r);    /* in fn: a real-mode interrupt (r: in and out). 0, or -1: not made
                                                   (DOS has ended the program meanwhile: fn stops) */
int app_api_lock(struct app *a);                /* the app's calls one at a time: 0, or -1 (it is ending) */
void app_api_unlock(struct app *a);
void api2_thread_gone(struct app *a, u32 number);       /* api2thr.c: a thread has ended: the locks it held are free */

/* dev.c: its keyboard and mouse as a DOS program reads them. */
void appdev_key(struct app *a, u32 key, int down, u32 ch, u32 mods);
void appdev_mouse(struct app *a, s32 x, s32 y, s32 dx, s32 dy, u32 buttons);
int appdev_int16(struct app *a, struct trapframe *from, struct rmregs *r);      /* 1: answered */
int appdev_int33(struct app *a, struct rmregs *r);


/* dev.c: an app's own devices. */
void appdev_init(struct app *a);
void appdev_tick(struct app *a);        /* every kernel tick while it isn't paused (interrupts off) */
void appdev_irq0(struct app *a);        /* the PC's timer interrupted */
void appdev_irq0_at(u32 tick);          /* ... at this tick (every time, apps or not) */

#endif
