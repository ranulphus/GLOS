/* Host test for the 2D library (kernel/gfx; kernel/include/gfx.h).
 *
 * Every drawing function, at 8, 15, 16 and 32 bits a pixel, against a model
 * that knows nothing but "this pixel, if it is inside the clip rectangle":
 * tens of thousands of random calls each, most of them partly or wholly
 * outside the surface, the two pictures compared after every call, with
 * guard bytes around the library's that must stay as they were. Then the
 * palette, the colour conversions, text and regions on their own, and the
 * test card (kernel/gfx/testcard.c) at each depth, written to
 * build/host/gfx-card-N.png to be looked at and compared with a screenshot
 * of the real thing (its CRC is printed). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "png.h"

extern const struct gfx_font gfx_font_ui, gfx_font_bold, gfx_font_mono;
void gfx_testcard(struct gfx_surface *s);

static int fails;
#define CHECK(c) do { if (!(c)) { if (fails++ < 20) printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

#define W 61
#define H 43
#define GUARD 64

static u32 rnd_state = 12345;
static u32 rnd(void) { rnd_state = rnd_state * 1664525u + 1013904223u; return rnd_state >> 8; }
static s32 rin(s32 lo, s32 hi) { return lo + (s32)(rnd() % (u32)(hi - lo + 1)); }

/* ---- the model: a surface of the same format, written a pixel at a time */

static void m_put(struct gfx_surface *s, s32 x, s32 y, u32 px)
{
    u8 *p;
    if (x < s->clip.x || y < s->clip.y || x >= s->clip.x + s->clip.w || y >= s->clip.y + s->clip.h)
        return;
    p = s->pix + (u32)y * s->pitch + (u32)x * (s->fmt.bpp / 8u);
    if (s->fmt.bpp == 8) *p = (u8)px;
    else if (s->fmt.bpp == 16) *(u16 *)p = (u16)px;
    else *(u32 *)p = px;
}

static void m_fill(struct gfx_surface *s, s32 x, s32 y, s32 w, s32 h, u32 rgb)
{
    s32 i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            m_put(s, x + i, y + j, gfx_px(&s->fmt, rgb));
}

static void m_frame(struct gfx_surface *s, s32 x, s32 y, s32 w, s32 h, u32 rgb)
{
    s32 i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (!i || !j || i == w - 1 || j == h - 1)
                m_put(s, x + i, y + j, gfx_px(&s->fmt, rgb));
}

static void m_line(struct gfx_surface *s, s32 x0, s32 y0, s32 x1, s32 y1, u32 rgb)
{
    s32 dx = abs(x1 - x0), dy = abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx - dy, e2;
    for (;;) {
        m_put(s, x0, y0, gfx_px(&s->fmt, rgb));
        if (x0 == x1 && y0 == y1)
            return;
        e2 = 2 * e;
        if (e2 > -dy) { e -= dy; x0 += sx; }
        if (e2 < dx) { e += dx; y0 += sy; }
    }
}

static void m_mono(struct gfx_surface *s, s32 x, s32 y, const u8 *bits, s32 w, s32 h, u32 stride, u32 fg, u32 bg)
{
    s32 i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            if (bits[(u32)j * stride + ((u32)i >> 3)] & (0x80u >> (i & 7)))
                m_put(s, x + i, y + j, gfx_px(&s->fmt, fg));
            else if (bg != GFX_NONE)
                m_put(s, x + i, y + j, gfx_px(&s->fmt, bg));
        }
}

static void m_image(struct gfx_surface *s, s32 x, s32 y, const u32 *rgb, s32 w, s32 h, u32 stride)
{
    s32 i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            u32 c = rgb[(u32)j * stride + (u32)i];
            m_put(s, x + i, y + j, s->fmt.bpp == 8 ? gfx_pal_dither(c, x + i, y + j) : gfx_px(&s->fmt, c));
        }
}

