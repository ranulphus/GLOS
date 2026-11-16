/* Colour (toolkit 2, step C6; docs/v1/toolkit-2.md, Dialogs): gui_colour_dialog.
 *
 * Swatches (sixteen basic colours, the theme's own, and the colours the app's user chose last), a square of
 * saturation (across) and value (up) for the hue a bar beside it sets, red, green and blue, hue, saturation and
 * value as spin boxes, the colour as hex, and the old colour beside the new. Every part takes the keys: the square
 * and the bar move by the arrows (Ctrl: ten at a time), and in the swatches the arrows take the swatch they land
 * on. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guiint.h"

#define SQ    128                               /* the square's side; the bar is as tall */
#define BAR_W 16
#define SW    18                                /* a swatch */
#define SW_ACROSS 8
#define IN    3                                 /* the square's and the bar's margin, where the focus ring goes */

static const unsigned basic[16] = {
    0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080, 0x008080, 0xC0C0C0,
    0x808080, 0xFF0000, 0x00FF00, 0xFFFF00, 0x0000FF, 0xFF00FF, 0x00FFFF, 0xFFFFFF
};
static const int theme_roles[8] = { GLOS_ROLE_FACE, GLOS_ROLE_FACE_TEXT, GLOS_ROLE_FIELD, GLOS_ROLE_LINE, GLOS_ROLE_DARK,
                                    GLOS_ROLE_MARK, GLOS_ROLE_CHOSEN, GLOS_ROLE_DESK };
static unsigned recent[SW_ACROSS];             /* the colours chosen last (the app's whole run), newest first */
static int nrecent;

static struct cd {
    struct gui_window *w;
    unsigned old, rgb;
    int h, s, v;                                /* 0-359, 0-100, 0-100 */
    int busy;                                   /* (the fields being set: their changes are not the user's) */
    struct gui_widget *sv, *bar, *sw, *r, *g, *b, *hh, *ss, *vv, *hex, *now;
    unsigned pix[SQ * SQ];
    int pix_hue;                                /* the hue the square's pixels were made for (-1: none) */
    int hot;                                    /* the swatch the arrows are on */
} C;

static void hsv_rgb(int h, int s, int v, unsigned *rgb)
{
    int i = (h / 60) % 6, f = h % 60, p, q, t, V = v * 255 / 100, r, g, b;
    p = V * (100 - s) / 100;
    q = V * (6000 - s * f) / 6000;
    t = V * (6000 - s * (60 - f)) / 6000;
    switch (i) {
    case 0: r = V, g = t, b = p; break;
    case 1: r = q, g = V, b = p; break;
    case 2: r = p, g = V, b = t; break;
    case 3: r = p, g = q, b = V; break;
    case 4: r = t, g = p, b = V; break;
    default: r = V, g = p, b = q; break;
    }
    *rgb = (unsigned)r << 16 | (unsigned)g << 8 | (unsigned)b;
}

static void rgb_hsv(unsigned rgb, int *h, int *s, int *v)      /* (h kept as it was for a grey) */
{
    int r = (int)(rgb >> 16 & 255), g = (int)(rgb >> 8 & 255), b = (int)(rgb & 255);
    int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b), d = mx - mn;
    *v = (mx * 100 + 127) / 255;
    *s = mx ? (d * 100 + mx / 2) / mx : 0;
    if (!d)
        return;
    if (mx == r)
        *h = (60 * (g - b) / d + 360) % 360;
    else if (mx == g)
        *h = 60 * (b - r) / d + 120;
    else
        *h = 60 * (r - g) / d + 240;
}

/* Everything shown again from C.rgb (from_rgb) or from C.h, C.s, C.v. */
static void refresh(int from_rgb)
{
    char hex[16];
    if (from_rgb)
        rgb_hsv(C.rgb, &C.h, &C.s, &C.v);
    else
        hsv_rgb(C.h, C.s, C.v, &C.rgb);
    C.busy = 1;
    gui_set_value(C.r, (int)(C.rgb >> 16 & 255));
    gui_set_value(C.g, (int)(C.rgb >> 8 & 255));
    gui_set_value(C.b, (int)(C.rgb & 255));
    gui_set_value(C.hh, C.h);
    gui_set_value(C.ss, C.s);
    gui_set_value(C.vv, C.v);
    snprintf(hex, sizeof hex, "#%06X", C.rgb & 0xFFFFFF);
    if (strcmp(gui_get_text(C.hex), hex))
        gui_set_text(C.hex, hex);
    C.busy = 0;
    gui_damage(C.sv);
    gui_damage(C.bar);
    gui_damage(C.now);
}

