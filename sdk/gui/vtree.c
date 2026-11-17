/* The virtual tree (toolkit 2, step C3; docs/v1/toolkit-2.md 2): a tree that keeps no texts. Of the app's
 * nodes it keeps the ids of those that are on show, in their order: the top's, and under each node that is
 * open, its children's. A node's text, icon and badge are asked for when it is drawn, and its children's ids
 * when it is opened, so a folder with thousands of folders in it costs nothing until it is opened and then
 * only their ids. The model says by a node's id; ids are the app's (a folder's number, a pointer), never 0.
 *
 * Keys and the mouse are gui_tree's: the arrows (Right opens or goes in, Left closes or goes out), Home, End,
 * the page keys, Enter (GUI_ON_ACTIVATE), a press on the box that opens a node, a double press. What is chosen
 * is a node's id (GUI_ON_SELECT). A badge is a short text at the row's right end (a count of unread things). */
#include "guiint.h"

struct shown {
    unsigned long id;
    unsigned short depth;
    unsigned char open, may;            /* its children are on show after it; it has, or may have, children (2: not
                                           asked yet: when it is first drawn, or opened) */
};

struct vtree {
    struct gui_widget w;
    struct gui_tmodel m;
    struct shown *v;
    int n, room;
    int top, cur;                       /* the first row shown; the current node's place (-1: none) */
    unsigned long cur_id;
    struct gui_sb sb;
    int dpress, dpx, dpy;               /* (toolkit 2, C7) a node pressed: dragged if it moves (dnd.c) */
    int drop_at;                        /* a drag over it: the row of the node a drop would go into (-1: none) */
};

#define VT(w) ((struct vtree *)(w))
#define INDENT 14
#define CHUNK 64

static int rows(struct vtree *t)
{
    int h = (t->w.r.h - 2) / GUI_ROW_H;
    return h > 1 ? h : 1;
}

static void place(struct vtree *t)
{
    if (t->top > t->n - rows(t))
        t->top = t->n - rows(t);
    if (t->top < 0)
        t->top = 0;
    t->sb.r.x = t->w.r.x + t->w.r.w - GUI_SB, t->sb.r.y = t->w.r.y, t->sb.r.w = GUI_SB, t->sb.r.h = t->w.r.h;
    t->sb.total = t->n, t->sb.page = rows(t), t->sb.pos = t->top;
}

static void see(struct vtree *t, int at)
{
    if (at < 0)
        return;
    if (at < t->top)
        t->top = at;
    if (at >= t->top + rows(t))
        t->top = at - rows(t) + 1;
}

/* Room for k more entries at place `at`. */
static void gap(struct vtree *t, int at, int k)
{
    if (t->n + k > t->room) {
        int room = (t->n + k) * 2 + 16;
        struct shown *v = realloc(t->v, (size_t)room * sizeof *v);
        if (!v)
            abort();
        t->v = v, t->room = room;
    }
    memmove(&t->v[at + k], &t->v[at], (size_t)(t->n - at) * sizeof *t->v);
    t->n += k;
}

/* The children of the node with that id (0: the top's) put on show from place `at`, at a depth: how many. */
static int add_children(struct vtree *t, unsigned long id, int at, int depth)
{
    unsigned long ids[CHUNK];
    int from = 0, total, k, i;
    do {
        total = t->m.children(t->m.ctx, id, ids, CHUNK, from);
        k = total - from < CHUNK ? total - from : CHUNK;
        if (k <= 0)
            break;
        gap(t, at + from, k);
        for (i = 0; i < k; i++) {               /* (their ids alone: a node is asked for when it is drawn) */
            struct shown *s = &t->v[at + from + i];
            s->id = ids[i], s->depth = (unsigned short)depth, s->open = 0, s->may = 2;
        }
        from += k;
    } while (from < total);
    return from;
}

static int may(struct vtree *t, int at)         /* has the node children, or may it? (asked once) */
{
    if (t->v[at].may == 2) {
        struct gui_tnode nd;
        memset(&nd, 0, sizeof nd);
        t->m.node(t->m.ctx, t->v[at].id, &nd);
        t->v[at].may = nd.may != 0;
    }
    return t->v[at].may;
}

