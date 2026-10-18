/* Text for the 2D library (kernel/include/gfx.h): UTF-8 through a 1-bit
 * font (kernel/gfx/fonts/, made by tools/mkfont.py), or its smoothed glyphs
 * when the toolkit has given them (M10, gfx_smooth). A code point the font
 * hasn't got is drawn as its box; a byte that isn't UTF-8 is taken for the
 * Latin-1 character it would be. */
#include "gfx.h"

/* The code point at p (of n bytes), and the bytes it took. */
static u32 utf8(const u8 *p, s32 n, s32 *used)
{
    u32 c = p[0], k, need = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0, v;
    *used = 1;
    if (c < 0x80 || c < 0xC2 || c > 0xF4 || (s32)need >= n)
        return c;
    v = c & (0x3Fu >> need);
    for (k = 1; k <= need; k++) {
        if ((p[k] & 0xC0) != 0x80)
            return c;
        v = v << 6 | (p[k] & 0x3F);
    }
    *used = (s32)need + 1;
    return v;
}

static const struct gfx_glyph *glyph(const struct gfx_font *f, u32 cp)
{
    u32 lo = 0, hi = f->nmore;
    if (cp >= f->first && cp - f->first < f->count)
        return &f->glyph[cp - f->first];
    while (lo < hi) {                           /* (the other ranges, in order: halved) */
        u32 mid = (lo + hi) / 2;
        const struct gfx_range *r = &f->more[mid];
        if (cp < r->first)
            hi = mid;
        else if (cp - r->first >= r->count)
            lo = mid + 1;
        else
            return &f->glyph[r->at + cp - r->first];
    }
    return &f->glyph[f->missing];
}

static s32 length(const char *text, s32 n)
{
    if (n < 0)
        for (n = 0; text[n]; n++) ;
    return n;
}

s32 gfx_text_width(const struct gfx_font *f, const char *text, s32 n)
{
    s32 i, used, w = 0;
    n = length(text, n);
    for (i = 0; i < n; i += used)
        w += glyph(f, utf8((const u8 *)text + i, n - i, &used))->advance;
    return w;
}

static const struct gfx_aa *smooth;     /* (gfx_smooth's: never the kernel's) */

void gfx_smooth(const struct gfx_aa *list) { smooth = list; }

/* One channel of 0 .. 15 coverage of fg over bg. */
static u32 mix(u32 fg, u32 bg, u32 c, u32 shift)
{
    u32 a = fg >> shift & 255, b = bg >> shift & 255;
    return (b * (15 - c) + a * c + 7) / 15 << shift;
}

/* A smoothed glyph's coverage at (x, y): ink in rgb, blended with what is there (16 and 32 bits). */
static void blend(struct gfx_surface *s, s32 x, s32 y, const struct gfx_aa_glyph *g, const u8 *bits, s32 h, u32 rgb)
{
    const struct gfx_rect *c = &s->clip;
    u32 stride = (g->width + 1u) / 2, ink = gfx_px(&s->fmt, rgb);
    s32 row, col;
    for (row = 0; row < h; row++) {
        s32 py = y + row;
        u8 *line;
        if (py < c->y || py >= c->y + c->h)
            continue;
        line = s->pix + (u32)py * s->pitch;
        for (col = 0; col < g->width; col++) {
            s32 px = x + col;
            u32 v = bits[(u32)row * stride + (u32)col / 2] >> (col & 1 ? 0 : 4) & 15, bg, out;
            if (!v || px < c->x || px >= c->x + c->w)
                continue;
            if (v == 15) {
                out = ink;
            } else {
                bg = gfx_rgb(&s->fmt, s->fmt.bpp == 16 ? ((u16 *)line)[px] : ((u32 *)line)[px]);
                out = gfx_px(&s->fmt, mix(rgb, bg, v, 16) | mix(rgb, bg, v, 8) | mix(rgb, bg, v, 0));
            }
            if (s->fmt.bpp == 16)
                ((u16 *)line)[px] = (u16)out;
            else
                ((u32 *)line)[px] = out;
        }
    }
}

s32 gfx_text(struct gfx_surface *s, s32 x, s32 y, const struct gfx_font *f, const char *text, s32 n, u32 rgb)
{
    const struct gfx_aa *aa = 0;
    s32 i, used;
    n = length(text, n);
    if (smooth && s->fmt.bpp != 8) {
        for (aa = smooth; aa && aa->font != f; aa = aa->next) ;
        if (aa && s->dev && s->dev->sync)
            s->dev->sync(s);                    /* (the engine finished before the pixels are read) */
    }
    for (i = 0; i < n; i += used) {
        const struct gfx_glyph *g = glyph(f, utf8((const u8 *)text + i, n - i, &used));
        if (aa) {
            const struct gfx_aa_glyph *a = &aa->glyph[g - f->glyph];
            if (a->width)
                blend(s, x + a->left, y, a, aa->bits + a->at, f->height, rgb);
        } else if (g->width) {
            gfx_mono(s, x + g->left, y, f->bits + g->at, g->width, f->height, (g->width + 7u) / 8, rgb, GFX_NONE);
        }
        x += g->advance;
    }
    return x;
}