/* ---- the square: saturation across, value up */
static void sv_draw(struct gui_widget *w, struct gfx_surface *s)
{
    int x, y, cx, cy;
    if (C.pix_hue != C.h) {
        for (y = 0; y < SQ; y++)
            for (x = 0; x < SQ; x++)
                hsv_rgb(C.h, x * 100 / (SQ - 1), (SQ - 1 - y) * 100 / (SQ - 1), &C.pix[y * SQ + x]);
        C.pix_hue = C.h;
    }
    gfx_image(s, w->r.x + IN + 1, w->r.y + IN + 1, C.pix, SQ, SQ, SQ);
    gfx_frame(s, w->r.x + IN, w->r.y + IN, SQ + 2, SQ + 2, GUI_LINE);
    cx = w->r.x + IN + 1 + C.s * (SQ - 1) / 100, cy = w->r.y + IN + 1 + (100 - C.v) * (SQ - 1) / 100;
    gfx_frame(s, cx - 3, cy - 3, 7, 7, C.v > 50 ? 0x000000 : 0xFFFFFF);
    if (gui_is_focus(w)) {
        struct gfx_rect f = { w->r.x, w->r.y, SQ + 2 + 2 * IN, SQ + 2 + 2 * IN };
        gui_draw_focus(s, &f);
    }
}

static int sv_event(struct gui_widget *w, struct gui_ev *e)
{
    int step = e->mods & GLOS_MOD_CTRL ? 10 : 1;
    switch (e->type) {
    case GUI_EV_PRESS:
    case GUI_EV_MOVE:
        if (e->type == GUI_EV_MOVE && !(e->buttons & 1))
            return 0;
        C.s = (e->x - w->r.x - IN - 1) * 100 / (SQ - 1);
        C.v = 100 - (e->y - w->r.y - IN - 1) * 100 / (SQ - 1);
        break;
    case GUI_EV_KEY:
        if (e->key == GUI_K_LEFT)
            C.s -= step;
        else if (e->key == GUI_K_RIGHT)
            C.s += step;
        else if (e->key == GUI_K_UP)
            C.v += step;
        else if (e->key == GUI_K_DOWN)
            C.v -= step;
        else
            return 0;
        break;
    case GUI_EV_FOCUS:
    case GUI_EV_BLUR:
        gui_damage(w);
        return 1;
    default:
        return 0;
    }
    C.s = C.s < 0 ? 0 : C.s > 100 ? 100 : C.s;
    C.v = C.v < 0 ? 0 : C.v > 100 ? 100 : C.v;
    refresh(0);
    return 1;
}

static void sv_measure(struct gui_widget *w, int *pw, int *ph) { (void)w, *pw = *ph = SQ + 2 + 2 * IN; }
static const struct gui_class sv_class = { "satval", GUI_C_FOCUS, sv_draw, sv_event, sv_measure, NULL, NULL };

/* ---- the hue bar: red at the top, round to red at the bottom */
static void bar_draw(struct gui_widget *w, struct gfx_surface *s)
{
    int y, my;
    for (y = 0; y < SQ; y++) {
        unsigned c;
        hsv_rgb(y * 359 / (SQ - 1), 100, 100, &c);
        gfx_fill(s, w->r.x + IN + 4, w->r.y + IN + 1 + y, BAR_W, 1, c);
    }
    gfx_frame(s, w->r.x + IN + 3, w->r.y + IN, BAR_W + 2, SQ + 2, GUI_LINE);
    my = w->r.y + IN + 1 + C.h * (SQ - 1) / 359;
    gfx_fill(s, w->r.x + IN, my, 3, 1, GUI_TEXT);                  /* (marks either side of the hue) */
    gfx_fill(s, w->r.x + IN + BAR_W + 5, my, 3, 1, GUI_TEXT);
    if (gui_is_focus(w)) {
        struct gfx_rect f = { w->r.x, w->r.y, BAR_W + 8 + 2 * IN, SQ + 2 + 2 * IN };
        gui_draw_focus(s, &f);
    }
}

static int bar_event(struct gui_widget *w, struct gui_ev *e)
{
    int step = e->mods & GLOS_MOD_CTRL ? 30 : 3;
    switch (e->type) {
    case GUI_EV_PRESS:
    case GUI_EV_MOVE:
        if (e->type == GUI_EV_MOVE && !(e->buttons & 1))
            return 0;
        C.h = (e->y - w->r.y - IN - 1) * 359 / (SQ - 1);
        break;
    case GUI_EV_KEY:
        if (e->key == GUI_K_UP)
            C.h -= step;
        else if (e->key == GUI_K_DOWN)
            C.h += step;
        else
            return 0;
        break;
    case GUI_EV_FOCUS:
    case GUI_EV_BLUR:
        gui_damage(w);
        return 1;
    default:
        return 0;
    }
    C.h = C.h < 0 ? 0 : C.h > 359 ? 359 : C.h;
    refresh(0);
    return 1;
}

