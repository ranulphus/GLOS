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

int gui_clip_set(const char *text, size_t n) { return glos_clip_put(GLOS_CLIP_TEXT, text, (unsigned)n) == 0 ? 0 : -1; }

char *gui_clip_get(size_t *n)
{
    unsigned len = 0, got = 0;
    char *t;
    int r = glos_clip_get(GLOS_CLIP_TEXT, NULL, 0, &len);       /* (its length: all of it "cut") */
    if ((r != 0 && r != GLOS_E_ROOM) || !len)
        return NULL;
    if (!(t = malloc(len + 1)))
        return NULL;
    r = glos_clip_get(GLOS_CLIP_TEXT, t, len, &got);
    if (r != 0 && r != GLOS_E_ROOM) {
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
    return gui_message_check(owner, title, text, buttons, NULL, NULL);
}

static void check_told(struct gui_widget *w, int what)
{
    if (what == GUI_ON_CHANGE && w->user)
        *(int *)w->user = gui_checked(w);
}

/* (toolkit 2) The same with a check box under the text ("Don't ask again", "Do this for all"), its state in and
   out through *checked: the app keeps it. */
int gui_message_check(struct gui_window *owner, const char *title, const char *text, int buttons, const char *check,
                      int *checked)
{
    struct gui_window *w = gui_window(title, 0, 0, GUI_W_DIALOG);
    struct gui_widget *box = gui_vbox(w->root, GUI_GAP, 12), *msg = gui_vbox(box, 2, 0), *row, *b, *cb = NULL;
    w->owner = owner;
    wrapped(msg, text, 360);
    if (check)
        cb = gui_check(box, check, checked && *checked);
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
        int r;
        if (cb && checked) {                    /* (the box tells *checked as it changes: gui_modal frees it) */
            cb->user = checked;
            gui_on(cb, check_told, 0);
        }
        r = gui_modal(w);
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

#define PATH_ROOM 160

static void icon_draw(struct gui_widget *w, struct gfx_surface *s)
{
    if (w->user)
        gui_draw_icon(s, w->r.x, w->r.y, w->user, GUI_BIG_W);
}

static void icon_measure(struct gui_widget *w, int *pw, int *ph) { (void)w, *pw = *ph = GUI_BIG_W; }

static const struct gui_class icon_class = { "icon", 0, icon_draw, NULL, icon_measure, NULL, NULL };

/* ---- About (toolkit 2, step C6): the app's icon, name and version, its copyright and licence, and what it uses,
   the toolkit's own fonts and icons added for every app (their licences ask for the credit). */
static const char *const toolkit_credits[] = {
    "DejaVu fonts 2.37: the text (Bitstream Vera licence; DejaVu's changes in the public domain)",
    "FatCow Farm-Fresh Web Icons (www.fatcow.com/free-icons): the colour icons, CC BY 3.0, reduced to GLOS's "
    "216 colours",
    "Font Awesome Free by Fonticons, Inc. (fontawesome.com): the toolbar and menu glyphs, CC BY 4.0, drawn as "
    "16-pixel masks",
    NULL
};

void gui_about(struct gui_window *owner, const struct gui_about *a)
{
    char title[96];
    struct gui_window *w;
    struct gui_widget *box, *top, *id, *l, *list, *row, *b;
    int i;
    snprintf(title, sizeof title, "About %s", a->name ? a->name : "this program");
    w = gui_window(title, 0, 0, GUI_W_DIALOG);
    w->owner = owner;
    box = gui_vbox(w->root, GUI_GAP, 12);
    top = gui_hbox(box, 10, 0);
    if (a->icon)
        gui_new(top, &icon_class, sizeof(struct gui_widget))->user = (void *)a->icon;
    id = gui_vbox(top, 2, 0);
    l = gui_label(id, a->name ? a->name : "");
    gui_label_style(l, gui_bold, GFX_NONE, -1);
    if (a->version)
        gui_label(id, a->version);
    if (a->copyright)
        wrapped(id, a->copyright, 300);
    if (a->licence)
        wrapped(box, a->licence, 360);
    gui_label(box, "It uses:");
    list = gui_vbox(box, 5, 0);                 /* (a credit a block, its lines together) */
    for (i = 0; a->credits && a->credits[i]; i++)
        wrapped(gui_vbox(list, 0, 0), a->credits[i], 360);
    for (i = 0; toolkit_credits[i]; i++)
        wrapped(gui_vbox(list, 0, 0), toolkit_credits[i], 360);
    row = buttons_row(box);
    (b = gui_button(row, "OK", answer, GUI_OK))->flags |= GUI_DEFAULT | GUI_CANCEL;
    w->focus = b;
    gui_modal(w);
}

/* ---- Folder (toolkit 2, step C6): a tree of the drives and their folders, the path typed in a combo box (Enter
   takes it), a New folder button. The tree's ids are places in the dialog's table of paths. */
#define FOLDERS 512

static struct fold {
    char *path[FOLDERS];                /* id - 1 */
    int n;
    struct gui_widget *tree, *at;
    char text[96];
    char *out;                          /* the caller's path, filled before the dialog goes */
    int room;
} FD;

static void fold_end(struct gui_window *w)      /* OK: the path taken, then the dialog's end */
{
    snprintf(FD.out, (size_t)FD.room, "%s", gui_get_text(FD.at));
    gui_end_modal(w, GUI_OK);
}

static void fold_ok(struct gui_widget *w, int what)
{
    (void)what;
    fold_end(w->win);
}

static unsigned long fold_id(const char *path)  /* the path's id, added if new (0: the table is full) */
{
    int i;
    for (i = 0; i < FD.n; i++)
        if (!strcmp(FD.path[i], path))
            return (unsigned long)i + 1;
    if (FD.n == FOLDERS || !(FD.path[FD.n] = strdup(path)))
        return 0;
    return (unsigned long)++FD.n;
}

static int fold_children(void *ctx, unsigned long id, unsigned long *ids, int room, int from)
{
    char drives[27], path[PATH_ROOM];
    struct gui_dirent *e = NULL;
    int n, i, k = 0, all = 0;
    (void)ctx;
    if (!id) {                                  /* the top: the drives */
        n = gui_drives(drives);
        for (i = 0; i < n; i++) {
            snprintf(path, sizeof path, "%c:\\", drives[i]);
            if (all >= from && k < room)
                ids[k++] = fold_id(path);
            all++;
        }
        return all;
    }
    if ((n = gui_dir_read(FD.path[id - 1], &e)) < 0)
        return 0;
    for (i = 0; i < n; i++) {
        if (!e[i].dir)
            continue;
        gui_path_join(path, sizeof path, FD.path[id - 1], e[i].name);
        if (all >= from && k < room)
            ids[k++] = fold_id(path);
        all++;
    }
    free(e);
    return all;
}

static void fold_node(void *ctx, unsigned long id, struct gui_tnode *out)
{
    const char *p = id && id <= (unsigned long)FD.n ? FD.path[id - 1] : "";
    (void)ctx;
    if (strlen(p) == 3 && p[1] == ':') {        /* a drive */
        snprintf(FD.text, sizeof FD.text, "%c:", p[0]);
        out->icon = gui_icon(p[0] == 'A' || p[0] == 'B' ? "drive-floppy" : "drive", 0);
    } else {
        gui_dos_text(gui_path_name(p), FD.text, sizeof FD.text);
        out->icon = gui_icon("folder", 0);
    }
    out->text = FD.text;
    out->may = 1;
}

static void fold_tree_on(struct gui_widget *w, int what)
{
    unsigned long id = gui_vtree_get(w);
    if (what == GUI_ON_SELECT && id && id <= (unsigned long)FD.n)
        gui_set_text(FD.at, FD.path[id - 1]);
    else if (what == GUI_ON_ACTIVATE && id)
        fold_end(w->win);
}

static void fold_at_on(struct gui_widget *w, int what)
{
    if (what == GUI_ON_ACTIVATE && gui_is_dir(gui_get_text(w)))
        fold_end(w->win);
}

/* The tree opened down to the path, and its node the current one. */
static void fold_show(const char *path)
{
    char part[PATH_ROOM];
    const char *p;
    unsigned long id;
    if (strlen(path) < 3 || path[1] != ':')
        return;
    snprintf(part, sizeof part, "%c:\\", path[0]);
    id = fold_id(part);
    for (p = path + 3;; ) {
        const char *q = strchr(p, '\\');
        size_t n = q ? (size_t)(q - path) : strlen(path);
        gui_vtree_open(FD.tree, id, 1);
        if (!*p)
            break;
        if (n >= sizeof part)
            return;
        memcpy(part, path, n);
        part[n] = 0;
        id = fold_id(part);
        if (!q)
            break;
        p = q + 1;
    }
    gui_vtree_set(FD.tree, id);
}

static void fold_new(struct gui_widget *w, int what)
{
    char name[64], path[PATH_ROOM], dos[64];
    unsigned long id = gui_vtree_get(FD.tree);
    (void)what;
    if (!id || id > (unsigned long)FD.n)
        return;
    name[0] = 0;
    if (gui_ask(w->win, "New folder", "&Name of the new folder:", name, sizeof name) != 1 || !name[0])
        return;
    if (gui_text_dos(name, dos, sizeof dos) != 0)
        return;
    gui_path_join(path, sizeof path, FD.path[id - 1], dos);
    if (gui_mkdir(path) != 0) {
        gui_message(w->win, "New folder", "The folder couldn't be made there.", GUI_MB_OK);
        return;
    }
    gui_vtree_changed(FD.tree, id);
    fold_show(path);
    gui_set_text(FD.at, path);
}

int gui_folder_dialog(struct gui_window *owner, const char *title, char *path, int room)
{
    static const struct gui_tmodel model = { fold_children, fold_node, NULL };
    struct gui_window *w = gui_window(title, 360, 320, GUI_W_DIALOG);
    struct gui_widget *box = gui_vbox(w->root, GUI_GAP, 10), *row, *b;
    int r, i;
    w->owner = owner;
    memset(&FD, 0, sizeof FD);
    FD.tree = gui_vtree(box, &model);
    gui_grow(FD.tree, 1);
    gui_size(FD.tree, 320, 200);
    gui_on(FD.tree, fold_tree_on, 0);
    row = gui_hbox(box, GUI_GAP, 0);
    gui_label(row, "&Folder:");
    FD.at = gui_combo(row, 30);
    gui_grow(FD.at, 1);
    gui_on(FD.at, fold_at_on, 0);
    gui_set_text(FD.at, path);
    row = buttons_row(box);
    gui_button(row, "&New folder", fold_new, 0);
    (b = gui_button(row, "OK", fold_ok, GUI_OK))->flags |= GUI_DEFAULT;
    gui_button(row, "Cancel", answer, GUI_CANCEL_R)->flags |= GUI_CANCEL;
    FD.out = path, FD.room = room;
    fold_show(path);
    w->focus = FD.tree;
    r = gui_modal(w);
    for (i = 0; i < FD.n; i++)
        free(FD.path[i]);
    FD.n = 0;
    return r == GUI_OK && gui_is_dir(path);
}
