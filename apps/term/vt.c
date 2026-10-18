/* The terminal's xterm screen (vt.h; M10 step D). What it takes is what
 * vi, nano, top, less and bash's readline send to TERM=xterm: ECMA-48's
 * cursor movement, erasing, insertion and deletion, scroll regions, SGR
 * (16, 256 and direct colours), DEC's private modes (application cursor
 * keys, the alternate screen 47/1047/1049, autowrap, origin, the cursor
 * shown), DECSC/DECRC, the line-drawing set, tab stops, the title (OSC 0
 * and 2), and the status reports (DSR, DA). xterm's own documentation
 * (ctlseqs) is the reference; what isn't here is read and ignored. */
#include <stdio.h>
#include <string.h>

#include "vt.h"

#define MOD_SHIFT 1                     /* (glos/api.h's GLOS_MOD_: vt.c builds on the host without it) */
#define MOD_CTRL  2
#define MOD_ALT   4

enum { S_GROUND, S_ESC, S_ESC_INTER, S_CSI, S_OSC, S_OSC_ESC, S_STR, S_STR_ESC, S_CHARSET };

static struct vt_cell blank(const struct vt *t)
{
    struct vt_cell c;
    c.ch = ' ';
    c.fg = t->fg;
    c.bg = t->bg;                               /* (erasing fills with the background in force, as xterm does) */
    c.attr = 0;
    return c;
}

static struct vt_cell *at(struct vt *t, int x, int y) { return &t->cell[y * VT_COLS_MAX + x]; }

static void dirty(struct vt *t, int y0, int y1)
{
    int y;
    for (y = y0 < 0 ? 0 : y0; y <= y1 && y < t->rows; y++)
        t->dirty[y] = 1;
}

static void clear_cells(struct vt *t, int x0, int y0, int x1, int y1)   /* (inclusive, row by row) */
{
    struct vt_cell b = blank(t);
    int y, x;
    for (y = y0; y <= y1; y++)
        for (x = y == y0 ? x0 : 0; x <= (y == y1 ? x1 : t->cols - 1); x++)
            *at(t, x, y) = b;
    dirty(t, y0, y1);
}

static void reset_tabs(struct vt *t)
{
    int x;
    for (x = 0; x < VT_COLS_MAX; x++)
        t->tabs[x] = x && x % 8 == 0;
}

static void soft_reset(struct vt *t)
{
    t->fg = t->bg = VT_DEFAULT;
    t->attr = 0;
    t->top = 0;
    t->bottom = t->rows - 1;
    t->origin = t->insert = t->app_cursor = t->app_keypad = t->bracketed = t->reverse_screen = t->lnm = 0;
    t->autowrap = t->cursor_on = 1;
    t->g0_lines = t->g1_lines = t->shift_out = 0;
    t->wrap_next = 0;
}

void vt_init(struct vt *t, int cols, int rows)
{
    memset(t, 0, sizeof *t);
    t->cols = cols < 1 ? 1 : cols > VT_COLS_MAX ? VT_COLS_MAX : cols;
    t->rows = rows < 1 ? 1 : rows > VT_ROWS_MAX ? VT_ROWS_MAX : rows;
    t->cell = t->main;
    soft_reset(t);
    reset_tabs(t);
    t->cell = t->alt;
    clear_cells(t, 0, 0, t->cols - 1, t->rows - 1);
    t->cell = t->main;
    clear_cells(t, 0, 0, t->cols - 1, t->rows - 1);
}

void vt_resize(struct vt *t, int cols, int rows)
{
    int y, x, oc = t->cols, orows = t->rows, s;
    cols = cols < 1 ? 1 : cols > VT_COLS_MAX ? VT_COLS_MAX : cols;
    rows = rows < 1 ? 1 : rows > VT_ROWS_MAX ? VT_ROWS_MAX : rows;
    if (rows < orows && t->y >= rows) {         /* (shorter: the lines above the cursor go, as xterm's do) */
        int up = t->y - rows + 1;
        for (s = 0; s < 2; s++) {
            struct vt_cell *c = s ? t->alt : t->main;
            memmove(c, c + up * VT_COLS_MAX, (size_t)(VT_ROWS_MAX - up) * VT_COLS_MAX * sizeof *c);
        }
        t->y -= up;
    }
    t->cols = cols;
    t->rows = rows;
    for (s = 0; s < 2; s++) {                   /* the new cells blank */
        struct vt_cell *keep = t->cell;
        t->cell = s ? t->alt : t->main;
        for (y = 0; y < rows; y++)
            for (x = y < orows ? oc : 0; x < cols; x++)
                *at(t, x, y) = blank(t);
        t->cell = keep;
    }
    t->top = 0;
    t->bottom = rows - 1;
    if (t->x >= cols)
        t->x = cols - 1;
    if (t->y >= rows)
        t->y = rows - 1;
    t->wrap_next = 0;
    dirty(t, 0, rows - 1);
}

