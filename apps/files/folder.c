/* The file manager's folders (files.h): a folder's window of its own
 * (spatial, PRD D33: one window a folder, opened again where it was left),
 * and the browser (a tree of the drives' folders beside a list with
 * columns). In both: open, a new folder, rename, delete, cut, copy and
 * paste (folders with all in them), large icons or details, up a folder.
 * Each window watches its folder (FS's WATCH): a change made anywhere,
 * by any program, the agent or SFTP, shows without a refresh.
 *
 * Where folders' windows were is kept in FOLDERS.CFG beside FILES.EXE. */
#include <ctype.h>
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
    struct gui_menu *context;           /* the list's context menu (phase E) */
    int following;                      /* the tree is being taken to the folder shown (tree_follow) */
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
       C_UP, C_BROWSER, C_BACK, C_FORWARD, C_ADDRESS, C_PROPS, C_FIND };

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

/* The browser's tree follows the folder shown (phase E), however it was gone to (the address bar, Back, Up, a
   folder opened in the list, Find's Go To): down from its drive, each folder on the way opened. */
static void tree_follow(struct view *v)
{
    char root[4], part[64], shown[128];
    const char *rest, *e;
    struct gui_node *n, *c;
    size_t k;
    if (!v->tree || !v->dir[0])
        return;
    snprintf(root, sizeof root, "%c:", toupper((unsigned char)v->dir[0]));
    if (!(n = gui_tree_child(v->tree, NULL, root)))
        return;
    v->following = 1;                           /* (what the tree tells on the way is no going) */
    for (rest = strlen(v->dir) > 3 ? v->dir + 3 : ""; *rest; rest += k + (e ? 1 : 0)) {
        e = strchr(rest, '\\');
        k = e ? (size_t)(e - rest) : strlen(rest);
        snprintf(part, sizeof part, "%.*s", (int)k, rest);
        gui_tree_expand(v->tree, n, 1);
        if (!(c = gui_tree_child(v->tree, n, gui_dos_text(part, shown, sizeof shown))))
            break;
        n = c;
    }
    if (gui_tree_get(v->tree) != n)
        gui_tree_set(v->tree, n);
    v->following = 0;
}

static void go(struct view *v, const char *dir)
{
    snprintf(v->dir, sizeof v->dir, "%s", dir);
    tree_follow(v);
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

/* A paste onto a name that is there already (phase E) is asked about: Replace (a file: written over; a folder:
   what is copied goes into it, over its files of the same names), Skip, Keep Both (the copy gets a name that
   is free: LETTER~1.TXT), or Cancel, which stops the paste there; "for all" answers for the rest of this
   paste. Before this a paste wrote over what was there without a word. */
enum { CLASH_REPLACE = 1, CLASH_SKIP, CLASH_BOTH, CLASH_STOP };

static struct {
    struct gui_widget *all;
    int for_all;
} clash;

static void clash_button(struct gui_widget *b, int what)
{
    (void)what;
    clash.for_all = gui_checked(clash.all);     /* (read while the dialog is there) */
    gui_end_modal(b->win, b->id);
}

static int clash_ask(struct view *v, const char *name, int dir)
{
    struct gui_window *d = gui_window(GUI_T("Paste"), 340, 0, GUI_W_DIALOG);
    struct gui_widget *box, *row;
    char t[200], shown[128];
    int r;
    d->owner = v->win;
    box = gui_vbox(d->root, 6, 8);
    snprintf(t, sizeof t, dir ? GUI_T("A folder named %s is here already.") : GUI_T("A file named %s is here already."),
             gui_dos_text(name, shown, sizeof shown));
    gui_label(box, t);
    gui_label(box, dir ? GUI_T("Replace copies into it, over files of the same names.")
                       : GUI_T("Replace writes over it. Keep Both names the copy anew."));
    clash.all = gui_check(box, GUI_T("Do this for &all that are here already"), 0);
    clash.for_all = 0;
    row = gui_hbox(box, 6, 0);
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("&Replace"), clash_button, CLASH_REPLACE);
    gui_button(row, GUI_T("&Skip"), clash_button, CLASH_SKIP)->flags |= GUI_DEFAULT;
    gui_button(row, GUI_T("&Keep Both"), clash_button, CLASH_BOTH);
    gui_button(row, GUI_T("Cancel"), clash_button, CLASH_STOP)->flags |= GUI_CANCEL;
    r = gui_modal(d);
    gui_close(d);
    return r >= CLASH_REPLACE && r <= CLASH_BOTH ? r : CLASH_STOP;     /* (its close box: Cancel) */
}

