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
 *   GUITEST tabs        "GUITEST Tabs": ten pages in tabs too many for a row,
 *                       and buttons that change their style (rows, scroll,
 *                       list) and the window's width
 *   GUITEST commands    "GUITEST Commands" (toolkit 2, step C2): commands set once
 *                       and shown by the menus and a toolbar: icons, a toggle,
 *                       a group of three, one that can be used only after
 *                       another (the update hook); a button that makes the
 *                       window narrow, so the toolbar's end goes into its menu
 *   GUITEST vlist       "GUITEST Virtual list" (toolkit 2, step C3): a list of ten
 *                       thousand rows that keeps none: the model is asked for the
 *                       rows on the screen alone (counted); rows that arrive,
 *                       sorting asked of the app, what is chosen kept by id
 *   GUITEST icons       "GUITEST Icons": GLOS's own icons by name: colour ones at 16
 *                       and 32 pixels (ICONS.DAT), one-colour glyphs in the text's
 *                       colour and greyed, and in a list, a tree and a menu
 *   GUITEST vtree       "GUITEST Virtual tree": a tree that keeps no texts: nodes asked
 *                       for when drawn, children's ids when a node is opened (one
 *                       has five thousand); badges; a node's children changed
 *   GUITEST views       "GUITEST List views" (toolkit 2, step C3): the virtual list's
 *                       columns moved by their heads, and its four views (detail, big
 *                       icons, small icons, thumbnails the page draws)
 *   GUITEST tasks       "GUITEST Tasks" (toolkit 2, step C4): background work: a task
 *                       "reads" ten thousand rows in pieces on a thread of its own,
 *                       the list filling as they are posted; its progress dialog
 *                       after a second, and Cancel
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
            gui_label_style(l, fonts[f], GFX_NONE, -1);
        }
    for (i = 0; i < 48; i++)                    /* (B0h-DFh: shades, lines and blocks, as DOS programs draw) */
        bytes[i] = (char)(0xB0 + i);
    bytes[48] = 0;
    cp_to_utf8(437, bytes, drawing, sizeof drawing);
    gui_label_style(gui_label(box, drawing), gui_mono, GFX_NONE, -1);
    gui_show(win, 1);
}

/* Tabs too many for a row, in each style, in a window made wider and narrower. */
static struct gui_widget *tabs_demo;

static void tabs_click(struct gui_widget *w, int what)
{
    static const char *const styles[] = { "rows", "scroll", "list" };
    int id = w->id;
    if (what != GUI_ON_CLICK)
        return;
    if (id < 3) {
        gui_tabs_style(tabs_demo, id);
        say("tabs style %s", styles[id], 0);
    } else {
        int width = id == 3 ? 620 : 420;
        if (id == 3)
            glos_move(win->id, 0, 0);           /* (room for 620 pixels on a 640x480 desktop wherever it was) */
        gui_resize(win, width, 240);
        say("tabs width %s%d", "", width);
    }
}

static void tabs_select(struct gui_widget *w, int what)
{
    if (what == GUI_ON_SELECT)
        say("tabs page %s%d", "", gui_tabs_get(w));
}

static void tabs(void)
{
    static const char *const titles[] = { "Display", "Input", "Sound", "Network", "Date and Time", "Disk Cache",
                                          "Memory", "Remote Desktop", "Desktop", "Shell" };
    struct gui_widget *box, *row, *page;
    char line[64];
    int i;
    win = gui_window("GUITEST Tabs", 420, 240, 0);
    box = gui_vbox(win->root, -1, -1);
    gui_grow(box, 1);
    tabs_demo = gui_tabs(box);
    gui_grow(tabs_demo, 1);
    for (i = 0; i < 10; i++) {
        page = gui_tab(tabs_demo, titles[i]);
        snprintf(line, sizeof line, "Page %d: %s", i + 1, titles[i]);
        gui_label(page, line);
    }
    gui_on(tabs_demo, tabs_select, 0);
    row = gui_hbox(box, -1, 0);
    gui_button(row, "&Rows", tabs_click, 0);
    gui_button(row, "&Scroll", tabs_click, 1);
    gui_button(row, "&List", tabs_click, 2);
    gui_button(row, "&Wider", tabs_click, 3);
    gui_button(row, "&Narrower", tabs_click, 4);
    status = gui_status(win);
    gui_set_text(status, "Ready");
    gui_show(win, 1);
}

