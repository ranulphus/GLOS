/* GLOS's shell (shell.h; docs/v1/shell.md; PRD D50): DOS's commands and syntax, modernised.
 *
 * Built in: CD (CHDIR), a drive's letter, DIR [/W] [/B], COPY, MOVE, REN (RENAME), DEL (ERASE), MD (MKDIR),
 * RD (RMDIR), TYPE, ECHO, SET, PATH, PROMPT, CLS, VER, EXIT, HISTORY, HELP, WHERE, START, and what the host
 * adds. A built-in's output goes to a file with > or >>.
 *
 * Anything else is a program: found here and along PATH (.COM, .EXE, .BAT) and run by the host, in this
 * shell's folder, with the variables this shell has set; a line with a pipe or a redirection is the host's
 * to run whole (through the DOS's COMMAND.COM until S4).
 *
 * %NAME% is a variable; %ERRORLEVEL% the last exit code. && runs what follows only after a success (exit code
 * 0), || only after a failure.
 *
 * The shell keeps its own folder for each drive and resolves paths itself, so every host's files see full
 * paths and the tests see what DOS does. */
#include <stdarg.h>
#include <string.h>

#include "shell.h"

static int upper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

static int same(const char *a, const char *b)
{
    for (; *a && upper((unsigned char)*a) == upper((unsigned char)*b); a++, b++)
        ;
    return !*a && !*b;
}

static void upper_all(char *q)
{
    for (; *q; q++)
        *q = (char)upper((unsigned char)*q);
}

int sh_fmt(char *out, unsigned room, const char *fmt, ...)
{
    va_list ap;
    unsigned n = 0;
    va_start(ap, fmt);
    for (; *fmt; fmt++) {
        int left = 0, zero = 0, width = 0, prec = -1, lng = 0, len = 0, pad;
        char num[24];
        const char *t = "";
        if (*fmt != '%' || fmt[1] == '%') {
            if (*fmt == '%')
                fmt++;
            if (n + 1 < room)
                out[n++] = *fmt;
            continue;
        }
        for (fmt++; *fmt == '-' || *fmt == '0'; fmt++)
            if (*fmt == '-')
                left = 1;
            else
                zero = 1;
        for (; *fmt >= '0' && *fmt <= '9'; fmt++)
            width = width * 10 + (*fmt - '0');
        if (*fmt == '.') {
            fmt++;
            if (*fmt == '*') {
                prec = va_arg(ap, int);
                fmt++;
            } else {
                for (prec = 0; *fmt >= '0' && *fmt <= '9'; fmt++)
                    prec = prec * 10 + (*fmt - '0');
            }
        }
        if (*fmt == 'l')
            lng = 1, fmt++;
        if (*fmt == 's') {
            if (!(t = va_arg(ap, const char *)))
                t = "";
            len = (int)strlen(t);
            if (prec >= 0 && len > prec)
                len = prec;
        } else if (*fmt == 'c') {
            num[0] = (char)va_arg(ap, int);
            t = num, len = 1;
        } else if (*fmt == 'd' || *fmt == 'u') {
            unsigned long v;
            int neg = 0, at = (int)sizeof num;
            if (*fmt == 'd') {
                long d = lng ? va_arg(ap, long) : (long)va_arg(ap, int);
                neg = d < 0;
                v = neg ? 0ul - (unsigned long)d : (unsigned long)d;
            } else {
                v = lng ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned);
            }
            do
                num[--at] = (char)('0' + v % 10);
            while (v /= 10);
            if (neg)
                num[--at] = '-';
            t = num + at, len = (int)sizeof num - at;
        } else if (!*fmt) {
            break;
        }
        pad = width - len;
        for (; !left && pad > 0; pad--)
            if (n + 1 < room)
                out[n++] = zero ? '0' : ' ';
        for (; len; len--, t++)
            if (n + 1 < room)
                out[n++] = *t;
        for (; pad > 0; pad--)
            if (n + 1 < room)
                out[n++] = ' ';
    }
    va_end(ap);
    if (room)
        out[n] = 0;
    return (int)n;
}

/* ---- what is written */

/* (the shell's own output is DOS's, as COMMAND.COM's would be: names and files' text in its code page) */
static void out(struct sh *s, const char *t) { s->h->write(s->h->ctx, SH_OUT, t, (int)strlen(t)); }

static void err(struct sh *s, const char *t) { s->h->write(s->h->ctx, SH_ERR, t, (int)strlen(t)); }

/* A built-in's output: to the terminal, or to a file (> and >>). */
static void say(struct sh *s, const char *t)
{
    if (s->redirect)
        s->h->put(s->h->ctx, s->redirect, t, (int)strlen(t));
    else
        out(s, t);
}

static char *copy_of(struct sh *s, const char *t)
{
    char *c = s->h->alloc(s->h->ctx, (unsigned)strlen(t) + 1);
    if (c)
        strcpy(c, t);
    return c;
}

/* ---- variables */

static int var_find(const struct sh *s, const char *name, size_t n)
{
    int i;
    for (i = 0; i < s->nvars; i++) {
        size_t k;
        for (k = 0; k < n && s->vars[i][k] && upper((unsigned char)s->vars[i][k]) == upper((unsigned char)name[k]); k++)
            ;
        if (k == n && s->vars[i][k] == '=')
            return i;
    }
    return -1;
}

