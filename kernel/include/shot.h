/* glos shot (kernel/dos/shot.c). */
#ifndef K_SHOT_H
#define K_SHOT_H
#include "types.h"

struct shot;

#define SHOT_PIECE 24576u               /* the most shot_read() gives at once */

/* A screenshot: as a PNG, or raw (a 16-byte head and RGB). wake() is called
   (interrupts off) when the VM thread has taken it. 0 without memory. */
struct shot *shot_new(int raw, void (*wake)(void));
int shot_poll(struct shot *s);          /* 0: not taken yet; 1: taken; -1: failed, shot_why() */
const char *shot_why(const struct shot *s);
u32 shot_read(struct shot *s, u8 *out); /* once taken: the file's next bytes; 0 at its end */
void shot_free(struct shot *s);

/* Every INT 10h, as it was called and AX as it came back (0 where that
   isn't known: only a VBE mode set needs it). */
void shot_int10(u32 ax, u32 bx, u32 ax_out);
void shot_int10_begin(void);            /* GLOS is about to make an INT 10h for a program (shot_int10 at its return) */
void shot_session_end(void);            /* the program has gone: none of its calls is under way */

#endif
