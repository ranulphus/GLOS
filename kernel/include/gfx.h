/* The 2D library (PRD §9.2, §9.3; docs/milestones-m6.md step C): what the
 * window system draws with, and what an app draws into its window with
 * (API v1, step F). Pure code on memory, so tests/host/gfx_test.c runs it
 * on the host against a model that draws a pixel at a time.
 *
 * A surface is pixels in memory at 8, 16 or 32 bits each:
 *   8    an index into the GLOS palette (pal.c): the 16 colours of text
 *        mode, a 6x6x6 colour cube, 24 greys. The same for every 8-bit
 *        screen and window, so pixels copy between them unchanged.
 *   16   red, green and blue fields as the screen has them (5-6-5, 5-5-5)
 *   32   8 bits each, as the screen has them (the fourth byte unused)
 * A colour is 00RRGGBBh to every function; gfx_px() gives the surface's
 * own value for it (the nearest of the palette's at 8 bits).
 *
 * Everything is clipped to the surface's clip rectangle, which starts as
 * the whole surface. Coordinates are signed and may lie anywhere. */
#ifndef K_GFX_H
#define K_GFX_H
#include "types.h"

struct gfx_rect {
    s32 x, y, w, h;                     /* empty when w or h is 0 or less */
};

struct gfx_fmt {
    u8 bpp;                             /* 8, 16, 32 */
    u8 rpos, rsize, gpos, gsize, bpos, bsize;   /* 16 and 32: each field's lowest bit and width */
};

struct gfx_surface {
    u8 *pix;
    s32 w, h;
    u32 pitch;                          /* bytes from a row to the next */
    struct gfx_fmt fmt;
    struct gfx_rect clip;
};

extern const struct gfx_fmt gfx_fmt8, gfx_fmt565, gfx_fmt555, gfx_fmt888;

/* A proportional 1-bit font (font.c; tools/mkfont.py makes the data from a
   TrueType font). A glyph is `height` rows of (width + 7) / 8 bytes, the
   most significant bit leftmost; the pen moves on by its advance. */
struct gfx_glyph {
    u32 at;                             /* in bits[] */
    u8 width, advance;
    signed char left;                   /* the bitmap's left edge from the pen */
};

struct gfx_font {
    const char *name;
    u8 height, ascent;                  /* rows; those above the baseline */
    u16 first, count;                   /* the code points it has: first .. first + count - 1 */
    const struct gfx_glyph *glyph;
    const u8 *bits;
    u16 missing;                        /* the glyph for the rest (an index) */
};

/* ---- gfx.c */
void gfx_init(struct gfx_surface *s, void *pix, s32 w, s32 h, u32 pitch, const struct gfx_fmt *fmt);
void gfx_clip(struct gfx_surface *s, const struct gfx_rect *r);        /* NULL: the whole surface */
u32 gfx_px(const struct gfx_fmt *f, u32 rgb);   /* the format's own value for a colour */
u32 gfx_rgb(const struct gfx_fmt *f, u32 px);   /* and back */
u32 gfx_get(const struct gfx_surface *s, s32 x, s32 y); /* a pixel's own value (0 outside) */

void gfx_fill(struct gfx_surface *s, s32 x, s32 y, s32 w, s32 h, u32 rgb);
void gfx_frame(struct gfx_surface *s, s32 x, s32 y, s32 w, s32 h, u32 rgb);    /* a rectangle's outline, one pixel wide */
void gfx_line(struct gfx_surface *s, s32 x0, s32 y0, s32 x1, s32 y1, u32 rgb); /* both ends drawn */

/* Pixels from another surface of the same format (or the same surface: the
   areas may overlap). */
void gfx_blit(struct gfx_surface *d, s32 dx, s32 dy, const struct gfx_surface *s, s32 sx, s32 sy, s32 w, s32 h);

/* A 1-bit picture (rows of `stride` bytes, the most significant bit
   leftmost): its set bits in fg; its clear bits in bg, or left alone when bg
   is GFX_NONE. */
#define GFX_NONE 0xFF000000u
void gfx_mono(struct gfx_surface *s, s32 x, s32 y, const u8 *bits, s32 w, s32 h, u32 stride, u32 fg, u32 bg);

/* A true-colour picture (00RRGGBBh pixels, `stride` of them a row). At 8
   bits it is dithered into the colour cube (a 4x4 ordered pattern, fixed to
   the surface's own coordinates, so pictures tile). */
void gfx_image(struct gfx_surface *s, s32 x, s32 y, const u32 *rgb, s32 w, s32 h, u32 stride);

/* ---- pal.c: the palette of every 8-bit surface */
extern const u8 gfx_palette[256][3];    /* red, green, blue */
u8 gfx_pal_nearest(u32 rgb);
u8 gfx_pal_dither(u32 rgb, s32 x, s32 y);       /* the cube's colour for this pixel of a dithered picture */

/* ---- font.c */
/* Text from the pen at (x, y), y the top of the font's rows: UTF-8, n bytes
   (-1: to the NUL). The pen's x afterwards. */
s32 gfx_text(struct gfx_surface *s, s32 x, s32 y, const struct gfx_font *f, const char *text, s32 n, u32 rgb);
s32 gfx_text_width(const struct gfx_font *f, const char *text, s32 n);

/* ---- region.c: areas as rectangles that don't overlap */
#define GFX_REGION_MAX 48

struct gfx_region {
    u32 n;
    u8 rough;                           /* it needed more rectangles than it has room for: it covers too much */
    struct gfx_rect r[GFX_REGION_MAX];
};

int gfx_rect_cut(struct gfx_rect *r, const struct gfx_rect *by);        /* r becomes its part inside `by`; 0 if none */
void gfx_region_set(struct gfx_region *g, const struct gfx_rect *r);   /* NULL or empty: nothing */
void gfx_region_add(struct gfx_region *g, const struct gfx_rect *r);
void gfx_region_sub(struct gfx_region *g, const struct gfx_rect *r);
void gfx_region_cut(struct gfx_region *g, const struct gfx_rect *r);   /* what of it lies inside r */
int gfx_region_has(const struct gfx_region *g, s32 x, s32 y);
void gfx_region_bounds(const struct gfx_region *g, struct gfx_rect *out);

#endif
