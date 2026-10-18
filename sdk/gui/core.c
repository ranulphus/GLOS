/* The toolkit's core (include/glos/gui.h; docs/gui.md): windows, the tree
 * of widgets, layout, events and the loop.
 *
 * A window's GLOS window is made when it is first shown, at the size its
 * widgets want if it is a dialog or was given none. Drawing: what is damaged
 * is drawn into the window's back buffer, widget by widget from the root
 * down, each clipped to its rectangle (and to its parents'), then copied
 * into the window's pixels and presented, a rectangle at a time. */
#include "guiint.h"

const struct gfx_font *gui_font = &gfx_font_ui, *gui_bold = &gfx_font_bold, *gui_mono = &gfx_font_mono;

#define MAX_TIMERS 32
#define MAX_IDLE   8

static struct {
    struct gui_window *wins;
    int quit, code;
    struct gui_window *cmd_win;         /* a command to run when the event is done (gui_post_command) */
    int cmd_id, cmd;
    struct gui_window *modal;           /* the innermost modal window */
    struct { struct gui_widget *w; unsigned ms, due; } timer[MAX_TIMERS];
    struct { void (*fn)(void *); void *arg; unsigned ms, due; } idle[MAX_IDLE];
} G;

unsigned gui_now(void) { return glos_ticks(); }

static unsigned dblclick = GUI_DBLCLICK;       /* [input] double_click (M9) */

int gui_init(int argc, char **argv)
{
    char v[16];
    if (glos_init() != 0)
        return -1;
    if (glos_config_get("input", "double_click", v, sizeof v) >= 0 && atoi(v) >= 200 && atoi(v) <= 1000)
        dblclick = (unsigned)atoi(v);
    gui_smooth_start(argc > 0 ? argv[0] : NULL);
    return 0;
}

void gui_screen_size(int *w, int *h)
{
    struct glos_window s;
    memset(&s, 0, sizeof s);
    if (glos_screen(&s) != 0)
        s.w = 640, s.h = 480;
    *w = s.w, *h = s.h;
}

/* ---- the tree */

struct gui_widget *gui_new(struct gui_widget *parent, const struct gui_class *cls, size_t size)
{
    struct gui_widget *w = calloc(1, size);
    if (!w)
        abort();
    w->cls = cls;
    if (parent) {
        struct gui_widget *before = NULL;
        w->win = parent->win;
        w->parent = parent;
        if (parent->win && parent == parent->win->root && parent->last && parent->last->cls == &gui_status_class)
            before = parent->last;              /* (the status bar stays last) */
        if (before) {
            w->next = before;
            w->prev = before->prev;
            if (before->prev)
                before->prev->next = w;
            else
                parent->first = w;
            before->prev = w;
        } else {
            w->prev = parent->last;
            if (parent->last)
                parent->last->next = w;
            else
                parent->first = w;
            parent->last = w;
        }
        if (w->win)
            gui_relayout(w->win);
    }
    return w;
}

static void drop_timers(struct gui_widget *w)
{
    int i;
    for (i = 0; i < MAX_TIMERS; i++)
        if (G.timer[i].w == w)
            G.timer[i].w = NULL;
}

static void destroy(struct gui_widget *w)
{
    struct gui_window *win = w->win;
    while (w->first) {
        struct gui_widget *c = w->first;
        w->first = c->next;
        destroy(c);
    }
    if (win) {
        if (win->focus == w)
            win->focus = NULL;
        if (win->capture == w)
            win->capture = NULL;
        if (win->hover == w)
            win->hover = NULL;
    }
    drop_timers(w);
    if (w->cls->destroy)
        w->cls->destroy(w);
    free(w->text);
    free(w);
}

static void unlink_widget(struct gui_widget *w)
{
    struct gui_widget *p = w->parent;
    if (!p)
        return;
    if (w->prev)
        w->prev->next = w->next;
    else
        p->first = w->next;
    if (w->next)
        w->next->prev = w->prev;
    else
        p->last = w->prev;
    w->parent = w->next = w->prev = NULL;
}

void gui_remove(struct gui_widget *w)
{
    struct gui_window *win = w->win;
    if (win && w->parent)
        gui_damage(w->parent);
    unlink_widget(w);
    destroy(w);
    if (win)
        gui_relayout(win);
}

void gui_clear(struct gui_widget *w)
{
    while (w->first)
        gui_remove(w->first);
    gui_damage(w);
}

int gui_visible_in(struct gui_widget *w)
{
    for (; w; w = w->parent)
        if (w->flags & GUI_HIDDEN)
            return 0;
    return 1;
}

static int usable(struct gui_widget *w)
{
    for (; w; w = w->parent)
        if (w->flags & (GUI_HIDDEN | GUI_DISABLED))
            return 0;
    return 1;
}

void gui_damage(struct gui_widget *w)
{
    if (w && w->win)
        gui_damage_rect(w->win, &w->r);
}

