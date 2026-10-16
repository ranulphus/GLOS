/* The toolkit's simple widgets (include/glos/gui.h): labels, buttons, check
 * boxes, radio buttons, forms, groups, progress bars, canvases, images, the
 * status bar, separators, spin boxes, sliders, tabs and the scrolling area. */
#include <stdio.h>

#include "guiint.h"

/* ---- labels */

struct label {
    struct gui_widget w;
    const struct gfx_font *font;
    unsigned rgb;
    int align;
};

static void label_measure(struct gui_widget *w, int *pw, int *ph)
{
    struct label *l = (struct label *)w;
    *pw = gui_text_w(l->font, gui_get_text(w));
    *ph = l->font->height;
}

static void label_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct label *l = (struct label *)w;
    int tw = gui_text_w(l->font, gui_get_text(w)), x = w->r.x;
    if (l->align == 0)
        x += (w->r.w - tw) / 2;
    else if (l->align > 0)
        x += w->r.w - tw;
    gui_draw_text(s, x, w->r.y + (w->r.h - l->font->height) / 2, l->font, gui_get_text(w),
                  w->flags & GUI_DISABLED ? GUI_DIM : l->rgb);
}

const struct gui_class gui_label_class = { "label", 0, label_draw, NULL, label_measure, NULL, NULL };

struct gui_widget *gui_label(struct gui_widget *parent, const char *text)
{
    struct label *l = (struct label *)gui_new(parent, &gui_label_class, sizeof *l);
    l->font = gui_font;
    l->rgb = GUI_LINE;
    l->align = -1;
    gui_set_text(&l->w, text);
    return &l->w;
}

void gui_label_style(struct gui_widget *w, const struct gfx_font *font, unsigned rgb, int align)
{
    struct label *l = (struct label *)w;
    if (font)
        l->font = font;
    l->rgb = rgb;
    l->align = align;
    gui_damage(w);
    if (w->win)
        gui_relayout(w->win);
}

/* ---- buttons */

struct button {
    struct gui_widget w;
    int down, inside;
};

static void button_measure(struct gui_widget *w, int *pw, int *ph)
{
    int tw = gui_text_w(gui_font, gui_get_text(w)) + 20;
    *pw = tw > GUI_BTN_W ? tw : GUI_BTN_W;
    *ph = GUI_BTN_H;
}

static void button_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct button *b = (struct button *)w;
    struct gfx_rect r = w->r;
    int pressed = b->down && b->inside, on = !(w->flags & GUI_DISABLED);
    int tw = gui_text_w(gui_font, gui_get_text(w));
    gfx_fill(s, r.x, r.y, r.w, r.h, GUI_FACE);
    if (w->flags & GUI_DEFAULT) {               /* (the default: a second line round it) */
        gui_frame(s, &r, GUI_LINE);
        r.x++, r.y++, r.w -= 2, r.h -= 2;
    }
    gui_draw_box(s, &r, GUI_FACE, pressed);
    gui_draw_text(s, r.x + (r.w - tw) / 2 + pressed, r.y + (r.h - gui_font->height) / 2 + pressed, gui_font,
                  gui_get_text(w), on ? GUI_LINE : GUI_DIM);
    if (gui_is_focus(w)) {
        struct gfx_rect f = { r.x + 3, r.y + 3, r.w - 6, r.h - 6 };
        gui_draw_focus(s, &f);
    }
}

static int inside(struct gui_widget *w, int x, int y)
{
    return x >= w->r.x && y >= w->r.y && x < w->r.x + w->r.w && y < w->r.y + w->r.h;
}

static int button_event(struct gui_widget *w, struct gui_ev *e)
{
    struct button *b = (struct button *)w;
    switch (e->type) {
    case GUI_EV_PRESS:
        if (e->button != 1)
            return 0;
        b->down = b->inside = 1;
        gui_damage(w);
        return 1;
    case GUI_EV_MOVE:
        if (b->down && inside(w, e->x, e->y) != b->inside) {
            b->inside = !b->inside;
            gui_damage(w);
        }
        return 1;
    case GUI_EV_RELEASE:
        if (!b->down)
            return 0;
        b->down = 0;
        gui_damage(w);
        if (b->inside)
            gui_send(w, GUI_ON_CLICK);
        return 1;
    case GUI_EV_KEY:
        if ((e->key == GUI_K_SPACE || e->key == GUI_K_ENTER || e->key == GUI_K_KPENTER) && !e->mods) {
            gui_send(w, GUI_ON_CLICK);
            return 1;
        }
        return 0;
    }
    return 0;
}

