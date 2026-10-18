/* GUITEST: the toolkit's gallery (docs/milestones-m9.md step A; the "gui"
 * suite of tests/loopa/jobs.py and tests/host's goldens). DJGPP, with
 * sdk/gui and sdk/libglos; on the host with tests/host/glosshim.c.
 *
 *   GUITEST gallery     one window, "GUITEST Gallery": a menu bar, tabs of
 *                       every widget, a status bar that says what happened
 *   GUITEST dialogs     a window with buttons that open each dialog
 *   GUITEST clip TEXT   a window with a field; Ctrl+C there copies, and the
 *                       field shows what Ctrl+V pasted
 *   GUITEST text        "GUITEST Text": UTF-8 in each font, and code page
 *                       437's drawing characters (M10 step G)
 *   GUITEST names DIR   files in DIR named in DOS's code page (GLOSCP), then
 *                       the file dialog there: the name chosen, as DOS's
 *                       bytes (M10 step G)
 *
 * What happens is written to stdout too ("GUI ..." lines), for the suite. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/cp.h>
#include <glos/gui.h>

static struct gui_window *win;
static struct gui_widget *status;

static void say(const char *fmt, const char *a, int n)
{
    char line[200];
    snprintf(line, sizeof line, fmt, a, n);
    if (status)
        gui_set_text(status, line);
    printf("GUI %s\n", line);
    fflush(stdout);
}

enum { C_NEW = 1, C_OPEN, C_SAVE, C_EXIT, C_CUT, C_COPY, C_PASTE, C_ALL, C_WRAP, C_ABOUT };

static struct gui_widget *editor, *field, *list, *tree;
static struct gui_menu *view_menu;
static int wrap;

static void command(struct gui_window *w, int id)
{
    char path[260];
    switch (id) {
    case C_NEW: say("new", "", 0); break;
    case C_OPEN:
        strcpy(path, "C:\\GUI\\");
        if (gui_file_dialog(w, "Open", 0, path, sizeof path))
            say("open %s", path, 0);
        else
            say("open: cancelled", "", 0);
        break;
    case C_SAVE: say("save", "", 0); break;
    case C_EXIT: gui_quit(0); break;
    case C_CUT: gui_editor_command(editor, GUI_CMD_CUT); break;
    case C_COPY: gui_editor_command(editor, GUI_CMD_COPY); break;
    case C_PASTE: gui_editor_command(editor, GUI_CMD_PASTE); break;
    case C_ALL: gui_editor_command(editor, GUI_CMD_ALL); break;
    case C_WRAP:
        wrap = !wrap;
        gui_editor_wrap(editor, wrap);
        gui_menu_check(view_menu, C_WRAP, wrap);
        say("wrap %s", wrap ? "on" : "off", 0);
        break;
    case C_ABOUT:
        gui_message(w, "About GUITEST", "GUITEST shows every widget of GLOS's toolkit, for its tests.", GUI_MB_OK);
        say("about done", "", 0);
        break;
    }
}

static void changed(struct gui_widget *w, int what)
{
    char t[64];
    const char *kind = what == GUI_ON_CLICK ? "click" : what == GUI_ON_CHANGE ? "change" : what == GUI_ON_SELECT ? "select"
                                                                                                                    : "activate";
    if (w == list)
        snprintf(t, sizeof t, "%s row %d", kind, gui_list_get(w));
    else if (w == tree)
        snprintf(t, sizeof t, "%s %s", kind, gui_node_text(gui_tree_get(w)));
    else
        snprintf(t, sizeof t, "%s %d", kind, w->id);
    if (w == tree && what == GUI_ON_CHANGE) {   /* a node opening: its children now */
        struct gui_node *n = gui_tree_opening(w);
        char c[32];
        int i;
        for (i = 1; i <= 3; i++) {
            snprintf(c, sizeof c, "%s.%d", gui_node_text(n), i);
            gui_tree_add(w, n, c, NULL, NULL, i == 1);
        }
    }
    say("%s", t, 0);
}

