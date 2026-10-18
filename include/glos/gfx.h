/* The GLOS 2D library (PRD §9.2, §9.3): what the window system draws with
 * in the kernel, and what an app draws into its window with (sdk/libglos:
 * the same code, in the program). Pure code on memory, so
 * tests/host/gfx_test.c runs it on the host against a model that draws a
 * pixel at a time.
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
#ifndef GLOS_GFX_H
#define GLOS_GFX_H
#ifdef __cplusplus
extern "C" {
#endif

struct gfx_rect {
    int x, y, w, h;                     /* empty when w or h is 0 or less */
};

struct gfx_fmt {
    unsigned char bpp;                  /* 8, 16, 32 */
    unsigned char rpos, rsize, gpos, gsize, bpos, bsize;        /* 16 and 32: each field's lowest bit and width */
};

struct gfx_dev;

struct gfx_surface {
    unsigned char *pix;
    int w, h;
    unsigned pitch;                     /* bytes from a row to the next */
    struct gfx_fmt fmt;
    struct gfx_rect clip;
    const struct gfx_dev *dev;          /* a drawing engine at these pixels (the kernel's); NULL: the CPU alone */
};

/* A drawing engine behind a surface (the kernel's, on a Matrox card: PRD
   §9.3, docs/milestones-m7.md step D). Each hook draws, on rectangles
   already cut to the clip and to the surfaces, exactly what the software
   would, and returns 1; or 0, and the software does it. The software
   touches a device surface's pixels only after sync, which waits for the
   engine to finish. Pixel values are the surface's own (gfx_px). */
struct gfx_dev {
    int (*fill)(struct gfx_surface *s, const struct gfx_rect *r, unsigned px);
    /* r of d from (sx, sy) of s: s the same device's (d itself, overlapping perhaps) or memory (s->dev NULL) */
    int (*copy)(struct gfx_surface *d, const struct gfx_rect *r, const struct gfx_surface *s, int sx, int sy);
    /* r of the 1-bit picture at (x, y): bg GFX_NONE leaves clear bits alone */
    int (*mono)(struct gfx_surface *s, const struct gfx_rect *r, int x, int y, const unsigned char *bits, int w, int h,
                unsigned stride, unsigned fg, unsigned bg);
    void (*sync)(const struct gfx_surface *s);
};

extern const struct gfx_fmt gfx_fmt8, gfx_fmt565, gfx_fmt555, gfx_fmt888;

/* A proportional 1-bit font (font.c; tools/mkfont.py makes the data from a
   TrueType font). A glyph is `height` rows of (width + 7) / 8 bytes, the
   most significant bit leftmost; the pen moves on by its advance. */
struct gfx_glyph {
    unsigned at;                        /* in bits[] */
    unsigned char width, advance;
    signed char left;                   /* the bitmap's left edge from the pen */
};

struct gfx_range {                      /* (M10) more code points: first .. first + count - 1, from glyph[at] on */
    unsigned short first, count, at;
};

struct gfx_font {
    const char *name;
    unsigned char height, ascent;       /* rows; those above the baseline */
    unsigned short first, count;        /* the code points it has: first .. first + count - 1 ... */
    const struct gfx_glyph *glyph;
    const unsigned char *bits;
    unsigned short missing;             /* the glyph for the rest (an index) */
    const struct gfx_range *more;       /* ... and these (M10: Greek, Cyrillic, box drawing ...), in order */
    unsigned short nmore;
};

/* ---- gfx.c */
void gfx_init(struct gfx_surface *s, void *pix, int w, int h, unsigned pitch, const struct gfx_fmt *fmt);
void gfx_clip(struct gfx_surface *s, const struct gfx_rect *r);        /* NULL: the whole surface */
unsigned gfx_px(const struct gfx_fmt *f, unsigned rgb);         /* the format's own value for a colour */
unsigned gfx_rgb(const struct gfx_fmt *f, unsigned px);         /* and back */
unsigned gfx_get(const struct gfx_surface *s, int x, int y);    /* a pixel's own value (0 outside) */