/* DOS names in the page DOS has: what M10's suite makes (letters the 437 upper-casing leaves alone). */
/* ---- commands (toolkit 2, step C2) */
enum { K_NEW = 100, K_OPEN, K_SAVE, K_CUT, K_COPY, K_PASTE, K_BOLD, K_SMALL, K_MEDIUM, K_LARGE, K_NARROW, K_TEXT,
       K_ABOUT, K_EXIT, K_PROPS };
static struct gui_widget *klist;
static unsigned char kicon[8][GUI_ICON_W * GUI_ICON_W];
static struct gui_widget *kbar;
static int kcopied, kbold, ksize = K_MEDIUM, knarrow, ktext;

/* Icons made here, a shape and a colour each: a page, a folder, a disk, scissors' cross, two pages, a board, a
   heavy B, an i. (Palette indices: 16 black, 231 white, the cube between.) */
static void kicons(void)
{
    static const unsigned char colour[8] = { 231, 226, 27, 196, 231, 180, 16, 27 };
    int k, x, y;
    for (k = 0; k < 8; k++)
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++) {
                int edge, in;
                unsigned char c = 0;
                switch (k) {
                case 1: in = x >= 1 && x <= 14 && y >= 4 && y <= 13, edge = x == 1 || x == 14 || y == 4 || y == 13; break;
                case 2: in = x >= 2 && x <= 13 && y >= 2 && y <= 13, edge = x == 2 || x == 13 || y == 2 || y == 13 ||
                        (y >= 9 && x >= 5 && x <= 10); break;
                case 3: in = 0, edge = (x == y || x == 15 - y) && x >= 3 && x <= 12; break;
                case 4: in = (x >= 2 && x <= 9 && y >= 1 && y <= 10) || (x >= 6 && x <= 13 && y >= 5 && y <= 14),
                        edge = ((x == 2 || x == 9) && y >= 1 && y <= 10) || ((y == 1 || y == 10) && x >= 2 && x <= 9) ||
                               ((x == 6 || x == 13) && y >= 5 && y <= 14) || ((y == 5 || y == 14) && x >= 6 && x <= 13);
                        break;
                case 5: in = x >= 3 && x <= 12 && y >= 2 && y <= 14, edge = x == 3 || x == 12 || y == 2 || y == 14 ||
                        (y <= 3 && x >= 6 && x <= 9); break;
                case 6: in = 0, edge = (x >= 4 && x <= 5 && y >= 3 && y <= 12) || ((y == 3 || y == 7 || y == 12) && x >= 4 &&
                        x <= 10) || (x >= 10 && x <= 11 && ((y >= 4 && y <= 6) || (y >= 8 && y <= 11))); break;
                case 7: in = 0, edge = (x >= 7 && x <= 8 && ((y >= 2 && y <= 3) || (y >= 6 && y <= 13))); break;
                default: in = x >= 3 && x <= 12 && y >= 1 && y <= 14, edge = x == 3 || x == 12 || y == 1 || y == 14; break;
                }
                if (edge)
                    c = (unsigned char)(k == 3 ? 196 : k == 7 ? 27 : 16);
                else if (in)
                    c = colour[k];
                kicon[k][y * 16 + x] = c;
            }
}

static void kupdate(struct gui_window *w)       /* what can be used now, and what is on */
{
    gui_command_enable(w, K_PASTE, kcopied);
    gui_command_check(w, K_BOLD, kbold);
    gui_command_radio(w, K_SMALL, ksize == K_SMALL);
    gui_command_radio(w, K_MEDIUM, ksize == K_MEDIUM);
    gui_command_radio(w, K_LARGE, ksize == K_LARGE);
    gui_command_check(w, K_NARROW, knarrow);
    gui_command_check(w, K_TEXT, ktext);
}

static void kcontext(struct gui_widget *w, int what)
{
    if (what == GUI_ON_CONTEXT)
        say("context for row%s %d", "", gui_list_get(w));
}

