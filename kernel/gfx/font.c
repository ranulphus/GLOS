/* Text for the 2D library (kernel/include/gfx.h): UTF-8 through a 1-bit
 * font (kernel/gfx/fonts/, made by tools/mkfont.py). A code point the font
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
    return &f->glyph[cp >= f->first && cp - f->first < f->count ? cp - f->first : f->missing];
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

s32 gfx_text(struct gfx_surface *s, s32 x, s32 y, const struct gfx_font *f, const char *text, s32 n, u32 rgb)
{
    s32 i, used;
    n = length(text, n);
    for (i = 0; i < n; i += used) {
        const struct gfx_glyph *g = glyph(f, utf8((const u8 *)text + i, n - i, &used));
        if (g->width)
            gfx_mono(s, x + g->left, y, f->bits + g->at, g->width, f->height, (g->width + 7u) / 8, rgb, GFX_NONE);
        x += g->advance;
    }
    return x;
}
