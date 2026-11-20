/* Host test for the PDF reader's objects (apps/shared/pdf.c): the small files tests/host/pdf_cases.py writes,
 * each held to what is in it: words of every kind, pages and what they have by their parents, streams packed
 * every way the reader unpacks, an updated file's newest objects, object streams and a cross-reference stream,
 * tables lost and made anew, a file cut short, locked, and none; files with bytes changed at random come to no
 * harm.
 *
 *     pdf_test DIR        (the .pdf files pdf_cases.py wrote) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../apps/shared/pdf.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); \
                                                printf("\n"); fails++; } } while (0)

static const char *dir;
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

/* A page's words: its content stream, unpacked. */
static int page_says(struct pdf *d, unsigned n, const char *want)
{
    unsigned long len = 0;
    unsigned char *b = pdf_stream(d, pdf_get(d, pdf_page(d, n), "Contents"), &len, NULL, NULL);
    int ok = b && len == strlen(want) && !memcmp(b, want, len);
    if (!ok)
        printf("  page %u says '%.*s'\n", n, b ? (int)len : 0, b ? (const char *)b : "");
    free(b);
    return ok;
}

static void two_pages(const char *name, const char *first, const char *title)
{
    int err = 0;
    struct pdf *d = open_pdf(name, &err);
    CHECK(d != NULL, "%s: not opened: %d", name, err);
    if (!d)
        return;
    CHECK(pdf_pages(d) == 2 && page_says(d, 0, first) && page_says(d, 1, "BT /F1 12 Tf 72 720 Td (Page 2) Tj ET")
          && !pdf_page(d, 2), "%s: its two pages", name);
    CHECK(pdf_num(pdf_item(d, pdf_page_attr(d, pdf_page(d, 0), "MediaBox"), 2), 0) == 612, "%s: a page's box by its parent", name);
    if (title)
        CHECK(!strcmp(pdf_string(pdf_get(d, pdf_get(d, pdf_trailer(d), "Info"), "Title"), NULL), title), "%s: its title", name);
    pdf_close(d);
}

