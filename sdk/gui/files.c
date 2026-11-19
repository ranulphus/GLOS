/* The toolkit's files (include/glos/gui.h): DOS paths, directories read and
 * sorted, and the few things the apps do to files.
 *
 * On DOS (DJGPP) these are the C library's calls, which take DOS paths. On
 * the host (the tests) a path's drive is a directory: C:\GLOS\X.TXT is
 * $GLOS_HOST_DRIVES/C/GLOS/X.TXT, and names are kept as DOS has them
 * (upper case, 8.3), so a test's pictures are the same in both places. */
#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>
#ifdef __DJGPP__
#include <dir.h>
#include <dos.h>
#endif

#include "glos/cp.h"
#include "guiint.h"

/* The path the C library is given. */
static const char *native(const char *path, char *buf, size_t room)
{
#ifdef __DJGPP__
    (void)buf, (void)room;
    return path;
#else
    const char *root = getenv("GLOS_HOST_DRIVES");
    size_t n;
    char *p;
    if (!root || !path[0] || path[1] != ':')
        return path;
    snprintf(buf, room, "%s/%c%s", root, path[0] >= 'a' ? path[0] - 32 : path[0], path + 2);
    for (p = buf + strlen(root); *p; p++)      /* (as DOS takes a name in any case: the drives' names are its
                                                   own, in capitals) */
        if (*p == '\\')
            *p = '/';
        else if (*p >= 'a' && *p <= 'z')
            *p = (char)(*p - 32);
    n = strlen(buf);
    if (n > 1 && buf[n - 1] == '/')
        buf[n - 1] = 0;
    return buf;
#endif
}

void (*gui_libc_hook)(int on);

/* (localtime keeps its answer in memory of its own: in a program with tasks, a folder read on a task's thread
   and one read on the window's would have each other's dates. The C library's lock is held over the call and
   the reading of its answer.) */
static void dos_time(time_t t, unsigned short *date, unsigned short *tm)
{
    struct tm *g;
    if (gui_libc_hook)
        gui_libc_hook(1);
    g = localtime(&t);
    if (!g || g->tm_year < 80) {
        *date = (1 << 5) | 1;
        *tm = 0;
    } else {
        *date = (unsigned short)(((g->tm_year - 80) << 9) | ((g->tm_mon + 1) << 5) | g->tm_mday);
        *tm = (unsigned short)((g->tm_hour << 11) | (g->tm_min << 5) | (g->tm_sec / 2));
    }
    if (gui_libc_hook)
        gui_libc_hook(0);
}

static int upper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

static int name_cmp(const struct gui_dirent *a, const struct gui_dirent *b)
{
    const char *p = a->name, *q = b->name;
    if (a->dir != b->dir)
        return a->dir ? -1 : 1;
    for (; *p && upper((unsigned char)*p) == upper((unsigned char)*q); p++, q++)
        ;
    return upper((unsigned char)*p) - upper((unsigned char)*q);
}

static int qcmp(const void *a, const void *b) { return name_cmp(a, b); }

#ifdef __DJGPP__
/* On DOS a file or a folder is asked after by DOS's own find: one call, which says its attributes, size, date
   and time. The C library's stat makes a dozen calls for the same and opens every file to see whether it is a
   program: 45 ms a name on the 486's profile, for a folder's listing a stat for each name in it, and for a job
   two or three for each file (a copy of two small files took a second there; 2026-11-19). */
#define DOS_ALL (FA_RDONLY | FA_HIDDEN | FA_SYSTEM | FA_DIREC | FA_ARCH)

static void from_find(const struct ffblk *f, const char *name, struct gui_dirent *e)
{
    memset(e, 0, sizeof *e);
    strncpy(e->name, name, sizeof e->name - 1);
    e->dir = (f->ff_attrib & FA_DIREC) != 0;
    e->size = e->dir ? 0 : f->ff_fsize;
    e->date = f->ff_fdate ? f->ff_fdate : (1 << 5) | 1;        /* (none: 1980's first day, as stat's was) */
    e->time = f->ff_fdate ? f->ff_ftime : 0;
}

/* A find that is not gone through to its end: with long file names DOS keeps a handle for it, which the
   library gives back when the next find comes to nothing. */