static const char *var_get(const struct sh *s, const char *name)
{
    int i = var_find(s, name, strlen(name));
    return i < 0 ? 0 : strchr(s->vars[i], '=') + 1;
}

static void var_set(struct sh *s, const char *name, const char *value)
{
    size_t n = strlen(name), k;
    int i = var_find(s, name, n);
    char *v;
    if (!value || !*value) {
        if (i >= 0) {
            s->h->free(s->h->ctx, s->vars[i]);
            s->vars[i] = s->vars[--s->nvars];
            s->changed[i] = s->changed[s->nvars];
        }
        return;
    }
    v = s->h->alloc(s->h->ctx, (unsigned)(n + strlen(value) + 2));
    if (!v)
        return;
    for (k = 0; k < n; k++)
        v[k] = (char)upper((unsigned char)name[k]);
    v[n] = '=';
    strcpy(v + n + 1, value);
    if (i < 0) {
        if (s->nvars == SH_VARS) {
            s->h->free(s->h->ctx, v);
            err(s, "No room for another variable.\r\n");
            return;
        }
        i = s->nvars++;
    } else {
        s->h->free(s->h->ctx, s->vars[i]);
    }
    s->vars[i] = v;
    s->changed[i] = 1;
}

/* %NAME% expanded; %% is %. */
static void expand(const struct sh *s, const char *in, char *outb, size_t room)
{
    size_t n = 0;
    while (*in && n < room - 1) {
        if (*in == '%') {
            const char *e = strchr(in + 1, '%');
            if (e == in + 1) {
                outb[n++] = '%';
                in += 2;
                continue;
            }
            if (e) {
                char name[64], num[16];
                const char *v;
                size_t k = (size_t)(e - in - 1) < sizeof name - 1 ? (size_t)(e - in - 1) : sizeof name - 1;
                memcpy(name, in + 1, k);
                name[k] = 0;
                if (same(name, "ERRORLEVEL")) {
                    sh_fmt(num, sizeof num, "%d", s->errorlevel);
                    v = num;
                } else {
                    v = var_get(s, name);
                }
                while (v && *v && n < room - 1)
                    outb[n++] = *v++;
                in = e + 1;
                continue;
            }
        }
        outb[n++] = *in++;
    }
    outb[n] = 0;
}

/* ---- paths */

static const char *path_name(const char *path)
{
    const char *p = strrchr(path, '\\');
    if (!p)
        p = strrchr(path, ':');
    return p ? p + 1 : path;
}

static void path_join(char *outb, unsigned room, const char *dir, const char *name)
{
    size_t n = strlen(dir);
    sh_fmt(outb, room, n && dir[n - 1] == '\\' ? "%s%s" : "%s\\%s", dir, name);
}

static void path_up(char *path)
{
    char *p = strrchr(path, '\\');
    if (!p)
        return;
    if (p == path + 2 && path[1] == ':')        /* "C:\A" -> "C:\" */
        p[1] = 0;
    else
        *p = 0;
}

/* p made a full path (a drive and its folders, "." and ".." gone) from this shell's folders. */
static void full_path(const struct sh *s, const char *p, char *outb, size_t room)
{
    char t[SH_PATH], *parts[32], *q;
    int n = 0, i, d = s->drive;
    size_t o;
    if (p[0] && p[1] == ':') {
        d = upper((unsigned char)p[0]) - 'A';
        if (d < 0 || d > 25)
            d = s->drive;
        p += 2;
    }
    if (p[0] == '\\' || p[0] == '/')
        sh_fmt(t, sizeof t, "%s", p + 1);
    else
        sh_fmt(t, sizeof t, "%s%s%s", s->cwd[d] + 3, s->cwd[d][3] ? "\\" : "", p);
    for (q = t; *q && n < 32;) {                /* its parts, between the slashes */
        char *part = q;
        while (*q && *q != '\\' && *q != '/')
            q++;
        if (*q)
            *q++ = 0;
        if (!*part || !strcmp(part, "."))
            continue;
        if (!strcmp(part, "..")) {
            if (n)
                n--;
            continue;
        }
        parts[n++] = part;
    }
    o = (size_t)sh_fmt(outb, (unsigned)room, "%c:\\", 'A' + d);
    for (i = 0; i < n && o < room; i++)
        o += (size_t)sh_fmt(outb + o, (unsigned)(room - o), "%s%s", i ? "\\" : "", parts[i]);
    upper_all(outb);
}

/* name matches a DOS pattern (* and ?) */
static int match(const char *pat, const char *name)
{
    for (; *pat; pat++, name++) {
        if (*pat == '*') {
            const char *t;
            for (t = name;; t++) {
                if (match(pat + 1, t))
                    return 1;
                if (!*t)
                    return 0;
            }
        }
        if (!*name || (*pat != '?' && upper((unsigned char)*pat) != upper((unsigned char)*name)))
            return 0;
    }
    return !*name;
}

/* A path with a pattern: its folder (full) and its pattern ("*.*" for a folder itself). 1 if it names a folder
   as a whole. */
