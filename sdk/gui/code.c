/* The code editor (toolkit 2, step C8; docs/v1/toolkit-2.md, Widgets): the IDE's and Notepad's editor grown.
 *
 * The text is a piece table (piece.c), so a big file opens at once and edits cost the same anywhere in it. The
 * margin has the lines' numbers, their marks (a bookmark, a breakpoint, an error or a warning, each with a note the
 * pointer shows), and boxes that fold what is indented under a line. The app's lexer colours a line at a time (its
 * state carried from line to line, kept, and worked out again only from a changed line on); the toolkit has one for
 * C. The bracket at the caret and its partner are framed. Alt with the mouse or with Shift and the arrows chooses a
 * column; typing then goes into every line of it. Ctrl and the wheel, or Ctrl with + and -, zoom (the fixed font's
 * three sizes). Ctrl+Space asks the app for words that may complete the one at the caret (gui_code_complete shows
 * them under it); gui_code_signature shows a call's parameters over it. Every change is a step of the toolkit's undo
 * (Ctrl+Z, Ctrl+Y; typing in a burst one step).
 *
 * Keys: the arrows, Home (the line's first non-blank, then its start), End, Page Up, Page Down, Ctrl with Home, End
 * and the side arrows (the text's ends, words), Shift choosing; Enter keeps the line's indentation; Tab indents the
 * lines chosen (Shift+Tab takes it back) or puts in spaces to the next stop; Ctrl+A, C, X, V. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guiint.h"
#include "piece.h"

#define LINE_MAX 4096                   /* bytes of a line drawn and lexed (the rest is there, not shown) */
#define FOLD_W 12
#define POP_ROWS 8

struct cmark {
    int line;
    unsigned flags;
    char *note;
};

struct fold {
    int head, end;                      /* the head line, and the last line folded under it */
};

struct code {
    struct gui_widget w;
    struct ptable t;
    const struct gfx_font *font;
    int zoom, cw, ch, tabw;
    size_t caret, anchor;
    int col_mode;                       /* a column chosen: lines col_a..col_b, columns cx_a..cx_b (characters) */
    int col_a, col_b, cx_a, cx_b;
    int top, left;                      /* the first row shown (rows: the lines not folded away); pixels to the right */
    int goal;                           /* the column Up and Down aim for (-1: the caret's) */
    struct gui_sb vsb, hsb;
    int drag, modified, widest;
    gui_lexer lex;
    void *lex_ctx;
    int *state;                         /* the lexer's state at each line's start, known for lines below state_ok */
    int sroom, state_ok;
    unsigned char *folded;              /* per line: 1 if folded (its head) */
    int froom;
    struct fold *folds;                 /* the folded ranges, in order, none inside another (fold_dirty: again) */
    int nfolds, foldroom, fold_dirty;
    struct cmark *marks;
    int nmarks;
    struct gui_undo *undo;
    int busy;                           /* (an undo or redo being done: not a step of its own) */
    /* completion and signature */
    struct gui_window *pop, *sig;
    struct gui_widget *popw;
    char **words;
    int nwords, hot, shown[64], nshown;
    size_t word_at;                     /* where the word being completed begins */
    char sigtext[160];
    void (*on_complete)(struct gui_widget *w, const char *word);
    unsigned style;                     /* GUI_CODE_WRAP, GUI_CODE_PLAIN */
    int crlf;                           /* the text's lines end in CR LF (its first does): Enter puts both in */
    int *wfirst;                        /* wrapping: each line's first row (nlines + 1 of them; a folded line has its
                                           next's), valid while wvalid and the width is wcols */
    int wroom, wvalid, wcols;
};

#define CE(w) ((struct code *)(w))

static int is_word(int c) { return c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c >= 0x80; }

/* ---- lines */
static int nlines(struct code *c) { return (int)pt_lines(&c->t); }

static size_t line_start(struct code *c, int line) { return pt_line_start(&c->t, (size_t)line); }

static size_t line_end(struct code *c, int line)       /* before its '\n' (and a '\r' before that) */
{
    size_t e = line + 1 < nlines(c) ? line_start(c, line + 1) - 1 : pt_length(&c->t);
    if (e > line_start(c, line) && pt_byte(&c->t, e - 1) == '\r')
        e--;
    return e;
}

static int line_text(struct code *c, int line, char *out)      /* its bytes (up to LINE_MAX), their count */
{
    size_t s = line_start(c, line), e = line_end(c, line);
    int n = (int)(e - s > LINE_MAX ? LINE_MAX : e - s);
    return (int)pt_read(&c->t, s, out, (size_t)n);
}

static int line_of(struct code *c, size_t at) { return (int)pt_line_of(&c->t, at); }

static int col_of(struct code *c, const char *t, int n)        /* the column after n bytes (tabs to their stops) */
{
    int i, col = 0;
    for (i = 0; i < n; i++) {
        if (t[i] == '\t')
            col = (col / c->tabw + 1) * c->tabw;
        else if (((unsigned char)t[i] & 0xC0) != 0x80)
            col++;
    }
    return col;
}

static int byte_at_col(struct code *c, const char *t, int n, int col)  /* the byte at (or nearest before) a column */
{
    int i = 0, cc = 0;
    while (i < n) {
        int next = t[i] == '\t' ? (cc / c->tabw + 1) * c->tabw : cc + 1;
        if (next > col) {
            if (col - cc > next - col)          /* (nearer its end: after it) */
                i++;
            break;
        }
        cc = next, i++;
        while (i < n && ((unsigned char)t[i] & 0xC0) == 0x80)
            i++;
    }
    return i;
}

static void where(struct code *c, size_t at, int *line, int *col)
{
    char t[LINE_MAX];
    int n;
    *line = line_of(c, at);
    n = line_text(c, *line, t);
    *col = col_of(c, t, (int)(at - line_start(c, *line)) < n ? (int)(at - line_start(c, *line)) : n);
}

static size_t at_of(struct code *c, int line, int col)
{
    char t[LINE_MAX];
    int n;
    line = line < 0 ? 0 : line >= nlines(c) ? nlines(c) - 1 : line;
    n = line_text(c, line, t);
    return line_start(c, line) + (size_t)byte_at_col(c, t, n, col);
}

static int indent_of(struct code *c, int line)         /* its leading blanks as columns; -1 for a blank line */
{
    char t[LINE_MAX];
    int n = line_text(c, line, t), i = 0;
    while (i < n && (t[i] == ' ' || t[i] == '\t'))
        i++;
    return i == n ? -1 : col_of(c, t, i);
}

/* ---- folds: what is indented under a line, hidden while its head is folded */
static int foldable(struct code *c, int line)           /* the next line with text is deeper */
{
    int k, in = indent_of(c, line), next;
    if (in < 0)
        return 0;
    for (k = line + 1; k < nlines(c) && k < line + 50; k++)
        if ((next = indent_of(c, k)) >= 0)
            return next > in;
    return 0;
}

static int fold_end(struct code *c, int head)
{
    int in = indent_of(c, head), k, last = head, d;
    for (k = head + 1; k < nlines(c); k++) {
        if ((d = indent_of(c, k)) < 0)
            continue;
        if (d <= in)
            break;
        last = k;
    }
    return last;
}

static void make_folds(struct code *c)
{
    int line, n = nlines(c);
    c->nfolds = 0;
    c->fold_dirty = 0;
    for (line = 0; line < n && line < c->froom; line++) {
        int end;
        if (!c->folded[line])
            continue;
        if (!foldable(c, line)) {               /* (the text changed under it) */
            c->folded[line] = 0;
            continue;
        }
        end = fold_end(c, line);
        if (c->nfolds == c->foldroom) {
            int room = c->foldroom * 2 + 16;
            struct fold *f = realloc(c->folds, (size_t)room * sizeof *f);
            if (!f)
                return;
            c->folds = f, c->foldroom = room;
        }
        c->folds[c->nfolds].head = line, c->folds[c->nfolds].end = end;
        c->nfolds++;
        line = end;                             /* (folds inside it are hidden with it) */
    }
}

static void folds_now(struct code *c) { if (c->fold_dirty) make_folds(c); }

static int plain_nrows(struct code *c)
{
    int k, n = nlines(c);
    folds_now(c);
    for (k = 0; k < c->nfolds; k++)
        n -= c->folds[k].end - c->folds[k].head;
    return n;
}

static int plain_line_of_row(struct code *c, int row)
{
    int k, line = row;
    folds_now(c);
    for (k = 0; k < c->nfolds && c->folds[k].head < line; k++)
        line += c->folds[k].end - c->folds[k].head;
    return line < nlines(c) ? line : nlines(c) - 1;
}

