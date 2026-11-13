/* The shell's DISK command (kernel/disk/diskcmd.c; docs/v1/shell.md, step S3). */
#ifndef K_DISKCMD_H
#define K_DISKCMD_H
#include "types.h"

struct diskcmd;

/* The command's work begun on a thread of its own: args as typed after DISK. kick is called (from that thread)
   when it has said something or ended. NULL: no memory, or another DISK is under way. */
struct diskcmd *diskcmd_start(const char *args, void (*kick)(void));
u32 diskcmd_read(struct diskcmd *c, char *buf, u32 room);       /* what it has said since the last read */
int diskcmd_done(const struct diskcmd *c, int *code);           /* 1 once it has ended: its exit code (0; 1 it
                                                                   failed or was refused; 2 nothing done: the
                                                                   command wasn't understood, or wants /Y) */
void diskcmd_free(struct diskcmd *c);   /* its reader has done with it, or has gone: the work goes on to its end */

#endif
