/* apps/term/vt.c on the host: each family of sequences against the screen xterm would show (ctlseqs): text and
 * wrapping, cursor movement, erasing, the scroll region, insertion and deletion, the alternate screen, SGR's
 * colours, the line-drawing set, UTF-8, the reports, a resize; and vt_key's encodings. */
#include <stdio.h>
#include <string.h>

#include "vt.h"

static struct vt t;
static int fails;
static char got[VT_COLS_MAX * VT_ROWS_MAX * 3 + VT_ROWS_MAX + 1];

#define CHECK(c, ...) do { if (!(c)) { printf("vt_test: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static void feed(const char *s) { vt_feed(&t, (const unsigned char *)s, (int)strlen(s)); }

/* The screen's text against want, lines joined by '|' (the last ones blank may be left out). */
static void screen(const char *name, const char *want)
{
    char w[4096];
    int i, n = 0, rows = 1;
    for (i = 0; want[i]; i++) {
        w[n++] = want[i] == '|' ? '\n' : want[i];
        rows += want[i] == '|';
    }
    w[n++] = '\n';
    for (; rows < t.rows; rows++)
        w[n++] = '\n';
    w[n] = 0;
    vt_dump(&t, got, sizeof got);
    CHECK(!strcmp(got, w), "%s: the screen is\n%s---- not\n%s----", name, got, w);
}

static void cursor(const char *name, int x, int y) { CHECK(t.x == x && t.y == y, "%s: cursor %d,%d not %d,%d", name, t.x, t.y, x, y); }

static void key(const char *name, int app, unsigned k, unsigned ch, unsigned mods, const char *want)
{
    char out[32];
    int n;
    t.app_cursor = app;
    n = vt_key(&t, k, ch, mods, out, sizeof out);
    CHECK(n == (int)strlen(want) && !memcmp(out, want, (size_t)n), "key %s: %d bytes %.*s", name, n, n, out);
}

