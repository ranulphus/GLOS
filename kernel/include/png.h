/* PNG files (kernel/lib/png.c). */
#ifndef K_PNG_H
#define K_PNG_H
#include "types.h"

struct png {
    u32 w, h;
    u32 row;                            /* a scanline's bytes, with the filter byte it starts with */
    u32 y;                              /* scanlines written */
    u32 a, b;                           /* the Adler-32 so far */
};

#define PNG_HEAD_MAX 813                /* the signature, IHDR and a PLTE of 256 colours */
#define PNG_BLOCK    65535u             /* the scanline bytes one png_rows() takes at most */
#define PNG_ROWS_MAX(bytes) ((bytes) + 35)      /* its output: IDAT with the stream's head and tail, IEND */

/* The head of a w x h picture into out: its length. depth 4 or 8: indexed
   colour, `colours` palette entries in rgb; 24: RGB. */
u32 png_begin(struct png *p, u32 w, u32 h, int depth, const u8 (*rgb)[3], u32 colours, u8 *out);

/* The next n scanlines (p->row bytes each: the filter byte 0, then the
   pixels, most significant bits first), no more than PNG_BLOCK bytes, as
   one IDAT chunk into out: its length. The file ends with the last one. */
u32 png_rows(struct png *p, const u8 *rows, u32 n, u8 *out);

#endif
