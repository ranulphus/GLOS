/* The file manager's pictures (files.h): drawn here, in code, at 16 and 32
 * pixels, from the palette's cube and greys, in Slate's restraint: black
 * lines, white and greys, a colour of its own for each kind. Index 0 is
 * clear. */
#include <string.h>

#include "files.h"

/* The palette's cube: r, g, b from 0 to 5. */
#define CUBE(r, g, b) ((unsigned char)(16 + (r) * 36 + (g) * 6 + (b)))
#define BLACK CUBE(0, 0, 0)
#define WHITE CUBE(5, 5, 5)
#define GREY  CUBE(3, 3, 3)
#define LIGHT CUBE(4, 4, 4)
#define DARK  CUBE(1, 1, 1)

static unsigned char pics[IC_COUNT][2][32 * 32];
static int made;

/* Drawing on a picture of side n, in units of a 32-pixel one (scaled down for 16). */
struct pen {
    unsigned char *p;
    int n;
};

static void px(struct pen *g, int x, int y, unsigned char c)
{
    if (x >= 0 && y >= 0 && x < g->n && y < g->n)
        g->p[y * g->n + x] = c;
}

static void fill(struct pen *g, int x, int y, int w, int h, unsigned char c)
{
    int i, j, s = 32 / g->n;
    for (j = y / s; j < (y + h + s - 1) / s; j++)
        for (i = x / s; i < (x + w + s - 1) / s; i++)
            px(g, i, j, c);
}

static void frame(struct pen *g, int x, int y, int w, int h, unsigned char c)
{
    int s = 32 / g->n;
    fill(g, x, y, w, s, c);
    fill(g, x, y + h - s, w, s, c);
    fill(g, x, y, s, h, c);
    fill(g, x + w - s, y, s, h, c);
}

static void page(struct pen *g, unsigned char lines)
{
    fill(g, 6, 2, 20, 28, WHITE);
    frame(g, 6, 2, 20, 28, BLACK);
    if (lines) {
        int y;
        for (y = 8; y < 26; y += 4)
            fill(g, 10, y, 12, 2, lines);
    }
}

static void screen(struct pen *g, unsigned char face)
{
    fill(g, 2, 4, 28, 20, face);
    frame(g, 2, 4, 28, 20, BLACK);
    fill(g, 10, 24, 12, 4, GREY);
    fill(g, 6, 28, 20, 2, BLACK);
}

