/* The property grid (toolkit 2, step C8; docs/v1/toolkit-2.md, Widgets): the IDE's and the Control Panel's
 * advanced settings. Names and values in two columns (the line between them dragged), under groups that open and
 * close; each value edited as its kind wants: text and numbers in a field put over the value (Enter keeps it, Esc
 * leaves it as it was; a number's Up and Down step it), a yes-or-no by a check box, a choice stepped through its
 * items, a colour and a font by their dialogs. The current property's help is shown under the grid.
 *
 * Keys: Up, Down, Page Up, Page Down, Home, End; Left and Right close and open a group (Left on a property: its
 * group); Enter or F2 edits; Space ticks or steps; typing begins an edit with what is typed. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guiint.h"

struct prow {
    int is_group, group;                /* a group's row; the group a property is in (-1: none) */
    int open;                           /* (a group) */
    char name[40];
    char *help;
    struct gui_prop p;                  /* (a property) its text and help are this row's own copies */
};

struct pgrid {
    struct gui_widget w;
    struct prow *row;
    int n, room;
    int cur, top;                       /* the current row and the first shown, as places among the rows shown */
    int split;                          /* the names' column's width (0: two fifths) */
    int drag;                           /* the line between the columns held */
    int editing;                        /* the field is over the current property's value */
    int changed;                        /* the property GUI_ON_CHANGE told of */
    int last_group;
    struct gui_widget *edit;
    struct gui_sb sb;
};

#define PG(w) ((struct pgrid *)(w))
#define HELP_H (GUI_ROW_H * 2 + 6)

static struct gui_class pf_class;               /* the field's, with the grid's keys in front (made once) */

static int has_help(struct pgrid *g)
{
    int i;
    for (i = 0; i < g->n; i++)
        if (g->row[i].help && g->row[i].help[0])
            return 1;
    return 0;
}

static int shown(struct pgrid *g, int i)        /* is row i shown (its group open)? */
{
    return g->row[i].is_group || g->row[i].group < 0 || g->row[g->row[i].group].open;
}

static int nshown(struct pgrid *g)
{
    int i, k = 0;
    for (i = 0; i < g->n; i++)
        k += shown(g, i);
    return k;
}

static int row_at(struct pgrid *g, int place)   /* the row shown at that place, or -1 */
{
    int i, k = 0;
    for (i = 0; i < g->n; i++)
        if (shown(g, i) && k++ == place)
            return i;
    return -1;
}

static int place_of(struct pgrid *g, int row)
{
    int i, k = 0;
    for (i = 0; i < g->n && i < row; i++)
        k += shown(g, i);
    return shown(g, row) ? k : -1;
}

static struct gfx_rect list_r(struct pgrid *g)  /* the rows' part (less the bar and the help) */
{
    struct gfx_rect r = { g->w.r.x + 1, g->w.r.y + 1, g->w.r.w - 2 - GUI_SB, g->w.r.h - 2 };
    if (has_help(g))
        r.h -= HELP_H;
    return r;
}

static int rows_fit(struct pgrid *g) { struct gfx_rect r = list_r(g); return r.h / GUI_ROW_H > 0 ? r.h / GUI_ROW_H : 1; }

static int split_x(struct pgrid *g)
{
    struct gfx_rect r = list_r(g);
    int s = g->split > 0 ? g->split : r.w * 2 / 5;
    return r.x + (s < 40 ? 40 : s > r.w - 40 ? r.w - 40 : s);
}

static void place(struct pgrid *g)
{
    struct gfx_rect r = list_r(g);
    g->sb.r.x = g->w.r.x + g->w.r.w - GUI_SB - 1, g->sb.r.y = r.y, g->sb.r.w = GUI_SB, g->sb.r.h = r.h;
    g->sb.total = nshown(g), g->sb.page = rows_fit(g), g->sb.pos = g->top;
    gui_sb_clamp(&g->sb);
    g->top = g->sb.pos;
}

