/* HELP.EXE: Help (GLOS v1, phase F; docs/v1/apps/help.md), the first part: GLOS's user guide in a window.
 *
 * The guide is Markdown pages (help/ in the sources) in one file beside the programs, HELP.DAT (tools/mkhelp.py
 * makes it, and holds the pages to their rules: every link goes somewhere, every page is in the contents). Here:
 *
 *   the contents  a tree at the left, from the list of links in the guide's first page (INDEX.MD), nested as
 *                 the list is;
 *   a page        in the toolkit's rich-text view, by its Markdown reader; a link to another page opens it;
 *   the way back  Back and Forward over the pages seen, Home;
 *   search        words looked for in every page (letters in either case): the lines that have them, each
 *                 opening its page.
 *
 * HELP NAME opens the page NAME.MD (the first page if there is none): F1 in any program is the toolkit's
 * (gui_help), which starts HELP with the program's own name, so NOTEPAD's F1 is the page NOTEPAD.MD.
 *
 * Not yet: pictures in pages, the place in a page a link names (#part) and where a search's words are, the
 * welcome tour and the tip of the day, a dialog's own Help button, one Help window for every F1.
 *
 * The suite's lines (tests/gui/help.txt): "HELP ..." on the output. */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/glos.h>
#include <glos/gui.h>

enum { C_BACK = 1, C_FORWARD, C_HOME, C_FIND, C_CONTENTS, C_ABOUT, C_EXIT };

#define PAGES 96
#define TRAIL 64

struct page {
    char name[16];                      /* NAME.MD */
    char title[80];                     /* its first line's, less the "# " */
    const char *text;
    int len;
    struct gui_node *node;              /* its line in the contents, or NULL */
};

static struct gui_window *win;
static struct gui_widget *tabs, *tree, *field, *results, *view, *status;
static char *dat;                       /* HELP.DAT, whole */
static struct page pages[PAGES];
static int npages, shown = -1;
static int trail[TRAIL], ntrail, at;    /* the pages seen; `at` the one shown */
static int quiet;                       /* (the tree is being set to the page shown: not a choice of the user's) */

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    va_list ap;
    printf("HELP ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

/* ---- the guide's file */

static int load(void)
{
    FILE *f = gui_open_beside("HELP.DAT");
    long size;
    char *p, *end;
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    rewind(f);
    if (size < 12 || size > 4l * 1024 * 1024 || !(dat = malloc((size_t)size + 1)) || fread(dat, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        return -1;
    }
    fclose(f);
    dat[size] = 0;
    end = dat + size;
    if (strncmp(dat, "GLOSHELP 1\n", 11))
        return -1;
    for (p = dat + 11; p < end && npages < PAGES;) {
        struct page *g = &pages[npages];
        char *line_end = memchr(p, '\n', (size_t)(end - p)), *next;
        size_t n;
        if (strncmp(p, "=== ", 4) || !line_end)
            return -1;
        n = (size_t)(line_end - p - 4);
        if (n == 0 || n >= sizeof g->name)
            return -1;
        memcpy(g->name, p + 4, n);
        g->name[n] = 0;
        p = line_end + 1;
        for (next = p; next < end && (strncmp(next, "=== ", 4) || (next > p && next[-1] != '\n'));) {
            char *nl = memchr(next, '\n', (size_t)(end - next));
            next = nl ? nl + 1 : end;
        }
        g->text = p;
        g->len = (int)(next - p);
        memcpy(g->title, g->name, n + 1);       /* (its name, until its first line says better) */
        if (g->len > 2 && p[0] == '#' && p[1] == ' ') {
            const char *e = memchr(p, '\n', (size_t)g->len);
            n = (size_t)((e ? e : p + g->len) - (p + 2));
            if (n >= sizeof g->title)
                n = sizeof g->title - 1;
            memcpy(g->title, p + 2, n);
            g->title[n] = 0;
        }
        npages++;
        p = next;
    }
    return npages ? 0 : -1;
}

static int page_named(const char *name, size_t n)       /* n bytes, "FILES.MD" or "FILES": its place, or -1 */
{
    int i;
    for (i = 0; i < npages; i++) {
        size_t len = strlen(pages[i].name);
        if (n && (n == len || (n + 3 == len && !strcmp(pages[i].name + n, ".MD"))) && !strncasecmp(name, pages[i].name, n))
            return i;
    }
    return -1;
}

/* ---- the contents: the first page's list of links, as a tree */

static void contents(void)
{
    struct gui_node *under[8] = { NULL };
    const char *p = pages[0].text, *end = p + pages[0].len;
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p)), *q = p, *t, *t_end, *to, *to_end;
        const char *line_end = nl ? nl : end;
        int depth, k;
        while (q < line_end && *q == ' ')
            q++;
        depth = (int)(q - p) / 2;
        if (depth < 8 && line_end - q > 6 && (*q == '-' || *q == '*') && q[1] == ' ' && q[2] == '['
            && (t_end = memchr(q + 3, ']', (size_t)(line_end - q - 3))) != NULL && t_end + 1 < line_end && t_end[1] == '('
            && (to_end = memchr(t_end + 2, ')', (size_t)(line_end - t_end - 2))) != NULL) {
            char title[80];
            size_t n;
            t = q + 3, to = t_end + 2;
            for (n = 0; to + n < to_end && to[n] != '#'; n++)
                ;
            if ((k = page_named(to, n)) >= 0) {
                snprintf(title, sizeof title, "%.*s", (int)(t_end - t), t);
                under[depth] = gui_tree_add(tree, depth ? under[depth - 1] : NULL, title, gui_icon("file-text", 0),
                                            (void *)(size_t)(k + 1), 0);
                if (!pages[k].node)
                    pages[k].node = under[depth];
                if (depth && under[depth - 1])
                    gui_tree_expand(tree, under[depth - 1], 1);
            }
        }
        p = nl ? nl + 1 : end;
    }
}

