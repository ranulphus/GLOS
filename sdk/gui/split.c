/* Splitters (toolkit 2, step C5; docs/v1/toolkit-2.md 7): two panes and a bar between them. The app makes the
 * panes in the split, the first and then the second; the bar is the split's own (it sits between them in the
 * list of children, so Tab reaches it between the panes).
 *
 * The bar is dragged by the mouse, or takes the keyboard: the arrows move it 8 pixels (with Ctrl, 1); Home folds
 * the first pane away and End the second, and the same key again unfolds it. GUI_ON_CHANGE goes to the split's
 * own function when the user has moved it, so the app can keep its place (gui_split_get).
 *
 * Which pane keeps its size as the window's changes: the first, when `at` is 0 or more (a folder tree beside a
 * list), or the second, when it is less (an output pane under an editor). Neither is made smaller than its least
 * (gui_split_min; 32 pixels unless said) while there is room for both. */
#include "guiint.h"

#define BAR 6                                   /* the bar's thickness */

struct split {
    struct gui_widget w;
    struct gui_widget *bar;
    int vertical;                               /* 1: one pane above the other (the bar lies across) */
    int at;                                     /* the first pane's size (>= 0), or minus the second's */
    int min1, min2;
    int fold;                                   /* 0, or 1 / 2: that pane folded away */
    int drag, drag_off;                         /* the bar held, and where in it */
    int nobar;                                  /* (dock.c: a side with no pane has no bar either) */
};

#define SP(w) ((struct split *)(w))

static void panes(struct split *s, struct gui_widget **a, struct gui_widget **b)
{
    struct gui_widget *c;
    *a = *b = NULL;
    for (c = s->w.first; c; c = c->next)
        if (c != s->bar) {
            if (!*a)
                *a = c;
            else if (!*b)
                *b = c;
        }
}

static int along(struct split *s) { return s->vertical ? s->w.r.h : s->w.r.w; }
static int barw(struct split *s) { return s->nobar ? 0 : BAR; }

/* The first pane's size now, as `at`, the least sizes and the folding say. */
static int first_size(struct split *s)
{
    int room = along(s) - barw(s), f;
    if (room < 0)
        room = 0;
    if (s->fold == 1)
        return 0;
    if (s->fold == 2)
        return room;
    f = s->at >= 0 ? s->at : room + s->at;
    if (f > room - s->min2)
        f = room - s->min2;
    if (f < s->min1)
        f = s->min1;
    if (f > room)
        f = room;
    return f < 0 ? 0 : f;
}

static void split_measure(struct gui_widget *w, int *pw, int *ph)
{
    struct split *s = SP(w);
    struct gui_widget *a, *b;
    int aw = 0, ah = 0, bw = 0, bh = 0;
    panes(s, &a, &b);
    if (a)
        gui_measure(a, &aw, &ah);
    if (b)
        gui_measure(b, &bw, &bh);
    if (s->vertical)
        *pw = aw > bw ? aw : bw, *ph = ah + BAR + bh;
    else
        *pw = aw + BAR + bw, *ph = ah > bh ? ah : bh;
}

static void split_layout(struct gui_widget *w)
{
    struct split *s = SP(w);
    struct gui_widget *a, *b;
    int f, rest;
    panes(s, &a, &b);
    if (a && s->bar->prev != a) {               /* (the bar between the panes, for Tab's order) */
        struct gui_widget *bar = s->bar;
        if (bar->prev)
            bar->prev->next = bar->next;
        else
            w->first = bar->next;
        if (bar->next)
            bar->next->prev = bar->prev;
        else
            w->last = bar->prev;
        bar->prev = a, bar->next = a->next;
        if (a->next)
            a->next->prev = bar;
        else
            w->last = bar;
        a->next = bar;
    }
    f = first_size(s);
    rest = along(s) - barw(s) - f;
    if (rest < 0)
        rest = 0;
    s->bar->flags = s->nobar ? s->bar->flags | GUI_HIDDEN : s->bar->flags & ~GUI_HIDDEN;
    if (s->vertical) {
        if (a)
            gui_place(a, w->r.x, w->r.y, w->r.w, f);
        gui_place(s->bar, w->r.x, w->r.y + f, w->r.w, barw(s));
        if (b)
            gui_place(b, w->r.x, w->r.y + f + barw(s), w->r.w, rest);
    } else {
        if (a)
            gui_place(a, w->r.x, w->r.y, f, w->r.h);
        gui_place(s->bar, w->r.x + f, w->r.y, barw(s), w->r.h);
        if (b)
            gui_place(b, w->r.x + f + barw(s), w->r.y, rest, w->r.h);
    }
    if (a)                                      /* (a folded pane is hidden: no keys go into it) */
        a->flags = f ? a->flags & ~GUI_HIDDEN : a->flags | GUI_HIDDEN;
    if (b)
        b->flags = rest ? b->flags & ~GUI_HIDDEN : b->flags | GUI_HIDDEN;
}

