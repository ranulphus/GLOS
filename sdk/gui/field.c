/* The toolkit's text (include/glos/gui.h): the one-line field and the
 * editor.
 *
 * A field's text is UTF-8 (a name, a number): the caret moves by whole
 * characters. The editor's is a file's bytes as they are, shown as Latin-1
 * (M9: GLOS has no code pages yet), kept in a gap buffer with the offsets
 * of its lines' starts, which an edit moves and adds to or takes from
 * rather than counting again: a megabyte's file edits as fast as a line's.
 * Lines end in LF or CR LF (the caret never stands between the two); Enter
 * writes what the file's first line ends in. Tabs go to every eighth space.
 * With wrapping on, a line is shown in as many rows as it needs, broken
 * after a space where it can be. */
#include "guiint.h"

/* ================================================================ fields */

struct field {
    struct gui_widget w;
    int chars, mask;
    int caret, anchor, off;             /* bytes in the text; off: the first shown */
    int drag;
};

static int f_len(struct field *f) { return (int)strlen(gui_get_text(&f->w)); }

static int f_prev(const char *t, int at)
{
    if (at <= 0)
        return 0;
    at--;
    while (at > 0 && ((unsigned char)t[at] & 0xC0) == 0x80)
        at--;
    return at;
}

static int f_next(const char *t, int at)
{
    int n = (int)strlen(t);
    if (at >= n)
        return n;
    at++;
    while (at < n && ((unsigned char)t[at] & 0xC0) == 0x80)
        at++;
    return at;
}

/* The width of the first n bytes as shown. */
static int f_width(struct field *f, int n)
{
    const char *t = gui_get_text(&f->w);
    if (f->mask) {
        int i, c = 0;
        for (i = 0; i < n; i = f_next(t, i))
            c++;
        return c * gfx_text_width(gui_font, "*", 1);
    }
    return gfx_text_width(gui_font, t, n);
}

static void f_visible(struct field *f)
{
    const char *t = gui_get_text(&f->w);
    int room = f->w.r.w - 8;
    if (room <= 0)                              /* (not laid out yet: done when it is drawn) */
        return;
    if (f->caret < f->off)
        f->off = f->caret;
    while (f->off < f->caret && f_width(f, f->caret) - f_width(f, f->off) > room)
        f->off = f_next(t, f->off);
}

static void field_measure(struct gui_widget *w, int *pw, int *ph)
{
    struct field *f = (struct field *)w;
    *pw = f->chars * gfx_text_width(gui_font, "n", 1) + 8;
    *ph = GUI_FIELD_H;
}

static void field_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct field *f = (struct field *)w;
    const char *t = gui_get_text(w);
    int on = !(w->flags & GUI_DISABLED), focus = gui_is_focus(w), n = (int)strlen(t);
    int a = f->caret < f->anchor ? f->caret : f->anchor, b = f->caret < f->anchor ? f->anchor : f->caret;
    int x0 = w->r.x + 4 - f_width(f, f->off), y = w->r.y + (w->r.h - gui_font->height) / 2;
    struct gfx_rect in = { w->r.x + 1, w->r.y + 1, w->r.w - 2, w->r.h - 2 };
    f_visible(f);
    x0 = w->r.x + 4 - f_width(f, f->off);
    gui_draw_field(s, &w->r, on);
    gfx_clip(s, &in);
    if (focus && a != b)
        gfx_fill(s, x0 + f_width(f, a), y, f_width(f, b) - f_width(f, a), gui_font->height, GUI_DARK);
    if (f->mask) {
        int i, x = x0;
        for (i = 0; i < n; i = f_next(t, i))
            x = gfx_text(s, x, y, gui_font, "*", 1, focus && i >= a && i < b ? GUI_FIELD : on ? GUI_LINE : GUI_DIM);
    } else if (focus && a != b) {
        int x = gfx_text(s, x0, y, gui_font, t, a, GUI_LINE);
        x = gfx_text(s, x, y, gui_font, t + a, b - a, GUI_FIELD);
        gfx_text(s, x, y, gui_font, t + b, n - b, GUI_LINE);
    } else {
        gfx_text(s, x0, y, gui_font, t, n, on ? GUI_LINE : GUI_DIM);
    }
    if (focus)
        gfx_fill(s, x0 + f_width(f, f->caret), y, 1, gui_font->height, GUI_LINE);
}

/* The byte offset nearest pixel x of the field. */
static int f_at(struct field *f, int x)
{
    const char *t = gui_get_text(&f->w);
    int i, x0 = f->w.r.x + 4 - f_width(f, f->off), best = 0, bd = 1 << 30;
    for (i = 0;; i = f_next(t, i)) {
        int d = abs(x0 + f_width(f, i) - x);
        if (d < bd)
            bd = d, best = i;
        if (!t[i])
            break;
    }
    return best;
}

static void f_replace(struct field *f, int a, int b, const char *with, int n)
{
    const char *t = gui_get_text(&f->w);
    int len = (int)strlen(t);
    char *u = malloc((size_t)(len - (b - a) + n + 1));
    if (!u)
        return;
    memcpy(u, t, (size_t)a);
    memcpy(u + a, with, (size_t)n);
    memcpy(u + a + n, t + b, (size_t)(len - b + 1));
    free(f->w.text);
    f->w.text = u;
    f->caret = f->anchor = a + n;
    f_visible(f);
    gui_damage(&f->w);
    gui_send(&f->w, GUI_ON_CHANGE);
}