/* A name that is free in dir, made from name as DOS makes its short ones: LETTER~1.TXT, ~2 ... (six of its
   letters kept, five from ~10 on). */
static int free_name(const char *dir, const char *name, char *out, int room)
{
    const char *dot = strrchr(name, '.');
    int stem = dot && dot > name ? (int)(dot - name) : (int)strlen(name), k;
    char leaf[24];
    struct gui_dirent e;
    if (!dot || dot == name)
        dot = "";
    for (k = 1; k < 100; k++) {
        int keep = k < 10 ? 6 : 5;
        snprintf(leaf, sizeof leaf, "%.*s~%d%.4s", stem < keep ? stem : keep, name, k, dot);
        gui_path_join(out, room, dir, leaf);
        if (gui_stat(out, &e) != 0)
            return 0;
    }
    return -1;
}

/* Is `to` the folder `from`, or under it? */
static int inside(const char *from, const char *to)
{
    size_t n = strlen(from);
    return !strncasecmp(from, to, n) && (!to[n] || to[n] == '\\');
}

static void paste(struct view *v)
{
    int i, done = 0, all = 0;
    char to[260], t[80];
    struct gui_dirent e;
    for (i = 0; i < clip.n; i++) {
        const char *name = gui_path_name(clip.paths[i]);
        int dir = gui_is_dir(clip.paths[i]);
        gui_path_join(to, sizeof to, v->dir, name);
        if (!strcmp(to, clip.paths[i])) {       /* into its own folder: a copy beside it; a move: it is there */
            if (clip.cut || free_name(v->dir, name, to, sizeof to) != 0)
                continue;
        } else if (dir && inside(clip.paths[i], to)) {
            gui_message(v->win, GUI_T("Files"), GUI_T("A folder can't be put into itself."), GUI_MB_OK);
            continue;
        } else if (gui_stat(to, &e) == 0) {     /* one of that name is here already */
            int a = all ? all : clash_ask(v, name, dir && e.dir);
            if (a == CLASH_STOP)
                break;
            if (!all && clash.for_all)
                all = a;
            if (a == CLASH_SKIP || (a == CLASH_BOTH && free_name(v->dir, name, to, sizeof to) != 0))
                continue;
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
static void properties(struct view *v);
static void find_files(struct view *v);

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
    case C_PROPS:
        properties(v);
        break;
    case C_FIND:
        find_files(v);
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
        if (v->context) {
            gui_context(v->list, NULL);
            gui_menu_free(v->context);
        }
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
    gui_menu_item(m, GUI_T("P&roperties"), C_PROPS, GUI_T("Alt+Enter"));
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("&Close"), C_CLOSE, GUI_T("Ctrl+W"));
    m = gui_menu(bar, GUI_T("&Edit"));
    gui_menu_item(m, GUI_T("Cu&t"), C_CUT, GUI_T("Ctrl+X"));
    gui_menu_item(m, GUI_T("&Copy"), C_COPY, GUI_T("Ctrl+C"));
    gui_menu_item(m, GUI_T("&Paste"), C_PASTE, GUI_T("Ctrl+V"));
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("Select &All"), C_ALL, GUI_T("Ctrl+A"));
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("&Find..."), C_FIND, GUI_T("Ctrl+F"));
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

/* ---- properties (phase E): what the chosen file or folder is: its name, where it is, its kind, its size (a
   folder's: its files' in all, counted to a limit), when it was last written, its attributes; with nothing
   chosen, the folder shown, and its drive's room. */