static int after(struct vtree *t, int at)       /* the place after the node at `at` and all under it */
{
    int i;
    for (i = at + 1; i < t->n && t->v[i].depth > t->v[at].depth; i++)
        ;
    return i;
}

static void fix_cur(struct vtree *t)            /* the current node's place, by its id */
{
    int i;
    for (i = 0; t->cur >= 0 && i < t->n; i++)
        if (t->v[i].id == t->cur_id) {
            t->cur = i;
            return;
        }
    t->cur = -1;
}

static void open_at(struct vtree *t, int at, int on)
{
    struct shown *s = &t->v[at];
    if (!may(t, at) || s->open == (on != 0))
        return;
    if (on) {
        unsigned long id = s->id;
        int depth = s->depth + 1, k;
        k = add_children(t, id, at + 1, depth);
        t->v[at].open = 1;
        if (!k)
            t->v[at].may = 0, t->v[at].open = 0;        /* (none after all: no box) */
    } else {
        int end = after(t, at);
        if (t->cur > at && t->cur < end)        /* (the current node was under it: it is the current one) */
            t->cur = at, t->cur_id = s->id;
        memmove(&t->v[at + 1], &t->v[end], (size_t)(t->n - end) * sizeof *t->v);
        t->n -= end - at - 1;
        t->v[at].open = 0;
    }
    fix_cur(t);
    gui_damage(&t->w);
}

static void go(struct vtree *t, int at)
{
    if (!t->n)
        return;
    if (at < 0)
        at = 0;
    if (at >= t->n)
        at = t->n - 1;
    t->cur = at, t->cur_id = t->v[at].id;
    see(t, at);
    gui_damage(&t->w);
    gui_send(&t->w, GUI_ON_SELECT);
}

static void vt_measure(struct gui_widget *w, int *pw, int *ph) { (void)w, *pw = 160, *ph = 8 * GUI_ROW_H + 2; }

static void vt_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct vtree *t = VT(w);
    int on = !(w->flags & GUI_DISABLED), focus = gui_is_focus(w), i;
    struct gfx_rect frame = { w->r.x, w->r.y, w->r.w - GUI_SB + 1, w->r.h };
    struct gfx_rect clip = s->clip, in = { w->r.x + 1, w->r.y + 1, w->r.w - GUI_SB - 1, w->r.h - 2 };
    place(t);
    gui_draw_field(s, &frame, on);
    gfx_rect_cut(&in, &clip);
    gfx_clip(s, &in);
    for (i = t->top; i < t->n && i < t->top + rows(t) + 1; i++) {      /* the nodes on the screen, asked for now */
        struct gui_tnode nd;
        int y = w->r.y + 1 + (i - t->top) * GUI_ROW_H, x = w->r.x + 3 + t->v[i].depth * INDENT, tw, right;
        unsigned fg = i == t->cur ? GUI_CHOSEN_TEXT : on ? GUI_FIELD_TEXT : GUI_DIM;
        if (y >= s->clip.y + s->clip.h || y + GUI_ROW_H <= s->clip.y)
            continue;
        memset(&nd, 0, sizeof nd);
        t->m.node(t->m.ctx, t->v[i].id, &nd);
        if (t->v[i].may == 2)
            t->v[i].may = nd.may != 0;
        if (t->v[i].may) {                      /* the box that opens it */
            gfx_fill(s, x + 1, y + 4, 9, 9, GUI_FIELD);
            gfx_frame(s, x + 1, y + 4, 9, 9, GUI_DIM);
            gfx_fill(s, x + 3, y + 8, 5, 1, GUI_FIELD_TEXT);
            if (!t->v[i].open)
                gfx_fill(s, x + 5, y + 6, 1, 5, GUI_FIELD_TEXT);
        }
        x += 13;
        right = w->r.x + w->r.w - GUI_SB - 3;
        if (nd.badge && nd.badge[0]) {          /* at the row's right end, in a box of its own */
            int bw = gfx_text_width(gui_font, nd.badge, -1) + 8;
            gfx_fill(s, right - bw, y + 1, bw, GUI_ROW_H - 2, GUI_DARK);
            gfx_text(s, right - bw + 4, y + 1, gui_font, nd.badge, -1, GUI_CHOSEN_TEXT);
            right -= bw + 4;
        }
        if (nd.icon) {
            gui_draw_icon_ink(s, x, y + (GUI_ROW_H - GUI_ICON_W) / 2, nd.icon, GUI_ICON_W, on ? GUI_FIELD_TEXT : GUI_DIM);
            x += GUI_ICON_W + 3;
        }
        tw = gfx_text_width(gui_font, nd.text ? nd.text : "", -1);
        if (tw > right - x)
            tw = right - x > 0 ? right - x : 0;
        if (i == t->cur)
            gfx_fill(s, x - 2, y, tw + 4, GUI_ROW_H, GUI_CHOSEN);
        gui_text_fit(s, x, y + 1, tw, gui_font, nd.text ? nd.text : "", fg);
        if (focus && i == t->cur) {
            struct gfx_rect f = { x - 2, y, tw + 4, GUI_ROW_H };
            gui_draw_focus(s, &f);
        }
        if (i == t->drop_at)                    /* a drop would go into this node */
            gfx_frame(s, x - 3, y, tw + 6, GUI_ROW_H, GUI_MARK);
    }
    gfx_clip(s, &clip);
    gui_sb_draw(s, &t->sb, on);
}