/* (from a copy of the source's pixels, so the model needs no thought about overlap) */
static void m_blit(struct gfx_surface *d, s32 dx, s32 dy, const struct gfx_surface *s, s32 sx, s32 sy, s32 w, s32 h)
{
    static u32 tmp[128 * 128];
    s32 i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            tmp[j * 128 + i] = gfx_get(s, sx + i, sy + j);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (sx + i >= 0 && sy + j >= 0 && sx + i < s->w && sy + j < s->h)
                m_put(d, dx + i, dy + j, tmp[j * 128 + i]);
}

static s32 m_text(struct gfx_surface *s, s32 x, s32 y, const struct gfx_font *f, const char *t, u32 rgb)
{
    for (; *t; t++) {                           /* (ASCII only here) */
        const struct gfx_glyph *g = &f->glyph[(u8)*t - f->first];
        m_mono(s, x + g->left, y, f->bits + g->at, g->width, f->height, (g->width + 7u) / 8, rgb, GFX_NONE);
        x += g->advance;
    }
    return x;
}

/* ---- the two surfaces, and the comparison */

struct pair {
    struct gfx_surface lib, model, src;         /* src: something to copy from (the same in both worlds) */
    u8 *lib_mem, *model_mem, *src_mem;
    u32 bytes;
};

static void pair_init(struct pair *p, const struct gfx_fmt *f)
{
    u32 pitch = (W * (f->bpp / 8u) + 7) & ~3u, i;      /* (rows have slack at the end) */
    p->bytes = pitch * H;
    p->lib_mem = malloc(p->bytes + 2 * GUARD);
    p->model_mem = malloc(p->bytes + 2 * GUARD);
    p->src_mem = malloc(p->bytes);
    memset(p->lib_mem, 0xA5, p->bytes + 2 * GUARD);
    memset(p->model_mem, 0xA5, p->bytes + 2 * GUARD);
    for (i = 0; i < p->bytes; i++)
        p->src_mem[i] = (u8)rnd();
    gfx_init(&p->lib, p->lib_mem + GUARD, W, H, pitch, f);
    gfx_init(&p->model, p->model_mem + GUARD, W, H, pitch, f);
    gfx_init(&p->src, p->src_mem, W, H, pitch, f);
}

static int same(const struct pair *p) { return !memcmp(p->lib_mem, p->model_mem, p->bytes + 2 * GUARD); }

static u32 some_rgb(void) { return rnd() & 1 ? rnd() & 0xFFFFFF : (rnd() & 0xFF) * 0x010101; }