static int split_pattern(const struct sh *s, const char *arg, char *dir, unsigned room, char *pat, unsigned proom)
{
    char full[SH_PATH];
    full_path(s, arg, full, sizeof full);
    if (s->h->is_dir(s->h->ctx, full)) {
        sh_fmt(dir, room, "%s", full);
        sh_fmt(pat, proom, "*.*");
        return 1;
    }
    sh_fmt(pat, proom, "%s", path_name(full));
    sh_fmt(dir, room, "%s", full);
    path_up(dir);
    return 0;
}

/* ---- the built-ins */

static void size_text(unsigned long v, char *b, size_t room)
{
    char t[32];
    int n = sh_fmt(t, sizeof t, "%lu", v), i, o = 0;
    for (i = 0; i < n && o < (int)room - 1; i++) {
        if (i && (n - i) % 3 == 0)
            b[o++] = ',';
        b[o++] = t[i];
    }
    b[o] = 0;
}

static int cmd_dir(struct sh *s, int argc, char **argv)
{
    char dir[SH_PATH], pat[64], line[160], sz[24];
    struct sh_dirent *e;
    int n, i, wide = 0, bare = 0, files = 0, dirs = 0, col = 0;
    unsigned long bytes = 0;
    const char *arg = 0;
    for (i = 1; i < argc; i++)
        if (same(argv[i], "/W"))
            wide = 1;
        else if (same(argv[i], "/B"))
            bare = 1;
        else
            arg = argv[i];
    split_pattern(s, arg ? arg : ".", dir, sizeof dir, pat, sizeof pat);
    if ((n = s->h->dir_read(s->h->ctx, dir, &e)) < 0) {
        err(s, "File not found\r\n");
        return 1;
    }
    if (!bare) {
        sh_fmt(line, sizeof line, " Directory of %s\r\n\r\n", dir);
        say(s, line);
    }
    for (i = 0; i < n; i++) {
        char nm[16], ext[8];
        const char *dot;
        if (!match(pat, e[i].name) && !(same(pat, "*.*")))
            continue;
        if (bare) {
            sh_fmt(line, sizeof line, "%s\r\n", e[i].name);
            say(s, line);
        } else if (wide) {
            if (e[i].dir)
                sh_fmt(line, sizeof line, "[%s]", e[i].name);
            else
                sh_fmt(line, sizeof line, "%s", e[i].name);
            sh_fmt(nm, sizeof nm, "%-15s", line);
            say(s, nm);
            if (++col == 5) {
                say(s, "\r\n");
                col = 0;
            }
        } else {
            dot = strchr(e[i].name, '.');
            sh_fmt(nm, sizeof nm, "%.*s", dot ? (int)(dot - e[i].name) : (int)strlen(e[i].name), e[i].name);
            sh_fmt(ext, sizeof ext, "%s", dot ? dot + 1 : "");
            size_text(e[i].size, sz, sizeof sz);
            sh_fmt(line, sizeof line, "%-8s %-3s %14s %02u-%02u-%04u  %2u:%02u\r\n", nm, ext, e[i].dir ? "<DIR>" : sz,
                   (unsigned)(e[i].date >> 5) & 15, (unsigned)e[i].date & 31, (unsigned)(e[i].date >> 9) + 1980,
                   (unsigned)e[i].time >> 11, (unsigned)(e[i].time >> 5) & 63);
            say(s, line);
        }
        if (e[i].dir)
            dirs++;
        else
            files++, bytes += e[i].size;
    }
    s->h->dir_free(s->h->ctx, e);
    if (wide && col)
        say(s, "\r\n");
    if (!files && !dirs && !same(pat, "*.*")) { /* (a name or a pattern, and nothing of it: as DOS says) */
        err(s, "File not found\r\n");
        return 1;
    }
    if (!bare) {
        size_text(bytes, sz, sizeof sz);
        sh_fmt(line, sizeof line, "%10d file(s) %14s bytes\r\n%10d dir(s)\r\n", files, sz, dirs);
        say(s, line);
    }
    return files + dirs ? 0 : 1;
}

static int copy_file(struct sh *s, const char *from, const char *to)
{
    void *a = s->h->open(s->h->ctx, from, 'r'), *b;
    char buf[4096];
    int n, bad = 0;
    if (!a)
        return -1;
    if (!(b = s->h->open(s->h->ctx, to, 'w'))) {
        s->h->close(s->h->ctx, a);
        return -1;
    }
    while ((n = s->h->read(s->h->ctx, a, buf, sizeof buf)) > 0)
        if (s->h->put(s->h->ctx, b, buf, n) != 0)
            bad = 1;
    s->h->close(s->h->ctx, a);
    if (s->h->close(s->h->ctx, b) != 0)
        bad = 1;
    return bad ? -1 : 0;
}

