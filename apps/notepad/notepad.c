/* NOTEPAD: GLOS's text editor (PRD §10.6; docs/milestones-m9.md step D).
 * A file's bytes as they are, in the toolkit's code editor (gui_code, plain:
 * no margin; toolkit 2, C10; DejaVu Sans Mono; Latin-1 until GLOS has code
 * pages): open, save, find, replace, go to a line, word wrap, the clipboard.
 *
 *   NOTEPAD [FILE]
 *
 * A file that doesn't exist yet is a new one by that name, made when it is
 * saved. Changes not saved are asked about before the file or the window
 * goes. "NOTEPAD ..." lines on stdout say what happened (the suite's). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/gui.h>

enum { C_NEW = 1, C_OPEN, C_SAVE, C_SAVEAS, C_EXIT, C_UNDO, C_CUT, C_COPY, C_PASTE, C_DELETE, C_FIND, C_NEXT,
       C_REPLACE, C_GOTO, C_ALL, C_WRAP, C_ABOUT };

static struct gui_window *win;
static struct gui_widget *ed, *status;
static struct gui_menu *format;
static char path[260], what[128];
static int wrap, last_line = -1, last_col = -1, last_mod = -1;

static void note(const char *fmt, const char *a)
{
    printf("NOTEPAD ");
    printf(fmt, a);
    printf("\n");
    fflush(stdout);
}

static void title(void)
{
    char t[300], name[128];
    snprintf(t, sizeof t, GUI_T("%s%s - Notepad"), gui_code_modified(ed) ? "*" : "",
             path[0] ? gui_dos_text(gui_path_name(path), name, sizeof name) : "Untitled");
    gui_title(win, t);
}

static void where(void *arg)
{
    int line, col, mod = gui_code_modified(ed);
    char t[64];
    (void)arg;
    gui_code_where(ed, &line, &col);
    if (line != last_line || col != last_col) {
        snprintf(t, sizeof t, GUI_T("Ln %d, Col %d"), line, col);
        gui_set_text(status, t);
        last_line = line, last_col = col;
    }
    if (mod != last_mod) {
        title();
        last_mod = mod;
    }
}

static int load(const char *p)
{
    FILE *f = gui_fopen(p, "rb");
    char *buf = NULL;
    size_t n = 0, room = 0, k;
    if (!f) {
        strncpy(path, p, sizeof path - 1);      /* (a new file by that name) */
        gui_code_set(ed, NULL, 0);
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
    gui_code_set(ed, buf, n);                   /* (the buffer is the editor's now) */
    strncpy(path, p, sizeof path - 1);
    path[sizeof path - 1] = 0;
    title();
    {
        char t[300];
        snprintf(t, sizeof t, "%s %lu", p, (unsigned long)n);
        note("open %s", t);
    }
    return 0;
}

static int save_to(const char *p)
{
    size_t n;
    char *buf = gui_code_text(ed, &n);
    FILE *f;
    if (!buf)
        return -1;
    if (!(f = gui_fopen(p, "wb")) || fwrite(buf, 1, n, f) != n || fclose(f) != 0) {
        free(buf);
        gui_message(win, GUI_T("Notepad"), GUI_T("The file couldn't be written."), GUI_MB_OK);
        return -1;
    }
    free(buf);
    strncpy(path, p, sizeof path - 1);
    gui_code_saved(ed);
    title();
    note("saved %s", p);
    return 0;
}

/* Open's and Save As's: text files first (a name saved without a type gets .TXT), or every file; the one chosen
   kept from one to the next. */
static const char *const filters[] = { GUI_N("Text files|*.TXT;*.MD;*.LOG;*.INI;*.CFG;*.BAT"), GUI_N("All files|*.*"),
    NULL };
static struct gui_file_opts file_opts = { filters, 0, NULL, 0, NULL, 0, NULL, NULL };

static int save_as(void)
{
    char p[260];
    snprintf(p, sizeof p, "%s", path[0] ? path : glos_boot_root());
    if (!gui_file_dialog_ex(win, GUI_T("Save As"), 1, p, sizeof p, &file_opts))
        return -1;
    return save_to(p);
}

static int save(void) { return path[0] ? save_to(path) : save_as(); }

/* Changes not saved: kept (0) or not wanted after all (-1: cancelled). */
static int keep_changes(void)
{
    char t[300], name[128];
    int r;
    if (!gui_code_modified(ed))
        return 0;
    snprintf(t, sizeof t, GUI_T("Save the changes to %s?"),
             path[0] ? gui_dos_text(gui_path_name(path), name, sizeof name) : "Untitled");
    r = gui_message(win, GUI_T("Notepad"), t, GUI_MB_YESNOCANCEL);
    if (r == GUI_YES)
        return save();
    return r == GUI_NO ? 0 : -1;
}

static void find_next(void)
{
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
    char p[260], with[128], t[200];
    int n;
    (void)w;
    switch (id) {
    case C_NEW:
        if (keep_changes() == 0) {
            path[0] = 0;
            gui_code_set(ed, NULL, 0);
            title();
        }
        break;
    case C_OPEN:
        snprintf(p, sizeof p, "%s", path[0] ? path : glos_boot_root());
        if (keep_changes() == 0 && gui_file_dialog_ex(win, GUI_T("Open"), 0, p, sizeof p, &file_opts))
            load(p);
        break;
    case C_SAVE: save(); break;
    case C_SAVEAS: save_as(); break;
    case C_EXIT:
        if (keep_changes() == 0)
            gui_quit(0);
        break;
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
    case C_WRAP:
        wrap = !wrap;
        gui_code_style(ed, GUI_CODE_PLAIN | (wrap ? GUI_CODE_WRAP : 0));
        gui_menu_check(format, C_WRAP, wrap);
        break;
    case C_ABOUT:
        gui_message(win, GUI_T("About Notepad"), GUI_T("Notepad: GLOS's text editor."), GUI_MB_OK);
        break;
    }
    gui_focus(ed);
}

static void closing(struct gui_window *w)
{
    command(w, C_EXIT);
}

int main(int argc, char **argv)
{
    struct gui_menubar *bar;
    struct gui_menu *m;
    if (gui_init(argc, argv) != 0) {
        printf("NOTEPAD no GLOS\n");
        return 1;
    }
    win = gui_window(GUI_T("Untitled - Notepad"), 520, 340, 0);
    win->on_command = command;
    win->on_close = closing;
    bar = gui_menubar(win);
    m = gui_menu(bar, GUI_T("&File"));
    gui_menu_item(m, GUI_T("&New"), C_NEW, GUI_T("Ctrl+N"));
    gui_menu_item(m, GUI_T("&Open..."), C_OPEN, GUI_T("Ctrl+O"));
    gui_menu_item(m, GUI_T("&Save"), C_SAVE, GUI_T("Ctrl+S"));
    gui_menu_item(m, GUI_T("Save &As..."), C_SAVEAS, NULL);
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
    m = gui_menu(bar, GUI_T("&Help"));
    gui_menu_item(m, GUI_T("&About Notepad"), C_ABOUT, NULL);
    ed = gui_code(gui_vbox(win->root, 0, 0));
    gui_code_style(ed, GUI_CODE_PLAIN);
    gui_code_tabs(ed, 8);
    gui_grow(ed->parent, 1);
    status = gui_status(win);
    if (argc > 1)
        load(argv[1]);
    win->focus = ed;
    gui_show(win, 1);
    gui_idle(where, NULL, 100);
    where(NULL);
    gui_run();
    note("end%s", "");
    return 0;
}
