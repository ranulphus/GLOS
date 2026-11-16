/* Docking panes (toolkit 2, step C5; docs/v1/toolkit-2.md 7): a window's main area with the app's content in the
 * middle and panes at its left, right and bottom (Paint's tools and layers, the IDE's project and output panes),
 * or floating in tool windows of their own.
 *
 * Made of the toolkit's own parts: three splitters (the left side | the rest; the middle | the right side; the
 * centre over the bottom side), and at each side a row of tabs whose pages are the panes there (one pane is a
 * tab of its own). A side with no pane shown is folded away. A pane's title in its side's tabs has a close box
 * (it hides the pane) and is dragged along to reorder; its context menu docks the pane at another side, floats it
 * or hides it. The View menu's checks are the app's: gui_pane_show and gui_pane_shown.
 *
 * Hidden panes keep their widgets (and so what is in them) on a shelf of the dock's, out of sight. The layout is
 * one line of text (gui_dock_save, gui_dock_load) for the app's settings: the splitters' places, then each pane in
 * its side's order, where it is, whether it shows, and a floating one's place. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guiint.h"

#define PANES 16
#define FLOAT_W 220
#define FLOAT_H 160

struct pane {
    int id;                                     /* the app's (0: no pane) */
    struct gui_widget *page;                    /* its vbox: in a side's tabs, a tool window's root, or the shelf */
    int side;                                   /* GUI_DOCK_ */
    int shown;
    struct gui_window *fw;                      /* floating: its tool window */
    int fx, fy;                                 /* floating: its frame's place (-1: GLOS chooses) */
    int fwidth, fheight;
    int frame_dx, frame_dy;                     /* the client area's corner less the frame's, once known */
};

struct dock {
    struct gui_widget w;
    struct gui_widget *split[3];                /* the left side's, the right side's, the bottom side's */
    struct gui_widget *tabs[3];                 /* GUI_DOCK_LEFT, _RIGHT, _BOTTOM */
    struct gui_widget *centre, *shelf;
    struct pane p[PANES];
    struct gui_menu *menu;                      /* a side's context menu */
    int menu_side;
};

enum { M_LEFT = 1, M_RIGHT, M_BOTTOM, M_FLOAT, M_HIDE };

#define DK(w) ((struct dock *)(w))

static void dock_measure(struct gui_widget *w, int *pw, int *ph)
{
    gui_measure(DK(w)->split[0], pw, ph);
}

static void dock_layout(struct gui_widget *w)
{
    gui_place(DK(w)->split[0], w->r.x, w->r.y, w->r.w, w->r.h);
}

static void dock_destroy(struct gui_widget *w)
{
    struct dock *d = DK(w);
    int i;
    for (i = 0; i < PANES; i++)                 /* (floating panes' windows: theirs go with them) */
        if (d->p[i].id && d->p[i].fw) {
            struct gui_window *fw = d->p[i].fw;
            d->p[i].fw = NULL;
            fw->user = NULL;
            gui_close(fw);
        }
    if (d->menu)
        gui_menu_free(d->menu);
}

static const struct gui_class dock_class = { "dock", 0, NULL, NULL, dock_measure, dock_layout, dock_destroy };

static struct pane *by_id(struct dock *d, int id)
{
    int i;
    for (i = 0; i < PANES; i++)
        if (d->p[i].id && d->p[i].id == id)
            return &d->p[i];
    return NULL;
}

static struct pane *by_page(struct dock *d, struct gui_widget *page)
{
    int i;
    for (i = 0; i < PANES; i++)
        if (d->p[i].id && d->p[i].page == page)
            return &d->p[i];
    return NULL;
}

static int page_index(struct gui_widget *tabs, struct gui_widget *page)
{
    int i;
    struct gui_widget *c;
    for (i = 0; (c = gui_tabs_page(tabs, i)) != NULL; i++)
        if (c == page)
            return i;
    return -1;
}