/* ---- scrolling */

static void scroll_up(struct vt *t, int top, int bottom, int n)
{
    int y;
    if (n > bottom - top + 1)
        n = bottom - top + 1;
    if (n <= 0)
        return;
    if (top == 0 && !t->on_alt)
        t->scrolled += n;
    for (y = top; y + n <= bottom; y++)
        memcpy(at(t, 0, y), at(t, 0, y + n), (size_t)t->cols * sizeof(struct vt_cell));
    clear_cells(t, 0, bottom - n + 1, t->cols - 1, bottom);
    dirty(t, top, bottom);
}

static void scroll_down(struct vt *t, int top, int bottom, int n)
{
    int y;
    if (n > bottom - top + 1)
        n = bottom - top + 1;
    if (n <= 0)
        return;
    for (y = bottom; y - n >= top; y--)
        memcpy(at(t, 0, y), at(t, 0, y - n), (size_t)t->cols * sizeof(struct vt_cell));
    clear_cells(t, 0, top, t->cols - 1, top + n - 1);
    dirty(t, top, bottom);
}

static void line_feed(struct vt *t)
{
    t->wrap_next = 0;
    if (t->y == t->bottom)
        scroll_up(t, t->top, t->bottom, 1);
    else if (t->y < t->rows - 1)
        t->y++;
}

static void reverse_index(struct vt *t)
{
    t->wrap_next = 0;
    if (t->y == t->top)
        scroll_down(t, t->top, t->bottom, 1);
    else if (t->y > 0)
        t->y--;
}

/* ---- the cursor */

static void move_to(struct vt *t, int x, int y)
{
    int lo = t->origin ? t->top : 0, hi = t->origin ? t->bottom : t->rows - 1;
    if (t->origin)
        y += t->top;
    t->x = x < 0 ? 0 : x >= t->cols ? t->cols - 1 : x;
    t->y = y < lo ? lo : y > hi ? hi : y;
    t->wrap_next = 0;
}

static void cursor_rel(struct vt *t, int dx, int dy)
{
    int lo = t->y >= t->top && t->y <= t->bottom ? t->top : 0;      /* (inside the region: kept in it) */
    int hi = t->y >= t->top && t->y <= t->bottom ? t->bottom : t->rows - 1;
    int y = t->y + dy;
    t->x += dx;
    t->x = t->x < 0 ? 0 : t->x >= t->cols ? t->cols - 1 : t->x;
    t->y = y < lo ? lo : y > hi ? hi : y;
    t->wrap_next = 0;
}

static void save_cursor(struct vt *t)
{
    t->saved.x = t->x, t->saved.y = t->y, t->saved.fg = t->fg, t->saved.bg = t->bg, t->saved.attr = t->attr;
    t->saved.origin = t->origin, t->saved.wrap_next = t->wrap_next;
    t->saved.g0 = t->g0_lines, t->saved.g1 = t->g1_lines, t->saved.so = t->shift_out;
}

static void restore_cursor(struct vt *t)
{
    t->x = t->saved.x < t->cols ? t->saved.x : t->cols - 1;
    t->y = t->saved.y < t->rows ? t->saved.y : t->rows - 1;
    t->fg = (unsigned short)t->saved.fg, t->bg = (unsigned short)t->saved.bg, t->attr = (unsigned char)t->saved.attr;
    t->origin = t->saved.origin, t->wrap_next = t->saved.wrap_next;
    t->g0_lines = t->saved.g0, t->g1_lines = t->saved.g1, t->shift_out = t->saved.so;
}

/* ---- characters */

