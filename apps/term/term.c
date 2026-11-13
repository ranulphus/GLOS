/* TERM: GLOS's terminal (PRD §10.6, D50, D51; docs/v1/shell.md, step S2c).
 *
 * A terminal and nothing else: the window shows an xterm screen (vt.c) with a thousand lines kept above it,
 * and the shell at it is GLOS's own, a session in the kernel opened as a job (API 2.1's GLOS_RUN_SHELL). What
 * the session writes is the screen's; the keys go to it as an xterm sends them; the window's size is the
 * terminal's. The shell, its commands, the programs it runs and SSH (SSH.EXE) are not the Terminal's business.
 *
 * The mouse chooses text (in what is kept above too), Ctrl+Shift+C or the Edit menu copies it, Shift+Ins or
 * Ctrl+Shift+V types the clipboard's text in; Shift+PgUp and Shift+PgDn move through what is kept. The window
 * can be any size.
 *
 *   TERM [-log] [COMMAND]
 *                      a terminal; COMMAND typed into it first. -log: on stdout, the screen whenever it has
 *                      been still for a moment ("VT| ...") and, at the end, every line kept and the screen
 *                      ("TERM| ...": the suite's) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/gui.h>

#include "vt.h"

#define LINES 1000

struct kept {                           /* a line that left the top of the screen: its cells to the last used */
    struct vt_cell *cell;
    int n;
};

static struct gui_window *win;
static struct gui_widget *grid;
static struct vt vt;
static struct kept kept[LINES];
static int ktop, kcount;                /* the oldest kept line, and how many there are */
static int cols = 80, rows = 25, cw, ch;
static int back;                        /* lines scrolled back (0: the screen shows) */
static int sel_on, sel_a_line, sel_a_col, sel_b_line, sel_b_col, dragging;    /* lines: kept ones, then the screen's */
static struct gui_sb sb;
static glos_handle job;                 /* the shell's session */
static int ended, exit_code, cursor_y = -1, log_on, dump_due, discard;
static unsigned changed_at;

static const struct kept *kept_at(int k) { return &kept[(ktop + k) % LINES]; }

static void damage_all(void)
{
    if (grid)
        gui_damage(grid);
}

/* ---- the lines kept */

static void line_gone(void *ctx, const struct vt_cell *row, int n)
{
    struct kept *k;
    (void)ctx;
    if (discard)                                /* (Edit, Clear: the screen being wiped is not kept) */
        return;
    while (n > 0 && row[n - 1].ch == ' ' && row[n - 1].bg == VT_DEFAULT && !(row[n - 1].attr & VT_REVERSE))
        n--;
    if (kcount == LINES) {                      /* the oldest goes */
        free(kept[ktop].cell);
        ktop = (ktop + 1) % LINES;
        kcount--;
        sel_a_line--, sel_b_line--;
    }
    k = &kept[(ktop + kcount) % LINES];
    k->cell = n ? malloc((size_t)n * sizeof *row) : NULL;
    k->n = k->cell ? n : 0;
    if (k->cell)
        memcpy(k->cell, row, (size_t)n * sizeof *row);
    kcount++;
    if (back && back < kcount)                  /* (what is looked at stays where it is) */
        back++;
    /* (the screen's rows moved up one and the kept lines grew by one: chosen text keeps its line numbers) */
    damage_all();
}

/* Line `line` (kept ones first, then the screen's rows), column x: its cell, or NULL for nothing there. */
static const struct vt_cell *cell_at(int line, int x)
{
    if (line < 0 || x < 0 || x >= cols)
        return NULL;
    if (line < kcount) {
        const struct kept *k = kept_at(line);
        return x < k->n ? &k->cell[x] : NULL;
    }
    line -= kcount;
    return line < vt.rows && x < vt.cols ? &vt.cell[line * VT_COLS_MAX + x] : NULL;
}

static int first_shown(void) { return kcount - back; }

static int in_sel(int line, int col)
{
    int al = sel_a_line, ac = sel_a_col, bl = sel_b_line, bc = sel_b_col;
    if (!sel_on)
        return 0;
    if (al > bl || (al == bl && ac > bc)) {
        int t = al;
        al = bl, bl = t;
        t = ac, ac = bc, bc = t;
    }
    if (line < al || line > bl)
        return 0;
    return (line > al || col >= ac) && (line < bl || col < bc);
}

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

/* ---- the session */

