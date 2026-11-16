/* Find and Replace (toolkit 2, step C6; docs/v1/toolkit-2.md, Dialogs): a modeless dialog over its window. What to
 * find and what to put in its place are combo boxes whose history lasts while the app runs; Match case, Whole
 * words and Regular expressions are check boxes. The app is told what to do (Find next, Find previous, Replace,
 * Replace all) and searches its own text, best with gui_match (sdk/gui/regex.c), so that every app finds alike;
 * gui_find_say puts its answer ("Not found", "3 replaced") in the dialog. */
#include <stdio.h>
#include <string.h>

#include "guiint.h"

#define HISTORY 10

static char hist[2][HISTORY][128];              /* the texts found and put, newest first (the app's whole run) */
static int nhist[2];

static void remember(int which, const char *text)
{
    int i, n = nhist[which];
    if (!text[0])
        return;
    for (i = 0; i < n && strcmp(hist[which][i], text); i++)
        ;
    if (i == n && n < HISTORY)
        n++;
    if (i == HISTORY)
        i = HISTORY - 1;
    memmove(hist[which][1], hist[which][0], (size_t)i * sizeof hist[which][0]);
    snprintf(hist[which][0], sizeof hist[which][0], "%s", text);
    nhist[which] = n;
}

static struct gui_find *of(struct gui_widget *w) { return w->win ? w->win->user : NULL; }

/* The dialog's fields into f, the texts remembered, and the app told. */
static void act(struct gui_widget *w, int what)
{
    struct gui_find *f = of(w);
    int i;
    (void)what;
    if (!f)
        return;
    snprintf(f->what, sizeof f->what, "%s", gui_get_text(f->what_box));
    if (f->with_box)
        snprintf(f->with, sizeof f->with, "%s", gui_get_text(f->with_box));
    f->flags = (gui_checked(f->case_box) ? GUI_FIND_CASE : 0) | (gui_checked(f->words_box) ? GUI_FIND_WORDS : 0)
               | (gui_checked(f->regex_box) ? GUI_FIND_REGEX : 0);
    remember(0, f->what);
    gui_combo_clear(f->what_box);
    for (i = 0; i < nhist[0]; i++)
        gui_combo_add(f->what_box, hist[0][i]);
    if (f->with_box) {
        remember(1, f->with);
        gui_combo_clear(f->with_box);
        for (i = 0; i < nhist[1]; i++)
            gui_combo_add(f->with_box, hist[1][i]);
    }
    gui_find_say(f, "");
    if (f->what[0] && f->on_find)
        f->on_find(f, w->id);
}

static void what_on(struct gui_widget *w, int what)    /* Enter in what to find: Find next */
{
    if (what == GUI_ON_ACTIVATE) {
        w->id = GUI_FIND_NEXT;
        act(w, 0);
    }
}

static void close_click(struct gui_widget *w, int what)
{
    (void)what;
    if (of(w))
        gui_find_close(of(w));
}

static void find_closed(struct gui_window *w)  /* its close box or Esc */
{
    if (w->user)
        gui_find_close(w->user);
}

void gui_find(struct gui_window *owner, struct gui_find *f)
{
    struct gui_widget *box, *form, *row, *checks, *buttons, *b;
    int i;
    if (f->w) {                                 /* (shown already: in front, the text to find chosen) */
        gui_show(f->w, 1);
        gui_focus(f->what_box);
        return;
    }
    f->w = gui_window(f->replace ? "Find and replace" : "Find", 0, 0, GUI_W_DIALOG);
    f->w->owner = owner;
    f->w->user = f;
    f->w->on_close = find_closed;
    box = gui_hbox(f->w->root, 12, 10);
    form = gui_vbox(box, GUI_GAP, 0);
    row = gui_form(form);
    f->what_box = gui_combo(gui_row(row, "Find &what:", NULL), 24);
    for (i = 0; i < nhist[0]; i++)
        gui_combo_add(f->what_box, hist[0][i]);
    gui_set_text(f->what_box, f->what);
    gui_on(f->what_box, what_on, GUI_FIND_NEXT);
    f->with_box = NULL;
    if (f->replace) {
        f->with_box = gui_combo(gui_row(row, "Re&place with:", NULL), 24);
        for (i = 0; i < nhist[1]; i++)
            gui_combo_add(f->with_box, hist[1][i]);
        gui_set_text(f->with_box, f->with);
    }
    checks = gui_vbox(form, 2, 0);
    f->case_box = gui_check(checks, "Match &case", (f->flags & GUI_FIND_CASE) != 0);
    f->words_box = gui_check(checks, "Whole w&ords", (f->flags & GUI_FIND_WORDS) != 0);
    f->regex_box = gui_check(checks, "Regular e&xpressions", (f->flags & GUI_FIND_REGEX) != 0);
    f->said = gui_label(form, " ");
    gui_size(f->said, 260, 0);
    buttons = gui_vbox(box, GUI_GAP, 0);
    (b = gui_button(buttons, "Find &next", act, GUI_FIND_NEXT))->flags |= GUI_DEFAULT;
    gui_button(buttons, "Find pre&vious", act, GUI_FIND_PREV);
    if (f->replace) {
        gui_button(buttons, "&Replace", act, GUI_FIND_REPLACE);
        gui_button(buttons, "Replace &all", act, GUI_FIND_ALL);
    }
    gui_button(buttons, "Close", close_click, 0)->flags |= GUI_CANCEL;
    f->w->focus = f->what_box;
    gui_show(f->w, 1);
}

void gui_find_say(struct gui_find *f, const char *text)
{
    if (f->w && f->said)
        gui_set_text(f->said, text[0] ? text : " ");
}

void gui_find_close(struct gui_find *f)
{
    struct gui_window *w = f->w;
    if (!w)
        return;
    f->w = NULL;
    f->what_box = f->with_box = f->case_box = f->words_box = f->regex_box = f->said = NULL;
    w->user = NULL;
    gui_close_later(w);
    if (w->owner && w->owner->id)
        glos_focus(w->owner->id);
}
