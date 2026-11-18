/* The file manager's folders (files.h): a folder's window of its own
 * (spatial, PRD D33: one window a folder, opened again where it was left),
 * and the browser (a tree of the drives' folders beside a list with
 * columns). In both: open, a new folder, rename, delete, cut, copy and
 * paste (folders with all in them), large icons or details, up a folder.
 * Each window watches its folder (FS's WATCH): a change made anywhere,
 * by any program, the agent or SFTP, shows without a refresh.
 *
 * Where folders' windows were is kept in FOLDERS.CFG beside FILES.EXE. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"

#define HIST 16

struct view {
    struct view *next;
    struct gui_window *win;
    struct gui_widget *list, *tree, *status;
    struct gui_widget *address;         /* the browser's address bar (phase E): the folder's path, to type into */
    struct gui_menu *view_menu;
    char dir[260];
    char hist[HIST][260];               /* the browser's way here: Back and Forward go along it */
    int nhist, at, moving;              /* at: where in it the folder shown is; moving: go() by Back or Forward */
    int browser, big;
    glos_handle watch;
    struct gui_dirent *ents;
    int n;
};

static struct view *views;
static struct {                         /* the files cut or copied */
    char paths[64][260];
    int n, cut;
} clip;

static struct {                         /* where folders' windows were */
    char path[64][100];
    int x[64], y[64], w[64], h[64], n;
} places;

enum { C_OPEN = 1, C_NEWDIR, C_RENAME, C_DELETE, C_CLOSE, C_CUT, C_COPY, C_PASTE, C_ALL, C_BIG, C_DETAILS, C_REFRESH,
       C_UP, C_BROWSER, C_BACK, C_FORWARD, C_ADDRESS };

static void say(const char *what, const char *a)
{
    printf("FILES %s %s\n", what, a);
    fflush(stdout);
}

static struct view *view_of(struct gui_window *w)
{
    struct view *v;
    for (v = views; v; v = v->next)
        if (v->win == w)
            return v;
    return NULL;
}

/* ---- what a folder shows */

static void status_text(struct view *v)
{
    char t[160];
    int i, files = 0, dirs = 0, chosen = 0;
    unsigned long bytes = 0;
    for (i = 0; i < v->n; i++) {
        if (v->ents[i].dir)
            dirs++;
        else
            files++, bytes += v->ents[i].size;
        chosen += gui_list_chosen(v->list, i);
    }
    if (chosen)
        snprintf(t, sizeof t, GUI_T("%d of %d chosen"), chosen, v->n);
    else
        snprintf(t, sizeof t, GUI_T("%d folder%s, %d file%s, %lu bytes"), dirs, dirs == 1 ? "" : "s", files,
                 files == 1 ? "" : "s", bytes);
    gui_set_text(v->status, t);
}

static void fill(struct view *v)
{
    int i, keep = gui_list_get(v->list);
    char keep_name[64] = "";
    if (keep >= 0 && keep < v->n)
        snprintf(keep_name, sizeof keep_name, "%s", v->ents[keep].name);
    free(v->ents);
    v->ents = NULL;
    v->n = gui_dir_read(v->dir, &v->ents);
    if (v->n < 0)
        v->n = 0;
    gui_list_clear(v->list);
    for (i = 0; i < v->n; i++) {
        char size[24], date[24];
        const char *cells[3];
        const struct gui_dirent *e = &v->ents[i];
        if (e->dir)
            snprintf(size, sizeof size, GUI_T("Folder"));
        else
            snprintf(size, sizeof size, "%lu", e->size);
        snprintf(date, sizeof date, "%04u-%02u-%02u %02u:%02u", (e->date >> 9) + 1980, (e->date >> 5) & 15, e->date & 31,
                 e->time >> 11, (e->time >> 5) & 63);
        cells[0] = e->text, cells[1] = size, cells[2] = date;
        gui_list_add(v->list, cells, icon(icon_for_file(e->name, e->dir), v->big), (void *)(size_t)i);
        if (keep_name[0] && !strcmp(keep_name, e->name))
            gui_list_set(v->list, i);
    }
    status_text(v);
}

