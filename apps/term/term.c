/* TERM: GLOS's terminal and its shell (PRD §10.6, D50, D51; docs/milestones-m9.md
 * step E; shell.c has the shell).
 *
 * The window is a grid of text in DejaVu Sans Mono over a scroll-back of a
 * thousand lines. The shell's prompt and the line being typed are at its
 * end; what a program writes comes in there too, as a console's output
 * does: CR to the line's start, LF down a line, BS back a character, tab to
 * the next eighth column, FF a clean page, the line wrapped at the window's
 * edge. While a program runs the keys are its own: typed into its console
 * (GLOS_FN_JOB), Ctrl+C as DOS's Ctrl-C, Ctrl+Break ending it.
 *
 * The mouse chooses text, Ctrl+Shift+C or the Edit menu copies it, Ctrl+V
 * or Shift+Ins types the clipboard's text in; Shift+PgUp and Shift+PgDn
 * move through the scroll-back. The window can be any size: the columns
 * follow its width.
 *
 * M10: ssh, sftp and ssh-keygen (ssh.c). While an ssh session with a pty
 * runs, the window shows its xterm screen (vt.c) instead, and every key but
 * the terminal's own (Shift+Ins, Ctrl+Break) is the session's.
 *
 *   TERM [-log] [COMMAND]
 *                      a terminal; COMMAND typed into it first. -log: at the
 *                      end, every line of the scroll-back on stdout ("TERM|
 *                      ...": the suite's), and an ssh session's screen
 *                      ("VT| ...") whenever it has been still for a moment */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/gui.h>

#include "term.h"
#include "vt.h"

#define LINES    1000
#define COLS_MAX 200

struct line {
    unsigned char ch[COLS_MAX], at[COLS_MAX];
    int n;
};

static struct gui_window *win;
static struct gui_widget *grid;
static struct line *ring;               /* the scroll-back: line k is ring[(top + k) % LINES] */
static int top, count;                  /* the oldest line, and how many there are */
static int cur_col;                     /* the output's column on the last line */
static int cols = 80, rows = 25, cw, ch;
static int back;                        /* lines scrolled back from the end (0: the end shows) */
static int sel_on, sel_a_line, sel_a_col, sel_b_line, sel_b_col, dragging;
static unsigned char attr_now;
static struct gui_sb sb;

/* the line being typed (the shell's) */
static char input[256];
static int in_len, in_pos, in_start_line, in_start_col, in_shown, in_hidden;
static struct vt *screen;               /* an ssh session's xterm screen, while it shows */
static int screen_cursor_y = -1;        /* the row its cursor was drawn on */
static char home[260] = "C:\\GLOS";      /* TERM.EXE's folder */

static const unsigned colours[] = { GUI_LINE, GUI_DARK, 0x993333, GUI_LINE };   /* T_ attributes (D7: greys, an
                                           error's red the only other) */

static struct line *line_at(int k) { return &ring[(top + k) % LINES]; }

static void new_line(void)
{
    if (count < LINES) {
        count++;
    } else {
        top = (top + 1) % LINES;
        if (sel_on)
            sel_a_line--, sel_b_line--;
        in_start_line--;
    }
    line_at(count - 1)->n = 0;
    cur_col = 0;
}

static void damage_all(void) { gui_damage(grid); }

static int drawn_first = -1, drawn_count = -1; /* what the last drawing showed: the first line and the count */
static int first_shown(void);

/* Lines from `line` to the end changed: their rows drawn again, not the whole grid (a 486 can't redraw two
   thousand characters for every key typed); everything when the grid has scrolled or grown. */
static void damage_from(int line)
{
    struct gfx_rect r;
    int first = first_shown();
    if (!grid || first != drawn_first || count != drawn_count || !ch) {
        damage_all();
        return;
    }
    if (line < first)
        line = first;
    r.x = grid->r.x + 1;
    r.w = grid->r.w - GUI_SB - 1;
    r.y = grid->r.y + 2 + (line - first) * ch;
    r.h = grid->r.y + grid->r.h - 1 - r.y;
    if (r.h > 0)
        gui_damage_rect(grid->win, &r);
}

