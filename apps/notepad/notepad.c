/* NOTEPAD: GLOS's text editor (PRD §10.6; docs/milestones-m9.md step D).
 * A file's bytes as they are, in the toolkit's code editor (gui_code, plain:
 * no margin; toolkit 2, C10; DejaVu Sans Mono; Latin-1 until GLOS has code
 * pages): open, save, find, replace, go to a line, word wrap, the clipboard.
 *
 *   NOTEPAD [FILE...]
 *
 * Tabs (phase E, docs/v1/apps/notepad.md): a tab for each open file, with its name and a * while it has changes
 * not saved. New makes an empty one, Open opens the file in a tab of its own (in the empty one, if that is all
 * there is), Close (Ctrl+W, a tab's close box) asks about its changes and takes it away; Ctrl+Tab goes round.
 * Twelve at most.
 *
 * View: Line Numbers shows the editor's margin (numbers, and fold boxes for what is indented under a line);
 * Highlighting colours a file by its kind, which its name's end says (C, batch, INI and GLOS.CFG, assembly,
 * Pascal, HTML: lexers.c; the kind is in the status bar). Both are every document's.
 *
 * A file that doesn't exist yet is a new one by that name, made when it is
 * saved. Changes not saved are asked about before the file or the window
 * goes. "NOTEPAD ..." lines on stdout say what happened (the suite's). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/gui.h>

#include "lexers.h"

enum { C_NEW = 1, C_OPEN, C_SAVE, C_SAVEAS, C_EXIT, C_UNDO, C_CUT, C_COPY, C_PASTE, C_DELETE, C_FIND, C_NEXT,
       C_REPLACE, C_GOTO, C_ALL, C_WRAP, C_ABOUT, C_CLOSE, C_NUMBERS, C_COLOURS };

#define DOCS 12

struct doc {
    struct gui_widget *page, *ed;
    char path[260];
    int last_mod;                       /* how its tab is marked now: -1 not yet */
};

static struct gui_window *win;
static struct gui_widget *strip, *status;
static struct gui_menu *format, *view;
static struct doc doc[DOCS];
static int numbers, colours = 1, kind_at;      /* View's two; the status bar's section for the file's kind */
static int ndocs;
static char what[128];
static int wrap, last_line = -1, last_col = -1;
static struct doc *last_doc;

static void note(const char *fmt, const char *a)
{
    printf("NOTEPAD ");
    printf(fmt, a);
    printf("\n");
    fflush(stdout);
}

static struct doc *cur(void)            /* the document shown */
{
    struct gui_widget *p = gui_tabs_page(strip, gui_tabs_get(strip));
    int i;
    for (i = 0; i < ndocs; i++)
        if (doc[i].page == p)
            return &doc[i];
    return &doc[0];
}

static const char *doc_name(const struct doc *d, char *name, size_t room)
{
    return d->path[0] ? gui_dos_text(gui_path_name(d->path), name, room) : "Untitled";
}

/* A document's look: the margin or not, wrapped or not, and its kind's colours (by its name's end). */
static void doc_style(struct doc *d)
{
    gui_code_style(d->ed, (numbers ? 0 : GUI_CODE_PLAIN) | (wrap ? GUI_CODE_WRAP : 0));
    gui_code_lexer(d->ed, colours && d->path[0] ? lexer_for(d->path, NULL) : NULL, NULL);
    gui_damage(d->ed);
}

/* The window's title, the shown document's; and each tab's, its document's name with a * while it has changes. */
static void title(void)
{
    char t[300], name[128];
    struct doc *d = cur();
    int i;
    snprintf(t, sizeof t, GUI_T("%s%s - Notepad"), gui_code_modified(d->ed) ? "*" : "", doc_name(d, name, sizeof name));
    gui_title(win, t);
    {                                           /* (and its kind, in the status bar) */
        const char *kind = "Text";
        if (d->path[0])
            lexer_for(d->path, &kind);
        gui_status_set(status, kind_at, GUI_T(kind), NULL);
    }
    for (i = 0; i < ndocs; i++) {
        int mod = gui_code_modified(doc[i].ed);
        snprintf(t, sizeof t, "%s%s", mod ? "*" : "", doc_name(&doc[i], name, sizeof name));
        if (strcmp(t, gui_get_text(doc[i].page))) {
            gui_set_text(doc[i].page, t);
            gui_damage(strip);
        }
        doc[i].last_mod = mod;
    }
}