/* Each side folded away when no pane shows there, unfolded when one does. */
static void refold(struct dock *d)
{
    static const int fold_of[3] = { 1, 2, 2 };  /* (the left side is its split's first pane; the others the second) */
    int s;
    for (s = 0; s < 3; s++) {
        int has = gui_tabs_page(d->tabs[s], 0) != NULL, now = gui_split_folded(d->split[s]);
        if (!has && now != fold_of[s])
            gui_split_gone(d->split[s], fold_of[s]);   /* (an empty side: not even its bar) */
        else if (has && now == fold_of[s])
            gui_split_gone(d->split[s], 0);
        if (has)
            gui_tabs_fix(d->tabs[s]);
    }
}

static void float_close(struct gui_window *fw);

/* The pane put where its side and shown say. */
static void place_pane(struct dock *d, struct pane *p)
{
    struct gui_widget *from = p->page->parent;
    if (p->fw && (!p->shown || p->side != GUI_DOCK_FLOAT)) {    /* (leaving its tool window: the place kept) */
        struct gui_window *fw = p->fw;
        int ox, oy;
        if (gui_origin(fw, &ox, &oy) == 0)
            p->fx = ox - p->frame_dx, p->fy = oy - p->frame_dy;
        p->fwidth = fw->w, p->fheight = fw->h;
        gui_adopt(p->page, d->shelf);
        p->fw = NULL;
        fw->user = NULL;
        gui_close(fw);
    }
    if (!p->shown) {
        gui_adopt(p->page, d->shelf);
    } else if (p->side == GUI_DOCK_FLOAT) {
        if (!p->fw) {
            struct gui_window *fw = p->fx >= 0 ? gui_window_at(gui_get_text(p->page), p->fx, p->fy,
                                                                p->fwidth, p->fheight, GUI_W_TOOL)
                                               : gui_window(gui_get_text(p->page), p->fwidth, p->fheight, GUI_W_TOOL);
            int ox, oy;
            fw->owner = d->w.win;
            fw->user = d;
            fw->on_close = float_close;
            gui_adopt(p->page, fw->root);
            p->page->flags &= ~GUI_HIDDEN;
            p->page->grow = 1;
            p->fw = fw;
            gui_show(fw, 1);
            if (p->fx >= 0 && gui_origin(fw, &ox, &oy) == 0)
                p->frame_dx = ox - p->fx, p->frame_dy = oy - p->fy;
        }
    } else {
        struct gui_widget *t = d->tabs[p->side];
        if (p->page->parent != t)
            gui_adopt(p->page, t);
        gui_tabs_set(t, page_index(t, p->page));
    }
    if (from && from != p->page->parent && from->cls == &gui_tabs_class)
        gui_tabs_fix(from);
    refold(d);
    gui_damage(&d->w);
}

static void changed(struct dock *d) { gui_send(&d->w, GUI_ON_CHANGE); }

static void float_close(struct gui_window *fw)          /* a floating pane's window closed: the pane hidden */
{
    struct dock *d = fw->user;
    int i;
    if (!d)
        return;
    for (i = 0; i < PANES; i++)
        if (d->p[i].id && d->p[i].fw == fw) {
            d->p[i].shown = 0;
            place_pane(d, &d->p[i]);
            changed(d);
            return;
        }
}

static struct pane *side_chosen(struct dock *d, int side)
{
    struct gui_widget *t = d->tabs[side];
    return by_page(d, gui_tabs_page(t, gui_tabs_get(t)));
}

static void menu_pick(struct gui_widget *w, int id)    /* the side's context menu, for its chosen pane */
{
    struct dock *d = DK(w);
    struct pane *p = side_chosen(d, d->menu_side);
    if (!p)
        return;
    if (id == M_HIDE)
        p->shown = 0;
    else
        p->side = id == M_LEFT ? GUI_DOCK_LEFT : id == M_RIGHT ? GUI_DOCK_RIGHT : id == M_BOTTOM ? GUI_DOCK_BOTTOM
                                                                                   : GUI_DOCK_FLOAT;
    place_pane(d, p);
    changed(d);
}