static void value_text(const struct gui_prop *p, char *t, int room)
{
    static const char *const faces[3] = { "Sans", "Bold", "Fixed" }, *const sizes[3] = { "small", "normal", "large" };
    int n = 0;
    switch (p->kind) {
    case GUI_PROP_TEXT: snprintf(t, (size_t)room, "%s", p->text ? p->text : ""); break;
    case GUI_PROP_NUMBER: snprintf(t, (size_t)room, "%d", p->value); break;
    case GUI_PROP_BOOL: snprintf(t, (size_t)room, "%s", p->value ? "Yes" : "No"); break;
    case GUI_PROP_CHOICE:
        while (p->items && p->items[n])
            n++;
        snprintf(t, (size_t)room, "%s", p->value >= 0 && p->value < n ? p->items[p->value] : "");
        break;
    case GUI_PROP_COLOUR: snprintf(t, (size_t)room, "#%06X", p->rgb & 0xFFFFFF); break;
    case GUI_PROP_FONT:
        snprintf(t, (size_t)room, "%s, %s", faces[p->font.face >= 0 && p->font.face < 3 ? p->font.face : 0],
                 sizes[p->font.size >= 0 && p->font.size < 3 ? p->font.size : 1]);
        break;
    default: t[0] = 0;
    }
}

static void pg_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct pgrid *g = PG(w);
    struct gfx_rect r, clip = s->clip;
    int on = !(w->flags & GUI_DISABLED), focus = gui_is_focus(w) || g->editing, i, sx;
    place(g);
    r = list_r(g);
    sx = split_x(g);
    gui_draw_field(s, &w->r, on);
    gfx_rect_cut(&r, &clip);
    gfx_clip(s, &r);
    for (i = 0; i < rows_fit(g) + 1; i++) {
        int k = row_at(g, g->top + i), y = list_r(g).y + i * GUI_ROW_H;
        struct prow *pr;
        char t[96];
        if (k < 0)
            break;
        pr = &g->row[k];
        if (pr->is_group) {                     /* a group: its box, its name in bold across both columns */
            int bx = list_r(g).x + 3;
            gfx_fill(s, list_r(g).x, y, list_r(g).w, GUI_ROW_H, GUI_FACE);
            gfx_fill(s, bx + 1, y + 4, 9, 9, GUI_FIELD);
            gfx_frame(s, bx + 1, y + 4, 9, 9, GUI_DIM);
            gfx_fill(s, bx + 3, y + 8, 5, 1, GUI_FIELD_TEXT);
            if (!pr->open)
                gfx_fill(s, bx + 5, y + 6, 1, 5, GUI_FIELD_TEXT);
            if (g->top + i == g->cur && focus)
                gfx_fill(s, bx + 13, y, gfx_text_width(gui_bold, pr->name, -1) + 4, GUI_ROW_H, GUI_CHOSEN);
            gfx_text(s, bx + 15, y + 1, gui_bold, pr->name, -1,
                     g->top + i == g->cur && focus ? GUI_CHOSEN_TEXT : on ? GUI_TEXT : GUI_DIM);
            continue;
        }
        if (g->top + i == g->cur)               /* the current property: its name chosen */
            gfx_fill(s, list_r(g).x, y, sx - list_r(g).x, GUI_ROW_H, focus ? GUI_CHOSEN : GUI_SHADE);
        gui_text_fit(s, list_r(g).x + 16, y + 1, sx - list_r(g).x - 18, gui_font, pr->name,
                     g->top + i == g->cur && focus ? GUI_CHOSEN_TEXT : on ? GUI_FIELD_TEXT : GUI_DIM);
        gfx_fill(s, sx, y, 1, GUI_ROW_H, GUI_SHADE);
        gfx_fill(s, list_r(g).x, y + GUI_ROW_H - 1, list_r(g).w, 1, GUI_SHADE);
        if (g->editing && g->top + i == g->cur)
            continue;                           /* (the field is there) */
        if (pr->p.kind == GUI_PROP_BOOL) {
            gui_draw_check(s, sx + 4, y + (GUI_ROW_H - 13) / 2, pr->p.value, on);
        } else if (pr->p.kind == GUI_PROP_COLOUR) {
            gfx_fill(s, sx + 4, y + 3, 14, GUI_ROW_H - 6, pr->p.rgb & 0xFFFFFF);
            gfx_frame(s, sx + 3, y + 2, 16, GUI_ROW_H - 4, GUI_TEXT);
            value_text(&pr->p, t, sizeof t);
            gui_text_fit(s, sx + 23, y + 1, list_r(g).x + list_r(g).w - sx - 25, gui_font, t, on ? GUI_FIELD_TEXT : GUI_DIM);
        } else {
            value_text(&pr->p, t, sizeof t);
            gui_text_fit(s, sx + 4, y + 1, list_r(g).x + list_r(g).w - sx - (pr->p.kind == GUI_PROP_CHOICE ? 20 : 6),
                         gui_font, t, on ? GUI_FIELD_TEXT : GUI_DIM);
            if (pr->p.kind == GUI_PROP_CHOICE)
                gui_draw_arrow(s, list_r(g).x + list_r(g).w - 9, y + GUI_ROW_H / 2, 1, on ? GUI_TEXT : GUI_DIM);
        }
    }
    gfx_clip(s, &clip);
    if (has_help(g)) {                          /* the current property's help */
        struct gfx_rect h = { w->r.x + 1, w->r.y + w->r.h - 1 - HELP_H, w->r.w - 2, HELP_H };
        int k = row_at(g, g->cur);
        gfx_fill(s, h.x, h.y, h.w, h.h, GUI_FACE);
        gfx_fill(s, h.x, h.y, h.w, 1, GUI_LINE);
        if (k >= 0) {
            gfx_text(s, h.x + 4, h.y + 3, gui_bold, g->row[k].name, -1, GUI_TEXT);
            if (g->row[k].help)
                gui_text_fit(s, h.x + 4, h.y + 3 + GUI_ROW_H, h.w - 8, gui_font, g->row[k].help, GUI_TEXT);
        }
    }
    gui_sb_draw(s, &g->sb, on);
    if (gui_is_focus(w)) {
        struct gfx_rect f = { w->r.x + 1, w->r.y + 1, w->r.w - GUI_SB - 2, list_r(g).h };
        gui_draw_focus(s, &f);
    }
}