/* ---- output (a program's, the shell's) */

void term_attr(int a) { attr_now = (unsigned char)a; }

void term_put(unsigned char c)
{
    struct line *l;
    if (!count)
        new_line();
    l = line_at(count - 1);
    switch (c) {
    case '\r':
        cur_col = 0;
        return;
    case '\n': {
        int col = cur_col;
        new_line();
        cur_col = 0;                            /* (DOS's text has CR LF: a lone LF starts the line too) */
        (void)col;
        return;
    }
    case '\b':
        if (cur_col > 0)
            cur_col--;
        return;
    case '\t':
        do
            term_put(' ');
        while (cur_col % 8);
        return;
    case '\f': {                                /* a clean page: as many empty lines as the window shows */
        int i;
        if (l->n || cur_col)
            new_line();
        for (i = 1; i < rows; i++)
            new_line();
        cur_col = 0;
        return;
    }
    case 7:
        return;                                 /* (the bell) */
    }
    if (cur_col >= cols) {
        new_line();
        l = line_at(count - 1);
    }
    while (l->n < cur_col) {
        l->ch[l->n] = ' ';
        l->at[l->n++] = 0;
    }
    l->ch[cur_col] = c < 0x20 ? ' ' : c;
    l->at[cur_col] = attr_now;
    if (cur_col == l->n)
        l->n++;
    cur_col++;
}

void term_write(const char *s, int n)
{
    int i, from = count ? count - 1 : 0, was_back = back;
    if (n < 0)
        n = (int)strlen(s);
    for (i = 0; i < n; i++)
        term_put((unsigned char)s[i]);
    back = 0;
    if (was_back)
        damage_all();
    else
        damage_from(from);
}

void term_print(const char *s) { term_write(s, -1); }

void term_clear(void) { term_put('\f'); damage_all(); }

/* ---- the line being typed: shown after the prompt, at the end */

static void input_erase(void)
{
    if (!in_shown)
        return;
    while (count - 1 > in_start_line) {         /* (it may have wrapped onto lines of its own) */
        count--;
    }
    if (count > 0)
        line_at(count - 1)->n = in_start_col;
    cur_col = in_start_col;
    in_shown = 0;
}

static void input_show(void)
{
    int i, was_back = back;
    input_erase();
    in_start_line = count - 1;
    in_start_col = cur_col;
    attr_now = T_INPUT;
    for (i = 0; i < in_len && !in_hidden; i++)     /* (a password: nothing shown, as ssh shows it) */
        term_put((unsigned char)input[i]);
    attr_now = 0;
    in_shown = 1;
    back = 0;
    if (was_back)
        damage_all();
    else
        damage_from(in_start_line);
}

void term_prompt_done(void)                     /* the prompt written: the line begins after it */
{
    if (!count)
        new_line();
    in_len = in_pos = 0;
    in_start_line = count - 1;
    in_start_col = cur_col;
    in_shown = 1;
}

void term_input_set(const char *s)
{
    snprintf(input, sizeof input, "%s", s);
    in_len = in_pos = (int)strlen(input);
    input_show();
}

const char *term_input(void) { return input; }

void term_input_hidden(int on) { in_hidden = on; }

void term_printf(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    term_write(buf, -1);
}

const char *term_home(void) { return home; }

/* ---- drawing */

static int lines_shown(void) { return rows; }

static void layout_grid(void)
{
    int w = grid->r.w - GUI_SB - 8, h = grid->r.h - 4;
    cw = gui_char_w(gui_mono, 'n');
    ch = gui_mono->height;
    cols = w / cw > 20 ? w / cw : 20;
    if (cols > COLS_MAX)
        cols = COLS_MAX;
    rows = h / ch > 3 ? h / ch : 3;
}

static int first_shown(void)
{
    int f = count - lines_shown() - back;
    return f < 0 ? 0 : f;
}