const struct gui_class gui_button_class = { "button", GUI_C_FOCUS, button_draw, button_event, button_measure, NULL, NULL };

struct gui_widget *gui_button(struct gui_widget *parent, const char *text, void (*on)(struct gui_widget *, int), int id)
{
    struct gui_widget *w = gui_new(parent, &gui_button_class, sizeof(struct button));
    gui_set_text(w, text);
    gui_on(w, on, id);
    return w;
}

/* ---- check boxes and radio buttons */

struct check {
    struct gui_widget w;
    int on, radio;
    struct gui_widget *group;           /* a radio button's group: its first */
};

static void check_measure(struct gui_widget *w, int *pw, int *ph)
{
    *pw = 13 + 6 + gui_text_w(gui_font, gui_get_text(w)) + 2;
    *ph = gui_font->height > 15 ? gui_font->height : 15;
}

static void check_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct check *c = (struct check *)w;
    int on = !(w->flags & GUI_DISABLED), y = w->r.y + (w->r.h - 13) / 2, tw;
    if (c->radio)
        gui_draw_radio(s, w->r.x, y, c->on, on);
    else
        gui_draw_check(s, w->r.x, y, c->on, on);
    gui_draw_text(s, w->r.x + 19, w->r.y + (w->r.h - gui_font->height) / 2, gui_font, gui_get_text(w),
                  on ? GUI_LINE : GUI_DIM);
    if (gui_is_focus(w)) {
        struct gfx_rect f;
        tw = gui_text_w(gui_font, gui_get_text(w));
        f.x = w->r.x + 17, f.y = w->r.y + (w->r.h - gui_font->height) / 2 - 1, f.w = tw + 4, f.h = gui_font->height + 2;
        gui_draw_focus(s, &f);
    }
}

static void radio_choose(struct check *c)
{
    struct gui_widget *k;
    if (c->on)
        return;
    for (k = c->w.parent ? c->w.parent->first : NULL; k; k = k->next) {
        struct check *o = (struct check *)k;
        if (k->cls == c->w.cls && o->radio && o->group == c->group && o->on) {
            o->on = 0;
            gui_damage(k);
        }
    }
    c->on = 1;
    gui_damage(&c->w);
    gui_send(&c->w, GUI_ON_CHANGE);
}

/* The next (by 1) or previous (-1) radio button of c's group among its siblings. */
static struct check *radio_step(struct check *c, int by)
{
    struct gui_widget *k = &c->w;
    for (;;) {
        k = by > 0 ? k->next : k->prev;
        if (!k)
            return NULL;
        if (k->cls == c->w.cls && ((struct check *)k)->radio && ((struct check *)k)->group == c->group
            && !(k->flags & (GUI_HIDDEN | GUI_DISABLED)))
            return (struct check *)k;
    }
}

static int check_event(struct gui_widget *w, struct gui_ev *e)
{
    struct check *c = (struct check *)w;
    if (e->type == GUI_EV_RELEASE && e->button == 1 && inside(w, e->x, e->y))
        goto toggle;
    if (e->type == GUI_EV_PRESS && e->button == 1)
        return 1;
    if (e->type != GUI_EV_KEY || e->mods & (GLOS_MOD_CTRL | GLOS_MOD_ALT)) {
        if (e->type == GUI_EV_KEY && e->mods & GLOS_MOD_ALT && w->mnemonic && e->key != GUI_K_TAB)
            return 0;
        return 0;
    }
    if (e->key == GUI_K_SPACE)
        goto toggle;
    if (c->radio && (e->key == GUI_K_DOWN || e->key == GUI_K_RIGHT || e->key == GUI_K_UP || e->key == GUI_K_LEFT)) {
        struct check *n = radio_step(c, e->key == GUI_K_DOWN || e->key == GUI_K_RIGHT ? 1 : -1);
        if (n) {
            gui_focus(&n->w);
            radio_choose(n);
        }
        return 1;
    }
    return 0;
toggle:
    if (c->radio) {
        radio_choose(c);
    } else {
        c->on = !c->on;
        gui_damage(w);
        gui_send(w, GUI_ON_CHANGE);
    }
    return 1;
}

static const struct gui_class check_class = { "check", GUI_C_FOCUS, check_draw, check_event, check_measure, NULL, NULL };

