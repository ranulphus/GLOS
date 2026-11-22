/* names_test: kernel/ndos/names.c: search templates, matching, search attributes and the code page's upper case,
 * against what MS-DOS 7.1 does (RBIL's 4Eh and 29h; the default capitalisation tables of code pages 437 and 850).
 * "names_test: 0 failures" when all held. */
#include <stdio.h>
#include <string.h>

#include "../../kernel/ndos/names.h"

static int fails;

static void tmpl(const char *spec, const char *want, int wild)
{
    unsigned char t[12];
    int r = dos_template(437, spec, t);

    t[11] = 0;
    if (want ? (r != wild || memcmp(t, want, 11)) : r != -1) {
        printf("FAIL template \"%s\": %d \"%s\", want %d \"%s\"\n", spec, r, r < 0 ? "" : (char *)t,
               want ? wild : -1, want ? want : "");
        fails++;
    }
}

static void match(const char *spec, const char *name11, int want)
{
    unsigned char t[11];

    if (dos_template(437, spec, t) < 0 || dos_match(t, (const unsigned char *)name11) != want) {
        printf("FAIL \"%s\" against \"%s\": want %d\n", spec, name11, want);
        fails++;
    }
}

static void attr(unsigned search, unsigned char a, int want)
{
    if (dos_attr_match(search, a) != want) {
        printf("FAIL search %02x, entry %02x: want %d\n", search, a, want);
        fails++;
    }
}

static void upper(int cp, unsigned char b, unsigned char want)
{
    unsigned char got = dos_upper(cp, b);

    if (got != want) {
        printf("FAIL upper cp%d %02x: %02x, want %02x\n", cp, b, got, want);
        fails++;
    }
}

int main(void)
{
    /* Templates. */
    tmpl("*.*", "???????????", 1);
    tmpl("*.TXT", "????????TXT", 1);
    tmpl("*.txt", "????????TXT", 1);
    tmpl("foo*.c", "FOO?????C  ", 1);
    tmpl("F*O.C", "F???????C  ", 1);
    tmpl("*", "????????   ", 1);
    tmpl("?.?", "?       ?  ", 1);
    tmpl("README", "README     ", 0);
    tmpl("README.", "README     ", 0);
    tmpl("toolongname.text", "TOOLONGNTEX", 0);
    tmpl("a?c.d*", "A?C     D??", 1);
    tmpl(".", ".          ", 0);
    tmpl("..", "..         ", 0);
    tmpl("caf\x82.txt", "CAFE    TXT", 0);       /* (cp437: e acute to E, as MS-DOS 7.1's table has it) */
    tmpl("a.b.c", 0, 0);
    tmpl(".txt", 0, 0);
    tmpl("", 0, 0);
    tmpl("a b.c", 0, 0);
    tmpl("a+b", 0, 0);
    tmpl("a|b", 0, 0);
    tmpl("[x]", 0, 0);
    tmpl("\xE5" "XY", "\x05" "XY        ", 0);   /* (437's sigma: not upper-cased; E5h kept as 05h) */

    /* Matching. */
    match("*.*", "README     ", 1);
    match("*.*", "AUTOEXECBAT", 1);
    match("*", "README     ", 1);
    match("*", "AUTOEXECBAT", 0);                  /* (no extension in "*": only names without one) */
    match("*.BAT", "AUTOEXECBAT", 1);
    match("*.BAT", "AUTOEXECBAK", 0);
    match("?.?", "A       B  ", 1);
    match("?.?", "AB         ", 0);
    match("?.?", "A          ", 1);
    match("AUTO*.*", "AUTOEXECBAT", 1);
    match("AUTO*.*", "CONFIG  SYS", 0);
    match("CONFIG.SYS", "CONFIG  SYS", 1);
    match("config.sys", "CONFIG  SYS", 1);

    /* Search attributes (bits: 01h read-only, 02h hidden, 04h system, 08h label, 10h folder, 20h archive). */
    attr(0x00, 0x00, 1);
    attr(0x00, 0x20, 1);                           /* archive doesn't count */
    attr(0x00, 0x01, 1);                           /* nor read-only */
    attr(0x00, 0x02, 0);
    attr(0x00, 0x10, 0);
    attr(0x10, 0x10, 1);
    attr(0x10, 0x12, 0);                           /* a hidden folder: hidden is not asked for */
    attr(0x16, 0x12, 1);
    attr(0x16, 0x07, 1);
    attr(0x08, 0x08, 1);                           /* label alone: the label */
    attr(0x08, 0x00, 0);                           /* ... and nothing else */
    attr(0x3F, 0x08, 1);                           /* everything: the label too */
    attr(0x16, 0x08, 0);
    attr(0x00, 0x08, 0);

    /* Upper case in the page. */
    upper(437, 'a', 'A');
    upper(437, 'Z', 'Z');
    upper(437, 0x82, 'E');                         /* e acute -> E (MS-DOS's table, though 437 has E acute) */
    upper(437, 0x84, 0x8E);                        /* a diaeresis -> A diaeresis */
    upper(437, 0x85, 'A');                         /* a grave -> A: 437 has no A grave */
    upper(437, 0x8A, 'E');                         /* e grave -> E */
    upper(437, 0xA4, 0xA5);                        /* n tilde -> N tilde */
    upper(437, 0x98, 'Y');                         /* y diaeresis -> Y */
    upper(437, 0xE1, 0xE1);                        /* sharp s: as it is */
    upper(437, 0xE5, 0xE5);                        /* sigma: a maths symbol in 437, as it is */
    upper(737, 0x98, 0x80);                        /* Greek alpha -> Alpha in the Greek page */
    upper(850, 0x85, 0xB7);                        /* a grave -> A grave: 850 has it */
    upper(850, 0x8A, 0xD4);                        /* e grave -> E grave */
    upper(850, 0x98, '_');                         /* y diaeresis -> _ (850 has no capital for it) */
    upper(850, 0x82, 0x90);                        /* e acute -> E acute in 850 */
    upper(866, 0xA0, 0x80);                        /* Cyrillic a -> A */

    printf("names_test: %d failures\n", fails);
    return fails != 0;
}
