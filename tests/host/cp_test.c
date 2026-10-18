/* The DOS code pages (sdk/config/cp.c, cp_tables.c; M10 step G): each
 * page's 128 upper bytes to Unicode and back, strings both ways, what a page
 * hasn't got, a buffer too small, stray UTF-8, and a page GLOS hasn't got. */
#include <stdio.h>
#include <string.h>

#include "glos/cp.h"

static int fails;

static void check(int ok, const char *what)
{
    if (!ok) {
        printf("cp_test: FAIL %s\n", what);
        fails++;
    }
}

int main(void)
{
    static const int pages[] = { 437, 850, 852, 866, 737 };
    char all[129], utf8[512], back[129], what[80];
    const char *p;
    unsigned i, k;
    for (k = 0; k < sizeof pages / sizeof pages[0]; k++) {
        check(cp_table(pages[k]) != NULL, "a table for each page");
        for (i = 0x80; i < 0x100; i++) {
            snprintf(what, sizeof what, "page %d byte %02Xh round trip", pages[k], i);
            check(cp_byte(pages[k], cp_char(pages[k], (unsigned char)i)) == (int)i, what);
        }
        for (i = 0; i < 128; i++)
            all[i] = (char)(0x80 + i);
        all[128] = 0;
        snprintf(what, sizeof what, "page %d's bytes to UTF-8 and back", pages[k]);
        check(cp_to_utf8(pages[k], all, utf8, sizeof utf8) > 256 &&
              cp_from_utf8(pages[k], utf8, back, sizeof back) == 0 && !strcmp(back, all), what);
    }
    check(cp_char(866, 0x8F) == 0x41F && cp_char(850, 0x9D) == 0xD8 && cp_char(737, 0x80) == 0x391 &&
          cp_char(852, 0x9D) == 0x141 && cp_char(437, 0xC4) == 0x2500, "some letters where they belong");
    check(cp_from_utf8(866, "caf\xc3\xa9 \xd0\x9f", back, sizeof back) == 1 && !strcmp(back, "caf? \x8f"),
          "what the page hasn't got: '?', counted");
    check(cp_from_utf8(437, "abcdef", back, 4) == -1, "too small");
    check(cp_to_utf8(866, "\x8f\x8e", utf8, 4) == 2 && !strcmp(utf8, "\xd0\x9f"), "UTF-8 cut at a whole character");
    p = "\xff\xd0";
    check(cp_utf8_next(&p) == 0xFF && cp_utf8_next(&p) == 0xD0 && !*p, "stray bytes as Latin-1");
    check(cp_table(1252) == NULL && cp_char(1252, 0xC4) == 0x2500, "another page: 437");
    printf("cp_test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
