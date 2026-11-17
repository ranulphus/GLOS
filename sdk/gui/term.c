/* The terminal widget (toolkit 2, step C8; docs/v1/toolkit-2.md, Widgets): the Terminal's text grid and xterm
 * screen (vt.c), moved into the toolkit for the IDE's output and the Task Manager's log too.
 *
 * What the app's program writes is fed to it (gui_term_feed); what is typed, pasted and what the screen answers
 * (the status reports) goes back through the app's send. The grid is as many cells as fit (the app hears the size
 * through its resize, for the program); so many lines are kept above the screen (Shift+PgUp, Shift+PgDn, the bar,
 * the wheel). The mouse chooses text, in what is kept too; Ctrl+Shift+C copies it, Shift+Ins and Ctrl+Shift+V
 * type the clipboard's text in (bracketed if the program asked). A double click on a line gives the app the line's
 * text (its line: a compiler's message, a log's entry). GUI_ON_CHANGE: the program set the title. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guiint.h"
#include "vt.h"

struct kept {                           /* a line that left the top of the screen: its cells to the last used */
    struct vt_cell *cell;
    int n;
};

struct term {
    struct gui_widget w;
    struct vt *vt;
    struct kept *kept;
    int room, ktop, kcount;             /* the lines kept: room at most, the oldest's place, how many */
    int cols, rows, cw, ch;
    int back;                           /* lines scrolled back (0: the screen shows) */
    int sel_on, sel_a_line, sel_a_col, sel_b_line, sel_b_col, dragging;        /* lines: kept ones, then the screen's */
    int cursor_y, ended, discard;
    struct gui_sb sb;
    struct gui_term_cb cb;
};

#define TM(w) ((struct term *)(w))

static const struct kept *kept_at(struct term *t, int k) { return &t->kept[(t->ktop + k) % t->room]; }

static int utf8_of(unsigned c, char *u)
{
    if (c < 0x80) {
        u[0] = (char)c;
        return 1;
    }
    if (c < 0x800) {
        u[0] = (char)(0xC0 | (c >> 6)), u[1] = (char)(0x80 | (c & 0x3F));
        return 2;
    }
    u[0] = (char)(0xE0 | (c >> 12)), u[1] = (char)(0x80 | ((c >> 6) & 0x3F)), u[2] = (char)(0x80 | (c & 0x3F));
    return 3;
}

static void line_gone(void *ctx, const struct vt_cell *row, int n)
{
    struct term *t = ctx;
    struct kept *k;
    if (t->discard || !t->room)                 /* (gui_term_clear: the screen being wiped is not kept) */
        return;
    while (n > 0 && row[n - 1].ch == ' ' && row[n - 1].bg == VT_DEFAULT && !(row[n - 1].attr & VT_REVERSE))
        n--;
    if (t->kcount == t->room) {                 /* the oldest goes */
        free(t->kept[t->ktop].cell);
        t->ktop = (t->ktop + 1) % t->room;
        t->kcount--;
        t->sel_a_line--, t->sel_b_line--;
    }
    k = &t->kept[(t->ktop + t->kcount) % t->room];
    k->cell = n ? malloc((size_t)n * sizeof *row) : NULL;
    k->n = k->cell ? n : 0;
    if (k->cell)
        memcpy(k->cell, row, (size_t)n * sizeof *row);
    t->kcount++;
    if (t->back && t->back < t->kcount)         /* (what is looked at stays where it is) */
        t->back++;
    gui_damage(&t->w);
}

/* Line `line` (kept ones first, then the screen's rows), column x: its cell, or NULL for nothing there. */
static const struct vt_cell *cell_at(struct term *t, int line, int x)
{
    if (line < 0 || x < 0 || x >= t->cols)
        return NULL;
    if (line < t->kcount) {
        const struct kept *k = kept_at(t, line);
        return x < k->n ? &k->cell[x] : NULL;
    }
    line -= t->kcount;
    return line < t->vt->rows && x < t->vt->cols ? &t->vt->cell[line * VT_COLS_MAX + x] : NULL;
}