static int plain_row_of_line(struct code *c, int line)  /* (a folded line: its head's row) */
{
    int k, row = line;
    folds_now(c);
    for (k = 0; k < c->nfolds && c->folds[k].head < line; k++) {
        if (line <= c->folds[k].end)
            return c->folds[k].head - (line - row);
        row -= c->folds[k].end - c->folds[k].head;
    }
    return row;
}

/* ---- wrapping: a line's rows start at these columns (after the last space that fits, else where it fills) */
#define NOEND 0x7FFFFFFF
static int wrap_width(struct code *c);

static int breaks(struct code *c, int line, int *st, int max)
{
    char t[LINE_MAX];
    int n = line_text(c, line, t), i = 0, col = 0, start = 0, after_space = -1, k = 1, cols = wrap_width(c);
    st[0] = 0;
    while (i < n) {
        int next = t[i] == '\t' ? (col / c->tabw + 1) * c->tabw : col + 1, adv = 1;
        while (i + adv < n && ((unsigned char)t[i + adv] & 0xC0) == 0x80)
            adv++;
        if (next - start > cols && col > start) {      /* this character goes to the next row */
            start = after_space > start ? after_space : col;
            if (k < max)
                st[k] = start;
            k++;
            after_space = -1;
        }
        if (t[i] == ' ' || t[i] == '\t')
            after_space = next;
        col = next, i += adv;
    }
    return k;
}

static int hidden(struct code *c, int line)            /* inside a fold (not its head) */
{
    int k;
    folds_now(c);
    for (k = 0; k < c->nfolds && c->folds[k].head < line; k++)
        if (line <= c->folds[k].end)
            return c->folds[k].head + 1;
    return 0;
}

static void wrap_build(struct code *c)
{
    int n = nlines(c), line, k = 0, st[1];
    if (c->wvalid && c->wcols == wrap_width(c))
        return;
    if (n + 1 > c->wroom) {
        int room = n * 2 + 64, *f = realloc(c->wfirst, (size_t)room * sizeof *f);
        if (!f)
            return;
        c->wfirst = f, c->wroom = room;
    }
    folds_now(c);
    for (line = 0; line < n; line++) {
        c->wfirst[line] = k;
        if (!hidden(c, line))
            k += breaks(c, line, st, 1);
    }
    c->wfirst[n] = k;
    c->wvalid = 1, c->wcols = wrap_width(c);
}

static int nrows(struct code *c)
{
    if (!(c->style & GUI_CODE_WRAP))
        return plain_nrows(c);
    wrap_build(c);
    return c->wfirst ? c->wfirst[nlines(c)] : plain_nrows(c);
}

/* The line a row shows, and the columns it shows of it ([c0, c1); c1 NOEND: to its end). */
static int row_line(struct code *c, int row, int *c0, int *c1)
{
    int lo = 0, hi, line, st[256], k;
    *c0 = 0, *c1 = NOEND;
    if (c->style & GUI_CODE_WRAP)
        wrap_build(c);
    if (!(c->style & GUI_CODE_WRAP) || !c->wfirst)
        return plain_line_of_row(c, row);
    hi = nlines(c) - 1;
    if (row >= c->wfirst[nlines(c)])
        row = c->wfirst[nlines(c)] - 1;
    while (lo < hi) {                           /* the last line whose first row is at or before it */
        int mid = (lo + hi + 1) / 2;
        if (c->wfirst[mid] <= row)
            lo = mid;
        else
            hi = mid - 1;
    }
    line = lo;
    while (line > 0 && c->wfirst[line] == c->wfirst[line + 1])
        line--;                                 /* (a line of no rows, folded: the one before it) */
    k = breaks(c, line, st, 256);
    row -= c->wfirst[line];
    if (row >= 0 && row < k && row < 256) {
        *c0 = st[row];
        *c1 = row + 1 < k && row + 1 < 256 ? st[row + 1] : NOEND;
    }
    return line;
}

static int row_of(struct code *c, int line, int col)   /* the row a place is on */
{
    int st[256], k, i, h;
    if (c->style & GUI_CODE_WRAP)
        wrap_build(c);
    if (!(c->style & GUI_CODE_WRAP) || !c->wfirst)
        return plain_row_of_line(c, line);
    if ((h = hidden(c, line)) != 0)
        return c->wfirst[h - 1];
    k = breaks(c, line, st, 256);
    for (i = k < 256 ? k - 1 : 255; i > 0 && st[i] > col; i--)
        ;
    return c->wfirst[line] + i;
}

static int row_of_line(struct code *c, int line) { return row_of(c, line, 0); }

static void fold_set(struct code *c, int line, int on)
{
    if (line >= c->froom) {
        int room = line * 2 + 64;
        unsigned char *f = realloc(c->folded, (size_t)room);
        if (!f)
            return;
        memset(f + c->froom, 0, (size_t)(room - c->froom));
        c->folded = f, c->froom = room;
    }
    c->folded[line] = (unsigned char)on;
    c->fold_dirty = 1;
    c->wvalid = 0;
    if (on) {                                   /* (the caret inside: to the head) */
        int cl, cc;
        where(c, c->caret, &cl, &cc);
        if (cl > line && cl <= fold_end(c, line))
            c->caret = c->anchor = line_end(c, line);
    }
    gui_damage(&c->w);
}

/* ---- the lexer's states */
static int state_at(struct code *c, int line)
{
    char t[LINE_MAX];
    unsigned char cls[LINE_MAX];
    if (!c->lex)
        return 0;
    if (line + 1 > c->sroom) {
        int room = line * 2 + 256;
        int *s = realloc(c->state, (size_t)room * sizeof *s);
        if (!s)
            return 0;
        c->state = s, c->sroom = room;
    }
    if (c->state_ok == 0)
        c->state[0] = 0, c->state_ok = 1;
    while (c->state_ok <= line) {               /* (from the last known on) */
        int l = c->state_ok - 1, n = line_text(c, l, t);
        c->state[l + 1] = c->lex(c->lex_ctx, t, n, c->state[l], cls);
        c->state_ok++;
    }
    return c->state[line];
}

/* ---- metrics and layout */
static void metrics(struct code *c)
{
    const struct gfx_font *f = gui_font_get(GUI_FONT_MONO, c->zoom);
    c->font = f ? f : gui_mono;
    c->cw = gui_char_w(c->font, 'n');
    c->ch = c->font->height + 1;
}

static int margin_w(struct code *c)
{
    char t[16];
    if (c->style & GUI_CODE_PLAIN)
        return 4;
    int digits = snprintf(t, sizeof t, "%d", nlines(c));
    return (digits < 3 ? 3 : digits) * c->cw + 8 + 12 + FOLD_W;        /* number, marks, the fold box */
}

static struct gfx_rect text_r(struct code *c)     /* (wrapped: no bar across, the text down to the bottom) */
{
    struct gfx_rect r = { c->w.r.x + margin_w(c), c->w.r.y + 1, c->w.r.w - margin_w(c) - GUI_SB - 1,
                          c->w.r.h - 2 - (c->style & GUI_CODE_WRAP ? 0 : GUI_SB) };
    return r;
}

static int rows_fit(struct code *c) { return text_r(c).h / c->ch > 1 ? text_r(c).h / c->ch : 1; }

static int wrap_width(struct code *c) { int k = (text_r(c).w - 2) / (c->cw > 0 ? c->cw : 8); return k > 8 ? k : 8; }

static void place(struct code *c)
{
    struct gfx_rect r;
    metrics(c);
    r = text_r(c);
    c->vsb.r.x = c->w.r.x + c->w.r.w - GUI_SB, c->vsb.r.y = c->w.r.y, c->vsb.r.w = GUI_SB;
    c->vsb.r.h = c->w.r.h - (c->style & GUI_CODE_WRAP ? 0 : GUI_SB);
    c->hsb.r.x = c->w.r.x, c->hsb.r.y = c->w.r.y + c->w.r.h - GUI_SB, c->hsb.r.w = c->w.r.w - GUI_SB, c->hsb.r.h = GUI_SB;
    if (c->style & GUI_CODE_WRAP)
        c->hsb.r.w = c->hsb.r.h = 0;
    c->vsb.total = nrows(c), c->vsb.page = rows_fit(c), c->vsb.pos = c->top;
    gui_sb_clamp(&c->vsb);
    c->top = c->vsb.pos;
    c->hsb.total = c->style & GUI_CODE_WRAP ? r.w : c->widest + c->cw * 4, c->hsb.page = r.w, c->hsb.pos = c->left;
    gui_sb_clamp(&c->hsb);
    c->left = c->hsb.pos;
}

