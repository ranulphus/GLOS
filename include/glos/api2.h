/* The GLOS API, version 2.0 (docs/v1/api-2.md; phase B of docs/v1-route.md,
 * docs/v1/phase-b.md). Frozen as GLOS's v1 API when GLOS v1 ships; after that
 * it only grows: nothing changes meaning and nothing is taken away.
 *
 * During phase B the kernel answers API 1.x (glos/api.h) beside this. A
 * program is one or the other, by the major version it gives INIT. Step 15
 * of phase B removes 1.x, and this file becomes glos/api.h.
 *
 * Finding GLOS: DPMI function 0A00h with the name "GLOS" (INT 31h, AX=0A00h,
 * DS:ESI the name) gives the entry in ES:EDI, a far procedure, as in 1.x.
 * Plain DOS and other DPMI hosts have no such name.
 *
 * A call: EAX the function's number, EBX the offset in DS of the call's
 * structure (0 for the few calls that take none), ECX and EDX 0. The carry
 * flag clear: done, EAX 0 or what the call says it returns; the carry flag
 * set: refused, EAX a GLOS_E_ code.
 *
 * A call that waits may come back early with GLOS_E_AGAIN: an interrupt of
 * the program's own is due, and its handler runs as the call returns. The
 * call is then made again with ECX and EDX as they came back (the time left,
 * and a mark that says so): it goes on waiting for the rest. libglos does
 * this; the caller's structure is not changed meanwhile.
 *
 * Numbers: GLOS_FN(area, n), 20000h + the area times 256 + the function in
 * the area. The major version is in the number, so 2.0's calls never meet
 * 1.x's (0-33) and every call says which API it is. Each area grows at its
 * own end.
 *
 * Structures start with their size in bytes, which the caller sets. GLOS
 * reads as much as it knows of, and takes what a smaller structure lacks as
 * zero; it writes back no more than the structure's size. New fields only
 * ever go at a structure's end, so an older program's smaller structure still
 * works.
 *
 * Handles: what an app makes is a handle, a 32-bit number with its kind in
 * the top byte (GLOS_HANDLE_KIND). CORE's CLOSE closes any kind. When an app
 * ends, GLOS closes all of its handles. 0 is no handle.
 *
 * Properties: most of what a handle has is a property, read and set by CORE's
 * GET and SET by its number. A small addition later is a new property number,
 * not a new function.
 *
 * Features, not versions: VERSION gives, for each area, a bit for each of its
 * functions that this GLOS has. Programs test those, not version numbers.
 *
 * Events: one queue an app, every kind through it: a header (struct
 * glos_ev), then a part for the kind. Kinds are numbered by area, as
 * functions are.
 *
 * Text is UTF-8 everywhere.
 *
 * sdk/libglos2 is the C side of this for DJGPP (glos2.h). */
#ifndef GLOS_API2_H
#define GLOS_API2_H

#define GLOS_MAJOR 2
#define GLOS_MINOR 0

#define GLOS_FN(area, n) ((2u << 16) | ((unsigned)(area) << 8) | (unsigned)(n))
#define GLOS_FN_MAJOR(fn) ((unsigned)(fn) >> 16)
#define GLOS_FN_AREA(fn) (((unsigned)(fn) >> 8) & 0xFFu)

/* ---- areas */
#define GLOS_AREA_CORE     0x00 /* the app, handles, properties, events, timers, time */
#define GLOS_AREA_WIN      0x01 /* windows (phase B step 2) */
#define GLOS_AREA_DRAW     0x02 /* pixels, the screen, the GPU, DOS-GL's paths (step 2) */
#define GLOS_AREA_INPUT    0x03 /* keys, text, the mouse, pads (step 3) */
#define GLOS_AREA_SHELL    0x04 /* the tray, notifications, Start entries, associations (step 8) */
#define GLOS_AREA_CLIP     0x05 /* the clipboard (step 6) */
#define GLOS_AREA_DND      0x06 /* drag and drop (step 6) */
#define GLOS_AREA_THREAD   0x07 /* an app's own threads (step 4) */
#define GLOS_AREA_JOB      0x08 /* running programs (step 9) */
#define GLOS_AREA_FS       0x09 /* files and disks (step 7) */
#define GLOS_AREA_AUDIO    0x0A /* sound and MIDI (step 10) */
#define GLOS_AREA_NET      0x0B /* the network (step 11) */
#define GLOS_AREA_SETTINGS 0x0C /* GLOS.CFG, themes, the region (step 5) */
#define GLOS_AREA_IDLE     0x0D /* idle time, the screensaver, power (step 5) */
#define GLOS_AREA_SYS      0x0E /* what GLOS knows of the system (step 9) */
#define GLOS_AREA_DEBUG    0x0F /* a debug channel to a program the caller started (step 12) */
#define GLOS_AREA_CAPTURE  0x10 /* a child's pixels and input (step 12) */
#define GLOS_AREA_SECRETS  0x11 /* the secrets store (step 13) */
#define GLOS_AREA_MODULES  0x12 /* (no calls in 2.0: DXE3 modules are each program's own) */
#define GLOS_AREAS         0x13

/* ---- errors (EAX, the carry flag set) */
#define GLOS_E_FUNCTION    1    /* no such function */
#define GLOS_E_NOT_APP     2    /* for apps only: INIT first */
#define GLOS_E_ARGUMENT    3    /* an argument is wrong, or the structure is too small for what is needed */
#define GLOS_E_VERSION     4    /* INIT: another major version, or a call of the other major's */
#define GLOS_E_INIT        5    /* INIT refused: glos_init's `why` says why (GLOS_WHY_) */
#define GLOS_E_HANDLE      6    /* no such handle of the app's */
#define GLOS_E_MEMORY      7    /* no memory for it */
#define GLOS_E_KIND        16   /* a handle of another kind than the call wants */
#define GLOS_E_PROPERTY    17   /* no such property for that kind, or it can't be set */
#define GLOS_E_ROOM        18   /* the room given is too small; what was needed is in the structure */
#define GLOS_E_REFUSED     19   /* not this app's to do */
#define GLOS_E_AGAIN       20   /* a wait cut short for an interrupt of the program's own: the call again, with ECX
                                   and EDX as they came back */
#define GLOS_E_NOT_FOUND   21   /* no such file, program or page; or nothing is set to open that kind of file */
/* (8-15 are the areas' own, numbered as 1.x numbered them: 8 no GPU, 9 no audio, 10 a setting refused,
    11-15 the network's) */

/* ---- handles */
typedef unsigned glos_handle;
#define GLOS_HANDLE_KIND(h) ((unsigned)(h) >> 24)
#define GLOS_HK_TIMER      1
#define GLOS_HK_WINDOW     2
#define GLOS_HK_THREAD     3
#define GLOS_HK_LOCK       4
#define GLOS_HK_SIGNAL     5
#define GLOS_HK_JOB        6
#define GLOS_HK_STREAM     7
#define GLOS_HK_CONNECTION 8
#define GLOS_HK_WATCH      9
#define GLOS_HK_TRAY       10
#define GLOS_HK_NOTIFY     11
#define GLOS_HK_DEBUG      12
#define GLOS_HK_CLIP       13
#define GLOS_HK_AWAKE      14
#define GLOS_HK_LIST       15

/* ---- CORE's functions */
#define GLOS_CORE_VERSION  GLOS_FN(GLOS_AREA_CORE, 0)  /* struct glos_version, for any program */
#define GLOS_CORE_INIT     GLOS_FN(GLOS_AREA_CORE, 1)  /* struct glos_init: the program becomes an app */
#define GLOS_CORE_CLOSE    GLOS_FN(GLOS_AREA_CORE, 2)  /* struct glos_close: any handle */
#define GLOS_CORE_GET      GLOS_FN(GLOS_AREA_CORE, 3)  /* struct glos_prop: a handle's property */
#define GLOS_CORE_SET      GLOS_FN(GLOS_AREA_CORE, 4)  /* struct glos_prop */
#define GLOS_CORE_EVENT    GLOS_FN(GLOS_AREA_CORE, 5)  /* struct glos_wait: the next event, waiting at most so long */
#define GLOS_CORE_POST     GLOS_FN(GLOS_AREA_CORE, 6)  /* struct glos_post: an event of the app's own */
#define GLOS_CORE_TIMER    GLOS_FN(GLOS_AREA_CORE, 7)  /* struct glos_timer: a timer, its events on the queue */
#define GLOS_CORE_TICKS    GLOS_FN(GLOS_AREA_CORE, 8)  /* (EBX 0) -> EAX milliseconds since GLOS started; any program */
#define GLOS_CORE_SLEEP    GLOS_FN(GLOS_AREA_CORE, 9)  /* struct glos_sleep */
#define GLOS_CORE_YIELD    GLOS_FN(GLOS_AREA_CORE, 10) /* (EBX 0) the rest of the app's turn to others */
#define GLOS_CORE_TIME     GLOS_FN(GLOS_AREA_CORE, 11) /* struct glos_time; any program */
#define GLOS_CORE_RANDOM   GLOS_FN(GLOS_AREA_CORE, 12) /* struct glos_random; any program */

/* VERSION */
struct glos_version {
    unsigned size;
    unsigned major, minor;      /* the API's: 2, 0 */
    unsigned machine;           /* GLOS_MACHINE_: what the machine can do now */
    char build[16];             /* GLOS's build (git describe), NUL-ended */
    unsigned functions[GLOS_AREAS][2];   /* bit n of [area][n / 32]: the area's function n exists */
};
#define GLOS_MACHINE_GPU    1   /* a drawing engine apps can use (a Matrox card on the desktop) */
#define GLOS_MACHINE_GLWIN  2   /* DOS-GL in a window on the desktop */
#define GLOS_MACHINE_AUDIO  4   /* GLOS's mixer */
#define GLOS_MACHINE_NET    8   /* the network is up */
#define GLOS_HAS(v, fn) (((v)->functions[GLOS_FN_AREA(fn)][((fn) & 0xFFu) / 32] >> (((fn) & 0xFFu) % 32)) & 1u)

/* INIT */
struct glos_init {
    unsigned size;
    unsigned major, minor;      /* what the program was built for: GLOS_MAJOR, GLOS_MINOR */
    char name[32];              /* its name, shown to the user (UTF-8) */
    char help[32];              /* its page in Help, or "" */
    char category[32];          /* its Start menu group ("Accessories", "Games"), or "" */
    unsigned flags;             /* none yet: 0 */
    unsigned app;               /* -> its id */
    unsigned why;               /* -> INIT refused (GLOS_E_INIT): GLOS_WHY_ */
};
#define GLOS_WHY_ALREADY   1    /* it is an app already */
#define GLOS_WHY_NESTED    2    /* a child program's, or from a handler or a callback */
#define GLOS_WHY_16BIT     3    /* a 16-bit DPMI program */
#define GLOS_WHY_PARENT    4    /* GLOS didn't start it */
#define GLOS_WHY_EXCLUSIVE 5    /* it runs exclusively (glos run --exclusive, direct mode) */
#define GLOS_WHY_MEMORY    6

/* CLOSE */
struct glos_close {
    unsigned size;
    glos_handle handle;
};

