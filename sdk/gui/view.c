/* The canvas view and the image view (toolkit 2, step C8; docs/v1/toolkit-2.md, Widgets): Paint's, the PDF
 * Viewer's, the Recorder's waveform, MIDI's timeline; the image view of games, the browser and previews.
 *
 * A canvas view shows a document of so many units across and down at a zoom (a percentage, or fitted to the view),
 * scrolled both ways by its bars, the wheel (Shift: across; Ctrl: the zoom, about the pointer) and the arrows, with
 * rulers along its top and left if asked. The app draws what shows (its draw is given the document's rectangle
 * that shows and where its corner is on the screen) and has the mouse's events in the document's units; a drag
 * keeps the mouse until the button is let go (the toolkit's own capture), the middle button pans, and the
 * pointer over the document takes the shape the app chose for its tool.
 *
 * The image view is a canvas view of a picture (kept, not copied): fitted (never enlarged) or at a zoom, centred
 * when smaller than the view; the left button pans it. A picture made smaller is averaged (box filter) once for
 * the zoom; made larger, its pixels are repeated. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guiint.h"

#define RULER_W 26                      /* the left ruler's width; the top one is as tall as a row and a bit */
#define RULER_H (gui_font->height + 6)

static const int zooms[] = { 10, 25, 33, 50, 67, 100, 150, 200, 300, 400, 600, 800, 1200, 1600 };
#define NZOOMS ((int)(sizeof zooms / sizeof zooms[0]))

struct cview {
    struct gui_widget w;
    int doc_w, doc_h;
    int num, den;                       /* the scale: a document unit is num/den pixels */
    int zoom;                           /* the percentage asked (0: fitted; -1: xnum/xden exactly) */
    int xnum, xden;                     /* (gui_cview_scale) */
    unsigned flags;                     /* GUI_VIEW_ */
    int ruler_unit;                     /* (rulers) a label every so many units at the least */
    int cursor, cursor_on;              /* the pointer's shape over the document; set now in the window */
    struct gui_sb hsb, vsb;             /* positions in pixels */
    int pan, pan_x, pan_y, pan_hx, pan_vx;      /* a pan by the middle (or the image's left) button */
    void (*draw)(struct gui_widget *, struct gfx_surface *, const struct gui_viewport *);
    int (*event)(struct gui_widget *, struct gui_ev *, int x, int y);
    /* the image view's */
    const unsigned *img;
    int iw, ih, istride;
    unsigned *small;                    /* the picture averaged down for the scale (num/den < 1) */
    int sw, sh, s_num, s_den;
};

#define CV(w) ((struct cview *)(w))

/* The document's area on the screen (less the rulers and the bars). */
static struct gfx_rect area(struct cview *v)
{
    struct gfx_rect a = v->w.r;
    if (v->flags & GUI_CV_RULERS)
        a.x += RULER_W, a.w -= RULER_W, a.y += RULER_H, a.h -= RULER_H;
    if (v->vsb.r.w)
        a.w -= GUI_SB;
    if (v->hsb.r.h)
        a.h -= GUI_SB;
    return a;
}

static long scaled(struct cview *v, long units) { return units * v->num / v->den; }