/* The DEC special graphics set, 5Fh-7Eh, as Unicode. */
static const unsigned short dec_lines[32] = {
    0x00A0, 0x25C6, 0x2592, 0x2409, 0x240C, 0x240D, 0x240A, 0x00B0, 0x00B1, 0x2424, 0x240B, 0x2518, 0x2510, 0x250C,
    0x2514, 0x253C, 0x23BA, 0x23BB, 0x2500, 0x23BC, 0x23BD, 0x251C, 0x2524, 0x2534, 0x252C, 0x2502, 0x2264, 0x2265,
    0x03C0, 0x2260, 0x00A3, 0x00B7 };

static void put_char(struct vt *t, unsigned c)
{
    struct vt_cell *p;
    if ((t->shift_out ? t->g1_lines : t->g0_lines) && c >= 0x5F && c <= 0x7E)
        c = dec_lines[c - 0x5F];
    if (c > 0xFFFF)
        c = 0xFFFD;
    if (t->wrap_next && t->autowrap) {
        t->x = 0;
        line_feed(t);
    }
    t->wrap_next = 0;
    if (t->insert) {
        memmove(at(t, t->x + 1, t->y), at(t, t->x, t->y), (size_t)(t->cols - t->x - 1) * sizeof(struct vt_cell));
    }
    p = at(t, t->x, t->y);
    p->ch = (unsigned short)c;
    p->fg = t->fg;
    p->bg = t->bg;
    p->attr = t->attr;
    t->dirty[t->y] = 1;
    t->last_ch = c;
    if (t->x == t->cols - 1)
        t->wrap_next = 1;
    else
        t->x++;
}

static void control(struct vt *t, unsigned char c)
{
    switch (c) {
    case 7:
        t->bell = 1;
        break;
    case 8:
        if (t->x > 0)
            t->x--;
        t->wrap_next = 0;
        break;
    case 9:
        while (t->x < t->cols - 1 && !t->tabs[++t->x]) ;
        t->wrap_next = 0;
        break;
    case 10:
    case 11:
    case 12:
        line_feed(t);
        if (t->lnm)
            t->x = 0;
        break;
    case 13:
        t->x = 0;
        t->wrap_next = 0;
        break;
    case 14:
        t->shift_out = 1;
        break;
    case 15:
        t->shift_out = 0;
        break;
    }
}

/* ---- CSI */

static int arg(const struct vt *t, int i, int dflt)
{
    return i < t->np && t->param[i] > 0 ? t->param[i] : dflt;
}

static int arg0(const struct vt *t, int i) { return i < t->np ? t->param[i] : 0; }

/* A direct colour taken to the nearest of the 256 (the 6x6x6 cube or the grey ramp). */
static unsigned short nearest(int r, int g, int b)
{
    static const int lev[6] = { 0, 95, 135, 175, 215, 255 };
    int ri, gi, bi, best, d, grey, gl, dg;
    for (ri = 0; ri < 5 && r > (lev[ri] + lev[ri + 1]) / 2; ri++) ;
    for (gi = 0; gi < 5 && g > (lev[gi] + lev[gi + 1]) / 2; gi++) ;
    for (bi = 0; bi < 5 && b > (lev[bi] + lev[bi + 1]) / 2; bi++) ;
    best = 16 + 36 * ri + 6 * gi + bi;
    d = (r - lev[ri]) * (r - lev[ri]) + (g - lev[gi]) * (g - lev[gi]) + (b - lev[bi]) * (b - lev[bi]);
    grey = (r + g + b) / 3;
    gl = grey < 8 ? 0 : grey > 238 ? 23 : (grey - 8) / 10;
    dg = (r - (8 + gl * 10)) * (r - (8 + gl * 10)) + (g - (8 + gl * 10)) * (g - (8 + gl * 10))
         + (b - (8 + gl * 10)) * (b - (8 + gl * 10));
    return (unsigned short)(dg < d ? 232 + gl : best);
}

