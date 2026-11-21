/* Host test for the PostScript kinds of font (apps/shared/psfont.c): tests/host/psfont_cases.py's two made-up
 * fonts, a CFF and a Type 1 (plain and in hex), whose every point is known: glyphs found by name, by character,
 * by the font's own encoding; each outline's pieces where they should be (every Type 2 curve operator, the
 * flexes, hints and masks passed over, subroutines of both kinds; Type 1's flex by its calls out, div, a letter
 * and its accent put together); and the fonts with bytes changed at random, every glyph drawn: no harm.
 *
 *     psfont_test DIR */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../apps/shared/psfont.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); \
                                                printf("\n"); fails++; } } while (0)

/* what an outline gave: each piece's end (thousandths of the em), and its kind: m, l, c */
static struct { double x[128], y[128]; char kind[129]; int n, moves; double lo_x, hi_x, lo_y, hi_y; } got;

static void note(char kind, double x, double y)
{
    if (got.n < 128)
        got.x[got.n] = x * 1000, got.y[got.n] = y * 1000, got.kind[got.n++] = kind, got.kind[got.n] = 0;
    got.lo_x = x * 1000 < got.lo_x ? x * 1000 : got.lo_x, got.hi_x = x * 1000 > got.hi_x ? x * 1000 : got.hi_x;
    got.lo_y = y * 1000 < got.lo_y ? y * 1000 : got.lo_y, got.hi_y = y * 1000 > got.hi_y ? y * 1000 : got.hi_y;
}

static void s_move(void *c, double x, double y) { (void)c; got.moves++; note('m', x, y); }
static void s_line(void *c, double x, double y) { (void)c; note('l', x, y); }
static void s_curve(void *c, double cx, double cy, double x, double y) { (void)c, (void)cx, (void)cy; note('q', x, y); }
static void s_cubic(void *c, double ax, double ay, double bx, double by, double x, double y)
{
    (void)c, (void)ax, (void)ay, (void)bx, (void)by;
    note('c', x, y);
}
static const struct ttf_sink sink = { s_move, s_line, s_curve, NULL, s_cubic };

static int shape(const struct psfont *f, int g)
{
    memset(&got, 0, sizeof got);
    got.lo_x = got.lo_y = 1e9, got.hi_x = got.hi_y = -1e9;
    return psfont_outline(f, g, &sink);
}

static int at(int i, char kind, double x, double y)    /* piece i ended there */
{
    int ok = i < got.n && got.kind[i] == kind && fabs(got.x[i] - x) < 0.01 && fabs(got.y[i] - y) < 0.01;
    if (!ok && i < got.n)
        printf("  piece %d is %c to %g, %g; not %c to %g, %g\n", i, got.kind[i], got.x[i], got.y[i], kind, x, y);
    return ok;
}

static unsigned a_name(const char *n)   /* (the few names the fonts use) */
{
    return !strcmp(n, "A") ? 'A' : !strcmp(n, "B") ? 'B' : !strcmp(n, "acute") ? 0xB4 : !strcmp(n, "Aacute") ? 0xC1 : 0;
}

static unsigned long seed = 20261121ul;
static unsigned long rnd(void)
{
    seed = seed * 1103515245ul + 12345ul;
    return (seed >> 8) & 0xFFFFFFul;
}

static unsigned char *slurp(const char *dir, const char *name, unsigned long *size)
{
    char path[600];
    unsigned char *b = malloc(1 << 16);
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    *size = 0;
    if ((f = fopen(path, "rb")) != NULL) {
        *size = (unsigned long)fread(b, 1, 1 << 16, f);
        fclose(f);
    }
    return b;
}

static void a_type1(const char *dir, const char *name)
{
    unsigned long n;
    unsigned char *b = slurp(dir, name, &n);
    struct psfont *f = psfont_type1(b, n, a_name);
    int g;
    free(b);                                    /* (a Type 1 font's bytes are copied) */
    CHECK(f && psfont_glyphs(f) == 5 && !psfont_is_cid(f), "%s: not opened, or not five glyphs", name);
    if (!f)
        return;
    g = psfont_by_name(f, "A");
    CHECK(g == 1 && psfont_by_unicode(f, 'A') == 1 && psfont_by_code(f, 65) == 1 && psfont_by_code(f, 66) == 4
          && psfont_by_code(f, 193) == 3 && psfont_by_code(f, 67) == -1 && psfont_by_name(f, "nothing") == -1
          && psfont_by_unicode(f, 0xB4) == 2 && psfont_unicode(f, 3) == 0xC1 && !psfont_unicode(f, 0), "%s: its glyphs found", name);
    CHECK(shape(f, 1) == 0 && got.moves == 1 && at(0, 'm', 100, 0) && at(1, 'l', 400, 700) && at(2, 'l', 700, 0) && at(3, 'l', 100, 0)
          && got.n == 4, "%s: its A: %s", name, got.kind);
    /* a letter and its accent: the A as it is, the accent 300 to the right of its own place and 200 up */
    CHECK(shape(f, 3) == 0 && got.moves == 2 && at(0, 'm', 100, 0) && at(4, 'm', 350, 800) && at(5, 'l', 450, 950) && at(6, 'l', 510, 950)
          && fabs(got.hi_y - 950) < 0.01 && fabs(got.hi_x - 700) < 0.01 && got.n == 8, "%s: A with its accent: %s, to %g, %g", name,
          got.kind, got.hi_x, got.hi_y);
    /* a subroutine's line, a curve of each short kind, div, a general curve, a flex (two curves), a line */
    CHECK(shape(f, 4) == 0 && got.moves == 1 && at(0, 'm', 200, 100) && at(1, 'l', 300, 100) && at(2, 'c', 400, 200)
          && at(3, 'l', 400, 202) && at(4, 'c', 460, 262) && at(5, 'c', 510, 302) && at(6, 'c', 560, 282) && at(7, 'l', 560, 82)
          && at(8, 'l', 200, 100) && got.n == 9, "%s: its B: %s", name, got.kind);
    CHECK(shape(f, 0) == 0 && got.n == 0 && shape(f, 5) == -1 && shape(f, -1) == -1, "%s: an empty glyph, and none", name);
    psfont_close(f);
}