static void side_on(struct gui_widget *t, int what)
{
    struct dock *d = t->user;
    int side = t->id;
    struct pane *p;
    switch (what) {
    case GUI_ON_CLOSE:                          /* a pane's close box: hidden (the View menu shows it again) */
        if ((p = by_page(d, gui_tabs_page(t, gui_tabs_closing(t)))) != NULL) {
            p->shown = 0;
            place_pane(d, p);
            changed(d);
        }
        break;
    case GUI_ON_MOVE:
        changed(d);
        break;
    case GUI_ON_CONTEXT:                        /* (the menu acts on this side's chosen pane) */
        d->menu_side = side;
        gui_menu_enable(d->menu, M_LEFT, side != GUI_DOCK_LEFT);
        gui_menu_enable(d->menu, M_RIGHT, side != GUI_DOCK_RIGHT);
        gui_menu_enable(d->menu, M_BOTTOM, side != GUI_DOCK_BOTTOM);
        break;
    }
}

static void split_on(struct gui_widget *w, int what)
{
    if (what == GUI_ON_CHANGE)
        changed(w->user);
}

static struct gui_widget *side_tabs(struct dock *d, struct gui_widget *parent, int side)
{
    struct gui_widget *t = gui_tabs(parent);
    gui_tabs_style(t, GUI_TABS_SCROLL);
    gui_tabs_flags(t, GUI_TABS_CLOSE | GUI_TABS_MOVE);
    gui_on(t, side_on, side);
    t->user = d;
    gui_context(t, d->menu);
    return t;
}

struct gui_widget *gui_dock(struct gui_widget *parent)
{
    struct dock *d = (struct dock *)gui_new(parent, &dock_class, sizeof *d);
    int s;
    d->menu = gui_menu_new();
    gui_menu_item(d->menu, "Dock at the &left", M_LEFT, NULL);
    gui_menu_item(d->menu, "Dock at the &right", M_RIGHT, NULL);
    gui_menu_item(d->menu, "Dock at the &bottom", M_BOTTOM, NULL);
    gui_menu_item(d->menu, "&Float", M_FLOAT, NULL);
    gui_menu_sep(d->menu);
    gui_menu_item(d->menu, "&Hide", M_HIDE, NULL);
    gui_menu_to(d->menu, &d->w, menu_pick);
    d->split[0] = gui_split(&d->w, 0, 160);                    /* left | the rest */
    d->tabs[GUI_DOCK_LEFT] = side_tabs(d, d->split[0], GUI_DOCK_LEFT);
    d->split[1] = gui_split(d->split[0], 0, -160);             /* the middle | right */
    d->split[2] = gui_split(d->split[1], 1, -110);             /* the centre over the bottom */
    d->centre = gui_vbox(d->split[2], 0, 0);
    d->tabs[GUI_DOCK_BOTTOM] = side_tabs(d, d->split[2], GUI_DOCK_BOTTOM);
    d->tabs[GUI_DOCK_RIGHT] = side_tabs(d, d->split[1], GUI_DOCK_RIGHT);
    for (s = 0; s < 3; s++) {                   /* (a side's bar moved: the layout changed) */
        gui_on(d->split[s], split_on, s);
        d->split[s]->user = d;
    }
    d->shelf = gui_vbox(&d->w, 0, 0);
    d->shelf->flags |= GUI_HIDDEN;
    refold(d);
    return &d->w;
}

struct gui_widget *gui_dock_centre(struct gui_widget *dock) { return DK(dock)->centre; }

struct gui_widget *gui_pane(struct gui_widget *dock, int id, const char *title, int side)
{
    struct dock *d = DK(dock);
    struct pane *p = NULL;
    int i;
    for (i = 0; i < PANES && !p; i++)
        if (!d->p[i].id)
            p = &d->p[i];
    if (!p || !id || by_id(d, id))
        return NULL;
    memset(p, 0, sizeof *p);
    p->id = id;
    p->side = side >= GUI_DOCK_LEFT && side <= GUI_DOCK_FLOAT ? side : GUI_DOCK_LEFT;
    p->shown = 1;
    p->fx = p->fy = -1;
    p->fwidth = FLOAT_W, p->fheight = FLOAT_H;
    p->page = gui_tab(d->tabs[GUI_DOCK_LEFT], title);  /* (made as a page; then put where it goes) */
    place_pane(d, p);
    return p->page;
}

void gui_pane_show(struct gui_widget *dock, int id, int on)
{
    struct dock *d = DK(dock);
    struct pane *p = by_id(d, id);
    if (p && p->shown != (on != 0)) {
        p->shown = on != 0;
        place_pane(d, p);
    }
}

