/* The toolkit's lists and trees (include/glos/gui.h).
 *
 * A list is rows of cells under a header (or none), each row with a 16-pixel
 * icon if it has one; or, as the file manager's folders show, 32-pixel icons
 * in a grid with their names under them. One row is chosen (or several, when
 * the list allows it, with Ctrl and Shift); typing a name's first letters
 * goes to it. A tree is nodes that open and close, the app adding a node's
 * children when it first opens (a folder's are read then). */
#include "guiint.h"

/* ================================================================ lists */
/* (Toolkit 2, C10) A list is the virtual list (vlist.c) over rows it keeps itself: the model below serves them,
 * each with an id of its own so that what is chosen follows a row wherever sorting puts it. Its look and its keys
 * are the virtual list's; gui_list_* are as they were. */

struct row {
    char **cells;
    const unsigned char *icon, *big;    /* (big NULL: the icon itself in the grids) */
    const unsigned *thumb;              /* gui_list_thumb: the app's pixels */
    short tw, th;
    void *user;
    unsigned long id;
};

struct lstore {
    struct gui_widget *w;
    int cols;
    struct row *rows;
    int n, room;
    unsigned long next_id;
    int (*cmp)(struct gui_widget *, int, int, int);
    int sort_col, sort_down;
};

static struct lstore *store_of(struct gui_widget *w);

static int l_count(void *ctx) { return ((struct lstore *)ctx)->n; }

static void l_row(void *ctx, int i, struct gui_vrow *out)
{
    struct lstore *s = ctx;
    int c;
    if (i < 0 || i >= s->n)
        return;
    for (c = 0; c < s->cols && c < GUI_VCOLS; c++)
        out->cell[c] = s->rows[i].cells[c];
    out->icon = s->rows[i].icon;
    out->big = s->rows[i].big ? s->rows[i].big : s->rows[i].icon;      /* (else the same icon, drawn big in the grid) */
    out->thumb = s->rows[i].thumb, out->thumb_w = s->rows[i].tw, out->thumb_h = s->rows[i].th;
    out->id = s->rows[i].id;
}

