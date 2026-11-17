/* The tour (toolkit 2, step C8; docs/v1/toolkit-2.md, Widgets): Help's pages that point at a place on the screen,
 * one after another: a part of GLOS's shell (SHELL's POINT: the Start button, the task bar, the tray, the clock)
 * or a widget of the app's.
 *
 * Each step is a bubble beside its place (under it if there is room, else over it, else beside it), with a tab on
 * its edge towards the place, a title, a few lines of text, "2 of 5", and Back, Next (Done on the last) and Close;
 * the place itself is framed in the accent colour. A step with no place has its bubble in the middle of the screen.
 * Enter is Next, Esc closes. The app hears how it ended (all of it, or closed at a step). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guiint.h"

#define BUBBLE_W 300
#define TAB 10                          /* the tab's depth */
#define RING 3                          /* the frame round the place */
#define PAD 10

static struct {
    struct gui_window *owner, *bubble, *ring[4];
    const struct gui_tour_step *steps;
    int n, at, side;                    /* side: where the place is from the bubble: 0 above, 1 below, 2 left, 3 right,
                                           -1 none */
    struct gfx_rect place;
    int tab_at;                         /* the tab's middle, along the bubble's edge */
    void (*done)(int step, int finished);
    struct gui_widget *count, *back, *next;
} T;

static void finish(int finished);

/* The text in lines no wider than width (drawn when s is given); the height it takes. */
static int wrap(struct gfx_surface *s, int x, int y, int width, const char *text, unsigned rgb)
{
    const char *p = text;
    int h = 0;
    while (p && *p) {
        int n = 0, cut = -1;
        while (p[n] && p[n] != '\n') {
            if (p[n] == ' ')
                cut = n;
            if (gfx_text_width(gui_font, p, n + 1) > width && cut > 0)
                break;
            n++;
        }
        if (p[n] && p[n] != '\n' && cut > 0)
            n = cut;
        if (s)
            gfx_text(s, x, y + h, gui_font, p, n, rgb);
        h += gui_font->height + 2;
        p += n;
        while (*p == ' ' || *p == '\n')
            p++;
    }
    return h;
}

/* ---- the bubble's own drawing: its frame, the tab, the title and text (the buttons are widgets) */
static void head_draw(struct gui_widget *w, struct gfx_surface *s)
{
    const struct gui_tour_step *st = &T.steps[T.at];
    gfx_text(s, w->r.x, w->r.y, gui_bold, st->title ? st->title : "", -1, GUI_TEXT);
    wrap(s, w->r.x, w->r.y + gui_bold->height + 6, w->r.w, st->text, GUI_TEXT);
}

static void head_measure(struct gui_widget *w, int *pw, int *ph)
{
    const struct gui_tour_step *st = &T.steps[T.at];
    (void)w;
    *pw = BUBBLE_W - 2 * PAD;
    *ph = gui_bold->height + 6 + wrap(NULL, 0, 0, BUBBLE_W - 2 * PAD, st->text, 0);
}

static const struct gui_class head_class = { "tourtext", 0, head_draw, NULL, head_measure, NULL, NULL };

static void frame_draw(struct gui_widget *w, struct gfx_surface *s)     /* the bubble's root: border and tab */
{
    struct gfx_rect r = w->r;
    int i, m = T.tab_at;
    gfx_fill(s, r.x, r.y, r.w, r.h, GUI_FACE);
    if (T.side == 0)
        r.y += TAB, r.h -= TAB;
    else if (T.side == 1)
        r.h -= TAB;
    else if (T.side == 2)
        r.x += TAB, r.w -= TAB;
    else if (T.side == 3)
        r.w -= TAB;
    gfx_frame(s, r.x, r.y, r.w, r.h, GUI_MARK);
    gfx_frame(s, r.x + 1, r.y + 1, r.w - 2, r.h - 2, GUI_MARK);
    for (i = 0; i < TAB; i++) {                 /* the tab: a triangle in the accent from the edge to the place */
        switch (T.side) {
        case 0: gfx_fill(s, w->r.x + m - i, w->r.y + TAB - i, 2 * i + 1, 1, GUI_MARK); break;
        case 1: gfx_fill(s, w->r.x + m - i, w->r.y + w->r.h - TAB + i - 1, 2 * i + 1, 1, GUI_MARK); break;
        case 2: gfx_fill(s, w->r.x + TAB - i, w->r.y + m - i, 1, 2 * i + 1, GUI_MARK); break;
        case 3: gfx_fill(s, w->r.x + w->r.w - TAB + i - 1, w->r.y + m - i, 1, 2 * i + 1, GUI_MARK); break;
        }
    }
}