static void draw(int kind, struct pen *g)
{
    int i;
    switch (kind) {
    case IC_FOLDER:
        fill(g, 2, 6, 12, 4, CUBE(5, 4, 1));
        fill(g, 2, 9, 28, 19, CUBE(5, 4, 1));
        frame(g, 2, 9, 28, 19, BLACK);
        frame(g, 2, 6, 12, 4, BLACK);
        break;
    case IC_DRIVE:
        fill(g, 2, 10, 28, 14, LIGHT);
        frame(g, 2, 10, 28, 14, BLACK);
        fill(g, 6, 18, 10, 2, DARK);
        fill(g, 22, 17, 4, 4, CUBE(1, 4, 1));
        break;
    case IC_FILE:
        page(g, 0);
        break;
    case IC_TEXT:
        page(g, GREY);
        break;
    case IC_PICTURE:
        page(g, 0);
        fill(g, 9, 6, 14, 12, CUBE(2, 4, 5));
        fill(g, 10, 12, 6, 6, CUBE(1, 3, 1));
        fill(g, 16, 8, 4, 4, CUBE(5, 5, 1));
        frame(g, 9, 6, 14, 12, BLACK);
        break;
    case IC_PROGRAM:
        fill(g, 2, 4, 28, 24, WHITE);
        frame(g, 2, 4, 28, 24, BLACK);
        fill(g, 2, 4, 28, 6, DARK);
        fill(g, 24, 6, 4, 2, CUBE(5, 3, 1));
        break;
    case IC_BATCH:
        page(g, DARK);
        fill(g, 6, 2, 20, 6, DARK);
        break;
    case IC_TERMINAL:
        screen(g, BLACK);
        fill(g, 6, 8, 6, 2, WHITE);
        fill(g, 14, 8, 4, 2, WHITE);
        fill(g, 6, 12, 8, 2, WHITE);
        fill(g, 6, 18, 4, 2, WHITE);
        break;
    case IC_NOTEPAD:
        page(g, GREY);
        fill(g, 6, 2, 20, 4, CUBE(1, 2, 4));
        break;
    case IC_CALC:
        fill(g, 6, 2, 20, 28, LIGHT);
        frame(g, 6, 2, 20, 28, BLACK);
        fill(g, 9, 5, 14, 6, WHITE);
        frame(g, 9, 5, 14, 6, BLACK);
        for (i = 0; i < 9; i++)
            fill(g, 9 + (i % 3) * 5, 14 + (i / 3) * 5, 4, 4, i == 8 ? CUBE(5, 3, 1) : DARK);
        break;
    case IC_IMAGES:
        fill(g, 2, 6, 28, 20, WHITE);
        fill(g, 4, 8, 24, 16, CUBE(2, 4, 5));
        fill(g, 6, 16, 10, 8, CUBE(1, 3, 1));
        fill(g, 16, 14, 12, 10, CUBE(2, 3, 2));
        fill(g, 20, 9, 4, 4, CUBE(5, 5, 1));
        frame(g, 2, 6, 28, 20, BLACK);
        break;
    case IC_MONITOR:
        screen(g, WHITE);
        fill(g, 5, 18, 3, 4, CUBE(1, 4, 1));
        fill(g, 9, 14, 3, 8, CUBE(1, 4, 1));
        fill(g, 13, 16, 3, 6, CUBE(1, 4, 1));
        fill(g, 17, 10, 3, 12, CUBE(5, 3, 1));
        fill(g, 21, 12, 3, 10, CUBE(1, 4, 1));
        break;
    case IC_CONTROL:
        fill(g, 4, 4, 24, 24, LIGHT);
        frame(g, 4, 4, 24, 24, BLACK);
        for (i = 0; i < 3; i++) {
            fill(g, 8 + i * 7, 8, 2, 16, DARK);
            fill(g, 6 + i * 7, 10 + i * 5, 6, 4, CUBE(5, 3, 1));
            frame(g, 6 + i * 7, 10 + i * 5, 6, 4, BLACK);
        }
        break;
    case IC_FILES:
        draw(IC_FOLDER, g);
        fill(g, 8, 13, 16, 2, DARK);
        fill(g, 8, 17, 16, 2, DARK);
        fill(g, 8, 21, 10, 2, DARK);
        break;
    case IC_GAME:
        fill(g, 4, 10, 24, 14, DARK);
        frame(g, 4, 10, 24, 14, BLACK);
        fill(g, 8, 16, 8, 2, WHITE);
        fill(g, 11, 13, 2, 8, WHITE);
        fill(g, 20, 14, 3, 3, CUBE(5, 1, 1));
        fill(g, 23, 18, 3, 3, CUBE(1, 3, 5));
        break;
    case IC_TOOL:
        fill(g, 4, 20, 16, 6, DARK);
        fill(g, 14, 6, 6, 18, GREY);
        frame(g, 14, 6, 6, 18, BLACK);
        fill(g, 20, 4, 8, 8, LIGHT);
        frame(g, 20, 4, 8, 8, BLACK);
        break;
    case IC_SOUND:
        fill(g, 4, 12, 8, 8, DARK);
        fill(g, 12, 8, 6, 16, DARK);
        fill(g, 22, 10, 2, 12, GREY);
        fill(g, 26, 6, 2, 20, GREY);
        break;
    case IC_NETWORK:
        screen(g, CUBE(2, 4, 5));
        fill(g, 14, 0, 4, 4, DARK);
        break;
    }
}

const unsigned char *icon(int kind, int big)
{
    if (!made) {
        int k, b;
        for (k = 0; k < IC_COUNT; k++)
            for (b = 0; b < 2; b++) {
                struct pen g;
                g.p = pics[k][b];
                g.n = b ? 32 : 16;
                memset(g.p, 0, sizeof pics[k][b]);
                draw(k, &g);
            }
        made = 1;
    }
    return pics[kind < 0 || kind >= IC_COUNT ? IC_FILE : kind][big ? 1 : 0];
}

int icon_by_name(const char *name)
{
    static const struct { const char *n; int k; } names[] = {
        { "program", IC_PROGRAM }, { "game", IC_GAME }, { "tool", IC_TOOL }, { "text", IC_TEXT },
        { "picture", IC_PICTURE }, { "sound", IC_SOUND }, { "network", IC_NETWORK } };
    unsigned i;
    for (i = 0; i < sizeof names / sizeof *names; i++)
        if (!strcmp(names[i].n, name))
            return names[i].k;
    return IC_PROGRAM;
}

static int ext_is(const char *name, const char *const *list)
{
    const char *d = strrchr(name, '.');
    int i;
    if (!d)
        return 0;
    for (i = 0; list[i]; i++) {
        const char *a = d + 1, *b = list[i];
        while (*a && *b && (*a & ~0x20) == *b)
            a++, b++;
        if (!*a && !*b)
            return 1;
    }
    return 0;
}

int icon_for_file(const char *name, int dir)
{
    static const char *const text[] = { "TXT", "CFG", "INI", "LOG", "DOC", "ME", "1ST", "C", "H", "ASM", "PAS", 0 };
    static const char *const pics_[] = { "PNG", "JPG", "JPEG", "BMP", "GIF", "TGA", "PCX", "PPM", "PGM", "PNM", 0 };
    static const char *const progs[] = { "EXE", "COM", 0 };
    static const char *const bats[] = { "BAT", 0 };
    if (dir)
        return IC_FOLDER;
    if (ext_is(name, text))
        return IC_TEXT;
    if (ext_is(name, pics_))
        return IC_PICTURE;
    if (ext_is(name, progs))
        return IC_PROGRAM;
    if (ext_is(name, bats))
        return IC_BATCH;
    return IC_FILE;
}
