/* Backups of a folder to another computer: backup.h says how they lie there. The folder is walked (findfirst
 * under DOS: a stat a file costs a DOS call more, docs/v1/phase-f.md), each file's size and DOS date and time
 * kept; a run of what changed reads the newest complete set's manifest and copies only the files whose size,
 * date or time differ from it (or that it lacks); the manifest goes last. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <utime.h>
#ifdef __DJGPP__
#include <dir.h>
#else
#include <dirent.h>
#endif

#include "backup.h"

#define PATH_ROOM 260

struct bk_file {
    char *path;                         /* within the folder, '/' between its parts */
    unsigned long size;
    unsigned date, time;                /* DOS's */
    int dir;                            /* (an empty folder: kept so that a restore makes it) */
    const char *set;                    /* (a backup) the set that holds it; (a restore) the same, from the manifest */
};

struct bk_list {
    struct bk_file *f;
    int n, room;
    char *sets;                         /* (a manifest read) the set names it gave, packed */
};

static void list_free(struct bk_list *l)
{
    int i;
    for (i = 0; i < l->n; i++)
        free(l->f[i].path);
    free(l->f), free(l->sets);
    memset(l, 0, sizeof *l);
}

static struct bk_file *list_add(struct bk_list *l, const char *path)
{
    struct bk_file *f;
    if (l->n == l->room) {
        int room = l->room ? l->room * 2 : 128;
        struct bk_file *m = realloc(l->f, (size_t)room * sizeof *m);
        if (!m)
            return NULL;
        l->f = m, l->room = room;
    }
    f = &l->f[l->n];
    memset(f, 0, sizeof *f);
    if (!(f->path = malloc(strlen(path) + 1)))
        return NULL;
    strcpy(f->path, path);
    l->n++;
    return f;
}

void bk_set_name(char *out, unsigned room, int year, int month, int day, int hour, int min, int sec)
{
    snprintf(out, room, "%04d%02d%02d-%02d%02d%02d", year, month, day, hour, min, sec);
}

static int is_set_name(const char *s)           /* YYYYMMDD-HHMMSS, perhaps -N after it */
{
    int i;
    for (i = 0; i < 15; i++)
        if (i == 8 ? s[i] != '-' : s[i] < '0' || s[i] > '9')
            return 0;
    if (!s[15])
        return 1;
    if (s[15] != '-' || !s[16])
        return 0;
    for (i = 16; s[i]; i++)
        if (s[i] < '0' || s[i] > '9' || i > 18)
            return 0;
    return 1;
}

static void join(char *out, unsigned room, const char *a, const char *b)
{
    size_t n = strlen(a);
    snprintf(out, room, "%s%s%s", a, n && a[n - 1] != '/' && a[n - 1] != '\\' && b[0] ? "/" : "", b);
}

static void join3(char *out, unsigned room, const char *a, const char *b, const char *c)     /* a/b/c */
{
    char ab[PATH_ROOM + 40];
    join(ab, sizeof ab, a, b);
    join(out, room, ab, c);
}

static void local_join(char *out, unsigned room, const char *dir, const char *rel)    /* rel's '/' made '\' */
{
    size_t n = strlen(dir), k;
    snprintf(out, room, "%s%s%s", dir, n && dir[n - 1] != '\\' && dir[n - 1] != '/' ? "\\" : "", rel);
#ifdef __DJGPP__
    for (k = n; out[k]; k++)
        if (out[k] == '/')
            out[k] = '\\';
#else
    for (k = 0; out[k]; k++)                    /* (the host's tests: its own separator) */
        if (out[k] == '\\')
            out[k] = '/';
#endif
}

/* ---- the folder here, walked */

