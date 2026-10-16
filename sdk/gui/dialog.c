/* The toolkit's dialogs (include/glos/gui.h): messages, a question with a
 * line to type, the file dialog; and the clipboard.
 *
 * Each is modal (gui_modal): its owner and the app's other windows hear
 * nothing until it is answered. The file dialog lists a folder (folders
 * first, then files, with their sizes), goes into a folder by Enter or a
 * double click, up by Backspace or "..", to another drive by its choice;
 * a name or path typed and Enter go there or choose it. */
#include <stdio.h>

#include "guiint.h"

/* ---- the clipboard */

int gui_clip_set(const char *text, size_t n) { return glos_clip_set(text, (unsigned)n); }

char *gui_clip_get(size_t *n)
{
    unsigned len = 0, got = 0;
    char *t;
    if (glos_clip_get(NULL, 0, &len) != 0 || !len)
        return NULL;
    if (!(t = malloc(len + 1)))
        return NULL;
    if (glos_clip_get(t, len, &got) != 0) {
        free(t);
        return NULL;
    }
    if (got > len)
        got = len;
    t[got] = 0;
    *n = got;
    return t;
}

/* ---- messages */

static void answer(struct gui_widget *w, int what)
{
    (void)what;
    gui_end_modal(w->win, w->id);
}

/* Text in lines no wider than width (at spaces), each a label. */
static void wrapped(struct gui_widget *box, const char *text, int width)
{
    char line[256];
    const char *p = text;
    while (*p) {
        int n = 0, last_space = -1;
        while (p[n] && p[n] != '\n' && n < (int)sizeof line - 1) {
            line[n] = p[n];
            if (p[n] == ' ')
                last_space = n;
            n++;
            line[n] = 0;
            if (gfx_text_width(gui_font, line, n) > width && last_space > 0) {
                n = last_space;
                break;
            }
        }
        line[n] = 0;
        {
            char esc[512];                      /* (shown as it is: "&" doubled, no letter of its own) */
            int i, o = 0;
            for (i = 0; line[i] && o < (int)sizeof esc - 2; i++) {
                if (line[i] == '&')
                    esc[o++] = '&';
                esc[o++] = line[i];
            }
            esc[o] = 0;
            gui_label(box, esc);
        }
        p += n;
        if (*p == ' ' || *p == '\n')
            p++;
    }
}

static struct gui_widget *buttons_row(struct gui_widget *parent)
{
    struct gui_widget *row = gui_hbox(parent, GUI_GAP, 0);
    gui_space(row, 0, 0);
    return row;
}

int gui_message(struct gui_window *owner, const char *title, const char *text, int buttons)
{
    struct gui_window *w = gui_window(title, 0, 0, GUI_W_DIALOG);
    struct gui_widget *box = gui_vbox(w->root, GUI_GAP, 12), *msg = gui_vbox(box, 2, 0), *row, *b;
    w->owner = owner;
    wrapped(msg, text, 360);
    gui_space(box, 0, 4);
    row = buttons_row(box);
    switch (buttons) {
    case GUI_MB_OKCANCEL:
        (b = gui_button(row, "OK", answer, GUI_OK))->flags |= GUI_DEFAULT;
        gui_button(row, "Cancel", answer, GUI_CANCEL_R)->flags |= GUI_CANCEL;
        break;
    case GUI_MB_YESNO:
        (b = gui_button(row, "&Yes", answer, GUI_YES))->flags |= GUI_DEFAULT;
        gui_button(row, "&No", answer, GUI_NO)->flags |= GUI_CANCEL;
        break;
    case GUI_MB_YESNOCANCEL:
        (b = gui_button(row, "&Yes", answer, GUI_YES))->flags |= GUI_DEFAULT;
        gui_button(row, "&No", answer, GUI_NO);
        gui_button(row, "Cancel", answer, GUI_CANCEL_R)->flags |= GUI_CANCEL;
        break;
    default:
        (b = gui_button(row, "OK", answer, GUI_OK))->flags |= GUI_DEFAULT | GUI_CANCEL;
        break;
    }
    w->focus = b;
    {
        int r = gui_modal(w);
        if (!r)                                 /* (closed by its box or Alt+F4) */
            r = buttons == GUI_MB_OK ? GUI_OK : buttons == GUI_MB_YESNO ? GUI_NO : GUI_CANCEL_R;
        return r;
    }
}

