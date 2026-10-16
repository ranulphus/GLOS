/* The clipboard (kernel/app/clip.c; GLOS_FN_CLIP, glos clip). */
#ifndef K_CLIP_H
#define K_CLIP_H
#include "types.h"

#define CLIP_MAX 65536u

int clip_set(const char *text, u32 n);          /* 0, or -1 (too long, no memory) */
int clip_set_user(u16 sel, u32 off, u32 n);     /* ... from an app's memory */
u32 clip_get(char *out, u32 room);              /* the clipboard's bytes; as many as fit copied */
int clip_get_user(u16 sel, u32 off, u32 room, u32 *len);       /* ... into an app's memory: 0, or -1 */

#endif
