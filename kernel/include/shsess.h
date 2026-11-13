/* A shell's session in the kernel (kernel/shell/shsess.c; docs/v1/shell.md, step S2): GLOS's shell
 * (sdk/shell) on a thread of its own, with a terminal that is bytes each way. (kernel/include/session.h is
 * another thing: a DOS program's hold of the screen.) */
#ifndef K_SHSESS_H
#define K_SHSESS_H
#include "types.h"

struct shsess;

/* For the terminal's side (the ssh thread; later the CONSOLE call and the PC's own screen). cols 0: no
   terminal. kick(arg) is called, from the session's thread, when there is something to read or it has ended. */
struct shsess *shsess_new(u32 cols, u32 rows, void (*kick)(void *arg), void *arg);  /* NULL: no memory, or no DOS
                                                            server */
struct shsess *shsess_app(u32 cols, u32 rows, const char *line, const char *dir, void (*kick)(void *arg), void *arg);
                                                         /* an app's (GLOS_RUN_SHELL): a login (line 0) or a line run,
                                                            with a terminal or none, in a folder (0: DOS's own) */
struct shsess *shsess_exec(const char *line, void (*kick)(void *arg), void *arg);   /* no terminal: the one line run, the
                                                            session's end its exit code; what it writes as it is
                                                            (no UTF-8 made of it), errors apart (stream 1) */
u32 shsess_read(struct shsess *s, int stream, u8 *buf, u32 room);       /* what it has written (a terminal's: UTF-8,
                                                            stream 0 alone) */
u32 shsess_waiting(const struct shsess *s);              /* ... how much of it is yet to be read */
u32 shsess_write(struct shsess *s, const u8 *d, u32 n);  /* what was typed there: the bytes taken (the rest is lost) */
void shsess_resize(struct shsess *s, u32 cols, u32 rows);
int shsess_ended(const struct shsess *s, int *code);     /* 1 once it has ended (EXIT, Ctrl+D): its exit code */
void shsess_free(struct shsess *s);                      /* the terminal has gone, or the end is taken: the session
                                                            ends if it hasn't, and its memory goes once it has */
u32 shsess_feed(struct shsess *s, int stream, const u8 *d, u32 n);       /* (the ssh thread) what a command of the
                                                            server's wrote for it: the bytes taken */
void shsess_fed(struct shsess *s, int code);             /* ... and that command's end */
/* A GLOS_INIT_TERMINAL app's side (interrupts off; kernel/app/api2sys.c): 1 if the terminal is job seq's app's. */
int shsess_term_is(struct shsess *s, u32 seq, u32 *cols, u32 *rows);
u32 shsess_term_read(struct shsess *s, u8 *buf, u32 room);      /* what was typed for it */
u32 shsess_term_write(struct shsess *s, const u8 *d, u32 n);    /* bytes for the terminal: those taken */
void shsess_turn(void);                                  /* the ssh thread's turn: sessions freed whose threads
                                                            have ended since shsess_free() */

#endif
