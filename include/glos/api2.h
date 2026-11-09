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

#endif