static void pg_layout(struct gui_widget *w)    /* the field over the value being edited, or out of sight */
{
    struct pgrid *g = PG(w);
    int place_now;
    if (!g->edit)
        return;
    place(g);
    place_now = g->cur - g->top;
    if (g->editing && place_now >= 0 && place_now < rows_fit(g)) {
        int sx = split_x(g), y = list_r(g).y + place_now * GUI_ROW_H;
        gui_place(g->edit, sx + 1, y - 2, list_r(g).x + list_r(g).w - sx - 1, GUI_ROW_H + 3);
    } else {
        gui_place(g->edit, w->r.x, w->r.y, 0, 0);
    }
}

static void tell(struct pgrid *g, int row)
{
    int i, id = 0;
    for (i = 0; i < row; i++)                   /* (a property's id: its place among the properties) */
        id += !g->row[i].is_group;
    g->changed = id;
    gui_damage(&g->w);
    gui_send(&g->w, GUI_ON_CHANGE);
}

static void select_all(struct gui_widget *f)     /* (as Ctrl+A in it) */
{
    struct gui_ev e;
    memset(&e, 0, sizeof e);
    e.type = GUI_EV_KEY, e.key = 0x1E, e.ch = 1, e.mods = GLOS_MOD_CTRL;
    gui_field_class.event(f, &e);
}