static int walk(struct bk_list *l, const char *top, const char *rel, int depth)
{
    char dir[PATH_ROOM], sub[PATH_ROOM];
    int any = 0;
    if (depth > 16)
        return 0;
    local_join(dir, sizeof dir, top, rel);
#ifdef __DJGPP__
    {
        struct ffblk ff;
        char pat[PATH_ROOM];
        int done;
        local_join(pat, sizeof pat, dir, "*.*");
        for (done = findfirst(pat, &ff, FA_DIREC | FA_RDONLY | FA_HIDDEN | FA_SYSTEM | FA_ARCH); !done; done = findnext(&ff)) {
            struct bk_file *f;
            if (!strcmp(ff.ff_name, ".") || !strcmp(ff.ff_name, ".."))
                continue;
            join(sub, sizeof sub, rel, ff.ff_name);
            any = 1;
            if (ff.ff_attrib & FA_DIREC) {
                if (walk(l, top, sub, depth + 1) == 0 && (f = list_add(l, sub)) != NULL)
                    f->dir = 1;                 /* (an empty folder) */
                continue;
            }
            if (!(f = list_add(l, sub)))
                return -1;
            f->size = (unsigned long)ff.ff_fsize, f->date = ff.ff_fdate, f->time = ff.ff_ftime;
        }
    }
#else
    {
        DIR *d = opendir(dir);
        struct dirent *e;
        if (!d)
            return depth ? 0 : -1;
        while ((e = readdir(d)) != NULL) {
            struct stat st;
            struct bk_file *f;
            char full[PATH_ROOM];
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
                continue;
            join(sub, sizeof sub, rel, e->d_name);
            local_join(full, sizeof full, top, sub);
            if (stat(full, &st) != 0)
                continue;
            any = 1;
            if (S_ISDIR(st.st_mode)) {
                if (walk(l, top, sub, depth + 1) == 0 && (f = list_add(l, sub)) != NULL)
                    f->dir = 1;
                continue;
            }
            if (!(f = list_add(l, sub))) {
                closedir(d);
                return -1;
            }
            {
                struct tm *t = localtime(&st.st_mtime);
                f->size = (unsigned long)st.st_size;
                f->date = (unsigned)(((t->tm_year - 80) << 9) | ((t->tm_mon + 1) << 5) | t->tm_mday);
                f->time = (unsigned)((t->tm_hour << 11) | (t->tm_min << 5) | (t->tm_sec / 2));
            }
        }
        closedir(d);
    }
#endif
    return any;
}

static int by_path(const void *a, const void *b)
{
    return strcmp(((const struct bk_file *)a)->path, ((const struct bk_file *)b)->path);
}

/* ---- manifests */

static int manifest_write(const char *file, const char *folder, const char *set, int changes, const struct bk_list *l)
{
    FILE *f = fopen(file, "w");
    int i;
    if (!f)
        return -1;
    fprintf(f, "GLOS-BACKUP 1\nfrom\t%s\nset\t%s\nkind\t%s\n", folder, set, changes ? "changes" : "full");
    for (i = 0; i < l->n; i++)
        if (l->f[i].dir)
            fprintf(f, "D\t%s\n", l->f[i].path);
        else if (l->f[i].set)
            fprintf(f, "F\t%s\t%lu\t%u\t%u\t%s\n", l->f[i].set, l->f[i].size, l->f[i].date, l->f[i].time, l->f[i].path);
    return fclose(f) == 0 ? 0 : -1;
}