static void kcommand(struct gui_window *w, int id)
{
    static const char *const names[] = { "new", "open", "save", "cut", "copy", "paste", "bold", "small", "medium",
                                         "large", "narrow", "text", "about" };
    switch (id) {
    case K_COPY: kcopied = 1; break;
    case K_BOLD: kbold = !kbold; break;
    case K_SMALL: case K_MEDIUM: case K_LARGE: ksize = id; break;
    case K_NARROW:
        knarrow = !knarrow;
        gui_resize(w, knarrow ? 150 : 380, 220);
        break;
    case K_PROPS:
        say("properties of row%s %d", "", gui_list_get(klist));
        gui_status_progress(status, 2, gui_list_get(klist) + 1, 3);
        return;
    case K_TEXT:
        ktext = !ktext;
        gui_toolbar_text(kbar, ktext);
        break;
    case K_EXIT: gui_quit(0); return;
    }
    say("command %s", names[id - K_NEW], 0);
    gui_status_set(status, 1, names[id - K_NEW], id == K_COPY ? kicon[4] : NULL);
}

static void commands(void)
{
    struct gui_menubar *bar;
    struct gui_menu *m;
    struct gui_widget *box;
    static const struct { int id; const char *text, *key; int icon; const char *tip; } def[] = {
        { K_NEW, "&New", "Ctrl+N", 0, "A new page" }, { K_OPEN, "&Open...", "Ctrl+O", 1, "Open a file" },
        { K_SAVE, "&Save", "Ctrl+S", 2, "Save the page" }, { K_CUT, "Cu&t", "Ctrl+X", 3, "Cut" },
        { K_COPY, "&Copy", "Ctrl+C", 4, "Copy" }, { K_PASTE, "&Paste", "Ctrl+V", 5, "Paste what was copied" },
        { K_BOLD, "&Bold", "Ctrl+B", 6, "Bold text" }, { K_SMALL, "&Small", NULL, -1, NULL },
        { K_MEDIUM, "&Medium", NULL, -1, NULL }, { K_LARGE, "&Large", NULL, -1, NULL },
        { K_NARROW, "&Narrow window", "F6", -1, NULL }, { K_TEXT, "&Text on the toolbar", "F7", -1, NULL },
        { K_ABOUT, "&About", "F1", 7, "About these commands" }, { K_EXIT, "E&xit", NULL, -1, NULL } };
    unsigned i;
    kicons();
    win = gui_window("GUITEST Commands", 380, 220, 0);
    win->on_command = kcommand;
    win->on_update = kupdate;
    for (i = 0; i < sizeof def / sizeof *def; i++)
        gui_command(win, def[i].id, def[i].text, def[i].key, def[i].icon >= 0 ? kicon[def[i].icon] : NULL, def[i].tip);
    bar = gui_menubar(win);
    m = gui_menu(bar, "&File");
    gui_menu_command(m, K_NEW), gui_menu_command(m, K_OPEN), gui_menu_command(m, K_SAVE);
    gui_menu_sep(m);
    gui_menu_command(m, K_EXIT);
    m = gui_menu(bar, "&Edit");
    gui_menu_command(m, K_CUT), gui_menu_command(m, K_COPY), gui_menu_command(m, K_PASTE);
    m = gui_menu(bar, "&View");
    gui_menu_command(m, K_BOLD);
    {                                           /* a submenu: the group of three */
        struct gui_menu *sub = gui_menu_sub(m, "Text &size");
        gui_menu_command(sub, K_SMALL), gui_menu_command(sub, K_MEDIUM), gui_menu_command(sub, K_LARGE);
    }
    gui_menu_sep(m);
    gui_menu_command(m, K_NARROW), gui_menu_command(m, K_TEXT);
    m = gui_menu(bar, "&Help");
    gui_menu_command(m, K_ABOUT);
    kbar = gui_toolbar(win);
    gui_toolbar_command(kbar, K_NEW), gui_toolbar_command(kbar, K_OPEN), gui_toolbar_command(kbar, K_SAVE);
    gui_toolbar_sep(kbar);
    gui_toolbar_command(kbar, K_CUT), gui_toolbar_command(kbar, K_COPY), gui_toolbar_command(kbar, K_PASTE);
    gui_toolbar_sep(kbar);
    gui_toolbar_command(kbar, K_BOLD);
    gui_toolbar_command(kbar, K_MEDIUM);        /* (a command with no icon: its text) */
    gui_toolbar_sep(kbar);
    gui_toolbar_command(kbar, K_ABOUT);
    box = gui_vbox(win->root, -1, -1);
    gui_grow(box, 1);
    gui_label(box, "Commands are set once; the menus and the toolbar show them.");
    gui_tip(gui_label(box, "Paste can be used once Copy has been."), "A label's own tooltip");
    {                                           /* a list with a context menu: commands, and an item of its own */
        static const char *const rows[3][1] = { { "First row" }, { "Second row" }, { "Third row" } };
        struct gui_menu *cm = gui_menu_new();
        klist = gui_list(box, 1, NULL, NULL);
        gui_grow(klist, 1);
        for (i = 0; i < 3; i++)
            gui_list_add(klist, rows[i], kicon[0], NULL);
        gui_on(klist, kcontext, 0);
        gui_menu_command(cm, K_OPEN), gui_menu_command(cm, K_COPY), gui_menu_command(cm, K_PASTE);
        gui_menu_sep(cm);
        gui_menu_item(cm, "P&roperties", K_PROPS, NULL);
        gui_context(klist, cm);
    }
    status = gui_status(win);
    gui_status_section(status, 90);             /* the last command, with Copy's icon */
    gui_status_section(status, 70);             /* a small progress bar: the row whose properties were asked */
    gui_show(win, 1);
    say("commands", "", 0);
}

