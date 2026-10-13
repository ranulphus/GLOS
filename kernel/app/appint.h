/* GLOS apps: what kernel/app's files share (app.c, dev.c). */
#ifndef K_APPINT_H
#define K_APPINT_H
#define GLOS_KERNEL
#include "glos/api.h"
#include "app.h"
#include "dpmi.h"
#include "vpic.h"

#define MAX_APPS 16
#define SPARE    160u                           /* the spare parent frame: this far below the kill stack's top */

enum { AC_NONE, AC_RM, AC_END };

struct cwd {
    u8 known, drive;
    char dir[66];                               /* as INT 21h 47h gives it: no drive, no first backslash */
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
    } c;
    int result;
    u8 child_have;                              /* a program it ran has ended: INT 21h 4Dh's answer is kept for it */
    u16 child;                                  /* (DOS has one for everybody, and another app may end first) */
    u32 code;                                   /* its exit code; then INT 21h 4Dh's AX */
    struct waitq wq;                            /* its thread: for the call, in a sleep */
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
    struct glos_event evq[32];                  /* events, for the API */
    u32 evh, evt;
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
};

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
