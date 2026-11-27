/* GLOS's shell (shell.h; docs/v1/shell.md; PRD D50): DOS's commands and syntax, modernised.
 *
 * Built in: CD (CHDIR), a drive's letter, DIR [/W] [/B], COPY, MOVE, XCOPY, REN (RENAME), DEL (ERASE), DELTREE, CHOICE,
 * MD (MKDIR), RD (RMDIR), TYPE, MORE, FIND, SORT, FC, COMP, ECHO, SET, PATH, PROMPT, CLS, VER, EXIT, HISTORY, HELP, WHERE, START, and
 * what the host adds. A built-in's output goes to a file with > or >>.
 *
 * Anything else is a program: found here and along PATH (.COM, .EXE, .BAT) and run by the host, in this
 * shell's folder, with the variables this shell has set. A .BAT file is run here, a line at a time (S4a: IF,
 * GOTO, CALL, FOR, SHIFT, PAUSE, %0 to %9). < and > around a program name the files of its standard input and
 * output, and a pipe is made steps with a temporary file between them, as DOS does (S4b).
 *
 * %NAME% is a variable; %ERRORLEVEL% the last exit code. && runs what follows only after a success (exit code
 * 0), || only after a failure.
 *
 * The shell keeps its own folder for each drive and resolves paths itself, so every host's files see full
 * paths and the tests see what DOS does. */
#include <stdarg.h>
#include <string.h>

#include "glos/api.h"
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
    if (s->discard)
        return;
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

/* %NAME% expanded; %% is %. In a batch file %0 to %9 are its arguments, and a name that is not a variable's
   is nothing; at the prompt such a name stays as it was typed (FOR %A IN (...) DO ... %A), as under MS-DOS. */
