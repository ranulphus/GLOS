/* regex_test.c: the toolkit's finding (sdk/gui/regex.c; toolkit 2, step C6): plain text with and without case
   and whole words, regular expressions (sets, counts, anchors, groups, alternatives, lazy counts, classes),
   searching back, what replaces a match, wrong expressions, and a pattern that would take forever bounded. */
#include <stdio.h>
#include <string.h>
#include "glos/gui.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* The first match of what in text from 0: "start-end", or "none", or "bad". */
static const char *m(const char *what, unsigned flags, const char *text)
{
    static char out[32];
    struct gui_found f;
    int r = gui_match(what, flags, text, (int)strlen(text), 0, &f);
    if (r < 0)
        return "bad";
    if (!r)
        return "none";
    snprintf(out, sizeof out, "%d-%d", f.start, f.end);
    return out;
}

static const char *rep(const char *what, const char *with, const char *text)
{
    static char out[128];
    struct gui_found f;
    if (gui_match(what, GUI_FIND_REGEX | GUI_FIND_CASE, text, (int)strlen(text), 0, &f) != 1)
        return "none";
    if (gui_match_replace(with, GUI_FIND_REGEX, &f, text, out, sizeof out) < 0)
        return "room";
    return out;
}

int main(void)
{
    const unsigned R = GUI_FIND_REGEX, C = GUI_FIND_CASE;
    struct gui_found f;
    static char big[4000];
    /* plain text */
    CHECK(!strcmp(m("fox", 0, "The quick brown fox"), "16-19"));
    CHECK(!strcmp(m("FOX", 0, "The quick brown fox"), "16-19"));
    CHECK(!strcmp(m("FOX", C, "The quick brown fox"), "none"));
    CHECK(!strcmp(m("cat", GUI_FIND_WORDS, "concat cat"), "7-10"));
    CHECK(!strcmp(m("a.b", 0, "axb a.b"), "4-7"));                     /* (no special characters) */
    CHECK(!strcmp(m("", 0, "anything"), "none"));
    /* expressions */
    CHECK(!strcmp(m("a.b", R, "axb"), "0-3"));
    CHECK(!strcmp(m("ab*c", R, "xac abbbc"), "1-3"));
    CHECK(!strcmp(m("ab+c", R, "xac abbbc"), "4-9"));
    CHECK(!strcmp(m("colou?r", R, "color colour"), "0-5"));
    CHECK(!strcmp(m("[0-9]+", R, "abc 1234 x"), "4-8"));
    CHECK(!strcmp(m("[^a-z ]+", R | C, "abc DEF"), "4-7"));
    CHECK(!strcmp(m("[^a-z ]+", R, "abc DEF"), "none"));               /* (any case: capitals are in a-z too) */
    CHECK(!strcmp(m("^b", R, "ab\nbc"), "3-4"));
    CHECK(!strcmp(m("a$", R, "ba\nca"), "1-2"));
    CHECK(!strcmp(m("\\d+\\s\\w+", R, "id: 42 apples"), "4-13"));
    CHECK(!strcmp(m("cat|dog", R, "hotdog"), "3-6"));
    CHECK(!strcmp(m("(ab)+", R, "xababab"), "1-7"));
    CHECK(!strcmp(m("(a|b)*c", R, "xabbac"), "1-6"));
    CHECK(!strcmp(m("<.*>", R, "<a><b>"), "0-6"));
    CHECK(!strcmp(m("<.*?>", R, "<a><b>"), "0-3"));
    CHECK(!strcmp(m("A[bc]", R, "xaC"), "1-3"));                       /* (any case: a set too) */
    CHECK(!strcmp(m("A[bc]", R | C, "xaC aB Ab"), "7-9"));
    CHECK(!strcmp(m("\\.\\*", R, "a.*b"), "1-3"));
    CHECK(!strcmp(m("x(y)?z", R, "xz"), "0-2"));
    CHECK(!strcmp(m("(a*)*b", R, "aaab"), "0-4"));                     /* (empty passes don't loop) */
    CHECK(!strcmp(m("[]x]", R, "a]"), "1-2"));
    /* wrong ones */
    CHECK(!strcmp(m("(ab", R, "ab"), "bad"));
    CHECK(!strcmp(m("[ab", R, "ab"), "bad"));
    CHECK(!strcmp(m("*a", R, "a"), "bad"));
    CHECK(!strcmp(m("a)", R, "a"), "bad"));
    /* back */
    CHECK(gui_match("o", GUI_FIND_BACK, "foo boo", 7, 6, &f) == 1 && f.start == 5);
    CHECK(gui_match("o", GUI_FIND_BACK, "foo boo", 7, 1, &f) == 0);
    /* replacing */
    CHECK(!strcmp(rep("(\\w+)@(\\w+)", "\\2 at \\1", "mail ross@glos now"), "glos at ross"));
    CHECK(!strcmp(rep("b+", "[&]", "abbbc"), "[bbb]"));
    CHECK(!strcmp(rep("b+", "\\&", "abbbc"), "&"));
    CHECK(!strcmp(rep("x(y)?z", "<\\1>", "xz"), "<>"));
    /* bounded: a pattern that backtracks without end on this text is "not found", not a hang */
    memset(big, 'a', sizeof big - 2);
    big[sizeof big - 2] = 'b';
    CHECK(gui_match("(a*)*c", R, big, (int)strlen(big), 0, &f) == 0);
    printf("regex_test: %d failures\n", fails);
    return fails != 0;
}