#define COUNT_MAX 5000

static void count(const char *dir, unsigned long *files, unsigned long long *bytes, int depth)
{
    struct gui_dirent *e = NULL;
    int n = gui_dir_read(dir, &e), i;
    char p[260];
    for (i = 0; i < n; i++) {
        if (!e[i].dir) {
            (*files)++;
            *bytes += e[i].size;
        } else if (depth < 8 && *files < COUNT_MAX) {
            gui_path_join(p, sizeof p, dir, e[i].name);
            count(p, files, bytes, depth + 1);
        }
    }
    if (n >= 0)
        free(e);
}

static void props_ok(struct gui_widget *b, int what)
{
    (void)what;
    gui_end_modal(b->win, 1);
}

static void props_row(struct gui_widget *form, const char *label, const char *text)
{
    gui_label(gui_row(form, label, NULL), text);
}

static void properties(struct view *v)
{
    struct gui_window *d;
    struct gui_widget *box, *form, *row;
    struct gui_dirent ent;
    char path[260], t[160], name[128];
    int r = gui_list_get(v->list), item = r >= 0;
    memset(&ent, 0, sizeof ent);
    if (item) {
        ent = v->ents[(size_t)gui_list_user(v->list, r)];
        gui_path_join(path, sizeof path, v->dir, ent.name);
    } else {
        snprintf(path, sizeof path, "%s", v->dir);
        ent.dir = 1;
        snprintf(ent.text, sizeof ent.text, "%s", strlen(path) > 3 ? gui_dos_text(gui_path_name(path), name, sizeof name) : path);
    }
    d = gui_window(GUI_T("Properties"), 300, 0, GUI_W_DIALOG);
    d->owner = v->win;
    box = gui_vbox(d->root, 6, 8);
    form = gui_form(box);
    props_row(form, GUI_T("Name:"), ent.text);
    props_row(form, GUI_T("Where:"), item ? v->dir : path);
    if (ent.dir) {
        unsigned long files = 0;
        unsigned long long bytes = 0;
        count(path, &files, &bytes, 0);
        props_row(form, GUI_T("Kind:"), GUI_T("Folder"));
        snprintf(t, sizeof t, files >= COUNT_MAX ? GUI_T("%lu files or more, %lu KB or more") : GUI_T("%lu files, %lu KB"), files,
                 (unsigned long)((bytes + 1023) / 1024));
        props_row(form, GUI_T("Holds:"), t);
    } else {
        const char *dot = strrchr(ent.name, '.');
        snprintf(t, sizeof t, dot && dot[1] ? GUI_T("%s file") : GUI_T("File"), dot ? dot + 1 : "");
        props_row(form, GUI_T("Kind:"), t);
        snprintf(t, sizeof t, GUI_T("%lu bytes"), ent.size);
        props_row(form, GUI_T("Size:"), t);
    }
    if (item) {
        snprintf(t, sizeof t, "%04u-%02u-%02u %02u:%02u", (ent.date >> 9) + 1980, (ent.date >> 5) & 15, ent.date & 31,
                 ent.time >> 11, (ent.time >> 5) & 63);
        props_row(form, GUI_T("Modified:"), t);
        snprintf(t, sizeof t, "%s%s%s%s", ent.attr & 1 ? GUI_T("Read-only ") : "", ent.attr & 2 ? GUI_T("Hidden ") : "",
                 ent.attr & 4 ? GUI_T("System ") : "", ent.attr & 0x20 ? GUI_T("Archive") : "");
        props_row(form, GUI_T("Attributes:"), t[0] ? t : GUI_T("None"));
    } else {                                    /* the folder shown: its drive's room */
        struct glos_fs_drive dr;
        unsigned n = 0;
        memset(&dr, 0, sizeof dr);
        if (glos_fs_drives((unsigned)path[0], &dr, 1, &n) == 0 && n && dr.total_kb && (dr.flags & GLOS_DRIVE_DETAILS)) {
            snprintf(t, sizeof t, GUI_T("%u MB free of %u MB"), dr.free_kb / 1024, dr.total_kb / 1024);
            props_row(form, GUI_T("Drive:"), t);
        }
    }
    row = gui_hbox(box, 6, 0);
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("OK"), props_ok, 1)->flags |= GUI_DEFAULT | GUI_CANCEL;
    say("properties", path);
    gui_modal(d);
    gui_close(d);
}