int main(void)
{
    vt_init(&t, 10, 4);
    feed("hello\r\nworld");
    screen("text", "hello|world");
    cursor("text", 5, 1);
    feed("\r\n0123456789AB");                   /* (10 columns: the 11th wraps) */
    screen("wrap", "hello|world|0123456789|AB");
    vt_init(&t, 10, 4);
    feed("0123456789");
    cursor("pending wrap", 9, 0);
    feed("\r");
    cursor("CR after a pending wrap", 0, 0);
    feed("\033[2;3HX\033[1;1HY\033[4;10HZ\033[99;99HW");
    screen("CUP", "Y123456789|  X||         W");
    cursor("CUP clamped", 9, 3);
    CHECK(t.cell[3 * VT_COLS_MAX + 9].ch == 'W', "CUP clamped: W at the corner");

    vt_init(&t, 10, 4);
    feed("aaaaaaaaaa\r\nbbbbbbbbbb\r\ncccccccccc\r\ndddddddddd");
    feed("\033[2;5H\033[K");
    screen("EL 0", "aaaaaaaaaa|bbbb|cccccccccc|dddddddddd");
    feed("\033[3;5H\033[1K");
    screen("EL 1", "aaaaaaaaaa|bbbb|     ccccc|dddddddddd");
    feed("\033[1;3H\033[J");
    screen("ED 0", "aa|||");
    feed("\033[2J");
    screen("ED 2", "|||");

    vt_init(&t, 10, 5);
    feed("1\r\n2\r\n3\r\n4\r\n5");
    feed("\033[2;4r");                          /* (the region: rows 2-4; the cursor goes home) */
    cursor("DECSTBM homes", 0, 0);
    feed("\033[4;1H\nX");
    screen("LF in the region", "1|3|4|X|5");
    feed("\033[2;1H\033M");
    screen("RI in the region", "1||3|4|5");
    feed("\033[r\033[5;1H\n");
    screen("LF at the bottom", "|3|4|5|");

    vt_init(&t, 10, 4);
    feed("abcdefghij\r\n2\r\n3\r\n4");
    feed("\033[1;3H\033[2@");
    screen("ICH", "ab  cdefgh|2|3|4");
    feed("\033[2P");
    screen("DCH", "abcdefgh|2|3|4");
    feed("\033[3X");
    screen("ECH", "ab   fgh|2|3|4");
    feed("\033[2;1H\033[L");
    screen("IL", "ab   fgh||2|3");
    feed("\033[M\033[M");
    screen("DL", "ab   fgh|3||");

    vt_init(&t, 10, 3);
    feed("main\r\nscreen");
    feed("\033[?1049h");
    screen("alt screen clean", "||");
    feed("vi here");
    feed("\033[?1049l");
    screen("main screen back", "main|screen|");
    cursor("1049 restores the cursor", 6, 1);

    vt_init(&t, 10, 2);
    feed("\033[1;31mR\033[0;38;5;200mP\033[48;2;255;255;0mY\033[7;4mU\033[mN");
    CHECK(t.cell[0].fg == 1 && (t.cell[0].attr & VT_BOLD), "SGR 1;31");
    CHECK(t.cell[1].fg == 200 && !t.cell[1].attr, "SGR 38;5;200: %u", t.cell[1].fg);
    CHECK(t.cell[2].bg == 226, "SGR 48;2;255;255;0 taken to 226: %u", t.cell[2].bg);
    CHECK((t.cell[3].attr & (VT_REVERSE | VT_UNDER)) == (VT_REVERSE | VT_UNDER), "SGR 7;4");
    CHECK(t.cell[4].fg == VT_DEFAULT && t.cell[4].bg == VT_DEFAULT && !t.cell[4].attr, "SGR 0");
    CHECK(vt_rgb(1, 1) == 0xCD0000 && vt_rgb(16 + 5 * 36, 1) == 0xFF0000 && vt_rgb(244, 1) == 0x808080, "vt_rgb");

    vt_init(&t, 10, 2);
    feed("\033(0lqk\033(Bx\016\017");
    vt_dump(&t, got, sizeof got);
    CHECK(!strncmp(got, "\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x90x", 10), "line drawing: %s", got);
    feed("\r\n\xc3\xa9\xe2\x82\xac!");
    CHECK(t.cell[VT_COLS_MAX].ch == 0xE9 && t.cell[VT_COLS_MAX + 1].ch == 0x20AC && t.cell[VT_COLS_MAX + 2].ch == '!',
          "UTF-8");

    vt_init(&t, 10, 4);
    feed("\033[3;4H\033[6n\033[c");
    CHECK(t.reply_n == 6 + 7 && !memcmp(t.reply, "\033[3;4R\033[?1;2c", 13), "DSR and DA: %.*s", t.reply_n, t.reply);
    feed("\033]0;the title\007\033]2;another\033\\x");
    CHECK(!strcmp(t.title, "another") && t.title_set, "OSC titles: %s", t.title);
    feed("\033P+q544e\033\\y");                 /* (a DCS read and skipped) */
    vt_dump(&t, got, sizeof got);
    CHECK(strstr(got, "xy") != NULL, "DCS skipped: %s", got);

    vt_init(&t, 10, 4);
    feed("\tA\033[3g\033[1;5H\033H\r\tB\033[1;5H\033[0g\r\tC");
    screen("tabs", "    B   AC|||");          /* (no stops left: a tab goes to the last column) */

    vt_init(&t, 10, 4);
    feed("1\r\n2\r\n3\r\n4");
    vt_resize(&t, 5, 2);
    screen("shorter: the top goes", "3|4");
    vt_resize(&t, 8, 3);
    screen("longer: blank below", "3|4|");

    key("up", 0, 0x148, 0, 0, "\033[A");
    key("up (application)", 1, 0x148, 0, 0, "\033OA");
    key("ctrl+right", 0, 0x14D, 0, 2, "\033[1;5C");
    key("home", 0, 0x147, 0, 0, "\033[H");
    key("delete", 0, 0x153, 0, 0, "\033[3~");
    key("pgdn", 0, 0x151, 0, 0, "\033[6~");
    key("F1", 0, 0x3B, 0, 0, "\033OP");
    key("F5", 0, 0x3F, 0, 0, "\033[15~");
    key("backspace", 0, 0x0E, 8, 0, "\x7f");
    key("enter", 0, 0x1C, 13, 0, "\r");
    key("escape", 0, 0x01, 27, 0, "\033");
    key("ctrl+c", 0, 0x2E, 'c', 2, "\x03");
    key("alt+x", 0, 0x2D, 'x', 4, "\033x");
    key("e acute", 0, 0x12, 0xE9, 0, "\xc3\xa9");
    key("shift+tab", 0, 0x0F, 0, 1, "\033[Z");

    printf("vt_test: %d failures\n", fails);
    return fails != 0;
}
