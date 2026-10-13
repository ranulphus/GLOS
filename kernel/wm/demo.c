/* A desktop to look at and to compare (kernel/include/wm.h): the task bar,
 * three windows that overlap, one of them the focus, something drawn in
 * each in one of the three fonts, and the pointer. The host test composes it and writes it out
 * (tests/host/wm_test.c: build/host/wm-desk-N.png); the kernel composes the
 * same on the real screen (glos desk demo), and a screenshot of that must be
 * the host's picture (make loopa-desk). */
#include "wm.h"


void wm_demo(struct wm *m)
{
    static u32 ramp[96 * 64];
    struct wm_window *a, *b, *c;
    s32 x, y, i;

    wm_bar(m, 1);
    wm_bar_text(m, "12:34", "ssh");
    a = wm_create(m, 24, 20, 260, 150, "Read me", WF_FRAME, 0);
    b = wm_create(m, 180, 90, 300, 200, "Pictures", WF_FRAME, 0);
    c = wm_create(m, 60, 220, 220, 120, "Processes", WF_FRAME | WF_FIXED, 0);
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
    /* (GLOS's own text in columns, in the fixed-width UI font. Not a DOS program's screen, nor a prompt: one
       of those in a window is drawn in the VGA's fonts, PRD D42.) */
    gfx_fill(&c->store, 0, 0, 220, 120, 0x333333);
    for (i = 0; i < 6; i++)
        gfx_text(&c->store, 4, 4 + i * 15, &gfx_font_mono,
                 (const char *[]){ "ID PROGRAM     STATE    KB", " 1 EDIT.EXE    running 184", " 2 PAINT.EXE   waiting 412",
                                   " 3 CALC.EXE    running  96", "   desk        thread", "   vm          thread" }[i], -1,
                 i ? 0xCCCCCC : 0xFFFFFF);
    wm_show(m, a, 1);
    wm_show(m, b, 1);
    wm_show(m, c, 1);
    wm_set_focus(m, b);
    wm_pointer(m, 300, 130, 1);
}