static void send(const char *d, int n)
{
    while (n > 0 && job && !ended) {
        int k = glos_job_write(job, d, (unsigned)n);
        if (k <= 0)
            break;                              /* (it takes no more now: what is left is lost, as typed ahead) */
        d += k, n -= k;
    }
}

static void dump_screen(void)
{
    static char text[VT_COLS_MAX * VT_ROWS_MAX * 3 + VT_ROWS_MAX + 1];
    char *p, *nl;
    vt_dump(&vt, text, sizeof text);
    printf("VT-SCREEN %dx%d\n", vt.cols, vt.rows);
    for (p = text; (nl = strchr(p, '\n')) != NULL; p = nl + 1)
        printf("VT| %.*s\n", (int)(nl - p), p);
    printf("VT-END\n");
    fflush(stdout);
}

/* What the session has written, onto the screen; its end. */
static void poll(void)
{
    unsigned char buf[2048];
    struct glos_job_status st;
    int n, y, any = 0;
    if (!job || ended)
        return;
    if (glos_job_status(job, &st) != 0)         /* (its state first: once it has ended, what is read is all of it) */
        st.state = GLOS_JS_DONE, st.code = 255;
    while ((n = glos_job_read(job, GLOS_JOB_OUT, buf, sizeof buf)) > 0) {
        vt_feed(&vt, buf, n);
        any = 1;
        if (vt.reply_n) {                       /* (what a program asked the terminal: its answer) */
            send(vt.reply, vt.reply_n);
            vt.reply_n = 0;
        }
    }
    if (any) {
        discard = 0;
        if (vt.title_set) {
            char full[160];
            vt.title_set = 0;
            snprintf(full, sizeof full, "%s%sTerminal", vt.title, vt.title[0] ? " - " : "");
            gui_title(win, full);
        }
        changed_at = glos_ticks();
        dump_due = 1;
        if (back) {
            damage_all();
        } else if (grid && ch) {                /* the rows that changed, and the cursor's old and new */
            if (cursor_y >= 0 && cursor_y < vt.rows)
                vt.dirty[cursor_y] = 1;
            vt.dirty[vt.y] = 1;
            for (y = 0; y < vt.rows; y++)
                if (vt.dirty[y]) {
                    struct gfx_rect r;
                    int k = 1;
                    while (y + k < vt.rows && vt.dirty[y + k])
                        k++;
                    r.x = grid->r.x + 1, r.w = grid->r.w - GUI_SB - 1;
                    r.y = grid->r.y + 2 + y * ch, r.h = k * ch;
                    gui_damage_rect(grid->win, &r);
                    y += k - 1;
                }
        }
    }
    if (st.state == GLOS_JS_DONE) {             /* EXIT, or the session's end: the window's (its state was asked
                                                   first, so what it wrote is all read) */
        ended = 1;
        exit_code = (int)st.code;
        gui_quit(exit_code);
    }
    if (log_on && dump_due && glos_ticks() - changed_at > 1500) {
        dump_due = 0;
        dump_screen();
    }
}

/* ---- drawing */

static void layout_grid(void)
{
    int w = grid->r.w - GUI_SB - 8, h = grid->r.h - 4;
    cw = gui_char_w(gui_mono, 'n');
    ch = gui_mono->height;
    cols = w / cw > 20 ? w / cw : 20;
    if (cols > VT_COLS_MAX)
        cols = VT_COLS_MAX;
    rows = h / ch > 3 ? h / ch : 3;
    if (rows > VT_ROWS_MAX)
        rows = VT_ROWS_MAX;
}

