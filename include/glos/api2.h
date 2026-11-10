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
 * structure (0 for the few calls that take none), ECX and EDX 0 (GLOS uses
 * them to finish a wait an interrupt of the program's own cut short: the call
 * is made again after its handler, with them set). The carry flag clear:
 * done, EAX 0 or what the call says it returns; the carry flag set: refused,
 * EAX a GLOS_E_ code.
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
/* (9: MODES, with the display's settings in step 5) */

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

/* ================================================================== INPUT (03h): keys, the mouse
 * (Phase B's step 3 adds the keyboard's state, raw keys, text, the mouse's capture, double clicks and pads.) */
#define GLOS_EV_KEY    GLOS_EV(GLOS_AREA_INPUT, 1)      /* struct glos_ev_key: a key went down or up while the window
                                                           had the focus */
#define GLOS_EV_MOUSE  GLOS_EV(GLOS_AREA_INPUT, 2)      /* struct glos_ev_mouse: the pointer moved in the window's
                                                           client area, or a button changed there; from a press
                                                           until every button is up, wherever the pointer goes */
#define GLOS_EV_WHEEL  GLOS_EV(GLOS_AREA_INPUT, 3)      /* struct glos_ev_wheel: the wheel turned over the window's
                                                           client area, whether it has the focus or not */
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
};
struct glos_ev_mouse {
    struct glos_ev ev;
    int x, y;                   /* the pointer in the client area */
    int dx, dy;                 /* how far it moved */
    unsigned buttons;           /* those down now: 1 left, 2 right, 4 middle */
    unsigned button;            /* the one that changed, 0 for a movement */
    unsigned down;              /* ... pressed (1) or let go (0) */
    unsigned mods;
};
struct glos_ev_wheel {
    struct glos_ev ev;
    int x, y;
    int notches;                /* positive: away from the user (up) */
    unsigned buttons, mods;
};

#endif