/* ---- GLOS's icons by name (docs/gui.md 1.11) */
static const char *const icolour[] = { "folder", "folder-open", "file", "file-text", "file-image", "file-sound",
    "file-video", "file-archive", "file-program", "drive", "drive-floppy", "drive-cd", "computer", "printer", "trash",
    "home", "app-files", "app-terminal", "app-notepad", "app-calc", "app-control", "settings", "info", "warning" };
static const char *const iglyph[] = { "new", "open", "save", "cut", "copy", "paste", "undo", "redo", "delete", "find",
    "print", "bold", "italic", "underline", "add", "close", "back", "forward", "up", "home", "refresh", "settings",
    "info", "help", "warning", "star", "play", "pause", "stop", "volume", "lock", "edit", "check", "terminal" };

static void icons_draw(struct gui_widget *w, struct gfx_surface *s)
{
    unsigned i;
    for (i = 0; i < sizeof icolour / sizeof *icolour; i++) {
        gui_draw_icon(s, w->r.x + 4 + (int)i * 20, w->r.y + 2, gui_icon(icolour[i], 0), GUI_ICON_W);
        if (i < 12)
            gui_draw_icon(s, w->r.x + 4 + (int)i * 40, w->r.y + 24, gui_icon(icolour[i], 1), GUI_BIG_W);
    }
    for (i = 0; i < sizeof iglyph / sizeof *iglyph; i++) {
        int x = w->r.x + 4 + (int)(i % 24) * 20, y = w->r.y + 62 + (int)(i / 24) * 40;
        gui_draw_icon(s, x, y, gui_glyph(iglyph[i]), GUI_ICON_W);
        gui_draw_icon_dim(s, x, y + 20, gui_glyph(iglyph[i]), GUI_ICON_W);
    }
}