static void f_sel(struct field *f, int *a, int *b)
{
    *a = f->caret < f->anchor ? f->caret : f->anchor;
    *b = f->caret < f->anchor ? f->anchor : f->caret;
}

static void f_move(struct field *f, int to, int extend)
{
    f->caret = to;
    if (!extend)
        f->anchor = to;
    f_visible(f);
    gui_damage(&f->w);
}

static int field_event(struct gui_widget *w, struct gui_ev *e)
{
    struct field *f = (struct field *)w;
    const char *t = gui_get_text(w);
    int a, b, shift = (e->mods & GLOS_MOD_SHIFT) != 0, len = (int)strlen(t);
    f_sel(f, &a, &b);
    switch (e->type) {
    case GUI_EV_FOCUS:
        f->anchor = 0, f->caret = len;          /* (all of it chosen, as a field the Tab key arrives at) */
        f_visible(f);
        return 1;
    case GUI_EV_PRESS:
        if (e->button != 1)
            return 0;
        if (e->clicks == 2) {
            f->anchor = 0, f->caret = len;
        } else {
            f_move(f, f_at(f, e->x), shift);
            f->drag = 1;
        }
        gui_damage(w);
        return 1;
    case GUI_EV_MOVE:
        if (f->drag)
            f_move(f, f_at(f, e->x), 1);
        return 1;
    case GUI_EV_RELEASE:
        f->drag = 0;
        return 1;
    case GUI_EV_KEY:
        break;
    default:
        return 0;
    }
    if (e->mods & GLOS_MOD_CTRL) {
        switch (e->ch) {
        case 1: f->anchor = 0, f->caret = len; gui_damage(w); return 1;          /* Ctrl+A */
        case 3: if (a != b && !f->mask) gui_clip_set(t + a, (size_t)(b - a)); return 1;
        case 24: if (a != b && !f->mask) { gui_clip_set(t + a, (size_t)(b - a)); f_replace(f, a, b, "", 0); } return 1;
        case 22: {                                                               /* Ctrl+V */
            size_t n;
            char *c = gui_clip_get(&n), *p;
            if (c) {
                for (p = c; *p && *p != '\r' && *p != '\n'; p++)
                    ;
                f_replace(f, a, b, c, (int)(p - c));
                free(c);
            }
            return 1;
        }
        }
    }
    if (e->mods & (GLOS_MOD_CTRL | GLOS_MOD_ALT))
        return 0;
    switch (e->key) {
    case GUI_K_LEFT: f_move(f, shift || a == b ? f_prev(t, f->caret) : a, shift); return 1;
    case GUI_K_RIGHT: f_move(f, shift || a == b ? f_next(t, f->caret) : b, shift); return 1;
    case GUI_K_HOME: f_move(f, 0, shift); return 1;
    case GUI_K_END: f_move(f, len, shift); return 1;
    case GUI_K_BACK:
        if (a != b)
            f_replace(f, a, b, "", 0);
        else if (a > 0)
            f_replace(f, f_prev(t, a), a, "", 0);
        return 1;
    case GUI_K_DEL:
        if (a != b)
            f_replace(f, a, b, "", 0);
        else if (a < len)
            f_replace(f, a, f_next(t, a), "", 0);
        return 1;
    case GUI_K_ENTER:
    case GUI_K_KPENTER:
        if (w->on) {
            gui_send(w, GUI_ON_ACTIVATE);
            return 1;
        }
        return 0;
    }
    if (e->ch >= 0x20 && e->ch != 0x7F) {
        char u[4];
        int n;
        if (e->ch < 0x80) {
            u[0] = (char)e->ch;
            n = 1;
        } else {
            u[0] = (char)(0xC0 | (e->ch >> 6));
            u[1] = (char)(0x80 | (e->ch & 0x3F));
            n = 2;
        }
        f_replace(f, a, b, u, n);
        return 1;
    }
    return 0;
}

const struct gui_class gui_field_class = { "field", GUI_C_FOCUS, field_draw, field_event, field_measure, NULL, NULL };

struct gui_widget *gui_field(struct gui_widget *parent, int chars)
{
    struct field *f = (struct field *)gui_new(parent, &gui_field_class, sizeof *f);
    f->chars = chars > 0 ? chars : 20;
    gui_set_text(&f->w, "");
    return &f->w;
}

void gui_field_mask(struct gui_widget *w, int on)
{
    ((struct field *)w)->mask = on;
    gui_damage(w);
}

void gui_field_changed(struct gui_widget *w)
{
    struct field *f = (struct field *)w;
    f->caret = f->anchor = f_len(f);
    f->off = 0;
    f_visible(f);
    w->mnemonic = 0;                            /* (a field's text is no key) */
}

/* ================================================================ the editor */

struct undo {
    int kind;                           /* 1 inserted, 2 removed */
    size_t at, n;
    char *bytes;
    size_t caret;
};

#define UNDO_MAX 200

struct editor {
    struct gui_widget w;
    const struct gfx_font *font;
    char *buf;
    size_t size, gap, gaplen;           /* the buffer, where the gap is, how long */
    size_t *ls;                         /* each line's first byte */
    int nl, lroom;
    int *rows;                          /* wrapping: each line's rows (-1: not counted yet) */
    int *rstart;                        /* ... the first row of each (valid while !rdirty) */
    int rdirty, wrap, wrap_w;
    size_t caret, anchor;
    int top, left;                      /* the first row shown; pixels scrolled to the right */
    int show;                           /* the caret to be shown once it has a size: 1, or 2 a third of the way down */
    int goal;                           /* the pixel column Up and Down aim for (-1: the caret's) */
    struct gui_sb vsb, hsb;
    int drag, modified, crlf, tabw;
    int widest;                         /* the widest row drawn last time */
    struct undo undo[UNDO_MAX];
    int nundo;
};