static void sgr(struct vt *t)
{
    int i;
    if (!t->np) {
        t->fg = t->bg = VT_DEFAULT;
        t->attr = 0;
        return;
    }
    for (i = 0; i < t->np; i++) {
        int p = t->param[i];
        switch (p) {
        case 0: t->fg = t->bg = VT_DEFAULT; t->attr = 0; break;
        case 1: t->attr |= VT_BOLD; break;
        case 2: t->attr |= VT_DIM; break;
        case 3: t->attr |= VT_ITALIC; break;
        case 4: t->attr |= VT_UNDER; break;
        case 5: case 6: t->attr |= VT_BLINK; break;
        case 7: t->attr |= VT_REVERSE; break;
        case 8: t->attr |= VT_HIDDEN; break;
        case 9: t->attr |= VT_STRIKE; break;
        case 21: case 22: t->attr &= (unsigned char)~(VT_BOLD | VT_DIM); break;
        case 23: t->attr &= (unsigned char)~VT_ITALIC; break;
        case 24: t->attr &= (unsigned char)~VT_UNDER; break;
        case 25: t->attr &= (unsigned char)~VT_BLINK; break;
        case 27: t->attr &= (unsigned char)~VT_REVERSE; break;
        case 28: t->attr &= (unsigned char)~VT_HIDDEN; break;
        case 29: t->attr &= (unsigned char)~VT_STRIKE; break;
        case 39: t->fg = VT_DEFAULT; break;
        case 49: t->bg = VT_DEFAULT; break;
        case 38:
        case 48: {
            unsigned short c = VT_DEFAULT;
            if (i + 2 < t->np && t->param[i + 1] == 5) {
                c = (unsigned short)(t->param[i + 2] & 255);
                i += 2;
            } else if (i + 4 < t->np && t->param[i + 1] == 2) {
                c = nearest(t->param[i + 2], t->param[i + 3], t->param[i + 4]);
                i += 4;
            } else {
                i = t->np;
            }
            if (p == 38)
                t->fg = c;
            else
                t->bg = c;
            break;
        }
        default:
            if (p >= 30 && p <= 37)
                t->fg = (unsigned short)(p - 30);
            else if (p >= 40 && p <= 47)
                t->bg = (unsigned short)(p - 40);
            else if (p >= 90 && p <= 97)
                t->fg = (unsigned short)(p - 90 + 8);
            else if (p >= 100 && p <= 107)
                t->bg = (unsigned short)(p - 100 + 8);
        }
    }
}

static void reply(struct vt *t, const char *s)
{
    int n = (int)strlen(s);
    if (t->reply_n + n <= (int)sizeof t->reply) {
        memcpy(t->reply + t->reply_n, s, (size_t)n);
        t->reply_n += n;
    }
}

static void alt_screen(struct vt *t, int on, int mode)
{
    if (on == t->on_alt)
        return;
    if (on) {
        if (mode == 1049)
            save_cursor(t);
        t->cell = t->alt;
        t->on_alt = 1;
        if (mode != 47)
            clear_cells(t, 0, 0, t->cols - 1, t->rows - 1);
    } else {
        if (mode == 1047)
            clear_cells(t, 0, 0, t->cols - 1, t->rows - 1);
        t->cell = t->main;
        t->on_alt = 0;
        if (mode == 1049)
            restore_cursor(t);
    }
    dirty(t, 0, t->rows - 1);
}

static void mode(struct vt *t, int on)
{
    int i;
    for (i = 0; i < t->np || i == 0; i++) {
        int p = arg0(t, i);
        if (!t->priv) {
            if (p == 4)
                t->insert = on;
            else if (p == 20)
                t->lnm = on;
            continue;
        }
        switch (p) {
        case 1: t->app_cursor = on; break;
        case 5: t->reverse_screen = on; dirty(t, 0, t->rows - 1); break;
        case 6: t->origin = on; move_to(t, 0, 0); break;
        case 7: t->autowrap = on; break;
        case 25: t->cursor_on = on; break;
        case 47: case 1047: case 1049: alt_screen(t, on, p); break;
        case 1048: if (on) save_cursor(t); else restore_cursor(t); break;
        case 2004: t->bracketed = on; break;
        }
    }
}