static struct {
    struct gui_widget *field;
    char *buf;
    int room;
} ask;

static void ask_ok(struct gui_widget *w, int what)
{
    (void)what;
    snprintf(ask.buf, (size_t)ask.room, "%s", gui_get_text(ask.field));
    gui_end_modal(w->win, GUI_OK);
}

int gui_ask(struct gui_window *owner, const char *title, const char *prompt, char *buf, int room)
{
    struct gui_window *w = gui_window(title, 0, 0, GUI_W_DIALOG);
    struct gui_widget *box = gui_vbox(w->root, GUI_GAP, 12), *f, *row;
    w->owner = owner;
    gui_label(box, prompt);
    f = gui_field(box, 36);
    gui_set_text(f, buf);
    gui_space(box, 0, 4);
    row = buttons_row(box);
    gui_button(row, "OK", ask_ok, GUI_OK)->flags |= GUI_DEFAULT;
    gui_button(row, "Cancel", answer, GUI_CANCEL_R)->flags |= GUI_CANCEL;
    ask.field = f, ask.buf = buf, ask.room = room;
    w->focus = f;
    return gui_modal(w) == GUI_OK;
}

/* ---- the file dialog */

struct fdlg {
    struct gui_window *w;
    struct gui_widget *where, *list, *name, *drive;
    char dir[260];
    char drives[27];
    struct gui_dirent *ents;
    int n, save;
    char *result;
    int room;
};

static void size_text(char *out, size_t room, const struct gui_dirent *e)
{
    if (e->dir)
        snprintf(out, room, "Folder");
    else if (e->size < 10240)
        snprintf(out, room, "%lu bytes", e->size);
    else if (e->size < 10240ul * 1024)
        snprintf(out, room, "%lu KB", (e->size + 1023) / 1024);
    else
        snprintf(out, room, "%lu MB", (e->size + 1024ul * 1024 - 1) / (1024ul * 1024));
}

static void fd_fill(struct fdlg *d)
{
    int i;
    char where[300];
    free(d->ents);
    d->ents = NULL;
    d->n = gui_dir_read(d->dir, &d->ents);
    if (d->n < 0)
        d->n = 0;
    gui_list_clear(d->list);
    if (strlen(d->dir) > 3) {
        const char *cells[2] = { "..", "" };
        gui_list_add(d->list, cells, NULL, NULL);
    }
    for (i = 0; i < d->n; i++) {
        char sz[24];
        const char *cells[2];
        size_text(sz, sizeof sz, &d->ents[i]);
        cells[0] = d->ents[i].name;
        cells[1] = sz;
        gui_list_add(d->list, cells, NULL, &d->ents[i]);
    }
    snprintf(where, sizeof where, "Folder: %s", d->dir);
    free(d->where->text);
    d->where->text = strdup(where);
    gui_damage(d->where);
    if (gui_list_count(d->list))
        gui_list_set(d->list, 0);
}

static void fd_go(struct fdlg *d, const char *dir)
{
    char n[260];
    strncpy(n, dir, sizeof n - 1);
    n[sizeof n - 1] = 0;
    if (strlen(n) == 2 && n[1] == ':')
        strcat(n, "\\");
    if (!gui_is_dir(n))
        return;
    strcpy(d->dir, n);
    fd_fill(d);
    if (d->drive) {
        char *p = strchr(d->drives, d->dir[0] >= 'a' ? d->dir[0] - 32 : d->dir[0]);
        if (p)
            gui_set_value(d->drive, (int)(p - d->drives));
    }
}

static void fd_done(struct fdlg *d, const char *path)
{
    snprintf(d->result, (size_t)d->room, "%s", path);
    gui_end_modal(d->w, GUI_OK);
}

/* A name typed (or a row chosen): a folder to go into, a path to go to, or the file. */
static void fd_take(struct fdlg *d, const char *typed)
{
    char path[300];
    if (!typed[0])
        return;
    if (!strcmp(typed, "..")) {
        strcpy(path, d->dir);
        gui_path_up(path);
        fd_go(d, path);
        return;
    }
    if (typed[1] == ':')
        snprintf(path, sizeof path, "%s", typed);
    else if (typed[0] == '\\')
        snprintf(path, sizeof path, "%c:%s", d->dir[0], typed);
    else
        gui_path_join(path, sizeof path, d->dir, typed);
    if (gui_is_dir(path)) {
        fd_go(d, path);
        gui_set_text(d->name, "");
        return;
    }
    if (!d->save) {
        struct gui_dirent e;
        if (gui_stat(path, &e) != 0) {
            gui_message(d->w, "Open", "There is no such file.", GUI_MB_OK);
            return;
        }
    }
    fd_done(d, path);
}