#define ED(w) ((struct editor *)(w))

static size_t e_len(struct editor *e) { return e->size - e->gaplen; }

static char e_at(struct editor *e, size_t i) { return i < e->gap ? e->buf[i] : e->buf[i + e->gaplen]; }

static void e_copy(struct editor *e, size_t a, size_t n, char *out)
{
    size_t i;
    if (a + n <= e->gap) {
        memcpy(out, e->buf + a, n);
    } else if (a >= e->gap) {
        memcpy(out, e->buf + a + e->gaplen, n);
    } else {
        i = e->gap - a;
        memcpy(out, e->buf + a, i);
        memcpy(out + i, e->buf + e->gap + e->gaplen, n - i);
    }
}

static void e_gap_to(struct editor *e, size_t at, size_t need)
{
    if (e->gaplen < need) {                     /* a bigger buffer: the gap grows */
        size_t more = need + e->size / 2 + 4096, tail = e->size - e->gap - e->gaplen;
        char *nb = realloc(e->buf, e->size + more);
        if (!nb)
            abort();
        e->buf = nb;
        memmove(e->buf + e->gap + e->gaplen + more, e->buf + e->gap + e->gaplen, tail);
        e->gaplen += more;
        e->size += more;
    }
    if (at < e->gap)
        memmove(e->buf + at + e->gaplen, e->buf + at, e->gap - at);
    else if (at > e->gap)
        memmove(e->buf + e->gap, e->buf + e->gap + e->gaplen, at - e->gap);
    e->gap = at;
}

/* The line holding byte p (the last whose start is at or before it). */
static int e_line(struct editor *e, size_t p)
{
    int lo = 0, hi = e->nl - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (e->ls[mid] <= p)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

/* Where line l's text ends (before its LF, or CR LF). */
static size_t e_eol(struct editor *e, int l)
{
    size_t end = l + 1 < e->nl ? e->ls[l + 1] - 1 : e_len(e);
    if (l + 1 < e->nl && end > e->ls[l] && e_at(e, end - 1) == '\r')
        end--;
    return end;
}

static void e_lines_room(struct editor *e, int more)
{
    if (e->nl + more > e->lroom) {
        int room = (e->nl + more) * 2 + 64;
        size_t *ls = realloc(e->ls, (size_t)room * sizeof *ls);
        int *rows = realloc(e->rows, (size_t)room * sizeof *rows);
        int *rs = realloc(e->rstart, (size_t)(room + 1) * sizeof *rs);
        if (!ls || !rows || !rs)
            abort();
        e->ls = ls, e->rows = rows, e->rstart = rs;
        e->lroom = room;
    }
}

/* ---- rows (wrapping) */

static int e_charw(struct editor *e, unsigned char c, int x)
{
    if (c == '\t')
        return e->tabw - x % e->tabw;
    return gui_char_w(e->font, c < 0x20 ? ' ' : c);
}

static int e_text_w(struct editor *e)
{
    return e->w.r.w - GUI_SB - 8 > 16 ? e->w.r.w - GUI_SB - 8 : 16;
}

/* Where line l's rows begin (into out, at most room of them): their count. Without wrapping, one. */
static int e_breaks(struct editor *e, int l, size_t *out, int room)
{
    size_t p = e->ls[l], end = e_eol(e, l), last_space = 0, row = p;
    int n = 1, x = 0, width = e_text_w(e);
    if (room > 0)
        out[0] = p;
    if (!e->wrap)
        return 1;
    while (p < end) {
        unsigned char c = (unsigned char)e_at(e, p);
        int cw = e_charw(e, c, x);
        if (x + cw > width && x > 0) {
            size_t brk = last_space > row ? last_space : p;
            if (n < room)
                out[n] = brk;
            n++;
            x = 0;
            last_space = 0;
            row = p = brk;
            continue;
        }
        x += cw;
        p++;
        if (c == ' ' || c == '\t')
            last_space = p;
    }
    return n;
}

static int e_rows_of(struct editor *e, int l)
{
    if (!e->wrap)
        return 1;
    if (e->rows[l] < 0) {
        size_t tmp[1];
        e->rows[l] = e_breaks(e, l, tmp, 0);
    }
    return e->rows[l];
}

static void e_rows_ready(struct editor *e)
{
    int l, r = 0;
    if (e->wrap && e->wrap_w != e_text_w(e)) {  /* (another width: every line counted again) */
        e->wrap_w = e_text_w(e);
        for (l = 0; l < e->nl; l++)
            e->rows[l] = -1;
        e->rdirty = 1;
    }
    if (!e->rdirty)
        return;
    for (l = 0; l < e->nl; l++) {
        e->rstart[l] = r;
        r += e_rows_of(e, l);
    }
    e->rstart[e->nl] = r;
    e->rdirty = 0;
}

static int e_total_rows(struct editor *e)
{
    e_rows_ready(e);
    return e->rstart[e->nl];
}

/* Row r's line, and where in the text the row begins and ends. */
static int e_row(struct editor *e, int r, size_t *a, size_t *b)
{
    int lo = 0, hi = e->nl - 1, l, k, n;
    size_t brk[256];
    e_rows_ready(e);
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (e->rstart[mid] <= r)
            lo = mid;
        else
            hi = mid - 1;
    }
    l = lo;
    k = r - e->rstart[l];
    n = e_breaks(e, l, brk, 256);
    if (k >= n)
        k = n - 1;
    if (k > 255)
        k = 255;
    *a = brk[k];
    *b = k + 1 < n && k + 1 < 256 ? brk[k + 1] : e_eol(e, l);
    return l;
}

