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
 * Saved sessions (File's Sessions..., Ctrl+Shift+O; phase F, the owner's choice of 2026-11-20: saved
 * connections and their settings as PuTTY has them): a list of sessions, each a computer to log in to (its
 * host, port and user, a key's file, a command to run there, a keep-alive, a file its output is logged to), its
 * tunnels, and the look of its tab (colours, the text's size, the lines kept). They are SESSIONS.CFG beside SSH's key
 * (apps/shared/sessions.h), which `ssh NAME` reads too. Open makes a tab with that look and types the SSH
 * command into its shell: `ssh NAME` for a session as it is saved, the whole command for one that is not
 * (SSH.EXE does the rest, its keys and its questions included: nothing secret is kept here).
 *
 *   TERM [-log] [-session NAME] [COMMAND]
 *                      a terminal; COMMAND typed into it first. -session: its first tab is that saved
 *                      session's. -log: on stdout, the screen whenever it has been still for a moment
 *                      ("VT| ...") and, at the end, every line kept and the screen ("TERM| ...": the suite's) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/gui.h>

#include "../shared/sessions.h"

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

/* A saved session's colours: the text and the ground that its programs leave as they are (0: the theme's). */
static const struct { unsigned text, back; } scheme[SESSION_COLOURS] = {
    { 0, 0 }, { 0x000000, 0xFFFFFF }, { 0xE5E5E5, 0x000000 }, { 0x33FF66, 0x001400 }, { 0xFFB000, 0x180C00 },
    { 0xFFFFFF, 0x0000AA },
};

static void tab_look(struct tab *t, int colours, int font)
{
    if (colours > 0 && colours < SESSION_COLOURS)
        gui_term_colours(t->grid, 1, scheme[colours].text, scheme[colours].back);
    if (font >= 1 && font <= 3)                 /* (a size FONTS.DAT hasn't: the UI's stays) */
        gui_term_font(t->grid, gui_font_get(GUI_FONT_MONO, font - 1));
}

/* A tab more: its terminal (so many lines kept above its screen; 0: the Terminal's thousand), and a shell's
   session at it. NULL if there are eight, or GLOS has no more. */
static struct tab *tab_new(int lines)
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
    t->grid = gui_terminal(t->page, lines > 0 ? lines : LINES);
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

/* A new tab, shown: with a saved session's look (k), or the Terminal's own (NULL: the "+", New Tab). */
static struct tab *tab_open_as(const struct session *k)
{
    struct tab *shown = tab_shown(), *t;
    int cols = 80, rows = 25, i;
    if (shown)
        gui_term_size(shown->grid, &cols, &rows);
    if (!(t = tab_new(k ? k->lines : 0)))
        return NULL;
    if (k)
        tab_look(t, k->colours, k->font);
    for (i = 0; gui_tabs_page(strip, i); i++)
        if (gui_tabs_page(strip, i) == t->page)
            gui_tabs_set(strip, i);
    gui_relayout(win);
    if (k && k->font)                           /* (text of another size: its own columns and rows) */
        gui_term_size(t->grid, &cols, &rows);
    tab_start(t, cols, rows);
    gui_focus(t->grid);
    title_now();
    printf("TERM tab new tabs=%d\n", ntabs);
    fflush(stdout);
    return t;
}

static void tab_open(void) { tab_open_as(NULL); }

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

/* ---- saved sessions (apps/shared/sessions.h): the list, and the dialog that keeps it */

static struct session sess[SESSIONS_MAX];
static int nsess;
static char home[200];                  /* the program's folder */
static char sess_dir[240], sess_file[300];      /* SSH's folder and SESSIONS.CFG in it ("": nowhere to keep them) */
static struct gui_window *sdlg;
static struct gui_widget *slist, *spages, *sname, *shost, *sport, *suser, *skey, *scmd, *salive, *slog, *tlist, *tport,
                         *tto, *topen, *scolours, *sfont, *slines;
static char tunnels_now[sizeof sess[0].tunnels];        /* the tunnels of the session the fields show */

/* TERM.CFG's connections (phase E's: a name, a host, a user and a port each, tabs between) taken over once,
   where there are no sessions yet. */
static void sess_import(void)
{
    char path[240], line[160];
    FILE *f;
    snprintf(path, sizeof path, "%s\\TERM.CFG", home);
    if (!(f = gui_fopen(path, "r")))
        return;
    while (nsess < SESSIONS_MAX && fgets(line, sizeof line, f)) {
        struct session *k = &sess[nsess];
        char *p[4], *q;
        int n;
        line[strcspn(line, "\r\n")] = 0;
        if (strncmp(line, "connection ", 11))
            continue;
        for (n = 0, q = line + 11; n < 4; n++) {
            p[n] = q;
            if ((q = strchr(q, '\t')) != NULL)
                *q++ = 0;
            else
                q = p[n] + strlen(p[n]);
        }
        if (!session_name_ok(p[0]) || !p[1][0] || sessions_find(sess, nsess, p[0]) >= 0)
            continue;
        session_defaults(k);
        snprintf(k->name, sizeof k->name, "%s", p[0]);
        snprintf(k->host, sizeof k->host, "%s", p[1]);
        snprintf(k->user, sizeof k->user, "%s", p[2]);
        k->port = atoi(p[3]) > 0 && atoi(p[3]) < 65536 ? atoi(p[3]) : 0;
        nsess++;
    }
    fclose(f);
}

static void sess_store(void)
{
    if (!sess_file[0])
        return;
    gui_mkdir(sess_dir);                        /* (SSH's folder, if no key has made it yet) */
    if (sessions_save(sess_file, sess, nsess) != 0 && sdlg)
        gui_message(sdlg, GUI_T("Sessions"), GUI_T("The sessions could not be written to their file."), GUI_MB_OK);
}

static void sess_start(void)
{
    const char *e = getenv("GLOSSSH");
    if (e && *e)
        snprintf(sess_dir, sizeof sess_dir, "%s", e);
    else if (home[0])
        snprintf(sess_dir, sizeof sess_dir, "%s\\SSH", home);
    else
        return;
    sessions_files(gui_fopen);
    sessions_path(sess_file, sizeof sess_file, sess_dir);
    nsess = sessions_load(sess_file, sess, SESSIONS_MAX);
    if (!nsess && home[0]) {
        sess_import();
        if (nsess)
            sess_store();
    }
}

static void sess_fill(int keep)
{
    int i;
    gui_list_clear(slist);
    for (i = 0; i < nsess; i++) {
        char where[100];
        const char *cells[2];
        snprintf(where, sizeof where, "%s%s%s", sess[i].user, sess[i].user[0] ? "@" : "", sess[i].host);
        cells[0] = sess[i].name, cells[1] = where;
        gui_list_add(slist, cells, NULL, NULL);
    }
    if (keep >= 0 && keep < nsess)
        gui_list_set(slist, keep);
}

static void tun_fill(void)                      /* the tunnels' list, from their line */
{
    const char *p = tunnels_now, *e;
    gui_list_clear(tlist);
    while (*p) {
        char host[80], port[12], to[100];
        const char *cells[3];
        unsigned lp = 0, hp = 0;
        int open = -1;
        while (*p == ' ')
            p++;
        for (e = p; *e && *e != ' '; e++)
            ;
        if (e > p && session_tunnel(p, (int)(e - p), &open, &lp, host, sizeof host, &hp) == 0) {
            snprintf(port, sizeof port, "%u", lp);
            snprintf(to, sizeof to, "%s:%u", host, hp);
            cells[0] = port, cells[1] = to, cells[2] = open == 1 ? GUI_T("Every computer") : GUI_T("This PC");
            gui_list_add(tlist, cells, NULL, NULL);
        }
        p = e;
    }
}

static void sess_show(const struct session *k)  /* a session into the fields */
{
    char n[16];
    gui_set_text(sname, k->name);
    gui_set_text(shost, k->host);
    snprintf(n, sizeof n, "%d", k->port);
    gui_set_text(sport, k->port ? n : "");
    gui_set_text(suser, k->user);
    gui_set_text(skey, k->key);
    gui_set_text(scmd, k->command);
    snprintf(n, sizeof n, "%d", k->keepalive);
    gui_set_text(salive, k->keepalive ? n : "");
    gui_set_text(slog, k->log);
    snprintf(tunnels_now, sizeof tunnels_now, "%s", k->tunnels);
    tun_fill();
    gui_set_text(tport, "");
    gui_set_text(tto, "");
    gui_set_checked(topen, 0);
    gui_set_value(scolours, k->colours);
    gui_set_value(sfont, k->font);
    snprintf(n, sizeof n, "%d", k->lines);
    gui_set_text(slines, k->lines ? n : "");
}

/* A field's text without the blanks at its ends: 1 if it is a word (nothing a command line would take apart). */
static int sess_word(struct gui_widget *field, char *out, size_t room, int word)
{
    const char *t = gui_get_text(field);
    while (*t == ' ')
        t++;
    snprintf(out, room, "%s", t);
    while (out[0] && out[strlen(out) - 1] == ' ')
        out[strlen(out) - 1] = 0;
    return !strpbrk(out, word ? " \t\"'&|<>" : "\t\r\n");
}

static int sess_number(struct gui_widget *field, int low, int high, int *out)   /* empty: 0 */
{
    char t[16], *end;
    long v;
    if (!sess_word(field, t, sizeof t, 1))
        return 0;
    *out = 0;
    if (!t[0])
        return 1;
    v = strtol(t, &end, 10);
    if (*end || v < low || v > high)
        return 0;
    *out = (int)v;
    return 1;
}

/* The fields as a session: 0, or -1 with the user told what is wrong. named: it must have a name (Save). */
static int sess_read(struct session *k, int named)
{
    const char *wrong = NULL;
    session_defaults(k);
    if (!sess_word(sname, k->name, sizeof k->name, 1) || (k->name[0] && !session_name_ok(k->name)) || (named && !k->name[0]))
        wrong = GUI_T("A session's name is a word of up to 23 characters, without @ : \\ or /.");
    else if (!sess_word(shost, k->host, sizeof k->host, 1) || !k->host[0])
        wrong = GUI_T("A session wants a host: a computer's name or its address.");
    else if (!sess_number(sport, 1, 65535, &k->port))
        wrong = GUI_T("The port is a number from 1 to 65535 (none: SSH's own, 22).");
    else if (!sess_word(suser, k->user, sizeof k->user, 1))
        wrong = GUI_T("The user is a word without spaces.");
    else if (!sess_word(skey, k->key, sizeof k->key, 1))
        wrong = GUI_T("The key's file is a path without spaces.");
    else if (!sess_word(scmd, k->command, sizeof k->command, 0))
        wrong = GUI_T("The command is one line.");
    else if (!sess_number(salive, 1, 3600, &k->keepalive))
        wrong = GUI_T("Keep alive is a number of seconds up to 3600 (none: no such packets).");
    else if (!sess_word(slog, k->log, sizeof k->log, 1))
        wrong = GUI_T("The log's file is a path without spaces.");
    else if (!sess_number(slines, 100, 20000, &k->lines))
        wrong = GUI_T("The lines kept are 100 to 20000 (none: 1000).");
    if (wrong) {
        printf("TERM session wrong\n");
        fflush(stdout);
        gui_message(sdlg, GUI_T("Sessions"), wrong, GUI_MB_OK);
        return -1;
    }
    snprintf(k->tunnels, sizeof k->tunnels, "%s", tunnels_now);
    k->colours = gui_value(scolours), k->font = gui_value(sfont);
    return 0;
}

/* SSH's program as a shell is told to run it: SSH.EXE beside the Terminal by its whole path (a tab's shell
   begins at the boot drive's root, and nothing says that the programs' folder is on its PATH); with no folder
   known, the word alone. */
static int ssh_word(char *line, size_t room)
{
    return home[0] ? snprintf(line, room, "%s\\SSH.EXE", home) : snprintf(line, room, "ssh");
}

/* The SSH command a session is opened by: its name alone where it is saved just so, else all of it in words. */
static void sess_command(const struct session *k, char *line, size_t room)
{
    int i = k->name[0] ? sessions_find(sess, nsess, k->name) : -1;
    size_t n = (size_t)ssh_word(line, room);
    const char *p, *e;
    if (n >= room)
        return;
    if (i >= 0 && session_same(&sess[i], k)) {
        snprintf(line + n, room - n, " %s\r", sess[i].name);
        return;
    }
    if (k->port && n < room)
        n += (size_t)snprintf(line + n, room - n, " -p %d", k->port);
    if (k->key[0] && n < room)
        n += (size_t)snprintf(line + n, room - n, " -i %s", k->key);
    if (k->keepalive && n < room)
        n += (size_t)snprintf(line + n, room - n, " -o ServerAliveInterval=%d", k->keepalive);
    if (k->log[0] && n < room)
        n += (size_t)snprintf(line + n, room - n, " -sessionlog %s", k->log);
    for (p = k->tunnels; *p && n < room; p = e) {
        while (*p == ' ')
            p++;
        for (e = p; *e && *e != ' '; e++)
            ;
        if (e > p)
            n += (size_t)snprintf(line + n, room - n, " -L %.*s", (int)(e - p), p);
    }
    if (n < room)
        n += (size_t)snprintf(line + n, room - n, " %s%s%s", k->user, k->user[0] ? "@" : "", k->host);
    if (k->command[0] && n < room)
        n += (size_t)snprintf(line + n, room - n, " %s", k->command);
    if (n < room)
        snprintf(line + n, room - n, "\r");
}

/* A session opened: a tab with its look, and its SSH command typed into the tab's shell. */
static void sess_open(const struct session *k)
{
    char line[700];
    struct tab *t = tab_open_as(k);
    if (!t)
        return;
    sess_command(k, line, sizeof line);
    job_write(t, line, (int)strlen(line));
    printf("TERM connect %s\n", k->name[0] ? k->name : k->host);
    fflush(stdout);
}

static struct session opened;                   /* what Open took from the fields, for after the dialog */

static void sess_button(struct gui_widget *b, int what)
{
    struct session k;
    int i, r = gui_list_get(slist);
    (void)what;
    switch (b->id) {
    case 10:                                    /* Open: what the fields say, saved or not */
        if (sess_read(&k, 0) != 0)
            return;
        opened = k;
        gui_end_modal(sdlg, 10);
        return;
    case 11:                                    /* Save: a new one, or the one of that name again */
        if (sess_read(&k, 1) != 0)
            return;
        if ((i = sessions_find(sess, nsess, k.name)) < 0) {
            if (nsess == SESSIONS_MAX) {
                gui_message(sdlg, GUI_T("Sessions"), GUI_T("There is no room for one more session."), GUI_MB_OK);
                return;
            }
            i = nsess++;
        }
        sess[i] = k;
        sess_store();
        sess_fill(i);
        printf("TERM session saved %s tunnels=%d colours=%d font=%d\n", k.name, session_tunnels_ok(k.tunnels), k.colours, k.font);
        fflush(stdout);
        return;
    case 12:                                    /* Remove */
        if (r < 0 || r >= nsess)
            return;
        printf("TERM session removed %s\n", sess[r].name);
        fflush(stdout);
        memmove(&sess[r], &sess[r + 1], (size_t)(nsess - r - 1) * sizeof sess[0]);
        nsess--;
        sess_store();
        sess_fill(r < nsess ? r : nsess - 1);
        return;
    case 14:                                    /* New: empty fields */
        session_defaults(&k);
        gui_list_set(slist, -1);
        sess_show(&k);
        gui_tabs_set(spages, 0);
        gui_focus(sname);
        return;
    case 15: {                                  /* Browse: the key's file */
        char path[260];
        snprintf(path, sizeof path, "%s", gui_get_text(skey)[0] ? gui_get_text(skey) : sess_dir);
        if (gui_file_dialog(sdlg, GUI_T("A Private Key"), 0, path, sizeof path) == 1)
            gui_set_text(skey, path);
        return;
    }
    case 18: {                                  /* ... and the log's (one to make, or one to add to) */
        char path[260];
        snprintf(path, sizeof path, "%s", gui_get_text(slog));
        if (gui_file_dialog(sdlg, GUI_T("The Session's Log"), 1, path, sizeof path) == 1)
            gui_set_text(slog, path);
        return;
    }
    case 16: {                                  /* a tunnel added: PORT, and HOST:PORT it goes to */
        char port[16], to[100], spec[130];
        if (!sess_word(tport, port, sizeof port, 1) || !sess_word(tto, to, sizeof to, 1))
            port[0] = 0;
        snprintf(spec, sizeof spec, "%s%s:%s", gui_checked(topen) ? "*:" : "", port, to);
        if (!port[0] || session_tunnel(spec, -1, 0, 0, 0, 0, 0) != 0) {
            gui_message(sdlg, GUI_T("Sessions"), GUI_T("A tunnel wants a port of this PC (1 to 65535), and where it goes: a "
                        "host and its port, as intranet:80."), GUI_MB_OK);
            return;
        }
        if (session_tunnels_ok(tunnels_now) >= SESSION_TUNNELS || strlen(tunnels_now) + strlen(spec) + 2 > sizeof tunnels_now) {
            gui_message(sdlg, GUI_T("Sessions"), GUI_T("A session has four tunnels at most."), GUI_MB_OK);
            return;
        }
        if (tunnels_now[0])
            strcat(tunnels_now, " ");
        strcat(tunnels_now, spec);
        tun_fill();
        gui_set_text(tport, "");
        gui_set_text(tto, "");
        gui_set_checked(topen, 0);
        gui_focus(tport);
        return;
    }
    case 17: {                                  /* the chosen tunnel removed: its words out of the line */
        char left[sizeof tunnels_now] = "";
        const char *p = tunnels_now, *e;
        int n = 0, row = gui_list_get(tlist);
        if (row < 0)
            return;
        while (*p) {
            while (*p == ' ')
                p++;
            for (e = p; *e && *e != ' '; e++)
                ;
            if (e > p && n++ != row)
                snprintf(left + strlen(left), sizeof left - strlen(left), "%s%.*s", left[0] ? " " : "", (int)(e - p), p);
            p = e;
        }
        strcpy(tunnels_now, left);
        tun_fill();
        return;
    }
    default:
        gui_end_modal(sdlg, 0);
    }
}

static void sess_pick(struct gui_widget *w, int what)
{
    int r = gui_list_get(w);
    if (r < 0 || r >= nsess)
        return;
    sess_show(&sess[r]);
    if (what == GUI_ON_ACTIVATE) {              /* (a double click, Enter on the list: opened) */
        opened = sess[r];
        gui_end_modal(sdlg, 10);
    }
}

static void sess_dialog(void)
{
    static const char *const heads[2] = { GUI_N("Name"), GUI_N("Where") };
    static const int widths[2] = { 84, 110 };
    static const char *const theads[3] = { GUI_N("Port"), GUI_N("Goes to"), GUI_N("For") };
    static const int twidths[3] = { 50, 150, 96 };
    struct gui_widget *box, *cols, *left, *page, *form, *row;
    struct session none;
    int r, i;
    sdlg = gui_window(GUI_T("Sessions"), 580, 330, GUI_W_DIALOG);
    sdlg->owner = win;
    box = gui_vbox(sdlg->root, 8, 8);
    gui_grow(box, 1);
    cols = gui_hbox(box, 8, 0);
    gui_grow(cols, 1);
    left = gui_vbox(cols, 6, 0);
    gui_label(left, GUI_T("Sa&ved sessions:"));
    slist = gui_list(left, 2, heads, widths);
    gui_grow(slist, 1);
    gui_on(slist, sess_pick, 0);
    row = gui_hbox(left, 6, 0);
    gui_button(row, GUI_T("Ne&w"), sess_button, 14);
    gui_button(row, GUI_T("&Remove"), sess_button, 12);
    spages = gui_tabs(cols);
    gui_grow(spages, 1);
    page = gui_tab(spages, GUI_T("Computer"));
    form = gui_form(page);
    sname = gui_field(gui_row(form, GUI_T("&Name:"), NULL), 22);
    shost = gui_field(gui_row(form, GUI_T("&Host:"), NULL), 30);
    sport = gui_field(gui_row(form, GUI_T("&Port:"), NULL), 6);
    suser = gui_field(gui_row(form, GUI_T("&User:"), NULL), 22);
    row = gui_row(form, GUI_T("&Key file:"), NULL);
    skey = gui_field(row, 22);
    gui_button(row, GUI_T("&Browse..."), sess_button, 15);
    scmd = gui_field(gui_row(form, GUI_T("Co&mmand:"), NULL), 30);
    row = gui_row(form, GUI_T("Keep &alive:"), NULL);
    salive = gui_field(row, 6);
    gui_label(row, GUI_T("seconds"));
    row = gui_row(form, GUI_T("Log &file:"), NULL);
    slog = gui_field(row, 22);
    gui_button(row, GUI_T("Browse..."), sess_button, 18);
    page = gui_tab(spages, GUI_T("Tunnels"));
    tlist = gui_list(page, 3, theads, twidths);
    gui_grow(tlist, 1);
    form = gui_form(page);
    tport = gui_field(gui_row(form, GUI_T("&This PC's port:"), NULL), 6);
    tto = gui_field(gui_row(form, GUI_T("&Goes to:"), NULL), 28);
    topen = gui_check(gui_row(form, "", NULL), GUI_T("Other computers ma&y use it"), 0);
    row = gui_hbox(page, 6, 0);
    gui_button(row, GUI_T("A&dd"), sess_button, 16);
    gui_button(row, GUI_T("Remov&e"), sess_button, 17);
    page = gui_tab(spages, GUI_T("Look"));
    form = gui_form(page);
    scolours = gui_choice(gui_row(form, GUI_T("Co&lours:"), NULL), 18);
    for (i = 0; i < SESSION_COLOURS; i++)
        gui_choice_add(scolours, GUI_T(session_colour_names[i]));
    sfont = gui_choice(gui_row(form, GUI_T("Te&xt:"), NULL), 18);
    gui_choice_add(sfont, GUI_T("The theme's size"));
    gui_choice_add(sfont, GUI_T("Small"));
    gui_choice_add(sfont, GUI_T("Normal"));
    gui_choice_add(sfont, GUI_T("Large"));
    row = gui_row(form, GUI_T("L&ines kept:"), NULL);
    slines = gui_field(row, 6);
    gui_label(row, GUI_T("(1000 if none)"));
    row = gui_hbox(box, 6, 0);
    gui_button(row, GUI_T("&Open"), sess_button, 10)->flags |= GUI_DEFAULT;
    gui_button(row, GUI_T("&Save"), sess_button, 11);
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("Close"), sess_button, 13)->flags |= GUI_CANCEL;
    session_defaults(&none);
    sess_show(&none);
    sess_fill(-1);
    gui_focus(sname);
    r = gui_modal(sdlg);                        /* (which closes it) */
    sdlg = NULL;
    if (r == 10)
        sess_open(&opened);
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

enum { C_COPY = 1, C_PASTE, C_CLEAR, C_EXIT, C_ABOUT, C_NEW, C_CLOSE, C_CONNS, C_FIND, C_FIND_NEXT, C_FIND_PREV };

/* ---- Find (phase E): text looked for in what the tab shown has kept and shows, with the toolkit's own dialog
   (which stays up beside the terminal: Find next and previous, Match case, Whole words, Regular expressions) and
   the terminal widget's finding (gui_term_find): each one found is chosen (Ctrl+Shift+C copies it) and scrolled
   to. Next goes down the lines, Previous up: from the bottom of what shows, the newest first. */
static struct gui_find fr;

static void find_step(int back)
{
    struct tab *t = tab_shown();
    char say[200];
    int r;
    if (!t || !fr.what[0])
        return;
    r = gui_term_find(t->grid, fr.what, fr.flags, back);
    snprintf(say, sizeof say, r == 1 ? "" : r < 0 ? GUI_T("That expression is wrong.") : GUI_T("\"%s\" isn't there."), fr.what);
    if (fr.w)
        gui_find_say(&fr, say);
    else if (r != 1)
        gui_message(win, GUI_T("Terminal"), say, GUI_MB_OK);
    printf("TERM find %s %s\n", fr.what, r == 1 ? "found" : r < 0 ? "wrong" : "none");
    fflush(stdout);
}

static void find_on(struct gui_find *f, int todo)
{
    (void)f;
    if (todo == GUI_FIND_NEXT || todo == GUI_FIND_PREV)
        find_step(todo == GUI_FIND_PREV);
}

static void command(struct gui_window *w, int id)
{
    struct tab *t = tab_shown();
    (void)w;
    switch (id) {
    case C_FIND:
        fr.replace = 0;
        fr.on_find = find_on;
        gui_find(win, &fr);
        return;
    case C_FIND_NEXT: find_step(0); return;
    case C_FIND_PREV: find_step(1); return;
    case C_NEW: tab_open(); return;
    case C_CLOSE: tab_close(t); return;
    case C_CONNS: sess_dialog(); return;
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
    int i, cols, rows, first = -1;
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
    sess_start();
    if (argc > 1 && !strcmp(argv[1], "-log")) {
        log_on = 1;
        argv++, argc--;
    }
    if (argc > 2 && !strcmp(argv[1], "-session")) {     /* its first tab: that saved session's */
        if ((first = sessions_find(sess, nsess, argv[2])) < 0)
            printf("TERM no session %s\n", argv[2]);
        argv += 2, argc -= 2;
    }
    win = gui_window(GUI_T("Terminal"), 8 + 80 * 7 + GUI_SB + 4 + 2 * PAGE_PAD + 2, 25 * 14 + 4 + STRIP_H, 0);
    gui_window_icon(win, "app-terminal");
    win->on_command = command;
    win->on_close = closing;
    bar = gui_menubar(win);
    m = gui_menu(bar, GUI_T("&File"));
    gui_menu_item(m, GUI_T("New &Tab"), C_NEW, GUI_T("Ctrl+Shift+T"));
    gui_menu_item(m, GUI_T("&Close Tab"), C_CLOSE, GUI_T("Ctrl+Shift+W"));
    gui_menu_item(m, GUI_T("&Sessions..."), C_CONNS, GUI_T("Ctrl+Shift+O"));
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("E&xit"), C_EXIT, NULL);
    m = gui_menu(bar, GUI_T("&Edit"));
    gui_menu_item(m, GUI_T("&Copy"), C_COPY, GUI_T("Ctrl+Shift+C"));
    gui_menu_item(m, GUI_T("&Paste"), C_PASTE, GUI_T("Shift+Ins"));
    gui_menu_item(m, GUI_T("C&lear"), C_CLEAR, NULL);
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("&Find..."), C_FIND, GUI_T("Ctrl+Shift+F"));
    gui_menu_item(m, GUI_T("Find &Next"), C_FIND_NEXT, GUI_T("Ctrl+Shift+G"));
    gui_menu_item(m, GUI_T("Find Pre&vious"), C_FIND_PREV, GUI_T("Ctrl+Shift+H"));
    m = gui_menu(bar, GUI_T("&Help"));
    gui_menu_item(m, GUI_T("&About Terminal"), C_ABOUT, NULL);
    strip = gui_tabs(gui_vbox(win->root, 0, 0));
    gui_tabs_style(strip, GUI_TABS_SCROLL);
    gui_tabs_flags(strip, GUI_TABS_CLOSE | GUI_TABS_NEW);
    gui_on(strip, strip_on, 0);
    gui_grow(strip->parent, 1);
    gui_grow(strip, 1);
    t = tab_new(first >= 0 ? sess[first].lines : 0);
    if (first >= 0)
        tab_look(t, sess[first].colours, sess[first].font);
    win->focus = t->grid;
    gui_on_event(events);
    gui_show(win, 1);
    gui_term_size(t->grid, &cols, &rows);
    tab_start(t, cols, rows);
    if (log_on) {
        printf("TERM size %dx%d\n", cols, rows);
        fflush(stdout);
    }
    if (first >= 0) {                           /* the session's SSH command typed into it */
        char line[300];
        sess_command(&sess[first], line, sizeof line);
        job_write(t, line, (int)strlen(line));
        printf("TERM connect %s\n", sess[first].name);
        fflush(stdout);
    } else if (argc > 1) {                      /* COMMAND: typed into it */
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