static struct fdlg *the_fd;

static void fd_list(struct gui_widget *w, int what)
{
    struct fdlg *d = the_fd;
    int r = gui_list_get(w);
    const char *name = gui_list_cell(w, r, 0);
    struct gui_dirent *e = gui_list_user(w, r);
    if (r < 0)
        return;
    if (what == GUI_ON_SELECT) {
        if (e && !e->dir)
            gui_set_text(d->name, name);
    } else if (what == GUI_ON_ACTIVATE) {
        fd_take(d, name);
    }
}

static void fd_button(struct gui_widget *w, int what)
{
    struct fdlg *d = the_fd;
    (void)what;
    if (w->id == GUI_OK) {
        const char *t = gui_get_text(d->name);
        if (!t[0] && gui_is_focus(d->list)) {
            fd_list(d->list, GUI_ON_ACTIVATE);
            return;
        }
        fd_take(d, t);
    } else {
        gui_end_modal(d->w, GUI_CANCEL_R);
    }
}

static void fd_name(struct gui_widget *w, int what)
{
    if (what == GUI_ON_ACTIVATE)
        fd_take(the_fd, gui_get_text(w));
}

static void fd_drive(struct gui_widget *w, int what)
{
    struct fdlg *d = the_fd;
    char dir[4];
    (void)what;
    dir[0] = d->drives[gui_value(w)], dir[1] = ':', dir[2] = '\\', dir[3] = 0;
    fd_go(d, dir);
}

static int fd_key(struct gui_window *w, struct gui_ev *e)
{
    (void)w;
    if (e->key == GUI_K_BACK && !e->mods && gui_is_focus(the_fd->list)) {
        fd_take(the_fd, "..");
        return 1;
    }
    return 0;
}

int gui_file_dialog(struct gui_window *owner, const char *title, int save, char *path, int room)
{
    struct fdlg d, *prev = the_fd;
    static const char *const heads[2] = { "Name", "Size" };
    static const int widths[2] = { 200, 100 };
    struct gui_widget *box, *row, *ok;
    int r, i;
    memset(&d, 0, sizeof d);
    d.save = save;
    d.result = path;
    d.room = room;
    the_fd = &d;
    d.w = gui_window(title, 0, 0, GUI_W_DIALOG);
    d.w->owner = owner;
    d.w->on_key = fd_key;
    box = gui_vbox(d.w->root, GUI_GAP, 10);
    d.where = gui_label(box, "");
    gui_size(d.where, 340, 0);
    d.list = gui_list(box, 2, heads, widths);
    gui_size(d.list, 340, 220);
    gui_on(d.list, fd_list, 0);
    row = gui_form(box);
    gui_on(d.name = gui_field(gui_row(row, save ? "File &name:" : "File &name:", NULL), 30), fd_name, 0);
    gui_drives(d.drives);
    {                                           /* (one drive or many: the same dialog) */
        d.drive = gui_choice(gui_row(row, "&Drive:", NULL), 4);
        for (i = 0; d.drives[i]; i++) {
            char t[3] = { d.drives[i], ':', 0 };
            gui_choice_add(d.drive, t);
        }
        gui_on(d.drive, fd_drive, 0);
    }
    row = buttons_row(box);
    (ok = gui_button(row, save ? "&Save" : "&Open", fd_button, GUI_OK))->flags |= GUI_DEFAULT;
    gui_button(row, "Cancel", fd_button, GUI_CANCEL_R)->flags |= GUI_CANCEL;
    {                                           /* where it starts: path's folder (and its name) */
        char start[260];
        snprintf(start, sizeof start, "%s", path && path[0] ? path : "C:\\");
        if (!gui_is_dir(start)) {
            const char *nm = gui_path_name(start);
            gui_set_text(d.name, nm);
            gui_path_up(start);
        }
        if (!gui_is_dir(start))
            snprintf(start, sizeof start, "C:\\");
        fd_go(&d, start);
    }
    d.w->focus = d.name;
    r = gui_modal(d.w);
    free(d.ents);
    the_fd = prev;
    return r == GUI_OK;
}
