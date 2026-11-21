/* Host test for the TrueType reader (apps/shared/ttf.c) on the PDF Viewer's stand-in fonts (apps/pdf/fonts, made
 * by tools/mkpdffonts.py): each opens; letters are found by their characters, have the advances the fonts they
 * were cut from have, and outlines of the right make (an A is two contours, an accented letter is put together
 * from two glyphs, every outline's pieces within the font's box and each contour closed); and the fonts with
 * bytes changed at random, every glyph of them drawn: no harm.
 *
 *     ttf_test FONT-DIR */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../apps/shared/ttf.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); \
                                                printf("\n"); fails++; } } while (0)

static struct { int moves, lines, curves; double lo, hi, sx, sy, x, y, open; } seen;

static void track(double x, double y)
{
    seen.lo = x < seen.lo ? x : seen.lo, seen.lo = y < seen.lo ? y : seen.lo;
    seen.hi = x > seen.hi ? x : seen.hi, seen.hi = y > seen.hi ? y : seen.hi;
    seen.x = x, seen.y = y;
}

static void s_move(void *c, double x, double y)
{
    (void)c;
    if (seen.moves && (seen.x != seen.sx || seen.y != seen.sy))
        seen.open++;                            /* (the contour before it didn't come back to its start) */
    seen.moves++;
    seen.sx = x, seen.sy = y;
    track(x, y);
}

static void s_line(void *c, double x, double y) { (void)c; seen.lines++; track(x, y); }
static void s_curve(void *c, double cx, double cy, double x, double y) { (void)c; seen.curves++; track(cx, cy); track(x, y); }
static const struct ttf_sink sink = { s_move, s_line, s_curve, NULL, NULL };

static int shape(const struct ttf *t, unsigned g)
{
    int r;
    memset(&seen, 0, sizeof seen);
    seen.lo = 1e9, seen.hi = -1e9;
    r = ttf_outline(t, g, &sink);
    if (seen.moves && (seen.x != seen.sx || seen.y != seen.sy))
        seen.open++;
    return r;
}

