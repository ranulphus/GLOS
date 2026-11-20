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
#include "../shared/arc.h"
#include "../shared/thumbs.h"

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
    int browser, view;                  /* view: the list's (GUI_VIEW_) */
    glos_handle watch;
    struct gui_dirent *ents;
    int n;
    struct tpic *tp;                    /* the thumbnail view: one for each of ents (thumbnails, below) */
    int tp_n;                           /* ... as many as there were entries when they were last fitted */
    char tp_dir[260];                   /* ... of that folder */
    /* The folder shown is in an archive (phase E: archives as folders; apps/shared/arc.c): */
    struct arc *arc;                    /* it, open */
    char arc_file[260], arc_in[520];    /* its file, and the folder inside ('/' between its parts; "" its top) */
    char **arc_row;                     /* each of ents' own name there (ents' keep 63 characters of it) */
    struct gui_dirent arc_was;          /* the file as it was when it was opened (changed: opened again) */
    int stale;                          /* its folder changed while a job was at work: to be listed again */
};

static struct view *views;
static struct {                         /* the files cut or copied */
    char paths[64][260];
    int n, cut;
} clip;

static struct {                         /* where folders' windows were, and how each showed its folder */
    char path[64][100];
    int x[64], y[64], w[64], h[64], view[64], n;
} places;
static struct {                         /* the browser's view and its columns, as they last were */
    int set, view, widths[3], order[3]; /* (set: to be written to FOLDERS.CFG) */
} bview = { 0, GUI_VIEW_DETAIL, { 130, 70, 120 }, { 0, 1, 2 } };

enum { C_OPEN = 1, C_NEWDIR, C_RENAME, C_DELETE, C_CLOSE, C_CUT, C_COPY, C_PASTE, C_ALL, C_BIG, C_DETAILS, C_REFRESH,
       C_UP, C_BROWSER, C_BACK, C_FORWARD, C_ADDRESS, C_PROPS, C_FIND, C_EXTRACT, C_ZIP, C_DELETE_NOW, C_RECYCLE,
       C_SMALL, C_THUMBS, C_OPENWITH };

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

/* ---- archives as folders (phase E; docs/v1/apps/files.md). An archive's file in a path is gone into as a
   folder is: C:\DOCS\PACK.ZIP\TEXT is the folder TEXT inside PACK.ZIP. What is in it is listed from the archive
   (arc_list), a file of it is opened from a copy unpacked beside FILES.EXE (ARCTEMP), and what is copied out of
   it is unpacked by a paste's job. A ZIP is changed from here too: files and folders pasted into it, a folder
   made in it, things deleted from it, and a new one made of what is chosen (Add to ZIP); the other kinds are
   only read. */

static int in_zip(const struct view *v) { return v->arc && !strcmp(arc_kind(v->arc), "ZIP"); }

/* The archive in a path: its file, and what follows as the folder inside. 1 if there is one. */
static int arc_split(const char *path, char *file, size_t froom, char *inner, size_t iroom)
{
    size_t i, n = strlen(path), o = 0;
    struct gui_dirent st;
    for (i = 3; i <= n; i++) {
        if (path[i] && path[i] != '\\')
            continue;
        if (i >= froom)
            return 0;
        memcpy(file, path, i);
        file[i] = 0;
        if (gui_is_dir(file))
            continue;
        if (!arc_name_is(file) || gui_stat(file, &st) != 0)
            return 0;
        for (i += path[i] != 0; path[i] && o + 1 < iroom; i++)
            inner[o++] = path[i] == '\\' ? '/' : path[i];
        while (o && inner[o - 1] == '/')
            o--;
        inner[o] = 0;
        return 1;
    }
    return 0;
}

static int utf8_ok(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int more = *p < 0x80 ? 0 : (*p & 0xE0) == 0xC0 ? 1 : (*p & 0xF0) == 0xE0 ? 2 : (*p & 0xF8) == 0xF0 ? 3 : -1;
        if (more < 0)
            return 0;
        for (p++; more; more--, p++)
            if ((*p & 0xC0) != 0x80)
                return 0;
    }
    return 1;
}

/* A name in an archive as text to show: UTF-8 as it is (the archives of today), DOS's bytes otherwise. */
static void arc_text(const char *name, char *out, int room)
{
    if (utf8_ok(name))
        snprintf(out, (size_t)room, "%s", name);
    else
        gui_dos_text(name, out, room);
}

static void arc_rows_free(struct view *v)
{
    int i;
    for (i = 0; v->arc_row && i < v->n; i++)
        free(v->arc_row[i]);
    free(v->arc_row);
    v->arc_row = NULL;
}

/* An archive's file has been written again by Files itself: whoever shows it reads its list anew at the next
   listing, whatever the file's size and time say (a file put in over itself leaves the archive the size it
   was, and within DOS's two seconds the time it was: but nothing in it is where it was). */
static void arc_changed(const char *file)
{
    struct view *v;
    for (v = views; v; v = v->next)
        if (v->arc && !strcmp(v->arc_file, file))
            v->arc_was.size = 0xFFFFFFFFul;
}

static void arc_leave(struct view *v)
{
    arc_rows_free(v);
    arc_close(v->arc);
    v->arc = NULL;
    v->arc_file[0] = v->arc_in[0] = 0;
}

struct arc_rows {
    struct view *v;
    int room, dirs;                     /* dirs: this pass takes the folders (the files come after them) */
};

static int arc_row_each(void *ctx, const char *name, int is_dir, const struct arc_entry *e)
{
    struct arc_rows *r = ctx;
    struct view *v = r->v;
    struct gui_dirent *d;
    if (!is_dir != !r->dirs)
        return 0;
    if (v->n == r->room) {
        int room = r->room ? r->room * 2 : 64;
        struct gui_dirent *m = realloc(v->ents, (size_t)room * sizeof *m);
        char **rows = m ? realloc(v->arc_row, (size_t)room * sizeof *rows) : NULL;
        if (m)
            v->ents = m;
        if (rows)
            v->arc_row = rows;
        if (!m || !rows)
            return 1;
        r->room = room;
    }
    if (!(v->arc_row[v->n] = strdup(name)))
        return 1;
    d = &v->ents[v->n++];
    memset(d, 0, sizeof *d);
    snprintf(d->name, sizeof d->name, "%s", name);
    arc_text(name, d->text, sizeof d->text);
    d->dir = (unsigned char)(is_dir != 0);
    if (e) {
        d->size = is_dir ? 0 : e->size;
        d->date = e->date, d->time = e->time;
    }
    return 0;
}

/* The archive's folder read into ents. If its file has changed since it was opened it is opened again; 0 if
   it is no longer there to be shown (the caller shows the folder it was in). */
static int arc_read(struct view *v)
{
    struct gui_dirent st;
    struct arc_rows r;
    arc_rows_free(v);
    free(v->ents);
    v->ents = NULL;
    v->n = 0;
    if (gui_stat(v->arc_file, &st) != 0)
        return 0;
    if (st.size != v->arc_was.size || st.date != v->arc_was.date || st.time != v->arc_was.time) {
        struct arc *a = arc_open(v->arc_file, NULL);
        if (!a)
            return 0;
        arc_close(v->arc);
        v->arc = a;
        v->arc_was = st;
        if (!arc_has_folder(a, v->arc_in))
            v->arc_in[0] = 0;
    }
    r.v = v, r.room = 0;
    for (r.dirs = 1; r.dirs >= 0; r.dirs--)
        arc_list(v->arc, v->arc_in, arc_row_each, &r);
    return 1;
}

/* A row's path as the address bar would have it: the folder shown and the row's own name. */
static int row_path(struct view *v, int row, char *out, size_t room)
{
    size_t idx = (size_t)gui_list_user(v->list, row);
    const char *name = v->arc ? v->arc_row[idx] : v->ents[idx].name;
    if (strlen(v->dir) + strlen(name) + 2 > room)
        return -1;
    gui_path_join(out, (int)room, v->dir, name);
    return 0;
}

static void arc_temp(char *out, size_t room) { snprintf(out, room, "%s\\ARCTEMP", files_home); }

/* What was unpacked to be opened is taken away when Files starts (the programs that opened it are gone then). */
static void arc_temp_clean(void)
{
    struct gui_dirent *e;
    char dir[260], p[260];
    int n, i;
    arc_temp(dir, sizeof dir);
    if ((n = gui_dir_read(dir, &e)) < 0)
        return;
    for (i = 0; i < n; i++)
        if (!e[i].dir) {
            gui_path_join(p, sizeof p, dir, e[i].name);
            gui_unlink(p);
        }
    free(e);
}

static int to_file(void *ctx, const void *p, unsigned n) { return fwrite(p, 1, n, ctx) != n; }

/* A file of the archive shown is opened: unpacked into ARCTEMP under the name DOS can have, and that opened. */
static void arc_open_row(struct view *v, int row)
{
    char dir[260], to[260], t[200];
    const char *dos;
    FILE *f;
    int i, r;
    snprintf(t, sizeof t, "%s%s%s", v->arc_in, v->arc_in[0] ? "/" : "", v->arc_row[(size_t)gui_list_user(v->list, row)]);
    if ((i = arc_find(v->arc, t)) < 0 || !(dos = arc_dos_path(v->arc, (unsigned)i)))
        return;
    arc_temp(dir, sizeof dir);
    gui_mkdir(dir);
    gui_path_join(to, sizeof to, dir, strrchr(dos, '\\') ? strrchr(dos, '\\') + 1 : dos);
    if (!(f = gui_fopen(to, "wb"))) {
        gui_message(v->win, GUI_T("Files"), GUI_T("The file couldn't be unpacked: there is nowhere to put it."), GUI_MB_OK);
        return;
    }
    r = arc_extract(v->arc, (unsigned)i, to_file, f);
    if (fclose(f) != 0 && r == ARC_OK)
        r = ARC_E_OPEN;
    if (r != ARC_OK) {
        gui_unlink(to);
        snprintf(t, sizeof t, GUI_T("The file couldn't be unpacked: %s."), GUI_T(arc_error(r)));
        gui_message(v->win, GUI_T("Files"), t, GUI_MB_OK);
        return;
    }
    say("unpacked", gui_path_name(to));
    files_open(to);
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
    if (v->arc && !chosen)
        snprintf(t + strlen(t), sizeof t - strlen(t), GUI_T(" (in a %s archive)"), arc_kind(v->arc));
    gui_set_text(v->status, t);
}

static void retitle(struct view *v);
static void way(struct view *v);
static void thumbs_fit(struct view *v);

static void fill(struct view *v)
{
    int i, keep = gui_list_get(v->list);
    char keep_name[64] = "";
    if (keep >= 0 && keep < v->n)
        snprintf(keep_name, sizeof keep_name, "%s", v->ents[keep].name);
    if (v->arc && !arc_read(v)) {               /* the archive has gone: the folder it was in */
        snprintf(v->dir, sizeof v->dir, "%s", v->arc_file);
        gui_path_up(v->dir);
        arc_leave(v);
        retitle(v);
        way(v);
    }
    if (!v->arc) {
        free(v->ents);
        v->ents = NULL;
        v->n = gui_dir_read(v->dir, &v->ents);
        if (v->n < 0)
            v->n = 0;
        for (i = 0; i < v->n; i++)              /* (a drive's Recycle folder is Files' own: recycle.c) */
            if (recycle_is(v->dir, v->ents[i].name)) {
                memmove(&v->ents[i], &v->ents[i + 1], (size_t)(v->n - i - 1) * sizeof *v->ents);
                v->n--;
                break;
            }
    }
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
        if (!e->date)
            date[0] = 0;                        /* (a folder that is only in an archive's names has none) */
        cells[0] = e->text, cells[1] = size, cells[2] = date;
        gui_list_big(v->list, gui_list_add(v->list, cells, icon(icon_for_file(e->name, e->dir), 0), (void *)(size_t)i),
                     icon(icon_for_file(e->name, e->dir), 1));
        if (keep_name[0] && !strcmp(keep_name, e->name))
            gui_list_set(v->list, i);
    }
    thumbs_fit(v);
    status_text(v);
}

