/* The window manager's core (kernel/include/wm.h; PRD §9.4; docs/graphics.md
 * §3).
 *
 * A window keeps its client area's pixels in a surface of its own, in the
 * screen's format: whoever owns the window draws there, and says where it
 * drew (wm_damage). The manager draws the rest, the title bar and border of
 * a framed window, straight onto the screen.
 *
 * The screen is made from the windows by the composer. `dirty` is what of
 * the screen is out of date: a region, added to by everything that changes
 * what is seen. wm_compose() draws each part of it once, from the top
 * window down: what a window covers of the region is drawn from that
 * window and taken out of the region, and what is left at the bottom is
 * desktop. Nothing is drawn and then drawn over, so nothing flickers,
 * though the manager draws on the screen itself. (If the region runs out of
 * room for its rectangles it can only say too much: that one time
 * everything in its bounds is drawn from the bottom up instead.) The
 * pointer goes on last, wherever the parts drawn touch it.
 *
 * Windows are in one list from the bottom up; those marked topmost (the
 * task bar) stay above the rest. The focus is one window or none: its title
 * bar shows it, and the task bar's button for it.
 *
 * The task bar is a window of the manager's own along the bottom: GLOS's
 * name, a button for every framed window, a note and the clock. */
#include "wm.h"


/* "Slate" (PRD Q5, the owner's choice of four): dark title bars and a dark
   task bar on a grey desktop, and orange for the window in front: its close
   box and its task-bar button. Every colour is one of the palette's cube. */
const struct wm_look wm_look_default = {
    1, 18, 10,
    0x666666,
    0x000000, 0x333333, 0xFFFFFF,
    0x999999, 0x333333,
    0xCC6633,
    0x333333, 0xFFFFFF, 0xCC6633, 0xFFFFFF,
    &gfx_font_ui, &gfx_font_bold,
};

#define BAR_H     20                    /* the task bar's height */
#define BAR_NAME  62                    /* its buttons start here, after GLOS's mark and name */
#define BAR_BTN   120                   /* a button's width at most */
#define BAR_RIGHT 110                   /* room for the note and the clock */
#define ICON_W    11

/* The task bar's pictures, one bit a pixel: GLOS's mark (two windows), a
   window (a button's), and a line in use (beside the note). */
static const u8 ico_glos[11][2] = {
    { 0xFE, 0x00 }, { 0xFE, 0x00 }, { 0x82, 0x00 }, { 0x8F, 0xE0 }, { 0x8F, 0xE0 }, { 0x88, 0x20 },
    { 0xF8, 0x20 }, { 0x08, 0x20 }, { 0x08, 0x20 }, { 0x08, 0x20 }, { 0x0F, 0xE0 },
};
static const u8 ico_win[9][2] = {
    { 0xFF, 0xE0 }, { 0xFF, 0xE0 }, { 0x80, 0x20 }, { 0x80, 0x20 }, { 0x80, 0x20 }, { 0x80, 0x20 },
    { 0x80, 0x20 }, { 0x80, 0x20 }, { 0xFF, 0xE0 },
};
static const u8 ico_link[7][2] = {
    { 0x01, 0x00 }, { 0xFF, 0x80 }, { 0x01, 0x00 }, { 0x00, 0x00 }, { 0x10, 0x00 }, { 0x3F, 0xE0 }, { 0x10, 0x00 },
};
/* The close box's cross, 10 by 10. */
static const u8 ico_cross[10][2] = {
    { 0x00, 0x00 }, { 0x00, 0x00 }, { 0x21, 0x00 }, { 0x12, 0x00 }, { 0x0C, 0x00 }, { 0x0C, 0x00 }, { 0x12, 0x00 },
    { 0x21, 0x00 }, { 0x00, 0x00 }, { 0x00, 0x00 },
};
#define PTR_W     12
#define PTR_H     19

/* The pointer: an arrow, its outline and its inside. */
static const u8 ptr_edge[PTR_H][2] = {
    { 0x80, 0x00 }, { 0xC0, 0x00 }, { 0xA0, 0x00 }, { 0x90, 0x00 }, { 0x88, 0x00 }, { 0x84, 0x00 }, { 0x82, 0x00 },
    { 0x81, 0x00 }, { 0x80, 0x80 }, { 0x80, 0x40 }, { 0x80, 0x20 }, { 0x83, 0xF0 }, { 0x92, 0x00 }, { 0xA9, 0x00 },
    { 0xC9, 0x00 }, { 0x84, 0x80 }, { 0x04, 0x80 }, { 0x02, 0x40 }, { 0x03, 0xC0 },
};
static const u8 ptr_fill[PTR_H][2] = {
    { 0x00, 0x00 }, { 0x00, 0x00 }, { 0x40, 0x00 }, { 0x60, 0x00 }, { 0x70, 0x00 }, { 0x78, 0x00 }, { 0x7C, 0x00 },
    { 0x7E, 0x00 }, { 0x7F, 0x00 }, { 0x7F, 0x80 }, { 0x7F, 0xC0 }, { 0x7C, 0x00 }, { 0x6C, 0x00 }, { 0x46, 0x00 },
    { 0x06, 0x00 }, { 0x03, 0x00 }, { 0x03, 0x00 }, { 0x01, 0x80 }, { 0x00, 0x00 },
};

/* The pointer's other shapes, drawn: X black, o white. (The up-and-down and the two slanted the other way are
   made from these; the four-way one is the two straight ones together.) */
