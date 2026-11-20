/* The Recycle folder (files.h; phase E, docs/v1/apps/files.md): what is deleted in Files is not gone at once.
 *
 * Each fixed drive has a folder of its own, X:\RECYCLE.GLS, with what was deleted from that drive under names
 * of the folder's own (R0000012.TXT: nothing in it can clash) and INDEX, a line a thing: its name there, when it
 * was deleted, what it comes to, whether it is a folder, and where it was. A thing goes in by being renamed
 * (DOS moves a file within a drive so, at once; a folder that DOS will not move is made there and its files
 * moved one by one), and comes out the same way. Floppies and drives that are not DOS's own have none: there a
 * delete is a delete, as it is with Shift+Del, in an archive, and for every DOS program.
 *
 * A drive's folder holds a tenth of the drive at most: when more is put in, the oldest go for good.
 * [desktop] recycle = off: nothing goes in (what is in it stays until it is emptied).
 *
 * The folder is Files' own: it is not shown in its drive's listing, in the tree, or to the Start menu's search
 * (recycle_is). Its window (the desktop's Recycle icon, Go's Recycle Folder) lists every drive's. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"

#define BIN "RECYCLE.GLS"

static void say(const char *what, const char *a)
{
    printf("FILES %s %s\n", what, a);
    fflush(stdout);
}

int recycle_is(const char *dir, const char *name)
{
    return dir[0] && dir[1] == ':' && (!dir[2] || (dir[2] == '\\' && !dir[3])) && !strcasecmp(name, BIN);
}

int recycle_on(void)
{
    char v[16];
    int set = 0;
    return !(glos_setting_get("desktop", "recycle", v, sizeof v, &set) == 0 && !strcasecmp(v, "off"));
}

static void bin_dir(int drive, char *out, size_t room) { snprintf(out, room, "%c:\\" BIN, toupper(drive)); }

/* A thing of this path can go to its drive's folder: a hard disc's (C: and on; not a floppy), not the folder
   itself or anything in it, and the folder is on. */
int recycle_can(const char *path)
{
    char bin[32];
    size_t n;
    if (!recycle_on() || !path[0] || path[1] != ':' || toupper((unsigned char)path[0]) < 'C' || strlen(path) < 4)
        return 0;
    bin_dir(path[0], bin, sizeof bin);
    n = strlen(bin);
    return strncasecmp(path, bin, n) != 0 || (path[n] && path[n] != '\\');
}

/* ---- moving and removing, folders with what is in them */

static int rm_tree(const char *path)
{
    struct gui_dirent *e;
    char p[260];
    int n, i, bad = 0;
    if (!gui_is_dir(path))
        return gui_unlink(path);
    if ((n = gui_dir_read(path, &e)) < 0)
        return -1;
    for (i = 0; i < n; i++) {
        gui_path_join(p, sizeof p, path, e[i].name);
        bad |= rm_tree(p) != 0;
    }
    free(e);
    return bad ? -1 : gui_rmdir(path);
}

/* Within a drive: renamed if DOS will (a file always; a folder under some DOSes), else the folder made anew
   and what is in it moved the same way. */
static int move_tree(const char *from, const char *to, int depth)
{
    struct gui_dirent *e;
    char a[260], b[260];
    int n, i, bad = 0;
    if (gui_rename(from, to) == 0)
        return 0;
    if (depth > 24 || !gui_is_dir(from) || (gui_mkdir(to) != 0 && !gui_is_dir(to)) || (n = gui_dir_read(from, &e)) < 0)
        return -1;
    for (i = 0; i < n; i++) {
        gui_path_join(a, sizeof a, from, e[i].name);
        gui_path_join(b, sizeof b, to, e[i].name);
        bad |= move_tree(a, b, depth + 1) != 0;
    }
    free(e);
    return bad ? -1 : gui_rmdir(from);
}

static unsigned long tree_kb(const char *path, int depth)
{
    struct gui_dirent st, *e;
    char p[260];
    unsigned long kb = 0;
    int n, i;
    if (gui_stat(path, &st) != 0)
        return 0;
    if (!st.dir)
        return (st.size + 1023) >> 10;
    if (depth > 24 || (n = gui_dir_read(path, &e)) < 0)
        return 0;
    for (i = 0; i < n; i++) {
        gui_path_join(p, sizeof p, path, e[i].name);
        kb += e[i].dir ? tree_kb(p, depth + 1) : (e[i].size + 1023) >> 10;
    }
    free(e);
    return kb;
}

