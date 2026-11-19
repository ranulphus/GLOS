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
    int align, boxed;
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
    int tw = gui_text_w(l->font, gui_get_text(w)), x = w->r.x, pad = l->boxed ? 6 : 0;
    if (l->boxed)
        gui_draw_field(s, &w->r, 1);
    if (l->align == 0)
        x += (w->r.w - tw) / 2;
    else if (l->align > 0)
        x += w->r.w - tw - pad;
    else
        x += pad;
    gui_draw_text(s, x, w->r.y + (w->r.h - l->font->height) / 2, l->font, gui_get_text(w),
                  w->flags & GUI_DISABLED ? GUI_DIM : l->rgb != GFX_NONE ? l->rgb : l->boxed ? GUI_FIELD_TEXT : GUI_TEXT);
}

const struct gui_class gui_label_class = { "label", 0, label_draw, NULL, label_measure, NULL, NULL };

struct gui_widget *gui_label(struct gui_widget *parent, const char *text)
{
    struct label *l = (struct label *)gui_new(parent, &gui_label_class, sizeof *l);
    l->font = gui_font;
    l->rgb = GFX_NONE;                  /* (the theme's text, whatever the theme is when it is drawn) */
    l->align = -1;
    gui_set_text(&l->w, text);
    return &l->w;
}

void gui_label_box(struct gui_widget *w, int on)
{
    ((struct label *)w)->boxed = on;
    gui_damage(w);
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
                  gui_get_text(w), on ? GUI_TEXT : GUI_DIM);
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
                  on ? GUI_TEXT : GUI_DIM);
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
    gui_draw_text(s, w->r.x + 9, w->r.y, gui_bold, gui_get_text(w), GUI_TEXT);
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

/* Sections (toolkit 2, step C2): section 0 is the bar's own text, with what the others leave; the others are at
   the right end, each as wide as it was made, with a text, an icon before it, or a small progress bar. */
#define STATUS_MAX 8

struct status {
    struct gui_widget w;
    struct {
        int width;
        char *text;
        const unsigned char *icon;
        int value, max;
    } sec[STATUS_MAX];
    int n;                              /* sections after the first */
};

static void status_measure(struct gui_widget *w, int *pw, int *ph)
{
    struct status *t = (struct status *)w;
    int h = gui_font->height + 5;
    *pw = 0;
    *ph = t->n && h < GUI_ICON_W + 4 ? GUI_ICON_W + 4 : h;
}

static void status_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct status *t = (struct status *)w;
    int right = w->r.x + w->r.w, i, ty = w->r.y + 1 + (w->r.h - 1 - gui_font->height) / 2;
    gfx_fill(s, w->r.x, w->r.y, w->r.w, w->r.h, GUI_FACE);
    gfx_fill(s, w->r.x, w->r.y, w->r.w, 1, GUI_LINE);
    for (i = t->n; i >= 1; i--) {               /* from the right end */
        int x = right - t->sec[i].width, tx = x + 6;
        if (x < w->r.x + 40)                    /* (a narrow window: the sections that fit) */
            break;
        gfx_fill(s, x, w->r.y + 3, 1, w->r.h - 5, GUI_DIM);
        if (t->sec[i].max > 0) {
            int bw = t->sec[i].width - 12, v = t->sec[i].value < 0 ? 0 : t->sec[i].value;
            if (v > t->sec[i].max)
                v = t->sec[i].max;
            gfx_frame(s, tx, w->r.y + 4, bw, w->r.h - 7, GUI_LINE);
            gfx_fill(s, tx + 1, w->r.y + 5, (bw - 2) * v / t->sec[i].max, w->r.h - 9, GUI_DARK);
        } else {
            if (t->sec[i].icon) {
                gui_draw_icon(s, tx, w->r.y + 1 + (w->r.h - 1 - GUI_ICON_W) / 2, t->sec[i].icon, GUI_ICON_W);
                tx += GUI_ICON_W + 4;
            }
            if (t->sec[i].text)
                gui_text_fit(s, tx, ty, right - tx - 4, gui_font, t->sec[i].text, GUI_TEXT);
        }
        right = x;
    }
    gui_text_fit(s, w->r.x + 6, t->n ? ty : w->r.y + 3, right - w->r.x - 12, gui_font, gui_get_text(w), GUI_TEXT);
}

static void status_destroy(struct gui_widget *w)
{
    struct status *t = (struct status *)w;
    int i;
    for (i = 1; i <= t->n; i++)
        free(t->sec[i].text);
}

const struct gui_class gui_status_class = { "status", 0, status_draw, NULL, status_measure, NULL, status_destroy };

struct gui_widget *gui_status(struct gui_window *win)
{
    struct gui_widget *w = gui_new(win->root, &gui_status_class, sizeof(struct status));
    gui_set_text(w, "");
    return w;
}

int gui_status_section(struct gui_widget *w, int width)
{
    struct status *t = (struct status *)w;
    if (t->n + 1 >= STATUS_MAX)
        return 0;
    t->sec[++t->n].width = width;
    if (w->win)
        gui_relayout(w->win);
    gui_damage(w);
    return t->n;
}