/* The row the byte p is in. */
static int e_row_of(struct editor *e, size_t p)
{
    int l = e_line(e, p), n, k;
    size_t brk[256];
    e_rows_ready(e);
    n = e_breaks(e, l, brk, 256);
    for (k = n - 1; k > 0; k--)
        if (k < 256 && brk[k] <= p)
            break;
    return e->rstart[l] + k;
}

/* The pixel x of byte p within its row (from the row's start a). */
static int e_x(struct editor *e, size_t a, size_t p)
{
    int x = 0;
    for (; a < p; a++)
        x += e_charw(e, (unsigned char)e_at(e, a), x);
    return x;
}

static size_t e_from_x(struct editor *e, size_t a, size_t b, int want)
{
    int x = 0;
    while (a < b) {
        int cw = e_charw(e, (unsigned char)e_at(e, a), x);
        if (x + cw / 2 >= want)
            break;
        x += cw;
        a++;
    }
    return a;
}

/* ---- editing */

static void e_mark(struct editor *e, int from_line)
{
    int l;
    for (l = from_line < 0 ? 0 : from_line; l < e->nl && l <= from_line + 1; l++)
        e->rows[l] = -1;
    e->rdirty = 1;
}

static void e_raw_insert(struct editor *e, size_t at, const char *t, size_t n)
{
    int l = e_line(e, at), k = 0, i;
    size_t j;
    for (j = 0; j < n; j++)
        if (t[j] == '\n')
            k++;
    e_gap_to(e, at, n);
    memcpy(e->buf + at, t, n);
    e->gap += n;
    e->gaplen -= n;
    e_lines_room(e, k);
    for (i = l + 1; i < e->nl; i++)
        e->ls[i] += n;
    if (k) {
        memmove(e->ls + l + 1 + k, e->ls + l + 1, (size_t)(e->nl - l - 1) * sizeof *e->ls);
        memmove(e->rows + l + 1 + k, e->rows + l + 1, (size_t)(e->nl - l - 1) * sizeof *e->rows);
        for (i = 0, j = 0; j < n; j++)
            if (t[j] == '\n') {
                e->ls[l + 1 + i] = at + j + 1;
                e->rows[l + 1 + i] = -1;
                i++;
            }
        e->nl += k;
    }
    e->rows[l] = -1;
    e_mark(e, l);
    e->modified = 1;
}

static void e_raw_delete(struct editor *e, size_t at, size_t n)
{
    int l = e_line(e, at), last = e_line(e, at + n), k = last - l, i;
    if (!n)
        return;
    e_gap_to(e, at, 0);
    e->gaplen += n;
    if (k) {
        memmove(e->ls + l + 1, e->ls + last + 1, (size_t)(e->nl - last - 1) * sizeof *e->ls);
        memmove(e->rows + l + 1, e->rows + last + 1, (size_t)(e->nl - last - 1) * sizeof *e->rows);
        e->nl -= k;
    }
    for (i = l + 1; i < e->nl; i++)
        e->ls[i] -= n;
    e->rows[l] = -1;
    e_mark(e, l);
    e->modified = 1;
}

static void e_push_undo(struct editor *e, int kind, size_t at, const char *bytes, size_t n, size_t caret)
{
    struct undo *u;
    if (e->nundo > 0) {                         /* (typing joins the last insertion) */
        u = &e->undo[e->nundo - 1];
        if (kind == 1 && u->kind == 1 && u->at + u->n == at && n == 1 && bytes[0] != '\n' && u->n < 4096) {
            char *nb = realloc(u->bytes, u->n + 1);
            if (nb) {
                u->bytes = nb;
                u->bytes[u->n++] = bytes[0];
                return;
            }
        }
    }
    if (e->nundo == UNDO_MAX) {
        free(e->undo[0].bytes);
        memmove(e->undo, e->undo + 1, (UNDO_MAX - 1) * sizeof *e->undo);
        e->nundo--;
    }
    u = &e->undo[e->nundo];
    u->bytes = malloc(n ? n : 1);
    if (!u->bytes)
        return;
    memcpy(u->bytes, bytes, n);
    u->kind = kind, u->at = at, u->n = n, u->caret = caret;
    e->nundo++;
}

static void e_scroll_to_caret(struct editor *e);

/* Bytes a..b replaced with t (n bytes), as one step to undo. */
static void e_replace(struct editor *e, size_t a, size_t b, const char *t, size_t n)
{
    size_t caret = e->caret;
    if (b > a) {
        char *old = malloc(b - a);
        if (old) {
            e_copy(e, a, b - a, old);
            e_push_undo(e, 2, a, old, b - a, caret);
            free(old);
        }
        e_raw_delete(e, a, b - a);
    }
    if (n) {
        e_push_undo(e, 1, a, t, n, caret);
        e_raw_insert(e, a, t, n);
    }
    e->caret = e->anchor = a + n;
    e->goal = -1;
    e_scroll_to_caret(e);
    gui_damage(&e->w);
    gui_send(&e->w, GUI_ON_CHANGE);
}