static const char *const art_busy[] = {
    "XXXXXXXXXXXXX", "XXXXXXXXXXXXX", " XoooooooooX ", " XoooooooooX ", " XooXoXoXooX ", "  XooXoXooX  ", "   XooXooX   ",
    "    XoooX    ", "     XoX     ", "    XoooX    ", "   XoooooX   ", "  XoooXoooX  ", " XoooXoXoooX ", " XooXoXoXooX ",
    " XoXoXoXoXoX ", "XXXXXXXXXXXXX", "XXXXXXXXXXXXX",
};
static const char *const art_text[] = {
    "oooo oooo", "oXXXoXXXo", "ooooXoooo", "   oXo   ", "   oXo   ", "   oXo   ", "   oXo   ", "   oXo   ", "   oXo   ",
    "   oXo   ", "   oXo   ", "   oXo   ", "   oXo   ", "   oXo   ", "   oXo   ", "ooooXoooo", "oXXXoXXXo", "oooo oooo",
};
static const char *const art_hand[] = {
    "     XX          ", "    XooX         ", "    XooX         ", "    XooX         ", "    XooX         ",
    "    XooXXX       ", "    XooXooXXX    ", "    XooXooXooXX  ", "    XooXooXooXoX ", "XXX XooXooXooXooX",
    "XooXXoooooooooooX", "XoooXoooooooooooX", " XooXoooooooooooX", "  XoooooooooooooX", "  XoooooooooooooX",
    "   XooooooooooooX", "   XoooooooooooX ", "    XooooooooooX ", "    XooooooooooX ", "     XooooooooX  ",
    "     XooooooooX  ", "     XXXXXXXXXX  ",
};
static const char *const art_cross[] = {
    "       oXo       ", "       oXo       ", "       oXo       ", "       oXo       ", "       oXo       ",
    "       oXo       ", "       oXo       ", "ooooooooXoooooooo", "XXXXXXXXXXXXXXXXX", "ooooooooXoooooooo",
    "       oXo       ", "       oXo       ", "       oXo       ", "       oXo       ", "       oXo       ",
    "       oXo       ", "       oXo       ",
};
static const char *const art_ew[] = {
    "    oo     oo    ", "   oXo     oXo   ", "  oXXo     oXXo  ", " oXXXoooooooXXXo ", "oXXXXXXXXXXXXXXXo",
    " oXXXoooooooXXXo ", "  oXXo     oXXo  ", "   oXo     oXo   ", "    oo     oo    ",
};
static const char *const art_nwse[] = {
    "ooooooo        ", "oXXXXXo        ", "oXXXXo         ", "oXXXXXo        ", "oXXoXXXo       ", "oXo oXXXo      ",
    "ooo  oXXXo     ", "     oXXXo     ", "     oXXXo  ooo", "      oXXXo oXo", "       oXXXoXXo", "        oXXXXXo",
    "         oXXXXo", "        oXXXXXo", "        ooooooo",
};

static struct wm_cursor cursors[WC_COUNT];
static u8 cursors_made;

static void cur_set(struct wm_cursor *c, int x, int y, int ch)
{
    u8 bit = (u8)(0x80 >> (x & 7));
    if (x < 0 || y < 0 || x > 31 || y > 31)
        return;
    c->edge[y][x / 8] &= (u8)~bit;
    c->fill[y][x / 8] &= (u8)~bit;
    if (ch == 'X')
        c->edge[y][x / 8] |= bit;
    else if (ch == 'o')
        c->fill[y][x / 8] |= bit;
    if (ch == 'X' || ch == 'o') {
        if (x + 1 > c->w) c->w = (u8)(x + 1);
        if (y + 1 > c->h) c->h = (u8)(y + 1);
    }
}

static int cur_get(const struct wm_cursor *c, int x, int y)
{
    u8 bit = (u8)(0x80 >> (x & 7));
    return c->edge[y][x / 8] & bit ? 'X' : c->fill[y][x / 8] & bit ? 'o' : ' ';
}

static void cur_art(struct wm_cursor *c, const char *const *rows, int n, int hx, int hy)
{
    int x, y;
    for (y = 0; y < n; y++)
        for (x = 0; rows[y][x]; x++)
            cur_set(c, x, y, rows[y][x]);
    c->hx = (u8)hx, c->hy = (u8)hy;
}

static void cursors_make(void)
{
    struct wm_cursor *c;
    int x, y, ch;
    if (cursors_made)
        return;
    cursors_made = 1;
    c = &cursors[WC_ARROW];                     /* (the arrow: its bits as they always were) */
    for (y = 0; y < PTR_H; y++)
        for (x = 0; x < PTR_W; x++) {
            u8 bit = (u8)(0x80 >> (x & 7));
            cur_set(c, x, y, ptr_edge[y][x / 8] & bit ? 'X' : ptr_fill[y][x / 8] & bit ? 'o' : ' ');
        }
    c->w = PTR_W, c->h = PTR_H;
    cur_art(&cursors[WC_BUSY], art_busy, 17, 6, 8);
    cur_art(&cursors[WC_TEXT], art_text, 18, 4, 9);
    cur_art(&cursors[WC_HAND], art_hand, 22, 5, 0);
    cur_art(&cursors[WC_CROSS], art_cross, 17, 8, 8);
    cur_art(&cursors[WC_SIZE_EW], art_ew, 9, 8, 4);
    cur_art(&cursors[WC_SIZE_NWSE], art_nwse, 15, 7, 7);
    for (y = 0; y < 17; y++)                    /* up and down: left and right, turned */
        for (x = 0; x < 9; x++)
            cur_set(&cursors[WC_SIZE_NS], x, y, cur_get(&cursors[WC_SIZE_EW], y, x));
    cursors[WC_SIZE_NS].hx = 4, cursors[WC_SIZE_NS].hy = 8;
    for (y = 0; y < 15; y++)                    /* the other slant: mirrored */
        for (x = 0; x < 15; x++)
            cur_set(&cursors[WC_SIZE_NESW], x, y, cur_get(&cursors[WC_SIZE_NWSE], 14 - x, y));
    cursors[WC_SIZE_NESW].hx = 7, cursors[WC_SIZE_NESW].hy = 7;
    c = &cursors[WC_MOVE];                      /* four ways: both straight ones, crossed at their middles */
    for (y = 0; y < 17; y++)
        for (x = 0; x < 17; x++) {
            int a = y >= 4 && y < 13 ? cur_get(&cursors[WC_SIZE_EW], x, y - 4) : ' ';
            int b = x >= 4 && x < 13 ? cur_get(&cursors[WC_SIZE_NS], x - 4, y) : ' ';
            ch = a == 'X' || b == 'X' ? 'X' : a == 'o' || b == 'o' ? 'o' : ' ';
            cur_set(c, x, y, ch);
        }
    c->hx = 8, c->hy = 8;
}