struct gui_widget *gui_check(struct gui_widget *parent, const char *text, int on)
{
    struct check *c = (struct check *)gui_new(parent, &check_class, sizeof *c);
    gui_set_text(&c->w, text);
    c->on = on != 0;
    return &c->w;
}

struct gui_widget *gui_radio(struct gui_widget *parent, const char *text, struct gui_widget *first)
{
    struct check *c = (struct check *)gui_new(parent, &check_class, sizeof *c);
    gui_set_text(&c->w, text);
    c->radio = 1;
    c->group = first ? first : &c->w;
    c->on = !first;
    if (first)
        c->w.flags |= GUI_NOTAB;                /* (Tab goes to the group's chosen one: arrows within it) */
    return &c->w;
}

int gui_checked(struct gui_widget *w) { return w->cls == &check_class && ((struct check *)w)->on; }

void gui_set_checked(struct gui_widget *w, int on)
{
    struct check *c = (struct check *)w;
    if (w->cls != &check_class)
        return;
    if (c->radio && on)
        radio_choose(c);
    else if (!c->radio && c->on != (on != 0)) {
        c->on = on != 0;
        gui_damage(w);
    }
    if (c->radio) {                             /* (Tab's stop: the chosen one) */
        struct gui_widget *k;
        for (k = w->parent ? w->parent->first : NULL; k; k = k->next)
            if (k->cls == &check_class && ((struct check *)k)->radio && ((struct check *)k)->group == c->group)
                k->flags = ((struct check *)k)->on ? k->flags & ~GUI_NOTAB : k->flags | GUI_NOTAB;
    }
}

/* ---- forms: rows of a label and a box */

static void form_measure(struct gui_widget *w, int *pw, int *ph)
{
    struct gui_widget *c;
    int lw = 0, fw = 0, h = 0, n = 0, a, b;
    for (c = w->first; c && c->next; c = c->next->next) {
        if (c->next->flags & GUI_HIDDEN)
            continue;
        gui_measure(c, &a, &b);
        if (a > lw)
            lw = a;
        gui_measure(c->next, &a, &b);
        if (a > fw)
            fw = a;
        h += b > gui_font->height ? b : gui_font->height;
        n++;
    }
    *pw = lw + GUI_GAP * 2 + fw;
    *ph = h + (n > 1 ? (n - 1) * GUI_GAP : 0);
}

static void form_layout(struct gui_widget *w)
{
    struct gui_widget *c;
    int lw = 0, a, b, y = w->r.y;
    for (c = w->first; c && c->next; c = c->next->next) {
        gui_measure(c, &a, &b);
        if (a > lw)
            lw = a;
    }
    for (c = w->first; c && c->next; c = c->next->next) {
        int h;
        if (c->next->flags & GUI_HIDDEN) {
            c->flags |= GUI_HIDDEN;
            continue;
        }
        c->flags &= ~GUI_HIDDEN;
        gui_measure(c->next, &a, &b);
        h = b > gui_font->height ? b : gui_font->height;
        gui_place(c, w->r.x, y, lw, h);
        gui_place(c->next, w->r.x + lw + GUI_GAP * 2, y, w->r.w - lw - GUI_GAP * 2, h);
        y += h + GUI_GAP;
    }
}

static const struct gui_class form_class = { "form", 0, NULL, NULL, form_measure, form_layout, NULL };

struct gui_widget *gui_form(struct gui_widget *parent) { return gui_new(parent, &form_class, sizeof(struct gui_widget)); }

struct gui_widget *gui_row(struct gui_widget *form, const char *label, struct gui_widget **label_out)
{
    struct gui_widget *l = gui_label(form, label), *b = gui_hbox(form, GUI_GAP, 0);
    if (label_out)
        *label_out = l;
    return b;
}

/* ---- groups: a frame with a title, a box in it */

static void group_measure(struct gui_widget *w, int *pw, int *ph)
{
    int a = 0, b = 0, tw = gui_text_w(gui_bold, gui_get_text(w)) + 16;
    if (w->first)
        gui_measure(w->first, &a, &b);
    *pw = (a > tw ? a : tw) + 2;
    *ph = b + gui_bold->height + 2;
}

static void group_layout(struct gui_widget *w)
{
    if (w->first)
        gui_place(w->first, w->r.x + 1, w->r.y + gui_bold->height + 1, w->r.w - 2, w->r.h - gui_bold->height - 2);
}