static void icons_page(void)
{
    static const char *const rows[4][2] = { { "A folder", "colour" }, { "A text file", "colour" },
                                            { "Saved", "a glyph" }, { "Found", "a glyph" } };
    struct gui_widget *box, *row, *l, *t;
    struct gui_node *n;
    struct gui_menubar *bar;
    struct gui_menu *m;
    int i;
    win = gui_window("GUITEST Icons", 500, 300, 0);
    win->on_command = kcommand;
    gui_command(win, K_SAVE, "&Save", "Ctrl+S", gui_glyph("save"), "Save");
    gui_command(win, K_OPEN, "&Open...", "Ctrl+O", gui_icon("folder-open", 0), "Open");
    gui_command(win, K_PASTE, "&Paste", "Ctrl+V", gui_glyph("paste"), "Paste");
    gui_command_enable(win, K_PASTE, 0);
    gui_command(win, K_EXIT, "E&xit", NULL, NULL, NULL);
    bar = gui_menubar(win);
    m = gui_menu(bar, "&File");
    gui_menu_command(m, K_OPEN), gui_menu_command(m, K_SAVE), gui_menu_command(m, K_PASTE);
    gui_menu_sep(m);
    gui_menu_command(m, K_EXIT);
    kbar = gui_toolbar(win);
    gui_toolbar_command(kbar, K_OPEN), gui_toolbar_command(kbar, K_SAVE), gui_toolbar_command(kbar, K_PASTE);
    box = gui_vbox(win->root, -1, -1);
    gui_grow(box, 1);
    gui_canvas(box, 484, 140, icons_draw, NULL);
    row = gui_hbox(box, -1, 0);
    gui_grow(row, 1);
    l = gui_list(row, 2, NULL, NULL);
    gui_grow(l, 1);
    for (i = 0; i < 4; i++)
        gui_list_add(l, rows[i], i == 0 ? gui_icon("folder", 0) : i == 1 ? gui_icon("file-text", 0)
                                 : i == 2 ? gui_glyph("save") : gui_glyph("find"), NULL);
    gui_list_set(l, 2);
    t = gui_tree(row);
    gui_grow(t, 1);
    n = gui_tree_add(t, NULL, "This PC", gui_icon("computer", 0), NULL, 1);
    gui_tree_add(t, n, "C:", gui_icon("drive", 0), NULL, 0);
    gui_tree_add(t, n, "A:", gui_icon("drive-floppy", 0), NULL, 0);
    gui_tree_add(t, n, "Starred", gui_glyph("star"), NULL, 0);
    gui_tree_expand(t, n, 1);
    status = gui_status(win);
    gui_show(win, 1);
    say("icons: folder %s", gui_icon("folder", 1) ? "there" : "missing", 0);
    say("icons: nothing %s", gui_icon("no-such-icon", 0) ? "there" : "missing", 0);
}

/* ---- the virtual list (toolkit 2, step C3) */
static struct gui_widget *vl;
static int vn = 20, vdown;
static unsigned char vseen[10000 / 8 + 1];      /* the rows the list has asked for since last told */

static int v_count(void *ctx) { (void)ctx; return vn; }

static void v_row(void *ctx, int row, struct gui_vrow *out)
{
    static char name[24], size[16];
    int item = vdown ? vn - 1 - row : row;
    (void)ctx;
    vseen[item >> 3] |= (unsigned char)(1 << (item & 7));
    snprintf(name, sizeof name, "Item %05d", item);
    snprintf(size, sizeof size, "%d", item * 37 % 10007);
    out->cell[0] = name, out->cell[1] = size, out->cell[2] = item % 3 ? "Plain" : "Third";
    out->icon = item % 5 ? icon_doc : icon_dir;
    out->big = gui_icon(item % 5 ? "file-text" : "folder", 1);  /* (the icon view's: GLOS's own, by name) */
    out->id = (unsigned long)item + 1;
}

static int v_find(void *ctx, unsigned long id)
{
    int item = (int)id - 1;
    (void)ctx;
    return item < 0 || item >= vn ? -1 : vdown ? vn - 1 - item : item;
}

/* The first row from `from` on whose name begins with what was typed ("i" and digits: Item 0NNNN), or -1. */
static int v_match(void *ctx, const char *typed, int from)
{
    char name[24];
    int i, row;
    for (i = 0; i < vn; i++) {
        row = (from + i) % vn;
        snprintf(name, sizeof name, "item%05d", vdown ? vn - 1 - row : row);
        if (!strncmp(name, typed, strlen(typed)) || !strncmp(name + 4, typed, strlen(typed)))
            return row;
    }
    (void)ctx;
    return -1;
}

