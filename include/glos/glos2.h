/* libglos 2.0: API 2.0 (glos/api2.h) for DJGPP programs (sdk/libglos2).
 * The entry is found once, with DPMI 0A00h "GLOS"; without GLOS (plain DOS,
 * another DPMI host) every call fails with -1, so a program can carry on as a
 * plain DOS program when glos_init() does.
 *
 * Functions return 0 for done and a GLOS_E_ code when GLOS refused, or -1
 * when there is no GLOS 2.0; those that return something else say so.
 *
 * During phase B this is beside libglos 1.x (glos/api.h, sdk/libglos); a
 * program uses one or the other. */
#ifndef GLOS_GLOS2_H
#define GLOS_GLOS2_H
#include <glos/api2.h>
#ifdef __cplusplus
extern "C" {
#endif

int glos_call(unsigned fn, void *s);                    /* any call: s its structure (or 0) */
int glos_running(void);                                 /* 1 if GLOS 2.0 answers */

int glos_version(struct glos_version *v);               /* v->size set by the caller */
int glos_init(const char *name, const char *help, const char *category);      /* the program becomes an app */
unsigned glos_app(void);                                /* its id, once it is one */
int glos_close(glos_handle h);

int glos_get(glos_handle h, unsigned prop, int *value);
int glos_set(glos_handle h, unsigned prop, int value);
int glos_get_text(glos_handle h, unsigned prop, char *buf, unsigned room);     /* GLOS_E_ROOM: cut */
int glos_set_text(glos_handle h, unsigned prop, const char *text);
int glos_get_data(glos_handle h, unsigned prop, void *buf, unsigned room, unsigned *length);   /* GLOS_E_ROOM: cut */
int glos_set_data(glos_handle h, unsigned prop, const void *data, unsigned length);

/* The next event (areas: a bit an area, 0 all), waiting at most ms (0 a look, GLOS_FOREVER): its size, 0 if
   none came, or minus a GLOS_E_ code (-GLOS_E_ROOM: room too small, the event stays). */
int glos_event(void *buf, unsigned room, unsigned ms, unsigned areas);
int glos_post(unsigned code, unsigned a, unsigned b, unsigned c);
glos_handle glos_timer(unsigned ms, int repeat);        /* 0 if refused */

unsigned glos_ticks(void);                              /* milliseconds since GLOS started (0 without GLOS) */
int glos_sleep(unsigned ms);
int glos_yield(void);
int glos_time(struct glos_time *t);                     /* t->size set by the caller */
int glos_random(void *buf, unsigned n);                 /* n at most 256 */

/* ---- windows (WIN) and their pixels (DRAW) */
glos_handle glos_window(const char *title, int x, int y, int w, int h, unsigned kind, unsigned flags, glos_handle owner);
                                                        /* hidden until glos_show(); 0 if refused (glos_error()) */
int glos_error(void);                                   /* why the last call that gives a handle gave 0 */
int glos_show(glos_handle w, unsigned state);           /* GLOS_WS_ */
int glos_move(glos_handle w, int x, int y);             /* its frame's corner */
int glos_resize(glos_handle w, int cw, int ch);         /* its client area: GLOS_EV_SIZE comes, its pixels are new */
int glos_focus(glos_handle w);
int glos_windows(unsigned app, struct glos_win_info *out, unsigned max, unsigned *count);      /* GLOS_E_ROOM: more */
int glos_drag(glos_handle w, unsigned part);            /* GLOS_DRAG_ */
int glos_pixels(glos_handle w, struct glos_pixels *p);  /* where its pixels are now (after every GLOS_EV_SIZE) */
int glos_present(glos_handle w, int x, int y, int cw, int ch);  /* drawn there (cw 0: everywhere): to the screen */
int glos_screen(struct glos_screen *s);
/* Pixels of the app's own (8, 16 or 32 bits in `format`; an 8-bit picture's 256 colours in palette, 0x00RRGGBB,
   or NULL: GLOS's palette) into the window's client area at (x, y), and to the screen. */
int glos_blit(glos_handle w, int x, int y, const void *pixels, int pw, int ph, unsigned pitch, const struct glos_format *format,
              const unsigned *palette);
/* ---- input (INPUT) */
int glos_keys(struct glos_keys *k);                     /* the keyboard as it is now */
int glos_capture(glos_handle w, int on);                /* the mouse to w, wherever the pointer is; or let go */
int glos_pads(struct glos_pads *p, int events);         /* the pads as they are; events: GLOS_EV_PAD from now on */
int glos_gameport(struct glos_gameport *g);
int glos_input_mode(unsigned flags);                    /* GLOS_INPUT_ */

/* A linear address GLOS gave (a window's pixels) as a pointer; NULL if the program can't have near pointers. It
   moves when the program's memory grows: asked for again after anything that may have allocated (or the program
   starts with _CRT0_FLAG_UNIX_SBRK, and it stays). */
void *glos_ptr(unsigned linear);

#ifdef __cplusplus
}
#endif
#endif
