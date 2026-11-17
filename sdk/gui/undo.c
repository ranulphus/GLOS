/* Undo (toolkit 2, step C7; docs/v1/toolkit-2.md 4): an app's history of changes, each an object that can be undone
 * and redone, and the history list that shows it (Paint's History panel).
 *
 * A step is a name ("Fill", "Typing"), the app's undo and redo functions and its data. Steps pushed with the same
 * merge key within a second of each other are one step (a burst of typing undoes at once): the app's merge
 * function folds the newer step's data into the older one. The history is held to so many bytes of the steps'
 * data (as the app counts them): the oldest steps go first. A step pushed after some were undone drops those.
 *
 * gui_undo_commands keeps the window's Undo and Redo commands (gui_command) named ("&Undo Fill") and enabled as
 * the history changes; the history list (gui_undo_list) shows every step, the ones undone greyed, and a step chosen
 * there undoes or redoes to it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guiint.h"

struct step {
    char name[40];
    void (*undo)(void *data), (*redo)(void *data), (*drop)(void *data);
    int (*merge)(void *into, void *data);       /* 1: data folded into `into` (then dropped) */
    void *data;
    unsigned key, at, bytes;
};

struct gui_undo {
    struct step *s;
    int n, room;                                /* steps; s[0 .. done-1] are done, s[done .. n-1] undone */
    int done;
    unsigned limit, bytes;
    struct gui_window *win;                     /* gui_undo_commands' */
    int undo_id, redo_id;
    struct gui_widget *list;                    /* the history list showing it, if any */
    void (*watch)(struct gui_undo *u, void *ctx);       /* the app's, after every change */
    void *watch_ctx;
};

void gui_undo_watch(struct gui_undo *u, void (*fn)(struct gui_undo *u, void *ctx), void *ctx)
{
    u->watch = fn, u->watch_ctx = ctx;
}

struct gui_undo *gui_undo_new(unsigned limit_bytes)
{
    struct gui_undo *u = calloc(1, sizeof *u);
    if (u)
        u->limit = limit_bytes ? limit_bytes : 256u * 1024;
    return u;
}

static void drop_step(struct step *s)
{
    if (s->drop)
        s->drop(s->data);
}

static void told(struct gui_undo *u);

void gui_undo_free(struct gui_undo *u)
{
    int i;
    if (!u)
        return;
    for (i = 0; i < u->n; i++)
        drop_step(&u->s[i]);
    free(u->s);
    free(u);
}

void gui_undo_push(struct gui_undo *u, const char *name, void (*undo)(void *), void (*redo)(void *), void *data,
                   unsigned bytes, void (*drop)(void *), int (*merge)(void *into, void *data), unsigned key)
{
    struct step *s;
    unsigned now = gui_now();
    while (u->n > u->done)                      /* (the steps undone go: this change is after the ones done) */
        drop_step(&u->s[--u->n]);
    if (key && merge && u->done && u->s[u->done - 1].key == key && now - u->s[u->done - 1].at <= 1000
        && merge(u->s[u->done - 1].data, data)) {
        s = &u->s[u->done - 1];
        s->at = now;
        u->bytes += bytes;
        s->bytes += bytes;
        if (drop)
            drop(data);
        told(u);
        return;
    }
    if (u->n == u->room) {
        int room = u->room * 2 + 16;
        struct step *ns = realloc(u->s, (size_t)room * sizeof *ns);
        if (!ns) {                              /* (no memory: the change stays, its undo doesn't) */
            if (drop)
                drop(data);
            return;
        }
        u->s = ns, u->room = room;
    }
    s = &u->s[u->n++];
    memset(s, 0, sizeof *s);
    snprintf(s->name, sizeof s->name, "%s", name ? name : "");
    s->undo = undo, s->redo = redo, s->drop = drop, s->merge = merge, s->data = data;
    s->key = key, s->at = now, s->bytes = bytes;
    u->done = u->n;
    u->bytes += bytes;
    while (u->bytes > u->limit && u->n > 1) {   /* (held to its bytes: the oldest go) */
        u->bytes -= u->s[0].bytes;
        drop_step(&u->s[0]);
        memmove(u->s, u->s + 1, (size_t)(u->n - 1) * sizeof *u->s);
        u->n--, u->done--;
    }
    told(u);
}

int gui_undo(struct gui_undo *u)
{
    struct step *s;
    if (!u || !u->done)
        return 0;
    s = &u->s[--u->done];
    s->key = 0;                                 /* (an undone step merges with nothing after it is redone) */
    if (s->undo)
        s->undo(s->data);
    told(u);
    return 1;
}