static void group_draw(struct gui_widget *w, struct gfx_surface *s)
{
    int top = w->r.y + gui_bold->height / 2, tw = gui_text_w(gui_bold, gui_get_text(w));
    gfx_frame(s, w->r.x, top, w->r.w, w->r.h - (top - w->r.y), GUI_LINE);
    gfx_fill(s, w->r.x + 6, w->r.y, tw + 6, gui_bold->height, GUI_FACE);
    gui_draw_text(s, w->r.x + 9, w->r.y, gui_bold, gui_get_text(w), GUI_LINE);
}

static const struct gui_class group_class = { "group", 0, group_draw, NULL, group_measure, group_layout, NULL };

struct gui_widget *gui_group(struct gui_widget *parent, const char *title)
{
    struct gui_widget *g = gui_new(parent, &group_class, sizeof *g);
    gui_set_text(g, title);
    return gui_vbox(g, GUI_GAP, GUI_PAD);
}

/* ---- progress bars */

struct progress {
    struct gui_widget w;
    int value, max;
};

static void progress_measure(struct gui_widget *w, int *pw, int *ph) { (void)w, *pw = 120, *ph = 14; }

static void progress_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct progress *p = (struct progress *)w;
    int fill = p->max > 0 ? (int)((long long)(w->r.w - 2) * p->value / p->max) : 0;
    gui_draw_field(s, &w->r, 1);
    if (fill > 0)
        gfx_fill(s, w->r.x + 1, w->r.y + 1, fill, w->r.h - 2, GUI_DARK);
}

static const struct gui_class progress_class = { "progress", 0, progress_draw, NULL, progress_measure, NULL, NULL };

struct gui_widget *gui_progress(struct gui_widget *parent, int max)
{
    struct progress *p = (struct progress *)gui_new(parent, &progress_class, sizeof *p);
    p->max = max;
    return &p->w;
}

void gui_progress_set(struct gui_widget *w, int value, int max)
{
    struct progress *p = (struct progress *)w;
    if (value < 0)
        value = 0;
    if (value > max)
        value = max;
    if (p->value != value || p->max != max) {
        p->value = value, p->max = max;
        gui_damage(w);
    }
}

/* ---- canvases: the app draws, and has the events */

struct canvas {
    struct gui_widget w;
    int cw, ch;
    void (*draw)(struct gui_widget *, struct gfx_surface *);
    int (*event)(struct gui_widget *, struct gui_ev *);
};

static void canvas_measure(struct gui_widget *w, int *pw, int *ph)
{
    struct canvas *c = (struct canvas *)w;
    *pw = c->cw, *ph = c->ch;
}

static void canvas_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct canvas *c = (struct canvas *)w;
    if (c->draw)
        c->draw(w, s);
}

static int canvas_event(struct gui_widget *w, struct gui_ev *e)
{
    struct canvas *c = (struct canvas *)w;
    return c->event ? c->event(w, e) : 0;
}

static const struct gui_class canvas_class = { "canvas", 0, canvas_draw, canvas_event, canvas_measure, NULL, NULL };
static const struct gui_class canvas_focus_class = { "canvas", GUI_C_FOCUS, canvas_draw, canvas_event, canvas_measure,
                                                     NULL, NULL };

struct gui_widget *gui_canvas(struct gui_widget *parent, int w, int h,
                              void (*draw)(struct gui_widget *, struct gfx_surface *),
                              int (*event)(struct gui_widget *, struct gui_ev *))
{
    struct canvas *c = (struct canvas *)gui_new(parent, event ? &canvas_focus_class : &canvas_class, sizeof *c);
    c->cw = w, c->ch = h;
    c->draw = draw;
    c->event = event;
    return &c->w;
}

/* ---- images */

struct image {
    struct gui_widget w;
    const unsigned *rgb;
    int iw, ih;
};

static void image_measure(struct gui_widget *w, int *pw, int *ph)
{
    struct image *i = (struct image *)w;
    *pw = i->iw, *ph = i->ih;
}

static void image_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct image *i = (struct image *)w;
    if (i->rgb)
        gfx_image(s, w->r.x + (w->r.w - i->iw) / 2, w->r.y + (w->r.h - i->ih) / 2, i->rgb, i->iw, i->ih, (unsigned)i->iw);
}

static const struct gui_class image_class = { "image", 0, image_draw, NULL, image_measure, NULL, NULL };