static void show_caret(struct code *c)
{
    int line, col, row, x;
    struct gfx_rect r;
    place(c);
    where(c, c->caret, &line, &col);
    row = row_of(c, line, col);
    if (row < c->top)
        c->top = row;
    if (row >= c->top + rows_fit(c))
        c->top = row - rows_fit(c) + 1;
    r = text_r(c);
    x = col * c->cw;
    if (c->style & GUI_CODE_WRAP)
        c->left = 0;                            /* (wrapped: nothing to the right) */
    else if (x < c->left)
        c->left = x - r.w / 4 > 0 ? x - r.w / 4 : 0;
    else if (x > c->left + r.w - 2 * c->cw)
        c->left = x - r.w + r.w / 4;
    gui_damage(&c->w);
}

/* ---- colours (light or dark, as the field is) */
static unsigned tok_rgb(int cls)
{
    unsigned f = GUI_FIELD;
    int dark = ((f >> 16 & 255) * 3 + (f >> 8 & 255) * 6 + (f & 255)) < 1280;
    static const unsigned light[] = { 0, 0x0030B0, 0x007070, 0xA03000, 0x806000, 0x408040, 0x804080, 0x303030 };
    static const unsigned darkp[] = { 0, 0x80A8FF, 0x60D0D0, 0xF0A070, 0xE0C060, 0x80C080, 0xD090D0, 0xD0D0D0 };
    if (cls <= GUI_TOK_TEXT || cls > GUI_TOK_OP)
        return GUI_FIELD_TEXT;
    return dark ? darkp[cls] : light[cls];
}

/* ---- the bracket at the caret and its partner */
static int partner(struct code *c, size_t at, size_t *out)
{
    static const char open[] = "([{", close[] = ")]}";
    int ch = pt_byte(&c->t, at), depth = 0, steps = 0;
    const char *o = ch > 0 ? strchr(open, ch) : NULL, *cl = ch > 0 ? strchr(close, ch) : NULL;
    size_t p = at;
    if (!o && !cl)
        return 0;
    if (o) {
        char want = close[o - open];
        while (++p < pt_length(&c->t) && steps++ < 200000) {
            int b = pt_byte(&c->t, p);
            if (b == ch)
                depth++;
            else if (b == want && depth-- == 0)
                return *out = p, 1;
        }
    } else {
        char want = open[cl - close];
        while (p-- > 0 && steps++ < 200000) {
            int b = pt_byte(&c->t, p);
            if (b == ch)
                depth++;
            else if (b == want && depth-- == 0)
                return *out = p, 1;
        }
    }
    return 0;
}

static int brace(struct code *c, size_t *a, size_t *b)  /* the bracket at or just before the caret, and its partner */
{
    if (partner(c, c->caret, b))
        return *a = c->caret, 1;
    if (c->caret > 0 && partner(c, c->caret - 1, b))
        return *a = c->caret - 1, 1;
    return 0;
}

/* ---- drawing */
static void sel_range(struct code *c, size_t *a, size_t *b)
{
    *a = c->caret < c->anchor ? c->caret : c->anchor;
    *b = c->caret < c->anchor ? c->anchor : c->caret;
}

static struct cmark *mark_of(struct code *c, int line)
{
    int i;
    for (i = 0; i < c->nmarks; i++)
        if (c->marks[i].line == line)
            return &c->marks[i];
    return NULL;
}

static void draw_mark(struct gfx_surface *s, int x, int y, int h, unsigned flags)
{
    int cy = y + h / 2;
    if (flags & GUI_CODE_BREAK)
        gfx_fill(s, x + 1, cy - 4, 9, 9, 0xC02020);
    if (flags & GUI_CODE_ERROR) {
        gfx_fill(s, x + 2, cy - 3, 7, 7, 0xE03020);
        gfx_fill(s, x + 5, cy - 2, 1, 3, 0xFFFFFF);
        gfx_fill(s, x + 5, cy + 2, 1, 1, 0xFFFFFF);
    } else if (flags & GUI_CODE_WARN) {
        int i;
        for (i = 0; i < 6; i++)
            gfx_fill(s, x + 5 - i, cy - 3 + i, 2 * i + 1, 1, 0xE0B020);
    }
    if (flags & GUI_CODE_BOOK)
        gfx_fill(s, x + 1, cy - 4, 3, 9, 0x2060D0);
}

static void ce_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct code *c = CE(w);
    struct gfx_rect r, clip = s->clip, mr;
    char t[LINE_MAX], num[16];
    unsigned char cls[LINE_MAX];
    int row, on = !(w->flags & GUI_DISABLED), focus = gui_is_focus(w), caret_line, caret_col, mw;
    size_t sa, sb, ba = 0, bb = 0;
    int have_brace;
    place(c);
    r = text_r(c);
    mw = margin_w(c);
    sel_range(c, &sa, &sb);
    where(c, c->caret, &caret_line, &caret_col);
    have_brace = focus && sa == sb && brace(c, &ba, &bb);
    gfx_fill(s, w->r.x, w->r.y, w->r.w, w->r.h, GUI_FIELD);
    gfx_frame(s, w->r.x, w->r.y, w->r.w - GUI_SB + 1, w->r.h - (c->style & GUI_CODE_WRAP ? 0 : GUI_SB - 1), GUI_LINE);
    mr.x = w->r.x + 1, mr.y = r.y, mr.w = mw - 1, mr.h = r.h;
    gfx_fill(s, mr.x, mr.y, mr.w, mr.h, GUI_FACE);
    for (row = 0; row <= rows_fit(c) && c->top + row < nrows(c); row++) {
        int c0, c1, line = row_line(c, c->top + row, &c0, &c1), y = r.y + row * c->ch, n, i, x, col, st;
        int last = c1 == NOEND;                 /* (wrapped: the row shows columns c0 to c1 of its line) */
        size_t ls = line_start(c, line);
        struct cmark *m = mark_of(c, line);
        if (y >= clip.y + clip.h || y + c->ch <= clip.y)
            continue;
        /* the margin: number, mark, fold box (on a line's first row) */
        snprintf(num, sizeof num, "%d", line + 1);
        gfx_clip(s, &mr);
        if (c0 > 0 || c->style & GUI_CODE_PLAIN)
            goto text;
        gfx_text(s, w->r.x + mw - FOLD_W - 4 - gfx_text_width(c->font, num, -1), y, c->font, num, -1,
                 line == caret_line ? GUI_TEXT : GUI_DIM);
        if (m)
            draw_mark(s, w->r.x + 3, y, c->ch, m->flags);
        if (foldable(c, line)) {
            int bx = w->r.x + mw - FOLD_W, by = y + (c->ch - 9) / 2;
            int closed = line < c->froom && c->folded[line];
            gfx_fill(s, bx + 1, by, 9, 9, GUI_FIELD);
            gfx_frame(s, bx + 1, by, 9, 9, GUI_DIM);
            gfx_fill(s, bx + 3, by + 4, 5, 1, GUI_FIELD_TEXT);
            if (closed)
                gfx_fill(s, bx + 5, by + 2, 1, 5, GUI_FIELD_TEXT);
        }
text:
        gfx_clip(s, &clip);
        {
            struct gfx_rect tr = r;
            gfx_rect_cut(&tr, &clip);
            gfx_clip(s, &tr);
        }
        /* the text: colours, the choice, the brackets, the caret */
        n = line_text(c, line, t);
        st = state_at(c, line);
        if (c->lex)
            c->lex(c->lex_ctx, t, n, st, cls);
        else
            memset(cls, 0, (size_t)n);
        for (i = 0, col = 0; i < n;) {
            int k = 1, cols, chosen;
            size_t at = ls + (size_t)i;
            while (i + k < n && ((unsigned char)t[i + k] & 0xC0) == 0x80)
                k++;
            cols = t[i] == '\t' ? (col / c->tabw + 1) * c->tabw - col : 1;
            if (col < c0 || col >= c1) {        /* (another row's) */
                col += cols, i += k;
                continue;
            }
            x = r.x + (col - c0) * c->cw - c->left;
            chosen = c->col_mode ? line >= (c->col_a < c->col_b ? c->col_a : c->col_b)
                                   && line <= (c->col_a < c->col_b ? c->col_b : c->col_a)
                                   && col >= (c->cx_a < c->cx_b ? c->cx_a : c->cx_b)
                                   && col < (c->cx_a < c->cx_b ? c->cx_b : c->cx_a)
                     : at >= sa && at < sb;
            if (chosen)
                gfx_fill(s, x, y, cols * c->cw, c->ch, focus ? GUI_CHOSEN : GUI_SHADE);
            if (have_brace && (at == ba || at == bb))
                gfx_frame(s, x, y, c->cw, c->ch, GUI_MARK);
            if (t[i] != '\t' && t[i] != ' ')
                gfx_text(s, x, y, c->font, t + i, k, chosen && focus ? GUI_CHOSEN_TEXT : on ? tok_rgb(cls[i]) : GUI_DIM);
            col += cols, i += k;
        }
        if (last && !c->col_mode && sa < sb && ls + (size_t)n >= sa && ls + (size_t)n < sb)    /* (its end chosen) */
            gfx_fill(s, r.x + (col - c0) * c->cw - c->left, y, c->cw / 2, c->ch, focus ? GUI_CHOSEN : GUI_SHADE);
        if (col * c->cw > c->widest)
            c->widest = col * c->cw;
        if (last && line < c->froom && c->folded[line] && foldable(c, line)) {    /* what is folded: a mark after it */
            int fx = r.x + (col - c0) * c->cw - c->left + c->cw;
            gfx_frame(s, fx, y + 1, 3 * c->cw, c->ch - 2, GUI_DIM);
            gfx_text(s, fx + 2, y, c->font, "...", 3, GUI_DIM);
        }
        if (focus && line == caret_line && !c->col_mode && caret_col >= c0 && (caret_col < c1 || last))
            gfx_fill(s, r.x + (caret_col - c0) * c->cw - c->left, y, 2, c->ch, GUI_FIELD_TEXT);
        if (focus && c->col_mode && line == c->col_b)
            gfx_fill(s, r.x + c->cx_b * c->cw - c->left, y, 2, c->ch, GUI_FIELD_TEXT);
        gfx_clip(s, &clip);
    }
    gui_sb_draw(s, &c->vsb, on);
    if (!(c->style & GUI_CODE_WRAP)) {
        gui_sb_draw(s, &c->hsb, on);
        gfx_fill(s, c->vsb.r.x, c->hsb.r.y, GUI_SB, GUI_SB, GUI_FACE);
    }
}

