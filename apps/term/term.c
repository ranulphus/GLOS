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
 * Tabs (phase E, docs/v1/apps/term.md): each tab is a terminal with a shell's session of its own, named by the
 * title its programs set ("Shell 2" until one does). The strip's "+", File's New Tab or Ctrl+Shift+T opens
 * one; a tab's close box, Close Tab or Ctrl+Shift+W closes it and ends its session; EXIT in a shell closes its
 * tab, and the last tab's end is the Terminal's. Eight at most.
 *
 * Saved connections (File's Connections..., Ctrl+Shift+O): a name, a host, a user and a port each, kept in
 * TERM.CFG beside the program. Connect opens a tab and types the SSH command into its shell (SSH.EXE does the
 * rest, its keys and its questions included: nothing secret is kept here).
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
#define TABS 8
#define PAGE_PAD 6                      /* the toolkit's margin round a tab's page, and its frame's line */
#define STRIP_H (gui_font->height + 8 + 2 * PAGE_PAD + 2 + 28) /* what the tabs take of the window's height */

struct tab {
    struct gui_widget *page, *grid;
    glos_handle job;                    /* its shell's session */
    int ended, dump_due, number;
    unsigned changed_at;
};

static struct gui_window *win;
static struct gui_widget *strip;        /* the tabs */
static struct tab tab[TABS];
static int ntabs, made;                 /* made: how many there have been (a new one's number) */
static int exit_code, log_on, in_poll;

static struct tab *tab_of(struct gui_widget *grid)
{
    int i;
    for (i = 0; i < ntabs; i++)
        if (tab[i].grid == grid)
            return &tab[i];
    return NULL;
}

static struct tab *tab_shown(void)
{
    struct gui_widget *p = gui_tabs_page(strip, gui_tabs_get(strip));
    int i;
    for (i = 0; i < ntabs; i++)
        if (tab[i].page == p)
            return &tab[i];
    return ntabs ? &tab[0] : NULL;
}

/* ---- a session */

static void job_write(struct tab *t, const char *d, int n)
{
    while (n > 0 && t->job && !t->ended) {
        int k = glos_job_write(t->job, d, (unsigned)n);
        if (k <= 0)
            break;                              /* (it takes no more now: what is left is lost, as typed ahead) */
        d += k, n -= k;
    }
}

static void dump_screen(struct tab *t)
{
    static char text[200 * 100 * 3 + 100 + 1];
    char *p, *nl;
    gui_term_dump(t->grid, text, sizeof text);
    printf("VT-SCREEN %dx%d\n", gui_term_cols(t->grid), gui_term_rows(t->grid));
    for (p = text; (nl = strchr(p, '\n')) != NULL; p = nl + 1)
        printf("VT| %.*s\n", (int)(nl - p), p);
    printf("VT-END\n");
    fflush(stdout);
}

static void title_now(void)                     /* the window's title: the shown tab's */
{
    struct tab *t = tab_shown();
    char full[160];
    const char *name = t ? gui_get_text(t->page) : "";
    if (ntabs > 1 || (t && gui_term_title(t->grid)[0]))
        snprintf(full, sizeof full, GUI_T("%s - Terminal"), name);
    else
        snprintf(full, sizeof full, "%s", GUI_T("Terminal"));
    gui_title(win, full);
}

/* A tab gone: its session ended (if it hadn't), its page taken out. */
static void tab_drop(struct tab *t)
{
    int i, at = (int)(t - tab);
    if (t->job)
        glos_close(t->job);
    for (i = 0; gui_tabs_page(strip, i); i++)
        if (gui_tabs_page(strip, i) == t->page) {
            gui_tab_remove(strip, i);
            break;
        }
    memmove(&tab[at], &tab[at + 1], (size_t)(ntabs - at - 1) * sizeof tab[0]);
    ntabs--;
    printf("TERM tab closed tabs=%d\n", ntabs);
    fflush(stdout);
    if ((t = tab_shown()) != NULL)
        gui_focus(t->grid);
    title_now();
    gui_relayout(win);
}

/* What the sessions have written, onto their screens; their ends. */
static void poll(void)
{
    unsigned char buf[2048];
    int i;
    for (i = 0; i < ntabs; i++) {
        struct tab *t = &tab[i];
        struct glos_job_status st;
        int n, any = 0;
        if (!t->job || t->ended)
            continue;
        if (glos_job_status(t->job, &st) != 0)  /* (its state first: once it has ended, what is read is all of it) */
            st.state = GLOS_JS_DONE, st.code = 255;
        in_poll = 1;
        while ((n = glos_job_read(t->job, GLOS_JOB_OUT, buf, sizeof buf)) > 0) {
            gui_term_feed(t->grid, buf, n);
            any = 1;
        }
        in_poll = 0;
        if (any) {
            t->changed_at = glos_ticks();
            t->dump_due = 1;
        }
        if (st.state == GLOS_JS_DONE) {         /* EXIT, or the session's end (its state was asked first, so what
                                                   it wrote is all read): the last tab's is the window's ... */
            t->ended = 1;
            if (ntabs == 1) {
                exit_code = (int)st.code;
                gui_term_end(t->grid);
                gui_quit(exit_code);
                return;
            }
            tab_drop(t);                        /* ... another's closes its tab */
            i--;
            continue;
        }
        if (log_on && t->dump_due && glos_ticks() - t->changed_at > 1500) {
            t->dump_due = 0;
            dump_screen(t);
        }
    }
}

/* ---- the terminal widget's side */

static void term_send(struct gui_widget *w, const char *d, int n, int typed)
{
    struct tab *t = tab_of(w);
    if (!t)
        return;
    job_write(t, d, n);
    if (typed && !in_poll)
        poll();                                 /* (what it answers at once: the echo) */
}

static void term_resize(struct gui_widget *w, int cols, int rows)
{
    struct tab *t = tab_of(w);
    if (t && t->job && !t->ended)
        glos_job_size(t->job, (unsigned)cols, (unsigned)rows);
}

static void term_on(struct gui_widget *w, int what)    /* the title a program set: the tab's */
{
    struct tab *t = tab_of(w);
    char name[32];
    if (what != GUI_ON_CHANGE || !t)
        return;
    if (gui_term_title(w)[0])
        gui_set_text(t->page, gui_term_title(w));
    else {
        snprintf(name, sizeof name, GUI_T("Shell %d"), t->number);
        gui_set_text(t->page, name);
    }
    gui_damage(strip);
    title_now();
}

/* A tab more: its terminal, and a shell's session at it. NULL if there are eight, or GLOS has no more. */
static struct tab *tab_new(void)
{
    static const struct gui_term_cb cb = { term_send, term_resize, NULL };
    struct tab *t;
    char name[32];
    if (ntabs == TABS)
        return NULL;
    t = &tab[ntabs];
    memset(t, 0, sizeof *t);
    t->number = ++made;
    snprintf(name, sizeof name, GUI_T("Shell %d"), t->number);
    t->page = gui_tab(strip, name);
    t->grid = gui_terminal(t->page, LINES);
    gui_term_callbacks(t->grid, &cb);
    gui_on(t->grid, term_on, 0);
    gui_grow(t->grid, 1);
    ntabs++;
    return t;
}

static int tab_start(struct tab *t, int cols, int rows)
{
    if (!(t->job = glos_job_shell_in(NULL, "\\", (unsigned)cols, (unsigned)rows))) {   /* (at the boot drive's root) */
        static const char none[] = "The Terminal could not open GLOS's shell.\r\n";
        gui_term_feed(t->grid, none, (int)sizeof none - 1);
        t->ended = 1;
        gui_term_end(t->grid);
        return -1;
    }
    return 0;
}

static void tab_open(void)                      /* (the "+", New Tab) */
{
    struct tab *shown = tab_shown(), *t;
    int cols = 80, rows = 25, i;
    if (shown)
        gui_term_size(shown->grid, &cols, &rows);
    if (!(t = tab_new()))
        return;
    tab_start(t, cols, rows);
    for (i = 0; gui_tabs_page(strip, i); i++)
        if (gui_tabs_page(strip, i) == t->page)
            gui_tabs_set(strip, i);
    gui_relayout(win);
    gui_focus(t->grid);
    title_now();
    printf("TERM tab new tabs=%d\n", ntabs);
    fflush(stdout);
}

static void tab_close(struct tab *t)            /* (a close box, Close Tab) */
{
    if (!t)
        return;
    if (ntabs == 1) {
        gui_quit(0);
        return;
    }
    tab_drop(t);
}

static void strip_on(struct gui_widget *w, int what)
{
    struct tab *t;
    int i;
    if (what == GUI_ON_NEW) {
        tab_open();
    } else if (what == GUI_ON_CLOSE) {
        struct gui_widget *p = gui_tabs_page(w, gui_tabs_closing(w));
        for (i = 0; i < ntabs; i++)
            if (tab[i].page == p) {
                tab_close(&tab[i]);
                break;
            }
    } else if (what == GUI_ON_CHANGE) {
        if ((t = tab_shown()) != NULL)
            gui_focus(t->grid);
        title_now();
    }
}

/* ---- saved connections */

#define CONNS 16

static struct conn {
    char name[24], host[64], user[24];
    int port;                           /* 0: SSH's own (22) */
} conn[CONNS];
static int nconn;
static char home[200];                  /* the program's folder: TERM.CFG is there (none known: nothing is kept) */
static struct gui_window *cdlg;
static struct gui_widget *clist, *cname, *chost, *cuser, *cport;

static void conn_load(void)
{
    char path[240], line[160];
    FILE *f;
    snprintf(path, sizeof path, "%s\\TERM.CFG", home);
    if (!home[0] || !(f = gui_fopen(path, "r")))
        return;
    while (nconn < CONNS && fgets(line, sizeof line, f)) {
        struct conn *c = &conn[nconn];
        char *p[4], *q = line;
        int k;
        line[strcspn(line, "\r\n")] = 0;
        if (strncmp(line, "connection ", 11))
            continue;
        for (k = 0, q = line + 11; k < 4; k++) {
            p[k] = q;
            if ((q = strchr(q, '\t')) != NULL)
                *q++ = 0;
            else
                q = p[k] + strlen(p[k]);
        }
        if (!p[0][0] || !p[1][0])
            continue;
        snprintf(c->name, sizeof c->name, "%s", p[0]);
        snprintf(c->host, sizeof c->host, "%s", p[1]);
        snprintf(c->user, sizeof c->user, "%s", p[2]);
        c->port = atoi(p[3]);
        nconn++;
    }
    fclose(f);
}

static void conn_save(void)
{
    char path[240];
    FILE *f;
    int i;
    snprintf(path, sizeof path, "%s\\TERM.CFG", home);
    if (!home[0] || !(f = gui_fopen(path, "w")))
        return;
    for (i = 0; i < nconn; i++)
        fprintf(f, "connection %s\t%s\t%s\t%d\n", conn[i].name, conn[i].host, conn[i].user, conn[i].port);
    fclose(f);
}

static void conn_fill(void)
{
    int i, keep = gui_list_get(clist);
    gui_list_clear(clist);
    for (i = 0; i < nconn; i++) {
        char where[100];
        const char *cells[2];
        snprintf(where, sizeof where, "%s%s%s", conn[i].user, conn[i].user[0] ? "@" : "", conn[i].host);
        cells[0] = conn[i].name, cells[1] = where;
        gui_list_add(clist, cells, NULL, NULL);
    }
    if (keep >= 0 && keep < nconn)
        gui_list_set(clist, keep);
}

/* A word of a field, as a connection can have it: no spaces, tabs or quotes (it goes into a command line). */
static int conn_word(struct gui_widget *field, char *out, size_t room)
{
    const char *t = gui_get_text(field);
    while (*t == ' ')
        t++;
    snprintf(out, room, "%s", t);
    while (out[0] && out[strlen(out) - 1] == ' ')
        out[strlen(out) - 1] = 0;
    return !strpbrk(out, " \t\"'&|<>");
}

/* The fields as a connection: 0, or -1 with the user told. */
static int conn_read(struct conn *c)
{
    char port[12];
    memset(c, 0, sizeof *c);
    if (!conn_word(cname, c->name, sizeof c->name) || !conn_word(chost, c->host, sizeof c->host)
        || !conn_word(cuser, c->user, sizeof c->user) || !conn_word(cport, port, sizeof port) || !c->host[0]
        || (port[0] && (atoi(port) < 1 || atoi(port) > 65535))) {
        gui_message(cdlg, GUI_T("Connections"), GUI_T("A connection wants a host, and words without spaces; its port is 1 to 65535."),
                    GUI_MB_OK);
        return -1;
    }
    c->port = atoi(port);
    if (!c->name[0])
        snprintf(c->name, sizeof c->name, "%.23s", c->host);
    return 0;
}

static void tab_open(void);

static void conn_button(struct gui_widget *b, int what)
{
    struct conn c;
    int i, r = gui_list_get(clist);
    (void)what;
    if (b->id == 11) {                          /* Save: a new one, or the one of that name again */
        if (conn_read(&c) != 0)
            return;
        for (i = 0; i < nconn && strcmp(conn[i].name, c.name); i++)
            ;
        if (i == CONNS)
            return;
        conn[i] = c;
        if (i == nconn)
            nconn++;
        conn_save();
        conn_fill();
        gui_list_set(clist, i);
        printf("TERM connection saved %s\n", c.name);
        fflush(stdout);
    } else if (b->id == 12) {                   /* Remove */
        if (r < 0 || r >= nconn)
            return;
        memmove(&conn[r], &conn[r + 1], (size_t)(nconn - r - 1) * sizeof conn[0]);
        nconn--;
        conn_save();
        conn_fill();
    } else if (b->id == 10) {                   /* Connect: what the fields say, saved or not */
        if (conn_read(&c) != 0)
            return;
        conn[CONNS - 1 >= nconn ? nconn : CONNS - 1] = c;      /* (kept past the dialog's end, beyond the saved ones) */
        gui_end_modal(cdlg, 10);
    } else {
        gui_end_modal(cdlg, 0);
    }
}

static void conn_pick(struct gui_widget *w, int what)
{
    int r = gui_list_get(w);
    char port[12];
    (void)what;
    if (r < 0 || r >= nconn)
        return;
    gui_set_text(cname, conn[r].name);
    gui_set_text(chost, conn[r].host);
    gui_set_text(cuser, conn[r].user);
    snprintf(port, sizeof port, "%d", conn[r].port);
    gui_set_text(cport, conn[r].port ? port : "");
}

static void conn_dialog(void)
{
    static const char *const heads[2] = { GUI_N("Name"), GUI_N("Where") };
    static const int widths[2] = { 100, 190 };
    struct gui_widget *box, *form, *row;
    struct tab *t;
    char line[160];
    int r;
    cdlg = gui_window(GUI_T("Connections"), 330, 300, GUI_W_DIALOG);
    cdlg->owner = win;
    box = gui_vbox(cdlg->root, 6, 8);
    gui_grow(box, 1);
    clist = gui_list(box, 2, heads, widths);
    gui_grow(clist, 1);
    gui_on(clist, conn_pick, 0);
    form = gui_form(box);
    cname = gui_field(gui_row(form, GUI_T("&Name:"), NULL), 22);
    chost = gui_field(gui_row(form, GUI_T("&Host:"), NULL), 30);
    cuser = gui_field(gui_row(form, GUI_T("&User:"), NULL), 22);
    cport = gui_field(gui_row(form, GUI_T("&Port:"), NULL), 6);
    row = gui_hbox(box, 6, 0);
    gui_button(row, GUI_T("&Connect"), conn_button, 10)->flags |= GUI_DEFAULT;
    gui_button(row, GUI_T("&Save"), conn_button, 11);
    gui_button(row, GUI_T("&Remove"), conn_button, 12);
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("Close"), conn_button, 13)->flags |= GUI_CANCEL;
    conn_fill();
    gui_focus(cname);
    r = gui_modal(cdlg);                        /* (which closes it) */
    cdlg = NULL;
    if (r != 10)
        return;
    {                                           /* a tab, and the SSH command typed into its shell */
        const struct conn *c = &conn[CONNS - 1 >= nconn ? nconn : CONNS - 1];
        char port[16] = "";
        if (c->port)
            snprintf(port, sizeof port, "-p %d ", c->port);
        snprintf(line, sizeof line, "ssh %s%s%s%s\r", port, c->user, c->user[0] ? "@" : "", c->host);
        tab_open();
        if ((t = tab_shown()) != NULL)
            job_write(t, line, (int)strlen(line));
        printf("TERM connect %s\n", c->name);
        fflush(stdout);
    }
}

