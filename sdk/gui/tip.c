/* Tooltips (toolkit 2, step C2; docs/v1/toolkit-2.md, the widgets' table): a line of text in a small window of
 * its own by the pointer, once the pointer has rested over a widget that has one (gui_tip) or over a part of a
 * widget that says it has one (gui_tip_at, from the widget's own handling of the pointer's moves: a toolbar's
 * button). It goes when the pointer moves on to something else, at a press, a key or the wheel, and when its
 * window loses the keyboard; it does not come back for the same thing until the pointer has been elsewhere.
 * The window is a pop-up shown without the keyboard: the app's window keeps it. */
#include <stdio.h>

#include "guiint.h"

#define TIP_AFTER 600                   /* milliseconds the pointer rests first */

static struct {
    struct gui_widget *w;               /* what the pointer is over that has a tooltip (NULL: nothing) ... */
    int key;                            /* ... and which part of it */
    char text[160];
    unsigned since;
    int x, y;                           /* the pointer, in the window's coordinates */
    int done;                           /* shown and gone, or dismissed: not again for the same thing */
    struct gui_window *pop;
    struct gui_widget *cw;              /* the candidate, while a move is being handled */
    int ckey;
    char ctext[160];
} T;

struct tipview {
    struct gui_widget w;
};

static void tip_draw(struct gui_widget *w, struct gfx_surface *s)
{
    gfx_fill(s, w->r.x, w->r.y, w->r.w, w->r.h, GUI_FIELD);
    gfx_frame(s, w->r.x, w->r.y, w->r.w, w->r.h, GUI_LINE);
    gfx_text(s, w->r.x + 4, w->r.y + 2, gui_font, T.text, -1, GUI_FIELD_TEXT);
}

static const struct gui_class tip_class = { "tip", 0, tip_draw, NULL, NULL, NULL, NULL };

void gui_tip_hide(void)
{
    if (T.pop) {
        struct gui_window *p = T.pop;
        T.pop = NULL;
        gui_close_later(p);
    }
    if (T.w)
        T.done = 1;
}

/* A widget gone: no tooltip of its. */
void gui_tip_forget(struct gui_widget *w)
{
    if (T.w == w) {
        gui_tip_hide();
        T.w = NULL;
    }
    if (T.cw == w)
        T.cw = NULL;
}

/* While the pointer's move is handled: what a tooltip here would say (text NULL or "": nothing). key tells the
   parts of a widget apart. */
void gui_tip_at(struct gui_widget *w, int key, const char *text)
{
    T.cw = text && text[0] ? w : NULL;
    T.ckey = key;
    snprintf(T.ctext, sizeof T.ctext, "%s", text ? text : "");
}

/* The move handled, the pointer at x, y of the widget's window. */
void gui_tip_moved(int x, int y)
{
    if (T.cw != T.w || T.ckey != T.key || strcmp(T.ctext, T.text)) {
        gui_tip_hide();
        T.w = T.cw, T.key = T.ckey;
        memcpy(T.text, T.ctext, sizeof T.text);
        T.since = gui_now();
        T.done = 0;
    }
    T.x = x, T.y = y;
}

unsigned gui_tip_due(unsigned now, unsigned wait)
{
    if (T.w && !T.pop && !T.done) {
        int d = (int)(T.since + TIP_AFTER - now);
        if (d < 0)
            d = 0;
        if ((unsigned)d < wait)
            wait = (unsigned)d;
    }
    return wait;
}

void gui_tip_fire(unsigned now)
{
    struct gui_window *p;
    struct gui_widget *v;
    int ox, oy, sw, sh, tw, th, sx, sy;
    if (!T.w || T.pop || T.done || (int)(now - T.since) < TIP_AFTER)
        return;
    if (!T.w->win || !gui_window_active(T.w->win) || gui_origin(T.w->win, &ox, &oy) != 0) {
        T.done = 1;
        return;
    }
    tw = gfx_text_width(gui_font, T.text, -1) + 8, th = gui_font->height + 4;
    gui_screen_size(&sw, &sh);
    sx = ox + T.x, sy = oy + T.y + 18;          /* under the pointer's arrow */
    if (sx + tw > sw)
        sx = sw - tw;
    if (sx < 0)
        sx = 0;
    if (sy + th > sh - 20)                      /* (above the task bar: over the pointer instead) */
        sy = oy + T.y - th - 4;
    p = gui_window_at("", sx, sy, tw, th, GUI_W_POPUP);
    p->owner = T.w->win;
    v = gui_new(p->root, &tip_class, sizeof(struct tipview));
    v->grow = 1;
    T.pop = p;
    gui_show_nofocus(p, sx, sy);
}

void gui_tip(struct gui_widget *w, const char *text)
{
    free(w->tip);
    w->tip = text && text[0] ? strdup(text) : NULL;
}