/* ---- changes (each a step of the undo) */
struct edit {
    struct code *c;
    size_t at, n;
    int insert;                         /* 1: text was put in at `at`; 0: it was taken away */
    char *text;
    size_t caret0, anchor0;             /* the caret and the choice before it */
};

static void changed(struct code *c, size_t at)
{
    int line = line_of(c, at);
    if (c->state_ok > line + 1)
        c->state_ok = line + 1;
    c->fold_dirty = 1;
    c->wvalid = 0;
    c->modified = 1;
    c->goal = -1;
    gui_damage(&c->w);
    gui_send(&c->w, GUI_ON_CHANGE);
}

static void raw_insert(struct code *c, size_t at, const char *text, size_t n)
{
    int line = line_of(c, at), k, nl = 0;
    size_t i;
    for (i = 0; i < n; i++)
        nl += text[i] == '\n';
    if (pt_insert(&c->t, at, text, n) != 0)
        return;
    if (nl) {                                   /* (marks and folds below move down with their lines) */
        for (k = 0; k < c->nmarks; k++)
            if (c->marks[k].line > line)
                c->marks[k].line += nl;
        if (c->froom > line + 1) {
            int keep = c->froom - (line + 1) - nl;
            if (keep > 0)
                memmove(c->folded + line + 1 + nl, c->folded + line + 1, (size_t)keep);
            memset(c->folded + line + 1, 0, (size_t)(nl < c->froom - line - 1 ? nl : c->froom - line - 1));
        }
    }
    changed(c, at);
}

static void raw_delete(struct code *c, size_t at, size_t n)
{
    int line = line_of(c, at), last = line_of(c, at + n), k, nl = last - line;
    if (pt_delete(&c->t, at, n) != 0)
        return;
    if (nl) {
        for (k = 0; k < c->nmarks; k++)
            if (c->marks[k].line > line)
                c->marks[k].line = c->marks[k].line <= last ? line : c->marks[k].line - nl;
        if (c->froom > last + 1)
            memmove(c->folded + line + 1, c->folded + last + 1, (size_t)(c->froom - last - 1));
    }
    changed(c, at);
}

static void edit_undo(void *data)
{
    struct edit *e = data;
    e->c->busy = 1;
    if (e->insert)
        raw_delete(e->c, e->at, e->n);
    else
        raw_insert(e->c, e->at, e->text, e->n);
    e->c->caret = e->caret0, e->c->anchor = e->anchor0;
    e->c->busy = 0;
    show_caret(e->c);
}

static void edit_redo(void *data)
{
    struct edit *e = data;
    e->c->busy = 1;
    if (e->insert)
        raw_insert(e->c, e->at, e->text, e->n);
    else
        raw_delete(e->c, e->at, e->n);
    e->c->caret = e->c->anchor = e->insert ? e->at + e->n : e->at;
    e->c->busy = 0;
    show_caret(e->c);
}

static void edit_drop(void *data)
{
    free(((struct edit *)data)->text);
    free(data);
}

static int edit_merge(void *into, void *data)  /* typing on, or deleting back: one step */
{
    struct edit *a = into, *b = data;
    char *t;
    if (a->insert != b->insert)
        return 0;
    if (a->insert && a->at + a->n == b->at && (t = realloc(a->text, a->n + b->n)) != NULL) {
        memcpy(t + a->n, b->text, b->n);
        a->text = t, a->n += b->n;
        return 1;
    }
    if (!a->insert && b->at + b->n == a->at && (t = malloc(a->n + b->n)) != NULL) {     /* (Backspace) */
        memcpy(t, b->text, b->n), memcpy(t + b->n, a->text, a->n);
        free(a->text);
        a->text = t, a->at = b->at, a->n += b->n;
        return 1;
    }
    if (!a->insert && b->at == a->at && (t = realloc(a->text, a->n + b->n)) != NULL) {  /* (Delete) */
        memcpy(t + a->n, b->text, b->n);
        a->text = t, a->n += b->n;
        return 1;
    }
    return 0;
}

static void record(struct code *c, int insert, size_t at, const char *text, size_t n, unsigned key, const char *name)
{
    struct edit *e;
    if (c->busy || !c->undo || !(e = calloc(1, sizeof *e)))
        return;
    if (!(e->text = malloc(n ? n : 1))) {
        free(e);
        return;
    }
    e->c = c, e->at = at, e->n = n, e->insert = insert, e->caret0 = c->caret, e->anchor0 = c->anchor;
    memcpy(e->text, text, n);
    gui_undo_push(c->undo, name, edit_undo, edit_redo, e, (unsigned)n + 32, edit_drop, edit_merge, key);
}

static void ins(struct code *c, size_t at, const char *text, size_t n, unsigned key)
{
    if (!n)
        return;
    record(c, 1, at, text, n, key, "Typing");
    raw_insert(c, at, text, n);
}

static void del(struct code *c, size_t at, size_t n, unsigned key)
{
    char *t;
    if (!n || !(t = malloc(n)))
        return;
    pt_read(&c->t, at, t, n);
    record(c, 0, at, t, n, key, "Delete");
    free(t);
    raw_delete(c, at, n);
}

static int del_sel(struct code *c)              /* the choice taken away: 1 if there was one */
{
    size_t a, b;
    if (c->col_mode) {                          /* a column: its part of each line */
        int l0 = c->col_a < c->col_b ? c->col_a : c->col_b, l1 = c->col_a < c->col_b ? c->col_b : c->col_a, l;
        int x0 = c->cx_a < c->cx_b ? c->cx_a : c->cx_b, x1 = c->cx_a < c->cx_b ? c->cx_b : c->cx_a;
        for (l = l1; l >= l0; l--) {
            size_t p = at_of(c, l, x0), q = at_of(c, l, x1);
            if (q > p)
                del(c, p, q - p, 0);
        }
        c->cx_a = c->cx_b = x0;
        return x1 > x0;
    }
    sel_range(c, &a, &b);
    if (a == b)
        return 0;
    del(c, a, b - a, 0);
    c->caret = c->anchor = a;
    return 1;
}

static void type_text(struct code *c, const char *text, size_t n, unsigned key)
{
    if (c->col_mode) {                          /* into every line of the column */
        int l0 = c->col_a < c->col_b ? c->col_a : c->col_b, l1 = c->col_a < c->col_b ? c->col_b : c->col_a, l;
        int x;
        del_sel(c);
        x = c->cx_a;
        for (l = l1; l >= l0; l--)
            ins(c, at_of(c, l, x), text, n, 0);
        c->cx_a = c->cx_b = x + col_of(c, text, (int)n);
        c->caret = c->anchor = at_of(c, c->col_b, c->cx_b);
        gui_damage(&c->w);
        return;
    }
    del_sel(c);
    ins(c, c->caret, text, n, key);
    c->caret = c->anchor = c->caret + n;
    show_caret(c);
}

/* ---- completion and signatures */
static void pop_close(struct code *c)
{
    if (c->pop) {
        gui_close_later(c->pop);
        c->pop = NULL, c->popw = NULL;
    }
}

static void sig_close(struct code *c)
{
    if (c->sig) {
        gui_close_later(c->sig);
        c->sig = NULL;
    }
}

static size_t word_start(struct code *c, size_t at)
{
    while (at > 0 && is_word(pt_byte(&c->t, at - 1)))
        at--;
    return at;
}