/* ---- the window */

/* -log's last words: every line the first tab there still is has kept, and its screen to its last row with
   anything on it. Once: when the window is closed (its widgets go with it), or after the loop's end. */
static void log_end(void)
{
    static char text[200 * 100 * 3 + 100 + 1];
    static int done;
    char u[200 * 3 + 8], *p, *nl;
    struct tab *t = &tab[0];
    int last = 0, k = 0, i;
    if (!log_on || !ntabs || done)
        return;
    done = 1;
    for (i = 0; i < gui_term_kept(t->grid); i++) {
        gui_term_line(t->grid, i, u, sizeof u);
        printf("TERM| %s\n", u);
    }
    gui_term_dump(t->grid, text, sizeof text);
    for (p = text; (nl = strchr(p, '\n')) != NULL; p = nl + 1, k++)
        if (nl > p)
            last = k + 1;
    for (p = text, k = 0; k < last && (nl = strchr(p, '\n')) != NULL; p = nl + 1, k++)
        printf("TERM| %.*s\n", (int)(nl - p), p);
    fflush(stdout);
}

static void closing(struct gui_window *w)       /* (the window's close box, Alt+F4) */
{
    (void)w;
    log_end();
    gui_quit(0);
}

enum { C_COPY = 1, C_PASTE, C_CLEAR, C_EXIT, C_ABOUT, C_NEW, C_CLOSE, C_CONNS };

