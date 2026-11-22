/* Native machines (docs/v1/native-dos.md, native-DOS sessions, NS1-NS2): V86 machines with no DOS under them,
 * whose INT 21h GLOS's own DOS answers (kernel/vm/ndm.c over kernel/ndos/dos21.c). A command starts one with a
 * program and reads what the program writes, as diskcmd.h's do. */
#ifndef K_NDM_H
#define K_NDM_H
#include "types.h"

struct ndm;
struct fsv_vol;

/* A machine started with its first program: path on volume v (the machine's C:, or 0 for none), or image (len
   bytes, a .COM or an .EXE) named name in C:'s root; tail its command tail (" abc"). 0 with *dos_err DOS's error
   for the load (02h no such file, 0Bh not a program, 08h no memory). kick: called when it has something to read
   or has ended. */
struct ndm *ndm_start(struct fsv_vol *v, const char *path, const u8 *image, u32 len, const char *name,
                      const char *tail, void (*kick)(void), int *dos_err);
u32 ndm_read(struct ndm *m, char *buf, u32 room);       /* what the program wrote since the last read */
int ndm_done(const struct ndm *m, int *code);           /* 1 once it has ended: its first program's exit code */
void ndm_free(struct ndm *m);                           /* its reader has done with it (or gone) */
void ndm_type(struct ndm *m, const u8 *b, u32 n);       /* its terminal's bytes, typed (NS3) */
void ndm_input_end(struct ndm *m);                      /* nothing more will be typed: a wait for a key ends it */
void ndm_stop(struct ndm *m);                           /* its command has gone: it ends */

/* A test program built into the kernel (NS1, before native volumes): "HELLO". */
extern const u8 ndm_hello[];
extern const u32 ndm_hello_len;

#endif
