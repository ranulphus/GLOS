/* The toolkit's menus (include/glos/gui.h): the menu bar, menus that drop
 * from it or pop up where the app says, and the choice (a button that drops
 * a list of its items down).
 *
 * An open menu is a pop-up window of its own (GLOS_WIN_POPUP), above every
 * other, with the keyboard while it is up: Up and Down, Enter, Esc, Left and
 * Right to the menu beside it, an item's letter. A press anywhere else takes
 * the keyboard from it, and it goes. One menu is open at a time. Choosing an
 * item closes the menu first; the window's on_command runs after. */
#include <stdio.h>

#include "guiint.h"

struct item {
    char *text;                         /* with "&" */
    int id;
    char *shortcut;
    unsigned key, mods;                 /* the shortcut's */
    int sep, off, checked;
    unsigned mnemonic;
};

struct gui_menu {
    char *title;
    struct item *items;
    int n, room;
    unsigned mnemonic;
};

struct gui_menubar {
    struct gui_widget w;
    struct gui_menu **menus;
    int n;
    int open;                           /* the menu dropped down from it (-1: none) */
};

/* What is open (one at a time). */
static struct {
    struct gui_window *popup, *owner;
    struct gui_menu *menu;
    struct gui_menubar *bar;            /* the bar it came from, or NULL */
    struct gui_widget *choice;          /* a choice's list, or NULL */
    int hot;                            /* the item under the pointer or the arrows */
    unsigned closed_at;                 /* when it last went (a press on its title then closes, not opens) */
    int closed_from;
} M = { NULL, NULL, NULL, NULL, NULL, -1, 0, -1 };

#define SEP_H 7

/* ---- shortcuts */

static const struct { const char *name; unsigned key; } keys[] = {
    { "Del", GUI_K_DEL }, { "Delete", GUI_K_DEL }, { "Ins", GUI_K_INS }, { "Enter", GUI_K_ENTER },
    { "Esc", GUI_K_ESC }, { "Tab", GUI_K_TAB }, { "Backspace", GUI_K_BACK }, { "Home", GUI_K_HOME },
    { "End", GUI_K_END }, { "PgUp", GUI_K_PGUP }, { "PgDn", GUI_K_PGDN }, { "Up", GUI_K_UP }, { "Down", GUI_K_DOWN },
    { "Left", GUI_K_LEFT }, { "Right", GUI_K_RIGHT }, { "Space", GUI_K_SPACE }, { "F11", 0x57 }, { "F12", 0x58 },
};

static unsigned letter_key(unsigned c)
{
    static const char row[] = "\0\0" "1234567890" "\0\0\0\0" "qwertyuiop" "\0\0\0\0" "asdfghjkl" "\0\0\0\0\0" "zxcvbnm";
    unsigned i;
    if (c >= 'A' && c <= 'Z')
        c += 32;
    for (i = 0; i < sizeof row - 1; i++)
        if ((unsigned char)row[i] == c && c)
            return i;
    return 0;
}

int gui_shortcut_parse(const char *s, unsigned *key, unsigned *mods)
{
    unsigned m = 0, i;
    *key = 0;
    for (;;) {
        if (!strncmp(s, "Ctrl+", 5))
            m |= GLOS_MOD_CTRL, s += 5;
        else if (!strncmp(s, "Alt+", 4))
            m |= GLOS_MOD_ALT, s += 4;
        else if (!strncmp(s, "Shift+", 6))
            m |= GLOS_MOD_SHIFT, s += 6;
        else
            break;
    }
    *mods = m;
    if (s[0] == 'F' && s[1] >= '1' && s[1] <= '9' && (!s[2] || (s[2] == '0' && !s[3]))) {
        int n = atoi(s + 1);
        if (n >= 1 && n <= 10) {
            *key = GUI_K_F1 + (unsigned)n - 1;
            return 0;
        }
    }
    for (i = 0; i < sizeof keys / sizeof *keys; i++)
        if (!strcmp(s, keys[i].name)) {
            *key = keys[i].key;
            return 0;
        }
    if (s[0] && !s[1] && (*key = letter_key((unsigned char)s[0])) != 0)
        return 0;
    return -1;
}

/* ---- menus */

struct gui_menu *gui_menu_new(void)
{
    struct gui_menu *m = calloc(1, sizeof *m);
    if (!m)
        abort();
    return m;
}

