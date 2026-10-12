/* The 2D library's drawing (kernel/include/gfx.h; PRD §9.3): fills, lines,
 * copies, 1-bit and true-colour pictures, at 8, 16 and 32 bits a pixel.
 * Each function cuts what it is asked for down to the surface's clip
 * rectangle first, then writes rows with a loop for the pixel size. */
#include "gfx.h"

void *memmove(void *d, const void *s, size_t n);

const struct gfx_fmt gfx_fmt8 = { 8, 0, 0, 0, 0, 0, 0 };
const struct gfx_fmt gfx_fmt565 = { 16, 11, 5, 5, 6, 0, 5 };
const struct gfx_fmt gfx_fmt555 = { 16, 10, 5, 5, 5, 0, 5 };
const struct gfx_fmt gfx_fmt888 = { 32, 16, 8, 8, 8, 0, 8 };

void gfx_clip(struct gfx_surface *s, const struct gfx_rect *r)
{
    struct gfx_rect all = { 0, 0, s->w, s->h };
    if (r && !gfx_rect_cut(&all, r))
        all.w = all.h = 0;
    s->clip = all;
}

void gfx_init(struct gfx_surface *s, void *pix, s32 w, s32 h, u32 pitch, const struct gfx_fmt *fmt)
{
    s->pix = pix;
    s->w = w;
    s->h = h;
    s->pitch = pitch;
    s->fmt = *fmt;
    gfx_clip(s, 0);
}

/* An 8-bit value into a field of n bits, and back with the top bits
   repeated below (so that full stays full: 1Fh becomes FFh). */
static u32 pack(u32 v, u32 pos, u32 size) { return (v >> (8 - size)) << pos; }

static u32 unpack(u32 px, u32 pos, u32 size)
{
    u32 v = (px >> pos) & ((1u << size) - 1);
    v <<= 8 - size;
    return v | v >> size;
}

u32 gfx_px(const struct gfx_fmt *f, u32 rgb)
{
    if (f->bpp == 8)
        return gfx_pal_nearest(rgb);
    return pack((rgb >> 16) & 0xFF, f->rpos, f->rsize) | pack((rgb >> 8) & 0xFF, f->gpos, f->gsize)
           | pack(rgb & 0xFF, f->bpos, f->bsize);
}

u32 gfx_rgb(const struct gfx_fmt *f, u32 px)
{
    if (f->bpp == 8) {
        const u8 *c = gfx_palette[px & 0xFF];
        return (u32)c[0] << 16 | (u32)c[1] << 8 | c[2];
    }
    return unpack(px, f->rpos, f->rsize) << 16 | unpack(px, f->gpos, f->gsize) << 8 | unpack(px, f->bpos, f->bsize);
}

static u8 *at(const struct gfx_surface *s, s32 x, s32 y) { return s->pix + (u32)y * s->pitch + (u32)x * (s->fmt.bpp / 8u); }

u32 gfx_get(const struct gfx_surface *s, s32 x, s32 y)
{
    const u8 *p;
    if (x < 0 || y < 0 || x >= s->w || y >= s->h)
        return 0;
    p = at(s, x, y);
    return s->fmt.bpp == 8 ? *p : s->fmt.bpp == 16 ? *(const u16 *)p : *(const u32 *)p;
}

/* n pixels of one value from p on. */
static void span(u8 *p, u32 n, u32 px, u32 bpp)
{
    u32 i;
    if (bpp == 8) {
        for (i = 0; i < n; i++)
            p[i] = (u8)px;
    } else if (bpp == 16) {
        u16 *q = (u16 *)p;
        for (i = 0; i < n; i++)
            q[i] = (u16)px;
    } else {
        u32 *q = (u32 *)p;
        for (i = 0; i < n; i++)
            q[i] = px;
    }
}

static void put(const struct gfx_surface *s, s32 x, s32 y, u32 px) { span(at(s, x, y), 1, px, s->fmt.bpp); }

static int inside(const struct gfx_surface *s, s32 x, s32 y)
{
    return x >= s->clip.x && y >= s->clip.y && x - s->clip.x < s->clip.w && y - s->clip.y < s->clip.h;
}

void gfx_fill(struct gfx_surface *s, s32 x, s32 y, s32 w, s32 h, u32 rgb)
{
    struct gfx_rect r = { x, y, w, h };
    u32 px = gfx_px(&s->fmt, rgb);
    s32 j;
    if (!gfx_rect_cut(&r, &s->clip))
        return;
    for (j = 0; j < r.h; j++)
        span(at(s, r.x, r.y + j), (u32)r.w, px, s->fmt.bpp);
}