/* COPY and MOVE: each file the first names, to the second (a folder, or a file's name). */
static int cmd_copy(struct sh *s, int argc, char **argv, int move)
{
    char dir[SH_PATH], pat[64], to[SH_PATH], from[SH_PATH], dst[SH_PATH], line[80];
    struct sh_dirent *e;
    int n, i, done = 0, to_dir;
    if (argc < 2) {
        err(s, "Required parameter missing\r\n");
        return 1;
    }
    split_pattern(s, argv[1], dir, sizeof dir, pat, sizeof pat);
    full_path(s, argc > 2 ? argv[2] : ".", to, sizeof to);
    to_dir = s->h->is_dir(s->h->ctx, to);
    if ((n = s->h->dir_read(s->h->ctx, dir, &e)) < 0) {
        err(s, "File not found\r\n");
        return 1;
    }
    for (i = 0; i < n; i++) {
        if (e[i].dir || !match(pat, e[i].name))
            continue;
        path_join(from, sizeof from, dir, e[i].name);
        if (to_dir)
            path_join(dst, sizeof dst, to, e[i].name);
        else
            sh_fmt(dst, sizeof dst, "%s", to);
        if (same(from, dst)) {
            err(s, "File cannot be copied onto itself\r\n");
            continue;
        }
        if ((move && s->h->rename(s->h->ctx, from, dst) == 0)
            || (copy_file(s, from, dst) == 0 && (!move || s->h->unlink(s->h->ctx, from) == 0))) {
            done++;
            if (argc > 2 && !to_dir)
                break;                          /* (several into one file: the first) */
        } else {
            sh_fmt(line, sizeof line, "%s: not %s\r\n", e[i].name, move ? "moved" : "copied");
            err(s, line);
        }
    }
    s->h->dir_free(s->h->ctx, e);
    sh_fmt(line, sizeof line, "%9d file(s) %s\r\n", done, move ? "moved" : "copied");
    say(s, line);
    return done ? 0 : 1;
}

static int cmd_del(struct sh *s, int argc, char **argv)
{
    char dir[SH_PATH], pat[64], p[SH_PATH], line[80];
    struct sh_dirent *e;
    int n, i, done = 0;
    if (argc < 2) {
        err(s, "Required parameter missing\r\n");
        return 1;
    }
    split_pattern(s, argv[1], dir, sizeof dir, pat, sizeof pat);
    if ((n = s->h->dir_read(s->h->ctx, dir, &e)) < 0) {
        err(s, "File not found\r\n");
        return 1;
    }
    for (i = 0; i < n; i++)
        if (!e[i].dir && match(pat, e[i].name)) {
            path_join(p, sizeof p, dir, e[i].name);
            if (s->h->unlink(s->h->ctx, p) == 0)
                done++;
        }
    s->h->dir_free(s->h->ctx, e);
    if (!done) {
        err(s, "File not found\r\n");
        return 1;
    }
    sh_fmt(line, sizeof line, "%d file(s) deleted\r\n", done);
    say(s, line);
    return 0;
}

static int cmd_type(struct sh *s, int argc, char **argv)
{
    char p[SH_PATH], buf[1024];
    void *f;
    int n;
    if (argc < 2) {
        err(s, "Required parameter missing\r\n");
        return 1;
    }
    full_path(s, argv[1], p, sizeof p);
    if (!(f = s->h->open(s->h->ctx, p, 'r'))) {
        err(s, "File not found\r\n");
        return 1;
    }
    while ((n = s->h->read(s->h->ctx, f, buf, sizeof buf - 1)) > 0) {
        if (s->redirect)
            s->h->put(s->h->ctx, s->redirect, buf, n);
        else
            s->h->write(s->h->ctx, SH_TEXT, buf, n);
    }
    s->h->close(s->h->ctx, f);
    return 0;
}

static int cmd_cd(struct sh *s, int argc, char **argv)
{
    char p[SH_PATH], line[300];
    if (argc < 2) {
        sh_fmt(line, sizeof line, "%s\r\n", s->cwd[s->drive]);
        say(s, line);
        return 0;
    }
    if (strlen(argv[1]) == 2 && argv[1][1] == ':') {    /* CD X: shows X's folder */
        int d = upper((unsigned char)argv[1][0]) - 'A';
        sh_fmt(line, sizeof line, "%s\r\n", d >= 0 && d < 26 ? s->cwd[d] : "");
        say(s, line);
        return 0;
    }
    full_path(s, argv[1], p, sizeof p);
    if (!s->h->is_dir(s->h->ctx, p)) {
        err(s, "Invalid directory\r\n");
        return 1;
    }
    sh_fmt(s->cwd[upper((unsigned char)p[0]) - 'A'], sizeof s->cwd[0], "%s", p);
    return 0;
}

static int cmd_set(struct sh *s, int argc, char **argv, const char *raw)
{
    const char *eq;
    int i;
    char line[300];
    if (argc < 2) {
        for (i = 0; i < s->nvars; i++) {
            sh_fmt(line, sizeof line, "%s\r\n", s->vars[i]);
            say(s, line);
        }
        return 0;
    }
    raw = strchr(raw, ' ');                     /* (the rest of the line, spaces and all) */
    while (raw && *raw == ' ')
        raw++;
    if (!raw || !(eq = strchr(raw, '='))) {
        const char *v = var_get(s, argv[1]);
        if (!v) {
            err(s, "Environment variable not defined\r\n");
            return 1;
        }
        sh_fmt(line, sizeof line, "%s=%s\r\n", argv[1], v);
        say(s, line);
        return 0;
    }
    {
        char name[64];
        size_t n = (size_t)(eq - raw) < sizeof name - 1 ? (size_t)(eq - raw) : sizeof name - 1;
        memcpy(name, raw, n);
        name[n] = 0;
        var_set(s, name, eq + 1);
    }
    return 0;
}