static void retitle(struct view *v)
{
    char t[400], dir[300];
    snprintf(t, sizeof t, "%s%s", gui_dos_text(v->dir, dir, sizeof dir), v->browser ? " - Files" : "");
    gui_title(v->win, t);
}

/* The browser's way: a folder gone to joins it after the one shown (what lay ahead is forgotten); Back and
   Forward move along it. The address bar says the folder, and keeps the ones typed or gone to. */
static void way(struct view *v)
{
    if (!v->browser)
        return;
    if (!v->moving && (!v->nhist || strcmp(v->hist[v->at], v->dir))) {
        if (v->nhist && v->at == HIST - 1) {
            memmove(v->hist[0], v->hist[1], sizeof v->hist[0] * (HIST - 1));
            v->at--;
        }
        if (v->nhist)
            v->at++;
        snprintf(v->hist[v->at], sizeof v->hist[0], "%s", v->dir);
        v->nhist = v->at + 1;
    }
    gui_command_enable(v->win, C_BACK, v->at > 0);
    gui_command_enable(v->win, C_FORWARD, v->at + 1 < v->nhist);
    gui_command_enable(v->win, C_UP, strlen(v->dir) > 3);
    if (v->address) {
        gui_set_text(v->address, v->dir);
        gui_combo_add_top(v->address, v->dir);
    }
}

static void go(struct view *v, const char *dir)
{
    snprintf(v->dir, sizeof v->dir, "%s", dir);
    way(v);
    if (v->watch)
        glos_close(v->watch);
    v->watch = glos_fs_watch(v->dir);
    retitle(v);
    fill(v);
    if (gui_list_count(v->list)) {
        gui_list_set(v->list, 0);
        status_text(v);                         /* (the first chosen: as a refresh would say it) */
    }
}

void folder_changed(unsigned watch)
{
    struct view *v;
    for (v = views; v; v = v->next)
        if (v->watch == watch) {
            fill(v);
            say("refreshed", v->dir);
        }
}

/* ---- the files chosen */

static int chosen_paths(struct view *v, char (*out)[260], int room)
{
    int i, n = 0;
    for (i = 0; i < v->n && n < room; i++)
        if (gui_list_chosen(v->list, i))
            gui_path_join(out[n++], 260, v->dir, v->ents[(size_t)gui_list_user(v->list, i)].name);
    return n;
}

/* ---- copying, moving, removing: folders with what is in them */

static int copy_file(const char *from, const char *to)
{
    FILE *a = gui_fopen(from, "rb"), *b;
    char buf[8192];
    size_t n;
    int bad = 0;
    if (!a)
        return -1;
    if (!(b = gui_fopen(to, "wb"))) {
        fclose(a);
        return -1;
    }
    while ((n = fread(buf, 1, sizeof buf, a)) > 0)
        if (fwrite(buf, 1, n, b) != n)
            bad = 1;
    fclose(a);
    if (fclose(b) != 0)
        bad = 1;
    return bad ? -1 : 0;
}

static int copy_tree(const char *from, const char *to)
{
    struct gui_dirent *e;
    int n, i, bad = 0;
    char a[260], b[260];
    if (!gui_is_dir(from))
        return copy_file(from, to);
    if (gui_mkdir(to) != 0 && !gui_is_dir(to))
        return -1;
    if ((n = gui_dir_read(from, &e)) < 0)
        return -1;
    for (i = 0; i < n; i++) {
        gui_path_join(a, sizeof a, from, e[i].name);
        gui_path_join(b, sizeof b, to, e[i].name);
        bad |= copy_tree(a, b) != 0;
    }
    free(e);
    return bad ? -1 : 0;
}

static int remove_tree(const char *path)
{
    struct gui_dirent *e;
    int n, i, bad = 0;
    char a[260];
    if (!gui_is_dir(path))
        return gui_unlink(path);
    if ((n = gui_dir_read(path, &e)) < 0)
        return -1;
    for (i = 0; i < n; i++) {
        gui_path_join(a, sizeof a, path, e[i].name);
        bad |= remove_tree(a) != 0;
    }
    free(e);
    return bad ? -1 : gui_rmdir(path);
}