static void fuzz(const struct gfx_fmt *f, const char *name)
{
    static const char *const words[] = { "GLOS", "Wy", "x", "{[|]}", "The quick brown fox", "" };
    static u8 bits[64 * 8];
    static u32 rgb[64 * 64];
    struct pair p;
    u32 i, n, bad = 0;
    pair_init(&p, f);
    for (i = 0; i < sizeof bits; i++)
        bits[i] = (u8)rnd();
    for (i = 0; i < ARRAY_SIZE(rgb); i++)
        rgb[i] = rnd() & 0xFFFFFF;
    for (n = 0; n < 60000 && bad < 5; n++) {
        s32 x = rin(-30, W + 20), y = rin(-30, H + 20), w = rin(-3, 70), h = rin(-3, 60);
        s32 x1 = rin(-40, W + 40), y1 = rin(-40, H + 40);
        u32 c = some_rgb(), op = rnd() % 10;
        if (!(rnd() % 7)) {                     /* a new clip rectangle, now and then none, now and then silly */
            struct gfx_rect r = { rin(-10, W), rin(-10, H), rin(-5, W + 20), rin(-5, H + 20) };
            gfx_clip(&p.lib, rnd() % 5 ? &r : 0);
            p.model.clip = p.lib.clip;
            CHECK(p.lib.clip.x >= 0 && p.lib.clip.y >= 0 && p.lib.clip.x + p.lib.clip.w <= W
                  && p.lib.clip.y + p.lib.clip.h <= H);
        }
        switch (op) {
        case 0:
            gfx_fill(&p.lib, x, y, w, h, c);
            m_fill(&p.model, x, y, w, h, c);
            break;
        case 1:
            gfx_frame(&p.lib, x, y, w, h, c);
            m_frame(&p.model, x, y, w, h, c);
            break;
        case 2:
        case 3:
            if (op == 3 && rnd() & 1) x1 = x; else if (op == 3) y1 = y;        /* (straight ones too) */
            gfx_line(&p.lib, x, y, x1, y1, c);
            m_line(&p.model, x, y, x1, y1, c);
            break;
        case 4: {
            u32 bg = rnd() & 1 ? GFX_NONE : some_rgb();
            w = rin(0, 64), h = rin(0, 64);
            gfx_mono(&p.lib, x, y, bits, w, h, 8, c, bg);
            m_mono(&p.model, x, y, bits, w, h, 8, c, bg);
            break;
        }
        case 5:
            w = rin(0, 64), h = rin(0, 64);
            gfx_image(&p.lib, x, y, rgb, w, h, 64);
            m_image(&p.model, x, y, rgb, w, h, 64);
            break;
        case 6:                                 /* from another surface, any part of it and beyond */
            gfx_blit(&p.lib, x, y, &p.src, x1, y1, w, h);
            m_blit(&p.model, x, y, &p.src, x1, y1, w, h);
            break;
        case 7:                                 /* from itself: any overlap */
            gfx_blit(&p.lib, x, y, &p.lib, x1, y1, w, h);
            m_blit(&p.model, x, y, &p.model, x1, y1, w, h);
            break;
        default: {
            const struct gfx_font *font = op == 8 ? &gfx_font_ui : &gfx_font_bold;
            const char *t = words[rnd() % ARRAY_SIZE(words)];
            s32 a = gfx_text(&p.lib, x, y, font, t, -1, c), b = m_text(&p.model, x, y, font, t, c);
            CHECK(a == b && a - x == gfx_text_width(font, t, -1));
        }
        }
        if (!same(&p)) {
            printf("FAIL %s: op %u differs at call %u (x=%d y=%d w=%d h=%d x1=%d y1=%d clip=%d,%d %dx%d)\n", name, op, n,
                   x, y, w, h, x1, y1, p.lib.clip.x, p.lib.clip.y, p.lib.clip.w, p.lib.clip.h);
            memcpy(p.model_mem, p.lib_mem, p.bytes + 2 * GUARD);
            bad++;
            fails++;
        }
    }
    for (i = 0; i < GUARD; i++)
        CHECK(p.lib_mem[i] == 0xA5 && p.lib_mem[GUARD + p.bytes + i] == 0xA5);
    free(p.lib_mem), free(p.model_mem), free(p.src_mem);
}

/* ---- colours */

static void colours(void)
{
    static const struct gfx_fmt *const fmts[] = { &gfx_fmt565, &gfx_fmt555, &gfx_fmt888 };
    u32 i, k, worst = 0;
    for (i = 0; i < 256; i++) {                 /* a palette colour's nearest is that colour */
        u32 c = (u32)gfx_palette[i][0] << 16 | (u32)gfx_palette[i][1] << 8 | gfx_palette[i][2];
        if (i >= 16)
            CHECK(gfx_rgb(&gfx_fmt8, gfx_pal_nearest(c)) == c);
        CHECK(gfx_rgb(&gfx_fmt8, i) == c);
        if (i >= 16 && i < 232)                 /* and the cube's colours dither to themselves everywhere */
            for (k = 0; k < 16; k++)
                CHECK(gfx_pal_dither(c, (s32)(k & 3), (s32)(k >> 2)) == i);
    }
    for (i = 0; i < 200000; i++) {              /* nothing lands far away: half a cube step on each axis at most */
        u32 c = rnd() & 0xFFFFFF, n = gfx_rgb(&gfx_fmt8, gfx_pal_nearest(c)), d = 0;
        for (k = 0; k < 24; k += 8) {
            u32 a = (c >> k) & 0xFF, b = (n >> k) & 0xFF, e = a > b ? a - b : b - a;
            if (e > d) d = e;
        }
        if (d > worst) worst = d;
    }
    CHECK(worst <= 26);
    for (k = 0; k < ARRAY_SIZE(fmts); k++) {
        CHECK(gfx_px(fmts[k], 0xFFFFFF) == (fmts[k]->bpp == 32 ? 0xFFFFFFu : fmts[k]->gsize == 6 ? 0xFFFFu : 0x7FFFu));
        CHECK(gfx_rgb(fmts[k], gfx_px(fmts[k], 0xFFFFFF)) == 0xFFFFFF && gfx_px(fmts[k], 0) == 0);
        for (i = 0; i < 100000; i++) {          /* a pixel's colour gives the pixel back */
            u32 px = gfx_px(fmts[k], rnd() & 0xFFFFFF);
            CHECK(gfx_px(fmts[k], gfx_rgb(fmts[k], px)) == px);
        }
    }
    {                                           /* a flat grey picture, dithered, averages to that grey */
        u32 v, x, y;
        for (v = 0; v < 256; v += 5) {
            u32 sum = 0, c = v * 0x010101;
            for (y = 0; y < 4; y++)
                for (x = 0; x < 4; x++)
                    sum += gfx_rgb(&gfx_fmt8, gfx_pal_dither(c, (s32)x, (s32)y)) & 0xFF;
            CHECK(sum / 16 + 4 >= v && sum / 16 <= v + 4);
        }
    }
}