static const char *const builtins[] = { "CD", "CHDIR", "DIR", "COPY", "MOVE", "REN", "RENAME", "DEL", "ERASE", "MD",
    "MKDIR", "RD", "RMDIR", "TYPE", "ECHO", "SET", "PATH", "PROMPT", "CLS", "VER", "EXIT", "HISTORY", "HELP", "WHERE",
    "START", 0 };

static const char *const helps[] = {
    "CD [path]           the folder (of a drive: CD X:); X: changes drive",
    "DIR [path] [/W] [/B] a folder's files: wide, or names alone",
    "COPY from [to]      files (* and ?), into a folder or a file",
    "MOVE from [to]      ... moved",
    "REN from to         a file's new name",
    "DEL files           files deleted (* and ?)",
    "MD path, RD path    a folder made, removed",
    "TYPE file           a file shown",
    "ECHO [text]         text written",
    "SET [name[=value]]  the variables; PATH and PROMPT as SET does",
    "CLS                 a clean window",
    "HISTORY             the lines typed before (Up and Down)",
    "WHERE program       where it is along PATH",
    "START program       a program started beside the terminal",
    0 };

static const char *const helps_end[] = {
    "VER, HELP, EXIT",
    "Programs run here until they want the screen. a && b: b after a has worked; a || b: b if it hasn't.",
    0 };

/* The n-th command's name: the shell's own, then the host's; 0 past the last. */
static const char *command_name(const struct sh *s, int n)
{
    int own = (int)(sizeof builtins / sizeof *builtins) - 1, i;
    if (n < own)
        return builtins[n];
    for (i = 0; s->h->extra_names && s->h->extra_names[i]; i++)
        if (i == n - own)
            return s->h->extra_names[i];
    return 0;
}

/* program: where it is (here, then along PATH; .COM, .EXE, .BAT), into found. 0, or -1. */
static int find_program(const struct sh *s, const char *name, char *found, unsigned room)
{
    static const char *const exts[] = { ".COM", ".EXE", ".BAT" };
    char base[SH_PATH], dirs[1024], cand[SH_PATH], *d, *next;
    const char *path = var_get(s, "PATH"), *dot = strrchr(path_name(name), '.');
    struct sh_dirent e;
    int has_dir = strchr(name, '\\') || strchr(name, ':') || strchr(name, '/');
    sh_fmt(dirs, sizeof dirs, "%s;%s", s->cwd[s->drive], has_dir ? "" : path ? path : "");
    for (d = dirs; d && *d; d = next) {
        int i;
        if ((next = strchr(d, ';')) != 0)
            *next++ = 0;
        if (!*d)
            continue;
        if (has_dir) {
            full_path(s, name, base, sizeof base);
        } else {
            char dd[SH_PATH];
            full_path(s, d, dd, sizeof dd);
            path_join(base, sizeof base, dd, name);
        }
        for (i = dot ? -1 : 0; i < 3; i++) {
            sh_fmt(cand, sizeof cand, "%s%s", base, i < 0 ? "" : exts[i]);
            upper_all(cand);
            if (s->h->stat(s->h->ctx, cand, &e) == 0 && !e.dir) {
                sh_fmt(found, room, "%s", cand);
                return 0;
            }
            if (dot)
                break;
        }
        if (has_dir)
            break;
    }
    return -1;
}

/* ECHO. (a line of its own, or text that begins with a word ECHO would take for ON or OFF) */
static int echo_dot(const char *w)
{
    return upper((unsigned char)w[0]) == 'E' && upper((unsigned char)w[1]) == 'C' && upper((unsigned char)w[2]) == 'H'
           && upper((unsigned char)w[3]) == 'O' && w[4] == '.';
}

static int is_builtin(const struct sh *s, const char *w)
{
    const char *name;
    int i;
    if (echo_dot(w))
        return 1;
    for (i = 0; (name = command_name(s, i)) != 0; i++)
        if (same(w, name))
            return 1;
    return strlen(w) == 2 && w[1] == ':';
}

/* ---- running a program: the host's. 1: under way; 0: started and left; -1: not run (and said). */

static int run_program(struct sh *s, const char *line, int detach)
{
    char command[1024], env[512];
    char prog[SH_PATH], first[128];
    const char *p = line, *args;
    size_t n = 0, e = 0;
    int i, found = 0, r;
    while (*p == ' ')
        p++;
    while (*p && *p != ' ' && n < sizeof first - 1)
        first[n++] = *p++;
    first[n] = 0;
    args = p;
    if (strpbrk(line, "<>|")) {                 /* a pipe or a redirection: the line whole */
        sh_fmt(command, sizeof command, "%s", line);
    } else if (find_program(s, first, prog, sizeof prog) == 0) {
        sh_fmt(command, sizeof command, "%s%s", prog, args);
        found = 1;
    } else {
        err(s, "Bad command or file name\r\n");
        return -1;
    }
    for (i = 0; i < s->nvars; i++)              /* the variables this shell has set */
        if (s->changed[i] && e + strlen(s->vars[i]) + 2 < sizeof env) {
            strcpy(env + e, s->vars[i]);
            e += strlen(s->vars[i]) + 1;
        }
    env[e] = 0;
    r = s->h->run(s->h->ctx, command, found ? prog : 0, first, s->cwd[s->drive], env, detach);
    if (r < 0)
        err(s, "GLOS can't run it here.\r\n");
    return r;
}