/* ---- thumbnails (phase E: the thumbnail view; apps/shared/thumbs.c makes one)

   A view that shows thumbnails has one place for each of its folder's entries (tp, beside ents). Pictures' are
   made one at a time on a worker's thread, those on the screen first and then a screenful after them
   (thumb_next, four times a second and at each one's end; none while a job is at work, which has the worker
   first). Their pixels are the view's: the list's row has only their address. All the views' together are kept
   under THUMB_ROOM, but for what is on the screen: when more would be needed, those farthest from what their view
   shows go, and are made again (from thumbs.c's kept files, quickly) when they are scrolled to. A folder listed
   again keeps the thumbnails of the files that have not changed. */
#define THUMB_ROOM (3ul << 20)

struct tpic {
    unsigned *px;
    short w, h;
    char state;                         /* 0 not looked at; 1 being made; 2 made (px); 3 none (no picture) */
    char name[64];                      /* whose it is: the entry as it was then */
    unsigned long size;
    unsigned short date, time;
};

struct tmake {                          /* one being made */
    struct view *v;
    char dir[260], path[300];
    struct gui_dirent e;
    unsigned *px;
    int w, h, made;
};

static struct {
    int busy, ticking, trimmed;
    unsigned long bytes;                /* all the views' thumbnails */
} thumbs;

static int jobs_running(void);
static void thumb_next(void *arg);

static int tp_is(const struct tpic *t, const struct gui_dirent *e)
{
    return !strcmp(t->name, e->name) && t->size == e->size && t->date == e->date && t->time == e->time;
}

static void tp_free(struct tpic *t)
{
    if (t->px)
        thumbs.bytes -= (unsigned long)t->w * (unsigned long)t->h * sizeof *t->px;
    free(t->px);
    t->px = NULL;
}

static int list_row_of(struct view *v, int ent)        /* the list's row that shows ents[ent] (sorting moves them) */
{
    int r, n = gui_list_count(v->list);
    if (ent < n && (int)(size_t)gui_list_user(v->list, ent) == ent)
        return ent;
    for (r = 0; r < n; r++)
        if ((int)(size_t)gui_list_user(v->list, r) == ent)
            return r;
    return -1;
}

static void thumbs_drop(struct view *v)         /* the view's all go (it shows another view, or it is closing) */
{
    int i;
    for (i = 0; v->tp && i < v->tp_n; i++) {
        if (v->tp[i].px && v->list)
            gui_list_thumb(v->list, list_row_of(v, i), NULL, 0, 0);
        tp_free(&v->tp[i]);
    }
    free(v->tp);
    v->tp = NULL, v->tp_n = 0;
}

/* The view's folder has been listed (fill): the thumbnails' places made for what is there now, those of entries
   that are as they were taken over. (The old places are in the old entries' order, which is mostly the new:
   each is looked for from where the last was found.) */
static void thumbs_fit(struct view *v)
{
    struct tpic *old = v->tp, *tp;
    int old_n = v->tp_n, i, j, at = 0, same;
    if (v->view != GUI_VIEW_THUMBS) {
        for (i = 0; old && i < old_n; i++)      /* (the list has been filled again: no row has their addresses) */
            tp_free(&old[i]);
        free(old);
        v->tp = NULL, v->tp_n = 0;
        return;
    }
    same = old && old_n && !v->arc && !strcmp(v->tp_dir, v->dir);
    if (!(tp = calloc((size_t)v->n + 1, sizeof *tp)))
        abort();
    for (i = 0; i < v->n; i++) {
        const struct gui_dirent *e = &v->ents[i];
        snprintf(tp[i].name, sizeof tp[i].name, "%s", e->name);
        tp[i].size = e->size, tp[i].date = e->date, tp[i].time = e->time;
        tp[i].state = e->dir || v->arc || !thumb_is_picture(e->name) ? 3 : 0;
        for (j = 0; same && !tp[i].state && j < old_n; j++) {
            struct tpic *o = &old[(at + j) % old_n];
            if (tp_is(o, e)) {
                tp[i].px = o->px, tp[i].w = o->w, tp[i].h = o->h, tp[i].state = o->state;
                o->px = NULL, o->name[0] = 0;
                at = (at + j + 1) % old_n;
                break;
            }
        }
        if (tp[i].px)
            gui_list_thumb(v->list, list_row_of(v, i), tp[i].px, tp[i].w, tp[i].h);
    }
    for (j = 0; old && j < old_n; j++)
        tp_free(&old[j]);
    free(old);
    v->tp = tp, v->tp_n = v->n;
    snprintf(v->tp_dir, sizeof v->tp_dir, "%s", v->arc ? "" : v->dir);
    if (!thumbs.ticking) {
        thumbs.ticking = 1;
        gui_idle(thumb_next, NULL, 250);
    }
    thumb_next(NULL);                           /* (the first of them at once) */
}

static void thumb_work(struct gui_task *t, void *arg)
{
    struct tmake *m = arg;
    (void)t;
    if (!thumbs.trimmed) {                      /* (once a run of Files, before the first is made) */
        thumbs.trimmed = 1;
        thumb_trim();
    }
    m->px = thumb_make(m->path, &m->e, &m->w, &m->h, &m->made);
}

static void thumb_done(struct gui_window *owner, void *arg, int cancelled)
{
    struct tmake *m = arg;
    struct view *v;
    int i;
    (void)owner, (void)cancelled;
    thumbs.busy = 0;
    for (v = views; v && v != m->v; v = v->next)
        ;
    if (v && v->tp && !v->arc && !strcmp(v->tp_dir, m->dir))
        for (i = 0; i < v->tp_n; i++)
            if (v->tp[i].state == 1 && tp_is(&v->tp[i], &m->e)) {
                struct tpic *t = &v->tp[i];
                if (m->px) {
                    char line[120];
                    t->px = m->px, t->w = (short)m->w, t->h = (short)m->h, t->state = 2;
                    thumbs.bytes += (unsigned long)t->w * (unsigned long)t->h * sizeof *t->px;
                    m->px = NULL;
                    gui_list_thumb(v->list, list_row_of(v, i), t->px, t->w, t->h);
                    snprintf(line, sizeof line, "%s %dx%d %s", m->e.name, t->w, t->h, m->made ? "made" : "kept");
                    say("thumb", line);
                } else {
                    t->state = 3;
                    say("thumb none", m->e.name);
                }
                break;
            }
    free(m->px);
    free(m);
    thumb_next(NULL);
}

/* Room made for one more, which is `than` rows from what its view shows (0: on the screen): the thumbnail
   farthest from what its own view shows goes, if that is farther. 0: none is. (Only farther ones: two rows as far
   as each other would take each other's room in turn for ever.) */
static int thumb_evict(int than)
{
    struct view *v, *bv = NULL;
    int best = -1, far = than;
    for (v = views; v; v = v->next) {
        int first = 0, n, r, rows;
        if (!v->tp || v->view != GUI_VIEW_THUMBS)
            continue;
        n = gui_list_shown(v->list, &first);
        rows = gui_list_count(v->list);
        for (r = 0; r < rows; r++) {
            int ent = (int)(size_t)gui_list_user(v->list, r), d = r < first ? first - r : r >= first + n ? r - (first + n) + 1 : 0;
            if (ent < v->tp_n && v->tp[ent].state == 2 && d > far)
                far = d, best = ent, bv = v;
        }
    }
    if (!bv)
        return 0;
    gui_list_thumb(bv->list, list_row_of(bv, best), NULL, 0, 0);
    tp_free(&bv->tp[best]);
    bv->tp[best].state = 0;
    return 1;
}

static void thumb_next(void *arg)
{
    struct view *v;
    int any = 0;
    (void)arg;
    for (v = views; v; v = v->next)
        any |= v->tp != NULL;
    if (!any) {                                 /* (no view shows thumbnails any more) */
        if (thumbs.ticking)
            gui_idle(thumb_next, NULL, 0);
        thumbs.ticking = 0;
        return;
    }
    if (thumbs.busy || jobs_running())
        return;
    for (v = views; v; v = v->next) {
        int first = 0, n, r, rows;
        if (!v->tp || v->view != GUI_VIEW_THUMBS)
            continue;
        n = gui_list_shown(v->list, &first);
        rows = gui_list_count(v->list);
        for (r = first; r < rows && r < first + 2 * n; r++) {  /* (what is shown, then as much after it) */
            int ent = (int)(size_t)gui_list_user(v->list, r);
            struct tmake *m;
            if (ent >= v->n || ent >= v->tp_n || v->tp[ent].state != 0 || !tp_is(&v->tp[ent], &v->ents[ent]))
                continue;
            while (thumbs.bytes + GUI_THUMB * GUI_THUMB * sizeof(unsigned) > THUMB_ROOM)
                if (!thumb_evict(r < first + n ? 0 : r - (first + n) + 1)) {
                    if (r < first + n)
                        break;                  /* (what is on the screen is made, whatever the room) */
                    return;                     /* (no room for those after the screen's) */
                }
            if (!(m = calloc(1, sizeof *m)))
                return;
            m->v = v, m->e = v->ents[ent];
            snprintf(m->dir, sizeof m->dir, "%s", v->dir);
            gui_path_join(m->path, sizeof m->path, v->dir, v->ents[ent].name);
            v->tp[ent].state = 1;
            thumbs.busy = 1;
            if (!gui_task_start(NULL, NULL, thumb_work, m, thumb_done, 0)) {   /* (no threads: made here) */
                thumb_work(NULL, m);
                thumb_done(NULL, m, 0);
            }
            return;
        }
    }
}

static void set_view(struct view *v, int view)
{
    v->view = view;
    gui_list_view(v->list, view);
    if (v->view_menu) {
        gui_menu_check(v->view_menu, C_BIG, view == GUI_VIEW_ICONS);
        gui_menu_check(v->view_menu, C_SMALL, view == GUI_VIEW_SMALL);
        gui_menu_check(v->view_menu, C_THUMBS, view == GUI_VIEW_THUMBS);
        gui_menu_check(v->view_menu, C_DETAILS, view == GUI_VIEW_DETAIL);
    }
    if (v->browser)
        bview.view = view;
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
    {                                           /* (in an archive: what a ZIP can have done to it, and no more) */
        static const int changes[] = { C_NEWDIR, C_DELETE, C_PASTE, C_DELETE_NOW, C_RENAME, C_CUT, C_FIND, C_EXTRACT,
                                       C_ZIP };
        size_t k;
        for (k = 0; k < sizeof changes / sizeof changes[0]; k++)
            gui_command_enable(v->win, changes[k], !v->arc || (k < 4 && in_zip(v)));
    }
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
    char file[260], in[520], t[300];
    int in_arc = arc_split(dir, file, sizeof file, in, sizeof in);
    if (in_arc && (!v->arc || strcmp(v->arc_file, file))) {     /* into an archive: opened first */
        int err;
        struct arc *a = arc_open(file, &err);
        if (!a) {
            snprintf(t, sizeof t, GUI_T("%s can't be opened: %s."), gui_path_name(file), GUI_T(arc_error(err)));
            gui_message(v->win, GUI_T("Files"), t, GUI_MB_OK);
            say("archive", "refused");
            if (v->dir[0])
                return;                         /* (the folder shown stays) */
            gui_path_up(file);                  /* (a window opened for it: the folder it is in) */
            dir = file;
            in_arc = 0;
        } else {
            arc_leave(v);
            v->arc = a;
            snprintf(v->arc_file, sizeof v->arc_file, "%s", file);
            gui_stat(file, &v->arc_was);
            say("archive", arc_kind(a));
        }
    }
    if (in_arc) {                               /* about in an archive */
        if (!arc_has_folder(v->arc, in))
            in[0] = 0;
        snprintf(v->arc_in, sizeof v->arc_in, "%s", in);
        snprintf(v->dir, sizeof v->dir, "%s%s%s", file, in[0] ? "\\" : "", in);
        {
            char *q;
            for (q = v->dir + strlen(file); *q; q++)
                if (*q == '/')
                    *q = '\\';
        }
    } else {
        if (v->arc)
            arc_leave(v);
        snprintf(v->dir, sizeof v->dir, "%s", dir);
    }
    tree_follow(v);
    way(v);
    if (v->watch)
        glos_close(v->watch);
    if (v->arc) {                               /* (the archive's own folder is watched: the file may change) */
        snprintf(file, sizeof file, "%s", v->arc_file);
        gui_path_up(file);
    }
    v->watch = glos_fs_watch(v->arc ? file : v->dir);
    retitle(v);
    fill(v);
    if (gui_list_count(v->list)) {
        gui_list_set(v->list, 0);
        status_text(v);                         /* (the first chosen: as a refresh would say it) */
    }
}