static void where(void *arg)
{
    struct doc *d = cur();
    int line, col, i, stale = d != last_doc;
    char t[64];
    (void)arg;
    gui_code_where(d->ed, &line, &col);
    if (line != last_line || col != last_col || stale) {
        snprintf(t, sizeof t, GUI_T("Ln %d, Col %d"), line, col);
        gui_set_text(status, t);
        last_line = line, last_col = col;
    }
    for (i = 0; i < ndocs; i++)
        if (gui_code_modified(doc[i].ed) != doc[i].last_mod)
            stale = 1;
    if (stale)
        title();
    last_doc = d;
}

/* A document more, empty, in a tab of its own, shown. NULL: there are twelve. */
static struct doc *doc_new(void)
{
    struct doc *d;
    int i;
    if (ndocs == DOCS) {
        gui_message(win, GUI_T("Notepad"), GUI_T("Twelve files are open: close one first."), GUI_MB_OK);
        return NULL;
    }
    d = &doc[ndocs++];
    memset(d, 0, sizeof *d);
    d->last_mod = -1;
    d->page = gui_tab(strip, "Untitled");
    d->ed = gui_code(d->page);
    doc_style(d);
    gui_code_tabs(d->ed, 8);
    gui_grow(d->ed, 1);
    for (i = 0; gui_tabs_page(strip, i); i++)
        if (gui_tabs_page(strip, i) == d->page)
            gui_tabs_set(strip, i);
    gui_relayout(win);
    return d;
}

static int load(struct doc *d, const char *p)
{
    FILE *f = gui_fopen(p, "rb");
    char *buf = NULL;
    size_t n = 0, room = 0, k;
    if (!f) {
        strncpy(d->path, p, sizeof d->path - 1);        /* (a new file by that name) */
        gui_code_set(d->ed, NULL, 0);
        doc_style(d);
        note("new %s", p);
        title();
        return 0;
    }
    for (;;) {
        if (n == room) {
            char *nb = realloc(buf, room = room * 2 + 65536);
            if (!nb) {
                fclose(f);
                free(buf);
                gui_message(win, GUI_T("Notepad"), GUI_T("The file is too big for the memory there is."), GUI_MB_OK);
                return -1;
            }
            buf = nb;
        }
        if (!(k = fread(buf + n, 1, room - n, f)))
            break;
        n += k;
    }
    fclose(f);
    gui_code_set(d->ed, buf, n);                /* (the buffer is the editor's now) */
    strncpy(d->path, p, sizeof d->path - 1);
    d->path[sizeof d->path - 1] = 0;
    doc_style(d);
    title();
    {
        char t[300];
        snprintf(t, sizeof t, "%s %lu", p, (unsigned long)n);
        note("open %s", t);
    }
    return 0;
}

/* A file opened: in the shown tab if that is an empty, untouched Untitled; in a tab of its own otherwise. */
static void open_file(const char *p)
{
    struct doc *d = cur();
    int i;
    for (i = 0; i < ndocs; i++)                 /* (open already: its tab) */
        if (doc[i].path[0] && !strcasecmp(doc[i].path, p)) {
            int k;
            for (k = 0; gui_tabs_page(strip, k); k++)
                if (gui_tabs_page(strip, k) == doc[i].page)
                    gui_tabs_set(strip, k);
            title();
            return;
        }
    if (d->path[0] || gui_code_modified(d->ed) || gui_code_length(d->ed))
        d = doc_new();
    if (d)
        load(d, p);
}