static void edit_end(struct pgrid *g, int keep)
{
    int k = row_at(g, g->cur);
    if (!g->editing)
        return;
    g->editing = 0;
    g->edit->flags |= GUI_HIDDEN;
    pg_layout(&g->w);
    gui_damage(&g->w);
    if (keep && k >= 0) {
        struct gui_prop *p = &g->row[k].p;
        const char *t = gui_get_text(g->edit);
        if (p->kind == GUI_PROP_NUMBER) {
            char *end;
            long v = strtol(t, &end, 10);
            if (end != t) {
                v = v < p->min ? p->min : v > p->max ? p->max : v;
                if (v != p->value) {
                    p->value = (int)v;
                    tell(g, k);
                }
            }
        } else if (!p->text || strcmp(p->text, t)) {
            free((char *)p->text);
            p->text = strdup(t);
            tell(g, k);
        }
    }
}

static void edit_begin(struct pgrid *g, const struct gui_ev *typed)
{
    int k = row_at(g, g->cur);
    char t[96];
    if (k < 0 || g->row[k].is_group)
        return;
    value_text(&g->row[k].p, t, sizeof t);
    gui_set_text(g->edit, typed ? "" : t);
    g->editing = 1;
    g->edit->flags &= ~GUI_HIDDEN;
    pg_layout(&g->w);
    gui_focus(g->edit);
    if (!typed)
        select_all(g->edit);
    else
        gui_field_class.event(g->edit, (struct gui_ev *)typed);
    gui_damage(&g->w);
}

static void act(struct pgrid *g, int back)      /* the current property acted on: Enter, a click on its value */
{
    int k = row_at(g, g->cur), n = 0;
    struct gui_prop *p;
    if (k < 0)
        return;
    if (g->row[k].is_group) {
        g->row[k].open = !g->row[k].open;
        gui_damage(&g->w);
        return;
    }
    p = &g->row[k].p;
    switch (p->kind) {
    case GUI_PROP_TEXT:
    case GUI_PROP_NUMBER:
        edit_begin(g, NULL);
        break;
    case GUI_PROP_BOOL:
        p->value = !p->value;
        tell(g, k);
        break;
    case GUI_PROP_CHOICE:
        while (p->items && p->items[n])
            n++;
        if (n) {
            p->value = ((p->value + (back ? -1 : 1)) % n + n) % n;
            tell(g, k);
        }
        break;
    case GUI_PROP_COLOUR: {
        unsigned c = p->rgb;
        if (gui_colour_dialog(g->w.win, g->row[k].name, &c) && c != p->rgb) {
            p->rgb = c;
            tell(g, k);
        }
        break;
    }
    case GUI_PROP_FONT: {
        struct gui_fontspec f = p->font;
        if (gui_font_dialog(g->w.win, g->row[k].name, &f) && (f.face != p->font.face || f.size != p->font.size)) {
            p->font = f;
            tell(g, k);
        }
        break;
    }
    }
}

static void go(struct pgrid *g, int place_now)
{
    int n = nshown(g);
    place_now = place_now < 0 ? 0 : place_now >= n ? n - 1 : place_now;
    g->cur = place_now;
    if (g->cur < g->top)
        g->top = g->cur;
    if (g->cur >= g->top + rows_fit(g))
        g->top = g->cur - rows_fit(g) + 1;
    gui_damage(&g->w);
}

/* The field's keys, the grid's first: Enter and Esc end the edit, Up and Down step a number or move on. */
static int pf_event(struct gui_widget *w, struct gui_ev *e)
{
    struct pgrid *g = PG(w->parent);
    int k = row_at(g, g->cur);
    if (e->type == GUI_EV_BLUR && g->editing) {
        gui_field_class.event(w, e);
        edit_end(g, 1);                         /* (the keyboard gone elsewhere: what was typed kept) */
        return 1;
    }
    if (e->type == GUI_EV_KEY && !(e->mods & (GLOS_MOD_CTRL | GLOS_MOD_ALT))) {
        if (e->key == GUI_K_ENTER || e->key == GUI_K_KPENTER || e->key == GUI_K_ESC) {
            edit_end(g, e->key != GUI_K_ESC);
            gui_focus(&g->w);
            return 1;
        }
        if ((e->key == GUI_K_UP || e->key == GUI_K_DOWN) && k >= 0 && g->row[k].p.kind == GUI_PROP_NUMBER) {
            char t[16];
            long v = strtol(gui_get_text(w), NULL, 10) + (e->key == GUI_K_UP ? 1 : -1);
            const struct gui_prop *p = &g->row[k].p;
            snprintf(t, sizeof t, "%ld", v < p->min ? (long)p->min : v > p->max ? (long)p->max : v);
            gui_set_text(w, t);
            select_all(w);
            return 1;
        }
        if (e->key == GUI_K_UP || e->key == GUI_K_DOWN) {
            edit_end(g, 1);
            gui_focus(&g->w);
            go(g, g->cur + (e->key == GUI_K_UP ? -1 : 1));
            return 1;
        }
    }
    return gui_field_class.event(w, e);
}