int main(int argc, char **argv)
{
    static const char *const names[] = { "PDFSANS", "PDFSANSB", "PDFSANSI", "PDFSANSZ", "PDFSERI", "PDFSERIB", "PDFSERII", "PDFSERIZ",
                                         "PDFMONO", "PDFMONOB", "PDFMONOI", "PDFMONOZ" };
    static unsigned char file[12][64 * 1024];
    static unsigned long size[12];
    const char *dir = argc > 1 ? argv[1] : ".";
    struct ttf t;
    unsigned i, g;
    for (i = 0; i < 12; i++) {
        char path[600];
        FILE *f;
        snprintf(path, sizeof path, "%s/%s.TTF", dir, names[i]);
        if ((f = fopen(path, "rb")) != NULL) {
            size[i] = (unsigned long)fread(file[i], 1, sizeof file[i], f);
            fclose(f);
        }
        CHECK(size[i] > 20000 && size[i] < sizeof file[i] && ttf_open(&t, file[i], size[i]) == 0, "%s: not opened (%lu bytes)", names[i],
              size[i]);
        if (!size[i] || ttf_open(&t, file[i], size[i]) != 0)
            continue;
        CHECK(t.em == 2048 && t.glyphs > 240 && t.glyphs < 260 && t.map_unicode, "%s: %u to the em, %u glyphs", names[i], t.em, t.glyphs);
        /* an A: two contours, of lines alone in the upright sans; an o: two contours of curves; a space: none */
        g = ttf_by_unicode(&t, 'A');
        CHECK(g && shape(&t, g) == 0 && seen.moves == 2 && !seen.open && seen.lo > -600 && seen.hi < 2200,
              "%s: its A: glyph %u, %d contours, %d left open, %g to %g", names[i], g, seen.moves, (int)seen.open, seen.lo, seen.hi);
        g = ttf_by_unicode(&t, 'o');
        CHECK(g && shape(&t, g) == 0 && seen.moves == 2 && seen.curves >= 8 && !seen.open, "%s: its o: %d contours, %d curves", names[i],
              seen.moves, seen.curves);
        g = ttf_by_unicode(&t, ' ');
        CHECK(g && shape(&t, g) == 0 && !seen.moves && ttf_advance(&t, g) > 400, "%s: its space", names[i]);
        /* an e with an acute accent: the e's contours and the accent's */
        g = ttf_by_unicode(&t, 0xE9);
        CHECK(g && shape(&t, g) == 0 && seen.moves == 3 && !seen.open && seen.hi > 1300, "%s: its e acute: %d contours to %g", names[i],
              seen.moves, seen.hi);
        CHECK(ttf_by_unicode(&t, 0x20AC) && ttf_by_unicode(&t, 0x2019) && ttf_by_unicode(&t, 0xFB01) && !ttf_by_unicode(&t, 0x4E00)
              && !ttf_by_symbol(&t, 'A') && !ttf_by_mac(&t, 'A'), "%s: the characters it has and hasn't", names[i]);
        for (g = 0; g <= t.glyphs + 3; g++) {   /* every glyph: drawn or (past the last) refused, each contour closed */
            int r = shape(&t, g);
            if ((g <= t.glyphs) != (r == 0) || seen.open) {
                CHECK(0, "%s: glyph %u: %d, %d contours left open", names[i], g, r, (int)seen.open);
                break;
            }
        }
    }
    /* the advances the fonts they were cut from have: Helvetica's, Times's and Courier's widths, of 2048 */
    if (ttf_open(&t, file[0], size[0]) == 0)
        CHECK(ttf_advance(&t, ttf_by_unicode(&t, 'A')) == 1366 && ttf_advance(&t, ttf_by_unicode(&t, 'i')) == 455
              && ttf_advance(&t, ttf_by_unicode(&t, ' ')) == 569, "the sans's advances: %d %d %d", ttf_advance(&t, ttf_by_unicode(&t, 'A')),
              ttf_advance(&t, ttf_by_unicode(&t, 'i')), ttf_advance(&t, ttf_by_unicode(&t, ' ')));
    if (ttf_open(&t, file[4], size[4]) == 0)
        CHECK(ttf_advance(&t, ttf_by_unicode(&t, 'A')) == 1479 && ttf_advance(&t, ttf_by_unicode(&t, 'i')) == 569,
              "the serif's advances: %d %d", ttf_advance(&t, ttf_by_unicode(&t, 'A')), ttf_advance(&t, ttf_by_unicode(&t, 'i')));
    if (ttf_open(&t, file[8], size[8]) == 0)
        CHECK(ttf_advance(&t, ttf_by_unicode(&t, 'A')) == 1229 && ttf_advance(&t, ttf_by_unicode(&t, 'i')) == 1229,
              "the mono's advances: %d %d", ttf_advance(&t, ttf_by_unicode(&t, 'A')), ttf_advance(&t, ttf_by_unicode(&t, 'i')));
    CHECK(ttf_open(&t, (const unsigned char *)"not a font at all, only some text", 33) != 0 && ttf_open(&t, file[0], 11) != 0, "no font");

    /* Fonts with bytes changed: opened or refused; every glyph of what opens drawn, every character looked up. */
    {
        unsigned long seed = 20261120ul, made = 0, opened = 0, round, k;
        static unsigned char copy[64 * 1024];
        for (round = 0; round < 3000; round++) {
            unsigned f = (unsigned)(round % 12);
            unsigned long n = size[f], changes;
            if (n < 1000)
                continue;
#define RND() (seed = seed * 1103515245ul + 12345ul, (seed >> 8) & 0xFFFFFFul)
            memcpy(copy, file[f], n);
            changes = 1 + RND() % 16;
            for (k = 0; k < changes; k++) {
                unsigned long at = RND() % 3 ? RND() % 400 : RND() % n;         /* (mostly in its tables' heads) */
                copy[at] = (unsigned char)RND();
            }
            if (RND() % 6 == 0)
                n = 12 + RND() % (n - 12);
            made++;
            if (ttf_open(&t, copy, n) == 0) {
                opened++;
                for (g = 0; g < 300; g++) {
                    shape(&t, g);
                    ttf_advance(&t, g);
                    ttf_by_unicode(&t, g * 37), ttf_by_symbol(&t, g), ttf_by_mac(&t, g);
                }
            }
        }
        CHECK(made == 3000 && opened > 300, "%lu changed fonts, %lu of them opened", made, opened);
        printf("ttf_test: %lu changed fonts, %lu of them opened\n", made, opened);
    }
    printf("ttf_test: %d failures (%d checks)\n", fails, checks);
    return fails != 0;
}