static int by_name(struct gui_widget *w, int a, int b, int col)
{
    return strcmp(gui_list_cell(w, a, col), gui_list_cell(w, b, col));
}

static unsigned char icon_doc[GUI_ICON_W * GUI_ICON_W], icon_dir[GUI_ICON_W * GUI_ICON_W];

static void icons(void)
{
    int x, y;
    for (y = 1; y < 15; y++)                    /* a page: white with a black edge */
        for (x = 3; x < 13; x++)
            icon_doc[y * 16 + x] = (unsigned char)(x == 3 || x == 12 || y == 1 || y == 14 ? 16 : 231);
    for (y = 3; y < 14; y++)                    /* a folder: the cube's yellow */
        for (x = 1; x < 15; x++)
            icon_dir[y * 16 + x] = (unsigned char)(x == 1 || x == 14 || y == 3 || y == 13 || (y == 5 && x < 7) ? 16 : 226);
}

static void gallery(void)
{
    struct gui_menubar *bar;
    struct gui_menu *m;
    struct gui_widget *tabs, *page, *row, *g, *form, *first;
    static const char *const heads[3] = { "Name", "Size", "Kind" };
    static const int widths[3] = { 140, 70, 80 };
    static const char *const rows[][3] = {
        { "AUTOEXEC.BAT", "412", "Batch" }, { "CONFIG.SYS", "230", "Text" }, { "GLOS.EXE", "41022", "Program" },
        { "README.TXT", "5120", "Text" }, { "DOOM.WAD", "11159840", "Data" }, { "FWHEEL.EXE", "1802240", "Program" },
        { "NOTES.TXT", "77", "Text" }, { "PICTURE.PNG", "20311", "Image" }, { "ZEBRA.PCX", "6400", "Image" },
        { "BIGFILE.DAT", "65536", "Data" }, { "EMPTY.TXT", "0", "Text" } };
    int i;

    win = gui_window("GUITEST Gallery", 440, 330, 0);
    win->on_command = command;
    bar = gui_menubar(win);
    m = gui_menu(bar, "&File");
    gui_menu_item(m, "&New", C_NEW, "Ctrl+N");
    gui_menu_item(m, "&Open...", C_OPEN, "Ctrl+O");
    gui_menu_item(m, "&Save", C_SAVE, "Ctrl+S");
    gui_menu_sep(m);
    gui_menu_item(m, "E&xit", C_EXIT, NULL);
    m = gui_menu(bar, "&Edit");
    gui_menu_item(m, "Cu&t", C_CUT, "Ctrl+X");
    gui_menu_item(m, "&Copy", C_COPY, "Ctrl+C");
    gui_menu_item(m, "&Paste", C_PASTE, "Ctrl+V");
    gui_menu_sep(m);
    gui_menu_item(m, "Select &all", C_ALL, "Ctrl+A");
    view_menu = m = gui_menu(bar, "&View");
    gui_menu_item(m, "&Word wrap", C_WRAP, NULL);
    m = gui_menu(bar, "&Help");
    gui_menu_item(m, "&About GUITEST", C_ABOUT, "F1");

    tabs = gui_tabs(gui_vbox(win->root, -1, 6));
    gui_grow(tabs, 1);
    gui_grow(tabs->parent, 1);

    page = gui_tab(tabs, "Buttons");
    row = gui_hbox(page, -1, 0);
    gui_on(gui_button(row, "&Press me", changed, 1), changed, 1);
    gui_button(row, "&Default", changed, 2)->flags |= GUI_DEFAULT;
    gui_enable(gui_button(row, "Off", changed, 3), 0);
    row = gui_hbox(page, 16, 0);
    g = gui_group(row, "Checks");
    gui_on(gui_check(g, "&Bold", 1), changed, 10);
    gui_on(gui_check(g, "&Italic", 0), changed, 11);
    gui_enable(gui_check(g, "Not now", 1), 0);
    g = gui_group(row, "Radios");
    first = gui_radio(g, "&Small", NULL);
    gui_on(first, changed, 20);
    gui_on(gui_radio(g, "&Medium", first), changed, 21);
    gui_on(gui_radio(g, "&Large", first), changed, 22);
    form = gui_form(page);
    gui_on(gui_spin(gui_row(form, "&Count:", NULL), 0, 99, 7), changed, 30);
    gui_on(gui_slider(gui_row(form, "Vol&ume:", NULL), 0, 256, 192), changed, 31);
    {
        struct gui_widget *c = gui_choice(gui_row(form, "C&olour:", NULL), 10);
        gui_choice_add(c, "Slate");
        gui_choice_add(c, "Paper");
        gui_choice_add(c, "Night");
        gui_on(c, changed, 32);
    }
    gui_progress_set(gui_progress(gui_row(form, "Progress:", NULL), 100), 40, 100);

    page = gui_tab(tabs, "Text");
    form = gui_form(page);
    field = gui_field(gui_row(form, "&Name:", NULL), 24);
    gui_set_text(field, "GLOS");
    gui_on(field, changed, 40);
    {
        struct gui_widget *pw = gui_field(gui_row(form, "Pass&word:", NULL), 24);
        gui_field_mask(pw, 1);
        gui_set_text(pw, "secret");
    }
    editor = gui_editor(page);
    {
        static const char sample[] = "The toolkit's editor.\r\nIt keeps a file's bytes as they are:\r\n"
                                     "\tTabs at every 8th column,\r\nand lines that end in CR LF.\r\nA long line goes on "
                                     "and on past the right edge of the window, so that word wrap has something to wrap.\r\n";
        gui_editor_set(editor, sample, sizeof sample - 1);
    }
    gui_on(editor, changed, 41);

    page = gui_tab(tabs, "Lists");
    row = gui_hbox(page, -1, 0);
    gui_grow(row, 1);
    list = gui_list(row, 3, heads, widths);
    gui_size(list, 300, 0);
    for (i = 0; i < (int)(sizeof rows / sizeof *rows); i++)
        gui_list_add(list, rows[i], i % 3 == 0 ? icon_dir : icon_doc, NULL);
    gui_list_sort(list, 0, by_name);
    gui_on(list, changed, 50);
    tree = gui_tree(row);
    gui_on(tree, changed, 51);
    {
        struct gui_node *n = gui_tree_add(tree, NULL, "C:", icon_dir, NULL, 1);
        gui_tree_add(tree, NULL, "D:", icon_dir, NULL, 1);
        gui_tree_expand(tree, n, 1);
    }

    status = gui_status(win);
    gui_set_text(status, "Ready");
    gui_show(win, 1);
}

