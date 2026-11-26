/* kernel/drv/pcialloc.c on the host: places for the PCI ranges a BIOS left unplaced. Bench C's 486 as PCIPLACE found
 * it (2026-11-26: a ViRGE at F0000000, a PCI G450 behind its HiNT bridge with only its registers placed); a window
 * that holds a device already and has to grow past a neighbour; RAM, the firmware's top and no room at all. */
#include <stdio.h>

#include "../../kernel/drv/pcialloc.h"

static int fails;

static void check(int ok, const char *what)
{
    if (!ok) {
        printf("pcialloc_test: FAIL %s\n", what);
        fails++;
    }
}

#define RAM8   { 0x00000000UL, 0x007FFFFFUL }
#define VIRGE  { 0xF0000000UL, 0xF3FFFFFFUL }
#define TOP    { 0xFEC00000UL, 0xFFFFFFFFUL }

int main(void)
{
    unsigned long at = 0;
    int r;

    /* bench C: the frame buffer (32 MB, prefetchable) in the bridge's prefetchable window, which is 9 MB and empty */
    {
        struct pa_range busy[] = { RAM8, VIRGE, TOP, { 0xFD000000UL, 0xFE8FFFFFUL } };    /* (its other window too) */
        struct pa_range win = { 0xFC000000UL, 0xFC8FFFFFUL };
        r = pa_place(busy, 4, 0, 0, &win, 0x2000000UL, 0x01000000UL, 0xFEC00000UL, &at);
        check(r == 0 && at == 0xFA000000UL && win.base == 0xFA000000UL && win.last == 0xFBFFFFFFUL,
              "bench C's frame buffer: FA000000, the window moved to hold it alone");
    }
    /* ... ILOAD (8 MB) in the memory window, beside the registers the BIOS placed (FD004000) */
    {
        struct pa_range busy[] = { RAM8, VIRGE, TOP, { 0xFA000000UL, 0xFBFFFFFFUL } };
        struct pa_range keep[] = { { 0xFD004000UL, 0xFD007FFFUL } };
        struct pa_range win = { 0xFD000000UL, 0xFE8FFFFFUL };
        r = pa_place(busy, 4, keep, 1, &win, 0x800000UL, 0x01000000UL, 0xFEC00000UL, &at);
        check(r == 0 && at == 0xFE000000UL && win.base == 0xFD000000UL && win.last == 0xFE8FFFFFUL,
              "bench C's ILOAD: FE000000, in the window as it is");
    }
    /* ... the ROM (128 KB) for its reading, where PCIPLACE read it */
    {
        struct pa_range busy[] = { RAM8, VIRGE, TOP, { 0xFA000000UL, 0xFBFFFFFFUL } };
        struct pa_range keep[] = { { 0xFD004000UL, 0xFD007FFFUL }, { 0xFE000000UL, 0xFE7FFFFFUL } };
        struct pa_range win = { 0xFD000000UL, 0xFE8FFFFFUL };
        r = pa_place(busy, 4, keep, 2, &win, 0x20000UL, 0x01000000UL, 0xFEC00000UL, &at);
        check(r == 0 && at == 0xFE8E0000UL, "bench C's ROM: FE8E0000, the window's top");
    }
    /* a window holding a device that must grow, a neighbour above it: it grows down instead */
    {
        struct pa_range busy[] = { RAM8, TOP, { 0xE1000000UL, 0xE1FFFFFFUL } };
        struct pa_range keep[] = { { 0xE0000000UL, 0xE0003FFFUL } };
        struct pa_range win = { 0xE0000000UL, 0xE00FFFFFUL };
        r = pa_place(busy, 3, keep, 1, &win, 0x1000000UL, 0x01000000UL, 0xFEC00000UL, &at);
        check(r == 0 && at == 0xDF000000UL && win.base == 0xDF000000UL && win.last == 0xE00FFFFFUL,
              "a window with a device in it widened downwards past its neighbour");
    }
    /* an empty window (base > last) and plenty of room: the highest free place */
    {
        struct pa_range busy[] = { RAM8, TOP };
        struct pa_range win = { 0xFFF00000UL, 0x000FFFFFUL };
        r = pa_place(busy, 2, 0, 0, &win, 0x2000000UL, 0x01000000UL, 0xFEC00000UL, &at);
        check(r == 0 && at == 0xFC000000UL && win.base == 0xFC000000UL && win.last == 0xFDFFFFFFUL,
              "an empty window: the highest aligned place below the firmware's");
    }
    /* RAM up to the floor kept clear; no room at all; sizes that aren't powers of two */
    {
        struct pa_range busy[] = { { 0x00000000UL, 0xEFFFFFFFUL }, TOP, { 0xF0000000UL, 0xFEBFFFFFUL } };
        struct pa_range win = { 0xFFF00000UL, 0x000FFFFFUL };
        r = pa_place(busy, 3, 0, 0, &win, 0x100000UL, 0x01000000UL, 0xFEC00000UL, &at);
        check(r == -1, "no room: -1");
        r = pa_place(busy, 1, 0, 0, &win, 0x3000000UL, 0x01000000UL, 0xFEC00000UL, &at);
        check(r == -1, "a size that isn't a power of two: -1");
    }
    {
        struct pa_range busy[] = { { 0x00000000UL, 0x0FFFFFFFUL }, TOP };
        struct pa_range win = { 0xFFF00000UL, 0x000FFFFFUL };
        r = pa_place(busy, 2, 0, 0, &win, 0x10000000UL, 0x10000000UL, 0x30000000UL, &at);
        check(r == 0 && at == 0x20000000UL, "within floor and ceil: the highest place below the ceiling");
    }
    printf("pcialloc_test: %d failures\n", fails);
    return fails != 0;
}
