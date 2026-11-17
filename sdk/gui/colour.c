/* Colour (toolkit 2, step C6; docs/v1/toolkit-2.md, Dialogs): gui_colour_dialog.
 *
 * Swatches (sixteen basic colours, the theme's own, and the colours the app's user chose last), a square of
 * saturation (across) and value (up) for the hue a bar beside it sets, red, green and blue, hue, saturation and
 * value as spin boxes, the colour as hex, and the old colour beside the new. Every part takes the keys: the square
 * and the bar move by the arrows (Ctrl: ten at a time), and in the swatches the arrows take the swatch they land
 * on.
 *
 * Step C8 (the colour picker): with GUI_COLOUR_ALPHA (gui_colour_dialog_ex) the colour has an alpha too (0xAARRGGBB,
 * FF opaque): a spin box for it, the hex as #AARRGGBB, the old and the new over a checkerboard. The colour button
 * (gui_colour_button) shows its colour and opens the dialog; the palette (gui_palette) is a grid of swatches for
 * Paint: the left button takes the primary colour, the right the secondary, a double click edits the swatch. */
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
    int alpha, old_alpha, with_alpha;           /* (GUI_COLOUR_ALPHA) 0-255 */
    struct gui_widget *aa;
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
    if (C.with_alpha) {
        gui_set_value(C.aa, C.alpha);
        snprintf(hex, sizeof hex, "#%02X%06X", (unsigned)C.alpha, C.rgb & 0xFFFFFF);
    } else {
        snprintf(hex, sizeof hex, "#%06X", C.rgb & 0xFFFFFF);
    }
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

/* A colour with its alpha over a checkerboard (squares of 4, light and dark grey), in x, y, w, h. */
static unsigned blend(unsigned rgb, int alpha, unsigned under)
{
    unsigned out = 0;
    int k;
    for (k = 0; k < 24; k += 8)
        out |= (unsigned)(((int)(rgb >> k & 255) * alpha + (int)(under >> k & 255) * (255 - alpha) + 127) / 255) << k;
    return out;
}

static void fill_alpha(struct gfx_surface *s, int x, int y, int w, int h, unsigned rgb, int alpha)
{
    int i, j;
    if (alpha >= 255) {
        gfx_fill(s, x, y, w, h, rgb);
        return;
    }
    for (j = 0; j < h; j += 4)
        for (i = 0; i < w; i += 4)
            gfx_fill(s, x + i, y + j, w - i < 4 ? w - i : 4, h - j < 4 ? h - j : 4,
                     blend(rgb, alpha, ((i ^ j) & 4) ? 0x999999u : 0xDDDDDDu));
}

/* ---- the old colour beside the new */
static void now_draw(struct gui_widget *w, struct gfx_surface *s)
{
    fill_alpha(s, w->r.x + 1, w->r.y + 1, w->r.w / 2 - 1, w->r.h - 2, C.old, C.with_alpha ? C.old_alpha : 255);
    fill_alpha(s, w->r.x + w->r.w / 2, w->r.y + 1, w->r.w - w->r.w / 2 - 1, w->r.h - 2, C.rgb,
               C.with_alpha ? C.alpha : 255);
    gfx_frame(s, w->r.x, w->r.y, w->r.w, w->r.h, GUI_LINE);
}

static void now_measure(struct gui_widget *w, int *pw, int *ph) { (void)w, *pw = 120, *ph = 28; }
static const struct gui_class now_class = { "oldnew", 0, now_draw, NULL, now_measure, NULL, NULL };

