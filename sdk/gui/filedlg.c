/* Open and Save (toolkit 2, step C6; docs/v1/toolkit-2.md, Dialogs): gui_file_dialog_ex.
 *
 * Places down the left (the drives, then the app's recent folders); the folder as a combo box above (the folders
 * typed completed, the ones visited kept); the files in a virtual list (detail or icon view, sorted by a column's
 * head; folders first), the files the chosen filter lets through; an optional preview pane the app draws for the
 * file chosen; the name, the filter and (saving) a format at the bottom. Keys: Enter on a folder goes in, on a
 * file takes it; Backspace in the list goes up; a name with * or ? typed is a filter of its own. Names are DOS's
 * 8.3 ones, shown in its code page as text (gui_dos_text), as everywhere in the toolkit for now.
 *
 * gui_file_dialog keeps its own, older form until the apps move to this one (step C10). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guiint.h"

#define ROOM 260

static struct fdx {
    struct gui_window *w;
    struct gui_widget *at, *places, *files, *name, *type, *format, *preview, *view;
    struct gui_file_opts *o;
    int save;
    char *out;
    int room;
    char dir[ROOM];
    struct gui_dirent *ents;
    int n;
    int *shown, nshown;                 /* places in ents the filter lets through, in the list's order */
    int sort_col, sort_down;
    char pattern[64];                   /* a pattern typed as the name (*.TXT), over the filter's */
    char cells[3][96];
    char place[32][ROOM];               /* the places' folders, row by row */
    int nplaces;
    char chosen[ROOM];                  /* the file the list is on (for the preview), or "" */
} X;