static void paste(struct view *v)
{
    int i, done = 0;
    char to[260], t[80];
    for (i = 0; i < clip.n; i++) {
        gui_path_join(to, sizeof to, v->dir, gui_path_name(clip.paths[i]));
        if (!strcmp(to, clip.paths[i])) {
            const char *dot = strrchr(gui_path_name(clip.paths[i]), '.');
            char base[16], name[20];
            snprintf(base, sizeof base, "%.*s", dot ? (int)(dot - gui_path_name(clip.paths[i])) : 6,
                     gui_path_name(clip.paths[i]));
            snprintf(name, sizeof name, "%.6s~1%s", base, dot ? dot : "");     /* (a copy beside the file) */
            gui_path_join(to, sizeof to, v->dir, name);
        }
        if (clip.cut ? (gui_rename(clip.paths[i], to) == 0
                        || (copy_tree(clip.paths[i], to) == 0 && remove_tree(clip.paths[i]) == 0))
                     : copy_tree(clip.paths[i], to) == 0)
            done++;
    }
    if (clip.cut)
        clip.n = 0;
    snprintf(t, sizeof t, "%d %s", done, clip.cut ? "moved" : "copied");
    say("pasted", t);
    fill(v);
}

/* ---- the commands */

static void closing(struct gui_window *w);

/* A name typed (text) as DOS's bytes, in place: false, and said, if DOS's code page hasn't got all of it. */
static int dos_name(struct gui_window *w, char *name)
{
    char b[128];
    if (gui_text_dos(name, b, sizeof b) != 0) {
        gui_message(w, GUI_T("Files"), GUI_T("DOS's code page hasn't got all of the name's letters."), GUI_MB_OK);
        return 0;
    }
    strcpy(name, b);
    return 1;
}

