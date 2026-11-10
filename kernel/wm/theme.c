/* The built-in themes (theme.h). Every colour is one of the 8-bit desktop's palette (each part 00, 33, 66, 99,
 * CC or FF: kernel/gfx/pal.c's cube), so a theme looks the same at every depth. Slate is the look GLOS has had
 * since M7. The sizes are the same in all three for now: a theme with other sizes needs every window laid out
 * again, which comes with toolkit 2 (phase C). */
#include "glos/api2.h"
#include "theme.h"

#define SIZES 1, 18, 10, 20, 16, 16, 1  /* border, title, box, bar, scroll, icon, text */

const struct theme themes[] = {
    { "slate", 0, {
        [GLOS_ROLE_FACE] = 0xCCCCCC, [GLOS_ROLE_FACE_TEXT] = 0x000000,
        [GLOS_ROLE_FIELD] = 0xFFFFFF, [GLOS_ROLE_FIELD_TEXT] = 0x000000,
        [GLOS_ROLE_LINE] = 0x000000, [GLOS_ROLE_DIM] = 0x999999, [GLOS_ROLE_SHADE] = 0x666666,
        [GLOS_ROLE_DARK] = 0x333333,
        [GLOS_ROLE_MARK] = 0xCC6633, [GLOS_ROLE_MARK_TEXT] = 0xFFFFFF,
        [GLOS_ROLE_CHOSEN] = 0x333333, [GLOS_ROLE_CHOSEN_TEXT] = 0xFFFFFF,
        [GLOS_ROLE_LINK] = 0x0033CC,
        [GLOS_ROLE_DESK] = 0x666666, [GLOS_ROLE_FRAME] = 0x000000,
        [GLOS_ROLE_TITLE] = 0x333333, [GLOS_ROLE_TITLE_TEXT] = 0xFFFFFF,
        [GLOS_ROLE_TITLE_OFF] = 0x999999, [GLOS_ROLE_TITLE_OFF_TEXT] = 0x333333,
        [GLOS_ROLE_CLOSE] = 0xCC6633,
        [GLOS_ROLE_BAR] = 0x333333, [GLOS_ROLE_BAR_TEXT] = 0xFFFFFF,
        [GLOS_ROLE_BAR_ON] = 0xCC6633, [GLOS_ROLE_BAR_ON_TEXT] = 0xFFFFFF,
        [GLOS_ROLE_CODE_KEYWORD] = 0x0033CC, [GLOS_ROLE_CODE_STRING] = 0x006633,
        [GLOS_ROLE_CODE_COMMENT] = 0x666666, [GLOS_ROLE_CODE_NUMBER] = 0x993300,
        [GLOS_ROLE_CODE_TYPE] = 0x660099, [GLOS_ROLE_CODE_DIRECTIVE] = 0x996600,
        [GLOS_ROLE_ERROR] = 0xCC0000, [GLOS_ROLE_GOOD] = 0x006633 },
      SIZES },
    { "dark", GLOS_THEME_DARK, {
        [GLOS_ROLE_FACE] = 0x333333, [GLOS_ROLE_FACE_TEXT] = 0xCCCCCC,
        [GLOS_ROLE_FIELD] = 0x000000, [GLOS_ROLE_FIELD_TEXT] = 0xCCCCCC,
        [GLOS_ROLE_LINE] = 0x999999, [GLOS_ROLE_DIM] = 0x666666, [GLOS_ROLE_SHADE] = 0x000000,
        [GLOS_ROLE_DARK] = 0x666666,
        [GLOS_ROLE_MARK] = 0xCC6633, [GLOS_ROLE_MARK_TEXT] = 0xFFFFFF,
        [GLOS_ROLE_CHOSEN] = 0x336699, [GLOS_ROLE_CHOSEN_TEXT] = 0xFFFFFF,
        [GLOS_ROLE_LINK] = 0x6699FF,
        [GLOS_ROLE_DESK] = 0x000033, [GLOS_ROLE_FRAME] = 0x666666,
        [GLOS_ROLE_TITLE] = 0x000000, [GLOS_ROLE_TITLE_TEXT] = 0xFFFFFF,
        [GLOS_ROLE_TITLE_OFF] = 0x333333, [GLOS_ROLE_TITLE_OFF_TEXT] = 0x999999,
        [GLOS_ROLE_CLOSE] = 0xCC6633,
        [GLOS_ROLE_BAR] = 0x000000, [GLOS_ROLE_BAR_TEXT] = 0xCCCCCC,
        [GLOS_ROLE_BAR_ON] = 0xCC6633, [GLOS_ROLE_BAR_ON_TEXT] = 0xFFFFFF,
        [GLOS_ROLE_CODE_KEYWORD] = 0x6699FF, [GLOS_ROLE_CODE_STRING] = 0x99CC66,
        [GLOS_ROLE_CODE_COMMENT] = 0x999999, [GLOS_ROLE_CODE_NUMBER] = 0xFF9966,
        [GLOS_ROLE_CODE_TYPE] = 0xCC99FF, [GLOS_ROLE_CODE_DIRECTIVE] = 0xFFCC66,
        [GLOS_ROLE_ERROR] = 0xFF6666, [GLOS_ROLE_GOOD] = 0x66CC66 },
      SIZES },
    { "contrast", GLOS_THEME_DARK | GLOS_THEME_CONTRAST, {
        [GLOS_ROLE_FACE] = 0x000000, [GLOS_ROLE_FACE_TEXT] = 0xFFFFFF,
        [GLOS_ROLE_FIELD] = 0x000000, [GLOS_ROLE_FIELD_TEXT] = 0xFFFFFF,
        [GLOS_ROLE_LINE] = 0xFFFFFF, [GLOS_ROLE_DIM] = 0x999999, [GLOS_ROLE_SHADE] = 0xFFFFFF,
        [GLOS_ROLE_DARK] = 0xFFFFFF,
        [GLOS_ROLE_MARK] = 0xFFFF00, [GLOS_ROLE_MARK_TEXT] = 0x000000,
        [GLOS_ROLE_CHOSEN] = 0x00FFFF, [GLOS_ROLE_CHOSEN_TEXT] = 0x000000,
        [GLOS_ROLE_LINK] = 0xFFFF00,
        [GLOS_ROLE_DESK] = 0x000000, [GLOS_ROLE_FRAME] = 0xFFFFFF,
        [GLOS_ROLE_TITLE] = 0xFFFFFF, [GLOS_ROLE_TITLE_TEXT] = 0x000000,
        [GLOS_ROLE_TITLE_OFF] = 0x000000, [GLOS_ROLE_TITLE_OFF_TEXT] = 0xFFFFFF,
        [GLOS_ROLE_CLOSE] = 0xFFFF00,
        [GLOS_ROLE_BAR] = 0x000000, [GLOS_ROLE_BAR_TEXT] = 0xFFFFFF,
        [GLOS_ROLE_BAR_ON] = 0xFFFFFF, [GLOS_ROLE_BAR_ON_TEXT] = 0x000000,
        [GLOS_ROLE_CODE_KEYWORD] = 0xFFFF00, [GLOS_ROLE_CODE_STRING] = 0x00FF00,
        [GLOS_ROLE_CODE_COMMENT] = 0xCCCCCC, [GLOS_ROLE_CODE_NUMBER] = 0x00FFFF,
        [GLOS_ROLE_CODE_TYPE] = 0xFF99FF, [GLOS_ROLE_CODE_DIRECTIVE] = 0xFFCC00,
        [GLOS_ROLE_ERROR] = 0xFF6666, [GLOS_ROLE_GOOD] = 0x00FF00 },
      SIZES },
};
const u32 themes_n = sizeof themes / sizeof themes[0];

const struct theme *theme_find(const char *name)
{
    u32 i, k;
    for (i = 0; i < themes_n; i++) {
        for (k = 0; themes[i].name[k] && themes[i].name[k] == name[k]; k++) ;
        if (themes[i].name[k] == name[k])
            return &themes[i];
    }
    return 0;
}

void theme_look(const struct theme *t, struct theme_look *l)
{
    l->desk = t->colour[GLOS_ROLE_DESK];
    l->edge = t->colour[GLOS_ROLE_FRAME];
    l->face = t->colour[GLOS_ROLE_TITLE];
    l->text = t->colour[GLOS_ROLE_TITLE_TEXT];
    l->face_off = t->colour[GLOS_ROLE_TITLE_OFF];
    l->text_off = t->colour[GLOS_ROLE_TITLE_OFF_TEXT];
    l->mark = t->colour[GLOS_ROLE_CLOSE];
    l->bar = t->colour[GLOS_ROLE_BAR];
    l->bar_text = t->colour[GLOS_ROLE_BAR_TEXT];
    l->bar_on = t->colour[GLOS_ROLE_BAR_ON];
    l->bar_on_text = t->colour[GLOS_ROLE_BAR_ON_TEXT];
}