/* The first pane to be f pixels, kept as `at` says (the first's size, or the second's). */
static void move_to(struct split *s, int f, int by_user)
{
    int room = along(s) - barw(s);
    if (f < 0)
        f = 0;
    if (f > room)
        f = room;
    s->fold = 0;
    s->at = s->at >= 0 ? f : -(room - f);
    gui_place(&s->w, s->w.r.x, s->w.r.y, s->w.r.w, s->w.r.h);
    gui_damage(&s->w);
    if (by_user)
        gui_send(&s->w, GUI_ON_CHANGE);
}

static void bar_draw(struct gui_widget *w, struct gfx_surface *g)
{
    struct split *s = SP(w->parent);
    int cx = w->r.x + w->r.w / 2, cy = w->r.y + w->r.h / 2, i;
    gfx_fill(g, w->r.x, w->r.y, w->r.w, w->r.h, GUI_FACE);
    for (i = -1; i <= 1; i++)                   /* a grip: three dots across the middle */
        if (s->vertical)
            gfx_fill(g, cx + i * 4 - 1, cy - 1, 2, 2, GUI_DIM);
        else
            gfx_fill(g, cx - 1, cy + i * 4 - 1, 2, 2, GUI_DIM);
    if (gui_is_focus(w)) {
        struct gfx_rect f = w->r;
        gui_draw_focus(g, &f);
    }
}

static int bar_event(struct gui_widget *w, struct gui_ev *e)
{
    struct split *s = SP(w->parent);
    int f = first_size(s), pos, step;
    switch (e->type) {
    case GUI_EV_PRESS:
        if (e->button != 1)
            return 1;
        s->drag = 1;
        s->drag_off = s->vertical ? e->y - w->r.y : e->x - w->r.x;
        return 1;
    case GUI_EV_MOVE:
        if (!s->drag)
            return 0;
        pos = (s->vertical ? e->y - s->w.r.y : e->x - s->w.r.x) - s->drag_off;
        if (pos != f)
            move_to(s, pos, 0);
        return 1;
    case GUI_EV_RELEASE:
        if (s->drag) {
            s->drag = 0;
            gui_send(&s->w, GUI_ON_CHANGE);
        }
        return 1;
    case GUI_EV_FOCUS:
    case GUI_EV_BLUR:
        gui_damage(w);
        return 1;
    case GUI_EV_KEY:
        if (e->mods & GLOS_MOD_ALT)
            return 0;
        step = e->mods & GLOS_MOD_CTRL ? 1 : 8;
        if (e->key == (s->vertical ? GUI_K_UP : GUI_K_LEFT)) {
            move_to(s, f - step, 1);
            return 1;
        }
        if (e->key == (s->vertical ? GUI_K_DOWN : GUI_K_RIGHT)) {
            move_to(s, f + step, 1);
            return 1;
        }
        if (e->key == GUI_K_HOME || e->key == GUI_K_END) {     /* fold a pane away, or back */
            int which = e->key == GUI_K_HOME ? 1 : 2;
            s->fold = s->fold == which ? 0 : which;
            gui_place(&s->w, s->w.r.x, s->w.r.y, s->w.r.w, s->w.r.h);
            gui_relayout(s->w.win);             /* (a pane shown or hidden: the focus may move) */
            gui_damage(&s->w);
            gui_send(&s->w, GUI_ON_CHANGE);
            return 1;
        }
        return 0;
    default:
        return 0;
    }
}

static void bar_measure(struct gui_widget *w, int *pw, int *ph) { (void)w, *pw = BAR, *ph = BAR; }

static const struct gui_class bar_class = { "splitbar", GUI_C_FOCUS, bar_draw, bar_event, bar_measure, NULL, NULL };
static const struct gui_class split_class = { "split", 0, NULL, NULL, split_measure, split_layout, NULL };

struct gui_widget *gui_split(struct gui_widget *parent, int vertical, int at)
{
    struct split *s = (struct split *)gui_new(parent, &split_class, sizeof *s);
    s->vertical = vertical != 0;
    s->at = at;
    s->min1 = s->min2 = 32;
    s->bar = gui_new(&s->w, &bar_class, sizeof *s->bar);
    return &s->w;
}

int gui_split_get(struct gui_widget *w) { return SP(w)->at; }

void gui_split_set(struct gui_widget *w, int at)
{
    struct split *s = SP(w);
    s->at = at;
    s->fold = 0;
    gui_relayout(w->win);
    gui_damage(w);
}

void gui_split_min(struct gui_widget *w, int first, int second)
{
    struct split *s = SP(w);
    s->min1 = first < 0 ? 0 : first;
    s->min2 = second < 0 ? 0 : second;
    gui_relayout(w->win);
}

int gui_split_folded(struct gui_widget *w) { return SP(w)->fold; }

void gui_split_gone(struct gui_widget *w, int which)   /* (dock.c) a pane folded with its bar: nothing of it shows */
{
    gui_split_fold(w, which);
    SP(w)->nobar = which == 1 || which == 2;
}

void gui_split_fold(struct gui_widget *w, int which)
{
    struct split *s = SP(w);
    s->fold = which == 1 || which == 2 ? which : 0;
    s->nobar = 0;
    gui_relayout(w->win);
    gui_damage(w);
}