static int split_words(char *t, char **argv, int max)
{
    int n = 0;
    while (*t && n < max) {
        while (*t == ' ' || *t == '\t')
            t++;
        if (!*t)
            break;
        if (*t == '"') {
            argv[n++] = ++t;
            while (*t && *t != '"')
                t++;
        } else {
            argv[n++] = t;
            while (*t && *t != ' ' && *t != '\t')
                t++;
        }
        if (*t)
            *t++ = 0;
    }
    return n;
}

/* ---- a command: its exit code, or SH_UNDER_WAY */

static int run_one(struct sh *s, const char *cmd)
{
    char line[SH_LINE], copy[SH_LINE], *argv[32], *gt, target[SH_PATH];
    int argc, r = 0, append = 0;
    while (*cmd == ' ' || *cmd == '\t')
        cmd++;
    expand(s, cmd, line, sizeof line);
    {
        char *e = line + strlen(line);
        while (e > line && (e[-1] == ' ' || e[-1] == '\t'))
            *--e = 0;
    }
    sh_fmt(copy, sizeof copy, "%s", line);
    argc = split_words(copy, argv, 32);
    if (!argc)
        return s->errorlevel;
    if (!is_builtin(s, argv[0]) || strpbrk(line, "|<")) {   /* (a built-in in a pipe, or with its input from a
                                                               file, is the DOS's own until S4) */
        int st = run_program(s, line, 0);
        return st > 0 ? SH_UNDER_WAY : st < 0 ? 1 : 0;
    }
    if ((gt = strchr(line, '>')) != 0) {
        append = gt[1] == '>';                  /* a built-in's output to a file */
        full_path(s, gt + 1 + append + strspn(gt + 1 + append, " "), target, sizeof target);
        {
            char *t = target + strlen(target);
            while (t > target && t[-1] == ' ')
                *--t = 0;
        }
        *gt = 0;
        sh_fmt(copy, sizeof copy, "%s", line);
        argc = split_words(copy, argv, 32);
        if (!(s->redirect = s->h->open(s->h->ctx, target, append ? 'a' : 'w'))) {
            err(s, "Access denied\r\n");
            return 1;
        }
    }
    if (strlen(argv[0]) == 2 && argv[0][1] == ':') {
        int d = upper((unsigned char)argv[0][0]) - 'A';
        char root[4] = { (char)('A' + d), ':', '\\', 0 };
        if (d < 0 || d > 25 || !s->h->is_dir(s->h->ctx, root)) {
            err(s, "Invalid drive specification\r\n");
            r = 1;
        } else {
            s->drive = d;
            if (!s->cwd[d][0])
                sh_fmt(s->cwd[d], sizeof s->cwd[d], "%s", root);
        }
    } else if (same(argv[0], "CD") || same(argv[0], "CHDIR")) {
        r = cmd_cd(s, argc, argv);
    } else if (same(argv[0], "DIR")) {
        r = cmd_dir(s, argc, argv);
    } else if (same(argv[0], "COPY")) {
        r = cmd_copy(s, argc, argv, 0);
    } else if (same(argv[0], "MOVE")) {
        r = cmd_copy(s, argc, argv, 1);
    } else if (same(argv[0], "REN") || same(argv[0], "RENAME")) {
        char a[SH_PATH], b[SH_PATH];
        if (argc < 3) {
            err(s, "Required parameter missing\r\n");
            r = 1;
        } else {
            char d[SH_PATH];
            full_path(s, argv[1], a, sizeof a);
            sh_fmt(d, sizeof d, "%s", a);
            path_up(d);
            path_join(b, sizeof b, d, argv[2]);
            upper_all(b);
            if (s->h->rename(s->h->ctx, a, b) != 0) {
                err(s, "Duplicate file name or file not found\r\n");
                r = 1;
            }
        }
    } else if (same(argv[0], "DEL") || same(argv[0], "ERASE")) {
        r = cmd_del(s, argc, argv);
    } else if (same(argv[0], "MD") || same(argv[0], "MKDIR") || same(argv[0], "RD") || same(argv[0], "RMDIR")) {
        char p[SH_PATH];
        int make = upper((unsigned char)argv[0][0]) == 'M';
        if (argc < 2) {
            err(s, "Required parameter missing\r\n");
            r = 1;
        } else {
            full_path(s, argv[1], p, sizeof p);
            if ((make ? s->h->mkdir(s->h->ctx, p) : s->h->rmdir(s->h->ctx, p)) != 0) {
                err(s, make ? "Unable to create directory\r\n" : "Invalid path, not directory, or directory not empty\r\n");
                r = 1;
            }
        }
    } else if (same(argv[0], "TYPE")) {
        r = cmd_type(s, argc, argv);
    } else if (same(argv[0], "ECHO") || echo_dot(argv[0])) {
        const char *t = line + 4;
        if (*t == '.') {
            say(s, t + 1);
            say(s, "\r\n");
        } else {
            while (*t == ' ')
                t++;
            if (!*t || same(t, "ON") || same(t, "OFF")) {
                if (!*t)
                    say(s, "ECHO is on\r\n");
            } else {
                say(s, t);
                say(s, "\r\n");
            }
        }
    } else if (same(argv[0], "SET")) {
        r = cmd_set(s, argc, argv, line);
    } else if (same(argv[0], "PATH") || same(argv[0], "PROMPT")) {
        const char *t = line + strlen(argv[0]);
        char l[300], name[8];
        sh_fmt(name, sizeof name, "%s", argv[0]);
        upper_all(name);
        while (*t == ' ' || *t == '=')
            t++;
        if (!*t) {
            const char *v = var_get(s, name);
            sh_fmt(l, sizeof l, "%s=%s\r\n", name, v ? v : "");
            say(s, l);
        } else {
            var_set(s, name, t);
        }
    } else if (same(argv[0], "CLS")) {
        s->h->clear(s->h->ctx);
    } else if (same(argv[0], "VER")) {
        say(s, "GLOS shell 1.0 (GLOS API 2.0)\r\n");
    } else if (same(argv[0], "EXIT")) {
        s->h->quit(s->h->ctx, 0);
    } else if (same(argv[0], "HISTORY")) {
        int i;
        char l[SH_LINE + 16];
        for (i = 0; i < s->nhist; i++) {
            sh_fmt(l, sizeof l, "%3d  %s\r\n", i + 1, s->hist[i]);
            say(s, l);
        }
    } else if (same(argv[0], "HELP")) {
        int i;
        for (i = 0; helps[i]; i++) {
            say(s, helps[i]);
            say(s, "\r\n");
        }
        for (i = 0; s->h->extra_help && s->h->extra_help[i]; i++) {
            say(s, s->h->extra_help[i]);
            say(s, "\r\n");
        }
        for (i = 0; helps_end[i]; i++) {
            say(s, helps_end[i]);
            say(s, "\r\n");
        }
    } else if (same(argv[0], "WHERE")) {
        char found[SH_PATH];
        if (argc < 2 || find_program(s, argv[1], found, sizeof found) != 0) {
            err(s, "Not found\r\n");
            r = 1;
        } else {
            say(s, found);
            say(s, "\r\n");
        }
    } else if (same(argv[0], "START")) {
        const char *t = line + 5;
        while (*t == ' ')
            t++;
        r = run_program(s, t, 1) < 0;
    } else {
        r = s->h->extra ? s->h->extra(s->h->ctx, argc, argv) : SH_NOT_MINE;     /* (SH_UNDER_WAY: as a program is) */
        if (r == SH_NOT_MINE)
            r = 1;
    }
    if (s->redirect) {
        s->h->close(s->h->ctx, s->redirect);
        s->redirect = 0;
    }
    return r;
}