int gui_pane_shown(struct gui_widget *dock, int id)
{
    struct pane *p = by_id(DK(dock), id);
    return p && p->shown;
}

void gui_pane_move(struct gui_widget *dock, int id, int side)
{
    struct dock *d = DK(dock);
    struct pane *p = by_id(d, id);
    if (p && side >= GUI_DOCK_LEFT && side <= GUI_DOCK_FLOAT) {
        p->side = side;
        p->shown = 1;
        place_pane(d, p);
    }
}

int gui_pane_side(struct gui_widget *dock, int id)
{
    struct pane *p = by_id(DK(dock), id);
    return p ? p->side : -1;
}

/* ---- the layout as a line: "dock1 L R B;id side shown[ x y w h];..." (side l r b f; the panes of a side in their
   order there; a floating pane's frame place and client size). */
static const char side_ch[] = "lrbf";

int gui_dock_save(struct gui_widget *dock, char *buf, int room)
{
    struct dock *d = DK(dock);
    int n, s, i;
    struct gui_widget *c;
    n = snprintf(buf, (size_t)room, "dock1 %d %d %d", gui_split_get(d->split[0]), gui_split_get(d->split[1]),
                 gui_split_get(d->split[2]));
    for (s = 0; s < 3; s++)                     /* the docked ones, side by side, in their order */
        for (i = 0; (c = gui_tabs_page(d->tabs[s], i)) != NULL; i++) {
            struct pane *p = by_page(d, c);
            if (p && n < room)
                n += snprintf(buf + n, (size_t)(room - n), ";%d %c 1", p->id, side_ch[s]);
        }
    for (i = 0; i < PANES; i++) {               /* then the floating ones and the hidden */
        struct pane *p = &d->p[i];
        if (!p->id || (p->shown && p->side != GUI_DOCK_FLOAT) || n >= room)
            continue;
        if (p->fw) {
            int ox, oy;
            if (gui_origin(p->fw, &ox, &oy) == 0)
                p->fx = ox - p->frame_dx, p->fy = oy - p->frame_dy;
            p->fwidth = p->fw->w, p->fheight = p->fw->h;
        }
        n += snprintf(buf + n, (size_t)(room - n), ";%d %c %d", p->id, side_ch[p->side], p->shown);
        if (p->side == GUI_DOCK_FLOAT && n < room)
            n += snprintf(buf + n, (size_t)(room - n), " %d %d %d %d", p->fx, p->fy, p->fwidth, p->fheight);
    }
    return n < room ? n : -1;
}

int gui_dock_load(struct gui_widget *dock, const char *text)
{
    struct dock *d = DK(dock);
    int a[3], s, i;
    const char *q;
    if (sscanf(text, "dock1 %d %d %d", &a[0], &a[1], &a[2]) != 3)
        return -1;
    for (s = 0; s < 3; s++)
        gui_split_set(d->split[s], a[s]);
    for (i = 0; i < PANES; i++)                 /* every pane to the shelf first: then in the line's order */
        if (d->p[i].id && d->p[i].shown) {
            d->p[i].shown = 0;
            place_pane(d, &d->p[i]);
        }
    for (q = strchr(text, ';'); q; q = strchr(q + 1, ';')) {
        int id, shown, x = -1, y = -1, w = FLOAT_W, h = FLOAT_H, got;
        char sc;
        struct pane *p;
        const char *k;
        got = sscanf(q + 1, "%d %c %d %d %d %d %d", &id, &sc, &shown, &x, &y, &w, &h);
        if (got < 3 || !(p = by_id(d, id)) || !(k = strchr(side_ch, sc)) || !*k)
            continue;                           /* (a pane the app no longer has, or nonsense: passed over) */
        p->side = (int)(k - side_ch);
        if (p->side == GUI_DOCK_FLOAT && got == 7)
            p->fx = x, p->fy = y, p->fwidth = w > 40 ? w : FLOAT_W, p->fheight = h > 30 ? h : FLOAT_H;
        p->shown = shown != 0;
        place_pane(d, p);
    }
    return 0;
}