const struct wm_cursor *wm_cursor_shape(int shape)
{
    cursors_make();
    return &cursors[shape >= 0 && shape < WC_COUNT ? shape : WC_ARROW];
}

static void copy_title(char *d, const char *s)
{
    u32 i;
    for (i = 0; s && s[i] && i < WM_TITLE_MAX - 1; i++)
        d[i] = s[i];
    d[i] = 0;
}

static void damage(struct wm *m, const struct gfx_rect *r)
{
    struct gfx_rect all = { 0, 0, m->w, m->h }, c = *r;
    if (gfx_rect_cut(&c, &all))
        gfx_region_add(&m->dirty, &c);
}

void wm_damage_all(struct wm *m)
{
    struct gfx_rect all = { 0, 0, m->w, m->h };
    gfx_region_set(&m->dirty, &all);
}

/* ---- geometry */

/* A framed window, top to bottom: border, title bar, a line, the client
   area, border. */
static int framed(const struct wm_window *w) { return (w->flags & (WF_FRAME | WF_FULL)) == WF_FRAME; }

static s32 top_h(const struct wm *m, const struct wm_window *w)
{
    return framed(w) ? 2 * m->look.border + m->look.title_h : 0;
}

static s32 side(const struct wm *m, const struct wm_window *w) { return framed(w) ? m->look.border : 0; }

int wm_can(const struct wm_window *w, int part)
{
    if (!framed(w))
        return 0;
    if (part == WH_SIZE)
        return !(w->flags & (WF_FIXED | WF_MAX));
    if (part == WH_MAX)
        return !(w->flags & WF_FIXED);
    if (part == WH_MIN)
        return !(w->flags & WF_NOMIN);
    return 0;
}

/* The title bar's boxes, from the right: close, maximise (or restore), minimise, each a square of the bar's
   height. 0 if the window hasn't that one. */
static int title_box(const struct wm *m, const struct wm_window *w, int part, struct gfx_rect *r)
{
    s32 cell = m->look.title_h, x = w->frame.x + w->frame.w - m->look.border - cell;
    if (!framed(w))
        return 0;
    r->y = w->frame.y + m->look.border;
    r->w = r->h = cell;
    r->x = x;
    if (part == WH_CLOSE)
        return 1;
    if (wm_can(w, WH_MAX)) {
        r->x = x -= cell;
        if (part == WH_MAX)
            return 1;
    } else if (part == WH_MAX)
        return 0;
    r->x = x - cell;
    return part == WH_MIN && wm_can(w, WH_MIN);
}

void wm_work(const struct wm *m, struct gfx_rect *out)
{
    out->x = out->y = 0;
    out->w = m->w;
    out->h = m->h - (m->bar ? m->bar->frame.h : 0);
}

#define WM_MIN_W 80                             /* the smallest client area the user can make */
#define WM_MIN_H 40

void wm_limits(const struct wm *m, const struct wm_window *w, s32 *min_w, s32 *min_h, s32 *max_w, s32 *max_h)
{
    (void)m;
    *min_w = w->min_w > 0 ? w->min_w : WM_MIN_W;
    *min_h = w->min_h > 0 ? w->min_h : WM_MIN_H;
    *max_w = w->max_w > 0 ? w->max_w : 4096;
    *max_h = w->max_h > 0 ? w->max_h : 4096;
    if (*max_w < *min_w) *max_w = *min_w;
    if (*max_h < *min_h) *max_h = *min_h;
}

void wm_client(const struct wm *m, const struct wm_window *w, struct gfx_rect *out)
{
    out->x = w->frame.x + side(m, w);
    out->y = w->frame.y + top_h(m, w);
    out->w = w->store.w;
    out->h = w->store.h;
}

static void set_frame(const struct wm *m, struct wm_window *w, s32 x, s32 y)
{
    w->frame.x = x;
    w->frame.y = y;
    w->frame.w = w->store.w + 2 * side(m, w);
    w->frame.h = w->store.h + top_h(m, w) + side(m, w);
}

static int shown(const struct wm_window *w) { return (w->flags & WF_SHOWN) != 0; }

/* ---- the list */

static void unlink(struct wm *m, struct wm_window *w)
{
    if (w->below) w->below->above = w->above; else m->bottom = w->above;
    if (w->above) w->above->below = w->below; else m->top = w->below;
    w->above = w->below = 0;
}

/* Onto the top of its kind: above everything, or just below the topmost ones. */
static void link_top(struct wm *m, struct wm_window *w)
{
    struct wm_window *over = 0, *under = m->top;        /* w goes between `under` and `over` */
    if (w->flags & WF_DESKTOP)                  /* (the desktop's: at the bottom) */
        over = m->bottom, under = 0;
    else if (!(w->flags & WF_TOPMOST))
        while (under && (under->flags & WF_TOPMOST)) {
            over = under;
            under = under->below;
        }
    w->below = under;
    w->above = over;
    if (under) under->above = w; else m->bottom = w;
    if (over) over->below = w; else m->top = w;
}

struct wm_window *wm_find(const struct wm *m, u32 id)
{
    struct wm_window *w;
    for (w = m->bottom; w && w->id != id; w = w->above) ;
    return w;
}

/* ---- the task bar */

static int on_bar(const struct wm_window *w)
{
    return (w->flags & (WF_FRAME | WF_NOBAR)) == WF_FRAME && (w->flags & (WF_SHOWN | WF_MIN));
}

/* The task bar goes while a full-screen window, or another window of the same owner's, has the focus. */
static void bar_fit(struct wm *m)
{
    struct wm_window *w, *f = m->focus;
    int hide = 0;
    if (!m->bar)
        return;
    if (f && (f->flags & WF_FULL))
        hide = 1;
    else if (f && f->owner)
        for (w = m->bottom; w; w = w->above)
            if (w->owner == f->owner && (w->flags & (WF_FULL | WF_SHOWN)) == (WF_FULL | WF_SHOWN))
                hide = 1;
    if (((m->bar->flags & WF_SHOWN) != 0) == !hide)
        return;
    m->bar->flags ^= WF_SHOWN;
    damage(m, &m->bar->frame);
}