static int save_to(struct doc *d, const char *p)
{
    size_t n;
    char *buf = gui_code_text(d->ed, &n);
    FILE *f;
    if (!buf)
        return -1;
    if (!(f = gui_fopen(p, "wb")) || fwrite(buf, 1, n, f) != n || fclose(f) != 0) {
        free(buf);
        gui_message(win, GUI_T("Notepad"), GUI_T("The file couldn't be written."), GUI_MB_OK);
        return -1;
    }
    free(buf);
    strncpy(d->path, p, sizeof d->path - 1);
    gui_code_saved(d->ed);
    doc_style(d);                               /* (another name may be another kind) */
    title();
    note("saved %s", p);
    return 0;
}

/* Open's and Save As's: text files first (a name saved without a type gets .TXT), or every file; the one chosen
   kept from one to the next. */
static const char *const filters[] = { GUI_N("Text files|*.TXT;*.MD;*.LOG;*.INI;*.CFG;*.BAT"), GUI_N("All files|*.*"),
    NULL };
static struct gui_file_opts file_opts = { filters, 0, NULL, 0, NULL, 0, NULL, NULL };

static int save_as(struct doc *d)
{
    char p[260];
    snprintf(p, sizeof p, "%s", d->path[0] ? d->path : glos_boot_root());
    if (!gui_file_dialog_ex(win, GUI_T("Save As"), 1, p, sizeof p, &file_opts))
        return -1;
    return save_to(d, p);
}

static int save(struct doc *d) { return d->path[0] ? save_to(d, d->path) : save_as(d); }

/* A document's changes not saved: kept (0) or not wanted after all (-1: cancelled). Its tab is shown to ask. */
static int keep_changes(struct doc *d)
{
    char t[300], name[128];
    int r, i;
    if (!gui_code_modified(d->ed))
        return 0;
    for (i = 0; gui_tabs_page(strip, i); i++)
        if (gui_tabs_page(strip, i) == d->page && gui_tabs_get(strip) != i) {
            gui_tabs_set(strip, i);
            title();
        }
    snprintf(t, sizeof t, GUI_T("Save the changes to %s?"), doc_name(d, name, sizeof name));
    r = gui_message(win, GUI_T("Notepad"), t, GUI_MB_YESNOCANCEL);
    if (r == GUI_YES)
        return save(d);
    return r == GUI_NO ? 0 : -1;
}

/* A tab closed, its changes asked about; the last one's leaves an empty Untitled in its place. */
static void doc_close(struct doc *d)
{
    int i, at = (int)(d - doc);
    if (keep_changes(d) != 0)
        return;
    note("closed %s", d->path[0] ? d->path : "Untitled");
    if (ndocs == 1) {
        d->path[0] = 0;
        gui_code_set(d->ed, NULL, 0);
        gui_code_saved(d->ed);
        doc_style(d);
        title();
        return;
    }
    for (i = 0; gui_tabs_page(strip, i); i++)
        if (gui_tabs_page(strip, i) == d->page) {
            gui_tab_remove(strip, i);
            break;
        }
    memmove(&doc[at], &doc[at + 1], (size_t)(ndocs - at - 1) * sizeof doc[0]);
    ndocs--;
    last_doc = NULL;
    gui_relayout(win);
    title();
}

static void find_next(void)
{
    struct gui_widget *ed = cur()->ed;
    int line, col;
    if (!what[0])
        return;
    if (gui_code_find(ed, what, 0, 1))
        return;
    gui_code_where(ed, &line, &col);          /* round from the top again */
    gui_code_goto(ed, 1, 1);
    if (!gui_code_find(ed, what, 0, 1)) {
        char t[200];
        gui_code_goto(ed, line, 1);
        snprintf(t, sizeof t, "\"%s\" isn't there.", what);
        gui_message(win, GUI_T("Notepad"), t, GUI_MB_OK);
    }
}