static int in_sel(int line, int col)
{
    int al = sel_a_line, ac = sel_a_col, bl = sel_b_line, bc = sel_b_col;
    if (!sel_on)
        return 0;
    if (al > bl || (al == bl && ac > bc)) {
        int t = al; al = bl; bl = t;
        t = ac; ac = bc; bc = t;
    }
    if (line < al || line > bl)
        return 0;
    if (line == al && col < ac)
        return 0;
    if (line == bl && col >= bc)
        return 0;
    return 1;
}

/* ---- the xterm screen (an ssh session's) */

void term_size(int *c, int *r)
{
    *c = cols < VT_COLS_MAX ? cols : VT_COLS_MAX;
    *r = rows < VT_ROWS_MAX ? rows : VT_ROWS_MAX;
}

void term_screen_on(struct vt *v)
{
    screen = v;
    screen_cursor_y = -1;
    damage_all();
}

void term_screen_off(void)
{
    screen = 0;
    damage_all();
}

/* Its rows that changed (and the cursor's, old and new) drawn again. */
void term_screen_dirty(void)
{
    int y;
    if (!screen || !grid || !ch)
        return;
    if (screen_cursor_y >= 0 && screen_cursor_y < screen->rows)
        screen->dirty[screen_cursor_y] = 1;
    screen->dirty[screen->y] = 1;
    for (y = 0; y < screen->rows; y++)
        if (screen->dirty[y]) {
            struct gfx_rect r;
            int n = 1;
            while (y + n < screen->rows && screen->dirty[y + n])
                n++;
            r.x = grid->r.x + 1;
            r.w = grid->r.w - GUI_SB - 1;
            r.y = grid->r.y + 2 + y * ch;
            r.h = n * ch;
            gui_damage_rect(grid->win, &r);
            y += n - 1;
        }
}

static void screen_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct gfx_rect frame = { w->r.x, w->r.y, w->r.w - GUI_SB + 1, w->r.h };
    int x0 = w->r.x + 4, y0 = w->r.y + 2, y, x, ymin, ymax;
    gui_draw_field(s, &frame, 1);
    ymin = (s->clip.y - y0) / ch;
    ymax = (s->clip.y + s->clip.h - y0) / ch + 1;
    for (y = ymin > 0 ? ymin : 0; y < screen->rows && y <= ymax; y++) {
        for (x = 0; x < screen->cols && x < cols; x++) {
            const struct vt_cell *c = &screen->cell[y * VT_COLS_MAX + x];
            int rev = !!(c->attr & VT_REVERSE) ^ screen->reverse_screen;
            int cur = screen->cursor_on && x == screen->x && y == screen->y && gui_window_active(win);
            unsigned fg = vt_rgb(rev ? c->bg : c->fg, !rev), bg = vt_rgb(rev ? c->fg : c->bg, rev);
            char u[3];
            int un;
            if (cur) {
                unsigned t = fg;
                fg = bg, bg = t;
            }
            if ((c->attr & VT_BOLD) && c->fg < 8 && !rev)
                fg = vt_rgb((unsigned short)(c->fg + 8), 1);     /* (bold as the bright colour, as xterm can) */
            if (bg != 0xFFFFFF)
                gfx_fill(s, x0 + x * cw, y0 + y * ch, cw, ch, bg);
            if (c->ch == ' ' || (c->attr & VT_HIDDEN))
                continue;
            if (c->ch < 0x80)
                u[0] = (char)c->ch, un = 1;
            else if (c->ch < 0x800)
                u[0] = (char)(0xC0 | (c->ch >> 6)), u[1] = (char)(0x80 | (c->ch & 0x3F)), un = 2;
            else
                u[0] = (char)(0xE0 | (c->ch >> 12)), u[1] = (char)(0x80 | ((c->ch >> 6) & 0x3F)),
                u[2] = (char)(0x80 | (c->ch & 0x3F)), un = 3;
            gfx_text(s, x0 + x * cw, y0 + y * ch, gui_mono, u, un, fg);
            if (c->attr & VT_UNDER)
                gfx_fill(s, x0 + x * cw, y0 + y * ch + ch - 1, cw, 1, fg);
        }
        screen->dirty[y] = 0;
    }
    screen_cursor_y = screen->y;
}