/* ---- a line: commands joined by && and || */

static void next_command(struct sh *s)
{
    while (s->rest[0] && !s->under_way) {
        char cmd[SH_LINE];
        const char *a = strstr(s->rest, "&&"), *o = strstr(s->rest, "||"), *cut = a && (!o || a < o) ? a : o;
        int op = cut == a && a ? 1 : cut ? 2 : 0, run, r;
        size_t n = cut ? (size_t)(cut - s->rest) : strlen(s->rest);
        memcpy(cmd, s->rest, n);
        cmd[n] = 0;
        run = s->rest_op == 0 || (s->rest_op == 1 && s->errorlevel == 0) || (s->rest_op == 2 && s->errorlevel != 0);
        memmove(s->rest, cut ? cut + 2 : s->rest + n, strlen(cut ? cut + 2 : s->rest + n) + 1);
        s->rest_op = op;
        if (!run)
            continue;
        r = run_one(s, cmd);
        if (r == SH_UNDER_WAY) {
            s->under_way = 1;                   /* (a program: on when it ends) */
            return;
        }
        s->errorlevel = r;
    }
    if (!s->under_way) {
        s->rest[0] = 0;
        sh_prompt(s);
    }
}

void sh_line(struct sh *s, const char *line)
{
    const char *p = line;
    while (*p == ' ')
        p++;
    if (*p && (!s->nhist || strcmp(s->hist[s->nhist - 1], p))) {
        char *c = copy_of(s, p);
        if (c) {
            if (s->nhist == SH_HISTORY) {
                s->h->free(s->h->ctx, s->hist[0]);
                memmove(s->hist, s->hist + 1, (SH_HISTORY - 1) * sizeof *s->hist);
                s->nhist--;
            }
            s->hist[s->nhist++] = c;
        }
    }
    s->hist_at = s->nhist;
    sh_fmt(s->rest, sizeof s->rest, "%s", p);
    s->rest_op = 0;
    next_command(s);
}

void sh_ended(struct sh *s, int code)
{
    s->under_way = 0;
    s->errorlevel = code;
    next_command(s);
}

/* ---- the prompt, history, completion */