void gui_damage_rect(struct gui_window *win, const struct gfx_rect *r)
{
    struct gfx_rect c = *r, all = { 0, 0, win->w, win->h };
    if (gfx_rect_cut(&c, &all))
        gfx_region_add(&win->dirty, &c);
}

void gui_relayout(struct gui_window *w)
{
    if (w)
        w->relayout = 1;
}

void gui_enable(struct gui_widget *w, int on)
{
    unsigned f = on ? w->flags & ~GUI_DISABLED : w->flags | GUI_DISABLED;
    if (f != w->flags) {
        w->flags = f;
        if (!on && w->win && w->win->focus == w)
            gui_focus_next(w->win, 0);
        gui_damage(w);
    }
}

void gui_visible(struct gui_widget *w, int on)
{
    unsigned f = on ? w->flags & ~GUI_HIDDEN : w->flags | GUI_HIDDEN;
    if (f != w->flags) {
        w->flags = f;
        if (w->parent)
            gui_damage(w->parent);
        if (w->win)
            gui_relayout(w->win);
    }
}

void gui_set_mnemonic(struct gui_widget *w, const char *text)
{
    const char *p;
    w->mnemonic = 0;
    for (p = text; p && *p; p++)
        if (*p == '&') {
            if (p[1] == '&') {
                p++;
                continue;
            }
            if (p[1]) {
                unsigned c = (unsigned char)p[1];
                w->mnemonic = c >= 'A' && c <= 'Z' ? c + 32 : c;
            }
            break;
        }
}

void gui_set_text(struct gui_widget *w, const char *text)
{
    char *t = strdup(text ? text : "");
    if (!t)
        abort();
    free(w->text);
    w->text = t;
    gui_set_mnemonic(w, t);
    gui_damage(w);
    if (w->win)
        gui_relayout(w->win);
    if (w->cls == &gui_field_class)             /* (a field keeps its caret in its text) */
        gui_field_changed(w);
}

const char *gui_get_text(struct gui_widget *w) { return w->text ? w->text : ""; }

void gui_size(struct gui_widget *w, int pw, int ph)
{
    w->pw = pw, w->ph = ph;
    if (w->win)
        gui_relayout(w->win);
}

void gui_grow(struct gui_widget *w, int share)
{
    w->grow = share;
    if (w->win)
        gui_relayout(w->win);
}

void gui_on(struct gui_widget *w, void (*on)(struct gui_widget *, int), int id)
{
    w->on = on;
    w->id = id;
}

void gui_send(struct gui_widget *w, int what)
{
    if (w->on)
        w->on(w, what);
}

int gui_is_focus(struct gui_widget *w) { return w->win && w->win->focus == w && w->win->active; }

/* ---- layout */

void gui_measure(struct gui_widget *w, int *pw, int *ph)
{
    int mw = 0, mh = 0;
    if (w->flags & GUI_HIDDEN) {
        *pw = *ph = 0;
        return;
    }
    if (w->cls->measure)
        w->cls->measure(w, &mw, &mh);
    *pw = w->pw > 0 ? w->pw : mw;
    *ph = w->ph > 0 ? w->ph : mh;
}

void gui_place(struct gui_widget *w, int x, int y, int cw, int ch)
{
    w->r.x = x, w->r.y = y, w->r.w = cw, w->r.h = ch;
    if (w->cls->layout)
        w->cls->layout(w);
}

/* ---- boxes */

static void box_measure(struct gui_widget *w, int *pw, int *ph)
{
    struct gui_box *b = (struct gui_box *)w;
    struct gui_widget *c;
    int along = 0, across = 0, n = 0, cw, chh;
    for (c = w->first; c; c = c->next) {
        if (c->flags & GUI_HIDDEN)
            continue;
        gui_measure(c, &cw, &chh);
        if (b->vertical) {
            along += chh;
            if (cw > across)
                across = cw;
        } else {
            along += cw;
            if (chh > across)
                across = chh;
        }
        n++;
    }
    if (n > 1)
        along += b->gap * (n - 1);
    if (b->vertical)
        *pw = across + 2 * b->pad, *ph = along + 2 * b->pad;
    else
        *pw = along + 2 * b->pad, *ph = across + 2 * b->pad;
}