static void dialogs_click(struct gui_widget *w, int what)
{
    char buf[64] = "typed here";
    char path[260] = "C:\\GUI\\";
    int r;
    (void)what;
    switch (w->id) {
    case 1:
        r = gui_message(win, "Question", "Save the changes to NOTES.TXT before it closes?", GUI_MB_YESNOCANCEL);
        say("message %s", r == GUI_YES ? "yes" : r == GUI_NO ? "no" : "cancel", 0);
        break;
    case 2:
        if (gui_ask(win, "Ask", "A name for the new folder:", buf, sizeof buf))
            say("ask %s", buf, 0);
        else
            say("ask: cancelled", "", 0);
        break;
    case 3:
        if (gui_file_dialog(win, "Save As", 1, path, sizeof path))
            say("save as %s", path, 0);
        else
            say("save as: cancelled", "", 0);
        break;
    }
}

static void dialogs(void)
{
    struct gui_widget *box;
    win = gui_window("GUITEST Dialogs", 300, 140, 0);
    box = gui_vbox(win->root, -1, -1);
    gui_button(box, "&Message", dialogs_click, 1);
    gui_button(box, "&Ask", dialogs_click, 2);
    gui_button(box, "&Save as", dialogs_click, 3);
    status = gui_status(win);
    gui_set_text(status, "Ready");
    gui_show(win, 1);
}

static void clip_paste(struct gui_widget *w, int what)
{
    if (what == GUI_ON_CHANGE)
        say("text %s", gui_get_text(w), 0);
}

