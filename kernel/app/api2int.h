/* API 2.0 inside the kernel: what api2.c (CORE, the dispatcher, events) and
 * the areas' files (api2win.c: WIN, DRAW and the desktop's events) share.
 *
 * Until phase B's step 15 the kernel has API 1.x's header as well (appint.h
 * brings it: an app's queue of 1.x events, the desktop's events). The two
 * give some names different meanings: 1.x's are kept here as V1_, and
 * glos/api2.h's are the ones a file that includes this sees. */
#ifndef K_API2INT_H
#define K_API2INT_H
#include "appint.h"

enum {
    V1_EV_KEY = GLOS_EV_KEY, V1_EV_MOUSE = GLOS_EV_MOUSE, V1_EV_FOCUS = GLOS_EV_FOCUS, V1_EV_RESIZE = GLOS_EV_RESIZE,
    V1_EV_CLOSE = GLOS_EV_CLOSE, V1_EV_WHEEL = GLOS_EV_WHEEL
};
#undef GLOS_EV_KEY
#undef GLOS_EV_MOUSE
#undef GLOS_EV_FOCUS
#undef GLOS_EV_RESIZE
#undef GLOS_EV_CLOSE
#undef GLOS_EV_WHEEL
#include "glos/api2.h"
#include "handle.h"

#define A2_QBYTES  16384u               /* an app's queued events */
#define A2_TIMERS  32                   /* an app's timers at once */

struct timer2 {
    u32 handle;
    u32 ms;                             /* the period as the app gave it (GET gives this back) */
    u32 period, next;                   /* ticks (1024 to the second): the period, and when it fires next */
    u32 count, missed;
    u8 repeat, pending, live;           /* pending: an event of its is queued, not yet taken */
    char name[32];
};

struct api2 {
    struct htab h;
    struct timer2 *timers[A2_TIMERS];
    u32 ntimers;
    u8 q[A2_QBYTES];
    u32 qh, qt;                         /* bytes taken and bytes put, ever growing (the ring's offsets mod A2_QBYTES) */
    u32 last_at;                        /* where the event put last begins ... */
    u8 last_merge;                      /* ... and whether one like it may take its place (a mouse's movement) */
    u32 ndrop, ndrop1x;                 /* events lost to a full queue; 1.x events with no 2.0 form yet */
    char name[32];                      /* INIT's */
    char help[32];
    char category[32];
};

/* A property of a kind of handle. */
#define P_NUM  1
#define P_TEXT 2
#define P_RO   4
#define P_DATA 8                        /* bytes, up to 1039 (text: up to 255 and a NUL) */
struct propdef {
    u32 prop;
    u32 flags;
    int (*get)(struct app *a, void *obj, s32 *v, char *text, u32 *len);        /* text: up to 256 bytes; 0, or GLOS_E_ */
    int (*set)(struct app *a, void *obj, s32 v, const char *text, u32 len);
};

/* api2.c */
int a2_fail(struct trapframe *tf, u32 e);                       /* the call's answer: refused */
int a2_ok(struct trapframe *tf, u32 v);                         /* ... done, EAX v */
int a2_rd(struct trapframe *tf, void *s, u32 n, u32 *size);     /* the call's structure, as far as GLOS knows it */
int a2_wr(struct trapframe *tf, const void *s, u32 n, u32 size);        /* ... and back, no larger than it came */
int a2_post(struct app *a, struct glos_ev *ev, int merge);      /* an event for the app (any thread, interrupts on
                                           or off): 0, or -1 (its queue is full). merge: it takes the place of the
                                           event queued last, if that was one of its kind for the same handle
                                           posted so too */

/* api2win.c */
extern const struct propdef api2_win_props[];
extern const u32 api2_win_nprops;
int api2_win(struct app *a, struct trapframe *tf, u32 n);       /* WIN's calls */
int api2_draw(struct app *a, struct trapframe *tf, u32 n);      /* DRAW's */
int api2_screen(struct trapframe *tf);                          /* DRAW's SCREEN, for any program */
int api2_win_close(struct app *a, u32 id);                      /* CLOSE of a window's handle */

#endif