static struct item *add(struct gui_menu *m)
{
    if (m->n == m->room) {
        int room = m->room * 2 + 8;
        struct item *ni = realloc(m->items, (size_t)room * sizeof *ni);
        if (!ni)
            abort();
        m->items = ni;
        m->room = room;
    }
    memset(&m->items[m->n], 0, sizeof m->items[m->n]);
    return &m->items[m->n++];
}

static unsigned mnemonic_of(const char *t)
{
    struct gui_widget tmp;
    memset(&tmp, 0, sizeof tmp);
    gui_set_mnemonic(&tmp, t);
    return tmp.mnemonic;
}

void gui_menu_item(struct gui_menu *m, const char *text, int id, const char *shortcut)
{
    struct item *it = add(m);
    it->text = strdup(text);
    it->id = id;
    it->mnemonic = mnemonic_of(text);
    if (shortcut) {
        it->shortcut = strdup(shortcut);
        if (gui_shortcut_parse(shortcut, &it->key, &it->mods) != 0)
            it->key = 0;
    }
}

void gui_menu_sep(struct gui_menu *m) { add(m)->sep = 1; }

static struct item *find_id(struct gui_menu *m, int id)
{
    int i;
    for (i = 0; i < m->n; i++)
        if (!m->items[i].sep && m->items[i].id == id)
            return &m->items[i];
    return NULL;
}

void gui_menu_enable(struct gui_menu *m, int id, int on)
{
    struct item *it = find_id(m, id);
    if (it)
        it->off = !on;
}

void gui_menu_check(struct gui_menu *m, int id, int on)
{
    struct item *it = find_id(m, id);
    if (it)
        it->checked = on != 0;
}

/* ---- the bar */

static int bar_h(void) { return gui_font->height + 6; }

static int title_x(struct gui_menubar *b, int i)
{
    int x = b->w.r.x + 4, k;
    for (k = 0; k < i; k++)
        x += gui_text_w(gui_font, b->menus[k]->title) + 14;
    return x;
}

static void bar_measure(struct gui_widget *w, int *pw, int *ph)
{
    struct gui_menubar *b = (struct gui_menubar *)w;
    *pw = title_x(b, b->n) - w->r.x;
    *ph = bar_h();
}

static void bar_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct gui_menubar *b = (struct gui_menubar *)w;
    int i;
    gfx_fill(s, w->r.x, w->r.y, w->r.w, w->r.h - 1, GUI_FACE);
    gfx_fill(s, w->r.x, w->r.y + w->r.h - 1, w->r.w, 1, GUI_LINE);
    for (i = 0; i < b->n; i++) {
        int x = title_x(b, i), tw = gui_text_w(gui_font, b->menus[i]->title);
        int on = b->open == i;
        if (on)
            gfx_fill(s, x, w->r.y, tw + 14, w->r.h - 1, GUI_DARK);
        gui_draw_text(s, x + 7, w->r.y + 3, gui_font, b->menus[i]->title, on ? GUI_FIELD : GUI_LINE);
    }
}

static void open_menu(struct gui_window *owner, struct gui_menu *m, int sx, int sy, struct gui_menubar *bar,
                      struct gui_widget *choice, int hot);

static void bar_open(struct gui_menubar *b, int i, int keyboard)
{
    int ox, oy;
    if (i < 0 || i >= b->n || gui_origin(b->w.win, &ox, &oy) != 0)
        return;
    b->open = i;
    gui_damage(&b->w);
    open_menu(b->w.win, b->menus[i], ox + title_x(b, i), oy + b->w.r.y + b->w.r.h, b, NULL, keyboard ? 0 : -1);
}

static int bar_event(struct gui_widget *w, struct gui_ev *e)
{
    struct gui_menubar *b = (struct gui_menubar *)w;
    int i;
    if (e->type != GUI_EV_PRESS || e->button != 1)
        return 0;
    for (i = 0; i < b->n; i++) {
        int x = title_x(b, i);
        if (e->x >= x && e->x < x + gui_text_w(gui_font, b->menus[i]->title) + 14) {
            if (M.closed_from == i && e->time - M.closed_at < 150) {    /* (its own menu just went: closed) */
                M.closed_from = -1;
                return 1;
            }
            bar_open(b, i, 0);
            return 1;
        }
    }
    return 1;
}