static void caret_screen(struct code *c, int *x, int *y)       /* the caret's place on the screen */
{
    int line, col, ox = 0, oy = 0;
    where(c, c->caret, &line, &col);
    if (c->w.win)
        gui_origin(c->w.win, &ox, &oy);
    *x = ox + text_r(c).x + col * c->cw - c->left;
    *y = oy + text_r(c).y + (row_of_line(c, line) - c->top) * c->ch;
}

static void filter(struct code *c)              /* the words that begin as the one at the caret does */
{
    char word[64];
    size_t n = c->caret - c->word_at;
    int i;
    if (n >= sizeof word)
        n = sizeof word - 1;
    pt_read(&c->t, c->word_at, word, n);
    word[n] = 0;
    c->nshown = 0;
    for (i = 0; i < c->nwords && c->nshown < 64; i++)
        if (!strncmp(c->words[i], word, n))
            c->shown[c->nshown++] = i;
    if (c->hot >= c->nshown)
        c->hot = c->nshown - 1;
    if (c->hot < 0)
        c->hot = 0;
}

static void pop_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct code *c = w->user;
    int i;
    gfx_fill(s, w->r.x, w->r.y, w->r.w, w->r.h, GUI_FIELD);
    gfx_frame(s, w->r.x, w->r.y, w->r.w, w->r.h, GUI_LINE);
    if (!c)
        return;
    for (i = 0; i < c->nshown && i < POP_ROWS; i++) {
        int y = w->r.y + 1 + i * GUI_ROW_H;
        if (i == c->hot)
            gfx_fill(s, w->r.x + 1, y, w->r.w - 2, GUI_ROW_H, GUI_CHOSEN);
        gfx_text(s, w->r.x + 4, y + 1, c->font, c->words[c->shown[i]], -1, i == c->hot ? GUI_CHOSEN_TEXT : GUI_FIELD_TEXT);
    }
}

static void take_word(struct code *c)           /* the chosen word in place of the one at the caret */
{
    const char *wd;
    if (!c->nshown)
        return;
    wd = c->words[c->shown[c->hot]];
    c->anchor = c->word_at;
    pop_close(c);
    type_text(c, wd, strlen(wd), 0);
}

static int pop_event(struct gui_widget *w, struct gui_ev *e)
{
    struct code *c = w->user;
    int i;
    if (!c)
        return 1;
    i = (e->y - w->r.y - 1) / GUI_ROW_H;
    if (e->type == GUI_EV_RELEASE && i >= 0 && i < c->nshown) {
        c->hot = i;
        take_word(c);
    }
    return 1;
}

static const struct gui_class pop_class = { "codewords", 0, pop_draw, pop_event, NULL, NULL, NULL };

static void pop_show(struct code *c)
{
    int x, y, i, wmax = 80, h;
    filter(c);
    if (!c->nshown) {
        pop_close(c);
        return;
    }
    for (i = 0; i < c->nshown; i++) {
        int tw = gfx_text_width(c->font, c->words[c->shown[i]], -1) + 12;
        wmax = tw > wmax ? tw : wmax;
    }
    h = (c->nshown < POP_ROWS ? c->nshown : POP_ROWS) * GUI_ROW_H + 2;
    caret_screen(c, &x, &y);
    y += c->ch;
    if (!c->pop) {
        c->pop = gui_window_at("", x, y, wmax, h, GUI_W_POPUP | GUI_W_BARE);
        c->pop->owner = c->w.win;
        c->popw = gui_new(c->pop->root, &pop_class, sizeof *c->popw);
        gui_grow(c->popw, 1);
        c->popw->user = c;
        gui_show_nofocus(c->pop, x, y);
    } else {
        gui_resize(c->pop, wmax, h);
        gui_damage(c->popw);
    }
}

static void sig_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct code *c = w->user;
    gfx_fill(s, w->r.x, w->r.y, w->r.w, w->r.h, GUI_FACE);
    gfx_frame(s, w->r.x, w->r.y, w->r.w, w->r.h, GUI_LINE);
    if (c)
        gfx_text(s, w->r.x + 4, w->r.y + 2, c->font, c->sigtext, -1, GUI_TEXT);
}

static const struct gui_class sig_class = { "codesig", 0, sig_draw, NULL, NULL, NULL, NULL };

/* ---- the mouse and the keys */
static void at_point(struct code *c, int ex, int ey, int *line, int *col)
{
    struct gfx_rect r = text_r(c);
    int row = (ey - r.y) / c->ch, c0, c1;
    if (ey < r.y)
        row = -1;
    *line = row_line(c, c->top + (row < 0 ? 0 : row), &c0, &c1);
    *col = c0 + (ex - r.x + c->left + c->cw / 2) / c->cw;
    *col = *col < c0 ? c0 : c1 != NOEND && *col >= c1 ? c1 - 1 : *col;
}

static void move_to(struct code *c, size_t at, int extend)
{
    c->col_mode = 0;
    c->caret = at;
    if (!extend)
        c->anchor = at;
    sig_close(c);
    show_caret(c);
    gui_send(&c->w, GUI_ON_SELECT);
}

static void tab_lines(struct code *c, int out)  /* the chosen lines indented by a stop (or taken back by one) */
{
    size_t a, b;
    int l0, l1, l;
    sel_range(c, &a, &b);
    l0 = line_of(c, a), l1 = line_of(c, b > a && b == line_start(c, line_of(c, b)) ? b - 1 : b);
    for (l = l1; l >= l0; l--) {
        size_t s = line_start(c, l);
        if (!out) {
            char sp[16];
            memset(sp, ' ', (size_t)c->tabw);
            if (line_end(c, l) > s)
                ins(c, s, sp, (size_t)c->tabw, 0);
        } else {
            int k = 0;
            while (k < c->tabw && pt_byte(&c->t, s + (size_t)k) == ' ')
                k++;
            if (!k && pt_byte(&c->t, s) == '\t')
                k = 1;
            if (k)
                del(c, s, (size_t)k, 0);
        }
    }
    c->anchor = line_start(c, l0), c->caret = l1 + 1 < nlines(c) ? line_start(c, l1 + 1) : pt_length(&c->t);
    show_caret(c);
}

