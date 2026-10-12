/* A desktop to look at and to compare (kernel/include/wm.h): the task bar,
 * three windows that overlap, one of them the focus, something drawn in
 * each, and the pointer. The host test composes it and writes it out
 * (tests/host/wm_test.c: build/host/wm-desk-N.png); the kernel composes the
 * same on the real screen (glos desk demo), and a screenshot of that must be
 * the host's picture (make loopa-desk). */
#include "wm.h"

extern const struct gfx_font gfx_font_ui, gfx_font_bold, gfx_font_mono;

void wm_demo(struct wm *m)
{
    static u32 ramp[96 * 64];
    struct wm_window *a, *b, *c;
    s32 x, y, i;

    wm_bar(m, 1);
    wm_bar_text(m, "12:34", "ssh");
    a = wm_create(m, 24, 20, 260, 150, "Read me", WF_FRAME, 0);
    b = wm_create(m, 180, 90, 300, 200, "Pictures", WF_FRAME, 0);
    c = wm_create(m, 60, 220, 220, 120, "C:\\GLOS", WF_FRAME | WF_FIXED, 0);
    if (!a || !b || !c)
        return;
    for (i = 0; i < 8; i++)
        gfx_text(&a->store, 8, 6 + i * 16, i & 1 ? &gfx_font_ui : &gfx_font_bold,
                 i & 1 ? "The quick brown fox jumps over the lazy dog." : "GLOS: a graphical DOS", -1, 0x000000);
    gfx_fill(&a->store, 0, 134, 260, 16, 0xCCCCCC);
    gfx_text(&a->store, 6, 136, &gfx_font_ui, "8 lines", -1, 0x000000);
    for (y = 0; y < 64; y++)
        for (x = 0; x < 96; x++)
            ramp[y * 96 + x] = (u32)(x * 255 / 95) << 16 | (u32)(y * 255 / 63) << 8 | (u32)(255 - x * 255 / 95);
    gfx_fill(&b->store, 0, 0, 300, 200, 0x333333);
    for (i = 0; i < 6; i++)
        gfx_image(&b->store, 8 + (i % 3) * 98, 10 + (i / 3) * 96, ramp, 96, 64, 96);
    for (i = 0; i < 6; i++)
        gfx_text(&b->store, 8 + (i % 3) * 98, 78 + (i / 3) * 96, &gfx_font_ui,
                 (const char *[]){ "one.bmp", "two.bmp", "three.bmp", "four.bmp", "five.bmp", "six.bmp" }[i], -1, 0xFFFFFF);
    gfx_fill(&c->store, 0, 0, 220, 120, 0x000000);
    for (i = 0; i < 6; i++)
        gfx_text(&c->store, 4, 4 + i * 15, &gfx_font_mono,
                 (const char *[]){ "C:\\GLOS>dir /w", "GLOS.EXE   GLOSK.BIN  GLOS.CFG", "KEYS       CRASH",
                                   "        3 file(s)", "        2 dir(s)", "C:\\GLOS>_" }[i], -1, 0x55FF55);
    wm_show(m, a, 1);
    wm_show(m, b, 1);
    wm_show(m, c, 1);
    wm_set_focus(m, b);
    wm_pointer(m, 300, 130, 1);
}
