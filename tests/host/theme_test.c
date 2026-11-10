/* Host test for the built-in themes (kernel/wm/theme.c; API 2.0 step 5): every role has a colour, every colour
 * is in the 8-bit desktop's palette, and text can be read on its ground. */
#include <stdio.h>
#include <string.h>
#include "glos/api2.h"
#include "theme.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static double part(unsigned v)                  /* an sRGB part's light, 0 to 1: the palette's six levels */
{
    static const double level[6] = { 0.0, 0.03310, 0.13287, 0.31855, 0.60383, 1.0 };
    return level[v / 0x33 > 5 ? 5 : v / 0x33];
}

static double light(u32 rgb) { return 0.2126 * part(rgb >> 16 & 255) + 0.7152 * part(rgb >> 8 & 255) + 0.0722 * part(rgb & 255); }

static double contrast(u32 a, u32 b)            /* WCAG's ratio: 1 to 21 */
{
    double la = light(a), lb = light(b);
    return la > lb ? (la + 0.05) / (lb + 0.05) : (lb + 0.05) / (la + 0.05);
}

/* Text and the ground it is written on. */
static const struct { int text, ground; const char *what; } pairs[] = {
    { GLOS_ROLE_FACE_TEXT, GLOS_ROLE_FACE, "face" }, { GLOS_ROLE_FIELD_TEXT, GLOS_ROLE_FIELD, "field" },
    { GLOS_ROLE_MARK_TEXT, GLOS_ROLE_MARK, "mark" }, { GLOS_ROLE_CHOSEN_TEXT, GLOS_ROLE_CHOSEN, "chosen" },
    { GLOS_ROLE_TITLE_TEXT, GLOS_ROLE_TITLE, "title" }, { GLOS_ROLE_TITLE_OFF_TEXT, GLOS_ROLE_TITLE_OFF, "title-off" },
    { GLOS_ROLE_BAR_TEXT, GLOS_ROLE_BAR, "bar" }, { GLOS_ROLE_BAR_ON_TEXT, GLOS_ROLE_BAR_ON, "bar-on" },
    { GLOS_ROLE_LINK, GLOS_ROLE_FIELD, "link" }, { GLOS_ROLE_CODE_KEYWORD, GLOS_ROLE_FIELD, "keyword" },
    { GLOS_ROLE_CODE_STRING, GLOS_ROLE_FIELD, "string" }, { GLOS_ROLE_CODE_COMMENT, GLOS_ROLE_FIELD, "comment" },
    { GLOS_ROLE_CODE_NUMBER, GLOS_ROLE_FIELD, "number" }, { GLOS_ROLE_CODE_TYPE, GLOS_ROLE_FIELD, "type" },
    { GLOS_ROLE_CODE_DIRECTIVE, GLOS_ROLE_FIELD, "directive" }, { GLOS_ROLE_ERROR, GLOS_ROLE_FIELD, "error" },
    { GLOS_ROLE_GOOD, GLOS_ROLE_FIELD, "good" }, { GLOS_ROLE_LINE, GLOS_ROLE_FACE, "line" },
};

int main(void)
{
    u32 t, r, i, seen;

    CHECK(THEME_ROLES == GLOS_ROLES && themes_n == 3);
    CHECK(theme_find("slate") == &themes[0] && theme_find("dark") == &themes[1] && theme_find("contrast") == &themes[2]);
    CHECK(theme_find("slat") == 0 && theme_find("slates") == 0 && theme_find("") == 0);
    CHECK(!(themes[0].flags & GLOS_THEME_DARK) && (themes[1].flags & GLOS_THEME_DARK) && (themes[2].flags & GLOS_THEME_CONTRAST));
    for (t = 0; t < themes_n; t++) {
        const struct theme *th = &themes[t];
        double least = t == 2 ? 7.0 : 3.0;      /* (3: WCAG's least for large text and a control's parts; 7: AAA) */
        CHECK(strlen(th->name) < 16);
        for (r = 0, seen = 0; r < GLOS_ROLES; r++) {            /* in the palette's cube: each part a multiple of 33h */
            u32 c = th->colour[r];
            if (c >> 24 || (c >> 16 & 255) % 0x33 || (c >> 8 & 255) % 0x33 || (c & 255) % 0x33) {
                printf("FAIL theme %s role %u colour %06x is not in the palette\n", th->name, r, c);
                fails++;
            }
            seen |= c != 0;
        }
        CHECK(seen);
        for (i = 0; i < sizeof pairs / sizeof pairs[0]; i++) {
            double k = contrast(th->colour[pairs[i].text], th->colour[pairs[i].ground]);
            if (k < least) {
                printf("FAIL theme %s: %s contrast %.2f under %.1f\n", th->name, pairs[i].what, k, least);
                fails++;
            }
        }
        /* What can't be used is still told from its ground, and from what can. */
        CHECK(contrast(th->colour[GLOS_ROLE_DIM], th->colour[GLOS_ROLE_FACE]) >= 1.5);
        CHECK(th->colour[GLOS_ROLE_DIM] != th->colour[GLOS_ROLE_FACE_TEXT]);
        /* The window with the focus is told from the others. */
        CHECK(th->colour[GLOS_ROLE_TITLE] != th->colour[GLOS_ROLE_TITLE_OFF]);
        CHECK(th->colour[GLOS_ROLE_BAR_ON] != th->colour[GLOS_ROLE_BAR]);
        CHECK(th->border >= 1 && th->title >= 12 && th->box < th->title && th->bar >= 12 && th->scroll >= 8 && th->icon >= 8);
        CHECK(th->text >= 0 && th->text <= 2);
    }
    /* Slate is the look GLOS has had (kernel/wm/wm.c's default, glos/gui.h's colours). */
    CHECK(themes[0].colour[GLOS_ROLE_FACE] == 0xCCCCCC && themes[0].colour[GLOS_ROLE_MARK] == 0xCC6633
          && themes[0].colour[GLOS_ROLE_DESK] == 0x666666 && themes[0].colour[GLOS_ROLE_TITLE] == 0x333333
          && themes[0].colour[GLOS_ROLE_TITLE_OFF] == 0x999999 && themes[0].title == 18 && themes[0].border == 1);

    printf("theme_test: %d failures\n", fails);
    return fails != 0;
}