static void box_layout(struct gui_widget *w)
{
    struct gui_box *b = (struct gui_box *)w;
    struct gui_widget *c;
    int avail = (b->vertical ? w->r.h : w->r.w) - 2 * b->pad;
    int across = (b->vertical ? w->r.w : w->r.h) - 2 * b->pad;
    int total = 0, shares = 0, n = 0, extra, pos, cw, chh, given = 0, left;
    for (c = w->first; c; c = c->next) {
        if (c->flags & GUI_HIDDEN)
            continue;
        gui_measure(c, &cw, &chh);
        total += b->vertical ? chh : cw;
        shares += c->grow;
        n++;
    }
    if (n > 1)
        total += b->gap * (n - 1);
    extra = avail - total;
    left = shares;
    pos = b->pad;
    for (c = w->first; c; c = c->next) {
        int size;
        if (c->flags & GUI_HIDDEN) {
            c->r.w = c->r.h = 0;
            continue;
        }
        gui_measure(c, &cw, &chh);
        size = b->vertical ? chh : cw;
        if (c->grow > 0 && shares > 0) {
            int part = left == c->grow ? extra - given : extra * c->grow / shares;
            given += part;
            left -= c->grow;
            size += part;
            if (size < 0)
                size = 0;
        }
        if (b->vertical)
            gui_place(c, w->r.x + b->pad, w->r.y + pos, across, size);
        else
            gui_place(c, w->r.x + pos, w->r.y + b->pad, size, across);
        pos += size + b->gap;
    }
}

static void box_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct gui_box *b = (struct gui_box *)w;
    if (b->bg != GFX_NONE)
        gui_fill(s, &w->r, b->bg);
}

const struct gui_class gui_box_class = { "box", 0, box_draw, NULL, box_measure, box_layout, NULL };

static struct gui_widget *box(struct gui_widget *parent, int vertical, int gap, int pad)
{
    struct gui_box *b = (struct gui_box *)gui_new(parent, &gui_box_class, sizeof *b);
    b->vertical = vertical;
    b->gap = gap < 0 ? GUI_GAP : gap;
    b->pad = pad < 0 ? GUI_PAD : pad;
    b->bg = GFX_NONE;
    return &b->w;
}

struct gui_widget *gui_vbox(struct gui_widget *parent, int gap, int pad) { return box(parent, 1, gap, pad); }
struct gui_widget *gui_hbox(struct gui_widget *parent, int gap, int pad) { return box(parent, 0, gap, pad); }

static void space_measure(struct gui_widget *w, int *pw, int *ph) { (void)w, *pw = 0, *ph = 0; }
static const struct gui_class space_class = { "space", 0, NULL, NULL, space_measure, NULL, NULL };

struct gui_widget *gui_space(struct gui_widget *parent, int w, int h)
{
    struct gui_widget *s = gui_new(parent, &space_class, sizeof *s);
    s->pw = w, s->ph = h;
    if (!w && !h)
        s->grow = 1;
    return s;
}

/* ---- hit-testing and drawing (children clipped to their parents) */

static struct gui_widget *hit(struct gui_widget *w, int x, int y, struct gfx_rect clip)
{
    struct gui_widget *c, *found;
    struct gfx_rect r = w->r;
    if (w->flags & GUI_HIDDEN || !gfx_rect_cut(&r, &clip) || x < r.x || y < r.y || x >= r.x + r.w || y >= r.y + r.h)
        return NULL;
    for (c = w->last; c; c = c->prev)          /* (the last made is above) */
        if ((found = hit(c, x, y, r)) != NULL)
            return found;
    return w;
}

struct gui_widget *gui_hit(struct gui_widget *w, int x, int y)
{
    struct gfx_rect all = { 0, 0, w->win ? w->win->w : 1 << 20, w->win ? w->win->h : 1 << 20 };
    return hit(w, x, y, all);
}

static void draw_tree(struct gui_widget *w, struct gfx_surface *s, struct gfx_rect clip)
{
    struct gui_widget *c;
    struct gfx_rect r = w->r;
    if (w->flags & GUI_HIDDEN || !gfx_rect_cut(&r, &clip))
        return;
    if (w->cls->draw) {
        gfx_clip(s, &r);
        w->cls->draw(w, s);
    }
    for (c = w->first; c; c = c->next)
        draw_tree(c, s, r);
}

/* The window's pixels and back buffer, found again (an app's memory may have moved: glos_ptr). */
static int surfaces(struct gui_window *win)
{
    if (!win->id || glos_surface(&win->gw, &win->s) != 0)
        return -1;
    if (!win->back_mem || win->back.w != win->w || win->back.h != win->h || win->back.fmt.bpp != win->s.fmt.bpp) {
        unsigned pitch = (unsigned)win->w * (win->s.fmt.bpp / 8u);
        free(win->back_mem);
        win->back_mem = malloc((size_t)pitch * (unsigned)(win->h > 0 ? win->h : 1) + 4);
        if (!win->back_mem)
            abort();
        gfx_init(&win->back, win->back_mem, win->w, win->h, pitch, &win->s.fmt);
        gfx_region_set(&win->dirty, NULL);
        {
            struct gfx_rect all = { 0, 0, win->w, win->h };
            gfx_region_add(&win->dirty, &all);
        }
    }
    return 0;
}

static void layout_window(struct gui_window *win)
{
    struct gfx_rect all = { 0, 0, win->w, win->h };
    win->relayout = 0;
    gui_place(win->root, 0, 0, win->w, win->h);
    gfx_region_set(&win->dirty, &all);
}

