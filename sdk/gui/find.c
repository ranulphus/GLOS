/* Find and Replace (toolkit 2, step C6; docs/v1/toolkit-2.md, Dialogs): a modeless dialog over its window. What to
 * find and what to put in its place are combo boxes whose history lasts while the app runs; Match case, Whole
 * words and Regular expressions are check boxes. The app is told what to do (Find next, Find previous, Replace,
 * Replace all) and searches its own text, best with gui_match (sdk/gui/regex.c), so that every app finds alike;
 * gui_find_say puts its answer ("Not found", "3 replaced") in the dialog.
 *
 * The dialog is made from a resource file's text (step C10): its words go through GUI_T as it is loaded, and a
 * translation can lay it out anew. Lines of the text that start with ? are Find and replace's alone, ! Find's. */
#include <stdio.h>
#include <stdlib.h>
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

enum { C_CLOSE = 9 };                          /* (the buttons' commands: GUI_FIND_NEXT ... and this) */

static const char dialog_text[] =
    "object Find: Dialog\n"
    "!  Title = \"Find\"\n"
    "?  Title = \"Find and replace\"\n"
    "  object Box: HBox\n"
    "    Gap = 12\n"
    "    Pad = 10\n"
    "    object Left: VBox\n"
    "      Pad = 0\n"
    "      object Fields: Form\n"
    "        object WhatRow: Row\n"
    "          Label = \"Find &what:\"\n"
    "          object What: Combo\n"
    "            Chars = 24\n"
    "          end\n"
    "        end\n"
    "?        object WithRow: Row\n"
    "?          Label = \"Re&place with:\"\n"
    "?          object With: Combo\n"
    "?            Chars = 24\n"
    "?          end\n"
    "?        end\n"
    "      end\n"
    "      object Checks: VBox\n"
    "        Gap = 2\n"
    "        Pad = 0\n"
    "        object Case: Check\n"
    "          Text = \"Match &case\"\n"
    "        end\n"
    "        object Words: Check\n"
    "          Text = \"Whole w&ords\"\n"
    "        end\n"
    "        object Regex: Check\n"
    "          Text = \"Regular e&xpressions\"\n"
    "        end\n"
    "      end\n"
    "      object Said: Label\n"
    "        Width = 260\n"
    "      end\n"
    "    end\n"
    "    object Buttons: VBox\n"
    "      Pad = 0\n"
    "      object Next: Button\n"
    "        Text = \"Find &next\"\n"
    "        Default = true\n"
    "        Command = 1\n"
    "      end\n"
    "      object Previous: Button\n"
    "        Text = \"Find pre&vious\"\n"
    "        Command = 2\n"
    "      end\n"
    "?      object Replace: Button\n"
    "?        Text = \"&Replace\"\n"
    "?        Command = 3\n"
    "?      end\n"
    "?      object All: Button\n"
    "?        Text = \"Replace &all\"\n"
    "?        Command = 4\n"
    "?      end\n"
    "      object Close: Button\n"
    "        Text = \"Close\"\n"
    "        Cancel = true\n"
    "        Command = 9\n"
    "      end\n"
    "    end\n"
    "  end\n"
    "end\n";

/* The dialog's text for Find (replace 0) or Find and replace: malloc'd. */
static char *text_for(int replace)
{
    char *out = malloc(sizeof dialog_text), *o = out;
    const char *p = dialog_text;
    if (!out)
        return NULL;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) + 1 : strlen(p);
        int keep = *p == '?' ? replace : *p == '!' ? !replace : 1;
        if (keep) {
            size_t skip = *p == '?' || *p == '!';
            memcpy(o, p + skip, n - skip);
            o += n - skip;
        }
        p += n;
    }
    *o = 0;
    return out;
}

/* The dialog's fields into f, the texts remembered, and the app told. */
static void act(struct gui_find *f, int what)
{
    int i;
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
        f->on_find(f, what);
}

static void what_on(struct gui_widget *w, int what)    /* Enter in what to find: Find next */
{
    if (what == GUI_ON_ACTIVATE && w->win)
        act(w->win->user, GUI_FIND_NEXT);
}

static void command(struct gui_window *w, int id)      /* its buttons */
{
    if (!w->user)
        return;
    if (id == C_CLOSE)
        gui_find_close(w->user);
    else
        act(w->user, id);
}

static void find_closed(struct gui_window *w)  /* its close box or Esc */
{
    if (w->user)
        gui_find_close(w->user);
}

void gui_find(struct gui_window *owner, struct gui_find *f)
{
    char *text, err[96];
    int i;
    if (f->w) {                                 /* (shown already: in front, the text to find chosen) */
        gui_show(f->w, 1);
        gui_focus(f->what_box);
        return;
    }
    if (!(text = text_for(f->replace)))
        return;
    f->w = gui_load_text(text, err, sizeof err);
    free(text);
    if (!f->w)
        return;
    f->w->owner = owner;
    f->w->user = f;
    f->w->on_close = find_closed;
    f->w->on_command = command;
    f->what_box = gui_named(f->w, "What");
    for (i = 0; i < nhist[0]; i++)
        gui_combo_add(f->what_box, hist[0][i]);
    gui_set_text(f->what_box, f->what);
    gui_on(f->what_box, what_on, GUI_FIND_NEXT);
    f->with_box = gui_named(f->w, "With");     /* (NULL: Find alone) */
    if (f->with_box) {
        for (i = 0; i < nhist[1]; i++)
            gui_combo_add(f->with_box, hist[1][i]);
        gui_set_text(f->with_box, f->with);
    }
    gui_set_checked(f->case_box = gui_named(f->w, "Case"), (f->flags & GUI_FIND_CASE) != 0);
    gui_set_checked(f->words_box = gui_named(f->w, "Words"), (f->flags & GUI_FIND_WORDS) != 0);
    gui_set_checked(f->regex_box = gui_named(f->w, "Regex"), (f->flags & GUI_FIND_REGEX) != 0);
    f->said = gui_named(f->w, "Said");
    gui_set_text(f->said, " ");
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