void gfx_frame(struct gfx_surface *s, s32 x, s32 y, s32 w, s32 h, u32 rgb)
{
    if (w <= 0 || h <= 0)
        return;
    gfx_fill(s, x, y, w, 1, rgb);
    if (h > 1)
        gfx_fill(s, x, y + h - 1, w, 1, rgb);
    if (h > 2) {
        gfx_fill(s, x, y + 1, 1, h - 2, rgb);
        if (w > 1)
            gfx_fill(s, x + w - 1, y + 1, 1, h - 2, rgb);
    }
}

/* Bresenham's line, every pixel tested against the clip rectangle: which
   pixels a line has never depends on what is clipped away. Horizontal and
   vertical lines are fills. */
void gfx_line(struct gfx_surface *s, s32 x0, s32 y0, s32 x1, s32 y1, u32 rgb)
{
    s32 dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    s32 sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx - dy, e2;
    u32 px;
    if (!dy) {
        gfx_fill(s, x0 < x1 ? x0 : x1, y0, dx + 1, 1, rgb);
        return;
    }
    if (!dx) {
        gfx_fill(s, x0, y0 < y1 ? y0 : y1, 1, dy + 1, rgb);
        return;
    }
    px = gfx_px(&s->fmt, rgb);
    for (;;) {
        if (inside(s, x0, y0))
            put(s, x0, y0, px);
        if (x0 == x1 && y0 == y1)
            return;
        e2 = 2 * e;
        if (e2 > -dy) {
            e -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            e += dx;
            y0 += sy;
        }
    }
}

void gfx_blit(struct gfx_surface *d, s32 dx, s32 dy, const struct gfx_surface *s, s32 sx, s32 sy, s32 w, s32 h)
{
    struct gfx_rect r = { dx, dy, w, h }, src = { 0, 0, s->w, s->h }, part;
    u32 bytes = d->fmt.bpp / 8u;
    s32 j;
    if (d->fmt.bpp != s->fmt.bpp)
        return;
    part.x = sx, part.y = sy, part.w = w, part.h = h;   /* what the source has of it ... */
    if (!gfx_rect_cut(&part, &src))
        return;
    r.x += part.x - sx;                                 /* ... lands here ... */
    r.y += part.y - sy;
    r.w = part.w;
    r.h = part.h;
    part = r;
    if (!gfx_rect_cut(&r, &d->clip))                    /* ... and this much of it is wanted */
        return;
    sx += r.x - dx;
    sy += r.y - dy;
    if (at(d, r.x, r.y) > at(s, sx, sy)) {      /* (the same memory, perhaps: towards higher addresses, last row first) */
        for (j = r.h - 1; j >= 0; j--)
            memmove(at(d, r.x, r.y + j), at(s, sx, sy + j), (u32)r.w * bytes);
    } else {
        for (j = 0; j < r.h; j++)
            memmove(at(d, r.x, r.y + j), at(s, sx, sy + j), (u32)r.w * bytes);
    }
}

void gfx_mono(struct gfx_surface *s, s32 x, s32 y, const u8 *bits, s32 w, s32 h, u32 stride, u32 fg, u32 bg)
{
    struct gfx_rect r = { x, y, w, h };
    u32 f = gfx_px(&s->fmt, fg), b = bg == GFX_NONE ? 0 : gfx_px(&s->fmt, bg);
    s32 i, j;
    if (!gfx_rect_cut(&r, &s->clip))
        return;
    for (j = 0; j < r.h; j++) {
        const u8 *row = bits + (u32)(r.y + j - y) * stride;
        u8 *p = at(s, r.x, r.y + j);
        u32 step = s->fmt.bpp / 8u;
        for (i = 0; i < r.w; i++, p += step) {
            u32 k = (u32)(r.x + i - x);
            if (row[k >> 3] & (0x80u >> (k & 7)))
                span(p, 1, f, s->fmt.bpp);
            else if (bg != GFX_NONE)
                span(p, 1, b, s->fmt.bpp);
        }
    }
}

void gfx_image(struct gfx_surface *s, s32 x, s32 y, const u32 *rgb, s32 w, s32 h, u32 stride)
{
    struct gfx_rect r = { x, y, w, h };
    s32 i, j;
    if (!gfx_rect_cut(&r, &s->clip))
        return;
    for (j = 0; j < r.h; j++) {
        const u32 *row = rgb + (u32)(r.y + j - y) * stride + (u32)(r.x - x);
        u8 *p = at(s, r.x, r.y + j);
        if (s->fmt.bpp == 8) {
            for (i = 0; i < r.w; i++)
                p[i] = gfx_pal_dither(row[i], r.x + i, r.y + j);
        } else if (s->fmt.bpp == 16) {
            for (i = 0; i < r.w; i++)
                ((u16 *)p)[i] = (u16)gfx_px(&s->fmt, row[i]);
        } else {
            for (i = 0; i < r.w; i++)
                ((u32 *)p)[i] = gfx_px(&s->fmt, row[i]);
        }
    }
}