/* The bar's buttons are the framed windows that are shown, in the order
   they were made: the k-th's window, and how wide a button is. */
static struct wm_window *bar_button(const struct wm *m, u32 k, s32 *width)
{
    struct wm_window *w, *best;
    u32 n = 0, last = 0, i;
    s32 room = m->w - BAR_NAME - BAR_RIGHT;
    for (w = m->bottom; w; w = w->above)
        n += on_bar(w);
    *width = n && room / (s32)n < BAR_BTN ? room / (s32)n : BAR_BTN;
    if (*width < 8)
        *width = 8;
    for (i = 0; i <= k; i++) {                  /* the window with the (k+1)-th lowest id */
        best = 0;
        for (w = m->bottom; w; w = w->above)
            if (on_bar(w) && w->id > last && (!best || w->id < best->id))
                best = w;
        if (!best)
            return 0;
        last = best->id;
    }
    return best;
}

static void bar_paint(struct wm *m)
{
    const struct wm_look *l = &m->look;
    struct gfx_surface *s = &m->bar->store;
    struct gfx_rect clip;
    struct wm_window *w;
    s32 bw, x, ty = (BAR_H - l->font->height) / 2 + 1, tw;
    u32 k;

    gfx_clip(s, 0);
    gfx_fill(s, 0, 0, s->w, s->h, l->bar);
    gfx_fill(s, 0, 0, s->w, 1, l->edge);
    gfx_mono(s, 6, (BAR_H - 11) / 2 + 1, &ico_glos[0][0], ICON_W, 11, 2, l->bar_text, GFX_NONE);
    gfx_text(s, 6 + ICON_W + 5, ty, l->bold, "GLOS", -1, l->bar_text);
    for (k = 0; (w = bar_button(m, k, &bw)); k++) {
        u32 ink = w == m->focus ? l->bar_on_text : w->flags & WF_MIN ? l->face_off : l->bar_text;
        x = BAR_NAME + (s32)k * bw;
        clip.x = x, clip.y = 1, clip.w = bw - 2, clip.h = s->h - 1;
        gfx_clip(s, &clip);
        if (w == m->focus)
            gfx_fill(s, x, 3, bw - 2, s->h - 5, l->bar_on);
        else
            gfx_frame(s, x, 3, bw - 2, s->h - 5, l->edge);
        clip.x += 4, clip.w -= 8;
        gfx_clip(s, &clip);
        gfx_mono(s, x + 5, (BAR_H - 9) / 2 + 1, &ico_win[0][0], ICON_W, 9, 2, ink, GFX_NONE);
        gfx_text(s, x + 5 + ICON_W + 4, ty, l->font, w->title, -1, ink);
    }
    gfx_clip(s, 0);
    tw = gfx_text_width(l->font, m->clock, -1);
    gfx_text(s, s->w - 6 - tw, ty, l->font, m->clock, -1, l->bar_text);
    if (m->note[0]) {                           /* someone is connected: the sign, and what by */
        x = s->w - 14 - tw - gfx_text_width(l->bold, m->note, -1);
        gfx_text(s, x, ty, l->bold, m->note, -1, l->bar_text);
        gfx_mono(s, x - ICON_W - 4, (BAR_H - 7) / 2 + 1, &ico_link[0][0], ICON_W, 7, 2, l->bar_text, GFX_NONE);
    }
    damage(m, &m->bar->frame);
}

static void bar_stale(struct wm *m)
{
    if (m->bar)
        bar_paint(m);
}

int wm_bar_mark(const struct wm *m, s32 x, s32 y)
{
    return m->bar && (m->bar->flags & WF_SHOWN) && y >= m->bar->frame.y && x < BAR_NAME;
}

struct wm_window *wm_bar_hit(const struct wm *m, s32 x, s32 y)
{
    s32 bw;
    if (!m->bar || !(m->bar->flags & WF_SHOWN) || y < m->bar->frame.y || x < BAR_NAME)
        return 0;
    if (!bar_button(m, 0, &bw))
        return 0;
    return (x - BAR_NAME) % bw < bw - 2 ? bar_button(m, (u32)((x - BAR_NAME) / bw), &bw) : 0;
}

void wm_bar_text(struct wm *m, const char *clock, const char *note)
{
    u32 i;
    for (i = 0; clock && clock[i] && i < sizeof m->clock - 1; i++)
        m->clock[i] = clock[i];
    m->clock[i] = 0;
    for (i = 0; note && note[i] && i < sizeof m->note - 1; i++)
        m->note[i] = note[i];
    m->note[i] = 0;
    bar_stale(m);
}

void wm_bar(struct wm *m, int on)
{
    if (!on) {
        if (m->bar) {
            struct wm_window *b = m->bar;
            m->bar = 0;
            wm_destroy(m, b);
        }
        return;
    }
    if (m->bar)
        return;
    m->bar = wm_create(m, 0, m->h - BAR_H, m->w, BAR_H, "", WF_TOPMOST | WF_NOFOCUS | WF_FIXED, 0);
    if (m->bar) {
        bar_paint(m);
        wm_show(m, m->bar, 1);
    }
}

/* ---- windows */

void wm_init(struct wm *m, const struct wm_host *host, s32 w, s32 h, const struct gfx_fmt *fmt)
{
    u32 i;
    for (i = 0; i < sizeof *m; i++)
        ((u8 *)m)[i] = 0;
    m->host = *host;
    m->look = wm_look_default;
    m->fmt = *fmt;
    m->w = w;
    m->h = h;
    m->next_id = 1;
    m->px = w / 2;
    m->py = h / 2;
    cursors_make();
    wm_damage_all(m);
}

/* The store for a client area of cw x ch: 0, or -1. */
/* A window's pixels: an app's from pages it can map; the desktop's own with
   rows of a multiple of 32 pixels, which a drawing engine can have. */
