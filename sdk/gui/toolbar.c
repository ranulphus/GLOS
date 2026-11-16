/* The toolbar (toolkit 2, step C2; docs/v1/toolkit-2.md 7): a bar under the menu bar whose buttons show the
 * window's commands (command.c): the command's icon (and its text, beside the icon if the bar is asked for
 * text, or alone where a command has no icon), greyed while it can't be used and held in while it is checked.
 * A press tells the window's on_command, as the menu's item does. What doesn't fit the window's width goes
 * into a menu behind a button at the bar's end (">>"), so a narrow window loses nothing.
 *
 * The bar never has the keyboard (PRD D34): its commands are in the menus and have shortcuts. */
#include <stdio.h>

#include "guiint.h"

struct tb_item {
    int id;                             /* the command's */
    int sep;
};

struct toolbar {
    struct gui_widget w;
    struct tb_item *items;
    int n;
    int text;                           /* texts beside the icons */
    int hot, pressed;                   /* the item under the pointer; the one a press began on (-1: none; n: the
                                           overflow's button) */
    struct gui_menu *more;              /* the overflow's menu, as last opened */
};

#define TB(w) ((struct toolbar *)(w))
#define MORE_W 16

static int bar_h(void) { return (gui_font->height > GUI_ICON_W ? gui_font->height : GUI_ICON_W) + 9; }

static int item_w(struct toolbar *t, int i)
{
    struct gui_cmd *c;
    if (t->items[i].sep)
        return 7;
    c = gui_cmd_find(t->w.win, t->items[i].id);
    if (!c)
        return GUI_ICON_W + 8;
    if (!c->icon)
        return gui_text_w(gui_font, c->text) + 12;
    return GUI_ICON_W + 8 + (t->text ? gui_text_w(gui_font, c->text) + 4 : 0);
}

/* How many of the items fit the bar (the rest are the overflow's), and where item i starts. */
static int fit(struct toolbar *t)
{
    int x = 3, i, all = 3;
    for (i = 0; i < t->n; i++)
        all += item_w(t, i);
    if (all <= t->w.r.w - 3)
        return t->n;
    for (i = 0; i < t->n; i++) {
        if (x + item_w(t, i) > t->w.r.w - 3 - MORE_W)
            break;
        x += item_w(t, i);
    }
    while (i > 0 && t->items[i - 1].sep)        /* (no rule left at the end) */
        i--;
    return i;
}

static int item_x(struct toolbar *t, int i)
{
    int x = t->w.r.x + 3, k;
    for (k = 0; k < i; k++)
        x += item_w(t, k);
    return x;
}

static int item_at(struct toolbar *t, int x, int y)
{
    int n = fit(t), i;
    if (y < t->w.r.y + 2 || y >= t->w.r.y + t->w.r.h - 3)
        return -1;
    for (i = 0; i < n; i++)
        if (!t->items[i].sep && x >= item_x(t, i) && x < item_x(t, i) + item_w(t, i))
            return i;
    if (n < t->n && x >= t->w.r.x + t->w.r.w - 3 - MORE_W && x < t->w.r.x + t->w.r.w - 3)
        return t->n;
    return -1;
}

static void tb_measure(struct gui_widget *w, int *pw, int *ph)
{
    (void)w;
    *pw = 3 + MORE_W + 3;                       /* (as narrow as its overflow: the window's width is not its to say) */
    *ph = bar_h();
}

static void tb_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct toolbar *t = TB(w);
    int n = fit(t), i, top = w->r.y + 2, h = w->r.h - 5;
    gfx_fill(s, w->r.x, w->r.y, w->r.w, w->r.h - 1, GUI_FACE);
    gfx_fill(s, w->r.x, w->r.y + w->r.h - 1, w->r.w, 1, GUI_LINE);
    for (i = 0; i < n; i++) {
        struct gui_cmd *c = gui_cmd_find(w->win, t->items[i].id);
        int x = item_x(t, i), iw = item_w(t, i), off, held, tx;
        if (t->items[i].sep) {
            gfx_fill(s, x + 3, top + 2, 1, h - 4, GUI_DIM);
            continue;
        }
        if (!c)
            continue;
        off = (c->state & GUI_CS_OFF) != 0;
        held = !off && ((c->state & GUI_CS_CHECKED) || (t->pressed == i && t->hot == i));
        if (held)
            gfx_fill(s, x, top, iw, h, GUI_DIM);
        if (held || (!off && t->hot == i))
            gfx_frame(s, x, top, iw, h, GUI_LINE);
        tx = x + 6;
        if (c->icon) {
            if (off)
                gui_draw_icon_dim(s, x + 4, top + (h - GUI_ICON_W) / 2, c->icon, GUI_ICON_W);
            else
                gui_draw_icon(s, x + 4, top + (h - GUI_ICON_W) / 2, c->icon, GUI_ICON_W);
            tx = x + 4 + GUI_ICON_W + 4;
        }
        if (!c->icon || t->text) {
            char plain[80];
            gui_strip_amp(plain, c->text, sizeof plain);
            gfx_text(s, tx, top + (h - gui_font->height) / 2, gui_font, plain, -1, off ? GUI_DIM : GUI_TEXT);
        }
    }
    if (n < t->n) {                             /* the overflow's button: two arrow heads */
        int x = w->r.x + w->r.w - 3 - MORE_W, held = t->pressed == t->n && t->hot == t->n;
        if (held)
            gfx_fill(s, x, top, MORE_W, h, GUI_DIM);
        if (held || t->hot == t->n)
            gfx_frame(s, x, top, MORE_W, h, GUI_LINE);
        gui_draw_arrow(s, x + 5, top + h / 2, 3, GUI_TEXT);
        gui_draw_arrow(s, x + 10, top + h / 2, 3, GUI_TEXT);
    }
}