static void redraw(struct gui_window *win)
{
    struct gfx_rect all = { 0, 0, win->w, win->h };
    unsigned i;
    if (!win->id || !win->shown || win->closing)
        return;
    if (surfaces(win) != 0)
        return;
    if (win->relayout)
        layout_window(win);
    if (!win->dirty.n)
        return;
    if (win->dirty.rough) {                     /* (too many pieces: all of it, once) */
        gfx_region_set(&win->dirty, &all);
    }
    for (i = 0; i < win->dirty.n; i++)
        draw_tree(win->root, &win->back, win->dirty.r[i]);
    gfx_clip(&win->s, NULL);
    for (i = 0; i < win->dirty.n; i++) {
        const struct gfx_rect *r = &win->dirty.r[i];
        gfx_blit(&win->s, r->x, r->y, &win->back, r->x, r->y, r->w, r->h);
    }
    {
        struct gfx_rect b;
        struct glos_rect g;
        gfx_region_bounds(&win->dirty, &b);
        g.x = b.x, g.y = b.y, g.w = b.w, g.h = b.h;
        glos_present(win->id, &g);
    }
    gfx_region_set(&win->dirty, NULL);
}

/* ---- windows */

static void window_root(struct gui_window *w)
{
    struct gui_box *b = (struct gui_box *)gui_new(NULL, &gui_box_class, sizeof *b);
    b->vertical = 1;
    b->gap = 0;
    b->pad = 0;
    b->bg = GUI_FACE;
    b->w.win = w;
    w->root = &b->w;
}

struct gui_window *gui_window_at(const char *title, int x, int y, int w, int h, unsigned flags)
{
    struct gui_window *win = calloc(1, sizeof *win);
    if (!win)
        abort();
    win->flags = flags;
    win->w = w, win->h = h;
    win->gw.x = x, win->gw.y = y;
    win->gw.flags = x < 0 && y < 0 ? GLOS_WIN_ANYWHERE : 0;
    strncpy(win->gw.title, title ? title : "", sizeof win->gw.title - 1);
    window_root(win);
    win->next = G.wins;
    G.wins = win;
    return win;
}

struct gui_window *gui_window(const char *title, int w, int h, unsigned flags)
{
    return gui_window_at(title, -1, -1, w, h, flags);
}

struct gui_window *gui_window_of(unsigned id)
{
    struct gui_window *w;
    for (w = G.wins; w; w = w->next)
        if (w->id == id && !w->closing)
            return w;
    return NULL;
}

int gui_window_active(struct gui_window *w) { return w->active; }

void gui_fit(struct gui_window *w)
{
    int pw, ph;
    gui_measure(w->root, &pw, &ph);
    if (pw < 80)
        pw = 80;
    if (ph < 20)
        ph = 20;
    if (w->id)
        gui_resize(w, pw, ph);
    else
        w->w = pw, w->h = ph;
}

void gui_win_realize(struct gui_window *w, int x, int y, int anywhere)
{
    struct glos_window gw;
    unsigned f = 0;
    if (w->id)
        return;
    if (w->flags & GUI_W_DIALOG || w->w <= 0 || w->h <= 0)
        gui_fit(w);
    if (w->flags & (GUI_W_FIXED | GUI_W_DIALOG))
        f |= GLOS_WIN_FIXED;
    if (w->flags & GUI_W_POPUP)
        f |= GLOS_WIN_POPUP;
    if (w->flags & GUI_W_BARE)
        f |= GLOS_WIN_BARE;
    if (w->flags & GUI_W_DESKTOP)
        f |= GLOS_WIN_DESKTOP;
    if (anywhere)
        f |= GLOS_WIN_ANYWHERE;
    memset(&gw, 0, sizeof gw);
    gw.x = x, gw.y = y, gw.w = w->w, gw.h = w->h;
    gw.flags = f;
    memcpy(gw.title, w->gw.title, sizeof gw.title);
    if (glos_window(&gw) != 0)
        return;
    w->gw = gw;
    w->id = gw.id;
    w->w = gw.w, w->h = gw.h;
    w->relayout = 1;
}

/* Where a dialog goes: the middle of its owner, on the screen. */
static void place_dialog(struct gui_window *w, int *x, int *y)
{
    int sw, sh, ox, oy;
    gui_screen_size(&sw, &sh);
    if (w->owner && gui_origin(w->owner, &ox, &oy) == 0) {
        *x = ox + (w->owner->w - w->w) / 2;
        *y = oy + (w->owner->h - w->h) / 2;
    } else {
        *x = (sw - w->w) / 2;
        *y = (sh - w->h) / 3;
    }
    if (*x + w->w + 2 > sw)
        *x = sw - w->w - 2;
    if (*y + w->h + 22 > sh - 20)
        *y = sh - 20 - w->h - 22;
    if (*x < 1)
        *x = 1;
    if (*y < 19)
        *y = 19;
}