struct gui_widget *gui_image(struct gui_widget *parent, const unsigned *rgb, int w, int h)
{
    struct image *i = (struct image *)gui_new(parent, &image_class, sizeof *i);
    i->rgb = rgb, i->iw = w, i->ih = h;
    return &i->w;
}

/* ---- the status bar */

static void status_measure(struct gui_widget *w, int *pw, int *ph) { (void)w, *pw = 0, *ph = gui_font->height + 5; }

static void status_draw(struct gui_widget *w, struct gfx_surface *s)
{
    gfx_fill(s, w->r.x, w->r.y, w->r.w, w->r.h, GUI_FACE);
    gfx_fill(s, w->r.x, w->r.y, w->r.w, 1, GUI_LINE);
    gui_text_fit(s, w->r.x + 6, w->r.y + 3, w->r.w - 12, gui_font, gui_get_text(w), GUI_LINE);
}

const struct gui_class gui_status_class = { "status", 0, status_draw, NULL, status_measure, NULL, NULL };

struct gui_widget *gui_status(struct gui_window *win)
{
    struct gui_widget *w = gui_new(win->root, &gui_status_class, sizeof *w);
    gui_set_text(w, "");
    return w;
}

/* ---- separators */

static void sep_measure(struct gui_widget *w, int *pw, int *ph) { (void)w, *pw = 1, *ph = 1; }
static void sep_draw(struct gui_widget *w, struct gfx_surface *s) { gui_fill(s, &w->r, GUI_DIM); }
static const struct gui_class sep_class = { "sep", 0, sep_draw, NULL, sep_measure, NULL, NULL };

struct gui_widget *gui_sep(struct gui_widget *parent) { return gui_new(parent, &sep_class, sizeof(struct gui_widget)); }

/* ---- spin boxes and sliders */

struct number {
    struct gui_widget w;
    int min, max, value, slider, held, drag;
};

static void number_measure(struct gui_widget *w, int *pw, int *ph)
{
    struct number *n = (struct number *)w;
    char a[16], b[16];
    int wa, wb;
    if (n->slider) {
        *pw = 140, *ph = 20;
        return;
    }
    sprintf(a, "%d", n->min);
    sprintf(b, "%d", n->max);
    wa = gfx_text_width(gui_font, a, -1);
    wb = gfx_text_width(gui_font, b, -1);
    *pw = (wa > wb ? wa : wb) + 12 + GUI_SB;
    *ph = GUI_FIELD_H;
}

static int slider_x(struct number *n)
{
    int span = n->max - n->min, room = n->w.r.w - 10;
    return n->w.r.x + 5 + (span > 0 ? (int)((long long)room * (n->value - n->min) / span) : 0);
}

static void number_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct number *n = (struct number *)w;
    int on = !(w->flags & GUI_DISABLED);
    if (n->slider) {
        int cy = w->r.y + w->r.h / 2, x = slider_x(n);
        struct gfx_rect k = { x - 4, w->r.y + 2, 9, w->r.h - 4 };
        gfx_fill(s, w->r.x + 5, cy - 1, w->r.w - 10, 3, on ? GUI_SHADE : GUI_DIM);
        gui_draw_box(s, &k, GUI_FACE, n->drag);
        if (gui_is_focus(w)) {
            struct gfx_rect f = { w->r.x, w->r.y, w->r.w, w->r.h };
            gui_draw_focus(s, &f);
        }
    } else {
        char t[16];
        struct gfx_rect f = { w->r.x, w->r.y, w->r.w - GUI_SB + 1, w->r.h };
        struct gfx_rect up = { w->r.x + w->r.w - GUI_SB, w->r.y, GUI_SB, w->r.h / 2 + 1 };
        struct gfx_rect dn = { w->r.x + w->r.w - GUI_SB, w->r.y + w->r.h / 2, GUI_SB, w->r.h - w->r.h / 2 };
        gui_draw_field(s, &f, on);
        sprintf(t, "%d", n->value);
        gfx_text(s, f.x + 4, f.y + (f.h - gui_font->height) / 2, gui_font, t, -1, on ? GUI_LINE : GUI_DIM);
        gui_draw_box(s, &up, GUI_FACE, n->held == 1);
        gui_draw_box(s, &dn, GUI_FACE, n->held == 2);
        gui_draw_arrow(s, up.x + up.w / 2, up.y + up.h / 2, 0, on ? GUI_LINE : GUI_DIM);
        gui_draw_arrow(s, dn.x + dn.w / 2, dn.y + dn.h / 2, 1, on ? GUI_LINE : GUI_DIM);
        if (gui_is_focus(w)) {
            struct gfx_rect g = { f.x + 2, f.y + 2, f.w - 4, f.h - 4 };
            gui_draw_focus(s, &g);
        }
    }
}