static int jobs_running(void);

/* A watched folder has changed. While a job is at work its own writing changes folders a file at a time: a
   folder shown is listed again once a second at most then (job_tick), and at the job's end; a paste of a
   hundred files was a hundred listings, each longer than the last, and the job waited for every one of them
   (a second and more for two small files on a 486). */
void folder_changed(unsigned watch)
{
    struct view *v;
    for (v = views; v; v = v->next)
        if (v->watch == watch) {
            if (jobs_running()) {
                v->stale = 1;
                continue;
            }
            fill(v);
            say("refreshed", v->dir);
        }
}

/* ---- the files chosen */

static int chosen_paths(struct view *v, char (*out)[260], int room)
{
    int i, n = 0;
    for (i = 0; i < v->n && n < room; i++)
        if (gui_list_chosen(v->list, i) && row_path(v, i, out[n], 260) == 0)
            n++;
    return n;
}

/* ---- copying, moving, removing: folders with what is in them */

/* A paste's work is done in the background (phase E; docs/v1/apps/files.md, File operations): on a thread of
   the toolkit's (gui_task), so that Files' windows, the desktop among them, go on answering. What there is to
   copy is counted first (in kilobytes), for a bar and the time left. A job that has run a second has a window
   of its own, no dialog over anything: its folder may be closed and the work goes on, and Files stays until it
   is done. Cancel stops it after the piece of a file in hand; the file cut short is taken away.

   The functions down to job_work run on the work's thread: nothing of a window is touched there. What the
   window's thread shows it reads from the job's counts (job_tick). */

#define JOB_ITEMS 64
#define JOB_BUF 16384

struct job {
    struct job *next;
    struct gui_task *task;              /* (NULL: no threads to be had, done at once on the window's thread) */
    struct gui_window *win;             /* its progress: made with it, shown when it has run a second */
    struct gui_widget *line, *left, *bar;
    char *buf;
    unsigned began;
    unsigned t_work, t_count, t_end;    /* when the work's thread took it up, had counted, had done (job_done says
                                           how long each took: where a job's time goes on a slow PC) */
    int n, cut, del, shown;             /* (del: a delete: `from` alone, and the counts are of files and folders) */
    int ext;                            /* an archive unpacked whole (Extract Here): said so */
    int bin;                            /* a delete that goes to the Recycle folder (recycle.c) */
    int zip;                            /* a ZIP changed (zfile): 1: each `from` put into it under the name `to`
                                           (with what is in it, a folder); 2: the `to`s and all under them taken
                                           out of it; 3: as 1, a new ZIP (Add to ZIP): said so */
    char zfile[260];
    unsigned char arc[JOB_ITEMS];       /* the item is a thing of an archive: `from` is its path there */
    volatile int at, ok, counted;       /* the item in hand, the items done well, the count is whole */
    volatile unsigned total_kb, done_kb;
    unsigned char well[JOB_ITEMS];      /* the items done well, each */
    char from[JOB_ITEMS][260], to[JOB_ITEMS][260];
};

static struct job *jobs;
static int jobs_running(void) { return jobs != NULL; }
int folder_slow;                        /* -slowcopy (the suites'): ten seconds before each file, to cancel in */

static int job_stopped(struct job *j) { return j->task && gui_task_cancelled(j->task); }

static int copy_file(struct job *j, const char *from, const char *to)
{
    FILE *a, *b;
    size_t n;
    unsigned part = 0;
    int bad = 0, k;
    for (k = 0; folder_slow && k < 200 && !job_stopped(j); k++)
        glos_sleep(50);
    if (job_stopped(j) || !(a = gui_fopen(from, "rb")))
        return -1;
    if (!(b = gui_fopen(to, "wb"))) {
        fclose(a);
        return -1;
    }
    while (!bad && (n = fread(j->buf, 1, JOB_BUF, a)) > 0) {
        if (fwrite(j->buf, 1, n, b) != n)
            bad = 1;
        part += (unsigned)n;
        j->done_kb += part >> 10;
        part &= 1023;
        if (job_stopped(j))
            bad = 1;
    }
    fclose(a);
    if (fclose(b) != 0)
        bad = 1;
    if (bad) {
        gui_unlink(to);                         /* (a piece of a file is no copy) */
    } else {
        struct gui_dirent st;                   /* the copy is as old as what it was copied from, as DOS's COPY */
        if (gui_stat(from, &st) == 0)           /* leaves it */
            gui_set_time(to, st.date, st.time);
    }
    return bad ? -1 : 0;
}

/* A thing of an archive unpacked to `to` (phase E): a file, or a folder with all that is in it (inner "": the
   whole archive), in one pass through the archive. Each gets the name DOS can have (arc_dos_path). */
struct unpack {
    struct job *j;
    struct arc *a;
    const char *inner;
    size_t ilen, skip;                  /* skip: the item's own DOS path's length, which `to` stands for */
    const char *to;
    FILE *f;
    char path[260];
    unsigned part;
    int bad, wrote;
};

static void make_folders(const char *path, int itself)
{
    char p[260];
    size_t i;
    snprintf(p, sizeof p, "%s", path);
    for (i = 3; p[i]; i++)
        if (p[i] == '\\') {
            p[i] = 0;
            if (!gui_is_dir(p))
                gui_mkdir(p);
            p[i] = '\\';
        }
    if (itself && !gui_is_dir(p))
        gui_mkdir(p);
}

static int unpack_begin(void *ctx, unsigned i, const struct arc_entry *e)
{
    struct unpack *u = ctx;
    const char *dos, *rel;
    int k;
    if (job_stopped(u->j))
        return -1;
    if (u->ilen && (strncmp(e->name, u->inner, u->ilen) != 0 || (e->name[u->ilen] && e->name[u->ilen] != '/')))
        return 0;
    if (!(dos = arc_dos_path(u->a, i))) {
        u->bad = 1;
        return 0;
    }
    rel = strlen(dos) > u->skip ? dos + u->skip + (u->skip != 0) : "";
    if (strlen(u->to) + strlen(rel) + 2 > sizeof u->path) {
        u->bad = 1;                     /* (a path longer than DOS has) */
        return 0;
    }
    snprintf(u->path, sizeof u->path, "%s%s%s", u->to, rel[0] ? "\\" : "", rel);
    make_folders(u->path, e->dir);
    if (e->dir)
        return 0;
    for (k = 0; folder_slow && k < 200 && !job_stopped(u->j); k++)
        glos_sleep(50);
    if (job_stopped(u->j))
        return -1;
    if (e->how != ARC_OK || !(u->f = gui_fopen(u->path, "wb"))) {
        u->bad = 1;
        return 0;
    }
    u->wrote = 0;
    return 1;
}

static int unpack_data(void *ctx, const void *p, unsigned n)
{
    struct unpack *u = ctx;
    if (fwrite(p, 1, n, u->f) != n) {
        u->wrote = -1;
        return 1;
    }
    u->part += n;
    u->j->done_kb += u->part >> 10;
    u->part &= 1023;
    return job_stopped(u->j);
}

static void unpack_end(void *ctx, unsigned i, int err)
{
    struct unpack *u = ctx;
    const struct arc_entry *e = arc_at(u->a, i);
    if (!u->f)
        return;                         /* (a folder's) */
    if (fclose(u->f) != 0 || u->wrote < 0)
        err = ARC_E_OPEN;
    u->f = NULL;
    if (err != ARC_OK) {
        gui_unlink(u->path);            /* (a piece of a file is no file) */
        u->bad = 1;
    } else if (e && e->date) {
        gui_set_time(u->path, e->date, e->time);
    }
}

static int unpack_tree(struct job *j, const char *file, const char *inner, const char *to)
{
    static const struct arc_sink sink = { unpack_begin, unpack_data, unpack_end };
    struct unpack u;
    char base[260];
    int r;
    memset(&u, 0, sizeof u);
    if (!(u.a = arc_open(file, NULL)))
        return -1;
    if (arc_dos_of(u.a, inner, base, sizeof base) != 0) {
        arc_close(u.a);
        return -1;
    }
    u.j = j, u.inner = inner, u.ilen = strlen(inner), u.skip = strlen(base), u.to = to;
    if (!inner[0])
        make_folders(to, 1);            /* (the whole archive: into a folder of its own) */
    r = arc_walk(u.a, &sink, &u);
    arc_close(u.a);
    return r != ARC_OK || u.bad || job_stopped(j) ? -1 : 0;
}

/* What a thing of an archive comes to unpacked, in kilobytes (a job's count). */
static unsigned unpack_count(const char *file, const char *inner)
{
    struct arc *a = arc_open(file, NULL);
    size_t n = strlen(inner);
    unsigned i, kb = 0;
    for (i = 0; a && i < arc_count(a); i++) {
        const struct arc_entry *e = arc_at(a, i);
        if (!e->dir && (!n || (!strncmp(e->name, inner, n) && (!e->name[n] || e->name[n] == '/'))))
            kb += (unsigned)((e->size + 1023) >> 10);
    }
    arc_close(a);
    return kb;
}

static int copy_tree(struct job *j, const char *from, const char *to)
{
    struct gui_dirent *e;
    int n, i, bad = 0;
    char a[260], b[520];
    if (j->arc[j->at] && arc_split(from, a, sizeof a, b, sizeof b))
        return unpack_tree(j, a, b, to);
    if (!gui_is_dir(from))
        return copy_file(j, from, to);
    if (gui_mkdir(to) != 0 && !gui_is_dir(to))
        return -1;
    if ((n = gui_dir_read(from, &e)) < 0)
        return -1;
    for (i = 0; i < n && !job_stopped(j); i++) {
        gui_path_join(a, sizeof a, from, e[i].name);
        gui_path_join(b, 260, to, e[i].name);
        bad |= copy_tree(j, a, b) != 0;
    }
    free(e);
    return bad || job_stopped(j) ? -1 : 0;
}

static int remove_tree(struct job *j, const char *path)
{
    struct gui_dirent *e;
    int n, i, bad = 0;
    char a[260];
    if (j->del)
        j->done_kb++;
    if (!gui_is_dir(path))
        return gui_unlink(path);
    if ((n = gui_dir_read(path, &e)) < 0)
        return -1;
    for (i = 0; i < n && !(j->del && job_stopped(j)); i++) {    /* (a move's taking away is not cut short: what
                                                                   was copied whole goes) */
        gui_path_join(a, sizeof a, path, e[i].name);
        bad |= remove_tree(j, a) != 0;
    }
    free(e);
    return bad || (j->del && job_stopped(j)) ? -1 : gui_rmdir(path);
}

/* What there is to copy, in kilobytes (a file's rounded up); to delete: how many files and folders. */
static void job_count(struct job *j, const char *path, int depth)
{
    struct gui_dirent st, *e;
    int n, i;
    char p[260];
    if (job_stopped(j))
        return;
    if (!gui_is_dir(path)) {
        if (j->del)
            j->total_kb++;
        else if (gui_stat(path, &st) == 0)
            j->total_kb += (st.size + 1023) >> 10;
        return;
    }
    if (j->del)
        j->total_kb++;
    if (depth > 24 || (n = gui_dir_read(path, &e)) < 0)
        return;
    for (i = 0; i < n; i++)
        if (e[i].dir) {
            gui_path_join(p, sizeof p, path, e[i].name);
            job_count(j, p, depth + 1);
        } else {
            j->total_kb += j->del ? 1 : (e[i].size + 1023) >> 10;
        }
    free(e);
}