static void command(struct gui_window *w, int id)
{
    struct view *v = view_of(w);
    char name[128], from[260], to[260], t[300];
    static char paths[64][260];
    int r, n, i;
    if (!v)
        return;
    r = gui_list_get(v->list);
    switch (id) {
    case C_OPEN:
        if (r >= 0)
            gui_send(v->list, GUI_ON_ACTIVATE);
        break;
    case C_NEWDIR:
        snprintf(name, sizeof name, "NEWDIR");
        if (gui_ask(w, GUI_T("New Folder"), GUI_T("The new folder's name:"), name, sizeof name) && name[0]) {
            if (!dos_name(w, name))
                break;
            gui_path_join(to, sizeof to, v->dir, name);
            if (gui_mkdir(to) != 0)
                gui_message(w, GUI_T("Files"), GUI_T("The folder couldn't be made."), GUI_MB_OK);
            else
                say("made", to);
            fill(v);
        }
        break;
    case C_RENAME:
        if (r < 0)
            break;
        gui_path_join(from, sizeof from, v->dir, v->ents[(size_t)gui_list_user(v->list, r)].name);
        snprintf(name, sizeof name, "%s", v->ents[(size_t)gui_list_user(v->list, r)].text);
        if (gui_ask(w, GUI_T("Rename"), GUI_T("Its new name:"), name, sizeof name) && name[0]) {
            if (!dos_name(w, name))
                break;
            gui_path_join(to, sizeof to, v->dir, name);
            if (gui_rename(from, to) != 0)
                gui_message(w, GUI_T("Files"), GUI_T("It couldn't be renamed."), GUI_MB_OK);
            else
                say("renamed", to);
            fill(v);
        }
        break;
    case C_DELETE:
        if (!(n = chosen_paths(v, paths, 64)))
            break;
        if (n == 1)
            snprintf(t, sizeof t, GUI_T("Delete %s?"), gui_dos_text(gui_path_name(paths[0]), name, sizeof name));
        else
            snprintf(t, sizeof t, GUI_T("Delete these %d?"), n);
        if (gui_message(w, GUI_T("Delete"), t, GUI_MB_YESNO) == GUI_YES) {
            for (i = 0; i < n; i++)
                if (remove_tree(paths[i]) == 0)
                    say("deleted", paths[i]);
            fill(v);
        }
        break;
    case C_CLOSE:
        closing(w);
        return;                                 /* (the window has gone: nothing of it touched again) */
    case C_CUT:
    case C_COPY:
        clip.n = chosen_paths(v, clip.paths, 64);
        clip.cut = id == C_CUT;
        snprintf(t, sizeof t, "%d %s", clip.n, clip.cut ? "cut" : "copied");
        gui_set_text(v->status, t);
        break;
    case C_PASTE:
        paste(v);
        break;
    case C_ALL:
        for (i = 0; i < v->n; i++)
            if (!gui_list_chosen(v->list, i)) {
                struct gui_ev e;
                memset(&e, 0, sizeof e);
                e.type = GUI_EV_KEY;
                e.mods = GLOS_MOD_CTRL;
                e.ch = 1;
                v->list->cls->event(v->list, &e);
                break;
            }
        status_text(v);
        break;
    case C_BIG:
    case C_DETAILS:
        v->big = id == C_BIG;
        gui_list_icons(v->list, v->big);
        gui_menu_check(v->view_menu, C_BIG, v->big);
        gui_menu_check(v->view_menu, C_DETAILS, !v->big);
        fill(v);
        break;
    case C_REFRESH:
        fill(v);
        break;
    case C_UP:
        if (strlen(v->dir) > 3) {
            snprintf(to, sizeof to, "%s", v->dir);
            gui_path_up(to);
            if (v->browser)
                go(v, to);
            else
                folder_open(to);
        }
        break;
    case C_BROWSER:
        browser_open(v->dir);
        break;
    case C_BACK:
    case C_FORWARD:
        if (id == C_BACK ? v->at > 0 : v->at + 1 < v->nhist) {
            v->at += id == C_BACK ? -1 : 1;
            v->moving = 1;
            go(v, v->hist[v->at]);
            v->moving = 0;
            say(id == C_BACK ? "back" : "forward", v->dir);
        }
        break;
    case C_ADDRESS:                             /* the keyboard's way to the address bar */
        if (v->address) {                       /* (its text chosen, as its own Ctrl+A does: what is typed takes
                                                   its place; gui_editor_command is the editor's alone) */
            struct gui_ev e;
            memset(&e, 0, sizeof e);
            e.type = GUI_EV_KEY;
            e.key = 0x1E;
            e.ch = 1;
            e.mods = GLOS_MOD_CTRL;
            gui_focus(v->address);
            v->address->cls->event(v->address, &e);
        }
        return;
    }
    if (v->list)
        gui_focus(v->list);
}

static void list_on(struct gui_widget *w, int what)
{
    struct view *v = view_of(w->win);
    int r;
    char p[260];
    if (!v)
        return;
    if (what == GUI_ON_SELECT) {
        status_text(v);
        return;
    }
    if (what != GUI_ON_ACTIVATE || (r = gui_list_get(w)) < 0)
        return;
    gui_path_join(p, sizeof p, v->dir, v->ents[(size_t)gui_list_user(w, r)].name);
    if (v->ents[(size_t)gui_list_user(w, r)].dir && v->browser)
        go(v, p);
    else
        files_open(p);
}

static int by_column(struct gui_widget *w, int a, int b, int col)
{
    struct view *v = view_of(w->win);
    const struct gui_dirent *x = &v->ents[(size_t)gui_list_user(w, a)], *y = &v->ents[(size_t)gui_list_user(w, b)];
    if (x->dir != y->dir)
        return x->dir ? -1 : 1;
    if (col == 1)
        return x->size < y->size ? -1 : x->size > y->size;
    if (col == 2)
        return x->date != y->date ? (x->date < y->date ? -1 : 1) : (x->time < y->time ? -1 : x->time > y->time);
    return strcmp(x->name, y->name);
}