static void bar_destroy(struct gui_widget *w)
{
    struct gui_menubar *b = (struct gui_menubar *)w;
    int i, k;
    for (i = 0; i < b->n; i++) {
        struct gui_menu *m = b->menus[i];
        for (k = 0; k < m->n; k++) {
            free(m->items[k].text);
            free(m->items[k].shortcut);
        }
        free(m->items);
        free(m->title);
        free(m);
    }
    free(b->menus);
    if (b->w.win)
        b->w.win->menubar = NULL;
}

const struct gui_class gui_menubar_class = { "menubar", 0, bar_draw, bar_event, bar_measure, NULL, bar_destroy };

struct gui_menubar *gui_menubar(struct gui_window *w)
{
    struct gui_menubar *b;
    struct gui_widget *root = w->root;
    if (w->menubar)
        return w->menubar;
    b = (struct gui_menubar *)gui_new(root, &gui_menubar_class, sizeof *b);
    if (root->first != &b->w) {                 /* (the bar goes first) */
        if (b->w.prev)
            b->w.prev->next = b->w.next;
        if (b->w.next)
            b->w.next->prev = b->w.prev;
        else
            root->last = b->w.prev;
        b->w.prev = NULL;
        b->w.next = root->first;
        root->first->prev = &b->w;
        root->first = &b->w;
    }
    b->open = -1;
    w->menubar = b;
    return b;
}

struct gui_menu *gui_menu(struct gui_menubar *bar, const char *title)
{
    struct gui_menu *m = gui_menu_new(), **nm = realloc(bar->menus, (size_t)(bar->n + 1) * sizeof *nm);
    if (!nm)
        abort();
    bar->menus = nm;
    m->title = strdup(title);
    m->mnemonic = mnemonic_of(title);
    bar->menus[bar->n++] = m;
    if (bar->w.win)
        gui_relayout(bar->w.win);
    return m;
}

int gui_menubar_height(struct gui_menubar *b) { return b ? bar_h() : 0; }

/* ---- the open menu's window */

struct view {
    struct gui_widget w;
};

static int item_h(const struct item *it) { return it->sep ? SEP_H : GUI_ROW_H + 2; }

static int item_y(struct gui_menu *m, int i)
{
    int y = 2, k;
    for (k = 0; k < i; k++)
        y += item_h(&m->items[k]);
    return y;
}

static int item_at(struct gui_menu *m, int y)
{
    int k, at = 2;
    for (k = 0; k < m->n; k++) {
        if (y >= at && y < at + item_h(&m->items[k]))
            return m->items[k].sep || m->items[k].off ? -1 : k;
        at += item_h(&m->items[k]);
    }
    return -1;
}

static void view_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct gui_menu *m = M.menu;
    int i, sc = 0;
    gfx_fill(s, w->r.x, w->r.y, w->r.w, w->r.h, GUI_FIELD);
    gfx_frame(s, w->r.x, w->r.y, w->r.w, w->r.h, GUI_LINE);
    if (!m)
        return;
    for (i = 0; i < m->n; i++)
        if (m->items[i].shortcut) {
            int k = gfx_text_width(gui_font, m->items[i].shortcut, -1);
            if (k > sc)
                sc = k;
        }
    for (i = 0; i < m->n; i++) {
        const struct item *it = &m->items[i];
        int y = item_y(m, i), hot = i == M.hot;
        unsigned fg = it->off ? GUI_DIM : hot ? GUI_FIELD : GUI_LINE;
        if (it->sep) {
            gfx_fill(s, w->r.x + 1, y + SEP_H / 2, w->r.w - 2, 1, GUI_DIM);
            continue;
        }
        if (hot)
            gfx_fill(s, w->r.x + 1, y, w->r.w - 2, GUI_ROW_H + 2, GUI_DARK);
        if (it->checked) {
            static const unsigned char tick[8] = { 0x01, 0x03, 0x86, 0xCC, 0x78, 0x30, 0x00, 0x00 };
            gfx_fill(s, w->r.x + 3, y + 2, 12, 12, hot ? GUI_DARK : GUI_FIELD);
            gfx_mono(s, w->r.x + 5, y + 5, tick, 8, 8, 1, fg, GFX_NONE);
        }
        gui_draw_text(s, w->r.x + 18, y + 2, gui_font, it->text, fg);
        if (it->shortcut)
            gfx_text(s, w->r.x + w->r.w - 10 - sc, y + 2, gui_font, it->shortcut, -1, fg);
    }
}

