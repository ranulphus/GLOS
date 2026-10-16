/* The toolkit's lists and trees (include/glos/gui.h).
 *
 * A list is rows of cells under a header (or none), each row with a 16-pixel
 * icon if it has one; or, as the file manager's folders show, 32-pixel icons
 * in a grid with their names under them. One row is chosen (or several, when
 * the list allows it, with Ctrl and Shift); typing a name's first letters
 * goes to it. A tree is nodes that open and close, the app adding a node's
 * children when it first opens (a folder's are read then). */
#include "guiint.h"

#define CELL_W  76                      /* a big icon's place in the grid */
#define ICON_GAP 4

/* ================================================================ lists */

struct row {
    char **cells;
    const unsigned char *icon;
    void *user;
    int chosen;
};

struct list {
    struct gui_widget w;
    int cols;
    char **heads;
    int *widths;
    struct row *rows;
    int n, room;
    int cur, anchor, multi, big;
    int top;                            /* the first row (grid row, when big) shown */
    struct gui_sb sb;
    char find[32];
    unsigned find_at;
    int (*cmp)(struct gui_widget *, int, int, int);
    int sort_col, sort_down;
};

#define LI(w) ((struct list *)(w))

static int head_h(struct list *l) { return l->heads ? GUI_ROW_H + 3 : 0; }
static int cell_h(void) { return GUI_BIG_W + ICON_GAP + 2 * gui_font->height + 2; }

static int per_line(struct list *l)
{
    int n = (l->w.r.w - GUI_SB - 4) / CELL_W;
    return n > 0 ? n : 1;
}

static int lines(struct list *l) { return l->big ? (l->n + per_line(l) - 1) / per_line(l) : l->n; }

static int shown(struct list *l)
{
    int h = l->w.r.h - 2 - head_h(l), unit = l->big ? cell_h() : GUI_ROW_H;
    return h / unit > 1 ? h / unit : 1;
}

static void sb_place(struct list *l)
{
    l->sb.r.x = l->w.r.x + l->w.r.w - GUI_SB, l->sb.r.y = l->w.r.y, l->sb.r.w = GUI_SB, l->sb.r.h = l->w.r.h;
    l->sb.total = lines(l);
    l->sb.page = shown(l);
    l->sb.pos = l->top;
}

static void list_see(struct list *l, int row)
{
    int line = l->big ? row / per_line(l) : row;
    if (row < 0)
        return;
    if (line < l->top)
        l->top = line;
    if (line >= l->top + shown(l))
        l->top = line - shown(l) + 1;
    if (l->top < 0)
        l->top = 0;
}

static void list_measure(struct gui_widget *w, int *pw, int *ph)
{
    struct list *l = LI(w);
    int i, tw = 0;
    for (i = 0; i < l->cols; i++)
        tw += l->widths ? l->widths[i] : 100;
    *pw = (tw > 120 ? tw : 120) + GUI_SB + 2;
    *ph = head_h(l) + 6 * GUI_ROW_H + 2;
}

static int col_x(struct list *l, int c)
{
    int x = l->w.r.x + 1, i;
    for (i = 0; i < c; i++)
        x += l->widths ? l->widths[i] : (l->w.r.w - GUI_SB - 2) / l->cols;
    return x;
}

static int col_w(struct list *l, int c)
{
    if (c == l->cols - 1)
        return l->w.r.x + l->w.r.w - GUI_SB - col_x(l, c);
    return l->widths ? l->widths[c] : (l->w.r.w - GUI_SB - 2) / l->cols;
}