static struct gui_widget *first_focus(struct gui_widget *w)
{
    struct gui_widget *c, *f;
    if (w->flags & (GUI_HIDDEN | GUI_DISABLED))
        return NULL;
    if (w->cls->flags & GUI_C_FOCUS && !(w->flags & GUI_NOTAB))
        return w;
    for (c = w->first; c; c = c->next)
        if ((f = first_focus(c)) != NULL)
            return f;
    return NULL;
}

void gui_show(struct gui_window *w, int on)
{
    if (on && !w->id) {
        int x = w->gw.x, y = w->gw.y, any = w->gw.flags & GLOS_WIN_ANYWHERE;
        if (w->flags & GUI_W_DIALOG) {
            gui_fit(w);
            place_dialog(w, &x, &y);
            any = 0;
        }
        gui_win_realize(w, x, y, any);
        if (!w->id)
            return;
    }
    if (!w->id)
        return;
    if (on && !w->shown) {                      /* the first focus, told it has the keyboard (a field chooses its text) */
        struct gui_widget *f = w->focus ? w->focus : first_focus(w->root);
        w->focus = NULL;
        if (f)
            gui_focus(f);
    }
    w->shown = on;
    if (on) {
        redraw(w);                              /* (drawn before it is seen) */
        w->active = 1;
    }
    glos_show(w->id, on);
}

void gui_title(struct gui_window *w, const char *title)
{
    strncpy(w->gw.title, title, sizeof w->gw.title - 1);
    w->gw.title[sizeof w->gw.title - 1] = 0;
    if (w->id)
        glos_title(w->id, title);
}

int gui_origin(struct gui_window *w, int *x, int *y)
{
    struct glos_window gw;
    memset(&gw, 0, sizeof gw);
    if (!w->id || glos_pixels(w->id, &gw) != 0)
        return -1;
    *x = gw.x, *y = gw.y;
    return 0;
}

static void new_pixels(struct gui_window *w)
{
    struct glos_window gw;
    memset(&gw, 0, sizeof gw);
    if (glos_pixels(w->id, &gw) != 0)
        return;
    memcpy(gw.title, w->gw.title, sizeof gw.title);
    w->gw = gw;
    w->w = gw.w, w->h = gw.h;
    w->relayout = 1;
    free(w->back_mem);
    w->back_mem = NULL;
}

void gui_resize(struct gui_window *w, int cw, int ch)
{
    if (!w->id) {
        w->w = cw, w->h = ch;
        return;
    }
    if (glos_resize(w->id, cw, ch) == 0)
        new_pixels(w);
}

void gui_close(struct gui_window *w)
{
    struct gui_window **p;
    if (w->closing)
        return;
    if (w == G.modal) {                         /* (gui_modal frees it) */
        w->closing = 1;
        return;
    }
    for (;;) {                                  /* pop-ups and dialogs of it go first */
        struct gui_window *o;
        for (o = G.wins; o && !(o->owner == w && !o->closing); o = o->next)
            ;
        if (!o)
            break;
        gui_close(o);
    }
    w->closing = 1;
    if (w->id)
        glos_close(w->id);
    destroy(w->root);
    w->root = NULL;
    for (p = &G.wins; *p; p = &(*p)->next)
        if (*p == w) {
            *p = w->next;
            break;
        }
    free(w->back_mem);
    free(w);
}

/* ---- focus */

static void collect(struct gui_widget *w, struct gui_widget **list, int *n, int room)
{
    struct gui_widget *c;
    if (w->flags & (GUI_HIDDEN | GUI_DISABLED))
        return;
    if (w->cls->flags & GUI_C_FOCUS && !(w->flags & GUI_NOTAB) && *n < room)
        list[(*n)++] = w;
    for (c = w->first; c; c = c->next)
        collect(c, list, n, room);
}

void gui_focus(struct gui_widget *w)
{
    struct gui_window *win = w ? w->win : NULL;
    struct gui_widget *old;
    struct gui_ev e;
    if (!win || win->focus == w)
        return;
    old = win->focus;
    win->focus = w;
    memset(&e, 0, sizeof e);
    if (old) {
        e.type = GUI_EV_BLUR;
        if (old->cls->event)
            old->cls->event(old, &e);
        gui_damage(old);
    }
    e.type = GUI_EV_FOCUS;
    if (w->cls->event)
        w->cls->event(w, &e);
    gui_damage(w);
}

void gui_focus_next(struct gui_window *win, int back)
{
    struct gui_widget *list[256];
    int n = 0, i, at = -1;
    collect(win->root, list, &n, 256);
    if (!n)
        return;
    for (i = 0; i < n; i++)
        if (list[i] == win->focus)
            at = i;
    if (at < 0)
        i = back ? n - 1 : 0;
    else
        i = back ? (at + n - 1) % n : (at + 1) % n;
    gui_focus(list[i]);
}

/* ---- events */