static void grid_draw(struct gui_widget *w, struct gfx_surface *s)
{
    int k, kmin, kmax, first, x0 = w->r.x + 4, y0 = w->r.y + 2;
    struct gfx_rect frame = { w->r.x, w->r.y, w->r.w - GUI_SB + 1, w->r.h };
    layout_grid();
    if (screen) {
        screen_draw(w, s);
        return;
    }
    gui_draw_field(s, &frame, 1);
    first = first_shown();
    drawn_first = first, drawn_count = count;
    kmin = (s->clip.y - y0) / ch;               /* (only the rows being drawn again) */
    kmax = (s->clip.y + s->clip.h - y0) / ch + 1;
    for (k = kmin > 0 ? kmin : 0; k < lines_shown() && k <= kmax && first + k < count; k++) {
        const struct line *l = line_at(first + k);
        int c, y = y0 + k * ch;
        for (c = 0; c < l->n && c < cols; c++) {
            int chosen = in_sel(first + k, c);
            char u[3];
            int un;
            unsigned char b = l->ch[c];
            if (chosen)
                gfx_fill(s, x0 + c * cw, y, cw, ch, GUI_DARK);
            if (b == ' ')
                continue;
            if (b < 0x80)
                u[0] = (char)b, un = 1;
            else
                u[0] = (char)(0xC0 | (b >> 6)), u[1] = (char)(0x80 | (b & 0x3F)), un = 2;
            gfx_text(s, x0 + c * cw, y, l->at[c] == T_PROMPT ? gui_mono : gui_mono, u, un,
                     chosen ? GUI_FIELD : colours[l->at[c] < 4 ? l->at[c] : 0]);
        }
    }
    if (!back && gui_window_active(win)) {      /* the caret: where typing goes, or the output's column */
        int line = term_busy() ? count - 1 : in_start_line,
            col = term_busy() ? cur_col : in_start_col + (in_hidden ? 0 : in_pos);
        while (col >= cols) {
            col -= cols;
            line++;
        }
        if (line - first >= 0 && line - first < lines_shown())
            gfx_fill(s, x0 + col * cw, y0 + (line - first) * ch + ch - 2, cw, 2, GUI_LINE);
    }
    sb.r.x = w->r.x + w->r.w - GUI_SB, sb.r.y = w->r.y, sb.r.w = GUI_SB, sb.r.h = w->r.h;
    sb.total = count;
    sb.page = lines_shown();
    sb.pos = first;
    gui_sb_draw(s, &sb, 1);
}

/* ---- the keyboard */