static void number_set(struct number *n, int v, int tell)
{
    if (v < n->min)
        v = n->min;
    if (v > n->max)
        v = n->max;
    if (v != n->value) {
        n->value = v;
        gui_damage(&n->w);
        if (tell)
            gui_send(&n->w, GUI_ON_CHANGE);
    }
}

static int number_event(struct gui_widget *w, struct gui_ev *e)
{
    struct number *n = (struct number *)w;
    int step = n->slider ? ((n->max - n->min) / 20 > 1 ? (n->max - n->min) / 20 : 1) : 1;
    switch (e->type) {
    case GUI_EV_KEY:
        if (e->mods & (GLOS_MOD_CTRL | GLOS_MOD_ALT))
            return 0;
        switch (e->key) {
        case GUI_K_UP: case GUI_K_RIGHT: number_set(n, n->value + step, 1); return 1;
        case GUI_K_DOWN: case GUI_K_LEFT: number_set(n, n->value - step, 1); return 1;
        case GUI_K_PGUP: number_set(n, n->value + step * 10, 1); return 1;
        case GUI_K_PGDN: number_set(n, n->value - step * 10, 1); return 1;
        case GUI_K_HOME: number_set(n, n->min, 1); return 1;
        case GUI_K_END: number_set(n, n->max, 1); return 1;
        }
        if (!n->slider && e->ch >= '0' && e->ch <= '9') {       /* typed: digits on the end, or anew */
            long v = (long)n->value * 10 + (long)(e->ch - '0');
            if (v > n->max)
                v = (long)(e->ch - '0');
            number_set(n, (int)v, 1);
            return 1;
        }
        if (!n->slider && e->key == GUI_K_BACK) {
            number_set(n, n->value / 10, 1);
            return 1;
        }
        if (!n->slider && e->ch == '-' && n->min < 0) {
            number_set(n, -n->value, 1);
            return 1;
        }
        return 0;
    case GUI_EV_PRESS:
        if (e->button != 1)
            return 0;
        if (n->slider) {
            n->drag = 1;
            goto drag;
        }
        if (e->x >= w->r.x + w->r.w - GUI_SB) {
            n->held = e->y < w->r.y + w->r.h / 2 ? 1 : 2;
            number_set(n, n->value + (n->held == 1 ? 1 : -1), 1);
            gui_timer(w, 400);
            gui_damage(w);
        }
        return 1;
    case GUI_EV_MOVE:
        if (n->slider && n->drag)
            goto drag;
        return n->held != 0;
    case GUI_EV_RELEASE:
        n->drag = 0;
        n->held = 0;
        gui_timer(w, 0);
        gui_damage(w);
        return 1;
    case GUI_EV_TIMER:
        if (n->held) {
            number_set(n, n->value + (n->held == 1 ? 1 : -1), 1);
            gui_timer(w, 80);
        }
        return 1;
    }
    return 0;
drag:
    {
        int span = n->max - n->min, room = w->r.w - 10;
        if (room > 0)
            number_set(n, n->min + (int)(((long long)(e->x - w->r.x - 5) * span + room / 2) / room), 1);
        gui_damage(w);
    }
    return 1;
}

static const struct gui_class number_class = { "number", GUI_C_FOCUS, number_draw, number_event, number_measure, NULL,
                                               NULL };

static struct gui_widget *number(struct gui_widget *parent, int min, int max, int value, int slider)
{
    struct number *n = (struct number *)gui_new(parent, &number_class, sizeof *n);
    n->min = min, n->max = max < min ? min : max;
    n->slider = slider;
    n->value = min;
    number_set(n, value, 0);
    return &n->w;
}

struct gui_widget *gui_spin(struct gui_widget *parent, int min, int max, int value) { return number(parent, min, max, value, 0); }
struct gui_widget *gui_slider(struct gui_widget *parent, int min, int max, int value) { return number(parent, min, max, value, 1); }

/* gui_value and gui_set_value for every kind that has a number. */
int gui_choice_value(struct gui_widget *w, int set, int value);
extern const struct gui_class gui_choice_class;

