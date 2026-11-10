/* Pixels from one format into another, a row at a time (include/glos/gfx.h:
 * gfx_convert): for pictures an app has in a format of its own that go into
 * a window's pixels, which are in the screen's (API 2.0's DRAW BLIT). Pure
 * code: tests/host/gfx_test.c. */
#include "gfx.h"

static unsigned rd(const unsigned char *p, int bytes)
{
    return bytes == 1 ? p[0] : bytes == 2 ? (unsigned)(p[0] | p[1] << 8) : (unsigned)p[0] | (unsigned)p[1] << 8
         | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

static void wr(unsigned char *p, int bytes, unsigned v)
{
    p[0] = (unsigned char)v;
    if (bytes > 1)
        p[1] = (unsigned char)(v >> 8);
    if (bytes > 2)
        p[2] = (unsigned char)(v >> 16), p[3] = (unsigned char)(v >> 24);
}

/* A field of `size` bits as 8: its top bits repeated below, so that full is FFh. */
static unsigned up8(unsigned v, int size)
{
    if (size >= 8)
        return v >> (size - 8);
    if (size <= 0)
        return 0;
    v <<= 8 - size;
    return v | v >> size | (size < 4 ? v >> 2 * size : 0);
}

int gfx_fmt_ok(const struct gfx_fmt *f)
{
    if (f->bpp == 8)
        return 1;
    if (f->bpp != 16 && f->bpp != 32)
        return 0;
    return f->rsize >= 1 && f->rsize <= 8 && f->gsize >= 1 && f->gsize <= 8 && f->bsize >= 1 && f->bsize <= 8
        && f->rpos + f->rsize <= f->bpp && f->gpos + f->gsize <= f->bpp && f->bpos + f->bsize <= f->bpp;
}

void gfx_convert(const struct gfx_fmt *df, unsigned char *dst, const struct gfx_fmt *sf, const unsigned char *src, int n,
                 const unsigned *lut, int x, int y)
{
    int sb = sf->bpp / 8, db = df->bpp / 8, i;
    if (sf->bpp == df->bpp && (sf->bpp == 8 ? !lut : sf->rpos == df->rpos && sf->rsize == df->rsize && sf->gpos == df->gpos
                               && sf->gsize == df->gsize && sf->bpos == df->bpos && sf->bsize == df->bsize)) {
        for (i = 0; i < n * sb; i++)            /* the same format: as they are */
            dst[i] = src[i];
        return;
    }
    if (sf->bpp == 8) {                         /* indices: each one's pixel from the table (or GLOS's palette) */
        for (i = 0; i < n; i++, dst += db)
            wr(dst, db, lut ? lut[src[i]] : gfx_px(df, gfx_rgb(&gfx_fmt8, src[i])));
        return;
    }
    for (i = 0; i < n; i++, src += sb, dst += db) {
        unsigned p = rd(src, sb);
        unsigned r = up8(p >> sf->rpos & ((1u << sf->rsize) - 1), sf->rsize);
        unsigned g = up8(p >> sf->gpos & ((1u << sf->gsize) - 1), sf->gsize);
        unsigned b = up8(p >> sf->bpos & ((1u << sf->bsize) - 1), sf->bsize);
        if (df->bpp == 8)
            dst[0] = gfx_pal_dither(r << 16 | g << 8 | b, x + i, y);
        else
            wr(dst, db, (r >> (8 - df->rsize)) << df->rpos | (g >> (8 - df->gsize)) << df->gpos | (b >> (8 - df->bsize)) << df->bpos);
    }
}
