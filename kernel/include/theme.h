/* The built-in themes ([desktop] theme; API 2.0's SETTINGS THEME; docs/v1/toolkit-2.md 5): colours by role and
 * the sizes that go with them. The window manager's look and every app's toolkit are drawn from the one in
 * force. Built for the host too (tests/host/theme_test.c). */
#ifndef K_THEME_H
#define K_THEME_H
#include "types.h"

#define THEME_ROLES 32                  /* (GLOS_ROLES) */

struct theme {
    const char *name;
    u32 flags;                          /* GLOS_THEME_ */
    u32 colour[THEME_ROLES];            /* by GLOS_ROLE_, 0x00RRGGBB */
    s32 border, title, box, bar, scroll, icon, text;
};

/* The window manager's part of a theme (its roles, by name: wm.h's look takes them). */
struct theme_look {
    u32 desk, edge, face, text, face_off, text_off, mark, bar, bar_text, bar_on, bar_on_text;
};
void theme_look(const struct theme *t, struct theme_look *l);

extern const struct theme themes[];
extern const u32 themes_n;
const struct theme *theme_find(const char *name);       /* 0: no such theme */

#endif