static int ce_key(struct code *c, struct gui_ev *e)
{
    int shift = (e->mods & GLOS_MOD_SHIFT) != 0, ctrl = (e->mods & GLOS_MOD_CTRL) != 0, alt = (e->mods & GLOS_MOD_ALT) != 0;
    int line, col;
    size_t a, b;
    where(c, c->caret, &line, &col);
    if (c->pop) {                               /* the words: theirs first */
        switch (e->key) {
        case GUI_K_UP: c->hot = c->hot > 0 ? c->hot - 1 : 0; gui_damage(c->popw); return 1;
        case GUI_K_DOWN: c->hot = c->hot + 1 < c->nshown ? c->hot + 1 : c->hot; gui_damage(c->popw); return 1;
        case GUI_K_ENTER: case GUI_K_KPENTER: case GUI_K_TAB: take_word(c); return 1;
        case GUI_K_ESC: pop_close(c); return 1;
        }
    }
    if (e->key == GUI_K_ESC && c->sig) {
        sig_close(c);
        return 1;
    }
    if (alt && shift && !ctrl && (e->key == GUI_K_UP || e->key == GUI_K_DOWN || e->key == GUI_K_LEFT || e->key == GUI_K_RIGHT)) {
        if (!c->col_mode)                       /* a column, by the keys */
            c->col_mode = 1, c->col_a = c->col_b = line, c->cx_a = c->cx_b = col;
        if (e->key == GUI_K_UP && c->col_b > 0)
            c->col_b--;
        if (e->key == GUI_K_DOWN && c->col_b + 1 < nlines(c))
            c->col_b++;
        if (e->key == GUI_K_LEFT && c->cx_b > 0)
            c->cx_b--;
        if (e->key == GUI_K_RIGHT)
            c->cx_b++;
        c->caret = c->anchor = at_of(c, c->col_b, c->cx_b);
        gui_damage(&c->w);
        return 1;
    }
    if (alt)
        return 0;
    if (ctrl) {
        switch (e->ch) {
        case 1: c->col_mode = 0, c->anchor = 0, c->caret = pt_length(&c->t); gui_damage(&c->w); return 1;     /* A */
        case 3: case 24: {                                                                    /* C, X */
            char *t = gui_code_selection(&c->w);
            if (t && t[0]) {
                gui_clip_set(t, strlen(t));
                if (e->ch == 24)
                    del_sel(c), show_caret(c);
            }
            free(t);
            return 1;
        }
        case 22: {                                                                            /* V */
            size_t n;
            char *t = gui_clip_get(&n);
            if (t) {
                type_text(c, t, n, 0);
                free(t);
            }
            return 1;
        }
        case 26: if (c->undo) gui_undo(c->undo); return 1;                                     /* Z */
        case 25: if (c->undo) gui_redo(c->undo); return 1;                                     /* Y */
        }
        if (e->key == GUI_K_SPACE) {            /* words for the one at the caret: the app's to give */
            char word[64];
            size_t n;
            c->word_at = word_start(c, c->caret);
            n = c->caret - c->word_at < sizeof word - 1 ? c->caret - c->word_at : sizeof word - 1;
            pt_read(&c->t, c->word_at, word, n);
            word[n] = 0;
            if (c->on_complete)
                c->on_complete(&c->w, word);
            return 1;
        }
        if (e->ch == '+' || e->ch == '=' || e->ch == '-' || e->key == 0x0D || e->key == 0x0C || e->key == 0x4E
            || e->key == 0x4A) {                /* (by the key too: with Ctrl there may be no character) */
            gui_code_zoom(&c->w, c->zoom + (e->ch == '-' || e->key == 0x0C || e->key == 0x4A ? -1 : 1));
            return 1;
        }
    }
    switch (e->key) {
    case GUI_K_LEFT:
        if (!shift && c->caret != c->anchor) {
            sel_range(c, &a, &b);
            move_to(c, a, 0);
        } else if (ctrl) {
            size_t p = c->caret;
            while (p > 0 && !is_word(pt_byte(&c->t, p - 1)))
                p--;
            move_to(c, word_start(c, p), shift);
        } else if (c->caret > 0) {
            size_t p = c->caret - 1;
            while (p > 0 && (pt_byte(&c->t, p) & 0xC0) == 0x80)
                p--;
            if (p > 0 && pt_byte(&c->t, p) == '\n' && pt_byte(&c->t, p - 1) == '\r')
                p--;
            move_to(c, p, shift);
        }
        return 1;
    case GUI_K_RIGHT:
        if (!shift && c->caret != c->anchor) {
            sel_range(c, &a, &b);
            move_to(c, b, 0);
        } else if (ctrl) {
            size_t p = c->caret, n = pt_length(&c->t);
            while (p < n && is_word(pt_byte(&c->t, p)))
                p++;
            while (p < n && !is_word(pt_byte(&c->t, p)))
                p++;
            move_to(c, p, shift);
        } else if (c->caret < pt_length(&c->t)) {
            size_t p = c->caret + 1;
            if (pt_byte(&c->t, c->caret) == '\r' && pt_byte(&c->t, p) == '\n')
                p++;
            while (p < pt_length(&c->t) && (pt_byte(&c->t, p) & 0xC0) == 0x80)
                p++;
            move_to(c, p, shift);
        }
        return 1;
    case GUI_K_UP:
    case GUI_K_DOWN:
    case GUI_K_PGUP:
    case GUI_K_PGDN: {
        int row = row_of(c, line, col), r0, r1, want, goal, l2;
        row_line(c, row, &r0, &r1);
        want = c->goal >= 0 ? c->goal : col - r0;     /* (the column within its row) */
        int step = e->key == GUI_K_UP ? -1 : e->key == GUI_K_DOWN ? 1 : e->key == GUI_K_PGUP ? -(rows_fit(c) - 1)
                   : rows_fit(c) - 1;
        if (ctrl && (e->key == GUI_K_UP || e->key == GUI_K_DOWN)) {    /* (the view, not the caret) */
            c->top += step;
            place(c);
            gui_damage(&c->w);
            return 1;
        }
        row += step;
        row = row < 0 ? 0 : row >= nrows(c) ? nrows(c) - 1 : row;
        if (e->key == GUI_K_PGUP || e->key == GUI_K_PGDN)
            c->top += step;
        goal = want;
        l2 = row_line(c, row, &r0, &r1);
        want = r0 + want;
        if (r1 != NOEND && want >= r1)
            want = r1 - 1;
        move_to(c, at_of(c, l2, want), shift);
        c->goal = goal;
        return 1;
    }
    case GUI_K_HOME:
        if (ctrl)
            move_to(c, 0, shift);
        else {
            int in = indent_of(c, line);
            size_t first = at_of(c, line, in < 0 ? 0 : in);
            move_to(c, c->caret == first ? line_start(c, line) : first, shift);
        }
        return 1;
    case GUI_K_END:
        move_to(c, ctrl ? pt_length(&c->t) : line_end(c, line), shift);
        return 1;
    case GUI_K_BACK:
        if (!del_sel(c) && c->caret > 0) {
            size_t p = c->caret - 1;
            while (p > 0 && (pt_byte(&c->t, p) & 0xC0) == 0x80)
                p--;
            if (p > 0 && pt_byte(&c->t, p) == '\n' && pt_byte(&c->t, p - 1) == '\r')
                p--;
            del(c, p, c->caret - p, 2);
            c->caret = c->anchor = p;
        }
        if (c->pop) {
            if (c->caret <= c->word_at)
                pop_close(c);
            else
                pop_show(c);
        }
        show_caret(c);
        return 1;
    case GUI_K_DEL:
        if (!del_sel(c) && c->caret < pt_length(&c->t)) {
            size_t p = c->caret + 1;
            if (pt_byte(&c->t, c->caret) == '\r' && pt_byte(&c->t, p) == '\n')
                p++;
            while (p < pt_length(&c->t) && (pt_byte(&c->t, p) & 0xC0) == 0x80)
                p++;
            del(c, c->caret, p - c->caret, 3);
        }
        show_caret(c);
        return 1;
    case GUI_K_ENTER:
    case GUI_K_KPENTER: {                       /* a new line (CR LF if the text's are), as indented as this one */
        char t[LINE_MAX + 3];
        int n = line_text(c, line, t), i = 0, e = c->crlf ? 2 : 1;
        while (i < n && (t[i] == ' ' || t[i] == '\t') && line_start(c, line) + (size_t)i < c->caret)
            i++;
        memmove(t + e, t, (size_t)i);
        if (c->crlf)
            t[0] = '\r', t[1] = '\n';
        else
            t[0] = '\n';
        type_text(c, t, (size_t)(i + e), 0);
        return 1;
    }
    case GUI_K_TAB:
        if (ctrl)                               /* (Ctrl+Tab is the window's: round its tabs, of which this may be one) */
            return 0;
        sel_range(c, &a, &b);
        if (a != b && line_of(c, a) != line_of(c, b)) {
            tab_lines(c, shift);
            return 1;
        }
        if (shift) {
            tab_lines(c, 1);
            return 1;
        }
        {
            char sp[16];
            int k = c->tabw - col % c->tabw;
            memset(sp, ' ', (size_t)k);
            type_text(c, sp, (size_t)k, 1);
        }
        return 1;
    }
    if (e->ch >= 0x20 && e->ch != 0x7F && !ctrl) {
        char u[4];
        int n;
        unsigned ch = e->ch;
        if (ch < 0x80)
            u[0] = (char)ch, n = 1;
        else if (ch < 0x800)
            u[0] = (char)(0xC0 | ch >> 6), u[1] = (char)(0x80 | (ch & 0x3F)), n = 2;
        else
            u[0] = (char)(0xE0 | ch >> 12), u[1] = (char)(0x80 | (ch >> 6 & 0x3F)), u[2] = (char)(0x80 | (ch & 0x3F)), n = 3;
        type_text(c, u, (size_t)n, 1);
        if (c->pop) {                           /* (the words narrowed as the word grows; gone at its end) */
            if (is_word((unsigned char)u[0]))
                pop_show(c);
            else
                pop_close(c);
        }
        if (ch == ')' || ch == ';')
            sig_close(c);
        return 1;
    }
    return 0;
}

