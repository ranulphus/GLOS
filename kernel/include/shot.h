/* glos shot (kernel/dos/shot.c). */
#ifndef K_SHOT_H
#define K_SHOT_H
#include "types.h"

/* The text-mode screen as a kmalloc'd PNG (length in *len), or 0 with the
   reason in *why. */
u8 *shot_text(u32 *len, const char **why);

#endif