/* The letter or digit of a set 1 key (for Alt and a letter), or 0. */
static unsigned key_letter(unsigned key)
{
    static const char row[0x33] = {
        0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', 0, 0, 0, 0,
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', 0, 0, 0, 0, 'a', 's',
        'd', 'f', 'g', 'h', 'j', 'k', 'l', 0, 0, 0, 0, 0, 'z', 'x', 'c', 'v',
        'b', 'n', 'm' };
    return key < sizeof row ? (unsigned char)row[key] : 0;
}

static struct gui_widget *find_mnemonic(struct gui_widget *w, unsigned c)
{
    struct gui_widget *k, *f;
    if (w->flags & (GUI_HIDDEN | GUI_DISABLED))
        return NULL;
    if (w->mnemonic == c && w->cls != &gui_menubar_class)
        return w;
    for (k = w->first; k; k = k->next)
        if ((f = find_mnemonic(k, c)) != NULL)
            return f;
    return NULL;
}

static struct gui_widget *find_flag(struct gui_widget *w, unsigned flag)
{
    struct gui_widget *k, *f;
    if (w->flags & (GUI_HIDDEN | GUI_DISABLED))
        return NULL;
    if (w->flags & flag)
        return w;
    for (k = w->first; k; k = k->next)
        if ((f = find_flag(k, flag)) != NULL)
            return f;
    return NULL;
}

/* The widget that takes the focus after w in Tab's order (a label's). */
static struct gui_widget *walk_after(struct gui_widget *x, struct gui_widget *w, int *seen)
{
    struct gui_widget *c, *f;
    if (x->flags & (GUI_HIDDEN | GUI_DISABLED))
        return NULL;
    if (*seen && x->cls->flags & GUI_C_FOCUS && !(x->flags & GUI_NOTAB))
        return x;
    if (x == w)
        *seen = 1;
    for (c = x->first; c; c = c->next)
        if ((f = walk_after(c, w, seen)) != NULL)
            return f;
    return NULL;
}

static struct gui_widget *after(struct gui_widget *w)
{
    int seen = 0;
    return walk_after(w->win->root, w, &seen);
}

static void press_widget(struct gui_widget *w)
{
    struct gui_ev e;
    memset(&e, 0, sizeof e);
    e.type = GUI_EV_KEY;
    e.key = GUI_K_SPACE;
    e.ch = ' ';
    if (w->cls->event)
        w->cls->event(w, &e);
}

static int deaf(struct gui_window *w)
{
    struct gui_window *o;
    if (!G.modal || w == G.modal)
        return 0;
    for (o = w; o; o = o->owner)                /* (the modal window's own pop-ups hear) */
        if (o == G.modal)
            return 0;
    return 1;
}

static void tabs_key(struct gui_window *w, struct gui_ev *e);

static void key(struct gui_window *w, const struct glos_event *ev)
{
    struct gui_ev e;
    memset(&e, 0, sizeof e);
    e.type = GUI_EV_KEY;
    e.key = ev->key, e.ch = ev->ch, e.mods = ev->mods, e.time = ev->time;
    if (!(w->flags & GUI_W_POPUP) && gui_menubar_key(w, &e, 0))
        return;
    if (w->focus && usable(w->focus) && w->focus->cls->event && w->focus->cls->event(w->focus, &e))
        return;
    if (w->flags & GUI_W_POPUP)
        return;
    if (gui_menubar_key(w, &e, 1))
        return;
    if (e.key == GUI_K_TAB && e.mods & GLOS_MOD_CTRL) {
        tabs_key(w, &e);
        return;
    }
    if ((e.key == GUI_K_PGUP || e.key == GUI_K_PGDN) && e.mods & GLOS_MOD_CTRL) {
        tabs_key(w, &e);
        return;
    }
    if (e.key == GUI_K_TAB && !(e.mods & (GLOS_MOD_CTRL | GLOS_MOD_ALT))) {
        gui_focus_next(w, (e.mods & GLOS_MOD_SHIFT) != 0);
        return;
    }
    if ((e.key == GUI_K_ENTER || e.key == GUI_K_KPENTER) && !e.mods) {
        struct gui_widget *d = find_flag(w->root, GUI_DEFAULT);
        if (d) {
            press_widget(d);
            return;
        }
    }
    if (e.key == GUI_K_ESC && !e.mods) {
        struct gui_widget *c = find_flag(w->root, GUI_CANCEL);
        if (c) {
            press_widget(c);
            return;
        }
    }
    if (e.mods & GLOS_MOD_ALT && !(e.mods & GLOS_MOD_CTRL)) {
        unsigned c = key_letter(e.key);
        struct gui_widget *m = c ? find_mnemonic(w->root, c) : NULL;
        if (m) {
            if (m->cls == &gui_label_class) {
                struct gui_widget *n = after(m);
                if (n)
                    gui_focus(n);
            } else {
                if (m->cls->flags & GUI_C_FOCUS)
                    gui_focus(m);
                press_widget(m);
            }
            return;
        }
    }
    if (w->on_key)
        w->on_key(w, &e);
}