static int ce_event(struct gui_widget *w, struct gui_ev *e)
{
    struct code *c = CE(w);
    int line, col;
    place(c);
    if (e->type != GUI_EV_KEY && (gui_sb_event(w, &c->vsb, e, 1)
                                  || (!(c->style & GUI_CODE_WRAP) && gui_sb_event(w, &c->hsb, e, c->cw * 4)))) {
        c->top = c->vsb.pos, c->left = c->hsb.pos;
        gui_damage(w);
        return 1;
    }
    switch (e->type) {
    case GUI_EV_KEY:
        return ce_key(c, e);
    case GUI_EV_PRESS:
        if (e->button != 1)
            return 0;
        gui_focus(w);
        pop_close(c);
        at_point(c, e->x, e->y, &line, &col);
        if (e->x < text_r(c).x) {               /* the margin: a fold box, else the line chosen */
            if (e->x >= w->r.x + margin_w(c) - FOLD_W && foldable(c, line))
                fold_set(c, line, !(line < c->froom && c->folded[line]));
            else
                c->col_mode = 0, c->anchor = line_start(c, line),
                c->caret = line + 1 < nlines(c) ? line_start(c, line + 1) : pt_length(&c->t);
            gui_damage(w);
            return 1;
        }
        if (e->mods & GLOS_MOD_ALT) {           /* a column */
            c->col_mode = 1, c->col_a = c->col_b = line, c->cx_a = c->cx_b = col;
            c->caret = c->anchor = at_of(c, line, col);
            c->drag = 2;
            gui_damage(w);
            return 1;
        }
        c->col_mode = 0;
        if (e->clicks == 2) {                   /* a word */
            size_t at = at_of(c, line, col), s = word_start(c, at), n = pt_length(&c->t);
            while (at < n && is_word(pt_byte(&c->t, at)))
                at++;
            c->anchor = s, c->caret = at;
            gui_damage(w);
            return 1;
        }
        c->caret = at_of(c, line, col);
        if (!(e->mods & GLOS_MOD_SHIFT))
            c->anchor = c->caret;
        c->drag = 1, c->goal = -1;
        sig_close(c);
        gui_damage(w);
        gui_send(w, GUI_ON_SELECT);
        return 1;
    case GUI_EV_MOVE:
        if (c->drag) {
            at_point(c, e->x, e->y, &line, &col);
            if (c->drag == 2)
                c->col_b = line, c->cx_b = col;
            c->caret = at_of(c, line, col);
            if (c->drag == 2)
                c->anchor = c->caret;
            if (e->y < text_r(c).y && c->top > 0)
                c->top--;
            else if (e->y >= text_r(c).y + text_r(c).h)
                c->top++;
            gui_damage(w);
        } else if (e->x < text_r(c).x) {        /* over the margin: a mark's note */
            struct cmark *m;
            at_point(c, e->x, e->y, &line, &col);
            m = mark_of(c, line);
            gui_tip_at(w, line + 1, m && m->note ? m->note : NULL);
        } else {
            gui_tip_at(w, 0, NULL);
        }
        return 1;
    case GUI_EV_RELEASE:
        c->drag = 0;
        return 1;
    case GUI_EV_WHEEL:
        if (e->mods & GLOS_MOD_CTRL) {
            gui_code_zoom(w, c->zoom + (e->wheel > 0 ? 1 : -1));
            return 1;
        }
        c->top -= e->wheel * 3;
        place(c);
        gui_damage(w);
        return 1;
    case GUI_EV_FOCUS:
        gui_damage(w);
        return 1;
    case GUI_EV_BLUR:
        pop_close(c);
        sig_close(c);
        gui_damage(w);
        return 1;
    }
    return 0;
}

static void ce_measure(struct gui_widget *w, int *pw, int *ph) { (void)w, *pw = 400, *ph = 240; }

static void ce_destroy(struct gui_widget *w)
{
    struct code *c = CE(w);
    int i;
    pop_close(c);
    sig_close(c);
    gui_undo_free(c->undo);
    pt_free(&c->t);
    for (i = 0; i < c->nmarks; i++)
        free(c->marks[i].note);
    for (i = 0; i < c->nwords; i++)
        free(c->words[i]);
    free(c->words), free(c->marks), free(c->state), free(c->folded), free(c->folds);
}

static const struct gui_class ce_class = { "code", GUI_C_FOCUS, ce_draw, ce_event, ce_measure, NULL, ce_destroy };

/* ---- the app's side */
struct gui_widget *gui_code(struct gui_widget *parent)
{
    struct code *c = (struct code *)gui_new(parent, &ce_class, sizeof *c);
    c->zoom = 1, c->tabw = 4, c->goal = -1;
    c->w.grow = 1;                              /* (as the editor did: it takes the room it is given) */
    pt_init(&c->t, NULL, 0);
    c->undo = gui_undo_new(1024u * 1024);
    metrics(c);
    return &c->w;
}

int gui_code_set(struct gui_widget *w, char *text, size_t n)
{
    struct code *c = CE(w);
    int i;
    pt_free(&c->t);
    if (pt_init(&c->t, text, n) != 0)
        return -1;
    c->caret = c->anchor = 0, c->top = c->left = 0, c->col_mode = 0, c->goal = -1;
    c->state_ok = 0, c->modified = 0, c->widest = 0, c->wvalid = 0;
    {
        const char *nl = text && n ? memchr(text, '\n', n) : NULL;
        c->crlf = nl && nl > text && nl[-1] == '\r';
    }
    if (c->folded)
        memset(c->folded, 0, (size_t)c->froom);
    c->fold_dirty = 1;
    for (i = 0; i < c->nmarks; i++)
        free(c->marks[i].note);
    c->nmarks = 0;
    if (c->undo)
        gui_undo_clear(c->undo);
    gui_damage(w);
    return 0;
}

char *gui_code_text(struct gui_widget *w, size_t *n)
{
    struct code *c = CE(w);
    if (n)
        *n = pt_length(&c->t);
    return pt_text(&c->t);
}

char *gui_code_selection(struct gui_widget *w)  /* what is chosen (a column: its lines' parts, a line each) */
{
    struct code *c = CE(w);
    size_t a, b;
    char *t;
    if (c->col_mode) {
        int l0 = c->col_a < c->col_b ? c->col_a : c->col_b, l1 = c->col_a < c->col_b ? c->col_b : c->col_a, l;
        int x0 = c->cx_a < c->cx_b ? c->cx_a : c->cx_b, x1 = c->cx_a < c->cx_b ? c->cx_b : c->cx_a;
        size_t room = (size_t)(l1 - l0 + 1) * (size_t)(x1 - x0 + 2) * 3 + 1, n = 0;
        if (!(t = malloc(room)))
            return NULL;
        for (l = l0; l <= l1; l++) {
            size_t p = at_of(c, l, x0), q = at_of(c, l, x1);
            if (q > p && n + (q - p) + 2 < room)
                n += pt_read(&c->t, p, t + n, q - p);
            if (l < l1)
                t[n++] = '\r', t[n++] = '\n';
        }
        t[n] = 0;
        return t;
    }
    sel_range(c, &a, &b);
    if (!(t = malloc(b - a + 1)))
        return NULL;
    pt_read(&c->t, a, t, b - a);
    t[b - a] = 0;
    return t;
}

size_t gui_code_length(struct gui_widget *w) { return pt_length(&CE(w)->t); }

int gui_code_modified(struct gui_widget *w) { return CE(w)->modified; }

void gui_code_saved(struct gui_widget *w) { CE(w)->modified = 0; }

void gui_code_where(struct gui_widget *w, int *line, int *col)
{
    where(CE(w), CE(w)->caret, line, col);
    (*line)++, (*col)++;
}

void gui_code_goto(struct gui_widget *w, int line, int col)    /* (from 1) */
{
    struct code *c = CE(w);
    int k;
    line = line < 1 ? 0 : line - 1;
    folds_now(c);
    for (k = 0; k < c->nfolds; k++)             /* (a folded line: its fold opened) */
        if (line > c->folds[k].head && line <= c->folds[k].end)
            fold_set(c, c->folds[k].head, 0);
    move_to(c, at_of(c, line, col < 1 ? 0 : col - 1), 0);
}

void gui_code_select(struct gui_widget *w, size_t from, size_t to)
{
    struct code *c = CE(w);
    c->col_mode = 0;
    c->anchor = from > pt_length(&c->t) ? pt_length(&c->t) : from;
    c->caret = to > pt_length(&c->t) ? pt_length(&c->t) : to;
    show_caret(c);
}

void gui_code_lexer(struct gui_widget *w, gui_lexer lex, void *ctx)
{
    CE(w)->lex = lex, CE(w)->lex_ctx = ctx, CE(w)->state_ok = 0;
    gui_damage(w);
}

void gui_code_mark(struct gui_widget *w, int line, unsigned flags, const char *note)   /* (line from 1; flags 0: none) */
{
    struct code *c = CE(w);
    struct cmark *m = mark_of(c, line - 1);
    if (!m && flags) {
        struct cmark *nm = realloc(c->marks, (size_t)(c->nmarks + 1) * sizeof *nm);
        if (!nm)
            return;
        c->marks = nm;
        m = &c->marks[c->nmarks++];
        memset(m, 0, sizeof *m);
        m->line = line - 1;
    }
    if (!m)
        return;
    free(m->note);
    m->note = note ? strdup(note) : NULL;
    m->flags = flags;
    if (!flags) {                               /* (no marks left: it goes) */
        *m = c->marks[--c->nmarks];
    }
    gui_damage(w);
}

const char *gui_code_note(struct gui_widget *w, int line)
{
    struct cmark *m = mark_of(CE(w), line - 1);
    return m ? m->note : NULL;
}

void gui_code_fold(struct gui_widget *w, int line, int folded)  /* (from 1) */
{
    if (foldable(CE(w), line - 1))
        fold_set(CE(w), line - 1, folded);
}

