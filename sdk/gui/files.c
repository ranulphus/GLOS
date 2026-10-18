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
    for (p = buf + strlen(root); *p; p++)
        if (*p == '\\')
            *p = '/';
    n = strlen(buf);
    if (n > 1 && buf[n - 1] == '/')
        buf[n - 1] = 0;
    return buf;
#endif
}

static void dos_time(time_t t, unsigned short *date, unsigned short *tm)
{
    struct tm *g = localtime(&t);
    if (!g || g->tm_year < 80) {
        *date = (1 << 5) | 1;
        *tm = 0;
        return;
    }
    *date = (unsigned short)(((g->tm_year - 80) << 9) | ((g->tm_mon + 1) << 5) | g->tm_mday);
    *tm = (unsigned short)((g->tm_hour << 11) | (g->tm_min << 5) | (g->tm_sec / 2));
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

int gui_stat(const char *path, struct gui_dirent *e)
{
    char buf[512];
    struct stat st;
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
    u.actime = u.modtime = mktime(&g);
    return utime(native(path, buf, sizeof buf), &u);
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