static void send_ev(struct gui_widget *w, struct gui_ev *e)
{
    if (w && w->cls->event && usable(w))
        w->cls->event(w, e);
}

static void mouse(struct gui_window *w, const struct glos_event *ev)
{
    struct gui_ev e;
    struct gui_widget *t;
    memset(&e, 0, sizeof e);
    e.x = ev->x, e.y = ev->y, e.buttons = ev->buttons, e.time = ev->time, e.mods = ev->mods;
    if (ev->key && ev->down) {                  /* a press */
        e.type = GUI_EV_PRESS;
        e.button = ev->key;
        e.clicks = 1;
        if (w->last_press_button == ev->key && ev->time - w->last_press_time <= dblclick
            && abs(ev->x - w->last_press_x) <= 4 && abs(ev->y - w->last_press_y) <= 4) {
            e.clicks = 2;
            w->last_press_button = 0;
        } else {
            w->last_press_button = ev->key;
        }
        w->last_press_time = ev->time;
        w->last_press_x = ev->x, w->last_press_y = ev->y;
        t = gui_hit(w->root, ev->x, ev->y);
        if (!t || !usable(t))
            return;
        if (!w->capture)
            w->capture = t;
        {
            struct gui_widget *f;               /* the focus to it, or the nearest that takes it */
            for (f = t; f && !(f->cls->flags & GUI_C_FOCUS); f = f->parent)
                ;
            if (f && usable(f))
                gui_focus(f);
        }
        send_ev(w->capture, &e);
    } else if (ev->key) {                       /* a release */
        e.type = GUI_EV_RELEASE;
        e.button = ev->key;
        t = w->capture;
        if (!ev->buttons)
            w->capture = NULL;
        send_ev(t, &e);
    } else {                                    /* a move */
        e.type = GUI_EV_MOVE;
        if (w->capture) {
            send_ev(w->capture, &e);
            return;
        }
        t = gui_hit(w->root, ev->x, ev->y);
        if (t != w->hover) {
            struct gui_ev l = e;
            l.type = GUI_EV_LEAVE;
            send_ev(w->hover, &l);
            w->hover = t;
        }
        send_ev(t, &e);
    }
}

static void resized(struct gui_window *w)
{
    new_pixels(w);
    if (w->on_resize)
        w->on_resize(w);
}

static void (*app_events)(const struct glos_event *ev);

void gui_on_event(void (*fn)(const struct glos_event *ev)) { app_events = fn; }

static void dispatch(const struct glos_event *ev)
{
    struct gui_window *w = gui_window_of(ev->window);
    if (!ev->window && app_events) {
        app_events(ev);
        return;
    }
    if (!w)
        return;
    switch (ev->type) {
    case GLOS_EV_KEY:
        if (deaf(w) || !ev->down)
            return;
        key(w, ev);
        break;
    case GLOS_EV_MOUSE:
        if (deaf(w)) {
            if (ev->key && ev->down)
                glos_focus(G.modal->id);
            return;
        }
        mouse(w, ev);
        break;
    case GLOS_EV_FOCUS:
        w->active = ev->down != 0;
        if (w->focus)
            gui_damage(w->focus);
        if (!ev->down && w->flags & GUI_W_POPUP && w->on_popdown)
            w->on_popdown(w);
        if (ev->down && deaf(w) && G.modal->id)
            glos_focus(G.modal->id);
        break;
    case GLOS_EV_RESIZE:
        resized(w);
        break;
    case GLOS_EV_CLOSE:
        if (deaf(w))
            return;
        if (w->on_close)
            w->on_close(w);
        else if (w == G.modal)
            gui_end_modal(w, 0);
        else
            gui_close(w);
        break;
    }
}

/* ---- tabs' keys, here because the window has them (Ctrl+Tab, Ctrl+PgUp/PgDn) */

static void tabs_key(struct gui_window *w, struct gui_ev *e)
{
    struct gui_widget *t = w->focus;
    int back = (e->key == GUI_K_TAB && e->mods & GLOS_MOD_SHIFT) || e->key == GUI_K_PGUP;
    while (t && t->cls != &gui_tabs_class)
        t = t->parent;
    if (!t) {                                   /* the first tabs in the window */
        struct gui_widget *stack[64], *x, *k;
        int sp = 0;
        stack[sp++] = w->root;
        while (sp && !t) {
            x = stack[--sp];
            if (x->cls == &gui_tabs_class && !(x->flags & GUI_HIDDEN))
                t = x;
            for (k = x->last; k && sp < 64; k = k->prev)
                stack[sp++] = k;
        }
    }
    if (t)
        gui_tabs_step(t, back ? -1 : 1);
}

/* ---- the loop */