/* GET and SET */
struct glos_prop {
    unsigned size;
    glos_handle handle;
    unsigned prop;              /* GLOS_PROP_ */
    int value;                  /* a number's value (in for SET, out for GET) */
    unsigned data, room;        /* text or data: the offset in DS of the program's room, and its size */
    unsigned length;            /* -> GET: the value's whole length (more than room: cut, GLOS_E_ROOM) */
};
#define GLOS_PROP(kind, n) (((unsigned)(kind) << 8) | (unsigned)(n))
#define GLOS_PROP_TIMER_MS     GLOS_PROP(GLOS_HK_TIMER, 0)   /* the period in milliseconds (read, write) */
#define GLOS_PROP_TIMER_REPEAT GLOS_PROP(GLOS_HK_TIMER, 1)   /* 1 repeating, 0 once (read, write) */
#define GLOS_PROP_TIMER_COUNT  GLOS_PROP(GLOS_HK_TIMER, 2)   /* times it has fired (read) */
#define GLOS_PROP_TIMER_NAME   GLOS_PROP(GLOS_HK_TIMER, 3)   /* the app's text for it, at most 31 bytes (read, write) */

/* EVENT */
struct glos_wait {
    unsigned size;
    unsigned ms;                /* the longest wait: 0 none (a look), GLOS_FOREVER */
    unsigned areas;             /* only events of these areas (bit n: area n), or 0 for all */
    unsigned buffer, room;      /* where the event goes: the offset in DS, and its size (GLOS_EV_MAX is always enough) */
    unsigned length;            /* -> the event's size, 0 if none came (GLOS_E_ROOM: what it needs; it stays queued) */
};
#define GLOS_FOREVER 0xFFFFFFFFu

/* Events: the header, then the kind's part. */
struct glos_ev {
    unsigned size;              /* of the whole event */
    unsigned short kind;        /* GLOS_EV_ */
    unsigned short flags;       /* none yet */
    glos_handle handle;         /* what it is about (a timer, a window), or 0 */
    unsigned time;              /* milliseconds since GLOS started */
};
#define GLOS_EV(area, n) ((unsigned short)(((unsigned)(area) << 8) | (unsigned)(n)))
#define GLOS_EV_AREA(kind) ((unsigned)(kind) >> 8)
#define GLOS_EV_MAX 256         /* no event is larger */

#define GLOS_EV_TIMER  GLOS_EV(GLOS_AREA_CORE, 1)       /* struct glos_ev_timer: a timer fired */
#define GLOS_EV_POST   GLOS_EV(GLOS_AREA_CORE, 2)       /* struct glos_ev_post: the app's own (POST) */
#define GLOS_EV_END    GLOS_EV(GLOS_AREA_CORE, 3)       /* (the header alone) GLOS asks the app to end: glos exit, a
                                                           shutdown, Task Manager's End */
struct glos_ev_timer {
    struct glos_ev ev;
    unsigned count;             /* times it has fired, this one too */
    unsigned missed;            /* firings merged into this one (the app was slow to take them) */
};
struct glos_ev_post {
    struct glos_ev ev;
    unsigned code, a, b, c;     /* the app's */
};

/* POST */
struct glos_post {
    unsigned size;
    unsigned code, a, b, c;
};

/* TIMER */
struct glos_timer {
    unsigned size;
    unsigned ms;                /* its period: at least 1 */
    unsigned repeat;            /* 1 repeating, 0 once */
    glos_handle handle;         /* -> the timer (GLOS_HK_TIMER), until CLOSE */
};

/* SLEEP */
struct glos_sleep {
    unsigned size;
    unsigned ms;
    unsigned left;              /* -> milliseconds left: an interrupt of the app's own cut it short (0: it slept) */
};

/* TIME */
struct glos_time {
    unsigned size;
    unsigned utc;               /* seconds since 1970 */
    unsigned how;               /* GLOS_TIME_: how the clock was last set */
    int offset;                 /* local time's offset from UTC now, in seconds (summer time included) */
};
#define GLOS_TIME_RTC  0        /* not since GLOS started: the RTC's, whatever that was */
#define GLOS_TIME_SNTP 1        /* from the network */
#define GLOS_TIME_HAND 2        /* by hand (glos time set, the Control Panel) */

/* RANDOM */
struct glos_random {
    unsigned size;
    unsigned buffer, length;    /* the offset in DS, and how many bytes (at most 256 a call) */
};

/* ================================================================== WIN (01h): windows
 *
 * A window is a handle (GLOS_HK_WINDOW). GLOS draws its frame: the title bar with its boxes (minimise, maximise or
 * restore, close) and the border, by which the user moves and sizes it; the app draws the client area, in pixels
 * of the screen's own format that GLOS keeps (DRAW). The user's doings come as events: the window's handle in
 * each. Closing the handle (CORE's CLOSE) takes the window away. */