static void list_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct list *l = LI(w);
    int on = !(w->flags & GUI_DISABLED), focus = gui_is_focus(w), i, c, y0 = w->r.y + 1 + head_h(l);
    struct gfx_rect frame = { w->r.x, w->r.y, w->r.w - GUI_SB + 1, w->r.h };
    struct gfx_rect clip = s->clip, in = { w->r.x + 1, y0, w->r.w - GUI_SB - 1, w->r.h - 2 - head_h(l) };
    sb_place(l);
    gui_draw_field(s, &frame, on);
    if (l->heads) {
        gfx_fill(s, w->r.x + 1, w->r.y + 1, w->r.w - GUI_SB - 1, head_h(l) - 1, GUI_FACE);
        gfx_fill(s, w->r.x + 1, w->r.y + head_h(l), w->r.w - GUI_SB - 1, 1, GUI_LINE);
        for (c = 0; c < l->cols; c++) {
            int x = col_x(l, c);
            struct gfx_rect hc = { x, w->r.y + 1, col_w(l, c), head_h(l) - 1 };
            gfx_rect_cut(&hc, &clip);
            gfx_clip(s, &hc);
            gui_text_fit(s, x + 4, w->r.y + 2, col_w(l, c) - 8, c == l->sort_col && l->cmp ? gui_bold : gui_font,
                         l->heads[c], GUI_LINE);
            gfx_clip(s, &clip);
            if (c > 0)
                gfx_fill(s, x - 1, w->r.y + 1, 1, head_h(l) - 1, GUI_DIM);
        }
    }
    gfx_rect_cut(&in, &clip);
    gfx_clip(s, &in);
    if (l->big) {
        int per = per_line(l), ch = cell_h();
        for (i = l->top * per; i < l->n && i < (l->top + shown(l) + 1) * per; i++) {
            struct row *r = &l->rows[i];
            int x = w->r.x + 2 + (i % per) * CELL_W, y = y0 + (i / per - l->top) * ch + 2;
            int tw = gfx_text_width(gui_font, r->cells[0], -1), ix = x + (CELL_W - GUI_BIG_W) / 2;
            gui_draw_icon(s, ix, y, r->icon, GUI_BIG_W);
            if (r->chosen)
                gfx_frame(s, ix - 2, y - 2, GUI_BIG_W + 4, GUI_BIG_W + 4, GUI_DARK);
            {
                int lw = tw < CELL_W - 4 ? tw : CELL_W - 4, lx = x + (CELL_W - lw) / 2, ly = y + GUI_BIG_W + ICON_GAP;
                if (r->chosen)
                    gfx_fill(s, lx - 2, ly - 1, lw + 4, gui_font->height + 2, GUI_DARK);
                gui_text_fit(s, lx, ly, CELL_W - 4, gui_font, r->cells[0], r->chosen ? GUI_FIELD : GUI_LINE);
                if (focus && i == l->cur) {
                    struct gfx_rect f = { lx - 2, ly - 1, lw + 4, gui_font->height + 2 };
                    gui_draw_focus(s, &f);
                }
            }
        }
    } else {
        for (i = l->top; i < l->n && i < l->top + shown(l) + 1; i++) {
            struct row *r = &l->rows[i];
            int y = y0 + (i - l->top) * GUI_ROW_H, chosen = r->chosen;
            unsigned fg = chosen ? GUI_FIELD : on ? GUI_LINE : GUI_DIM;
            if (chosen)
                gfx_fill(s, w->r.x + 1, y, w->r.w - GUI_SB - 1, GUI_ROW_H, GUI_DARK);
            for (c = 0; c < l->cols; c++) {
                int x = col_x(l, c) + 3, cw = col_w(l, c) - 6;
                struct gfx_rect cc = { col_x(l, c), y, col_w(l, c), GUI_ROW_H };
                gfx_rect_cut(&cc, &in);
                gfx_clip(s, &cc);
                if (c == 0 && r->icon) {
                    gui_draw_icon(s, x, y, r->icon, GUI_ICON_W);
                    x += GUI_ICON_W + 3, cw -= GUI_ICON_W + 3;
                }
                if (r->cells[c])
                    gui_text_fit(s, x, y + 1, cw, gui_font, r->cells[c], fg);
            }
            gfx_clip(s, &in);
            if (focus && i == l->cur) {
                struct gfx_rect f = { w->r.x + 1, y, w->r.w - GUI_SB - 1, GUI_ROW_H };
                gui_draw_focus(s, &f);
            }
        }
    }
    gfx_clip(s, &clip);
    gui_sb_draw(s, &l->sb, on);
}

static void choose_only(struct list *l, int row)
{
    int i;
    for (i = 0; i < l->n; i++)
        l->rows[i].chosen = i == row;
}

static void choose_range(struct list *l, int a, int b)
{
    int i;
    if (a > b) {
        int t = a;
        a = b, b = t;
    }
    for (i = 0; i < l->n; i++)
        l->rows[i].chosen = i >= a && i <= b;
}