/* The bars and the scale for the size the widget has now. */
static void place(struct cview *v)
{
    struct gfx_rect a = v->w.r;
    int i, need_h, need_v, sw, sh;
    if (v->flags & GUI_CV_RULERS)
        a.x += RULER_W, a.w -= RULER_W, a.y += RULER_H, a.h -= RULER_H;
    if (v->zoom == 0) {                         /* fitted (never enlarged): the smaller of the two ratios */
        int aw = a.w > 1 ? a.w : 1, ah = a.h > 1 ? a.h : 1;
        if (v->doc_w <= aw && v->doc_h <= ah)
            v->num = v->den = 1;
        else if ((long)aw * v->doc_h <= (long)ah * v->doc_w)
            v->num = aw, v->den = v->doc_w > 0 ? v->doc_w : 1;
        else
            v->num = ah, v->den = v->doc_h > 0 ? v->doc_h : 1;
    } else if (v->zoom < 0) {
        v->num = v->xnum, v->den = v->xden;
    } else {
        v->num = v->zoom, v->den = 100;
    }
    sw = (int)scaled(v, v->doc_w), sh = (int)scaled(v, v->doc_h);
    need_h = sw > a.w, need_v = sh > a.h;       /* (a bar takes room from the other way: asked twice) */
    for (i = 0; i < 2; i++) {
        need_h = sw > a.w - (need_v ? GUI_SB : 0);
        need_v = sh > a.h - (need_h ? GUI_SB : 0);
    }
    v->vsb.r.w = v->vsb.r.h = v->hsb.r.w = v->hsb.r.h = 0;
    if (need_v) {
        v->vsb.r.x = v->w.r.x + v->w.r.w - GUI_SB, v->vsb.r.y = a.y;
        v->vsb.r.w = GUI_SB, v->vsb.r.h = a.h - (need_h ? GUI_SB : 0);
    }
    if (need_h) {
        v->hsb.r.x = a.x, v->hsb.r.y = v->w.r.y + v->w.r.h - GUI_SB;
        v->hsb.r.w = a.w - (need_v ? GUI_SB : 0), v->hsb.r.h = GUI_SB;
    }
    a = area(v);
    v->hsb.total = sw, v->hsb.page = a.w;
    v->vsb.total = sh, v->vsb.page = a.h;
    gui_sb_clamp(&v->hsb);
    gui_sb_clamp(&v->vsb);
    if (!need_h)
        v->hsb.pos = 0;
    if (!need_v)
        v->vsb.pos = 0;
}

static void view_of(struct cview *v, struct gui_viewport *out)
{
    struct gfx_rect a = area(v);
    int sw = (int)scaled(v, v->doc_w), sh = (int)scaled(v, v->doc_h);
    memset(out, 0, sizeof *out);
    out->area = a;
    out->num = v->num, out->den = v->den;
    out->zoom = v->num * 100 / v->den;
    out->ox = a.x - v->hsb.pos + (sw < a.w && v->flags & GUI_CV_CENTRE ? (a.w - sw) / 2 : 0);
    out->oy = a.y - v->vsb.pos + (sh < a.h && v->flags & GUI_CV_CENTRE ? (a.h - sh) / 2 : 0);
    out->doc.x = (int)((long)(a.x - out->ox) * v->den / v->num);
    out->doc.y = (int)((long)(a.y - out->oy) * v->den / v->num);
    out->doc.x = out->doc.x < 0 ? 0 : out->doc.x, out->doc.y = out->doc.y < 0 ? 0 : out->doc.y;
    out->doc.w = (int)((long)a.w * v->den / v->num) + 2, out->doc.h = (int)((long)a.h * v->den / v->num) + 2;
    if (out->doc.x + out->doc.w > v->doc_w)
        out->doc.w = v->doc_w - out->doc.x;
    if (out->doc.y + out->doc.h > v->doc_h)
        out->doc.h = v->doc_h - out->doc.y;
}

static int ruler_step(struct cview *v)          /* units between labels: the ruler's unit times 1, 2, 5, 10, 20, ...,
                                                   the first at least 50 pixels apart */
{
    static const int mul[3] = { 1, 2, 5 };
    long step = 0, base = v->ruler_unit > 0 ? v->ruler_unit : 10, tens = 1;
    int k;
    for (k = 0; k < 24; k++) {
        step = base * mul[k % 3] * tens;
        if (scaled(v, step) >= 50)
            break;
        if (k % 3 == 2)
            tens *= 10;
    }
    return (int)step;
}