int main(int argc, char **argv)
{
    static unsigned char text[20 * 45 + 256], rows[12 * 48];
    struct pdf *d;
    struct pdf_obj *o, *a;
    unsigned i, n;
    int err;
    dir = argc > 1 ? argv[1] : ".";
    for (i = 0; i < 20; i++)
        memcpy(text + i * 45, "The quick brown fox jumps over the lazy dog. ", 45);
    for (i = 0; i < 256; i++)
        text[900 + i] = (unsigned char)i;
    for (i = 0; i < sizeof rows; i++)
        rows[i] = (unsigned char)((i / 48 * 7 + i % 48 * 13) & 255);

    /* a plain file: pages, what a page has of its own and by its parent, words of every kind */
    two_pages("plain.pdf", "BT /F1 12 Tf 72 720 Td (Page 1) Tj ET", NULL);
    if ((d = open_pdf("plain.pdf", &err)) != NULL) {
        unsigned len = 0;
        const char *s;
        o = pdf_page(d, 1);
        CHECK(pdf_int(pdf_page_attr(d, o, "Rotate"), 0) == 90 && pdf_num(pdf_item(d, pdf_page_attr(d, o, "MediaBox"), 2), 0) == 200
              && pdf_int(pdf_page_attr(d, pdf_page(d, 0), "Rotate"), 0) == 0, "plain: a page's own box and turn");
        o = pdf_get(d, pdf_get(d, pdf_page_attr(d, pdf_page(d, 0), "Resources"), "Font"), "F1");
        CHECK(pdf_name_is(pdf_get(d, o, "BaseFont"), "Helvetica") && pdf_name_is(pdf_get(d, pdf_catalog(d), "Type"), "Catalog"),
              "plain: the font by the pages' resources");
        o = pdf_get(d, pdf_trailer(d), "Info");
        s = pdf_string(pdf_get(d, o, "Title"), &len);
        CHECK(len == 29 && !memcmp(s, "A (test) \\ A\n(nested) endless", 29), "plain: a string's escapes: %u '%s'", len, s);
        s = pdf_string(pdf_get(d, o, "Hex"), &len);
        CHECK(len == 6 && !memcmp(s, "Hello ", 6), "plain: a string in hex: %u '%s'", len, s);
        CHECK(pdf_name_is(pdf_get(d, o, "Name With"), "ABC") && pdf_string(pdf_get(d, o, "Empty"), &len) && len == 0
              && pdf_is(pdf_get(d, o, "Empty"), PDF_STRING), "plain: a name's escapes");
        a = pdf_get(d, o, "Numbers");
        CHECK(pdf_is(a, PDF_ARRAY) && a->n == 6 && pdf_num(pdf_item(d, a, 0), 9) == -0.5 && pdf_num(pdf_item(d, a, 1), 9) == 3
              && pdf_num(pdf_item(d, a, 2), 9) == 1.25 && pdf_int(pdf_item(d, a, 3), 9) == 0 && pdf_int(pdf_item(d, a, 4), 9) == -17
              && pdf_int(pdf_item(d, a, 5), 9) == 9 && pdf_num(pdf_item(d, a, 5), 9) == 4294967296.0, "plain: numbers");
        a = pdf_get(d, o, "Flags");
        CHECK(a && a->n == 3 && pdf_is(pdf_item(d, a, 0), PDF_BOOL) && pdf_num(pdf_item(d, a, 0), 5) == 5
              && pdf_item(d, a, 0)->u.num == 1 && pdf_item(d, a, 1)->u.num == 0 && pdf_is(pdf_item(d, a, 2), PDF_NULL), "plain: flags");
        a = pdf_get(d, o, "Deep");
        CHECK(a && a->n == 2 && pdf_int(pdf_item(d, pdf_item(d, pdf_item(d, pdf_item(d, a, 0), 1), 1), 0), 0) == 3
              && pdf_name_is(pdf_get(d, pdf_item(d, a, 1), "K"), "V"), "plain: arrays in arrays");
        CHECK(!pdf_object(d, 0) && !pdf_object(d, 99) && !pdf_get(d, o, "Nothing") && !pdf_item(d, a, 7) && pdf_int(NULL, 4) == 4
              && !strcmp(pdf_name(NULL), ""), "plain: what isn't there");
        pdf_trim(d, 0);                         /* (everything let go: read again as it is asked for) */
        CHECK(page_says(d, 1, "BT /F1 12 Tf 72 720 Td (Page 2) Tj ET"), "plain: after a trim");
        pdf_close(d);
    }
    /* an update: the newest of an object stands, a new object is there, the old ones still */
    two_pages("update.pdf", "BT /F1 12 Tf 72 720 Td (Page 11) Tj ET", NULL);
    if ((d = open_pdf("update.pdf", &err)) != NULL) {
        CHECK(!strcmp(pdf_string(pdf_object(d, 10), NULL), "new") && pdf_name_is(pdf_get(d, pdf_object(d, 7), "BaseFont"), "Helvetica"),
              "update: the new object and an old one");
        pdf_close(d);
    }
    /* 1.5's kind: objects in an object stream, the table a stream with a predictor */
    two_pages("packed.pdf", "BT /F1 12 Tf 72 720 Td (Page 1) Tj ET", "Packed");
    /* the tables lost: made anew from the objects themselves, those in an object stream too */
    two_pages("broken.pdf", "BT /F1 12 Tf 72 720 Td (Page 1) Tj ET", NULL);
    two_pages("brokenp.pdf", "BT /F1 12 Tf 72 720 Td (Page 1) Tj ET", "Packed");
    /* cut in its last object, no table at all: the pages are read */
    if ((d = open_pdf("cut.pdf", &err)) != NULL) {
        CHECK(pdf_pages(d) == 2 && page_says(d, 0, "BT /F1 12 Tf 72 720 Td (Page 1) Tj ET"), "cut: its pages");
        pdf_close(d);
    } else {
        CHECK(0, "cut.pdf: not opened: %d", err);
    }
    d = open_pdf("locked.pdf", &err);
    CHECK(!d && err == PDF_E_LOCKED, "locked: %d", err);
    d = open_pdf("nopdf.pdf", &err);
    CHECK(!d && err == PDF_E_KIND, "no PDF file: %d", err);

    /* streams packed every way: each says what it is */
    if ((d = open_pdf("filters.pdf", &err)) != NULL) {
        unsigned seen = 0, pictures = 0;
        for (n = 3; n < 40; n++) {
            unsigned long len = 0;
            const char *last = NULL, *is;
            unsigned char *b;
            if (!(o = pdf_object(d, n)) || !*(is = pdf_name(pdf_get(d, o, "Is"))))
                continue;
            b = pdf_stream(d, o, &len, &last, &err);
            seen++;
            if (!strcmp(is, "Text"))
                CHECK(b && len == sizeof text && !memcmp(b, text, len) && !*last, "filters: stream %u is not the text: %lu bytes, %d", n,
                      len, err);
            else if (!strcmp(is, "Rows"))
                CHECK(b && len == sizeof rows && !memcmp(b, rows, len) && !*last, "filters: stream %u is not the rows: %lu bytes", n, len);
            else {
                pictures++;
                CHECK(b && len == 26 && b[0] == 0xFF && b[1] == 0xD8 && !strcmp(last, "DCTDecode"), "filters: stream %u: %lu bytes, '%s'",
                      n, len, last);
            }
            free(b);
        }
        CHECK(seen == 16 && pictures == 2, "filters: %u streams, %u pictures", seen, pictures);
        pdf_close(d);
    } else {
        CHECK(0, "filters.pdf: not opened: %d", err);
    }

    /* a tree of pages three deep: each page by its number, what it has by its parents */
    if ((d = open_pdf("tree.pdf", &err)) != NULL) {
        int ok = pdf_pages(d) == 7;
        for (n = 0; n < 7; n++)
            ok = ok && pdf_int(pdf_get(d, pdf_page(d, n), "Number"), -1) == (long)n;
        CHECK(ok && !pdf_page(d, 7), "tree: its seven pages in order");
        CHECK(pdf_int(pdf_page_attr(d, pdf_page(d, 2), "Rotate"), 0) == 180 && pdf_int(pdf_page_attr(d, pdf_page(d, 1), "Rotate"), 0) == 0
              && pdf_page_attr(d, pdf_page(d, 3), "CropBox") && !pdf_page_attr(d, pdf_page(d, 4), "CropBox")
              && pdf_int(pdf_get(d, pdf_page_attr(d, pdf_page(d, 6), "Resources"), "A"), 0) == 1, "tree: by the parents");
        pdf_close(d);
    } else {
        CHECK(0, "tree.pdf: not opened: %d", err);
    }

    /* Files with bytes changed: opened or refused; every object, page and stream of what opens is gone through. */
    {
        static const char *const from[] = { "plain.pdf", "packed.pdf", "filters.pdf", "update.pdf", "tree.pdf" };
        unsigned long seed = 20261120ul, made = 0, opened = 0, round, k, orig_size = 0;
        unsigned char *orig = NULL;
        for (round = 0; round < 2500; round++) {
            unsigned long changes;
            if (round % 500 == 0) {
                d = open_pdf(from[round / 500], &err);
                CHECK(d != NULL, "%s: not opened", from[round / 500]);
                pdf_close(d);
                free(orig);
                orig = malloc(file_size);
                memcpy(orig, file, file_size);
                orig_size = file_size;
            }
            if (orig_size < 100)
                continue;
#define RND() (seed = seed * 1103515245ul + 12345ul, (seed >> 8) & 0xFFFFFFul)
            free(file);
            file = malloc(orig_size);
            memcpy(file, orig, orig_size);
            file_size = orig_size;
            changes = 1 + RND() % 10;
            for (k = 0; k < changes; k++) {
                unsigned long at = RND() % file_size;
                if (at < 8 && RND() % 8)                /* (mostly still a PDF) */
                    continue;
                file[at] = RND() % 3 ? (unsigned char)RND() : (unsigned char)"0123456789 /<>[]()R\n"[RND() % 20];
            }
            if (RND() % 5 == 0)
                file_size = 20 + RND() % (file_size - 20);
            made++;
            if ((d = pdf_open(mem_read, NULL, file_size, &err)) != NULL) {
                opened++;
                for (n = 0; n < pdf_pages(d) + 2 && n < 50; n++) {
                    unsigned long len;
                    o = pdf_page(d, n);
                    pdf_page_attr(d, o, "MediaBox");
                    free(pdf_stream(d, pdf_get(d, o, "Contents"), &len, NULL, NULL));
                }
                for (n = 0; n < 60; n++) {
                    unsigned long len;
                    if ((o = pdf_object(d, n)) != NULL && o->stream)
                        free(pdf_stream(d, o, &len, NULL, NULL));
                    if (n == 30)
                        pdf_trim(d, 2000);
                }
                pdf_close(d);
            }
        }
        free(orig);
        CHECK(made == 2500 && opened > 1500, "%lu changed files, %lu of them opened", made, opened);
        printf("pdf_test: %lu changed files, %lu of them opened\n", made, opened);
    }
    free(file);
    printf("pdf_test: %d failures (%d checks)\n", fails, checks);
    return fails != 0;
}