static void spin_on(struct gui_widget *w, int what)
{
    if (what != GUI_ON_CHANGE || C.busy)
        return;
    if (w == C.aa) {
        C.alpha = gui_value(w);
        refresh(1);
    } else if (w == C.r || w == C.g || w == C.b) {
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
    if ((strlen(t) != 6 && !(C.with_alpha && strlen(t) == 8)) || C.busy)
        return;
    v = strtoul(t, &end, 16);
    if (*end)
        return;
    C.rgb = (unsigned)v & 0xFFFFFF;
    if (strlen(t) == 8)
        C.alpha = (int)(v >> 24 & 255);
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

int gui_colour_dialog_ex(struct gui_window *owner, const char *title, unsigned *rgb, unsigned flags)
{
    struct gui_widget *box, *top, *pick, *fields, *row, *f;
    int r, i;
    memset(&C, 0, sizeof C);
    C.old = C.rgb = *rgb & 0xFFFFFF;
    C.with_alpha = (flags & GUI_COLOUR_ALPHA) != 0;
    C.alpha = C.old_alpha = C.with_alpha ? (int)(*rgb >> 24 & 255) : 255;
    C.pix_hue = -1;
    C.w = gui_window(title, 0, 0, GUI_W_DIALOG);
    C.w->owner = owner;
    box = gui_vbox(C.w->root, GUI_GAP, 10);
    top = gui_hbox(box, 12, 0);
    pick = gui_vbox(top, GUI_GAP, 0);
    gui_label(pick, GUI_T("&Swatches:"));
    C.sw = gui_new(pick, &sw_class, sizeof *C.sw);
    row = gui_hbox(top, 6, 0);
    C.sv = gui_new(row, &sv_class, sizeof *C.sv);
    C.bar = gui_new(row, &bar_class, sizeof *C.bar);
    fields = gui_form(top);
    C.r = gui_spin(gui_row(fields, GUI_T("&Red:"), NULL), 0, 255, 0);
    C.g = gui_spin(gui_row(fields, GUI_T("&Green:"), NULL), 0, 255, 0);
    C.b = gui_spin(gui_row(fields, GUI_T("&Blue:"), NULL), 0, 255, 0);
    C.hh = gui_spin(gui_row(fields, GUI_T("&Hue:"), NULL), 0, 359, 0);
    C.ss = gui_spin(gui_row(fields, GUI_T("Sa&turation:"), NULL), 0, 100, 0);
    C.vv = gui_spin(gui_row(fields, GUI_T("&Value:"), NULL), 0, 100, 0);
    if (C.with_alpha)
        C.aa = gui_spin(gui_row(fields, GUI_T("&Alpha:"), NULL), 0, 255, 255);
    C.hex = gui_field(gui_row(fields, GUI_T("He&x:"), NULL), C.with_alpha ? 10 : 8);
    f = gui_row(fields, GUI_T("Old, new:"), NULL);
    C.now = gui_new(f, &now_class, sizeof *C.now);
    gui_on(C.r, spin_on, 0);
    gui_on(C.g, spin_on, 0);
    gui_on(C.b, spin_on, 0);
    gui_on(C.hh, spin_on, 0);
    gui_on(C.ss, spin_on, 0);
    gui_on(C.vv, spin_on, 0);
    gui_on(C.hex, hex_on, 0);
    if (C.aa)
        gui_on(C.aa, spin_on, 0);
    row = gui_hbox(box, GUI_GAP, 0);
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("OK"), ok_click, 0)->flags |= GUI_DEFAULT;
    gui_button(row, GUI_T("Cancel"), cancel_click, 0)->flags |= GUI_CANCEL;
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
    *rgb = C.with_alpha ? (unsigned)C.alpha << 24 | C.rgb : C.rgb;
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

int gui_colour_dialog(struct gui_window *owner, const char *title, unsigned *rgb)
{
    return gui_colour_dialog_ex(owner, title, rgb, 0);
}

/* ---- the colour button: its colour in a button's face; pressed, the dialog */
struct cbutton {
    struct gui_widget w;
    unsigned argb, flags;
    int down;
};

#define CBT(w) ((struct cbutton *)(w))

static void cb_measure(struct gui_widget *w, int *pw, int *ph) { (void)w, *pw = 44, *ph = GUI_FIELD_H; }

static void cb_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct cbutton *b = CBT(w);
    int on = !(w->flags & GUI_DISABLED);
    gui_draw_box(s, &w->r, GUI_FACE, b->down);
    fill_alpha(s, w->r.x + 5, w->r.y + 4, w->r.w - 10, w->r.h - 8, b->argb & 0xFFFFFF,
               b->flags & GUI_COLOUR_ALPHA ? (int)(b->argb >> 24) : 255);
    gfx_frame(s, w->r.x + 4, w->r.y + 3, w->r.w - 8, w->r.h - 6, on ? GUI_TEXT : GUI_DIM);
    if (gui_is_focus(w)) {
        struct gfx_rect f = { w->r.x + 2, w->r.y + 1, w->r.w - 4, w->r.h - 2 };
        gui_draw_focus(s, &f);
    }
}

static void cb_open(struct gui_widget *w)
{
    struct cbutton *b = CBT(w);
    unsigned c = b->argb;
    b->down = 0;
    gui_damage(w);
    if (gui_colour_dialog_ex(w->win, "Colour", &c, b->flags) && c != b->argb) {
        b->argb = c;
        gui_damage(w);
        gui_send(w, GUI_ON_CHANGE);
    }
}

static int cb_event(struct gui_widget *w, struct gui_ev *e)
{
    struct cbutton *b = CBT(w);
    switch (e->type) {
    case GUI_EV_PRESS:
        if (e->button != 1)
            return 0;
        b->down = 1;
        gui_damage(w);
        return 1;
    case GUI_EV_RELEASE:
        if (b->down && e->x >= w->r.x && e->x < w->r.x + w->r.w && e->y >= w->r.y && e->y < w->r.y + w->r.h)
            cb_open(w);
        b->down = 0;
        gui_damage(w);
        return 1;
    case GUI_EV_KEY:
        if (!(e->mods & (GLOS_MOD_CTRL | GLOS_MOD_ALT)) && (e->key == GUI_K_SPACE || e->key == GUI_K_ENTER)) {
            cb_open(w);
            return 1;
        }
        return 0;
    case GUI_EV_FOCUS:
    case GUI_EV_BLUR:
        gui_damage(w);
        return 1;
    }
    return 0;
}

static const struct gui_class cb_class = { "colourbutton", GUI_C_FOCUS, cb_draw, cb_event, cb_measure, NULL, NULL };

struct gui_widget *gui_colour_button(struct gui_widget *parent, unsigned argb, unsigned flags)
{
    struct cbutton *b = (struct cbutton *)gui_new(parent, &cb_class, sizeof *b);
    b->flags = flags;
    b->argb = flags & GUI_COLOUR_ALPHA ? argb : (argb & 0xFFFFFF) | 0xFF000000u;
    return &b->w;
}

unsigned gui_colour_get(struct gui_widget *w)
{
    if (w->cls == &cb_class)
        return CBT(w)->flags & GUI_COLOUR_ALPHA ? CBT(w)->argb : CBT(w)->argb & 0xFFFFFF;
    return 0;
}

void gui_colour_set(struct gui_widget *w, unsigned argb)
{
    if (w->cls != &cb_class)
        return;
    CBT(w)->argb = CBT(w)->flags & GUI_COLOUR_ALPHA ? argb : (argb & 0xFFFFFF) | 0xFF000000u;
    gui_damage(w);
}

/* ---- the palette: swatches in rows; the left button the primary colour, the right the secondary */
#define PAL_MAX 64

struct palette {
    struct gui_widget w;
    unsigned c[PAL_MAX];
    int n, across, hot;
    int chosen[2];                      /* the primary's and the secondary's swatch */
};

#define PAL(w) ((struct palette *)(w))

static void pal_measure(struct gui_widget *w, int *pw, int *ph)
{
    struct palette *p = PAL(w);
    *pw = p->across * (SW + 2);
    *ph = (p->n + p->across - 1) / p->across * (SW + 2);
}

static unsigned contrast(unsigned rgb)          /* black on a light colour, white on a dark one */
{
    return (rgb >> 16 & 255) * 3 + (rgb >> 8 & 255) * 6 + (rgb & 255) > 1280 ? 0x000000u : 0xFFFFFFu;
}

static void pal_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct palette *p = PAL(w);
    int i;
    for (i = 0; i < p->n; i++) {
        int x = w->r.x + i % p->across * (SW + 2), y = w->r.y + i / p->across * (SW + 2);
        gfx_fill(s, x + 1, y + 1, SW - 2, SW - 2, p->c[i]);
        gfx_frame(s, x, y, SW, SW, i == p->chosen[0] || i == p->chosen[1] ? GUI_TEXT : GUI_LINE);
        if (i == p->chosen[0])                  /* (the primary: a square at its top left; the secondary: a hollow
                                                   one at its bottom right; black or white, whichever shows) */
            gfx_fill(s, x + 2, y + 2, 6, 6, contrast(p->c[i]));
        if (i == p->chosen[1])
            gfx_frame(s, x + SW - 8, y + SW - 8, 6, 6, contrast(p->c[i]));
        if (gui_is_focus(w) && i == p->hot) {
            struct gfx_rect f = { x - 1, y - 1, SW + 2, SW + 2 };
            gui_draw_focus(s, &f);
        }
    }
}