static void grid_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct gfx_rect frame = { w->r.x, w->r.y, w->r.w - GUI_SB + 1, w->r.h };
    int x0 = w->r.x + 4, y0 = w->r.y + 2, y, x, ymin, ymax, first = first_shown();
    layout_grid();
    gui_draw_field(s, &frame, 1);
    ymin = (s->clip.y - y0) / ch;               /* (only the rows being drawn again) */
    ymax = (s->clip.y + s->clip.h - y0) / ch + 1;
    for (y = ymin > 0 ? ymin : 0; y < rows && y <= ymax; y++) {
        int line = first + y, on_screen = line >= kcount;
        for (x = 0; x < cols; x++) {
            const struct vt_cell *c = cell_at(line, x);
            int chosen = in_sel(line, x), rev, cur;
            unsigned fg, bg;
            char u[3];
            cur = on_screen && vt.cursor_on && x == vt.x && line - kcount == vt.y && gui_window_active(win) && !ended;
            if (!c) {
                if (chosen || cur)
                    gfx_fill(s, x0 + x * cw, y0 + y * ch, cw, ch, chosen ? GUI_DARK : 0x000000);
                continue;
            }
            rev = !!(c->attr & VT_REVERSE) ^ vt.reverse_screen;
            fg = vt_rgb(rev ? c->bg : c->fg, !rev), bg = vt_rgb(rev ? c->fg : c->bg, rev);
            if ((c->attr & VT_BOLD) && c->fg < 8 && !rev)
                fg = vt_rgb((unsigned short)(c->fg + 8), 1);     /* (bold as the bright colour, as xterm can) */
            if (cur) {
                unsigned t = fg;
                fg = bg, bg = t;
            }
            if (chosen)
                fg = GUI_FIELD, bg = GUI_DARK;
            if (bg != 0xFFFFFF)
                gfx_fill(s, x0 + x * cw, y0 + y * ch, cw, ch, bg);
            if (c->ch == ' ' || (c->attr & VT_HIDDEN))
                continue;
            gfx_text(s, x0 + x * cw, y0 + y * ch, gui_mono, u, utf8_of(c->ch, u), fg);
            if (c->attr & VT_UNDER)
                gfx_fill(s, x0 + x * cw, y0 + y * ch + ch - 1, cw, 1, fg);
        }
        if (on_screen && line - kcount < vt.rows)
            vt.dirty[line - kcount] = 0;
    }
    cursor_y = back ? -1 : vt.y;
    sb.r.x = w->r.x + w->r.w - GUI_SB, sb.r.y = w->r.y, sb.r.w = GUI_SB, sb.r.h = w->r.h;
    sb.total = kcount + rows;
    sb.page = rows;
    sb.pos = first;
    gui_sb_draw(s, &sb, 1);
}

/* ---- copy and paste */