static struct gfx_rect inner(struct gui_widget *w)    /* inside the border, less the tab */
{
    struct gfx_rect r = { w->r.x + PAD, w->r.y + PAD, w->r.w - 2 * PAD, w->r.h - 2 * PAD };
    if (T.side == 0)
        r.y += TAB, r.h -= TAB;
    else if (T.side == 1)
        r.h -= TAB;
    else if (T.side == 2)
        r.x += TAB, r.w -= TAB;
    else if (T.side == 3)
        r.w -= TAB;
    return r;
}

static void frame_layout(struct gui_widget *w)
{
    struct gfx_rect r = inner(w);
    if (w->first)
        gui_place(w->first, r.x, r.y, r.w, r.h);
}

static const struct gui_class frame_class = { "tourframe", 0, frame_draw, NULL, NULL, frame_layout, NULL };

/* ---- where the step's place is */
static int place_of(const struct gui_tour_step *st, struct gfx_rect *r)
{
    struct glos_point p;
    int ox, oy;
    if (st->point) {
        memset(&p, 0, sizeof p);
        if (glos_point((unsigned)st->point, &p) != 0 || p.w <= 0)
            return 0;
        r->x = p.x, r->y = p.y, r->w = p.w, r->h = p.h;
        return 1;
    }
    if (st->at && st->at->win && gui_origin(st->at->win, &ox, &oy) == 0 && !(st->at->flags & GUI_HIDDEN)) {
        r->x = ox + st->at->r.x, r->y = oy + st->at->r.y, r->w = st->at->r.w, r->h = st->at->r.h;
        return 1;
    }
    return 0;
}

static void screen_size(int *w, int *h)
{
    struct glos_point p;
    memset(&p, 0, sizeof p);
    if (glos_point(GLOS_POINT_WORK, &p) == 0 && p.w > 0)
        *w = p.x + p.w, *h = p.y + p.h;
    else
        *w = 640, *h = 480;
    if (glos_point(GLOS_POINT_BAR, &p) == 0 && p.w > 0 && p.y + p.h > *h)
        *h = p.y + p.h;
}

static void close_rings(void)
{
    int i;
    for (i = 0; i < 4; i++)
        if (T.ring[i]) {
            gui_close_later(T.ring[i]);
            T.ring[i] = NULL;
        }
}

static void ring_draw(struct gui_widget *w, struct gfx_surface *s) { gfx_fill(s, w->r.x, w->r.y, w->r.w, w->r.h, GUI_MARK); }
static const struct gui_class ring_class = { "tourring", 0, ring_draw, NULL, NULL, NULL, NULL };

static void ring(int i, int x, int y, int w, int h)
{
    struct gui_widget *c;
    T.ring[i] = gui_window_at("", x, y, w, h, GUI_W_POPUP | GUI_W_BARE);
    T.ring[i]->owner = T.owner;
    c = gui_new(T.ring[i]->root, &ring_class, sizeof *c);
    gui_grow(c, 1);
    gui_show_nofocus(T.ring[i], x, y);
}

static void step_on(struct gui_widget *b, int what);

