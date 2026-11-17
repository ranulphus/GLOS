/* The wizard (toolkit 2, step C8; docs/v1/toolkit-2.md, Widgets): Disk Tools' and the first start's pages with
 * Back, Next and Finish.
 *
 * The app adds pages (gui_wizard_page: a title, a line or two saying what the page is for, and a box the app fills)
 * and hears GUI_ON_CHANGE when the page shown changes (gui_wizard_at; the app may send it elsewhere with
 * gui_wizard_go), GUI_ON_ACTIVATE when Finish is pressed and GUI_ON_CLOSE when Cancel is. Next (Enter) and Back
 * skip the pages the app has skipped (gui_wizard_skip); Next is Finish on the last page; the app holds Next back
 * until a page is filled in (gui_wizard_next_ok). With GUI_WIZ_STEPS the pages' titles are listed down the left,
 * the one shown in bold, the ones skipped left out. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guiint.h"

#define MAXPAGES 16
#define STEPS_W 130
#define PAD 10

struct wpage {
    struct gui_widget *box;
    char title[48];
    char text[200];
    int skip;
};

struct wizard {
    struct gui_widget w;
    unsigned flags;
    struct wpage page[MAXPAGES];
    int n, at;
    int next_ok;
    struct gui_widget *back, *next, *cancel;
};

#define WZ(w) ((struct wizard *)(w))

/* The text in lines no wider than width, at spaces: drawn from y on (draw 0: only counted); the height taken. */
static int wrap(struct gfx_surface *s, int x, int y, int width, const char *text, unsigned rgb)
{
    const char *p = text;
    int h = 0;
    while (*p) {
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

static int left_w(struct wizard *z) { return z->flags & GUI_WIZ_STEPS ? STEPS_W : 0; }

static int head_h(struct wizard *z)            /* the title and the text of the page shown */
{
    int h = gui_bold->height + 2 + PAD * 2, i;
    for (i = 0; i < z->n; i++) {                /* (as tall as the tallest page's, so that pages don't jump) */
        int t = gui_bold->height + 2 + PAD * 2
                + (z->page[i].text[0] ? 4 + wrap(NULL, 0, 0, z->w.r.w - left_w(z) - 2 * PAD, z->page[i].text, 0) : 0);
        h = t > h ? t : h;
    }
    return h;
}

static int buttons_h(void) { return GUI_FIELD_H + 6 + 2 * PAD; }

static void wz_measure(struct gui_widget *w, int *pw, int *ph)
{
    struct wizard *z = WZ(w);
    int i, mw = 260, mh = 120;
    for (i = 0; i < z->n; i++) {
        int cw, ch;
        gui_measure(z->page[i].box, &cw, &ch);
        mw = cw > mw ? cw : mw, mh = ch > mh ? ch : mh;
    }
    *pw = left_w(z) + mw + 2 * PAD;
    *ph = gui_bold->height + 2 + PAD * 2 + 3 * (gui_font->height + 2) + mh + PAD + buttons_h();
}

static void wz_layout(struct gui_widget *w)
{
    struct wizard *z = WZ(w);
    int i, x = w->r.x + left_w(z), top = w->r.y + head_h(z) + 1, bottom = w->r.y + w->r.h - buttons_h();
    int bw, bh = GUI_FIELD_H + 6, by = w->r.y + w->r.h - PAD - bh, bx = w->r.x + w->r.w - PAD;
    for (i = 0; i < z->n; i++)
        gui_place(z->page[i].box, x + PAD, top + PAD, w->r.w - left_w(z) - 2 * PAD, bottom - top - PAD);
    gui_measure(z->cancel, &bw, &bh);
    bh = GUI_FIELD_H + 6, bw = bw > 80 ? bw : 80;
    gui_place(z->cancel, bx - bw, by, bw, bh);
    bx -= bw + 12;
    gui_place(z->next, bx - bw, by, bw, bh);
    bx -= bw + 4;
    gui_place(z->back, bx - bw, by, bw, bh);
}

static void wz_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct wizard *z = WZ(w);
    int x = w->r.x + left_w(z), hh = head_h(z), i, y;
    struct wpage *p = z->n ? &z->page[z->at] : NULL;
    gfx_fill(s, w->r.x, w->r.y, w->r.w, w->r.h, GUI_FACE);
    gfx_fill(s, x, w->r.y, w->r.w - left_w(z), hh, GUI_FIELD);  /* the page's head, on the field's ground */
    gfx_fill(s, x, w->r.y + hh, w->r.w - left_w(z), 1, GUI_LINE);
    gfx_fill(s, w->r.x, w->r.y + w->r.h - buttons_h(), w->r.w, 1, GUI_LINE);
    if (p) {
        gfx_text(s, x + PAD, w->r.y + PAD, gui_bold, p->title, -1, GUI_FIELD_TEXT);
        if (p->text[0])
            wrap(s, x + PAD + 8, w->r.y + PAD + gui_bold->height + 6, w->r.w - left_w(z) - 2 * PAD - 8, p->text,
                 GUI_FIELD_TEXT);
    }
    if (z->flags & GUI_WIZ_STEPS) {             /* the steps: the pages not skipped, the one shown in bold */
        gfx_fill(s, w->r.x, w->r.y, STEPS_W, w->r.h - buttons_h(), GUI_SHADE);
        for (i = 0, y = w->r.y + PAD; i < z->n; i++) {
            const struct gfx_font *f = i == z->at ? gui_bold : gui_font;
            if (z->page[i].skip)
                continue;
            if (i == z->at)
                gfx_fill(s, w->r.x + 4, y + 2, 4, f->height - 2, GUI_MARK);
            gui_text_fit(s, w->r.x + 12, y, STEPS_W - 16, f, z->page[i].title, i < z->at ? GUI_DIM : GUI_TEXT);
            y += GUI_ROW_H + 2;
        }
    }
}

static void show(struct wizard *z)
{
    int i, first, last;
    for (i = 0; i < z->n; i++)
        gui_visible(z->page[i].box, i == z->at);
    for (first = 0; first < z->n && z->page[first].skip; first++)
        ;
    for (last = z->n - 1; last >= 0 && z->page[last].skip; last--)
        ;
    gui_enable(z->back, z->at > first);
    gui_enable(z->next, z->next_ok);
    if (strcmp(gui_get_text(z->next), z->at >= last ? "&Finish" : "&Next >"))
        gui_set_text(z->next, z->at >= last ? "&Finish" : "&Next >");
    gui_damage(&z->w);
    if (z->w.win)
        gui_relayout(z->w.win);
}

static struct gui_widget *first_focus(struct gui_widget *w)     /* the first widget in w that takes the keyboard */
{
    struct gui_widget *c, *f;
    if (w->flags & (GUI_HIDDEN | GUI_DISABLED))
        return NULL;
    if (w->cls->flags & GUI_C_FOCUS)
        return w;
    for (c = w->first; c; c = c->next)
        if ((f = first_focus(c)) != NULL)
            return f;
    return NULL;
}

static void step(struct wizard *z, int dir)
{
    int i = z->at + dir;
    while (i >= 0 && i < z->n && z->page[i].skip)
        i += dir;
    if (i < 0 || i >= z->n)
        return;
    z->at = i;
    z->next_ok = 1;                             /* (each page starts with Next usable: the app may hold it back) */
    show(z);
    gui_send(&z->w, GUI_ON_CHANGE);
    {                                           /* the keyboard to the page's first control (else Next) */
        struct gui_widget *f = first_focus(z->page[z->at].box);
        gui_focus(f ? f : z->next->flags & GUI_DISABLED ? z->back : z->next);
    }
}

static void back_click(struct gui_widget *b, int what) { (void)what; step(WZ(b->parent), -1); }

static void next_click(struct gui_widget *b, int what)
{
    struct wizard *z = WZ(b->parent);
    int last;
    (void)what;
    for (last = z->n - 1; last >= 0 && z->page[last].skip; last--)
        ;
    if (z->at >= last)
        gui_send(&z->w, GUI_ON_ACTIVATE);       /* Finish */
    else
        step(z, 1);
}

static void cancel_click(struct gui_widget *b, int what) { (void)what; gui_send(b->parent, GUI_ON_CLOSE); }

static const struct gui_class wz_class = { "wizard", 0, wz_draw, NULL, wz_measure, wz_layout, NULL };

struct gui_widget *gui_wizard(struct gui_widget *parent, unsigned flags)
{
    struct wizard *z = (struct wizard *)gui_new(parent, &wz_class, sizeof *z);
    z->flags = flags;
    z->next_ok = 1;
    z->back = gui_button(&z->w, "< &Back", back_click, 0);
    z->next = gui_button(&z->w, "&Next >", next_click, 0);
    z->next->flags |= GUI_DEFAULT;
    z->cancel = gui_button(&z->w, "Cancel", cancel_click, 0);
    z->cancel->flags |= GUI_CANCEL;
    return &z->w;
}

struct gui_widget *gui_wizard_page(struct gui_widget *w, const char *title, const char *text)
{
    struct wizard *z = WZ(w);
    struct wpage *p;
    if (z->n == MAXPAGES)
        return NULL;
    p = &z->page[z->n++];
    snprintf(p->title, sizeof p->title, "%s", title ? title : "");
    snprintf(p->text, sizeof p->text, "%s", text ? text : "");
    p->box = gui_vbox(w, GUI_GAP, 0);
    show(z);
    return p->box;
}

void gui_wizard_go(struct gui_widget *w, int page)
{
    struct wizard *z = WZ(w);
    if (page < 0 || page >= z->n || page == z->at)
        return;
    z->at = page;
    z->next_ok = 1;
    show(z);
}

int gui_wizard_at(struct gui_widget *w) { return WZ(w)->at; }

void gui_wizard_next_ok(struct gui_widget *w, int ok)
{
    WZ(w)->next_ok = ok != 0;
    show(WZ(w));
}

void gui_wizard_skip(struct gui_widget *w, int page, int skip)
{
    struct wizard *z = WZ(w);
    if (page >= 0 && page < z->n) {
        z->page[page].skip = skip != 0;
        show(z);
    }
}