static int store_new(struct wm *m, struct gfx_surface *s, void **mem, s32 cw, s32 ch, int kind)
{
    u32 pitch = (kind == WM_MEM_APP ? (u32)cw : ((u32)cw + 31) & ~31u) * (m->fmt.bpp / 8u);
    void *p = m->host.alloc(pitch * (u32)ch, kind);
    if (!p)
        return -1;
    gfx_init(s, p, cw, ch, pitch, &m->fmt);
    s->dev = m->host.dev ? m->host.dev(p) : 0;
    gfx_fill(s, 0, 0, cw, ch, 0xFFFFFF);
    *mem = p;
    return 0;
}

static int kind_of(const struct wm_window *w) { return w->owner ? WM_MEM_APP : WM_MEM_DESK; }

static void store_free(struct wm *m, struct wm_window *w)
{
    m->host.free(w->mem, w->store.pitch * (u32)w->store.h, w->owner ? WM_MEM_APP : w->store.dev ? WM_MEM_DESK : WM_MEM_RAM);
}

int wm_store_again(struct wm *m, struct wm_window *w, int kind)
{
    struct gfx_surface s;
    void *mem;
    if (w->owner || store_new(m, &s, &mem, w->store.w, w->store.h, kind) != 0)
        return -1;
    gfx_blit(&s, 0, 0, &w->store, 0, 0, w->store.w, w->store.h);
    if (w->store.dev)
        w->store.dev->sync(&w->store);          /* (the engine had it: done with it before it goes) */
    store_free(m, w);
    w->store = s;
    w->mem = mem;
    return 0;
}

struct wm_window *wm_create(struct wm *m, s32 x, s32 y, s32 cw, s32 ch, const char *title, u32 flags, void *owner)
{
    struct wm_window *w;
    u32 i;
    if (cw < 1 || ch < 1 || cw > 4096 || ch > 4096 || !(w = m->host.alloc(sizeof *w, WM_MEM_SMALL)))
        return 0;
    for (i = 0; i < sizeof *w; i++)
        ((u8 *)w)[i] = 0;
    w->owner = owner;
    if (store_new(m, &w->store, &w->mem, cw, ch, kind_of(w)) != 0) {
        m->host.free(w, sizeof *w, WM_MEM_SMALL);
        return 0;
    }
    w->id = m->next_id++;
    w->flags = flags & ~(u32)(WF_SHOWN | WF_MIN | WF_PLACED);
    copy_title(w->title, title);
    set_frame(m, w, x, y);
    link_top(m, w);
    m->count++;
    return w;
}

void wm_destroy(struct wm *m, struct wm_window *w)
{
    if (shown(w))
        damage(m, &w->frame);
    unlink(m, w);
    m->count--;
    if (m->focus == w)
        m->focus = 0;
    if (m->bar == w)
        m->bar = 0;
    if (w->store.dev)
        w->store.dev->sync(&w->store);
    store_free(m, w);
    w->flags = 0;
    bar_stale(m);
    bar_fit(m);
    m->host.free(w, sizeof *w, WM_MEM_SMALL);
}

void wm_show(struct wm *m, struct wm_window *w, int on)
{
    if (w->flags & WF_MIN) {                    /* (minimised: shown again as it was, or hidden for good) */
        w->flags &= ~(u32)WF_MIN;
        if (!on) {
            bar_stale(m);
            return;
        }
    }
    if (shown(w) == (on != 0))
        return;
    w->flags = on ? w->flags | WF_SHOWN : w->flags & ~(u32)WF_SHOWN;
    damage(m, &w->frame);
    if (!on && m->focus == w)
        m->focus = 0;
    bar_stale(m);
    bar_fit(m);
}

/* ---- states */

int wm_state_of(const struct wm_window *w)
{
    return w->flags & WF_MIN ? WS_MIN : w->flags & WF_FULL ? WS_FULL : w->flags & WF_MAX ? WS_MAX
         : w->flags & WF_SNAP_L ? WS_SNAP_L : w->flags & WF_SNAP_R ? WS_SNAP_R : WS_NORMAL;
}

/* The frame a place gives the window (its flags that place's already). */
static void place_frame(const struct wm *m, const struct wm_window *w, struct gfx_rect *f)
{
    wm_work(m, f);
    if (w->flags & WF_FULL) {
        f->h = m->h;
    } else if (w->flags & WF_SNAP_L) {
        f->w = f->w / 2;
    } else if (w->flags & WF_SNAP_R) {
        f->x = f->w / 2;
        f->w -= f->x;
    } else if (!(w->flags & WF_MAX)) {          /* its own place again */
        f->x = w->normal.x;
        f->y = w->normal.y;
        f->w = w->normal.w + 2 * side(m, w);
        f->h = w->normal.h + top_h(m, w) + side(m, w);
    }
}

/* The window into frame f: pixels of that client area's size, there. */
static int fit(struct wm *m, struct wm_window *w, const struct gfx_rect *f)
{
    s32 cw = f->w - 2 * side(m, w), ch = f->h - top_h(m, w) - side(m, w);
    if (shown(w))
        damage(m, &w->frame);
    if (wm_resize(m, w, cw < 1 ? 1 : cw, ch < 1 ? 1 : ch) != 0)
        return -1;
    set_frame(m, w, f->x, f->y);
    if (shown(w))
        damage(m, &w->frame);
    return 0;
}

int wm_state(struct wm *m, struct wm_window *w, int state)
{
    u32 was = w->flags, place;
    struct gfx_rect f;
    if (state == WS_MIN) {
        if (!shown(w))
            return 0;                           /* (minimised already, or hidden: nothing to take away) */
        wm_show(m, w, 0);
        w->flags |= WF_MIN;
        bar_stale(m);
        return 0;
    }
    if (w->flags & WF_MIN)                      /* any other state brings it back */
        wm_show(m, w, 1);
    if (state == WS_UNMIN)
        return 0;
    place = state == WS_MAX ? WF_MAX : state == WS_FULL ? WF_FULL : state == WS_SNAP_L ? WF_SNAP_L
          : state == WS_SNAP_R ? WF_SNAP_R : 0;
    if ((w->flags & WF_PLACED) == place)
        return 0;
    if (!(w->flags & WF_PLACED)) {
        w->normal.x = w->frame.x, w->normal.y = w->frame.y;
        w->normal.w = w->store.w, w->normal.h = w->store.h;
    }
    w->flags = (w->flags & ~(u32)WF_PLACED) | place;
    place_frame(m, w, &f);
    if (fit(m, w, &f) != 0) {
        w->flags = (w->flags & ~(u32)WF_PLACED) | (was & WF_PLACED);
        set_frame(m, w, w->frame.x, w->frame.y);
        return -1;
    }
    bar_stale(m);
    bar_fit(m);
    return 0;
}