static void rulers(struct cview *v, struct gfx_surface *s, const struct gui_viewport *gv)
{
    struct gfx_rect clip = s->clip, top = { gv->area.x, v->w.r.y, gv->area.w, RULER_H },
                    left = { v->w.r.x, gv->area.y, RULER_W, gv->area.h };
    int step = ruler_step(v), u, minor = step / 5 > 0 ? step / 5 : step;
    char t[16];
    gfx_fill(s, v->w.r.x, v->w.r.y, RULER_W, RULER_H, GUI_FACE);
    gfx_fill(s, top.x, top.y, top.w, top.h, GUI_FACE);
    gfx_fill(s, left.x, left.y, left.w, left.h, GUI_FACE);
    gfx_fill(s, top.x, top.y + top.h - 1, top.w, 1, GUI_LINE);
    gfx_fill(s, left.x + left.w - 1, left.y, 1, left.h, GUI_LINE);
    gfx_rect_cut(&top, &clip);
    gfx_clip(s, &top);
    for (u = gv->doc.x / minor * minor; u <= gv->doc.x + gv->doc.w && u <= v->doc_w; u += minor) {
        int x = gv->ox + (int)scaled(v, u);
        gfx_fill(s, x, top.y + top.h - (u % step ? 4 : 8), 1, u % step ? 4 : 8, GUI_TEXT);
        if (u % step == 0) {
            snprintf(t, sizeof t, "%d", u);
            gfx_text(s, x + 2, top.y + 1, gui_font, t, -1, GUI_TEXT);
        }
    }
    gfx_clip(s, &clip);
    gfx_rect_cut(&left, &clip);
    gfx_clip(s, &left);
    for (u = gv->doc.y / minor * minor; u <= gv->doc.y + gv->doc.h && u <= v->doc_h; u += minor) {
        int y = gv->oy + (int)scaled(v, u);
        gfx_fill(s, left.x + left.w - (u % step ? 4 : 8), y, u % step ? 4 : 8, 1, GUI_TEXT);
        if (u % step == 0) {
            snprintf(t, sizeof t, "%d", u);
            gfx_text(s, left.x + 2, y + 1, gui_font, t, -1, GUI_TEXT);
        }
    }
    gfx_clip(s, &clip);
}

static void cv_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct cview *v = CV(w);
    struct gui_viewport gv;
    struct gfx_rect clip = s->clip, a, d;
    place(v);
    view_of(v, &gv);
    a = gv.area;
    gfx_fill(s, a.x, a.y, a.w, a.h, GUI_DARK);  /* (round the document, where it doesn't reach) */
    d.x = gv.ox, d.y = gv.oy, d.w = (int)scaled(v, v->doc_w), d.h = (int)scaled(v, v->doc_h);
    gfx_rect_cut(&d, &a);
    gfx_rect_cut(&d, &clip);
    if (d.w > 0 && d.h > 0 && v->draw) {
        gfx_clip(s, &d);
        v->draw(w, s, &gv);
        gfx_clip(s, &clip);
    }
    if (v->flags & GUI_CV_RULERS)
        rulers(v, s, &gv);
    if (v->vsb.r.w)
        gui_sb_draw(s, &v->vsb, 1);
    if (v->hsb.r.h)
        gui_sb_draw(s, &v->hsb, 1);
    if (v->vsb.r.w && v->hsb.r.h)
        gfx_fill(s, v->vsb.r.x, v->hsb.r.y, GUI_SB, GUI_SB, GUI_FACE);
    if (gui_is_focus(w) && v->flags & GUI_CV_RING) {
        struct gfx_rect f = { a.x + 1, a.y + 1, a.w - 2, a.h - 2 };
        gui_draw_focus(s, &f);
    }
}

static void set_cursor(struct cview *v, int on)
{
    int shape = on ? v->cursor : GLOS_CURSOR_ARROW;
    if (on == v->cursor_on && (!on || shape == v->cursor_on - 1))
        return;
    v->cursor_on = on ? shape + 1 : 0;
    if (v->w.win && v->w.win->id)
        glos_set(v->w.win->id, GLOS_PROP_WIN_CURSOR, shape);
}

/* The zoom made z (a percentage; 0: fitted), the document's point (dx, dy) kept at the screen's (sx, sy). */
static void zoom_to(struct cview *v, int z, int dx, int dy, int sx, int sy)
{
    struct gui_viewport gv;
    if (z == v->zoom)
        return;
    v->zoom = z;
    place(v);
    view_of(v, &gv);
    v->hsb.pos += gv.ox + (int)scaled(v, dx) - sx;
    v->vsb.pos += gv.oy + (int)scaled(v, dy) - sy;
    gui_sb_clamp(&v->hsb);
    gui_sb_clamp(&v->vsb);
    gui_damage(&v->w);
    gui_send(&v->w, GUI_ON_CHANGE);
}