/* A manifest read into l (sorted by path): 0, or -1 (not one). */
static int manifest_read(const char *file, struct bk_list *l)
{
    FILE *f = fopen(file, "r");
    char line[PATH_ROOM + 120];
    size_t sets_n = 0, sets_room = 0;
    int ok = 0;
    if (!f)
        return -1;
    while (fgets(line, sizeof line, f)) {
        char *e = line + strcspn(line, "\r\n"), *p[6];
        int k = 0;
        *e = 0;
        if (!ok) {
            ok = !strcmp(line, "GLOS-BACKUP 1");
            if (!ok)
                break;
            continue;
        }
        p[k++] = line;
        for (e = line; *e && k < 6; e++)
            if (*e == '\t')
                *e = 0, p[k++] = e + 1;
        if (!strcmp(p[0], "D") && k == 2) {
            struct bk_file *d = list_add(l, p[1]);
            if (!d)
                break;
            d->dir = 1;
        } else if (!strcmp(p[0], "F") && k == 6 && is_set_name(p[1])) {
            struct bk_file *x = list_add(l, p[5]);
            size_t at;
            if (!x)
                break;
            x->size = strtoul(p[2], NULL, 10), x->date = (unsigned)atoi(p[3]), x->time = (unsigned)atoi(p[4]);
            /* (the set's name kept once in the packed list: an offset now, made a pointer at the end) */
            for (at = 0; at < sets_n && strcmp(l->sets + at, p[1]); at += strlen(l->sets + at) + 1)
                ;
            if (at >= sets_n) {
                size_t need = strlen(p[1]) + 1;
                if (sets_n + need > sets_room) {
                    char *m = realloc(l->sets, sets_room = (sets_n + need) * 2);
                    if (!m)
                        break;
                    l->sets = m;
                }
                memcpy(l->sets + sets_n, p[1], need);
                at = sets_n, sets_n += need;
            }
            x->set = (const char *)(size_t)(at + 1);
        }
    }
    fclose(f);
    if (ok) {
        int i;
        for (i = 0; i < l->n; i++)
            if (l->f[i].set)
                l->f[i].set = l->sets + ((size_t)l->f[i].set - 1);
        qsort(l->f, (size_t)l->n, sizeof *l->f, by_path);
    }
    return ok ? 0 : -1;
}

/* ---- the store's sets */

struct sets_ctx { char (*sets)[BK_SET]; int n, room; };

static int set_one(void *arg, const char *name, int dir)
{
    struct sets_ctx *c = arg;
    if (dir && is_set_name(name) && strlen(name) < BK_SET && c->n < c->room)
        snprintf(c->sets[c->n++], BK_SET, "%s", name);
    return 0;
}

static int newest_first(const void *a, const void *b)
{
    return strcmp((const char *)b, (const char *)a);
}

struct has_ctx { int found; };

static int has_manifest(void *arg, const char *name, int dir)
{
    struct has_ctx *c = arg;
    if (!dir && !strcmp(name, "MANIFEST.TXT"))
        c->found = 1;
    return 0;
}

int bk_sets(const struct bk_store *st, const char *where, char (*sets)[BK_SET], int room)
{
    struct sets_ctx c;
    char dir[PATH_ROOM];
    int i, k;
    c.sets = sets, c.n = 0, c.room = room;
    if (st->list(st->ctx, where, set_one, &c) < 0)
        return -1;
    qsort(sets, (size_t)c.n, BK_SET, newest_first);
    for (i = k = 0; i < c.n; i++) {             /* (complete ones only: the manifest is written last) */
        struct has_ctx h = { 0 };
        join(dir, sizeof dir, where, sets[i]);
        if (st->list(st->ctx, dir, has_manifest, &h) == 0 && h.found) {
            if (k != i)
                memcpy(sets[k], sets[i], BK_SET);
            k++;
        }
    }
    return k;
}

/* Every folder of path made there (path's own last part left: a file's). */
static int store_dirs(const struct bk_store *st, const char *base, const char *path)
{
    char d[PATH_ROOM], rel[PATH_ROOM];
    const char *p;
    for (p = strchr(path, '/'); p; p = strchr(p + 1, '/')) {
        snprintf(rel, sizeof rel, "%.*s", (int)(p - path), path);
        join(d, sizeof d, base, rel);
        if (st->mkdir(st->ctx, d) < 0)
            return -1;
    }
    return 0;
}

static int store_tree(const struct bk_store *st, const char *where)    /* where's own folders, from the top */
{
    char d[PATH_ROOM];
    const char *p;
    for (p = where; (p = strchr(p + 1, '/')) != NULL;) {
        snprintf(d, sizeof d, "%.*s", (int)(p - where), where);
        if (st->mkdir(st->ctx, d) < 0)
            return -1;
    }
    return st->mkdir(st->ctx, where);
}

/* ---- a backup */