/* ---- a ZIP changed (phase E): on the work's thread, as the rest */

struct znames {                         /* names in the archive, sorted once all are there */
    char **name;
    unsigned char *seen;
    int n, room;
};

static int znames_add(struct znames *z, const char *name)
{
    if (z->n == z->room) {
        int room = z->room ? z->room * 2 : 64;
        char **m = realloc(z->name, (size_t)room * sizeof *m);
        if (!m)
            return -1;
        z->name = m, z->room = room;
    }
    if (!(z->name[z->n] = strdup(name)))
        return -1;
    z->n++;
    return 0;
}

static int znames_cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

static void znames_free(struct znames *z)
{
    int i;
    for (i = 0; i < z->n; i++)
        free(z->name[i]);
    free(z->name);
    free(z->seen);
}

/* The names a thing gets in the archive, itself and (a folder) all in it. */
static void zip_names(struct znames *z, const char *from, const char *name, int depth)
{
    struct gui_dirent *e;
    char a[260], b[520];
    int n, i;
    znames_add(z, name);
    if (depth > 24 || !gui_is_dir(from) || (n = gui_dir_read(from, &e)) < 0)
        return;
    for (i = 0; i < n; i++) {
        gui_path_join(a, sizeof a, from, e[i].name);
        snprintf(b, sizeof b, "%s/%s", name, e[i].name);
        zip_names(z, a, b, depth + 1);
    }
    free(e);
}

/* An entry that was there under a name now put in again: the old one (the first of the two) goes. seen: 0 for
   a name the archive had before (its first entry is the old one), anything else for the rest. */
static int zip_drop_old(void *ctx, const char *name)
{
    struct znames *z = ctx;
    char **hit = bsearch(&name, z->name, (size_t)z->n, sizeof *z->name, znames_cmp);
    if (!hit || z->seen[hit - z->name])
        return 0;
    z->seen[hit - z->name] = 1;
    return 1;
}

/* An entry that is one of the names, or under one. */
static int zip_drop_under(void *ctx, const char *name)
{
    struct job *j = ctx;
    int i;
    for (i = 0; i < j->n; i++) {
        size_t n = strlen(j->to[i]);
        if (!strncmp(name, j->to[i], n) && (!name[n] || name[n] == '/')) {
            j->well[i] = 1;
            return 1;
        }
    }
    return 0;
}

static int zip_add(struct job *j, struct arcw *w, const char *from, const char *name, int depth)
{
    struct gui_dirent st, *e;
    char a[260], b[520];
    int n, i, bad = 0;
    FILE *f;
    size_t got;
    unsigned part = 0;
    if (job_stopped(j) || gui_stat(from, &st) != 0)
        return -1;
    if (st.dir) {
        if (arcw_folder(w, name, st.date, st.time) != 0 || depth > 24 || (n = gui_dir_read(from, &e)) < 0)
            return -1;
        for (i = 0; i < n && !bad; i++) {
            gui_path_join(a, sizeof a, from, e[i].name);
            snprintf(b, sizeof b, "%s/%s", name, e[i].name);
            bad = zip_add(j, w, a, b, depth + 1) != 0;
        }
        free(e);
        return bad ? -1 : 0;
    }
    for (i = 0; folder_slow && i < 200 && !job_stopped(j); i++)
        glos_sleep(50);
    if (job_stopped(j) || !(f = gui_fopen(from, "rb")))
        return -1;
    bad = arcw_begin(w, name, st.date, st.time) != 0;
    while (!bad && (got = fread(j->buf, 1, JOB_BUF, f)) > 0) {
        bad = arcw_data(w, j->buf, (unsigned)got) != 0 || job_stopped(j);
        part += (unsigned)got;
        j->done_kb += part >> 10;
        part &= 1023;
    }
    fclose(f);
    if (!bad)
        bad = arcw_end(w) != 0;
    return bad ? -1 : 0;
}

static void zip_work(struct job *j)
{
    struct znames z;
    struct gui_dirent st;
    struct arcw *w;
    int i, bad = 0, olds = 0, there = gui_stat(j->zfile, &st) == 0;
    memset(&z, 0, sizeof z);
    if (j->zip == 2) {                          /* taken out */
        j->counted = 1;
        if (arc_zip_remove(j->zfile, zip_drop_under, j) > 0)
            for (i = 0; i < j->n; i++)
                j->ok += j->well[i];
        else
            memset(j->well, 0, sizeof j->well);
        return;
    }
    for (i = 0; i < j->n; i++) {
        job_count(j, j->from[i], 0);
        zip_names(&z, j->from[i], j->to[i], 0);
    }
    j->counted = 1;
    /* Which of those names the archive has already (a paste that replaces: asked about before): their old
       entries go once the new are in. */
    if (there && z.n && (z.seen = malloc((size_t)z.n)) != NULL) {
        struct arc *old = arc_open(j->zfile, NULL);
        qsort(z.name, (size_t)z.n, sizeof *z.name, znames_cmp);
        for (i = 0; i < z.n; i++) {
            z.seen[i] = (unsigned char)(old && arc_find(old, z.name[i]) >= 0 ? 0 : 2);
            olds += !z.seen[i];
        }
        arc_close(old);
    }
    if (!(w = arcw_open(j->zfile, 1, 2, NULL))) {
        znames_free(&z);
        return;
    }
    for (i = 0; i < j->n && !bad && !job_stopped(j); i++) {
        j->at = i;
        bad = zip_add(j, w, j->from[i], j->to[i], 0) != 0;
    }
    /* All or nothing: a ZIP half added to is the ZIP as it was (a new one: none). */
    if (arcw_close(w, !bad && !job_stopped(j)) != ARC_OK) {
        znames_free(&z);
        return;
    }
    for (i = 0; i < j->n; i++)
        j->well[i] = 1, j->ok++;
    if (olds)                                   /* (now that the new are in) */
        arc_zip_remove(j->zfile, zip_drop_old, &z);
    znames_free(&z);
    for (i = 0; j->cut && i < j->n; i++)        /* (a move: what was put in is taken away where it was) */
        remove_tree(j, j->from[i]);
}

static void job_work(struct gui_task *t, void *arg)
{
    struct job *j = arg;
    int i;
    (void)t;
    j->t_work = j->t_count = glos_ticks();
    if (j->zip) {
        zip_work(j);
        j->t_end = glos_ticks();
        return;
    }
    for (i = 0; i < j->n; i++) {
        char file[260], in[520];
        if (j->arc[i] && arc_split(j->from[i], file, sizeof file, in, sizeof in))
            j->total_kb += unpack_count(file, in);
        else
            job_count(j, j->from[i], 0);
    }
    j->counted = 1;
    j->t_count = glos_ticks();
    for (i = 0; i < j->n && !job_stopped(j); i++) {
        j->at = i;
        if (j->del ? (j->bin ? recycle_put(j->from[i]) == 0 : remove_tree(j, j->from[i]) == 0)
            : j->cut ? (gui_rename(j->from[i], j->to[i]) == 0
                        || (copy_tree(j, j->from[i], j->to[i]) == 0 && remove_tree(j, j->from[i]) == 0))
            : copy_tree(j, j->from[i], j->to[i]) == 0) {
            j->well[i] = 1;
            j->ok++;
        }
    }
    j->t_end = glos_ticks();
}

/* ---- ... and on the window's thread: its progress, and its end */

static void job_tick(void *arg)                 /* four times a second while there are jobs */
{
    struct job *j;
    static unsigned ticks;
    (void)arg;
    if (!(++ticks & 3)) {                       /* once a second: the folders that changed meanwhile */
        struct view *v;
        for (v = views; v; v = v->next)
            if (v->stale) {
                v->stale = 0;
                fill(v);
                say("refreshed", v->dir);
            }
    }
    for (j = jobs; j; j = j->next) {
        unsigned run = glos_ticks() - j->began, total = j->total_kb, done = j->done_kb;
        char t[200], name[128];
        int at = j->at;
        if (!j->shown) {
            if (run < 1000)
                continue;
            j->shown = 1;
            gui_show(j->win, 1);
            say("progress", j->del ? "deleting" : j->cut ? "moving" : "copying");
        }
        if (done > total)
            done = total;
        snprintf(t, sizeof t, GUI_T("%s (%d of %d)"), gui_dos_text(gui_path_name(j->from[at]), name, sizeof name),
                 at + 1, j->n);
        gui_set_text(j->line, t);
        if (job_stopped(j)) {
            snprintf(t, sizeof t, "%s", GUI_T("Stopping..."));
        } else if (!j->counted || !done || run < 3000) {
            snprintf(t, sizeof t, "%s", GUI_T("Working out how long it takes..."));
        } else {
            unsigned left = (unsigned)((unsigned long long)run * (total - done) / done / 1000);
            if (left < 90)
                snprintf(t, sizeof t, GUI_T("About %u seconds left"), left < 5 ? 5 : (left + 4) / 5 * 5);
            else
                snprintf(t, sizeof t, GUI_T("About %u minutes left"), (left + 30) / 60);
        }
        gui_set_text(j->left, t);
        gui_progress_set(j->bar, (int)(done >> 2), (total >> 2) ? (int)(total >> 2) : 1);
    }
}

static void job_cancel(struct gui_widget *b, int what)
{
    struct job *j = b->win->user;
    (void)what;
    if (j->task)
        gui_task_cancel(j->task);
}

static void job_close(struct gui_window *w)     /* (its close box: Cancel; it goes when the work has stopped) */
{
    struct job *j = w->user;
    if (j->task)
        gui_task_cancel(j->task);
}

static void job_done(struct gui_window *owner, void *arg, int cancelled)
{
    struct job *j = arg, **p;
    struct view *v;
    char t[80];
    (void)owner;
    for (p = &jobs; *p; p = &(*p)->next)
        if (*p == j) {
            *p = j->next;
            break;
        }
    if (!jobs)
        gui_idle(job_tick, NULL, 0);
    if (j->task) {                              /* its time, in milliseconds: until the work's thread took it up, the
                                                   counting, the work, and until this was told of its end */
        snprintf(t, sizeof t, "start=%u count=%u work=%u told=%u", j->t_work - j->began, j->t_count - j->t_work,
                 j->t_end - j->t_count, glos_ticks() - j->t_end);
        say("job", t);
    }
    if (j->del) {
        int i;
        for (i = 0; i < j->n; i++)
            if (j->well[i]) {
                if (j->bin)
                    say("recycled", j->from[i]);
                say("deleted", j->from[i]);
            }
    } else if (j->zip == 3) {
        say("zipped", j->ok == j->n ? gui_path_name(j->zfile) : "nothing");
    } else {
        snprintf(t, sizeof t, "%d %s%s", j->ok, j->ext ? "unpacked" : j->cut ? "moved" : "copied", cancelled ? " stopped" : "");
        say(j->ext ? "extracted" : "pasted", t);
    }
    if (j->zip)
        arc_changed(j->zfile);
    for (v = views; v; v = v->next) {           /* (where it wrote and what it took from, shown as they are now) */
        v->stale = 0;
        fill(v);
    }
    gui_close(j->win);
    free(j->buf);
    free(j);
}

static void job_start(struct job *j)
{
    struct gui_widget *box, *row;
    j->began = glos_ticks();
    j->win = gui_window(j->del ? GUI_T("Deleting") : j->ext ? GUI_T("Unpacking") : j->zip == 3 ? GUI_T("Packing")
                        : j->cut ? GUI_T("Moving") : GUI_T("Copying"), 320, 0, GUI_W_DIALOG);
    j->win->user = j;
    j->win->on_close = job_close;
    box = gui_vbox(j->win->root, 6, 8);
    j->line = gui_label(box, " ");
    gui_size(j->line, 300, 0);
    j->bar = gui_progress(box, 100);
    gui_size(j->bar, 300, 0);
    j->left = gui_label(box, " ");
    gui_size(j->left, 300, 0);
    row = gui_hbox(box, 6, 0);
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("Cancel"), job_cancel, 0)->flags |= GUI_CANCEL;
    j->next = jobs;
    jobs = j;
    if (!(j->task = gui_task_start(NULL, NULL, job_work, j, job_done, 0))) {
        job_work(NULL, j);                      /* (no threads to be had: done here, the window waiting) */
        job_done(NULL, j, 0);
        return;
    }
    gui_idle(job_tick, NULL, 250);
}