int main(int argc, char **argv)
{
    static const double blob[][2] = { { 100, 50 }, { 140, 160 }, { 170, 230 }, { 230, 270 }, { 239, 282 }, { 249, 302 }, { 259, 322 },
                                      { 268, 334 }, { 338, 364 }, { 518, 334 }, { 527, 346 }, { 554, 376 }, { 644, 436 }, { 864, 376 },
                                      { 954, 496 }, { 864, 786 }, { 914, 836 }, { 914, 636 } };
    static const char kinds[] = "cccccllccccccccclll";
    const char *dir = argc > 1 ? argv[1] : ".";
    unsigned long n;
    unsigned char *cff = slurp(dir, "SYNTH.CFF", &n);
    struct psfont *f = psfont_cff(cff, n, a_name);
    int i, ok;

    CHECK(f && psfont_glyphs(f) == 4 && !psfont_is_cid(f), "the CFF: not opened, or not four glyphs");
    if (f) {
        CHECK(psfont_by_name(f, "A") == 1 && psfont_by_unicode(f, 'B') == 2 && psfont_by_name(f, "blob") == 3
              && psfont_by_code(f, 65) == 1 && psfont_by_code(f, 0x80) == 3 && psfont_by_code(f, 67) == -1
              && psfont_by_name(f, "nothing") == -1 && psfont_by_unicode(f, 'C') == -1, "the CFF: its glyphs found");
        CHECK(psfont_unicode(f, 1) == 'A' && psfont_unicode(f, 2) == 'B' && !psfont_unicode(f, 3) && !psfont_unicode(f, 0)
              && !psfont_unicode(f, 4) && !psfont_unicode(f, -1), "the CFF: its glyphs' characters");
        /* the width before the first move is no part of it */
        CHECK(shape(f, 1) == 0 && got.moves == 1 && at(0, 'm', 100, 0) && at(1, 'l', 400, 700) && at(2, 'l', 700, 0) && at(3, 'l', 100, 0)
              && got.n == 4, "the CFF: its A: %s", got.kind);
        /* hints and a mask passed over; lines across and up by turns; a subroutine's circle, back where it began */
        CHECK(shape(f, 2) == 0 && got.moves == 2 && at(0, 'm', 50, 50) && at(1, 'l', 650, 50) && at(2, 'l', 650, 650) && at(3, 'l', 50, 650)
              && at(4, 'l', 50, 50) && at(5, 'm', 450, 350) && at(6, 'c', 350, 250) && at(7, 'c', 250, 350) && at(8, 'c', 350, 450)
              && at(9, 'c', 450, 350) && got.n == 10, "the CFF: its B: %s", got.kind);
        /* every curve operator, the flexes, the other kind of subroutine: where each piece ends */
        ok = shape(f, 3) == 0 && got.moves == 1 && at(0, 'm', 0, 0) && got.n == 20 && at(19, 'l', 0, 0);
        for (i = 0; i < 18; i++)
            ok = ok && at(i + 1, kinds[i], blob[i][0], blob[i][1]);
        CHECK(ok, "the CFF: its curves of every kind: %d pieces, %s", got.n, got.kind);
        CHECK(shape(f, 0) == 0 && got.n == 0 && shape(f, 4) == -1, "the CFF: an empty glyph, and none");
        psfont_close(f);
    }
    a_type1(dir, "SYNTH.PFA");
    a_type1(dir, "SYNTHX.PFA");
    CHECK(!psfont_cff((const unsigned char *)"no font", 7, a_name) && !psfont_type1((const unsigned char *)"no font", 7, a_name)
          && !psfont_type1(cff, n, a_name), "no fonts");

    /* Fonts with bytes changed: opened or refused; every glyph of what opens drawn and looked up. */
    {
        unsigned long made = 0, opened = 0, round, k, tn;
        unsigned char *t1 = slurp(dir, "SYNTH.PFA", &tn), *copy = malloc(1 << 16);
        for (round = 0; round < 4000; round++) {
            int is_cff = round & 1;
            unsigned long size = is_cff ? n : tn, changes;
            memcpy(copy, is_cff ? cff : t1, size);
            changes = 1 + rnd() % 6;
            for (k = 0; k < changes; k++) {
                unsigned long where = rnd() % size;
                copy[where] = (unsigned char)rnd();
            }
            if (rnd() % 6 == 0)
                size = 8 + rnd() % (size - 8);
            made++;
            f = is_cff ? psfont_cff(copy, size, a_name) : psfont_type1(copy, size, a_name);
            if (f) {
                opened++;
                for (i = -1; i < 12; i++) {
                    shape(f, i);
                    psfont_by_code(f, (unsigned)(i * 23) & 255), psfont_by_cid(f, (unsigned)i * 7);
                }
                psfont_by_name(f, "A"), psfont_by_name(f, "blob"), psfont_by_unicode(f, 0xB4);
                psfont_close(f);
            }
        }
        free(t1);
        free(copy);
        CHECK(made == 4000 && opened > 1500, "%lu changed fonts, %lu of them opened", made, opened);
        printf("psfont_test: %lu changed fonts, %lu of them opened\n", made, opened);
    }
    free(cff);
    printf("psfont_test: %d failures (%d checks)\n", fails, checks);
    return fails != 0;
}
