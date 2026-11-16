/* The virtual list (toolkit 2, step C3; docs/v1/toolkit-2.md 2): a list that keeps no rows. It asks the app's
 * model how many rows there are and, for the rows on the screen alone, what is in them, when it draws; so a
 * folder of ten thousand files shows its first screen at once, and rows may go on arriving while it shows
 * (gui_vlist_changed).
 *
 * What is chosen is kept by the rows' ids, which the model gives, so it survives sorting and rows arriving or
 * going. Sorting is the app's, since only the app has all the rows: a press on a column's head asks for it
 * (GUI_ON_SORT; gui_vlist_sort says which column and which way), and the app sorts and says the list changed.
 * A column's edge in the header is dragged to make it wider or narrower.
 *
 * The detail view (rows of cells under a header) is here; the icon views come on the same model. gui_list
 * (list.c) stays for short lists that are simplest kept in the widget. */
#include "guiint.h"

struct vlist {
    struct gui_widget w;
    struct gui_model m;
    int cols;
    char **heads;
    int widths[GUI_VCOLS];              /* (the last column has what is left) */
    int top;                            /* the first row shown */
    int cur;                            /* the current row (-1: none), kept in step with cur_id */
    unsigned long cur_id, anchor_id;
    int anchor;
    unsigned long *chosen;              /* the ids chosen */
    int nchosen, room;
    int multi;
    int sort_col, sort_down;            /* what the header last asked for (sort_col -1: nothing) */
    int drag_col;                       /* the column whose right edge is dragged (-1: none) */
    struct gui_sb sb;
};

#define VL(w) ((struct vlist *)(w))

static int count(struct vlist *l) { int n = l->m.count ? l->m.count(l->m.ctx) : 0; return n > 0 ? n : 0; }
static int head_h(struct vlist *l) { return l->heads ? GUI_ROW_H + 3 : 0; }

static int shown(struct vlist *l)
{
    int h = l->w.r.h - 2 - head_h(l);
    return h / GUI_ROW_H > 1 ? h / GUI_ROW_H : 1;
}

static int col_x(struct vlist *l, int c)
{
    int x = l->w.r.x + 1, i;
    for (i = 0; i < c; i++)
        x += l->widths[i];
    return x;
}

static int col_w(struct vlist *l, int c)
{
    if (c == l->cols - 1) {
        int w = l->w.r.x + l->w.r.w - GUI_SB - col_x(l, c);
        return w > 0 ? w : 0;
    }
    return l->widths[c];
}

static void place(struct vlist *l, int n)
{
    if (l->top > n - shown(l))
        l->top = n - shown(l);
    if (l->top < 0)
        l->top = 0;
    l->sb.r.x = l->w.r.x + l->w.r.w - GUI_SB, l->sb.r.y = l->w.r.y, l->sb.r.w = GUI_SB, l->sb.r.h = l->w.r.h;
    l->sb.total = n, l->sb.page = shown(l), l->sb.pos = l->top;
}

static int is_chosen(struct vlist *l, unsigned long id)
{
    int i;
    for (i = 0; i < l->nchosen; i++)
        if (l->chosen[i] == id)
            return 1;
    return 0;
}

static void choose(struct vlist *l, unsigned long id, int on)
{
    int i;
    for (i = 0; i < l->nchosen; i++)
        if (l->chosen[i] == id) {
            if (!on)
                l->chosen[i] = l->chosen[--l->nchosen];
            return;
        }
    if (!on)
        return;
    if (l->nchosen == l->room) {
        int room = l->room * 2 + 16;
        unsigned long *n = realloc(l->chosen, (size_t)room * sizeof *n);
        if (!n)
            abort();
        l->chosen = n, l->room = room;
    }
    l->chosen[l->nchosen++] = id;
}

static unsigned long id_of(struct vlist *l, int row)
{
    struct gui_vrow r;
    memset(&r, 0, sizeof r);
    l->m.row(l->m.ctx, row, &r);
    return r.id;
}

static void see(struct vlist *l, int row)
{
    if (row < 0)
        return;
    if (row < l->top)
        l->top = row;
    if (row >= l->top + shown(l))
        l->top = row - shown(l) + 1;
    if (l->top < 0)
        l->top = 0;
}