static void command(struct gui_window *w, int id)
{
    struct doc *d = cur();
    struct gui_widget *ed = d->ed;
    char p[260], with[128], t[200];
    int n, i;
    (void)w;
    switch (id) {
    case C_NEW:
        doc_new();
        title();
        break;
    case C_OPEN:
        snprintf(p, sizeof p, "%s", d->path[0] ? d->path : glos_boot_root());
        if (gui_file_dialog_ex(win, GUI_T("Open"), 0, p, sizeof p, &file_opts))
            open_file(p);
        break;
    case C_SAVE: save(d); break;
    case C_SAVEAS: save_as(d); break;
    case C_CLOSE: doc_close(d); break;
    case C_EXIT:
        for (i = 0; i < ndocs; i++)             /* each one's changes; any "cancel" keeps the window */
            if (keep_changes(&doc[i]) != 0)
                return;
        gui_quit(0);
        return;
    case C_UNDO: gui_code_command(ed, GUI_CMD_UNDO); break;
    case C_CUT: gui_code_command(ed, GUI_CMD_CUT); break;
    case C_COPY: gui_code_command(ed, GUI_CMD_COPY); break;
    case C_PASTE: gui_code_command(ed, GUI_CMD_PASTE); break;
    case C_DELETE: gui_code_command(ed, GUI_CMD_DELETE); break;
    case C_ALL: gui_code_command(ed, GUI_CMD_ALL); break;
    case C_FIND:
        if (gui_ask(win, GUI_T("Find"), GUI_T("Find what:"), what, sizeof what))
            find_next();
        break;
    case C_NEXT:
        if (!what[0] && !gui_ask(win, GUI_T("Find"), GUI_T("Find what:"), what, sizeof what))
            break;
        find_next();
        break;
    case C_REPLACE:
        with[0] = 0;
        if (!gui_ask(win, GUI_T("Replace"), GUI_T("Replace what:"), what, sizeof what) || !what[0]
            || !gui_ask(win, GUI_T("Replace"), GUI_T("With:"), with, sizeof with))
            break;
        gui_code_goto(ed, 1, 1);
        for (n = 0; gui_code_find(ed, what, 0, 1); n++)
            gui_code_replace_sel(ed, with);
        snprintf(t, sizeof t, GUI_T("%d replaced."), n);
        gui_message(win, GUI_T("Replace"), t, GUI_MB_OK);
        break;
    case C_GOTO:
        t[0] = 0;
        if (gui_ask(win, GUI_T("Go To Line"), GUI_T("Line number:"), t, sizeof t) && atoi(t) > 0)
            gui_code_goto(ed, atoi(t), 1);
        break;
    case C_WRAP:                                /* (every document's, as View's two are) */
    case C_NUMBERS:
    case C_COLOURS:
        if (id == C_WRAP)
            wrap = !wrap;
        else if (id == C_NUMBERS)
            numbers = !numbers;
        else
            colours = !colours;
        for (i = 0; i < ndocs; i++)
            doc_style(&doc[i]);
        gui_menu_check(format, C_WRAP, wrap);
        gui_menu_check(view, C_NUMBERS, numbers);
        gui_menu_check(view, C_COLOURS, colours);
        note("view %s", id == C_WRAP ? "wrap" : id == C_NUMBERS ? "numbers" : "highlighting");
        break;
    case C_ABOUT:
        gui_message(win, GUI_T("About Notepad"), GUI_T("Notepad: GLOS's text editor."), GUI_MB_OK);
        break;
    }
    gui_focus(cur()->ed);
}

static void closing(struct gui_window *w)
{
    command(w, C_EXIT);
}

static void strip_on(struct gui_widget *w, int what_on)
{
    int i;
    if (what_on == GUI_ON_NEW) {
        command(win, C_NEW);
    } else if (what_on == GUI_ON_CLOSE) {
        struct gui_widget *p = gui_tabs_page(w, gui_tabs_closing(w));
        for (i = 0; i < ndocs; i++)
            if (doc[i].page == p) {
                doc_close(&doc[i]);
                break;
            }
        gui_focus(cur()->ed);
    } else if (what_on == GUI_ON_CHANGE) {
        title();
        gui_focus(cur()->ed);
    }
}

