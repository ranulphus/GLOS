/* Level meters and graphs (toolkit 2, step C8; docs/v1/toolkit-2.md, Widgets): the Mixer's and the Recorder's
 * levels, the Task Manager's and the Control Panel's lines over time.
 *
 * A level meter shows one or two channels' levels (0 to 1000) as bars, across or upright, green, then yellow past
 * 70% and red past 90% (a meter's own colours, the same in every theme), with each channel's peak held as a mark for
 * a second and a half before it falls back to the level.
 *
 * A graph keeps the last so many samples of up to four series and draws them as lines, the newest at the right,
 * over a grid, each series' name in its colour; the scale is the samples' own highest (or a range the app sets). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guiint.h"

#define PEAK_MS 1500
#define GREEN  0x30B040u
#define YELLOW 0xE0C020u
#define RED    0xE03020u

struct meter {
    struct gui_widget w;
    int ch, vertical;
    int level[2], peak[2];
    unsigned peak_at[2];
};

static unsigned zone(int at) { return at > 900 ? RED : at > 700 ? YELLOW : GREEN; }

static void meter_measure(struct gui_widget *w, int *pw, int *ph)
{
    struct meter *m = (struct meter *)w;
    int across = m->ch * 8 + (m->ch + 1) * 2;
    if (m->vertical)
        *pw = across, *ph = 100;
    else
        *pw = 140, *ph = across;
}

static void meter_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct meter *m = (struct meter *)w;
    int c, len = m->vertical ? w->r.h - 4 : w->r.w - 4, seg;
    gfx_fill(s, w->r.x, w->r.y, w->r.w, w->r.h, GUI_DARK);
    for (c = 0; c < m->ch; c++) {
        int along = 2 + c * 10, upto = m->level[c] * len / 1000, peak = m->peak[c] * len / 1000;
        for (seg = 0; seg < upto; seg += 3) {   /* in segments of two pixels, a gap of one */
            unsigned col = zone(seg * 1000 / len);
            if (m->vertical)
                gfx_fill(s, w->r.x + along, w->r.y + w->r.h - 2 - seg - 2, 8, 2, col);
            else
                gfx_fill(s, w->r.x + 2 + seg, w->r.y + along, 2, 8, col);
        }
        if (peak > 0) {                         /* the peak held */
            if (m->vertical)
                gfx_fill(s, w->r.x + along, w->r.y + w->r.h - 2 - peak, 8, 2, zone(m->peak[c]));
            else
                gfx_fill(s, w->r.x + 2 + peak - 2, w->r.y + along, 2, 8, zone(m->peak[c]));
        }
    }
}

static int meter_event(struct gui_widget *w, struct gui_ev *e)
{
    struct meter *m = (struct meter *)w;
    int c, held = 0;
    if (e->type != GUI_EV_TIMER)
        return 0;
    for (c = 0; c < m->ch; c++)                 /* peaks held long enough fall back to the level */
        if (m->peak[c] > m->level[c]) {
            if (gui_now() - m->peak_at[c] >= PEAK_MS) {
                m->peak[c] = m->level[c];
                gui_damage(w);
            } else {
                held = 1;
            }
        }
    if (!held)
        gui_timer(w, 0);
    return 1;
}

static const struct gui_class meter_class = { "meter", 0, meter_draw, meter_event, meter_measure, NULL, NULL };

struct gui_widget *gui_meter(struct gui_widget *parent, int channels, int vertical)
{
    struct meter *m = (struct meter *)gui_new(parent, &meter_class, sizeof *m);
    m->ch = channels == 2 ? 2 : 1;
    m->vertical = vertical != 0;
    return &m->w;
}

void gui_meter_set(struct gui_widget *w, int left, int right)
{
    struct meter *m = (struct meter *)w;
    int v[2], c;
    v[0] = left, v[1] = right;
    for (c = 0; c < m->ch; c++) {
        v[c] = v[c] < 0 ? 0 : v[c] > 1000 ? 1000 : v[c];
        m->level[c] = v[c];
        if (v[c] >= m->peak[c]) {
            m->peak[c] = v[c];
            m->peak_at[c] = gui_now();
        }
    }
    gui_timer(w, 100);                          /* (the peaks' fall watched) */
    gui_damage(w);
}

/* ---- graphs */

#define SERIES 4