static int pg_event(struct gui_widget *w, struct gui_ev *e)
{
    struct pgrid *g = PG(w);
    struct gfx_rect r;
    int k, plain = !(e->mods & (GLOS_MOD_CTRL | GLOS_MOD_ALT));
    place(g);
    r = list_r(g);
    if (e->type != GUI_EV_KEY && gui_sb_event(w, &g->sb, e, 1)) {
        edit_end(g, 1);
        g->top = g->sb.pos;
        gui_damage(w);
        return 1;
    }
    switch (e->type) {
    case GUI_EV_KEY:
        if (!plain)
            return 0;
        k = row_at(g, g->cur);
        switch (e->key) {
        case GUI_K_UP: go(g, g->cur - 1); return 1;
        case GUI_K_DOWN: go(g, g->cur + 1); return 1;
        case GUI_K_PGUP: go(g, g->cur - rows_fit(g) + 1); return 1;
        case GUI_K_PGDN: go(g, g->cur + rows_fit(g) - 1); return 1;
        case GUI_K_HOME: go(g, 0); return 1;
        case GUI_K_END: go(g, nshown(g) - 1); return 1;
        case GUI_K_LEFT:
            if (k >= 0 && g->row[k].is_group && g->row[k].open)
                g->row[k].open = 0, gui_damage(w);
            else if (k >= 0 && !g->row[k].is_group && g->row[k].group >= 0)
                go(g, place_of(g, g->row[k].group));
            return 1;
        case GUI_K_RIGHT:
            if (k >= 0 && g->row[k].is_group && !g->row[k].open)
                g->row[k].open = 1, gui_damage(w);
            return 1;
        case GUI_K_ENTER:
        case GUI_K_KPENTER:
        case GUI_K_F1 + 1:                      /* (F2) */
            act(g, 0);
            return 1;
        case GUI_K_SPACE:
            if (k >= 0 && !g->row[k].is_group && (g->row[k].p.kind == GUI_PROP_BOOL || g->row[k].p.kind == GUI_PROP_CHOICE)) {
                act(g, (e->mods & GLOS_MOD_SHIFT) != 0);
                return 1;
            }
            break;
        }
        if (k >= 0 && !g->row[k].is_group && e->ch > ' ' && e->ch < 0x7F
            && (g->row[k].p.kind == GUI_PROP_TEXT || (g->row[k].p.kind == GUI_PROP_NUMBER
                                                      && ((e->ch >= '0' && e->ch <= '9') || e->ch == '-')))) {
            edit_begin(g, e);                   /* typing: an edit begun with it */
            return 1;
        }
        return 0;
    case GUI_EV_PRESS:
        if (e->button != 1 || e->y < r.y || e->y >= r.y + r.h)
            return 1;
        if (abs(e->x - split_x(g)) <= 2) {      /* the line between the columns */
            g->drag = 1;
            return 1;
        }
        edit_end(g, 1);
        gui_focus(w);
        k = row_at(g, g->top + (e->y - r.y) / GUI_ROW_H);
        if (k < 0)
            return 1;
        go(g, place_of(g, k));
        if (g->row[k].is_group) {
            if (e->x < r.x + 15 || e->clicks == 2)
                act(g, 0);
        } else if (e->x > split_x(g)) {
            act(g, 0);
        }
        return 1;
    case GUI_EV_MOVE:
        if (g->drag) {
            g->split = e->x - r.x;
            pg_layout(w);
            gui_damage(w);
        }
        return 1;
    case GUI_EV_RELEASE:
        g->drag = 0;
        return 1;
    case GUI_EV_WHEEL:
        edit_end(g, 1);
        g->top -= e->wheel * 3;
        place(g);
        gui_damage(w);
        return 1;
    case GUI_EV_FOCUS:
    case GUI_EV_BLUR:
        gui_damage(w);
        return 1;
    }
    return 0;
}

