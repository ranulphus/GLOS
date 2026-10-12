/* The 2D library's test card (kernel/include/gfx.h): one picture with every
 * kind of drawing in it, for any surface 320x200 or larger. The host test
 * draws it (tests/host/gfx_test.c) and so does the kernel on the real
 * screen (glos display card), and a screenshot of that must be the host's
 * picture, pixel for pixel. */
#include "gfx.h"

extern const struct gfx_font gfx_font_ui, gfx_font_bold, gfx_font_mono;

static const u8 arrow[16][2] = {                /* a pointer, 12 wide */
    { 0x80, 0x00 }, { 0xC0, 0x00 }, { 0xE0, 0x00 }, { 0xF0, 0x00 }, { 0xF8, 0x00 }, { 0xFC, 0x00 }, { 0xFE, 0x00 },
    { 0xFF, 0x00 }, { 0xFF, 0x80 }, { 0xFF, 0xC0 }, { 0xFC, 0x00 }, { 0xEE, 0x00 }, { 0xC6, 0x00 }, { 0x87, 0x00 },
    { 0x03, 0x00 }, { 0x03, 0x00 },
};

void gfx_testcard(struct gfx_surface *s)
{
    static u32 ramp[64 * 48];
    struct gfx_rect clip = { 0, 0, 0, 0 };
    s32 w = s->w, h = s->h, i, x, y, cw = (w - 16) / 16;
    char line[64];
    const char *depth = s->fmt.bpp == 8 ? "8" : s->fmt.bpp == 16 ? (s->fmt.gsize == 6 ? "16" : "15") : "32";
    u32 n = 0;

    gfx_clip(s, 0);
    gfx_fill(s, 0, 0, w, h, 0x808080);
    gfx_frame(s, 0, 0, w, h, 0xFFFFFF);
    gfx_frame(s, 2, 2, w - 4, h - 4, 0x000000);
    for (i = 0; i < 16; i++)                    /* the sixteen colours of text mode */
        gfx_fill(s, 8 + i * cw, 8, cw, 16, (u32)gfx_palette[i][0] << 16 | (u32)gfx_palette[i][1] << 8 | gfx_palette[i][2]);
    for (i = 0; i < 16; i++)                    /* red, green, blue and grey, sixteen steps each */
        for (y = 0; y < 4; y++) {
            u32 v = (u32)i * 17, c = y == 0 ? v << 16 : y == 1 ? v << 8 : y == 2 ? v : v * 0x010101;
            gfx_fill(s, 8 + i * cw, 28 + y * 8, cw, 8, c);
        }
    for (y = 0; y < 48; y++)                    /* a picture: red across, green down, blue with both */
        for (x = 0; x < 64; x++)
            ramp[y * 64 + x] = (u32)(x * 4 + 1) << 16 | (u32)(y * 5 + 7) << 8 | (u32)((x + y) * 2 + 16);
    gfx_image(s, 8, 64, ramp, 64, 48, 64);
    gfx_image(s, 76, 64, ramp, 64, 48, 64);     /* (twice: the dither's pattern tiles) */
    for (i = 0; i < 12; i++)                    /* lines every 30 degrees or so, from one point */
        gfx_line(s, 180, 88, 180 + (s32)((const signed char[]){ 24, 21, 12, 0, -12, -21, -24, -21, -12, 0, 12, 21 }[i]),
                 88 + (s32)((const signed char[]){ 0, 12, 21, 24, 21, 12, 0, -12, -21, -24, -21, -12 }[i]), 0xFFFF00);
    for (i = 0; i < 5; i++)
        gfx_frame(s, 214 + i * 3, 64 + i * 3, 48 - i * 6, 48 - i * 6, i & 1 ? 0x000000 : 0xFFFFFF);
    gfx_mono(s, 270, 64, &arrow[0][0], 12, 16, 2, 0x000000, GFX_NONE);
    gfx_mono(s, 286, 64, &arrow[0][0], 12, 16, 2, 0xFFFFFF, 0x0000AA);
    clip.x = 270, clip.y = 88, clip.w = 40, clip.h = 20;       /* clipped: only the middle of each shows */
    gfx_clip(s, &clip);
    gfx_fill(s, 260, 80, 60, 40, 0x00AA00);
    gfx_line(s, 250, 70, 330, 130, 0xFFFFFF);
    gfx_text(s, 262, 91, &gfx_font_bold, "clipped text", -1, 0x000000);
    gfx_clip(s, 0);
    line[n++] = 'G', line[n++] = 'L', line[n++] = 'O', line[n++] = 'S', line[n++] = ' ';
    for (i = 1000; i; i /= 10)
        if (w >= i)
            line[n++] = (char)('0' + w / i % 10);
    line[n++] = 'x';
    for (i = 1000; i; i /= 10)
        if (h >= i)
            line[n++] = (char)('0' + h / i % 10);
    line[n++] = 'x';
    while (*depth)
        line[n++] = *depth++;
    line[n] = 0;
    gfx_text(s, 8, 120, &gfx_font_bold, line, -1, 0xFFFFFF);
    gfx_text(s, 8, 136, &gfx_font_ui, "The quick brown fox jumps over the lazy dog. 0123456789", -1, 0x000000);
    gfx_text(s, 8, 152, &gfx_font_ui, "Gr\xC3\xBC\xC3\x9F" "e, \xC3\xA7" "a va? \xC2\xA3" "5 \xC2\xBD \xE2\x82\xAC", -1, 0x000000);
    gfx_text(s, 8, 168, &gfx_font_mono, "C:\\GLOS> dir *.exe /w", -1, 0x00FF00);
    gfx_blit(s, w - 72, h - 56, s, 8, 64, 64, 48);      /* the picture again, copied from the surface itself */
    gfx_blit(s, 12, 68, s, 8, 64, 20, 12);              /* and a corner of it onto itself, overlapping */
}
