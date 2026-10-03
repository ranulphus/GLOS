/* PNG files (kernel/lib/png.c). */
#ifndef K_PNG_H
#define K_PNG_H
#include "types.h"

/* A w x h image of palette indices 0-15 (one byte each): a kmalloc'd PNG,
   its length in *len; 0 without memory. */
u8 *png_indexed4(u32 w, u32 h, const u8 rgb[16][3], const u8 *pix, u32 *len);

#endif