static void csi(struct vt *t, unsigned char f)
{
    int n = arg(t, 0, 1), i;
    char buf[32];
    if (t->inter == ' ' || t->inter == '"' || t->inter == '$')     /* (DECSCUSR, DECSCA, DECRQM...: not here) */
        return;
    if (t->inter == '!' && f == 'p') {          /* DECSTR */
        soft_reset(t);
        return;
    }
    if (t->priv == '>' || t->priv == '=') {     /* (secondary DA, xterm's modifyKeys...) */
        if (f == 'c' && t->priv == '>')
            reply(t, "\033[>0;95;0c");
        return;
    }
    switch (f) {
    case '@':                                   /* ICH */
        if (n > t->cols - t->x)
            n = t->cols - t->x;
        memmove(at(t, t->x + n, t->y), at(t, t->x, t->y), (size_t)(t->cols - t->x - n) * sizeof(struct vt_cell));
        for (i = 0; i < n; i++)
            *at(t, t->x + i, t->y) = blank(t);
        t->dirty[t->y] = 1;
        break;
    case 'A': cursor_rel(t, 0, -n); break;
    case 'B': case 'e': cursor_rel(t, 0, n); break;
    case 'C': case 'a': cursor_rel(t, n, 0); break;
    case 'D': cursor_rel(t, -n, 0); break;
    case 'E': cursor_rel(t, 0, n); t->x = 0; break;
    case 'F': cursor_rel(t, 0, -n); t->x = 0; break;
    case 'G': case '`': t->x = n - 1 < t->cols ? n - 1 : t->cols - 1; t->wrap_next = 0; break;
    case 'H': case 'f': move_to(t, arg(t, 1, 1) - 1, n - 1); break;
    case 'I': for (i = 0; i < n; i++) control(t, 9); break;
    case 'Z':                                   /* CBT */
        for (i = 0; i < n && t->x > 0; i++)
            while (t->x > 0 && !t->tabs[--t->x]) ;
        break;
    case 'J':
        switch (arg0(t, 0)) {
        case 0: clear_cells(t, t->x, t->y, t->cols - 1, t->rows - 1); break;
        case 1: clear_cells(t, 0, 0, t->x, t->y); break;
        case 2: case 3: clear_cells(t, 0, 0, t->cols - 1, t->rows - 1); break;
        }
        t->wrap_next = 0;
        break;
    case 'K':
        switch (arg0(t, 0)) {
        case 0: clear_cells(t, t->x, t->y, t->cols - 1, t->y); break;
        case 1: clear_cells(t, 0, t->y, t->x, t->y); break;
        case 2: clear_cells(t, 0, t->y, t->cols - 1, t->y); break;
        }
        t->wrap_next = 0;
        break;
    case 'L':
        if (t->y >= t->top && t->y <= t->bottom)
            scroll_down(t, t->y, t->bottom, n);
        t->x = 0;
        break;
    case 'M':
        if (t->y >= t->top && t->y <= t->bottom)
            scroll_up(t, t->y, t->bottom, n);
        t->x = 0;
        break;
    case 'P':                                   /* DCH */
        if (n > t->cols - t->x)
            n = t->cols - t->x;
        memmove(at(t, t->x, t->y), at(t, t->x + n, t->y), (size_t)(t->cols - t->x - n) * sizeof(struct vt_cell));
        for (i = t->cols - n; i < t->cols; i++)
            *at(t, i, t->y) = blank(t);
        t->dirty[t->y] = 1;
        t->wrap_next = 0;
        break;
    case 'S': scroll_up(t, t->top, t->bottom, n); break;
    case 'T': if (t->np <= 1) scroll_down(t, t->top, t->bottom, n); break;
    case 'X':                                   /* ECH */
        for (i = 0; i < n && t->x + i < t->cols; i++)
            *at(t, t->x + i, t->y) = blank(t);
        t->dirty[t->y] = 1;
        t->wrap_next = 0;
        break;
    case 'b':                                   /* REP */
        for (i = 0; i < n && i < 1000 && t->last_ch; i++)
            put_char(t, t->last_ch);
        break;
    case 'c':
        if (!t->priv)
            reply(t, "\033[?1;2c");             /* a VT100 with the advanced video option, as xterm says */
        break;
    case 'd': move_to(t, t->x, n - 1 - (t->origin ? 0 : 0)); break;
    case 'g':
        if (arg0(t, 0) == 0)
            t->tabs[t->x] = 0;
        else if (arg0(t, 0) == 3)
            memset(t->tabs, 0, sizeof t->tabs);
        break;
    case 'h': mode(t, 1); break;
    case 'l': mode(t, 0); break;
    case 'm': if (!t->priv) sgr(t); break;
    case 'n':
        if (arg0(t, 0) == 5) {
            reply(t, "\033[0n");
        } else if (arg0(t, 0) == 6) {
            snprintf(buf, sizeof buf, "\033[%s%d;%dR", t->priv == '?' ? "?" : "",
                     t->y + 1 - (t->origin ? t->top : 0), t->x + 1);
            reply(t, buf);
        }
        break;
    case 'r':                                   /* DECSTBM */
        if (t->priv)
            break;
        {
            int top = arg(t, 0, 1) - 1, bottom = arg(t, 1, t->rows) - 1;
            if (bottom >= t->rows)
                bottom = t->rows - 1;
            if (top < bottom) {
                t->top = top;
                t->bottom = bottom;
                move_to(t, 0, 0);
            }
        }
        break;
    case 's': if (!t->priv) save_cursor(t); break;
    case 'u': if (!t->priv) restore_cursor(t); break;
    }
}