static int l_find(void *ctx, unsigned long id)
{
    struct lstore *s = ctx;
    int i;
    for (i = 0; i < s->n; i++)
        if (s->rows[i].id == id)
            return i;
    return -1;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int prefix(const char *a, const char *p)
{
    for (; *p; a++, p++)
        if (lower((unsigned char)*a) != lower((unsigned char)*p))
            return 0;
    return 1;
}

static int l_match(void *ctx, const char *typed, int from)
{
    struct lstore *s = ctx;
    int i;
    for (i = 0; i < s->n; i++) {
        int k = ((from < 0 ? 0 : from) + i) % s->n;
        if (s->rows[k].cells[0] && prefix(s->rows[k].cells[0], typed))
            return k;
    }
    return -1;
}

static void free_row(struct lstore *s, struct row *r)
{
    int c;
    for (c = 0; c < s->cols; c++)
        free(r->cells[c]);
    free(r->cells);
}

static void l_drop(void *ctx)
{
    struct lstore *s = ctx;
    int i;
    for (i = 0; i < s->n; i++)
        free_row(s, &s->rows[i]);
    free(s->rows);
    free(s);
}

/* Sorting: a merge sort of the rows' order by the app's comparison of two rows' places (stable). */
static void sort_rows(struct lstore *s)
{
    struct row *tmp = malloc((size_t)(s->n ? s->n : 1) * sizeof *tmp);
    int *ord = malloc((size_t)(s->n ? s->n : 1) * sizeof *ord), *buf = malloc((size_t)(s->n ? s->n : 1) * sizeof *buf);
    int width, i;
    if (!tmp || !ord || !buf)
        abort();
    for (i = 0; i < s->n; i++)
        ord[i] = i;
    for (width = 1; width < s->n; width *= 2) {
        for (i = 0; i < s->n; i += 2 * width) {
            int a = i, am = i + width < s->n ? i + width : s->n, b = am, bm = i + 2 * width < s->n ? i + 2 * width : s->n, k = i;
            while (a < am || b < bm) {
                int take_a = b >= bm;
                if (!take_a && a < am) {
                    int d = s->cmp(s->w, ord[a], ord[b], s->sort_col);
                    take_a = (s->sort_down ? -d : d) <= 0;
                }
                buf[k++] = take_a ? ord[a++] : ord[b++];
            }
        }
        memcpy(ord, buf, (size_t)s->n * sizeof *ord);
    }
    for (i = 0; i < s->n; i++)
        tmp[i] = s->rows[ord[i]];
    memcpy(s->rows, tmp, (size_t)s->n * sizeof *tmp);
    free(tmp);
    free(ord);
    free(buf);
    gui_vlist_sort_show(s->w, s->sort_col, s->sort_down);
    gui_vlist_changed(s->w);                    /* (the current row and the chosen ones follow their ids) */
}

static void l_sort(struct gui_widget *w, int col, int down)    /* (a head pressed) */
{
    struct lstore *s = store_of(w);
    if (!s->cmp)
        return;
    s->sort_col = col, s->sort_down = down;
    sort_rows(s);
}

static struct lstore *store_of(struct gui_widget *w) { return (struct lstore *)gui_vlist_ctx(w); }

struct gui_widget *gui_list(struct gui_widget *parent, int columns, const char *const *heads, const int *widths)
{
    struct lstore *s = calloc(1, sizeof *s);
    struct gui_model m;
    struct gui_widget *w;
    if (!s)
        abort();
    s->cols = columns > 0 ? (columns < GUI_VCOLS ? columns : GUI_VCOLS) : 1;
    m.count = l_count, m.row = l_row, m.find = l_find, m.match = l_match, m.ctx = s;
    w = gui_vlist(parent, s->cols, heads, widths, &m);
    s->w = w;
    gui_vlist_hooks(w, l_sort, l_drop);
    gui_grow(w, 1);
    return w;
}

int gui_list_add(struct gui_widget *w, const char *const *cells, const unsigned char *icon, void *user)
{
    struct lstore *s = store_of(w);
    struct row *r;
    int c;
    if (s->n == s->room) {
        int room = s->room * 2 + 32;
        struct row *nr = realloc(s->rows, (size_t)room * sizeof *nr);
        if (!nr)
            abort();
        s->rows = nr, s->room = room;
    }
    r = &s->rows[s->n];
    r->cells = calloc((size_t)s->cols, sizeof *r->cells);
    for (c = 0; c < s->cols; c++)
        r->cells[c] = strdup(cells && cells[c] ? cells[c] : "");
    r->icon = icon, r->user = user, r->id = ++s->next_id;
    r->big = NULL, r->thumb = NULL, r->tw = r->th = 0;
    s->n++;
    gui_vlist_changed(w);
    return s->n - 1;
}

void gui_list_clear(struct gui_widget *w)
{
    struct lstore *s = store_of(w);
    int i;
    for (i = 0; i < s->n; i++)
        free_row(s, &s->rows[i]);
    s->n = 0;
    gui_vlist_set(w, -1);
    gui_vlist_changed(w);
}

int gui_list_count(struct gui_widget *w) { return store_of(w)->n; }

int gui_list_get(struct gui_widget *w)          /* the current row, if it is chosen */
{
    struct lstore *s = store_of(w);
    int r = gui_vlist_get(w);
    return r >= 0 && r < s->n && gui_vlist_is_chosen(w, s->rows[r].id) ? r : -1;
}

void gui_list_set(struct gui_widget *w, int row) { gui_vlist_set(w, row >= 0 && row < store_of(w)->n ? row : -1); }

void *gui_list_user(struct gui_widget *w, int row)
{
    struct lstore *s = store_of(w);
    return row >= 0 && row < s->n ? s->rows[row].user : NULL;
}

const char *gui_list_cell(struct gui_widget *w, int row, int col)
{
    struct lstore *s = store_of(w);
    return row >= 0 && row < s->n && col >= 0 && col < s->cols ? s->rows[row].cells[col] : "";
}

void gui_list_multi(struct gui_widget *w, int on) { gui_vlist_multi(w, on); }

int gui_list_chosen(struct gui_widget *w, int row)
{
    struct lstore *s = store_of(w);
    return row >= 0 && row < s->n && gui_vlist_is_chosen(w, s->rows[row].id);
}

void gui_list_icons(struct gui_widget *w, int big) { gui_vlist_view(w, big ? GUI_VIEW_ICONS : GUI_VIEW_DETAIL); }

void gui_list_view(struct gui_widget *w, int view) { gui_vlist_view(w, view); }

void gui_list_big(struct gui_widget *w, int row, const unsigned char *big)
{
    struct lstore *s = store_of(w);
    if (row >= 0 && row < s->n)
        s->rows[row].big = big;
}

void gui_list_thumb(struct gui_widget *w, int row, const unsigned *px, int tw, int th)
{
    struct lstore *s = store_of(w);
    if (row < 0 || row >= s->n)
        return;
    s->rows[row].thumb = px && tw > 0 && th > 0 ? px : NULL;
    s->rows[row].tw = (short)tw, s->rows[row].th = (short)th;
    gui_damage(w);
}

int gui_list_shown(struct gui_widget *w, int *first) { return gui_vlist_shown(w, first); }

void gui_list_resort(struct gui_widget *w)
{
    struct lstore *s = store_of(w);
    if (s->cmp && s->n > 1)
        sort_rows(s);
}

void gui_list_sort(struct gui_widget *w, int col, int (*cmp)(struct gui_widget *, int, int, int))
{
    struct lstore *s = store_of(w);
    s->cmp = cmp;
    s->sort_col = col, s->sort_down = 0;
    if (cmp)
        sort_rows(s);
}

/* ================================================================ trees */
/* (Toolkit 2, C10) A tree is the virtual tree (vtree.c) over nodes it keeps itself; a node's id is its address.
 * Before a node first opens its app is told (GUI_ON_CHANGE, gui_tree_opening), so that it adds the node's
 * children then (a folder's read as it opens); a node that has none after all loses its box. */

struct gui_node {
    struct gui_node *parent, *first, *last, *next, *prev;
    char *text;
    const unsigned char *icon;
    void *user;
    int may;
};

struct tstore {
    struct gui_widget *w;
    struct gui_node root;
    struct gui_node *opening;
};

#define ID(n) ((unsigned long)(n))
#define NODE(id) ((struct gui_node *)(id))

static struct tstore *tstore_of(struct gui_widget *w) { return (struct tstore *)gui_vtree_ctx(w); }

static int t_children(void *ctx, unsigned long id, unsigned long *ids, int room, int from)
{
    struct tstore *s = ctx;
    struct gui_node *p = id ? NODE(id) : &s->root, *c;
    int k = 0, i = 0;
    for (c = p->first; c; c = c->next, i++)
        if (i >= from && k < room)
            ids[k++] = ID(c);
    return i;
}

static void t_node(void *ctx, unsigned long id, struct gui_tnode *out)
{
    struct gui_node *n = NODE(id);
    (void)ctx;
    out->text = n->text, out->icon = n->icon, out->may = n->may || n->first;
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

static void t_drop(void *ctx)
{
    free_nodes(&((struct tstore *)ctx)->root);
    free(ctx);
}

static void t_opening(struct gui_widget *w, unsigned long id)  /* the app adds its children now */
{
    struct tstore *s = tstore_of(w);
    struct gui_node *n = NODE(id);
    if (n->first)
        return;
    s->opening = n;
    gui_send(w, GUI_ON_CHANGE);
    s->opening = NULL;
    if (!n->first)
        n->may = 0;
}

struct gui_widget *gui_tree(struct gui_widget *parent)
{
    struct tstore *s = calloc(1, sizeof *s);
    struct gui_tmodel m;
    struct gui_widget *w;
    if (!s)
        abort();
    m.children = t_children, m.node = t_node, m.ctx = s;
    w = gui_vtree(parent, &m);
    s->w = w;
    gui_vtree_hooks(w, t_opening, t_drop);
    gui_grow(w, 1);
    return w;
}

struct gui_node *gui_tree_add(struct gui_widget *w, struct gui_node *parent, const char *text,
                              const unsigned char *icon, void *user, int may_have_children)
{
    struct tstore *s = tstore_of(w);
    struct gui_node *n = calloc(1, sizeof *n), *p = parent ? parent : &s->root;
    if (!n)
        abort();
    n->text = strdup(text ? text : "");
    n->icon = icon, n->user = user, n->may = may_have_children, n->parent = p;
    n->prev = p->last;
    if (p->last)
        p->last->next = n;
    else
        p->first = n;
    p->last = n;
    if (s->opening != p)                        /* (while it opens, the tree reads them itself) */
        gui_vtree_changed(w, p == &s->root ? 0 : ID(p));
    return n;
}

void gui_tree_expand(struct gui_widget *w, struct gui_node *n, int open)
{
    if (n)
        gui_vtree_open(w, ID(n), open);
}

void gui_tree_clear(struct gui_widget *w, struct gui_node *n)
{
    struct tstore *s = tstore_of(w);
    struct gui_node *p = n ? n : &s->root, *c, *cur = NODE(gui_vtree_get(w));
    int inside = 0;
    for (c = cur; c; c = c->parent)             /* (the chosen node goes with them: n is chosen instead) */
        if (c->parent == p)
            inside = 1;
    if (n)
        gui_vtree_open(w, ID(n), 0);
    free_nodes(p);
    gui_vtree_changed(w, n ? ID(n) : 0);
    if (inside && n)
        gui_vtree_set(w, ID(n));
}

struct gui_node *gui_tree_get(struct gui_widget *w) { return NODE(gui_vtree_get(w)); }

void gui_tree_set(struct gui_widget *w, struct gui_node *n)
{
    struct gui_node *chain[64];
    int k = 0;
    struct gui_node *p;
    struct tstore *s = tstore_of(w);
    if (!n)
        return;
    for (p = n->parent; p && p != &s->root && k < 64; p = p->parent)
        chain[k++] = p;
    while (k-- > 0)                             /* (its parents opened, the top's first) */
        gui_vtree_open(w, ID(chain[k]), 1);
    gui_vtree_set(w, ID(n));
}

struct gui_node *gui_tree_opening(struct gui_widget *w) { return tstore_of(w)->opening; }
void *gui_node_user(struct gui_node *n) { return n ? n->user : NULL; }
const char *gui_node_text(struct gui_node *n) { return n ? n->text : ""; }
struct gui_node *gui_node_parent(struct gui_node *n) { return n && n->parent && n->parent->parent ? n->parent : NULL; }

struct gui_node *gui_tree_child(struct gui_widget *w, struct gui_node *n, const char *text)
{
    struct gui_node *c;
    for (c = n ? n->first : tstore_of(w)->root.first; c; c = c->next)
        if (!strcmp(c->text, text))
            return c;
    return NULL;
}