static int first_shown(struct term *t) { return t->kcount - t->back; }

static int in_sel(struct term *t, int line, int col)
{
    int al = t->sel_a_line, ac = t->sel_a_col, bl = t->sel_b_line, bc = t->sel_b_col;
    if (!t->sel_on)
        return 0;
    if (al > bl || (al == bl && ac > bc)) {
        int s = al;
        al = bl, bl = s;
        s = ac, ac = bc, bc = s;
    }
    if (line < al || line > bl)
        return 0;
    return (line > al || col >= ac) && (line < bl || col < bc);
}

static void send(struct term *t, const char *d, int n, int typed)
{
    if (t->cb.send && n > 0 && !t->ended)
        t->cb.send(&t->w, d, n, typed);
}

/* ---- the grid's size and drawing */
static void measure_grid(struct term *t)
{
    int w = t->w.r.w - GUI_SB - 8, h = t->w.r.h - 4;
    t->cw = gui_char_w(gui_mono, 'n');
    t->ch = gui_mono->height;
    t->cols = w / t->cw > 20 ? w / t->cw : 20;
    if (t->cols > VT_COLS_MAX)
        t->cols = VT_COLS_MAX;
    t->rows = h / t->ch > 3 ? h / t->ch : 3;
    if (t->rows > VT_ROWS_MAX)
        t->rows = VT_ROWS_MAX;
}

static void tm_layout(struct gui_widget *w)    /* the window's size is the screen's: the app's program told */
{
    struct term *t = TM(w);
    measure_grid(t);
    if (t->cols != t->vt->cols || t->rows != t->vt->rows) {
        vt_resize(t->vt, t->cols, t->rows);
        if (t->cb.resize && !t->ended)
            t->cb.resize(w, t->cols, t->rows);
    }
    gui_damage(w);
}

static void tm_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct term *t = TM(w);
    struct vt *vt = t->vt;
    struct gfx_rect frame = { w->r.x, w->r.y, w->r.w - GUI_SB + 1, w->r.h };
    int x0 = w->r.x + 4, y0 = w->r.y + 2, y, x, ymin, ymax, first = first_shown(t), cw = t->cw, ch = t->ch;
    measure_grid(t);
    gui_draw_field(s, &frame, 1);
    ymin = (s->clip.y - y0) / ch;               /* (only the rows being drawn again) */
    ymax = (s->clip.y + s->clip.h - y0) / ch + 1;
    for (y = ymin > 0 ? ymin : 0; y < t->rows && y <= ymax; y++) {
        int line = first + y, on_screen = line >= t->kcount;
        for (x = 0; x < t->cols; x++) {
            const struct vt_cell *c = cell_at(t, line, x);
            int chosen = in_sel(t, line, x), rev, cur;
            unsigned fg, bg;
            char u[3];
            cur = on_screen && vt->cursor_on && x == vt->x && line - t->kcount == vt->y && gui_window_active(w->win)
                  && !t->ended;
            if (!c) {
                if (chosen || cur)
                    gfx_fill(s, x0 + x * cw, y0 + y * ch, cw, ch, chosen ? GUI_CHOSEN : 0x000000);
                continue;
            }
            rev = !!(c->attr & VT_REVERSE) ^ vt->reverse_screen;
            fg = vt_rgb(rev ? c->bg : c->fg, !rev), bg = vt_rgb(rev ? c->fg : c->bg, rev);
            if ((c->attr & VT_BOLD) && c->fg < 8 && !rev)
                fg = vt_rgb((unsigned short)(c->fg + 8), 1);     /* (bold as the bright colour, as xterm can) */
            if (cur) {
                unsigned k = fg;
                fg = bg, bg = k;
            }
            if (chosen)
                fg = GUI_CHOSEN_TEXT, bg = GUI_CHOSEN;
            if (bg != 0xFFFFFF)
                gfx_fill(s, x0 + x * cw, y0 + y * ch, cw, ch, bg);
            if (c->ch == ' ' || (c->attr & VT_HIDDEN))
                continue;
            gfx_text(s, x0 + x * cw, y0 + y * ch, gui_mono, u, utf8_of(c->ch, u), fg);
            if (c->attr & VT_UNDER)
                gfx_fill(s, x0 + x * cw, y0 + y * ch + ch - 1, cw, 1, fg);
        }
        if (on_screen && line - t->kcount < vt->rows)
            vt->dirty[line - t->kcount] = 0;
    }
    t->cursor_y = t->back ? -1 : vt->y;
    t->sb.r.x = w->r.x + w->r.w - GUI_SB, t->sb.r.y = w->r.y, t->sb.r.w = GUI_SB, t->sb.r.h = w->r.h;
    t->sb.total = t->kcount + t->rows;
    t->sb.page = t->rows;
    t->sb.pos = first;
    gui_sb_draw(s, &t->sb, 1);
}