static void e_undo(struct editor *e)
{
    struct undo *u;
    if (!e->nundo)
        return;
    u = &e->undo[--e->nundo];
    if (u->kind == 1)
        e_raw_delete(e, u->at, u->n);
    else
        e_raw_insert(e, u->at, u->bytes, u->n);
    /* (two steps made by one replacement come back together) */
    if (u->kind == 1 && e->nundo > 0 && e->undo[e->nundo - 1].kind == 2 && e->undo[e->nundo - 1].at == u->at
        && e->undo[e->nundo - 1].caret == u->caret) {
        free(u->bytes);
        u = &e->undo[--e->nundo];
        e_raw_insert(e, u->at, u->bytes, u->n);
    }
    e->caret = e->anchor = u->caret <= e_len(e) ? u->caret : e_len(e);
    free(u->bytes);
    e->goal = -1;
    e_scroll_to_caret(e);
    gui_damage(&e->w);
    gui_send(&e->w, GUI_ON_CHANGE);
}

/* ---- view */

static int e_rows_shown(struct editor *e)
{
    int h = e->w.r.h - 4 - (e->wrap ? 0 : GUI_SB);
    return h / e->font->height > 1 ? h / e->font->height : 1;
}

static void e_bars(struct editor *e)
{
    struct gfx_rect r = e->w.r;
    e->vsb.r.x = r.x + r.w - GUI_SB, e->vsb.r.y = r.y, e->vsb.r.w = GUI_SB, e->vsb.r.h = r.h - (e->wrap ? 0 : GUI_SB - 1);
    e->vsb.total = e_total_rows(e);
    e->vsb.page = e_rows_shown(e);
    e->vsb.pos = e->top;
    if (e->wrap) {
        e->hsb.r.w = 0;
    } else {
        e->hsb.r.x = r.x, e->hsb.r.y = r.y + r.h - GUI_SB, e->hsb.r.w = r.w - GUI_SB + 1, e->hsb.r.h = GUI_SB;
        e->hsb.total = e->widest > e->left + e_text_w(e) ? e->widest : e->left + e_text_w(e);  /* (the rows
                                                   shown: a megabyte isn't measured for a scroll bar) */
        e->hsb.page = e_text_w(e);
        e->hsb.pos = e->left;
    }
}

static void e_scroll_to_caret(struct editor *e)
{
    int row, shown, x;
    size_t a, b;
    if (e->w.r.w <= 0 || e->w.r.h <= 0) {      /* (not laid out yet, a hidden page's: when it is drawn) */
        if (!e->show)
            e->show = 1;
        return;
    }
    row = e_row_of(e, e->caret), shown = e_rows_shown(e);
    if (row < e->top)
        e->top = row;
    if (row >= e->top + shown)
        e->top = row - shown + 1;
    if (!e->wrap) {
        e_row(e, row, &a, &b);
        x = e_x(e, a, e->caret);
        if (x < e->left)
            e->left = x - e_text_w(e) / 4 > 0 ? x - e_text_w(e) / 4 : 0;
        if (x > e->left + e_text_w(e) - 2)
            e->left = x - e_text_w(e) * 3 / 4;
    }
}

static void editor_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct editor *e = ED(w);
    int on = !(w->flags & GUI_DISABLED), focus = gui_is_focus(w), shown = e_rows_shown(e), r, total;
    size_t sa = e->caret < e->anchor ? e->caret : e->anchor, sb = e->caret < e->anchor ? e->anchor : e->caret;
    struct gfx_rect in = { w->r.x + 1, w->r.y + 1, w->r.w - GUI_SB, w->r.h - 2 - (e->wrap ? 0 : GUI_SB - 1) };
    struct gfx_rect frame = { w->r.x, w->r.y, w->r.w - GUI_SB + 1, w->r.h - (e->wrap ? 0 : GUI_SB - 1) };
    struct gfx_rect clip = s->clip;
    if (e->show) {
        int third = e->show == 2;
        e->show = 0;
        e_scroll_to_caret(e);
        if (third)
            e->top = e_row_of(e, e->caret) - shown / 3 > 0 ? e_row_of(e, e->caret) - shown / 3 : 0;
    }
    e_bars(e);
    gui_draw_field(s, &frame, on);
    gfx_rect_cut(&in, &clip);
    gfx_clip(s, &in);
    total = e_total_rows(e);
    e->widest = 0;
    for (r = e->top; r < e->top + shown + 1 && r < total; r++) {
        size_t a, b, p;
        int x, y = w->r.y + 2 + (r - e->top) * e->font->height, l;
        l = e_row(e, r, &a, &b);
        (void)l;
        if (y >= in.y + in.h)
            break;
        x = w->r.x + 4 - e->left;
        {
            int rw = e_x(e, a, b) + 8;
            if (rw > e->widest)
                e->widest = rw;
        }
        if (focus || sa != sb) {                /* the chosen part's background */
            size_t ca = sa > a ? sa : a, cb = sb < b ? sb : b;
            int end_too = sb > b && sa <= b;    /* (the line's end chosen too: a little more) */
            if (ca < cb || end_too)
                gfx_fill(s, x + e_x(e, a, ca), y, e_x(e, ca, cb) + (end_too ? 4 : 0), e->font->height, GUI_DARK);
        }
        p = a;
        while (p < b) {                         /* text in runs of one colour, tabs as gaps */
            char run[128];
            int n = 0, chosen = p >= sa && p < sb, px = x + e_x(e, a, p);
            if (e_at(e, p) == '\t') {
                p++;
                continue;
            }
            while (p < b && n < (int)sizeof run && e_at(e, p) != '\t' && (p >= sa && p < sb) == chosen) {
                run[n++] = e_at(e, p);
                p++;
            }
            if (px > in.x + in.w)
                break;
            gui_text_latin1(s, px, y, e->font, run, n, chosen ? GUI_FIELD : on ? GUI_LINE : GUI_DIM);
        }
        if (focus && e->caret >= a && (e->caret < b || (e->caret == b && (b == e_eol(e, e_line(e, a)) ||
                                                                           r + 1 >= total))))
            gfx_fill(s, x + e_x(e, a, e->caret), y, 1, e->font->height, GUI_LINE);
    }
    gfx_clip(s, &clip);
    e_bars(e);                                  /* (the widest row now known) */
    gui_sb_draw(s, &e->vsb, on);
    if (!e->wrap) {
        gui_sb_draw(s, &e->hsb, on);
        gfx_fill(s, w->r.x + w->r.w - GUI_SB + 1, w->r.y + w->r.h - GUI_SB + 1, GUI_SB - 2, GUI_SB - 2, GUI_FACE);
    }
    (void)focus;
}

