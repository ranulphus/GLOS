/* rich_test.c - the rich-text view's find and show (make host-test; phase F, sdk/gui/rich.c): a document of
 * paragraphs, a closed details block and a picture; gui_richview_find forward and back, from what is chosen,
 * round the end once, into the closed block (opened), case, a wrong expression; gui_richview_show scrolls. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/gui.h>

#include "../../sdk/gui/guiint.h"

static int fails;

#define CHECK(c, ...) do { if (!(c)) { printf("rich_test: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static void chosen_is(struct gui_widget *v, const char *want, const char *step)
{
    char *t = gui_richview_text(v, 1);
    CHECK(t && !strcmp(t, want), "%s: chosen \"%s\", want \"%s\"", step, t ? t : "(null)", want);
    free(t);
}

int main(void)
{
    static const unsigned px[4] = { 0, 0, 0, 0 };
    struct gui_window *w = gui_window("t", 300, 120, 0);
    struct gui_widget *v = gui_richview(w->root);
    struct gui_rdoc *d = gui_rdoc_new();
    char line[64];
    int i, r;
    gui_place(v, 0, 0, 300, 120);
    gui_rdoc_block(d, GUI_RB_HEAD1, 0);
    gui_rdoc_text(d, "The Apple Tree", -1, 0, NULL);                    /* block 0 */
    for (i = 1; i <= 30; i++) {                                         /* blocks 1 .. 30 */
        gui_rdoc_block(d, GUI_RB_PARA, 0);
        snprintf(line, sizeof line, "Line %d of ", i);
        gui_rdoc_text(d, line, -1, 0, NULL);
        gui_rdoc_text(d, i == 12 ? "an apple and an apple" : "nothing", -1, GUI_RS_BOLD, NULL);
    }
    gui_rdoc_details(d, 0);                                             /* block 31, closed */
    gui_rdoc_text(d, "More", -1, 0, NULL);
    gui_rdoc_block(d, GUI_RB_PARA, 0);                                  /* block 32, inside it */
    gui_rdoc_text(d, "a hidden Apple", -1, 0, NULL);
    gui_rdoc_end_details(d);
    gui_rdoc_image(d, px, 2, 2, "apple");                               /* block 33: a picture, not searched */
    gui_richview_set(v, d);

    r = gui_richview_find(v, "apple", 0, 0);                            /* nothing chosen: from the start */
    CHECK(r == 1, "first: %d", r);
    chosen_is(v, "Apple", "first");
    r = gui_richview_find(v, "apple", 0, 0);
    CHECK(r == 1, "second: %d", r);
    chosen_is(v, "apple", "second");                                    /* (block 12's first) */
    r = gui_richview_find(v, "apple", 0, 0);
    chosen_is(v, "apple", "third");                                     /* (block 12's second) */
    r = gui_richview_find(v, "apple", 0, 0);                            /* into the closed block: opened */
    CHECK(r == 1, "hidden: %d", r);
    chosen_is(v, "Apple", "hidden");
    r = gui_richview_find(v, "apple", 0, 0);                            /* round the end: the heading's again */
    CHECK(r == 1, "round: %d", r);
    chosen_is(v, "Apple", "round");
    {
        char *all = gui_richview_text(v, 0);
        CHECK(all && strstr(all, "The Apple Tree"), "round: not the heading");
        free(all);
    }
    r = gui_richview_find(v, "apple", 0, 1);                            /* back from the heading: round to the end */
    CHECK(r == 1, "back round: %d", r);
    chosen_is(v, "Apple", "back round");
    r = gui_richview_find(v, "apple", 0, 1);
    chosen_is(v, "apple", "back");
    r = gui_richview_find(v, "Apple", GUI_FIND_CASE, 0);                /* case: after block 12, the hidden one */
    CHECK(r == 1, "case: %d", r);
    chosen_is(v, "Apple", "case");
    r = gui_richview_find(v, "pear", 0, 0);
    CHECK(r == 0, "none: %d", r);
    r = gui_richview_find(v, "a(", GUI_FIND_REGEX, 0);
    CHECK(r == -1, "wrong expression: %d", r);
    r = gui_richview_find(v, "Line 2[0-9]", GUI_FIND_REGEX, 0);
    CHECK(r == 1, "regex: %d", r);
    chosen_is(v, "Line 20", "regex");
    CHECK(gui_richview_show(v, 0) == 0 && gui_richview_show(v, 99) == -1, "show");
    if (!fails)
        printf("rich_test: ok\n");
    return fails != 0;
}