int gui_redo(struct gui_undo *u)
{
    struct step *s;
    if (!u || u->done == u->n)
        return 0;
    s = &u->s[u->done++];
    if (s->redo)
        s->redo(s->data);
    told(u);
    return 1;
}

void gui_undo_to(struct gui_undo *u, int done)  /* undone or redone until `done` steps are done */
{
    while (u->done > done && gui_undo(u))
        ;
    while (u->done < done && gui_redo(u))
        ;
}

const char *gui_undo_name(struct gui_undo *u, int redo)        /* the step Undo (or Redo) would act on, or NULL */
{
    if (!u)
        return NULL;
    if (redo)
        return u->done < u->n ? u->s[u->done].name : NULL;
    return u->done ? u->s[u->done - 1].name : NULL;
}

int gui_undo_steps(struct gui_undo *u, int *done)
{
    if (done)
        *done = u ? u->done : 0;
    return u ? u->n : 0;
}

const char *gui_undo_step(struct gui_undo *u, int i) { return u && i >= 0 && i < u->n ? u->s[i].name : NULL; }

void gui_undo_clear(struct gui_undo *u)        /* (a file saved or loaded: a new start) */
{
    while (u->n)
        drop_step(&u->s[--u->n]);
    u->done = 0, u->bytes = 0;
    told(u);
}

/* ---- the window's commands */
static void told(struct gui_undo *u)
{
    char text[64];
    const char *n;
    if (u->win && u->undo_id) {
        n = gui_undo_name(u, 0);
        snprintf(text, sizeof text, n ? GUI_T("&Undo %s") : GUI_T("&Undo"), n ? n : "");
        gui_command(u->win, u->undo_id, text, "Ctrl+Z", gui_glyph("undo"), NULL);
        gui_command_enable(u->win, u->undo_id, n != NULL);
    }
    if (u->win && u->redo_id) {
        n = gui_undo_name(u, 1);
        snprintf(text, sizeof text, n ? GUI_T("&Redo %s") : GUI_T("&Redo"), n ? n : "");
        gui_command(u->win, u->redo_id, text, "Ctrl+Y", gui_glyph("redo"), NULL);
        gui_command_enable(u->win, u->redo_id, n != NULL);
    }
    if (u->list) {
        gui_vlist_changed(u->list);
        gui_vlist_set(u->list, u->done);        /* (the row of where the history is: 0 is its start) */
    }
    if (u->watch)
        u->watch(u, u->watch_ctx);
}

void gui_undo_commands(struct gui_undo *u, struct gui_window *w, int undo_id, int redo_id)
{
    u->win = w, u->undo_id = undo_id, u->redo_id = redo_id;
    told(u);
}

/* ---- the history list: "(the start)" and a row a step, the undone greyed; a row chosen: the history to it */
static char row_text[64];                      /* (a step's name, 40 bytes, and " (undone)") */

static int h_count(void *ctx) { return ((struct gui_undo *)ctx)->n + 1; }

static void h_row(void *ctx, int row, struct gui_vrow *out)
{
    struct gui_undo *u = ctx;
    if (row == 0)
        snprintf(row_text, sizeof row_text, GUI_T("(the start)"));
    else
        snprintf(row_text, sizeof row_text, "%s%s", u->s[row - 1].name, row > u->done ? " (undone)" : "");
    out->cell[0] = row_text;
    out->icon = row == 0 ? gui_glyph("home") : row > u->done ? gui_glyph("undo") : gui_glyph("check");
    out->id = (unsigned long)row + 1;
}

static int h_find(void *ctx, unsigned long id)
{
    return (int)id - 1 <= ((struct gui_undo *)ctx)->n ? (int)id - 1 : -1;
}

static void h_on(struct gui_widget *w, int what)
{
    struct gui_undo *u = w->user;
    int r = gui_vlist_get(w);
    if ((what == GUI_ON_SELECT || what == GUI_ON_ACTIVATE) && u && r >= 0 && r != u->done)
        gui_undo_to(u, r);
}

struct gui_widget *gui_undo_list(struct gui_widget *parent, struct gui_undo *u)
{
    static struct gui_model model;
    struct gui_widget *l;
    model.count = h_count, model.row = h_row, model.find = h_find, model.match = NULL, model.ctx = u;
    l = gui_vlist(parent, 1, NULL, NULL, &model);
    l->user = u;
    gui_on(l, h_on, 0);
    u->list = l;
    told(u);
    return l;
}