static void v_on(struct gui_widget *w, int what)
{
    if (what == GUI_ON_SELECT) {
        say("row%s %d", "", gui_vlist_get(w));
    } else if (what == GUI_ON_ACTIVATE) {
        say("open id%s %d", "", (int)gui_vlist_id(w));
    } else if (what == GUI_ON_SORT) {
        int col, down;
        gui_vlist_sort(w, &col, &down);
        vdown = down;
        gui_vlist_changed(w);
        say("sorted %s by column %d", down ? "down" : "up", col);
    }
}

static void v_more(struct gui_widget *w, int what)
{
    (void)w, (void)what;
    vn = 10000;
    gui_vlist_changed(vl);
    say("rows%s %d", "", vn);
}

static void v_icons(struct gui_widget *w, int what)     /* the detail view and the icon view, turn about */
{
    static int icons;
    (void)w, (void)what;
    icons = !icons;
    gui_vlist_view(vl, icons ? GUI_VIEW_ICONS : GUI_VIEW_DETAIL);
    say("view %s", icons ? "icons" : "detail", 0);
}

static void v_asked(struct gui_widget *w, int what)     /* how many rows the list asked for since last told */
{
    unsigned long ids[4];
    int n = gui_vlist_chosen(vl, ids, 4), i, rows = 0;
    (void)w, (void)what;
    for (i = 0; i < 10000; i++)
        rows += vseen[i >> 3] >> (i & 7) & 1;
    memset(vseen, 0, sizeof vseen);
    if (rows <= 120)                            /* (a few screens of a dozen rows, of ten thousand) */
        say("asked for a few screens of rows, not the list; chosen%s %d", "", n);
    else
        say("asked for%s %d rows!", "", rows);
}

static void vlist(void)
{
    static const char *const heads[] = { "Name", "Size", "Kind" };
    static const int widths[] = { 150, 70, 80 };
    static const struct gui_model model = { v_count, v_row, v_find, v_match, NULL };
    struct gui_widget *box, *row;
    win = gui_window("GUITEST Virtual list", 380, 240, 0);
    box = gui_vbox(win->root, -1, -1);
    gui_grow(box, 1);
    row = gui_hbox(box, -1, 0);
    gui_button(row, "&More rows", v_more, 0);
    gui_button(row, "&Asked?", v_asked, 0);
    gui_button(row, "&Icons", v_icons, 0);
    vl = gui_vlist(box, 3, heads, widths, &model);
    gui_vlist_multi(vl, 1);
    gui_on(vl, v_on, 0);
    gui_grow(vl, 1);
    status = gui_status(win);
    gui_show(win, 1);
    gui_focus(vl);
    say("vlist", "", 0);
}

/* ---- the virtual list's columns and views (toolkit 2, step C3): the same model as the vlist page, ten thousand
   rows; thumbnails of three shapes, drawn here once (a landscape, a portrait, a square), and folders without */
static unsigned thumbs[3][GUI_THUMB * GUI_THUMB];
static const int thumb_wh[3][2] = { { 96, 72 }, { 64, 96 }, { 96, 96 } };

static void w_row(void *ctx, int row, struct gui_vrow *out)
{
    int item = vdown ? vn - 1 - row : row, k = item % 3;
    v_row(ctx, row, out);
    if (item % 5) {                             /* (folders, every fifth, have none: their big icon) */
        out->thumb = thumbs[k];
        out->thumb_w = thumb_wh[k][0], out->thumb_h = thumb_wh[k][1];
    }
}

static void w_view(struct gui_widget *w, int what)
{
    static const char *const names[] = { "detail", "icons", "small", "thumbs" };
    int view = w->id;                           /* (the button's id: the view) */
    (void)what;
    gui_vlist_view(vl, view);
    gui_focus(vl);                              /* (as a toolbar's view buttons do: the list keeps the keys) */
    say("view %s", names[view], 0);
}

static void w_columns(struct gui_widget *w, int what)  /* the columns as the list has them now */
{
    int order[3], widths[3];
    char text[64];
    (void)w, (void)what;
    gui_vlist_columns(vl, order, widths);
    snprintf(text, sizeof text, "columns %d,%d,%d widths %d,%d,%d", order[0], order[1], order[2], widths[0], widths[1],
             widths[2]);
    say("%s", text, 0);
}