static void choose(int i);

static void step(int by)
{
    struct gui_menu *m = M.menu;
    int i, k = M.hot;
    if (!m || !m->n)
        return;
    for (i = 0; i < m->n; i++) {
        k = k < 0 ? (by > 0 ? 0 : m->n - 1) : (k + by + m->n) % m->n;
        if (!m->items[k].sep && !m->items[k].off)
            break;
    }
    M.hot = k;
    if (M.popup && M.popup->root)
        gui_damage(M.popup->root);
}

static void close_menu(void)
{
    struct gui_window *p = M.popup;
    if (!p)
        return;
    M.closed_at = gui_now();
    M.closed_from = M.bar ? M.bar->open : -1;
    if (M.bar) {
        M.bar->open = -1;
        gui_damage(&M.bar->w);
    }
    M.popup = NULL;
    M.menu = NULL;
    M.bar = NULL;
    M.choice = NULL;
    gui_close_later(p);
    if (M.owner && M.owner->id)
        glos_focus(M.owner->id);
}

int gui_choice_value(struct gui_widget *w, int set, int value);

static void choose(int i)
{
    struct gui_menu *m = M.menu;
    struct gui_window *owner = M.owner;
    struct gui_widget *choice = M.choice;
    int id;
    if (!m || i < 0 || i >= m->n || m->items[i].sep || m->items[i].off)
        return;
    id = m->items[i].id;
    close_menu();
    if (choice) {
        gui_choice_value(choice, 1, id);
        gui_send(choice, GUI_ON_SELECT);
    } else if (owner) {
        gui_post_command(owner, id);
    }
}

static int view_event(struct gui_widget *w, struct gui_ev *e)
{
    struct gui_menu *m = M.menu;
    int i;
    (void)w;
    if (!m)
        return 0;
    switch (e->type) {
    case GUI_EV_MOVE:
        i = item_at(m, e->y - w->r.y);
        if (i != M.hot && i >= 0) {
            M.hot = i;
            gui_damage(w);
        }
        return 1;
    case GUI_EV_PRESS:
        return 1;
    case GUI_EV_RELEASE:
        i = item_at(m, e->y - w->r.y);
        if (i >= 0)
            choose(i);
        return 1;
    case GUI_EV_KEY:
        break;
    default:
        return 0;
    }
    switch (e->key) {
    case GUI_K_UP: step(-1); return 1;
    case GUI_K_DOWN: step(1); return 1;
    case GUI_K_HOME: M.hot = -1; step(1); return 1;
    case GUI_K_END: M.hot = -1; step(-1); return 1;
    case GUI_K_ENTER:
    case GUI_K_KPENTER:
    case GUI_K_SPACE:
        choose(M.hot);
        return 1;
    case GUI_K_ESC:
        close_menu();
        return 1;
    case GUI_K_LEFT:
    case GUI_K_RIGHT:
        if (M.bar && M.bar->n > 1) {
            struct gui_menubar *b = M.bar;
            int to = (b->open + (e->key == GUI_K_RIGHT ? 1 : -1) + b->n) % b->n;
            close_menu();
            M.closed_from = -1;
            bar_open(b, to, 1);
        }
        return 1;
    case GUI_K_F10:
        close_menu();
        return 1;
    }
    if (e->ch) {                                /* an item's letter */
        unsigned c = e->ch >= 'A' && e->ch <= 'Z' ? e->ch + 32 : e->ch;
        for (i = 0; i < m->n; i++)
            if (m->items[i].mnemonic == c && !m->items[i].off && !m->items[i].sep) {
                choose(i);
                return 1;
            }
    }
    return 1;
}

static const struct gui_class view_class = { "menu", GUI_C_FOCUS, view_draw, view_event, NULL, NULL, NULL };

static void popdown(struct gui_window *w)
{
    if (w == M.popup)
        close_menu();
}