#define GLOS_WIN_CREATE  GLOS_FN(GLOS_AREA_WIN, 0)  /* struct glos_win_create: a window, hidden until SHOW */
#define GLOS_WIN_SHOW    GLOS_FN(GLOS_AREA_WIN, 1)  /* struct glos_win_show: its state */
#define GLOS_WIN_PLACE   GLOS_FN(GLOS_AREA_WIN, 2)  /* struct glos_win_place: where it is and how large */
#define GLOS_WIN_FOCUS   GLOS_FN(GLOS_AREA_WIN, 3)  /* struct glos_win_focus: the keyboard to it, and in front */
#define GLOS_WIN_ENUM    GLOS_FN(GLOS_AREA_WIN, 4)  /* struct glos_win_enum: the windows there are */
#define GLOS_WIN_DRAG    GLOS_FN(GLOS_AREA_WIN, 6)  /* struct glos_win_drag: the user is moving or sizing it by a
                                                       part the app drew itself (a bare window's own title bar) */
/* (5: EMBED, with phase B's step 2d) */

/* A window's kind. */
#define GLOS_WK_NORMAL   0      /* a framed window with a button on the task bar */
#define GLOS_WK_DIALOG   1      /* framed, in front of its owner, no button of its own and no minimise box */
#define GLOS_WK_TOOL     2      /* framed, in front of its owner, no button of its own (a palette) */
#define GLOS_WK_POPUP    3      /* bare and above everything: a menu, a drop-down list. It never gets the focus when
                                   another window's goes, and a press outside it takes the keyboard from it */
#define GLOS_WK_DESKTOP  4      /* (the shell's) under every other window, the screen less the task bar; one */

/* Its flags. */
#define GLOS_WF_FIXED    1      /* its size is not the user's to change (no maximise box) */
#define GLOS_WF_BARE     2      /* no title bar or border: the app draws its own (GLOS_WIN_DRAG) */
#define GLOS_WF_ANYWHERE 4      /* GLOS chooses the place: x and y are not looked at */
#define GLOS_WF_MODAL    8      /* a dialog's: its owner takes no input while it is up */
#define GLOS_WF_HUNG     0x100  /* (ENUM tells) its app isn't taking its events: "not answering" */

struct glos_win_create {
    unsigned size;
    unsigned kind;              /* GLOS_WK_ */
    unsigned flags;             /* GLOS_WF_ */
    int x, y;                   /* the frame's top left corner on the screen */
    int w, h;                   /* the client area */
    glos_handle owner;          /* the window it belongs to (a dialog's, a tool window's, a pop-up's), or 0 */
    char title[64];             /* UTF-8, NUL-ended */
    glos_handle handle;         /* -> the window */
};

/* A window's state. */
#define GLOS_WS_HIDDEN   0
#define GLOS_WS_NORMAL   1      /* shown, where and as large as PLACE (or the user) put it */
#define GLOS_WS_MIN      2      /* minimised: only its button on the task bar */
#define GLOS_WS_MAX      3      /* maximised: the screen less the task bar */
#define GLOS_WS_FULL     4      /* full screen: no frame, the whole screen, the task bar gone while it has the focus */
#define GLOS_WS_SHOWN    5      /* (SHOW) shown as it last was: maximised still, if it was */
#define GLOS_WS_LEFT     6      /* (read) snapped to the left half of the work area by the user ... */
#define GLOS_WS_RIGHT    7      /* ... or the right */

struct glos_win_show {
    unsigned size;
    glos_handle handle;
    unsigned state;             /* GLOS_WS_ (not LEFT or RIGHT) */
    unsigned flags;             /* GLOS_SHOW_ */
};
#define GLOS_SHOW_NOFOCUS 1     /* shown without taking the focus */

struct glos_win_place {
    unsigned size;
    glos_handle handle;
    unsigned what;              /* GLOS_PLACE_: which of these to change */
    int x, y;                   /* the frame's corner */
    int w, h;                   /* the client area (new pixels: GLOS_EV_SIZE comes) */
};
#define GLOS_PLACE_MOVE 1
#define GLOS_PLACE_SIZE 2

struct glos_win_focus {
    unsigned size;
    glos_handle handle;         /* a window of the app's own; or 0, and ... */
    unsigned id;                /* ... any window by its id (ENUM's): the shell's and Task Manager's to do */
};

/* ENUM: one of these a window, front to back. */
struct glos_win_info {
    unsigned size;              /* (of this record: more may follow in later versions) */
    unsigned id;                /* the window's number for everyone (a handle is its owner's alone) */
    unsigned app;               /* its app's id (0: GLOS's own) */
    unsigned kind, flags;       /* GLOS_WK_, GLOS_WF_ */
    unsigned state;             /* GLOS_WS_ */
    unsigned focus;             /* 1: it has the focus */
    int x, y, w, h;             /* its frame's corner; its client area's size */
    char title[64];
};
struct glos_win_enum {
    unsigned size;
    unsigned app;               /* 0: the caller's own; an app's id; GLOS_ENUM_ALL */
    unsigned buffer, room;      /* where the records go (offset in DS), and how many bytes there are */
    unsigned count;             /* -> how many windows there are (GLOS_E_ROOM: more than fit; those that fit given) */
};
#define GLOS_ENUM_ALL 0xFFFFFFFFu

struct glos_win_drag {
    unsigned size;
    glos_handle handle;
    unsigned part;              /* GLOS_DRAG_MOVE, or GLOS_DRAG_SIZE with the edges that follow the pointer */
};
#define GLOS_DRAG_MOVE  0
#define GLOS_DRAG_SIZE  16
#define GLOS_DRAG_N     1
#define GLOS_DRAG_S     2
#define GLOS_DRAG_W     4
#define GLOS_DRAG_E     8

/* A window's properties (CORE's GET and SET). */
#define GLOS_PROP_WIN_TITLE    GLOS_PROP(GLOS_HK_WINDOW, 0)  /* text, at most 63 bytes */
#define GLOS_PROP_WIN_STATE    GLOS_PROP(GLOS_HK_WINDOW, 1)  /* GLOS_WS_ (read: SHOW sets it) */
#define GLOS_PROP_WIN_X        GLOS_PROP(GLOS_HK_WINDOW, 2)  /* the client area's corner on the screen (read) */
#define GLOS_PROP_WIN_Y        GLOS_PROP(GLOS_HK_WINDOW, 3)
#define GLOS_PROP_WIN_W        GLOS_PROP(GLOS_HK_WINDOW, 4)  /* ... and its size (read) */
#define GLOS_PROP_WIN_H        GLOS_PROP(GLOS_HK_WINDOW, 5)
#define GLOS_PROP_WIN_MIN_W    GLOS_PROP(GLOS_HK_WINDOW, 6)  /* the least and the most the user can make the client */
#define GLOS_PROP_WIN_MIN_H    GLOS_PROP(GLOS_HK_WINDOW, 7)  /* area (0: GLOS's own limits) */
#define GLOS_PROP_WIN_MAX_W    GLOS_PROP(GLOS_HK_WINDOW, 8)
#define GLOS_PROP_WIN_MAX_H    GLOS_PROP(GLOS_HK_WINDOW, 9)
#define GLOS_PROP_WIN_CURSOR   GLOS_PROP(GLOS_HK_WINDOW, 10) /* GLOS_CURSOR_: the pointer's shape over the client area */
#define GLOS_PROP_WIN_ID       GLOS_PROP(GLOS_HK_WINDOW, 11) /* its number for everyone (read) */
#define GLOS_PROP_WIN_TOPMOST  GLOS_PROP(GLOS_HK_WINDOW, 12) /* 1: in front of the ordinary windows, always */
#define GLOS_PROP_WIN_TASKBAR  GLOS_PROP(GLOS_HK_WINDOW, 13) /* 1: it has a button on the task bar */
#define GLOS_PROP_WIN_FLASH    GLOS_PROP(GLOS_HK_WINDOW, 14) /* 1: its button asks for attention, until it has the focus */
#define GLOS_PROP_WIN_PROGRESS GLOS_PROP(GLOS_HK_WINDOW, 15) /* 0 to 100: a bar along its button; -1: none */
#define GLOS_PROP_WIN_ICON     GLOS_PROP(GLOS_HK_WINDOW, 16) /* data: its picture for the title bar and the task bar,
                                                                16 by 16 pixels of 4 bytes (blue, green, red, alpha:
                                                                drawn where alpha is 80h or more), 1024 bytes; none:
                                                                taken away */
#define GLOS_PROP_WIN_POINTER  GLOS_PROP(GLOS_HK_WINDOW, 17) /* data: struct glos_cursor, a pointer of the app's own;
                                                                CURSOR becomes GLOS_CURSOR_CUSTOM */
#define GLOS_PROP_WIN_HELP     GLOS_PROP(GLOS_HK_WINDOW, 18) /* text, at most 31 bytes: its page in Help */

/* The pointer's shapes. */
#define GLOS_CURSOR_ARROW  0
#define GLOS_CURSOR_BUSY   1
#define GLOS_CURSOR_TEXT   2
#define GLOS_CURSOR_HAND   3
#define GLOS_CURSOR_CROSS  4
#define GLOS_CURSOR_NS     5    /* sizing: up and down */
#define GLOS_CURSOR_EW     6    /* left and right */
#define GLOS_CURSOR_NWSE   7    /* the slants */
#define GLOS_CURSOR_NESW   8
#define GLOS_CURSOR_MOVE   9
#define GLOS_CURSOR_CUSTOM 10   /* the window's own (GLOS_PROP_WIN_POINTER) */
struct glos_cursor {
    unsigned char w, h;         /* at most 32 by 32 */
    unsigned char hot_x, hot_y; /* the pixel of it that points */
    unsigned char black[32][4]; /* a bit a pixel, a row's first pixel its first byte's top bit: black ... */
    unsigned char white[32][4]; /* ... and white; neither: the screen shows through */
};

/* A window's events (the window's handle in each). */
#define GLOS_EV_FOCUS  GLOS_EV(GLOS_AREA_WIN, 1)        /* struct glos_ev_focus: it got the focus, or lost it */
#define GLOS_EV_SIZE   GLOS_EV(GLOS_AREA_WIN, 2)        /* struct glos_ev_size: its client area has another size
                                                           (the user's doing, its state's, the screen's): its pixels
                                                           are elsewhere (DRAW's PIXELS) and to be drawn again */
#define GLOS_EV_STATE  GLOS_EV(GLOS_AREA_WIN, 3)        /* struct glos_ev_state: its state changed */
#define GLOS_EV_CLOSE  GLOS_EV(GLOS_AREA_WIN, 4)        /* (the header alone) the user asked for it to be closed */
struct glos_ev_focus {
    struct glos_ev ev;
    unsigned got;               /* 1 got, 0 lost */
};
struct glos_ev_size {
    struct glos_ev ev;
    int w, h;
};
struct glos_ev_state {
    struct glos_ev ev;
    unsigned state;             /* GLOS_WS_ */
};

/* ================================================================== DRAW (02h): pixels and the screen */
#define GLOS_DRAW_PIXELS  GLOS_FN(GLOS_AREA_DRAW, 0)    /* struct glos_pixels: where a window's pixels are */
#define GLOS_DRAW_PRESENT GLOS_FN(GLOS_AREA_DRAW, 1)    /* struct glos_present: the app drew there: to the screen */
#define GLOS_DRAW_SCREEN  GLOS_FN(GLOS_AREA_DRAW, 2)    /* struct glos_screen: the desktop's screen; any program */
#define GLOS_DRAW_GPU     GLOS_FN(GLOS_AREA_DRAW, 3)    /* struct glos_gpu: the Matrox card's drawing engine, for
                                                           programs with a HAL of their own (DOS-GL) */
#define GLOS_DRAW_GPU_LOCK   GLOS_FN(GLOS_AREA_DRAW, 4) /* (EBX 0) the engine the app's own (it waits for it) */
#define GLOS_DRAW_GPU_UNLOCK GLOS_FN(GLOS_AREA_DRAW, 5) /* (EBX 0) ... and no longer */
#define GLOS_DRAW_VRAM    GLOS_FN(GLOS_AREA_DRAW, 6)    /* struct glos_vram: a block of the card's memory */
#define GLOS_DRAW_WINVRAM GLOS_FN(GLOS_AREA_DRAW, 7)    /* struct glos_winvram: a window shows pixels in such a block */
#define GLOS_DRAW_BLIT    GLOS_FN(GLOS_AREA_DRAW, 8)    /* struct glos_blit: pixels in a format of the app's own into
                                                           a window's (DOS-GL's software renderer; pictures) */
#define GLOS_DRAW_MODES   GLOS_FN(GLOS_AREA_DRAW, 9)    /* struct glos_modes: the screen modes the card has (for the
                                                           Control Panel; the mode itself is [display] mode) */

/* A pixel format: bits a pixel, and where each colour's bits are in one (at 8 bits: GLOS's palette, the 6x6x6
   cube and greys of glos/gfx.h). */
struct glos_format {
    unsigned bpp;
    unsigned char rpos, rsize, gpos, gsize, bpos, bsize, pad[2];
};
struct glos_pixels {
    unsigned size;
    glos_handle handle;
    unsigned pixels;            /* -> where they are: the linear address, in the app's address space, of the client
                                   area's top left pixel (libglos's glos_ptr() makes a pointer of it) */
    unsigned pitch;             /* -> bytes from a row to the next */
    int w, h;                   /* -> the client area's size */
    struct glos_format format;  /* -> the screen's */
};
struct glos_present {
    unsigned size;
    glos_handle handle;
    int x, y, w, h;             /* the rectangle drawn, in the client area (w 0: all of it) */
};
struct glos_screen {
    unsigned size;
    int w, h;                   /* -> the screen */
    struct glos_format format;
    int work_x, work_y, work_w, work_h;         /* -> the screen less the task bar */
};

struct glos_mode {
    int w, h;
    unsigned bits;              /* 8, 16 or 32 */
};
struct glos_modes {
    unsigned size;
    unsigned buffer;            /* where the modes go: struct glos_mode each, by size then depth */
    unsigned room;              /* how many there is room for */
    unsigned count;             /* -> how many the card has (GLOS_E_ROOM: more than fit; those that fit given). 0:
                                   no card GLOS can put in a graphics mode */
};

/* BLIT: a rectangle of pixels the app has, in any format of 8, 16 or 32 bits, into the window's client area at
   (x, y), in the screen's format (16- and 32-bit pixels dithered on an 8-bit screen), and to the screen: no
   PRESENT is needed. What falls outside the client area is left out. */
struct glos_blit {
    unsigned size;
    glos_handle handle;
    unsigned pixels;            /* the source's top left pixel: an offset in DS */
    unsigned pitch;             /* bytes from a row of it to the next */
    struct glos_format format;
    int w, h;                   /* its size */
    int x, y;                   /* where it goes in the client area */
    unsigned palette;           /* an 8-bit source's colours: 256 of 0x00RRGGBB (an offset in DS); 0: its bytes are
                                   GLOS's palette's */
};

/* The Matrox card for programs with a HAL of their own (DOS-GL's windowed path): as API 1.x had it. The engine is
   one owner's at a time: the app's once it has the lock, until someone else needs the engine while the app is in
   a GLOS or DOS call (never in the middle of its own drawing). Its registers are mapped into the app only while
   it has the lock: a touch without it waits for the lock and takes it. Refused (GLOS_E_NO_GPU) without a Matrox
   card whose engine draws the desktop (VERSION's GLOS_MACHINE_GPU). */
#define GLOS_E_NO_GPU 8
#ifndef GLOS_API_H              /* (during phase B, API 1.x's header has these two structures, the same) */
struct glos_gpu {
    unsigned size;
    unsigned device, revision;  /* the chip: its PCI device ID and revision (0525h with revision 80h or above: G450) */
    unsigned vram;              /* bytes of memory on the card */
    unsigned regs;              /* the linear address of its registers (16 KB), mapped while the app has the lock */
    unsigned base;              /* the linear address of the card's memory as the app sees it: its own blocks
                                   (VRAM), each at its offset from here; nothing else */
    unsigned share;             /* the linear address of the struct glos_gpu_share GLOS keeps for the app */
    unsigned bpp;               /* the screen's bits a pixel */
    unsigned fb_phys, regs_phys;        /* the card's apertures (physical; to describe the chip, not to map) */
    unsigned option, subsys;    /* the chip's OPTION word and subsystem IDs */
};
/* GLOS writes it; the app reads it before each piece of work with the engine: it clears `quiet`, then if `held`
   is 0 it calls GPU_LOCK, and if `lost` is not what it was the last time, someone else has had the engine since,
   and whatever the app's HAL believes of the registers is to be forgotten. The lock never leaves the app between
   that look and its drawing: only in a GLOS or DOS call, or while `quiet` is set. */
struct glos_gpu_share {
    unsigned size;
    volatile unsigned held;     /* 1: the engine is the app's (its registers are mapped) */
    volatile unsigned lost;     /* the times it was given the engine after someone else had it */
    volatile unsigned want;     /* someone waits for the engine while the app has it */
    volatile unsigned quiet;    /* (the app's) 1: it isn't using the engine now; its turn may end at any moment */
};
#endif
struct glos_vram {
    unsigned size;
    unsigned op;                /* 0: a block; 1: the block at `offset` freed */
    unsigned least, most;       /* bytes at least and at most (0: `least`) */
    unsigned align;             /* a power of two, at least 4096 */
    unsigned offset;            /* -> the block's offset in the card's memory, mapped at that offset from
                                   glos_gpu's base (op 1: the block to free) */
    unsigned bytes;             /* -> its size */
};
struct glos_winvram {
    unsigned size;
    glos_handle handle;
    unsigned offset;            /* pixels in a block of the app's, in the screen's format and the window's size:
                                   the window shows them from now on (a swap: the other buffer). GLOS_OWN_PIXELS:
                                   its own pixels again */
    unsigned pitch;             /* bytes from one of their rows to the next (a multiple of 64) */
};
#define GLOS_OWN_PIXELS 0xFFFFFFFFu

/* ================================================================== INPUT (03h): keys, text, the mouse, pads */
#define GLOS_INPUT_KEYS     GLOS_FN(GLOS_AREA_INPUT, 0) /* struct glos_keys: the keyboard as it is now */
#define GLOS_INPUT_CAPTURE  GLOS_FN(GLOS_AREA_INPUT, 1) /* struct glos_capture: the mouse to one window, wherever
                                                           the pointer is */
#define GLOS_INPUT_PADS     GLOS_FN(GLOS_AREA_INPUT, 2) /* struct glos_pads: the pads there are, as they are now */
#define GLOS_INPUT_GAMEPORT GLOS_FN(GLOS_AREA_INPUT, 3) /* struct glos_gameport: the game port DOS programs see */
#define GLOS_INPUT_MODE     GLOS_FN(GLOS_AREA_INPUT, 4) /* struct glos_input_mode: how the app takes its input */

/* KEYS: which keys are down, by their codes (a set 1 scan code, 100h added for those with an E0 prefix). */
struct glos_keys {
    unsigned size;
    unsigned char down[64];     /* bit (code & 7) of byte (code >> 3): that key is down */
    unsigned mods;              /* GLOS_MOD_: what is held */
    unsigned locks;             /* GLOS_LOCK_: what is on */
};
#define GLOS_LOCK_CAPS 1
#define GLOS_LOCK_NUM  2
#define GLOS_KEY_DOWN(k, code) (((k)->down[(code) >> 3] >> ((code) & 7)) & 1)

/* CAPTURE: from now until it is let go, every movement and button of the mouse is this window's, wherever the
   pointer is (x and y still count from its client area's corner, and may lie outside it), and a press elsewhere
   changes no focus and moves no window: for a menu or a list dropped down, which closes on a press outside itself.
   It is let go by on 0, when the window goes or is hidden, and when the focus leaves the app. (Without it, a
   window has the mouse from a press in its client area until every button is up, as always.) */
struct glos_capture {
    unsigned size;
    glos_handle handle;
    unsigned on;                /* 1 capture, 0 let go */
};

/* PADS: game pads and joysticks (USB's; phase H adds kinds). */
struct glos_pad {
    unsigned present;           /* 1: one is plugged in here */
    unsigned flags;             /* GLOS_PAD_ */
    int lx, ly;                 /* the left stick, -127 to 127: right and down positive, 0 its rest. A pad's
                                   direction buttons move it too */
    int rx, ry;                 /* the right stick, if it has one */
    unsigned buttons;           /* bit n: its button n + 1 is down. An Xbox pad's: 1 A, 2 B, 3 X, 4 Y, 5 LB, 6 RB,
                                   7 View or Back, 8 Menu or Start, 9 and 10 the sticks' clicks; others: as the
                                   pad numbers them */
};
#define GLOS_PAD_RIGHT 1        /* it has a right stick */
#define GLOS_PADS 2
struct glos_pads {
    unsigned size;
    unsigned flags;             /* GLOS_PADS_EVENTS: GLOS_EV_PAD from now on (0: no longer) */
    struct glos_pad pad[GLOS_PADS];     /* -> */
};
#define GLOS_PADS_EVENTS 1

/* GAMEPORT: the joystick port (201h) DOS programs read: GLOS's own while a pad is plugged in or [joystick] port
   says so (SETTINGS), else the PC's. A lone pad is the whole port; of two, each is one joystick. */
struct glos_gameport {
    unsigned size;
    unsigned virtual_on;        /* -> 1: GLOS answers the port now */
    unsigned mode;              /* -> [joystick] port: 0 auto, 1 virtual, 2 off */
    unsigned range;             /* -> [joystick] range: microseconds an axis's one-shot runs at its far end */
    unsigned pads;              /* -> bit n: pad n feeds it */
};

/* MODE: how the app takes its keys. */
struct glos_input_mode {
    unsigned size;
    unsigned set;               /* 1: `flags` are set; 0: only read */
    unsigned flags;             /* GLOS_INPUT_ */
};
#define GLOS_INPUT_RAW 1        /* keys as they go down and up: no repeats while one is held, and no GLOS_EV_TEXT
                                   (a piano's keys, a game's) */

/* INPUT's events. */
#define GLOS_EV_KEY    GLOS_EV(GLOS_AREA_INPUT, 1)      /* struct glos_ev_key: a key went down or up while the window
                                                           had the focus */
#define GLOS_EV_MOUSE  GLOS_EV(GLOS_AREA_INPUT, 2)      /* struct glos_ev_mouse: the pointer moved in the window's
                                                           client area, or a button changed there; from a press
                                                           until every button is up, wherever the pointer goes */
#define GLOS_EV_WHEEL  GLOS_EV(GLOS_AREA_INPUT, 3)      /* struct glos_ev_wheel: the wheel turned over the window's
                                                           client area, whether it has the focus or not */
#define GLOS_EV_TEXT   GLOS_EV(GLOS_AREA_INPUT, 4)      /* struct glos_ev_text: a character was typed in the window
                                                           (after the key's GLOS_EV_KEY): what a field takes as
                                                           text, whatever keys made it */
#define GLOS_EV_PAD    GLOS_EV(GLOS_AREA_INPUT, 5)      /* struct glos_ev_pad (no window: handle 0): a pad was
                                                           plugged in or taken out (to every app that asked,
                                                           GLOS_PADS_EVENTS), or changed (to the one with the
                                                           focus) */
#ifndef GLOS_MOD_SHIFT
#define GLOS_MOD_SHIFT   1
#define GLOS_MOD_CTRL    2
#define GLOS_MOD_ALT     4
#endif
struct glos_ev_key {
    struct glos_ev ev;
    unsigned key;               /* the key: a set 1 scan code, 100h added for those with an E0 prefix */
    unsigned ch;                /* its character (Unicode) when it went down, 0 if it has none */
    unsigned mods;              /* GLOS_MOD_: what was held */
    unsigned down;              /* 1 pressed (again and again while it is held), 0 let go */
    unsigned repeat;            /* 1: the keyboard repeating a key that is held */
};
struct glos_ev_text {
    struct glos_ev ev;
    unsigned ch;                /* the character (Unicode), 20h or above */
    unsigned mods;
};
struct glos_ev_pad {
    struct glos_ev ev;
    unsigned index;             /* which: 0 or 1 */
    struct glos_pad pad;        /* as it is now */
    unsigned changed;           /* its buttons that went down or up */
};
struct glos_ev_mouse {
    struct glos_ev ev;
    int x, y;                   /* the pointer in the client area */
    int dx, dy;                 /* how far it moved */
    unsigned buttons;           /* those down now: 1 left, 2 right, 4 middle */
    unsigned button;            /* the one that changed, 0 for a movement */
    unsigned down;              /* ... pressed (1) or let go (0) */
    unsigned mods;
    unsigned clicks;            /* a press: 1, or 2 for the second of a double click, 3 for the third of three
                                   ([input] double_click's time, 4 pixels) */
};
struct glos_ev_wheel {
    struct glos_ev ev;
    int x, y;
    int notches;                /* positive: away from the user (up) */
    unsigned buttons, mods;
};

/* ================================================================== JOB (08h): running programs
 *
 * A job is a program (or a line for COMMAND.COM) that GLOS runs for an app in the DOS session beside it: a handle
 * (GLOS_HK_JOB), the app's until CLOSE. Jobs run one at a time, in the order they were asked for; while one runs,
 * the others wait (QUEUED). What the program writes to its standard output and error is kept for the app to READ
 * (16 KB each: a program that fills one waits for the app); what the app WRITEs is the program's standard input.
 * Only where GLOS runs with its agent (GLOS_E_REFUSED under GLOS /RUN). */
#define GLOS_JOB_RUN    GLOS_FN(GLOS_AREA_JOB, 0)       /* struct glos_job_run: a program started */
#define GLOS_JOB_READ   GLOS_FN(GLOS_AREA_JOB, 1)       /* struct glos_job_io: what it has written, as far as it has */
#define GLOS_JOB_WRITE  GLOS_FN(GLOS_AREA_JOB, 2)       /* struct glos_job_io: its input */
#define GLOS_JOB_SIGNAL GLOS_FN(GLOS_AREA_JOB, 3)       /* struct glos_job_signal: Ctrl+C, Ctrl+Break, or its end */
#define GLOS_JOB_STATUS GLOS_FN(GLOS_AREA_JOB, 4)       /* struct glos_job_status: how it is doing */

struct glos_job_run {
    unsigned size;
    unsigned command;           /* text: a program and its arguments, or a line for COMMAND.COM (a batch file, an
                                   internal command), at most 255 bytes */
    unsigned dir;               /* text: the drive and folder it starts in, at most 79 bytes (0: the system's) */
    unsigned env;               /* NAME=VALUE strings, a NUL after each and one more: added to its environment, at
                                   most 511 bytes (0: none) */
    unsigned how;               /* GLOS_RUN_ */
    unsigned flags;             /* GLOS_RUNF_ */
    unsigned profile;           /* text: the profile it runs with, a program's name (0: its own) */
    glos_handle handle;         /* -> the job (no handle with GLOS_RUNF_DETACH) */
    unsigned id;                /* -> its number (SYS's) */
};
#define GLOS_RUN_HIDDEN    0    /* beside the desktop: its output kept for READ. If it wants the screen (a mode set,
                                   the text or graphics screen touched) it has it, full-screen, to its end */
#define GLOS_RUN_WINDOW    1    /* its console is the app's to show (a text window): what it writes to the screen
                                   is READ as bytes, WRITE types its keys; full-screen if it wants the screen */
#define GLOS_RUN_FULL      2    /* as WINDOW, full-screen from the start */
#define GLOS_RUN_EXCLUSIVE 3    /* an exclusive session: the machine is its alone until it ends */
#define GLOS_RUN_APP       4    /* it is made an app without asking (a program that never calls INIT) */
#define GLOS_RUNF_DETACH 1      /* nothing of it is the app's: no handle, no output kept, it runs on when the app ends */
#define GLOS_RUNF_STDIN  2      /* its standard input is the app's WRITEs (else the keyboard's, when it has it) */
#define GLOS_RUNF_DIRECT 4      /* direct mode (the PC's ports are its own: PRD D20) */

struct glos_job_io {
    unsigned size;
    glos_handle handle;
    unsigned stream;            /* READ: GLOS_JOB_OUT or GLOS_JOB_ERR. WRITE: GLOS_JOB_IN; GLOS_JOB_KEYS (a WINDOW
                                   job's keys: 2 bytes each, the scan code above the character) */
    unsigned buffer;
    unsigned length;            /* READ: the room. WRITE: how many bytes; 0 with GLOS_JOB_IN: the input's end (the
                                   program reads no more: end of file) */
    unsigned done;              /* -> the bytes read, or taken (WRITE: fewer than length when its input is full,
                                   4 KB: the rest later) */
};
#define GLOS_JOB_OUT  0
#define GLOS_JOB_ERR  1
#define GLOS_JOB_IN   2
#define GLOS_JOB_KEYS 3

struct glos_job_signal {
    unsigned size;
    glos_handle handle;
    unsigned what;              /* GLOS_SIGNAL_ */
};
#define GLOS_SIGNAL_INT   1     /* Ctrl+C, as from the keyboard */
#define GLOS_SIGNAL_BREAK 2     /* Ctrl+Break */
#define GLOS_SIGNAL_END   3     /* it is ended, whatever it is doing (its exit code is then 255) */

struct glos_job_status {
    unsigned size;
    glos_handle handle;
    unsigned state;             /* -> GLOS_JS_ */
    unsigned code;              /* -> its exit code, once DONE (126: it could not be run) */
    unsigned out, err;          /* -> the bytes waiting to be READ */
    unsigned ms;                /* -> how long it has run (or ran) */
    unsigned id;                /* -> its number */
};
#define GLOS_JS_QUEUED  0       /* waiting for the job before it */
#define GLOS_JS_RUNNING 1       /* running, beside the desktop or in the app's window */
#define GLOS_JS_FULL    2       /* running full-screen, as a session */
#define GLOS_JS_APP     3       /* it became an app: no longer a job of DOS's */
#define GLOS_JS_DONE    4       /* ended */

/* JOB's event: the job moved on (its state), or wrote where it had written nothing (READ it). */
#define GLOS_EV_JOB GLOS_EV(GLOS_AREA_JOB, 1)           /* struct glos_ev_job */
struct glos_ev_job {
    struct glos_ev ev;          /* (handle: the job) */
    unsigned state;             /* GLOS_JS_ */
    unsigned code;              /* its exit code, when DONE */
};

/* ================================================================== FS (09h): files and disks
 *
 * What costs a program many of DOS's calls, in one of GLOS's: a folder's entries a batch at a time, several
 * files' details at once, the drives there are. Ordinary file I/O stays the C library's, through DOS.
 * Paths and names are UTF-8 here; DOS has them in its code page, and GLOS converts (REGION's).
 * Long file names are DOS's to give (its long-name calls, INT 21h 71xxh, from a driver such as DOSLFN): where a
 * drive has them, LIST and INFO take and give long names, and a drive's GLOS_DRIVE_LONG says so. */
#define GLOS_FS_LIST    GLOS_FN(GLOS_AREA_FS, 0)        /* struct glos_fs_list: a folder's entries, a batch a call */
#define GLOS_FS_INFO    GLOS_FN(GLOS_AREA_FS, 1)        /* struct glos_fs_info: several files' details at once */
#define GLOS_FS_DRIVES  GLOS_FN(GLOS_AREA_FS, 2)        /* struct glos_fs_drives: the drives */
#define GLOS_FS_WATCH   GLOS_FN(GLOS_AREA_FS, 3)        /* struct glos_fs_watch: a folder or a file watched */
#define GLOS_FS_SECTORS GLOS_FN(GLOS_AREA_FS, 4)        /* struct glos_fs_sectors: a disk's sectors read or written */

/* An entry: a file or a folder. They lie one after another in the buffer, each `size` bytes (a multiple of 4). */
struct glos_fs_entry {
    unsigned size;              /* this record's, its name and all */
    unsigned attr;              /* GLOS_FA_ */
    unsigned bytes_lo, bytes_hi;        /* the file's size */
    unsigned mtime;             /* when it was last written: DOS's date in the high word (years from 1980 in bits
                                   9-15, the month in 5-8, the day in 0-4) and time in the low (hours in bits
                                   11-15, minutes in 5-10, seconds halved in 0-4); local time */
    char short_name[16];        /* its 8.3 name, where the name below is a long one ("" otherwise) */
    char name[4];               /* its name, a NUL after it (as long as it is) */
};
#define GLOS_FA_READONLY 0x01
#define GLOS_FA_HIDDEN   0x02
#define GLOS_FA_SYSTEM   0x04
#define GLOS_FA_FOLDER   0x10
#define GLOS_FA_ARCHIVE  0x20
#define GLOS_FA_NONE     0x80000000u    /* INFO: there is no such file (bytes_lo: DOS's error) */

struct glos_fs_list {
    unsigned size;
    glos_handle handle;         /* 0: begin a folder (`path`). -> a listing (GLOS_HK_LIST) to go on with in the next
                                   call, or 0: that was the folder's last batch (nothing to close) */
    unsigned path;              /* the folder ("C:\\GAMES"), to begin */
    unsigned flags;             /* GLOS_FS_ */
    unsigned buffer;            /* where the entries go ... */
    unsigned room;              /* ... and the room there (512 bytes at least) */
    unsigned count;             /* -> how many entries were given ("." and ".." never are) */
    unsigned length;            /* -> the bytes they take */
    unsigned long_names;        /* -> 1: the names are long ones (the drive has DOS's long-name calls) */
};
#define GLOS_FS_SHORT 1         /* 8.3 names only, even where the drive has long ones */

struct glos_fs_info {
    unsigned size;
    unsigned paths;             /* the files' and folders' paths, each ended by a NUL */
    unsigned count;             /* how many: 1 to 64 */
    unsigned flags;             /* GLOS_FS_ */
    unsigned buffer;            /* where their entries go, in the paths' order (GLOS_FA_NONE: no such file) */
    unsigned room;
    unsigned length;            /* -> the bytes they take (GLOS_E_ROOM: more than room; `given` fit) */
    unsigned given;             /* -> how many entries were given */
};

struct glos_fs_drive {
    unsigned letter;            /* 'A' to 'Z' */
    unsigned kind;              /* GLOS_DRIVE_ */
    unsigned flags;             /* GLOS_DRIVE_ flags */
    unsigned serial;            /* the volume's serial number (another disk in the drive has another); 0: none */
    unsigned total_kb, free_kb; /* its size and what is free (FFFFFFFFh: more than fits) */
    char label[12];             /* the volume's label */
    char system[12];            /* its file system as DOS names it ("FAT16") */
};
#define GLOS_DRIVE_FLOPPY 1
#define GLOS_DRIVE_HARD   2     /* a hard disk, a CF card, anything fixed */
#define GLOS_DRIVE_CD     3     /* a CD-ROM (MSCDEX's) */
#define GLOS_DRIVE_NET    4     /* a network drive */
#define GLOS_DRIVE_RAM    5     /* a RAM disk */
#define GLOS_DRIVE_REMOVABLE 1  /* flags: its disk can be taken out */
#define GLOS_DRIVE_LONG      2  /* DOS's long-name calls answer for it */
#define GLOS_DRIVE_DETAILS   4  /* serial, sizes, label and system were asked for and are there */

struct glos_fs_drives {
    unsigned size;
    unsigned drive;             /* 0: every drive (the details of hard disks and RAM disks only: a floppy or a CD
                                   is not made to spin); 'A' to 'Z': that one, with its details (no disk in it:
                                   GLOS_DRIVE_DETAILS clear) */
    unsigned buffer;            /* where they go: struct glos_fs_drive each */
    unsigned room;              /* how many there is room for */
    unsigned count;             /* -> how many there are (GLOS_E_ROOM: more than room) */
};

struct glos_fs_watch {
    unsigned size;
    unsigned path;              /* a folder ("C:\\DOCS": anything made, removed, renamed or written in it), or a
                                   file ("C:\\DOCS\\A.TXT": that file written, removed or renamed) */
    glos_handle handle;         /* -> the watch (GLOS_HK_WATCH), until CLOSE */
};

struct glos_fs_sectors {
    unsigned size;
    unsigned disk;              /* the BIOS's number: 0 and 1 the floppy drives, 80h up the hard disks */
    unsigned write;             /* 0 read, 1 write (GLOS_E_REFUSED: the disk GLOS itself runs from is never written) */
    unsigned lba_lo, lba_hi;    /* the first sector */
    unsigned count;             /* how many: 0 (only the disk's size is asked) to 32 */
    unsigned buffer;            /* count sectors' bytes */
    unsigned bytes;             /* -> a sector's size (512) */
    unsigned total_lo, total_hi;        /* -> how many sectors the disk has */
    unsigned error;             /* -> the BIOS's error code when it refused (GLOS_E_REFUSED) */
};

/* FS's events. */
#define GLOS_EV_FS GLOS_EV(GLOS_AREA_FS, 1)     /* (the header alone; handle: the watch) what is watched changed.
                                                   Changes close together are told once */

/* ================================================================== SETTINGS (0Ch): GLOS.CFG, the theme, the region
 *
 * A setting is a section, a key and a value, all text (GLOS.CFG's "[section]" and "key = value"). The schema
 * says which there are, each one's kind, range, default and words for the Control Panel. Texts are passed by
 * address (offsets in DS); those GLOS writes take a room and give a length, as a property's. */
#define GLOS_SETTINGS_GET      GLOS_FN(GLOS_AREA_SETTINGS, 0)  /* struct glos_setting: a setting's value now */
#define GLOS_SETTINGS_SET      GLOS_FN(GLOS_AREA_SETTINGS, 1)  /* struct glos_setting: change it, or take it out */
#define GLOS_SETTINGS_SECTIONS GLOS_FN(GLOS_AREA_SETTINGS, 2)  /* struct glos_setting_list: the file's sections */
#define GLOS_SETTINGS_KEYS     GLOS_FN(GLOS_AREA_SETTINGS, 3)  /* struct glos_setting_list: a section's settings */
#define GLOS_SETTINGS_SCHEMA   GLOS_FN(GLOS_AREA_SETTINGS, 4)  /* struct glos_schema: one of the schema's entries */
#define GLOS_SETTINGS_WATCH    GLOS_FN(GLOS_AREA_SETTINGS, 5)  /* struct glos_setting_watch: GLOS_EV_SETTING from now */
#define GLOS_SETTINGS_THEME    GLOS_FN(GLOS_AREA_SETTINGS, 6)  /* struct glos_theme: the theme now */
#define GLOS_SETTINGS_REGION   GLOS_FN(GLOS_AREA_SETTINGS, 7)  /* struct glos_region: the region's formats now */

#define GLOS_E_SETTING 10       /* no such setting, or the value is not one it can have (SET: why, in words) */

struct glos_setting {
    unsigned size;
    unsigned section;           /* its section's name ("display"; "program NAME.EXE" for a program's profile) */
    unsigned key;               /* its key ("mode") */
    unsigned value;             /* GET: where its value goes. SET: its new value, or 0 to take the setting out of
                                   the file (its default again) */
    unsigned room;              /* GET: the room there */
    unsigned length;            /* -> GET: the value's whole length (GLOS_E_ROOM: cut) */
    unsigned set;               /* -> GET: 1 the file sets it, 0 this is its default */
    unsigned when;              /* -> SET: GLOS_SET_: when the change takes effect */
    unsigned why;               /* SET: where the reason goes if it is refused (0: not wanted) ... */
    unsigned why_room;          /* ... and the room there */
};
#define GLOS_SET_NOW     0      /* at once */
#define GLOS_SET_RESTART 1      /* when GLOS next starts */
#define GLOS_SET_PROGRAM 2      /* for programs started from now on */
#define GLOS_SET_DESKTOP 3      /* when the desktop next starts */
#define GLOS_SET_APP     4      /* for apps started from now on */

struct glos_setting_list {
    unsigned size;
    unsigned section;           /* KEYS: the section (SECTIONS: not used) */
    unsigned buffer;            /* where the list goes: SECTIONS: a name a line; KEYS: "key = value" a line, those
                                   the file sets */
    unsigned room;
    unsigned length;            /* -> the list's whole length (GLOS_E_ROOM: cut) */
};

struct glos_schema {
    unsigned size;
    unsigned index;             /* which entry: 0 to count - 1 (GLOS_E_ARGUMENT past the last) */
    unsigned count;             /* -> how many entries there are */
    unsigned kind;              /* -> GLOS_SK_ */
    unsigned when;              /* -> GLOS_SET_: when a change takes effect */
    int min, max;               /* -> a number's range; a text's shortest and longest */
    unsigned buffer;            /* where its texts go, each ended by a NUL: the section, the key ("*": any), the
                                   choices ("a|b|c": a choice's values, a number's words, a text's class), the
                                   default, the label (its letter after "&") and the description */
    unsigned room;
    unsigned length;            /* -> their whole length (GLOS_E_ROOM: cut) */
};
#define GLOS_SK_CHOICE  1       /* one of its choices */
#define GLOS_SK_NUMBER  2       /* a number in its range, or one of its words */
#define GLOS_SK_TEXT    3       /* text of its class (line, host, hostport, tz, page) */
#define GLOS_SK_MODE    4       /* a screen mode: WxHxBITS, or none */
#define GLOS_SK_PATH    5       /* a DOS path */
#define GLOS_SK_BLASTER 6       /* a BLASTER string */
#define GLOS_SK_ENV     7       /* NAME=VALUE lines */

struct glos_setting_watch {
    unsigned size;
    unsigned section;           /* the section to be told of ("" or 0: every section) */
    unsigned on;                /* 1 from now on, 0 no more */
};
#define GLOS_WATCH_MAX 8        /* sections an app may watch at once (or all of them) */

/* The theme: colours by what they are for (0x00RRGGBB), and sizes. An app draws with these and nothing of its
   own, and asks again at GLOS_EV_THEME. Every colour of a built-in theme is in the 8-bit desktop's palette. */
#define GLOS_ROLE_FACE            0     /* a window's and a control's surface ... */
#define GLOS_ROLE_FACE_TEXT       1     /* ... and text on it */
#define GLOS_ROLE_FIELD           2     /* where text is typed and lists are: its ground ... */
#define GLOS_ROLE_FIELD_TEXT      3
#define GLOS_ROLE_LINE            4     /* outlines and rules */
#define GLOS_ROLE_DIM             5     /* text and marks of what can't be used now */
#define GLOS_ROLE_SHADE           6     /* a control's shaded side, a scroll bar's trough */
#define GLOS_ROLE_DARK            7     /* strong fills: a header, a pressed button */
#define GLOS_ROLE_MARK            8     /* the accent: the default button, the focus's mark ... */
#define GLOS_ROLE_MARK_TEXT       9
#define GLOS_ROLE_CHOSEN          10    /* what is selected: its ground ... */
#define GLOS_ROLE_CHOSEN_TEXT     11
#define GLOS_ROLE_LINK            12    /* a link's text, on a field */
#define GLOS_ROLE_DESK            13    /* the desktop */
#define GLOS_ROLE_FRAME           14    /* a window's border */
#define GLOS_ROLE_TITLE           15    /* the title bar of the window with the focus ... */
#define GLOS_ROLE_TITLE_TEXT      16
#define GLOS_ROLE_TITLE_OFF       17    /* ... and of the others */
#define GLOS_ROLE_TITLE_OFF_TEXT  18
#define GLOS_ROLE_CLOSE           19    /* the focus's close box */
#define GLOS_ROLE_BAR             20    /* the task bar ... */
#define GLOS_ROLE_BAR_TEXT        21
#define GLOS_ROLE_BAR_ON          22    /* ... and its button for the window with the focus */
#define GLOS_ROLE_BAR_ON_TEXT     23
#define GLOS_ROLE_CODE_KEYWORD    24    /* an editor's syntax colours, on a field */
#define GLOS_ROLE_CODE_STRING     25
#define GLOS_ROLE_CODE_COMMENT    26
#define GLOS_ROLE_CODE_NUMBER     27
#define GLOS_ROLE_CODE_TYPE       28
#define GLOS_ROLE_CODE_DIRECTIVE  29
#define GLOS_ROLE_ERROR           30    /* what went wrong, on a field */
#define GLOS_ROLE_GOOD            31    /* what went right */
#define GLOS_ROLES                32

struct glos_theme {
    unsigned size;
    char name[16];              /* -> "slate", "dark", "contrast" ([desktop] theme) */
    unsigned flags;             /* -> GLOS_THEME_ */
    unsigned roles;             /* -> how many colours follow that GLOS knows (GLOS_ROLES today) */
    unsigned colour[GLOS_ROLES];        /* -> by GLOS_ROLE_ */
    int border;                 /* -> a window's border, in pixels */
    int title;                  /* -> its title bar's height */
    int bar;                    /* -> the task bar's height */
    int scroll;                 /* -> a scroll bar's width */
    int icon;                   /* -> a toolbar icon's side */
    int text;                   /* -> the UI font's size: 0 small, 1 normal, 2 large */
};
#define GLOS_THEME_DARK     1   /* light text on dark grounds */
#define GLOS_THEME_CONTRAST 2   /* made for the strongest contrast */

struct glos_region {
    unsigned size;
    unsigned date;              /* -> the order of a date's parts: GLOS_DATE_ */
    unsigned date_sep;          /* -> the character between them */
    unsigned clock12;           /* -> 1: twelve hours with AM and PM */
    unsigned decimal;           /* -> a number's decimal mark */
    unsigned thousands;         /* -> the character between groups of three digits, 0 none */
    unsigned first_day;         /* -> the week's first day: 0 Sunday, 1 Monday */
    unsigned codepage;          /* -> the code page of file names and DOS programs' text; 0: what DOS says
                                   (INT 21h 6601h) */
    char layout[8];             /* -> the keyboard's layout ("us") */
    char language[8];           /* -> the language of GLOS's own words ("en") */
    int offset;                 /* -> local time's seconds east of UTC now (as TIME's) */
};
#define GLOS_DATE_YMD 0
#define GLOS_DATE_DMY 1
#define GLOS_DATE_MDY 2

/* SETTINGS' events. */
#define GLOS_EV_SETTING GLOS_EV(GLOS_AREA_SETTINGS, 1)  /* struct glos_ev_setting: a setting of a watched section
                                                           changed (whoever changed it) */
#define GLOS_EV_THEME   GLOS_EV(GLOS_AREA_SETTINGS, 2)  /* (the header alone) the theme changed: THEME again, and
                                                           everything drawn again. To every app */
struct glos_ev_setting {
    struct glos_ev ev;
    char section[32];
    char key[32];
};

/* ================================================================== IDLE (0Dh): idle time, the saver, the screen's power
 *
 * GLOS starts the screen saver when nobody has touched the keyboard, the mouse or a pad for [saver] wait
 * minutes, and turns the screen's power off [saver] power minutes after that (where the card can). Not while a
 * full-screen program has the screen, an app holds the GPU, or any app has an AWAKE handle open. The first key
 * or movement ends it (and is used up). */
#define GLOS_IDLE_TIME  GLOS_FN(GLOS_AREA_IDLE, 0)      /* (EBX 0) -> EAX milliseconds since the last input */
#define GLOS_IDLE_AWAKE GLOS_FN(GLOS_AREA_IDLE, 1)      /* struct glos_awake: the screen stays on while the handle
                                                           (GLOS_HK_AWAKE) is open: a video, a game, a slide show */
#define GLOS_IDLE_SAVER GLOS_FN(GLOS_AREA_IDLE, 2)      /* struct glos_saver: the saver's state; start it; end it */
#define GLOS_IDLE_POWER GLOS_FN(GLOS_AREA_IDLE, 3)      /* struct glos_power: the screen's power */

struct glos_awake {
    unsigned size;
    glos_handle handle;         /* -> until CLOSE */
};

struct glos_saver {
    unsigned size;
    unsigned what;              /* GLOS_SAVER_ */
    unsigned on;                /* -> 1 the saver is on the screen now */
    unsigned wait;              /* -> [saver] wait in seconds, 0 never */
    unsigned awake;             /* -> how many AWAKE handles are open (any app's) */
};
#define GLOS_SAVER_STATE 0      /* only tell */
#define GLOS_SAVER_START 1      /* now, whatever the idle time (GLOS_E_REFUSED: no desktop on the screen) */
#define GLOS_SAVER_END   2

struct glos_power {
    unsigned size;
    unsigned what;              /* 0 tell, 1 set */
    unsigned state;             /* set: GLOS_POWER_; -> the state now */
    unsigned can;               /* -> the states the card can be put in: a bit each (1 << GLOS_POWER_), 0 none (a
                                   set is then GLOS_E_REFUSED) */
};
#define GLOS_POWER_ON      0
#define GLOS_POWER_STANDBY 1
#define GLOS_POWER_SUSPEND 2
#define GLOS_POWER_OFF     3

/* IDLE's events: to every app. */
#define GLOS_EV_SAVER GLOS_EV(GLOS_AREA_IDLE, 1)        /* struct glos_ev_saver: the saver came on or went off */
struct glos_ev_saver {
    struct glos_ev ev;
    unsigned on;
};

/* ================================================================== SHELL (04h): the tray, notifications, opening files
 *
 * GLOS draws the task bar, the notification area (the tray) and notifications itself, so they never wait for an
 * app. Any app may have tray icons and send notifications, open files and ask where the shell's parts are. One
 * app is the desktop's shell (DESKTOP): it draws the Start menu and the desktop's icons, and is told when the
 * launcher or the task list is asked for and when an app stops answering.
 *
 * Programs in GLOS's own folder are GLOS's own. Only they may be the shell, bring another app's window to the
 * front (WIN's FOCUS by a window's number) and take the bare desktop's drops (DND's TARGET with no window). */
#define GLOS_SHELL_DESKTOP GLOS_FN(GLOS_AREA_SHELL, 0)  /* struct glos_shell_desktop: the app is the shell, or no longer */
#define GLOS_SHELL_TRAY    GLOS_FN(GLOS_AREA_SHELL, 1)  /* struct glos_tray: an icon in the tray (GLOS_HK_TRAY) */
#define GLOS_SHELL_NOTIFY  GLOS_FN(GLOS_AREA_SHELL, 2)  /* struct glos_notify: a notification (GLOS_HK_NOTIFY) */
#define GLOS_SHELL_POINT   GLOS_FN(GLOS_AREA_SHELL, 3)  /* struct glos_point: where a part of the shell is */
#define GLOS_SHELL_HELP    GLOS_FN(GLOS_AREA_SHELL, 4)  /* struct glos_help: Help, at a page */
#define GLOS_SHELL_OPEN    GLOS_FN(GLOS_AREA_SHELL, 5)  /* struct glos_open: a file or a link opened */
#define GLOS_SHELL_ASSOC   GLOS_FN(GLOS_AREA_SHELL, 6)  /* struct glos_assoc: kinds of file, their apps, verbs */
#define GLOS_SHELL_ENTRY    GLOS_FN(GLOS_AREA_SHELL, 7) /* struct glos_entry: the Start menu's entries, their tasks */
#define GLOS_SHELL_RECENT   GLOS_FN(GLOS_AREA_SHELL, 8) /* struct glos_recent: the documents opened lately */
#define GLOS_SHELL_SCHEDULE GLOS_FN(GLOS_AREA_SHELL, 9) /* struct glos_schedule: commands GLOS runs at times */

struct glos_shell_desktop {
    unsigned size;
    unsigned on;                /* 1: the app is the shell (GLOS_E_REFUSED: another is, or it isn't GLOS's own); 0: no
                                   longer */
};

struct glos_tray {
    unsigned size;
    unsigned icon;              /* its picture: 16 by 16 pixels of 4 bytes, as a window's icon (1024 bytes) */
    unsigned tip;               /* its tooltip: text, at most 63 bytes (0: none) */
    glos_handle handle;         /* -> the icon, until CLOSE */
};
#define GLOS_PROP_TRAY_ICON GLOS_PROP(GLOS_HK_TRAY, 0)  /* data: its picture, 1024 bytes (write) */
#define GLOS_PROP_TRAY_TIP  GLOS_PROP(GLOS_HK_TRAY, 1)  /* text, at most 63 bytes */
#define GLOS_TRAY_MAX 12        /* icons in the tray at once, everybody's */

struct glos_notify {
    unsigned size;
    unsigned title;             /* text, at most 47 bytes */
    unsigned text;              /* text, at most 127 bytes (0: none): up to three lines, broken where it must be
                                   (or at a line feed) */
    unsigned action[2];         /* up to two buttons' labels, at most 15 bytes each (0: none) */
    unsigned icon;              /* a picture as a tray icon's (0: none) */
    unsigned seconds;           /* how long it is shown: 2 to 60; 0: the usual (6) */
    glos_handle handle;         /* -> the notification (GLOS_HK_NOTIFY), until CLOSE (which takes it away, shown or
                                   waiting). GLOS_E_MEMORY: too many are waiting */
};

struct glos_point {
    unsigned size;
    unsigned what;              /* GLOS_POINT_ */
    int x, y, w, h;             /* -> where it is on the screen (w 0: it isn't there now) */
};
#define GLOS_POINT_START 1      /* the Start button (GLOS's mark at the task bar's left) */
#define GLOS_POINT_BAR   2      /* the task bar */
#define GLOS_POINT_TRAY  3      /* the notification area */
#define GLOS_POINT_CLOCK 4      /* the clock */
#define GLOS_POINT_WORK  5      /* the work area: the screen less the task bar */

struct glos_help {
    unsigned size;
    unsigned page;              /* the page (text, at most 31 bytes); 0: the app's own (INIT's) */
};

struct glos_open {
    unsigned size;
    unsigned path;              /* a file ("C:\\DOCS\\A.TXT"), a program, or a link ("http://...") */
    unsigned with;              /* "open with": a program to open it with (0: the one its kind has) */
    unsigned verb;              /* a verb's label ("Add to ZIP": `with` is then the program whose verb it is) */
    unsigned job;               /* -> the job's number (for JOB's calls) */
};

struct glos_assoc {
    unsigned size;
    unsigned what;              /* GLOS_ASSOC_ */
    unsigned kind;              /* a kind of file: a path, a name or an extension ("A.TXT", ".TXT") */
    unsigned text;              /* SET: the program (0: none). VERB: the verb's label */
    unsigned args;              /* VERB: its arguments ("" for none; 0: the verb is taken away) */
    unsigned buffer;            /* GET: where the program goes. VERBS: where the list goes: a line each,
                                   "Label", a tab, the program and its arguments */
    unsigned room;
    unsigned length;            /* -> the whole length (GLOS_E_ROOM: cut) */
};
#define GLOS_ASSOC_GET   0      /* the program that opens the kind ("" if none) */
#define GLOS_ASSOC_SET   1      /* ... set (GLOS.CFG's [open]) */
#define GLOS_ASSOC_VERBS 2      /* what programs offer for the kind beside opening it */
#define GLOS_ASSOC_VERB  3      /* a verb of the calling app's own for the kind, set or taken away (kept in the
                                   app's profile, GLOS.CFG's [program NAME] verbs) */