static void editor_measure(struct gui_widget *w, int *pw, int *ph)
{
    (void)w;
    *pw = 200, *ph = 100;
}

/* The byte under the pointer. */
static size_t e_hit(struct editor *e, int x, int y)
{
    int r = e->top + (y - e->w.r.y - 2) / e->font->height, total = e_total_rows(e);
    size_t a, b;
    if (y < e->w.r.y + 2)
        r = e->top - 1;
    if (r < 0)
        r = 0;
    if (r >= total)
        r = total - 1;
    e_row(e, r, &a, &b);
    return e_from_x(e, a, b, x - e->w.r.x - 4 + e->left);
}

static void e_go(struct editor *e, size_t to, int extend)
{
    e->caret = to;
    if (!extend)
        e->anchor = to;
    e_scroll_to_caret(e);
    gui_damage(&e->w);
}

/* The next character's start after p (CR LF is one), and the one before. */
static size_t e_right(struct editor *e, size_t p)
{
    if (p >= e_len(e))
        return p;
    if (e_at(e, p) == '\r' && p + 1 < e_len(e) && e_at(e, p + 1) == '\n')
        return p + 2;
    return p + 1;
}

static size_t e_left(struct editor *e, size_t p)
{
    if (!p)
        return 0;
    if (e_at(e, p - 1) == '\n' && p >= 2 && e_at(e, p - 2) == '\r')
        return p - 2;
    return p - 1;
}

static int is_word(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || (unsigned char)c >= 0xC0; }

static size_t e_word(struct editor *e, size_t p, int forward)
{
    size_t n = e_len(e);
    if (forward) {
        while (p < n && is_word(e_at(e, p)))
            p++;
        while (p < n && !is_word(e_at(e, p)))
            p++;
    } else {
        while (p > 0 && !is_word(e_at(e, p - 1)))
            p--;
        while (p > 0 && is_word(e_at(e, p - 1)))
            p--;
    }
    return p;
}

static void e_vertical(struct editor *e, int by, int extend)
{
    int row = e_row_of(e, e->caret), total = e_total_rows(e), to = row + by;
    size_t a, b;
    if (e->goal < 0) {
        e_row(e, row, &a, &b);
        e->goal = e_x(e, a, e->caret);
    }
    if (to < 0)
        to = 0;
    if (to >= total)
        to = total - 1;
    e_row(e, to, &a, &b);
    {
        int g = e->goal;
        e_go(e, e_from_x(e, a, b, g), extend);
        e->goal = g;
    }
}

int gui_editor_command(struct gui_widget *w, int cmd)
{
    struct editor *e = ED(w);
    size_t a = e->caret < e->anchor ? e->caret : e->anchor, b = e->caret < e->anchor ? e->anchor : e->caret;
    char *t;
    switch (cmd) {
    case GUI_CMD_COPY:
    case GUI_CMD_CUT:
        if (a == b)
            return 0;
        if (!(t = malloc(b - a)))
            return 0;
        e_copy(e, a, b - a, t);
        gui_clip_set(t, b - a);
        free(t);
        if (cmd == GUI_CMD_CUT)
            e_replace(e, a, b, "", 0);
        return 1;
    case GUI_CMD_PASTE: {
        size_t n;
        char *c = gui_clip_get(&n);
        if (!c)
            return 0;
        e_replace(e, a, b, c, n);
        free(c);
        return 1;
    }
    case GUI_CMD_DELETE:
        if (a == b)
            return 0;
        e_replace(e, a, b, "", 0);
        return 1;
    case GUI_CMD_ALL:
        e->anchor = 0;
        e->caret = e_len(e);
        gui_damage(w);
        return 1;
    case GUI_CMD_UNDO:
        e_undo(e);
        return 1;
    case GUI_CMD_END:
        e->goal = -1;
        e_go(e, e_len(e), 0);
        return 1;
    }
    return 0;
}