static void closing(struct gui_window *w)
{
    struct view *v = view_of(w), **p;
    int x, y;
    if (v && !v->browser && gui_origin(w, &x, &y) == 0) {      /* (where it was: for the next time) */
        int i;
        for (i = 0; i < places.n && strcmp(places.path[i], v->dir); i++)
            ;
        if (i == places.n && places.n < 64)
            places.n++;
        if (i < 64) {
            snprintf(places.path[i], sizeof places.path[i], "%s", v->dir);
            places.x[i] = x - 1, places.y[i] = y - 20, places.w[i] = w->w, places.h[i] = w->h;
        }
        folders_save();
    }
    for (p = &views; *p; p = &(*p)->next)
        if (*p == v) {
            *p = v->next;
            break;
        }
    if (v) {
        if (v->watch)
            glos_close(v->watch);
        free(v->ents);
        free(v);
    }
    gui_close_later(w);                         /* (its own event may be what closes it) */
}

static struct gui_menu *menus(struct gui_window *w, int browser)
{
    struct gui_menubar *bar = gui_menubar(w);
    struct gui_menu *m = gui_menu(bar, GUI_T("&File"));
    gui_menu_item(m, GUI_T("&Open"), C_OPEN, NULL);
    gui_menu_item(m, GUI_T("&New Folder"), C_NEWDIR, GUI_T("Ctrl+Shift+N"));
    gui_menu_item(m, GUI_T("&Rename"), C_RENAME, GUI_T("F2"));
    gui_menu_item(m, GUI_T("&Delete"), C_DELETE, GUI_T("Del"));
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("&Close"), C_CLOSE, GUI_T("Ctrl+W"));
    m = gui_menu(bar, GUI_T("&Edit"));
    gui_menu_item(m, GUI_T("Cu&t"), C_CUT, GUI_T("Ctrl+X"));
    gui_menu_item(m, GUI_T("&Copy"), C_COPY, GUI_T("Ctrl+C"));
    gui_menu_item(m, GUI_T("&Paste"), C_PASTE, GUI_T("Ctrl+V"));
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("Select &All"), C_ALL, GUI_T("Ctrl+A"));
    m = gui_menu(bar, GUI_T("&View"));
    gui_menu_item(m, GUI_T("&Large icons"), C_BIG, NULL);
    gui_menu_item(m, GUI_T("&Details"), C_DETAILS, NULL);
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("&Refresh"), C_REFRESH, GUI_T("F5"));
    {
        struct gui_menu *view = m;
        m = gui_menu(bar, GUI_T("&Go"));
        if (browser) {
            gui_menu_item(m, GUI_T("&Back"), C_BACK, GUI_T("Alt+Left"));
            gui_menu_item(m, GUI_T("&Forward"), C_FORWARD, GUI_T("Alt+Right"));
            gui_menu_item(m, GUI_T("&Address bar"), C_ADDRESS, GUI_T("Ctrl+L"));
        }
        gui_menu_item(m, GUI_T("&Up a folder"), C_UP, GUI_T("Backspace"));
        if (!browser)
            gui_menu_item(m, GUI_T("&Browser"), C_BROWSER, NULL);
        return view;
    }
}

/* The address bar: Enter goes to the folder typed (or chosen from the ones kept); what is no folder is said. */
static void address_on(struct gui_widget *w, int what)
{
    struct view *v = view_of(w->win);
    char p[260];
    size_t n;
    if (what != GUI_ON_ACTIVATE || !v)
        return;
    snprintf(p, sizeof p, "%s", gui_get_text(w));
    for (n = strlen(p); n > 3 && (p[n - 1] == '\\' || p[n - 1] == ' '); n--)
        p[n - 1] = 0;
    for (n = 0; p[n]; n++)
        if (p[n] >= 'a' && p[n] <= 'z')
            p[n] = (char)(p[n] - 32);
    if (!gui_is_dir(p)) {
        gui_message(v->win, GUI_T("Files"), GUI_T("There is no such folder."), GUI_MB_OK);
        gui_set_text(w, v->dir);
        return;
    }
    go(v, p);
    say("address", v->dir);
    gui_focus(v->list);
}