/* ---- what the program wrote */
void gui_term_feed(struct gui_widget *w, const void *data, int n)
{
    struct term *t = TM(w);
    struct vt *vt = t->vt;
    int y;
    if (n <= 0)
        return;
    vt_feed(vt, data, n);
    t->discard = 0;
    if (vt->reply_n) {                          /* (what a program asked the terminal: its answer) */
        int k = vt->reply_n;
        vt->reply_n = 0;
        send(t, vt->reply, k, 0);
    }
    if (vt->title_set) {
        vt->title_set = 0;
        gui_send(w, GUI_ON_CHANGE);
    }
    if (t->back) {
        gui_damage(w);
    } else if (t->ch) {                         /* the rows that changed, and the cursor's old and new */
        if (t->cursor_y >= 0 && t->cursor_y < vt->rows)
            vt->dirty[t->cursor_y] = 1;
        vt->dirty[vt->y] = 1;
        for (y = 0; y < vt->rows; y++)
            if (vt->dirty[y]) {
                struct gfx_rect r;
                int k = 1;
                while (y + k < vt->rows && vt->dirty[y + k])
                    k++;
                r.x = w->r.x + 1, r.w = w->r.w - GUI_SB - 1;
                r.y = w->r.y + 2 + y * t->ch, r.h = k * t->ch;
                if (w->win)
                    gui_damage_rect(w->win, &r);
                y += k - 1;
            }
    }
}

/* ---- copy and paste */
static char *selected(struct term *t, int *len)
{
    char *s;
    int line, n = 0, room = 65536;
    if (!t->sel_on || !(s = malloc((size_t)room)))
        return NULL;
    for (line = 0; line < t->kcount + t->vt->rows && n < room - 3; line++) {
        int c, any = 0;
        for (c = 0; c < t->cols && n < room - 6; c++)
            if (in_sel(t, line, c)) {
                const struct vt_cell *cell = cell_at(t, line, c);
                if (cell)
                    n += utf8_of(cell->ch, s + n);
                else
                    s[n++] = ' ';
                any = 1;
            }
        if (any && in_sel(t, line + 1, 0)) {
            while (n > 0 && s[n - 1] == ' ')
                n--;
            s[n++] = '\r', s[n++] = '\n';
        }
    }
    while (n > 0 && s[n - 1] == ' ')
        n--;
    *len = n;
    return s;
}

void gui_term_copy(struct gui_widget *w)
{
    int n;
    char *s = selected(TM(w), &n);
    if (s) {
        gui_clip_set(s, (size_t)n);
        free(s);
    }
}

void gui_term_paste(struct gui_widget *w)
{
    struct term *t = TM(w);
    size_t n, i, k = 0;
    char *c = gui_clip_get(&n);
    if (!c)
        return;
    for (i = 0; i < n; i++)                     /* (a line's end is Enter's carriage return) */
        if (c[i] != '\n')
            c[k++] = c[i];
        else if (!i || c[i - 1] != '\r')
            c[k++] = '\r';
    if (t->vt->bracketed)
        send(t, "\033[200~", 6, 1);
    send(t, c, (int)k, 1);
    if (t->vt->bracketed)
        send(t, "\033[201~", 6, 1);
    free(c);
}