/* ---- a page shown */

static void show(int k, int walked)     /* walked: by Back or Forward (the trail stays as it is) */
{
    char title[120];
    if (k < 0 || k >= npages)
        return;
    gui_richview_set(view, gui_markdown(pages[k].text, pages[k].len, NULL, NULL));
    shown = k;
    if (!walked) {
        if (ntrail && at < ntrail - 1)
            ntrail = at + 1;            /* (what was ahead is no longer) */
        if (ntrail == TRAIL) {
            memmove(trail, trail + 1, (TRAIL - 1) * sizeof *trail);
            ntrail--;
        }
        trail[ntrail++] = k;
        at = ntrail - 1;
    }
    snprintf(title, sizeof title, GUI_T("%s - Help"), pages[k].title);
    gui_title(win, title);
    quiet = 1;
    if (pages[k].node)
        gui_tree_set(tree, pages[k].node);
    quiet = 0;
    gui_set_text(status, pages[k].title);
    say("page %s", pages[k].name);
}

static void view_on(struct gui_widget *w, int what)
{
    const char *link = what == GUI_ON_ACTIVATE ? gui_richview_link(w) : NULL;
    size_t n;
    int k;
    if (!link || !link[0])
        return;
    if (strstr(link, "://")) {          /* (out of the guide: named, not followed yet) */
        say("link out %s", link);
        gui_set_text(status, link);
        return;
    }
    for (n = 0; link[n] && link[n] != '#'; n++)
        ;
    if ((k = page_named(link, n)) >= 0)
        show(k, 0);
}

static void tree_on(struct gui_widget *w, int what)
{
    struct gui_node *n = gui_tree_get(w);
    int k = n ? (int)(size_t)gui_node_user(n) - 1 : -1;
    if (quiet || (what != GUI_ON_SELECT && what != GUI_ON_ACTIVATE) || k < 0 || k == shown)
        return;
    show(k, 0);
}