/* The Start menu's entries are programs' profiles (GLOS.CFG's [program NAME.EXE]: menu, title, group, icon, path,
   args, tasks). A program sets its own; the shell reads them all and draws the menu. */
struct glos_entry {
    unsigned size;
    unsigned what;              /* GLOS_ENTRY_ */
    unsigned title;             /* SET: its name in the menu, at most 40 bytes (0: as it is; the file's name if none) */
    unsigned group;             /* SET: its group there, at most 31 bytes (0: the one INIT gave) */
    unsigned icon;              /* SET: its picture: one of GLOS's kinds ("program", "game", "tool", "text", "picture",
                                   "sound", "network"; 0: as it is) */
    unsigned args;              /* SET: what the menu starts it with (0: as it is). TASK: the task's arguments ("" for
                                   none; 0: the task is taken away) */
    unsigned label;             /* TASK: the task's label, at most 31 bytes */
    unsigned buffer;            /* LIST: where the list goes: a line an entry, tabs between its file's name, title,
                                   group, icon, folder, arguments and tasks ("Label=ARGUMENTS;...") */
    unsigned room;
    unsigned length;            /* -> LIST: the whole length (GLOS_E_ROOM: cut) */
};
#define GLOS_ENTRY_SET    0     /* the calling program is in the Start menu, so described */
#define GLOS_ENTRY_REMOVE 1     /* ... and no longer */
#define GLOS_ENTRY_LIST   2     /* every entry */
#define GLOS_ENTRY_TASK   3     /* a task of the calling program's entry ("New window"): set or taken away */

