/* NOTEPAD: GLOS's text editor (PRD §10.6; docs/milestones-m9.md step D).
 * A file's bytes as they are, in the toolkit's editor (DejaVu Sans Mono;
 * Latin-1 until GLOS has code pages): open, save, find, replace, go to a
 * line, word wrap, the clipboard.
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
    char t[300];
    snprintf(t, sizeof t, "%s%s - Notepad", gui_editor_modified(ed) ? "*" : "", path[0] ? gui_path_name(path) : "Untitled");
    gui_title(win, t);
}

static void where(void *arg)
{
    int line, col, mod = gui_editor_modified(ed);
    char t[64];
    (void)arg;
    gui_editor_where(ed, &line, &col);
    if (line != last_line || col != last_col) {
        snprintf(t, sizeof t, "Ln %d, Col %d", line, col);
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
        gui_editor_set(ed, "", 0);
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
                gui_message(win, "Notepad", "The file is too big for the memory there is.", GUI_MB_OK);
                return -1;
            }
            buf = nb;
        }
        if (!(k = fread(buf + n, 1, room - n, f)))
            break;
        n += k;
    }
    fclose(f);
    gui_editor_set(ed, buf ? buf : "", n);
    free(buf);
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
    size_t n = gui_editor_length(ed);
    char *buf = malloc(n ? n : 1);
    FILE *f;
    if (!buf)
        return -1;
    gui_editor_get(ed, buf, n);
    if (!(f = gui_fopen(p, "wb")) || fwrite(buf, 1, n, f) != n || fclose(f) != 0) {
        free(buf);
        gui_message(win, "Notepad", "The file couldn't be written.", GUI_MB_OK);
        return -1;
    }
    free(buf);
    strncpy(path, p, sizeof path - 1);
    gui_editor_saved(ed);
    title();
    note("saved %s", p);
    return 0;
}

static int save_as(void)
{
    char p[260];
    snprintf(p, sizeof p, "%s", path[0] ? path : "C:\\");
    if (!gui_file_dialog(win, "Save As", 1, p, sizeof p))
        return -1;
    return save_to(p);
}

static int save(void) { return path[0] ? save_to(path) : save_as(); }

/* Changes not saved: kept (0) or not wanted after all (-1: cancelled). */
static int keep_changes(void)
{
    char t[300];
    int r;
    if (!gui_editor_modified(ed))
        return 0;
    snprintf(t, sizeof t, "Save the changes to %s?", path[0] ? gui_path_name(path) : "Untitled");
    r = gui_message(win, "Notepad", t, GUI_MB_YESNOCANCEL);
    if (r == GUI_YES)
        return save();
    return r == GUI_NO ? 0 : -1;
}

static void find_next(void)
{
    int line, col;
    if (!what[0])
        return;
    if (gui_editor_find(ed, what, 0, 1))
        return;
    gui_editor_where(ed, &line, &col);          /* round from the top again */
    gui_editor_goto(ed, 1);
    if (!gui_editor_find(ed, what, 0, 1)) {
        char t[200];
        gui_editor_goto(ed, line);
        snprintf(t, sizeof t, "\"%s\" isn't there.", what);
        gui_message(win, "Notepad", t, GUI_MB_OK);
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
            gui_editor_set(ed, "", 0);
            title();
        }
        break;
    case C_OPEN:
        snprintf(p, sizeof p, "%s", path[0] ? path : "C:\\");
        if (keep_changes() == 0 && gui_file_dialog(win, "Open", 0, p, sizeof p))
            load(p);
        break;
    case C_SAVE: save(); break;
    case C_SAVEAS: save_as(); break;
    case C_EXIT:
        if (keep_changes() == 0)
            gui_quit(0);
        break;
    case C_UNDO: gui_editor_command(ed, GUI_CMD_UNDO); break;
    case C_CUT: gui_editor_command(ed, GUI_CMD_CUT); break;
    case C_COPY: gui_editor_command(ed, GUI_CMD_COPY); break;
    case C_PASTE: gui_editor_command(ed, GUI_CMD_PASTE); break;
    case C_DELETE: gui_editor_command(ed, GUI_CMD_DELETE); break;
    case C_ALL: gui_editor_command(ed, GUI_CMD_ALL); break;
    case C_FIND:
        if (gui_ask(win, "Find", "Find what:", what, sizeof what))
            find_next();
        break;
    case C_NEXT:
        if (!what[0] && !gui_ask(win, "Find", "Find what:", what, sizeof what))
            break;
        find_next();
        break;
    case C_REPLACE:
        with[0] = 0;
        if (!gui_ask(win, "Replace", "Replace what:", what, sizeof what) || !what[0]
            || !gui_ask(win, "Replace", "With:", with, sizeof with))
            break;
        gui_editor_goto(ed, 1);
        for (n = 0; gui_editor_find(ed, what, 0, 1); n++)
            gui_editor_replace_sel(ed, with);
        snprintf(t, sizeof t, "%d replaced.", n);
        gui_message(win, "Replace", t, GUI_MB_OK);
        break;
    case C_GOTO:
        t[0] = 0;
        if (gui_ask(win, "Go To Line", "Line number:", t, sizeof t) && atoi(t) > 0)
            gui_editor_goto(ed, atoi(t));
        break;
    case C_WRAP:
        wrap = !wrap;
        gui_editor_wrap(ed, wrap);
        gui_menu_check(format, C_WRAP, wrap);
        break;
    case C_ABOUT:
        gui_message(win, "About Notepad", "Notepad: GLOS's text editor.", GUI_MB_OK);
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
    win = gui_window("Untitled - Notepad", 520, 340, 0);
    win->on_command = command;
    win->on_close = closing;
    bar = gui_menubar(win);
    m = gui_menu(bar, "&File");
    gui_menu_item(m, "&New", C_NEW, "Ctrl+N");
    gui_menu_item(m, "&Open...", C_OPEN, "Ctrl+O");
    gui_menu_item(m, "&Save", C_SAVE, "Ctrl+S");
    gui_menu_item(m, "Save &As...", C_SAVEAS, NULL);
    gui_menu_sep(m);
    gui_menu_item(m, "E&xit", C_EXIT, NULL);
    m = gui_menu(bar, "&Edit");
    gui_menu_item(m, "&Undo", C_UNDO, "Ctrl+Z");
    gui_menu_sep(m);
    gui_menu_item(m, "Cu&t", C_CUT, "Ctrl+X");
    gui_menu_item(m, "&Copy", C_COPY, "Ctrl+C");
    gui_menu_item(m, "&Paste", C_PASTE, "Ctrl+V");
    gui_menu_item(m, "De&lete", C_DELETE, "Del");
    gui_menu_sep(m);
    gui_menu_item(m, "&Find...", C_FIND, "Ctrl+F");
    gui_menu_item(m, "Find &Next", C_NEXT, "F3");
    gui_menu_item(m, "&Replace...", C_REPLACE, "Ctrl+H");
    gui_menu_item(m, "&Go To...", C_GOTO, "Ctrl+G");
    gui_menu_sep(m);
    gui_menu_item(m, "Select &All", C_ALL, "Ctrl+A");
    format = m = gui_menu(bar, "F&ormat");
    gui_menu_item(m, "&Word Wrap", C_WRAP, NULL);
    m = gui_menu(bar, "&Help");
    gui_menu_item(m, "&About Notepad", C_ABOUT, NULL);
    ed = gui_editor(gui_vbox(win->root, 0, 0));
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
