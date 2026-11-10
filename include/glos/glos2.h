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

/* ---- settings (SETTINGS), the screen's modes (DRAW) and idle time (IDLE) */
/* A setting's value (a NUL always; GLOS_E_ROOM: cut); *set 1 if the file sets it, 0 if this is its default.
   GLOS_E_SETTING: no such setting. */
int glos_setting_get(const char *section, const char *key, char *value, unsigned room, int *set);
/* Changes a setting (value NULL: takes it out of the file). *when: GLOS_SET_, when the change takes effect.
   GLOS_E_SETTING: refused, and why (room bytes) says why in words. when and why may be NULL. */
int glos_setting_set(const char *section, const char *key, const char *value, unsigned *when, char *why, unsigned room);
int glos_setting_sections(char *buf, unsigned room);    /* the file's sections, a name a line */
int glos_setting_keys(const char *section, char *buf, unsigned room);  /* "key = value" a line */
/* The schema's entry `index` (s->count: how many there are): its texts in buf, each ended by a NUL: section,
   key, choices, default, label, description. */
int glos_schema(unsigned index, struct glos_schema *s, char *buf, unsigned room);
int glos_setting_watch(const char *section, int on);    /* GLOS_EV_SETTING for it from now ("" or NULL: all) */
int glos_theme(struct glos_theme *t);                   /* the theme now (again at GLOS_EV_THEME) */
int glos_region(struct glos_region *r);                 /* the region's formats now */
int glos_modes(struct glos_mode *out, unsigned room, unsigned *count);  /* the screen modes the card has */
unsigned glos_idle(void);                               /* milliseconds since the last key, movement or press */
glos_handle glos_awake(void);                           /* the screen stays on until this is closed; 0 if refused */
int glos_saver(unsigned what, struct glos_saver *state);        /* GLOS_SAVER_; state may be NULL */
int glos_power(int set, unsigned state, struct glos_power *now);        /* the screen's power told (set 0) or set */

/* ---- the clipboard (CLIP) and drag and drop (DND) */
/* The whole clipboard in one format, the simple way: put (it becomes the clipboard, alone), and get (into buf;
   *length its whole length: more than room, it was cut and the answer is GLOS_E_ROOM; GLOS_E_ARGUMENT: the
   clipboard has nothing in that format). */
int glos_clip_put(unsigned format, const void *data, unsigned length);
int glos_clip_get(unsigned format, void *buf, unsigned room, unsigned *length);
/* Transactions, for several formats at once and for large data. */
glos_handle glos_clip_open(int write);                  /* 0 if refused; glos_close() ends it (a writing one's data
                                                           becomes the clipboard then) */
int glos_clip_add(glos_handle t, unsigned format, const void *data, unsigned length);
/* Up to room bytes of a format from byte `offset` on: how many were read (0 at its end), or minus a GLOS_E_ code.
   *total (if not NULL): its whole length. */
int glos_clip_read(glos_handle t, unsigned format, void *buf, unsigned room, unsigned offset, unsigned *total);
int glos_clip_formats(glos_handle t, struct glos_clip_formats *f);     /* t 0: the clipboard as it is now */
unsigned glos_clip_format(const char *name);            /* a private format's number, 0 if refused */
/* Starts a drag from window w, where a button is down: data a writing transaction with what is offered (closed
   by this call), effects GLOS_DND_ bits, picture pw by ph pixels of 0xAARRGGBB or NULL. */
int glos_dnd(glos_handle w, glos_handle data, unsigned effects, const unsigned *picture, unsigned pw, unsigned ph);
/* Window w (0: the bare desktop) takes drops of these formats with these effects (effects 0: no more). */
int glos_dnd_target(glos_handle w, unsigned effects, const unsigned *formats, unsigned count);
int glos_dnd_answer(glos_handle w, unsigned effect);   /* what a drop where the pointer is would do (0: nothing) */

/* ---- threads (THREAD; docs/supervisor.md 12.7)
 *
 * A thread of the app's own runs fn(arg) on a stack libglos allocates (stack bytes: 0 for 64 KB, at least 16 KB),
 * and ends when fn returns or by glos_thread_exit(). What only the program's first thread may do: have signals,
 * interrupt handlers and real-mode callbacks, and use the GPU. A fault in another thread ends the program.
 *
 * DJGPP's C library is not safe for threads. A program that calls glos_thread() must be linked with
 * @glos2wrap.opt (the SDK's file of linker options: without it the link fails), which puts one lock round the C
 * library's memory, stdio and file functions, those listed in sdk/libglos2/wrap.list. What stays unsafe from
 * two threads at once, and is the program's to keep to one thread or to put between glos_libc_lock() and
 * glos_libc_unlock():
 *   - the macros getc, putc, getchar and putchar (fgetc and fputc are locked);
 *   - functions that keep their answer in memory of their own: strtok, localtime, gmtime, ctime, asctime,
 *     strerror, tmpnam(NULL), and errno itself (one for the program);
 *   - the program's own use of the transfer buffer (__tb) and of __dpmi_int with it;
 *   - exit() while another thread is inside the C library waits for that thread.
 * exit() from any thread ends the program, with its code. Once the program has made a thread, DJGPP's own last
 * step of exit (its selectors and memory freed) is left to GLOS, which ends the other threads and frees it all. */
glos_handle glos_thread(void (*fn)(void *arg), void *arg, unsigned stack, unsigned flags);     /* GLOS_THREAD_; 0 if
                                                           refused (glos_error()) */
void glos_thread_exit(unsigned code) __attribute__((noreturn));        /* the calling thread (not the first) */
/* Waits at most ms for a thread's end: 1 it has ended (*code its code; its handle is closed and its stack freed),
   0 not yet, or minus a GLOS_E_ code. */
int glos_join(glos_handle t, unsigned ms, unsigned *code);
int glos_detach(glos_handle t);                         /* never joined: its stack is freed once it has ended */
unsigned glos_self(void);                               /* the calling thread's number: 0 the first */
glos_handle glos_lock(void);                            /* a lock, free; 0 if refused */
int glos_take(glos_handle lock, unsigned ms);           /* 1 taken (again, if the caller held it), 0 not in that time,
                                                           or minus a GLOS_E_ code */
int glos_give(glos_handle lock);
glos_handle glos_signal(unsigned flags);                /* GLOS_SIGNAL_; 0 if refused */
int glos_signal_set(glos_handle signal, int set);
/* Waits at most ms for the first of n handles (threads, signals, timers; at most GLOS_WAIT_MAX) to be ready:
   *which its index, or -1 if none was in that time. */
int glos_wait(const glos_handle *handles, unsigned n, unsigned ms, int *which);
void glos_libc_lock(void);                              /* the C library's lock: the caller's (as often as it likes) */
void glos_libc_unlock(void);

/* A linear address GLOS gave (a window's pixels) as a pointer; NULL if the program can't have near pointers. It
   moves when the program's memory grows: asked for again after anything that may have allocated (or the program
   starts with _CRT0_FLAG_UNIX_SBRK, and it stays). */
void *glos_ptr(unsigned linear);

#ifdef __cplusplus
}
#endif
#endif
