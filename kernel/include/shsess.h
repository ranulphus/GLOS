/* A shell's session in the kernel (kernel/shell/shsess.c; docs/v1/shell.md, step S2): GLOS's shell
 * (sdk/shell) on a thread of its own, with a terminal that is bytes each way. (kernel/include/session.h is
 * another thing: a DOS program's hold of the screen.) */
#ifndef K_SHSESS_H
#define K_SHSESS_H
#include "types.h"

struct shsess;

/* For the terminal's side (the ssh thread; later the CONSOLE call and the PC's own screen). cols 0: no
   terminal. kick is called, from the session's thread, when there is something to read or it has ended. */
struct shsess *shsess_new(u32 cols, u32 rows, void (*kick)(void));      /* NULL: no memory, or no DOS server */
u32 shsess_read(struct shsess *s, u8 *buf, u32 room);    /* what it has written for the terminal (UTF-8) */
u32 shsess_write(struct shsess *s, const u8 *d, u32 n);  /* what was typed there: the bytes taken (the rest is lost) */
void shsess_resize(struct shsess *s, u32 cols, u32 rows);
int shsess_ended(const struct shsess *s, int *code);     /* 1 once it has ended (EXIT, Ctrl+D): its exit code */
void shsess_free(struct shsess *s);                      /* the terminal has gone, or the end is taken: the session
                                                            ends if it hasn't, and its memory goes once it has */
void shsess_turn(void);                                  /* the ssh thread's turn: sessions freed whose threads
                                                            have ended since shsess_free() */

#endif
