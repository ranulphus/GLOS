/* Host test for the PDF Viewer's drawing (apps/shared/pdfdraw.c): tests/host/pdf_cases.py's draw.pdf, a page with
 * a shape of every kind at a known place, drawn and its pixels looked at: a filled box, a stroked line, a
 * triangle, a hole by the even-odd rule, a clip, a matrix, a disc of curves, pictures (RGB, indexed, a stencil),
 * half strength, CMYK, a form; the page turned and cropped; a part of a page at a larger scale; and files with
 * bytes changed at random, drawn: no harm.
 *
 *     pdfdraw_test DIR [OUT]        (the .pdf files; OUT: the pages written there as .ppm, to look at) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../apps/shared/pdfdraw.h"
#include "../../apps/shared/pdffont.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); \
                                                printf("\n"); fails++; } } while (0)

static const char *dir, *out_dir, *fonts_dir;
static unsigned char *file;
static unsigned long file_size;

static unsigned long mem_read(void *ctx, unsigned long at, void *buf, unsigned long n)
{
    (void)ctx;
    if (at >= file_size)
        return 0;
    if (n > file_size - at)
        n = file_size - at;
    memcpy(buf, file + at, n);
    return n;
}

static struct pdf *open_pdf(const char *name, int *err)
{
    char path[600];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    free(file);
    file = NULL, file_size = 0;
    *err = -99;
    if (!(f = fopen(path, "rb")))
        return NULL;
    fseek(f, 0, SEEK_END);
    file_size = (unsigned long)ftell(f);
    rewind(f);
    file = malloc(file_size + 1);
    if (fread(file, 1, file_size, f) != file_size)
        file_size = 0;
    fclose(f);
    return pdf_open(mem_read, NULL, file_size, err);
}

static struct pdf_canvas cv;
static unsigned long seed = 20261120ul;

static unsigned long rnd(void)
{
    seed = seed * 1103515245ul + 12345ul;
    return (seed >> 8) & 0xFFFFFFul;
}

static void canvas(int w, int h)
{
    free(cv.px);
    cv.w = w, cv.h = h;
    cv.px = malloc((size_t)w * (size_t)h * sizeof *cv.px);
    memset(cv.px, 0xFF, (size_t)w * (size_t)h * sizeof *cv.px);
}

static unsigned at(int x, int y) { return cv.px[y * cv.w + x] & 0xFFFFFF; }

static int is(int x, int y, unsigned want)      /* within a step or two a colour (rounding) */
{
    unsigned got = at(x, y);
    int i;
    for (i = 0; i < 24; i += 8)
        if (abs((int)(got >> i & 255) - (int)(want >> i & 255)) > 2) {
            printf("  (%d, %d) is %06X, not %06X\n", x, y, got, want);
            return 0;
        }
    return 1;
}

/* The stand-in fonts, from apps/pdf/fonts. */
static const unsigned char *a_face(int family, int bold, int italic, unsigned long *len)
{
    static const char *const stem[3] = { "PDFSANS", "PDFSERI", "PDFMONO" };
    static unsigned char *kept[3][2][2];
    static unsigned long size[3][2][2];
    char path[600];
    FILE *f;
    if (!kept[family][bold][italic]) {
        snprintf(path, sizeof path, "%s/%s%s.TTF", fonts_dir, stem[family], bold && italic ? "Z" : bold ? "B" : italic ? "I" : "");
        if (!(f = fopen(path, "rb")))
            return NULL;
        kept[family][bold][italic] = malloc(1 << 16);
        size[family][bold][italic] = (unsigned long)fread(kept[family][bold][italic], 1, 1 << 16, f);
        fclose(f);
    }
    *len = size[family][bold][italic];
    return kept[family][bold][italic];
}

/* The ink in a part of the canvas: how many pixels are darker than half, and where they are across. */
static int ink(int x0, int y0, int x1, int y1, int *left, int *right)
{
    int x, y, n = 0;
    *left = x1, *right = x0;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++) {
            unsigned p = cv.px[y * cv.w + x];
            if ((p >> 16 & 255) + (p >> 8 & 255) < 255) {
                n++;
                *left = x < *left ? x : *left, *right = x > *right ? x : *right;
            }
        }
    return n;
}

