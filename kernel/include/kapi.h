/* The kernel's own names for what passes between the desktop, the agent and
 * the apps' code (kernel/wm, kernel/dos, kernel/app): an event as the
 * desktop makes it, a window as the desktop describes it, a program as the
 * agent is asked to run it, a program as the figures list it.
 *
 * These were API 1.x's structures and numbers (include/glos/api.h) until
 * phase B's step 15 took that API away: the kernel had grown up speaking
 * them, and keeps them as its own. No program sees them. The API's own
 * (errors, states, the events an app is given) are in glos/api.h, which
 * this brings; kernel/app/api2win.c makes an app's events of these. */
#ifndef K_KAPI_H
#define K_KAPI_H
#include "glos/api.h"

/* An event, as the desktop (and the agent, the watches) gives it to an app: app_event(). */
#define KEV_KEY      1          /* a key went down or up while a window of the app's had the focus */
#define KEV_MOUSE    2          /* the pointer moved in a window's client area, or a button changed there */
#define KEV_FOCUS    3          /* the window got the focus (down 1) or lost it (0) */
#define KEV_RESIZE   4          /* the window's pixels are elsewhere now */
#define KEV_CLOSE    5          /* the user asked for the window to be closed */
#define KEV_LAUNCH   6          /* (the shell's) the launcher was asked for */
#define KEV_SYSKEY   7          /* (the shell's) Ctrl-Alt-Del: the task list */
#define KEV_JOB      8          /* a job of the app's moved on: key its number, x its state, y its exit code */
#define KEV_FILES    9          /* a watched folder changed: key the watch's id */
#define KEV_WHEEL    11         /* the wheel turned over a window: key the notches (signed) */
/* (kernel/include/desk.h goes on from here: DESK_EV_STATE and the rest, the desktop's news) */

struct kevent {
    unsigned size;              /* (a mouse event on its way in: the pointer's movement, dx) */
    unsigned type;              /* KEV_ */
    unsigned window;            /* the window's id */
    unsigned time;              /* milliseconds since GLOS started (a mouse event on its way in: dy) */
    int x, y;                   /* MOUSE: the pointer in the client area; RESIZE: its new width and height */
    unsigned buttons;           /* MOUSE: the buttons down now (1 left, 2 right, 4 middle) */
    unsigned key;               /* KEY: the key, as a set 1 scan code (100h added for those with an E0 prefix);
                                   MOUSE: the button that changed (as in `buttons`), 0 for a movement */
    unsigned ch;                /* KEY: its character (Unicode), 0 if it has none */
    unsigned mods;              /* GLOS_MOD_: what was held */
    unsigned down;              /* KEY, MOUSE's button: pressed (1) or released (0); FOCUS: got or lost */
};

/* A window of an app's, as the desktop takes and describes it (kernel/wm/deskapp.c). */
#define KWIN_FIXED    1         /* its size is not the user's to change */
#define KWIN_BARE     2         /* no title bar or border */
#define KWIN_ANYWHERE 4         /* where it goes is the desktop's to choose */
#define KWIN_POPUP    8         /* a menu's: bare, above every other window, no button on the task bar */
#define KWIN_DESKTOP  16        /* (the shell's) bare, under every other window, the screen less the task bar */

struct kwindow {
    unsigned size;
    unsigned id;                /* out */
    int x, y, w, h;             /* in: where and how large; out: the client area's size (the screen's) */
    unsigned flags;             /* KWIN_ */
    char title[48];
    unsigned pixels;            /* out: the linear address of its pixels, mapped into the app (0: none) */
    unsigned pitch;             /* out: bytes from a row to the next */
    unsigned bpp;               /* out: 8 (an index into the GLOS palette), 16 or 32 */
    unsigned char rpos, rsize, gpos, gsize, bpos, bsize, pad[2];        /* out: 16 and 32 bits: each field */
};

struct krect {
    int x, y, w, h;
};

/* A program for the agent to run (kernel/dos/agent.c: agent_post_job()). */
#define KRUN_CONSOLE 1          /* its console is the asker's: its output kept to be read, its keys typed by it */
#define KRUN_DETACH  2          /* nothing of it is the asker's */
#define KRUN_FULL    4          /* full-screen from the start */

struct krun {
    unsigned size;
    unsigned flags;             /* KRUN_ */
    char command[256];          /* a program and its arguments, or a line for COMMAND.COM */
    char dir[80];               /* the drive and folder it starts in ("": the system's) */
    char env[512];              /* NAME=VALUE strings, each NUL-ended, then a NUL: added to its environment */
};

/* A program in the figures' list (kernel/app/stat.c: stat_apps()). */
struct kprog {
    unsigned size;
    unsigned kind;              /* 0 an app, 1 a job, 2 the session */
    unsigned id;                /* an app's id or a job's */
    unsigned busy_ms;           /* the processor's time it has had */
    unsigned mem_kb, windows;
    unsigned state;             /* an app: 0 running, 1 paused (a session has the machine); a job: GLOS_JS_ */
    char name[16];
};

#endif