void wm_move(struct wm *m, struct wm_window *w, s32 x, s32 y)
{
    if (x == w->frame.x && y == w->frame.y)
        return;
    if (shown(w))
        damage(m, &w->frame);
    set_frame(m, w, x, y);
    if (shown(w))
        damage(m, &w->frame);
}

int wm_resize(struct wm *m, struct wm_window *w, s32 cw, s32 ch)
{
    struct gfx_surface s;
    void *mem;
    if (cw < 1 || ch < 1 || cw > 4096 || ch > 4096)
        return -1;
    if (cw == w->store.w && ch == w->store.h)
        return 0;
    if (store_new(m, &s, &mem, cw, ch, kind_of(w)) != 0)
        return -1;
    gfx_blit(&s, 0, 0, &w->store, 0, 0, w->store.w, w->store.h);
    if (w->store.dev)
        w->store.dev->sync(&w->store);
    if (shown(w))
        damage(m, &w->frame);
    store_free(m, w);
    w->store = s;
    w->mem = mem;
    w->has_ext = 0;                             /* (of the old size) */
    set_frame(m, w, w->frame.x, w->frame.y);
    if (shown(w))
        damage(m, &w->frame);
    return 0;
}

void wm_raise(struct wm *m, struct wm_window *w)
{
    struct wm_window *over = w->above;
    if (!over || (!(w->flags & WF_TOPMOST) && (over->flags & WF_TOPMOST)) || (w->flags & WF_DESKTOP))
        return;                                 /* as high as it goes already (the desktop's: never above) */
    unlink(m, w);
    link_top(m, w);
    if (shown(w))
        damage(m, &w->frame);
}

void wm_set_focus(struct wm *m, struct wm_window *w)
{
    struct wm_window *was = m->focus;
    if (w && (!shown(w) || (w->flags & WF_NOFOCUS)))
        return;
    if (w)
        wm_raise(m, w);
    if (was == w)
        return;
    m->focus = w;
    if (was && shown(was))
        damage(m, &was->frame);
    if (w)
        damage(m, &w->frame);
    bar_stale(m);
    bar_fit(m);
}

void wm_title(struct wm *m, struct wm_window *w, const char *title)
{
    copy_title(w->title, title);
    if (shown(w) && framed(w)) {
        struct gfx_rect r = w->frame;
        r.h = top_h(m, w);
        damage(m, &r);
    }
    bar_stale(m);
}

void wm_damage(struct wm *m, struct wm_window *w, const struct gfx_rect *r)
{
    struct gfx_rect c, part;
    if (!shown(w))
        return;
    wm_client(m, w, &c);
    if (r) {
        part.x = c.x + r->x, part.y = c.y + r->y, part.w = r->w, part.h = r->h;
        if (!gfx_rect_cut(&part, &c))
            return;
        c = part;
    }
    damage(m, &c);
}

#define CORNER 12                               /* this near a corner, an edge sizes both ways */

static int part_at(const struct wm *m, const struct wm_window *w, s32 x, s32 y)
{
    const struct wm_look *l = &m->look;
    s32 fx = w->frame.x, fy = w->frame.y, fw = w->frame.w, fh = w->frame.h, b = l->border;
    struct gfx_rect c, box;
    if (!framed(w))
        return WH_CLIENT;
    wm_client(m, w, &c);
    if (wm_can(w, WH_SIZE)) {                   /* its edges: the border, the title bar's top two rows, and outside */
        int n = y < fy + b + 2, so = y >= fy + fh - b, we = x < fx + b, e = x >= fx + fw - b;
        int nn = y < fy + CORNER, ns = y >= fy + fh - CORNER, nw = x < fx + CORNER, ne = x >= fx + fw - CORNER;
        if (n || so)
            return WH_SIZE | (n ? WD_N : WD_S) | (nw ? WD_W : ne ? WD_E : 0);
        if (we || e)
            return WH_SIZE | (we ? WD_W : WD_E) | (nn ? WD_N : ns ? WD_S : 0);
        if (ns && ne)
            return WH_GROW;                     /* its bottom right corner, the client's own pixels there too */
    }
    if (x >= c.x && y >= c.y && x - c.x < c.w && y - c.y < c.h)
        return WH_CLIENT;
    if (y < c.y - b && y >= fy + b) {
        if (title_box(m, w, WH_CLOSE, &box) && x >= box.x && x < box.x + box.w)
            return WH_CLOSE;
        if (title_box(m, w, WH_MAX, &box) && x >= box.x && x < box.x + box.w)
            return WH_MAX;
        if (title_box(m, w, WH_MIN, &box) && x >= box.x && x < box.x + box.w)
            return WH_MIN;
        return WH_TITLE;
    }
    return WH_BORDER;
}

struct wm_window *wm_hit(const struct wm *m, s32 x, s32 y, int *part)
{
    struct wm_window *w;
    for (w = m->top; w; w = w->below) {
        s32 g = wm_can(w, WH_SIZE) ? WM_GRAB : 0;
        if (shown(w) && x >= w->frame.x - g && y >= w->frame.y - g && x - w->frame.x < w->frame.w + g
            && y - w->frame.y < w->frame.h + g)
            break;
    }
    if (part)
        *part = w ? part_at(m, w, x, y) : WH_NONE;
    return w;
}

int wm_cursor_for(const struct wm_window *w, int part)
{
    int d = part & 15;
    if (part == WH_CLIENT)
        return w->cursor;
    if (!(part & WH_SIZE))
        return WC_ARROW;
    if (d == WD_N || d == WD_S)
        return WC_SIZE_NS;
    if (d == WD_W || d == WD_E)
        return WC_SIZE_EW;
    return d == (WD_N | WD_W) || d == (WD_S | WD_E) ? WC_SIZE_NWSE : WC_SIZE_NESW;
}