static int vt_event(struct gui_widget *w, struct gui_ev *e)
{
    struct vtree *t = VT(w);
    int at;
    place(t);
    if (e->type == GUI_EV_DRAG || e->type == GUI_EV_DRAG_LEAVE || e->type == GUI_EV_DROP) {
        int r = -1;                             /* a drag over it: the node under the pointer */
        if (e->type == GUI_EV_DRAG && e->y >= w->r.y + 1) {
            r = (e->y - w->r.y - 1) / GUI_ROW_H + t->top;
            r = r < t->n ? r : -1;
        }
        if (e->type == GUI_EV_DROP)
            e->ch = (unsigned)t->drop_at;
        if (r != t->drop_at) {
            t->drop_at = r;
            gui_damage(w);
        }
        return 1;
    }
    if (t->dpress && (e->type == GUI_EV_MOVE || e->type == GUI_EV_RELEASE)) {
        if (e->type == GUI_EV_RELEASE)
            t->dpress = 0;
        else if (e->buttons & 1 && abs(e->x - t->dpx) + abs(e->y - t->dpy) > 5) {
            t->dpress = 0;
            gui_drag_start(w);                  /* (the current node offered: the app's offer reads it) */
        }
        return 1;
    }
    if (e->type != GUI_EV_KEY && gui_sb_event(w, &t->sb, e, 1)) {
        t->top = t->sb.pos;
        return 1;
    }
    switch (e->type) {
    case GUI_EV_PRESS:
        if (e->button != 1 && e->button != 2)
            return 0;
        at = (e->y - w->r.y - 1) / GUI_ROW_H + t->top;
        if (e->y < w->r.y + 1 || at >= t->n || e->x >= w->r.x + w->r.w - GUI_SB)
            return 1;
        if (e->button == 1 && may(t, at)) {
            int bx = w->r.x + 3 + t->v[at].depth * INDENT;
            if (e->x >= bx && e->x < bx + 12) {
                open_at(t, at, !t->v[at].open);
                return 1;
            }
        }
        go(t, at);
        if (e->button == 1 && e->clicks == 2) {
            open_at(t, t->cur, !t->v[t->cur].open);
            gui_send(w, GUI_ON_ACTIVATE);
        } else if (e->button == 1 && w->dnd) {
            t->dpress = 1, t->dpx = e->x, t->dpy = e->y;
        }
        return 1;
    case GUI_EV_FOCUS:
    case GUI_EV_BLUR:
        gui_damage(w);
        return 1;
    case GUI_EV_KEY:
        break;
    default:
        return 0;
    }
    if (e->mods & (GLOS_MOD_ALT | GLOS_MOD_CTRL))
        return 0;
    at = t->cur;
    switch (e->key) {
    case GUI_K_UP: go(t, at - 1); return 1;
    case GUI_K_DOWN: go(t, at < 0 ? 0 : at + 1); return 1;
    case GUI_K_PGUP: go(t, at - (rows(t) - 1)); return 1;
    case GUI_K_PGDN: go(t, at + (rows(t) - 1)); return 1;
    case GUI_K_HOME: go(t, 0); return 1;
    case GUI_K_END: go(t, t->n - 1); return 1;
    case GUI_K_RIGHT:
        if (at < 0)
            return 1;
        if (may(t, at) && !t->v[at].open)
            open_at(t, at, 1);
        else if (t->v[at].open && at + 1 < t->n)
            go(t, at + 1);
        return 1;
    case GUI_K_LEFT:
        if (at < 0)
            return 1;
        if (t->v[at].open) {
            open_at(t, at, 0);
        } else {
            int i;                              /* out, to the node it is under */
            for (i = at - 1; i >= 0 && t->v[i].depth >= t->v[at].depth; i--)
                ;
            if (i >= 0)
                go(t, i);
        }
        return 1;
    case GUI_K_ENTER:
    case GUI_K_KPENTER:
        if (at >= 0) {
            gui_send(w, GUI_ON_ACTIVATE);
            return 1;
        }
        return 0;
    }
    return 0;
}