static void expand(const struct sh *s, const char *in, char *outb, size_t room)
{
    const struct sh_batch *b = s->nbat ? s->bat[s->nbat - 1] : 0;
    size_t n = 0;
    while (*in && n < room - 1) {
        if (*in == '%') {
            const char *e;
            if (in[1] == '%') {
                outb[n++] = '%';
                in += 2;
                continue;
            }
            if (b && in[1] >= '0' && in[1] <= '9') {
                int k = b->shift + (in[1] - '0');
                const char *v = k < b->argc ? b->arg[k] : "";
                while (*v && n < room - 1)
                    outb[n++] = *v++;
                in += 2;
                continue;
            }
            if ((e = strchr(in + 1, '%')) != 0) {
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
                if (v || b) {
                    while (v && *v && n < room - 1)
                        outb[n++] = *v++;
                    in = e + 1;
                    continue;
                }
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

/* name matches a DOS pattern (* and ?); as in DOS, ".*" at the pattern's end takes a name with no dot too ("*.*"
   is every name, "A*.*" every one beginning with A) */
static int match(const char *pat, const char *name)
{
    for (; *pat; pat++, name++) {
        if (pat[0] == '.' && pat[1] == '*' && !pat[2] && !*name)
            return 1;
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

/* DOS's switches among a command's words (/Y, /-Y, /V ...): the ones in `known` (in capitals, between
   spaces) are taken out, as answered or as nothing this shell does otherwise;
   any other is said to be invalid, as DOS says it, and the command is not run. The words left: how many, or
   -1. */
static int switches(struct sh *s, int argc, char **argv, const char *known)
{
    int i, n = 1;
    for (i = 1; i < argc; i++) {
        const char *k = known;
        int ok = 0;
        if (argv[i][0] != '/' || !argv[i][1]) {
            argv[n++] = argv[i];
            continue;
        }
        while (*k && !ok) {                     /* (NAME: that switch; NAME*: with anything after it, as /O:N) */
            const char *a = argv[i] + 1;
            for (; *k && *k != ' ' && *k != '*' && upper((unsigned char)*a) == *k; k++, a++)
                ;
            ok = *k == '*' || ((!*k || *k == ' ') && !*a);
            while (*k && *k != ' ')
                k++;
            while (*k == ' ')
                k++;
        }
        if (!ok) {
            char line[80];
            sh_fmt(line, sizeof line, "Invalid switch - %s\r\n", argv[i]);
            err(s, line);
            return -1;
        }
    }
    return n;
}

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
    if ((argc = switches(s, argc, argv, "W B O* P -P L")) < 0)   /* (/O: by name, as it is; /P, /L: nothing) */
        return 1;
    if (argc > 1)
        arg = argv[argc - 1];
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

/* A size in KB as people read it: "720 KB", "347 MB", "16.1 GB". */
static void kb_text(unsigned long kb, char *b, size_t room)
{
    if (kb < 10240)
        sh_fmt(b, (unsigned)room, "%lu KB", kb);
    else if (kb < 10485760ul)
        sh_fmt(b, (unsigned)room, "%lu MB", kb >> 10);
    else
        sh_fmt(b, (unsigned)room, "%lu.%lu GB", kb >> 20, ((kb >> 10) & 1023) * 10 / 1024);
}

/* DF [drive ...]: each drive's volume, its size, what is used and free. With no drive named: the hard disks'
   and whatever else has a letter from C: on (a floppy is asked for by name: reading an empty drive takes its
   time). */
static int cmd_df(struct sh *s, int argc, char **argv)
{
    char line[120], label[12], a[16], b[16], c[16];
    unsigned long total, left, used, pct;
    int i, d, shown = 0, bad = 0;
    if ((argc = switches(s, argc, argv, "")) < 0)
        return 1;
    if (!s->h->space) {
        err(s, "DF: not here\r\n");
        return 1;
    }
    for (i = 1; i < argc; i++)
        if (!argv[i][0] || upper((unsigned char)argv[i][0]) < 'A' || upper((unsigned char)argv[i][0]) > 'Z'
            || (argv[i][1] && (argv[i][1] != ':' || (argv[i][2] && (argv[i][2] != '\\' || argv[i][3]))))) {
            err(s, "DF [drive: ...]\r\n");
            return 1;
        }
    for (i = argc > 1 ? 1 : 2; argc > 1 ? i < argc : i < 26; i++) {
        d = argc > 1 ? upper((unsigned char)argv[i][0]) - 'A' : i;
        label[0] = 0;
        if (s->h->space(s->h->ctx, d, &total, &left, label) != 0) {
            if (argc > 1) {
                sh_fmt(line, sizeof line, "%c:     no such drive, or nothing in it\r\n", 'A' + d);
                err(s, line);
                bad = 1;
            }
            continue;
        }
        if (!shown++)
            say(s, "Drive  Volume            Size        Used        Free  Used\r\n");
        if (left > total)
            left = total;
        kb_text(total, a, sizeof a);
        kb_text(total - left, b, sizeof b);
        kb_text(left, c, sizeof c);
        used = total - left;                    /* (in hundredths of the whole where a product would not fit) */
        pct = !total ? 0 : total >= 42949672ul ? used / (total / 100) : used * 100 / total;
        sh_fmt(line, sizeof line, "%c:     %-11s %10s  %10s  %10s  %3lu%%\r\n", 'A' + d, label, a, b, c,
               pct > 100 ? 100ul : pct);
        say(s, line);
    }
    if (!shown && argc <= 1)
        err(s, "DF: no drive from C: on\r\n");
    return shown && !bad ? 0 : 1;
}

/* DU [folder] [/S]: what a folder's files take, with everything under it: each folder in it, then the whole
   (/S: the whole alone). Sizes are the files' own, in KB (not the clusters they take). */
struct du {
    unsigned long kb, rest, files;
    int failed;
};

/* name after the folder in path (len its length; the caller has seen to the room). */
static void du_join(char *path, size_t len, const char *name)
{
    if (len && path[len - 1] != '\\')
        path[len++] = '\\';
    strcpy(path + len, name);
}

static void du_walk(struct sh *s, char *path, int depth, struct du *t)
{
    struct sh_dirent *e;
    size_t len = strlen(path);
    int n, i;
    if ((n = s->h->dir_read(s->h->ctx, path, &e)) < 0) {
        t->failed = 1;
        return;
    }
    for (i = 0; i < n; i++) {
        if (!e[i].dir) {
            t->kb += e[i].size >> 10;
            t->rest += e[i].size & 1023;
            t->kb += t->rest >> 10, t->rest &= 1023;
            t->files++;
        } else if (depth < 32 && len + strlen(e[i].name) + 2 < SH_PATH) {
            du_join(path, len, e[i].name);
            du_walk(s, path, depth + 1, t);
            path[len] = 0;
        }
    }
    s->h->dir_free(s->h->ctx, e);
}

static void du_line(struct sh *s, const struct du *t, const char *path)
{
    char line[SH_PATH + 40], sz[24];
    size_text(t->kb + (t->rest ? 1 : 0), sz, sizeof sz);
    sh_fmt(line, sizeof line, "%12s KB  %s\r\n", sz, path);
    say(s, line);
}

static int cmd_du(struct sh *s, int argc, char **argv)
{
    char path[SH_PATH], line[SH_PATH + 40], sz[24];
    struct sh_dirent *e;
    struct du all, one;
    size_t len;
    int n, i, summary = 0, failed = 0;
    for (i = 1; i < argc; i++)
        if (same(argv[i], "/S"))
            summary = 1;
    if ((argc = switches(s, argc, argv, "S")) < 0)
        return 1;
    full_path(s, argc > 1 ? argv[1] : ".", path, sizeof path);
    if (argc > 2 || !s->h->is_dir(s->h->ctx, path) || (n = s->h->dir_read(s->h->ctx, path, &e)) < 0) {
        err(s, argc > 2 ? "DU [folder] [/S]\r\n" : "DU: no such folder\r\n");
        return 1;
    }
    memset(&all, 0, sizeof all);
    len = strlen(path);
    for (i = 0; i < n; i++) {
        if (!e[i].dir) {
            all.kb += e[i].size >> 10;
            all.rest += e[i].size & 1023;
            all.kb += all.rest >> 10, all.rest &= 1023;
            all.files++;
            continue;
        }
        if (len + strlen(e[i].name) + 2 >= SH_PATH)
            continue;
        memset(&one, 0, sizeof one);
        du_join(path, len, e[i].name);
        du_walk(s, path, 1, &one);
        if (!summary)
            du_line(s, &one, path);
        path[len] = 0;
        all.kb += one.kb, all.rest += one.rest, all.files += one.files;
        all.kb += all.rest >> 10, all.rest &= 1023;
        failed |= one.failed;
    }
    s->h->dir_free(s->h->ctx, e);
    du_line(s, &all, path);
    size_text(all.files, sz, sizeof sz);
    sh_fmt(line, sizeof line, "%12s file(s)\r\n", sz);
    say(s, line);
    if (failed)
        err(s, "DU: a folder could not be read\r\n");
    return failed;
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
    if ((argc = switches(s, argc, argv, "Y -Y V A B")) < 0)     /* (what is there is written over, asked or not) */
        return 1;
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
    int n, i, done = 0, found = 0;
    if ((argc = switches(s, argc, argv, "Q")) < 0)      /* (it never asks) */
        return 1;
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
            found++;
            path_join(p, sizeof p, dir, e[i].name);
            if (s->h->unlink(s->h->ctx, p) == 0)
                done++;
        }
    s->h->dir_free(s->h->ctx, e);
    if (!done) {                                /* (there, but not deleted: read-only, as MS-DOS says it) */
        err(s, found ? "Access denied\r\n" : "File not found\r\n");
        return 1;
    }
    sh_fmt(line, sizeof line, "%d file(s) deleted\r\n", done);
    say(s, line);
    return 0;
}

/* ATTRIB [+R|-R] [+A|-A] [+S|-S] [+H|-H] [path] (2026-11-24, for a native GLOS's GLOSLDR.SYS, read-only, hidden
   and system, put back over SSH): the attributes changed, or with none asked, listed as MS-DOS 7.1's ATTRIB lists
   them (asked of the real one on the reference disk): A in the third column, S, H and R in the sixth to the
   eighth, the name padded to fourteen from the fourteenth, then the path in full. A pattern (or no path: *.*
   where one is) takes files; a folder named alone is shown or changed itself. A change says nothing. */
static int cmd_attrib(struct sh *s, int argc, char **argv)
{
    static const char letters[] = "RHSA";
    static const unsigned bits[] = { 0x01, 0x02, 0x04, 0x20 };
    char dir[SH_PATH], pat[64], p[SH_PATH], line[SH_PATH + 40];
    const char *path = 0;
    struct sh_dirent one, *e = 0;
    unsigned set = 0, clr = 0;
    int i, n, done = 0, alone = 0, r = 0;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if ((a[0] == '+' || a[0] == '-') && a[1] && !a[2] && strchr(letters, upper((unsigned char)a[1]))) {
            unsigned b = bits[strchr(letters, upper((unsigned char)a[1])) - letters];
            if (a[0] == '+')
                set |= b, clr &= ~b;
            else
                clr |= b, set &= ~b;
        } else if (a[0] == '/') {
            sh_fmt(line, sizeof line, "Invalid switch - %s\r\n", a);
            err(s, line);
            return 1;
        } else if (a[0] == '+' || a[0] == '-' || path) {
            sh_fmt(line, sizeof line, "Invalid parameter - %s\r\n", a);
            err(s, line);
            return 1;
        } else {
            path = a;
        }
    }
    if ((set || clr) && !s->h->setattr) {
        err(s, "ATTRIB can't change attributes here\r\n");
        return 1;
    }
    full_path(s, path ? path : "*.*", p, sizeof p);
    if (path && !strpbrk(path, "*?") && s->h->is_dir(s->h->ctx, p) && s->h->stat(s->h->ctx, p, &one) == 0) {
        alone = 1, n = 1;                       /* (a folder named alone: itself) */
        e = &one;
        sh_fmt(dir, sizeof dir, "%s", p);
        path_up(dir);
    } else {
        sh_fmt(pat, sizeof pat, "%s", path_name(p));
        sh_fmt(dir, sizeof dir, "%s", p);
        path_up(dir);
        if ((n = s->h->dir_read(s->h->ctx, dir, &e)) < 0)
            n = 0, e = 0;
    }
    for (i = 0; i < n; i++) {
        char full[SH_PATH];
        unsigned a = e[i].attr;
        if (!alone && (e[i].dir || !match(pat, e[i].name)))
            continue;
        done++;
        path_join(full, sizeof full, dir, e[i].name);
        if (alone)
            sh_fmt(full, sizeof full, "%s", p);
        if (set || clr) {
            if (s->h->setattr(s->h->ctx, full, ((a & 0x27) & ~clr) | set) != 0) {
                sh_fmt(line, sizeof line, "Access denied - %s\r\n", full);
                err(s, line);
                r = 1;
            }
            continue;
        }
        sh_fmt(line, sizeof line, "  %c  %c%c%c     %-14s%s\r\n", a & 0x20 ? 'A' : ' ', a & 0x04 ? 'S' : ' ',
               a & 0x02 ? 'H' : ' ', a & 0x01 ? 'R' : ' ', e[i].name, full);
        say(s, line);
    }
    if (e && !alone)
        s->h->dir_free(s->h->ctx, e);
    if (!done) {
        sh_fmt(line, sizeof line, "File not found - %s\r\n", p);
        err(s, line);
        return 1;
    }
    return r;
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

/* ---- XCOPY, MORE and DELTREE (2026-11-25: phase G, a native GLOS has no FreeDOS to bring them) */

/* Each folder of path made that is not there yet: 0, or -1. */
static int make_dirs(struct sh *s, const char *path)
{
    char p[SH_PATH];
    size_t i, n = strlen(path);
    if (n >= sizeof p)
        return -1;
    for (i = 3; i <= n; i++) {                  /* (after "C:\") */
        if (i < n && path[i] != '\\')
            continue;
        memcpy(p, path, i);
        p[i] = 0;
        if (!s->h->is_dir(s->h->ctx, p) && s->h->mkdir(s->h->ctx, p) != 0)
            return -1;
    }
    return 0;
}

/* A date as DOS's word ((year - 1980) << 9 | month << 5 | day) from "m-d-y" (or / or . between): 0 if it isn't one. */
static unsigned dos_date(const char *t)
{
    unsigned v[3] = { 0, 0, 0 }, k = 0;
    for (; *t && k < 3; t++) {
        if (*t >= '0' && *t <= '9')
            v[k] = v[k] * 10 + (unsigned)(*t - '0');
        else if (*t == '-' || *t == '/' || *t == '.')
            k++;
        else
            return 0;
    }
    if (k != 2 || v[0] < 1 || v[0] > 12 || v[1] < 1 || v[1] > 31)
        return 0;
    if (v[2] < 100)
        v[2] += v[2] < 80 ? 2000 : 1900;
    if (v[2] < 1980 || v[2] > 2107)
        return 0;
    return (v[2] - 1980) << 9 | v[0] << 5 | v[1];
}

/* XCOPY's switches: MS-DOS 7.1's (/A /M /D:date /S /E /V; /P and /W, which ask, are not taken) and some of later
   Windows' (/I /H /Q /F /L /T /C /R /K, /D alone, /Y and /-Y). */
#define XC_S 0x0001                     /* folders in it too (/E: empty ones as well) */
#define XC_E 0x0002
#define XC_A 0x0004                     /* only files to be archived (/M: and then no longer) */
#define XC_M 0x0008
#define XC_H 0x0010                     /* hidden and system files (and folders) too */
#define XC_Q 0x0020                     /* no names shown */
#define XC_F 0x0040                     /* the names in full, from and to */
#define XC_L 0x0080                     /* listed, not copied */
#define XC_T 0x0100                     /* the folders alone */
#define XC_C 0x0200                     /* on after a file that could not be copied */
#define XC_R 0x0400                     /* read-only files written over */
#define XC_K 0x0800                     /* the attributes kept */
#define XC_I 0x1000                     /* a destination not there is a folder */
#define XC_NEWER 0x2000                 /* /D alone: only files newer than the destination's */

struct xc {
    unsigned flags, since;              /* since: /D:date's (DOS's word), or 0 */
    const char *pat, *typed;            /* the files' pattern; the source's folder as typed (for the names shown) */
    char rel[SH_PATH];                  /* the folder under the source's being copied ("SUB\DEEP\") */
    int files, found, failed, itself, said;
};

/* One folder's files (and, with /S, its folders'), from src to dst (both changed and put back as they were). A
   file to a file (not a folder): dst is its name and to_file 1. -1: stopped (a file that could not be copied). */
static int xc_walk(struct sh *s, struct xc *x, char *src, char *dst, int to_file, int depth)
{
    struct sh_dirent *e, d;
    char from[SH_PATH], to[SH_PATH], line[2 * SH_PATH + 16];
    size_t sl = strlen(src), dl = strlen(dst), rl = strlen(x->rel);
    int n, i, made = to_file || s->h->is_dir(s->h->ctx, dst), r = 0;
    if ((n = s->h->dir_read(s->h->ctx, src, &e)) < 0) {
        sh_fmt(line, sizeof line, "Invalid path - %s\r\n", src);
        err(s, line);
        x->failed++;
        return x->flags & XC_C ? 0 : -1;
    }
    if (!made && (x->flags & (XC_E | XC_L)) == XC_E && make_dirs(s, dst) == 0)
        made = 1;                               /* (/E: every folder, files in it or not) */
    for (i = 0; i < n && r == 0; i++) {
        if (e[i].dir || !match(x->pat, e[i].name) || ((e[i].attr & 0x06) && !(x->flags & XC_H))
            || ((x->flags & (XC_A | XC_M)) && !(e[i].attr & 0x20)) || (x->since && e[i].date < x->since))
            continue;
        x->found++;
        path_join(from, sizeof from, src, e[i].name);
        if (to_file)
            sh_fmt(to, sizeof to, "%s", dst);
        else
            path_join(to, sizeof to, dst, e[i].name);
        if ((x->flags & XC_NEWER) && s->h->stat(s->h->ctx, to, &d) == 0
            && ((unsigned long)d.date << 16 | d.time) >= ((unsigned long)e[i].date << 16 | e[i].time))
            continue;
        if (!made && !(x->flags & XC_L)) {
            if (make_dirs(s, dst) != 0) {
                sh_fmt(line, sizeof line, "Unable to create directory - %s\r\n", dst);
                err(s, line);
                x->failed++;
                r = x->flags & XC_C ? 0 : -1;
                break;
            }
            made = 1;
        }
        if (x->flags & XC_T)                    /* (the folder, made: no file) */
            continue;
        if (same(from, to)) {
            err(s, "File cannot be copied onto itself\r\n");
            x->itself++;
            continue;
        }
        if (!x->said && !(x->flags & (XC_Q | XC_L))) {
            say(s, "Reading source file(s)...\r\n");
            x->said = 1;
        }
        if (!(x->flags & XC_L)) {
            if ((x->flags & XC_R) && s->h->setattr && s->h->stat(s->h->ctx, to, &d) == 0 && (d.attr & 0x01))
                s->h->setattr(s->h->ctx, to, d.attr & 0x26u);
            if (copy_file(s, from, to) != 0) {
                sh_fmt(line, sizeof line, "File creation error - %s%s%s\r\n", x->typed, x->rel, e[i].name);
                err(s, line);
                x->failed++;
                r = x->flags & XC_C ? 0 : -1;
                continue;
            }
            if ((x->flags & XC_K) && s->h->setattr)
                s->h->setattr(s->h->ctx, to, (e[i].attr & 0x27u) | 0x20u);
            if ((x->flags & XC_M) && s->h->setattr)
                s->h->setattr(s->h->ctx, from, e[i].attr & 0x07u);
        }
        x->files++;
        if (x->flags & XC_F)
            sh_fmt(line, sizeof line, "%s -> %s\r\n", from, to);
        else
            sh_fmt(line, sizeof line, "%s%s%s\r\n", x->typed, x->rel, e[i].name);
        if (!(x->flags & XC_Q))
            say(s, line);
    }
    for (i = 0; i < n && r == 0 && (x->flags & XC_S) && depth < 32; i++) {
        if (!e[i].dir || ((e[i].attr & 0x06) && !(x->flags & XC_H)) || sl + strlen(e[i].name) + 2 >= SH_PATH
            || dl + strlen(e[i].name) + 2 >= SH_PATH || rl + strlen(e[i].name) + 2 >= SH_PATH)
            continue;
        du_join(src, sl, e[i].name);
        du_join(dst, dl, e[i].name);
        sh_fmt(x->rel + rl, (unsigned)(sizeof x->rel - rl), "%s\\", e[i].name);
        r = xc_walk(s, x, src, dst, 0, depth + 1);
        src[sl] = 0, dst[dl] = 0, x->rel[rl] = 0;
    }
    s->h->dir_free(s->h->ctx, e);
    return r;
}

/* XCOPY source [destination] [switches]: files, and with /S the folders under them, copied. As MS-DOS 7.1's
   (asked of the real one on the reference disk): "Reading source file(s)...", each file named as the source was
   typed, "N File(s) copied"; exit codes 0, 1 (no file found), 4 (a mistake in what was asked: a switch, the
   number of names, a drive, a path, a copy into itself), 5 (a file not written). It never asks: a destination
   that is there is written over (as COPY does here), and one that is not is a folder unless a single file is
   copied to a name (where MS-DOS asks "file or directory?"). */
static int cmd_xcopy(struct sh *s, int argc, char **argv)
{
    static const char letters[] = "SEAMHQFLTCRKI";
    static const unsigned bits[] = { XC_S, XC_E, XC_A, XC_M, XC_H, XC_Q, XC_F, XC_L, XC_T, XC_C, XC_R, XC_K, XC_I };
    char src[SH_PATH], dst[SH_PATH], typed[SH_PATH], pat[64], root[4], line[SH_PATH + 40];
    const char *names[2] = { 0, 0 };
    struct xc x;
    int i, n = 0, folder, wild, to_file, r;
    memset(&x, 0, sizeof x);
    for (i = 1; i < argc; i++) {
        const char *a = argv[i], *k;
        if (a[0] != '/') {
            if (n == 2) {
                err(s, "Invalid number of parameters\r\n");
                return 4;
            }
            names[n++] = a;
            continue;
        }
        if (upper((unsigned char)a[1]) == 'D' && (!a[2] || a[2] == ':')) {
            if (!a[2]) {
                x.flags |= XC_NEWER;
            } else if (!(x.since = dos_date(a + 3))) {
                err(s, "Invalid date\r\n");
                return 4;
            }
        } else if (a[1] && !a[2] && (k = strchr(letters, upper((unsigned char)a[1]))) != 0) {
            x.flags |= bits[k - letters];
        } else if (!same(a, "/V") && !same(a, "/Y") && !same(a, "/-Y")) {      /* (verified, asked: never) */
            sh_fmt(line, sizeof line, "Invalid switch - %s\r\n", a);
            err(s, line);
            return 4;
        }
    }
    if (!n) {
        err(s, "Invalid number of parameters\r\n");
        return 4;
    }
    if (x.flags & XC_E)
        x.flags |= XC_S;
    full_path(s, names[0], src, sizeof src);
    full_path(s, names[1] ? names[1] : ".", dst, sizeof dst);
    for (i = 0; i < 2; i++) {
        const char *p = i ? dst : src;
        sh_fmt(root, sizeof root, "%c:\\", p[0]);
        if (!s->h->is_dir(s->h->ctx, root)) {
            err(s, "Invalid drive specification\r\n");
            return 4;
        }
    }
    wild = strpbrk(names[0], "*?") != 0;
    folder = !wild && s->h->is_dir(s->h->ctx, src);
    sh_fmt(typed, sizeof typed, "%s", names[0]);
    upper_all(typed);
    if (folder) {
        size_t k = strlen(typed);
        sh_fmt(pat, sizeof pat, "*.*");
        if (k && typed[k - 1] != '\\' && typed[k - 1] != ':')
            sh_fmt(typed + k, (unsigned)(sizeof typed - k), "\\");
    } else {
        char *cut = strrchr(typed, '\\');
        sh_fmt(pat, sizeof pat, "%s", path_name(src));
        path_up(src);
        if (!cut)
            cut = typed[0] && typed[1] == ':' ? typed + 1 : 0;
        if (cut)
            cut[1] = 0;
        else
            typed[0] = 0;
        if (!s->h->is_dir(s->h->ctx, src)) {
            err(s, "Invalid path\r\n");
            return 4;
        }
    }
    {
        size_t k = strlen(names[1] ? names[1] : "");
        int dir_named = k && (names[1][k - 1] == '\\' || names[1][k - 1] == ':');
        to_file = names[1] && !folder && !wild && !(x.flags & XC_S) && !dir_named && !s->h->is_dir(s->h->ctx, dst);
    }
    if (!to_file && (x.flags & XC_S)) {         /* (into itself: it would never end) */
        size_t k = strlen(src);
        if (!strncmp(dst, src, k) && (!dst[k] || dst[k] == '\\' || src[k - 1] == '\\')) {
            err(s, "Cannot perform a cyclic copy\r\n");
            return 4;
        }
    }
    x.pat = pat, x.typed = typed;
    r = xc_walk(s, &x, src, dst, to_file, 0);
    if (!x.found && !x.failed && !(x.flags & XC_T) && !folder) {
        sh_fmt(line, sizeof line, "File not found - %s\r\n", pat);
        err(s, line);
    }
    if (!(x.flags & XC_T)) {
        sh_fmt(line, sizeof line, x.flags & XC_L ? "%9d File(s)\r\n" : "%9d File(s) copied\r\n", x.files);
        say(s, line);
    }
    return r < 0 || x.failed ? 5 : x.itself && !x.files ? 4 : x.files || (x.flags & XC_T) ? 0 : 1;
}

/* MORE [file], MORE < file, command | MORE: a screen's rows at a time (24, the 25th for "-- More --"), any key the
   next (Ctrl+C, Esc or Q: no more). Tabs to every eighth column; long lines counted as the rows they take on an
   80-column screen; Ctrl+Z ends the text. With its output in a file, or nobody at a keyboard, all at once. */
#define MORE_ROWS 24
#define MORE_COLS 80

/* From tool.pos: a page (all: the rest) written. 1 if more is left, 0 at the end, -1 if the file is gone. */
static int more_page(struct sh *s, int all)
{
    char in[512], o[600];
    void *f = s->h->open(s->h->ctx, s->tool.path, 'r');
    unsigned long at = 0;
    int got, i, rows = 0, col = 0, k = 0, end = 0, left = 0;
    if (!f)
        return -1;
    if (s->tool.pos && (!s->h->seek || s->h->seek(s->h->ctx, f, s->tool.pos) != 0)) {
        while (at < s->tool.pos && (got = s->h->read(s->h->ctx, f, in,
                                                      s->tool.pos - at < sizeof in ? (int)(s->tool.pos - at) : (int)sizeof in)) > 0)
            at += (unsigned long)got;
    }
    while (!end && (got = s->h->read(s->h->ctx, f, in, sizeof in)) > 0) {
        for (i = 0; i < got; i++) {
            unsigned char c = (unsigned char)in[i];
            if (!all && rows == MORE_ROWS) {    /* (a page: the rest is the next's) */
                left = c != 0x1A;
                end = 1;
                break;
            }
            s->tool.pos++;
            if (c == 0x1A) {
                end = 2;
                break;
            }
            if (c == '\n') {
                rows++, col = 0;
                o[k++] = (char)c;
            } else if (c == '\r') {
                col = 0;
                o[k++] = (char)c;
            } else if (c == '\t') {
                do {
                    if (col == MORE_COLS)
                        rows++, col = 0;
                    o[k++] = ' ', col++;
                } while (col % 8);
            } else {
                if (col == MORE_COLS)           /* (the screen goes on to the next row before it) */
                    rows++, col = 0;
                o[k++] = (char)c, col++;
            }
            if (k > (int)sizeof o - 16) {
                if (s->redirect)
                    s->h->put(s->h->ctx, s->redirect, o, k);
                else if (!s->discard)
                    s->h->write(s->h->ctx, SH_TEXT, o, k);
                k = 0;
            }
        }
    }
    if (k) {
        if (s->redirect)
            s->h->put(s->h->ctx, s->redirect, o, k);
        else if (!s->discard)
            s->h->write(s->h->ctx, SH_TEXT, o, k);
    }
    s->h->close(s->h->ctx, f);
    return left;
}

static int more_step(struct sh *s, int key)
{
    if (key == 3 || key == 27 || upper(key) == 'Q') {
        out(s, "\r\n");
        return 0;
    }
    out(s, "\r\n");
    for (;;) {
        int r = more_page(s, 0);
        if (r <= 0)
            return r < 0;
        out(s, "-- More --");
        if (s->h->key && s->h->key(s->h->ctx) == SH_UNDER_WAY)
            return SH_UNDER_WAY;
        out(s, "\r\n");                         /* (nobody to press a key: on) */
    }
}

static int cmd_more(struct sh *s, int argc, char **argv, const char *source)
{
    char line[SH_PATH + 40];
    void *f;
    if ((argc = switches(s, argc, argv, "")) < 0)
        return 1;
    if (argc > 1)
        full_path(s, argv[1], s->tool.path, sizeof s->tool.path);
    else if (source && source[0] && !same(source, "NUL"))
        sh_fmt(s->tool.path, sizeof s->tool.path, "%s", source);
    else if (source && same(source, "NUL"))
        return 0;
    else {
        err(s, "MORE file, MORE < file, or command | MORE\r\n");
        return 1;
    }
    if (!(f = s->h->open(s->h->ctx, s->tool.path, 'r'))) {
        sh_fmt(line, sizeof line, "File not found - %s\r\n", argc > 1 ? argv[1] : s->tool.path);
        err(s, line);
        return 1;
    }
    s->h->close(s->h->ctx, f);
    s->tool.pos = 0;
    if (s->redirect || s->discard || !s->h->key)
        return more_page(s, 1) < 0;
    for (;;) {
        int r = more_page(s, 0);
        if (r <= 0)
            return r < 0;
        out(s, "-- More --");
        if (s->h->key(s->h->ctx) == SH_UNDER_WAY) {
            s->tool.step = more_step;
            s->key_for = 3;
            return SH_UNDER_WAY;
        }
        out(s, "\r\n");
    }
}

/* A file, or a folder with everything in it, deleted (read-only, hidden and system files too): 0, or -1. A folder's
   files go, then it goes into its first folder, until one has none; that one is removed, and so up again. */
static int tree_delete(struct sh *s, const char *top)
{
    char p[SH_PATH], f[SH_PATH];
    struct sh_dirent one, *e;
    int n, i, sub;
    unsigned long turns = 0;
    if (s->h->stat(s->h->ctx, top, &one) != 0)
        return -1;
    if (!one.dir) {
        if ((one.attr & 0x07) && s->h->setattr)
            s->h->setattr(s->h->ctx, top, one.attr & 0x20u);
        return s->h->unlink(s->h->ctx, top);
    }
    sh_fmt(p, sizeof p, "%s", top);
    while (++turns < 1000000ul) {
        if ((n = s->h->dir_read(s->h->ctx, p, &e)) < 0)
            return -1;
        for (i = 0, sub = -1; i < n; i++) {
            if (e[i].dir) {
                if (sub < 0)
                    sub = i;
                continue;
            }
            path_join(f, sizeof f, p, e[i].name);
            if ((e[i].attr & 0x07) && s->h->setattr)
                s->h->setattr(s->h->ctx, f, e[i].attr & 0x20u);
            if (s->h->unlink(s->h->ctx, f) != 0) {
                s->h->dir_free(s->h->ctx, e);
                return -1;
            }
        }
        if (sub >= 0) {
            if (strlen(p) + strlen(e[sub].name) + 2 >= sizeof p) {
                s->h->dir_free(s->h->ctx, e);
                return -1;
            }
            path_join(f, sizeof f, p, e[sub].name);
            s->h->dir_free(s->h->ctx, e);
            sh_fmt(p, sizeof p, "%s", f);
            continue;
        }
        s->h->dir_free(s->h->ctx, e);
        if (s->h->stat(s->h->ctx, p, &one) == 0 && (one.attr & 0x07) && s->h->setattr)
            s->h->setattr(s->h->ctx, p, 0);
        if (s->h->rmdir(s->h->ctx, p) != 0)
            return -1;
        if (same(p, top))
            return 0;
        path_up(p);
    }
    return -1;
}

/* The next one DELTREE is to ask about of a (a path: itself, once; a pattern: its matches in the host's order,
   folders first and then by name, after tool.last): 1 with tool.path, tool.shown and tool.last set, or 0. */
static int deltree_find(struct sh *s, const char *a, int *isdir)
{
    char full[SH_PATH], dir[SH_PATH];
    struct sh_dirent one, *e;
    const char *cut;
    int n, i, found = 0;
    full_path(s, a, full, sizeof full);
    if (!strpbrk(a, "*?")) {
        if (s->tool.last[0] || s->h->stat(s->h->ctx, full, &one) != 0)
            return 0;                           /* (done, or not there: nothing said, as MS-DOS says nothing) */
        sh_fmt(s->tool.path, sizeof s->tool.path, "%s", full);
        sh_fmt(s->tool.shown, sizeof s->tool.shown, "%s", a);
        upper_all(s->tool.shown);
        sh_fmt(s->tool.last, sizeof s->tool.last, "%s", "?");
        *isdir = one.dir;
        return 1;
    }
    sh_fmt(dir, sizeof dir, "%s", full);
    path_up(dir);
    if ((n = s->h->dir_read(s->h->ctx, dir, &e)) < 0)
        return 0;
    if (!(cut = strrchr(a, '\\')))
        cut = a[0] && a[1] == ':' ? a + 1 : 0;
    for (i = 0; i < n && !found; i++) {
        if (!match(path_name(full), e[i].name))
            continue;
        if (s->tool.last[0] && (e[i].dir > s->tool.last_dir
                                || (e[i].dir == s->tool.last_dir && strcmp(e[i].name, s->tool.last) <= 0)))
            continue;                           /* (before the last, or it: asked already) */
        path_join(s->tool.path, sizeof s->tool.path, dir, e[i].name);
        sh_fmt(s->tool.shown, sizeof s->tool.shown, "%.*s%s", cut ? (int)(cut - a + 1) : 0, a, e[i].name);
        upper_all(s->tool.shown);
        sh_fmt(s->tool.last, sizeof s->tool.last, "%s", e[i].name);
        s->tool.last_dir = e[i].dir;
        *isdir = e[i].dir;
        found = 1;
    }
    s->h->dir_free(s->h->ctx, e);
    return found;
}

static int deltree_step(struct sh *s, int key);

static void deltree_one(struct sh *s)
{
    char line[SH_PATH + 40];
    sh_fmt(line, sizeof line, "Deleting %s...\r\n", s->tool.shown);
    say(s, line);
    if (tree_delete(s, s->tool.path) != 0) {
        sh_fmt(line, sizeof line, "Unable to delete - %s\r\n", s->tool.shown);
        err(s, line);
        s->tool.code = 1;
    }
}

/* DELTREE's paths from tool.at on: each found is asked about (SH_UNDER_WAY: the key comes to deltree_step) or with
   /Y deleted; at the end, the exit code. */
static int deltree_on(struct sh *s)
{
    char line[SH_PATH + 80];
    int isdir;
    for (; *s->tool.at; s->tool.at += strlen(s->tool.at) + 1, s->tool.last[0] = 0) {
        while (deltree_find(s, s->tool.at, &isdir)) {
            if (strlen(s->tool.path) <= 3) {    /* (a drive's root: never) */
                sh_fmt(line, sizeof line, "DELTREE won't delete a drive's root - %s\r\n", s->tool.path);
                err(s, line);
                s->tool.code = 1;
                continue;
            }
            if (s->tool.yes) {
                deltree_one(s);
                continue;
            }
            if (isdir)
                sh_fmt(line, sizeof line, "Delete directory \"%s\" and all its subdirectories? [yn] ", s->tool.shown);
            else
                sh_fmt(line, sizeof line, "Delete file \"%s\"? [yn] ", s->tool.shown);
            out(s, line);
            if (s->h->key && s->h->key(s->h->ctx) == SH_UNDER_WAY) {
                s->tool.step = deltree_step;
                s->key_for = 3;
                return SH_UNDER_WAY;
            }
            err(s, "\r\nDELTREE asks before it deletes, and nobody is at a keyboard here: DELTREE /Y\r\n");
            return 1;
        }
    }
    return s->tool.code;
}

static int deltree_step(struct sh *s, int key)
{
    if (key == 3) {
        out(s, "^C\r\n");
        return 1;
    }
    if (upper(key) != 'Y' && upper(key) != 'N')         /* (asked again) */
        return s->h->key && s->h->key(s->h->ctx) == SH_UNDER_WAY ? SH_UNDER_WAY : 1;
    out(s, upper(key) == 'Y' ? "y\r\n" : "n\r\n");
    if (upper(key) == 'Y')
        deltree_one(s);
    return deltree_on(s);
}

/* DELTREE [/Y] path ...: folders with everything in them (and files) deleted, each asked about first unless /Y,
   as MS-DOS 7.1 asks ("Delete directory "X" and all its subdirectories? [yn]"; here y or n alone, no Enter).
   A path that isn't there: nothing said. A pattern: each of its matches. A drive's root: never. */
static int cmd_deltree(struct sh *s, int argc, char **argv)
{
    unsigned k = 0;
    int i;
    s->tool.yes = 0;
    for (i = 1; i < argc; i++)
        if (same(argv[i], "/Y"))
            s->tool.yes = 1;
    if ((argc = switches(s, argc, argv, "Y")) < 0)
        return 1;
    if (argc < 2) {
        err(s, "Required parameter missing\r\n");
        return 1;
    }
    for (i = 1; i < argc; i++) {
        size_t n = strlen(argv[i]) + 1;
        if (k + n + 1 >= sizeof s->tool.args)
            break;
        memcpy(s->tool.args + k, argv[i], n);
        k += (unsigned)n;
    }
    s->tool.args[k] = 0;
    s->tool.at = s->tool.args;
    s->tool.last[0] = 0;
    s->tool.code = 0;
    return deltree_on(s);
}

/* ---- FIND and SORT (2026-11-27, C1a of docs/v1/cli.md: a native GLOS has no FreeDOS to bring them)
 *
 * As MS-DOS 7.1's FIND.EXE and SORT.EXE (asked of the real ones on the reference disk), where docs/v1/shell.md
 * doesn't say otherwise. Text is code page 437's to both: FIND /I's capitals and SORT's order are MS-DOS 7.1's for
 * that page (the shell is told no other). */

/* Code page 437's capitals for 80h-A5h (from A6h a character is its own): MS-DOS 7.1's, which FIND /I folds by (e
   acute is E there, and E acute itself); kernel/ndos/fucase.c's table for 437. */
static const unsigned char cp437_upper[] = {
    0x80, 0x9A, 0x45, 0x41, 0x8E, 0x41, 0x8F, 0x80, 0x45, 0x45, 0x45, 0x49, 0x49, 0x49, 0x8E, 0x8F,
    0x90, 0x92, 0x92, 0x4F, 0x99, 0x4F, 0x55, 0x55, 0x59, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
    0x41, 0x49, 0x4F, 0x55, 0xA5, 0xA5 };

/* SORT's order for 80h-AFh: the letter or sign each sorts as one with, case and accents aside (MS-DOS 7.1's SORT,
   measured with a line for every byte); from B0h a byte is its own, but for E1h (sharp s), which is S. Below 80h a
   small letter is its capital. */
static const unsigned char cp437_order[] = {
    0x43, 0x55, 0x45, 0x41, 0x41, 0x41, 0x41, 0x43, 0x45, 0x45, 0x45, 0x49, 0x49, 0x49, 0x41, 0x41,
    0x45, 0x41, 0x41, 0x4F, 0x4F, 0x4F, 0x55, 0x55, 0x59, 0x4F, 0x55, 0x24, 0x24, 0x24, 0x24, 0x24,
    0x41, 0x49, 0x4F, 0x55, 0x4E, 0x4E, 0xA6, 0xA7, 0x3F, 0xA9, 0xAA, 0xAB, 0xAC, 0x21, 0x22, 0x22 };

static int fold437(int c)
{
    return c < 0x80 ? upper(c) : c < 0x80 + (int)sizeof cp437_upper ? cp437_upper[c - 0x80] : c;
}

static int order437(int c)
{
    return c < 0x80 ? upper(c) : c < 0x80 + (int)sizeof cp437_order ? cp437_order[c - 0x80] : c == 0xE1 ? 'S' : c;
}

/* A file's own text written (to the terminal as a file's bytes, or to the output's file). */
static void say_text(struct sh *s, const void *t, int n)
{
    if (s->discard)
        return;
    if (s->redirect)
        s->h->put(s->h->ctx, s->redirect, t, n);
    else
        s->h->write(s->h->ctx, SH_TEXT, (const char *)t, n);
}

/* A file named to FIND or SORT, * and ? taken (which MS-DOS 7.1's take as names: "File not found - W\*.TXT"): each
   file it names given to each(), by its full path and as it is to be shown (the folder as typed, the name as
   found; hidden and system files are left, as DOS's find-first leaves them). How many each() took (0: none there
   that could be read), or -1 for a folder named alone. */
static int each_file(struct sh *s, const char *word, int (*each)(struct sh *s, void *arg, const char *path,
                     const char *shown), void *arg)
{
    char full[SH_PATH], dir[SH_PATH], p[SH_PATH], shown[SH_PATH];
    struct sh_dirent *e;
    size_t len = strlen(word);
    int n, i, took = 0;
    full_path(s, word, full, sizeof full);
    if (!strpbrk(word, "*?")) {
        if (len && (word[len - 1] == '\\' || word[len - 1] == '/'))
            return 0;                           /* (a folder's name with a backslash after it: no file) */
        if (s->h->is_dir(s->h->ctx, full))
            return -1;
        return each(s, arg, full, word) == 0;
    }
    sh_fmt(dir, sizeof dir, "%s", full);
    path_up(dir);
    if ((n = s->h->dir_read(s->h->ctx, dir, &e)) < 0)
        return 0;
    for (i = 0; i < n; i++) {
        if (e[i].dir || (e[i].attr & 0x06) || !match(path_name(full), e[i].name))
            continue;
        path_join(p, sizeof p, dir, e[i].name);
        sh_fmt(shown, sizeof shown, "%.*s%s", (int)(path_name(word) - word), word, e[i].name);
        if (each(s, arg, p, shown) == 0)
            took++;
    }
    s->h->dir_free(s->h->ctx, e);
    return took;
}

/* FIND [/V] [/C] [/N] [/I] "string" [[drive:][path]filename ...]: the lines that have the string (/V: that haven't),
   each file's under "---------- NAME" (the name as typed) after an empty line; /N each line's number first, "[n]";
   /C only how many ("---------- NAME: n"); /I the case of letters aside. With no file, the input (a pipe, or <)
   with no heading. As MS-DOS 7.1's FIND.EXE: the string must be in quotation marks (two of them in it are one);
   the words between switches, names and the string are parted by spaces, tabs, commas or semicolons; a line is
   at most 4096 bytes (a longer one is taken as lines of 4096, each counted, and each written with a line end);
   a line is written with its own line end, or CR LF if it had none; Ctrl+Z ends a file. The exit code is what
   came last: 2 after a file that couldn't be read, 0 after a line with the string (with /V too); 1 if neither
   came; 2 for what can't be run at all. */
#define FIND_V 1
#define FIND_C 2
#define FIND_N 4
#define FIND_I 8
#define FIND_LINE 4096

struct find {
    char what[SH_LINE];                 /* the string */
    char files[SH_LINE];                /* the names, each ended by a NUL, the last by two */
    char w[SH_LINE];                    /* (a word of the command's, read) */
    int len, flags, code;
    unsigned char line[FIND_LINE], in[4096];
};

static int find_has(const struct find *f, const unsigned char *t, int n)
{
    int i, k;
    for (i = 0; f->len && i + f->len <= n; i++) {
        for (k = 0; k < f->len; k++) {
            int a = t[i + k], b = (unsigned char)f->what[k];
            if (a != b && (!(f->flags & FIND_I) || fold437(a) != fold437(b)))
                break;
        }
        if (k == f->len)
            return 1;
    }
    return 0;
}

/* One file's lines (or the input's, shown 0: no heading) searched: 0, or -1 if it can't be opened. */
static int find_in(struct sh *s, void *arg, const char *path, const char *shown)
{
    struct find *f = arg;
    char t[SH_PATH + 40];
    void *h = s->h->open(s->h->ctx, path, 'r');
    unsigned long no = 0, count = 0;
    int n = 0, got = 0, at = 0, end = 0;
    if (!h)
        return -1;
    if (shown && !(f->flags & FIND_C)) {
        sh_fmt(t, sizeof t, "\r\n---------- %s\r\n", shown);
        say(s, t);
    }
    while (!end) {
        int c = -1, hit;
        if (at == got) {
            got = s->h->read(s->h->ctx, h, f->in, (int)sizeof f->in);
            at = 0;
        }
        if (got <= 0 || (c = f->in[at++]) == 0x1A)
            end = 1, c = -1;
        if (c >= 0)
            f->line[n++] = (unsigned char)c;
        if (!n || (c != '\n' && n < FIND_LINE && !end))
            continue;
        no++;
        if ((hit = find_has(f, f->line, n)) != 0)
            f->code = 0;
        if (hit != ((f->flags & FIND_V) != 0)) {
            count++;
            if (!(f->flags & FIND_C)) {
                if (f->flags & FIND_N) {
                    sh_fmt(t, sizeof t, "[%lu]", no);
                    say(s, t);
                }
                say_text(s, f->line, n);
                if (f->line[n - 1] != '\n')
                    say_text(s, "\r\n", 2);
            }
        }
        n = 0;
    }
    s->h->close(s->h->ctx, h);
    if (f->flags & FIND_C) {
        if (shown)
            sh_fmt(t, sizeof t, "\r\n---------- %s: %lu\r\n", shown, count);
        else
            sh_fmt(t, sizeof t, "%lu\r\n", count);
        say(s, t);
    }
    return 0;
}

/* raw: the command as typed, its redirections taken out; source: its input's file (a pipe, <), or "". */
static int cmd_find(struct sh *s, const char *raw, const char *source)
{
    struct find *f = s->h->alloc(s->h->ctx, sizeof *f);
    const char *t = raw, *bad = 0;
    char line[SH_PATH + 40];
    unsigned k = 0;
    int have = 0, r;
    if (!f) {
        err(s, "FIND: Insufficient memory\r\n");
        return s->level = 2;
    }
    f->flags = 0, f->code = 1;
    while (*t == ' ' || *t == '\t')
        t++;
    while (*t && *t != ' ' && *t != '\t')       /* (FIND itself) */
        t++;
    for (;;) {
        char *w = f->w;
        unsigned n = 0;
        int quoted;
        while (*t && strchr(" \t,;", *t))
            t++;
        if (!*t)
            break;
        if ((quoted = *t == '"') != 0) {        /* ("" in it is one; what follows the last begins the next word) */
            for (t++; *t && (*t != '"' || t[1] == '"'); t++) {
                if (*t == '"')
                    t++;
                if (n < sizeof f->w - 1)
                    w[n++] = *t;
            }
            if (!*t) {
                bad = "FIND: Parameter format not correct\r\n";
                break;
            }
            t++;
        } else {
            for (; *t && !strchr(" \t,;", *t); t++)
                if (n < sizeof f->w - 1)
                    w[n++] = *t;
        }
        w[n] = 0;
        if (!quoted && w[0] == '/') {
            const char *sw = "VCNI";
            if (n != 2 || !strchr(sw, upper((unsigned char)w[1]))) {
                bad = "FIND: Invalid switch\r\n";
                break;
            }
            f->flags |= 1 << (strchr(sw, upper((unsigned char)w[1])) - sw);
        } else if (!have) {
            if (!quoted) {
                bad = "FIND: Parameter format not correct\r\n";
                break;
            }
            memcpy(f->what, w, n + 1);
            f->len = (int)n;
            have = 1;
        } else if (k + n + 2 < sizeof f->files) {
            memcpy(f->files + k, w, n + 1);
            k += n + 1;
        }
    }
    f->files[k] = 0;
    if (!bad && !have)
        bad = "FIND: Required parameter missing\r\n";
    if (bad) {
        err(s, bad);
        s->h->free(s->h->ctx, f);
        return s->level = 2;
    }
    if (!k) {                                   /* the input (nothing to read from NUL) */
        if (!source || !source[0]) {
            err(s, "FIND \"string\" file, FIND \"string\" < file, or command | FIND \"string\"\r\n");
            f->code = 2;
        } else if (same(source, "NUL")) {
            if (f->flags & FIND_C)
                say(s, "0\r\n");
        } else if (find_in(s, f, source, 0) != 0) {
            sh_fmt(line, sizeof line, "File not found - %s\r\n", source);
            err(s, line);
            f->code = 2;
        }
    } else {
        const char *w;
        for (w = f->files; *w; w += strlen(w) + 1) {
            /* (a line end after each, which MS-DOS's leave to the next heading's empty line or the prompt) */
            int n = each_file(s, w, find_in, f);
            if (n > 0)
                continue;
            sh_fmt(line, sizeof line, n < 0 ? "Access denied  - %s\r\n" : "File not found - %s\r\n", w);
            err(s, line);
            f->code = 2;
        }
    }
    r = f->code;
    s->h->free(s->h->ctx, f);
    return s->level = r;
}

/* SORT [/R] [/+n] [[drive:][path]filename ...]: the lines of the files (or of the input: a pipe, <) in order, to the
   output. As MS-DOS 7.1's SORT.EXE: letters in any case, and with accents, sort as one (cp437_order); a line that
   is the start of another first; /R the other way; /+n by what is from the line's n-th character on (a line
   shorter has nothing there, and goes first). LF ends a line, a CR before it is not the line's, and each is
   written with CR LF; a last line with none is written with none, if it is written last; Ctrl+Z ends a file. A
   file that isn't there is said and passed over (the exit code stays 0, as MS-DOS's); a switch it doesn't know:
   1. Lines the same in order keep the order they came in (MS-DOS 7.1's are as its sort leaves them); lines are
   kept whole (MS-DOS's are cut at 511 characters) and so are NULs in them; it all must fit in SORT_MAX. */
#define SORT_MAX (256ul * 1024)
#define SORT_END 0x80000000ul           /* (in a line's len: it had a line end) */

struct sort_line {
    unsigned long at, len;
};

struct sort {
    unsigned char *text;
    unsigned long size, used, room;     /* what the files are, what has been read, what may be */
    unsigned long *ends;                /* where each file's text ends */
    unsigned nfiles, room_files;
    unsigned long col;                  /* /+n: n - 1 */
    int rev;
};

static int sort_cmp(const struct sort *o, const struct sort_line *a, const struct sort_line *b)
{
    unsigned long la = a->len & ~SORT_END, lb = b->len & ~SORT_END, i;
    const unsigned char *p = o->text + a->at, *q = o->text + b->at;
    int r = 0;
    la = la > o->col ? la - o->col : 0;         /* (what is from column n on: nothing, in a shorter line) */
    lb = lb > o->col ? lb - o->col : 0;
    p += la ? o->col : 0;
    q += lb ? o->col : 0;
    for (i = 0; i < la && i < lb && !r; i++)
        r = order437(p[i]) - order437(q[i]);
    if (!r)
        r = la < lb ? -1 : la > lb;
    return o->rev ? -r : r;
}

/* (each_file's each: a file's size counted, the first time; its text read, the second) */
static int sort_size(struct sh *s, void *arg, const char *path, const char *shown)
{
    struct sort *o = arg;
    struct sh_dirent e;
    (void)shown;
    if (s->h->stat(s->h->ctx, path, &e) != 0)
        return -1;
    o->size += e.size;
    o->nfiles++;
    return 0;
}

static int sort_read(struct sh *s, void *arg, const char *path, const char *shown)
{
    struct sort *o = arg;
    void *f = s->h->open(s->h->ctx, path, 'r');
    unsigned long from = o->used;
    unsigned char *z;
    int got;
    (void)shown;
    if (!f)
        return -1;
    while (o->used < o->room
           && (got = s->h->read(s->h->ctx, f, o->text + o->used,
                                o->room - o->used < 16384 ? (int)(o->room - o->used) : 16384)) > 0)
        o->used += (unsigned long)got;
    s->h->close(s->h->ctx, f);
    if ((z = memchr(o->text + from, 0x1A, o->used - from)) != 0)
        o->used = (unsigned long)(z - o->text);
    if (o->nfiles < o->room_files)
        o->ends[o->nfiles++] = o->used;
    return 0;
}

static int cmd_sort(struct sh *s, int argc, char **argv, const char *source)
{
    struct sort o;
    struct sort_line *lines = 0;
    unsigned long n = 0, i, w;
    unsigned at;
    int k, pass, files = 0, r = 1;
    char line[SH_PATH + 40];
    memset(&o, 0, sizeof o);
    for (k = 1; k < argc; k++) {
        const char *a = argv[k];
        if (a[0] != '/') {
            argv[++files] = argv[k];
            continue;
        }
        if (same(a, "/R")) {
            o.rev = 1;
        } else if (a[1] == '+' && a[2] >= '0' && a[2] <= '9') {
            unsigned long c = 0;
            for (a += 2; *a >= '0' && *a <= '9' && c < 100000000ul; a++)   /* (what follows the number: nothing) */
                c = c * 10 + (unsigned long)(*a - '0');
            if (!c)
                break;
            o.col = c - 1;
        } else {
            break;
        }
    }
    if (k < argc) {
        err(s, "SORT: Invalid switch\r\n");
        return s->level = 1;
    }
    if (!files && (!source || !source[0])) {
        err(s, "SORT file, SORT < file, or command | SORT\r\n");
        return s->level = 1;
    }
    if (!files && same(source, "NUL"))
        return s->level = 0;
    /* twice: the sizes (and what isn't there said), then the text read */
    for (pass = 0; pass < 2; pass++) {
        if (pass) {
            if (o.size > SORT_MAX || !(o.text = s->h->alloc(s->h->ctx, (unsigned)o.size + 1))
                || !(o.ends = s->h->alloc(s->h->ctx, (o.nfiles + 1) * (unsigned)sizeof *o.ends))) {
                sh_fmt(line, sizeof line, "SORT: Insufficient memory (it sorts %lu KB at most)\r\n", SORT_MAX / 1024);
                err(s, line);
                goto done;
            }
            o.room = o.size, o.room_files = o.nfiles + 1, o.nfiles = 0;
        }
        if (!files) {
            if ((pass ? sort_read : sort_size)(s, &o, source, 0) != 0 && !pass) {
                sh_fmt(line, sizeof line, "SORT: File <%s> not found\r\n", source);
                err(s, line);
            }
            continue;
        }
        for (k = 1; k <= files; k++) {
            int got = each_file(s, argv[k], pass ? sort_read : sort_size, &o);
            size_t len = strlen(argv[k]);
            if (got > 0 || pass)
                continue;
            if (got < 0)                        /* (a folder: MS-DOS names its files) */
                sh_fmt(line, sizeof line, "SORT: File <%s%s*.*> not found\r\n", argv[k],
                       len && argv[k][len - 1] == ':' ? "" : "\\");
            else
                sh_fmt(line, sizeof line, "SORT: File <%s> not found\r\n", argv[k]);
            err(s, line);
        }
    }
    /* the lines: LF ends each (a CR before it not the line's), a file's end the last */
    for (pass = 0; pass < 2; pass++) {
        unsigned long from = 0;
        n = 0;
        for (at = 0; at < o.nfiles; at++) {
            for (i = from; i < o.ends[at]; i = w + 1) {
                for (w = i; w < o.ends[at] && o.text[w] != '\n'; w++)
                    ;
                if (pass) {
                    unsigned long len = w - i;
                    if (w < o.ends[at] && len && o.text[w - 1] == '\r')
                        len--;
                    lines[n].at = i;
                    lines[n].len = len | (w < o.ends[at] ? SORT_END : 0);
                }
                n++;
            }
            from = o.ends[at];
        }
        if (!pass && n && !(lines = s->h->alloc(s->h->ctx, (unsigned)(n * 2 * sizeof *lines)))) {
            sh_fmt(line, sizeof line, "SORT: Insufficient memory (it sorts %lu KB at most)\r\n", SORT_MAX / 1024);
            err(s, line);
            goto done;
        }
        if (!n)
            break;
    }
    /* merged in runs that double (the second half of lines is the room for it): equal lines keep their order */
    if (n) {
        struct sort_line *from = lines, *to = lines + n, *t;
        for (w = 1; w < n; w *= 2) {
            for (i = 0; i < n; i += 2 * w) {
                unsigned long mid = i + w < n ? i + w : n, hi = i + 2 * w < n ? i + 2 * w : n, x = i, y = mid, c = i;
                while (x < mid && y < hi)
                    to[c++] = sort_cmp(&o, &from[y], &from[x]) < 0 ? from[y++] : from[x++];
                while (x < mid)
                    to[c++] = from[x++];
                while (y < hi)
                    to[c++] = from[y++];
            }
            t = from, from = to, to = t;
        }
        for (i = 0; i < n; i++) {
            const unsigned char *l = o.text + from[i].at;
            unsigned long len = from[i].len & ~SORT_END;
            if ((from[i].len & SORT_END) && l[len] == '\r') {
                say_text(s, l, (int)len + 2);   /* (its own CR LF) */
            } else {
                say_text(s, l, (int)len);
                if ((from[i].len & SORT_END) || i + 1 < n)
                    say_text(s, "\r\n", 2);
            }
        }
    }
    r = 0;
done:
    if (lines)
        s->h->free(s->h->ctx, lines);
    if (o.ends)
        s->h->free(s->h->ctx, o.ends);
    if (o.text)
        s->h->free(s->h->ctx, o.text);
    return s->level = r;
}

/* ---- FC and COMP (2026-11-27, C1b of docs/v1/cli.md)
 *
 * FC as MS-DOS 7.1's FC.EXE (asked of the real one on the reference disk), where docs/v1/shell.md doesn't say
 * otherwise; COMP as MS-DOS 6.22's was documented (7.1 has none). */

/* v in hex, digits of it at least (capitals). */
static void hex_text(char *b, unsigned room, unsigned long v, int digits)
{
    char t[16];
    int n = 0;
    do
        t[n++] = "0123456789ABCDEF"[v & 15];
    while ((v >>= 4) != 0 || n < digits);
    for (room--; n && room; room--)
        *b++ = t[--n];
    *b = 0;
}

/* A pattern's part (a name's, an extension's) put over a name's: ? takes the character in its place, * the rest; -1
   if a ? has none to take. */
static int like_part(const char *pat, size_t pn, const char *src, size_t sn, char *out, size_t *k, size_t room)
{
    size_t i, at = 0;
    for (i = 0; i < pn; i++, at++) {
        if (pat[i] == '*') {
            for (; at < sn && *k < room - 1; at++)
                out[(*k)++] = src[at];
            return 0;
        }
        if (pat[i] == '?' && at >= sn)
            return -1;
        if (*k < room - 1)
            out[(*k)++] = pat[i] == '?' ? src[at] : pat[i];
    }
    return 0;
}

/* FC's and COMP's second name, a pattern, made to match the first's (as MS-DOS 7.1's FC: "*.BAK" with A.TXT is A.BAK,
   "Z*.TXT" is Z.TXT, "*" alone the whole name, a pattern with no dot no extension): 0, or -1 if a ? has no character
   of the name's to take ("Could not expand second filename so as to match first"). */
static int name_like(const char *pat, const char *src, char *out, size_t room)
{
    const char *pd, *sd;
    size_t k = 0;
    if (!strcmp(pat, "*"))
        pat = "*.*";
    pd = strrchr(pat, '.'), sd = strrchr(src, '.');
    if (like_part(pat, pd ? (size_t)(pd - pat) : strlen(pat), src, sd ? (size_t)(sd - src) : strlen(src), out, &k,
                  room))
        return -1;
    if (pd && pd[1]) {
        size_t dot = k;
        out[k++] = '.';
        if (like_part(pd + 1, strlen(pd + 1), sd ? sd + 1 : "", sd ? strlen(sd + 1) : 0, out, &k, room))
            return -1;
        if (k == dot + 1)
            k = dot;
    }
    out[k] = 0;
    return 0;
}

/* FC [/A] [/C] [/L] [/LBn] [/N] [/T] [/W] [/nnnn] name1 name2, FC /B name1 name2: as MS-DOS 7.1's FC.EXE.
 *
 * Text (but /B, or a first name ending .EXE .COM .SYS .OBJ .LIB or .BIN without /L: bytes): each file's lines are
 * read into a buffer of /LB's lines (100). Where they differ, the buffers are moved on to the last line that was the
 * same (none if that was the last of the buffer's, as MS-DOS's buffer has it), and the two are searched for /nnnn
 * lines (2) the same again: pairs as far into either as one goes, (d, 1), (1, d), (d, 2), (2, d) ...; a match that
 * runs past either buffer's end counts. Found: "****** NAME" and that file's lines from the buffer's first to the
 * first of the match, the same of the other, "******" and an empty line; both go on from there. Not found: to the
 * files' ends, or, if they don't end in the buffers, "Resync failed.  Files are too different" and the buffers.
 * A line is read up to its LF or 127 bytes; tabs become spaces to every eighth column (not with /T), CRs go (/T keeps
 * them), a NUL ends it, and what is past the 127th column is the next line. /C: case aside (code page 437's
 * capitals, as FIND /I); /W: spaces and tabs at either end aside and a run of them as one, lines of nothing else
 * passed over; /N: each line's number; /A: a block's first and last lines, "..." between. Bytes: "%08X: %02X %02X"
 * for each that differs, "FC: NAME longer than NAME". The first name may be a pattern; the second a pattern made
 * from each of the first's matches. Its messages are its output, as MS-DOS's are. Exit codes as MS-DOS 7.1's: 1 for
 * too few or too many names and switches that don't go together, else 0 (differences too). */
#define FC_COL 127
#define FC_A 0x01
#define FC_B 0x02
#define FC_C 0x04
#define FC_L 0x08
#define FC_N 0x10
#define FC_T 0x20
#define FC_W 0x40
#define FC_NUM 0x80                     /* /nnnn was given */

struct fc_line {
    unsigned long no;                   /* its number in its file */
    int len;
    unsigned char t[FC_COL + 1];        /* as it is shown (tabs made spaces, CRs gone: but /T) */
};

struct fc_file {
    void *h;
    int at, got, eof;
    unsigned char in[512];
    unsigned char raw[FC_COL];          /* the bytes read to make lines of, and how far they have been */
    int rn, rat;
    unsigned long no;
    struct fc_line *b;                  /* the buffer, /LB lines */
    int n;
    char name[SH_PATH];                 /* as shown */
};

struct fc {
    unsigned flags;
    int lb, resync;
    struct fc_file f[2];
};

static int fc_byte(struct sh *s, struct fc_file *f)
{
    if (f->at == f->got) {
        f->got = s->h->read(s->h->ctx, f->h, f->in, (int)sizeof f->in);
        f->at = 0;
        if (f->got <= 0) {
            f->got = 0;
            return -1;
        }
    }
    return f->in[f->at++];
}

static int fc_blank(const struct fc_line *l)
{
    int i;
    for (i = 0; i < l->len; i++)
        if (l->t[i] != ' ' && l->t[i] != '\t')
            return 0;
    return 1;
}

/* A file's next line into l: 1, or 0 at its end. */
static int fc_read(struct sh *s, const struct fc *o, struct fc_file *f, struct fc_line *l)
{
    for (;;) {
        int col = 0;
        if (f->rat >= f->rn) {                  /* (the next bytes: to an LF, or 127) */
            int c;
            f->rn = f->rat = 0;
            while (f->rn < FC_COL && (c = fc_byte(s, f)) >= 0)
                if ((f->raw[f->rn++] = (unsigned char)c) == '\n')
                    break;
            if (!f->rn) {
                f->eof = 1;
                return 0;
            }
        }
        while (f->rat < f->rn) {
            int c = f->raw[f->rat];
            if (!c) {                           /* (a NUL: the rest is no one's) */
                f->rat = f->rn;
                break;
            }
            if (c == '\n' || (c == '\r' && !(o->flags & FC_T))) {
                f->rat++;
                continue;
            }
            if (c == '\t' && !(o->flags & FC_T)) {
                int to = (col / 8 + 1) * 8;
                f->rat++;
                if (to > FC_COL) {              /* (a tab past the 127th column ends the line there) */
                    while (col < FC_COL)
                        l->t[col++] = ' ';
                    break;
                }
                while (col < to)
                    l->t[col++] = ' ';
                continue;
            }
            if (col == FC_COL)
                break;                          /* (this character begins the next line) */
            l->t[col++] = (unsigned char)c;
            f->rat++;
        }
        l->t[col] = 0;
        l->len = col;
        l->no = ++f->no;
        if (!(o->flags & FC_W) || !fc_blank(l))
            return 1;
    }
}

/* (/W: spaces and tabs at the ends gone, a run of them one space) */
static int fc_squeeze(const struct fc_line *l, unsigned char *out)
{
    int i = 0, k = 0;
    while (i < l->len && (l->t[i] == ' ' || l->t[i] == '\t'))
        i++;
    for (; i < l->len; i++) {
        if (l->t[i] == ' ' || l->t[i] == '\t') {
            if (k && out[k - 1] != ' ')
                out[k++] = ' ';
        } else {
            out[k++] = l->t[i];
        }
    }
    if (k && out[k - 1] == ' ')
        k--;
    return k;
}

static int fc_same(const struct fc *o, const struct fc_line *a, const struct fc_line *b)
{
    unsigned char x[FC_COL + 1], y[FC_COL + 1];
    const unsigned char *p = a->t, *q = b->t;
    int n = a->len, m = b->len, i;
    if (o->flags & FC_W) {
        n = fc_squeeze(a, x), m = fc_squeeze(b, y);
        p = x, q = y;
    }
    if (n != m)
        return 0;
    for (i = 0; i < n; i++)
        if (p[i] != q[i] && (!(o->flags & FC_C) || fold437(p[i]) != fold437(q[i])))
            return 0;
    return 1;
}

static void fc_fill(struct sh *s, const struct fc *o, struct fc_file *f)
{
    while (f->n < o->lb && !f->eof && fc_read(s, o, f, &f->b[f->n]))
        f->n++;
}

static void fc_drop(struct fc_file *f, int k)
{
    memmove(f->b, f->b + k, (size_t)(f->n - k) * sizeof *f->b);
    f->n -= k;
}

/* The lines of a block: its first count of a buffer's, under "****** NAME". */
static void fc_lines(struct sh *s, const struct fc *o, const struct fc_file *f, int count)
{
    char t[SH_PATH + 16];
    int i;
    sh_fmt(t, sizeof t, "****** %s\r\n", f->name);
    say(s, t);
    for (i = 0; i < count; i++) {
        if ((o->flags & FC_A) && count > 3 && i > 0 && i < count - 1) {
            if (i == 1)
                say(s, "...\r\n");
            continue;
        }
        if (o->flags & FC_N) {
            sh_fmt(t, sizeof t, "%6lu:  ", f->b[i].no);
            say(s, t);
        }
        say_text(s, f->b[i].t, f->b[i].len);
        say(s, "\r\n");
    }
}

static int fc_try(const struct fc *o, int i1, int i2)
{
    const struct fc_file *a = &o->f[0], *b = &o->f[1];
    int k;
    if (i1 >= a->n || i2 >= b->n || !o->resync)
        return 0;
    for (k = 0; k < o->resync && i1 + k < a->n && i2 + k < b->n; k++)
        if (!fc_same(o, &a->b[i1 + k], &b->b[i2 + k]))
            return 0;
    return 1;
}

/* Two files' lines compared: 1 if they differ. */
static int fc_text(struct sh *s, struct fc *o)
{
    struct fc_file *a = &o->f[0], *b = &o->f[1];
    int differ = 0;
    for (;;) {
        int m, d, j, r1 = -1, r2 = -1;
        fc_fill(s, o, a);
        fc_fill(s, o, b);
        if (!a->n && !b->n)
            break;
        for (m = 0; m < a->n && m < b->n && fc_same(o, &a->b[m], &b->b[m]); m++)
            ;
        if (m == a->n && m == b->n) {
            fc_drop(a, m);
            fc_drop(b, m);
            continue;
        }
        m = m == a->n || m == b->n ? m : m ? m - 1 : 0;     /* (the last line the same stays: but at an end) */
        fc_drop(a, m);
        fc_drop(b, m);
        fc_fill(s, o, a);
        fc_fill(s, o, b);
        differ = 1;
        for (d = 1; r1 < 0 && d < (a->n > b->n ? a->n : b->n); d++)
            for (j = 1; r1 < 0 && j <= d; j++) {
                if (fc_try(o, d, j))
                    r1 = d, r2 = j;
                else if (j != d && fc_try(o, j, d))
                    r1 = j, r2 = d;
            }
        if (r1 < 0) {                           /* (to the end: the files', or the buffers' if they go on) */
            if (!a->eof || !b->eof)
                say(s, "Resync failed.  Files are too different\r\n");
            fc_lines(s, o, a, a->n);
            fc_lines(s, o, b, b->n);
            say(s, "******\r\n\r\n");
            break;
        }
        fc_lines(s, o, a, r1 + 1);
        fc_lines(s, o, b, r2 + 1);
        say(s, "******\r\n\r\n");
        fc_drop(a, r1);
        fc_drop(b, r2);
    }
    return differ;
}

/* Two files' bytes compared: 1 if they differ. */
static int fc_bytes(struct sh *s, struct fc *o)
{
    char t[SH_PATH * 2 + 40], at[12], x[4], y[4];
    unsigned long off = 0;
    int a, b, differ = 0;
    for (;; off++) {
        a = fc_byte(s, &o->f[0]), b = fc_byte(s, &o->f[1]);
        if (a < 0 || b < 0)
            break;
        if (a == b)
            continue;
        hex_text(at, sizeof at, off, 8);
        hex_text(x, sizeof x, (unsigned long)a, 2);
        hex_text(y, sizeof y, (unsigned long)b, 2);
        sh_fmt(t, sizeof t, "%s: %s %s\r\n", at, x, y);
        say(s, t);
        differ = 1;
    }
    if (a >= 0 || b >= 0) {
        sh_fmt(t, sizeof t, "FC: %s longer than %s\r\n", o->f[a < 0].name, o->f[a >= 0].name);
        say(s, t);
        differ = 1;
    }
    return differ;
}

/* The file whose name a FC or COMP was given, as it is shown: the folder as typed, the name as found. */
static void typed_dir(char *out, size_t room, const char *word, const char *name)
{
    sh_fmt(out, (unsigned)room, "%.*s%s", (int)(path_name(word) - word), word, name);
}

static int cmd_fc(struct sh *s, int argc, char **argv)
{
    static const char *const bin[] = { ".EXE", ".OBJ", ".LIB", ".COM", ".BIN", ".SYS", 0 };
    struct fc *o = s->h->alloc(s->h->ctx, sizeof *o);
    char dir[SH_PATH], pat[64], full[SH_PATH], t[SH_PATH * 2 + 64];
    const char *names[2] = { 0, 0 }, *ext;
    struct sh_dirent *e;
    size_t len;
    int k, n = 0, i, found = 0, binary, isdir, r = 0;
    if (!o) {
        say(s, "FC: out of memory\r\n");
        return s->level = 1;
    }
    memset(o, 0, sizeof *o);
    o->lb = 100, o->resync = 2;
    for (k = 1; k < argc; k++) {
        const char *a = argv[k];
        if (a[0] != '/') {
            if (n < 2)
                names[n] = a;
            n++;
            continue;
        }
        if (a[1] && !a[2] && strchr("ABCLNTW", upper((unsigned char)a[1]))) {
            o->flags |= 1u << (strchr("ABCLNTW", upper((unsigned char)a[1])) - "ABCLNTW");
        } else if (upper((unsigned char)a[1]) == 'L' && upper((unsigned char)a[2]) == 'B'
                   && strspn(a + 3, "0123456789") == strlen(a + 3)) {
            for (o->lb = a[3] ? 0 : 100, a += 3; *a && o->lb < 10000; a++)
                o->lb = o->lb * 10 + (*a - '0');
        } else if (a[1] && strspn(a + 1, "0123456789") == strlen(a + 1)) {
            for (o->resync = 0, a++; *a && o->resync < 10000; a++)
                o->resync = o->resync * 10 + (*a - '0');
            o->flags |= FC_NUM;
        } else {
            say(s, "FC: Invalid switch\r\n\r\n");
        }
    }
    if (n < 2 || n > 2) {
        say(s, n < 2 ? "FC: Insufficient number of filespecs\r\n" : "FC: Too many filenames\r\n\r\n");
        s->h->free(s->h->ctx, o);
        return s->level = 1;
    }
    ext = strrchr(path_name(names[0]), '.');
    for (binary = (o->flags & FC_B) != 0, i = 0; !binary && !(o->flags & FC_L) && ext && bin[i]; i++)
        binary = same(ext, bin[i]);
    if (binary && (o->flags & (FC_L | FC_N | FC_NUM))) {
        say(s, "FC: Incompatible switches\r\n");
        s->h->free(s->h->ctx, o);
        return s->level = 1;
    }
    full_path(s, names[0], full, sizeof full);
    sh_fmt(pat, sizeof pat, "%s", path_name(full));
    sh_fmt(dir, sizeof dir, "%s", full);
    path_up(dir);
    if ((n = s->h->dir_read(s->h->ctx, dir, &e)) < 0)
        n = 0, e = 0;
    for (i = 0; i < n; i++) {
        struct fc_file *a = &o->f[0], *b = &o->f[1];
        char second[64], p[SH_PATH];
        if (e[i].dir || (e[i].attr & 0x06) || !match(pat, e[i].name))
            continue;
        found = 1;
        typed_dir(a->name, sizeof a->name, names[0], e[i].name);
        if (!strpbrk(path_name(names[1]), "*?")) {
            sh_fmt(b->name, sizeof b->name, "%s", names[1]);
        } else if (name_like(path_name(names[1]), e[i].name, second, sizeof second) == 0) {
            typed_dir(b->name, sizeof b->name, names[1], second);
        } else {
            sh_fmt(t, sizeof t, "%s     %s\r\nCould not expand second filename so as to match first\r\n", a->name,
                   names[1]);
            say(s, t);
            continue;
        }
        sh_fmt(t, sizeof t, "Comparing files %s and %s\r\n", a->name, b->name);
        say(s, t);
        path_join(p, sizeof p, dir, e[i].name);
        a->h = s->h->open(s->h->ctx, p, 'r');
        full_path(s, b->name, p, sizeof p);
        len = strlen(b->name);
        isdir = s->h->is_dir(s->h->ctx, p) && b->name[len - 1] != '\\' && b->name[len - 1] != '/';
        b->h = isdir || b->name[len - 1] == '\\' || b->name[len - 1] == '/' ? 0 : s->h->open(s->h->ctx, p, 'r');
        for (k = 0; k < 2; k++) {
            struct fc_file *f = &o->f[k];
            f->at = f->got = f->eof = f->rn = f->rat = f->n = 0;
            f->no = 0;
        }
        if (!a->h || !b->h) {
            sh_fmt(t, sizeof t, "FC: cannot open %s - %s\r\n\r\n", a->h ? b->name : a->name,
                   a->h && isdir ? "Permission denied" : "No such file or directory");
            say(s, t);
        } else if (binary) {
            if (!fc_bytes(s, o))
                say(s, "FC: no differences encountered\r\n");
            say(s, "\r\n");
        } else {
            for (k = 0; k < 2; k++)
                if (!o->f[k].b && !(o->f[k].b = s->h->alloc(s->h->ctx, (unsigned)(o->lb ? o->lb : 1)
                                                                       * (unsigned)sizeof *o->f[k].b)))
                    break;
            if (k < 2) {
                say(s, "FC: out of memory\r\n");
                r = 1;
            } else {
                say(s, fc_text(s, o) ? "\r\n" : "FC: no differences encountered\r\n\r\n");
            }
        }
        if (a->h)
            s->h->close(s->h->ctx, a->h);
        if (b->h)
            s->h->close(s->h->ctx, b->h);
        a->h = b->h = 0;
    }
    if (e)
        s->h->dir_free(s->h->ctx, e);
    if (!found) {
        sh_fmt(t, sizeof t, "File(s) not found : %s\r\n", names[0]);
        say(s, t);
    }
    for (k = 0; k < 2; k++)
        if (o->f[k].b)
            s->h->free(s->h->ctx, o->f[k].b);
    s->h->free(s->h->ctx, o);
    return s->level = r;
}

/* COMP [name1] [name2] [/D] [/A] [/L] [/N=n] [/C]: MS-DOS 6.22's, as it was documented (MS-DOS 7.1 has none, so
   nothing was asked of a real one): the bytes compared, "Compare error at OFFSET n" (hex; /L: "at LINE n") with
   "file1 = xx" and "file2 = yy" (hex; /D decimal, /A characters) for each that differs, ten at most ("10
   Mismatches - ending compare"); "Files compare OK"; "Files are different sizes" (and nothing compared) unless /N=n,
   which compares the first n lines alone; /C case aside. Patterns as FC's; a folder second: the same name in it. It
   asks for nothing: not for names, and not "Compare more files (Y/N)?". The exit code: 0 the same, 1 different, 2
   a file not found or what can't be run. */
static int comp_pair(struct sh *s, const char *p1, const char *p2, const char *shown1, const char *shown2,
                     unsigned flags, unsigned long lines)
{
    struct sh_dirent x, y;
    char t[SH_PATH * 2 + 40], v1[8], v2[8], at[12];
    void *f1, *f2;
    unsigned char b1[512], b2[512];
    unsigned long off = 0, line = 1;
    int n1 = 0, n2 = 0, i1 = 0, i2 = 0, errors = 0;
    int no1, no2;
    sh_fmt(t, sizeof t, "Comparing %s and %s...\r\n", shown1, shown2);
    say(s, t);
    no1 = s->h->stat(s->h->ctx, p1, &x) != 0 || x.dir;
    no2 = s->h->stat(s->h->ctx, p2, &y) != 0 || y.dir;
    if (no1 || no2) {
        sh_fmt(t, sizeof t, "Can't find/open file: %s\r\n\r\n", no1 ? shown1 : shown2);
        say(s, t);
        return 2;
    }
    if (!lines && x.size != y.size) {
        say(s, "Files are different sizes\r\n\r\n");
        return 1;
    }
    if (!(f1 = s->h->open(s->h->ctx, p1, 'r')) || !(f2 = s->h->open(s->h->ctx, p2, 'r'))) {
        if (f1)
            s->h->close(s->h->ctx, f1);
        sh_fmt(t, sizeof t, "Can't find/open file: %s\r\n\r\n", f1 ? shown2 : shown1);
        say(s, t);
        return 2;
    }
    for (;; off++) {
        int a, b;
        if (i1 == n1) {
            n1 = s->h->read(s->h->ctx, f1, b1, (int)sizeof b1);
            i1 = 0;
        }
        if (i2 == n2) {
            n2 = s->h->read(s->h->ctx, f2, b2, (int)sizeof b2);
            i2 = 0;
        }
        if (n1 <= 0 || n2 <= 0 || (lines && line > lines))
            break;
        a = b1[i1++], b = b2[i2++];
        if (a != b && (!(flags & 4) || fold437(a) != fold437(b))) {
            if (flags & 8) {
                sh_fmt(t, sizeof t, "Compare error at LINE %lu\r\n", line);
            } else {
                hex_text(at, sizeof at, off, 1);
                sh_fmt(t, sizeof t, "Compare error at OFFSET %s\r\n", at);
            }
            say(s, t);
            if (flags & 1) {
                sh_fmt(v1, sizeof v1, "%u", (unsigned)a);
                sh_fmt(v2, sizeof v2, "%u", (unsigned)b);
            } else if (flags & 2) {
                sh_fmt(v1, sizeof v1, "%c", a);
                sh_fmt(v2, sizeof v2, "%c", b);
            } else {
                hex_text(v1, sizeof v1, (unsigned long)a, 2);
                hex_text(v2, sizeof v2, (unsigned long)b, 2);
            }
            sh_fmt(t, sizeof t, "file1 = %s\r\nfile2 = %s\r\n", v1, v2);
            say(s, t);
            if (++errors == 10) {
                say(s, "10 Mismatches - ending compare\r\n");
                break;
            }
        }
        if (a == '\n')
            line++;
    }
    s->h->close(s->h->ctx, f1);
    s->h->close(s->h->ctx, f2);
    say(s, errors ? "\r\n" : "Files compare OK\r\n\r\n");
    return errors ? 1 : 0;
}

static int cmd_comp(struct sh *s, int argc, char **argv)
{
    char dir[SH_PATH], pat[64], full[SH_PATH], p1[SH_PATH], p2[SH_PATH], shown1[SH_PATH], shown2[SH_PATH];
    char t[SH_PATH + 40];
    const char *names[2] = { 0, 0 };
    struct sh_dirent *e;
    unsigned flags = 0;
    unsigned long lines = 0;
    int k, n = 0, i, found = 0, r = 0;
    for (k = 1; k < argc; k++) {
        const char *a = argv[k];
        if (a[0] != '/') {
            if (n < 2)
                names[n] = a;
            n++;
        } else if (a[1] && !a[2] && strchr("DACL", upper((unsigned char)a[1]))) {
            flags |= 1u << (strchr("DACL", upper((unsigned char)a[1])) - "DACL");
        } else if (upper((unsigned char)a[1]) == 'N' && a[2] == '=' && a[3]
                   && strspn(a + 3, "0123456789") == strlen(a + 3)) {
            for (lines = 0, a += 3; *a && lines < 100000000ul; a++)
                lines = lines * 10 + (unsigned long)(*a - '0');
        } else {
            sh_fmt(t, sizeof t, "Invalid switch - %s\r\n", a);
            err(s, t);
            return s->level = 2;
        }
    }
    if (n != 2) {
        err(s, n < 2 ? "COMP name1 name2 [/D] [/A] [/L] [/N=n] [/C]: GLOS's COMP asks for no names\r\n"
                     : "Too many parameters\r\n");
        return s->level = 2;
    }
    full_path(s, names[0], full, sizeof full);
    sh_fmt(pat, sizeof pat, "%s", path_name(full));
    sh_fmt(dir, sizeof dir, "%s", full);
    path_up(dir);
    if ((n = s->h->dir_read(s->h->ctx, dir, &e)) < 0)
        n = 0, e = 0;
    for (i = 0; i < n; i++) {
        char second[64];
        int got;
        if (e[i].dir || (e[i].attr & 0x06) || !match(pat, e[i].name))
            continue;
        found = 1;
        path_join(p1, sizeof p1, dir, e[i].name);
        typed_dir(shown1, sizeof shown1, names[0], e[i].name);
        full_path(s, names[1], p2, sizeof p2);
        if (!strpbrk(path_name(names[1]), "*?") && s->h->is_dir(s->h->ctx, p2)) {
            size_t len = strlen(names[1]);      /* (a folder: the same name in it) */
            path_join(p2, sizeof p2, p2, e[i].name);
            sh_fmt(shown2, sizeof shown2, len && (names[1][len - 1] == '\\' || names[1][len - 1] == ':') ? "%s%s"
                   : "%s\\%s", names[1], e[i].name);
        } else if (!strpbrk(path_name(names[1]), "*?")) {
            sh_fmt(shown2, sizeof shown2, "%s", names[1]);
        } else if (name_like(path_name(names[1]), e[i].name, second, sizeof second) == 0) {
            typed_dir(shown2, sizeof shown2, names[1], second);
            full_path(s, shown2, p2, sizeof p2);
        } else {
            sh_fmt(t, sizeof t, "Can't find/open file: %s\r\n\r\n", names[1]);
            say(s, t);
            r = 2;
            continue;
        }
        got = comp_pair(s, p1, p2, shown1, shown2, flags, lines);
        if (got > r)
            r = got;
    }
    if (e)
        s->h->dir_free(s->h->ctx, e);
    if (!found) {
        sh_fmt(t, sizeof t, "Can't find/open file: %s\r\n", names[0]);
        say(s, t);
        r = 2;
    }
    return s->level = r;
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

/* ---- CHOICE and SET /P (2026-11-25: phase G's installer, and DOS batch files with no FreeDOS CHOICE.COM)
 *
 * CHOICE [/C[:]keys] [/N] [/S] [/T[:]c,nn] [text]: MS-DOS 6's. The text, then "[Y,N]?" (unless /N), and a key
 * from the list (any case, unless /S); the key is written and the line ended, and ERRORLEVEL is its place in the
 * list (1 for the first), for IF ERRORLEVEL as from MS-DOS's CHOICE.COM. A key not in the list beeps. Ctrl+C:
 * 0. With nobody at a keyboard, /T's key, else the first, as if typed. /T's time is not waited out: the host has
 * no timed wait for a key yet, so at a keyboard CHOICE waits for one.
 *
 * SET /P name=prompt (or SET /P "name=prompt", which keeps a space at the prompt's end): the prompt, then a line
 * typed (Backspace, Esc clears it, Enter ends it, Ctrl+C gives up) as the variable's value. An empty line, Ctrl+C, or nobody at a keyboard: the variable as it was, exit code 1. */
static int choice_at(const struct sh *s, int key)
{
    int i;
    for (i = 0; s->tool.choices[i]; i++)
        if (s->tool.sens ? s->tool.choices[i] == key : upper(s->tool.choices[i]) == upper(key))
            return i + 1;
    return 0;
}

static int choice_step(struct sh *s, int key)
{
    char e[4];
    int at;
    if (key == 3) {
        out(s, "^C\r\n");
        s->level = 0;
        return 0;
    }
    if (!(at = choice_at(s, key))) {
        out(s, "\a");
        if (s->h->key && s->h->key(s->h->ctx) == SH_UNDER_WAY)
            return SH_UNDER_WAY;
        /* (nobody to press another, as when a job's input has ended: /T's answer, else the first; never a key the
           batch file didn't name as the answer for no one, so a question whose /T says "stop" stops) */
        at = s->tool.def ? choice_at(s, s->tool.def) : 1;
    }
    e[0] = s->tool.choices[at - 1], e[1] = '\r', e[2] = '\n', e[3] = 0;
    out(s, e);
    s->level = at;
    return at;
}

static int cmd_choice(struct sh *s, int argc, char **argv)
{
    char text[SH_LINE], prompt[SH_LINE + 64];
    int i, n = 0, quiet = 0, def = 0, k;
    unsigned t = 0;
    sh_fmt(s->tool.choices, sizeof s->tool.choices, "%s", "YN");
    s->tool.sens = 0, s->tool.def = 0;
    text[0] = 0;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (a[0] == '/' && (upper(a[1]) == 'C') && (!a[2] || a[2] == ':' || a[2] != ' ')) {
            const char *keys = a + 2 + (a[2] == ':');
            if (!*keys && i + 1 < argc)
                keys = argv[++i];               /* (/C YN) */
            if (!*keys) {
                err(s, "CHOICE: /C needs its keys\r\n");
                return 255;
            }
            sh_fmt(s->tool.choices, sizeof s->tool.choices, "%s", keys);
        } else if (a[0] == '/' && upper(a[1]) == 'N' && !a[2]) {
            quiet = 1;
        } else if (a[0] == '/' && upper(a[1]) == 'S' && !a[2]) {
            s->tool.sens = 1;
        } else if (a[0] == '/' && upper(a[1]) == 'T') {
            const char *v = a + 2 + (a[2] == ':');
            if (!v[0] || v[1] != ',' || v[2] < '0' || v[2] > '9') {
                err(s, "CHOICE: /T:key,seconds\r\n");
                return 255;
            }
            def = (unsigned char)v[0];
            for (v += 2; *v >= '0' && *v <= '9'; v++)
                t = t * 10 + (unsigned)(*v - '0');
        } else if (a[0] == '/') {
            sh_fmt(prompt, sizeof prompt, "CHOICE: invalid switch - %s\r\n", a);
            err(s, prompt);
            return 255;
        } else {
            n += sh_fmt(text + n, sizeof text - (unsigned)n, "%s%s", n ? " " : "", a);
        }
    }
    (void)t;
    if (!s->tool.sens)                          /* (as MS-DOS's: the keys shown and answered in capitals) */
        for (k = 0; s->tool.choices[k]; k++)
            s->tool.choices[k] = (char)upper((unsigned char)s->tool.choices[k]);
    if (n >= 2 && text[0] == '"' && text[n - 1] == '"') {
        memmove(text, text + 1, (size_t)n - 2);
        text[n - 2] = 0;
    }
    if (def && !choice_at(s, def)) {
        err(s, "CHOICE: /T's key isn't one of /C's\r\n");
        return 255;
    }
    s->tool.def = def;
    n = sh_fmt(prompt, sizeof prompt, "%s", text);
    if (!quiet) {
        n += sh_fmt(prompt + n, sizeof prompt - (unsigned)n, "%s[", text[0] ? " " : "");
        for (k = 0; s->tool.choices[k]; k++)
            n += sh_fmt(prompt + n, sizeof prompt - (unsigned)n, "%s%c", k ? "," : "", s->tool.choices[k]);
        sh_fmt(prompt + n, sizeof prompt - (unsigned)n, "]?");
    }
    out(s, prompt);
    if (s->h->key && s->h->key(s->h->ctx) == SH_UNDER_WAY) {
        s->tool.step = choice_step;
        s->key_for = 3;
        return SH_UNDER_WAY;
    }
    return choice_step(s, def ? def : (unsigned char)s->tool.choices[0]);     /* (nobody at a keyboard) */
}

static int set_p_step(struct sh *s, int key)
{
    char e[2];
    for (;;) {
        if (key == 13) {
            out(s, "\r\n");
            if (!s->tool.nedit)
                return 1;
            s->tool.edit[s->tool.nedit] = 0;
            var_set(s, s->tool.var, s->tool.edit);
            return 0;
        }
        if (key == 3) {
            out(s, "^C\r\n");
            return 1;
        }
        if (key == 8 && s->tool.nedit) {
            s->tool.nedit--;
            out(s, "\b \b");
        } else if (key == 27) {
            for (; s->tool.nedit; s->tool.nedit--)
                out(s, "\b \b");
        } else if ((key >= 32 && key != 127) && s->tool.nedit < (int)sizeof s->tool.edit - 1) {
            s->tool.edit[s->tool.nedit++] = (char)key;
            e[0] = (char)key, e[1] = 0;
            out(s, e);
        }
        if (s->h->key && s->h->key(s->h->ctx) == SH_UNDER_WAY)
            return SH_UNDER_WAY;
        out(s, "\r\n");                         /* (nobody at a keyboard any more: nothing set) */
        return 1;
    }
}

static int cmd_set_p(struct sh *s, const char *raw)
{
    const char *eq;
    char quoted[SH_LINE];
    size_t n;
    if (raw[0] == '"') {                        /* SET /P "name=prompt ": to its last quote */
        const char *end = strrchr(raw + 1, '"');
        n = end ? (size_t)(end - raw - 1) : strlen(raw + 1);
        if (n >= sizeof quoted)
            n = sizeof quoted - 1;
        memcpy(quoted, raw + 1, n);
        quoted[n] = 0;
        raw = quoted;
    }
    eq = strchr(raw, '=');
    if (!eq || eq == raw) {
        err(s, "SET /P name=prompt\r\n");
        return 1;
    }
    n = (size_t)(eq - raw) < sizeof s->tool.var - 1 ? (size_t)(eq - raw) : sizeof s->tool.var - 1;
    memcpy(s->tool.var, raw, n);
    while (n && (s->tool.var[n - 1] == ' ' || s->tool.var[n - 1] == '\t'))
        n--;
    s->tool.var[n] = 0;
    s->tool.nedit = 0;
    out(s, eq + 1);
    if (s->h->key && s->h->key(s->h->ctx) == SH_UNDER_WAY) {
        s->tool.step = set_p_step;
        s->key_for = 3;
        return SH_UNDER_WAY;
    }
    out(s, "\r\n");
    return 1;                                   /* (nobody at a keyboard: nothing typed) */
}

static int cmd_set(struct sh *s, int argc, char **argv, const char *raw)
{
    const char *eq;
    int i;
    char line[300];
    if (argc > 1 && same(argv[1], "/P")) {      /* SET /P name=prompt */
        const char *p = strchr(raw, ' ');
        while (p && *p == ' ')
            p++;
        p = p ? strchr(p, ' ') : 0;             /* (past /P) */
        while (p && *p == ' ')
            p++;
        return cmd_set_p(s, p ? p : "");
    }
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
    "START", "DF", "DU", "CALL", "FOR", "GOTO", "IF", "PAUSE", "REM", "SHIFT", "VOL", "TRUENAME", "BREAK", "VERIFY",
    "LH", "LOADHIGH", "CTTY", "ATTRIB", "XCOPY", "MORE", "DELTREE", "CHOICE", "FIND", "SORT",
    "FC", "COMP", 0 };

static const char *const helps[] = {
    "CD [path]           the folder (of a drive: CD X:); X: changes drive",
    "DIR [path] [/W] [/B] a folder's files: wide, or names alone",
    "COPY from [to]      files (* and ?), into a folder or a file",
    "MOVE from [to]      ... moved",
    "REN from to         a file's new name",
    "DEL files           files deleted (* and ?)",
    "XCOPY from [to] [/S] [/E] [/H] ...  files copied, with /S the folders in them",
    "DELTREE [/Y] path ...  a folder and all in it deleted (asked first)",
    "MORE [file]         a page at a time (also: command | MORE)",
    "FIND [/V] [/C] [/N] [/I] \"text\" [files]  the lines with the text (also: command | FIND \"text\")",
    "SORT [/R] [/+n] [files]  the lines in order (also: command | SORT)",
    "FC [/A /C /L /LBn /N /T /W /nnnn] file1 file2, FC /B file1 file2  where two files differ",
    "COMP file1 file2 [/D] [/A] [/L] [/N=n] [/C]  two files' bytes compared",
    "ATTRIB [+R|-R] [+A|-A] [+S|-S] [+H|-H] [path]  attributes changed, or listed",
    "MD path, RD path    a folder made, removed",
    "DF [drive: ...]     the drives: their size, what is used and free",
    "DU [folder] [/S]    what a folder's files take, with all under it",
    "TYPE file           a file shown",
    "ECHO [text]         text written",
    "SET [name[=value]]  the variables; PATH and PROMPT as SET does",
    "SET /P name=prompt  a variable from a line typed",
    "CHOICE [/C:keys] [/N] [/S] [/T:c,nn] [text]  a key asked for: ERRORLEVEL its place in keys",
    "CLS                 a clean window",
    "HISTORY             the lines typed before (Up and Down)",
    "WHERE program       where it is along PATH",
    "START program       a program started beside the terminal",
    "VOL [drive:], TRUENAME [path]  a drive's label; a path in full",
    "name.BAT [words]    a batch file run: %0 to %9, IF, GOTO, CALL, FOR, SHIFT, PAUSE, REM, @",
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
    if (echo_dot(w) || w[0] == '@' || w[0] == ':')
        return 1;
    for (i = 0; (name = command_name(s, i)) != 0; i++)
        if (same(w, name))
            return 1;
    return strlen(w) == 2 && w[1] == ':';
}

/* DOS's own commands that neither this shell nor its host has: the line is the DOS's COMMAND.COM's, whole. (DATE,
   TIME and CHCP are the host's if it has them: the kernel's does.) */
static int dos_own(const struct sh *s, const char *w)
{
    static const char *const names[] = { "DATE", "TIME", "CHCP", 0 };
    int i;
    for (i = 0; names[i]; i++)
        if (same(w, names[i]))
            return !is_builtin(s, w);
    return 0;
}

int sh_builtin(const struct sh *s, const char *line)
{
    char first[32];
    unsigned n = 0;
    while (*line == ' ' || *line == '\t')
        line++;
    for (; line[n] && line[n] != ' ' && line[n] != '\t' && n < sizeof first - 1; n++)
        first[n] = line[n];
    first[n] = 0;
    return is_builtin(s, first) && !strpbrk(line, "|<");
}

/* ---- running a program: the host's. 1: under way; 0: started and left; -1: not run (and said). */

static int batch_start(struct sh *s, const char *path, const char *line, int call);

#define BATCH_BEGUN 2                   /* run_program(): the program was a batch file, whose lines come next */

/* how: 0 run and waited for; 1 started and left to itself (START); 2 CALLed (a batch file comes back to the one
   that called it). */
static int run_program(struct sh *s, const char *line, int how)
{
    int detach = how == 1;
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
    if ((strpbrk(line, "<>|") && !s->h->io) || dos_own(s, first)
        || (!s->nbat && s->h->as_typed && s->h->as_typed(s->h->ctx))) {
        /* a pipe, a redirection, a command of DOS's own, or a host that finds programs itself: the line whole
           (in a batch file the shell looks all the same: a batch file named there takes this one's place) */
        sh_fmt(command, sizeof command, "%s", line);
    } else if (find_program(s, first, prog, sizeof prog) == 0) {
        size_t pl = strlen(prog);
        if (!detach && pl > 4 && same(prog + pl - 4, ".BAT"))    /* a batch file: this shell runs its lines */
            return batch_start(s, prog, line, how == 2) == 0 ? BATCH_BEGUN : -1;
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

/* ---- batch files (S4; docs/v1/shell.md): frames, their lines, labels */

static void loop_end(struct sh *s)
{
    if (s->loop.items)
        s->h->free(s->h->ctx, s->loop.items);
    s->loop.items = 0;
    s->loop.at = 0;
}

static void batch_pop(struct sh *s)
{
    struct sh_batch *b = s->bat[--s->nbat];
    s->h->free(s->h->ctx, b->line);
    s->h->free(s->h->ctx, b);
}

static void batch_end(struct sh *s)             /* the one whose lines are read has ended */
{
    if (!s->nbat)
        return;
    batch_pop(s);
    if (!s->nbat) {
        s->echo_off = s->echo_was;              /* (ECHO as it was before the first of them) */
        s->errorlevel = s->level;               /* (a batch file's own result is its last program's exit code, as
                                                   COMMAND /C's is: what && and || after it, and a line's end, see) */
    }
}

static void batch_end_all(struct sh *s)
{
    while (s->nbat)
        batch_end(s);
    loop_end(s);
    s->rest[0] = 0;
}

/* A batch file begins: its lines are read from the next turn of next_command(). CALLed, it is put on top of the
   one that called it; named without CALL in a batch file, it takes that one's place. 0, or -1 (and said). */
static int batch_start(struct sh *s, const char *path, const char *line, int call)
{
    struct sh_batch *b;
    int first = !s->nbat;
    if (s->nbat && !call)
        batch_pop(s);
    if (s->nbat == SH_BATCH) {
        err(s, "Batch files nested too deep\r\n");
        return -1;
    }
    if (!(b = s->h->alloc(s->h->ctx, sizeof *b)))
        return -1;
    memset(b, 0, sizeof *b);
    sh_fmt(b->path, sizeof b->path, "%s", path);
    if (!(b->line = copy_of(s, line))) {
        s->h->free(s->h->ctx, b);
        return -1;
    }
    b->argc = split_words(b->line, b->arg, SH_ARGS);
    if (first)
        s->echo_was = s->echo_off;
    s->bat[s->nbat++] = b;
    if (!call) {                                /* (what was left of the line that named it is not run) */
        s->rest[0] = 0;
        loop_end(s);
    }
    return 0;
}

/* The file's bytes from `from`, a line at a time into out (no CR, no LF): each line's first byte's place is
   given to see(), which says 1 to stop there. The place after the last line seen, or the file's end; -1 if the
   file can't be opened. One opening, read through in pieces. */
static long batch_scan(struct sh *s, const char *path, unsigned long from, char *out, unsigned room,
                       int (*see)(struct sh *s, const char *line, void *arg), void *arg)
{
    char buf[256];
    void *f = s->h->open(s->h->ctx, path, 'r');
    unsigned long pos = from, start = from;
    unsigned k = 0;
    int n, i, stop = 0, any = 0;
    if (!f)
        return -1;
    if (from && s->h->seek) {
        if (s->h->seek(s->h->ctx, f, from) != 0)
            stop = 1;
    } else {
        unsigned long skip = from;
        while (skip && !stop) {
            n = s->h->read(s->h->ctx, f, buf, skip < sizeof buf ? (int)skip : (int)sizeof buf);
            if (n <= 0)
                stop = 1;
            else
                skip -= (unsigned long)n;
        }
    }
    while (!stop && (n = s->h->read(s->h->ctx, f, buf, sizeof buf)) > 0) {
        for (i = 0; i < n && !stop; i++) {
            char c = buf[i];
            pos++;
            if (c == 0x1A) {                    /* (DOS's end-of-file mark) */
                pos--;
                n = 0;
                break;
            }
            any = 1;
            if (c == '\n') {
                out[k] = 0;
                k = 0;
                any = 0;
                stop = see(s, out, arg);
                start = pos;
            } else if (c != '\r' && k < room - 1) {
                out[k++] = c;
            }
        }
        if (!n)
            break;
    }
    if (!stop && any) {                         /* (a last line with no line end) */
        out[k] = 0;
        see(s, out, arg);
        start = pos;
    }
    s->h->close(s->h->ctx, f);
    return (long)start;
}

static int see_one(struct sh *s, const char *line, void *arg)
{
    (void)s, (void)line;
    *(int *)arg = 1;
    return 1;
}

/* The next line of the batch file on top: 1 with it; 0 at the file's end; -1 if the file has gone. */
static int batch_read(struct sh *s, char *out, unsigned room)
{
    struct sh_batch *b = s->bat[s->nbat - 1];
    int got = 0;
    long end = batch_scan(s, b->path, b->pos, out, room, see_one, &got);
    if (end < 0) {
        err(s, "Batch file missing\r\n");
        return -1;
    }
    b->pos = (unsigned long)end;
    return got;
}

/* A label's line: ":NAME"; eight characters of the name count, and its case does not. */
static int see_label(struct sh *s, const char *line, void *arg)
{
    const char *want = arg;
    int i;
    (void)s;
    while (*line == ' ' || *line == '\t')
        line++;
    if (*line++ != ':')
        return 0;
    for (i = 0; i < 8; i++) {
        int a = (unsigned char)line[i], b = (unsigned char)want[i];
        if (a == ' ' || a == '\t')
            a = 0;
        if (upper(a) != upper(b))
            return 0;
        if (!a)
            break;
    }
    return 1;
}

static int cmd_goto(struct sh *s, const char *t)
{
    struct sh_batch *b = s->nbat ? s->bat[s->nbat - 1] : 0;
    char label[16], line[SH_LINE];
    unsigned n = 0;
    long at;
    if (!b)
        return s->errorlevel;                   /* (at the prompt: nothing to go to) */
    while (*t == ' ' || *t == '\t' || *t == ':')
        t++;
    for (; t[n] && t[n] != ' ' && t[n] != '\t' && n < 8; n++)
        label[n] = t[n];
    label[n] = 0;
    line[0] = 0;
    at = batch_scan(s, b->path, 0, line, sizeof line, see_label, label);
    if (at < 0 || !see_label(s, line, label)) { /* (the last line seen is the label's, if it was found) */
        err(s, "Label not found\r\n");
        batch_end_all(s);
        return s->errorlevel;
    }
    b->pos = (unsigned long)at;
    s->rest[0] = 0;                             /* (the rest of the GOTO's own line is not run) */
    loop_end(s);
    return s->errorlevel;
}

/* t begins with the word w (in capitals), a space or the text's end after it: the place after it, else 0. */
static const char *word(const char *t, const char *w)
{
    size_t n = strlen(w), i;
    for (i = 0; i < n; i++)
        if (upper((unsigned char)t[i]) != w[i])
            return 0;
    if (t[n] && t[n] != ' ' && t[n] != '\t')
        return 0;
    t += n;
    while (*t == ' ' || *t == '\t')
        t++;
    return t;
}

static int run_line(struct sh *s, char *line);

/* A file, or any file a pattern names, is there (IF EXIST): a folder's NUL is, if the folder is. */
static int exists(struct sh *s, const char *name)
{
    char full[SH_PATH], dir[SH_PATH], pat[64];
    struct sh_dirent e, *list = 0;
    int n, i, yes = 0;
    full_path(s, name, full, sizeof full);
    if (!strpbrk(name, "*?")) {
        if (same(path_name(full), "NUL")) {
            path_up(full);
            return s->h->is_dir(s->h->ctx, full);
        }
        return s->h->stat(s->h->ctx, full, &e) == 0 && !e.dir;
    }
    split_pattern(s, name, dir, sizeof dir, pat, sizeof pat);
    n = s->h->dir_read(s->h->ctx, dir, &list);
    for (i = 0; i < n && !yes; i++)
        yes = !list[i].dir && match(pat, list[i].name);
    if (n >= 0)
        s->h->dir_free(s->h->ctx, list);
    return yes;
}

/* IF [NOT] ERRORLEVEL n | EXIST name | a==b  command */
static int cmd_if(struct sh *s, char *t)
{
    const char *u;
    int not = 0, yes = 0;
    if ((u = word(t, "NOT")) != 0) {
        not = 1;
        t = (char *)u;
    }
    if ((u = word(t, "ERRORLEVEL")) != 0 && *u >= '0' && *u <= '9') {
        int n = 0;
        while (*u >= '0' && *u <= '9')
            n = n * 10 + (*u++ - '0');
        yes = s->level >= n;
        t = (char *)u;
    } else if ((u = word(t, "EXIST")) != 0 && *u) {
        char name[SH_PATH];
        unsigned n = 0;
        while (*u && *u != ' ' && *u != '\t' && n < sizeof name - 1)
            name[n++] = *u++;
        name[n] = 0;
        yes = exists(s, name);
        t = (char *)u;
    } else {
        char *eq = strstr(t, "=="), *a = t, *b, *ae, *be;
        if (!eq) {
            err(s, "Syntax error\r\n");
            return s->errorlevel;
        }
        for (ae = eq; ae > a && (ae[-1] == ' ' || ae[-1] == '\t'); ae--)
            ;
        for (b = eq + 2; *b == ' ' || *b == '\t'; b++)
            ;
        for (be = b; *be && *be != ' ' && *be != '\t'; be++)
            ;
        yes = ae - a == be - b && !strncmp(a, b, (size_t)(ae - a));
        t = be;
    }
    while (*t == ' ' || *t == '\t')
        t++;
    return yes != not ? run_line(s, t) : s->errorlevel;
}

/* FOR %v IN (set) DO command: the set's words, and the files its patterns name, are kept; next_command() runs
   the command for each in turn (a program among them is waited for like any other). */
static int cmd_for(struct sh *s, const char *t)
{
    char set[SH_LINE], *w, *items;
    const char *u, *open, *close;
    unsigned room = 2048, k = 0;
    char var;
    if (*t != '%' || !t[1] || !(u = word(t + 2, "")) || !(u = word(u, "IN")) || *u != '('
        || !(close = strchr(u, ')')) || !(open = u) || !(u = word(close + 1 + strspn(close + 1, " \t"), "DO")) || !*u) {
        err(s, "Syntax error\r\n");
        return s->errorlevel;
    }
    var = t[1];
    sh_fmt(set, sizeof set, "%.*s", (int)(close - open - 1), open + 1);
    if (!(items = s->h->alloc(s->h->ctx, room)))
        return s->errorlevel;
    for (w = set; *w;) {
        char *e;
        while (*w == ' ' || *w == '\t' || *w == ',' || *w == ';')
            w++;
        if (!*w)
            break;
        for (e = w; *e && *e != ' ' && *e != '\t' && *e != ',' && *e != ';'; e++)
            ;
        if (*e)
            *e++ = 0;
        if (strpbrk(w, "*?")) {                 /* the files it names, each with the folder as it was written */
            char dir[SH_PATH], pat[64];
            struct sh_dirent *list = 0;
            const char *name = strrchr(w, '\\') ? strrchr(w, '\\') + 1 : strchr(w, ':') ? strchr(w, ':') + 1 : w;
            int n, i;
            split_pattern(s, w, dir, sizeof dir, pat, sizeof pat);
            n = s->h->dir_read(s->h->ctx, dir, &list);
            for (i = 0; i < n; i++)
                if (!list[i].dir && match(pat, list[i].name)
                    && k + (unsigned)(name - w) + strlen(list[i].name) + 2 < room)
                    k += (unsigned)sh_fmt(items + k, room - k, "%.*s%s", (int)(name - w), w, list[i].name) + 1;
            if (n >= 0)
                s->h->dir_free(s->h->ctx, list);
        } else if (k + strlen(w) + 2 < room) {
            k += (unsigned)sh_fmt(items + k, room - k, "%s", w) + 1;
        }
        w = e;
    }
    items[k] = 0;
    loop_end(s);
    s->loop.var = var;
    s->loop.items = items;
    s->loop.at = items;
    sh_fmt(s->loop.cmd, sizeof s->loop.cmd, "%s", u);
    return s->errorlevel;
}

/* The prompt's text (PROMPT's codes put in): its length. */
static size_t prompt_text(const struct sh *s, char *t, size_t room)
{
    const char *p = var_get(s, "PROMPT");
    size_t n = 0;
    if (!p)
        p = "$P$G";
    for (; *p && n < room - 80; p++) {
        if (*p != '$' || !p[1]) {
            t[n++] = *p;
            continue;
        }
        switch (upper((unsigned char)*++p)) {
        case 'P': n += (size_t)sh_fmt(t + n, (unsigned)(room - n), "%s", s->cwd[s->drive]); break;
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
    return n;
}

/* ---- redirection and pipes (S4b): the shell's own, around built-ins and programs alike */

#define THEN '\001'                     /* in `rest`: the next command whatever came of this one (a pipe's steps) */
#define GONE '\002'                     /* a step of the shell's own: this file deleted, quietly */

/* The first of c in t that is not between quotation marks, or 0. */
static char *bare(char *t, const char *c)
{
    int q = 0;
    for (; *t; t++) {
        if (*t == '"')
            q = !q;
        else if (!q && strchr(c, *t))
            return t;
    }
    return 0;
}

/* < file, > file and >> file taken out of a command: the files' full paths ("NUL" as it is; "" for none).
   0, or -1 (a sign with no file after it). */
static int take_redirs(struct sh *s, char *line, char *in, char *outf, unsigned room, int *append)
{
    char *p;
    in[0] = outf[0] = 0;
    *append = 0;
    while ((p = bare(line, "<>")) != 0) {
        char name[SH_PATH], *e = p + 1, *dst = *p == '<' ? in : outf;
        unsigned n = 0;
        int app = 0;
        if (*p == '>' && *e == '>')
            app = 1, e++;
        while (*e == ' ' || *e == '\t')
            e++;
        if (*e == '"') {
            for (e++; *e && *e != '"' && n < sizeof name - 1; e++)
                name[n++] = *e;
            if (*e)
                e++;
        } else {
            for (; *e && *e != ' ' && *e != '\t' && *e != '<' && *e != '>' && n < sizeof name - 1; e++)
                name[n++] = *e;
        }
        name[n] = 0;
        if (!n)
            return -1;
        if (same(name, "NUL") || same(name, "NUL:"))
            sh_fmt(dst, room, "NUL");
        else
            full_path(s, name, dst, room);
        if (dst == outf)
            *append = app;
        memmove(p, e, strlen(e) + 1);           /* (the sign and its file gone: what was before and after stays) */
    }
    return 0;
}

/* A line with pipes (a | b | c) made the steps that do it, as DOS does with one program at a time: a's output
   to a temporary file, b reading it and writing the next, each file deleted when it has been read. The files
   are made first, empty, and written by adding to them, so that a step which writes nothing (an IF that is not
   so) still leaves the next one a file to read. In TEMP's folder (or TMP's), else the drive's root. The steps,
   THEN between them, into outb: 0, or -1 (too long, or no file could be made). */
static int pipe_steps(struct sh *s, char *cmd, char *outb, unsigned room)
{
    const char *dir = var_get(s, "TEMP");
    char tmp[2][SH_PATH], base[SH_PATH], *stage = cmd, *bar;
    unsigned k = 0;
    int i = 0;
    if (!dir)
        dir = var_get(s, "TMP");
    full_path(s, dir && *dir ? dir : "\\", base, sizeof base);
    tmp[0][0] = tmp[1][0] = 0;
    for (;; i++) {
        char name[16], *e;
        void *f;
        if ((bar = bare(stage, "|")) != 0)
            *bar = 0;
        while (*stage == ' ' || *stage == '\t')
            stage++;
        for (e = stage + strlen(stage); e > stage && (e[-1] == ' ' || e[-1] == '\t'); e--)
            ;
        *e = 0;
        if (!*stage)
            return -1;
        if (bar) {                              /* its output's file */
            sh_fmt(name, sizeof name, "GP%03u%03u.TMP", s->id % 1000, (unsigned)(s->pipes++ % 1000));
            path_join(tmp[i & 1], sizeof tmp[0], base, name);
            if (!(f = s->h->open(s->h->ctx, tmp[i & 1], 'w')))
                return -1;
            s->h->close(s->h->ctx, f);
        }
        k += (unsigned)sh_fmt(outb + k, room - k, "%s%s%s%s%s%s", k ? "\001" : "", stage, i ? " < " : "",
                              i ? tmp[(i - 1) & 1] : "", bar ? " >> " : "", bar ? tmp[i & 1] : "");
        if (i)
            k += (unsigned)sh_fmt(outb + k, room - k, "\001\002%s", tmp[(i - 1) & 1]);
        if (k >= room - 1)
            return -1;
        if (!bar)
            return 0;
        stage = bar + 1;
    }
}

/* ---- a command: its exit code, or SH_UNDER_WAY */

static int run_one(struct sh *s, const char *cmd)
{
    char line[SH_LINE];
    while (*cmd == ' ' || *cmd == '\t')
        cmd++;
    expand(s, cmd, line, sizeof line);
    return run_line(s, line);
}

/* A command with its variables put in already (line is changed). */
static int run_line(struct sh *s, char *line)
{
    char copy[SH_LINE], *argv[32], target[SH_PATH], source[SH_PATH];
    const char *u;
    int argc, r = 0, append = 0;
    source[0] = target[0] = 0;
    while (*line == ' ' || *line == '\t' || *line == '@')
        line++;
    {
        char *e = line + strlen(line);
        while (e > line && (e[-1] == ' ' || e[-1] == '\t'))
            *--e = 0;
    }
    if (*line == ':')                           /* (a label) */
        return s->errorlevel;
    if (*line == GONE) {                        /* (a pipe's file, read: gone) */
        s->h->unlink(s->h->ctx, line + 1);
        return s->errorlevel;
    }
    /* the batch language: these take the rest of the line as it stands (a > in it is the inner command's) */
    if ((u = word(line, "REM")) != 0)
        return s->errorlevel;
    if ((u = word(line, "IF")) != 0)
        return cmd_if(s, (char *)u);
    if ((u = word(line, "FOR")) != 0)
        return cmd_for(s, u);
    if ((u = word(line, "GOTO")) != 0)
        return cmd_goto(s, u);
    if ((u = word(line, "CALL")) != 0) {
        char first[128], found[SH_PATH];
        unsigned n = 0;
        size_t fl;
        for (; u[n] && u[n] != ' ' && u[n] != '\t' && n < sizeof first - 1; n++)
            first[n] = u[n];
        first[n] = 0;
        if (n && !is_builtin(s, first) && find_program(s, first, found, sizeof found) == 0
            && (fl = strlen(found)) > 4 && same(found + fl - 4, ".BAT"))
            return batch_start(s, found, u, 1) == 0 ? s->errorlevel : 1;
        return run_line(s, (char *)u);          /* (anything else is run as it would be without CALL) */
    }
    if ((u = word(line, "LH")) != 0 || (u = word(line, "LOADHIGH")) != 0) {
        while (*u == '/') {                     /* (its switches: upper memory is GLOS's to give) */
            while (*u && *u != ' ' && *u != '\t')
                u++;
            while (*u == ' ' || *u == '\t')
                u++;
        }
        return run_line(s, (char *)u);
    }
    sh_fmt(copy, sizeof copy, "%s", line);
    argc = split_words(copy, argv, 32);
    if (!argc)
        return s->errorlevel;
    if (!is_builtin(s, argv[0])) {              /* a program: its standard input and output are the files named */
        int st, io = 0;
        if (bare(line, "<>") && s->h->io) {
            if (take_redirs(s, line, source, target, sizeof target, &append) != 0) {
                err(s, "Syntax error\r\n");
                return 1;
            }
            s->h->io(s->h->ctx, source[0] ? source : 0, target[0] ? target : 0, append);
            io = 1;
        }
        st = run_program(s, line, 0);
        if (io && st != 1)                      /* (no program was started with them: a batch file, as under
                                                   MS-DOS, is not redirected by the line that names it) */
            s->h->io(s->h->ctx, 0, 0, 0);
        return st == BATCH_BEGUN ? s->errorlevel : st > 0 ? SH_UNDER_WAY : st < 0 ? 1 : 0;
    }
    if (bare(line, "<>")) {                     /* a built-in: its output to the file (MORE, FIND, SORT read input) */
        if (take_redirs(s, line, source, target, sizeof target, &append) != 0) {
            err(s, "Syntax error\r\n");
            return 1;
        }
        sh_fmt(copy, sizeof copy, "%s", line);
        argc = split_words(copy, argv, 32);
        if (!argc)
            return s->errorlevel;
        if (same(target, "NUL")) {
            s->discard = 1;
        } else if (target[0] && !(s->redirect = s->h->open(s->h->ctx, target, append ? 'a' : 'w'))) {
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
    } else if (same(argv[0], "XCOPY")) {
        r = cmd_xcopy(s, argc, argv);
    } else if (same(argv[0], "MORE")) {
        r = cmd_more(s, argc, argv, source);
    } else if (same(argv[0], "FIND")) {
        r = cmd_find(s, line, source);
    } else if (same(argv[0], "SORT")) {
        r = cmd_sort(s, argc, argv, source);
    } else if (same(argv[0], "FC")) {
        r = cmd_fc(s, argc, argv);
    } else if (same(argv[0], "COMP")) {
        r = cmd_comp(s, argc, argv);
    } else if (same(argv[0], "DELTREE")) {
        r = cmd_deltree(s, argc, argv);
    } else if (same(argv[0], "CHOICE")) {
        r = cmd_choice(s, argc, argv);
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
    } else if (same(argv[0], "DF")) {
        r = cmd_df(s, argc, argv);
    } else if (same(argv[0], "DU")) {
        r = cmd_du(s, argc, argv);
    } else if (same(argv[0], "TYPE")) {
        r = cmd_type(s, argc, argv);
    } else if (same(argv[0], "ATTRIB")) {
        r = cmd_attrib(s, argc, argv);
    } else if (same(argv[0], "ECHO") || echo_dot(argv[0])) {
        const char *t = line + 4;
        if (*t == '.') {
            say(s, t + 1);
            say(s, "\r\n");
        } else {
            while (*t == ' ')
                t++;
            if (!*t) {
                say(s, s->echo_off ? "ECHO is off\r\n" : "ECHO is on\r\n");
            } else if (same(t, "ON") || same(t, "OFF")) {
                s->echo_off = same(t, "OFF");
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
        r = s->h->clear(s->h->ctx);
    } else if (same(argv[0], "VER")) {
        char line[64];                          /* (the API's version as the headers have it: it said 2.1 until
                                                   2026-11-16, two minors behind) */
        sh_fmt(line, sizeof line, "GLOS shell 1.0 (GLOS API %u.%u)\r\n", (unsigned)GLOS_MAJOR, (unsigned)GLOS_MINOR);
        say(s, line);
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
    } else if (same(argv[0], "SHIFT")) {
        if (s->nbat && s->bat[s->nbat - 1]->shift < SH_ARGS)
            s->bat[s->nbat - 1]->shift++;
        r = s->errorlevel;
    } else if (same(argv[0], "PAUSE")) {
        say(s, "Press any key to continue . . .\r\n");
        r = s->errorlevel;
        if (s->h->key && s->h->key(s->h->ctx) == SH_UNDER_WAY) {
            s->key_for = 1;
            r = SH_UNDER_WAY;
        }
    } else if (same(argv[0], "VOL")) {
        int d = argc > 1 && argv[1][0] && argv[1][1] == ':' ? upper((unsigned char)argv[1][0]) - 'A' : s->drive;
        unsigned long total, left;
        char label[12], l[80];
        if (!s->h->space || s->h->space(s->h->ctx, d, &total, &left, label) != 0) {
            err(s, "Invalid drive specification\r\n");
            r = 1;
        } else {
            if (label[0])
                sh_fmt(l, sizeof l, " Volume in drive %c is %s\r\n", 'A' + d, label);
            else
                sh_fmt(l, sizeof l, " Volume in drive %c has no label\r\n", 'A' + d);
            say(s, l);
        }
    } else if (same(argv[0], "TRUENAME")) {
        char full[SH_PATH];
        full_path(s, argc > 1 ? argv[1] : ".", full, sizeof full);
        say(s, full);
        say(s, "\r\n");
    } else if (same(argv[0], "BREAK") || same(argv[0], "VERIFY")) {
        if (argc < 2)                           /* (GLOS has neither switch: taken, and said to be off) */
            say(s, upper((unsigned char)argv[0][0]) == 'B' ? "BREAK is off\r\n" : "VERIFY is off\r\n");
        r = s->errorlevel;
    } else if (same(argv[0], "CTTY")) {
        err(s, "CTTY: GLOS has no other console to give.\r\n");
        r = 1;
    } else {
        r = s->h->extra ? s->h->extra(s->h->ctx, argc, argv, line) : SH_NOT_MINE;       /* (SH_UNDER_WAY: as a program
                                                                                           is) */
        if (r == SH_NOT_MINE)
            r = 1;
        else if (r != SH_UNDER_WAY)
            s->level = r;                       /* (a command of the host's is as a program to IF ERRORLEVEL) */
    }
    if (r == SH_UNDER_WAY && s->key_for == 3)
        return r;                               /* (a built-in waiting for a key: its output's file kept: sh_key) */
    if (s->redirect) {
        s->h->close(s->h->ctx, s->redirect);
        s->redirect = 0;
    }
    s->discard = 0;
    return r;
}

/* ---- a line: commands joined by && and || */

/* The question after Ctrl+C in a batch file or a FOR: 1 if a key is waited for (sh_key() goes on). With no
   keyboard here they end at once. */
static int ask_stop(struct sh *s)
{
    s->rest[0] = 0;
    out(s, "\r\nTerminate batch job (Y/N)?");
    if (s->h->key && s->h->key(s->h->ctx) == SH_UNDER_WAY) {
        s->key_for = 2;
        s->under_way = 1;
        return 1;
    }
    out(s, "\r\n");
    batch_end_all(s);
    return 0;
}

static void next_command(struct sh *s)
{
    for (;;) {
        while (s->rest[0] && !s->under_way) {
            char cmd[SH_LINE];
            const char *a = strstr(s->rest, "&&"), *o = strstr(s->rest, "||"), *t = strchr(s->rest, THEN);
            const char *cut = a && (!o || a < o) ? a : o;
            int op, run, r, w = 2;
            size_t n;
            if (t && (!cut || t < cut))
                cut = t, w = 1;
            op = !cut || w == 1 ? 0 : cut == a ? 1 : 2;
            n = cut ? (size_t)(cut - s->rest) : strlen(s->rest);
            memcpy(cmd, s->rest, n);
            cmd[n] = 0;
            run = s->rest_op == 0 || (s->rest_op == 1 && s->errorlevel == 0) || (s->rest_op == 2 && s->errorlevel != 0);
            memmove(s->rest, cut ? cut + w : s->rest + n, strlen(cut ? cut + w : s->rest + n) + 1);
            s->rest_op = op;
            if (!run)
                continue;
            if (s->h->io && cmd[0] != GONE && bare(cmd, "|")) {        /* a pipe: its steps, before what follows */
                char steps[SH_LINE], all[SH_LINE + 8];
                if (pipe_steps(s, cmd, steps, sizeof steps) != 0
                    || sh_fmt(all, sizeof all, "%s%s%s", steps, op == 1 ? "&&" : op == 2 ? "||" : s->rest[0] ? "\001" : "",
                              s->rest) >= (int)sizeof s->rest) {
                    err(s, "The line is too long for its pipes, or no temporary file can be made (TEMP)\r\n");
                    s->errorlevel = 1;
                    continue;
                }
                strcpy(s->rest, all);
                s->rest_op = 0;
                continue;
            }
            r = s->rest_plain ? run_line(s, cmd) : run_one(s, cmd);
            if (r == SH_UNDER_WAY) {
                s->under_way = 1;               /* (a program: on when it ends) */
                return;
            }
            s->errorlevel = r;
        }
        if (s->under_way)
            return;
        /* the line is done: a FOR's next turn, a batch file's next line, or the prompt */
        if (!s->nbat && !s->loop.items)
            break;
        if (s->h->stop && s->h->stop(s->h->ctx)) {      /* Ctrl+C */
            if (ask_stop(s))
                return;
            continue;                           /* (nobody to ask: they have ended) */
        }
        if (s->loop.items) {
            if (*s->loop.at) {                  /* its command, the variable's place taken by the next of the set */
                const char *c = s->loop.cmd, *item = s->loop.at;
                size_t k = 0;
                s->loop.at += strlen(item) + 1;
                while (*c && k < sizeof s->rest - 1) {
                    if (c[0] == '%' && upper((unsigned char)c[1]) == upper((unsigned char)s->loop.var)) {
                        k += (size_t)sh_fmt(s->rest + k, (unsigned)(sizeof s->rest - k), "%s", item);
                        c += 2;
                    } else {
                        s->rest[k++] = *c++;
                    }
                }
                s->rest[k < sizeof s->rest ? k : sizeof s->rest - 1] = 0;
                s->rest_op = 0;
                s->rest_plain = 1;
                continue;
            }
            loop_end(s);
            continue;
        }
        {
            char raw[SH_LINE], *p = raw;
            int got = batch_read(s, raw, sizeof raw);
            if (got <= 0) {                     /* its end (or the file has gone) */
                batch_end(s);
                continue;
            }
            while (*p == ' ' || *p == '\t')
                p++;
            if (!*p || *p == ':')               /* (an empty line; a label) */
                continue;
            expand(s, p, s->rest, sizeof s->rest);
            if (!s->echo_off && *p != '@') {    /* shown as it is run, the prompt before it */
                char t[300];
                size_t n = prompt_text(s, t, sizeof t);
                s->h->write(s->h->ctx, SH_OUT, t, (int)n);      /* (as output: nothing is typed after this one) */
                out(s, s->rest);
                out(s, "\r\n");
            }
            s->rest_op = 0;
            s->rest_plain = 1;
        }
    }
    s->rest[0] = 0;
    s->rest_plain = 0;
    sh_prompt(s);
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
    {
        char *c;
        for (c = s->rest; *c; c++)              /* (THEN and GONE are the shell's own marks: not typed) */
            if (*c == THEN || *c == GONE)
                *c = ' ';
    }
    s->rest_op = 0;
    s->rest_plain = 0;
    next_command(s);
}

void sh_ended(struct sh *s, int code)
{
    s->under_way = 0;
    s->errorlevel = code;
    s->level = code;
    next_command(s);
}

void sh_key(struct sh *s, int ch)
{
    int what = s->key_for;
    if (!what)
        return;
    if (what == 3) {                            /* a built-in's (MORE, DELTREE): on, or ended */
        int r = s->tool.step(s, ch);
        if (r == SH_UNDER_WAY)
            return;
        if (s->redirect) {
            s->h->close(s->h->ctx, s->redirect);
            s->redirect = 0;
        }
        s->discard = 0;
        s->key_for = 0;
        s->under_way = 0;
        s->errorlevel = r;
        next_command(s);
        return;
    }
    if (what == 2) {                            /* Terminate batch job (Y/N)? */
        if (upper(ch) != 'Y' && upper(ch) != 'N') {
            if (!s->h->key || s->h->key(s->h->ctx) != SH_UNDER_WAY) {   /* (asked again; nobody there: ended) */
                s->key_for = 0;
                s->under_way = 0;
                batch_end_all(s);
                next_command(s);
            }
            return;
        }
        out(s, upper(ch) == 'Y' ? "Y\r\n" : "N\r\n");
        if (upper(ch) == 'Y')
            batch_end_all(s);
    }
    s->key_for = 0;
    s->under_way = 0;
    next_command(s);
}

/* ---- the prompt, history, completion */

void sh_prompt(struct sh *s)
{
    char t[300];
    size_t n = s->echo_off ? 0 : prompt_text(s, t, sizeof t);  /* (ECHO OFF at the prompt: none, as under DOS) */
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

/* The shell's folder is this (a full path, as DOS has it for the program that starts the shell): its drive's,
   and the drive the shell is on. Nothing, if it is no folder. */
void sh_folder(struct sh *s, const char *path)
{
    char p[SH_PATH];
    int d = upper((unsigned char)path[0]) - 'A';
    if (d < 0 || d > 25 || path[1] != ':' || strlen(path) >= sizeof s->cwd[0])
        return;
    full_path(s, path, p, sizeof p);
    if (!s->h->is_dir(s->h->ctx, p))
        return;
    sh_fmt(s->cwd[d], sizeof s->cwd[d], "%s", p);
    s->drive = d;
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