static int up(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

static void upper(char *s)                      /* (DOS's names are its capitals, whatever was typed) */
{
    for (; *s; s++)
        *s = (char)up((unsigned char)*s);
}

/* DOS's name against one pattern (* and ?, any case). */
static int glob(const char *p, const char *s)
{
    for (; *p; p++, s++) {
        if (*p == '*') {
            if (!p[1])
                return 1;
            for (; *s; s++)
                if (glob(p + 1, s))
                    return 1;
            return glob(p + 1, s);
        }
        if (!*s || (*p != '?' && up((unsigned char)*p) != up((unsigned char)*s)))
            return 0;
    }
    return !*s || (s[0] == '.' && !s[1]);
}

/* The filter now in force: the name field's pattern, else the chosen filter's ("Pictures|*.BMP;*.PNG"). */
static const char *patterns(void)
{
    const char *f;
    if (X.pattern[0])
        return X.pattern;
    if (!X.o || !X.o->filters || !X.o->filters[0])
        return "*.*";
    f = X.o->filters[X.o->filter >= 0 ? X.o->filter : 0];
    return strchr(f, '|') ? strchr(f, '|') + 1 : "*.*";
}

static int passes(const char *name)
{
    const char *p = patterns();
    char one[32];
    while (*p) {
        size_t n = strcspn(p, ";");
        if (n < sizeof one) {
            memcpy(one, p, n);
            one[n] = 0;
            if (!strcmp(one, "*.*") || glob(one, name) || (!strchr(name, '.') && !strcmp(one, "*")))
                return 1;
        }
        p += n;
        if (*p == ';')
            p++;
    }
    return 0;
}

static const char *icon_name(const struct gui_dirent *e)
{
    static const struct { const char *ext, *icon; } kinds[] = {
        { "TXT", "file-text" }, { "DOC", "file-text" }, { "CFG", "file-text" }, { "INI", "file-text" },
        { "MD", "file-text" }, { "BMP", "file-image" }, { "PNG", "file-image" }, { "JPG", "file-image" },
        { "GIF", "file-image" }, { "PCX", "file-image" }, { "TGA", "file-image" }, { "WAV", "file-sound" },
        { "MID", "file-sound" }, { "MOD", "file-sound" }, { "VOC", "file-sound" }, { "AVI", "file-video" },
        { "FLI", "file-video" }, { "ZIP", "file-archive" }, { "ARJ", "file-archive" }, { "LZH", "file-archive" },
        { "EXE", "file-program" }, { "COM", "file-program" }, { "BAT", "file-batch" }, { "C", "file-code" },
        { "H", "file-code" }, { "ASM", "file-code" }, { "PAS", "file-code" }, { "FON", "file-font" },
    };
    const char *dot = strrchr(e->name, '.');
    size_t i;
    if (e->dir)
        return "folder";
    for (i = 0; dot && i < sizeof kinds / sizeof kinds[0]; i++) {
        const char *a = dot + 1, *b = kinds[i].ext;
        while (*a && *b && up((unsigned char)*a) == *b)
            a++, b++;
        if (!*a && !*b)
            return kinds[i].icon;
    }
    return "file";
}

static int cmp_ents(const void *pa, const void *pb)
{
    const struct gui_dirent *a = &X.ents[*(const int *)pa], *b = &X.ents[*(const int *)pb];
    long d;
    if (a->dir != b->dir)
        return a->dir ? -1 : 1;                 /* (folders first, whichever way) */
    if (X.sort_col == 1)
        d = (long)a->size - (long)b->size;
    else if (X.sort_col == 2)
        d = ((long)a->date << 16 | a->time) - ((long)b->date << 16 | b->time);
    else
        d = 0;
    if (!d) {
        const char *s = a->name, *t = b->name;
        while (*s && up((unsigned char)*s) == up((unsigned char)*t))
            s++, t++;
        d = up((unsigned char)*s) - up((unsigned char)*t);
    }
    if (X.sort_down)
        d = -d;
    return d > 0 ? 1 : d < 0 ? -1 : 0;
}

/* The folder's files again: read, filtered, sorted. */
static void fill(void)
{
    int i;
    free(X.ents);
    free(X.shown);
    X.ents = NULL, X.shown = NULL, X.nshown = 0;
    X.n = gui_dir_read(X.dir, &X.ents);
    if (X.n < 0)
        X.n = 0;
    X.shown = malloc((size_t)(X.n ? X.n : 1) * sizeof *X.shown);
    for (i = 0; X.shown && i < X.n; i++)
        if (X.ents[i].dir || passes(X.ents[i].name))
            X.shown[X.nshown++] = i;
    if (X.nshown > 1)
        qsort(X.shown, (size_t)X.nshown, sizeof *X.shown, cmp_ents);
    gui_vlist_changed(X.files);
    gui_vlist_set(X.files, X.nshown ? 0 : -1);
}

static void go(const char *dir)
{
    snprintf(X.dir, sizeof X.dir, "%s", dir);
    gui_set_text(X.at, X.dir);
    gui_combo_add_top(X.at, X.dir);            /* (the folders visited, for the combo box's list) */
    X.chosen[0] = 0;
    fill();
    if (X.preview)
        gui_damage(X.preview);
}

/* ---- the files' model */
static int m_count(void *ctx) { (void)ctx; return X.nshown; }

static void m_row(void *ctx, int row, struct gui_vrow *out)
{
    const struct gui_dirent *e;
    (void)ctx;
    if (row < 0 || row >= X.nshown)
        return;
    e = &X.ents[X.shown[row]];
    gui_dos_text(e->name, X.cells[0], sizeof X.cells[0]);
    if (e->dir)
        snprintf(X.cells[1], sizeof X.cells[1], "Folder");
    else if (e->size < 10240)
        snprintf(X.cells[1], sizeof X.cells[1], "%lu bytes", e->size);
    else
        snprintf(X.cells[1], sizeof X.cells[1], "%lu KB", (e->size + 1023) / 1024);
    snprintf(X.cells[2], sizeof X.cells[2], "%04u-%02u-%02u %02u:%02u", (e->date >> 9) + 1980, (e->date >> 5) & 15,
             e->date & 31, e->time >> 11, (e->time >> 5) & 63);
    out->cell[0] = X.cells[0], out->cell[1] = X.cells[1], out->cell[2] = X.cells[2];
    out->icon = gui_icon(icon_name(e), 0);
    out->big = gui_icon(icon_name(e), 1);
    out->id = (unsigned long)X.shown[row] + 1;
}

static int m_find(void *ctx, unsigned long id)
{
    int i;
    (void)ctx;
    for (i = 0; i < X.nshown; i++)
        if ((unsigned long)X.shown[i] + 1 == id)
            return i;
    return -1;
}

static int m_match(void *ctx, const char *typed, int from)
{
    int i, k;
    (void)ctx;
    for (k = 0; k < X.nshown; k++) {
        const char *s = X.ents[X.shown[(from + k) % X.nshown]].name, *t = typed;
        for (i = 0; t[i] && up((unsigned char)s[i]) == up((unsigned char)t[i]); i++)
            ;
        if (!t[i])
            return (from + k) % X.nshown;
    }
    return -1;
}

static const struct gui_dirent *current(void)
{
    int r = gui_vlist_get(X.files);
    return r >= 0 && r < X.nshown ? &X.ents[X.shown[r]] : NULL;
}

/* ---- taking a name */
static void finish(const char *path)
{
    snprintf(X.out, (size_t)X.room, "%s", path);
    if (X.o) {
        if (X.type)
            X.o->filter = gui_value(X.type);
        if (X.format)
            X.o->format = gui_value(X.format);
    }
    gui_end_modal(X.w, GUI_OK);
}

static void take(void)
{
    char t[128], dos[128], path[ROOM];
    const struct gui_dirent *e = current();
    snprintf(t, sizeof t, "%s", gui_get_text(X.name));
    if (!t[0]) {                                /* (no name typed: the list's) */
        if (!e)
            return;
        if (e->dir) {
            gui_path_join(path, sizeof path, X.dir, e->name);
            go(path);
            return;
        }
        gui_dos_text(e->name, t, sizeof t);
    }
    if (strchr(t, '*') || strchr(t, '?')) {     /* a pattern: a filter of its own */
        snprintf(X.pattern, sizeof X.pattern, "%.63s", t);
        gui_set_text(X.name, "");
        fill();
        return;
    }
    if (gui_text_dos(t, dos, sizeof dos) != 0)
        return;
    upper(dos);
    if ((dos[0] && dos[1] == ':') || dos[0] == '\\')
        snprintf(path, sizeof path, "%s", dos);
    else
        gui_path_join(path, sizeof path, X.dir, dos);
    if (gui_is_dir(path)) {                     /* a folder named: there */
        gui_set_text(X.name, "");
        go(path);
        return;
    }
    if (!X.save) {
        struct gui_dirent st;
        if (gui_stat(path, &st) != 0) {
            gui_message(X.w, "Open", "There is no such file there.", GUI_MB_OK);
            return;
        }
        finish(path);
        return;
    }
    if (!strchr(gui_path_name(path), '.')) {    /* (saving a name without a type: the filter's first) */
        const char *p = patterns(), *dot = strchr(p, '.');
        size_t n = dot ? strcspn(dot, ";") : 0;
        if (dot && n > 1 && n < 5 && !memchr(dot, '*', n) && !memchr(dot, '?', n) && strlen(path) + n < sizeof path)
            strncat(path, dot, n);
    }
    {
        struct gui_dirent st;
        if (gui_stat(path, &st) == 0 && gui_message(X.w, "Save", "That file is there already. Replace it?",
                                                    GUI_MB_YESNO) != GUI_YES)
            return;
    }
    finish(path);
}

/* ---- the widgets' functions */
static void files_on(struct gui_widget *w, int what)
{
    const struct gui_dirent *e = current();
    char path[ROOM], t[128];
    if (what == GUI_ON_SELECT && e) {
        if (!e->dir)                            /* (a file chosen: its name, and its preview) */
            gui_set_text(X.name, gui_dos_text(e->name, t, sizeof t));
        gui_path_join(X.chosen, sizeof X.chosen, X.dir, e->name);
        if (e->dir)
            X.chosen[0] = 0;
        if (X.preview)
            gui_damage(X.preview);
    } else if (what == GUI_ON_ACTIVATE && e) {
        if (e->dir) {
            gui_path_join(path, sizeof path, X.dir, e->name);
            gui_set_text(X.name, "");
            go(path);
        } else {
            take();
        }
    } else if (what == GUI_ON_SORT) {
        gui_vlist_sort(w, &X.sort_col, &X.sort_down);
        fill();
    }
}

static int files_key(struct gui_window *w, struct gui_ev *e)
{
    (void)w;
    if (e->type == GUI_EV_KEY && e->key == GUI_K_BACK && !e->mods && X.w->focus == X.files && strlen(X.dir) > 3) {
        char path[ROOM];
        snprintf(path, sizeof path, "%s", X.dir);
        gui_path_up(path);                      /* Backspace in the list: up a folder */
        go(path);
        return 1;
    }
    return 0;
}

static void up_click(struct gui_widget *w, int what)
{
    char path[ROOM];
    (void)w, (void)what;
    snprintf(path, sizeof path, "%s", X.dir);
    if (strlen(path) > 3) {
        gui_path_up(path);
        go(path);
    }
}

static void view_click(struct gui_widget *w, int what)
{
    (void)what;
    X.o->view = X.o->view == GUI_VIEW_ICONS ? GUI_VIEW_DETAIL : GUI_VIEW_ICONS;
    gui_vlist_view(X.files, X.o->view);
    (void)w;
}

static void at_on(struct gui_widget *w, int what)
{
    char dos[ROOM];
    if (what != GUI_ON_ACTIVATE)
        return;
    if (gui_text_dos(gui_get_text(w), dos, sizeof dos) == 0 && (upper(dos), gui_is_dir(dos)))
        go(dos);
    else
        gui_message(X.w, X.save ? "Save" : "Open", "There is no such folder.", GUI_MB_OK);
}

static void at_source(struct gui_widget *w, const char *typed)  /* the folders that begin with what is typed */
{
    char parent[ROOM], path[ROOM], text[ROOM];
    struct gui_dirent *e = NULL;
    const char *slash = strrchr(typed, '\\');
    int n, i, k = 0;
    if (!slash || slash - typed + 1 >= (int)sizeof parent)
        return;
    memcpy(parent, typed, (size_t)(slash - typed + 1));
    parent[slash - typed + 1] = 0;
    if (gui_text_dos(parent, path, sizeof path) != 0 || (n = gui_dir_read(path, &e)) < 0)
        return;
    gui_combo_clear(w);
    for (i = 0; i < n && k < 40; i++)
        if (e[i].dir) {
            char name[64];
            snprintf(text, sizeof text, "%s%s", parent, gui_dos_text(e[i].name, name, sizeof name));
            gui_combo_add(w, text);
            k++;
        }
    free(e);
}

static void places_on(struct gui_widget *w, int what)
{
    int r = gui_list_get(w);
    if ((what == GUI_ON_SELECT || what == GUI_ON_ACTIVATE) && r >= 0 && r < X.nplaces && gui_is_dir(X.place[r]))
        go(X.place[r]);
}

static void type_on(struct gui_widget *w, int what)
{
    if (what != GUI_ON_SELECT)
        return;
    X.o->filter = gui_value(w);
    X.pattern[0] = 0;
    fill();
}

static void name_on(struct gui_widget *w, int what)
{
    (void)w;
    if (what == GUI_ON_ACTIVATE)
        take();
}

static void ok_click(struct gui_widget *w, int what)
{
    (void)w, (void)what;
    take();
}

static void cancel_click(struct gui_widget *w, int what)
{
    (void)what;
    gui_end_modal(w->win, GUI_CANCEL_R);
}

static void preview_draw(struct gui_widget *w, struct gfx_surface *s)
{
    gfx_fill(s, w->r.x, w->r.y, w->r.w, w->r.h, GUI_FIELD);
    gfx_frame(s, w->r.x, w->r.y, w->r.w, w->r.h, GUI_LINE);
    if (X.chosen[0] && X.o && X.o->preview)
        X.o->preview(w, s, X.chosen, X.o->ctx);
}

int gui_file_dialog_ex(struct gui_window *owner, const char *title, int save, char *path, int room,
                       struct gui_file_opts *o)
{
    static const char *const heads[] = { "Name", "Size", "Modified" };
    static const int widths[] = { 150, 80, 110 };
    static const struct gui_model model = { m_count, m_row, m_find, m_match, NULL };
    static struct gui_file_opts none;
    struct gui_widget *box, *row, *mid, *form, *ok, *left, *right;
    char drives[27], start[ROOM];
    int r, i, n;
    memset(&X, 0, sizeof X);
    memset(&none, 0, sizeof none);
    X.o = o ? o : &none;
    X.save = save, X.out = path, X.room = room;
    X.w = gui_window(title, 600, 380, GUI_W_DIALOG);
    X.w->owner = owner;
    X.w->on_key = files_key;
    box = gui_vbox(X.w->root, GUI_GAP, 8);
    row = gui_hbox(box, GUI_GAP, 0);
    gui_label(row, "Look &in:");
    X.at = gui_combo(row, 40);
    gui_grow(X.at, 1);
    gui_combo_history(X.at, 12);
    gui_combo_source(X.at, at_source);
    gui_on(X.at, at_on, 0);
    for (i = 0; X.o->recent && X.o->recent[i]; i++)
        gui_combo_add(X.at, X.o->recent[i]);
    gui_button(row, "&Up", up_click, 0);
    X.view = gui_button(row, "&View", view_click, 0);  /* (details and icons, turn about) */
    mid = gui_hbox(box, GUI_GAP, 0);
    gui_grow(mid, 1);
    left = gui_list(mid, 1, NULL, NULL);       /* the places: the drives, then the recent folders */
    gui_size(left, 110, 0);
    X.places = left;
    n = gui_drives(drives);
    for (i = 0; i < n && X.nplaces < 32; i++) {
        char t[4] = { drives[i], ':', 0, 0 };
        const char *cells[1] = { t };
        snprintf(X.place[X.nplaces++], ROOM, "%c:\\", drives[i]);
        gui_list_add(left, cells, gui_icon(drives[i] < 'C' ? "drive-floppy" : "drive", 0), NULL);
    }
    for (i = 0; X.o->recent && X.o->recent[i] && X.nplaces < 32; i++) {
        const char *cells[1] = { gui_path_name(X.o->recent[i])[0] ? gui_path_name(X.o->recent[i]) : X.o->recent[i] };
        snprintf(X.place[X.nplaces++], ROOM, "%s", X.o->recent[i]);
        gui_list_add(left, cells, gui_icon("folder", 0), NULL);
    }
    gui_on(left, places_on, 0);
    X.files = gui_vlist(mid, 3, heads, widths, &model);
    gui_grow(X.files, 1);
    gui_vlist_view(X.files, X.o->view == GUI_VIEW_ICONS ? GUI_VIEW_ICONS : GUI_VIEW_DETAIL);
    gui_on(X.files, files_on, 0);
    if (X.o->preview) {                         /* the app draws the chosen file here */
        right = gui_canvas(mid, 140, 140, preview_draw, NULL);
        X.preview = right;
    }
    form = gui_form(box);
    X.name = gui_field(gui_row(form, "File &name:", NULL), 40);
    gui_on(X.name, name_on, 0);
    if (X.o->filters && X.o->filters[0]) {
        X.type = gui_choice(gui_row(form, "Files of &type:", NULL), 30);
        for (i = 0; X.o->filters[i]; i++) {
            char t[64];
            const char *bar = strchr(X.o->filters[i], '|');
            snprintf(t, sizeof t, "%.*s (%s)", bar ? (int)(bar - X.o->filters[i]) : 40, X.o->filters[i],
                     bar ? bar + 1 : "");
            gui_choice_add(X.type, t);
        }
        gui_set_value(X.type, X.o->filter);
        gui_on(X.type, type_on, 0);
    }
    if (save && X.o->formats && X.o->formats[0]) {
        X.format = gui_choice(gui_row(form, "&Format:", NULL), 30);
        for (i = 0; X.o->formats[i]; i++)
            gui_choice_add(X.format, X.o->formats[i]);
        gui_set_value(X.format, X.o->format);
    }
    row = gui_hbox(box, GUI_GAP, 0);
    gui_space(row, 0, 0);
    (ok = gui_button(row, save ? "&Save" : "&Open", ok_click, 0))->flags |= GUI_DEFAULT;
    gui_button(row, "Cancel", cancel_click, 0)->flags |= GUI_CANCEL;
    snprintf(start, sizeof start, "%s", path && path[0] ? path : glos_boot_root());
    if (!gui_is_dir(start)) {                   /* (a file's path: its folder, and its name in the field) */
        char nm[128];
        gui_set_text(X.name, gui_dos_text(gui_path_name(start), nm, sizeof nm));
        gui_path_up(start);
    }
    if (!gui_is_dir(start))
        snprintf(start, sizeof start, "%s", glos_boot_root());
    go(start);
    X.w->focus = X.name;
    r = gui_modal(X.w);
    free(X.ents);
    free(X.shown);
    X.ents = NULL, X.shown = NULL;
    return r == GUI_OK;
}
