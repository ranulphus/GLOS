/* The GLOS API, version 1 (PRD §10). M6 builds it up: what is here is what
 * exists.
 *
 * A program finds the API with DPMI function 0A00h and the name "GLOS"
 * (INT 31h, AX=0A00h, DS:ESI the name): ES:EDI comes back as the entry, a
 * far procedure. Plain DOS and other DPMI hosts have no such name, so a
 * program that gets no answer runs as it always did. A function is called
 * by its number in EAX, with its arguments in EBX, ECX, EDX, ESI and EDI,
 * and returns in the same registers; the carry flag is set for an error,
 * whose GLOS_E_ code is then in EAX. Numbers are never reused, and new
 * functions are added at the end. Structures passed by address start with
 * their own size.
 *
 * A program becomes a GLOS app with GLOS_FN_INIT: from then on it runs
 * beside other apps on a thread of its own, and the rest of the API is
 * open to it. Until then it is an exclusive session, as any DOS program.
 *
 * sdk/libglos is the C side of this for DJGPP (glos_init() and the rest). */
#ifndef GLOS_API_H
#define GLOS_API_H

#define GLOS_API_MAJOR   1
#define GLOS_API_MINOR   0
#define GLOS_API_VERSION ((GLOS_API_MAJOR << 16) | GLOS_API_MINOR)

/* Functions (EAX). */
#define GLOS_FN_VERSION  0      /* -> EAX the API's version (major << 16 | minor), EBX capability bits (none yet),
                                   ECX the number of functions. For any program. */
#define GLOS_FN_INIT     1      /* EBX the version the program was built for (its major must be the API's)
                                   -> EAX 0, EBX the app's id. The program is a GLOS app. Refused with
                                   GLOS_E_INIT and a GLOS_INIT_ reason in EBX: the program runs on as a session. */
#define GLOS_FN_YIELD    2      /* the rest of this turn to other threads */
#define GLOS_FN_SLEEP    3      /* EBX milliseconds */
#define GLOS_FN_TICKS    4      /* -> EAX milliseconds since GLOS started (it wraps after 49 days) */
#define GLOS_FN_EVENT    5      /* DS:EBX a struct glos_event (its size set), ECX how long to wait for one in
                                   milliseconds (0: not at all; FFFFFFFFh: for ever) -> EAX 1 and the event, or 0 */
#define GLOS_FN_WINDOW   6      /* DS:EBX a struct glos_window: a window with a client area of w x h at x, y
                                   (GLOS_WIN_ flags, title) -> EAX 0 and the structure filled in: its id, and its
                                   pixels (GLOS_FN_PIXELS). Hidden until GLOS_FN_SHOW. */
#define GLOS_FN_CLOSE    7      /* EBX a window's id: it is gone */
#define GLOS_FN_SHOW     8      /* EBX id, ECX 1 shown (and given the focus), 0 hidden */
#define GLOS_FN_MOVE     9      /* EBX id, ECX x, EDX y: its frame's corner on the screen */
#define GLOS_FN_RESIZE   10     /* EBX id, ECX w, EDX h of its client area: its pixels are kept from the top left,
                                   in new memory (GLOS_FN_PIXELS again) */
#define GLOS_FN_TITLE    11     /* EBX id, DS:ECX the title (ASCIIZ, UTF-8) */
#define GLOS_FN_PIXELS   12     /* EBX id, DS:ECX a struct glos_window -> filled in: where its pixels are now, their
                                   size and format */
#define GLOS_FN_PRESENT  13     /* EBX id, DS:ECX a struct glos_rect of its client area the app has drawn in (ECX 0:
                                   all of it): the screen follows */
#define GLOS_FN_FOCUS    14     /* EBX id: the focus, and above the other windows */
#define GLOS_FN_SCREEN   15     /* DS:EBX a struct glos_window -> the screen's w, h and format (pixels 0) */
#define GLOS_FN_COUNT    16

/* Errors (EAX, with the carry flag). */
#define GLOS_E_FUNCTION  1      /* no such function */
#define GLOS_E_NOT_APP   2      /* for apps only: GLOS_FN_INIT first (or called from a real-mode callback) */
#define GLOS_E_ARGUMENT  3
#define GLOS_E_VERSION   4      /* GLOS_FN_INIT: the program wants another major version */
#define GLOS_E_INIT      5      /* GLOS_FN_INIT: refused, EBX says why */
#define GLOS_E_WINDOW    6      /* no such window of the app's */
#define GLOS_E_MEMORY    7      /* no memory for it */

/* Events (GLOS_FN_EVENT). */
#define GLOS_EV_KEY      1      /* a key went down or up while a window of the app's had the focus */
#define GLOS_EV_MOUSE    2      /* the pointer moved in a window's client area, or a button changed there */
#define GLOS_EV_FOCUS    3      /* the window got the focus (down 1) or lost it (0) */
#define GLOS_EV_RESIZE   4      /* the window's pixels are elsewhere now (GLOS_FN_PIXELS), and what the app drew
                                   since is lost: the user gave it another size (x, y: its client area's width
                                   and height now; what was there is kept from the top left), or the screen has
                                   another format (every window: cleared) */