static void bar_measure(struct gui_widget *w, int *pw, int *ph) { (void)w, *pw = BAR_W + 8 + 2 * IN, *ph = SQ + 2 + 2 * IN; }
static const struct gui_class bar_class = { "huebar", GUI_C_FOCUS, bar_draw, bar_event, bar_measure, NULL, NULL };

/* ---- the swatches: the basic sixteen, the theme's eight, the recent ones */
static int swatch_count(void) { return 16 + 8 + nrecent; }

static unsigned swatch(int i)
{
    if (i < 16)
        return basic[i];
    if (i < 24)
        return GUI_ROLE(theme_roles[i - 16]);
    return recent[i - 24];
}

static void sw_xy(struct gui_widget *w, int i, int *x, int *y)
{
    int row = i < 16 ? i / SW_ACROSS : i < 24 ? 2 : 3;  /* (a row each for the theme's and the recent ones) */
    *x = w->r.x + (i < 16 ? i % SW_ACROSS : i < 24 ? i - 16 : i - 24) * (SW + 2);
    *y = w->r.y + row * (SW + 2) + (row >= 2 ? 6 : 0);
}

static void sw_draw(struct gui_widget *w, struct gfx_surface *s)
{
    int i, x, y;
    for (i = 0; i < swatch_count(); i++) {
        sw_xy(w, i, &x, &y);
        gfx_fill(s, x + 1, y + 1, SW - 2, SW - 2, swatch(i));
        gfx_frame(s, x, y, SW, SW, (swatch(i) & 0xFFFFFF) == (C.rgb & 0xFFFFFF) ? GUI_TEXT : GUI_LINE);
        if (gui_is_focus(w) && i == C.hot) {
            struct gfx_rect f = { x - 1, y - 1, SW + 2, SW + 2 };
            gui_draw_focus(s, &f);
        }
    }
}

static int sw_event(struct gui_widget *w, struct gui_ev *e)
{
    int i, x, y, n = swatch_count();
    switch (e->type) {
    case GUI_EV_PRESS:
        for (i = 0; i < n; i++) {
            sw_xy(w, i, &x, &y);
            if (e->x >= x && e->x < x + SW && e->y >= y && e->y < y + SW) {
                C.hot = i;
                C.rgb = swatch(i);
                refresh(1);
                gui_damage(w);
                return 1;
            }
        }
        return 1;
    case GUI_EV_KEY:
        if (e->key == GUI_K_LEFT && C.hot > 0)
            C.hot--;
        else if (e->key == GUI_K_RIGHT && C.hot + 1 < n)
            C.hot++;
        else if (e->key == GUI_K_UP)
            C.hot = C.hot >= SW_ACROSS ? C.hot - SW_ACROSS : C.hot;
        else if (e->key == GUI_K_DOWN)
            C.hot = C.hot + SW_ACROSS < n ? C.hot + SW_ACROSS : C.hot;
        else if (e->key != GUI_K_SPACE)
            return 0;
        C.rgb = swatch(C.hot);                  /* (the swatch the arrows land on is the colour; Enter is OK) */
        refresh(1);
        gui_damage(w);
        return 1;
    case GUI_EV_FOCUS:
    case GUI_EV_BLUR:
        gui_damage(w);
        return 1;
    default:
        return 0;
    }
}

static void sw_measure(struct gui_widget *w, int *pw, int *ph)
{
    (void)w;
    *pw = SW_ACROSS * (SW + 2);
    *ph = 4 * (SW + 2) + 6;
}

static const struct gui_class sw_class = { "swatches", GUI_C_FOCUS, sw_draw, sw_event, sw_measure, NULL, NULL };

/* ---- the old colour beside the new */
static void now_draw(struct gui_widget *w, struct gfx_surface *s)
{
    gfx_fill(s, w->r.x + 1, w->r.y + 1, w->r.w / 2 - 1, w->r.h - 2, C.old);
    gfx_fill(s, w->r.x + w->r.w / 2, w->r.y + 1, w->r.w - w->r.w / 2 - 1, w->r.h - 2, C.rgb);
    gfx_frame(s, w->r.x, w->r.y, w->r.w, w->r.h, GUI_LINE);
}