/* The documents opened lately: at most GLOS_RECENT_MAX, the newest first, each with the program that opened it.
   OPEN adds what it opens; a program adds what it opens itself. Kept in GLOS.DAT. */
struct glos_recent {
    unsigned size;
    unsigned what;              /* GLOS_RECENT_ */
    unsigned path;              /* ADD: the document (a full path, at most 199 bytes). CLEAR: one to forget (0: all) */
    unsigned program;           /* LIST: only those this program opened ("NOTEPAD.EXE"; 0: every one) */
    unsigned buffer;            /* LIST: a line each: the path, a tab, the program */
    unsigned room;
    unsigned length;            /* -> LIST: the whole length (GLOS_E_ROOM: cut) */
};
#define GLOS_RECENT_ADD   0
#define GLOS_RECENT_LIST  1
#define GLOS_RECENT_CLEAR 2
#define GLOS_RECENT_MAX   32

/* Commands GLOS runs at times, whatever apps are running (Disk Tools' backups): kept in GLOS.DAT and run by the
   kernel, as a program of nobody's. A time that passed while GLOS was not running is made up for once, when it
   next runs. A command waits for a full-screen program to end. */
struct glos_schedule {
    unsigned size;
    unsigned what;              /* GLOS_SCHEDULE_ */
    unsigned name;              /* its name: 1 to 23 letters, digits, '-' and '_' */
    unsigned when;              /* SET: "start" (each time GLOS starts), "daily HH:MM", "weekly DAY HH:MM" (sun to
                                   sat); the region's local time */
    unsigned command;           /* SET: the command, as RUN takes one: at most 160 bytes */
    unsigned buffer;            /* LIST: a line each: tabs between the name, its times, its command, how many times
                                   it has run, and when it last did (seconds since 1970, UTC; 0: never) */
    unsigned room;
    unsigned length;            /* -> LIST: the whole length (GLOS_E_ROOM: cut) */
};
#define GLOS_SCHEDULE_SET    0  /* a command scheduled, or its times or command changed */
#define GLOS_SCHEDULE_REMOVE 1  /* (GLOS_E_NOT_FOUND: no such name) */
#define GLOS_SCHEDULE_LIST   2
#define GLOS_SCHEDULE_MAX    16