/* ---- text */

static void text(void)
{
    const struct gfx_font *f = &gfx_font_ui;
    CHECK(f->height >= 12 && f->ascent < f->height && f->first == 0x20 && f->count == 224);
    CHECK(gfx_text_width(f, "", -1) == 0 && gfx_text_width(f, "abc", 2) == gfx_text_width(f, "ab", -1));
    CHECK(gfx_text_width(f, "\xC3\xA9", -1) == f->glyph[0xE9 - 0x20].advance);         /* U+00E9, two bytes */
    CHECK(gfx_text_width(f, "\xE9", -1) == f->glyph[0xE9 - 0x20].advance);             /* the Latin-1 byte alone */
    CHECK(gfx_text_width(f, "\xE2\x82\xAC", -1) == f->glyph[f->missing].advance);     /* U+20AC: not in it, the box */
    CHECK(gfx_text_width(f, "\xC3", -1) == f->glyph[0xC3 - 0x20].advance);             /* cut short: the byte itself */
    CHECK(gfx_text_width(&gfx_font_mono, "iiii", -1) == gfx_text_width(&gfx_font_mono, "MMMM", -1));
    CHECK(gfx_text_width(&gfx_font_bold, "GLOS", -1) > gfx_text_width(f, "GLOS", -1));
}

/* ---- regions, against a bitmap */

#define RW 40
#define RH 30

static void regions(void)
{
    static u8 map[RH][RW];
    struct gfx_region g;
    struct gfx_rect all = { 0, 0, RW, RH }, b;
    u32 n, i, k, rough = 0;
    s32 x, y;
    gfx_region_set(&g, 0);
    CHECK(g.n == 0);
    for (n = 0; n < 20000; n++) {
        struct gfx_rect r = { rin(-5, RW), rin(-5, RH), rin(-2, 25), rin(-2, 20) }, in = r;
        u32 op = rnd() % 8;
        if (op == 0 || g.rough) {               /* afresh (always after one that ran out of room) */
            rough += g.rough;
            gfx_region_set(&g, &in);
            memset(map, 0, sizeof map);
            op = 1;
        }
        gfx_rect_cut(&in, &all);
        if (op <= 3) {
            gfx_region_add(&g, &r);
            for (y = in.y; y < in.y + in.h; y++)
                for (x = in.x; x < in.x + in.w; x++)
                    map[y][x] = 1;
            gfx_region_cut(&g, &all);           /* (the bitmap is only the 40x30) */
        } else if (op <= 6) {
            gfx_region_sub(&g, &r);
            for (y = in.y; y < in.y + in.h; y++)
                for (x = in.x; x < in.x + in.w; x++)
                    map[y][x] = 0;
        } else {
            gfx_region_cut(&g, &r);
            for (y = 0; y < RH; y++)
                for (x = 0; x < RW; x++)
                    if (x < in.x || y < in.y || x >= in.x + in.w || y >= in.y + in.h)
                        map[y][x] = 0;
        }
        for (y = 0; y < RH; y++)
            for (x = 0; x < RW; x++)
                if (g.rough ? map[y][x] && !gfx_region_has(&g, x, y) : gfx_region_has(&g, x, y) != map[y][x]) {
                    if (fails++ < 5)
                        printf("FAIL region: call %u op %u at %d,%d has=%d want=%d rough=%u\n", n, op, x, y,
                               gfx_region_has(&g, x, y), map[y][x], g.rough);
                    y = RH;
                    break;
                }
        for (i = 0; i < g.n && !g.rough; i++)   /* no two overlap, none is empty */
            for (k = 0; k < g.n; k++) {
                struct gfx_rect c = g.r[i];
                CHECK(c.w > 0 && c.h > 0);
                if (k != i)
                    CHECK(!gfx_rect_cut(&c, &g.r[k]));
            }
        gfx_region_bounds(&g, &b);
        for (i = 0; i < g.n; i++) {
            struct gfx_rect c = g.r[i];
            CHECK(gfx_rect_cut(&c, &b) && c.w == g.r[i].w && c.h == g.r[i].h);
        }
    }
    {                                           /* more pieces than there is room for: too much, never too little */
        gfx_region_set(&g, &all);
        for (x = 0; x < RW; x += 2)
            for (y = 0; y < RH; y += 2) {
                struct gfx_rect dot = { x, y, 1, 1 };
                gfx_region_sub(&g, &dot);
            }
        CHECK(g.rough && g.n <= GFX_REGION_MAX && gfx_region_has(&g, 1, 1) && gfx_region_has(&g, RW - 1, RH - 1));
    }
    printf("gfx regions: %u calls, %u ran out of room\n", n, rough);
}