static void vl_measure(struct gui_widget *w, int *pw, int *ph)
{
    struct vlist *l = VL(w);
    int i, tw = 0;
    for (i = 0; i < l->cols; i++)
        tw += l->widths[i];
    *pw = (tw > 120 ? tw : 120) + GUI_SB + 2;
    *ph = head_h(l) + 6 * GUI_ROW_H + 2;
}

static void vl_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct vlist *l = VL(w);
    int on = !(w->flags & GUI_DISABLED), focus = gui_is_focus(w), i, c, y0 = w->r.y + 1 + head_h(l), n = count(l);
    struct gfx_rect frame = { w->r.x, w->r.y, w->r.w - GUI_SB + 1, w->r.h };
    struct gfx_rect clip = s->clip, in = { w->r.x + 1, y0, w->r.w - GUI_SB - 1, w->r.h - 2 - head_h(l) };
    place(l, n);
    gui_draw_field(s, &frame, on);
    if (l->heads) {
        gfx_fill(s, w->r.x + 1, w->r.y + 1, w->r.w - GUI_SB - 1, head_h(l) - 1, GUI_FACE);
        gfx_fill(s, w->r.x + 1, w->r.y + head_h(l), w->r.w - GUI_SB - 1, 1, GUI_LINE);
        for (c = 0; c < l->cols; c++) {
            int x = col_x(l, c), cw = col_w(l, c);
            struct gfx_rect hc = { x, w->r.y + 1, cw, head_h(l) - 1 };
            gfx_rect_cut(&hc, &clip);
            gfx_clip(s, &hc);
            gui_text_fit(s, x + 4, w->r.y + 2, cw - 8 - (c == l->sort_col ? 10 : 0), c == l->sort_col ? gui_bold : gui_font,
                         l->heads[c], GUI_TEXT);
            if (c == l->sort_col && cw > 20)    /* which way it is sorted */
                gui_draw_arrow(s, x + cw - 9, w->r.y + 1 + head_h(l) / 2, l->sort_down ? 1 : 0, GUI_TEXT);
            gfx_clip(s, &clip);
            if (c > 0)
                gfx_fill(s, x - 1, w->r.y + 1, 1, head_h(l) - 1, GUI_DIM);
        }
    }
    gfx_rect_cut(&in, &clip);
    gfx_clip(s, &in);
    for (i = l->top; i < n && i < l->top + shown(l) + 1; i++) {        /* the rows on the screen, asked for now */
        struct gui_vrow r;
        int y = y0 + (i - l->top) * GUI_ROW_H, chosen;
        unsigned fg;
        if (y >= s->clip.y + s->clip.h || y + GUI_ROW_H <= s->clip.y)
            continue;                           /* (not among what is drawn again: not asked for) */
        memset(&r, 0, sizeof r);
        l->m.row(l->m.ctx, i, &r);
        chosen = is_chosen(l, r.id);
        fg = chosen ? GUI_CHOSEN_TEXT : on ? GUI_FIELD_TEXT : GUI_DIM;
        if (chosen)
            gfx_fill(s, w->r.x + 1, y, w->r.w - GUI_SB - 1, GUI_ROW_H, GUI_CHOSEN);
        for (c = 0; c < l->cols; c++) {
            int x = col_x(l, c) + 3, cw = col_w(l, c) - 6;
            struct gfx_rect cc = { col_x(l, c), y, col_w(l, c), GUI_ROW_H };
            gfx_rect_cut(&cc, &in);
            gfx_clip(s, &cc);
            if (c == 0 && r.icon) {
                gui_draw_icon(s, x, y + (GUI_ROW_H - GUI_ICON_W) / 2, r.icon, GUI_ICON_W);
                x += GUI_ICON_W + 3, cw -= GUI_ICON_W + 3;
            }
            if (r.cell[c])
                gui_text_fit(s, x, y + 1, cw, gui_font, r.cell[c], fg);
        }
        gfx_clip(s, &in);
        if (focus && i == l->cur) {
            struct gfx_rect f = { w->r.x + 1, y, w->r.w - GUI_SB - 1, GUI_ROW_H };
            gui_draw_focus(s, &f);
        }
    }
    gfx_clip(s, &clip);
    gui_sb_draw(s, &l->sb, on);
}

/* The current row: row, chosen alone, or with Shift (multi) the rows from the anchor to it, or with Ctrl its
   choice turned over. */
