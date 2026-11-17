/* TERM: GLOS's terminal (PRD §10.6, D50, D51; docs/v1/shell.md, step S2c).
 *
 * A terminal and nothing else: the window shows the toolkit's terminal (an xterm screen; sdk/gui/term.c and
 * vt.c, there since toolkit 2, C8) with a thousand lines kept above it, and the shell at it is GLOS's own, a
 * session in the kernel opened as a job (API 2.1's GLOS_RUN_SHELL). What
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

#define LINES 1000

static struct gui_window *win;
static struct gui_widget *grid;
static glos_handle job;                 /* the shell's session */
static int ended, exit_code, log_on, dump_due, in_poll;
static unsigned changed_at;

/* ---- the session */

static void job_write(const char *d, int n)
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
    static char text[200 * 100 * 3 + 100 + 1];
    char *p, *nl;
    gui_term_dump(grid, text, sizeof text);
    printf("VT-SCREEN %dx%d\n", gui_term_cols(grid), gui_term_rows(grid));
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
    int n, any = 0;
    if (!job || ended)
        return;
    if (glos_job_status(job, &st) != 0)         /* (its state first: once it has ended, what is read is all of it) */
        st.state = GLOS_JS_DONE, st.code = 255;
    in_poll = 1;
    while ((n = glos_job_read(job, GLOS_JOB_OUT, buf, sizeof buf)) > 0) {
        gui_term_feed(grid, buf, n);
        any = 1;
    }
    in_poll = 0;
    if (any) {
        changed_at = glos_ticks();
        dump_due = 1;
    }
    if (st.state == GLOS_JS_DONE) {             /* EXIT, or the session's end: the window's (its state was asked
                                                   first, so what it wrote is all read) */
        ended = 1;
        exit_code = (int)st.code;
        gui_term_end(grid);
        gui_quit(exit_code);
    }
    if (log_on && dump_due && glos_ticks() - changed_at > 1500) {
        dump_due = 0;
        dump_screen();
    }
}

/* ---- the terminal widget's side */

static void term_send(struct gui_widget *w, const char *d, int n, int typed)
{
    (void)w;
    job_write(d, n);
    if (typed && !in_poll)
        poll();                                 /* (what it answers at once: the echo) */
}

static void term_resize(struct gui_widget *w, int cols, int rows)
{
    (void)w;
    if (job && !ended)
        glos_job_size(job, (unsigned)cols, (unsigned)rows);
}

static void term_on(struct gui_widget *w, int what)    /* the title a program set */
{
    char full[160];
    const char *t = gui_term_title(w);
    if (what != GUI_ON_CHANGE)
        return;
    snprintf(full, sizeof full, GUI_T("%s%sTerminal"), t, t[0] ? " - " : "");
    gui_title(win, full);
}

/* ---- the window */

enum { C_COPY = 1, C_PASTE, C_CLEAR, C_EXIT, C_ABOUT };

static void command(struct gui_window *w, int id)
{
    (void)w;
    switch (id) {
    case C_COPY: gui_term_copy(grid); break;
    case C_PASTE: gui_term_paste(grid); break;
    case C_CLEAR:                               /* what is kept goes, and the shell clears its screen (Ctrl+L) */
        gui_term_clear(grid);
        job_write("\014", 1);
        break;
    case C_EXIT: gui_quit(0); break;
    case C_ABOUT:
        gui_message(win, GUI_T("About Terminal"), GUI_T("Terminal: GLOS's shell. HELP lists its commands."), GUI_MB_OK);
        break;
    }
    gui_focus(grid);
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
    static const struct gui_term_cb cb = { term_send, term_resize, NULL };
    struct gui_menubar *bar;
    struct gui_menu *m;
    int i, cols, rows;
    if (gui_init(argc, argv) != 0) {
        printf("TERM no GLOS\n");
        return 1;
    }
    if (argc > 1 && !strcmp(argv[1], "-log")) {
        log_on = 1;
        argv++, argc--;
    }
    win = gui_window(GUI_T("Terminal"), 8 + 80 * 7 + GUI_SB + 4, 25 * 14 + 4, 0);
    win->on_command = command;
    bar = gui_menubar(win);
    m = gui_menu(bar, GUI_T("&Edit"));
    gui_menu_item(m, GUI_T("&Copy"), C_COPY, GUI_T("Ctrl+Shift+C"));
    gui_menu_item(m, GUI_T("&Paste"), C_PASTE, GUI_T("Shift+Ins"));
    gui_menu_item(m, GUI_T("C&lear"), C_CLEAR, NULL);
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("E&xit"), C_EXIT, NULL);
    m = gui_menu(bar, GUI_T("&Help"));
    gui_menu_item(m, GUI_T("&About Terminal"), C_ABOUT, NULL);
    grid = gui_terminal(gui_vbox(win->root, 0, 0), LINES);
    gui_term_callbacks(grid, &cb);
    gui_on(grid, term_on, 0);
    gui_grow(grid->parent, 1);
    gui_grow(grid, 1);
    win->focus = grid;
    gui_on_event(events);
    gui_show(win, 1);
    gui_term_size(grid, &cols, &rows);
    if (!(job = glos_job_shell_in(NULL, "\\", (unsigned)cols, (unsigned)rows))) {      /* (at the boot drive's root) */
        static const char none[] = "The Terminal could not open GLOS's shell.\r\n";
        gui_term_feed(grid, none, (int)sizeof none - 1);
        ended = 1;
        gui_term_end(grid);
    }
    if (argc > 1) {                             /* COMMAND: typed into it */
        char line[256] = "";
        for (i = 1; i < argc; i++) {
            strncat(line, argv[i], sizeof line - strlen(line) - 2);
            if (i + 1 < argc)
                strcat(line, " ");
        }
        job_write(line, (int)strlen(line));
    }
    gui_idle(tick, NULL, 100);
    gui_run();
    if (job)
        glos_close(job);
    if (log_on) {
        static char text[200 * 100 * 3 + 100 + 1];
        char u[200 * 3 + 8], *p, *nl;
        int last = 0, k = 0;
        for (i = 0; i < gui_term_kept(grid); i++) {
            gui_term_line(grid, i, u, sizeof u);
            printf("TERM| %s\n", u);
        }
        gui_term_dump(grid, text, sizeof text); /* ... and the screen, to its last row with anything on it */
        for (p = text; (nl = strchr(p, '\n')) != NULL; p = nl + 1, k++)
            if (nl > p)
                last = k + 1;
        for (p = text, k = 0; k < last && (nl = strchr(p, '\n')) != NULL; p = nl + 1, k++)
            printf("TERM| %.*s\n", (int)(nl - p), p);
    }
    printf("TERM end\n");
    return gui_exit_code();
}
