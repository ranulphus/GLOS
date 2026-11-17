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
 *   GUITEST split       "GUITEST Splitters" (toolkit 2, step C5): a folder tree beside
 *                       a list, an output pane under the list that keeps its height;
 *                       the bars dragged, moved by keys, panes folded; the window
 *                       made wider (each split keeps the pane it should)
 *   GUITEST doctabs     "GUITEST Document tabs" (toolkit 2, step C5): tabs that close
 *                       (a box, the middle button, Ctrl+F4; a page with changes says no
 *                       once), move by dragging, add a page ("+") and list every
 *                       title (the button at the strip's end)
 *   GUITEST dock        "GUITEST Docking" (toolkit 2, step C5): a document in the middle,
 *                       Tools and Layers at the left, Output at the bottom, Properties at
 *                       the right; panes closed, shown again (the View button), docked
 *                       elsewhere and floated by their context menus; the layout saved,
 *                       reset and loaded
 *   GUITEST combo       "GUITEST Combo boxes" (toolkit 2, step C6): a folder box with
 *                       items and a history, and a name box whose items the app makes
 *                       for what is typed; driven by keys alone
 *   GUITEST smalldlg    "GUITEST Small dialogs" (toolkit 2, step C6): a message with
 *                       "Don't ask again", About (GUITEST's credits and the toolkit's),
 *                       and Folder (a tree of the drives' folders, New folder)
 *   GUITEST filedlg     "GUITEST Open and Save" (toolkit 2, step C6): the new Open and
 *                       Save dialog (places, the folder's combo box, filters, a
 *                       preview, formats), by keys alone
 *   GUITEST find        "GUITEST Find" (toolkit 2, step C6): Find and Replace over a line
 *                       of text (the page searches with gui_match): next, previous,
 *                       replace, replace all, a regular expression with groups
 *   GUITEST colour      "GUITEST Colour" (toolkit 2, step C6): the Colour dialog by keys
 *   GUITEST font        "GUITEST Font" (toolkit 2, step C6): the Font dialog by keys, and
 *                       a line of the page drawn in the font chosen
 *   GUITEST undo        "GUITEST Undo" (toolkit 2, step C7): a number and a line changed
 *                       by buttons, undone and redone by the Edit menu, Ctrl+Z and
 *                       Ctrl+Y and the history list
 *   GUITEST clipfmt     "GUITEST Clipboard" (toolkit 2, step C7): text, a picture, files
 *                       and a private format put in one transaction, and read back
 *   GUITEST dnd         "GUITEST Drag and drop" (toolkit 2, step C7): fruit dragged into
 *                       a basket (copied where it is let go), and reordered in its list
 *   GUITEST calendar    "GUITEST Calendar" (toolkit 2, step C8): a month (several days
 *                       chosen, two marked, today fixed), a date field, a time field
 *   GUITEST chat        "GUITEST Chat" (toolkit 2, step C8): a chat view, lines coloured by
 *                       who wrote them, kept to twelve, at the end unless scrolled back
 *   GUITEST rich        "GUITEST Rich text" (toolkit 2, step C8): a Markdown page in the
 *                       rich-text view (its blocks as lines), links, a block opened, a choice
 *   GUITEST preview     "GUITEST Preview" (toolkit 2, step C8): GUITEST embedded (another
 *                       of itself) shown inside a widget, then ended
 *   GUITEST embedded    "GUITEST Embedded": the program the preview page shows
 *   GUITEST tour        "GUITEST Tour" (toolkit 2, step C8): four steps pointing at a
 *                       button, a field, GLOS's Start button, and nowhere
 *   GUITEST terminal    "GUITEST Terminal" (toolkit 2, step C8): the toolkit's terminal fed
 *                       a build's output (colours, a title); a line's text by a double click
 *   GUITEST wizard      "GUITEST Wizard" (toolkit 2, step C8): four pages with steps,
 *                       Next held back until a name is typed, a page skipped
 *   GUITEST props       "GUITEST Properties" (toolkit 2, step C8): a property grid, two
 *                       groups, every kind of value edited
 *   GUITEST cview       "GUITEST Views" (toolkit 2, step C8): an image view (fitted,
 *                       zoomed) and a canvas view with rulers (presses and a drag in units)
 *   GUITEST picker      "GUITEST Picker" (toolkit 2, step C8): a palette (primary and
 *                       secondary colours, a swatch edited), colour buttons, one with alpha
 *   GUITEST meters      "GUITEST Meters" (toolkit 2, step C8): sliders across with marks
 *                       and a time, and upright; level meters with their peaks; a graph
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

/* ---- splitters (toolkit 2, step C5) */
static struct gui_widget *sp_side, *sp_out;

static void sp_where(struct gui_widget *w, int what)   /* both bars, as the app would keep them */
{
    char text[64];
    (void)w, (void)what;
    snprintf(text, sizeof text, "side %d fold %d, out %d fold %d", gui_split_get(sp_side), gui_split_folded(sp_side),
             gui_split_get(sp_out), gui_split_folded(sp_out));
    say("%s", text, 0);
}

static void sp_changed(struct gui_widget *w, int what)
{
    if (what == GUI_ON_CHANGE)
        say("moved%s %d", w == sp_side ? " side" : " out", gui_split_get(w));
}

static void sp_wider(struct gui_widget *w, int what)
{
    (void)w, (void)what;
    gui_resize(win, win->w + 100, win->h);
    say("wider", "", 0);
}

static void split_page(void)
{
    static const char *const heads[] = { "Name", "Size" };
    static const int widths[] = { 120, 60 };
    static const char *const rows[][2] = { { "README.TXT", "2 KB" }, { "GLOS.EXE", "57 KB" }, { "GLOSK.BIN", "951 KB" },
                                           { "FONTS.DAT", "109 KB" } };
    struct gui_widget *box, *row, *l, *t, *right, *out;
    struct gui_node *n;
    int i;
    win = gui_window_at("GUITEST Splitters", 0, 0, 420, 280, 0);       /* (at the corner: the script makes it 100
                                                   wider, which a window GLOS placed after many others may not have
                                                   room for: the gate's split "wider", 2026-11-17) */
    box = gui_vbox(win->root, -1, -1);
    gui_grow(box, 1);
    row = gui_hbox(box, -1, 0);
    gui_button(row, "&Where?", sp_where, 0);
    gui_button(row, "W&ider", sp_wider, 0);
    sp_side = gui_split(box, 0, 130);           /* the tree keeps its width */
    gui_grow(sp_side, 1);
    gui_on(sp_side, sp_changed, 0);
    t = gui_tree(sp_side);
    n = gui_tree_add(t, NULL, "C:", gui_icon("drive", 0), NULL, 1);
    gui_tree_add(t, n, "GLOS", gui_icon("folder", 0), NULL, 0);
    gui_tree_add(t, n, "FREEDOS", gui_icon("folder", 0), NULL, 0);
    gui_tree_expand(t, n, 1);
    sp_out = right = gui_split(sp_side, 1, -60);        /* the output pane keeps its height */
    gui_on(sp_out, sp_changed, 0);
    l = gui_list(right, 2, heads, widths);
    for (i = 0; i < 4; i++)
        gui_list_add(l, rows[i], gui_icon("file-text", 0), NULL);
    out = gui_vbox(right, 0, 4);
    gui_label(out, "Output: 4 files");
    status = gui_status(win);
    gui_show(win, 1);
    gui_focus(t);
    say("split", "", 0);
}

/* ---- tabs that close, move and overflow (toolkit 2, step C5) */
static struct gui_widget *dt;
static int dt_new;

static void dt_page(const char *title)
{
    struct gui_widget *p = gui_tab(dt, title), *f;
    gui_label(p, "A document's page:");
    f = gui_field(p, 24);
    gui_set_text(f, title);
}

static void dt_order(struct gui_widget *w, int what)   /* the pages' titles, in order */
{
    char text[160];
    int i, n = 0;
    struct gui_widget *p;
    (void)w, (void)what;
    text[0] = 0;
    for (i = 0; (p = gui_tabs_page(dt, i)) != NULL; i++)
        n += snprintf(text + n, sizeof text - (size_t)n, "%s%s", i ? "," : "", gui_get_text(p));
    say("order %s", text, 0);
}

static void dt_on(struct gui_widget *w, int what)
{
    if (what == GUI_ON_CLOSE) {
        int i = gui_tabs_closing(w);
        struct gui_widget *p = gui_tabs_page(w, i);
        char title[48];
        snprintf(title, sizeof title, "%s", gui_get_text(p));
        if (title[0] && title[strlen(title) - 1] == '*') {     /* changes: kept, this once (as "Save first?") */
            title[strlen(title) - 1] = 0;
            gui_set_text(p, title);
            gui_damage(w);
            say("kept %s (it had changes)", title, 0);
            return;
        }
        gui_tab_remove(w, i);
        say("closed %s", title, 0);
    } else if (what == GUI_ON_MOVE) {
        dt_order(w, 0);
    } else if (what == GUI_ON_NEW) {
        char title[24];
        snprintf(title, sizeof title, "Untitled %d", ++dt_new);
        dt_page(title);
        gui_tabs_set(w, 9999);                  /* (out of range: nothing; the new page is chosen below) */
        {
            int n;
            for (n = 0; gui_tabs_page(w, n); n++)
                ;
            gui_tabs_set(w, n - 1);
        }
        say("new %s", title, 0);
    } else if (what == GUI_ON_SELECT) {
        say("page%s %d", "", gui_tabs_get(w));
    }
}

static void doctabs(void)
{
    static const char *const titles[] = { "README.TXT", "NOTES.TXT*", "GLOS.CFG", "AUTOEXEC.BAT", "FDCONFIG.SYS",
                                          "TODO.TXT" };
    struct gui_widget *box, *row;
    int i;
    win = gui_window("GUITEST Document tabs", 360, 200, 0);
    box = gui_vbox(win->root, -1, -1);
    gui_grow(box, 1);
    row = gui_hbox(box, -1, 0);
    gui_button(row, "&Order?", dt_order, 0);
    dt = gui_tabs(box);
    gui_grow(dt, 1);
    gui_tabs_style(dt, GUI_TABS_SCROLL);
    gui_tabs_flags(dt, GUI_TABS_CLOSE | GUI_TABS_MOVE | GUI_TABS_NEW | GUI_TABS_MENU);
    for (i = 0; i < 6; i++)
        dt_page(titles[i]);
    gui_on(dt, dt_on, 0);
    status = gui_status(win);
    gui_show(win, 1);
    gui_focus(dt);
    say("doctabs", "", 0);
}

/* ---- docking panes (toolkit 2, step C5) */
static struct gui_widget *dk;
static char dk_saved[256];

static void dk_panes(const char *why)          /* each pane's side and whether it shows, by the API */
{
    static const char sides[] = "lrbf";
    char text[96];
    int id, n = 0;
    for (id = 1; id <= 4; id++)
        n += snprintf(text + n, sizeof text - (size_t)n, "%s%d:%c%d", id > 1 ? " " : "", id,
                      sides[gui_pane_side(dk, id)], gui_pane_shown(dk, id));
    say("%s", why, 0);
    say("panes %s", text, 0);
}

static void dk_on(struct gui_widget *w, int what)
{
    (void)w;
    if (what == GUI_ON_CHANGE)
        dk_panes("layout changed");
}

static void dk_view(struct gui_widget *w, int what)     /* the View menu's "Output" check, as a button */
{
    (void)w, (void)what;
    gui_pane_show(dk, 3, !gui_pane_shown(dk, 3));
    dk_panes(gui_pane_shown(dk, 3) ? "output shown" : "output hidden");
}

static void dk_save(struct gui_widget *w, int what)
{
    (void)w, (void)what;
    if (gui_dock_save(dk, dk_saved, sizeof dk_saved) > 0)
        dk_panes("saved");
}

static void dk_load(struct gui_widget *w, int what)
{
    (void)w, (void)what;
    if (gui_dock_load(dk, dk_saved) == 0)
        dk_panes("loaded");
}

static void dk_reset(struct gui_widget *w, int what)
{
    (void)w, (void)what;
    gui_dock_load(dk, "dock1 110 -100 -90;1 l 1;2 l 1;4 r 1;3 b 1");
    dk_panes("reset");
}

static void dk_width(struct gui_widget *w, int what)   /* the window narrower than its panes need, and wide again */
{
    char line[160];
    (void)what;
    gui_resize(win, w->id, win->h);
    gui_relayout(win);
    gui_step(0);                                /* (laid out at the new width before the layout is read) */
    if (gui_dock_save(dk, line, sizeof line) > 0)
        say("layout %s", line, 0);
    dk_panes(w->id < 390 ? "narrow" : "wide");
}

static void dock_page(void)
{
    static const char *const heads[] = { "Layer" };
    static const int widths[] = { 100 };
    static const char *const layers[] = { "Background", "Sketch", "Ink" };
    struct gui_widget *box, *row, *pane, *c, *l, *f;
    int i;
    win = gui_window_at("GUITEST Docking", 0, 0, 390, 330, 0);    /* (a floating pane goes beside it) */
    box = gui_vbox(win->root, -1, -1);
    gui_grow(box, 1);
    row = gui_hbox(box, -1, 0);
    gui_button(row, "&View Output", dk_view, 0);
    gui_button(row, "&Save layout", dk_save, 0);
    gui_button(row, "&Load layout", dk_load, 0);
    gui_button(row, "R&eset", dk_reset, 0);
    dk = gui_dock(box);
    gui_grow(dk, 1);
    gui_on(dk, dk_on, 0);
    c = gui_dock_centre(dk);
    gui_label(c, "The document");
    f = gui_field(c, 30);
    gui_set_text(f, "MOUNTAIN.PNG");
    row = gui_hbox(c, -1, 0);
    gui_button(row, "&Narrow", dk_width, 330);
    gui_button(row, "W&ide", dk_width, 430);
    pane = gui_pane(dk, 1, "Tools", GUI_DOCK_LEFT);
    gui_button(pane, "Pencil", NULL, 0);
    gui_button(pane, "Brush", NULL, 0);
    pane = gui_pane(dk, 2, "Layers", GUI_DOCK_LEFT);
    l = gui_list(pane, 1, heads, widths);
    gui_grow(l, 1);
    for (i = 0; i < 3; i++)
        gui_list_add(l, &layers[i], NULL, NULL);
    pane = gui_pane(dk, 3, "Output", GUI_DOCK_BOTTOM);
    gui_label(pane, "Saved MOUNTAIN.PNG (640x480)");
    pane = gui_pane(dk, 4, "Properties", GUI_DOCK_RIGHT);
    gui_label(pane, "Width: 640");
    gui_label(pane, "Height: 480");
    gui_dock_load(dk, "dock1 110 -100 -90;1 l 1;2 l 1;4 r 1;3 b 1");   /* (sides for the window's 390 pixels) */
    status = gui_status(win);
    gui_show(win, 1);
    gui_focus(f);
    say("dock", "", 0);
}

/* ---- combo boxes (toolkit 2, step C6) */
static struct gui_widget *cb_dir, *cb_name;

static void cb_on(struct gui_widget *w, int what)
{
    if (what == GUI_ON_ACTIVATE)
        say(w == cb_dir ? "go %s" : "name %s", gui_get_text(w), 0);
}

static void cb_names(struct gui_widget *w, const char *typed)  /* the app's items for what is typed */
{
    char item[40];
    int i;
    gui_combo_clear(w);
    for (i = 1; i <= 3; i++) {
        snprintf(item, sizeof item, "%.24s%d.txt", typed, i);
        gui_combo_add(w, item);
    }
}

static void cb_list(struct gui_widget *w, int what)    /* the folder box's list, in order */
{
    char text[200];
    int i, n = 0;
    (void)w, (void)what;
    text[0] = 0;
    for (i = 0; i < gui_combo_count(cb_dir) && n < (int)sizeof text - 40; i++)
        n += snprintf(text + n, sizeof text - (size_t)n, "%s%s", i ? "," : "", gui_combo_item(cb_dir, i));
    say("list %s", text, 0);
}

static void combo_page(void)
{
    static const char *const dirs[] = { "C:\\GLOS", "C:\\GAMES", "C:\\FREEDOS", "D:\\DATA" };
    struct gui_widget *form, *row;
    int i;
    win = gui_window("GUITEST Combo boxes", 360, 160, 0);
    form = gui_form(win->root);
    row = gui_row(form, "&Folder:", NULL);
    cb_dir = gui_combo(row, 24);
    for (i = 0; i < 4; i++)
        gui_combo_add(cb_dir, dirs[i]);
    gui_combo_history(cb_dir, 6);
    gui_on(cb_dir, cb_on, 0);
    row = gui_row(form, "&Name:", NULL);
    cb_name = gui_combo(row, 24);
    gui_combo_source(cb_name, cb_names);
    gui_on(cb_name, cb_on, 0);
    row = gui_hbox(win->root, -1, -1);
    gui_button(row, "&List?", cb_list, 0);
    status = gui_status(win);
    gui_show(win, 1);
    gui_focus(cb_dir);
    say("combo", "", 0);
}

/* ---- the small dialogs (toolkit 2, step C6) */
static int sd_dont;

static void sd_message(struct gui_widget *w, int what)
{
    int r;
    (void)what;
    r = gui_message_check(w->win, "Save changes?", "NOTES.TXT has changed. Save the changes before it closes?",
                          GUI_MB_YESNO, "&Don't ask again", &sd_dont);
    say(r == GUI_YES ? "message yes%s, checked %d" : "message no%s, checked %d", "", sd_dont);
}

static void sd_about(struct gui_widget *w, int what)
{
    static const char *const credits[] = { "stb_image 2.30 by Sean Barrett: none here, an example line", NULL };
    static const struct gui_about a = { "GUITEST", "Version 2.0 (toolkit 2)", "Copyright (c) 2026 the GLOS authors",
                                        "MIT licence: free to use, copy and change, with this notice kept.", credits,
                                        NULL };
    struct gui_about b = a;
    (void)what;
    b.icon = gui_icon("app-files", 1);
    gui_about(w->win, &b);
    say("about closed", "", 0);
}

static void sd_folder(struct gui_widget *w, int what)
{
    char path[160] = "C:\\GUI";
    (void)what;
    if (gui_folder_dialog(w->win, "Choose a folder", path, sizeof path)) {
        say("folder %s", path, 0);
        if (strstr(path, "NEWDIR"))             /* (the script's new folder taken away again: Loop A plays the other
                                                   scripts on the same disk after this one) */
            gui_rmdir(path);
    } else
        say("folder cancelled", "", 0);
}

static void smalldlg(void)
{
    struct gui_widget *row;
    win = gui_window("GUITEST Small dialogs", 320, 90, 0);
    row = gui_hbox(win->root, -1, -1);
    gui_button(row, "&Message", sd_message, 0);
    gui_button(row, "&About", sd_about, 0);
    gui_button(row, "&Folder", sd_folder, 0);
    status = gui_status(win);
    gui_show(win, 1);
    say("smalldlg", "", 0);
}

/* ---- Open and Save (toolkit 2, step C6) */
static const char *const fd_filters[] = { "Text|*.TXT;*.CFG", "Pictures|*.BMP;*.PNG;*.JPG;*.GIF;*.PCX", "All files|*.*",
                                          NULL };

static void fd_preview(struct gui_widget *pane, struct gfx_surface *s, const char *path, void *ctx)
{
    struct gui_dirent e;
    char line[64];
    (void)ctx;
    if (gui_stat(path, &e) != 0)
        return;
    gui_draw_text(s, pane->r.x + 6, pane->r.y + 6, gui_bold, gui_path_name(path), GUI_FIELD_TEXT);
    snprintf(line, sizeof line, "%lu bytes", e.size);
    gui_draw_text(s, pane->r.x + 6, pane->r.y + 24, gui_font, line, GUI_FIELD_TEXT);
}

static void fd_open(struct gui_widget *w, int what)
{
    static const char *const recent[] = { "C:\\GUI\\SUB", NULL };
    struct gui_file_opts o;
    char path[160] = "C:\\GUI", line[200];
    (void)what;
    memset(&o, 0, sizeof o);
    o.filters = fd_filters, o.recent = recent, o.preview = fd_preview;
    if (gui_file_dialog_ex(w->win, "Open a file", 0, path, sizeof path, &o)) {
        snprintf(line, sizeof line, "open %s filter %d", path, o.filter);
        say("%s", line, 0);
    } else {
        say("open cancelled", "", 0);
    }
}

static void fd_save(struct gui_widget *w, int what)
{
    static const char *const formats[] = { "Text (CR LF)", "Text (LF)", NULL };
    static char path[160] = "C:\\GUI\\NOTES.TXT";
    struct gui_file_opts o;
    char line[200];
    (void)what;
    memset(&o, 0, sizeof o);
    o.filters = fd_filters, o.formats = formats;
    if (gui_file_dialog_ex(w->win, "Save as", 1, path, sizeof path, &o)) {
        snprintf(line, sizeof line, "save %s format %d", path, o.format);
        say("%s", line, 0);
    } else {
        say("save cancelled", "", 0);
    }
}

static void filedlg(void)
{
    struct gui_widget *row;
    win = gui_window("GUITEST Open and Save", 300, 90, 0);
    row = gui_hbox(win->root, -1, -1);
    gui_button(row, "&Open...", fd_open, 0);
    gui_button(row, "&Save as...", fd_save, 0);
    status = gui_status(win);
    gui_show(win, 1);
    say("filedlg", "", 0);
}

/* ---- Find and Replace (toolkit 2, step C6): the page's "document" is one line, searched with gui_match */
static char fr_doc[200] = "The quick brown fox jumps over the lazy dog. The fox is quick.";
static int fr_at, fr_start = -1, fr_end = -1;   /* where the next search starts; the found text */
static struct gui_widget *fr_label;
static struct gui_find fr;

static void fr_show(void)
{
    gui_set_text(fr_label, fr_doc);
}

static void fr_on(struct gui_find *f, int what)
{
    struct gui_found m;
    char line[160], with[200];                  /* (say's own line is 200 bytes) */
    int len = (int)strlen(fr_doc), r, n = 0;
    if (what == GUI_FIND_NEXT || what == GUI_FIND_PREV) {
        r = gui_match(f->what, f->flags | (what == GUI_FIND_PREV ? GUI_FIND_BACK : 0), fr_doc, len,
                      what == GUI_FIND_PREV ? (fr_start >= 0 ? fr_start : len) : fr_at, &m);
        if (r < 0) {
            gui_find_say(f, "That expression is wrong.");
            say("bad expression", "", 0);
        } else if (!r) {
            gui_find_say(f, "Not found.");
            say("not found", "", 0);
        } else {
            fr_start = m.start, fr_end = m.end, fr_at = m.end;
            snprintf(line, sizeof line, "found %d-%d", m.start, m.end);
            say("%s", line, 0);
        }
        return;
    }
    if (what == GUI_FIND_REPLACE) {             /* the found text (if it is still there), then the next */
        if (fr_start >= 0 && gui_match(f->what, f->flags, fr_doc, len, fr_start, &m) == 1 && m.start == fr_start
            && gui_match_replace(f->with, f->flags, &m, fr_doc, with, sizeof with) >= 0
            && len - (m.end - m.start) + (int)strlen(with) < (int)sizeof fr_doc) {
            memmove(fr_doc + m.start + strlen(with), fr_doc + m.end, (size_t)(len - m.end + 1));
            memcpy(fr_doc + m.start, with, strlen(with));
            fr_at = m.start + (int)strlen(with);
            fr_start = fr_end = -1;
            fr_show();
            say("replaced: %s", fr_doc, 0);
        }
        fr_on(f, GUI_FIND_NEXT);
        return;
    }
    fr_at = 0;                                  /* GUI_FIND_ALL */
    while ((r = gui_match(f->what, f->flags, fr_doc, (len = (int)strlen(fr_doc)), fr_at, &m)) == 1
           && gui_match_replace(f->with, f->flags, &m, fr_doc, with, sizeof with) >= 0
           && len - (m.end - m.start) + (int)strlen(with) < (int)sizeof fr_doc && n < 50) {
        memmove(fr_doc + m.start + strlen(with), fr_doc + m.end, (size_t)(len - m.end + 1));
        memcpy(fr_doc + m.start, with, strlen(with));
        fr_at = m.start + (int)strlen(with) + (m.end == m.start);
        n++;
    }
    fr_start = fr_end = -1;
    fr_at = 0;
    fr_show();
    snprintf(line, sizeof line, "%d replaced.", n);
    gui_find_say(f, line);
    snprintf(line, sizeof line, "replaced %d: %.140s", n, fr_doc);
    say("%s", line, 0);
}

static void fr_open(struct gui_widget *w, int what)
{
    (void)what;
    fr.replace = w->id;
    fr.on_find = fr_on;
    gui_find(w->win, &fr);
}

static void find_page(void)
{
    struct gui_widget *box, *row;
    win = gui_window_at("GUITEST Find", 0, 0, 470, 90, 0);
    box = gui_vbox(win->root, -1, -1);
    fr_label = gui_label(box, "");
    fr_show();
    row = gui_hbox(box, -1, 0);
    gui_button(row, "&Find...", fr_open, 0);
    gui_button(row, "&Replace...", fr_open, 1);
    status = gui_status(win);
    gui_show(win, 1);
    say("find", "", 0);
}

/* ---- the Colour dialog (toolkit 2, step C6) */
static unsigned cl_rgb = 0x336699;

static void cl_choose(struct gui_widget *w, int what)
{
    char line[32];
    (void)what;
    if (gui_colour_dialog(w->win, "Choose a colour", &cl_rgb)) {
        snprintf(line, sizeof line, "colour #%06X", cl_rgb);
        say("%s", line, 0);
    } else {
        say("colour cancelled", "", 0);
    }
}

static void colour_page(void)
{
    struct gui_widget *row;
    win = gui_window("GUITEST Colour", 260, 80, 0);
    row = gui_hbox(win->root, -1, -1);
    gui_button(row, "&Colour...", cl_choose, 0);
    status = gui_status(win);
    gui_show(win, 1);
    say("colour", "", 0);
}

/* ---- the Font dialog (toolkit 2, step C6) */
static struct gui_fontspec fn_spec = { GUI_FONT_SANS, 1 };
static struct gui_widget *fn_line;

static void fn_choose(struct gui_widget *w, int what)
{
    (void)what;
    if (gui_font_dialog(w->win, "Choose a font", &fn_spec)) {
        const struct gfx_font *f = gui_font_get(fn_spec.face, fn_spec.size);
        if (f)
            gui_label_style(fn_line, f, GFX_NONE, -1);
        gui_relayout(w->win);
        say("font %s, size %d", fn_spec.face == GUI_FONT_SANS ? "sans" : fn_spec.face == GUI_FONT_BOLD ? "bold"
                                : "fixed", fn_spec.size);
    } else {
        say("font cancelled", "", 0);
    }
}

static void font_page(void)
{
    struct gui_widget *box, *row;
    win = gui_window("GUITEST Font", 360, 110, 0);
    box = gui_vbox(win->root, -1, -1);
    row = gui_hbox(box, -1, 0);
    gui_button(row, "F&ont...", fn_choose, 0);
    fn_line = gui_label(box, "A line in the font chosen.");
    status = gui_status(win);
    gui_show(win, 1);
    say("font", "", 0);
}

/* ---- undo (toolkit 2, step C7): the "document" is a number and a line of typing */
enum { U_UNDO = 100, U_REDO };
static struct gui_undo *ud;
static int ud_value;
static char ud_text[64];
static struct gui_widget *ud_label;

struct ud_change { int before, after; char text[16]; };       /* a number's change, or letters typed */

static void ud_show(const char *why)
{
    char line[160];
    int done, n = gui_undo_steps(ud, &done);
    snprintf(line, sizeof line, "value %d, text '%s'", ud_value, ud_text);
    gui_set_text(ud_label, line);
    snprintf(line, sizeof line, "%s: value %d text '%s' steps %d done %d undo %s redo %s", why, ud_value, ud_text, n,
             done, gui_undo_name(ud, 0) ? gui_undo_name(ud, 0) : "-", gui_undo_name(ud, 1) ? gui_undo_name(ud, 1) : "-");
    say("%s", line, 0);
}

static void ud_num_undo(void *d) { ud_value = ((struct ud_change *)d)->before; }
static void ud_num_redo(void *d) { ud_value = ((struct ud_change *)d)->after; }
static void ud_text_undo(void *d)
{
    ud_text[strlen(ud_text) - strlen(((struct ud_change *)d)->text)] = 0;
}
static void ud_text_redo(void *d) { strcat(ud_text, ((struct ud_change *)d)->text); }
static int ud_merge(void *into, void *d)
{
    struct ud_change *a = into, *b = d;
    if (strlen(a->text) + strlen(b->text) >= sizeof a->text)
        return 0;
    strcat(a->text, b->text);
    return 1;
}

static void ud_number(struct gui_widget *w, int what)  /* id 1: add one; 2: double */
{
    struct ud_change *c = calloc(1, sizeof *c);
    (void)what;
    c->before = ud_value;
    ud_value = c->after = w->id == 1 ? ud_value + 1 : ud_value * 2;
    gui_undo_push(ud, w->id == 1 ? "Add 1" : "Double", ud_num_undo, ud_num_redo, c, sizeof *c, free, NULL, 0);
}

static void ud_type(struct gui_widget *w, int what)    /* three letters, one at a time, merged into one step */
{
    int i;
    (void)w, (void)what;
    for (i = 0; i < 3 && strlen(ud_text) < sizeof ud_text - 2; i++) {
        struct ud_change *c = calloc(1, sizeof *c);
        c->text[0] = (char)('a' + (int)strlen(ud_text) % 26);
        strcat(ud_text, c->text);
        gui_undo_push(ud, "Typing", ud_text_undo, ud_text_redo, c, sizeof *c, free, ud_merge, 1);
    }
}

static void ud_command(struct gui_window *w, int id)
{
    (void)w;
    if (id == U_UNDO)
        gui_undo(ud);
    else if (id == U_REDO)
        gui_redo(ud);
}

static void ud_watch(struct gui_undo *u, void *ctx)    /* every change: the page shown and said */
{
    (void)u, (void)ctx;
    ud_show("state");
}

static void undo_page(void)
{
    struct gui_widget *box, *row, *list;
    struct gui_menubar *bar;
    struct gui_menu *m;
    win = gui_window_at("GUITEST Undo", 0, 0, 360, 260, 0);
    win->on_command = ud_command;
    bar = gui_menubar(win);
    m = gui_menu(bar, "&Edit");
    ud = gui_undo_new(0);
    gui_undo_commands(ud, win, U_UNDO, U_REDO);    /* (before the label: the watch comes after it) */
    gui_menu_command(m, U_UNDO);
    gui_menu_command(m, U_REDO);
    box = gui_vbox(win->root, -1, -1);
    gui_grow(box, 1);
    ud_label = gui_label(box, "");
    row = gui_hbox(box, -1, 0);
    gui_button(row, "Add &1", ud_number, 1);
    gui_button(row, "&Double", ud_number, 2);
    gui_button(row, "&Type", ud_type, 0);
    gui_label(box, "&History:");
    list = gui_undo_list(box, ud);
    gui_grow(list, 1);
    gui_undo_watch(ud, ud_watch, NULL);
    status = gui_status(win);
    gui_show(win, 1);
    ud_show("undo page");
}

/* ---- the clipboard's formats (toolkit 2, step C7) */
static void cp_all(struct gui_widget *w, int what)
{
    static const char *const files[] = { "C:\\GUI\\NOTES.TXT", "C:\\GUI\\EDIT.TXT" };
    unsigned pix[64], pf;
    int i, step = 0;
    glos_handle t = gui_clip_start();
    (void)w, (void)what;
    for (i = 0; i < 64; i++)                    /* an 8 by 8 chequer */
        pix[i] = (i / 8 + i % 8) & 1 ? 0xFF102030u : 0xFFF0E0D0u;
    pf = glos_clip_format("glos.guitest.rows");
    if (!t || (step = 1, gui_clip_put_text(t, "GLOS clipboard text", 19)) || (step = 2, gui_clip_put_image(t, pix, 8, 8))
        || (step = 3, gui_clip_put_files(t, files, 2)) || (step = 4, gui_clip_put(t, pf, "3 rows", 6))
        || (step = 5, gui_clip_done(t))) {
        char line[80];
        snprintf(line, sizeof line, "copy failed at step %d (format %u, error %d)", step, pf, glos_error());
        say("%s", line, 0);
        if (t && step < 5)
            glos_close(t);                      /* (the transaction not left open) */
        return;
    }
    say("copied all", "", 0);
}

static void cp_bad(struct gui_widget *w, int what)     /* a copy whose second put fails: nothing changes */
{
    glos_handle t = gui_clip_start();
    int a, b;
    (void)w, (void)what;
    a = gui_clip_put_text(t, "half a copy", 11);
    b = gui_clip_put(t, 9, "x", 1);             /* (9: no format there is) */
    gui_clip_done(t);
    say(a == 0 && b != 0 ? "bad copy refused%s" : "bad copy NOT refused%s", "", 0);
}

static void cp_text(struct gui_widget *w, int what)
{
    (void)w, (void)what;
    gui_clip_set("just text", 9);
    say("copied text", "", 0);
}

static void cp_paste(struct gui_widget *w, int what)   /* what the clipboard has now */
{
    char line[200], *t, **files, *priv;
    unsigned *pix;
    int iw = 0, ih = 0, n = 0, k;
    unsigned pf = glos_clip_format("glos.guitest.rows");
    size_t tn;
    (void)w, (void)what;
    k = snprintf(line, sizeof line, "has text %d image %d files %d private %d", gui_clip_has(GLOS_CLIP_TEXT),
                 gui_clip_has(GLOS_CLIP_IMAGE), gui_clip_has(GLOS_CLIP_FILES), gui_clip_has(pf));
    say("%s", line, 0);
    if ((t = gui_clip_get(&tn)) != NULL) {
        say("text '%s'", t, 0);
        free(t);
    }
    if ((pix = gui_clip_get_image(&iw, &ih)) != NULL) {
        snprintf(line, sizeof line, "image %dx%d, first %08X, second %08X", iw, ih, pix[0], pix[1]);
        say("%s", line, 0);
        free(pix);
    }
    if ((files = gui_clip_get_files(&n)) != NULL) {
        snprintf(line, sizeof line, "files %d: %s, %s", n, files[0], n > 1 ? files[1] : "-");
        say("%s", line, 0);
        free(files);
    }
    if ((priv = gui_clip_read(pf, NULL)) != NULL) {
        say("private '%s'", priv, 0);
        free(priv);
    }
    (void)k;
}

static void clip_page(void)
{
    struct gui_widget *row;
    win = gui_window("GUITEST Clipboard", 380, 80, 0);
    row = gui_hbox(win->root, -1, -1);
    gui_button(row, "Copy &all", cp_all, 0);
    gui_button(row, "Copy &text", cp_text, 0);
    gui_button(row, "&Bad", cp_bad, 0);
    gui_button(row, "&Paste?", cp_paste, 0);
    status = gui_status(win);
    gui_show(win, 1);
    say("clipfmt", "", 0);
}

/* ---- drag and drop (toolkit 2, step C7): two lists of names; the private format carries the dragged rows' places */
static char dd_fruit[8][16] = { "Apple", "Banana", "Cherry", "Damson", "Elderberry", "Fig" }, dd_basket[16][16] = {
    "Grape", "Kiwi" };
static int dd_nf = 6, dd_nb = 2;
static struct gui_widget *dd_lf, *dd_lb;
static unsigned dd_rows;                        /* the private format "glos.guitest.rows" */

static int dd_count(void *ctx) { return ctx == dd_fruit ? dd_nf : dd_nb; }

static void dd_row(void *ctx, int row, struct gui_vrow *out)
{
    out->cell[0] = ctx == dd_fruit ? dd_fruit[row] : dd_basket[row];
    out->icon = gui_glyph("star");
    out->id = (unsigned long)row + 1;
}

static int dd_offer(struct gui_widget *w, glos_handle t)       /* the fruit chosen: names (text) and places */
{
    unsigned long ids[8];
    int n = gui_vlist_chosen(w, ids, 8), i;
    char names[128] = "", rows[32] = "";
    if (n <= 0)
        return -1;
    for (i = 0; i < n && i < 8; i++) {
        snprintf(names + strlen(names), sizeof names - strlen(names), "%s%s", i ? "\n" : "", dd_fruit[ids[i] - 1]);
        snprintf(rows + strlen(rows), sizeof rows - strlen(rows), "%s%lu", i ? "," : "", ids[i] - 1);
    }
    gui_clip_put_text(t, names, strlen(names));
    gui_clip_put(t, dd_rows, rows, strlen(rows));
    return 0;
}

static void dd_done(struct gui_widget *w, unsigned effect)
{
    (void)w;
    say(effect == GLOS_DND_MOVE ? "drag done: moved%s" : effect == GLOS_DND_COPY ? "drag done: copied%s"
        : "drag done: nothing%s", "", 0);
}

static void dd_into_basket(struct gui_widget *w, int where, glos_handle data, unsigned effect)
{
    char *names = gui_drop_read(data, GLOS_CLIP_TEXT, NULL), *p, *nl, line[160];
    int k = 0;
    glos_close(data);
    if (!names)
        return;
    where = where < 0 || where > dd_nb ? dd_nb : where;
    for (p = names; *p && dd_nb < 16; p = nl ? nl + 1 : p + strlen(p), k++) {
        nl = strchr(p, '\n');
        if (nl)
            *nl = 0;
        memmove(dd_basket[where + k + 1], dd_basket[where + k], (size_t)(dd_nb - where - k) * sizeof dd_basket[0]);
        snprintf(dd_basket[where + k], sizeof dd_basket[0], "%s", p);
        dd_nb++;
        if (!nl)
            break;
    }
    free(names);
    gui_vlist_changed(w);
    snprintf(line, sizeof line, "basket: %s %s %s %s %s (%s at %d)", dd_basket[0], dd_basket[1], dd_nb > 2 ? dd_basket[2] : "",
             dd_nb > 3 ? dd_basket[3] : "", dd_nb > 4 ? dd_basket[4] : "", effect == GLOS_DND_COPY ? "copy" : "move", where);
    say("%s", line, 0);
}

static void dd_reorder(struct gui_widget *w, int where, glos_handle data, unsigned effect)    /* within the fruit */
{
    char *rows = gui_drop_read(data, dd_rows, NULL), line[160], keep[16];
    int from;
    glos_close(data);
    if (!rows || gui_drag_from() != w || effect != GLOS_DND_MOVE) {
        free(rows);
        return;
    }
    from = atoi(rows);                          /* (one row moved: the first chosen) */
    free(rows);
    if (from < 0 || from >= dd_nf || where < 0 || where > dd_nf)
        return;
    memcpy(keep, dd_fruit[from], sizeof keep);
    memmove(dd_fruit[from], dd_fruit[from + 1], (size_t)(dd_nf - from - 1) * sizeof dd_fruit[0]);
    if (where > from)
        where--;
    memmove(dd_fruit[where + 1], dd_fruit[where], (size_t)(dd_nf - 1 - where) * sizeof dd_fruit[0]);
    memcpy(dd_fruit[where], keep, sizeof keep);
    gui_vlist_changed(w);
    gui_vlist_set(w, where);
    snprintf(line, sizeof line, "fruit: %s %s %s %s %s %s", dd_fruit[0], dd_fruit[1], dd_fruit[2], dd_fruit[3], dd_fruit[4],
             dd_fruit[5]);
    say("%s", line, 0);
}

static const char *const dd_places[] = { "Kitchen", "Larder" };

static int dd_children(void *ctx, unsigned long id, unsigned long *ids, int room, int from)
{
    int i, k = 0;
    (void)ctx;
    if (id)
        return 0;
    for (i = from; i < 2 && k < room; i++)
        ids[k++] = (unsigned long)i + 1;
    return 2;
}

static void dd_node(void *ctx, unsigned long id, struct gui_tnode *out)
{
    (void)ctx;
    out->text = dd_places[id - 1];
    out->icon = gui_glyph("home");
}

static void dd_into_place(struct gui_widget *w, int where, glos_handle data, unsigned effect)
{
    char *names = gui_drop_read(data, GLOS_CLIP_TEXT, NULL), *nl, line[120];
    unsigned long id = gui_vtree_id_at(w, where);
    glos_close(data);
    if (!names)
        return;
    if ((nl = strchr(names, '\n')) != NULL)
        *nl = ' ';
    snprintf(line, sizeof line, "into %s: %s (%s)", id ? dd_places[id - 1] : "nowhere", names,
             effect == GLOS_DND_COPY ? "copy" : "move");
    free(names);
    say("%s", line, 0);
}

static void dnd_page(void)
{
    static const char *const hf[] = { "Fruit" }, *const hb[] = { "Basket" };
    static const int width[] = { 140 };
    static struct gui_model mf, mb;
    unsigned text_rows[2];
    static const struct gui_tmodel mt = { dd_children, dd_node, NULL };
    struct gui_widget *box, *row, *tree;
    dd_rows = glos_clip_format("glos.guitest.rows");
    mf.count = dd_count, mf.row = dd_row, mf.ctx = dd_fruit;
    mb.count = dd_count, mb.row = dd_row, mb.ctx = dd_basket;
    win = gui_window_at("GUITEST Drag and drop", 0, 0, 380, 270, 0);
    box = gui_vbox(win->root, -1, -1);
    gui_grow(box, 1);
    row = gui_hbox(box, -1, 0);
    gui_grow(row, 1);
    dd_lf = gui_vlist(row, 1, hf, width, &mf);
    gui_grow(dd_lf, 1);
    dd_lb = gui_vlist(row, 1, hb, width, &mb);
    gui_grow(dd_lb, 1);
    gui_vlist_multi(dd_lf, 1);
    gui_drag_source(dd_lf, GLOS_DND_COPY | GLOS_DND_MOVE, dd_offer, dd_done);
    text_rows[0] = dd_rows;
    gui_drop_target(dd_lf, text_rows, 1, GLOS_DND_MOVE, dd_reorder);
    text_rows[0] = GLOS_CLIP_TEXT, text_rows[1] = dd_rows;
    gui_drop_target(dd_lb, text_rows, 2, GLOS_DND_COPY, dd_into_basket);
    gui_label(box, "Places:");
    tree = gui_vtree(box, &mt);
    gui_size(tree, 0, 40);
    gui_drop_target(tree, text_rows, 1, GLOS_DND_COPY, dd_into_place);
    status = gui_status(win);
    gui_show(win, 1);
    say("dnd", "", 0);
}

/* ---- sliders, level meters and graphs (toolkit 2, step C8) */
static struct gui_widget *mt_seek, *mt_vol, *mt_stereo, *mt_mono, *mt_graph;
static int mt_t;

static void mt_time(int v, char *out, int room) { snprintf(out, (size_t)room, "%d:%02d", v / 60, v % 60); }

static void mt_slid(struct gui_widget *w, int what)
{
    char t[16];
    if (what != GUI_ON_CHANGE)
        return;
    if (w == mt_seek) {
        mt_time(gui_value(w), t, sizeof t);
        say("seek %s", t, 0);
    } else {
        say("volume%s %d", "", gui_value(w));
    }
}

static void mt_levels(struct gui_widget *w, int what)  /* a loud moment, then quieter: the peaks stay a while */
{
    (void)w, (void)what;
    gui_meter_set(mt_stereo, 950, 640);
    gui_meter_set(mt_mono, 820, 0);
    gui_meter_set(mt_stereo, 400, 300);
    gui_meter_set(mt_mono, 350, 0);
    say("levels", "", 0);
}

static void mt_feed(struct gui_widget *w, int what)    /* twenty samples of two series */
{
    static const int cpu[20] = { 10, 25, 40, 35, 60, 80, 75, 50, 45, 30, 20, 55, 90, 85, 60, 40, 30, 35, 20, 15 };
    int i, v[2];
    (void)w, (void)what;
    for (i = 0; i < 20; i++, mt_t++) {
        v[0] = cpu[i], v[1] = 100 - cpu[(i + 7) % 20];
        gui_graph_add(mt_graph, v);
    }
    say("fed", "", 0);
}

static void meters_page(void)
{
    struct gui_widget *box, *row, *col;
    win = gui_window_at("GUITEST Meters", 0, 0, 420, 270, 0);
    box = gui_vbox(win->root, -1, -1);
    row = gui_hbox(box, -1, 0);
    gui_label(row, "&Seek:");
    mt_seek = gui_slider(row, 0, 300, 83);
    gui_slider_style(mt_seek, GUI_SLIDER_VALUE, 60, mt_time);
    gui_on(mt_seek, mt_slid, 0);
    row = gui_hbox(box, 12, 0);
    col = gui_vbox(row, 2, 0);
    gui_label(col, "&Volume");
    mt_vol = gui_slider(col, 0, 100, 70);
    gui_slider_style(mt_vol, GUI_SLIDER_VERTICAL | GUI_SLIDER_VALUE, 25, NULL);
    gui_on(mt_vol, mt_slid, 0);
    col = gui_vbox(row, 4, 0);
    gui_label(col, "Levels:");
    mt_stereo = gui_meter(col, 2, 0);
    mt_mono = gui_meter(row, 1, 1);
    col = gui_vbox(row, 4, 0);
    mt_graph = gui_graph(col, 2, 40);
    gui_graph_series(mt_graph, 0, "CPU", GFX_NONE);
    gui_graph_series(mt_graph, 1, "Free", GFX_NONE);
    row = gui_hbox(box, -1, 0);
    gui_button(row, "&Levels", mt_levels, 0);
    gui_button(row, "&Feed", mt_feed, 0);
    status = gui_status(win);
    gui_show(win, 1);
    gui_focus(mt_seek);
    say("meters", "", 0);
}

/* ---- the rich-text view and Markdown (toolkit 2, step C8) */
static struct gui_widget *rt;

static const char rt_page[] =
    "# Getting started\n"
    "\n"
    "GLOS runs **DOS programs** and *its own* apps side by side. Press `Alt+Tab` to go between them,\n"
    "or see [the manual](help:manual).\n"
    "\n"
    "## Lists\n"
    "\n"
    "- Files\n"
    "- Notepad\n"
    "  - its find\n"
    "  - its undo\n"
    "1. First\n"
    "2. Second\n"
    "\n"
    "> A quote that is long enough to wrap on to\n"
    "> a second line in the view.\n"
    "\n"
    "```\n"
    "make all\n"
    "make test\n"
    "```\n"
    "\n"
    "| Key | Does |\n"
    "|-----|------|\n"
    "| F1 | Help |\n"
    "| Esc | Leaves |\n"
    "\n"
    "---\n"
    "\n"
    "<details>\n"
    "<summary>More about windows</summary>\n"
    "\n"
    "Windows can be **moved** and sized.\n"
    "\n"
    "</details>\n"
    "\n"
    "![The desktop](desk.png)\n"
    "\n"
    "A last paragraph with ~~old~~ new text and <https://glos.example/>.\n";

static void rt_on(struct gui_widget *w, int what)
{
    if (what == GUI_ON_ACTIVATE)
        say("link %s", gui_richview_link(w), 0);
}

static void rt_chosen(struct gui_widget *w, int what)
{
    char *t = gui_richview_text(rt, 1), line[96];
    int i;
    (void)w, (void)what;
    for (i = 0; t && t[i] && i < 60; i++)       /* (its first characters, its line ends shown as |) */
        line[i] = t[i] == '\r' ? '|' : t[i] == '\n' ? '|' : t[i];
    line[i] = 0;
    say("chosen: %s", line, 0);
    free(t);
}

static void rich_page(void)
{
    struct gui_rdoc *d = gui_markdown(rt_page, -1, NULL, NULL);
    struct gui_widget *box;
    char line[200];
    int i;
    for (i = 0; i < gui_rdoc_blocks(d); i++) {  /* (what the Markdown made, block by block) */
        gui_rdoc_describe(d, i, line, sizeof line);
        printf("GUI block %d %s\n", i, line);
    }
    win = gui_window_at("GUITEST Rich text", 0, 0, 420, 320, 0);
    box = gui_vbox(win->root, 4, 0);
    gui_grow(box, 1);
    rt = gui_richview(box);
    gui_grow(rt, 1);
    gui_richview_set(rt, d);
    gui_on(rt, rt_on, 0);
    gui_button(box, "&Chosen", rt_chosen, 0);
    status = gui_status(win);
    gui_show(win, 1);
    gui_focus(rt);
    say("rich", "", 0);
}

/* ---- the chat view (toolkit 2, step C8) */
static struct gui_widget *ch_view;
static int ch_n;

static void ch_count(void) { say("blocks %s%d", "", gui_rdoc_blocks(gui_richview_doc(ch_view))); }

static void ch_send(struct gui_widget *w, int what)
{
    char t[64];
    (void)w, (void)what;
    snprintf(t, sizeof t, "message %d, with **bold** and `code`", ++ch_n);
    gui_chat_line(ch_view, "you", 0x2060D0, t);
    ch_count();
}

static void ch_many(struct gui_widget *w, int what)
{
    int i;
    char t[64];
    (void)w, (void)what;
    for (i = 0; i < 15; i++) {
        snprintf(t, sizeof t, "line %d of a long talk", ++ch_n);
        gui_chat_line(ch_view, i & 1 ? "bob" : "alice", i & 1 ? 0x208040 : 0xC03020, t);
    }
    ch_count();
}

static void chat_page(void)
{
    struct gui_widget *box, *row;
    win = gui_window_at("GUITEST Chat", 0, 0, 360, 260, 0);
    box = gui_vbox(win->root, 4, 0);
    gui_grow(box, 1);
    ch_view = gui_chatview(box, 12);
    gui_grow(ch_view, 1);
    gui_chat_line(ch_view, "alice", 0xC03020, "Hello! Is the *new* build up?");
    gui_chat_line(ch_view, "bob", 0x208040, "Yes: see [the notes](notes:build) for what changed.");
    gui_chat_markdown(ch_view, "A message of two parts:\n\n- one\n- two");
    row = gui_hbox(box, 4, 0);
    gui_button(row, "&Send", ch_send, 0);
    gui_button(row, "&Many", ch_many, 0);
    status = gui_status(win);
    gui_show(win, 1);
    gui_focus(ch_view);
    say("chat", "", 0);
    ch_count();
}

/* ---- the preview surface (toolkit 2, step C8): another GUITEST inside a widget */
static const char *self;                        /* GUITEST's own path (argv[0]) */
static struct gui_widget *pv;

static void pv_on(struct gui_widget *w, int what)
{
    struct glos_win_info info;
    char line[100];
    if (what != GUI_ON_CHANGE)
        return;
    if (!gui_preview_window(w)) {
        say("preview ended", "", 0);
        return;
    }
    if (gui_preview_info(w, &info) == 0)        /* (its size: the widget's, less its frame) */
        snprintf(line, sizeof line, "preview in: %s, %dx%d", info.title, info.w, info.h);
    else
        snprintf(line, sizeof line, "preview in: no size");
    say("%s", line, 0);
}

static void pv_start(struct gui_widget *w, int what)
{
    char cmd[200], *p;
    (void)w, (void)what;
    snprintf(cmd, sizeof cmd, "%s embedded", self);
    for (p = cmd; *p && *p != ' '; p++)         /* (DJGPP's argv[0] is c:/test/guitest.exe: DOS's is wanted, or the
                                                   agent runs it through COMMAND.COM, under which it may not be an
                                                   app) */
        if (*p == '/')
            *p = '\\';
    if (gui_preview_run(pv, cmd) != 0)
        say("preview not started", "", 0);
}

static void pv_stop(struct gui_widget *w, int what)
{
    (void)w, (void)what;
    gui_preview_stop(pv);
}

static void preview_page(void)
{
    struct gui_widget *box, *row;
    win = gui_window_at("GUITEST Preview", 0, 0, 300, 230, 0);
    box = gui_vbox(win->root, 8, 10);
    gui_grow(box, 1);
    pv = gui_preview(box);
    gui_size(pv, 202, 122);
    gui_on(pv, pv_on, 0);
    row = gui_hbox(box, 4, 0);
    gui_button(row, "&Start", pv_start, 0);
    gui_button(row, "S&top", pv_stop, 0);
    status = gui_status(win);
    gui_show(win, 1);
    say("preview", "", 0);
}

static void embedded_draw(struct gui_widget *w, struct gfx_surface *s)
{
    gfx_fill(s, w->r.x, w->r.y, w->r.w, w->r.h, 0x3060C0);
    gfx_text(s, w->r.x + 8, w->r.y + 8, gui_bold, "Embedded", -1, 0xFFFFFF);
}

static void embedded_page(void)                 /* (the program the preview shows) */
{
    struct gui_widget *c;
    win = gui_window("GUITEST Embedded", 160, 100, 0);
    c = gui_canvas(win->root, 0, 0, embedded_draw, NULL);
    gui_grow(c, 1);
    gui_show(win, 1);
}

/* ---- the tour (toolkit 2, step C8) */
static struct gui_tour_step tr_steps[4];

static void tr_done(int step, int finished)
{
    say(finished ? "tour finished at step %s%d" : "tour closed at step %s%d", "", step + 1);
}

static void tr_start(struct gui_widget *w, int what)
{
    (void)w, (void)what;
    gui_tour(win, tr_steps, 4, tr_done);
}

static void tour_page(void)
{
    struct gui_widget *box, *row, *open, *f;
    win = gui_window_at("GUITEST Tour", 160, 80, 300, 160, 0);
    box = gui_vbox(win->root, 8, 10);
    row = gui_hbox(box, 4, 0);
    open = gui_button(row, "&Open", NULL, 0);
    gui_button(row, "&Save", NULL, 0);
    f = gui_form(box);
    f = gui_field(gui_row(f, "&Find:", NULL), 16);
    gui_button(box, "&Tour", tr_start, 0);
    tr_steps[0].title = "Open a file", tr_steps[0].text = "Open shows the files you can read here.", tr_steps[0].at = open;
    tr_steps[1].title = "Find", tr_steps[1].text = "Type a word here to find it in the file.", tr_steps[1].at = f;
    tr_steps[2].title = "GLOS's programs", tr_steps[2].text = "Every program is under GLOS's mark at the task bar's left.";
    tr_steps[2].point = GLOS_POINT_START;
    tr_steps[3].title = "That is all", tr_steps[3].text = "Press Done to start.";
    status = gui_status(win);
    gui_show(win, 1);
    say("tour", "", 0);
}

/* ---- the terminal widget (toolkit 2, step C8): an IDE's output, no program behind it */
static struct gui_widget *tw;

static void tw_line(struct gui_widget *w, int line, const char *text)
{
    char out[160];
    (void)w;
    snprintf(out, sizeof out, "line %d: %s", line, text);
    say("%s", out, 0);
}

static void tw_send(struct gui_widget *w, const char *d, int n, int typed)
{
    char out[64];
    int i, k = 0;
    (void)w;
    for (i = 0; i < n && k < (int)sizeof out - 5; i++)  /* (what would go to a program, as text) */
        k += snprintf(out + k, sizeof out - (size_t)k, (unsigned char)d[i] < 0x20 ? "^%c" : "%c",
                      (unsigned char)d[i] < 0x20 ? d[i] + 0x40 : d[i]);
    say(typed ? "typed %s" : "answered %s", out, 0);
}

static void tw_on(struct gui_widget *w, int what)
{
    if (what == GUI_ON_CHANGE)
        say("title %s", gui_term_title(w), 0);
}

static void terminal_page(void)
{
    static const struct gui_term_cb cb = { tw_send, NULL, tw_line };
    static const char out[] =
        "\033]0;Building\007"
        "\033[1mmake\033[0m all\r\n"
        "  CC      main.c\r\n"
        "  CC      draw.c\r\n"
        "draw.c:42:7: \033[31merror:\033[0m 'width' undeclared\r\n"
        "  \033[32mok\033[0m      2 files, \033[33m1 warning\033[0m\r\n"
        "$ ";
    win = gui_window_at("GUITEST Terminal", 0, 0, 420, 200, 0);
    tw = gui_terminal(gui_vbox(win->root, 0, 0), 100);
    gui_grow(tw->parent, 1);
    gui_grow(tw, 1);
    gui_term_callbacks(tw, &cb);
    gui_on(tw, tw_on, 0);
    status = gui_status(win);
    gui_show(win, 1);
    gui_focus(tw);
    gui_term_feed(tw, out, (int)sizeof out - 1);
    say("terminal", "", 0);
}

/* ---- the wizard (toolkit 2, step C8) */
static struct gui_widget *wz, *wz_name, *wz_skip, *wz_quick, *wz_drive;

static void wz_on(struct gui_widget *w, int what)
{
    char line[96];
    if (w == wz_name && what == GUI_ON_CHANGE) {
        gui_wizard_next_ok(wz, gui_get_text(w)[0] != 0);
        return;
    }
    if (w == wz_skip && what == GUI_ON_CHANGE) {
        gui_wizard_skip(wz, 2, gui_checked(w));
        return;
    }
    if (w != wz)
        return;
    if (what == GUI_ON_CHANGE) {
        if (gui_wizard_at(wz) == 1)
            gui_wizard_next_ok(wz, gui_get_text(wz_name)[0] != 0);
        say("page %s%d", "", gui_wizard_at(wz));
    } else if (what == GUI_ON_ACTIVATE) {
        snprintf(line, sizeof line, "finished: %s on %s, quick=%d", gui_get_text(wz_name),
                 gui_checked(wz_drive) ? "A:" : "B:", gui_checked(wz_quick));
        say("%s", line, 0);
    } else if (what == GUI_ON_CLOSE) {
        say("cancelled", "", 0);
    }
}

static void wizard_page(void)
{
    struct gui_widget *p, *f;
    win = gui_window_at("GUITEST Wizard", 0, 0, 470, 300, 0);
    wz = gui_wizard(win->root, GUI_WIZ_STEPS);
    gui_grow(wz, 1);
    gui_on(wz, wz_on, 0);
    p = gui_wizard_page(wz, "Choose a disk", "The floppy disk to make ready. Everything on it will be lost.");
    wz_drive = gui_radio(p, "Drive &A:", NULL);
    gui_radio(p, "Drive &B:", wz_drive);
    gui_set_checked(wz_drive, 1);
    wz_skip = gui_check(p, "&Skip the options", 0);
    gui_on(wz_skip, wz_on, 0);
    p = gui_wizard_page(wz, "Name it", "A name of up to eleven letters, shown when the disk is read.");
    f = gui_form(p);
    wz_name = gui_field(gui_row(f, "&Name:", NULL), 11);
    gui_on(wz_name, wz_on, 0);
    p = gui_wizard_page(wz, "Options", "A quick format only clears the disk's tables.");
    wz_quick = gui_check(p, "&Quick format", 0);
    gui_wizard_page(wz, "Ready", "Press Finish to make the disk ready, or Back to change something.");
    status = gui_status(win);
    gui_show(win, 1);
    say("wizard", "", 0);
}

/* ---- the property grid (toolkit 2, step C8) */
static void pg_on(struct gui_widget *w, int what)
{
    static const char *const kinds[] = { "text", "number", "yes-no", "choice", "colour", "font" };
    int id = gui_prop_changed(w);
    const struct gui_prop *p = gui_prop_get(w, id);
    char line[96];
    if (what != GUI_ON_CHANGE || !p)
        return;
    if (p->kind == GUI_PROP_TEXT)
        snprintf(line, sizeof line, "property %d (%s): %s", id, kinds[p->kind], p->text ? p->text : "");
    else if (p->kind == GUI_PROP_COLOUR)
        snprintf(line, sizeof line, "property %d (%s): %06X", id, kinds[p->kind], p->rgb);
    else if (p->kind == GUI_PROP_FONT)
        snprintf(line, sizeof line, "property %d (%s): %d %d", id, kinds[p->kind], p->font.face, p->font.size);
    else
        snprintf(line, sizeof line, "property %d (%s): %d", id, kinds[p->kind], p->value);
    say("%s", line, 0);
}

static void props_page(void)
{
    static const char *const aligns[] = { "Left", "Centre", "Right", NULL };
    struct gui_prop p;
    struct gui_widget *g;
    win = gui_window_at("GUITEST Properties", 0, 0, 320, 300, 0);
    g = gui_propgrid(win->root);
    gui_grow(g, 1);
    gui_on(g, pg_on, 0);
    gui_prop_group(g, "Appearance");
    memset(&p, 0, sizeof p);
    p.kind = GUI_PROP_TEXT, p.text = "Untitled";
    gui_prop_add(g, "Title", "The text in the window's title bar.", &p);
    memset(&p, 0, sizeof p);
    p.kind = GUI_PROP_NUMBER, p.value = 320, p.min = 100, p.max = 2000;
    gui_prop_add(g, "Width", "Its width in pixels, 100 to 2000.", &p);
    memset(&p, 0, sizeof p);
    p.kind = GUI_PROP_BOOL, p.value = 1;
    gui_prop_add(g, "Visible", "Whether it is shown.", &p);
    memset(&p, 0, sizeof p);
    p.kind = GUI_PROP_CHOICE, p.items = aligns;
    gui_prop_add(g, "Align", "Where its text goes.", &p);
    memset(&p, 0, sizeof p);
    p.kind = GUI_PROP_COLOUR, p.rgb = 0x3060C0;
    gui_prop_add(g, "Colour", "Its ground.", &p);
    memset(&p, 0, sizeof p);
    p.kind = GUI_PROP_FONT, p.font.face = GUI_FONT_SANS, p.font.size = 1;
    gui_prop_add(g, "Font", "Its text's font.", &p);
    gui_prop_group(g, "Behaviour");
    memset(&p, 0, sizeof p);
    p.kind = GUI_PROP_NUMBER, p.value = 3, p.min = 0, p.max = 10;
    gui_prop_add(g, "Retries", "How often it tries again.", &p);
    memset(&p, 0, sizeof p);
    p.kind = GUI_PROP_BOOL;
    gui_prop_add(g, "Log", "Whether it keeps a log.", &p);
    status = gui_status(win);
    gui_show(win, 1);
    gui_focus(g);
    say("props", "", 0);
}

/* ---- the image view and the canvas view (toolkit 2, step C8) */
static unsigned cv_pic[320 * 200];
static struct gui_widget *cv_img, *cv_can;
static int cv_mark_x = -1, cv_mark_y;

static void cv_draw(struct gui_widget *w, struct gfx_surface *s, const struct gui_viewport *v)
{
    int x, y;
    (void)w;
    gfx_fill(s, v->ox, v->oy, 400 * v->num / v->den, 300 * v->num / v->den, 0xFFFFFF);  /* the page */
    for (x = 0; x <= 400; x += 50)              /* a grid every 50 units */
        gfx_fill(s, v->ox + x * v->num / v->den, v->oy, 1, 300 * v->num / v->den, 0xC0C0E0);
    for (y = 0; y <= 300; y += 50)
        gfx_fill(s, v->ox, v->oy + y * v->num / v->den, 400 * v->num / v->den, 1, 0xC0C0E0);
    gfx_fill(s, v->ox + 100 * v->num / v->den, v->oy + 100 * v->num / v->den, 100 * v->num / v->den,
             50 * v->num / v->den, 0x3060C0);
    if (cv_mark_x >= 0)                         /* where the last press was */
        gfx_fill(s, v->ox + cv_mark_x * v->num / v->den - 2, v->oy + cv_mark_y * v->num / v->den - 2, 5, 5, 0xD02020);
}

static int cv_event(struct gui_widget *w, struct gui_ev *e, int x, int y)
{
    char line[64];
    if (e->type == GUI_EV_PRESS && e->button == 1) {
        cv_mark_x = x, cv_mark_y = y;
        gui_damage(w);
        snprintf(line, sizeof line, "canvas press %d,%d", x, y);
        say("%s", line, 0);
        return 1;
    }
    if (e->type == GUI_EV_RELEASE && e->button == 1) {
        snprintf(line, sizeof line, "canvas let go %d,%d", x, y);
        say("%s", line, 0);
        return 1;
    }
    return 0;
}

static void cv_zoomed(struct gui_widget *w, int what)
{
    if (what == GUI_ON_CHANGE)
        say(w == cv_img ? "image zoom %s%d" : "canvas zoom %s%d", "", gui_cview_get_zoom(w));
}

static void cview_page(void)
{
    struct gui_widget *row;
    int x, y;
    for (y = 0; y < 200; y++)                   /* a picture: red across, green down, a white square, a black line */
        for (x = 0; x < 320; x++)
            cv_pic[y * 320 + x] = (unsigned)(x * 255 / 319) << 16 | (unsigned)(y * 255 / 199) << 8 | 0x80;
    for (y = 80; y < 120; y++)
        for (x = 140; x < 180; x++)
            cv_pic[y * 320 + x] = 0xFFFFFF;
    for (x = 0; x < 200; x++)
        cv_pic[x * 320 + x + 60] = 0;
    win = gui_window_at("GUITEST Views", 0, 0, 440, 260, 0);
    row = gui_hbox(win->root, 8, 8);
    gui_grow(row, 1);
    cv_img = gui_image_view(row);
    gui_image_view_set(cv_img, cv_pic, 320, 200, 320);
    gui_grow(cv_img, 1);
    gui_on(cv_img, cv_zoomed, 0);
    cv_can = gui_canvas_view(row, 400, 300, cv_draw, cv_event);
    gui_cview_style(cv_can, GUI_CV_RULERS | GUI_CV_RING, 10);
    gui_cview_cursor(cv_can, GLOS_CURSOR_CROSS);
    gui_grow(cv_can, 1);
    gui_on(cv_can, cv_zoomed, 0);
    status = gui_status(win);
    gui_show(win, 1);
    gui_focus(cv_img);
    say("cview", "", 0);
}

/* ---- the colour picker: a palette and colour buttons (toolkit 2, step C8) */
static struct gui_widget *pk_pal, *pk_window, *pk_glass;

static void pk_on(struct gui_widget *w, int what)
{
    char line[80];
    if (w == pk_pal && what == GUI_ON_SELECT)
        snprintf(line, sizeof line, "primary %06X, secondary %06X", gui_palette_get(w, 0), gui_palette_get(w, 1));
    else if (w == pk_pal && what == GUI_ON_CHANGE)
        snprintf(line, sizeof line, "swatch edited: %06X", gui_palette_colour(w, 0));
    else if (what == GUI_ON_CHANGE)
        snprintf(line, sizeof line, "%s %08X", w == pk_glass ? "glass" : "window", gui_colour_get(w));
    else
        return;
    say("%s", line, 0);
}

static void picker_page(void)
{
    static const unsigned paint[28] = {
        0x000000, 0x808080, 0x800000, 0x808000, 0x008000, 0x008080, 0x000080, 0x800080, 0x808040, 0x004040,
        0x0080FF, 0x004080, 0x8000FF, 0x804000, 0xFFFFFF, 0xC0C0C0, 0xFF0000, 0xFFFF00, 0x00FF00, 0x00FFFF,
        0x0000FF, 0xFF00FF, 0xFFFF80, 0x00FF80, 0x80FFFF, 0x8080FF, 0xFF0080, 0xFF8040 };
    struct gui_widget *box, *form;
    win = gui_window_at("GUITEST Picker", 0, 0, 330, 160, 0);
    box = gui_vbox(win->root, 6, 10);
    gui_grow(box, 1);
    gui_label(box, "Palette:");
    pk_pal = gui_palette(box, paint, 28, 14);
    gui_on(pk_pal, pk_on, 0);
    form = gui_form(box);
    pk_window = gui_colour_button(gui_row(form, "&Window:", NULL), 0x3060C0, 0);
    gui_on(pk_window, pk_on, 0);
    pk_glass = gui_colour_button(gui_row(form, "&Glass:", NULL), 0x80FFFFFFu, GUI_COLOUR_ALPHA);
    gui_on(pk_glass, pk_on, 0);
    status = gui_status(win);
    gui_show(win, 1);
    gui_focus(pk_pal);
    say("picker", "", 0);
}

/* ---- the calendar and the date and time fields (toolkit 2, step C8) */
static struct gui_widget *cl_cal, *cl_date, *cl_time;

static int cl_mark(void *ctx, int date) { (void)ctx; return date == 20261105 || date == 20261125; }

static void cl_on(struct gui_widget *w, int what)
{
    char a[16], b[16], line[64];
    int to, from;
    if (what != GUI_ON_CHANGE && what != GUI_ON_ACTIVATE)
        return;
    if (w == cl_cal) {
        from = gui_calendar_get(w, &to);
        gui_date_text(from, a, sizeof a), gui_date_text(to, b, sizeof b);
        snprintf(line, sizeof line, "%s %s to %s (%s)", what == GUI_ON_ACTIVATE ? "opened" : "days", a, b,
                 gui_weekday(from) == 0 ? "a Sunday" : "not a Sunday");
    } else if (w == cl_date) {
        gui_date_text(gui_datetime_get(w), a, sizeof a);
        snprintf(line, sizeof line, "date %s", a);
    } else {
        snprintf(line, sizeof line, "time %06d", gui_datetime_get(w));
    }
    say("%s", line, 0);
}

static void calendar_page(void)
{
    struct gui_widget *row, *form;
    win = gui_window_at("GUITEST Calendar", 0, 0, 400, 190, 0);
    row = gui_hbox(win->root, 12, 0);
    gui_grow(row, 1);
    cl_cal = gui_calendar(row, 20261117);
    gui_calendar_style(cl_cal, GUI_CAL_RANGE, 20261117, cl_mark, NULL);
    gui_on(cl_cal, cl_on, 0);
    form = gui_form(row);
    cl_date = gui_date_field(gui_row(form, "&Date:", NULL), 20261117);
    gui_on(cl_date, cl_on, 0);
    cl_time = gui_time_field(gui_row(form, "&Time:", NULL), 140530, 1);
    gui_on(cl_time, cl_on, 0);
    status = gui_status(win);
    gui_show(win, 1);
    gui_focus(cl_cal);
    say("calendar", "", 0);
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
    } else {
        for (i = 0; chosen[i] && o < (int)sizeof line - 5; i++)      /* (DOS's bytes: those above 7Fh as \xNN) */
            o += snprintf(line + o, sizeof line - (size_t)o, (unsigned char)chosen[i] >= 0x80 ? "\\x%02X" : "%c",
                          (unsigned char)chosen[i]);
        printf("GUI chose %s\n", line);
    }
    for (i = 0; list[i]; i++) {                 /* (Loop A plays every script on one disk: the files and the
                                                   folder go, or later scripts' folder pictures show them) */
        gui_text_dos(list[i], dos, sizeof dos);
        gui_path_join(path, sizeof path, dir, dos);
        gui_unlink(path);
    }
    gui_rmdir(dir);
}

int main(int argc, char **argv)
{
    const char *what = argc > 1 ? argv[1] : "gallery";
    self = argv[0];
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
    else if (!strcmp(what, "split"))
        split_page();
    else if (!strcmp(what, "doctabs"))
        doctabs();
    else if (!strcmp(what, "dock"))
        dock_page();
    else if (!strcmp(what, "combo"))
        combo_page();
    else if (!strcmp(what, "smalldlg"))
        smalldlg();
    else if (!strcmp(what, "filedlg"))
        filedlg();
    else if (!strcmp(what, "find"))
        find_page();
    else if (!strcmp(what, "colour"))
        colour_page();
    else if (!strcmp(what, "font"))
        font_page();
    else if (!strcmp(what, "undo"))
        undo_page();
    else if (!strcmp(what, "clipfmt"))
        clip_page();
    else if (!strcmp(what, "dnd"))
        dnd_page();
    else if (!strcmp(what, "meters"))
        meters_page();
    else if (!strcmp(what, "calendar"))
        calendar_page();
    else if (!strcmp(what, "picker"))
        picker_page();
    else if (!strcmp(what, "cview"))
        cview_page();
    else if (!strcmp(what, "props"))
        props_page();
    else if (!strcmp(what, "wizard"))
        wizard_page();
    else if (!strcmp(what, "terminal"))
        terminal_page();
    else if (!strcmp(what, "tour"))
        tour_page();
    else if (!strcmp(what, "preview"))
        preview_page();
    else if (!strcmp(what, "rich"))
        rich_page();
    else if (!strcmp(what, "chat"))
        chat_page();
    else if (!strcmp(what, "embedded"))
        embedded_page();
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