static void pg_measure(struct gui_widget *w, int *pw, int *ph) { (void)w, *pw = 240, *ph = 160; }

static void pg_destroy(struct gui_widget *w)
{
    struct pgrid *g = PG(w);
    int i;
    for (i = 0; i < g->n; i++) {
        free(g->row[i].help);
        if (!g->row[i].is_group)
            free((char *)g->row[i].p.text);
    }
    free(g->row);
}

static const struct gui_class pg_class = { "propgrid", GUI_C_FOCUS, pg_draw, pg_event, pg_measure, pg_layout,
                                           pg_destroy };

struct gui_widget *gui_propgrid(struct gui_widget *parent)
{
    struct pgrid *g = (struct pgrid *)gui_new(parent, &pg_class, sizeof *g);
    if (!pf_class.name) {
        pf_class = gui_field_class;
        pf_class.name = "propfield";
        pf_class.event = pf_event;
    }
    g->last_group = -1;
    g->edit = gui_field(&g->w, 20);
    g->edit->cls = &pf_class;
    g->edit->flags |= GUI_HIDDEN;
    return &g->w;
}

static struct prow *more(struct pgrid *g)
{
    if (g->n == g->room) {
        int room = g->room * 2 + 8;
        struct prow *r = realloc(g->row, (size_t)room * sizeof *r);
        if (!r)
            return NULL;
        g->row = r, g->room = room;
    }
    memset(&g->row[g->n], 0, sizeof g->row[0]);
    return &g->row[g->n++];
}

void gui_prop_group(struct gui_widget *w, const char *name)
{
    struct pgrid *g = PG(w);
    struct prow *r = more(g);
    if (!r)
        return;
    r->is_group = 1, r->open = 1, r->group = -1;
    snprintf(r->name, sizeof r->name, "%s", name ? name : "");
    g->last_group = g->n - 1;
    gui_damage(w);
}

int gui_prop_add(struct gui_widget *w, const char *name, const char *help, const struct gui_prop *p)
{
    struct pgrid *g = PG(w);
    struct prow *r = more(g);
    int i, id = 0;
    if (!r)
        return -1;
    r->group = g->last_group;
    snprintf(r->name, sizeof r->name, "%s", name ? name : "");
    r->help = help ? strdup(help) : NULL;
    r->p = *p;
    r->p.text = p->text ? strdup(p->text) : NULL;
    for (i = 0; i < g->n - 1; i++)
        id += !g->row[i].is_group;
    gui_damage(w);
    return id;
}

static struct prow *prop(struct pgrid *g, int id)
{
    int i;
    for (i = 0; i < g->n; i++)
        if (!g->row[i].is_group && id-- == 0)
            return &g->row[i];
    return NULL;
}

const struct gui_prop *gui_prop_get(struct gui_widget *w, int id)
{
    struct prow *r = prop(PG(w), id);
    return r ? &r->p : NULL;
}

void gui_prop_set(struct gui_widget *w, int id, const struct gui_prop *p)
{
    struct prow *r = prop(PG(w), id);
    if (!r)
        return;
    free((char *)r->p.text);
    r->p = *p;
    r->p.text = p->text ? strdup(p->text) : NULL;
    gui_damage(w);
}

int gui_prop_changed(struct gui_widget *w) { return PG(w)->changed; }