/* ---- search: the lines of every page that have the words typed */

static int has(const char *line, size_t n, const char *words)   /* every word of `words` in the line, in either case */
{
    while (*words) {
        size_t w = strcspn(words, " "), i;
        int found = 0;
        if (w) {
            for (i = 0; i + w <= n && !found; i++)
                found = !strncasecmp(line + i, words, w);
            if (!found)
                return 0;
        }
        words += w;
        while (*words == ' ')
            words++;
    }
    return 1;
}

static void plain(const char *line, size_t n, char *out, size_t room)   /* a line of Markdown as the words it shows */
{
    size_t i, k = 0;
    for (i = 0; i < n && (line[i] == '#' || line[i] == ' ' || line[i] == '-' || line[i] == '>' || line[i] == '|'); i++)
        ;
    for (; i < n && k + 1 < room; i++) {
        if (line[i] == '*' || line[i] == '`' || line[i] == '[')
            continue;
        if (line[i] == ']' && i + 1 < n && line[i + 1] == '(') {       /* a link: its words, not where it goes */
            while (i < n && line[i] != ')')
                i++;
            continue;
        }
        out[k++] = line[i] == '|' ? ' ' : line[i];
    }
    out[k] = 0;
}

static void search(const char *words)
{
    int k, lines = 0, in = 0;
    gui_list_clear(results);
    while (*words == ' ')
        words++;
    if (!*words)
        return;
    for (k = 0; k < npages && lines < 200; k++) {
        const char *p = pages[k].text, *end = p + pages[k].len;
        int here = 0;
        while (p < end && here < 4) {   /* (a page's first four lines that have them: the list is of pages) */
            const char *nl = memchr(p, '\n', (size_t)(end - p));
            size_t n = (size_t)((nl ? nl : end) - p);
            if (n && has(p, n, words)) {
                char text[160];
                const char *cells[2];
                plain(p, n, text, sizeof text);
                cells[0] = pages[k].title, cells[1] = text;
                gui_list_add(results, cells, gui_icon("file-text", 0), (void *)(size_t)(k + 1));
                here++, lines++;
            }
            p = nl ? nl + 1 : end;
        }
        in += here != 0;
    }
    if (lines)
        gui_list_set(results, 0);
    say("found %d lines in %d pages for \"%s\"", lines, in, words);
}

static void field_on(struct gui_widget *w, int what)
{
    if (what == GUI_ON_ACTIVATE) {
        char words[80];
        snprintf(words, sizeof words, "%s", gui_get_text(w));
        search(words);
        if (gui_list_count(results))
            gui_focus(results);
    }
}

static void results_on(struct gui_widget *w, int what)
{
    int row = gui_list_get(w);
    if (what == GUI_ON_ACTIVATE && row >= 0)
        show((int)(size_t)gui_list_user(w, row) - 1, 0);
}

/* ---- commands */

static void command(struct gui_window *w, int id)
{
    (void)w;
    switch (id) {
    case C_BACK:
        if (at > 0)
            show(trail[--at], 1);
        break;
    case C_FORWARD:
        if (at < ntrail - 1)
            show(trail[++at], 1);
        break;
    case C_HOME: show(0, 0); break;
    case C_FIND:
        gui_tabs_set(tabs, 1);
        gui_focus(field);
        break;
    case C_CONTENTS:
        gui_tabs_set(tabs, 0);
        gui_focus(tree);
        break;
    case C_ABOUT: {
        int k = page_named("HELP", 4);
        if (k >= 0)
            show(k, 0);
        break;
    }
    case C_EXIT: gui_close(win); break;
    }
}

static void update(struct gui_window *w)
{
    gui_command_enable(w, C_BACK, at > 0);
    gui_command_enable(w, C_FORWARD, at < ntrail - 1);
}