int gui_value(struct gui_widget *w)
{
    if (w->cls == &number_class)
        return ((struct number *)w)->value;
    if (w->cls == &progress_class)
        return ((struct progress *)w)->value;
    if (w->cls == &gui_choice_class)
        return gui_choice_value(w, 0, 0);
    return 0;
}

void gui_set_value(struct gui_widget *w, int value)
{
    if (w->cls == &number_class)
        number_set((struct number *)w, value, 0);
    else if (w->cls == &progress_class)
        gui_progress_set(w, value, ((struct progress *)w)->max);
    else if (w->cls == &gui_choice_class)
        gui_choice_value(w, 1, value);
}

/* ---- tabs: a row of titles, and the chosen one's page under them */

struct tabs {
    struct gui_widget w;
    int at;
};

#define TAB_H 22

static void tabs_measure(struct gui_widget *w, int *pw, int *ph)
{
    struct gui_widget *c;
    int mw = 0, mh = 0, a, b, titles = 0;
    for (c = w->first; c; c = c->next) {
        int saved = c->flags;
        c->flags &= ~GUI_HIDDEN;                /* (the biggest page decides) */
        gui_measure(c, &a, &b);
        c->flags = (unsigned)saved;
        if (a > mw)
            mw = a;
        if (b > mh)
            mh = b;
        titles += gui_text_w(gui_font, gui_get_text(c)) + 16;
    }
    *pw = (mw > titles ? mw : titles) + 2;
    *ph = mh + TAB_H + 2;
}

static void tabs_layout(struct gui_widget *w)
{
    struct tabs *t = (struct tabs *)w;
    struct gui_widget *c;
    int i = 0;
    for (c = w->first; c; c = c->next, i++) {
        c->flags = i == t->at ? c->flags & ~GUI_HIDDEN : c->flags | GUI_HIDDEN;
        if (i == t->at)
            gui_place(c, w->r.x + 1, w->r.y + TAB_H + 1, w->r.w - 2, w->r.h - TAB_H - 2);
    }
}

static void tabs_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct tabs *t = (struct tabs *)w;
    struct gui_widget *c;
    int x = w->r.x, i = 0, sel_x = 0, sel_w = 0;
    gfx_fill(s, w->r.x, w->r.y, w->r.w, TAB_H, GUI_FACE);
    gfx_frame(s, w->r.x, w->r.y + TAB_H - 1, w->r.w, w->r.h - TAB_H + 1, GUI_LINE);
    for (c = w->first; c; c = c->next, i++) {
        int tw = gui_text_w(gui_font, gui_get_text(c)) + 16, on = i == t->at;
        int y = w->r.y + (on ? 0 : 3);
        gfx_fill(s, x, y, tw, TAB_H - (y - w->r.y), on ? GUI_FACE : GUI_FACE);
        gfx_frame(s, x, y, tw, TAB_H - (y - w->r.y) + (on ? 0 : 0), GUI_LINE);
        gui_draw_text(s, x + 8, y + (TAB_H - (y - w->r.y) - gui_font->height) / 2, on ? gui_bold : gui_font,
                      gui_get_text(c), GUI_LINE);
        if (on)
            sel_x = x, sel_w = tw;
        x += tw - 1;
    }
    if (sel_w)                                  /* (the chosen one opens into its page) */
        gfx_fill(s, sel_x + 1, w->r.y + TAB_H - 1, sel_w - 2, 1, GUI_FACE);
    if (gui_is_focus(w) && sel_w) {
        struct gfx_rect f = { sel_x + 3, w->r.y + 3, sel_w - 6, TAB_H - 6 };
        gui_draw_focus(s, &f);
    }
}

static void tabs_go(struct tabs *t, int at)
{
    int n = 0;
    struct gui_widget *c;
    for (c = t->w.first; c; c = c->next)
        n++;
    if (!n || at < 0 || at >= n || at == t->at)
        return;
    t->at = at;
    gui_damage(&t->w);
    if (t->w.win) {
        struct gui_widget *f = t->w.win->focus, *p;
        gui_relayout(t->w.win);
        for (p = f; p && p->parent != &t->w; p = p->parent)
            ;
        if (p && f != &t->w)                    /* (the focus was on the page that went: the tabs have it) */
            gui_focus(&t->w);
    }
    gui_send(&t->w, GUI_ON_SELECT);
}