struct graph {
    struct gui_widget w;
    int series, points;
    int *v;                                     /* points samples of each series, a ring: v[s * points + i] */
    int head, count;                            /* the next sample's place; how many there are */
    int lo, hi;                                 /* the range (hi <= lo: the samples' own) */
    unsigned rgb[SERIES];
    char name[SERIES][16];
};

static void graph_measure(struct gui_widget *w, int *pw, int *ph) { (void)w, *pw = 200, *ph = 80; }

static void graph_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct graph *g = (struct graph *)w;
    int lo = g->lo, hi = g->hi, i, k, x0 = w->r.x + 1, y0 = w->r.y + 1, gw = w->r.w - 2, gh = w->r.h - 2, lx;
    char t[16];
    gui_draw_field(s, &w->r, 1);
    for (i = 1; i < 4; i++)                     /* the grid */
        gfx_fill(s, x0, y0 + gh * i / 4, gw, 1, GUI_SHADE);
    if (hi <= lo) {                             /* (the samples' own range: from 0 to their highest) */
        lo = 0, hi = 1;
        for (k = 0; k < g->series * g->points; k++)
            if (g->v[k] > hi)
                hi = g->v[k];
    }
    for (k = 0; k < g->series; k++) {
        int px = -1, py = 0;
        for (i = 0; i < g->count; i++) {        /* the oldest kept at the left, the newest at the right */
            int idx = (g->head - g->count + i + g->points) % g->points, val = g->v[k * g->points + idx];
            int x = x0 + (g->points > 1 ? (g->points - g->count + i) * (gw - 1) / (g->points - 1) : 0);
            int y = y0 + gh - 1 - (int)((long long)(val - lo) * (gh - 1) / (hi - lo));
            y = y < y0 ? y0 : y > y0 + gh - 1 ? y0 + gh - 1 : y;
            if (px >= 0)
                gfx_line(s, px, py, x, y, g->rgb[k]);
            px = x, py = y;
        }
    }
    for (k = 0, lx = x0 + 4; k < g->series; k++) {    /* each series' name in its colour */
        if (!g->name[k][0])
            continue;
        gfx_fill(s, lx, y0 + 4 + (gui_font->height - 6) / 2, 6, 6, g->rgb[k]);
        lx = gfx_text(s, lx + 9, y0 + 2, gui_font, g->name[k], -1, GUI_FIELD_TEXT) + 10;
    }
    snprintf(t, sizeof t, "%d", hi);            /* the scale's top */
    gfx_text(s, x0 + gw - gfx_text_width(gui_font, t, -1) - 4, y0 + 2, gui_font, t, -1, GUI_DIM);
}

static void graph_destroy(struct gui_widget *w) { free(((struct graph *)w)->v); }

static const struct gui_class graph_class = { "graph", 0, graph_draw, NULL, graph_measure, NULL, graph_destroy };

struct gui_widget *gui_graph(struct gui_widget *parent, int series, int points)
{
    static const unsigned colours[SERIES] = { 0x2060D0u, 0xD04020u, 0x20A040u, 0x9030B0u };
    struct graph *g = (struct graph *)gui_new(parent, &graph_class, sizeof *g);
    int k;
    g->series = series < 1 ? 1 : series > SERIES ? SERIES : series;
    g->points = points < 2 ? 2 : points;
    g->v = calloc((size_t)(g->series * g->points), sizeof *g->v);
    for (k = 0; k < SERIES; k++)
        g->rgb[k] = colours[k];
    return &g->w;
}

void gui_graph_series(struct gui_widget *w, int i, const char *name, unsigned rgb)
{
    struct graph *g = (struct graph *)w;
    if (i < 0 || i >= g->series)
        return;
    snprintf(g->name[i], sizeof g->name[i], "%s", name ? name : "");
    if (rgb != GFX_NONE)
        g->rgb[i] = rgb;
    gui_damage(w);
}

void gui_graph_range(struct gui_widget *w, int lo, int hi)
{
    struct graph *g = (struct graph *)w;
    g->lo = lo, g->hi = hi;
    gui_damage(w);
}

void gui_graph_add(struct gui_widget *w, const int *values)    /* a sample of each series */
{
    struct graph *g = (struct graph *)w;
    int k;
    if (!g->v)
        return;
    for (k = 0; k < g->series; k++)
        g->v[k * g->points + g->head] = values[k];
    g->head = (g->head + 1) % g->points;
    if (g->count < g->points)
        g->count++;
    gui_damage(w);
}