int main(int argc, char **argv)
{
    struct gui_menubar *bar;
    struct gui_menu *m;
    int i;
    if (gui_init(argc, argv) != 0) {
        printf("NOTEPAD no GLOS\n");
        return 1;
    }
    win = gui_window(GUI_T("Untitled - Notepad"), 520, 340, 0);
    gui_window_icon(win, "app-notepad");
    win->on_command = command;
    win->on_close = closing;
    bar = gui_menubar(win);
    m = gui_menu(bar, GUI_T("&File"));
    gui_menu_item(m, GUI_T("&New"), C_NEW, GUI_T("Ctrl+N"));
    gui_menu_item(m, GUI_T("&Open..."), C_OPEN, GUI_T("Ctrl+O"));
    gui_menu_item(m, GUI_T("&Save"), C_SAVE, GUI_T("Ctrl+S"));
    gui_menu_item(m, GUI_T("Save &As..."), C_SAVEAS, NULL);
    gui_menu_item(m, GUI_T("&Close"), C_CLOSE, GUI_T("Ctrl+W"));
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("E&xit"), C_EXIT, NULL);
    m = gui_menu(bar, GUI_T("&Edit"));
    gui_menu_item(m, GUI_T("&Undo"), C_UNDO, GUI_T("Ctrl+Z"));
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("Cu&t"), C_CUT, GUI_T("Ctrl+X"));
    gui_menu_item(m, GUI_T("&Copy"), C_COPY, GUI_T("Ctrl+C"));
    gui_menu_item(m, GUI_T("&Paste"), C_PASTE, GUI_T("Ctrl+V"));
    gui_menu_item(m, GUI_T("De&lete"), C_DELETE, GUI_T("Del"));
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("&Find..."), C_FIND, GUI_T("Ctrl+F"));
    gui_menu_item(m, GUI_T("Find &Next"), C_NEXT, GUI_T("F3"));
    gui_menu_item(m, GUI_T("&Replace..."), C_REPLACE, GUI_T("Ctrl+H"));
    gui_menu_item(m, GUI_T("&Go To..."), C_GOTO, GUI_T("Ctrl+G"));
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("Select &All"), C_ALL, GUI_T("Ctrl+A"));
    format = m = gui_menu(bar, GUI_T("F&ormat"));
    gui_menu_item(m, GUI_T("&Word Wrap"), C_WRAP, NULL);
    view = m = gui_menu(bar, GUI_T("&View"));
    gui_menu_item(m, GUI_T("&Line Numbers"), C_NUMBERS, NULL);
    gui_menu_item(m, GUI_T("&Highlighting"), C_COLOURS, NULL);
    gui_menu_check(m, C_COLOURS, colours);
    m = gui_menu(bar, GUI_T("&Help"));
    gui_menu_item(m, GUI_T("&About Notepad"), C_ABOUT, NULL);
    strip = gui_tabs(gui_vbox(win->root, 0, 0));
    gui_tabs_style(strip, GUI_TABS_SCROLL);
    gui_tabs_flags(strip, GUI_TABS_CLOSE | GUI_TABS_NEW);
    gui_on(strip, strip_on, 0);
    gui_grow(strip->parent, 1);
    gui_grow(strip, 1);
    status = gui_status(win);
    kind_at = gui_status_section(status, 90);
    doc_new();
    for (i = 1; i < argc; i++)                  /* NOTEPAD FILE...: each in a tab */
        open_file(argv[i]);
    if (ndocs > 1)
        gui_tabs_set(strip, 0);
    win->focus = cur()->ed;
    gui_show(win, 1);
    gui_idle(where, NULL, 100);
    where(NULL);
    gui_run();
    note("end%s", "");
    return 0;
}