int bk_backup(struct bk_job *j, const char *folder)
{
    const struct bk_store *st = j->store;
    struct bk_list now = { 0 }, last = { 0 };
    char sets[64][BK_SET], setdir[PATH_ROOM], remote[PATH_ROOM], local[PATH_ROOM];
    unsigned long long total = 0;
    int i, n, r = BK_OK, k;
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    j->files = j->copied = j->kept = j->failed = j->folders = 0, j->bytes = 0, j->why[0] = 0;
    bk_set_name(j->set, sizeof j->set, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
    {
        struct stat fs;                         /* (findfirst can't tell a folder that isn't there from an empty one) */
        if (stat(folder, &fs) != 0 || !S_ISDIR(fs.st_mode)) {
            snprintf(j->why, sizeof j->why, "there is no folder %.150s", folder);
            return BK_E_LOCAL;
        }
    }
    if (walk(&now, folder, "", 0) < 0) {
        snprintf(j->why, sizeof j->why, "%.150s couldn't be read", folder);
        list_free(&now);
        return BK_E_LOCAL;
    }
    qsort(now.f, (size_t)now.n, sizeof *now.f, by_path);
    if (store_tree(st, j->where) < 0) {
        snprintf(j->why, sizeof j->why, "the folder %.150s couldn't be made there", j->where);
        list_free(&now);
        return BK_E_STORE;
    }
    n = bk_sets(st, j->where, sets, 64);
    for (k = 2; n > 0 && k < 100; k++) {        /* (a set of this second already: -2, -3 ...) */
        int i2, clash = 0;
        for (i2 = 0; i2 < n; i2++)
            clash |= !strcmp(sets[i2], j->set);
        if (!clash)
            break;
        snprintf(j->set + 15, sizeof j->set - 15, "-%d", k);
    }
    if (j->changes && n > 0) {                  /* the newest complete set's manifest: what it holds */
        join3(remote, sizeof remote, j->where, sets[0], "MANIFEST.TXT");
        if (st->get(st->ctx, remote, j->temp) != 0 || manifest_read(j->temp, &last) != 0) {
            list_free(&last);                   /* (unreadable: everything is copied, as for a full one) */
            memset(&last, 0, sizeof last);
        }
    }
    join(setdir, sizeof setdir, j->where, j->set);
    if (st->mkdir(st->ctx, setdir) < 0) {
        snprintf(j->why, sizeof j->why, "the folder %.150s couldn't be made there", setdir);
        r = BK_E_STORE;
        goto out;
    }
    for (i = k = 0; i < now.n; i++) {           /* what to copy: compared with the last set's, both sorted */
        struct bk_file *f = &now.f[i];
        if (f->dir) {
            j->folders++;
            continue;
        }
        j->files++;
        while (k < last.n && strcmp(last.f[k].path, f->path) < 0)
            k++;
        if (k < last.n && !last.f[k].dir && !strcmp(last.f[k].path, f->path) && last.f[k].size == f->size
            && last.f[k].date == f->date && last.f[k].time == f->time && last.f[k].set)
            f->set = last.f[k].set;             /* (held already) */
        else
            total += f->size;
    }
    {
        unsigned long long done = 0;
        for (i = 0; i < now.n; i++) {
            struct bk_file *f = &now.f[i];
            if (f->dir)
                continue;
            if (f->set) {
                j->kept++;
                continue;
            }
            if (j->progress && j->progress(j->ctx, f->path, done, total)) {
                r = BK_E_STOPPED;
                goto out;
            }
            join(remote, sizeof remote, setdir, f->path);
            local_join(local, sizeof local, folder, f->path);
            if (store_dirs(st, setdir, f->path) < 0 || st->put(st->ctx, local, remote) != 0) {
                FILE *probe = fopen(local, "rb");      /* (here or there: a file here that can't be read is
                                                   passed over; the other computer failing ends the run) */
                if (probe) {
                    fclose(probe);
                    snprintf(j->why, sizeof j->why, "%.150s couldn't be written there", f->path);
                    r = BK_E_STORE;
                    goto out;
                }
                j->failed++;
                continue;
            }
            f->set = j->set;
            j->copied++, j->bytes += f->size, done += f->size;
        }
        if (j->progress && j->progress(j->ctx, "", done, total)) {
            r = BK_E_STOPPED;
            goto out;
        }
    }
    join(remote, sizeof remote, setdir, "MANIFEST.TXT");        /* last: the set is a backup from now on */
    if (manifest_write(j->temp, folder, j->set, j->changes, &now) != 0) {
        snprintf(j->why, sizeof j->why, "the list of files couldn't be written here (%.150s)", j->temp);
        r = BK_E_LOCAL;
    } else if (st->put(st->ctx, j->temp, remote) != 0) {
        snprintf(j->why, sizeof j->why, "the list of files couldn't be written there");
        r = BK_E_STORE;
    }
    remove(j->temp);
out:
    list_free(&now), list_free(&last);
    return r;
}

/* ---- a restore */

static int local_dirs(const char *to, const char *path, int whole)    /* path's folders made here (whole: it too) */
{
    char d[PATH_ROOM], rel[PATH_ROOM];
    const char *p = path;
    struct stat st;
    if (stat(to, &st) != 0 && mkdir(to, 0777) != 0)
        return -1;
    for (;;) {
        p = strchr(p, '/');
        if (!p && !whole)
            return 0;
        snprintf(rel, sizeof rel, "%.*s", p ? (int)(p - path) : (int)strlen(path), path);
        local_join(d, sizeof d, to, rel);
        if (stat(d, &st) != 0 && mkdir(d, 0777) != 0)
            return -1;
        if (!p)
            return 0;
        p++;
    }
}

static void dos_stamp(const char *file, unsigned date, unsigned tim)
{
    struct tm tm;
    struct utimbuf u;
    memset(&tm, 0, sizeof tm);
    tm.tm_year = (int)(date >> 9) + 80, tm.tm_mon = (int)((date >> 5) & 15) - 1, tm.tm_mday = (int)(date & 31);
    tm.tm_hour = (int)(tim >> 11), tm.tm_min = (int)((tim >> 5) & 63), tm.tm_sec = (int)(tim & 31) * 2;
    tm.tm_isdst = -1;
    u.actime = u.modtime = mktime(&tm);
    if (u.modtime != (time_t)-1)
        utime(file, &u);
}

int bk_restore(struct bk_job *j, const char *set, const char *to)
{
    const struct bk_store *st = j->store;
    struct bk_list l = { 0 };
    char remote[PATH_ROOM], local[PATH_ROOM];
    unsigned long long total = 0, done = 0;
    int i, r = BK_OK;
    j->files = j->copied = j->kept = j->failed = j->folders = 0, j->bytes = 0, j->why[0] = 0;
    snprintf(j->set, sizeof j->set, "%s", set);
    join3(remote, sizeof remote, j->where, set, "MANIFEST.TXT");
    if (st->get(st->ctx, remote, j->temp) != 0 || manifest_read(j->temp, &l) != 0) {
        remove(j->temp);
        list_free(&l);
        snprintf(j->why, sizeof j->why, "the backup %.150s has no list of its files", set);
        return BK_E_NONE;
    }
    remove(j->temp);
    for (i = 0; i < l.n; i++)
        total += l.f[i].dir ? 0 : l.f[i].size;
    for (i = 0; i < l.n; i++) {
        struct bk_file *f = &l.f[i];
        if (j->progress && j->progress(j->ctx, f->path, done, total)) {
            r = BK_E_STOPPED;
            break;
        }
        if (f->dir) {
            j->folders++;
            if (local_dirs(to, f->path, 1) != 0)
                j->failed++;
            continue;
        }
        j->files++;
        join3(remote, sizeof remote, j->where, f->set, f->path);
        local_join(local, sizeof local, to, f->path);
        if (local_dirs(to, f->path, 0) != 0) {
            j->failed++;
            continue;
        }
        if (st->get(st->ctx, remote, local) != 0) {
            j->failed++;
            continue;
        }
        dos_stamp(local, f->date, f->time);
        j->copied++, j->bytes += f->size, done += f->size;
    }
    list_free(&l);
    return r;
}