/* SHELL's events. */
#define GLOS_EV_TRAY   GLOS_EV(GLOS_AREA_SHELL, 1)      /* struct glos_ev_tray: a tray icon was pressed */
#define GLOS_EV_NOTIFY GLOS_EV(GLOS_AREA_SHELL, 2)      /* struct glos_ev_notify: what became of a notification */
#define GLOS_EV_SHELL  GLOS_EV(GLOS_AREA_SHELL, 3)      /* struct glos_ev_shell: (the shell's) something is asked */
struct glos_ev_tray {
    struct glos_ev ev;          /* (handle: the icon) */
    unsigned what;              /* GLOS_TRAY_ */
    int x, y, w, h;             /* where the icon is on the screen (a menu opens above it) */
};
#define GLOS_TRAY_CLICK  1      /* the left button, once */
#define GLOS_TRAY_DOUBLE 2      /* ... twice */
#define GLOS_TRAY_MENU   3      /* the right button: its menu is asked for */
struct glos_ev_notify {
    struct glos_ev ev;          /* (handle: the notification) */
    unsigned what;              /* GLOS_NOTIFY_ */
};
#define GLOS_NOTIFY_ACTION1 1   /* its first button was pressed */
#define GLOS_NOTIFY_ACTION2 2
#define GLOS_NOTIFY_CLOSED  3   /* it was pressed elsewhere, or its close box was */
#define GLOS_NOTIFY_TIMEOUT 4   /* its time ran out */
struct glos_ev_shell {
    struct glos_ev ev;
    unsigned what;              /* GLOS_SHELL_ */
    int x, y;                   /* LAUNCH: where the menu's bottom left goes */
    unsigned window, app;       /* HUNG, ANSWERS: the window's number and its app's id */
};
#define GLOS_SHELL_LAUNCH  1    /* the launcher: the Start button, the Windows key or Ctrl+Esc */
#define GLOS_SHELL_TASKS   2    /* the task list: Ctrl+Alt+Del */
#define GLOS_SHELL_HUNG    3    /* a window's app stopped taking its events ("not answering") */
#define GLOS_SHELL_ANSWERS 4    /* ... and takes them again */
#define GLOS_SHELL_ENTRIES 5    /* a program's profile changed: the Start menu's entries may have */
#define GLOS_SHELL_RECENTS 6    /* the recent documents changed */