static void pal_choose(struct palette *p, int i, int which)
{
    p->hot = i;
    p->chosen[which] = i;
    gui_damage(&p->w);
    gui_send(&p->w, GUI_ON_SELECT);
}

static void pal_edit(struct palette *p, int i)
{
    unsigned c = p->c[i];
    if (gui_colour_dialog(p->w.win, GUI_T("Edit colour"), &c) && c != p->c[i]) {
        p->c[i] = c;
        gui_damage(&p->w);
        gui_send(&p->w, GUI_ON_CHANGE);
    }
}

static int pal_event(struct gui_widget *w, struct gui_ev *e)
{
    struct palette *p = PAL(w);
    int i;
    switch (e->type) {
    case GUI_EV_PRESS:
        if (e->button != 1 && e->button != 2)
            return 0;
        i = (e->y - w->r.y) / (SW + 2) * p->across + (e->x - w->r.x) / (SW + 2);
        if (e->x - w->r.x >= p->across * (SW + 2) || i < 0 || i >= p->n)
            return 1;
        if (e->button == 1 && e->clicks == 2)
            pal_edit(p, i);
        else
            pal_choose(p, i, e->button == 2);
        return 1;
    case GUI_EV_KEY:
        if (e->mods & (GLOS_MOD_CTRL | GLOS_MOD_ALT))
            return 0;
        switch (e->key) {
        case GUI_K_LEFT: p->hot = p->hot > 0 ? p->hot - 1 : 0; break;
        case GUI_K_RIGHT: p->hot = p->hot + 1 < p->n ? p->hot + 1 : p->hot; break;
        case GUI_K_UP: p->hot = p->hot >= p->across ? p->hot - p->across : p->hot; break;
        case GUI_K_DOWN: p->hot = p->hot + p->across < p->n ? p->hot + p->across : p->hot; break;
        case GUI_K_SPACE: pal_choose(p, p->hot, (e->mods & GLOS_MOD_SHIFT) != 0); return 1;
        case GUI_K_ENTER: pal_edit(p, p->hot); return 1;
        default: return 0;
        }
        gui_damage(w);
        return 1;
    case GUI_EV_FOCUS:
    case GUI_EV_BLUR:
        gui_damage(w);
        return 1;
    }
    return 0;
}

static const struct gui_class pal_class = { "palette", GUI_C_FOCUS, pal_draw, pal_event, pal_measure, NULL, NULL };

struct gui_widget *gui_palette(struct gui_widget *parent, const unsigned *colours, int n, int across)
{
    struct palette *p = (struct palette *)gui_new(parent, &pal_class, sizeof *p);
    int i;
    p->n = n < 1 ? 1 : n > PAL_MAX ? PAL_MAX : n;
    p->across = across > 0 ? across : 14;
    for (i = 0; i < p->n; i++)
        p->c[i] = colours ? colours[i] & 0xFFFFFF : 0;
    p->chosen[0] = 0, p->chosen[1] = p->n > 1 ? p->n - 1 : 0;
    return &p->w;
}

unsigned gui_palette_get(struct gui_widget *w, int which)      /* 0 the primary colour, 1 the secondary */
{
    struct palette *p = PAL(w);
    return p->c[p->chosen[which ? 1 : 0]];
}

unsigned gui_palette_colour(struct gui_widget *w, int i)
{
    struct palette *p = PAL(w);
    return i >= 0 && i < p->n ? p->c[i] : 0;
}