void gui_timer(struct gui_widget *w, unsigned ms)
{
    int i, free_at = -1;
    for (i = 0; i < MAX_TIMERS; i++) {
        if (G.timer[i].w == w) {
            if (!ms) {
                G.timer[i].w = NULL;
                return;
            }
            G.timer[i].ms = ms;
            G.timer[i].due = gui_now() + ms;
            return;
        }
        if (!G.timer[i].w && free_at < 0)
            free_at = i;
    }
    if (ms && free_at >= 0) {
        G.timer[free_at].w = w;
        G.timer[free_at].ms = ms;
        G.timer[free_at].due = gui_now() + ms;
    }
}

void gui_idle(void (*fn)(void *), void *arg, unsigned ms)
{
    int i, free_at = -1;
    for (i = 0; i < MAX_IDLE; i++) {
        if (G.idle[i].fn == fn && G.idle[i].arg == arg) {
            if (!ms)
                G.idle[i].fn = NULL;
            else
                G.idle[i].ms = ms, G.idle[i].due = gui_now() + ms;
            return;
        }
        if (!G.idle[i].fn && free_at < 0)
            free_at = i;
    }
    if (ms && free_at >= 0) {
        G.idle[free_at].fn = fn;
        G.idle[free_at].arg = arg;
        G.idle[free_at].ms = ms;
        G.idle[free_at].due = gui_now() + ms;
    }
}

static unsigned until_due(unsigned wait)
{
    unsigned now = gui_now();
    int i;
    for (i = 0; i < MAX_TIMERS; i++)
        if (G.timer[i].w) {
            int d = (int)(G.timer[i].due - now);
            if (d < 0)
                d = 0;
            if ((unsigned)d < wait)
                wait = (unsigned)d;
        }
    for (i = 0; i < MAX_IDLE; i++)
        if (G.idle[i].fn) {
            int d = (int)(G.idle[i].due - now);
            if (d < 0)
                d = 0;
            if ((unsigned)d < wait)
                wait = (unsigned)d;
        }
    return wait;
}

static void fire(void)
{
    unsigned now = gui_now();
    int i;
    for (i = 0; i < MAX_TIMERS; i++)
        if (G.timer[i].w && (int)(now - G.timer[i].due) >= 0) {
            struct gui_widget *w = G.timer[i].w;
            struct gui_ev e;
            G.timer[i].due = now + G.timer[i].ms;
            memset(&e, 0, sizeof e);
            e.type = GUI_EV_TIMER;
            e.time = now;
            if (w->cls->event)
                w->cls->event(w, &e);
        }
    for (i = 0; i < MAX_IDLE; i++)
        if (G.idle[i].fn && (int)(now - G.idle[i].due) >= 0) {
            G.idle[i].due = now + G.idle[i].ms;
            G.idle[i].fn(G.idle[i].arg);
        }
}

void gui_close_later(struct gui_window *w)
{
    if (w && !w->closing) {
        w->closing = 2;
        if (w->id)
            glos_show(w->id, 0);
    }
}

void gui_post_command(struct gui_window *w, int id)
{
    G.cmd_win = w;
    G.cmd_id = id;
    G.cmd = 1;
}

static void after_event(void)
{
    struct gui_window *w;
    for (;;) {                                  /* windows closed while their events were handled */
        for (w = G.wins; w && w->closing != 2; w = w->next)
            ;
        if (!w)
            break;
        w->closing = 0;
        gui_close(w);
    }
    if (G.cmd) {
        G.cmd = 0;
        for (w = G.wins; w && w != G.cmd_win; w = w->next)
            ;
        if (w && w->on_command && !w->closing)
            w->on_command(w, G.cmd_id);
    }
}

int gui_step(unsigned wait_ms)
{
    struct gui_window *w;
    struct glos_event ev;
    int got;
    for (w = G.wins; w; w = w->next)
        redraw(w);
    memset(&ev, 0, sizeof ev);
    got = glos_event(&ev, until_due(wait_ms));
    if (got)
        dispatch(&ev);
    after_event();
    fire();
    after_event();
    return got;
}

void gui_run(void)
{
    while (!G.quit && G.wins)
        gui_step(0xFFFFFFFFu);
    {
        struct gui_window *w;                   /* what is still damaged, drawn before the end */
        for (w = G.wins; w; w = w->next)
            redraw(w);
    }
}

void gui_quit(int code)
{
    G.quit = 1;
    G.code = code;
}

int gui_exit_code(void) { return G.code; }

int gui_modal(struct gui_window *w)
{
    struct gui_window *prev = G.modal;
    int r;
    G.modal = w;
    w->result = 0;
    gui_show(w, 1);
    while (!w->closing && !G.quit)
        gui_step(0xFFFFFFFFu);
    r = w->result;
    G.modal = prev;
    w->closing = 0;
    gui_close(w);
    return r;
}

void gui_end_modal(struct gui_window *w, int result)
{
    w->result = result;
    w->closing = 1;
}