static void find_end(struct ffblk *f)
{
    if (!memcmp(f->lfn_magic, "LFN32", 6))
        while (findnext(f) == 0)
            ;
}
#endif

int gui_stat(const char *path, struct gui_dirent *e)
{
    char buf[512];
    struct stat st;
#ifdef __DJGPP__
    {
        size_t n = strlen(path);
        struct ffblk f;
        if (n > 3 && path[n - 1] != '\\' && path[n - 1] != '/' && path[n - 1] != ':' && !strpbrk(path, "*?")) {
            if (findfirst(path, &f, DOS_ALL) != 0)
                return -1;
            find_end(&f);
            from_find(&f, gui_path_name(path), e);
            gui_dos_text(e->name, e->text, sizeof e->text);
            return 0;
        }
    }                                           /* (a drive's root, a path that ends oddly: the library's) */
#endif
    if (stat(native(path, buf, sizeof buf), &st) != 0)
        return -1;
    memset(e, 0, sizeof *e);
    strncpy(e->name, gui_path_name(path), sizeof e->name - 1);
    gui_dos_text(e->name, e->text, sizeof e->text);
    e->dir = S_ISDIR(st.st_mode) != 0;
    e->size = e->dir ? 0 : (unsigned long)st.st_size;
    dos_time(st.st_mtime, &e->date, &e->time);
    return 0;
}

#ifdef __DJGPP__
int gui_dir_read(const char *dir, struct gui_dirent **out)
{
    char pattern[512];
    struct ffblk f;
    struct gui_dirent *list = NULL;
    size_t dn = strlen(dir);
    int n = 0, room = 0, r;
    *out = NULL;
    if (!gui_is_dir(dir))
        return -1;
    snprintf(pattern, sizeof pattern, "%s%s*.*", dir, dn && (dir[dn - 1] == '\\' || dir[dn - 1] == '/' || dir[dn - 1] == ':')
                                                   ? "" : "\\");
    for (r = findfirst(pattern, &f, DOS_ALL); r == 0; r = findnext(&f)) {       /* (to its end: see find_end) */
        struct gui_dirent e;
        char *q;
        if (!strcmp(f.ff_name, ".") || !strcmp(f.ff_name, ".."))
            continue;
        from_find(&f, f.ff_name, &e);
        for (q = e.name; *q; q++)               /* (DOS's names as DOS has them, in capitals) */
            if (*q >= 'a' && *q <= 'z')
                *q -= 32;
        if (n == room) {
            struct gui_dirent *nl;
            room = room * 2 + 32;
            if (!(nl = realloc(list, (size_t)room * sizeof *nl))) {
                while (findnext(&f) == 0)
                    ;
                free(list);
                return -1;
            }
            list = nl;
        }
        gui_dos_text(e.name, e.text, sizeof e.text);
        list[n++] = e;
    }
    if (n)
        qsort(list, (size_t)n, sizeof *list, qcmp);
    *out = list;
    return n;
}
#else
int gui_dir_read(const char *dir, struct gui_dirent **out)
{
    char buf[512], full[512];
    DIR *d = opendir(native(dir, buf, sizeof buf));
    struct dirent *de;
    struct gui_dirent *list = NULL;
    int n = 0, room = 0;
    *out = NULL;
    if (!d)
        return -1;
    while ((de = readdir(d)) != NULL) {
        struct gui_dirent e;
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        gui_path_join(full, sizeof full, dir, de->d_name);
        if (gui_stat(full, &e) != 0)
            continue;
#ifdef __DJGPP__
        {                                       /* (DJGPP gives DOS's names in lower case: as DOS has them) */
            char *q;
            for (q = e.name; *q; q++)
                if (*q >= 'a' && *q <= 'z')
                    *q -= 32;
        }
#endif
        if (n == room) {
            struct gui_dirent *nl;
            room = room * 2 + 32;
            if (!(nl = realloc(list, (size_t)room * sizeof *nl))) {
                closedir(d);
                free(list);
                return -1;
            }
            list = nl;
        }
        gui_dos_text(e.name, e.text, sizeof e.text);  /* (again: upper case) */
        list[n++] = e;
    }
    closedir(d);
    if (n)
        qsort(list, (size_t)n, sizeof *list, qcmp);
    *out = list;
    return n;
}
#endif