void gui_status_set(struct gui_widget *w, int section, const char *text, const unsigned char *icon)
{
    struct status *t = (struct status *)w;
    if (section == 0) {
        gui_set_text(w, text ? text : "");
        return;
    }
    if (section < 1 || section > t->n)
        return;
    free(t->sec[section].text);
    t->sec[section].text = text ? strdup(text) : NULL;
    t->sec[section].icon = icon;
    t->sec[section].max = 0;
    gui_damage(w);
}

void gui_status_progress(struct gui_widget *w, int section, int value, int max)
{
    struct status *t = (struct status *)w;
    if (section < 1 || section > t->n)
        return;
    t->sec[section].value = value, t->sec[section].max = max;
    gui_damage(w);
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
    unsigned char typing, minus;        /* a spin: a number is being typed (the next digit goes on its end; else the
                                           first digit is the number anew); a minus sign came before its digits */
    unsigned style;                     /* (toolkit 2) a slider's GUI_SLIDER_ flags */
    int marks;                          /* ... a mark every so many (0: none) */
    void (*fmt)(int value, char *out, int room);        /* ... its value as text (NULL: the number) */
};

#define VTRACK 140                      /* a vertical slider's length */

static void value_text(const struct number *n, char *out, int room)
{
    if (n->fmt)
        n->fmt(n->value, out, room);
    else
        snprintf(out, (size_t)room, "%d", n->value);
}

static int value_w(const struct number *n)     /* the room the value takes beside (or under) the track */
{
    char a[32], b[32];
    struct number m = *n;
    int wa, wb;
    if (!(n->style & GUI_SLIDER_VALUE))
        return 0;
    m.value = n->min, value_text(&m, a, sizeof a);
    m.value = n->max, value_text(&m, b, sizeof b);
    wa = gfx_text_width(gui_font, a, -1), wb = gfx_text_width(gui_font, b, -1);
    return (wa > wb ? wa : wb) + 6;
}

static void number_measure(struct gui_widget *w, int *pw, int *ph)
{
    struct number *n = (struct number *)w;
    char a[16], b[16];
    int wa, wb;
    if (n->slider) {
        if (n->style & GUI_SLIDER_VERTICAL)     /* (the value under the track) */
            *pw = 20 > value_w(n) ? 20 : value_w(n), *ph = VTRACK + (n->style & GUI_SLIDER_VALUE ? gui_font->height + 4 : 0);
        else
            *pw = 140 + value_w(n), *ph = n->marks ? 26 : 20;
        return;
    }
    sprintf(a, "%d", n->min);
    sprintf(b, "%d", n->max);
    wa = gfx_text_width(gui_font, a, -1);
    wb = gfx_text_width(gui_font, b, -1);
    *pw = (wa > wb ? wa : wb) + 12 + GUI_SB;
    *ph = GUI_FIELD_H;
}

static int track_len(struct number *n)          /* the track's length less the knob's travel margins */
{
    if (n->style & GUI_SLIDER_VERTICAL)
        return VTRACK - 10;
    return n->w.r.w - value_w(n) - 10;
}

static int slider_x(struct number *n)           /* (vertical: its y; the minimum at the bottom) */
{
    int span = n->max - n->min, room = track_len(n), at = span > 0 ? (int)((long long)room * (n->value - n->min) / span) : 0;
    if (n->style & GUI_SLIDER_VERTICAL)
        return n->w.r.y + 5 + room - at;
    return n->w.r.x + 5 + at;
}