void gui_tabs_step(struct gui_widget *w, int by)
{
    struct tabs *t = (struct tabs *)w;
    int n = 0;
    struct gui_widget *c;
    for (c = w->first; c; c = c->next)
        n++;
    if (n)
        tabs_go(t, (t->at + by + n) % n);
}

static int tabs_event(struct gui_widget *w, struct gui_ev *e)
{
    struct tabs *t = (struct tabs *)w;
    if (e->type == GUI_EV_KEY && !(e->mods & (GLOS_MOD_CTRL | GLOS_MOD_ALT))) {
        if (e->key == GUI_K_LEFT) {
            if (t->at > 0)
                tabs_go(t, t->at - 1);
            return 1;
        }
        if (e->key == GUI_K_RIGHT) {
            tabs_go(t, t->at + 1);
            return 1;
        }
        return 0;
    }
    if (e->type == GUI_EV_PRESS && e->button == 1 && e->y < w->r.y + TAB_H) {
        struct gui_widget *c;
        int x = w->r.x, i = 0;
        for (c = w->first; c; c = c->next, i++) {
            int tw = gui_text_w(gui_font, gui_get_text(c)) + 16;
            if (e->x >= x && e->x < x + tw) {
                tabs_go(t, i);
                break;
            }
            x += tw - 1;
        }
        return 1;
    }
    return 0;
}

const struct gui_class gui_tabs_class = { "tabs", GUI_C_FOCUS, tabs_draw, tabs_event, tabs_measure, tabs_layout, NULL };

struct gui_widget *gui_tabs(struct gui_widget *parent) { return gui_new(parent, &gui_tabs_class, sizeof(struct tabs)); }

struct gui_widget *gui_tab(struct gui_widget *tabs, const char *title)
{
    struct gui_widget *page = gui_vbox(tabs, GUI_GAP, GUI_PAD);
    gui_set_text(page, title);
    gui_set_mnemonic(page, "");                 /* (a page's title is no key of its own) */
    if (page != tabs->first)
        page->flags |= GUI_HIDDEN;
    return page;
}

int gui_tabs_get(struct gui_widget *tabs) { return ((struct tabs *)tabs)->at; }
void gui_tabs_set(struct gui_widget *tabs, int page) { tabs_go((struct tabs *)tabs, page); }

/* ---- the scrolling area: a vbox taller than the room it has, a bar at its right */

struct scroll {
    struct gui_widget w;
    struct gui_sb sb;
};

static void scroll_measure(struct gui_widget *w, int *pw, int *ph)
{
    int a = 0, b = 0;
    if (w->first)
        gui_measure(w->first, &a, &b);
    *pw = a + GUI_SB;
    *ph = b < 120 ? b : 120;
}

static void scroll_layout(struct gui_widget *w)
{
    struct scroll *s = (struct scroll *)w;
    int a = 0, b = 0;
    if (!w->first)
        return;
    gui_measure(w->first, &a, &b);
    s->sb.total = b;
    s->sb.page = w->r.h;
    gui_sb_clamp(&s->sb);
    s->sb.r.x = w->r.x + w->r.w - GUI_SB, s->sb.r.y = w->r.y, s->sb.r.w = GUI_SB, s->sb.r.h = w->r.h;
    if (b <= w->r.h)
        s->sb.r.w = 0;
    gui_place(w->first, w->r.x, w->r.y - s->sb.pos, w->r.w - (s->sb.r.w ? GUI_SB : 0), b > w->r.h ? b : w->r.h);
}

static void scroll_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct scroll *sc = (struct scroll *)w;
    gui_fill(s, &w->r, GUI_FACE);
    if (sc->sb.r.w)
        gui_sb_draw(s, &sc->sb, 1);
}

static int scroll_event(struct gui_widget *w, struct gui_ev *e)
{
    struct scroll *sc = (struct scroll *)w;
    int old = sc->sb.pos;
    if (gui_sb_event(w, &sc->sb, e, 16)) {
        if (sc->sb.pos != old && w->win)
            gui_relayout(w->win);
        return 1;
    }
    return 0;
}

static const struct gui_class scroll_class = { "scroll", 0, scroll_draw, scroll_event, scroll_measure, scroll_layout,
                                               NULL };

struct gui_widget *gui_scroll(struct gui_widget *parent)
{
    struct gui_widget *s = gui_new(parent, &scroll_class, sizeof(struct scroll));
    s->grow = 1;
    return gui_vbox(s, GUI_GAP, GUI_PAD);
}