/* ---- search (phase E): files and folders by name, under the folder shown: a name with DOS's wildcards (* and
   ?), or a piece of a name; what is found is listed with its folder, and Go To shows one in its folder. Looked
   for at once, to a limit (the plan's search in the background, with its results as they come, is to come). */

#define FOUND_MAX 200
#define VISIT_MAX 5000

static struct {
    struct gui_window *win;
    struct gui_widget *what, *list, *note;
    char dir[FOUND_MAX][200], name[FOUND_MAX][64];
    int n, visited, go;
} fnd;

/* A name against a pattern of * and ?, letters' case apart. */
static int wild(const char *p, const char *s)
{
    for (; *p; p++, s++) {
        if (*p == '*') {
            while (p[1] == '*')
                p++;
            if (!p[1])
                return 1;
            for (; *s; s++)
                if (wild(p + 1, s))
                    return 1;
            return 0;
        }
        if (!*s || (*p != '?' && toupper((unsigned char)*p) != toupper((unsigned char)*s)))
            return 0;
    }
    return !*s;
}

static void find_in(const char *dir, const char *pattern, int depth)
{
    struct gui_dirent *e = NULL;
    int n = gui_dir_read(dir, &e), i;
    char p[260];
    for (i = 0; i < n && fnd.n < FOUND_MAX && fnd.visited < VISIT_MAX; i++) {
        fnd.visited++;
        if (wild(pattern, e[i].name)) {
            snprintf(fnd.dir[fnd.n], sizeof fnd.dir[0], "%s", dir);
            snprintf(fnd.name[fnd.n++], sizeof fnd.name[0], "%s", e[i].name);
        }
        if (e[i].dir && depth < 8) {
            gui_path_join(p, sizeof p, dir, e[i].name);
            find_in(p, pattern, depth + 1);
        }
    }
    if (n >= 0)
        free(e);
}

static void find_button(struct gui_widget *b, int what)
{
    struct view *v = b->win->owner ? view_of(b->win->owner) : NULL;
    char pattern[80], t[100];
    int i;
    (void)what;
    if (b->id == 10 && v) {                     /* Find: under the folder shown */
        snprintf(pattern, sizeof pattern, "%s", gui_get_text(fnd.what));
        if (!pattern[0])
            return;
        if (!strpbrk(pattern, "*?")) {          /* (a piece of a name: anywhere in it) */
            snprintf(t, sizeof t, "*%s*", pattern);
            snprintf(pattern, sizeof pattern, "%s", t);
        }
        fnd.n = fnd.visited = 0;
        find_in(v->dir, pattern, 0);
        gui_list_clear(fnd.list);
        for (i = 0; i < fnd.n; i++) {
            const char *cells[2] = { fnd.name[i], fnd.dir[i] };
            gui_list_add(fnd.list, cells, NULL, NULL);
        }
        snprintf(t, sizeof t, fnd.n == FOUND_MAX || fnd.visited == VISIT_MAX ? GUI_T("%d found, and there may be more.")
                                                                           : GUI_T("%d found."), fnd.n);
        gui_set_text(fnd.note, t);
        if (fnd.n) {
            gui_list_set(fnd.list, 0);
            gui_focus(fnd.list);
        }
        snprintf(t, sizeof t, "%d %s", fnd.n, gui_get_text(fnd.what));
        say("found", t);
    } else if (b->id == 11) {                   /* Go To: the chosen one, in its folder */
        fnd.go = gui_list_get(fnd.list);
        if (fnd.go >= 0 && fnd.go < fnd.n)
            gui_end_modal(fnd.win, 1);
    } else if (b->id == 12) {
        gui_end_modal(fnd.win, 0);
    }
}