void gui_code_zoom(struct gui_widget *w, int size)
{
    struct code *c = CE(w);
    size = size < 0 ? 0 : size > 2 ? 2 : size;
    if (size == c->zoom || !gui_font_get(GUI_FONT_MONO, size))
        return;
    c->zoom = size;
    c->widest = 0;
    show_caret(c);
    gui_send(w, GUI_ON_CHANGE);
}

int gui_code_get_zoom(struct gui_widget *w) { return CE(w)->zoom; }

void gui_code_on_complete(struct gui_widget *w, void (*fn)(struct gui_widget *w, const char *word))
{
    CE(w)->on_complete = fn;
}

void gui_code_complete(struct gui_widget *w, const char *const *words, int n)
{
    struct code *c = CE(w);
    int i;
    for (i = 0; i < c->nwords; i++)
        free(c->words[i]);
    free(c->words);
    c->words = n > 0 ? malloc((size_t)n * sizeof *c->words) : NULL;
    c->nwords = 0, c->hot = 0;
    for (i = 0; c->words && i < n; i++)
        if ((c->words[c->nwords] = strdup(words[i])) != NULL)
            c->nwords++;
    c->word_at = word_start(c, c->caret);
    pop_show(c);
}

void gui_code_signature(struct gui_widget *w, const char *text)
{
    struct code *c = CE(w);
    int x, y, wd;
    struct gui_widget *s;
    sig_close(c);
    if (!text || !text[0])
        return;
    snprintf(c->sigtext, sizeof c->sigtext, "%s", text);
    caret_screen(c, &x, &y);
    wd = gfx_text_width(c->font, c->sigtext, -1) + 10;
    y -= c->font->height + 6;
    c->sig = gui_window_at("", x, y, wd, c->font->height + 5, GUI_W_POPUP | GUI_W_BARE);
    c->sig->owner = c->w.win;
    s = gui_new(c->sig->root, &sig_class, sizeof *s);
    gui_grow(s, 1);
    s->user = c;
    gui_show_nofocus(c->sig, x, y);
}

/* ---- the toolkit's lexer for C (and the C-like): keywords, types, strings, characters, numbers, comments (a block
   comment's state carried to the next line), the preprocessor's lines, operators */
int gui_code_lex_c(void *ctx, const char *s, int n, int state, unsigned char *cls)
{
    static const char *const kw[] = { "if", "else", "for", "while", "do", "return", "switch", "case", "break",
        "continue", "goto", "default", "sizeof", "typedef", "struct", "union", "enum", "static", "extern", "const",
        "volatile", "register", "inline", "auto", NULL };
    static const char *const ty[] = { "void", "char", "short", "int", "long", "float", "double", "signed", "unsigned",
        "size_t", "u8", "u16", "u32", "s32", "uint8_t", "uint16_t", "uint32_t", "int32_t", NULL };
    int i = 0, k;
    (void)ctx;
    if (state == 1) {                           /* inside a block comment */
        while (i < n && !(s[i] == '*' && i + 1 < n && s[i + 1] == '/'))
            cls[i++] = GUI_TOK_COMMENT;
        if (i >= n)
            return 1;
        cls[i] = cls[i + 1] = GUI_TOK_COMMENT;
        i += 2;
    }
    {
        int j = i;
        while (j < n && (s[j] == ' ' || s[j] == '\t'))
            cls[j++] = GUI_TOK_TEXT;
        if (j < n && s[j] == '#' && state == 0) {       /* a preprocessor line: as such to a comment */
            while (j < n && !(s[j] == '/' && j + 1 < n && (s[j + 1] == '/' || s[j + 1] == '*')))
                cls[j++] = GUI_TOK_PREPROC;
            i = j;
        }
    }
    while (i < n) {
        char ch = s[i];
        if (ch == '/' && i + 1 < n && s[i + 1] == '/') {
            while (i < n)
                cls[i++] = GUI_TOK_COMMENT;
            return 0;
        }
        if (ch == '/' && i + 1 < n && s[i + 1] == '*') {
            cls[i] = cls[i + 1] = GUI_TOK_COMMENT;
            i += 2;
            while (i < n && !(s[i] == '*' && i + 1 < n && s[i + 1] == '/'))
                cls[i++] = GUI_TOK_COMMENT;
            if (i >= n)
                return 1;
            cls[i] = cls[i + 1] = GUI_TOK_COMMENT;
            i += 2;
            continue;
        }
        if (ch == '"' || ch == '\'') {
            cls[i++] = GUI_TOK_STRING;
            while (i < n && s[i] != ch) {
                if (s[i] == '\\' && i + 1 < n)
                    cls[i++] = GUI_TOK_STRING;
                cls[i++] = GUI_TOK_STRING;
            }
            if (i < n)
                cls[i++] = GUI_TOK_STRING;
            continue;
        }
        if (ch >= '0' && ch <= '9') {
            while (i < n && (is_word((unsigned char)s[i]) || s[i] == '.'))
                cls[i++] = GUI_TOK_NUMBER;
            continue;
        }
        if (is_word((unsigned char)ch)) {
            int j = i, kind = GUI_TOK_TEXT;
            while (j < n && is_word((unsigned char)s[j]))
                j++;
            for (k = 0; kw[k]; k++)
                if ((int)strlen(kw[k]) == j - i && !strncmp(kw[k], s + i, (size_t)(j - i)))
                    kind = GUI_TOK_KEYWORD;
            for (k = 0; ty[k]; k++)
                if ((int)strlen(ty[k]) == j - i && !strncmp(ty[k], s + i, (size_t)(j - i)))
                    kind = GUI_TOK_TYPE;
            while (i < j)
                cls[i++] = (unsigned char)kind;
            continue;
        }
        cls[i++] = strchr("+-*/%=<>!&|^~?:;,.()[]{}", ch) ? GUI_TOK_OP : GUI_TOK_TEXT;
    }
    return 0;
}

/* ---- (C10) what Notepad has of it: wrapping, a plain look, finding, replacing, the Edit menu's commands */
void gui_code_tabs(struct gui_widget *w, int width)    /* a tab's stops (4 unless said; Notepad's 8) */
{
    CE(w)->tabw = width > 0 && width <= 16 ? width : 4;
    CE(w)->wvalid = 0;
    gui_damage(w);
}

void gui_code_style(struct gui_widget *w, unsigned style)
{
    struct code *c = CE(w);
    c->style = style;
    c->wvalid = 0, c->left = 0;
    show_caret(c);
}

int gui_code_find(struct gui_widget *w, const char *what, int match_case, int forward)    /* found and chosen */
{
    struct code *c = CE(w);
    size_t n = strlen(what), len = pt_length(&c->t), a, b, i;
    char *t;
    if (!n || n > len || !(t = pt_text(&c->t)))
        return 0;
    sel_range(c, &a, &b);
    for (i = forward ? b : (a ? a - 1 : (size_t)-1); i != (size_t)-1 && i + n <= len; i = forward ? i + 1 : i - 1) {
        size_t k;
        for (k = 0; k < n; k++) {
            int x = (unsigned char)t[i + k], y = (unsigned char)what[k];
            if (!match_case && x >= 'A' && x <= 'Z')
                x += 32;
            if (!match_case && y >= 'A' && y <= 'Z')
                y += 32;
            if (x != y)
                break;
        }
        if (k == n) {
            free(t);
            c->col_mode = 0;
            c->anchor = i, c->caret = i + n;
            show_caret(c);
            gui_send(w, GUI_ON_SELECT);
            return 1;
        }
        if (!forward && i == 0)
            break;
    }
    free(t);
    return 0;
}

void gui_code_replace_sel(struct gui_widget *w, const char *text) { type_text(CE(w), text, strlen(text), 0); }

int gui_code_command(struct gui_widget *w, int cmd)    /* GUI_CMD_: 1 done */
{
    struct code *c = CE(w);
    struct gui_ev e;
    memset(&e, 0, sizeof e);
    e.type = GUI_EV_KEY, e.mods = GLOS_MOD_CTRL;
    switch (cmd) {
    case GUI_CMD_CUT: e.ch = 24; break;
    case GUI_CMD_COPY: e.ch = 3; break;
    case GUI_CMD_PASTE: e.ch = 22; break;
    case GUI_CMD_ALL: e.ch = 1; break;
    case GUI_CMD_UNDO: e.ch = 26; break;
    case GUI_CMD_DELETE:
        if (!del_sel(c) && c->caret < pt_length(&c->t))
            del(c, c->caret, 1, 0);
        show_caret(c);
        return 1;
    case GUI_CMD_END:
        move_to(c, pt_length(&c->t), 0);
        c->top = nrows(c) - rows_fit(c) > 0 ? nrows(c) - rows_fit(c) : 0;
        return 1;
    default:
        return 0;
    }
    return ce_key(c, &e);
}