static struct view *new_view(const char *dir, int browser)
{
    struct view *v = calloc(1, sizeof *v);
    static const char *const heads[3] = { GUI_N("Name"), GUI_N("Size"), GUI_N("Modified") };
    static const int widths[3] = { 130, 70, 120 };
    int i, x = -1, y = -1, w = 420, h = 280;
    if (!v)
        abort();
    v->browser = browser;
    v->big = !browser;
    for (i = 0; !browser && i < places.n; i++)
        if (!strcmp(places.path[i], dir))
            x = places.x[i], y = places.y[i], w = places.w[i], h = places.h[i];
    v->win = gui_window_at(dir, x, y, browser ? 540 : w, browser ? 330 : h, 0);
    if (v->win)
        gui_window_icon(v->win, browser ? "app-files" : "folder");
    v->win->on_command = command;
    v->win->on_close = closing;
    v->view_menu = menus(v->win, browser);
    if (browser) {                              /* its toolbar and its address bar (phase E) */
        static const struct { int id; const char *text, *glyph, *tip; } tools[] = {
            { C_BACK, GUI_N("&Back"), "back", GUI_N("Back (Alt+Left)") },
            { C_FORWARD, GUI_N("&Forward"), "forward", GUI_N("Forward (Alt+Right)") },
            { C_UP, GUI_N("&Up a folder"), "up", GUI_N("Up a folder (Backspace)") },
            { C_REFRESH, GUI_N("&Refresh"), "refresh", GUI_N("Refresh (F5)") },
            { 0, NULL, NULL, NULL },
            { C_NEWDIR, GUI_N("&New Folder"), "folder-new", GUI_N("New folder (Ctrl+Shift+N)") },
            { 0, NULL, NULL, NULL },
            { C_CUT, GUI_N("Cu&t"), "cut", GUI_N("Cut (Ctrl+X)") },
            { C_COPY, GUI_N("&Copy"), "copy", GUI_N("Copy (Ctrl+C)") },
            { C_PASTE, GUI_N("&Paste"), "paste", GUI_N("Paste (Ctrl+V)") },
            { C_DELETE, GUI_N("&Delete"), "delete", GUI_N("Delete (Del)") },
            { 0, NULL, NULL, NULL },
            { C_BIG, GUI_N("&Large icons"), "grid", GUI_N("Large icons") },
            { C_DETAILS, GUI_N("&Details"), "list", GUI_N("Details") },
        };
        struct gui_widget *bar = gui_toolbar(v->win), *row;
        size_t k;
        for (k = 0; k < sizeof tools / sizeof tools[0]; k++)
            if (!tools[k].id) {
                gui_toolbar_sep(bar);
            } else {
                gui_command(v->win, tools[k].id, GUI_T(tools[k].text), NULL, gui_glyph(tools[k].glyph), GUI_T(tools[k].tip));
                gui_toolbar_command(bar, tools[k].id);
            }
        row = gui_hbox(v->win->root, 6, 3);
        gui_label(row, GUI_T("Address:"));
        v->address = gui_combo(row, 40);
        gui_grow(v->address, 1);
        gui_on(v->address, address_on, 0);
    }
    {
        struct gui_widget *row = gui_hbox(v->win->root, 0, 0);
        gui_grow(row, 1);
        if (browser) {
            v->tree = gui_tree(row);
            gui_size(v->tree, 170, 0);
            gui_grow(v->tree, 0);
        }
        v->list = gui_list(row, browser ? 3 : 1, browser ? heads : NULL, browser ? widths : NULL);
        gui_list_multi(v->list, 1);
        gui_list_icons(v->list, v->big);
        gui_on(v->list, list_on, 0);
        if (browser)
            gui_list_sort(v->list, 0, by_column);   /* (a column's head sorts by it) */
    }
    v->status = gui_status(v->win);
    gui_menu_check(v->view_menu, C_BIG, v->big);
    gui_menu_check(v->view_menu, C_DETAILS, !v->big);
    v->next = views;
    views = v;
    return v;
}