static void esc(struct vt *t, unsigned char f)
{
    switch (f) {
    case '7': save_cursor(t); break;
    case '8': restore_cursor(t); break;
    case 'D': line_feed(t); break;
    case 'E': t->x = 0; line_feed(t); break;
    case 'H': t->tabs[t->x] = 1; break;
    case 'M': reverse_index(t); break;
    case '=': t->app_keypad = 1; break;
    case '>': t->app_keypad = 0; break;
    case 'c': {                                 /* RIS */
        int cols = t->cols, rows = t->rows;
        char title[sizeof t->title];
        memcpy(title, t->title, sizeof title);
        vt_init(t, cols, rows);
        memcpy(t->title, title, sizeof title);
        dirty(t, 0, rows - 1);
        break;
    }
    }
}

/* ---- the parser */

static void osc_done(struct vt *t)
{
    t->osc[t->osc_n] = 0;
    if ((t->osc[0] == '0' || t->osc[0] == '2') && t->osc[1] == ';') {
        snprintf(t->title, sizeof t->title, "%s", t->osc + 2);
        t->title_set = 1;
    }
    t->state = S_GROUND;
}

static void byte(struct vt *t, unsigned char b)
{
    if (b == 0x18 || b == 0x1A) {               /* CAN, SUB: whatever was under way, given up */
        t->state = S_GROUND;
        return;
    }
    switch (t->state) {
    case S_GROUND:
        if (b == 0x1B)
            t->state = S_ESC;
        else if (b < 0x20 || b == 0x7F)
            control(t, b);
        else
            put_char(t, b);
        return;
    case S_ESC:
        t->priv = t->inter = t->np = t->have_param = 0;
        memset(t->param, 0, sizeof t->param);
        if (b == '[') {
            t->state = S_CSI;
        } else if (b == ']') {
            t->state = S_OSC;
            t->osc_n = 0;
        } else if (b == 'P' || b == '^' || b == '_' || b == 'X') {
            t->state = S_STR;                   /* (DCS, PM, APC, SOS: read to their ST) */
        } else if (b == '(' || b == ')') {
            t->inter = b;
            t->state = S_CHARSET;
        } else if (b >= 0x20 && b <= 0x2F) {
            t->inter = b;
            t->state = S_ESC_INTER;
        } else if (b < 0x20) {
            control(t, b);
        } else {
            esc(t, b);
            t->state = S_GROUND;
        }
        return;
    case S_ESC_INTER:
        if (b >= 0x30) {
            if (t->inter == '#' && b == '8') {  /* DECALN: the screen filled with E */
                int x, y;
                for (y = 0; y < t->rows; y++)
                    for (x = 0; x < t->cols; x++) {
                        at(t, x, y)->ch = 'E';
                        at(t, x, y)->attr = 0;
                    }
                dirty(t, 0, t->rows - 1);
            }
            t->state = S_GROUND;
        }
        return;
    case S_CHARSET:
        if (t->inter == '(')
            t->g0_lines = b == '0';
        else
            t->g1_lines = b == '0';
        t->state = S_GROUND;
        return;
    case S_CSI:
        if (b < 0x20) {
            control(t, b);
        } else if (b >= '0' && b <= '9') {
            if (!t->np)
                t->np = 1;
            if (t->param[t->np - 1] < 10000)
                t->param[t->np - 1] = t->param[t->np - 1] * 10 + (b - '0');
        } else if (b == ';' || b == ':') {
            if (!t->np)
                t->np = 1;
            if (t->np < 16)
                t->param[t->np++] = 0;
        } else if (b >= '<' && b <= '?') {
            t->priv = b;
        } else if (b >= 0x20 && b <= 0x2F) {
            t->inter = b;
        } else if (b >= 0x40 && b <= 0x7E) {
            csi(t, b);
            t->state = S_GROUND;
        }
        return;
    case S_OSC:
        if (b == 7)
            osc_done(t);
        else if (b == 0x1B)
            t->state = S_OSC_ESC;
        else if (t->osc_n < (int)sizeof t->osc - 1)
            t->osc[t->osc_n++] = (char)b;
        return;
    case S_OSC_ESC:
        osc_done(t);                            /* (ESC \ ends it; anything else after ESC does too) */
        if (b != '\\')
            byte(t, b);
        return;
    case S_STR:
        if (b == 0x1B)
            t->state = S_STR_ESC;
        return;
    case S_STR_ESC:
        t->state = b == '\\' ? S_GROUND : S_STR;
        return;
    }
}