static int editor_event(struct gui_widget *w, struct gui_ev *ev)
{
    struct editor *e = ED(w);
    int shift = (ev->mods & GLOS_MOD_SHIFT) != 0, ctrl = (ev->mods & GLOS_MOD_CTRL) != 0;
    size_t a = e->caret < e->anchor ? e->caret : e->anchor, b = e->caret < e->anchor ? e->anchor : e->caret;
    int oldtop = e->top, oldleft = e->left;
    e_bars(e);
    if (ev->type != GUI_EV_KEY) {
        if (gui_sb_event(w, &e->vsb, ev, 1) || (!e->wrap && gui_sb_event(w, &e->hsb, ev, 16))) {
            e->top = e->vsb.pos;
            if (!e->wrap)
                e->left = e->hsb.pos;
            return 1;
        }
    }
    switch (ev->type) {
    case GUI_EV_PRESS:
        if (ev->button != 1)
            return 0;
        if (ev->clicks == 2) {                  /* a word */
            size_t p = e_hit(e, ev->x, ev->y), s = p, t = p;
            while (s > 0 && is_word(e_at(e, s - 1)))
                s--;
            while (t < e_len(e) && is_word(e_at(e, t)))
                t++;
            e->anchor = s, e->caret = t;
            gui_damage(w);
            return 1;
        }
        e->goal = -1;
        e_go(e, e_hit(e, ev->x, ev->y), shift);
        e->drag = 1;
        return 1;
    case GUI_EV_MOVE:
        if (e->drag) {
            e->goal = -1;
            e_go(e, e_hit(e, ev->x, ev->y), 1);
        }
        return 1;
    case GUI_EV_RELEASE:
        e->drag = 0;
        return 1;
    case GUI_EV_FOCUS:
    case GUI_EV_BLUR:
        gui_damage(w);
        return 1;
    case GUI_EV_KEY:
        break;
    default:
        return 0;
    }
    if (ctrl && !(ev->mods & GLOS_MOD_ALT)) {
        switch (ev->key) {
        case GUI_K_HOME: e->goal = -1; e_go(e, 0, shift); return 1;
        case GUI_K_END: e->goal = -1; e_go(e, e_len(e), shift); return 1;
        case GUI_K_LEFT: e->goal = -1; e_go(e, e_word(e, e->caret, 0), shift); return 1;
        case GUI_K_RIGHT: e->goal = -1; e_go(e, e_word(e, e->caret, 1), shift); return 1;
        case GUI_K_UP: if (e->top > 0) { e->top--; gui_damage(w); } return 1;
        case GUI_K_DOWN: if (e->top + 1 < e_total_rows(e)) { e->top++; gui_damage(w); } return 1;
        }
        switch (ev->ch) {
        case 1: return gui_editor_command(w, GUI_CMD_ALL);
        case 3: gui_editor_command(w, GUI_CMD_COPY); return 1;
        case 24: gui_editor_command(w, GUI_CMD_CUT); return 1;
        case 22: gui_editor_command(w, GUI_CMD_PASTE); return 1;
        case 26: gui_editor_command(w, GUI_CMD_UNDO); return 1;
        }
        return 0;
    }
    if (ev->mods & GLOS_MOD_ALT)
        return 0;
    switch (ev->key) {
    case GUI_K_LEFT: e->goal = -1; e_go(e, shift || a == b ? e_left(e, e->caret) : a, shift); return 1;
    case GUI_K_RIGHT: e->goal = -1; e_go(e, shift || a == b ? e_right(e, e->caret) : b, shift); return 1;
    case GUI_K_UP: e_vertical(e, -1, shift); return 1;
    case GUI_K_DOWN: e_vertical(e, 1, shift); return 1;
    case GUI_K_PGUP: e->top -= e_rows_shown(e) - 1; if (e->top < 0) e->top = 0; e_vertical(e, -(e_rows_shown(e) - 1), shift); return 1;
    case GUI_K_PGDN: e->top += e_rows_shown(e) - 1; e_vertical(e, e_rows_shown(e) - 1, shift); return 1;
    case GUI_K_HOME: {
        size_t ra, rb;
        e_row(e, e_row_of(e, e->caret), &ra, &rb);
        e->goal = -1;
        e_go(e, ra, shift);
        return 1;
    }
    case GUI_K_END: {
        size_t ra, rb;
        e_row(e, e_row_of(e, e->caret), &ra, &rb);
        e->goal = -1;
        e_go(e, rb, shift);
        return 1;
    }
    case GUI_K_BACK:
        if (a != b)
            e_replace(e, a, b, "", 0);
        else if (a > 0)
            e_replace(e, e_left(e, a), a, "", 0);
        return 1;
    case GUI_K_DEL:
        if (a != b)
            e_replace(e, a, b, "", 0);
        else if (a < e_len(e))
            e_replace(e, a, e_right(e, a), "", 0);
        return 1;
    case GUI_K_ENTER:
    case GUI_K_KPENTER:
        e_replace(e, a, b, e->crlf ? "\r\n" : "\n", e->crlf ? 2 : 1);
        return 1;
    case GUI_K_TAB:
        e_replace(e, a, b, "\t", 1);
        return 1;
    }
    if (ev->ch >= 0x20 && ev->ch < 0x100 && ev->ch != 0x7F) {
        char c = (char)ev->ch;
        e_replace(e, a, b, &c, 1);
        return 1;
    }
    if (e->top != oldtop || e->left != oldleft)
        gui_damage(w);
    return 0;
}

static void editor_destroy(struct gui_widget *w)
{
    struct editor *e = ED(w);
    int i;
    for (i = 0; i < e->nundo; i++)
        free(e->undo[i].bytes);
    free(e->buf);
    free(e->ls);
    free(e->rows);
    free(e->rstart);
}