/* ================================================================== CLIP (05h): the clipboard
 *
 * The clipboard holds one thing in up to GLOS_CLIP_ITEMS formats at once. GLOS keeps it, so it stays when the
 * app that put it there ends. It is read and written through a transaction (a handle, GLOS_HK_CLIP):
 *   - to write: OPEN for writing, PUT each format, CLOSE. At CLOSE what was put becomes the clipboard, all at
 *     once (a transaction with no PUT changes nothing);
 *   - to read: OPEN for reading, FORMATS, GET, CLOSE. The transaction sees the clipboard as it was at OPEN,
 *     whatever is put meanwhile.
 * There is no limit to the data's size but memory. */
#define GLOS_CLIP_OPEN    GLOS_FN(GLOS_AREA_CLIP, 0)    /* struct glos_clip_open: a transaction */
#define GLOS_CLIP_PUT     GLOS_FN(GLOS_AREA_CLIP, 1)    /* struct glos_clip_data: a format's data into a writing one */
#define GLOS_CLIP_GET     GLOS_FN(GLOS_AREA_CLIP, 2)    /* struct glos_clip_data: a format's data from a reading one */
#define GLOS_CLIP_FORMATS GLOS_FN(GLOS_AREA_CLIP, 3)    /* struct glos_clip_formats: the formats there are */
#define GLOS_CLIP_FORMAT  GLOS_FN(GLOS_AREA_CLIP, 4)    /* struct glos_clip_format: a private format's number */

/* The formats every program knows. A private format is a number GLOS gives a name (FORMAT). */
#define GLOS_CLIP_TEXT    1     /* text, UTF-8, no NUL at its end */
#define GLOS_CLIP_IMAGE   2     /* a picture: struct glos_clip_image, then h rows of w pixels, 0xAARRGGBB each */
#define GLOS_CLIP_FILES   3     /* files and folders: each one's full DOS path and a NUL */
#define GLOS_CLIP_RICH    4     /* styled text: the toolkit's runs (docs/v1/toolkit-2.md) */
#define GLOS_CLIP_PRIVATE 0x100 /* private formats' numbers are from here up */
#define GLOS_CLIP_ITEMS   8     /* formats at once */

struct glos_clip_image {
    unsigned w, h;
};

struct glos_clip_open {
    unsigned size;
    unsigned write;             /* 0 to read, 1 to write */
    glos_handle handle;         /* -> the transaction, until CLOSE */
    unsigned serial;            /* -> how many times the clipboard has been changed since GLOS started */
};

struct glos_clip_data {
    unsigned size;
    glos_handle handle;
    unsigned format;
    unsigned data;              /* PUT: the data. GET: where it goes */
    unsigned length;            /* PUT: its length. GET: the room there */
    unsigned offset;            /* GET: from this byte of the data on (large data, a piece at a time) */
    unsigned total;             /* -> GET: the data's whole length (what was read: the lesser of length and
                                   total - offset) */
};

struct glos_clip_formats {
    unsigned size;
    glos_handle handle;         /* a transaction; or 0: the clipboard as it is now */
    unsigned count;             /* -> how many formats it has (0: empty) */
    unsigned format[GLOS_CLIP_ITEMS];   /* -> each ... */
    unsigned length[GLOS_CLIP_ITEMS];   /* -> ... and its data's length */
    unsigned serial;            /* -> the clipboard's count of changes when this was it (0: never the clipboard) */
};