void folder_open(const char *dir)
{
    struct view *v;
    char d[260];
    snprintf(d, sizeof d, "%s", dir);
    for (v = views; v; v = v->next)             /* (spatial: one window a folder) */
        if (!v->browser && !strcmp(v->dir, d)) {
            gui_show(v->win, 1);
            glos_focus(v->win->id);
            return;
        }
    v = new_view(d, 0);
    go(v, d);
    v->win->focus = v->list;
    gui_show(v->win, 1);
    say("folder", d);
}

/* ---- the browser's tree */

static void tree_on(struct gui_widget *w, int what)
{
    struct view *v = view_of(w->win);
    struct gui_node *n = what == GUI_ON_CHANGE ? gui_tree_opening(w) : gui_tree_get(w);
    char *path;
    if (!v || !n)
        return;
    path = gui_node_user(n);
    if (what == GUI_ON_CHANGE) {                /* a folder opening: its folders, now */
        struct gui_dirent *e;
        int k, i;
        if ((k = gui_dir_read(path, &e)) < 0)
            return;
        for (i = 0; i < k; i++)
            if (e[i].dir) {
                char *p = malloc(260);
                gui_path_join(p, 260, path, e[i].name);
                gui_tree_add(w, n, e[i].text, icon(IC_FOLDER, 0), p, 1);
            }
        free(e);
    } else if (what == GUI_ON_SELECT && strcmp(v->dir, path)) {
        go(v, path);
    }
}

static struct gui_node *find_child(struct gui_widget *t, struct gui_node *n, const char *name)
{
    return gui_tree_child(t, n, name);
}

void browser_open(const char *dir)
{
    struct view *v = new_view(dir, 1);
    char letters[27], root[4];
    int i;
    gui_on(v->tree, tree_on, 0);
    gui_drives(letters);
    for (i = 0; letters[i]; i++) {
        char *p = malloc(4);
        struct gui_node *n;
        snprintf(root, sizeof root, "%c:", letters[i]);
        snprintf(p, 4, "%c:\\", letters[i]);
        n = gui_tree_add(v->tree, NULL, root, icon(IC_DRIVE, 0), p, 1);
        if (letters[i] == ((dir[0] >= 'a' ? dir[0] - 32 : dir[0]))) {
            const char *rest = dir + 3, *e;     /* down the folders to dir, each opened */
            gui_tree_set(v->tree, n);
            while (*rest) {
                char part[64];
                size_t k;
                struct gui_node *c = NULL;
                e = strchr(rest, '\\');
                k = e ? (size_t)(e - rest) : strlen(rest);
                snprintf(part, sizeof part, "%.*s", (int)k, rest);
                gui_tree_expand(v->tree, n, 1);
                c = find_child(v->tree, n, part);
                if (!c)
                    break;
                n = c;
                gui_tree_set(v->tree, n);
                rest += k + (e ? 1 : 0);
            }
        }
    }
    go(v, dir);
    v->win->focus = v->list;
    gui_show(v->win, 1);
    say("browser", dir);
}

/* ---- where folders' windows were */

void folders_load(void)
{
    char p[300], line[400];
    FILE *f;
    snprintf(p, sizeof p, "%s\\FOLDERS.CFG", files_home);
    if (!(f = gui_fopen(p, "r")))
        return;
    while (fgets(line, sizeof line, f) && places.n < 64) {
        int n = 0, k = places.n;
        if (sscanf(line, "%d %d %d %d %n", &places.x[k], &places.y[k], &places.w[k], &places.h[k], &n) != 4)
            continue;
        snprintf(places.path[k], sizeof places.path[k], "%s", line + n);
        places.path[k][strcspn(places.path[k], "\r\n")] = 0;
        places.n++;
    }
    fclose(f);
}

void folders_save(void)
{
    char p[300];
    FILE *f;
    int i;
    snprintf(p, sizeof p, "%s\\FOLDERS.CFG", files_home);
    if (!places.n || !(f = gui_fopen(p, "w")))
        return;
    for (i = 0; i < places.n; i++)
        fprintf(f, "%d %d %d %d %s\n", places.x[i], places.y[i], places.w[i], places.h[i], places.path[i]);
    fclose(f);
}