static void clip(const char *text)
{
    struct gui_widget *box, *f;
    win = gui_window("GUITEST Clip", 260, 70, 0);
    box = gui_vbox(win->root, -1, -1);
    f = gui_field(box, 30);
    gui_set_text(f, text);
    gui_on(f, clip_paste, 1);
    status = gui_status(win);
    gui_set_text(status, "Ready");
    gui_show(win, 1);
}

static void text(void)
{
    static const char *const lines[] = {        /* (UTF-8) */
        "Łódź, Žižkov, Œuvre, Ğ ş ı — “quotes” … € 5",
        "Ελληνικά: Καλημέρα κόσμε, ΑΒΓΔ αβγδ ω",
        "Кириллица: Привет, мир! ЁЖЯ ёжя",
        "№ 5, ™, ← ↑ → ↓, ‰, † ‡ •",
    };
    const struct gfx_font *fonts[3] = { gui_font, gui_bold, gui_mono };
    struct gui_widget *box, *l;
    char bytes[49], drawing[200];
    int f, i;
    win = gui_window("GUITEST Text", 0, 0, 0);
    box = gui_vbox(win->root, -1, -1);
    for (f = 0; f < 3; f++)
        for (i = 0; i < (int)(sizeof lines / sizeof lines[0]); i++) {
            l = gui_label(box, lines[i]);
            gui_label_style(l, fonts[f], 0, -1);
        }
    for (i = 0; i < 48; i++)                    /* (B0h-DFh: shades, lines and blocks, as DOS programs draw) */
        bytes[i] = (char)(0xB0 + i);
    bytes[48] = 0;
    cp_to_utf8(437, bytes, drawing, sizeof drawing);
    gui_label_style(gui_label(box, drawing), gui_mono, 0, -1);
    gui_show(win, 1);
}

/* DOS names in the page DOS has: what M10's suite makes (letters the 437 upper-casing leaves alone). */
static void names(const char *dir)
{
    static const char *const n850[] = { "ÅRHUS.TXT", "ÉTÉ.TXT", "NIÑO.TXT", "ØL.TXT", 0 };
    static const char *const n866[] = { "ПОРТ.TXT", "РОТ.TXT", "ЮЯ.TXT", 0 };
    static const char *const n437[] = { "PLAIN.TXT", 0 };
    const char *const *list = cp_active() == 850 ? n850 : cp_active() == 866 ? n866 : n437;
    char path[260], dos[64], chosen[260], line[400];
    int i, o = 0;
    void *f;
    gui_mkdir(dir);
    for (i = 0; list[i]; i++) {
        gui_text_dos(list[i], dos, sizeof dos);
        gui_path_join(path, sizeof path, dir, dos);
        if ((f = gui_fopen(path, "wb")) != NULL)
            fclose(f);
    }
    printf("GUI page %d\n", cp_active());
    snprintf(chosen, sizeof chosen, "%s", dir);
    if (!gui_file_dialog(NULL, "Open", 0, chosen, sizeof chosen)) {
        printf("GUI chose nothing\n");
        return;
    }
    for (i = 0; chosen[i] && o < (int)sizeof line - 5; i++)  /* (DOS's bytes: those above 7Fh as \xNN) */
        o += snprintf(line + o, sizeof line - (size_t)o, (unsigned char)chosen[i] >= 0x80 ? "\\x%02X" : "%c",
                      (unsigned char)chosen[i]);
    printf("GUI chose %s\n", line);
}

int main(int argc, char **argv)
{
    const char *what = argc > 1 ? argv[1] : "gallery";
    if (gui_init(argc, argv) != 0) {
        printf("GUI no GLOS\n");
        return 1;
    }
    icons();
    if (!strcmp(what, "dialogs"))
        dialogs();
    else if (!strcmp(what, "clip"))
        clip(argc > 2 ? argv[2] : "");
    else if (!strcmp(what, "text"))
        text();
    else if (!strcmp(what, "names")) {
        names(argc > 2 ? argv[2] : "C:\\GUI\\NAMES");
        printf("GUI end\n");
        return 0;
    } else
        gallery();
    gui_run();
    printf("GUI end\n");
    return gui_exit_code();
}
