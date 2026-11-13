/* libglos: the GLOS API (2.0: glos/api.h) for DJGPP programs (sdk/libglos).
 * The entry is found once, with DPMI 0A00h "GLOS"; without GLOS (plain DOS,
 * another DPMI host) every call fails with -1, so a program can carry on as a
 * plain DOS program when glos_init() does.
 *
 * Functions return 0 for done and a GLOS_E_ code when GLOS refused, or -1
 * when there is no GLOS 2.0; those that return something else say so.
 *
 * With it in libglos.a: the 2D library (glos/gfx.h), the settings' schema,
 * time zones and code pages (glos/config.h, tz.h, cp.h) and BSD sockets
 * (glos/net.h). The toolkit is libgui.a (glos/gui.h). */
#ifndef GLOS_GLOS_H
#define GLOS_GLOS_H
#include <glos/api.h>
#ifdef __cplusplus
extern "C" {
#endif

int glos_call(unsigned fn, void *s);                    /* any call: s its structure (or 0) */
int glos_running(void);                                 /* 1 if GLOS 2.0 answers */

int glos_version(struct glos_version *v);               /* v->size set by the caller */
int glos_init(const char *name, const char *help, const char *category);      /* the program becomes an app */
unsigned glos_app(void);                                /* its id, once it is one */
unsigned glos_why(void);                                /* why glos_init() was refused (GLOS_WHY_), after GLOS_E_INIT */
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
int glos_random(void *buf, unsigned n);                 /* n at most 256 (GLOS_E_ARGUMENT above it) */

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
/* The Matrox card's drawing engine, for a program with a HAL of its own (struct glos_gpu and what follows it in
   glos/api.h): the card described; the engine the app's (it waits for it) and no longer; a block of the card's
   memory (least to most bytes, most 0: least; align a power of two, 4096 at least) and freed; a window showing
   pixels in such a block (GLOS_OWN_PIXELS: its own again). GLOS_E_NO_GPU without such a card. */
int glos_gpu(struct glos_gpu *g);
int glos_gpu_lock(void);
int glos_gpu_unlock(void);
int glos_vram(unsigned least, unsigned most, unsigned align, unsigned *offset, unsigned *bytes);
int glos_vram_free(unsigned offset);
int glos_winvram(glos_handle w, unsigned offset, unsigned pitch);
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

/* ---- files and disks (FS) */
#define GLOS_FS_NEXT(e) ((struct glos_fs_entry *)((char *)(e) + (e)->size))    /* the entry after e in a batch */
/* A batch of a folder's entries into buf (room 1024 at least): *listing 0 to begin with path; it comes back as
   the listing to go on with, or 0 after the folder's last batch. *count entries, one after another. A listing
   not read to its end is closed with glos_close(). GLOS_E_ARGUMENT: no such folder. */
int glos_fs_list(glos_handle *listing, const char *path, void *buf, unsigned room, unsigned *count, unsigned flags);
/* count files' and folders' details at once: paths each ended by a NUL, an entry each in buf in their order
   (attr GLOS_FA_NONE: no such file). */
int glos_fs_info(const char *paths, unsigned count, void *buf, unsigned room, unsigned flags);
int glos_fs_drives(unsigned drive, struct glos_fs_drive *out, unsigned room, unsigned *count);  /* drive 0: all */
glos_handle glos_fs_watch(const char *path);            /* GLOS_EV_FS when the folder or the file changes; 0 if
                                                           refused */
int glos_fs_sectors(struct glos_fs_sectors *s);         /* (size set here) */

/* SHELL */
int glos_shell(int on);                                 /* the app is the shell (GLOS_EV_SHELL), or no longer */
glos_handle glos_tray(const unsigned *icon, const char *tip);   /* an icon in the tray (16 by 16 pixels); 0 if
                                                                   refused (glos_error()). Changed with glos_set_data
                                                                   (GLOS_PROP_TRAY_ICON) and glos_set_text (_TIP) */
/* A notification: text, the two actions and the icon may be 0; seconds 0 for the usual. 0 if refused. */
glos_handle glos_notify(const char *title, const char *text, const char *action1, const char *action2,
                        const unsigned *icon, unsigned seconds);
int glos_point(unsigned what, struct glos_point *out);  /* where a part of the shell is (GLOS_POINT_) */
int glos_help(const char *page);                        /* Help at a page (0: the app's own) */
/* A file or a link opened: with (a program) and verb (a verb's label of that program's) may be 0. The job's
   number, or -1 (glos_error()). */
int glos_open(const char *path, const char *with, const char *verb);
int glos_assoc_get(const char *kind, char *program, unsigned room);    /* the program that opens a kind ("" none) */
int glos_assoc_set(const char *kind, const char *program);             /* (0: none) */
int glos_assoc_verbs(const char *kind, char *list, unsigned room, unsigned *length);    /* "Label\tPROGRAM ARGS" a line */
int glos_assoc_verb(const char *kind, const char *label, const char *args);    /* the app's own (args 0: taken away) */
/* The app's Start menu entry (each 0: as it is; icon: "program", "game", "tool", "text", "picture", "sound",
   "network"), and its end. */
int glos_entry_set(const char *title, const char *group, const char *icon, const char *args);
int glos_entry_remove(void);
int glos_entry_task(const char *label, const char *args);              /* a task of its entry (args 0: taken away) */
int glos_entry_list(char *list, unsigned room, unsigned *length);      /* a line an entry: name, title, group, icon,
                                                                           folder, arguments, tasks; tabs between */
int glos_recent_add(const char *path);                                  /* a document the app opened */
int glos_recent_list(const char *program, char *list, unsigned room, unsigned *length);    /* "PATH\tPROGRAM" lines */
int glos_recent_clear(const char *path);                                /* (0: all of them) */
int glos_schedule_set(const char *name, const char *when, const char *command);    /* "start", "daily HH:MM",
                                                                                       "weekly DAY HH:MM" */
int glos_schedule_remove(const char *name);
int glos_schedule_list(char *list, unsigned room, unsigned *length);   /* name, times, command, runs, last run */

/* ---- running programs (JOB) */
/* A program started (dir, env and profile may be 0; how: GLOS_RUN_; flags: GLOS_RUNF_): the job, 0 if refused
   (glos_error()). With GLOS_RUNF_DETACH there is no job to have: 0, and glos_error() 0 if it was started. */
glos_handle glos_job_run(const char *command, const char *dir, const char *env, unsigned how, unsigned flags,
                         const char *profile);
/* ... with every detail (size is set here): files for its streams among them. */
glos_handle glos_job_start(struct glos_job_run *run);
int glos_job_read(glos_handle job, unsigned stream, void *buf, unsigned room);     /* the bytes, or -1 */
int glos_job_write(glos_handle job, const void *data, unsigned length);            /* its input: those taken, or -1 */
int glos_job_eof(glos_handle job);                                                 /* its input's end */
int glos_job_keys(glos_handle job, const unsigned short *keys, unsigned count);    /* a window's job: those taken */
int glos_job_signal(glos_handle job, unsigned what);                               /* GLOS_SIGNAL_ */
int glos_job_status(glos_handle job, struct glos_job_status *out);
/* 2.1: GLOS's shell as a job (GLOS_RUN_SHELL): a login at a terminal of cols by rows (line 0), or a line run
   (with a terminal, or with none: 0 by 0). Its terminal's bytes are glos_job_read()'s, what is typed there
   glos_job_write()'s. 0 if refused (glos_error()). */
glos_handle glos_job_shell(const char *line, unsigned cols, unsigned rows);
int glos_job_size(glos_handle job, unsigned cols, unsigned rows);                  /* its terminal's size now */

/* ---- sound (AUDIO) */
/* A stream out: rate samples a second, 1 or 2 channels of signed 16 bits, a ring of bytes (a power of two, 4096
   to 65536). *ring is its header (the samples follow at ring->ring), *id its number; either may be NULL. 0 if
   refused (glos_error(): GLOS_E_NO_AUDIO without a card). Its volume, balance, mute, pause, position and NOTIFY
   are properties (glos_get, glos_set). */
glos_handle glos_audio_out(unsigned rate, unsigned channels, unsigned bytes, struct glos_audio_ring **ring, unsigned *id);
/* A stream in, asked for as one out: what the card records, to be read from its ring. */
glos_handle glos_audio_in(unsigned rate, unsigned channels, unsigned bytes, struct glos_audio_ring **ring, unsigned *id);
unsigned glos_audio_read(struct glos_audio_ring *r, void *samples, unsigned room);     /* the bytes read */
unsigned glos_audio_room(const struct glos_audio_ring *r);     /* bytes that can be written now */
unsigned glos_audio_write(struct glos_audio_ring *r, const void *samples, unsigned bytes);     /* those that fitted */
int glos_mixer(struct glos_audio_mixer *m);             /* MIXER (size set here) */
int glos_audio_levels(unsigned id, unsigned *left, unsigned *right);   /* a stream's, or the output's (0) */
int glos_audio_devices(struct glos_audio_device *out, unsigned room, unsigned *count);
/* MIDI: a port out or in (GLOS_MIDI_PORT_), 0 with glos_error(); messages with their times on glos_ticks()'s
   clock (0: now); every note off; an MPU-401's bytes as they are. */
glos_handle glos_midi_out(unsigned port);
glos_handle glos_midi_in(unsigned port);               /* (its messages come as GLOS_EV_MIDI events) */
int glos_midi_send(glos_handle h, const struct glos_midi_msg *m, unsigned count, unsigned *taken, unsigned *queued);
int glos_midi_flush(glos_handle h);
int glos_midi_raw(glos_handle h, const void *bytes, unsigned count);

/* NET: TCP. Addresses as GLOS has them (a.b.c.d: a | b << 8 | c << 16 | d << 24); times in milliseconds. */
int glos_net_resolve(const char *name, unsigned ms, unsigned *address);
glos_handle glos_net_connect(unsigned address, unsigned port, unsigned ms, unsigned flags);    /* 0: glos_error() */
int glos_net_send(glos_handle h, const void *bytes, unsigned count, unsigned ms);      /* the bytes taken, or -GLOS_E_ */
int glos_net_receive(glos_handle h, void *buffer, unsigned room, unsigned ms, unsigned *state); /* the bytes copied
                                                           (0 and *state CLOSED: the other end is done), or -GLOS_E_ */
glos_handle glos_net_listen(unsigned port, unsigned backlog, unsigned flags);
glos_handle glos_net_accept(glos_handle listener, unsigned ms, unsigned flags, unsigned *address, unsigned *port);
int glos_net_state(struct glos_net_if *out, unsigned room, unsigned *count);
int glos_net_close(glos_handle h, int reset);           /* closed: what waits to be sent is sent first, or (reset)
                                                           dropped and the connection reset */

/* CAPTURE: a window of a program this one started (or its own), by the window's number: its picture
   (GLOS_CAPTURE_RGBA or _PNG; *length the bytes there are), keys and mouse events into it. */
int glos_capture_pixels(unsigned window, unsigned format, void *buffer, unsigned room, unsigned *length, unsigned *w,
                        unsigned *h);
int glos_capture_keys(unsigned window, const struct glos_capture_key *keys, unsigned count);
int glos_capture_mouse(unsigned window, const struct glos_capture_mouse *events, unsigned count);
/* SECRETS: this app's secrets by name (flags GLOS_SECRET_SHARED: one that programs share, by the user's leave).
   GLOS may ask the user for the master password first: the call waits for that. */
int glos_secret_put(const char *name, const void *value, unsigned length, unsigned flags);
int glos_secret_get(const char *name, void *buffer, unsigned room, unsigned *length, unsigned flags);
int glos_secret_remove(const char *name, unsigned flags);
/* DRAW's CACHE: some of the program's DOS memory write-combining (GLOS_CACHE_WC) or write-back again; for any
   program. 0, or GLOS_E_. */
int glos_cache(unsigned address, unsigned bytes, unsigned type);
/* DEBUG: a channel to a job of this app's (run with GLOS_RUNF_DEBUG, or running), 0 with glos_error(); gdb's
   remote protocol's bytes to the stub and from it. */
glos_handle glos_debug_attach(glos_handle job);
int glos_debug_write(glos_handle h, const void *bytes, unsigned count);
int glos_debug_read(glos_handle h, void *buffer, unsigned room, unsigned ms);  /* the bytes copied, or -GLOS_E_ */
/* EMBED: a window (by its number) of a program this one started, shown inside w at a rectangle of its client
   area; cw 0: let go. */
int glos_embed(glos_handle w, unsigned window, int x, int y, int cw, int ch);

/* ---- the system (SYS) */
/* Figures of one kind (GLOS_STAT_) into buf: structures, each with its size first, or text. *length the bytes
   there are, *count the structures given (either may be 0). GLOS_E_ROOM: cut. */
int glos_sys_stat(unsigned kind, void *buf, unsigned room, unsigned *length, unsigned *count);
int glos_programs(struct glos_program *out, unsigned room, unsigned *count);   /* (room in bytes) */
int glos_sys_kill(unsigned what, unsigned id);          /* GLOS_KILL_ */
/* GLOS's log from pos on (0: the oldest kept): the bytes read, or -1; *next where to go on. follow 1:
   GLOS_EV_LOG when there is more. */
int glos_sys_log(unsigned pos, char *buf, unsigned room, unsigned *next, int follow);

/* ---- threads (THREAD; docs/supervisor.md 12.7)
 *
 * A thread of the app's own runs fn(arg) on a stack libglos allocates (stack bytes: 0 for 64 KB, at least 16 KB),
 * and ends when fn returns or by glos_thread_exit(). What only the program's first thread may do: have signals,
 * interrupt handlers and real-mode callbacks, and use the GPU. A fault in another thread ends the program.
 *
 * DJGPP's C library is not safe for threads. A program that calls glos_thread() must be linked with
 * @gloswrap.opt (the SDK's file of linker options: without it the link fails), which puts one lock round the C
 * library's memory, stdio and file functions, those listed in sdk/libglos/wrap.list. What stays unsafe from
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