/* A paste onto a name that is there already (phase E) is asked about: Replace (a file: written over; a folder:
   what is copied goes into it, over its files of the same names), Skip, Keep Both (the copy gets a name that
   is free: LETTER~1.TXT), or Cancel, with which nothing of the paste is done; "for all" answers for the rest
   of this paste. All of it is asked before any of it is done (the work is a job's, below). Before this a paste
   wrote over what was there without a word. */
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
    r = gui_modal(d);                           /* (which closes it) */
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

/* A name in the ZIP shown that nothing there has: NAME~1.EXT, as free_name's on a disc. */
static int zip_free_name(struct view *v, const char *name, char *out, size_t room)
{
    const char *dot = strrchr(name, '.');
    int stem = dot && dot > name ? (int)(dot - name) : (int)strlen(name), k;
    if (!dot || dot == name)
        dot = "";
    for (k = 1; k < 100; k++) {
        int keep = k < 10 ? 6 : 5;
        snprintf(out, room, "%s%s%.*s~%d%.4s", v->arc_in, v->arc_in[0] ? "/" : "", stem < keep ? stem : keep, name, k, dot);
        if (arc_find(v->arc, out) < 0 && !arc_has_folder(v->arc, out))
            return 0;
    }
    return -1;
}

/* A paste into the ZIP shown: what was cut or copied is put into it (a job's work: zip_work), under its own
   names in the folder shown. One of that name there already is asked about as on a disc. */
static void paste_zip(struct view *v)
{
    struct job *j = NULL;
    int i, all = 0;
    char to[520], file[260], in[520];
    if (!clip.n || !(j = calloc(1, sizeof *j)) || !(j->buf = malloc(JOB_BUF))) {
        free(j);
        say("pasted", clip.cut ? "0 moved" : "0 copied");
        return;
    }
    j->cut = clip.cut;
    j->zip = 1;
    snprintf(j->zfile, sizeof j->zfile, "%s", v->arc_file);
    for (i = 0; i < clip.n && j->n < JOB_ITEMS; i++) {
        const char *name = gui_path_name(clip.paths[i]);
        int dir = gui_is_dir(clip.paths[i]), a;
        if (arc_split(clip.paths[i], file, sizeof file, in, sizeof in) || !strcasecmp(clip.paths[i], v->arc_file)) {
            gui_message(v->win, GUI_T("Files"), GUI_T("An archive's own things are unpacked to a folder first; "
                                                    "and an archive is not put into itself."), GUI_MB_OK);
            continue;
        }
        snprintf(to, sizeof to, "%s%s%s", v->arc_in, v->arc_in[0] ? "/" : "", name);
        if (strlen(to) >= sizeof j->to[0])
            continue;
        if (arc_find(v->arc, to) >= 0 || arc_has_folder(v->arc, to)) {  /* one of that name is there already */
            a = all ? all : clash_ask(v, name, dir && arc_has_folder(v->arc, to));
            if (a == CLASH_STOP) {
                free(j->buf);
                free(j);
                say("pasted", "cancelled");
                return;
            }
            if (!all && clash.for_all)
                all = a;
            if (a == CLASH_SKIP || (a == CLASH_BOTH && zip_free_name(v, name, to, sizeof j->to[0]) != 0))
                continue;
        }
        snprintf(j->from[j->n], sizeof j->from[0], "%s", clip.paths[i]);
        snprintf(j->to[j->n++], sizeof j->to[0], "%s", to);
    }
    if (clip.cut)
        clip.n = 0;
    if (!j->n) {
        say("pasted", j->cut ? "0 moved" : "0 copied");
        free(j->buf);
        free(j);
        return;
    }
    job_start(j);
}

static void paste(struct view *v)
{
    struct job *j;
    int i, all = 0;
    char to[260];
    struct gui_dirent e;
    if (v->arc) {
        paste_zip(v);
        return;
    }
    if (!clip.n || !(j = calloc(1, sizeof *j)) || !(j->buf = malloc(JOB_BUF))) {
        say("pasted", clip.cut ? "0 moved" : "0 copied");
        return;
    }
    j->cut = clip.cut;
    for (i = 0; i < clip.n && j->n < JOB_ITEMS; i++) {
        const char *name = gui_path_name(clip.paths[i]);
        int dir = gui_is_dir(clip.paths[i]), in_arc;
        char file[260], in[520], dos[260];
        if ((in_arc = arc_split(clip.paths[i], file, sizeof file, in, sizeof in)) != 0) {
            struct arc *a = arc_open(file, NULL);       /* a thing of an archive: under the name DOS can have */
            int ok = a && in[0] && arc_dos_of(a, in, dos, sizeof dos) == 0;
            dir = ok && arc_has_folder(a, in);
            arc_close(a);
            if (!ok)
                continue;
            name = strrchr(dos, '\\') ? strrchr(dos, '\\') + 1 : dos;
            j->cut = 0;                         /* (never moved out of one) */
        }
        gui_path_join(to, sizeof to, v->dir, name);
        if (!strcmp(to, clip.paths[i])) {       /* into its own folder: a copy beside it; a move: it is there */
            if (clip.cut || free_name(v->dir, name, to, sizeof to) != 0)
                continue;
        } else if (dir && inside(clip.paths[i], to)) {
            gui_message(v->win, GUI_T("Files"), GUI_T("A folder can't be put into itself."), GUI_MB_OK);
            continue;
        } else if (gui_stat(to, &e) == 0) {     /* one of that name is here already */
            int a = all ? all : clash_ask(v, name, dir && e.dir);
            if (a == CLASH_STOP) {              /* Cancel: nothing of this paste is done */
                free(j->buf);
                free(j);
                say("pasted", "cancelled");
                return;
            }
            if (!all && clash.for_all)
                all = a;
            if (a == CLASH_SKIP || (a == CLASH_BOTH && free_name(v->dir, name, to, sizeof to) != 0))
                continue;
        }
        j->arc[j->n] = (unsigned char)in_arc;
        snprintf(j->from[j->n], sizeof j->from[0], "%s", clip.paths[i]);
        snprintf(j->to[j->n++], sizeof j->to[0], "%s", to);
    }
    if (clip.cut)
        clip.n = 0;
    if (!j->n) {
        say("pasted", j->cut ? "0 moved" : "0 copied");
        free(j->buf);
        free(j);
        return;
    }
    job_start(j);                               /* (its end says what was pasted, and shows the folders anew) */
}

/* ---- the commands */