static void keep(const char *name)
{
    char path[600];
    FILE *f;
    int i;
    if (!out_dir)
        return;
    snprintf(path, sizeof path, "%s/%s.ppm", out_dir, name);
    if (!(f = fopen(path, "wb")))
        return;
    fprintf(f, "P6 %d %d 255\n", cv.w, cv.h);
    for (i = 0; i < cv.w * cv.h; i++) {
        unsigned char rgb[3] = { (unsigned char)(cv.px[i] >> 16), (unsigned char)(cv.px[i] >> 8), (unsigned char)cv.px[i] };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    struct pdf *d;
    double w, h;
    int err, x, y, n;
    dir = argc > 1 ? argv[1] : ".";
    fonts_dir = argc > 2 ? argv[2] : "apps/pdf/fonts";
    out_dir = argc > 3 ? argv[3] : NULL;
    pdf_font_faces(a_face);

    if ((d = open_pdf("draw.pdf", &err)) != NULL) {
        CHECK(pdf_page_size(d, 0, &w, &h) == PDF_OK && w == 200 && h == 100, "the page's size: %g by %g", w, h);
        canvas(200, 100);
        CHECK(pdf_draw(d, 0, 1, 0, 0, &cv) == PDF_OK, "drawn");
        keep("draw");
        CHECK(is(50, 70, 0xFF0000) && is(5, 70, 0xFFFFFF) && is(50, 55, 0xFFFFFF) && is(50, 95, 0xFFFFFF), "a filled box");
        CHECK(is(140, 79, 0x0000FF) && is(140, 80, 0x0000FF) && is(140, 75, 0xFFFFFF) && is(140, 84, 0xFFFFFF) && is(95, 80, 0xFFFFFF),
              "a line four wide");
        CHECK(is(140, 40, 0x00FF00) && is(140, 14, 0x00FF00) && is(105, 20, 0xFFFFFF) && is(175, 20, 0xFFFFFF), "a triangle");
        CHECK(is(25, 15, 0x000000) && is(35, 25, 0xFFFFFF) && is(45, 35, 0x000000), "a hole by the even-odd rule");
        CHECK(is(160, 90, 0xFF00FF) && is(145, 90, 0xFFFFFF) && is(160, 70, 0xFFFFFF) && is(175, 90, 0xFFFFFF), "a clip");
        CHECK(is(4, 96, 0x808080) && is(4, 88, 0xFFFFFF) && is(12, 96, 0xFF0000) == 0, "a matrix");
        CHECK(is(80, 25, 0x00FFFF) && is(88, 25, 0x00FFFF) && is(80, 11, 0xFFFFFF) && is(90, 15, 0xFFFFFF), "a disc of curves");
        CHECK(is(155, 25, 0xFF0000) && is(165, 25, 0x00FF00) && is(155, 35, 0x0000FF) && is(165, 35, 0xFFFF00), "a picture");
        CHECK(is(184, 12, 0x800000) && is(192, 12, 0x008000), "an indexed picture");
        CHECK(is(66, 92, 0xFF8000) && is(62, 92, 0xFFFFFF) && is(62, 96, 0xFF8000) && is(66, 96, 0xFFFFFF), "a stencil");
        CHECK(is(20, 83, 0x7F007F), "half strength over red");
        CHECK(is(192, 96, 0xFF00FF) && is(196, 92, 0xFFFFFF), "CMYK");
        CHECK(is(125, 93, 0x4080BF), "a form");
        /* an edge between pixels is neither colour: the line's ends at x = 100 are sharp (on a pixel's edge),
           the triangle's sloping sides are smoothed */
        {
            int soft = 0;
            for (x = 100; x < 140; x++) {
                unsigned p = at(x, 30);
                soft += p != 0xFFFFFF && p != 0x00FF00;
            }
            CHECK(soft >= 1 && soft <= 3, "the triangle's edge is smoothed: %d pixels between the colours in a row", soft);
        }
        /* turned a quarter: 100 by 200, the page's bottom left at the top left */
        CHECK(pdf_page_size(d, 1, &w, &h) == PDF_OK && w == 100 && h == 200, "turned: %g by %g", w, h);
        canvas(100, 200);
        pdf_draw(d, 1, 1, 0, 0, &cv);
        keep("turned");
        CHECK(is(30, 50, 0xFF0000) && is(20, 140, 0x0000FF) && is(50, 5, 0xFFFFFF), "turned a quarter");
        /* cropped: the box's part of the page alone */
        CHECK(pdf_page_size(d, 2, &w, &h) == PDF_OK && w == 100 && h == 50, "cropped: %g by %g", w, h);
        canvas(100, 50);
        pdf_draw(d, 2, 1, 0, 0, &cv);
        keep("cropped");
        CHECK(is(40, 30, 0x0000FF) && is(60, 40, 0xFF00FF) && is(92, 46, 0xFF00FF) && is(40, 10, 0xFFFFFF), "cropped");
        /* a part of the page at three times the size: the canvas's corner 300 pixels across, 150 down */
        canvas(120, 90);
        pdf_draw(d, 0, 3, 300, 150, &cv);
        keep("part");
        CHECK(is(60, 87, 0x0000FF) && is(119, 89, 0x0000FF) && is(60, 80, 0xFFFFFF) && is(5, 40, 0xFFFFFF)
              && is(100, 30, 0xFFFFFF), "a part of it, larger: the line twelve pixels thick at the canvas's foot");
        CHECK(pdf_draw(d, 9, 1, 0, 0, &cv) == PDF_E_DAMAGED, "no such page");
        pdf_close(d);
    } else {
        CHECK(0, "draw.pdf: not opened: %d", err);
    }

    /* text: at twice the size (600 by 400). Where each line is, there is ink; a line is as long as its font's
       advances make it; what is not painted leaves none */
    if ((d = open_pdf("text.pdf", &err)) != NULL) {
        int l, r, n2;
        canvas(600, 400);
        CHECK(pdf_draw(d, 0, 2, 0, 0, &cv) == PDF_OK, "text drawn");
        keep("text");
        /* "Hello, GLOS!" in the sans at 24 points: its advances sum to 5.891 em (as Helvetica's): 141.4 points */
        n = ink(0, 20, 600, 80, &l, &r);
        CHECK(n > 2500 && l >= 40 && l <= 46 && r >= 2 * (20 + 141.4) - 14 && r <= 2 * (20 + 141.4) + 2,
              "the stand-in sans: %d pixels of ink from %d to %d", n, l, r);
        n = ink(0, 100, 600, 128, &l, &r);
        CHECK(n > 1200 && l >= 40 && l <= 44 && r > 330 && r < 420, "the serif, kerned: %d pixels from %d to %d", n, l, r);
        /* the mono at 10 points, a point more after each letter and four more at each space: 19 letters of 7
           points and two spaces of four more: 141 points */
        n = ink(0, 140, 600, 166, &l, &r);
        CHECK(n > 600 && l >= 40 && l <= 46 && r >= 2 * (20 + 141) - 16 && r <= 2 * (20 + 141), "the mono, spaced: %d pixels from %d to %d", n,
              l, r);
        n = ink(0, 172, 600, 200, &l, &r);
        CHECK(n > 1200 && r < 330, "bold with accents (the spacing gone again): %d pixels to %d", n, r);
        n2 = ink(0, 202, 600, 230, &l, &r);
        CHECK(n2 > 700 && l >= 40 && l <= 46, "italic on the next line: %d pixels from %d", n2, l);
        n = ink(0, 255, 520, 295, &l, &r);
        CHECK(n > 800 && l >= 40 && l <= 46 && r >= 2 * (20 + 23 * 7) - 14 && r <= 2 * (20 + 23 * 7) + 2,
              "a font the file carries, 23 letters of half an em each: %d pixels from %d to %d", n, l, r);
        n = ink(0, 298, 280, 330, &l, &r);
        CHECK(n > 800 && l >= 40 && l <= 48 && r >= 2 * (20 + 9 * 8.4) - 16 && r <= 2 * (20 + 9 * 8.4) + 2,
              "a font by glyph numbers, nine letters of 0.6 em: %d pixels from %d to %d", n, l, r);
        n = ink(530, 240, 580, 380, &l, &r);
        CHECK(n > 300 && r - l < 30, "a quarter turned: %d pixels, %d wide", n, r - l + 1);
        n = ink(290, 340, 420, 366, &l, &r);
        CHECK(n == 0, "unseen text: %d pixels", n);
        n = ink(296, 366, 440, 392, &l, &r);
        CHECK(n > 150 && l >= 300 && l <= 306, "small text in blue: %d pixels from %d", n, l);
        pdf_close(d);
    } else {
        CHECK(0, "text.pdf: not opened: %d", err);
    }

    /* Files with bytes changed, drawn: whatever is in them, the drawing comes back and stays in its canvas. */
    {
        unsigned long made = 0, drawn = 0, round, k, orig_size = 0;
        unsigned char *orig = NULL;
        for (round = 0; round < 1500; round++) {
            unsigned long changes;
            if (round % 750 == 0) {             /* (the shapes' page, then the text's) */
                d = open_pdf(round ? "text.pdf" : "draw.pdf", &err);
                pdf_close(d);
                free(orig);
                orig = malloc(file_size), orig_size = file_size;
                memcpy(orig, file, file_size);
            }
            if (orig_size < 200)
                continue;
            free(file);
            file = malloc(orig_size);
            memcpy(file, orig, orig_size);
            file_size = orig_size;
            changes = 1 + rnd() % 12;
            for (k = 0; k < changes; k++) {
                unsigned long where = rnd() % file_size;
                if (where < 8 && rnd() % 8)
                    continue;
                file[where] = rnd() % 3 ? (unsigned char)rnd() : (unsigned char)"0123456789 /<>[]().-\n"[rnd() % 21];
            }
            made++;
            if ((d = pdf_open(mem_read, NULL, file_size, &err)) != NULL) {
                for (n = 0; n < 3; n++) {
                    canvas(64 + (int)(rnd() % 200), 48 + (int)(rnd() % 100));
                    if (pdf_draw(d, (unsigned)n, 0.25 + (double)(rnd() % 300) / 100, (int)(rnd() % 100) - 20, (int)(rnd() % 100) - 20, &cv)
                        == PDF_OK)
                        drawn++;
                }
                pdf_close(d);
            }
        }
        free(orig);
        CHECK(made == 1500 && drawn > 1200, "%lu changed files, %lu pages drawn", made, drawn);
        printf("pdfdraw_test: %lu changed files, %lu pages drawn\n", made, drawn);
    }
    free(file);
    free(cv.px);
    (void)x, (void)y;
    printf("pdfdraw_test: %d failures (%d checks)\n", fails, checks);
    return fails != 0;
}