static void command(struct gui_window *w, int id)
{
    struct tab *t = tab_shown();
    (void)w;
    switch (id) {
    case C_NEW: tab_open(); return;
    case C_CLOSE: tab_close(t); return;
    case C_CONNS: conn_dialog(); return;
    case C_COPY:
        if (t)
            gui_term_copy(t->grid);
        break;
    case C_PASTE:
        if (t)
            gui_term_paste(t->grid);
        break;
    case C_CLEAR:                               /* what is kept goes, and the shell clears its screen (Ctrl+L) */
        if (t) {
            gui_term_clear(t->grid);
            job_write(t, "\014", 1);
        }
        break;
    case C_EXIT: gui_quit(0); break;
    case C_ABOUT:
        gui_message(win, GUI_T("About Terminal"), GUI_T("Terminal: GLOS's shell. HELP lists its commands."), GUI_MB_OK);
        break;
    }
    if ((t = tab_shown()) != NULL)
        gui_focus(t->grid);
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
    struct tab *t;
    int i, cols, rows;
    if (gui_init(argc, argv) != 0) {
        printf("TERM no GLOS\n");
        return 1;
    }
    {                                           /* its own folder, as DOS writes it: TERM.CFG's place */
        char *q;
        snprintf(home, sizeof home, "%s", argv[0]);
        for (q = home; *q; q++)
            *q = *q == '/' ? '\\' : (*q >= 'a' && *q <= 'z') ? (char)(*q - 32) : *q;
        if ((q = strrchr(home, '\\')) != NULL)
            *q = 0;
        else
            home[0] = 0;
    }
    conn_load();
    if (argc > 1 && !strcmp(argv[1], "-log")) {
        log_on = 1;
        argv++, argc--;
    }
    win = gui_window(GUI_T("Terminal"), 8 + 80 * 7 + GUI_SB + 4 + 2 * PAGE_PAD + 2, 25 * 14 + 4 + STRIP_H, 0);
    gui_window_icon(win, "app-terminal");
    win->on_command = command;
    win->on_close = closing;
    bar = gui_menubar(win);
    m = gui_menu(bar, GUI_T("&File"));
    gui_menu_item(m, GUI_T("New &Tab"), C_NEW, GUI_T("Ctrl+Shift+T"));
    gui_menu_item(m, GUI_T("&Close Tab"), C_CLOSE, GUI_T("Ctrl+Shift+W"));
    gui_menu_item(m, GUI_T("C&onnections..."), C_CONNS, GUI_T("Ctrl+Shift+O"));
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("E&xit"), C_EXIT, NULL);
    m = gui_menu(bar, GUI_T("&Edit"));
    gui_menu_item(m, GUI_T("&Copy"), C_COPY, GUI_T("Ctrl+Shift+C"));
    gui_menu_item(m, GUI_T("&Paste"), C_PASTE, GUI_T("Shift+Ins"));
    gui_menu_item(m, GUI_T("C&lear"), C_CLEAR, NULL);
    m = gui_menu(bar, GUI_T("&Help"));
    gui_menu_item(m, GUI_T("&About Terminal"), C_ABOUT, NULL);
    strip = gui_tabs(gui_vbox(win->root, 0, 0));
    gui_tabs_style(strip, GUI_TABS_SCROLL);
    gui_tabs_flags(strip, GUI_TABS_CLOSE | GUI_TABS_NEW);
    gui_on(strip, strip_on, 0);
    gui_grow(strip->parent, 1);
    gui_grow(strip, 1);
    t = tab_new();
    win->focus = t->grid;
    gui_on_event(events);
    gui_show(win, 1);
    gui_term_size(t->grid, &cols, &rows);
    tab_start(t, cols, rows);
    if (log_on) {
        printf("TERM size %dx%d\n", cols, rows);
        fflush(stdout);
    }
    if (argc > 1) {                             /* COMMAND: typed into it */
        char line[256] = "";
        for (i = 1; i < argc; i++) {
            strncat(line, argv[i], sizeof line - strlen(line) - 2);
            if (i + 1 < argc)
                strcat(line, " ");
        }
        job_write(t, line, (int)strlen(line));
    }
    gui_idle(tick, NULL, 100);
    gui_run();
    for (i = 0; i < ntabs; i++)
        if (tab[i].job)
            glos_close(tab[i].job);
    log_end();
    printf("TERM end\n");
    return gui_exit_code();
}