void vt_feed(struct vt *t, const unsigned char *data, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        unsigned char b = data[i];
        if (t->state == S_GROUND && t->u8_left) {   /* the rest of a UTF-8 character */
            if ((b & 0xC0) == 0x80) {
                t->u8 = t->u8 << 6 | (b & 0x3F);
                if (!--t->u8_left)
                    put_char(t, t->u8);
                continue;
            }
            t->u8_left = 0;
            put_char(t, 0xFFFD);
        }
        if (t->state == S_GROUND && b >= 0x80) {
            if (b >= 0xC2 && b <= 0xDF)
                t->u8 = b & 0x1F, t->u8_left = 1;
            else if (b >= 0xE0 && b <= 0xEF)
                t->u8 = b & 0x0F, t->u8_left = 2;
            else if (b >= 0xF0 && b <= 0xF4)
                t->u8 = b & 0x07, t->u8_left = 3;
            else
                put_char(t, 0xFFFD);            /* (C1 controls aren't taken as 8-bit bytes in UTF-8) */
            continue;
        }
        byte(t, b);
    }
}

/* ---- keys */

int vt_key(const struct vt *t, unsigned key, unsigned ch, unsigned mods, char *out, int room)
{
    const char *s = 0;
    char buf[16];
    int m = 1 + ((mods & MOD_SHIFT) ? 1 : 0) + ((mods & MOD_ALT) ? 2 : 0) + ((mods & MOD_CTRL) ? 4 : 0), n;
    char cur = 0;
    switch (key) {
    case 0x148: cur = 'A'; break;               /* (cursor keys: SS3 in application mode, CSI otherwise) */
    case 0x150: cur = 'B'; break;
    case 0x14D: cur = 'C'; break;
    case 0x14B: cur = 'D'; break;
    case 0x147: cur = 'H'; break;
    case 0x14F: cur = 'F'; break;
    case 0x152: s = "\033[2~"; break;
    case 0x153: s = "\033[3~"; break;
    case 0x149: s = "\033[5~"; break;
    case 0x151: s = "\033[6~"; break;
    case 0x3B: s = "\033OP"; break;
    case 0x3C: s = "\033OQ"; break;
    case 0x3D: s = "\033OR"; break;
    case 0x3E: s = "\033OS"; break;
    case 0x3F: s = "\033[15~"; break;
    case 0x40: s = "\033[17~"; break;
    case 0x41: s = "\033[18~"; break;
    case 0x42: s = "\033[19~"; break;
    case 0x43: s = "\033[20~"; break;
    case 0x44: s = "\033[21~"; break;
    case 0x57: s = "\033[23~"; break;
    case 0x58: s = "\033[24~"; break;
    }
    if (cur) {
        if (m > 1)
            n = snprintf(buf, sizeof buf, "\033[1;%d%c", m, cur);
        else
            n = snprintf(buf, sizeof buf, "\033%c%c", t->app_cursor ? 'O' : '[', cur);
        s = buf;
    } else if (s && m > 1 && s[1] == '[' && s[strlen(s) - 1] == '~') {    /* Delete with a modifier: CSI 3 ; m ~ */
        n = snprintf(buf, sizeof buf, "%.*s;%d~", (int)strlen(s) - 1, s, m);
        s = buf;
    }
    if (s) {
        n = (int)strlen(s);
    } else {
        unsigned c = ch;
        n = 0;
        if (key == 0x0E || key == 0x10E)        /* Backspace: DEL, as xterm sends it */
            c = 0x7F;
        else if (key == 0x1C || key == 0x11C)
            c = '\r';
        else if (key == 0x0F)
            c = (mods & MOD_SHIFT) ? 0 : '\t';
        else if (key == 0x01)
            c = 0x1B;
        if (key == 0x0F && (mods & MOD_SHIFT)) {
            s = "\033[Z";
            n = 3;
        } else if (c) {
            if ((mods & MOD_CTRL) && c >= 0x40 && c < 0x80)
                c &= 0x1F;                      /* Ctrl+letter, Ctrl+[ and the like */
            else if ((mods & MOD_CTRL) && c == ' ')
                c = 0;
            if (mods & MOD_ALT)
                buf[n++] = 0x1B;
            if (c < 0x80) {
                buf[n++] = (char)c;
            } else if (c < 0x800) {
                buf[n++] = (char)(0xC0 | c >> 6);
                buf[n++] = (char)(0x80 | (c & 0x3F));
            } else {
                buf[n++] = (char)(0xE0 | c >> 12);
                buf[n++] = (char)(0x80 | ((c >> 6) & 0x3F));
                buf[n++] = (char)(0x80 | (c & 0x3F));
            }
            s = buf;
        } else if ((mods & MOD_CTRL) && (key == 0x03 || key == 0x39)) {   /* Ctrl+2, Ctrl+Space: NUL */
            buf[0] = 0;
            s = buf;
            n = 1;
        }
    }
    if (!s || n > room)
        return 0;
    memcpy(out, s, (size_t)n);
    return n;
}