static const struct gui_class editor_class = { "editor", GUI_C_FOCUS, editor_draw, editor_event, editor_measure, NULL,
                                               editor_destroy };

struct gui_widget *gui_editor(struct gui_widget *parent)
{
    struct editor *e = (struct editor *)gui_new(parent, &editor_class, sizeof *e);
    e->font = gui_mono;
    e->tabw = 8 * gui_char_w(e->font, ' ');
    e->goal = -1;
    e->w.grow = 1;
    e_lines_room(e, 1);
    e->nl = 1;
    e->ls[0] = 0;
    e->rows[0] = -1;
    e->rdirty = 1;
    e->crlf = 1;
    return &e->w;
}

void gui_editor_set(struct gui_widget *w, const char *text, size_t n)
{
    struct editor *e = ED(w);
    size_t j;
    int i;
    for (i = 0; i < e->nundo; i++)
        free(e->undo[i].bytes);
    e->nundo = 0;
    free(e->buf);
    e->size = n + 4096;
    e->buf = malloc(e->size);
    if (!e->buf)
        abort();
    memcpy(e->buf, text, n);
    e->gap = n;
    e->gaplen = e->size - n;
    e->nl = 1;
    e->ls[0] = 0;
    for (j = 0; j < n; j++)
        if (text[j] == '\n') {
            e_lines_room(e, 1);
            e->ls[e->nl++] = j + 1;
        }
    for (i = 0; i < e->nl; i++)
        e->rows[i] = -1;
    e->rdirty = 1;
    e->crlf = 1;                                /* (a new file: DOS's line ends) */
    for (j = 0; j < n; j++)
        if (text[j] == '\n') {
            e->crlf = j > 0 && text[j - 1] == '\r';
            break;
        }
    e->caret = e->anchor = 0;
    e->top = e->left = e->show = 0;
    e->goal = -1;
    e->modified = 0;
    gui_damage(w);
}

size_t gui_editor_length(struct gui_widget *w) { return e_len(ED(w)); }

size_t gui_editor_get(struct gui_widget *w, char *out, size_t room)
{
    struct editor *e = ED(w);
    size_t n = e_len(e);
    e_copy(e, 0, n < room ? n : room, out);
    return n;
}

int gui_editor_modified(struct gui_widget *w) { return ED(w)->modified; }
void gui_editor_saved(struct gui_widget *w) { ED(w)->modified = 0; }

void gui_editor_wrap(struct gui_widget *w, int on)
{
    struct editor *e = ED(w);
    int i;
    e->wrap = on;
    e->wrap_w = 0;
    e->left = 0;
    for (i = 0; i < e->nl; i++)
        e->rows[i] = -1;
    e->rdirty = 1;
    e->top = 0;
    e_scroll_to_caret(e);
    gui_damage(w);
}

void gui_editor_font(struct gui_widget *w, const struct gfx_font *f)
{
    struct editor *e = ED(w);
    e->font = f;
    e->tabw = 8 * gui_char_w(f, ' ');
    gui_editor_wrap(w, e->wrap);
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

int gui_editor_find(struct gui_widget *w, const char *what, int match_case, int forward)
{
    struct editor *e = ED(w);
    size_t n = strlen(what), len = e_len(e), p, a = e->caret < e->anchor ? e->caret : e->anchor;
    size_t b = e->caret < e->anchor ? e->anchor : e->caret, k;
    if (!n || n > len)
        return 0;
    p = forward ? b : (a > 0 ? a - 1 : len);
    for (;;) {
        if (forward && p + n > len)
            return 0;
        for (k = 0; k < n; k++) {
            int c = (unsigned char)e_at(e, p + k), d = (unsigned char)what[k];
            if (match_case ? c != d : lower(c) != lower(d))
                break;
        }
        if (k == n) {
            e->anchor = p;
            e->caret = p + n;
            e->goal = -1;
            e_scroll_to_caret(e);
            gui_damage(w);
            return 1;
        }
        if (forward)
            p++;
        else if (p-- == 0)
            return 0;
        if (!forward && p + n > len)
            p = len - n;
    }
}

void gui_editor_replace_sel(struct gui_widget *w, const char *text)
{
    struct editor *e = ED(w);
    size_t a = e->caret < e->anchor ? e->caret : e->anchor, b = e->caret < e->anchor ? e->anchor : e->caret;
    e_replace(e, a, b, text, strlen(text));
}

void gui_editor_goto(struct gui_widget *w, int line)
{
    struct editor *e = ED(w);
    if (line < 1)
        line = 1;
    if (line > e->nl)
        line = e->nl;
    e->goal = -1;
    e_go(e, e->ls[line - 1], 0);
    if (e->show)
        e->show = 2;
    else
        e->top = e_row_of(e, e->caret) - e_rows_shown(e) / 3 > 0 ? e_row_of(e, e->caret) - e_rows_shown(e) / 3 : 0;
}

void gui_editor_where(struct gui_widget *w, int *line, int *col)
{
    struct editor *e = ED(w);
    int l = e_line(e, e->caret);
    size_t p;
    int c = 1;
    for (p = e->ls[l]; p < e->caret; p++)
        c = e_at(e, p) == '\t' ? ((c - 1) / 8 + 1) * 8 + 1 : c + 1;
    *line = l + 1;
    *col = c;
}