static int zoom_step(struct cview *v, int dir)  /* the next zoom up (dir 1) or down from the one shown */
{
    int now = v->num * 100 / v->den, i;
    if (dir > 0) {
        for (i = 0; i < NZOOMS && zooms[i] <= now; i++)
            ;
        return i < NZOOMS ? zooms[i] : zooms[NZOOMS - 1];
    }
    for (i = NZOOMS - 1; i >= 0 && zooms[i] >= now; i--)
        ;
    return i >= 0 ? zooms[i] : zooms[0];
}

static int cv_event(struct gui_widget *w, struct gui_ev *e)
{
    struct cview *v = CV(w);
    struct gui_viewport gv;
    struct gfx_rect a;
    int in, dx, dy, image = v->img != NULL;
    place(v);
    view_of(v, &gv);
    a = gv.area;
    in = e->x >= a.x && e->x < a.x + a.w && e->y >= a.y && e->y < a.y + a.h;
    dx = (int)((long)(e->x - gv.ox) * v->den / v->num), dy = (int)((long)(e->y - gv.oy) * v->den / v->num);
    if (e->x < gv.ox)                           /* (rounded down to the left too) */
        dx--;
    if (e->y < gv.oy)
        dy--;
    if (v->pan) {                               /* panning: the document follows the pointer */
        if (e->type == GUI_EV_MOVE) {
            v->hsb.pos = v->pan_hx - (e->x - v->pan_x), v->vsb.pos = v->pan_vx - (e->y - v->pan_y);
            gui_sb_clamp(&v->hsb);
            gui_sb_clamp(&v->vsb);
            gui_damage(w);
            return 1;
        }
        if (e->type == GUI_EV_RELEASE) {
            v->pan = 0;
            return 1;
        }
    }
    if (e->type != GUI_EV_KEY && e->type != GUI_EV_WHEEL) {
        if ((v->vsb.r.w && gui_sb_event(w, &v->vsb, e, 16)) || (v->hsb.r.h && gui_sb_event(w, &v->hsb, e, 16))) {
            gui_damage(w);
            return 1;
        }
    }
    switch (e->type) {
    case GUI_EV_PRESS:
        if (in && (e->button == 4 || (image && e->button == 1 && !(v->flags & GUI_CV_TOOLS)))) {
            v->pan = 1, v->pan_x = e->x, v->pan_y = e->y, v->pan_hx = v->hsb.pos, v->pan_vx = v->vsb.pos;
            return 1;
        }
        if (!in)
            return 1;
        break;
    case GUI_EV_MOVE:
        set_cursor(v, in);
        break;
    case GUI_EV_LEAVE:
        set_cursor(v, 0);
        break;
    case GUI_EV_WHEEL:
        if (e->mods & GLOS_MOD_CTRL) {
            zoom_to(v, zoom_step(v, e->wheel > 0 ? 1 : -1), dx, dy, e->x, e->y);
            return 1;
        }
        if (e->mods & GLOS_MOD_SHIFT || !v->vsb.r.w)
            v->hsb.pos -= e->wheel * 48;
        else
            v->vsb.pos -= e->wheel * 48;
        gui_sb_clamp(&v->hsb);
        gui_sb_clamp(&v->vsb);
        gui_damage(w);
        return 1;
    case GUI_EV_FOCUS:
    case GUI_EV_BLUR:
        gui_damage(w);
        return v->event ? v->event(w, e, 0, 0) || 1 : 1;
    default:
        break;
    }
    if (v->event && v->event(w, e, dx, dy))     /* the app's, in the document's units */
        return 1;
    if (e->type == GUI_EV_KEY && !(e->mods & GLOS_MOD_ALT)) {   /* not the app's: the view's keys */
        int step = 32;
        switch (e->key) {
        case GUI_K_LEFT: v->hsb.pos -= step; break;
        case GUI_K_RIGHT: v->hsb.pos += step; break;
        case GUI_K_UP: v->vsb.pos -= step; break;
        case GUI_K_DOWN: v->vsb.pos += step; break;
        case GUI_K_PGUP: v->vsb.pos -= a.h - step; break;
        case GUI_K_PGDN: v->vsb.pos += a.h - step; break;
        case GUI_K_HOME: v->hsb.pos = v->vsb.pos = 0; break;
        case GUI_K_END: v->hsb.pos = v->hsb.total, v->vsb.pos = v->vsb.total; break;
        default:
            if (e->ch == '+' || e->ch == '=' || e->ch == '-' || (image && (e->ch == '0' || e->ch == '1'))) {
                int z = e->ch == '0' ? 0 : e->ch == '1' ? 100 : zoom_step(v, e->ch == '-' ? -1 : 1);
                int cx = a.x + a.w / 2, cy = a.y + a.h / 2;     /* (about the middle of what shows) */
                zoom_to(v, z, (int)((long)(cx - gv.ox) * v->den / v->num), (int)((long)(cy - gv.oy) * v->den / v->num),
                        cx, cy);
                return 1;
            }
            return 0;
        }
        gui_sb_clamp(&v->hsb);
        gui_sb_clamp(&v->vsb);
        gui_damage(w);
        return 1;
    }
    return in && (e->type == GUI_EV_PRESS || e->type == GUI_EV_RELEASE || e->type == GUI_EV_MOVE);
}

