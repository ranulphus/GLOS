/* GLOS's own tray icons' pictures (kernel/wm/desktray.c; phase D, D2): one-bit shapes, drawn in the bar's text
 * colour. Here for the host's tests too: the demonstration's desktop there has the network's icon, as a PC with an
 * address has it (tests/host/wm_test.c, Loop A's desk suite). */
#ifndef K_TRAYART_H
#define K_TRAYART_H
#include "types.h"

/* The pictures, 16 by 16: X is the bar's text colour. */
static const char *const art_vol[16] = {
    "................", "................", ".......X........", "......XX...X....", ".....XXX....X...", ".XXXXXXX..X..X..",
    ".XXXXXXX...X.X..", ".XXXXXXX...X.X..", ".XXXXXXX...X.X..", ".XXXXXXX..X..X..", ".....XXX....X...", "......XX...X....",
    ".......X........", "................", "................", "................",
};
static const char *const art_low[16] = {
    "................", "................", ".......X........", "......XX........", ".....XXX........", ".XXXXXXX..X.....",
    ".XXXXXXX...X....", ".XXXXXXX...X....", ".XXXXXXX...X....", ".XXXXXXX..X.....", ".....XXX........", "......XX........",
    ".......X........", "................", "................", "................",
};
static const char *const art_mute[16] = {
    "................", "................", ".......X........", "......XX........", ".....XXX........", ".XXXXXXX.X...X..",
    ".XXXXXXX..X.X...", ".XXXXXXX...X....", ".XXXXXXX..X.X...", ".XXXXXXX.X...X..", ".....XXX........", "......XX........",
    ".......X........", "................", "................", "................",
};
static const char *const art_net[16] = {
    "................", "..XXXXXXXXXXXX..", "..X..........X..", "..X..........X..", "..X..........X..", "..X..........X..",
    "..X..........X..", "..XXXXXXXXXXXX..", ".......XX.......", ".......XX.......", "....XXXXXXXX....", "................",
    ".XXXXXXXXXXXXXX.", ".......XX.......", "................", "................",
};
static const char *const art_nonet[16] = {
    "................", "..XXXXXXXXXXXX..", "..X..X....X..X..", "..X...X..X...X..", "..X....XX....X..", "..X...X..X...X..",
    "..X..X....X..X..", "..XXXXXXXXXXXX..", ".......XX.......", ".......XX.......", "....XXXXXXXX....", "................",
    ".XXXXXXXXXXXXXX.", ".......XX.......", "................", "................",
};
static const char *const art_pad[16] = {
    "................", "................", "................", "...XXXX..XXXX...", "..XXXXXXXXXXXX..", ".XXX.XXXXXX.XXX.",
    ".XX...XXXX.X.XX.", ".XXX.XXXXXX.XXX.", ".XXXXXXXXXXXXXX.", ".XXXX......XXXX.", ".XXX........XXX.", "..X..........X..",
    "................", "................", "................", "................",
};
static const char *const art_remote[16] = {
    "................", "................", "................", "....XXXXXXXX....", "..XX........XX..", ".X....XXXX....X.",
    "X....XXXXXX....X", "X....XXXXXX....X", ".X....XXXX....X.", "..XX........XX..", "....XXXXXXXX....", "................",
    "................", "................", "................", "................",
};

/* A picture's 256 pixels, its shape in `ink` (00RRGGBBh), the rest clear. */
static inline void trayart_picture(u32 *out, const char *const *art, u32 ink)
{
    u32 i, j;
    for (j = 0; j < 16; j++)
        for (i = 0; i < 16; i++)
            out[j * 16 + i] = art[j][i] == 'X' ? 0xFF000000u | ink : 0;
}

#endif