static void overflow(struct toolbar *t)
{
    int n = fit(t), i;
    if (!t->more)
        t->more = gui_menu_new();
    gui_menu_clear(t->more);
    for (i = n; i < t->n; i++) {
        if (t->items[i].sep)
            gui_menu_sep(t->more);
        else
            gui_menu_command(t->more, t->items[i].id);
    }
    gui_popup_menu(t->w.win, t->more, t->w.r.x + t->w.r.w - 3 - MORE_W, t->w.r.y + t->w.r.h - 1);
}

static int tb_event(struct gui_widget *w, struct gui_ev *e)
{
    struct toolbar *t = TB(w);
    int i;
    switch (e->type) {
    case GUI_EV_MOVE:
        i = item_at(t, e->x, e->y);
        if (i != t->hot) {
            t->hot = i;
            gui_damage(w);
        }
        if (i >= 0 && i < t->n) {               /* the button's tooltip: the command's, or its name where the bar
                                                   shows no texts; with its shortcut */
            struct gui_cmd *c = gui_cmd_find(w->win, t->items[i].id);
            char plain[80], tip[160];
            if (c && (c->tip || (c->icon && !t->text))) {
                gui_strip_amp(plain, c->text, sizeof plain);
                snprintf(tip, sizeof tip, "%s%s%s%s", c->tip ? c->tip : plain, c->shortcut ? " (" : "",
                         c->shortcut ? c->shortcut : "", c->shortcut ? ")" : "");
                gui_tip_at(w, i + 1, tip);
            }
        }
        return 1;
    case GUI_EV_LEAVE:
        if (t->hot != -1) {
            t->hot = -1;
            gui_damage(w);
        }
        return 1;
    case GUI_EV_PRESS:
        if (e->button != 1)
            return 1;
        t->hot = t->pressed = item_at(t, e->x, e->y);
        gui_damage(w);
        if (t->pressed == t->n) {               /* (the overflow's menu drops at the press, as a menu's title) */
            t->pressed = -1;
            overflow(t);
        }
        return 1;
    case GUI_EV_RELEASE:
        if (e->button != 1)
            return 1;
        i = t->pressed;
        t->pressed = -1;
        gui_damage(w);
        if (i >= 0 && i < t->n && i == item_at(t, e->x, e->y)) {
            struct gui_cmd *c = gui_cmd_find(w->win, t->items[i].id);
            if (c && !(c->state & GUI_CS_OFF))
                gui_post_command(w->win, c->id);
        }
        return 1;
    }
    return 0;
}

static void tb_destroy(struct gui_widget *w)
{
    struct toolbar *t = TB(w);
    free(t->items);
    if (t->more)
        gui_menu_free(t->more);
    if (w->win && w->win->toolbar == w)
        w->win->toolbar = NULL;
}

const struct gui_class gui_toolbar_class = { "toolbar", 0, tb_draw, tb_event, tb_measure, NULL, tb_destroy };

struct gui_widget *gui_toolbar(struct gui_window *win)
{
    struct gui_widget *root = win->root, *w, *after;
    struct toolbar *t;
    if (win->toolbar)
        return win->toolbar;
    w = gui_new(root, &gui_toolbar_class, sizeof *t);
    t = TB(w);
    t->hot = t->pressed = -1;
    after = root->first && root->first->cls == &gui_menubar_class ? root->first : NULL;
    if (w != (after ? after->next : root->first)) {     /* under the menu bar, above the rest */
        if (w->prev)
            w->prev->next = w->next;
        if (w->next)
            w->next->prev = w->prev;
        else
            root->last = w->prev;
        w->prev = after;
        w->next = after ? after->next : root->first;
        if (w->next)
            w->next->prev = w;
        else
            root->last = w;
        if (after)
            after->next = w;
        else
            root->first = w;
    }
    win->toolbar = w;
    gui_relayout(win);
    return w;
}

static struct tb_item *add(struct gui_widget *w)
{
    struct toolbar *t = TB(w);
    struct tb_item *n = realloc(t->items, (size_t)(t->n + 1) * sizeof *n);
    if (!n)
        abort();
    t->items = n;
    memset(&t->items[t->n], 0, sizeof *n);
    gui_damage(w);
    return &t->items[t->n++];
}

void gui_toolbar_command(struct gui_widget *w, int id) { add(w)->id = id; }
void gui_toolbar_sep(struct gui_widget *w) { add(w)->sep = 1; }

void gui_toolbar_text(struct gui_widget *w, int on)
{
    TB(w)->text = on != 0;
    gui_damage(w);
}