static void open_menu(struct gui_window *owner, struct gui_menu *m, int sx, int sy, struct gui_menubar *bar,
                      struct gui_widget *choice, int hot)
{
    struct gui_window *p;
    struct gui_widget *v;
    int i, tw = 60, sc = 0, h = 4, sw, shh;
    close_menu();
    for (i = 0; i < m->n; i++) {
        int k = gui_text_w(gui_font, m->items[i].text ? m->items[i].text : "");
        if (k > tw)
            tw = k;
        if (m->items[i].shortcut && (k = gfx_text_width(gui_font, m->items[i].shortcut, -1)) > sc)
            sc = k;
        h += item_h(&m->items[i]);
    }
    tw += 18 + 12 + (sc ? sc + 20 : 0);
    if (choice && tw < choice->r.w)
        tw = choice->r.w;
    gui_screen_size(&sw, &shh);
    if (sx + tw > sw)
        sx = sw - tw;
    if (sx < 0)
        sx = 0;
    if (sy + h > shh - 20) {                    /* (above the task bar: up, if it fits better there) */
        int above = sy - h - (bar ? bar_h() : choice ? choice->r.h : 0);
        sy = above >= 0 ? above : shh - 20 - h;
    }
    p = gui_window_at("", sx, sy, tw, h, GUI_W_POPUP);
    p->owner = owner;
    p->on_popdown = popdown;
    ((struct gui_box *)p->root)->bg = GUI_FIELD;
    v = gui_new(p->root, &view_class, sizeof(struct view));
    v->grow = 1;
    M.popup = p;
    M.owner = owner;
    M.menu = m;
    M.bar = bar;
    M.choice = choice;
    M.hot = hot;
    if (hot >= 0 && (m->items[hot].sep || m->items[hot].off)) {
        M.hot = -1;
        step(1);
    }
    gui_win_realize(p, sx, sy, 0);
    gui_show(p, 1);
    gui_focus(v);
}

void gui_popup_menu(struct gui_window *w, struct gui_menu *m, int x, int y)
{
    int ox, oy;
    if (gui_origin(w, &ox, &oy) == 0)
        open_menu(w, m, ox + x, oy + y, NULL, NULL, -1);
}

void gui_popdown_all(void) { close_menu(); }

/* The window's keys for its menu bar: shortcuts, Alt and a title's letter, F10. */
int gui_menubar_key(struct gui_window *w, struct gui_ev *e, int late)
{
    struct gui_menubar *b = w->menubar;
    unsigned mods = e->mods & (GLOS_MOD_CTRL | GLOS_MOD_ALT | GLOS_MOD_SHIFT);
    int i, k;
    if (!b || b->w.flags & GUI_HIDDEN)
        return 0;
    if (late != !(mods & (GLOS_MOD_CTRL | GLOS_MOD_ALT)))
        return 0;
    for (i = 0; i < b->n; i++)                  /* a shortcut */
        for (k = 0; k < b->menus[i]->n; k++) {
            struct item *it = &b->menus[i]->items[k];
            if (it->key && it->key == e->key && it->mods == mods && !it->off && !it->sep) {
                if (w->on_command)
                    w->on_command(w, it->id);
                return 1;
            }
        }
    if (late && e->key == GUI_K_F10 && !mods) {
        bar_open(b, 0, 1);
        return 1;
    }
    if (!late && mods == GLOS_MOD_ALT) {        /* Alt and a title's letter */
        static const char row[] = "\0\0" "1234567890" "\0\0\0\0" "qwertyuiop" "\0\0\0\0" "asdfghjkl" "\0\0\0\0\0" "zxcvbnm";
        unsigned c = e->key < sizeof row - 1 ? (unsigned char)row[e->key] : 0;
        for (i = 0; c && i < b->n; i++)
            if (b->menus[i]->mnemonic == c) {
                bar_open(b, i, 1);
                return 1;
            }
    }
    return 0;
}

/* ---- the choice */

struct choice {
    struct gui_widget w;
    int chars;
    struct gui_menu menu;               /* its items, ids 0, 1, ... */
    int value;
};

static void choice_measure(struct gui_widget *w, int *pw, int *ph)
{
    struct choice *c = (struct choice *)w;
    int i, mw = c->chars * gfx_text_width(gui_font, "n", 1);
    for (i = 0; i < c->menu.n; i++) {
        int k = gui_text_w(gui_font, c->menu.items[i].text);
        if (k > mw)
            mw = k;
    }
    *pw = mw + 12 + GUI_SB;
    *ph = GUI_FIELD_H;
}