static void cv_measure(struct gui_widget *w, int *pw, int *ph) { (void)w, *pw = 200, *ph = 150; }

static void cv_destroy(struct gui_widget *w) { free(CV(w)->small); }

static const struct gui_class cv_class = { "canvasview", GUI_C_FOCUS, cv_draw, cv_event, cv_measure, NULL, cv_destroy };

struct gui_widget *gui_canvas_view(struct gui_widget *parent, int doc_w, int doc_h,
                                   void (*draw)(struct gui_widget *, struct gfx_surface *, const struct gui_viewport *),
                                   int (*event)(struct gui_widget *, struct gui_ev *, int x, int y))
{
    struct cview *v = (struct cview *)gui_new(parent, &cv_class, sizeof *v);
    v->doc_w = doc_w > 0 ? doc_w : 1, v->doc_h = doc_h > 0 ? doc_h : 1;
    v->zoom = 100, v->num = v->den = 1;
    v->draw = draw, v->event = event;
    v->cursor = GLOS_CURSOR_ARROW;
    return &v->w;
}

void gui_cview_style(struct gui_widget *w, unsigned flags, int ruler_unit)
{
    CV(w)->flags = flags, CV(w)->ruler_unit = ruler_unit;
    gui_damage(w);
}

void gui_cview_size(struct gui_widget *w, int doc_w, int doc_h)
{
    struct cview *v = CV(w);
    v->doc_w = doc_w > 0 ? doc_w : 1, v->doc_h = doc_h > 0 ? doc_h : 1;
    gui_damage(w);
}

void gui_cview_zoom(struct gui_widget *w, int percent)  /* 0: fitted */
{
    struct cview *v = CV(w);
    struct gui_viewport gv;
    int cx, cy;
    place(v);
    view_of(v, &gv);
    cx = gv.area.x + gv.area.w / 2, cy = gv.area.y + gv.area.h / 2;
    zoom_to(v, percent < 0 ? 100 : percent > 1600 ? 1600 : percent, (int)((long)(cx - gv.ox) * v->den / v->num),
            (int)((long)(cy - gv.oy) * v->den / v->num), cx, cy);
}

void gui_cview_scale(struct gui_widget *w, int num, int den)   /* exactly num/den (an app's own steps: 1/8) */
{
    struct cview *v = CV(w);
    if (num <= 0 || den <= 0)
        return;
    v->zoom = -1, v->xnum = num, v->xden = den;
    gui_damage(w);
}

void gui_cview_get_scale(struct gui_widget *w, int *num, int *den)    /* the scale shown (fitted: its own) */
{
    place(CV(w));
    *num = CV(w)->num, *den = CV(w)->den;
}

void gui_cview_on_event(struct gui_widget *w, int (*event)(struct gui_widget *, struct gui_ev *, int x, int y))
{
    CV(w)->event = event;                       /* (the app's first, keys too: 1 taken) */
}

int gui_cview_get_zoom(struct gui_widget *w)    /* the percentage shown (fitted: what the fit came to) */
{
    place(CV(w));
    return CV(w)->num * 100 / CV(w)->den;
}

void gui_cview_cursor(struct gui_widget *w, int shape)
{
    struct cview *v = CV(w);
    v->cursor = shape;
    if (v->cursor_on)
        set_cursor(v, 1);
}