void gui_term_clear(struct gui_widget *w)      /* what is kept goes; the screen the program wipes next isn't kept */
{
    struct term *t = TM(w);
    while (t->kcount) {
        free(t->kept[t->ktop].cell);
        t->ktop = (t->ktop + 1) % t->room;
        t->kcount--;
    }
    t->back = t->sel_on = 0;
    t->discard = 1;
    gui_damage(w);
}

/* ---- the mouse and the keyboard */
static void tell_line(struct term *t, int line)        /* a double click: the line's text to the app */
{
    char text[VT_COLS_MAX * 3 + 1];
    if (t->cb.line && gui_term_line(&t->w, line, text, sizeof text) >= 0)
        t->cb.line(&t->w, line, text);
}

static int tm_event(struct gui_widget *w, struct gui_ev *e)
{
    struct term *t = TM(w);
    int old = t->back, n, ch = t->ch ? t->ch : 1, cw = t->cw ? t->cw : 1;
    char out[32];
    t->sb.r.x = w->r.x + w->r.w - GUI_SB, t->sb.r.y = w->r.y, t->sb.r.w = GUI_SB, t->sb.r.h = w->r.h;
    t->sb.total = t->kcount + t->rows, t->sb.page = t->rows, t->sb.pos = first_shown(t);
    if (e->type != GUI_EV_KEY && gui_sb_event(w, &t->sb, e, 1)) {
        t->back = t->kcount - t->sb.pos;
        t->back = t->back < 0 ? 0 : t->back > t->kcount ? t->kcount : t->back;
        return 1;
    }
    switch (e->type) {
    case GUI_EV_PRESS:
        if (e->button != 1)
            return 1;
        if (e->clicks == 2) {
            t->sel_on = t->dragging = 0;
            tell_line(t, first_shown(t) + (e->y - w->r.y - 2) / ch);
            gui_damage(w);
            return 1;
        }
        t->sel_on = 1;
        t->sel_a_line = t->sel_b_line = first_shown(t) + (e->y - w->r.y - 2) / ch;
        t->sel_a_col = t->sel_b_col = (e->x - w->r.x - 4 + cw / 2) / cw;
        t->dragging = 1;
        gui_damage(w);
        return 1;
    case GUI_EV_MOVE:
        if (t->dragging) {
            t->sel_b_line = first_shown(t) + (e->y - w->r.y - 2) / ch;
            t->sel_b_col = (e->x - w->r.x - 4 + cw / 2) / cw;
            gui_damage(w);
        }
        return 1;
    case GUI_EV_RELEASE:
        t->dragging = 0;
        if (t->sel_a_line == t->sel_b_line && t->sel_a_col == t->sel_b_col)
            t->sel_on = 0;
        gui_damage(w);
        return 1;
    case GUI_EV_WHEEL:
        t->back += e->wheel * 3;
        t->back = t->back < 0 ? 0 : t->back > t->kcount ? t->kcount : t->back;
        gui_damage(w);
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
    if (e->mods & GLOS_MOD_SHIFT && (e->key == GUI_K_PGUP || e->key == GUI_K_PGDN)) {
        t->back += e->key == GUI_K_PGUP ? t->rows - 1 : -(t->rows - 1);
        t->back = t->back > t->kcount ? t->kcount : t->back < 0 ? 0 : t->back;
        if (t->back != old)
            gui_damage(w);
        return 1;
    }
    if ((e->mods & GLOS_MOD_SHIFT && e->key == GUI_K_INS) || (e->mods == (GLOS_MOD_CTRL | GLOS_MOD_SHIFT) && e->key == 0x2F)) {
        gui_term_paste(w);
        return 1;
    }
    if (e->mods == (GLOS_MOD_CTRL | GLOS_MOD_SHIFT) && e->key == 0x2E) {
        gui_term_copy(w);
        return 1;
    }
    if (e->key == 0x2A || e->key == 0x36 || e->key == 0x1D || e->key == 0x38 || e->key == 0x11D || e->key == 0x138
        || e->key == 0x3A || e->key == 0x45)
        return 1;                               /* (the modifiers alone are no keys) */
    if (e->mods & GLOS_MOD_ALT)
        return 0;                               /* (the menu's) */
    if (t->ended)
        return 0;
    if (t->sel_on || t->back) {
        t->sel_on = 0;
        t->back = 0;                            /* (typing is at the screen) */
        gui_damage(w);
    }
    if ((n = vt_key(t->vt, e->key, e->ch, e->mods, out, sizeof out)) > 0)
        send(t, out, n, 1);
    return 1;
}

static void tm_measure(struct gui_widget *w, int *pw, int *ph)
{
    (void)w;
    *pw = 8 + 80 * gui_char_w(gui_mono, 'n') + GUI_SB, *ph = 4 + 25 * gui_mono->height;
}

static void tm_destroy(struct gui_widget *w)
{
    struct term *t = TM(w);
    while (t->kcount) {
        free(t->kept[t->ktop].cell);
        t->ktop = (t->ktop + 1) % t->room;
        t->kcount--;
    }
    free(t->kept);
    free(t->vt);
}

static const struct gui_class tm_class = { "terminal", GUI_C_FOCUS, tm_draw, tm_event, tm_measure, tm_layout,
                                           tm_destroy };

struct gui_widget *gui_terminal(struct gui_widget *parent, int lines_kept)
{
    struct term *t = (struct term *)gui_new(parent, &tm_class, sizeof *t);
    t->vt = calloc(1, sizeof *t->vt);
    t->room = lines_kept > 0 ? lines_kept : 0;
    t->kept = t->room ? calloc((size_t)t->room, sizeof *t->kept) : NULL;
    if (!t->kept)
        t->room = 0;
    t->cols = 80, t->rows = 25, t->cursor_y = -1;
    if (!t->vt)
        abort();
    vt_init(t->vt, t->cols, t->rows);
    t->vt->gone = line_gone;
    t->vt->gone_ctx = t;
    return &t->w;
}

void gui_term_callbacks(struct gui_widget *w, const struct gui_term_cb *cb)
{
    TM(w)->cb = *cb;
}

void gui_term_size(struct gui_widget *w, int *cols, int *rows)
{
    struct term *t = TM(w);
    if (t->w.r.w)                               /* (laid out: the size it has; else 80 by 25) */
        measure_grid(t);
    if (t->cols != t->vt->cols || t->rows != t->vt->rows)
        vt_resize(t->vt, t->cols, t->rows);
    *cols = t->cols, *rows = t->rows;
}

void gui_term_end(struct gui_widget *w)        /* the program has gone: no cursor, keys not taken */
{
    TM(w)->ended = 1;
    gui_damage(w);
}

const char *gui_term_title(struct gui_widget *w) { return TM(w)->vt->title; }

int gui_term_kept(struct gui_widget *w) { return TM(w)->kcount; }

int gui_term_rows(struct gui_widget *w) { return TM(w)->vt->rows; }

int gui_term_cols(struct gui_widget *w) { return TM(w)->vt->cols; }

int gui_term_line(struct gui_widget *w, int line, char *out, int room)
{
    struct term *t = TM(w);
    int n = 0, c, cols;
    if (line < 0 || line >= t->kcount + t->vt->rows || room < 1)
        return -1;
    if (line < t->kcount) {                     /* (a kept line as it was kept, whatever the width now) */
        const struct kept *k = kept_at(t, line);
        for (c = 0; c < k->n && n < room - 4; c++)
            n += utf8_of(k->cell[c].ch, out + n);
    } else {
        cols = t->vt->cols;
        for (c = 0; c < cols && n < room - 4; c++) {
            const struct vt_cell *cell = cell_at(t, line, c);
            n += utf8_of(cell ? cell->ch : ' ', out + n);
        }
    }
    while (n > 0 && out[n - 1] == ' ')
        n--;
    out[n] = 0;
    return n;
}

int gui_term_dump(struct gui_widget *w, char *out, int room) { return vt_dump(TM(w)->vt, out, room); }