static int page(void)
{
    static int cp;
    if (!cp)
        cp = cp_active();
    return cp;
}

const char *gui_dos_text(const char *dos, char *out, int room)
{
    cp_to_utf8(page(), dos, out, room);
    return out;
}

int gui_text_dos(const char *text, char *out, int room)
{
    return cp_from_utf8(page(), text, out, room) == 0 ? 0 : -1;
}

int gui_drives(char *letters)
{
    int n = 0;
#ifdef __DJGPP__
    unsigned i;
    unsigned char equip = 0;
    {
        unsigned short e;
        dosmemget(0x410, 2, &e);                /* (floppies from the BIOS: not touched to see) */
        equip = (unsigned char)e;
    }
    if (equip & 1) {
        letters[n++] = 'A';
        if (((equip >> 6) & 3) >= 1)
            letters[n++] = 'B';
    }
    for (i = 3; i <= 26; i++) {
        union REGS r;                           /* INT 21h 4408h: is it removable (a drive there at all) */
        r.x.ax = 0x4408;
        r.h.bl = (unsigned char)i;
        int86(0x21, &r, &r);
        if (!r.x.cflag || r.x.ax != 0x0F)
            letters[n++] = (char)('A' + i - 1);
    }
#else
    const char *root = getenv("GLOS_HOST_DRIVES");
    char c, path[512];
    struct stat st;
    for (c = 'A'; root && c <= 'Z'; c++) {
        snprintf(path, sizeof path, "%s/%c", root, c);
        if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
            letters[n++] = c;
    }
#endif
    letters[n] = 0;
    return n;
}

int gui_is_dir(const char *path)
{
    struct gui_dirent e;
    if (path[0] && path[1] == ':' && (!path[2] || (path[2] == '\\' && !path[3])))
        return 1;                               /* (a drive's root) */
    return gui_stat(path, &e) == 0 && e.dir;
}

void *gui_fopen(const char *path, const char *mode)
{
    char buf[512];
    return fopen(native(path, buf, sizeof buf), mode);
}

int gui_mkdir(const char *path)
{
    char buf[512];
    return mkdir(native(path, buf, sizeof buf), 0777);
}

int gui_rmdir(const char *path)
{
    char buf[512];
    return rmdir(native(path, buf, sizeof buf));
}

int gui_unlink(const char *path)
{
    char buf[512];
    return unlink(native(path, buf, sizeof buf));
}

int gui_rename(const char *from, const char *to)
{
    char a[512], b[512];
    return rename(native(from, a, sizeof a), native(to, b, sizeof b));
}

int gui_set_time(const char *path, unsigned short date, unsigned short tm)
{
    char buf[512];
    struct tm g;
    struct utimbuf u;
    memset(&g, 0, sizeof g);
    g.tm_year = (date >> 9) + 80;
    g.tm_mon = ((date >> 5) & 15) - 1;
    g.tm_mday = date & 31;
    g.tm_hour = tm >> 11;
    g.tm_min = (tm >> 5) & 63;
    g.tm_sec = (tm & 31) * 2;
    g.tm_isdst = -1;
    {                                           /* (mktime's zone and utime's own DOS calls are not among the C
                                                   library's locked ones: in a program with tasks, under its lock) */
        int r;
        if (gui_libc_hook)
            gui_libc_hook(1);
        u.actime = u.modtime = mktime(&g);
        r = utime(native(path, buf, sizeof buf), &u);
        if (gui_libc_hook)
            gui_libc_hook(0);
        return r;
    }
}

void gui_path_join(char *out, int room, const char *dir, const char *name)
{
    size_t n = strlen(dir);
    if (n && dir[n - 1] == '\\')
        snprintf(out, (size_t)room, "%s%s", dir, name);
    else
        snprintf(out, (size_t)room, "%s\\%s", dir, name);
}

void gui_path_up(char *path)
{
    char *p = strrchr(path, '\\');
    if (!p)
        return;
    if (p == path + 2 && path[1] == ':')        /* "C:\A" -> "C:\" */
        p[1] = 0;
    else
        *p = 0;
}

const char *gui_path_name(const char *path)
{
    const char *p = strrchr(path, '\\');
    if (!p)
        p = strrchr(path, ':');
    return p ? p + 1 : path;
}