/* ---- the test card */

static u32 crc32(const u8 *p, u32 n)
{
    u32 c = ~0u, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8; k++)
            c = c >> 1 ^ (c & 1 ? 0xEDB88320u : 0);
    }
    return ~c;
}

static void card(const struct gfx_fmt *f, const char *name, s32 w, s32 h)
{
    struct gfx_surface s;
    struct png png;
    char path[64];
    u32 pitch = (u32)w * (f->bpp / 8u), row = 1 + (u32)w * 3, n;
    u8 *pix = calloc(pitch, (size_t)h), *rgb = malloc(row * (size_t)h), *out = malloc(PNG_ROWS_MAX(PNG_BLOCK) + PNG_HEAD_MAX);
    s32 x, y, at = 0;
    FILE *fp;
    gfx_init(&s, pix, w, h, pitch, f);
    gfx_testcard(&s);
    for (y = 0; y < h; y++) {
        rgb[(u32)y * row] = 0;
        for (x = 0; x < w; x++) {
            u32 c = gfx_rgb(f, gfx_get(&s, x, y));
            u8 *p = rgb + (u32)y * row + 1 + (u32)x * 3;
            p[0] = (u8)(c >> 16), p[1] = (u8)(c >> 8), p[2] = (u8)c;
        }
    }
    snprintf(path, sizeof path, "build/host/gfx-card-%s.png", name);
    if ((fp = fopen(path, "wb"))) {
        n = png_begin(&png, (u32)w, (u32)h, 24, 0, 0, out);
        fwrite(out, 1, n, fp);
        while (at < h) {
            s32 rows = (s32)(PNG_BLOCK / row) < h - at ? (s32)(PNG_BLOCK / row) : h - at;
            n = png_rows(&png, rgb + (u32)at * row, (u32)rows, out);
            fwrite(out, 1, n, fp);
            at += rows;
        }
        fclose(fp);
    }
    printf("gfx card %s %dx%d crc=%08x\n", name, w, h, crc32(rgb, row * (u32)h));
    free(pix), free(rgb), free(out);
}

int main(void)
{
    fuzz(&gfx_fmt8, "8");
    fuzz(&gfx_fmt555, "15");
    fuzz(&gfx_fmt565, "16");
    fuzz(&gfx_fmt888, "32");
    colours();
    text();
    regions();
    card(&gfx_fmt8, "8", 640, 480);
    card(&gfx_fmt565, "16", 640, 480);
    card(&gfx_fmt888, "32", 640, 480);
    card(&gfx_fmt8, "8-320", 320, 200);
    printf("gfx_test: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