/* ---- a drive's INDEX: a line a thing, "NAME|DATE|TIME|KB|D or F|WHERE IT WAS" */

static int index_read(int drive, struct recycled **out)
{
    char path[64], line[400];
    struct recycled *list = NULL;
    int n = 0, room = 0;
    FILE *f;
    *out = NULL;
    bin_dir(drive, path, sizeof path);
    strcat(path, "\\INDEX");
    if (!(f = gui_fopen(path, "r")))
        return 0;
    while (fgets(line, sizeof line, f)) {
        struct recycled r;
        unsigned date, tm;
        char kind;
        int at = 0;
        memset(&r, 0, sizeof r);
        line[strcspn(line, "\r\n")] = 0;
        if (sscanf(line, "%12[^|]|%u|%u|%lu|%c|%n", r.stored, &date, &tm, &r.kb, &kind, &at) != 5 || !at || !line[at])
            continue;
        r.drive = (char)toupper(drive);
        r.date = (unsigned short)date, r.time = (unsigned short)tm;
        r.dir = kind == 'D';
        snprintf(r.was, sizeof r.was, "%s", line + at);
        if (n == room) {
            struct recycled *m = realloc(list, (size_t)(room = room * 2 + 16) * sizeof *m);
            if (!m)
                break;
            list = m;
        }
        list[n++] = r;
    }
    fclose(f);
    *out = list;
    return n;
}

static int index_write(int drive, const struct recycled *list, int n)
{
    char path[64];
    FILE *f;
    int i, bad;
    bin_dir(drive, path, sizeof path);
    strcat(path, "\\INDEX");
    if (!(f = gui_fopen(path, "w")))
        return -1;
    for (i = 0; i < n; i++)
        fprintf(f, "%s|%u|%u|%lu|%c|%s\n", list[i].stored, list[i].date, list[i].time, list[i].kb, list[i].dir ? 'D' : 'F',
                list[i].was);
    bad = ferror(f);
    return fclose(f) != 0 || bad ? -1 : 0;
}

static void stored_path(const struct recycled *r, char *out, size_t room)
{
    snprintf(out, room, "%c:\\" BIN "\\%s", r->drive, r->stored);
}

/* More than a tenth of the drive in its folder: the oldest go for good (they are the first in the list). */
static int trim(int drive, struct recycled *list, int n)
{
    struct glos_fs_drive dr;
    unsigned got = 0;
    unsigned long total = 0, cap;
    char p[64];
    int i, gone = 0;
    memset(&dr, 0, sizeof dr);
    if (glos_fs_drives((unsigned)toupper(drive), &dr, 1, &got) != 0 || !got || !dr.total_kb)
        return n;
    cap = dr.total_kb / 10;
    for (i = 0; i < n; i++)
        total += list[i].kb;
    while (gone < n - 1 && total > cap) {       /* (never the one just put in) */
        stored_path(&list[gone], p, sizeof p);
        rm_tree(p);
        say("recycle-trimmed", list[gone].was);
        total -= list[gone].kb;
        gone++;
    }
    if (gone)
        memmove(list, list + gone, (size_t)(n - gone) * sizeof *list);
    return n - gone;
}

/* Now, as DOS writes a date and a time (the kernel's clock and its zone: the C library's localtime keeps its
   answer in memory of its own, and this runs on a job's thread). */
static void stamp_now(unsigned short *date, unsigned short *tm)
{
    static const unsigned char mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    struct glos_time t;
    unsigned long local, days, secs, len;
    unsigned y = 1970, m;
    *date = (1 << 5) | 1, *tm = 0;
    memset(&t, 0, sizeof t);
    t.size = sizeof t;
    if (glos_time(&t) != 0)
        return;
    local = (unsigned long)((long)t.utc + t.offset);
    days = local / 86400ul, secs = local % 86400ul;
    for (;; y++) {
        len = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
        if (days < len)
            break;
        days -= len;
    }
    for (m = 0; m < 12; m++) {
        len = mdays[m] + (m == 1 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0) ? 1u : 0u);
        if (days < len)
            break;
        days -= len;
    }
    if (y < 1980 || y > 2107)
        return;
    *date = (unsigned short)((y - 1980) << 9 | (m + 1) << 5 | (unsigned)(days + 1));
    *tm = (unsigned short)((unsigned)(secs / 3600) << 11 | (unsigned)(secs / 60 % 60) << 5 | (unsigned)(secs % 60 / 2));
}