static void number_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct number *n = (struct number *)w;
    int on = !(w->flags & GUI_DISABLED);
    if (n->slider && n->style & GUI_SLIDER_VERTICAL) {
        int cx = w->r.x + w->r.w / 2, y = slider_x(n), m;
        struct gfx_rect k = { cx - 8, y - 4, 17, 9 };
        gfx_fill(s, cx - 1, w->r.y + 5, 3, VTRACK - 10, on ? GUI_SHADE : GUI_DIM);
        for (m = n->min; n->marks > 0 && m <= n->max; m += n->marks) {     /* marks beside the track */
            int span = n->max - n->min, my = w->r.y + 5 + track_len(n) - (span ? track_len(n) * (m - n->min) / span : 0);
            gfx_fill(s, cx + 6, my, 4, 1, GUI_DIM);
        }
        gui_draw_box(s, &k, GUI_FACE, n->drag);
        if (n->style & GUI_SLIDER_VALUE) {
            char t[32];
            value_text(n, t, sizeof t);
            gfx_text(s, w->r.x + (w->r.w - gfx_text_width(gui_font, t, -1)) / 2, w->r.y + VTRACK + 2, gui_font, t, -1,
                     on ? GUI_TEXT : GUI_DIM);
        }
        if (gui_is_focus(w)) {
            struct gfx_rect f = { w->r.x, w->r.y, w->r.w, VTRACK };
            gui_draw_focus(s, &f);
        }
    } else if (n->slider) {
        int th = n->marks ? 20 : w->r.h, cy = w->r.y + th / 2, x = slider_x(n), m, tw = w->r.w - value_w(n);
        struct gfx_rect k = { x - 4, w->r.y + 2, 9, th - 4 };
        gfx_fill(s, w->r.x + 5, cy - 1, tw - 10, 3, on ? GUI_SHADE : GUI_DIM);
        for (m = n->min; n->marks > 0 && m <= n->max; m += n->marks) {     /* marks under the track */
            int span = n->max - n->min, mx = w->r.x + 5 + (span ? track_len(n) * (m - n->min) / span : 0);
            gfx_fill(s, mx, w->r.y + th + 1, 1, 4, GUI_DIM);
        }
        gui_draw_box(s, &k, GUI_FACE, n->drag);
        if (n->style & GUI_SLIDER_VALUE) {
            char t[32];
            value_text(n, t, sizeof t);
            gfx_text(s, w->r.x + tw + 4, w->r.y + (th - gui_font->height) / 2, gui_font, t, -1, on ? GUI_TEXT : GUI_DIM);
        }
        if (gui_is_focus(w)) {
            struct gfx_rect f = { w->r.x, w->r.y, tw, th };
            gui_draw_focus(s, &f);
        }
    } else {
        char t[16];
        struct gfx_rect f = { w->r.x, w->r.y, w->r.w - GUI_SB + 1, w->r.h };
        struct gfx_rect up = { w->r.x + w->r.w - GUI_SB, w->r.y, GUI_SB, w->r.h / 2 + 1 };
        struct gfx_rect dn = { w->r.x + w->r.w - GUI_SB, w->r.y + w->r.h / 2, GUI_SB, w->r.h - w->r.h / 2 };
        gui_draw_field(s, &f, on);
        sprintf(t, "%d", n->value);
        gfx_text(s, f.x + 4, f.y + (f.h - gui_font->height) / 2, gui_font, t, -1, on ? GUI_FIELD_TEXT : GUI_DIM);
        gui_draw_box(s, &up, GUI_FACE, n->held == 1);
        gui_draw_box(s, &dn, GUI_FACE, n->held == 2);
        gui_draw_arrow(s, up.x + up.w / 2, up.y + up.h / 2, 0, on ? GUI_TEXT : GUI_DIM);
        gui_draw_arrow(s, dn.x + dn.w / 2, dn.y + dn.h / 2, 1, on ? GUI_TEXT : GUI_DIM);
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
    case GUI_EV_FOCUS:
    case GUI_EV_BLUR:
        n->typing = n->minus = 0;               /* (a number typed is typed from its first digit) */
        return 0;
    case GUI_EV_KEY:
        if (e->mods & (GLOS_MOD_CTRL | GLOS_MOD_ALT))
            return 0;
        if (!n->slider && e->ch >= '0' && e->ch <= '9') {
            /* A number typed: its first digit is the number anew (as text chosen is typed over), the next ones
               go on its end; a minus sign before them makes it negative. Until 2026-11-19 every digit went on
               the end of the value that was there (5, then "40" typed, was 0), and a minus sign only changed
               the sign of what was there ("-50" over 100 was -100). */
            long d = e->ch - '0', v;
            if (!n->typing) {
                v = n->minus ? -d : d;
            } else {
                v = (long)n->value * 10 + (n->value < 0 || (n->minus && !n->value) ? -d : d);
                if (v > n->max || v < n->min)
                    v = n->minus ? -d : d;
            }
            n->typing = 1;
            number_set(n, (int)v, 1);
            return 1;
        }
        if (!n->slider && e->ch == '-' && n->min < 0) {
            if (n->typing && n->value) {        /* (among the digits: the sign of what is typed) */
                number_set(n, -n->value, 1);
                n->minus = n->value < 0;
            } else {
                n->minus = !n->minus;
            }
            return 1;
        }
        if (!n->slider && e->key == GUI_K_BACK) {
            number_set(n, n->value / 10, 1);
            n->typing = 1;
            return 1;
        }
        n->typing = n->minus = 0;               /* (any other key: the next digit starts a number) */
        switch (e->key) {
        case GUI_K_UP: case GUI_K_RIGHT: number_set(n, n->value + step, 1); return 1;
        case GUI_K_DOWN: case GUI_K_LEFT: number_set(n, n->value - step, 1); return 1;
        case GUI_K_PGUP: number_set(n, n->value + step * 10, 1); return 1;
        case GUI_K_PGDN: number_set(n, n->value - step * 10, 1); return 1;
        case GUI_K_HOME: number_set(n, n->min, 1); return 1;
        case GUI_K_END: number_set(n, n->max, 1); return 1;
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
        int span = n->max - n->min, room = track_len(n);
        int along = n->style & GUI_SLIDER_VERTICAL ? room - (e->y - w->r.y - 5) : e->x - w->r.x - 5;
        if (room > 0)
            number_set(n, n->min + (int)(((long long)along * span + room / 2) / room), 1);
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

void gui_slider_style(struct gui_widget *w, unsigned style, int marks, void (*fmt)(int value, char *out, int room))
{
    struct number *n = (struct number *)w;
    n->style = style, n->marks = marks > 0 ? marks : 0, n->fmt = fmt;
    gui_damage(w);
    if (w->win)
        gui_relayout(w->win);
}

/* gui_value and gui_set_value for every kind that has a number. */
int gui_choice_value(struct gui_widget *w, int set, int value);
extern const struct gui_class gui_choice_class;

/* (res.c) what a resource file has of these */
void gui_res_check(struct gui_widget *w, int *radio, struct gui_widget **group)
{
    *radio = ((struct check *)w)->radio, *group = ((struct check *)w)->group;
}

void gui_res_number(struct gui_widget *w, int *slider, int *min, int *max)
{
    struct number *n = (struct number *)w;
    *slider = n->slider, *min = n->min, *max = n->max;
}

void gui_res_number_range(struct gui_widget *w, int min, int max)
{
    struct number *n = (struct number *)w;
    n->min = min, n->max = max < min ? min : max;
    number_set(n, n->value, 0);
    gui_damage(w);
}

int gui_res_progress_max(struct gui_widget *w) { return ((struct progress *)w)->max; }

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

/* ---- tabs: titles, and the chosen one's page under them (or beside them). Titles that don't fit in a row go
 * into more rows (GUI_TABS_ROWS, the default: in their order, the rows fixed), stay in one row with two arrows
 * that scroll it (GUI_TABS_SCROLL), or are a list down the left (GUI_TABS_LIST). Each is laid out for the width
 * the tabs have, so a wider window takes fewer rows or scrolls less. */

struct tabs {
    struct gui_widget w;
    int at;
    int style;
    int first;                          /* GUI_TABS_SCROLL, GUI_TABS_LIST: the first title shown */
    struct gui_sb sb;                   /* GUI_TABS_LIST taller than its room: its scroll bar */
    unsigned flags;                     /* GUI_TABS_CLOSE, _MOVE, _NEW, _MENU (toolkit 2) */
    int closing;                        /* the page whose close was asked (GUI_ON_CLOSE) */
    int press, press_x, moving, target; /* a title held (-1: none), and, dragged along, where it would go */
    int out, drop_x, drop_y, dropped;   /* GUI_TABS_DRAG: held well out of the strip; where it was let go */
    struct gui_menu *menu;              /* GUI_TABS_MENU: every title, dropped from the strip's end */
};

#define TAB_H    (gui_font->height + 8)  /* (22 with the normal font) */
#define TAB_LINE (TAB_H - 1)            /* GUI_TABS_LIST: a title's height in the list */
#define TABS_MAX 64                     /* titles placed (more are neither drawn nor clicked) */
#define CLOSE_W  14                     /* GUI_TABS_CLOSE: a title's close box */
#define BTN_W    (TAB_H - 4)            /* the "+" and the list button at the strip's end */

static int tab_w(struct tabs *t, struct gui_widget *c)  /* (room for the title in bold too, as the chosen tab has it) */
{
    int plain = gui_text_w(gui_font, gui_get_text(c)) + 16, bold = gui_text_w(gui_bold, gui_get_text(c)) + 10;
    return (plain > bold ? plain : bold) + (t->flags & GUI_TABS_CLOSE && t->style != GUI_TABS_LIST ? CLOSE_W : 0);
}

static int extras(struct tabs *t)               /* the "+" and the list button's room at the strip's right end */
{
    if (t->style == GUI_TABS_LIST)
        return 0;
    return (t->flags & GUI_TABS_NEW ? BTN_W : 0) + (t->flags & GUI_TABS_MENU ? BTN_W : 0);
}

/* GUI_TABS_SCROLL with more than fits: the arrows' room at the strip's right. */
static int tabs_arrows(struct tabs *t, int gw)
{
    struct gui_widget *c;
    int x = 0;
    if (t->style != GUI_TABS_SCROLL)
        return 0;
    for (c = t->w.first; c; c = c->next)
        x += tab_w(t, c) - 1;
    return x + 1 > gw - extras(t) ? 2 * GUI_SB : 0;
}

/* Each title's place for tabs at gx, gy, gw wide and gh high, into r[] (w 0: not shown), and the room the titles
 * take: the strip's height above the page, or GUI_TABS_LIST's width beside it (with its scroll bar, if any). */
static int tabs_geom(struct tabs *t, int gx, int gy, int gw, int gh, struct gfx_rect *r)
{
    struct gui_widget *c;
    int sw = gw - extras(t), i = 0, x = 0, row = 0, room = sw - tabs_arrows(t, gw), first = room < sw ? t->first : 0;
    int lw = 0, n = 0, fit;
    if (t->style == GUI_TABS_LIST) {
        for (c = t->w.first; c; c = c->next, n++)
            if (tab_w(t, c) > lw)
                lw = tab_w(t, c);
        fit = gh > TAB_H ? (gh - 1) / TAB_LINE : 1;
        first = n > fit ? t->first : 0;
        for (c = t->w.first; c && i < TABS_MAX; c = c->next, i++) {
            r[i].x = gx, r[i].y = gy + (i - first) * TAB_LINE, r[i].w = lw, r[i].h = TAB_H;
            if (i < first || i - first >= fit)
                r[i].w = 0;                     /* (scrolled out of the list) */
        }
        return lw + (n > fit ? GUI_SB : 0);
    }
    for (c = t->w.first; c && i < TABS_MAX; c = c->next, i++) {
        int tw = tab_w(t, c);
        r[i].w = 0;
        if (t->style == GUI_TABS_SCROLL) {
            if (i < first || (i > first && x + tw > room))
                continue;                       /* (scrolled out of the strip; the first shown even if too wide) */
        } else if (x > 0 && x + tw > sw) {
            row++;                              /* GUI_TABS_ROWS: the next row */
            x = 0;
        }
        r[i].x = gx + x, r[i].y = gy + row * TAB_H, r[i].w = i == first && tw > room ? room : tw, r[i].h = TAB_H;
        x += tw - 1;
    }
    return (row + 1) * TAB_H;
}

static void tabs_measure(struct gui_widget *w, int *pw, int *ph)
{
    struct tabs *t = (struct tabs *)w;
    struct gfx_rect r[TABS_MAX];
    struct gui_widget *c;
    int mw = 0, mh = 0, a, b, widest = 0, n = 0, room;
    for (c = w->first; c; c = c->next, n++) {
        int saved = c->flags;
        c->flags &= ~GUI_HIDDEN;                /* (the biggest page decides) */
        gui_measure(c, &a, &b);
        c->flags = (unsigned)saved;
        if (a > mw)
            mw = a;
        if (b > mh)
            mh = b;
        if (tab_w(t, c) > widest)
            widest = tab_w(t, c);
    }
    widest += extras(t);
    if (t->style == GUI_TABS_LIST) {
        room = tabs_geom(t, 0, 0, 0, 1 << 20, r);
        *pw = room + GUI_GAP + mw + 2;
        *ph = (mh + 2 > n * TAB_LINE + 1 ? mh + 2 : n * TAB_LINE + 1);
        return;
    }
    if (t->style == GUI_TABS_SCROLL)
        widest += 2 * GUI_SB;
    *pw = (mw > widest ? mw : widest) + 2;      /* (the pages' width: the titles wrap or scroll in it) */
    *ph = mh + tabs_geom(t, 0, 0, *pw, 0, r) + 2;
}

/* GUI_TABS_SCROLL, GUI_TABS_LIST: the chosen title in the strip or the list. */
static void tabs_show(struct tabs *t)
{
    struct gfx_rect r[TABS_MAX];
    if (t->at < t->first)
        t->first = t->at;
    for (;;) {
        tabs_geom(t, t->w.r.x, t->w.r.y, t->w.r.w, t->w.r.h, r);
        if (t->at >= TABS_MAX || r[t->at].w || t->first >= t->at)
            break;
        t->first++;
    }
}

static void tabs_layout(struct gui_widget *w)
{
    struct tabs *t = (struct tabs *)w;
    struct gfx_rect r[TABS_MAX];
    struct gui_widget *c;
    int i = 0, room, n = 0;
    if (t->style != GUI_TABS_ROWS)
        tabs_show(t);
    room = tabs_geom(t, w->r.x, w->r.y, w->r.w, w->r.h, r);
    t->sb.r.w = 0;
    for (c = w->first; c; c = c->next)
        n++;
    if (t->style == GUI_TABS_LIST && n > (w->r.h - 1) / TAB_LINE) {
        t->sb.total = n, t->sb.page = (w->r.h - 1) / TAB_LINE, t->sb.pos = t->first;
        t->sb.r.x = w->r.x + room - GUI_SB, t->sb.r.y = w->r.y, t->sb.r.w = GUI_SB, t->sb.r.h = w->r.h;
    }
    for (c = w->first; c; c = c->next, i++) {
        c->flags = i == t->at ? c->flags & ~GUI_HIDDEN : c->flags | GUI_HIDDEN;
        if (i != t->at)
            continue;
        if (t->style == GUI_TABS_LIST)
            gui_place(c, w->r.x + room + GUI_GAP + 1, w->r.y + 1, w->r.w - room - GUI_GAP - 2, w->r.h - 2);
        else
            gui_place(c, w->r.x + 1, w->r.y + room + 1, w->r.w - 2, w->r.h - room - 2);
    }
}

static void tabs_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct tabs *t = (struct tabs *)w;
    struct gfx_rect r[TABS_MAX], sel = { 0, 0, 0, 0 };
    struct gui_widget *c;
    int i = 0, room = tabs_geom(t, w->r.x, w->r.y, w->r.w, w->r.h, r), arrows = tabs_arrows(t, w->r.w), n = 0;
    int close = t->flags & GUI_TABS_CLOSE && t->style != GUI_TABS_LIST ? CLOSE_W : 0;
    if (t->style == GUI_TABS_LIST) {
        struct gfx_rect l = { w->r.x, w->r.y, room - t->sb.r.w + (t->sb.r.w ? 1 : 0), w->r.h };
        int px = w->r.x + room + GUI_GAP;
        gui_draw_field(s, &l, 1);
        if (t->sb.r.w)
            gui_sb_draw(s, &t->sb, 1);
        gfx_fill(s, l.x + l.w, w->r.y, GUI_GAP, w->r.h, GUI_FACE);
        gfx_frame(s, px, w->r.y, w->r.w - (px - w->r.x), w->r.h, GUI_LINE);
        for (c = w->first; c && i < TABS_MAX; c = c->next, i++) {
            int on = i == t->at;
            struct gfx_rect a = { r[i].x + 1, r[i].y + 1, r[i].w - 2, TAB_LINE - 1 };
            if (!r[i].w)
                continue;
            if (on) {
                gfx_fill(s, a.x, a.y, a.w, a.h, GUI_CHOSEN);
                sel = a;
            }
            gui_draw_text(s, a.x + 7, a.y + (a.h - gui_font->height) / 2, on ? gui_bold : gui_font, gui_get_text(c),
                          on ? GUI_CHOSEN_TEXT : GUI_TEXT);
        }
        if (gui_is_focus(w) && sel.w) {
            struct gfx_rect f = { sel.x + 1, sel.y + 1, sel.w - 2, sel.h - 2 };
            gui_draw_focus(s, &f);
        }
        return;
    }
    gfx_fill(s, w->r.x, w->r.y, w->r.w, room, GUI_FACE);
    gfx_frame(s, w->r.x, w->r.y + room - 1, w->r.w, w->r.h - room + 1, GUI_LINE);
    for (c = w->first; c && i < TABS_MAX; c = c->next, i++) {
        int on = i == t->at, y = r[i].y + (on ? 0 : 3);
        if (!r[i].w)
            continue;
        n = i;
        gfx_fill(s, r[i].x, y, r[i].w, r[i].y + TAB_H - y, t->moving && i == t->press ? GUI_SHADE : GUI_FACE);
        gfx_frame(s, r[i].x, y, r[i].w, r[i].y + TAB_H - y, GUI_LINE);
        if (r[i].w < tab_w(t, c))               /* (GUI_TABS_SCROLL's one title wider than the strip) */
            gui_text_fit(s, r[i].x + 8, y + (r[i].y + TAB_H - y - gui_font->height) / 2, r[i].w - 12 - close,
                         on ? gui_bold : gui_font, gui_get_text(c), GUI_TEXT);
        else
            gui_draw_text(s, r[i].x + 8, y + (r[i].y + TAB_H - y - gui_font->height) / 2, on ? gui_bold : gui_font,
                          gui_get_text(c), GUI_TEXT);
        if (close) {                            /* a small cross at the title's right */
            int bx = r[i].x + r[i].w - CLOSE_W + 1, by = y + (r[i].y + TAB_H - y) / 2 - 3;
            gfx_line(s, bx, by, bx + 6, by + 6, on ? GUI_TEXT : GUI_DIM);
            gfx_line(s, bx + 6, by, bx, by + 6, on ? GUI_TEXT : GUI_DIM);
        }
        if (on)
            sel = r[i];
    }
    if (t->moving && !t->out && t->target != t->press && t->target < TABS_MAX && r[t->target].w) {    /* where it would go */
        int bx = t->target < t->press ? r[t->target].x : r[t->target].x + r[t->target].w - 1;
        gfx_fill(s, bx - 1, r[t->target].y, 3, TAB_H, GUI_MARK);
    }
    if (extras(t)) {                            /* the "+" and the list button */
        int ex = w->r.x + w->r.w - extras(t), ey = w->r.y + 3;
        if (t->flags & GUI_TABS_NEW) {
            struct gfx_rect b = { ex, ey, BTN_W, TAB_H - 4 };
            int cx = ex + BTN_W / 2, cy = ey + (TAB_H - 4) / 2;
            gui_draw_box(s, &b, GUI_FACE, 0);
            gfx_fill(s, cx - 4, cy, 9, 1, GUI_TEXT);
            gfx_fill(s, cx, cy - 4, 1, 9, GUI_TEXT);
            ex += BTN_W;
        }
        if (t->flags & GUI_TABS_MENU) {
            struct gfx_rect b = { ex, ey, BTN_W, TAB_H - 4 };
            gui_draw_box(s, &b, GUI_FACE, 0);
            gui_draw_arrow(s, ex + BTN_W / 2, ey + (TAB_H - 4) / 2, 1, GUI_TEXT);
        }
    }
    if (sel.w && sel.y + TAB_H == w->r.y + room)        /* (the chosen one in the last row opens into its page) */
        gfx_fill(s, sel.x + 1, sel.y + TAB_H - 1, sel.w - 2, 1, GUI_FACE);
    if (arrows) {                               /* GUI_TABS_SCROLL: left and right, dim at either end */
        struct gfx_rect a = { w->r.x + w->r.w - extras(t) - 2 * GUI_SB, w->r.y + 3, GUI_SB, TAB_H - 4 };
        struct gfx_rect b = { a.x + GUI_SB, a.y, GUI_SB, a.h };
        gui_draw_box(s, &a, GUI_FACE, 0);
        gui_draw_box(s, &b, GUI_FACE, 0);
        gui_draw_arrow(s, a.x + a.w / 2, a.y + a.h / 2, 2, t->first > 0 ? GUI_TEXT : GUI_DIM);
        gui_draw_arrow(s, b.x + b.w / 2, b.y + b.h / 2, 3, c || (n + 1 < TABS_MAX && n + 1 < i) ? GUI_TEXT : GUI_DIM);
    }
    if (gui_is_focus(w) && sel.w) {
        struct gfx_rect f = { sel.x + 3, sel.y + 3, sel.w - 6, TAB_H - 6 };
        gui_draw_focus(s, &f);
    }
}

static int tabs_count(struct tabs *t)
{
    struct gui_widget *c;
    int n = 0;
    for (c = t->w.first; c; c = c->next)
        n++;
    return n;
}

static void tabs_go(struct tabs *t, int at)
{
    int n = tabs_count(t);
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
    int n = tabs_count(t);
    if (n)
        tabs_go(t, (t->at + by + n) % n);
}

static struct gui_widget *page_at(struct tabs *t, int i)
{
    struct gui_widget *c;
    for (c = t->w.first; c && i > 0; c = c->next, i--)
        ;
    return i == 0 ? c : NULL;
}

/* GUI_TABS_MOVE: the page at `from` put at `to` (the pages' order is the titles'), and chosen. */
static void tabs_move(struct tabs *t, int from, int to)
{
    struct gui_widget *p = page_at(t, from), *at = page_at(t, to), *w = &t->w;
    if (!p || !at || p == at)
        return;
    if (p->prev)                                /* (out of the list ...) */
        p->prev->next = p->next;
    else
        w->first = p->next;
    if (p->next)
        p->next->prev = p->prev;
    else
        w->last = p->prev;
    if (to < from) {                            /* (... and in again before the page at `to`, or after it) */
        p->prev = at->prev, p->next = at;
        if (at->prev)
            at->prev->next = p;
        else
            w->first = p;
        at->prev = p;
    } else {
        p->prev = at, p->next = at->next;
        if (at->next)
            at->next->prev = p;
        else
            w->last = p;
        at->next = p;
    }
    t->at = to;
    gui_relayout(w->win);
    gui_damage(w);
    gui_send(w, GUI_ON_MOVE);
}

static void tabs_pick(struct gui_widget *w, int id)    /* (the list's choice) */
{
    struct tabs *t = (struct tabs *)w;
    tabs_go(t, id);
    if (t->style != GUI_TABS_ROWS) {
        tabs_show(t);
        gui_damage(w);
    }
}

/* GUI_TABS_MENU: every title in a menu dropped at x, y, the chosen one checked. */
static void tabs_list(struct tabs *t, int x, int y)
{
    struct gui_widget *c;
    int i = 0;
    if (!t->menu) {
        t->menu = gui_menu_new();
        gui_menu_to(t->menu, &t->w, tabs_pick);
    }
    gui_menu_clear(t->menu);
    for (c = t->w.first; c; c = c->next, i++) {
        gui_menu_item(t->menu, gui_get_text(c), i, NULL);
        if (i == t->at)
            gui_menu_check(t->menu, i, 1);
    }
    gui_popup_menu(t->w.win, t->menu, x, y);
}

static int tabs_event(struct gui_widget *w, struct gui_ev *e)
{
    struct tabs *t = (struct tabs *)w;
    if (e->type == GUI_EV_KEY && e->key == GUI_K_F4 && (e->mods & (GLOS_MOD_CTRL | GLOS_MOD_ALT)) == GLOS_MOD_CTRL
        && t->flags & GUI_TABS_CLOSE && tabs_count(t)) {
        t->closing = t->at;                     /* Ctrl+F4: the chosen page's close asked */
        gui_send(w, GUI_ON_CLOSE);
        return 1;
    }
    if (e->type == GUI_EV_KEY && !(e->mods & (GLOS_MOD_CTRL | GLOS_MOD_ALT))) {
        int list = t->style == GUI_TABS_LIST;
        if (e->key == GUI_K_LEFT || (list && e->key == GUI_K_UP)) {
            if (t->at > 0)
                tabs_go(t, t->at - 1);
            return 1;
        }
        if (e->key == GUI_K_RIGHT || (list && e->key == GUI_K_DOWN)) {
            tabs_go(t, t->at + 1);
            return 1;
        }
        return 0;
    }
    if (t->style == GUI_TABS_LIST && t->sb.r.w && gui_sb_event(w, &t->sb, e, 1)) {
        if (t->sb.pos != t->first) {
            t->first = t->sb.pos;
            gui_damage(w);
        }
        return 1;
    }
    if (t->press >= 0 && (e->type == GUI_EV_MOVE || e->type == GUI_EV_RELEASE)) {    /* a title held */
        struct gfx_rect r[TABS_MAX];
        int i, n = tabs_count(t), to = t->target;
        tabs_geom(t, w->r.x, w->r.y, w->r.w, w->r.h, r);
        for (i = 0; i < n && i < TABS_MAX; i++)
            if (r[i].w && e->x >= r[i].x && e->x < r[i].x + r[i].w && e->y >= r[i].y && e->y < r[i].y + TAB_H)
                to = i;
        if (t->flags & GUI_TABS_DRAG) {         /* well out of the strip (a pane to dock elsewhere) */
            int out = e->y < w->r.y - 16 || e->y > w->r.y + TAB_H + 16 || e->x < w->r.x - 16
                      || e->x > w->r.x + w->r.w + 16;
            if (out != t->out) {
                t->out = out;
                if (out)
                    t->moving = 1;
                gui_damage(w);
            }
        }
        if (e->type == GUI_EV_MOVE) {
            if (!t->moving && (e->x - t->press_x > 5 || t->press_x - e->x > 5))
                t->moving = 1;
            if (t->moving && to != t->target) {
                t->target = to;
                gui_damage(w);
            }
            return 1;
        }
        if (t->moving && t->out) {              /* let go out there: the app's (GUI_ON_DROP) */
            t->moving = t->out = 0;
            t->dropped = t->press, t->drop_x = e->x, t->drop_y = e->y;
            t->press = -1;
            gui_damage(w);
            gui_send(w, GUI_ON_DROP);
            return 1;
        }
        if (t->moving) {
            t->moving = 0;
            if (to != t->press)
                tabs_move(t, t->press, to);
            gui_damage(w);
        }
        t->press = -1;
        return 1;
    }
    if (e->type == GUI_EV_PRESS && (e->button == 1 || e->button == 4)) {
        struct gfx_rect r[TABS_MAX];
        int i, n = tabs_count(t), room = tabs_geom(t, w->r.x, w->r.y, w->r.w, w->r.h, r);
        int arrows = tabs_arrows(t, w->r.w), ex = w->r.x + w->r.w - extras(t);
        int close = t->flags & GUI_TABS_CLOSE && t->style != GUI_TABS_LIST;
        if (t->style == GUI_TABS_LIST ? e->x >= w->r.x + room : e->y >= w->r.y + room)
            return 0;                           /* (the page's) */
        if (e->button == 1 && extras(t) && e->x >= ex && e->y < w->r.y + TAB_H) {
            if (t->flags & GUI_TABS_NEW && e->x < ex + BTN_W) {
                gui_send(w, GUI_ON_NEW);        /* (the app adds a page) */
                return 1;
            }
            if (t->flags & GUI_TABS_MENU)
                tabs_list(t, w->r.x + w->r.w - BTN_W, w->r.y + TAB_H - 1);
            return 1;
        }
        if (e->button == 1 && arrows && e->x >= ex - arrows && e->x < ex) {
            if (e->x < ex - GUI_SB) {
                if (t->first > 0)
                    t->first--;
            } else {
                for (i = n < TABS_MAX ? n : TABS_MAX; i > 0 && !r[i - 1].w; i--)
                    ;
                if (i < n && t->first < n - 1)
                    t->first++;                 /* (a title after the last shown: one more along) */
            }
            gui_damage(w);
            return 1;
        }
        for (i = 0; i < n && i < TABS_MAX; i++)
            if (r[i].w && e->x >= r[i].x && e->x < r[i].x + r[i].w && e->y >= r[i].y && e->y < r[i].y + TAB_H) {
                if (close && (e->button == 4 || e->x >= r[i].x + r[i].w - CLOSE_W - 1)) {
                    t->closing = i;             /* (the app removes the page if it agrees: gui_tab_remove) */
                    gui_send(w, GUI_ON_CLOSE);
                    return 1;
                }
                if (e->button != 1)
                    return 1;
                tabs_go(t, i);
                if (t->flags & GUI_TABS_MOVE && t->style != GUI_TABS_LIST)
                    t->press = t->target = i, t->press_x = e->x, t->moving = 0;
                break;
            }
        return 1;
    }
    return 0;
}

static void tabs_destroy(struct gui_widget *w)
{
    struct tabs *t = (struct tabs *)w;
    if (t->menu)
        gui_menu_free(t->menu);
}

const struct gui_class gui_tabs_class = { "tabs", GUI_C_FOCUS, tabs_draw, tabs_event, tabs_measure, tabs_layout,
                                          tabs_destroy };

struct gui_widget *gui_tabs(struct gui_widget *parent)
{
    struct tabs *t = (struct tabs *)gui_new(parent, &gui_tabs_class, sizeof(struct tabs));
    t->press = -1;
    return &t->w;
}

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

void gui_tabs_flags(struct gui_widget *tabs, unsigned flags)
{
    ((struct tabs *)tabs)->flags = flags;
    gui_damage(tabs);
    if (tabs->win)
        gui_relayout(tabs->win);
}

int gui_tabs_closing(struct gui_widget *tabs) { return ((struct tabs *)tabs)->closing; }

int gui_tabs_dropped(struct gui_widget *tabs, int *x, int *y)
{
    struct tabs *t = (struct tabs *)tabs;
    *x = t->drop_x, *y = t->drop_y;
    return t->dropped;
}

/* Pages came or went other than by the tabs (a docked pane moved: dock.c): the chosen one among those there. */
void gui_tabs_fix(struct gui_widget *tabs)
{
    struct tabs *t = (struct tabs *)tabs;
    int n = tabs_count(t);
    if (t->at >= n)
        t->at = n > 0 ? n - 1 : 0;
    if (t->first >= n)
        t->first = n > 0 ? n - 1 : 0;
    gui_damage(tabs);
    if (tabs->win)
        gui_relayout(tabs->win);
}

struct gui_widget *gui_tabs_page(struct gui_widget *tabs, int page) { return page_at((struct tabs *)tabs, page); }

void gui_tab_remove(struct gui_widget *tabs, int page)
{
    struct tabs *t = (struct tabs *)tabs;
    struct gui_widget *p = page_at(t, page);
    int was = t->at, n;
    if (!p)
        return;
    gui_remove(p);
    n = tabs_count(t);
    if (t->at > page || t->at >= n)             /* (the one after it, or the last; the chosen's index moves down
                                                   with the pages after the one gone) */
        t->at = t->at > 0 ? t->at - 1 : 0;
    if (t->first > 0 && t->first >= n)
        t->first = n - 1;
    gui_damage(tabs);
    if (tabs->win) {
        gui_relayout(tabs->win);
        if (!tabs->win->focus)                  /* (the focus was in the page that went) */
            gui_focus(tabs);
    }
    if (was == page && n)
        gui_send(tabs, GUI_ON_SELECT);
}

void gui_tabs_style(struct gui_widget *tabs, int style)
{
    struct tabs *t = (struct tabs *)tabs;
    t->style = style;
    t->first = 0;
    gui_damage(tabs);
    if (tabs->win)
        gui_relayout(tabs->win);
}

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
