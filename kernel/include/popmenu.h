/* The desktop's own pop-up menus (phase D, docs/v1/phase-d.md D1): a task bar button's window menu, a group's
 * windows, and later the tray's lists. Which items there are, which one is current, what a key or a press at a
 * place chooses, and how the menu is laid out. The desktop draws it and acts on the choice (kernel/wm/deskpop.c).
 * Built for the host too (tests/host/popmenu_test.c). */
#ifndef K_POPMENU_H
#define K_POPMENU_H
#include "types.h"

#define PM_ITEMS 24                     /* items at most */
#define PM_TEXT  48                     /* an item's text's bytes, with its NUL ("&" before its letter) */

#define PMI_OFF   1                     /* can't be chosen now (drawn dim) */
#define PMI_SEP   2                     /* a line between items (no text, never current) */
#define PMI_CHECK 4                     /* ticked (the window in front, in a group's menu) */

struct pm_item {
    char text[PM_TEXT];
    u32 flags;                          /* PMI_ */
    u32 data;                           /* the desktop's own word for it (a window's id, a command) */
};

struct popmenu {
    struct pm_item it[PM_ITEMS];
    u32 n;
    s32 cur;                            /* the current item, -1: none */
    s32 w, h;                           /* its size, as laid out */
    s32 item_h, sep_h, pad;             /* an item's height, a separator's, the frame's padding */
};

#define PM_NONE   -2                    /* the key was the menu's, and nothing is chosen yet */
#define PM_CANCEL -1                    /* Esc: the menu goes, nothing chosen */

void pm_clear(struct popmenu *p);
int pm_add(struct popmenu *p, const char *text, u32 flags, u32 data);  /* its index, or -1 (full) */
void pm_layout(struct popmenu *p, int (*width)(const char *text, int len, int bold), s32 line_h, s32 min_w);
s32 pm_item_y(const struct popmenu *p, u32 i);  /* an item's top, in the menu */
int pm_at(const struct popmenu *p, s32 x, s32 y);      /* the item that can be chosen at x, y in the menu, or -1 */
int pm_key(struct popmenu *p, u32 key, u32 ch);        /* a key (a scan code, its character): PM_NONE, PM_CANCEL
                                                          or the item chosen */
int pm_first(struct popmenu *p);        /* the current item made the first that can be chosen; -1: none can */

#endif