static void go(struct list *l, int row, int extend, int toggle)
{
    if (!l->n)
        return;
    if (row < 0)
        row = 0;
    if (row >= l->n)
        row = l->n - 1;
    l->cur = row;
    if (toggle && l->multi) {
        l->rows[row].chosen = !l->rows[row].chosen;
        l->anchor = row;
    } else if (extend && l->multi) {
        choose_range(l, l->anchor, row);
    } else {
        choose_only(l, row);
        l->anchor = row;
    }
    list_see(l, row);
    gui_damage(&l->w);
    gui_send(&l->w, GUI_ON_SELECT);
}

static int row_at(struct list *l, int x, int y)
{
    int y0 = l->w.r.y + 1 + head_h(l), r;
    if (y < y0 || x >= l->w.r.x + l->w.r.w - GUI_SB)
        return -1;
    if (l->big) {
        int col = (x - l->w.r.x - 2) / CELL_W, line = (y - y0) / cell_h() + l->top;
        if (col < 0 || col >= per_line(l))
            return -1;
        r = line * per_line(l) + col;
    } else {
        r = (y - y0) / GUI_ROW_H + l->top;
    }
    return r >= 0 && r < l->n ? r : -1;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int prefix(const char *a, const char *p)
{
    for (; *p; a++, p++)
        if (lower((unsigned char)*a) != lower((unsigned char)*p))
            return 0;
    return 1;
}

static void sort_rows(struct list *l);

static int list_event(struct gui_widget *w, struct gui_ev *e)
{
    struct list *l = LI(w);
    int shift = (e->mods & GLOS_MOD_SHIFT) != 0, ctrl = (e->mods & GLOS_MOD_CTRL) != 0, r;
    int step = l->big ? per_line(l) : 1;
    sb_place(l);
    if (e->type != GUI_EV_KEY && gui_sb_event(w, &l->sb, e, 1)) {
        l->top = l->sb.pos;
        return 1;
    }
    switch (e->type) {
    case GUI_EV_PRESS:
        if (l->heads && e->y < w->r.y + head_h(l)) {
            int c;
            for (c = l->cols - 1; c > 0 && e->x < col_x(l, c); c--)
                ;
            if (l->cmp) {
                l->sort_down = c == l->sort_col ? !l->sort_down : 0;
                l->sort_col = c;
                sort_rows(l);
            }
            return 1;
        }
        r = row_at(l, e->x, e->y);
        if (r < 0) {
            if (!ctrl && !shift && l->multi) {  /* (a press on nothing: nothing chosen) */
                choose_only(l, -1);
                gui_damage(w);
                gui_send(w, GUI_ON_SELECT);
            }
            return 1;
        }
        if (e->clicks == 2 && r == l->cur) {
            gui_send(w, GUI_ON_ACTIVATE);
            return 1;
        }
        if (e->button == 2 && l->rows[r].chosen)       /* (a right press keeps what is chosen: a menu for it) */
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
    switch (e->key) {
    case GUI_K_UP: go(l, l->cur - step, shift, 0); return 1;
    case GUI_K_DOWN: go(l, l->cur < 0 ? 0 : l->cur + step, shift, 0); return 1;
    case GUI_K_LEFT: if (!l->big) return 0; go(l, l->cur - 1, shift, 0); return 1;
    case GUI_K_RIGHT: if (!l->big) return 0; go(l, l->cur + 1, shift, 0); return 1;
    case GUI_K_PGUP: go(l, l->cur - step * (shown(l) - 1), shift, 0); return 1;
    case GUI_K_PGDN: go(l, l->cur + step * (shown(l) - 1), shift, 0); return 1;
    case GUI_K_HOME: go(l, 0, shift, 0); return 1;
    case GUI_K_END: go(l, l->n - 1, shift, 0); return 1;
    case GUI_K_ENTER:
    case GUI_K_KPENTER:
        if (l->cur >= 0) {
            gui_send(w, GUI_ON_ACTIVATE);
            return 1;
        }
        return 0;
    case GUI_K_SPACE:
        if (ctrl && l->multi) {
            go(l, l->cur, 0, 1);
            return 1;
        }
        break;
    }
    if (ctrl && e->ch == 1 && l->multi) {        /* Ctrl+A */
        int i;
        for (i = 0; i < l->n; i++)
            l->rows[i].chosen = 1;
        gui_damage(w);
        gui_send(w, GUI_ON_SELECT);
        return 1;
    }
    if (!ctrl && e->ch > 0x20 && e->ch < 0x100) {       /* typed: the row whose first cell begins so */
        size_t n = strlen(l->find);
        int i, from;
        if (e->time - l->find_at > 1000)
            n = 0;
        l->find_at = e->time;
        if (n < sizeof l->find - 1) {
            l->find[n] = (char)e->ch;
            l->find[n + 1] = 0;
        }
        from = n == 0 ? l->cur + 1 : l->cur;
        for (i = 0; i < l->n; i++) {
            int k = ((from < 0 ? 0 : from) + i) % l->n;
            if (l->rows[k].cells[0] && prefix(l->rows[k].cells[0], l->find)) {
                go(l, k, 0, 0);
                break;
            }
        }
        return 1;
    }
    return 0;
}

static void free_row(struct list *l, struct row *r)
{
    int c;
    for (c = 0; c < l->cols; c++)
        free(r->cells[c]);
    free(r->cells);
}

static void list_destroy(struct gui_widget *w)
{
    struct list *l = LI(w);
    int i;
    for (i = 0; i < l->n; i++)
        free_row(l, &l->rows[i]);
    free(l->rows);
    for (i = 0; l->heads && i < l->cols; i++)
        free(l->heads[i]);
    free(l->heads);
    free(l->widths);
}

static const struct gui_class list_class = { "list", GUI_C_FOCUS, list_draw, list_event, list_measure, NULL,
                                             list_destroy };

struct gui_widget *gui_list(struct gui_widget *parent, int columns, const char *const *heads, const int *widths)
{
    struct list *l = (struct list *)gui_new(parent, &list_class, sizeof *l);
    int i;
    l->cols = columns > 0 ? columns : 1;
    l->cur = l->anchor = -1;
    l->w.grow = 1;
    if (heads) {
        l->heads = calloc((size_t)l->cols, sizeof *l->heads);
        for (i = 0; i < l->cols; i++)
            l->heads[i] = strdup(heads[i] ? heads[i] : "");
    }
    if (widths) {
        l->widths = malloc((size_t)l->cols * sizeof *l->widths);
        memcpy(l->widths, widths, (size_t)l->cols * sizeof *l->widths);
    }
    return &l->w;
}

int gui_list_add(struct gui_widget *w, const char *const *cells, const unsigned char *icon, void *user)
{
    struct list *l = LI(w);
    struct row *r;
    int c;
    if (l->n == l->room) {
        int room = l->room * 2 + 32;
        struct row *nr = realloc(l->rows, (size_t)room * sizeof *nr);
        if (!nr)
            abort();
        l->rows = nr;
        l->room = room;
    }
    r = &l->rows[l->n];
    r->cells = calloc((size_t)l->cols, sizeof *r->cells);
    for (c = 0; c < l->cols; c++)
        r->cells[c] = strdup(cells && cells[c] ? cells[c] : "");
    r->icon = icon;
    r->user = user;
    r->chosen = 0;
    gui_damage(w);
    return l->n++;
}

void gui_list_clear(struct gui_widget *w)
{
    struct list *l = LI(w);
    int i;
    for (i = 0; i < l->n; i++)
        free_row(l, &l->rows[i]);
    l->n = 0;
    l->cur = l->anchor = -1;
    l->top = 0;
    gui_damage(w);
}

int gui_list_count(struct gui_widget *w) { return LI(w)->n; }
int gui_list_get(struct gui_widget *w) { struct list *l = LI(w); return l->cur >= 0 && l->cur < l->n && l->rows[l->cur].chosen ? l->cur : -1; }

void gui_list_set(struct gui_widget *w, int row)
{
    struct list *l = LI(w);
    if (row < 0 || row >= l->n) {
        choose_only(l, -1);
        l->cur = -1;
        gui_damage(w);
        return;
    }
    l->cur = l->anchor = row;
    choose_only(l, row);
    list_see(l, row);
    gui_damage(w);
}

void *gui_list_user(struct gui_widget *w, int row) { struct list *l = LI(w); return row >= 0 && row < l->n ? l->rows[row].user : NULL; }

const char *gui_list_cell(struct gui_widget *w, int row, int col)
{
    struct list *l = LI(w);
    return row >= 0 && row < l->n && col >= 0 && col < l->cols ? l->rows[row].cells[col] : "";
}

void gui_list_multi(struct gui_widget *w, int on) { LI(w)->multi = on; }
int gui_list_chosen(struct gui_widget *w, int row) { struct list *l = LI(w); return row >= 0 && row < l->n && l->rows[row].chosen; }

void gui_list_icons(struct gui_widget *w, int big)
{
    struct list *l = LI(w);
    l->big = big;
    l->top = 0;
    list_see(l, l->cur);
    gui_damage(w);
}

/* Sorting: a merge sort of the rows' order (stable; qsort has no way to pass the list). */
static struct list *sorting;

static int row_cmp(const struct row *a, const struct row *b)
{
    struct list *l = sorting;
    int ia = (int)(a - l->rows), ib = (int)(b - l->rows), d = l->cmp(&l->w, ia, ib, l->sort_col);
    return l->sort_down ? -d : d;
}

static void sort_rows(struct list *l)
{
    struct row *tmp = malloc((size_t)(l->n ? l->n : 1) * sizeof *tmp), *src = l->rows;
    int *ord = malloc((size_t)(l->n ? l->n : 1) * sizeof *ord), *buf = malloc((size_t)(l->n ? l->n : 1) * sizeof *buf);
    int width, i, cur_user = -1;
    void *cur = l->cur >= 0 ? l->rows[l->cur].user : NULL;
    if (!tmp || !ord || !buf)
        abort();
    sorting = l;
    for (i = 0; i < l->n; i++)
        ord[i] = i;
    for (width = 1; width < l->n; width *= 2) {
        for (i = 0; i < l->n; i += 2 * width) {
            int a = i, am = i + width < l->n ? i + width : l->n, b = am, bm = i + 2 * width < l->n ? i + 2 * width : l->n, k = i;
            while (a < am || b < bm)
                buf[k++] = b >= bm || (a < am && row_cmp(&src[ord[a]], &src[ord[b]]) <= 0) ? ord[a++] : ord[b++];
        }
        memcpy(ord, buf, (size_t)l->n * sizeof *ord);
    }
    for (i = 0; i < l->n; i++)
        tmp[i] = src[ord[i]];
    memcpy(l->rows, tmp, (size_t)l->n * sizeof *tmp);
    for (i = 0; i < l->n; i++)
        if (cur && l->rows[i].user == cur)
            cur_user = i;
    if (cur_user >= 0)
        l->cur = l->anchor = cur_user;
    free(tmp);
    free(ord);
    free(buf);
    gui_damage(&l->w);
}

void gui_list_sort(struct gui_widget *w, int col, int (*cmp)(struct gui_widget *, int, int, int))
{
    struct list *l = LI(w);
    l->cmp = cmp;
    l->sort_col = col;
    if (cmp)
        sort_rows(l);
}

/* ================================================================ trees */

struct gui_node {
    struct gui_node *parent, *first, *last, *next, *prev;
    char *text;
    const unsigned char *icon;
    void *user;
    int open, may;
};

struct tree {
    struct gui_widget w;
    struct gui_node root;
    struct gui_node *cur, *opening;
    int top;
    struct gui_sb sb;
};

#define TR(w) ((struct tree *)(w))
#define INDENT 14

static int depth(struct gui_node *n)
{
    int d = -1;
    for (; n && n->parent; n = n->parent)
        d++;
    return d;
}

/* The node shown after n (depth first, through open nodes). */
static struct gui_node *after_node(struct gui_node *n)
{
    if (n->open && n->first)
        return n->first;
    while (n) {
        if (n->next)
            return n->next;
        n = n->parent;
        if (n && !n->parent)
            return NULL;
    }
    return NULL;
}

static int count_shown(struct tree *t)
{
    struct gui_node *n;
    int c = 0;
    for (n = t->root.first; n; n = after_node(n))
        c++;
    return c;
}

static int index_of(struct tree *t, struct gui_node *x)
{
    struct gui_node *n;
    int c = 0;
    for (n = t->root.first; n; n = after_node(n), c++)
        if (n == x)
            return c;
    return -1;
}

static struct gui_node *nth(struct tree *t, int i)
{
    struct gui_node *n;
    for (n = t->root.first; n && i > 0; n = after_node(n))
        i--;
    return i == 0 ? n : NULL;
}

static int tree_rows(struct tree *t)
{
    int h = (t->w.r.h - 2) / GUI_ROW_H;
    return h > 1 ? h : 1;
}

static void tree_sb(struct tree *t)
{
    t->sb.r.x = t->w.r.x + t->w.r.w - GUI_SB, t->sb.r.y = t->w.r.y, t->sb.r.w = GUI_SB, t->sb.r.h = t->w.r.h;
    t->sb.total = count_shown(t);
    t->sb.page = tree_rows(t);
    t->sb.pos = t->top;
}

static void tree_see(struct tree *t, struct gui_node *n)
{
    int i = index_of(t, n);
    if (i < 0)
        return;
    if (i < t->top)
        t->top = i;
    if (i >= t->top + tree_rows(t))
        t->top = i - tree_rows(t) + 1;
}

static void tree_measure(struct gui_widget *w, int *pw, int *ph) { (void)w, *pw = 160, *ph = 8 * GUI_ROW_H + 2; }

static void tree_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct tree *t = TR(w);
    struct gui_node *n;
    int on = !(w->flags & GUI_DISABLED), focus = gui_is_focus(w), y, row = 0;
    struct gfx_rect frame = { w->r.x, w->r.y, w->r.w - GUI_SB + 1, w->r.h };
    struct gfx_rect clip = s->clip, in = { w->r.x + 1, w->r.y + 1, w->r.w - GUI_SB - 1, w->r.h - 2 };
    tree_sb(t);
    gui_draw_field(s, &frame, on);
    gfx_rect_cut(&in, &clip);
    gfx_clip(s, &in);
    for (n = t->root.first; n; n = after_node(n), row++) {
        int x, d;
        if (row < t->top)
            continue;
        y = w->r.y + 1 + (row - t->top) * GUI_ROW_H;
        if (y >= w->r.y + w->r.h)
            break;
        d = depth(n);
        x = w->r.x + 3 + d * INDENT;
        if (n->may || n->first) {               /* the box that opens it */
            gfx_fill(s, x + 1, y + 4, 9, 9, GUI_FIELD);
            gfx_frame(s, x + 1, y + 4, 9, 9, GUI_DIM);
            gfx_fill(s, x + 3, y + 8, 5, 1, GUI_LINE);
            if (!n->open)
                gfx_fill(s, x + 5, y + 6, 1, 5, GUI_LINE);
        }
        x += 13;
        if (n->icon) {
            gui_draw_icon(s, x, y, n->icon, GUI_ICON_W);
            x += GUI_ICON_W + 3;
        }
        {
            int tw = gfx_text_width(gui_font, n->text, -1);
            if (n == t->cur)
                gfx_fill(s, x - 2, y, tw + 4, GUI_ROW_H, GUI_DARK);
            gfx_text(s, x, y + 1, gui_font, n->text, -1, n == t->cur ? GUI_FIELD : on ? GUI_LINE : GUI_DIM);
            if (focus && n == t->cur) {
                struct gfx_rect f = { x - 2, y, tw + 4, GUI_ROW_H };
                gui_draw_focus(s, &f);
            }
        }
    }
    gfx_clip(s, &clip);
    gui_sb_draw(s, &t->sb, on);
}

static void tree_go(struct tree *t, struct gui_node *n)
{
    if (!n || n == t->cur)
        return;
    t->cur = n;
    tree_see(t, n);
    gui_damage(&t->w);
    gui_send(&t->w, GUI_ON_SELECT);
}

void gui_tree_expand(struct gui_widget *w, struct gui_node *n, int open)
{
    struct tree *t = TR(w);
    if (!n || n->open == (open != 0))
        return;
    if (open && !n->first) {
        t->opening = n;
        gui_send(w, GUI_ON_CHANGE);
        t->opening = NULL;
        if (!n->first)
            n->may = 0;
    }
    n->open = open && n->first;
    if (!n->open && t->cur) {                   /* (the chosen node inside it: the node itself) */
        struct gui_node *p;
        for (p = t->cur->parent; p; p = p->parent)
            if (p == n) {
                tree_go(t, n);
                break;
            }
    }
    gui_damage(w);
}

static int tree_event(struct gui_widget *w, struct gui_ev *e)
{
    struct tree *t = TR(w);
    struct gui_node *n;
    tree_sb(t);
    if (e->type != GUI_EV_KEY && gui_sb_event(w, &t->sb, e, 1)) {
        t->top = t->sb.pos;
        return 1;
    }
    switch (e->type) {
    case GUI_EV_PRESS:
        if (e->button != 1)
            return 0;
        n = nth(t, (e->y - w->r.y - 1) / GUI_ROW_H + t->top);
        if (!n)
            return 1;
        {
            int bx = w->r.x + 3 + depth(n) * INDENT;
            if (e->x >= bx && e->x < bx + 12 && (n->may || n->first)) {
                gui_tree_expand(w, n, !n->open);
                return 1;
            }
        }
        tree_go(t, n);
        if (e->clicks == 2) {
            gui_tree_expand(w, n, !n->open);
            gui_send(w, GUI_ON_ACTIVATE);
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
    n = t->cur ? t->cur : t->root.first;
    switch (e->key) {
    case GUI_K_UP: tree_go(t, t->cur ? nth(t, index_of(t, n) - 1) : n); return 1;
    case GUI_K_DOWN: tree_go(t, t->cur ? after_node(n) : n); return 1;
    case GUI_K_HOME: tree_go(t, t->root.first); return 1;
    case GUI_K_END: tree_go(t, nth(t, count_shown(t) - 1)); return 1;
    case GUI_K_PGUP: {
        int i = index_of(t, n) - (tree_rows(t) - 1);
        tree_go(t, nth(t, i < 0 ? 0 : i));
        return 1;
    }
    case GUI_K_PGDN: {
        int i = index_of(t, n) + (tree_rows(t) - 1), c = count_shown(t);
        tree_go(t, nth(t, i >= c ? c - 1 : i));
        return 1;
    }
    case GUI_K_RIGHT:
        if (n && (n->may || n->first) && !n->open)
            gui_tree_expand(w, n, 1);
        else if (n && n->open && n->first)
            tree_go(t, n->first);
        return 1;
    case GUI_K_LEFT:
        if (n && n->open)
            gui_tree_expand(w, n, 0);
        else if (n && n->parent && n->parent != &t->root)
            tree_go(t, n->parent);
        return 1;
    case GUI_K_ENTER:
    case GUI_K_KPENTER:
        if (n)
            gui_send(w, GUI_ON_ACTIVATE);
        return 1;
    }
    return 0;
}

static void free_nodes(struct gui_node *n)
{
    while (n->first) {
        struct gui_node *c = n->first;
        n->first = c->next;
        free_nodes(c);
        free(c->text);
        free(c);
    }
    n->last = NULL;
}

static void tree_destroy(struct gui_widget *w) { free_nodes(&TR(w)->root); }

static const struct gui_class tree_class = { "tree", GUI_C_FOCUS, tree_draw, tree_event, tree_measure, NULL, tree_destroy };

struct gui_widget *gui_tree(struct gui_widget *parent)
{
    struct tree *t = (struct tree *)gui_new(parent, &tree_class, sizeof *t);
    t->root.open = 1;
    t->w.grow = 1;
    return &t->w;
}

struct gui_node *gui_tree_add(struct gui_widget *w, struct gui_node *parent, const char *text,
                              const unsigned char *icon, void *user, int may_have_children)
{
    struct tree *t = TR(w);
    struct gui_node *n = calloc(1, sizeof *n), *p = parent ? parent : &t->root;
    if (!n)
        abort();
    n->text = strdup(text ? text : "");
    n->icon = icon;
    n->user = user;
    n->may = may_have_children;
    n->parent = p;
    n->prev = p->last;
    if (p->last)
        p->last->next = n;
    else
        p->first = n;
    p->last = n;
    gui_damage(w);
    return n;
}

void gui_tree_clear(struct gui_widget *w, struct gui_node *n)
{
    struct tree *t = TR(w);
    struct gui_node *p = n ? n : &t->root, *c;
    for (c = t->cur; c; c = c->parent)          /* (the chosen node goes with them: n is chosen instead) */
        if (c->parent == p) {
            t->cur = n;
            break;
        }
    free_nodes(p);
    if (n)
        n->open = 0;
    gui_damage(w);
}

struct gui_node *gui_tree_get(struct gui_widget *w) { return TR(w)->cur; }

void gui_tree_set(struct gui_widget *w, struct gui_node *n)
{
    struct tree *t = TR(w);
    struct gui_node *p;
    for (p = n ? n->parent : NULL; p && p != &t->root; p = p->parent)
        if (!p->open)
            gui_tree_expand(w, p, 1);
    t->cur = n;
    if (n)
        tree_see(t, n);
    gui_damage(w);
}

struct gui_node *gui_tree_opening(struct gui_widget *w) { return TR(w)->opening; }
void *gui_node_user(struct gui_node *n) { return n ? n->user : NULL; }
const char *gui_node_text(struct gui_node *n) { return n ? n->text : ""; }
struct gui_node *gui_node_parent(struct gui_node *n) { return n && n->parent && n->parent->parent ? n->parent : NULL; }