static void now_measure(struct gui_widget *w, int *pw, int *ph) { (void)w, *pw = 120, *ph = 28; }
static const struct gui_class now_class = { "oldnew", 0, now_draw, NULL, now_measure, NULL, NULL };

static void spin_on(struct gui_widget *w, int what)
{
    if (what != GUI_ON_CHANGE || C.busy)
        return;
    if (w == C.r || w == C.g || w == C.b) {
        C.rgb = (unsigned)gui_value(C.r) << 16 | (unsigned)gui_value(C.g) << 8 | (unsigned)gui_value(C.b);
        refresh(1);
    } else {
        C.h = gui_value(C.hh), C.s = gui_value(C.ss), C.v = gui_value(C.vv);
        refresh(0);
    }
}

static void hex_on(struct gui_widget *w, int what)
{
    const char *t = gui_get_text(w);
    char *end;
    unsigned long v;
    if (what != GUI_ON_ACTIVATE && what != GUI_ON_CHANGE)
        return;
    if (*t == '#')
        t++;
    if (strlen(t) != 6 || C.busy)
        return;
    v = strtoul(t, &end, 16);
    if (*end)
        return;
    C.rgb = (unsigned)v;
    refresh(1);
}

static void ok_click(struct gui_widget *w, int what)
{
    (void)what;
    gui_end_modal(w->win, GUI_OK);
}

static void cancel_click(struct gui_widget *w, int what)
{
    (void)what;
    gui_end_modal(w->win, GUI_CANCEL_R);
}

int gui_colour_dialog(struct gui_window *owner, const char *title, unsigned *rgb)
{
    struct gui_widget *box, *top, *pick, *fields, *row, *f;
    int r, i;
    memset(&C, 0, sizeof C);
    C.old = C.rgb = *rgb & 0xFFFFFF;
    C.pix_hue = -1;
    C.w = gui_window(title, 0, 0, GUI_W_DIALOG);
    C.w->owner = owner;
    box = gui_vbox(C.w->root, GUI_GAP, 10);
    top = gui_hbox(box, 12, 0);
    pick = gui_vbox(top, GUI_GAP, 0);
    gui_label(pick, "&Swatches:");
    C.sw = gui_new(pick, &sw_class, sizeof *C.sw);
    row = gui_hbox(top, 6, 0);
    C.sv = gui_new(row, &sv_class, sizeof *C.sv);
    C.bar = gui_new(row, &bar_class, sizeof *C.bar);
    fields = gui_form(top);
    C.r = gui_spin(gui_row(fields, "&Red:", NULL), 0, 255, 0);
    C.g = gui_spin(gui_row(fields, "&Green:", NULL), 0, 255, 0);
    C.b = gui_spin(gui_row(fields, "&Blue:", NULL), 0, 255, 0);
    C.hh = gui_spin(gui_row(fields, "&Hue:", NULL), 0, 359, 0);
    C.ss = gui_spin(gui_row(fields, "Sa&turation:", NULL), 0, 100, 0);
    C.vv = gui_spin(gui_row(fields, "&Value:", NULL), 0, 100, 0);
    C.hex = gui_field(gui_row(fields, "He&x:", NULL), 8);
    f = gui_row(fields, "Old, new:", NULL);
    C.now = gui_new(f, &now_class, sizeof *C.now);
    gui_on(C.r, spin_on, 0);
    gui_on(C.g, spin_on, 0);
    gui_on(C.b, spin_on, 0);
    gui_on(C.hh, spin_on, 0);
    gui_on(C.ss, spin_on, 0);
    gui_on(C.vv, spin_on, 0);
    gui_on(C.hex, hex_on, 0);
    row = gui_hbox(box, GUI_GAP, 0);
    gui_space(row, 0, 0);
    gui_button(row, "OK", ok_click, 0)->flags |= GUI_DEFAULT;
    gui_button(row, "Cancel", cancel_click, 0)->flags |= GUI_CANCEL;
    for (i = 0; i < swatch_count(); i++)        /* (the arrows start on the colour's swatch, if it has one) */
        if (swatch(i) == C.rgb) {
            C.hot = i;
            break;
        }
    refresh(1);
    C.w->focus = C.sw;
    r = gui_modal(C.w);
    if (r != GUI_OK)
        return 0;
    *rgb = C.rgb;
    for (i = 0; i < nrecent && recent[i] != C.rgb; i++)
        ;
    if (i == nrecent && nrecent < SW_ACROSS)
        nrecent++;
    if (i == SW_ACROSS)
        i = SW_ACROSS - 1;
    memmove(recent + 1, recent, (size_t)i * sizeof recent[0]);
    recent[0] = C.rgb;
    return 1;
}