int recycle_put(const char *path)
{
    struct recycled *list, r;
    char bin[32], to[64];
    const char *name = gui_path_name(path), *dot;
    unsigned long seq = 0;
    int n, i, drive = toupper((unsigned char)path[0]);
    if (!recycle_can(path))
        return -1;
    bin_dir(drive, bin, sizeof bin);
    if (gui_mkdir(bin) != 0 && !gui_is_dir(bin))
        return -1;
    n = index_read(drive, &list);
    for (i = 0; i < n; i++)
        if (strtoul(list[i].stored + 1, NULL, 10) > seq)
            seq = strtoul(list[i].stored + 1, NULL, 10);
    memset(&r, 0, sizeof r);
    r.drive = (char)drive;
    r.dir = gui_is_dir(path);
    dot = r.dir ? NULL : strrchr(name, '.');
    snprintf(r.stored, sizeof r.stored, "R%07lu%.4s", (seq + 1) % 10000000ul, dot && dot != name ? dot : "");
    r.kb = tree_kb(path, 0);
    stamp_now(&r.date, &r.time);
    snprintf(r.was, sizeof r.was, "%s", path);
    stored_path(&r, to, sizeof to);
    if (move_tree(path, to, 0) != 0) {
        free(list);
        return -1;
    }
    {
        struct recycled *m = realloc(list, (size_t)(n + 1) * sizeof *m);
        if (m) {
            list = m;
            list[n++] = r;
            n = trim(drive, list, n);
            index_write(drive, list, n);
        }
    }
    free(list);
    return 0;
}

int recycle_list(struct recycled **out)
{
    char letters[27];
    struct recycled *all = NULL;
    int total = 0, i;
    gui_drives(letters);
    for (i = 0; letters[i]; i++) {
        struct recycled *list, *m;
        int n;
        if (letters[i] < 'C' || (n = index_read(letters[i], &list)) <= 0)
            continue;
        if ((m = realloc(all, (size_t)(total + n) * sizeof *m)) != NULL) {
            all = m;
            memcpy(all + total, list, (size_t)n * sizeof *list);
            total += n;
        }
        free(list);
    }
    *out = all;
    return total;
}

/* One taken out of its drive's INDEX (it has been put back, or removed). */
static void index_drop(const struct recycled *it)
{
    struct recycled *list;
    int n = index_read(it->drive, &list), i, k = 0;
    for (i = 0; i < n; i++)
        if (strcmp(list[i].stored, it->stored))
            list[k++] = list[i];
    index_write(it->drive, list, k);
    free(list);
}

/* Back where it was: its folder made again if it has gone; if one of its name is there now, beside it under
   a name that is free (where: the path it has). 0, or -1. */
int recycle_restore(const struct recycled *it, char *where, int room)
{
    char from[64], dir[260], leaf[24];
    const char *name = gui_path_name(it->was), *dot = strrchr(name, '.');
    struct gui_dirent st;
    int stem = dot && dot > name ? (int)(dot - name) : (int)strlen(name), k;
    size_t i;
    stored_path(it, from, sizeof from);
    snprintf(dir, sizeof dir, "%s", it->was);
    gui_path_up(dir);
    for (i = 3; dir[i]; i++)                    /* the folders on the way, where they are no longer */
        if (dir[i] == '\\') {
            dir[i] = 0;
            if (!gui_is_dir(dir))
                gui_mkdir(dir);
            dir[i] = '\\';
        }
    if (strlen(dir) > 3 && !gui_is_dir(dir))
        gui_mkdir(dir);
    snprintf(where, (size_t)room, "%s", it->was);
    if (!dot || dot == name)
        dot = "";
    for (k = 1; k < 100 && (gui_stat(where, &st) == 0 || gui_is_dir(where)); k++) {
        int keep = k < 10 ? 6 : 5;
        snprintf(leaf, sizeof leaf, "%.*s~%d%.4s", stem < keep ? stem : keep, name, k, dot);
        gui_path_join(where, room, dir, leaf);
    }
    if (k == 100 || move_tree(from, where, 0) != 0)
        return -1;
    index_drop(it);
    return 0;
}

int recycle_delete(const struct recycled *it)
{
    char from[64];
    stored_path(it, from, sizeof from);
    rm_tree(from);
    index_drop(it);
    return 0;
}

/* ---- its window */

static struct {
    struct gui_window *win;
    struct gui_widget *list, *note;
    struct recycled *items;
    int n;
} R;