static void input_key(struct gui_ev *e)
{
    unsigned c = e->ch;
    if (e->mods & GLOS_MOD_CTRL) {
        if (e->key == GUI_K_LEFT) {
            while (in_pos > 0 && input[in_pos - 1] == ' ')
                in_pos--;
            while (in_pos > 0 && input[in_pos - 1] != ' ')
                in_pos--;
        } else if (e->key == GUI_K_RIGHT) {
            while (in_pos < in_len && input[in_pos] != ' ')
                in_pos++;
            while (in_pos < in_len && input[in_pos] == ' ')
                in_pos++;
        }
        damage_all();
        return;
    }
    switch (e->key) {
    case GUI_K_LEFT: if (in_pos > 0) in_pos--; damage_from(in_start_line); return;
    case GUI_K_RIGHT: if (in_pos < in_len) in_pos++; damage_from(in_start_line); return;
    case GUI_K_HOME: in_pos = 0; damage_from(in_start_line); return;
    case GUI_K_END: in_pos = in_len; damage_from(in_start_line); return;
    case GUI_K_UP: if (!in_hidden) shell_history(-1); return;
    case GUI_K_DOWN: if (!in_hidden) shell_history(1); return;
    case GUI_K_BACK:
        if (in_pos > 0) {
            memmove(input + in_pos - 1, input + in_pos, (size_t)(in_len - in_pos + 1));
            in_pos--, in_len--;
            input_show();
        }
        return;
    case GUI_K_DEL:
        if (in_pos < in_len) {
            memmove(input + in_pos, input + in_pos + 1, (size_t)(in_len - in_pos));
            in_len--;
            input_show();
        }
        return;
    case GUI_K_ESC:
        input[0] = 0;
        in_len = in_pos = 0;
        input_show();
        return;
    case GUI_K_TAB:
        if (!ssh_asking())
            shell_complete();
        return;
    case GUI_K_ENTER:
    case GUI_K_KPENTER: {
        char line[256];
        memcpy(line, input, (size_t)in_len + 1);
        in_len = in_pos = 0;
        input[0] = 0;
        in_shown = 0;
        term_print("\r\n");
        shell_line(line);
        memset(line, 0, sizeof line);           /* (a password, perhaps) */
        return;
    }
    }
    if (c >= 0x20 && c < 0x100 && c != 0x7F && in_len < (int)sizeof input - 1) {
        memmove(input + in_pos + 1, input + in_pos, (size_t)(in_len - in_pos + 1));
        input[in_pos++] = (char)c;
        in_len++;
        input_show();
    }
}

/* Typed while a program runs: into its console (the BIOS's word: scan code high, character low). */
static void job_key(struct gui_ev *e)
{
    unsigned short k;
    if (e->key == 0x146 && e->mods & GLOS_MOD_CTRL) {   /* Ctrl+Break */
        shell_break();
        return;
    }
    k = (unsigned short)(((e->key & 0xFF) << 8) | (e->ch & 0xFF));
    if (e->key >= 0x100 && !e->ch)
        k = (unsigned short)(((e->key & 0xFF) << 8) | 0xE0);   /* (the grey keys: E0h, as the BIOS's 10h gives) */
    if (e->key == 0x14B || e->key == 0x14D || e->key == 0x148 || e->key == 0x150 || e->key == 0x147
        || e->key == 0x14F || e->key == 0x149 || e->key == 0x151 || e->key == 0x152 || e->key == 0x153)
        k = (unsigned short)((e->key & 0xFF) << 8);     /* (00h below them, as INT 16h 00h gives) */
    if (k)
        shell_type(&k, 1);
}

/* Keys typed into a program that ended without reading them (type-ahead): the prompt's, or the next program's,
   as DOS's BIOS buffer would have kept them. */
void term_keys(const unsigned short *k, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        struct gui_ev e;
        unsigned scan = k[i] >> 8, c = k[i] & 0xFF;
        memset(&e, 0, sizeof e);
        e.type = GUI_EV_KEY;
        e.key = scan | ((c == 0 || c == 0xE0) && scan >= 0x47 && scan <= 0x53 ? 0x100u : 0u);
        e.ch = c == 0xE0 ? 0 : c;
        if (term_busy())
            job_key(&e);
        else
            input_key(&e);
    }
}

void term_paste(void)
{
    size_t n, i;
    char *c = gui_clip_get(&n);
    if (!c)
        return;
    for (i = 0; i < n; i++) {
        struct gui_ev e;
        memset(&e, 0, sizeof e);
        e.type = GUI_EV_KEY;
        if (c[i] == '\r')
            continue;
        if (c[i] == '\n') {
            e.key = GUI_K_ENTER;
            e.ch = 13;
        } else {
            e.ch = (unsigned char)c[i];
        }
        if (term_busy())
            job_key(&e);
        else if (e.key != GUI_K_ENTER || i + 1 < n)
            input_key(&e);
    }
    free(c);
}