struct glos_clip_format {
    unsigned size;
    unsigned name;              /* the format's name: up to 47 printable characters without spaces
                                   ("glos.gui.widgets"). The same name is the same number for every program,
                                   until GLOS ends */
    unsigned format;            /* -> its number */
};

/* A transaction's properties. */
#define GLOS_PROP_CLIP_COMMIT GLOS_PROP(GLOS_HK_CLIP, 0)       /* a writing one: 1 (as it starts) what was put
                                                                   becomes the clipboard at CLOSE, 0 it is dropped */

/* CLIP's event: to every app. */
#define GLOS_EV_CLIP GLOS_EV(GLOS_AREA_CLIP, 1)         /* struct glos_ev_clip: the clipboard changed */
struct glos_ev_clip {
    struct glos_ev ev;
    unsigned serial;
};

/* ================================================================== DND (06h): drag and drop
 *
 * The source, with a mouse button down in a window of its own, puts what it offers in a writing clipboard
 * transaction and calls DRAG with it: the data is the drag's, not the clipboard's. From then until the button
 * is let go the pointer is the drag's: the source gets no mouse events, and windows that are targets (TARGET)
 * for a format offered get ENTER, OVER and LEAVE as the pointer crosses them. A target may say what a drop
 * where the pointer is would do (ANSWER); until it does, it is the first effect it takes of those the source
 * allows (copy, then move, then link). At the button's release over a target whose answer is an effect, the
 * target gets DROP with the data (a reading transaction: GET, then CLOSE), and the source DONE with the effect;
 * anywhere else, or at Esc, the source gets DONE with no effect. */
#define GLOS_DND_DRAG   GLOS_FN(GLOS_AREA_DND, 0)       /* struct glos_dnd: start dragging */
#define GLOS_DND_TARGET GLOS_FN(GLOS_AREA_DND, 1)       /* struct glos_dnd_target: a window takes drops */
#define GLOS_DND_ANSWER GLOS_FN(GLOS_AREA_DND, 2)       /* struct glos_dnd_answer: what a drop here would do */

#define GLOS_DND_COPY 1         /* the effects: a bit each */
#define GLOS_DND_MOVE 2
#define GLOS_DND_LINK 4
#define GLOS_DND_PICTURE 64     /* a drag's picture is at most so many pixels each way */

struct glos_dnd {
    unsigned size;
    glos_handle window;         /* the source's window, in which a button is down (GLOS_E_REFUSED otherwise) */
    glos_handle data;           /* a writing clipboard transaction with what is offered: the drag's from now (the
                                   handle is closed by this call, done or refused) */
    unsigned effects;           /* GLOS_DND_: what the source allows */
    unsigned picture;           /* a picture to go with the pointer: pw by ph pixels, 0xAARRGGBB (alpha 0: clear,
                                   anything else: solid); 0: none */
    unsigned pw, ph;
};

struct glos_dnd_target {
    unsigned size;
    glos_handle window;         /* a window of the app's; 0: the desktop itself, where no window is (the shell's) */
    unsigned effects;           /* GLOS_DND_: what it takes; 0: it is a target no more */
    unsigned count;             /* how many formats it takes: 1 to GLOS_CLIP_ITEMS */
    unsigned format[GLOS_CLIP_ITEMS];
};

struct glos_dnd_answer {
    unsigned size;
    glos_handle window;         /* the target the drag is over (GLOS_E_REFUSED: it isn't) */
    unsigned effect;            /* one GLOS_DND_ effect it takes and the source allows, or 0: no drop here */
};

/* DND's events. */
#define GLOS_EV_DND_ENTER GLOS_EV(GLOS_AREA_DND, 1)    /* struct glos_ev_dnd: a drag came over the target */
#define GLOS_EV_DND_OVER  GLOS_EV(GLOS_AREA_DND, 2)    /* ... moved over it */
#define GLOS_EV_DND_LEAVE GLOS_EV(GLOS_AREA_DND, 3)    /* ... left it, or was given up */
#define GLOS_EV_DND_DROP  GLOS_EV(GLOS_AREA_DND, 4)    /* ... was dropped on it: `data` */
#define GLOS_EV_DND_DONE  GLOS_EV(GLOS_AREA_DND, 5)    /* (to the source) the drag is over: `effects` what was done */
struct glos_ev_dnd {
    struct glos_ev ev;          /* (handle: the window; 0 for the desktop) */
    int x, y;                   /* the pointer in the window's client area (the desktop: on the screen) */
    unsigned effects;           /* ENTER, OVER: what the source allows. DROP, DONE: the effect done (DONE 0: none) */
    unsigned count;             /* ENTER, DROP: the formats offered ... */
    unsigned format[GLOS_CLIP_ITEMS];
    glos_handle data;           /* DROP: the data, a reading clipboard transaction of the target's: GET, CLOSE */
};

/* ================================================================== THREAD (07h): an app's own threads
 *
 * An app's threads share its memory. Each has a stack the app gives it, and is scheduled as a thread of GLOS's
 * (docs/supervisor.md 12.7). What only the app's first thread may do: have the app's interrupts and callbacks
 * (they are never run on another), touch the GPU (DRAW's GPU calls: GLOS_E_REFUSED from another), and have an
 * exception handled (one in another thread ends the app, with a crash report). Every GLOS call may be made from
 * any thread; the app's calls are made one at a time, except those that wait (EVENT, SLEEP, JOIN, TAKE, WAIT).
 * DJGPP's C library is not safe for threads: libglos 2.0 has a lock round it (glos/glos2.h). */
#define GLOS_THREAD_CREATE GLOS_FN(GLOS_AREA_THREAD, 0) /* struct glos_thread: a thread, running from now */
#define GLOS_THREAD_EXIT   GLOS_FN(GLOS_AREA_THREAD, 1) /* struct glos_thread_exit: the calling thread ends (no return;
                                                           from the first thread: GLOS_E_REFUSED, its end is the
                                                           program's) */
#define GLOS_THREAD_JOIN   GLOS_FN(GLOS_AREA_THREAD, 2) /* struct glos_thread_join: wait for a thread's end */
#define GLOS_THREAD_LOCK   GLOS_FN(GLOS_AREA_THREAD, 3) /* struct glos_lock: a lock (GLOS_HK_LOCK), free */
#define GLOS_THREAD_TAKE   GLOS_FN(GLOS_AREA_THREAD, 4) /* struct glos_take: the lock is the calling thread's */
#define GLOS_THREAD_GIVE   GLOS_FN(GLOS_AREA_THREAD, 5) /* struct glos_give: ... and free again */
#define GLOS_THREAD_SIGNAL GLOS_FN(GLOS_AREA_THREAD, 6) /* struct glos_signal: a signal (GLOS_HK_SIGNAL), not set */
#define GLOS_THREAD_SET    GLOS_FN(GLOS_AREA_THREAD, 7) /* struct glos_signal_set: set it, or clear it */
#define GLOS_THREAD_WAIT   GLOS_FN(GLOS_AREA_THREAD, 8) /* struct glos_wait_any: until one of several handles is
                                                           ready */
#define GLOS_THREAD_SELF   GLOS_FN(GLOS_AREA_THREAD, 9) /* (EBX 0) -> EAX the calling thread's number: 0 the app's
                                                           first, 1 to 16 the ones it made */

#define GLOS_THREADS 16         /* threads an app may have made and not yet closed, beside its first */

struct glos_thread {
    unsigned size;
    unsigned entry;             /* where it starts (an offset in the caller's code segment): void entry(void *arg),
                                   C's calling convention. It must not return: it ends by EXIT */
    unsigned arg;               /* its argument */
    unsigned stack;             /* the top of its stack (an offset in the caller's stack segment): memory of the
                                   app's own, which must stay until the thread has ended */
    unsigned flags;             /* GLOS_THREAD_ */
    glos_handle handle;         /* -> the thread (GLOS_HK_THREAD). CLOSE doesn't end it */
    unsigned number;            /* -> its number, as SELF gives it there */
    unsigned data;              /* the selector its DS, ES and SS are to be: a writable data segment of the app's
                                   with the base of the caller's. 0: the caller's own DS, ES and SS. (A DJGPP
                                   program's data segment is shrunk for a moment by each of its signals, which
                                   every thread using that selector would feel: libglos gives DJGPP's second
                                   one here) */
};
#define GLOS_THREAD_BACKGROUND 1 /* it runs when nothing else of the desktop's wants to (and a share besides): for
                                   work that must not slow what the user sees */

struct glos_thread_exit {
    unsigned size;
    unsigned code;              /* what JOIN gives */
};

struct glos_thread_join {
    unsigned size;
    glos_handle handle;
    unsigned ms;                /* the longest wait: 0 a look, GLOS_FOREVER */
    unsigned ended;             /* -> 1 it has ended, 0 not yet */
    unsigned code;              /* -> its EXIT's code */
};

struct glos_lock {
    unsigned size;
    glos_handle handle;         /* -> the lock */
};

struct glos_take {
    unsigned size;
    glos_handle handle;
    unsigned ms;                /* the longest wait: 0 a try, GLOS_FOREVER */
    unsigned taken;             /* -> 1 it is the caller's (once more, if it was already: GIVE as many times), 0 not */
};

struct glos_give {
    unsigned size;
    glos_handle handle;         /* (GLOS_E_REFUSED: the caller doesn't hold it) */
};

struct glos_signal {
    unsigned size;
    unsigned flags;             /* GLOS_SIGNAL_ */
    glos_handle handle;         /* -> the signal */
};
#define GLOS_SIGNAL_ONE 1       /* a wait that finds it set clears it: each SET lets one waiter go on. Without:
                                   set, it stays set for every waiter until it is cleared */

struct glos_signal_set {
    unsigned size;
    glos_handle handle;
    unsigned set;               /* 1 set, 0 clear */
};

#define GLOS_WAIT_MAX 8
struct glos_wait_any {
    unsigned size;
    unsigned count;             /* how many handles: 1 to GLOS_WAIT_MAX */
    glos_handle handles[GLOS_WAIT_MAX];  /* threads (ready once ended), signals (set), timers (they have fired
                                   since they were last waited for here) */
    unsigned ms;                /* the longest wait: 0 a look, GLOS_FOREVER */
    int which;                  /* -> the first that is ready (its index), -1 none in that time */
};

/* A thread's properties. */
#define GLOS_PROP_THREAD_NUMBER GLOS_PROP(GLOS_HK_THREAD, 0)    /* its number (read) */
#define GLOS_PROP_THREAD_ENDED  GLOS_PROP(GLOS_HK_THREAD, 1)    /* 1 once it has ended (read) */
#define GLOS_PROP_THREAD_CODE   GLOS_PROP(GLOS_HK_THREAD, 2)    /* its EXIT's code (read) */
#define GLOS_PROP_THREAD_FLAGS  GLOS_PROP(GLOS_HK_THREAD, 3)    /* GLOS_THREAD_ (read) */
#define GLOS_PROP_THREAD_MS     GLOS_PROP(GLOS_HK_THREAD, 4)    /* the processor's time it has had, in ms (read) */
/* A lock's and a signal's. */
#define GLOS_PROP_LOCK_HELD     GLOS_PROP(GLOS_HK_LOCK, 0)      /* 0 free, or the number of the thread holding it
                                                                   plus 1 (read) */
#define GLOS_PROP_SIGNAL_SET    GLOS_PROP(GLOS_HK_SIGNAL, 0)    /* 1 set (read) */

#endif