/* ---- colours */

unsigned vt_rgb(unsigned short c, int fg)
{
    static const unsigned base[16] = { 0x000000, 0xCD0000, 0x00CD00, 0xCDCD00, 0x0000EE, 0xCD00CD, 0x00CDCD,
                                       0xE5E5E5, 0x7F7F7F, 0xFF0000, 0x00FF00, 0xFFFF00, 0x5C5CFF, 0xFF00FF,
                                       0x00FFFF, 0xFFFFFF };    /* (xterm's) */
    static const int lev[6] = { 0, 95, 135, 175, 215, 255 };
    if (c == VT_DEFAULT)
        return fg ? 0x000000 : 0xFFFFFF;        /* (black on white, as GLOS's fields are) */
    if (c < 16)
        return base[c];
    if (c < 232) {
        int i = c - 16;
        return (unsigned)lev[i / 36] << 16 | (unsigned)lev[i / 6 % 6] << 8 | (unsigned)lev[i % 6];
    }
    {
        unsigned g = 8 + (c - 232) * 10;
        return g << 16 | g << 8 | g;
    }
}

int vt_dump(const struct vt *t, char *out, int room)
{
    int x, y, n = 0;
    for (y = 0; y < t->rows; y++) {
        int end = t->cols;
        while (end > 0 && t->cell[y * VT_COLS_MAX + end - 1].ch == ' ')
            end--;
        for (x = 0; x < end; x++) {
            unsigned c = t->cell[y * VT_COLS_MAX + x].ch;
            if (n + 4 >= room)
                return n;
            if (c < 0x80) {
                out[n++] = (char)c;
            } else if (c < 0x800) {
                out[n++] = (char)(0xC0 | c >> 6);
                out[n++] = (char)(0x80 | (c & 0x3F));
            } else {
                out[n++] = (char)(0xE0 | c >> 12);
                out[n++] = (char)(0x80 | ((c >> 6) & 0x3F));
                out[n++] = (char)(0x80 | (c & 0x3F));
            }
        }
        if (n + 1 >= room)
            return n;
        out[n++] = '\n';
    }
    out[n] = 0;
    return n;
}