/* The window to give the focus after w (Alt-Tab): the next one made that can
   have it, round and round; after none, the first. */
struct wm_window *wm_next(const struct wm *m, const struct wm_window *w)
{
    struct wm_window *v, *next = 0, *first = 0;
    for (v = m->bottom; v; v = v->above) {
        if ((!shown(v) && !(v->flags & WF_MIN)) || (v->flags & WF_NOFOCUS))
            continue;                           /* (a minimised one too: whoever gives it the focus brings it back) */
        if (!first || v->id < first->id)
            first = v;
        if (w && v->id > w->id && (!next || v->id < next->id))
            next = v;
    }
    return next ? next : first;
}

static void ptr_rect(const struct wm *m, struct gfx_rect *r)
{
    const struct wm_cursor *c = &cursors[m->cursor];
    r->x = m->px - c->hx, r->y = m->py - c->hy, r->w = c->w, r->h = c->h;
}

void wm_pointer(struct wm *m, s32 x, s32 y, int on)
{
    struct gfx_rect r;
    if (x == m->px && y == m->py && (on != 0) == m->pointer)
        return;
    ptr_rect(m, &r);
    if (m->pointer && !m->hw_pointer)
        damage(m, &r);
    m->px = x < 0 ? 0 : x >= m->w ? m->w - 1 : x;
    m->py = y < 0 ? 0 : y >= m->h ? m->h - 1 : y;
    m->pointer = on != 0;
    ptr_rect(m, &r);
    if (m->pointer && !m->hw_pointer)
        damage(m, &r);
}

void wm_cursor(struct wm *m, int shape)
{
    struct gfx_rect r;
    if (shape < 0 || shape >= WC_COUNT)
        shape = WC_ARROW;
    if (shape == m->cursor)
        return;
    ptr_rect(m, &r);
    if (m->pointer && !m->hw_pointer)
        damage(m, &r);
    m->cursor = (u8)shape;
    m->cursor_gen++;
    ptr_rect(m, &r);
    if (m->pointer && !m->hw_pointer)
        damage(m, &r);
}

void wm_hw_pointer(struct wm *m, int on)
{
    struct gfx_rect r;
    if ((on != 0) == m->hw_pointer)
        return;
    m->hw_pointer = on != 0;
    ptr_rect(m, &r);
    if (m->pointer)                             /* (drawn now, or no longer) */
        damage(m, &r);
}

/* White where the shape is white (XOR 1), black where it is black (XOR 0), both opaque (AND 0); the rest the
   screen (AND 1, XOR 0): as draw_pointer() draws it, its top left corner the cursor's. */
void wm_pointer_masks(const struct wm *m, u8 and_mask[512], u8 xor_mask[512], s32 *hx, s32 *hy)
{
    const struct wm_cursor *c = &cursors[m->cursor];
    s32 x, y;
    for (y = 0; y < 64 * 8; y++)
        and_mask[y] = 0xFF, xor_mask[y] = 0;
    for (y = 0; y < c->h; y++)
        for (x = 0; x < c->w; x++) {
            u8 bit = (u8)(0x80 >> (x & 7));
            int edge = c->edge[y][x / 8] & bit, fill = c->fill[y][x / 8] & bit;
            if (edge || fill) {
                and_mask[y * 8 + x / 8] &= (u8)~bit;
                if (!edge)
                    xor_mask[y * 8 + x / 8] |= bit;
            }
        }
    *hx = c->hx, *hy = c->hy;
}

/* The outline's four sides. */
static void ghost_damage(struct wm *m)
{
    struct gfx_rect g = m->ghost, r;
    if (!m->ghost_on)
        return;
    r = g, r.h = 2;
    damage(m, &r);
    r.y = g.y + g.h - 2;
    damage(m, &r);
    r = g, r.w = 2;
    damage(m, &r);
    r.x = g.x + g.w - 2;
    damage(m, &r);
}

void wm_ghost(struct wm *m, const struct gfx_rect *r)
{
    if (r && m->ghost_on && r->x == m->ghost.x && r->y == m->ghost.y && r->w == m->ghost.w && r->h == m->ghost.h)
        return;
    ghost_damage(m);
    m->ghost_on = r != 0;
    if (r)
        m->ghost = *r;
    ghost_damage(m);
}

void wm_ext(struct wm *m, struct wm_window *w, const struct gfx_surface *s)
{
    w->has_ext = s && s->w == w->store.w && s->h == w->store.h;
    if (w->has_ext)
        w->ext = *s;
    wm_damage(m, w, 0);
}

void wm_screen(struct wm *m, s32 w, s32 h, const struct gfx_fmt *fmt)
{
    struct wm_window *v;
    int same = fmt->bpp == m->fmt.bpp && fmt->rpos == m->fmt.rpos && fmt->gpos == m->fmt.gpos && fmt->gsize == m->fmt.gsize;
    int bar = m->bar != 0;
    if (bar)
        wm_bar(m, 0);                           /* (its size is the screen's: made again) */
    m->fmt = *fmt;
    m->w = w;
    m->h = h;
    for (v = m->bottom; v && !same; v = v->above) {    /* pixels of another format mean nothing: cleared */
        struct gfx_surface s;
        void *mem;
        v->has_ext = 0;
        if (store_new(m, &s, &mem, v->store.w, v->store.h, kind_of(v)) == 0) {
            if (v->store.dev)
                v->store.dev->sync(&v->store);
            store_free(m, v);
            v->store = s;
            v->mem = mem;
        }
    }
    if (bar)
        wm_bar(m, 1);
    for (v = m->bottom; v; v = v->above)        /* those the manager places: to this screen's size */
        if (v->flags & WF_PLACED) {
            struct gfx_rect f;
            place_frame(m, v, &f);
            fit(m, v, &f);
        }
    bar_fit(m);
    wm_damage_all(m);
}

/* ---- the composer */