static void build(void)
{
    static const char *const heads[] = { GUI_N("Page"), GUI_N("Line") };
    static const int widths[] = { 90, 260 };
    struct gui_menubar *bar;
    struct gui_menu *m;
    struct gui_widget *tb, *split, *page;
    win = gui_window(GUI_T("Help"), 620, 440, 0);
    win->on_command = command;
    win->on_update = update;
    gui_window_icon(win, "app-help");
    gui_command(win, C_BACK, GUI_T("&Back"), "Alt+Left", gui_glyph("back"), GUI_T("The page before"));
    gui_command(win, C_FORWARD, GUI_T("&Forward"), "Alt+Right", gui_glyph("forward"), GUI_T("The page after"));
    gui_command(win, C_HOME, GUI_T("&First Page"), "Alt+Home", gui_glyph("home"), GUI_T("The guide's first page"));
    gui_command(win, C_FIND, GUI_T("&Search..."), "Ctrl+F", gui_glyph("find"), GUI_T("Look for words in every page"));
    gui_command(win, C_CONTENTS, GUI_T("&Contents"), "Ctrl+T", gui_glyph("list"), GUI_T("The guide's contents"));
    gui_command(win, C_ABOUT, GUI_T("&Using Help"), "F1", gui_glyph("help"), NULL);
    gui_command(win, C_EXIT, GUI_T("E&xit"), "Alt+F4", NULL, NULL);
    bar = gui_menubar(win);
    m = gui_menu(bar, GUI_T("&Help"));
    gui_menu_command(m, C_CONTENTS), gui_menu_command(m, C_FIND);
    gui_menu_sep(m);
    gui_menu_command(m, C_ABOUT);
    gui_menu_sep(m);
    gui_menu_command(m, C_EXIT);
    m = gui_menu(bar, GUI_T("&Go"));
    gui_menu_command(m, C_BACK), gui_menu_command(m, C_FORWARD), gui_menu_command(m, C_HOME);
    tb = gui_toolbar(win);
    gui_toolbar_command(tb, C_BACK), gui_toolbar_command(tb, C_FORWARD), gui_toolbar_command(tb, C_HOME);
    gui_toolbar_sep(tb);
    gui_toolbar_command(tb, C_CONTENTS), gui_toolbar_command(tb, C_FIND);
    split = gui_split(win->root, 0, 212);       /* the contents and the search | the page */
    gui_grow(split, 1);
    tabs = gui_tabs(split);
    page = gui_tab(tabs, GUI_T("Contents"));
    tree = gui_tree(page);
    gui_grow(tree, 1);
    gui_on(tree, tree_on, 0);
    page = gui_tab(tabs, GUI_T("Search"));
    field = gui_field(page, 20);
    gui_on(field, field_on, 0);
    results = gui_list(page, 2, heads, widths);
    gui_grow(results, 1);
    gui_on(results, results_on, 0);
    view = gui_richview(split);
    gui_on(view, view_on, 0);
    status = gui_status(win);
}

int main(int argc, char **argv)
{
    int k = -1;
    if (gui_init(argc, argv) != 0) {
        printf("HELP no GLOS\n");
        return 1;
    }
    if (load() != 0) {
        gui_message(NULL, GUI_T("Help"), GUI_T("The guide (HELP.DAT, beside GLOS's programs) is not there, or is not a guide."),
                    GUI_MB_OK);
        printf("HELP no guide\n");
        return 1;
    }
    build();
    contents();
    gui_show(win, 1);
    if (argc > 1 && argv[1][0] != '-')          /* HELP NAME: that program's page, if the guide has one */
        k = page_named(argv[1], strcspn(argv[1], "."));
    say("guide asked=%s", argc > 1 ? argv[1] : "-");
    show(k >= 0 ? k : 0, 0);
    gui_focus(view);
    gui_run();
    free(dat);
    printf("HELP end\n");
    return 0;
}
