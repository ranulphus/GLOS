/* NDOS, the shell's command for native machines and their volume (kernel/vm/ndcmd.c), as diskcmd.h's DISK. */
#ifndef K_NDCMD_H
#define K_NDCMD_H
#include "types.h"

struct ndcmd;

struct ndcmd *ndcmd_start(const char *args, void (*kick)(void));   /* 0: another NDOS is under way, or no memory */
u32 ndcmd_read(struct ndcmd *c, char *buf, u32 room);               /* what it has said since the last read */
int ndcmd_done(const struct ndcmd *c, int *code);                   /* 1 once it has ended: its exit code */
void ndcmd_free(struct ndcmd *c);                                   /* its reader has done with it, or has gone */

#endif
