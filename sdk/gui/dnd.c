/* Drag and drop (toolkit 2, step C7; docs/v1/toolkit-2.md 9, on API 2.0's DND and CLIP).
 *
 * A widget may be a drag's source (gui_drag_source: the app fills a writing transaction with what is chosen, in
 * the formats it offers) and a drop's target (gui_drop_target: the formats and effects it takes, and the app's
 * function for a drop). A window's own DND target is all of its widgets' formats; GLOS tells it of a drag over it,
 * and the toolkit tells the widget under the pointer (GUI_EV_DRAG, GUI_EV_DRAG_LEAVE, GUI_EV_DROP): a list shows a
 * line where a drop would go and gives the drop that place. The source hears when the drag is over and what was
 * done (a move: it takes what was moved away). Lists drag the rows chosen in them. */
#include <stdlib.h>
#include <string.h>

#include "guiint.h"

struct dnd_spec {
    unsigned drag_effects;
    int (*offer)(struct gui_widget *w, glos_handle t);
    void (*done)(struct gui_widget *w, unsigned effect);
    unsigned drop_effects, formats[GLOS_CLIP_ITEMS];
    int n;
    void (*drop)(struct gui_widget *w, int where, glos_handle data, unsigned effect);
};

static struct {
    struct gui_widget *src;                     /* the widget a drag of this app's came from (while it lasts) */
    struct gui_widget *over;                    /* the target widget the pointer is over */
} D;

static int dnd_event(const struct glos_ev *ev);

static struct dnd_spec *spec(struct gui_widget *w)
{
    if (!w->dnd && (w->dnd = calloc(1, sizeof(struct dnd_spec))) != NULL)
        gui_dnd_hook = dnd_event;
    return w->dnd;
}

/* The window's DND target: its widgets' formats and effects, together. */
static void union_of(struct gui_widget *w, unsigned *effects, unsigned *formats, unsigned *n)
{
    struct gui_widget *c;
    const struct dnd_spec *s = w->dnd;
    int i;
    unsigned k;
    if (s && s->drop) {
        *effects |= s->drop_effects;
        for (i = 0; i < s->n; i++) {
            for (k = 0; k < *n && formats[k] != s->formats[i]; k++)
                ;
            if (k == *n && *n < GLOS_CLIP_ITEMS)
                formats[(*n)++] = s->formats[i];
        }
    }
    for (c = w->first; c; c = c->next)
        union_of(c, effects, formats, n);
}

void gui_dnd_register(struct gui_window *win)  /* (core.c, once the window is made; and when a target is set) */
{
    unsigned effects = 0, formats[GLOS_CLIP_ITEMS], n = 0;
    if (!win || !win->id || !win->root)
        return;
    union_of(win->root, &effects, formats, &n);
    glos_dnd_target(win->id, n ? effects : 0, formats, n ? n : 1);
}

void gui_drag_source(struct gui_widget *w, unsigned effects, int (*offer)(struct gui_widget *w, glos_handle t),
                     void (*done)(struct gui_widget *w, unsigned effect))
{
    struct dnd_spec *s = spec(w);
    if (s)
        s->drag_effects = effects, s->offer = offer, s->done = done;
}

void gui_drop_target(struct gui_widget *w, const unsigned *formats, int n, unsigned effects,
                     void (*drop)(struct gui_widget *w, int where, glos_handle data, unsigned effect))
{
    struct dnd_spec *s = spec(w);
    int i;
    if (!s)
        return;
    s->n = n < GLOS_CLIP_ITEMS ? n : GLOS_CLIP_ITEMS;
    for (i = 0; i < s->n; i++)
        s->formats[i] = formats[i];
    s->drop_effects = effects, s->drop = drop;
    gui_dnd_register(w->win);
}

struct gui_widget *gui_drag_from(void) { return D.src; }

/* (a class's: the user has dragged what is chosen in w) the drag begun: 1, or 0 (w offers nothing). */
int gui_drag_start(struct gui_widget *w)
{
    struct dnd_spec *s = w->dnd;
    glos_handle t;
    if (!s || !s->offer || !s->drag_effects || !w->win || !w->win->id || !(t = gui_clip_start()))
        return 0;
    if (s->offer(w, t) != 0) {                  /* (nothing to offer: the transaction given up, not committed) */
        glos_set(t, GLOS_PROP_CLIP_COMMIT, 0);
        glos_close(t);
        return 0;
    }
    if (glos_dnd(w->win->id, t, s->drag_effects, NULL, 0, 0) != 0)
        return 0;
    D.src = w;
    if (w->win->capture == w)                   /* (the pointer is the drag's until it ends) */
        w->win->capture = NULL;
    return 1;
}