static void copy(void)
{
    char *t;
    int line, n = 0, room = 65536;
    if (!sel_on || !(t = malloc((size_t)room)))
        return;
    for (line = 0; line < kcount + vt.rows && n < room - 3; line++) {
        int c, any = 0;
        for (c = 0; c < cols && n < room - 6; c++)
            if (in_sel(line, c)) {
                const struct vt_cell *cell = cell_at(line, c);
                if (cell)
                    n += utf8_of(cell->ch, t + n);
                else
                    t[n++] = ' ';
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

static void paste(void)
{
    size_t n, i, k = 0;
    char *c = gui_clip_get(&n);
    if (!c)
        return;
    for (i = 0; i < n; i++)                     /* (a line's end is Enter's carriage return) */
        if (c[i] != '\n')
            c[k++] = c[i];
        else if (!i || c[i - 1] != '\r')
            c[k++] = '\r';
    if (vt.bracketed)
        send("\033[200~", 6);
    send(c, (int)k);
    if (vt.bracketed)
        send("\033[201~", 6);
    free(c);
}

/* ---- the mouse and the keyboard */

static int grid_event(struct gui_widget *w, struct gui_ev *e)
{
    int old = back;
    char out[32];
    int n;
    sb.r.x = w->r.x + w->r.w - GUI_SB, sb.r.y = w->r.y, sb.r.w = GUI_SB, sb.r.h = w->r.h;
    sb.total = kcount + rows, sb.page = rows, sb.pos = first_shown();
    if (e->type != GUI_EV_KEY && gui_sb_event(w, &sb, e, 1)) {
        back = kcount - sb.pos;
        if (back < 0)
            back = 0;
        if (back > kcount)
            back = kcount;
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
        back += e->key == GUI_K_PGUP ? rows - 1 : -(rows - 1);
        if (back > kcount)
            back = kcount;
        if (back < 0)
            back = 0;
        if (back != old)
            damage_all();
        return 1;
    }
    if ((e->mods & GLOS_MOD_SHIFT && e->key == GUI_K_INS) || (e->mods == (GLOS_MOD_CTRL | GLOS_MOD_SHIFT) && e->key == 0x2F)) {
        paste();
        return 1;
    }
    if (e->mods == (GLOS_MOD_CTRL | GLOS_MOD_SHIFT) && e->key == 0x2E) {
        copy();
        return 1;
    }
    if (e->key == 0x2A || e->key == 0x36 || e->key == 0x1D || e->key == 0x38 || e->key == 0x11D || e->key == 0x138
        || e->key == 0x3A || e->key == 0x45)
        return 1;                               /* (the modifiers alone are no keys) */
    if (e->mods & GLOS_MOD_ALT)
        return 0;                               /* (the menu's) */
    if (sel_on || back) {
        sel_on = 0;
        back = 0;                               /* (typing is at the screen) */
        damage_all();
    }
    if ((n = vt_key(&vt, e->key, e->ch, e->mods, out, sizeof out)) > 0) {
        send(out, n);
        poll();                                 /* (what it answers at once: the echo) */
    }
    return 1;
}

/* ---- the window */

enum { C_COPY = 1, C_PASTE, C_CLEAR, C_EXIT, C_ABOUT };

static void command(struct gui_window *w, int id)
{
    (void)w;
    switch (id) {
    case C_COPY: copy(); break;
    case C_PASTE: paste(); break;
    case C_CLEAR:                               /* what is kept goes, and the shell clears its screen (Ctrl+L) */
        while (kcount) {
            free(kept[ktop].cell);
            ktop = (ktop + 1) % LINES;
            kcount--;
        }
        back = sel_on = 0;
        discard = 1;
        send("\014", 1);
        damage_all();
        break;
    case C_EXIT: gui_quit(0); break;
    case C_ABOUT:
        gui_message(win, "About Terminal", "Terminal: GLOS's shell. HELP lists its commands.", GUI_MB_OK);
        break;
    }
    gui_focus(grid);
}

static void resized(struct gui_window *w)
{
    (void)w;
    layout_grid();
    if (cols != vt.cols || rows != vt.rows) {
        vt_resize(&vt, cols, rows);
        if (job && !ended)
            glos_job_size(job, (unsigned)cols, (unsigned)rows);
    }
    damage_all();
}

static void tick(void *arg)
{
    (void)arg;
    poll();
}

static void events(const struct glos_ev *ev)
{
    if (ev->kind == GLOS_EV_JOB)
        poll();
}

int main(int argc, char **argv)
{
    struct gui_menubar *bar;
    struct gui_menu *m;
    int i;
    if (gui_init(argc, argv) != 0) {
        printf("TERM no GLOS\n");
        return 1;
    }
    if (argc > 1 && !strcmp(argv[1], "-log")) {
        log_on = 1;
        argv++, argc--;
    }
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
    gui_show(win, 1);
    layout_grid();
    vt_init(&vt, cols, rows);
    vt.gone = line_gone;
    if (!(job = glos_job_shell_in(NULL, "\\", (unsigned)cols, (unsigned)rows))) {      /* (at the boot drive's root) */
        static const char none[] = "The Terminal could not open GLOS's shell.\r\n";
        vt_feed(&vt, (const unsigned char *)none, (int)sizeof none - 1);
        ended = 1;
    }
    if (argc > 1) {                             /* COMMAND: typed into it */
        char line[256] = "";
        for (i = 1; i < argc; i++) {
            strncat(line, argv[i], sizeof line - strlen(line) - 2);
            if (i + 1 < argc)
                strcat(line, " ");
        }
        send(line, (int)strlen(line));
    }
    gui_idle(tick, NULL, 100);
    gui_run();
    if (job)
        glos_close(job);
    if (log_on) {
        static char text[VT_COLS_MAX * VT_ROWS_MAX * 3 + VT_ROWS_MAX + 1];
        char u[VT_COLS_MAX * 3 + 1], *p, *nl;
        int c, n, last = 0, k = 0;
        for (i = 0; i < kcount; i++) {
            const struct kept *l = kept_at(i);
            for (c = 0, n = 0; c < l->n; c++)
                n += utf8_of(l->cell[c].ch, u + n);
            while (n > 0 && u[n - 1] == ' ')
                n--;
            printf("TERM| %.*s\n", n, u);
        }
        vt_dump(&vt, text, sizeof text);        /* ... and the screen, to its last row with anything on it */
        for (p = text; (nl = strchr(p, '\n')) != NULL; p = nl + 1, k++)
            if (nl > p)
                last = k + 1;
        for (p = text, k = 0; k < last && (nl = strchr(p, '\n')) != NULL; p = nl + 1, k++)
            printf("TERM| %.*s\n", (int)(nl - p), p);
    }
    printf("TERM end\n");
    return gui_exit_code();
}
