/* The GLOS palette (kernel/include/gfx.h; PRD §9.2): what every 8-bit
 * surface's pixels index, and the DAC is loaded with while the desktop is
 * on an 8-bit screen.
 *   0-15     the sixteen colours of text mode, in their order
 *   16-231   a colour cube, six levels each of red, green and blue (00h,
 *            33h, 66h, 99h, CCh, FFh): 16 + 36 r + 6 g + b
 *   232-255  greys between the cube's own: 08h, 12h, ... EEh
 * A colour's nearest entry is the cube's, or a grey when the colour is one
 * (nearly). Pictures are dithered into the cube alone. */
#include "gfx.h"

#define C(r, g, b) { r, g, b }
#define ROW(r, g) C(r, g, 0x00), C(r, g, 0x33), C(r, g, 0x66), C(r, g, 0x99), C(r, g, 0xCC), C(r, g, 0xFF)
#define PLANE(r) ROW(r, 0x00), ROW(r, 0x33), ROW(r, 0x66), ROW(r, 0x99), ROW(r, 0xCC), ROW(r, 0xFF)

const u8 gfx_palette[256][3] = {
    C(0x00, 0x00, 0x00), C(0x00, 0x00, 0xAA), C(0x00, 0xAA, 0x00), C(0x00, 0xAA, 0xAA),
    C(0xAA, 0x00, 0x00), C(0xAA, 0x00, 0xAA), C(0xAA, 0x55, 0x00), C(0xAA, 0xAA, 0xAA),
    C(0x55, 0x55, 0x55), C(0x55, 0x55, 0xFF), C(0x55, 0xFF, 0x55), C(0x55, 0xFF, 0xFF),
    C(0xFF, 0x55, 0x55), C(0xFF, 0x55, 0xFF), C(0xFF, 0xFF, 0x55), C(0xFF, 0xFF, 0xFF),
    PLANE(0x00), PLANE(0x33), PLANE(0x66), PLANE(0x99), PLANE(0xCC), PLANE(0xFF),
#define G(v) C(v, v, v)
    G(0x08), G(0x12), G(0x1C), G(0x26), G(0x30), G(0x3A), G(0x44), G(0x4E), G(0x58), G(0x62), G(0x6C), G(0x76),
    G(0x80), G(0x8A), G(0x94), G(0x9E), G(0xA8), G(0xB2), G(0xBC), G(0xC6), G(0xD0), G(0xDA), G(0xE4), G(0xEE),
};

/* The cube's level nearest to v: 0 to 5. */
static u32 level(u32 v) { return (v + 25) / 51; }

u8 gfx_pal_nearest(u32 rgb)
{
    u32 r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    u32 lr = level(r), lg = level(g), lb = level(b), max = r, min = r, v, i;
    s32 dr = (s32)(r - lr * 51), dg = (s32)(g - lg * 51), db = (s32)(b - lb * 51), d;
    u32 cube = (u32)(dr * dr + dg * dg + db * db);
    if (g > max) max = g;
    if (b > max) max = b;
    if (g < min) min = g;
    if (b < min) min = b;
    if (max - min > 24)                         /* a colour: the cube's */
        return (u8)(16 + 36 * lr + 6 * lg + lb);
    v = (r + g + b + 1) / 3;                    /* nearly a grey: the nearer of the cube's and the ramp's */
    i = v < 8 ? 0 : v > 0xEE ? 23 : (v - 8 + 5) / 10;
    dr = (s32)(r - (8 + i * 10));
    dg = (s32)(g - (8 + i * 10));
    db = (s32)(b - (8 + i * 10));
    d = dr * dr + dg * dg + db * db;
    return (u32)d < cube ? (u8)(232 + i) : (u8)(16 + 36 * lr + 6 * lg + lb);
}

/* Ordered dithering: a threshold from a 4x4 pattern decides between the two
   cube levels a value lies between. */
u8 gfx_pal_dither(u32 rgb, s32 x, s32 y)
{
    static const u8 bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
    u32 t = bayer[y & 3][x & 3] * 51u / 16 + 1, c[3], i;        /* 1 to 48: a value on a level stays on it */
    for (i = 0; i < 3; i++) {
        u32 v = (rgb >> (16 - 8 * i)) & 0xFF;
        c[i] = v / 51 + (v % 51 >= t);
    }
    return (u8)(16 + 36 * c[0] + 6 * c[1] + c[2]);
}