void sh_prompt(struct sh *s)
{
    const char *p = var_get(s, "PROMPT");
    char t[300];
    size_t n = 0;
    if (!p)
        p = "$P$G";
    for (; *p && n < sizeof t - 80; p++) {
        if (*p != '$' || !p[1]) {
            t[n++] = *p;
            continue;
        }
        switch (upper((unsigned char)*++p)) {
        case 'P': n += (size_t)sh_fmt(t + n, (unsigned)(sizeof t - n), "%s", s->cwd[s->drive]); break;
        case 'N': t[n++] = (char)('A' + s->drive); break;
        case 'G': t[n++] = '>'; break;
        case 'L': t[n++] = '<'; break;
        case 'B': t[n++] = '|'; break;
        case 'Q': t[n++] = '='; break;
        case '$': t[n++] = '$'; break;
        case '_': t[n++] = '\r', t[n++] = '\n'; break;
        case 'E': t[n++] = 27; break;
        }
    }
    s->h->write(s->h->ctx, SH_PROMPT, t, (int)n);       /* (the folder: DOS's bytes) */
    s->h->prompted(s->h->ctx);
}

void sh_history(struct sh *s, int by)
{
    if (!s->nhist)
        return;
    s->hist_at += by;
    if (s->hist_at < 0)
        s->hist_at = 0;
    if (s->hist_at >= s->nhist) {
        s->hist_at = s->nhist;
        s->h->input(s->h->ctx, "");
        return;
    }
    s->h->input(s->h->ctx, s->hist[s->hist_at]);
}

void sh_complete(struct sh *s, const char *typed)
{
    char line[256], dir[SH_PATH], pre[64], common[64], full[300];
    const char *word;
    struct sh_dirent *e;
    int n, i, found = 0;
    size_t wl;
    sh_fmt(line, sizeof line, "%s", typed);
    word = strrchr(line, ' ');
    word = word ? word + 1 : line;
    wl = strlen(word);
    {                                           /* the folder it is in, and what of its name is typed */
        const char *slash = strrchr(word, '\\');
        char part[SH_PATH];
        if (slash) {
            sh_fmt(part, sizeof part, "%.*s", (int)(slash - word + 1), word);
            full_path(s, part, dir, sizeof dir);
            sh_fmt(pre, sizeof pre, "%s", slash + 1);
        } else if (wl >= 2 && word[1] == ':') {
            sh_fmt(part, sizeof part, "%.2s", word);
            full_path(s, part, dir, sizeof dir);
            sh_fmt(pre, sizeof pre, "%s", word + 2);
        } else {
            sh_fmt(dir, sizeof dir, "%s", s->cwd[s->drive]);
            sh_fmt(pre, sizeof pre, "%s", word);
        }
    }
    if (word == line && !strpbrk(word, "\\:")) {     /* the first word: a built-in command's name too */
        int k, hits = 0;
        const char *name;
        char only[16];
        for (k = 0; (name = command_name(s, k)) != 0; k++)
            if (wl) {
                size_t j;
                for (j = 0; j < wl && upper((unsigned char)word[j]) == name[j]; j++)
                    ;
                if (j == wl && !hits++)
                    sh_fmt(only, sizeof only, "%s", name);
            }
        if (hits == 1) {
            sh_fmt(full, sizeof full, "%s ", only);
            s->h->input(s->h->ctx, full);
            return;
        }
    }
    if ((n = s->h->dir_read(s->h->ctx, dir, &e)) <= 0) {
        if (!n)
            s->h->dir_free(s->h->ctx, e);
        return;
    }
    common[0] = 0;
    for (i = 0; i < n; i++) {
        size_t k;
        for (k = 0; pre[k] && upper((unsigned char)pre[k]) == upper((unsigned char)e[i].name[k]); k++)
            ;
        if (pre[k])
            continue;
        if (!found++) {
            sh_fmt(common, sizeof common, "%s%s", e[i].name, e[i].dir ? "\\" : "");
        } else {
            for (k = 0; common[k] && upper((unsigned char)common[k]) == upper((unsigned char)e[i].name[k]); k++)
                ;
            common[k] = 0;
        }
    }
    if (found > 1 && strlen(common) == strlen(pre)) {  /* several, and nothing more in common: they're listed */
        out(s, "\r\n");
        for (i = 0; i < n; i++) {
            size_t k;
            for (k = 0; pre[k] && upper((unsigned char)pre[k]) == upper((unsigned char)e[i].name[k]); k++)
                ;
            if (!pre[k]) {
                sh_fmt(full, sizeof full, e[i].dir ? "%s\\  " : "%s  ", e[i].name);
                out(s, full);
            }
        }
        out(s, "\r\n");
        s->h->dir_free(s->h->ctx, e);
        sh_fmt(full, sizeof full, "%s", line);
        sh_prompt(s);
        s->h->input(s->h->ctx, full);
        return;
    }
    s->h->dir_free(s->h->ctx, e);
    if (found) {
        size_t keep = (size_t)(word - line) + wl - strlen(pre);
        sh_fmt(full, sizeof full, "%.*s%s", (int)keep, line, common);
        s->h->input(s->h->ctx, full);
    }
}

void sh_start(struct sh *s, const struct sh_host *h, const char *const *env)
{
    memset(s, 0, sizeof *s);
    s->h = h;
    s->drive = 2;
    for (; env && *env && s->nvars < SH_VARS; env++)
        if (strchr(*env, '=') && (s->vars[s->nvars] = copy_of(s, *env)) != 0)
            s->changed[s->nvars++] = 0;
    sh_fmt(s->cwd[2], sizeof s->cwd[2], "C:\\");
    out(s, "GLOS shell 1.0. HELP lists its commands.\r\n\r\n");
}