#define GLOS_EV_CLOSE    5      /* the user asked for the window to be closed (its box, Alt-F4) */

#define GLOS_MOD_SHIFT   1
#define GLOS_MOD_CTRL    2
#define GLOS_MOD_ALT     4

struct glos_event {
    unsigned size;              /* of this structure, set by the caller */
    unsigned type;              /* GLOS_EV_ */
    unsigned window;            /* the window's id */
    unsigned time;              /* milliseconds since GLOS started */
    int x, y;                   /* MOUSE: the pointer in the client area; RESIZE: its new width and height */
    unsigned buttons;           /* MOUSE: the buttons down now (1 left, 2 right, 4 middle) */
    unsigned key;               /* KEY: the key, as a set 1 scan code (100h added for those with an E0 prefix);
                                   MOUSE: the button that changed (as in `buttons`), 0 for a movement */
    unsigned ch;                /* KEY: its character (Unicode), 0 if it has none */
    unsigned mods;              /* GLOS_MOD_: what was held */
    unsigned down;              /* KEY, MOUSE's button: pressed (1) or released (0); FOCUS: got or lost */
};

/* Windows (GLOS_FN_WINDOW and the rest). */
#define GLOS_WIN_FIXED   1      /* its size is not the user's to change */
#define GLOS_WIN_BARE    2      /* no title bar or border */
#define GLOS_WIN_ANYWHERE 4     /* GLOS_FN_WINDOW: where it goes is GLOS's to choose (x and y are not looked at) */

struct glos_rect {
    int x, y, w, h;
};

struct glos_window {
    unsigned size;              /* of this structure, set by the caller */
    unsigned id;                /* out */
    int x, y, w, h;             /* in for GLOS_FN_WINDOW; out: the client area's size (the screen's) */
    unsigned flags;             /* GLOS_WIN_ */
    char title[48];
    unsigned pixels;            /* out: the linear address of its pixels, mapped into the app (0: none) */
    unsigned pitch;             /* out: bytes from a row to the next */
    unsigned bpp;               /* out: 8 (an index into the GLOS palette), 16 or 32 */
    unsigned char rpos, rsize, gpos, gsize, bpos, bsize, pad[2];        /* out: 16 and 32 bits: each field */
};

/* Why GLOS_FN_INIT was refused (EBX). */
#define GLOS_INIT_ALREADY   1   /* it is an app already */
#define GLOS_INIT_NESTED    2   /* called by a program another program started, or from a handler or a callback */
#define GLOS_INIT_16BIT     3   /* a 16-bit DPMI client */
#define GLOS_INIT_PARENT    4   /* GLOS didn't start the program itself (a batch file did, or COMMAND.COM) */
#define GLOS_INIT_EXCLUSIVE 5   /* it was started to run as an exclusive session (glos run --exclusive, direct mode) */
#define GLOS_INIT_MEMORY    6   /* no memory for another app, or too many */

/* sdk/libglos (DJGPP). */
#ifndef GLOS_KERNEL
#ifdef __cplusplus
extern "C" {
#endif
unsigned glos_version(void);    /* the API's version, 0 without GLOS */
int glos_init(void);            /* become an app: 0; -1 without GLOS or when refused (glos_error()) */
int glos_error(void);           /* the last failed call's GLOS_E_ code (0: there is no GLOS) ... */
int glos_reason(void);          /* ... and for GLOS_E_INIT, the GLOS_INIT_ reason */
unsigned glos_id(void);         /* the app's id, 0 before glos_init() */
void glos_yield(void);
void glos_sleep(unsigned ms);
unsigned glos_ticks(void);      /* milliseconds since GLOS started */
int glos_event(struct glos_event *ev, unsigned wait_ms);        /* 1 and the event; 0: none came in that time */
int glos_window(struct glos_window *w);         /* w's x, y, w, h, flags and title in; 0 and the rest out, or -1 */
int glos_close(unsigned id);
int glos_show(unsigned id, int on);
int glos_move(unsigned id, int x, int y);
int glos_resize(unsigned id, int w, int h);
int glos_title(unsigned id, const char *title);
int glos_pixels(unsigned id, struct glos_window *w);    /* where the window's pixels are now (after a resize) */
int glos_present(unsigned id, const struct glos_rect *r);       /* NULL: all of it */
int glos_focus(unsigned id);
int glos_screen(struct glos_window *w);
struct gfx_surface;
int glos_surface(const struct glos_window *w, struct gfx_surface *s);   /* the window's pixels as a surface of the 2D
                                           library (glos/gfx.h), to draw in; -1: they can't be reached */
void *glos_ptr(unsigned linear);        /* a pointer for a linear address GLOS gave (a window's pixels); to be asked
                                           for again when the program has allocated memory since, unless it runs
                                           with _CRT0_FLAG_UNIX_SBRK */
#ifdef __cplusplus
}
#endif
#endif

#endif