static void closing(struct gui_window *w);
static void properties(struct view *v);
static void find_files(struct view *v);
static void open_with(struct view *v, const char *path);

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
    if (v->arc && (id == C_RENAME || id == C_CUT || id == C_FIND || id == C_EXTRACT || id == C_ZIP
                   || ((id == C_NEWDIR || id == C_DELETE || id == C_DELETE_NOW || id == C_PASTE) && !in_zip(v))))
        return;                                 /* (in an archive: what a ZIP can have done to it, and no more) */
    switch (id) {
    case C_OPEN:
        if (r >= 0)
            gui_send(v->list, GUI_ON_ACTIVATE);
        break;
    case C_OPENWITH:                            /* the chosen file, by a program chosen for it */
        if (r >= 0 && !v->arc && !v->ents[(size_t)gui_list_user(v->list, r)].dir) {
            char p[260];
            gui_path_join(p, sizeof p, v->dir, v->ents[(size_t)gui_list_user(v->list, r)].name);
            open_with(v, p);
        }
        break;
    case C_EXTRACT:                             /* the chosen archive unpacked into a folder of its name, here */
        if (r < 0)
            break;
        gui_path_join(from, sizeof from, v->dir, v->ents[(size_t)gui_list_user(v->list, r)].name);
        if (!arc_name_is(from) || gui_is_dir(from)) {
            gui_message(w, GUI_T("Files"), GUI_T("That is no archive Files can unpack."), GUI_MB_OK);
            break;
        }
        {
            struct job *j;
            char *dot;
            snprintf(name, sizeof name, "%s", v->ents[(size_t)gui_list_user(v->list, r)].name);
            if ((dot = strrchr(name, '.')) != NULL)
                *dot = 0;
            gui_path_join(to, sizeof to, v->dir, name);
            {
                struct gui_dirent st;           /* one of that name is here: a free name beside it */
                if ((gui_stat(to, &st) == 0 || gui_is_dir(to)) && free_name(v->dir, name, to, sizeof to) != 0)
                    break;
            }
            if (!(j = calloc(1, sizeof *j)) || !(j->buf = malloc(JOB_BUF))) {
                free(j);
                break;
            }
            j->ext = 1;
            j->arc[0] = 1;
            snprintf(j->from[0], sizeof j->from[0], "%s", from);
            snprintf(j->to[0], sizeof j->to[0], "%s", to);
            j->n = 1;
            job_start(j);
        }
        break;
    case C_NEWDIR:
        snprintf(name, sizeof name, "NEWDIR");
        if (gui_ask(w, GUI_T("New Folder"), GUI_T("The new folder's name:"), name, sizeof name) && name[0]) {
            if (!dos_name(w, name))
                break;
            if (v->arc) {                       /* in the ZIP shown: an entry for it */
                struct arcw *z;
                snprintf(t, sizeof t, "%s%s%s", v->arc_in, v->arc_in[0] ? "/" : "", name);
                if (arc_find(v->arc, t) >= 0 || arc_has_folder(v->arc, t) || !(z = arcw_open(v->arc_file, 1, 2, NULL))
                    || (arcw_folder(z, t, 0, 0), arcw_close(z, 1)) != ARC_OK)
                    gui_message(w, GUI_T("Files"), GUI_T("The folder couldn't be made."), GUI_MB_OK);
                else
                    say("made", t);
                arc_changed(v->arc_file);
                fill(v);
                break;
            }
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
    case C_RECYCLE:
        recycle_window(w);
        for (v = views; v; v = v->next)         /* (what was put back is shown where it is again) */
            fill(v);
        return;
    case C_DELETE:                              /* to the Recycle folder, where the drive has one (recycle.c) ... */
    case C_DELETE_NOW: {                        /* ... or for good: Shift+Del, an archive's things, a floppy's */
        int bin;
        if (!(n = chosen_paths(v, paths, 64)))
            break;
        for (bin = id == C_DELETE && !v->arc, i = 0; i < n && bin; i++)
            bin = recycle_can(paths[i]);
        if (n == 1)
            snprintf(t, sizeof t, bin ? GUI_T("Move %s to the Recycle folder?") : GUI_T("Delete %s for good?"),
                     gui_dos_text(gui_path_name(paths[0]), name, sizeof name));
        else
            snprintf(t, sizeof t, bin ? GUI_T("Move these %d to the Recycle folder?") : GUI_T("Delete these %d for good?"), n);
        if (gui_message(w, GUI_T("Delete"), t, GUI_MB_YESNO) == GUI_YES) {
            struct job *j = calloc(1, sizeof *j);       /* a job's work, as a paste's: its end says what went */
            if (!j)
                break;
            j->del = 1;
            j->bin = bin;
            if (v->arc) {                       /* out of the ZIP shown: each by its name there */
                j->zip = 2;
                snprintf(j->zfile, sizeof j->zfile, "%s", v->arc_file);
            }
            for (i = 0; i < n && i < JOB_ITEMS; i++) {
                if (v->arc) {
                    char file[260];
                    if (!arc_split(paths[i], file, sizeof file, j->to[j->n], sizeof j->to[0]))
                        continue;
                }
                snprintf(j->from[j->n++], sizeof j->from[0], "%s", paths[i]);
            }
            job_start(j);
        }
        break;
    }
    case C_ZIP:                                 /* a new ZIP of what is chosen, here: named after it (one thing), or
                                                   after the folder (several) */
        if (!(n = chosen_paths(v, paths, 64)))
            break;
        {
            struct job *j;
            struct gui_dirent st;
            char *dot;
            snprintf(name, sizeof name, "%s",
                     n == 1 ? gui_path_name(paths[0]) : strlen(v->dir) > 3 ? gui_path_name(v->dir) : "FILES");
            if ((dot = strrchr(name, '.')) != NULL)
                *dot = 0;
            if (!name[0])
                snprintf(name, sizeof name, "FILES");
            snprintf(t, sizeof t, "%.8s.ZIP", name);
            gui_path_join(to, sizeof to, v->dir, t);
            if (gui_stat(to, &st) == 0 && free_name(v->dir, t, to, sizeof to) != 0)
                break;
            if (!(j = calloc(1, sizeof *j)) || !(j->buf = malloc(JOB_BUF))) {
                free(j);
                break;
            }
            j->zip = 3;
            snprintf(j->zfile, sizeof j->zfile, "%s", to);
            for (i = 0; i < n && i < JOB_ITEMS; i++) {
                snprintf(j->from[j->n], sizeof j->from[0], "%s", paths[i]);
                snprintf(j->to[j->n++], sizeof j->to[0], "%s", gui_path_name(paths[i]));
            }
            job_start(j);
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
    case C_SMALL:
    case C_THUMBS:
    case C_DETAILS:
        set_view(v, id == C_BIG ? GUI_VIEW_ICONS : id == C_SMALL ? GUI_VIEW_SMALL : id == C_THUMBS ? GUI_VIEW_THUMBS
                    : GUI_VIEW_DETAIL);
        if (v->view == GUI_VIEW_THUMBS)
            thumbs_fit(v);                      /* (their places made; the pictures follow) */
        else
            thumbs_drop(v);
        say("view", v->view == GUI_VIEW_ICONS ? "large" : v->view == GUI_VIEW_SMALL ? "small"
                    : v->view == GUI_VIEW_THUMBS ? "thumbnails" : "details");
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
    if (v->arc) {                               /* in an archive: its folders gone into, its files opened unpacked */
        if (!v->ents[(size_t)gui_list_user(w, r)].dir)
            arc_open_row(v, r);
        else if (row_path(v, r, p, sizeof p) == 0)
            go(v, p);
        return;
    }
    gui_path_join(p, sizeof p, v->dir, v->ents[(size_t)gui_list_user(w, r)].name);
    if (v->browser && (v->ents[(size_t)gui_list_user(w, r)].dir || arc_name_is(p)))
        go(v, p);                               /* (an archive is gone into as a folder is) */
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
    if (v && v->arc)
        arc_leave(v);
    if (v && !v->browser && gui_origin(w, &x, &y) == 0) {      /* (where it was: for the next time) */
        int i;
        for (i = 0; i < places.n && strcmp(places.path[i], v->dir); i++)
            ;
        if (i == places.n && places.n < 64)
            places.n++;
        if (i < 64) {
            snprintf(places.path[i], sizeof places.path[i], "%s", v->dir);
            places.x[i] = x - 1, places.y[i] = y - 20, places.w[i] = w->w, places.h[i] = w->h;
            places.view[i] = v->view;
        }
        folders_save();
    }
    if (v && v->browser) {                      /* (its view and its columns: the next browser's) */
        bview.set = 1, bview.view = v->view;
        gui_vlist_columns(v->list, bview.order, bview.widths);
        folders_save();
    }
    if (v)
        thumbs_drop(v);
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
    gui_menu_item(m, GUI_T("Open &With..."), C_OPENWITH, NULL);
    gui_menu_item(m, GUI_T("&New Folder"), C_NEWDIR, GUI_T("Ctrl+Shift+N"));
    gui_menu_item(m, GUI_T("&Rename"), C_RENAME, GUI_T("F2"));
    gui_menu_item(m, GUI_T("&Delete"), C_DELETE, GUI_T("Del"));
    gui_menu_item(m, GUI_T("Delete for &Good"), C_DELETE_NOW, GUI_T("Shift+Del"));
    gui_menu_item(m, GUI_T("&Extract Here"), C_EXTRACT, NULL);
    gui_menu_item(m, GUI_T("Add to &ZIP"), C_ZIP, NULL);
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
    gui_menu_item(m, GUI_T("&Small icons"), C_SMALL, NULL);
    gui_menu_item(m, GUI_T("&Thumbnails"), C_THUMBS, NULL);
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
        gui_menu_item(m, GUI_T("&Recycle Folder"), C_RECYCLE, NULL);
        if (!browser)
            gui_menu_item(m, GUI_T("&Browser"), C_BROWSER, NULL);
        return view;
    }
}

/* ---- properties (phase E): what the chosen file or folder is: its name, where it is, its kind, its size, when
   it was last written, its attributes; with nothing chosen, the folder shown, and its drive's room.

   What a folder holds is counted on a worker's thread while the dialog is up (the whole of a drive may be asked
   for): the line says how far the count has got, twice a second or so, and then the whole. Closing the dialog
   ends the count. (Without threads it is counted first, to a limit, as it was.) */

#define COUNT_MAX 5000

static void count(const char *dir, unsigned long *files, unsigned long long *bytes, int depth)
{
    struct gui_dirent *e = NULL;
    int n = gui_dir_read(dir, &e), i;
    char p[260];
    for (i = 0; i < n; i++) {
        if (recycle_is(dir, e[i].name))
            continue;
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

static struct {                         /* the dialog that is up: its "Holds" line, and the count for it */
    struct gui_widget *label;           /* (NULL: none is up) */
    struct gui_task *task;
    int gen;                            /* which dialog: a count's word for an older one is nobody's */
} pshow;

struct pcount {                         /* a count at work */
    char dir[260];
    int gen;
    unsigned long files, told;          /* told: files when the dialog was last told */
    unsigned long long bytes;
    unsigned at;                        /* ... and when */
};

struct pnote {                          /* its word to the window's thread */
    int gen, done;
    unsigned long files, kb;
};

static void pcount_said(void *data)
{
    struct pnote *n = data;
    if (pshow.label && n->gen == pshow.gen) {
        char t[100];
        snprintf(t, sizeof t, n->done ? GUI_T("%lu files, %lu KB") : GUI_T("%lu files, %lu KB so far"), n->files, n->kb);
        gui_set_text(pshow.label, t);
        if (n->done) {
            snprintf(t, sizeof t, "%lu files %lu KB", n->files, n->kb);
            say("counted", t);
        }
    }
    free(n);
}

static void pcount_tell(struct gui_task *t, struct pcount *c, int done)
{
    struct pnote *n = malloc(sizeof *n);
    if (!n)
        return;
    n->gen = c->gen, n->done = done, n->files = c->files, n->kb = (unsigned long)((c->bytes + 1023) / 1024);
    c->told = c->files, c->at = glos_ticks();
    gui_task_post(t, pcount_said, n);
}

static void pcount_walk(struct gui_task *t, struct pcount *c, const char *dir, int depth)
{
    struct gui_dirent *e = NULL;
    int n = gui_dir_read(dir, &e), i;
    char p[260];
    for (i = 0; i < n && !gui_task_cancelled(t); i++) {
        if (recycle_is(dir, e[i].name))
            continue;
        if (!e[i].dir) {
            c->files++;
            c->bytes += e[i].size;
        } else if (depth < 24) {
            gui_path_join(p, sizeof p, dir, e[i].name);
            pcount_walk(t, c, p, depth + 1);
        }
        if (c->files - c->told >= 64 && glos_ticks() - c->at >= 400)
            pcount_tell(t, c, 0);
    }
    if (n >= 0)
        free(e);
}

static void pcount_work(struct gui_task *t, void *arg)
{
    struct pcount *c = arg;
    c->at = glos_ticks();
    pcount_walk(t, c, c->dir, 0);
    if (!gui_task_cancelled(t))
        pcount_tell(t, c, 1);
}

static void pcount_done(struct gui_window *owner, void *arg, int cancelled)
{
    struct pcount *c = arg;
    (void)owner, (void)cancelled;
    if (c->gen == pshow.gen)
        pshow.task = NULL;                      /* (the toolkit frees it after this) */
    free(c);
}

/* The dialog's "Holds" line for the folder at path, its count begun. */
static void props_holds(struct gui_widget *form, const char *path)
{
    struct pcount *c = calloc(1, sizeof *c);
    char t[100];
    pshow.gen++;
    pshow.label = gui_label(gui_row(form, GUI_T("Holds:"), NULL), GUI_T("Counting..."));
    gui_size(pshow.label, 190, 0);              /* (room for what it will say) */
    if (c) {
        snprintf(c->dir, sizeof c->dir, "%s", path);
        c->gen = pshow.gen;
        if ((pshow.task = gui_task_start(NULL, NULL, pcount_work, c, pcount_done, 0)) != NULL)
            return;
        free(c);
    }
    {                                           /* (no threads: counted here, to a limit) */
        unsigned long files = 0;
        unsigned long long bytes = 0;
        count(path, &files, &bytes, 0);
        snprintf(t, sizeof t, files >= COUNT_MAX ? GUI_T("%lu files or more, %lu KB or more") : GUI_T("%lu files, %lu KB"), files,
                 (unsigned long)((bytes + 1023) / 1024));
        gui_set_text(pshow.label, t);
    }
}

static void props_closed(void)          /* the dialog has gone: its count ends, and has nobody to tell */
{
    pshow.label = NULL;
    if (pshow.task)
        gui_task_cancel(pshow.task);
    pshow.task = NULL;
    pshow.gen++;
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
    if (v->arc) {                               /* a thing of an archive, or the archive's folder shown */
        const struct arc_entry *e = NULL;
        unsigned long files = 0, kb = 0;
        unsigned i;
        size_t n;
        if (item) {
            ent = v->ents[(size_t)gui_list_user(v->list, r)];
            snprintf(path, sizeof path, "%s%s%s", v->arc_in, v->arc_in[0] ? "/" : "",
                     v->arc_row[(size_t)gui_list_user(v->list, r)]);
        } else {
            snprintf(path, sizeof path, "%s", v->arc_in);
            ent.dir = 1;
            snprintf(ent.text, sizeof ent.text, "%s", gui_path_name(v->dir));
        }
        if (!ent.dir && (r = arc_find(v->arc, path)) >= 0)
            e = arc_at(v->arc, (unsigned)r);
        d = gui_window(GUI_T("Properties"), 300, 0, GUI_W_DIALOG);
        d->owner = v->win;
        box = gui_vbox(d->root, 6, 8);
        form = gui_form(box);
        props_row(form, GUI_T("Name:"), ent.text);
        props_row(form, GUI_T("Where:"), item ? v->dir : v->arc_file);
        snprintf(t, sizeof t, ent.dir ? GUI_T("Folder in a %s archive") : GUI_T("File in a %s archive"), arc_kind(v->arc));
        props_row(form, GUI_T("Kind:"), t);
        if (ent.dir) {
            n = strlen(path);
            for (i = 0; i < arc_count(v->arc); i++) {
                const struct arc_entry *x = arc_at(v->arc, i);
                if (!x->dir && (!n || (!strncmp(x->name, path, n) && x->name[n] == '/')))
                    files++, kb += (x->size + 1023) >> 10;
            }
            snprintf(t, sizeof t, GUI_T("%lu files, %lu KB"), files, kb);
            props_row(form, GUI_T("Holds:"), t);
        } else if (e) {
            snprintf(t, sizeof t, GUI_T("%lu bytes"), e->size);
            props_row(form, GUI_T("Size:"), t);
            snprintf(t, sizeof t, GUI_T("%lu bytes"), e->packed);
            props_row(form, GUI_T("Packed:"), t);
            if (e->how != ARC_OK)
                props_row(form, GUI_T("Note:"), GUI_T(arc_error(e->how)));
        }
        if (item && ent.date) {
            snprintf(t, sizeof t, "%04u-%02u-%02u %02u:%02u", (ent.date >> 9) + 1980, (ent.date >> 5) & 15, ent.date & 31,
                     ent.time >> 11, (ent.time >> 5) & 63);
            props_row(form, GUI_T("Modified:"), t);
        }
        row = gui_hbox(box, 6, 0);
        gui_space(row, 0, 0);
        gui_button(row, GUI_T("OK"), props_ok, 1)->flags |= GUI_DEFAULT | GUI_CANCEL;
        say("properties", path);
        gui_modal(d);                           /* (which closes it) */
        return;
    }
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
        props_row(form, GUI_T("Kind:"), GUI_T("Folder"));
        props_holds(form, path);
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
    gui_modal(d);                               /* (which closes it) */
    props_closed();
}

/* ---- Open With (phase E): the chosen file opened by a program of the user's choosing, this once or, with the
   box ticked, whenever a file of its kind is opened ([open] .EXT = PROGRAM in GLOS.CFG, which is what Files goes
   by: files.c). The list has GLOS's own programs that take a file, the one [open] names now if it is another,
   and whatever Other finds (any program on the discs). */

static struct {
    struct gui_window *win;
    struct gui_widget *list, *always;
    char prog[8][260];                  /* each row's program: a name beside FILES.EXE, or a path */
    int n, go, keep;                    /* go: the row chosen; keep: the box was ticked */
} ow;

static void ow_add(const char *title, const char *prog, int kind)
{
    const char *cells[1] = { title };
    int i;
    for (i = 0; i < ow.n; i++)
        if (!strcasecmp(ow.prog[i], prog)) {    /* (there already: it is chosen) */
            gui_list_set(ow.list, i);
            return;
        }
    if (ow.n == 8)
        return;
    snprintf(ow.prog[ow.n], sizeof ow.prog[0], "%s", prog);
    gui_list_set(ow.list, gui_list_add(ow.list, cells, icon(kind, 0), NULL));
    ow.n++;
}

static void ow_button(struct gui_widget *b, int what)
{
    static const char *const filters[] = { GUI_N("Programs|*.EXE;*.COM;*.BAT"), NULL };
    (void)what;
    if (b->id == 1) {
        ow.go = gui_list_get(ow.list);
        ow.keep = ow.always && gui_checked(ow.always);  /* (read while the dialog is there: gui_modal closes it) */
        if (ow.go >= 0)
            gui_end_modal(ow.win, 1);
    } else if (b->id == 2) {
        gui_end_modal(ow.win, 0);
    } else if (b->id == 3) {                    /* Other: a program found on the discs */
        struct gui_file_opts o;
        char p[260];
        memset(&o, 0, sizeof o);
        o.filters = filters;
        snprintf(p, sizeof p, "%s", files_home);
        if (gui_file_dialog_ex(ow.win, GUI_T("Choose a Program"), 0, p, sizeof p, &o)) {
            char name[128];
            ow_add(gui_dos_text(gui_path_name(p), name, sizeof name), p, IC_PROGRAM);
        }
    }
}

static void ow_list_on(struct gui_widget *w, int what)
{
    if (what == GUI_ON_ACTIVATE && (ow.go = gui_list_get(w)) >= 0) {
        ow.keep = ow.always && gui_checked(ow.always);
        gui_end_modal(ow.win, 1);
    }
}

static void open_with(struct view *v, const char *path)
{
    static const struct { const char *title, *exe; int kind; } own[] = {
        { GUI_N("Notepad"), "NOTEPAD.EXE", IC_NOTEPAD }, { GUI_N("Images"), "IMAGES.EXE", IC_IMAGES },
        { GUI_N("Paint"), "PAINT.EXE", IC_PAINT } };
    struct gui_widget *box, *row;
    char t[300], name[128], ext[8] = ".", now[120], cmd[600], dir[260];
    const char *d = strrchr(gui_path_name(path), '.');
    int set = 0, r;
    size_t i;
    ow.win = gui_window(GUI_T("Open With"), 300, 0, GUI_W_DIALOG);
    ow.win->owner = v->win;
    box = gui_vbox(ow.win->root, 6, 8);
    snprintf(t, sizeof t, GUI_T("Open %s with:"), gui_dos_text(gui_path_name(path), name, sizeof name));
    gui_label(box, t);
    ow.list = gui_list(box, 1, NULL, NULL);
    gui_size(ow.list, 280, 110);
    gui_on(ow.list, ow_list_on, 0);
    ow.n = 0;
    for (i = 0; i < sizeof own / sizeof own[0]; i++)
        ow_add(GUI_T(own[i].title), own[i].exe, own[i].kind);
    gui_list_set(ow.list, 0);
    now[0] = 0;
    if (d && d[1] && strlen(d) <= 4) {          /* (the kind's own program, if [open] names one: chosen) */
        snprintf(ext + 1, sizeof ext - 1, "%s", d + 1);
        if (glos_setting_get("open", ext, now, sizeof now, &set) == 0 && set && now[0])
            ow_add(gui_path_name(now), now, IC_PROGRAM);
    }
    if (ext[1]) {
        snprintf(t, sizeof t, GUI_T("&Always open %s files with it"), ext);
        ow.always = gui_check(box, t, 0);
    } else {
        ow.always = NULL;                       /* (a name without a kind: this once only) */
    }
    row = gui_hbox(box, 6, 0);
    gui_button(row, GUI_T("O&ther..."), ow_button, 3);
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("OK"), ow_button, 1)->flags |= GUI_DEFAULT;
    gui_button(row, GUI_T("Cancel"), ow_button, 2)->flags |= GUI_CANCEL;
    gui_focus(ow.list);
    ow.go = -1, ow.keep = 0;
    r = gui_modal(ow.win);                      /* (which closes it) */
    ow.win = NULL;
    if (r != 1 || ow.go < 0 || ow.go >= ow.n)
        return;
    snprintf(dir, sizeof dir, "%s", path);
    gui_path_up(dir);
    if (strchr(ow.prog[ow.go], '\\') || strchr(ow.prog[ow.go], ':'))
        snprintf(cmd, sizeof cmd, "%s %s", ow.prog[ow.go], path);
    else
        snprintf(cmd, sizeof cmd, "%s\\%s %s", files_home, ow.prog[ow.go], path);
    snprintf(t, sizeof t, "%s %s", ow.prog[ow.go], path);
    say("open-with", t);
    files_run(cmd, dir);
    if (ow.keep) {
        char why[120] = "";
        if (glos_setting_set("open", ext, ow.prog[ow.go], NULL, why, sizeof why) != 0) {
            gui_message(v->win, GUI_T("Open With"), why[0] ? why : GUI_T("The setting couldn't be written."), GUI_MB_OK);
        } else {
            snprintf(t, sizeof t, "%s %s", ext, ow.prog[ow.go]);
            say("always", t);
        }
    }
}

/* ---- search (phase E): files and folders by name, under the folder shown: a name with DOS's wildcards (* and
   ?), or a piece of a name; and, if text is given too, only the files that have that text in them (letters' case
   apart). The folders are gone through on a worker's thread: what is found is listed as it comes, with its
   folder, and a line says where the search has got to; Find again begins another, and closing the window ends
   it. Go To shows one in its folder. */

#define FOUND_MAX 2000

static struct {
    struct gui_window *win;
    struct gui_widget *what, *text, *list, *note;
    char (*dir)[200], (*name)[64];      /* what was found, in the list's order */
    int n, room, go;
    struct gui_task *task;              /* the search at work (NULL: none) */
    int gen;                            /* which search: an older one's word is nobody's */
} fnd;

struct fsearch {                        /* a search at work */
    int gen;
    char top[260], pattern[80], text[80];       /* text: DOS's bytes, in capitals ("": names alone) */
    int ntext, found, more;             /* more: it stopped at FOUND_MAX */
    unsigned long seen;
    unsigned at;                        /* when the window was last told where it is */
    unsigned char *buf;                 /* a file's bytes, a piece at a time */
};

struct fnote {                          /* its word to the window's thread */
    int gen, kind;                      /* kind: 0 one found (dir, name); 1 where it is (dir); 2 its end */
    int found, more;
    char dir[200], name[64];
};

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

static void find_said(void *data)       /* (the window's thread) */
{
    struct fnote *n = data;
    char t[300];
    if (fnd.win && n->gen == fnd.gen) {
        if (n->kind == 0 && fnd.n < fnd.room) {
            const char *cells[2] = { n->name, n->dir };
            snprintf(fnd.dir[fnd.n], sizeof fnd.dir[0], "%s", n->dir);
            snprintf(fnd.name[fnd.n], sizeof fnd.name[0], "%s", n->name);
            gui_list_add(fnd.list, cells, NULL, (void *)(size_t)fnd.n);
            fnd.n++;
        } else if (n->kind == 1) {
            snprintf(t, sizeof t, GUI_T("%d found so far. Looking in %s"), n->found, n->dir);
            gui_set_text(fnd.note, t);
        } else if (n->kind == 2) {
            snprintf(t, sizeof t, n->more ? GUI_T("%d found, and there may be more.") : GUI_T("%d found."), fnd.n);
            gui_set_text(fnd.note, t);
            if (fnd.n && gui_list_get(fnd.list) < 0) {
                gui_list_set(fnd.list, 0);
                if (fnd.win->focus == fnd.what || fnd.win->focus == fnd.text)
                    gui_focus(fnd.list);
            }
            snprintf(t, sizeof t, "%d %s%s%s", fnd.n, gui_get_text(fnd.what)[0] ? gui_get_text(fnd.what) : "*",
                     gui_get_text(fnd.text)[0] ? " containing " : "", gui_get_text(fnd.text));
            say("found", t);
        }
    }
    free(n);
}

static void find_tell(struct gui_task *t, struct fsearch *q, int kind, const char *dir, const char *name)
{
    struct fnote *n = calloc(1, sizeof *n);
    if (!n)
        return;
    n->gen = q->gen, n->kind = kind, n->found = q->found, n->more = q->more;
    snprintf(n->dir, sizeof n->dir, "%s", dir ? dir : "");
    snprintf(n->name, sizeof n->name, "%s", name ? name : "");
    q->at = glos_ticks();
    gui_task_post(t, find_said, n);
}

/* Whether the file has the text in it: read a piece at a time, each piece begun with the end of the one before
   (the text may lie across two). Letters' case apart (ASCII's: the text is in capitals already). */
#define FIND_PIECE 16384
static int find_inside(struct gui_task *t, struct fsearch *q, const char *path)
{
    FILE *f = gui_fopen(path, "rb");
    size_t have = 0, got, i;
    int hit = 0;
    if (!f)
        return 0;
    while (!hit && !gui_task_cancelled(t) && (got = fread(q->buf + have, 1, FIND_PIECE, f)) > 0) {
        size_t n = have + got;
        for (i = have; i < n; i++)
            if (q->buf[i] >= 'a' && q->buf[i] <= 'z')
                q->buf[i] -= 32;
        for (i = 0; !hit && i + (size_t)q->ntext <= n; i++)
            hit = q->buf[i] == (unsigned char)q->text[0] && !memcmp(q->buf + i, q->text, (size_t)q->ntext);
        have = n < (size_t)q->ntext - 1 ? n : (size_t)q->ntext - 1;
        memmove(q->buf, q->buf + n - have, have);
    }
    fclose(f);
    return hit;
}

static void find_in(struct gui_task *t, struct fsearch *q, const char *dir, int depth)
{
    struct gui_dirent *e = NULL;
    int n = gui_dir_read(dir, &e), i;
    char p[260];
    for (i = 0; i < n && !q->more && !gui_task_cancelled(t); i++) {
        if (recycle_is(dir, e[i].name))
            continue;
        q->seen++;
        if (wild(q->pattern, e[i].name) && (!q->ntext || !e[i].dir)) {
            gui_path_join(p, sizeof p, dir, e[i].name);
            if (!q->ntext || find_inside(t, q, p)) {
                if (q->found == FOUND_MAX) {
                    q->more = 1;
                    break;
                }
                q->found++;
                find_tell(t, q, 0, dir, e[i].name);
            }
        }
        if (e[i].dir && depth < 24) {
            gui_path_join(p, sizeof p, dir, e[i].name);
            find_in(t, q, p, depth + 1);
        }
        if (glos_ticks() - q->at >= 500)
            find_tell(t, q, 1, dir, NULL);
    }
    if (n >= 0)
        free(e);
}

static void find_work(struct gui_task *t, void *arg)
{
    struct fsearch *q = arg;
    q->at = glos_ticks();
    if (!q->ntext || (q->buf = malloc(FIND_PIECE + sizeof q->text)) != NULL)
        find_in(t, q, q->top, 0);
    free(q->buf);
    if (!gui_task_cancelled(t))
        find_tell(t, q, 2, NULL, NULL);
}

static void find_done(struct gui_window *owner, void *arg, int cancelled)
{
    struct fsearch *q = arg;
    (void)owner, (void)cancelled;
    if (q->gen == fnd.gen)
        fnd.task = NULL;                        /* (the toolkit frees it after this) */
    free(q);
}

static void find_stop(void)             /* the search at work, if any, ends, and has nobody to tell */
{
    if (fnd.task)
        gui_task_cancel(fnd.task);
    fnd.task = NULL;
    fnd.gen++;
}

static void find_start(struct view *v)
{
    struct fsearch *q;
    char t[100], dos[80];
    const char *named = gui_get_text(fnd.what), *text = gui_get_text(fnd.text);
    int i;
    if (!named[0] && !text[0])
        return;
    find_stop();
    if (!(q = calloc(1, sizeof *q)))
        return;
    q->gen = fnd.gen;
    snprintf(q->top, sizeof q->top, "%s", v->dir);
    snprintf(q->pattern, sizeof q->pattern, "%s", named[0] ? named : "*");
    if (!strpbrk(q->pattern, "*?")) {           /* (a piece of a name: anywhere in it) */
        snprintf(t, sizeof t, "*%s*", q->pattern);
        snprintf(q->pattern, sizeof q->pattern, "%s", t);
    }
    if (text[0]) {                              /* (as the files have it: DOS's bytes) */
        if (gui_text_dos(text, dos, sizeof dos) != 0)
            snprintf(dos, sizeof dos, "%s", text);
        snprintf(q->text, sizeof q->text, "%s", dos);
        for (i = 0; q->text[i]; i++)
            if (q->text[i] >= 'a' && q->text[i] <= 'z')
                q->text[i] = (char)(q->text[i] - 32);
        q->ntext = i;
    }
    gui_list_clear(fnd.list);
    fnd.n = 0;
    gui_set_text(fnd.note, GUI_T("Looking..."));
    if (!(fnd.task = gui_task_start(NULL, NULL, find_work, q, find_done, 0))) {        /* (no threads: here) */
        gui_message(fnd.win, GUI_T("Find Files"), GUI_T("There is no memory to search with."), GUI_MB_OK);
        free(q);
    }
}

static void find_button(struct gui_widget *b, int what)
{
    struct view *v = b->win->owner ? view_of(b->win->owner) : NULL;
    (void)what;
    if (b->id == 10 && v) {                     /* Find: under the folder shown */
        find_start(v);
    } else if (b->id == 11) {                   /* Go To: the chosen one, in its folder */
        int r = gui_list_get(fnd.list);
        fnd.go = r >= 0 ? (int)(size_t)gui_list_user(fnd.list, r) : -1;
        if (fnd.go >= 0 && fnd.go < fnd.n)
            gui_end_modal(fnd.win, 1);
    } else if (b->id == 12) {
        gui_end_modal(fnd.win, 0);
    }
}

static void find_list_on(struct gui_widget *w, int what)
{
    if (what == GUI_ON_ACTIVATE) {              /* (Enter or a double click on one: Go To) */
        int r = gui_list_get(w);
        fnd.go = r >= 0 ? (int)(size_t)gui_list_user(w, r) : -1;
        if (fnd.go >= 0 && fnd.go < fnd.n)
            gui_end_modal(fnd.win, 1);
    }
}

static void find_files(struct view *v)
{
    static const char *const heads[2] = { GUI_N("Name"), GUI_N("In folder") };
    static const int widths[2] = { 110, 210 };
    struct gui_widget *box, *form, *row;
    char dir[200], name[64];
    int r, i;
    if (!fnd.dir) {                             /* (room for what is found: once) */
        fnd.dir = malloc(FOUND_MAX * sizeof *fnd.dir);
        fnd.name = malloc(FOUND_MAX * sizeof *fnd.name);
        if (!fnd.dir || !fnd.name)
            abort();
        fnd.room = FOUND_MAX;
    }
    fnd.win = gui_window(GUI_T("Find Files"), 360, 290, GUI_W_DIALOG);
    fnd.win->owner = v->win;
    box = gui_vbox(fnd.win->root, 6, 8);
    gui_grow(box, 1);
    row = gui_hbox(box, 6, 0);
    form = gui_form(row);
    gui_grow(form, 1);
    fnd.what = gui_field(gui_row(form, GUI_T("Named:"), NULL), 24);
    fnd.text = gui_field(gui_row(form, GUI_T("Containing:"), NULL), 24);
    {                                           /* (the button at its own height, beside the first field) */
        struct gui_widget *col = gui_vbox(row, 0, 0);
        gui_button(col, GUI_T("&Find"), find_button, 10)->flags |= GUI_DEFAULT;
        gui_space(col, 0, 0);
    }
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
    r = gui_modal(fnd.win);                     /* (which closes it) */
    fnd.win = NULL;
    find_stop();
    if (r != 1 || fnd.go < 0 || fnd.go >= fnd.n)
        return;
    snprintf(dir, sizeof dir, "%s", fnd.dir[fnd.go]);
    snprintf(name, sizeof name, "%s", fnd.name[fnd.go]);
    if (v->browser)                             /* the one chosen, shown in its folder */
        go(v, dir);
    else
        folder_open(dir);
    for (i = 0; v->browser && i < v->n; i++)
        if (!strcmp(v->ents[(size_t)gui_list_user(v->list, i)].name, name))
            gui_list_set(v->list, i);
    say("went", name);
}

/* The address bar: Enter goes to the folder typed (or chosen from the ones kept); what is no folder is said. */
static void address_on(struct gui_widget *w, int what)
{
    struct view *v = view_of(w->win);
    char p[260];
    size_t n;
    if (what != GUI_ON_ACTIVATE || !v)
        return;
    {
        char file[260], in[520], typed[260];
        snprintf(typed, sizeof typed, "%s", gui_get_text(w));
        for (n = strlen(typed); n > 3 && (typed[n - 1] == '\\' || typed[n - 1] == ' '); n--)
            typed[n - 1] = 0;
        snprintf(p, sizeof p, "%s", typed);
        for (n = 0; p[n]; n++)
            if (p[n] >= 'a' && p[n] <= 'z')
                p[n] = (char)(p[n] - 32);
        if (!gui_is_dir(p)) {
            if (!arc_split(p, file, sizeof file, in, sizeof in)) {
                gui_message(v->win, GUI_T("Files"), GUI_T("There is no such folder."), GUI_MB_OK);
                gui_set_text(w, v->dir);
                return;
            }
            /* Into an archive: its file as DOS has it, in capitals; what is inside it as it was typed (an
               archive's own names have small letters too). */
            snprintf(p, sizeof p, "%s%s", file, typed + strlen(file));
        }
    }
    go(v, p);
    gui_set_text(w, v->dir);                    /* (an archive that would not open: the folder shown still) */
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
    v->view = browser ? bview.view : GUI_VIEW_ICONS;
    for (i = 0; !browser && i < places.n; i++)
        if (!strcmp(places.path[i], dir))
            x = places.x[i], y = places.y[i], w = places.w[i], h = places.h[i], v->view = places.view[i];
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
        if (browser)                            /* (its columns as they were last left) */
            gui_vlist_set_columns(v->list, bview.order, bview.widths);
        gui_on(v->list, list_on, 0);
        {                                       /* the list's context menu (the right button, the Menu key) */
            struct gui_menu *c = v->context = gui_menu_new();
            gui_menu_item(c, GUI_T("&Open"), C_OPEN, NULL);
            gui_menu_item(c, GUI_T("Open &With..."), C_OPENWITH, NULL);
            gui_menu_sep(c);
            gui_menu_item(c, GUI_T("Cu&t"), C_CUT, NULL);
            gui_menu_item(c, GUI_T("&Copy"), C_COPY, NULL);
            gui_menu_item(c, GUI_T("&Paste"), C_PASTE, NULL);
            gui_menu_sep(c);
            gui_menu_item(c, GUI_T("&Delete"), C_DELETE, NULL);
            gui_menu_item(c, GUI_T("Rena&me"), C_RENAME, NULL);
            gui_menu_item(c, GUI_T("&Extract Here"), C_EXTRACT, NULL);
            gui_menu_item(c, GUI_T("Add to &ZIP"), C_ZIP, NULL);
            gui_menu_sep(c);
            gui_menu_item(c, GUI_T("&New Folder"), C_NEWDIR, NULL);
            gui_menu_item(c, GUI_T("P&roperties"), C_PROPS, NULL);
            gui_context(v->list, c);
        }
        if (browser)
            gui_list_sort(v->list, 0, by_column);   /* (a column's head sorts by it) */
    }
    v->status = gui_status(v->win);
    set_view(v, v->view);
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
            if (e[i].dir && !recycle_is(path, e[i].name)) {
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
    if (v->view != GUI_VIEW_DETAIL)             /* (the view the last browser was left in) */
        say("view kept", v->view == GUI_VIEW_ICONS ? "large" : v->view == GUI_VIEW_SMALL ? "small" : "thumbnails");
}

/* ---- where folders' windows were */

void folders_load(void)
{
    char p[300], line[400];
    FILE *f;
    arc_files(gui_fopen, gui_unlink, gui_rename);       /* (the toolkit's: they know DOS's paths on the host too) */
    arc_temp_clean();                           /* (Files is starting: what was unpacked to be opened goes) */
    thumb_home(files_home);                     /* (thumbnails are kept beside the programs) */
    snprintf(p, sizeof p, "%s\\FOLDERS.CFG", files_home);
    if (!(f = gui_fopen(p, "r")))
        return;
    while (fgets(line, sizeof line, f) && places.n < 64) {
        int n = 0, k = places.n, view = 0, m = 0;
        if (!strncmp(line, "browser ", 8)) {    /* the browser's view and columns */
            int b[7];
            if (sscanf(line + 8, "%d %d %d %d %d %d %d", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6]) == 7
                && b[0] >= GUI_VIEW_DETAIL && b[0] <= GUI_VIEW_THUMBS && b[4] + b[5] + b[6] == 3
                && b[4] >= 0 && b[5] >= 0 && b[6] >= 0 && b[4] != b[5] && b[5] != b[6] && b[4] != b[6]) {
                bview.set = 1, bview.view = b[0];
                memcpy(bview.widths, b + 1, sizeof bview.widths);
                memcpy(bview.order, b + 4, sizeof bview.order);
            }
            continue;
        }
        if (sscanf(line, "%d %d %d %d %n", &places.x[k], &places.y[k], &places.w[k], &places.h[k], &n) != 4)
            continue;
        places.view[k] = GUI_VIEW_ICONS;
        if (sscanf(line + n, "view=%d %n", &view, &m) == 1 && m && view >= GUI_VIEW_DETAIL && view <= GUI_VIEW_THUMBS)
            places.view[k] = view, n += m;      /* (how it showed its folder: after the four numbers, since phase E) */
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
    if ((!places.n && !bview.set) || !(f = gui_fopen(p, "w")))
        return;
    if (bview.set)
        fprintf(f, "browser %d %d %d %d %d %d %d\n", bview.view, bview.widths[0], bview.widths[1], bview.widths[2],
                bview.order[0], bview.order[1], bview.order[2]);
    for (i = 0; i < places.n; i++)
        fprintf(f, "%d %d %d %d view=%d %s\n", places.x[i], places.y[i], places.w[i], places.h[i], places.view[i],
                places.path[i]);
    fclose(f);
}