/* A dropped format's data (malloc'd, a NUL after it), or NULL. */
void *gui_drop_read(glos_handle data, unsigned format, size_t *n)
{
    unsigned total = 0, got;
    unsigned char *d;
    int r = glos_clip_read(data, format, NULL, 0, 0, &total);
    if (r < 0 || !(d = malloc((size_t)total + 1)))
        return NULL;
    for (got = 0; got < total; got += (unsigned)r)
        if ((r = glos_clip_read(data, format, d + got, total - got, got, NULL)) <= 0)
            break;
    d[got] = 0;
    if (n)
        *n = got;
    return d;
}

/* The target widget at x, y: the one there that takes drops, or one of its parents that does. */
static struct gui_widget *target_at(struct gui_window *w, int x, int y)
{
    struct gui_widget *t = gui_hit(w->root, x, y);
    for (; t; t = t->parent)
        if (t->dnd && ((struct dnd_spec *)t->dnd)->drop)
            return t;
    return NULL;
}

static void leave_over(void)
{
    if (D.over) {
        struct gui_ev e;
        memset(&e, 0, sizeof e);
        e.type = GUI_EV_DRAG_LEAVE;
        if (D.over->cls->event)
            D.over->cls->event(D.over, &e);
        D.over = NULL;
    }
}

static unsigned choose(const struct dnd_spec *s, struct gui_widget *t, unsigned allowed)
{
    unsigned may = s->drop_effects & allowed;
    if (D.src == t && may & GLOS_DND_MOVE)      /* (within one widget: a move, as a list's rows reordered) */
        return GLOS_DND_MOVE;
    return may & GLOS_DND_COPY ? GLOS_DND_COPY : may & GLOS_DND_MOVE ? GLOS_DND_MOVE : may & GLOS_DND_LINK;
}

static int dnd_event(const struct glos_ev *ev)
{
    const struct glos_ev_dnd *d = (const struct glos_ev_dnd *)ev;
    struct gui_window *w;
    struct gui_widget *t;
    struct gui_ev e;
    if (ev->kind != GLOS_EV_DND_ENTER && ev->kind != GLOS_EV_DND_OVER && ev->kind != GLOS_EV_DND_LEAVE
        && ev->kind != GLOS_EV_DND_DROP && ev->kind != GLOS_EV_DND_DONE)
        return 0;
    w = gui_window_of(ev->handle);
    if (ev->kind == GLOS_EV_DND_DONE) {         /* (to the source) the drag is over */
        struct gui_widget *src = D.src;
        D.src = NULL;
        if (src && src->dnd && ((struct dnd_spec *)src->dnd)->done)
            ((struct dnd_spec *)src->dnd)->done(src, d->effects);
        return 1;
    }
    if (ev->kind == GLOS_EV_DND_LEAVE || !w) {
        leave_over();
        if (ev->kind == GLOS_EV_DND_DROP)
            glos_close(d->data);
        return 1;
    }
    t = target_at(w, d->x, d->y);
    if (t != D.over)
        leave_over();
    memset(&e, 0, sizeof e);
    e.x = d->x, e.y = d->y, e.key = d->effects;
    e.ch = (unsigned)-1;                        /* (the place a drop goes: the widget's to say; -1: the whole of it) */
    if (ev->kind == GLOS_EV_DND_DROP) {
        if (t) {
            e.type = GUI_EV_DROP;
            if (t->cls->event)
                t->cls->event(t, &e);
            D.over = NULL;
            ((struct dnd_spec *)t->dnd)->drop(t, (int)e.ch, d->data, d->effects);
        } else {
            glos_close(d->data);
        }
        return 1;
    }
    D.over = t;                                 /* ENTER, OVER */
    if (t) {
        e.type = GUI_EV_DRAG;
        if (t->cls->event)
            t->cls->event(t, &e);
    }
    glos_dnd_answer(w->id, t ? choose(t->dnd, t, d->effects) : 0);
    return 1;
}