/* What of window w lies in the clip rectangle of the screen, drawn. */
static void draw_window(const struct wm *m, struct gfx_surface *scr, const struct wm_window *w)
{
    const struct wm_look *l = &m->look;
    struct gfx_rect c, in = scr->clip, bar, keep = scr->clip;
    int focus = w == m->focus;

    wm_client(m, w, &c);
    if (framed(w) && !(in.x >= c.x && in.y >= c.y && in.x + in.w <= c.x + c.w && in.y + in.h <= c.y + c.h)) {
        s32 b = l->border, x = w->frame.x, y = w->frame.y, fw = w->frame.w, fh = w->frame.h, i;
        for (i = 0; i < b; i++)
            gfx_frame(scr, x + i, y + i, fw - 2 * i, fh - 2 * i, l->edge);
        gfx_fill(scr, x + b, y + b + l->title_h, fw - 2 * b, b, l->edge);
        bar.x = x + b, bar.y = y + b, bar.w = fw - 2 * b, bar.h = l->title_h;
        gfx_fill(scr, bar.x, bar.y, bar.w, bar.h, focus ? l->face : l->face_off);
        if (gfx_rect_cut(&bar, &keep)) {        /* its boxes and the title, inside the bar and what is being drawn */
            u32 ink = focus ? l->text : l->text_off, back = focus ? l->face : l->face_off;
            s32 o = (l->title_h - l->box) / 2, left = x + fw - b;
            struct gfx_rect r;
            gfx_clip(scr, &bar);
            if (title_box(m, w, WH_CLOSE, &r)) {        /* a box, the mark's colour in front, with a cross */
                if (focus && l->mark != GFX_NONE)
                    gfx_fill(scr, r.x + o, r.y + o, l->box, l->box, l->mark);
                gfx_frame(scr, r.x + o, r.y + o, l->box, l->box, ink);
                gfx_mono(scr, r.x + o, r.y + o, &ico_cross[0][0], 10, 10, 2, ink, GFX_NONE);
                left = r.x;
            }
            if (title_box(m, w, WH_MAX, &r)) {
                if (w->flags & WF_MAX) {                /* restore: one box in front of another */
                    gfx_frame(scr, r.x + o + 3, r.y + o, l->box - 3, l->box - 3, ink);
                    gfx_fill(scr, r.x + o, r.y + o + 3, l->box - 3, l->box - 3, back);
                    gfx_frame(scr, r.x + o, r.y + o + 3, l->box - 3, l->box - 3, ink);
                } else {                                /* maximise: a box with a title bar of its own */
                    gfx_frame(scr, r.x + o, r.y + o, l->box, l->box, ink);
                    gfx_fill(scr, r.x + o, r.y + o + 1, l->box, 1, ink);
                }
                left = r.x;
            }
            if (title_box(m, w, WH_MIN, &r)) {          /* minimise: a line along the bottom */
                gfx_fill(scr, r.x + o, r.y + o + l->box - 2, l->box, 2, ink);
                left = r.x;
            }
            r = bar;
            r.x = x + b, r.w = left - r.x - 2;
            if (r.w > 0 && gfx_rect_cut(&r, &bar)) {
                gfx_clip(scr, &r);
                gfx_text(scr, x + b + 6, y + b + (l->title_h - l->bold->height) / 2 + 1, focus ? l->bold : l->font, w->title, -1,
                         ink);
            }
            gfx_clip(scr, &keep);
        }
    }
    gfx_blit(scr, c.x, c.y, w->has_ext ? &w->ext : &w->store, 0, 0, c.w, c.h);
}

/* What is above every window: the outline (black outside, white inside: it
   shows on anything), and the pointer. */
static void draw_pointer(const struct wm *m, struct gfx_surface *scr)
{
    if (m->ghost_on) {
        gfx_frame(scr, m->ghost.x, m->ghost.y, m->ghost.w, m->ghost.h, 0x000000);
        gfx_frame(scr, m->ghost.x + 1, m->ghost.y + 1, m->ghost.w - 2, m->ghost.h - 2, 0xFFFFFF);
    }
    if (!m->pointer || m->hw_pointer)
        return;
    {
        const struct wm_cursor *c = &cursors[m->cursor];
        gfx_mono(scr, m->px - c->hx, m->py - c->hy, &c->fill[0][0], c->w, c->h, 4, 0xFFFFFF, GFX_NONE);
        gfx_mono(scr, m->px - c->hx, m->py - c->hy, &c->edge[0][0], c->w, c->h, 4, 0x000000, GFX_NONE);
    }
}

int wm_compose(struct wm *m, struct gfx_surface *scr)
{
    struct gfx_region todo = m->dirty, vis;
    struct gfx_rect all = { 0, 0, m->w, m->h }, bounds, keep = scr->clip;
    struct wm_window *w;
    u32 i;
    int rough = todo.rough;

    if (!todo.n)
        return 0;
    gfx_region_set(&m->dirty, 0);
    gfx_region_cut(&todo, &all);
    gfx_region_bounds(&todo, &bounds);
    for (w = m->top; w && todo.n && !rough; w = w->below) {
        if (!shown(w))
            continue;
        vis = todo;
        gfx_region_cut(&vis, &w->frame);
        for (i = 0; i < vis.n; i++) {
            gfx_clip(scr, &vis.r[i]);
            draw_window(m, scr, w);
            draw_pointer(m, scr);
        }
        gfx_region_sub(&todo, &w->frame);
        rough = todo.rough;
    }
    if (rough) {                                /* too many pieces: everything in the bounds, from the bottom up */
        gfx_clip(scr, &bounds);
        gfx_fill(scr, bounds.x, bounds.y, bounds.w, bounds.h, m->look.desk);
        for (w = m->bottom; w; w = w->above)
            if (shown(w))
                draw_window(m, scr, w);
        draw_pointer(m, scr);
    } else {
        for (i = 0; i < todo.n; i++) {          /* what no window covers */
            gfx_clip(scr, &todo.r[i]);
            gfx_fill(scr, todo.r[i].x, todo.r[i].y, todo.r[i].w, todo.r[i].h, m->look.desk);
            draw_pointer(m, scr);
        }
    }
    gfx_clip(scr, &keep);
    return 1;
}