static void fill(void)
{
    int i;
    unsigned long kb = 0;
    char t[120];
    free(R.items);
    R.n = recycle_list(&R.items);
    gui_list_clear(R.list);
    for (i = 0; i < R.n; i++) {
        const struct recycled *it = &R.items[i];
        char name[128], dir[260], shown[300], when[24], size[24];
        const char *cells[4];
        gui_dos_text(gui_path_name(it->was), name, sizeof name);
        snprintf(dir, sizeof dir, "%s", it->was);
        gui_path_up(dir);
        gui_dos_text(dir, shown, sizeof shown);
        snprintf(when, sizeof when, "%04u-%02u-%02u %02u:%02u", (it->date >> 9) + 1980, (it->date >> 5) & 15, it->date & 31,
                 it->time >> 11, (it->time >> 5) & 63);
        if (it->dir)
            snprintf(size, sizeof size, "%s", GUI_T("Folder"));
        else
            snprintf(size, sizeof size, "%lu KB", it->kb);
        cells[0] = name, cells[1] = shown, cells[2] = when, cells[3] = size;
        gui_list_add(R.list, cells, icon(icon_for_file(gui_path_name(it->was), it->dir), 0), (void *)(size_t)i);
        kb += it->kb;
    }
    if (R.n)
        gui_list_set(R.list, 0);
    snprintf(t, sizeof t, R.n ? GUI_T("%d deleted, %lu KB in all.") : GUI_T("Nothing is in the Recycle folder."), R.n, kb);
    gui_set_text(R.note, t);
}

static void button(struct gui_widget *b, int event)
{
    int r = gui_list_get(R.list), i, what = b->id;
    const struct recycled *it = r >= 0 ? &R.items[(size_t)gui_list_user(R.list, r)] : NULL;
    char where[260], t[340], name[128];
    (void)event;
    if (what == 4) {
        gui_end_modal(b->win, 0);
        return;
    }
    if (what == 1 && it) {                      /* Restore: back where it was */
        if (recycle_restore(it, where, sizeof where) != 0) {
            gui_message(b->win, GUI_T("Recycle"), GUI_T("It couldn't be put back where it was."), GUI_MB_OK);
        } else {
            say("restored", where);
            if (strcasecmp(where, it->was)) {
                snprintf(t, sizeof t, GUI_T("One of its name is there now: it is back as %s."),
                         gui_dos_text(gui_path_name(where), name, sizeof name));
                gui_message(b->win, GUI_T("Recycle"), t, GUI_MB_OK);
            }
        }
    } else if (what == 2 && it) {               /* Delete: this one for good */
        snprintf(t, sizeof t, GUI_T("Delete %s for good?"), gui_dos_text(gui_path_name(it->was), name, sizeof name));
        if (gui_message(b->win, GUI_T("Recycle"), t, GUI_MB_YESNO) != GUI_YES)
            return;
        recycle_delete(it);
        say("recycle-deleted", it->was);
    } else if (what == 3 && R.n) {              /* Empty: all of them */
        snprintf(t, sizeof t, GUI_T("Delete all %d for good?"), R.n);
        if (gui_message(b->win, GUI_T("Recycle"), t, GUI_MB_YESNO) != GUI_YES)
            return;
        for (i = 0; i < R.n; i++)
            recycle_delete(&R.items[i]);
        say("recycle-emptied", "");
    } else {
        return;
    }
    fill();
    gui_focus(R.list);
}

void recycle_window(struct gui_window *owner)
{
    static const char *const heads[4] = { GUI_N("Name"), GUI_N("Was in"), GUI_N("Deleted"), GUI_N("Size") };
    static const int widths[4] = { 100, 150, 110, 60 };
    struct gui_widget *box, *row;
    if (R.win)
        return;
    R.win = gui_window(GUI_T("Recycle Folder"), 460, 260, GUI_W_DIALOG);
    R.win->owner = owner;
    box = gui_vbox(R.win->root, 6, 8);
    gui_grow(box, 1);
    R.list = gui_list(box, 4, heads, widths);
    gui_grow(R.list, 1);
    R.note = gui_label(box, " ");
    row = gui_hbox(box, 6, 0);
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("&Restore"), button, 1)->flags |= GUI_DEFAULT;
    gui_button(row, GUI_T("&Delete"), button, 2);
    gui_button(row, GUI_T("&Empty"), button, 3);
    gui_button(row, GUI_T("Close"), button, 4)->flags |= GUI_CANCEL;
    fill();
    gui_focus(R.list);
    say("recycle", "open");
    gui_modal(R.win);                           /* (which closes it) */
    R.win = NULL;
    free(R.items);
    R.items = NULL;
    R.n = 0;
}