void gfx_fill(struct gfx_surface *s, int x, int y, int w, int h, unsigned rgb);
void gfx_frame(struct gfx_surface *s, int x, int y, int w, int h, unsigned rgb);        /* an outline, a pixel wide */
void gfx_line(struct gfx_surface *s, int x0, int y0, int x1, int y1, unsigned rgb);     /* both ends drawn */

/* Pixels from another surface of the same format (or the same surface: the
   areas may overlap). */
void gfx_blit(struct gfx_surface *d, int dx, int dy, const struct gfx_surface *s, int sx, int sy, int w, int h);

/* A 1-bit picture (rows of `stride` bytes, the most significant bit
   leftmost): its set bits in fg; its clear bits in bg, or left alone when bg
   is GFX_NONE. */
#define GFX_NONE 0xFF000000u
void gfx_mono(struct gfx_surface *s, int x, int y, const unsigned char *bits, int w, int h, unsigned stride, unsigned fg,
              unsigned bg);

/* A true-colour picture (00RRGGBBh pixels, `stride` of them a row). At 8
   bits it is dithered into the colour cube (a 4x4 ordered pattern, fixed to
   the surface's own coordinates, so pictures tile). */
void gfx_image(struct gfx_surface *s, int x, int y, const unsigned *rgb, int w, int h, unsigned stride);

/* ---- pal.c: the palette of every 8-bit surface */
extern const unsigned char gfx_palette[256][3];         /* red, green, blue */
unsigned char gfx_pal_nearest(unsigned rgb);
unsigned char gfx_pal_dither(unsigned rgb, int x, int y);       /* the cube's colour for this pixel of a dithered picture */

/* ---- font.c */
/* Text from the pen at (x, y), y the top of the font's rows: UTF-8, n bytes
   (-1: to the NUL). The pen's x afterwards. */
int gfx_text(struct gfx_surface *s, int x, int y, const struct gfx_font *f, const char *text, int n, unsigned rgb);
int gfx_text_width(const struct gfx_font *f, const char *text, int n);

/* Smoothed text (M10; the toolkit's, sdk/gui/smooth.c, from sdk/gui/fonts/SMOOTH.DAT: tools/mkfont.py --smooth). A
   font's glyphs again, in the same order, as 4-bit coverage (rows of (width + 1) / 2 bytes, the high nibble
   leftmost; 15 is all ink), blended into 16- and 32-bit surfaces in place of the 1-bit ones. The font's own
   advances still move the pen, so smoothed text takes the same room. At 8 bits text stays 1-bit. */
struct gfx_aa_glyph {
    unsigned at;                        /* in bits[] */
    unsigned char width;
    signed char left;
};
struct gfx_aa {
    const struct gfx_font *font;        /* the font it smooths: glyph[i] is font->glyph[i] */
    const struct gfx_aa_glyph *glyph;
    const unsigned char *bits;
    const struct gfx_aa *next;
};
void gfx_smooth(const struct gfx_aa *list);     /* these fonts smoothed from now on (NULL: none; the kernel's) */

/* ---- region.c: areas as rectangles that don't overlap */
#define GFX_REGION_MAX 48

struct gfx_region {
    unsigned n;
    unsigned char rough;                /* it needed more rectangles than it has room for: it covers too much */
    struct gfx_rect r[GFX_REGION_MAX];
};

int gfx_rect_cut(struct gfx_rect *r, const struct gfx_rect *by);        /* r becomes its part inside `by`; 0 if none */
void gfx_region_set(struct gfx_region *g, const struct gfx_rect *r);   /* NULL or empty: nothing */
void gfx_region_add(struct gfx_region *g, const struct gfx_rect *r);
void gfx_region_sub(struct gfx_region *g, const struct gfx_rect *r);
void gfx_region_cut(struct gfx_region *g, const struct gfx_rect *r);   /* what of it lies inside r */
int gfx_region_has(const struct gfx_region *g, int x, int y);
void gfx_region_bounds(const struct gfx_region *g, struct gfx_rect *out);

/* ---- the fonts everything on the desktop is written in (their data: kernel/gfx/fonts) */
extern const struct gfx_font gfx_font_ui, gfx_font_bold, gfx_font_mono;

#ifdef __cplusplus
}
#endif
#endif