static void choice_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct choice *c = (struct choice *)w;
    int on = !(w->flags & GUI_DISABLED);
    struct gfx_rect r = w->r;
    gui_draw_box(s, &r, GUI_FACE, 0);
    if (c->value >= 0 && c->value < c->menu.n)
        gui_text_fit(s, r.x + 5, r.y + (r.h - gui_font->height) / 2, r.w - GUI_SB - 8, gui_font,
                     c->menu.items[c->value].text, on ? GUI_LINE : GUI_DIM);
    gfx_fill(s, r.x + r.w - GUI_SB - 1, r.y + 3, 1, r.h - 6, GUI_DIM);
    gui_draw_arrow(s, r.x + r.w - GUI_SB / 2 - 1, r.y + r.h / 2, 1, on ? GUI_LINE : GUI_DIM);
    if (gui_is_focus(w)) {
        struct gfx_rect f = { r.x + 3, r.y + 3, r.w - GUI_SB - 6, r.h - 6 };
        gui_draw_focus(s, &f);
    }
}

static void choice_drop(struct choice *c)
{
    int ox, oy;
    if (!c->menu.n || gui_origin(c->w.win, &ox, &oy) != 0)
        return;
    open_menu(c->w.win, &c->menu, ox + c->w.r.x, oy + c->w.r.y + c->w.r.h, NULL, &c->w, c->value);
}

static int choice_event(struct gui_widget *w, struct gui_ev *e)
{
    struct choice *c = (struct choice *)w;
    if (e->type == GUI_EV_PRESS && e->button == 1) {
        choice_drop(c);
        return 1;
    }
    if (e->type != GUI_EV_KEY)
        return 0;
    if ((e->key == GUI_K_DOWN && e->mods & GLOS_MOD_ALT) || (e->key == GUI_K_SPACE && !e->mods) || e->key == 0x3E) {
        choice_drop(c);                         /* Alt+Down, Space, F4 */
        return 1;
    }
    if (e->mods & (GLOS_MOD_CTRL | GLOS_MOD_ALT))
        return 0;
    if ((e->key == GUI_K_UP || e->key == GUI_K_LEFT) && c->value > 0) {
        gui_choice_value(w, 1, c->value - 1);
        gui_send(w, GUI_ON_SELECT);
        return 1;
    }
    if ((e->key == GUI_K_DOWN || e->key == GUI_K_RIGHT) && c->value + 1 < c->menu.n) {
        gui_choice_value(w, 1, c->value + 1);
        gui_send(w, GUI_ON_SELECT);
        return 1;
    }
    if (e->key == GUI_K_UP || e->key == GUI_K_DOWN || e->key == GUI_K_LEFT || e->key == GUI_K_RIGHT)
        return 1;
    return 0;
}

static void choice_destroy(struct gui_widget *w)
{
    struct choice *c = (struct choice *)w;
    int i;
    for (i = 0; i < c->menu.n; i++)
        free(c->menu.items[i].text);
    free(c->menu.items);
}

const struct gui_class gui_choice_class = { "choice", GUI_C_FOCUS, choice_draw, choice_event, choice_measure, NULL,
                                            choice_destroy };

struct gui_widget *gui_choice(struct gui_widget *parent, int chars)
{
    struct choice *c = (struct choice *)gui_new(parent, &gui_choice_class, sizeof *c);
    c->chars = chars > 0 ? chars : 10;
    c->value = -1;
    return &c->w;
}

void gui_choice_add(struct gui_widget *w, const char *text)
{
    struct choice *c = (struct choice *)w;
    struct item *it = add(&c->menu);
    char *p;
    it->text = strdup(text);                    /* (no letters of its own: "&" is shown as "+") */
    for (p = it->text; p && *p; p++)
        if (*p == '&')
            *p = '+';
    it->id = c->menu.n - 1;
    if (c->value < 0)
        c->value = 0;
    gui_damage(w);
    if (w->win)
        gui_relayout(w->win);
}

void gui_choice_clear(struct gui_widget *w)
{
    struct choice *c = (struct choice *)w;
    choice_destroy(w);
    memset(&c->menu, 0, sizeof c->menu);
    c->value = -1;
    gui_damage(w);
}

int gui_choice_value(struct gui_widget *w, int set, int value)
{
    struct choice *c = (struct choice *)w;
    if (set && value >= 0 && value < c->menu.n && value != c->value) {
        c->value = value;
        gui_damage(w);
    }
    return c->value;
}
