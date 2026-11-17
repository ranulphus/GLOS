/* Host test for the desktop's pop-up menus (kernel/wm/popmenu.c; phase D step D1): the keys, letters, presses and
 * the layout. */
#include <stdio.h>
#include <string.h>
#include "popmenu.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int width(const char *t, int len, int bold) { (void)t, (void)bold; return len * 6; }    /* 6 pixels a character */

int main(void)
{
    static struct popmenu p;
    int r;

    /* A window's menu: Restore can't be used (it is normal), a line, Close. */
    pm_clear(&p);
    CHECK(pm_add(&p, "&Restore", PMI_OFF, 1) == 0);
    CHECK(pm_add(&p, "Mi&nimise", 0, 2) == 1);
    CHECK(pm_add(&p, "Ma&ximise", 0, 3) == 2);
    CHECK(pm_add(&p, "", PMI_SEP, 0) == 3);
    CHECK(pm_add(&p, "&Close", 0, 4) == 4);
    CHECK(pm_first(&p) == 1);                   /* (Restore can't be current) */
    pm_layout(&p, width, 11, 100);
    CHECK(p.item_h == 17 && p.sep_h == 7);
    CHECK(p.h == 3 + 4 * 17 + 7 + 3);
    CHECK(p.w == 100 + 6);                      /* (the texts are narrower than the least) */
    CHECK(pm_item_y(&p, 4) == 3 + 3 * 17 + 7);

    /* Keys: Down skips the line, wraps round past Restore; Up back; Home and End. */
    CHECK(pm_key(&p, 0x150, 0) == PM_NONE && p.cur == 2);
    CHECK(pm_key(&p, 0x150, 0) == PM_NONE && p.cur == 4);
    CHECK(pm_key(&p, 0x150, 0) == PM_NONE && p.cur == 1);
    CHECK(pm_key(&p, 0x48, 0) == PM_NONE && p.cur == 4);
    CHECK(pm_key(&p, 0x147, 0) == PM_NONE && p.cur == 1);
    CHECK(pm_key(&p, 0x14F, 0) == PM_NONE && p.cur == 4);
    CHECK(pm_key(&p, 0x1C, 0) == 4);            /* Enter: Close */
    CHECK(pm_key(&p, 0x01, 0) == PM_CANCEL);

    /* Letters: the marked one chooses at once when only one item has it; Restore's R can't. */
    p.cur = 1;
    CHECK(pm_key(&p, 0x2D, 'x') == 2);
    CHECK(pm_key(&p, 0x2E, 'C') == 4);
    CHECK(pm_key(&p, 0x13, 'r') == PM_NONE && p.cur == 4);     /* (nothing else has R: nothing changes) */

    /* Presses: on an item, on the line, outside. */
    CHECK(pm_at(&p, 10, 3 + 17 + 5) == 1);
    CHECK(pm_at(&p, 10, 3 + 5) == -1);          /* Restore: can't */
    CHECK(pm_at(&p, 10, 3 + 3 * 17 + 3) == -1); /* the line */
    CHECK(pm_at(&p, 10, 3 + 3 * 17 + 7 + 5) == 4);
    CHECK(pm_at(&p, 200, 30) == -1 && pm_at(&p, 10, 500) == -1);

    /* A group's windows: two begin with N: the first goes there, the second time to the next; a wide text widens it. */
    pm_clear(&p);
    pm_add(&p, "Notepad - A.TXT", PMI_CHECK, 11);
    pm_add(&p, "Notepad - B.TXT with a long name that is wider", 0, 12);
    pm_add(&p, "Files", 0, 13);
    pm_first(&p);
    r = pm_key(&p, 0x31, 'n');
    CHECK(r == PM_NONE && p.cur == 1);
    r = pm_key(&p, 0x31, 'n');
    CHECK(r == PM_NONE && p.cur == 0);
    CHECK(pm_key(&p, 0x21, 'f') == 2);
    pm_layout(&p, width, 11, 100);
    CHECK(p.w == (int)strlen("Notepad - B.TXT with a long name that is wider") * 6 + 36 + 6);

    /* Full, and nothing that can be chosen. */
    pm_clear(&p);
    for (r = 0; r < PM_ITEMS; r++)
        pm_add(&p, "x", PMI_OFF, 0);
    CHECK(pm_add(&p, "y", 0, 0) == -1);
    CHECK(pm_first(&p) == -1 && pm_key(&p, 0x1C, 0) == PM_NONE && pm_key(&p, 0x50, 0) == PM_NONE && p.cur == -1);

    printf("popmenu_test: %d failures\n", fails);
    return fails != 0;
}