static void w_reset(struct gui_widget *w, int what)    /* the columns as they began */
{
    static const int order[] = { 0, 1, 2 }, widths[] = { 150, 70, 80 };
    (void)w, (void)what;
    gui_vlist_set_columns(vl, order, widths);
    say("columns reset", "", 0);
}

static void views(void)
{
    static const char *const heads[] = { "Name", "Size", "Kind" };
    static const int widths[] = { 150, 70, 80 };
    static const struct gui_model model = { v_count, w_row, v_find, v_match, NULL };
    struct gui_widget *box, *row;
    int k, x, y;
    for (k = 0; k < 3; k++)                     /* a sky, a ground and a sun, each its own size */
        for (y = 0; y < thumb_wh[k][1]; y++)
            for (x = 0; x < thumb_wh[k][0]; x++) {
                int w = thumb_wh[k][0], h = thumb_wh[k][1], dx = x - w * 2 / 3, dy = y - h / 3;
                unsigned c = y > h * 2 / 3 ? 0x2E7D32u + (unsigned)((y * 40 / h) << 8)
                             : 0x1565C0u + (unsigned)((y * 100 / h) << 16) + (unsigned)(y * 60 / h << 8);
                if (dx * dx + dy * dy < (w / 7) * (w / 7))
                    c = 0xFFD54Fu;
                thumbs[k][y * w + x] = c;
            }
    vn = 10000;
    win = gui_window("GUITEST List views", 470, 300, 0);
    box = gui_vbox(win->root, -1, -1);
    gui_grow(box, 1);
    row = gui_hbox(box, -1, 0);
    gui_button(row, "&Detail", w_view, GUI_VIEW_DETAIL);
    gui_button(row, "&Icons", w_view, GUI_VIEW_ICONS);
    gui_button(row, "&Small", w_view, GUI_VIEW_SMALL);
    gui_button(row, "&Thumbs", w_view, GUI_VIEW_THUMBS);
    gui_button(row, "&Columns?", w_columns, 0);
    gui_button(row, "&Reset", w_reset, 0);
    vl = gui_vlist(box, 3, heads, widths, &model);
    gui_vlist_multi(vl, 1);
    gui_on(vl, v_on, 0);
    gui_grow(vl, 1);
    status = gui_status(win);
    gui_show(win, 1);
    gui_focus(vl);
    say("views", "", 0);
}

/* ---- the virtual tree (toolkit 2, step C3). Ids: 1, 2, 3 at the top; a node's children are its id times ten
   and 1, 2, 3 (to three levels); but node 2's are five thousand leaves, 2000000 and on. */
static struct gui_widget *vt;
static int vt_asked, vt_more;

static int t_children(void *ctx, unsigned long id, unsigned long *ids, int room, int from)
{
    int total = id == 0 ? 3 : id == 2 ? 5000 : id == 1 ? 3 + vt_more : id < 100 ? 3 : 0, i;
    (void)ctx;
    for (i = from; i < total && i - from < room; i++)
        ids[i - from] = id == 2 ? 2000000ul + (unsigned long)i : id * 10 + (unsigned long)i + 1;
    return total;
}

static void t_node(void *ctx, unsigned long id, struct gui_tnode *out)
{
    static char text[32];
    (void)ctx;
    vt_asked++;
    if (id <= 3)
        snprintf(text, sizeof text, "%s", id == 1 ? "Disk C:" : id == 2 ? "Disk D:" : "Mail");
    else
        snprintf(text, sizeof text, id >= 2000000ul ? "File %04lu" : "Folder %lu", id >= 2000000ul ? id - 2000000ul : id);
    out->text = text;
    out->may = id < 100;
    out->icon = id == 3 ? gui_glyph("mail") : gui_icon(id <= 2 ? "drive" : out->may ? "folder" : "file", 0);
    out->badge = id == 3 ? "12" : id == 31 ? "5" : NULL;
}

static void t_on(struct gui_widget *w, int what)
{
    if (what == GUI_ON_SELECT)
        say("node%s %d", "", (int)gui_vtree_get(w));
    else if (what == GUI_ON_ACTIVATE)
        say("open node%s %d", "", (int)gui_vtree_get(w));
}