void term_copy(void)
{
    char *t;
    int line, n = 0, room = 65536;
    if (!sel_on)
        return;
    if (!(t = malloc((size_t)room)))
        return;
    for (line = 0; line < count && n < room - 3; line++) {
        const struct line *l = line_at(line);
        int c, any = 0;
        for (c = 0; c < cols && n < room - 3; c++)
            if (in_sel(line, c)) {
                t[n++] = c < l->n ? (char)l->ch[c] : ' ';
                any = 1;
            }
        if (any && in_sel(line + 1, 0)) {
            while (n > 0 && t[n - 1] == ' ')
                n--;
            t[n++] = '\r', t[n++] = '\n';
        }
    }
    while (n > 0 && t[n - 1] == ' ')
        n--;
    gui_clip_set(t, (size_t)n);
    free(t);
}

static int grid_event(struct gui_widget *w, struct gui_ev *e)
{
    int old = back;
    sb.r.x = w->r.x + w->r.w - GUI_SB, sb.r.y = w->r.y, sb.r.w = GUI_SB, sb.r.h = w->r.h;
    sb.total = count, sb.page = lines_shown(), sb.pos = first_shown();
    if (e->type != GUI_EV_KEY && gui_sb_event(w, &sb, e, 1)) {
        back = count - lines_shown() - sb.pos;
        if (back < 0)
            back = 0;
        return 1;
    }
    switch (e->type) {
    case GUI_EV_PRESS:
        if (e->button != 1)
            return 1;
        sel_on = 1;
        sel_a_line = sel_b_line = first_shown() + (e->y - w->r.y - 2) / (ch ? ch : 1);
        sel_a_col = sel_b_col = (e->x - w->r.x - 4 + cw / 2) / (cw ? cw : 1);
        dragging = 1;
        damage_all();
        return 1;
    case GUI_EV_MOVE:
        if (dragging) {
            sel_b_line = first_shown() + (e->y - w->r.y - 2) / (ch ? ch : 1);
            sel_b_col = (e->x - w->r.x - 4 + cw / 2) / (cw ? cw : 1);
            damage_all();
        }
        return 1;
    case GUI_EV_RELEASE:
        dragging = 0;
        if (sel_a_line == sel_b_line && sel_a_col == sel_b_col)
            sel_on = 0;
        damage_all();
        return 1;
    case GUI_EV_FOCUS:
    case GUI_EV_BLUR:
        damage_all();
        return 1;
    case GUI_EV_KEY:
        break;
    default:
        return 0;
    }
    if (e->mods & GLOS_MOD_SHIFT && (e->key == GUI_K_PGUP || e->key == GUI_K_PGDN)) {
        back += e->key == GUI_K_PGUP ? lines_shown() - 1 : -(lines_shown() - 1);
        if (back > count - lines_shown())
            back = count - lines_shown();
        if (back < 0)
            back = 0;
        if (back != old)
            damage_all();
        return 1;
    }
    if ((e->mods & GLOS_MOD_SHIFT && e->key == GUI_K_INS) || (!screen && e->mods == GLOS_MOD_CTRL && e->ch == 22)) {
        term_paste();
        return 1;
    }
    if (ssh_active() && !ssh_asking()) {        /* (an ssh session's keys, Alt's too) */
        if (e->key == 0x146 && e->mods & GLOS_MOD_CTRL)
            ssh_break();
        else if (!(e->key == 0x2A || e->key == 0x36 || e->key == 0x1D || e->key == 0x38 || e->key == 0x11D
                   || e->key == 0x138 || e->key == 0x3A || e->key == 0x45))
            ssh_key(e->key, e->ch, e->mods);
        return 1;
    }
    if (e->mods == (GLOS_MOD_CTRL | GLOS_MOD_SHIFT) && e->key == 0x2E) {
        term_copy();
        return 1;
    }
    if (e->key == 0x2A || e->key == 0x36 || e->key == 0x1D || e->key == 0x38 || e->key == 0x11D || e->key == 0x138
        || e->key == 0x3A || e->key == 0x45)
        return 1;                               /* (the modifiers alone are no keys) */
    if (e->mods & GLOS_MOD_ALT)
        return 0;                               /* (the menu's) */
    sel_on = 0;
    if (term_busy())
        job_key(e);
    else
        input_key(e);
    return 1;
}