static void show_step(void)
{
    const struct gui_tour_step *st = &T.steps[T.at];
    struct gui_widget *box, *row, *frame;
    int has = place_of(st, &T.place), sw, sh, bw = BUBBLE_W, bh, x, y, mw, mh;
    char count[24];
    close_rings();
    screen_size(&sw, &sh);
    if (!T.bubble) {                            /* one bubble for the whole tour: the keyboard stays in it */
        T.bubble = gui_window_at("Tour", 0, 0, bw, 100, GUI_W_TOOL | GUI_W_BARE);
        T.bubble->owner = T.owner;
        frame = gui_new(T.bubble->root, &frame_class, sizeof *frame);
        gui_grow(frame, 1);
    } else {
        frame = T.bubble->root->first;
        while (frame->first)
            gui_remove(frame->first);
    }
    box = gui_vbox(frame, 8, 0);
    gui_new(box, &head_class, sizeof(struct gui_widget));
    row = gui_hbox(box, 4, 0);
    snprintf(count, sizeof count, "%d of %d", T.at + 1, T.n);
    T.count = gui_label(row, count);
    gui_space(row, 0, 0);
    T.back = gui_button(row, "&Back", step_on, -1);
    gui_enable(T.back, T.at > 0);
    T.next = gui_button(row, T.at + 1 < T.n ? "&Next" : "&Done", step_on, 1);
    T.next->flags |= GUI_DEFAULT;
    gui_button(row, "Close", step_on, 0)->flags |= GUI_CANCEL;
    gui_measure(box, &mw, &mh);                 /* (the bubble as tall as its text and buttons) */
    bh = mh + 2 * PAD;
    T.side = -1;
    if (!has) {                                 /* nowhere to point: the middle */
        x = (sw - bw) / 2, y = (sh - bh) / 2;
    } else {
        int cx = T.place.x + T.place.w / 2, cy = T.place.y + T.place.h / 2;
        if (T.place.y + T.place.h + RING + bh + TAB <= sh)
            T.side = 0, bh += TAB, x = cx - bw / 2, y = T.place.y + T.place.h + RING;
        else if (T.place.y - RING - bh - TAB >= 0)
            T.side = 1, bh += TAB, x = cx - bw / 2, y = T.place.y - RING - bh;
        else if (T.place.x + T.place.w + RING + bw + TAB <= sw)
            T.side = 2, bw += TAB, x = T.place.x + T.place.w + RING, y = cy - bh / 2;
        else
            T.side = 3, bw += TAB, x = T.place.x - RING - bw, y = cy - bh / 2;
        x = x < 0 ? 0 : x + bw > sw ? sw - bw : x;
        y = y < 0 ? 0 : y + bh > sh ? sh - bh : y;
        T.tab_at = T.side <= 1 ? cx - x : cy - y;
        T.tab_at = T.tab_at < TAB + 4 ? TAB + 4 : T.tab_at > (T.side <= 1 ? bw : bh) - TAB - 4
                   ? (T.side <= 1 ? bw : bh) - TAB - 4 : T.tab_at;
        ring(0, T.place.x - RING, T.place.y - RING, T.place.w + 2 * RING, RING);
        ring(1, T.place.x - RING, T.place.y + T.place.h, T.place.w + 2 * RING, RING);
        ring(2, T.place.x - RING, T.place.y, RING, T.place.h);
        ring(3, T.place.x + T.place.w, T.place.y, RING, T.place.h);
    }
    if (!T.bubble->id) {
        T.bubble->gw.x = x, T.bubble->gw.y = y;
        gui_resize(T.bubble, bw, bh);
        T.bubble->focus = T.next;
        gui_show(T.bubble, 1);
    } else {
        gui_resize(T.bubble, bw, bh);
        glos_move(T.bubble->id, x, y);
        gui_relayout(T.bubble);
        gui_damage(T.bubble->root);
        gui_focus(T.next);
    }
}

static void step_on(struct gui_widget *b, int what)
{
    (void)what;
    if (b->id == 0) {
        finish(0);
    } else if (b->id > 0 && T.at + 1 >= T.n) {
        finish(1);
    } else {
        T.at += b->id;
        show_step();
    }
}

static void finish(int finished)
{
    int at = T.at;
    void (*done)(int, int) = T.done;
    if (T.bubble)
        gui_close_later(T.bubble);
    T.bubble = NULL;
    close_rings();
    T.steps = NULL;
    if (T.owner && T.owner->id)
        glos_focus(T.owner->id);
    if (done)
        done(at, finished);
}

void gui_tour(struct gui_window *owner, const struct gui_tour_step *steps, int n, void (*done)(int step, int finished))
{
    if (T.steps)
        finish(0);
    if (!steps || n <= 0)
        return;
    T.owner = owner, T.steps = steps, T.n = n, T.at = 0, T.done = done;
    show_step();
}

void gui_tour_end(void)
{
    if (T.steps)
        finish(0);
}
