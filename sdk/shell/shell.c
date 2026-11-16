/* GLOS's shell (shell.h; docs/v1/shell.md; PRD D50): DOS's commands and syntax, modernised.
 *
 * Built in: CD (CHDIR), a drive's letter, DIR [/W] [/B], COPY, MOVE, REN (RENAME), DEL (ERASE), MD (MKDIR),
 * RD (RMDIR), TYPE, ECHO, SET, PATH, PROMPT, CLS, VER, EXIT, HISTORY, HELP, WHERE, START, and what the host
 * adds. A built-in's output goes to a file with > or >>.
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
    int n, i, done = 0;
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
    "START", "DF", "DU", "CALL", "FOR", "GOTO", "IF", "PAUSE", "REM", "SHIFT", "VOL", "TRUENAME", "BREAK", "VERIFY",
    "LH", "LOADHIGH", "CTTY", 0 };

static const char *const helps[] = {
    "CD [path]           the folder (of a drive: CD X:); X: changes drive",
    "DIR [path] [/W] [/B] a folder's files: wide, or names alone",
    "COPY from [to]      files (* and ?), into a folder or a file",
    "MOVE from [to]      ... moved",
    "REN from to         a file's new name",
    "DEL files           files deleted (* and ?)",
    "MD path, RD path    a folder made, removed",
    "DF [drive: ...]     the drives: their size, what is used and free",
    "DU [folder] [/S]    what a folder's files take, with all under it",
    "TYPE file           a file shown",
    "ECHO [text]         text written",
    "SET [name[=value]]  the variables; PATH and PROMPT as SET does",
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
    if (bare(line, "<>")) {                     /* a built-in: its output to the file (it reads none) */
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