static void find_list_on(struct gui_widget *w, int what)
{
    if (what == GUI_ON_ACTIVATE) {              /* (Enter or a double click on one: Go To) */
        fnd.go = gui_list_get(w);
        if (fnd.go >= 0 && fnd.go < fnd.n)
            gui_end_modal(fnd.win, 1);
    }
}

static void find_files(struct view *v)
{
    static const char *const heads[2] = { GUI_N("Name"), GUI_N("In folder") };
    static const int widths[2] = { 110, 210 };
    struct gui_widget *box, *row;
    int r, i;
    fnd.win = gui_window(GUI_T("Find Files"), 360, 260, GUI_W_DIALOG);
    fnd.win->owner = v->win;
    box = gui_vbox(fnd.win->root, 6, 8);
    gui_grow(box, 1);
    row = gui_hbox(box, 6, 0);
    gui_label(row, GUI_T("Named:"));
    fnd.what = gui_field(row, 24);
    gui_grow(fnd.what, 1);
    gui_button(row, GUI_T("&Find"), find_button, 10)->flags |= GUI_DEFAULT;
    fnd.list = gui_list(box, 2, heads, widths);
    gui_grow(fnd.list, 1);
    gui_on(fnd.list, find_list_on, 0);
    fnd.note = gui_label(box, GUI_T("A name, or a piece of one; * and ? stand for any letters."));
    row = gui_hbox(box, 6, 0);
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("&Go To"), find_button, 11);
    gui_button(row, GUI_T("Close"), find_button, 12)->flags |= GUI_CANCEL;
    fnd.n = 0;
    gui_focus(fnd.what);
    r = gui_modal(fnd.win);
    gui_close(fnd.win);
    fnd.win = NULL;
    if (r != 1)
        return;
    if (v->browser)                             /* the one chosen, shown in its folder */
        go(v, fnd.dir[fnd.go]);
    else
        folder_open(fnd.dir[fnd.go]);
    for (i = 0; v->browser && i < v->n; i++)
        if (!strcmp(v->ents[(size_t)gui_list_user(v->list, i)].name, fnd.name[fnd.go]))
            gui_list_set(v->list, i);
    say("went", fnd.name[fnd.go]);
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
            { 0, NULL, NULL, NULL },
            { C_FIND, GUI_N("&Find..."), "find", GUI_N("Find files (Ctrl+F)") },
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
        {                                       /* the list's context menu (the right button, the Menu key) */
            struct gui_menu *c = v->context = gui_menu_new();
            gui_menu_item(c, GUI_T("&Open"), C_OPEN, NULL);
            gui_menu_sep(c);
            gui_menu_item(c, GUI_T("Cu&t"), C_CUT, NULL);
            gui_menu_item(c, GUI_T("&Copy"), C_COPY, NULL);
            gui_menu_item(c, GUI_T("&Paste"), C_PASTE, NULL);
            gui_menu_sep(c);
            gui_menu_item(c, GUI_T("&Delete"), C_DELETE, NULL);
            gui_menu_item(c, GUI_T("Rena&me"), C_RENAME, NULL);
            gui_menu_sep(c);
            gui_menu_item(c, GUI_T("&New Folder"), C_NEWDIR, NULL);
            gui_menu_item(c, GUI_T("P&roperties"), C_PROPS, NULL);
            gui_context(v->list, c);
        }
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
    } else if (what == GUI_ON_SELECT && !v->following && strcmp(v->dir, path)) {
        go(v, path);
    }
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
        snprintf(root, sizeof root, "%c:", letters[i]);
        snprintf(p, 4, "%c:\\", letters[i]);
        gui_tree_add(v->tree, NULL, root, icon(IC_DRIVE, 0), p, 1);
    }
    go(v, dir);                                 /* (the tree goes down the folders to dir: tree_follow) */
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
