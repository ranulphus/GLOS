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
    V1_EV_CLOSE = GLOS_EV_CLOSE, V1_EV_WHEEL = GLOS_EV_WHEEL, V1_EV_JOB = GLOS_EV_JOB,
    V1_RUN_CONSOLE = GLOS_RUN_CONSOLE, V1_RUN_DETACH = GLOS_RUN_DETACH, V1_RUN_FULL = GLOS_RUN_FULL
};
#undef GLOS_EV_JOB
#undef GLOS_RUN_FULL
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
#define A2_TARGETS 16                   /* an app's windows that take drops at once */

struct timer2 {
    u32 handle;
    u32 ms;                             /* the period as the app gave it (GET gives this back) */
    u32 period, next;                   /* ticks (1024 to the second): the period, and when it fires next */
    u32 count, missed;
    u8 repeat, pending, live;           /* pending: an event of its is queued, not yet taken */
    u8 fired;                           /* it has fired since THREAD's WAIT last looked at it */
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
    u32 in_flags;                       /* GLOS_INPUT_: how it takes its keys (INPUT's MODE) */
    char watch[GLOS_WATCH_MAX][32];     /* the sections whose changes it is told of (SETTINGS' WATCH) ... */
    u8 nwatch, watch_all;               /* ... or every section's */
    struct {                            /* its windows that take drops (DND's TARGET) */
        u32 win;                        /* the window's number; 0: the bare desktop */
        u32 effects, n;
        u32 format[GLOS_CLIP_ITEMS];
    } target[A2_TARGETS];
    u32 ntargets;
    u8 pads_events;                     /* it asked for GLOS_EV_PAD */
    u8 log_follow;                      /* it follows the log (SYS's LOG): GLOS_EV_LOG when it has more ... */
    u32 log_told;                       /* ... than this place, the last it read to or was told of */
    struct glos_pad pad[GLOS_PADS];     /* each pad as it was last told of it */
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
#define A2_RESUME 0x52455355u           /* EDX: a wait cut short by an interrupt of the app's, ECX its ms left */
u32 a2_ticks_of(u32 ms);                                        /* milliseconds as ticks (1024 to the second) */
u32 a2_ms_of(u32 ticks);
u32 a2_wait_ms(struct trapframe *tf, u32 ms);                   /* a wait's time: the structure's, or what was left */
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
int api2_screen(struct trapframe *tf);

/* api2thr.c */
extern const struct propdef api2_thr_props[];
extern const u32 api2_thr_nprops;
int api2_thread(struct app *a, struct trapframe *tf, u32 n);    /* THREAD's calls */
void api2_thread_close(struct app *a, int kind, void *obj);     /* CLOSE of a thread's, a lock's or a signal's handle */

/* api2clip.c */
extern const struct propdef api2_clip_props[];
extern const u32 api2_clip_nprops;
int api2_clip(struct app *a, struct trapframe *tf, u32 n);     /* CLIP's calls */
int api2_dnd(struct app *a, struct trapframe *tf, u32 n);      /* DND's */
void api2_clip_close(struct app *a, void *obj, int commit);    /* CLOSE of a transaction's handle */
void api2_dnd_gone(struct app *a, u32 win);                     /* an app (win 0), or a window of its, is no more */
void api2_clip_init(void);                                      /* (once: the clipboard's changes told from now) */

/* api2fs.c */
int api2_fs(struct app *a, struct trapframe *tf, u32 n);       /* FS's calls */
void api2_fs_close(struct app *a, int kind, void *obj, int ending);    /* CLOSE of a listing's or a watch's handle */

/* api2shell.c */
extern const struct propdef api2_shell_props[];
extern const u32 api2_shell_nprops;
int api2_shell(struct app *a, struct trapframe *tf, u32 n);    /* SHELL's calls */
void api2_shell_close(struct app *a, int kind, void *obj);     /* CLOSE of a tray icon's or a notification's handle */
void api2_shell_gone(struct app *a);                            /* the app is no more: its icons, its notifications */
void api2_shell_1x(struct app *a, u32 type, int x, int y);     /* the desktop's launcher and task-list events */
void api2_shell_news(unsigned what);                            /* GLOS_EV_SHELL to the shell: entries, recents */
void api2_shell_changed(const char *section);                   /* a setting changed: the shell may be told */
/* api2start.c: the Start menu's data and the schedule */
int api2_start(struct app *a, struct trapframe *tf, u32 fn);   /* ENTRY, RECENT, SCHEDULE */
void api2_recent_add(const char *prog, const char *path);       /* a document opened, by this program */
/* api2fs.c, for it */
int api2_fs_path(struct trapframe *tf, u32 off, char *out, u32 room);  /* a path of the app's as DOS's bytes: its
                                                                           length, or -1 */
int api2_fs_exists(struct app *a, const char *path);            /* 1 a file, 2 a folder, 0 neither, -1 the app ends */

/* api2audio.c */
extern const struct propdef api2_audio_props[];
extern const u32 api2_audio_nprops;
int api2_audio(struct app *a, struct trapframe *tf, u32 n);    /* AUDIO's calls */
void api2_audio_close(struct app *a, void *obj);                /* CLOSE of a stream's handle */
void api2_audio_tick(struct app *a);                            /* each tick: a stream below its NOTIFY */

/* api2sys.c */
int api2_sys(struct app *a, struct trapframe *tf, u32 n);      /* SYS's calls */
void api2_sys_tick(struct app *a);                              /* each tick: the log's followers */
void api2_end_event(struct app *a);                             /* GLOS_EV_END: the app is asked to end */

/* api2job.c */
int api2_job(struct app *a, struct trapframe *tf, u32 n);      /* JOB's calls */
void api2_job_close(struct app *a, void *obj);                  /* CLOSE of a job's handle */
void api2_job_event(struct app *a, u32 seq, u32 state, u32 code);      /* the agent's news of a job */

/* api2set.c */
int a2_text_in(struct trapframe *tf, u32 off, char *buf, u32 room);    /* a text of the app's, its NUL kept: its
                                                                           length, or -1 (none, unreadable, too long) */
int a2_text_out(struct trapframe *tf, u32 off, u32 room, const char *text, u32 len);   /* 0, GLOS_E_ROOM (cut), or
                                                                                           GLOS_E_ARGUMENT */
int api2_settings(struct app *a, struct trapframe *tf, u32 n); /* SETTINGS' calls */
int api2_idle(struct app *a, struct trapframe *tf, u32 n);     /* IDLE's */
void api2_settings_init(void);                                  /* (once: changes' and the saver's news from now) */

/* api2in.c */
int api2_input(struct app *a, struct trapframe *tf, u32 n);     /* INPUT's calls */
void api2_input_init(void);                                     /* (once: the pads' news comes from now on) */
int api2_win_close(struct app *a, u32 id);                      /* CLOSE of a window's handle */

#endif