static void t_asked(struct gui_widget *w, int what)
{
    (void)w, (void)what;
    if (vt_asked <= 400)                        /* (a few screens of a dozen, each drawn a few times) */
        say("asked for the nodes on the screen, not the tree%s", "", 0);
    else
        say("asked for%s %d nodes!", "", vt_asked);
    vt_asked = 0;
}

static void t_more(struct gui_widget *w, int what)      /* Disk C: has a folder more */
{
    (void)w, (void)what;
    vt_more = 1;
    gui_vtree_changed(vt, 1);
    say("changed%s", "", 0);
}

static void vtree(void)
{
    static const struct gui_tmodel model = { t_children, t_node, NULL };
    struct gui_widget *box, *row;
    win = gui_window("GUITEST Virtual tree", 300, 260, 0);
    box = gui_vbox(win->root, -1, -1);
    gui_grow(box, 1);
    row = gui_hbox(box, -1, 0);
    gui_button(row, "&Asked?", t_asked, 0);
    gui_button(row, "&More in C:", t_more, 0);
    vt = gui_vtree(box, &model);
    gui_on(vt, t_on, 0);
    gui_grow(vt, 1);
    status = gui_status(win);
    gui_show(win, 1);
    gui_focus(vt);
    say("vtree", "", 0);
}

/* ---- background work (toolkit 2, step C4) */
static struct gui_widget *kl;
static int kn;                                  /* the rows that have arrived */

static int k_count(void *ctx) { (void)ctx; return kn; }

static void k_row(void *ctx, int row, struct gui_vrow *out)
{
    static char name[24];
    (void)ctx;
    snprintf(name, sizeof name, "Row %05d", row);
    out->cell[0] = name;
    out->icon = gui_icon("file", 0);
    out->id = (unsigned long)row + 1;
}

static void k_arrived(void *data)               /* (the window's thread) a piece of rows has come */
{
    kn += (int)(long)data;
    gui_vlist_changed(kl);
}

static void k_work(struct gui_task *t, void *arg)       /* (a worker's thread) a hundred pieces, a tenth of a second
                                                           each: ten seconds, so that Loop A's driver, seconds a
                                                           step on a 486, sees the dialog and can cancel */
{
    char line[48];
    int i;
    (void)arg;
    for (i = 0; i < 100 && !gui_task_cancelled(t); i++) {
        glos_sleep(100);
        gui_task_post(t, k_arrived, (void *)100L);
        snprintf(line, sizeof line, "%d of 10000 rows", (i + 1) * 100);
        gui_task_progress(t, i + 1, 100, line);
    }
}

static void k_done(struct gui_window *w, void *arg, int cancelled)
{
    (void)w, (void)arg;
    if (cancelled)
        say("read cancelled%s", "", 0);
    else
        say("read done rows%s %d", "", kn);
}

static void k_read(struct gui_widget *w, int what)
{
    (void)w, (void)what;
    kn = 0;
    gui_vlist_changed(kl);
    if (!gui_task_start(win, "Reading rows", k_work, NULL, k_done, GUI_TASK_DIALOG))
        say("no threads%s", "", 0);
    else
        say("reading%s", "", 0);
}

static void tasks(void)
{
    static const char *const heads[] = { "Name" };
    static const struct gui_model model = { k_count, k_row, NULL, NULL, NULL };
    struct gui_widget *box, *row;
    win = gui_window("GUITEST Tasks", 300, 220, 0);
    box = gui_vbox(win->root, -1, -1);
    gui_grow(box, 1);
    row = gui_hbox(box, -1, 0);
    gui_button(row, "&Read", k_read, 0);
    kl = gui_vlist(box, 1, heads, NULL, &model);
    gui_grow(kl, 1);
    status = gui_status(win);
    gui_show(win, 1);
    say("tasks", "", 0);
}

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
    else if (!strcmp(what, "tabs"))
        tabs();
    else if (!strcmp(what, "commands"))
        commands();
    else if (!strcmp(what, "vlist"))
        vlist();
    else if (!strcmp(what, "icons"))
        icons_page();
    else if (!strcmp(what, "vtree"))
        vtree();
    else if (!strcmp(what, "tasks"))
        tasks();
    else if (!strcmp(what, "views"))
        views();
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