void gui_cview_scroll(struct gui_widget *w, int doc_x, int doc_y)       /* that point of the document to the corner */
{
    struct cview *v = CV(w);
    place(v);
    v->hsb.pos = (int)scaled(v, doc_x), v->vsb.pos = (int)scaled(v, doc_y);
    gui_sb_clamp(&v->hsb);
    gui_sb_clamp(&v->vsb);
    gui_damage(w);
}

/* ---- the image view */
static void shrink(struct cview *v)             /* the picture averaged down to the scale, if it is less than 1 */
{
    int x, y, sx0, sx1, sy0, sy1, i, j;
    if (v->num >= v->den) {
        free(v->small), v->small = NULL;
        return;
    }
    if (v->small && v->s_num == v->num && v->s_den == v->den)
        return;
    free(v->small);
    v->sw = (int)scaled(v, v->iw), v->sh = (int)scaled(v, v->ih);
    v->sw = v->sw > 0 ? v->sw : 1, v->sh = v->sh > 0 ? v->sh : 1;
    v->small = malloc((size_t)v->sw * (size_t)v->sh * sizeof *v->small);
    v->s_num = v->num, v->s_den = v->den;
    if (!v->small)
        return;
    for (y = 0; y < v->sh; y++) {
        sy0 = (int)((long)y * v->ih / v->sh), sy1 = (int)((long)(y + 1) * v->ih / v->sh);
        sy1 = sy1 > sy0 ? sy1 : sy0 + 1;
        for (x = 0; x < v->sw; x++) {
            unsigned long r = 0, g = 0, b = 0, n;
            sx0 = (int)((long)x * v->iw / v->sw), sx1 = (int)((long)(x + 1) * v->iw / v->sw);
            sx1 = sx1 > sx0 ? sx1 : sx0 + 1;
            for (j = sy0; j < sy1; j++)
                for (i = sx0; i < sx1; i++) {
                    unsigned p = v->img[(size_t)j * (size_t)v->istride + (size_t)i];
                    r += p >> 16 & 255, g += p >> 8 & 255, b += p & 255;
                }
            n = (unsigned long)(sy1 - sy0) * (unsigned long)(sx1 - sx0);
            v->small[(size_t)y * (size_t)v->sw + (size_t)x] = (unsigned)((r + n / 2) / n << 16 | (g + n / 2) / n << 8
                                                                         | (b + n / 2) / n);
        }
    }
}

static void image_draw(struct gui_widget *w, struct gfx_surface *s, const struct gui_viewport *gv)
{
    struct cview *v = CV(w);
    struct gfx_rect c = s->clip;
    unsigned row[2048];
    int x, y;
    if (!v->img)
        return;
    shrink(v);
    if (v->small) {                             /* smaller: the averaged picture, as it is */
        gfx_image(s, gv->ox, gv->oy, v->small, v->sw, v->sh, (unsigned)v->sw);
        return;
    }
    for (y = c.y; y < c.y + c.h; y++) {         /* the same size or larger: each pixel repeated */
        int sy = (int)((long)(y - gv->oy) * v->den / v->num), n = c.w < 2048 ? c.w : 2048;
        const unsigned *src;
        if (sy < 0 || sy >= v->ih)
            continue;
        src = v->img + (size_t)sy * (size_t)v->istride;
        for (x = 0; x < n; x++) {
            int sx = (int)((long)(c.x + x - gv->ox) * v->den / v->num);
            row[x] = src[sx < 0 ? 0 : sx >= v->iw ? v->iw - 1 : sx];
        }
        gfx_image(s, c.x, y, row, n, 1, (unsigned)n);
    }
}

struct gui_widget *gui_image_view(struct gui_widget *parent)
{
    struct gui_widget *w = gui_canvas_view(parent, 1, 1, image_draw, NULL);
    CV(w)->zoom = 0;
    CV(w)->flags = GUI_CV_CENTRE | GUI_CV_RING;
    CV(w)->cursor = GLOS_CURSOR_MOVE;
    return w;
}

void gui_image_view_set(struct gui_widget *w, const unsigned *rgb, int iw, int ih, int stride)
{
    struct cview *v = CV(w);
    v->img = rgb, v->iw = iw, v->ih = ih, v->istride = stride > 0 ? stride : iw;
    free(v->small), v->small = NULL;
    gui_cview_size(w, iw, ih);
}