/* ---- the window */

enum { C_COPY = 1, C_PASTE, C_CLEAR, C_EXIT, C_ABOUT };

static void command(struct gui_window *w, int id)
{
    (void)w;
    switch (id) {
    case C_COPY: term_copy(); break;
    case C_PASTE: term_paste(); break;
    case C_CLEAR: term_clear(); shell_prompt(); break;
    case C_EXIT: gui_quit(0); break;
    case C_ABOUT:
        gui_message(win, "About Terminal", "Terminal: GLOS's shell. HELP lists its commands.", GUI_MB_OK);
        break;
    }
    gui_focus(grid);
}

static void resized(struct gui_window *w)
{
    int c, r;
    (void)w;
    layout_grid();
    term_size(&c, &r);
    ssh_resize(c, r);
    damage_all();
}

static void tick(void *arg)
{
    (void)arg;
    shell_poll();
    ssh_poll();
}

void term_title(const char *t)
{
    char full[80];
    snprintf(full, sizeof full, "%s%sTerminal", t, *t ? " - " : "");
    gui_title(win, full);
}

void term_quit(int code) { gui_quit(code); }

static void events(const struct glos_event *ev)
{
    if (ev->type == GLOS_EV_JOB)
        shell_poll();
    else if (ev->type == GLOS_EV_NET)
        ssh_poll();
}

int main(int argc, char **argv)
{
    struct gui_menubar *bar;
    struct gui_menu *m;
    int i, log = 0;
    if (gui_init(argc, argv) != 0) {
        printf("TERM no GLOS\n");
        return 1;
    }
    {
        char *p;
        snprintf(home, sizeof home, "%s", argv[0]);     /* (DJGPP gives it with / and its case) */
        for (p = home; *p; p++)
            *p = *p == '/' ? '\\' : (*p >= 'a' && *p <= 'z') ? (char)(*p - 32) : *p;
        if (strrchr(home, '\\'))
            *strrchr(home, '\\') = 0;
        else
            snprintf(home, sizeof home, "C:\\GLOS");
    }
    if (argc > 1 && !strcmp(argv[1], "-log")) {
        log = 1;
        argv++, argc--;
    }
    ssh_log_screens(log);
    ring = calloc(LINES, sizeof *ring);
    if (!ring)
        return 1;
    win = gui_window("Terminal", 8 + 80 * 7 + GUI_SB + 4, 25 * 14 + 4, 0);
    win->on_command = command;
    win->on_resize = resized;
    bar = gui_menubar(win);
    m = gui_menu(bar, "&Edit");
    gui_menu_item(m, "&Copy", C_COPY, "Ctrl+Shift+C");
    gui_menu_item(m, "&Paste", C_PASTE, "Shift+Ins");
    gui_menu_item(m, "C&lear", C_CLEAR, NULL);
    gui_menu_sep(m);
    gui_menu_item(m, "E&xit", C_EXIT, NULL);
    m = gui_menu(bar, "&Help");
    gui_menu_item(m, "&About Terminal", C_ABOUT, NULL);
    grid = gui_canvas(gui_vbox(win->root, 0, 0), 0, 0, grid_draw, grid_event);
    gui_grow(grid->parent, 1);
    gui_grow(grid, 1);
    win->focus = grid;
    gui_on_event(events);
    shell_start();
    gui_show(win, 1);
    layout_grid();
    shell_prompt();
    if (argc > 1) {
        char line[256] = "";
        for (i = 1; i < argc; i++) {
            strncat(line, argv[i], sizeof line - strlen(line) - 2);
            if (i + 1 < argc)
                strcat(line, " ");
        }
        term_input_set(line);
    }
    gui_idle(tick, NULL, 250);
    gui_run();
    for (i = 0; log && i < count; i++) {
        const struct line *l = line_at(i);
        printf("TERM| %.*s\n", l->n, (const char *)l->ch);
    }
    printf("TERM end\n");
    return gui_exit_code();
}