static void vt_destroy(struct gui_widget *w) { free(VT(w)->v); }

static const struct gui_class vtree_class = { "vtree", GUI_C_FOCUS, vt_draw, vt_event, vt_measure, NULL, vt_destroy };

struct gui_widget *gui_vtree(struct gui_widget *parent, const struct gui_tmodel *model)
{
    struct vtree *t = (struct vtree *)gui_new(parent, &vtree_class, sizeof *t);
    t->m = *model;
    t->cur = t->drop_at = -1;
    add_children(t, 0, 0, 0);
    return &t->w;
}

unsigned long gui_vtree_get(struct gui_widget *w) { return VT(w)->cur >= 0 ? VT(w)->cur_id : 0; }

unsigned long gui_vtree_id_at(struct gui_widget *w, int row)   /* (a drop's place) the node on show at that row */
{
    struct vtree *t = VT(w);
    return row >= 0 && row < t->n ? t->v[row].id : 0;
}

static int place_of(struct vtree *t, unsigned long id)
{
    int i;
    for (i = 0; i < t->n; i++)
        if (t->v[i].id == id)
            return i;
    return -1;
}

int gui_vtree_set(struct gui_widget *w, unsigned long id)
{
    struct vtree *t = VT(w);
    int at = place_of(t, id);
    if (at < 0)
        return -1;                              /* (not on show: its parents are to be opened first) */
    t->cur = at, t->cur_id = id;
    see(t, at);
    gui_damage(w);
    return 0;
}

int gui_vtree_open(struct gui_widget *w, unsigned long id, int on)
{
    struct vtree *t = VT(w);
    int at = place_of(t, id);
    if (at < 0)
        return -1;
    open_at(t, at, on);
    return 0;
}

/* The children of a node changed (0: the top's): asked for again, the nodes that were open under it open still
   where they are still there, and the current node found by its id. */
void gui_vtree_changed(struct gui_widget *w, unsigned long id)
{
    struct vtree *t = VT(w);
    unsigned long *was = NULL;
    int at = id ? place_of(t, id) : -1, end, nwas = 0, i, depth;
    if (id && (at < 0 || !t->v[at].open)) {
        if (at >= 0) {                          /* (closed: it may have children now, or none) */
            struct gui_tnode nd;
            memset(&nd, 0, sizeof nd);
            t->m.node(t->m.ctx, id, &nd);
            t->v[at].may = nd.may != 0;
            gui_damage(w);
        }
        return;
    }
    end = id ? after(t, at) : t->n;
    depth = id ? t->v[at].depth + 1 : 0;
    was = malloc((size_t)(end - at) * sizeof *was + 1);
    for (i = at + 1; was && i < end; i++)
        if (t->v[i].open)
            was[nwas++] = t->v[i].id;
    memmove(&t->v[at + 1], &t->v[end], (size_t)(t->n - end) * sizeof *t->v);
    t->n -= end - at - 1;
    add_children(t, id, at + 1, depth);
    for (i = 0; i < nwas; i++) {                /* (in the order they were: a parent before its children) */
        int p = place_of(t, was[i]);
        if (p >= 0)
            open_at(t, p, 1);
    }
    free(was);
    fix_cur(t);
    place(t);
    gui_damage(w);
}