static void go(struct vlist *l, int row, int extend, int toggle)
{
    int n = count(l), i;
    if (!n)
        return;
    if (row < 0)
        row = 0;
    if (row >= n)
        row = n - 1;
    l->cur = row;
    l->cur_id = id_of(l, row);
    if (toggle && l->multi) {
        choose(l, l->cur_id, !is_chosen(l, l->cur_id));
        l->anchor = row, l->anchor_id = l->cur_id;
    } else if (extend && l->multi && l->anchor >= 0 && l->anchor < n) {
        int a = l->anchor < row ? l->anchor : row, b = l->anchor < row ? row : l->anchor;
        l->nchosen = 0;
        for (i = a; i <= b; i++)                /* (the rows between are asked for their ids: the user's own act) */
            choose(l, id_of(l, i), 1);
    } else {
        l->nchosen = 0;
        choose(l, l->cur_id, 1);
        l->anchor = row, l->anchor_id = l->cur_id;
    }
    see(l, row);
    gui_damage(&l->w);
    gui_send(&l->w, GUI_ON_SELECT);
}

static int vl_event(struct gui_widget *w, struct gui_ev *e)
{
    struct vlist *l = VL(w);
    int shift = (e->mods & GLOS_MOD_SHIFT) != 0, ctrl = (e->mods & GLOS_MOD_CTRL) != 0, n = count(l), c, r;
    place(l, n);
    if (l->drag_col >= 0) {                     /* a column's edge held */
        if (e->type == GUI_EV_MOVE) {
            int nw = e->x - col_x(l, l->drag_col);
            l->widths[l->drag_col] = nw < 16 ? 16 : nw;
            gui_damage(w);
            return 1;
        }
        if (e->type == GUI_EV_RELEASE) {
            l->drag_col = -1;
            return 1;
        }
    }
    if (e->type != GUI_EV_KEY && gui_sb_event(w, &l->sb, e, 1)) {
        l->top = l->sb.pos;
        return 1;
    }
    switch (e->type) {
    case GUI_EV_PRESS:
        if (l->heads && e->y < w->r.y + head_h(l)) {
            if (e->button != 1)
                return 1;
            for (c = 0; c < l->cols - 1; c++)   /* near a column's right edge: it is dragged */
                if (e->x >= col_x(l, c) + col_w(l, c) - 3 && e->x <= col_x(l, c) + col_w(l, c) + 2) {
                    l->drag_col = c;
                    return 1;
                }
            for (c = l->cols - 1; c > 0 && e->x < col_x(l, c); c--)
                ;
            l->sort_down = c == l->sort_col ? !l->sort_down : 0;
            l->sort_col = c;
            gui_damage(w);
            gui_send(w, GUI_ON_SORT);           /* (the app sorts, and says the list changed) */
            return 1;
        }
        r = e->y < w->r.y + 1 + head_h(l) || e->x >= w->r.x + w->r.w - GUI_SB ? -1
            : (e->y - w->r.y - 1 - head_h(l)) / GUI_ROW_H + l->top;
        if (r < 0 || r >= n) {
            if (!ctrl && !shift && l->multi && l->nchosen) {    /* (a press on nothing: nothing chosen) */
                l->nchosen = 0;
                gui_damage(w);
                gui_send(w, GUI_ON_SELECT);
            }
            return 1;
        }
        if (e->clicks == 2 && r == l->cur) {
            gui_send(w, GUI_ON_ACTIVATE);
            return 1;
        }
        if (e->button == 2 && is_chosen(l, id_of(l, r)))        /* (a right press keeps what is chosen: a menu for it) */
            return 1;
        go(l, r, shift, ctrl);
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
    if (e->mods & GLOS_MOD_ALT)
        return 0;
    if (ctrl && l->multi && (e->key == GUI_K_UP || e->key == GUI_K_DOWN) && n) {
        int to = l->cur + (e->key == GUI_K_UP ? -1 : 1);       /* Ctrl and an arrow: the current row moves, what
                                                                   is chosen stays (Ctrl+Space then chooses it too) */
        l->cur = to < 0 ? 0 : to >= n ? n - 1 : to;
        l->cur_id = id_of(l, l->cur);
        see(l, l->cur);
        gui_damage(w);
        return 1;
    }
    switch (e->key) {
    case GUI_K_UP: go(l, l->cur - 1, shift, 0); return 1;
    case GUI_K_DOWN: go(l, l->cur < 0 ? 0 : l->cur + 1, shift, 0); return 1;
    case GUI_K_PGUP: go(l, l->cur - (shown(l) - 1), shift, 0); return 1;
    case GUI_K_PGDN: go(l, l->cur + (shown(l) - 1), shift, 0); return 1;
    case GUI_K_HOME: go(l, 0, shift, 0); return 1;
    case GUI_K_END: go(l, n - 1, shift, 0); return 1;
    case GUI_K_ENTER:
    case GUI_K_KPENTER:
        if (l->cur >= 0) {
            gui_send(w, GUI_ON_ACTIVATE);
            return 1;
        }
        return 0;
    case GUI_K_SPACE:
        if (ctrl && l->multi && l->cur >= 0) {
            go(l, l->cur, 0, 1);
            return 1;
        }
        break;
    }
    return 0;
}

static void vl_destroy(struct gui_widget *w)
{
    struct vlist *l = VL(w);
    int i;
    for (i = 0; l->heads && i < l->cols; i++)
        free(l->heads[i]);
    free(l->heads);
    free(l->chosen);
}

static const struct gui_class vlist_class = { "vlist", GUI_C_FOCUS, vl_draw, vl_event, vl_measure, NULL, vl_destroy };

struct gui_widget *gui_vlist(struct gui_widget *parent, int columns, const char *const *heads, const int *widths,
                             const struct gui_model *model)
{
    struct vlist *l = (struct vlist *)gui_new(parent, &vlist_class, sizeof *l);
    int i;
    l->cols = columns < 1 ? 1 : columns > GUI_VCOLS ? GUI_VCOLS : columns;
    l->m = *model;
    if (heads) {
        l->heads = calloc((size_t)l->cols, sizeof *l->heads);
        for (i = 0; l->heads && i < l->cols; i++)
            l->heads[i] = strdup(heads[i]);
    }
    for (i = 0; i < l->cols; i++)
        l->widths[i] = widths ? widths[i] : 100;
    l->cur = l->anchor = -1;
    l->sort_col = l->drag_col = -1;
    return &l->w;
}

/* Rows came, went or changed places: the current row is where its id is now. The model's find says where;
   without one it is looked for a screen either side of where it was, and is no row's if it isn't there. */
void gui_vlist_changed(struct gui_widget *w)
{
    struct vlist *l = VL(w);
    int n = count(l), i, at = -1;
    if (l->cur >= 0) {
        if (l->m.find) {
            at = l->m.find(l->m.ctx, l->cur_id);
        } else {
            int a = l->cur - shown(l), b = l->cur + shown(l);
            for (i = a < 0 ? 0 : a; i <= b && i < n && at < 0; i++)
                if (id_of(l, i) == l->cur_id)
                    at = i;
        }
        l->cur = at >= 0 && at < n ? at : -1;
        if (l->cur >= 0 && l->m.find)
            see(l, l->cur);
    }
    l->anchor = l->cur;
    place(l, n);
    gui_damage(w);
}

int gui_vlist_get(struct gui_widget *w) { return VL(w)->cur; }
unsigned long gui_vlist_id(struct gui_widget *w) { return VL(w)->cur >= 0 ? VL(w)->cur_id : 0; }

void gui_vlist_set(struct gui_widget *w, int row)
{
    struct vlist *l = VL(w);
    int n = count(l);
    if (row < 0 || row >= n) {
        l->cur = l->anchor = -1;
        l->nchosen = 0;
    } else {
        l->cur = l->anchor = row;
        l->cur_id = l->anchor_id = id_of(l, row);
        l->nchosen = 0;
        choose(l, l->cur_id, 1);
        see(l, row);
    }
    gui_damage(w);
}

void gui_vlist_multi(struct gui_widget *w, int on) { VL(w)->multi = on != 0; }

int gui_vlist_chosen(struct gui_widget *w, unsigned long *ids, int room)
{
    struct vlist *l = VL(w);
    int i;
    for (i = 0; ids && i < l->nchosen && i < room; i++)
        ids[i] = l->chosen[i];
    return l->nchosen;
}

void gui_vlist_sort(struct gui_widget *w, int *col, int *down)
{
    *col = VL(w)->sort_col;
    *down = VL(w)->sort_down;
}
