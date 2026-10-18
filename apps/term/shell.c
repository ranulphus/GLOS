/* GLOS's shell (PRD D50; docs/milestones-m9.md step E): DOS's commands and
 * syntax, modernised, in the terminal (term.c).
 *
 * Built in: CD (CHDIR), a drive's letter, DIR [/W] [/B], COPY, MOVE, REN
 * (RENAME), DEL (ERASE), MD (MKDIR), RD (RMDIR), TYPE, ECHO, SET, PATH,
 * PROMPT, CLS, VER, EXIT, HISTORY, HELP, WHERE, START. A built-in's output
 * goes to a file with > or >>.
 *
 * Anything else is a program: found here and along PATH (.COM, .EXE, .BAT),
 * started by GLOS as a console job of this window's (PRD D51), in this
 * shell's folder, with the variables this shell has set; a batch file, and a
 * line with a pipe or a redirection, through COMMAND.COM /C (D40). A GLOS
 * app goes on beside the terminal. A program whose profile says screen =
 * full runs full-screen from the start.
 *
 * %NAME% is a variable; %ERRORLEVEL% the last exit code. && runs what
 * follows only after a success (exit code 0), || only after a failure. Up
 * and Down go through the history, Tab completes a file's name.
 *
 * The shell keeps its own folder for each drive and resolves paths itself,
 * through the toolkit's calls on files, so the host's tests see what DOS
 * does. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/gui.h>

#include "term.h"

#define HISTORY 50
#define VARS    64

static char cwd[26][80];                /* each drive's folder: "C:\", "C:\GUI" */
static int drive = 2;                   /* C: */
static char *vars[VARS];                /* NAME=VALUE */
static unsigned char changed[VARS];     /* set by this shell: given to the programs it starts */
static int nvars;
static char *hist[HISTORY];
static int nhist, hist_at;
static int errorlevel;

/* what runs, and what is left of the line */
static int job = -1;                    /* the program's job, or -1 */
static unsigned job_state;
static char rest[512];                  /* the line's commands still to come */
static int rest_op;                     /* how they follow: 0 none, 1 &&, 2 || */

extern char **environ;

int term_busy(void) { return job >= 0; }

static int upper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

static int same(const char *a, const char *b)
{
    for (; *a && upper((unsigned char)*a) == upper((unsigned char)*b); a++, b++)
        ;
    return !*a && !*b;
}

static void out(const char *s) { term_print(s); }

static void err(const char *s)
{
    term_attr(T_ERROR);
    term_print(s);
    term_attr(T_NORMAL);
}

/* ---- variables */

static int var_find(const char *name, size_t n)
{
    int i;
    for (i = 0; i < nvars; i++) {
        size_t k;
        for (k = 0; k < n && vars[i][k] && upper((unsigned char)vars[i][k]) == upper((unsigned char)name[k]); k++)
            ;
        if (k == n && vars[i][k] == '=')
            return i;
    }
    return -1;
}

static const char *var_get(const char *name)
{
    int i = var_find(name, strlen(name));
    return i < 0 ? NULL : strchr(vars[i], '=') + 1;
}

static void var_set(const char *name, const char *value)
{
    size_t n = strlen(name);
    int i = var_find(name, n);
    char *v;
    if (!value || !*value) {
        if (i >= 0) {
            free(vars[i]);
            vars[i] = vars[--nvars];
            changed[i] = changed[nvars];
        }
        return;
    }
    v = malloc(n + strlen(value) + 2);
    if (!v)
        return;
    for (size_t k = 0; k < n; k++)
        v[k] = (char)upper((unsigned char)name[k]);
    v[n] = '=';
    strcpy(v + n + 1, value);
    if (i < 0) {
        if (nvars == VARS) {
            free(v);
            err("No room for another variable.\r\n");
            return;
        }
        i = nvars++;
    } else {
        free(vars[i]);
    }
    vars[i] = v;
    changed[i] = 1;
}

/* %NAME% expanded; %% is %. */
static void expand(const char *in, char *outb, size_t room)
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
                    snprintf(num, sizeof num, "%d", errorlevel);
                    v = num;
                } else {
                    v = var_get(name);
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

/* p made a full path (a drive and its folders, "." and ".." gone) from this shell's folders. */
static void full_path(const char *p, char *outb, size_t room)
{
    char t[260], *parts[32];
    int n = 0, i, d = drive;
    size_t o;
    if (p[0] && p[1] == ':') {
        d = upper((unsigned char)p[0]) - 'A';
        if (d < 0 || d > 25)
            d = drive;
        p += 2;
    }
    if (p[0] == '\\' || p[0] == '/')
        snprintf(t, sizeof t, "%s", p + 1);
    else
        snprintf(t, sizeof t, "%s%s%s", cwd[d] + 3, cwd[d][3] ? "\\" : "", p);
    for (char *s = strtok(t, "\\/"); s && n < 32; s = strtok(NULL, "\\/")) {
        if (!strcmp(s, "."))
            continue;
        if (!strcmp(s, "..")) {
            if (n)
                n--;
            continue;
        }
        parts[n++] = s;
    }
    o = (size_t)snprintf(outb, room, "%c:\\", 'A' + d);
    for (i = 0; i < n && o < room; i++)
        o += (size_t)snprintf(outb + o, room - o, "%s%s", i ? "\\" : "", parts[i]);
    for (char *q = outb; *q; q++)
        *q = (char)upper((unsigned char)*q);
}

/* name matches a DOS pattern (* and ?) */
static int match(const char *pat, const char *name)
{
    for (; *pat; pat++, name++) {
        if (*pat == '*') {
            for (const char *s = name;; s++) {
                if (match(pat + 1, s))
                    return 1;
                if (!*s)
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
static int split_pattern(const char *arg, char *dir, size_t room, char *pat, size_t proom)
{
    char full[260];
    const char *name;
    full_path(arg, full, sizeof full);
    if (gui_is_dir(full)) {
        snprintf(dir, room, "%s", full);
        snprintf(pat, proom, "*.*");
        return 1;
    }
    name = gui_path_name(full);
    snprintf(pat, proom, "%s", name);
    snprintf(dir, room, "%s", full);
    gui_path_up(dir);
    return 0;
}

/* ---- output of a built-in, to the window or a file (> and >>) */

static FILE *redirect;

static void say(const char *s)
{
    if (redirect)
        fputs(s, redirect);
    else
        out(s);
}

/* ---- the built-ins */

static void size_text(unsigned long v, char *b, size_t room)
{
    char t[32];
    int n = snprintf(t, sizeof t, "%lu", v), i, o = 0;
    for (i = 0; i < n && o < (int)room - 1; i++) {
        if (i && (n - i) % 3 == 0)
            b[o++] = ',';
        b[o++] = t[i];
    }
    b[o] = 0;
}

static int cmd_dir(int argc, char **argv)
{
    char dir[260], pat[64], line[160], sz[24];
    struct gui_dirent *e;
    int n, i, wide = 0, bare = 0, files = 0, dirs = 0, col = 0;
    unsigned long bytes = 0;
    const char *arg = NULL;
    for (i = 1; i < argc; i++)
        if (same(argv[i], "/W"))
            wide = 1;
        else if (same(argv[i], "/B"))
            bare = 1;
        else
            arg = argv[i];
    split_pattern(arg ? arg : ".", dir, sizeof dir, pat, sizeof pat);
    if ((n = gui_dir_read(dir, &e)) < 0) {
        err("File not found\r\n");
        return 1;
    }
    if (!bare) {
        snprintf(line, sizeof line, " Directory of %s\r\n\r\n", dir);
        say(line);
    }
    for (i = 0; i < n; i++) {
        char nm[16], ext[8];
        const char *dot;
        if (!match(pat, e[i].name) && !(same(pat, "*.*")))
            continue;
        if (bare) {
            snprintf(line, sizeof line, "%s\r\n", e[i].name);
            say(line);
        } else if (wide) {
            snprintf(line, sizeof line, e[i].dir ? "[%s]" : "%s", e[i].name);
            snprintf(nm, sizeof nm, "%-15s", line);
            say(nm);
            if (++col == 5) {
                say("\r\n");
                col = 0;
            }
        } else {
            dot = strchr(e[i].name, '.');
            snprintf(nm, sizeof nm, "%.*s", dot ? (int)(dot - e[i].name) : (int)strlen(e[i].name), e[i].name);
            snprintf(ext, sizeof ext, "%s", dot ? dot + 1 : "");
            size_text(e[i].size, sz, sizeof sz);
            snprintf(line, sizeof line, "%-8s %-3s %14s %02u-%02u-%04u  %2u:%02u\r\n", nm, ext,
                     e[i].dir ? "<DIR>" : sz, (e[i].date >> 5) & 15, e[i].date & 31, (e[i].date >> 9) + 1980,
                     e[i].time >> 11, (e[i].time >> 5) & 63);
            say(line);
        }
        if (e[i].dir)
            dirs++;
        else
            files++, bytes += e[i].size;
    }
    free(e);
    if (wide && col)
        say("\r\n");
    if (!bare) {
        size_text(bytes, sz, sizeof sz);
        snprintf(line, sizeof line, "%10d file(s) %14s bytes\r\n%10d dir(s)\r\n", files, sz, dirs);
        say(line);
    }
    return files + dirs ? 0 : 1;
}

static int copy_file(const char *from, const char *to)
{
    FILE *a = gui_fopen(from, "rb"), *b;
    char buf[4096];
    size_t n;
    int bad = 0;
    if (!a)
        return -1;
    if (!(b = gui_fopen(to, "wb"))) {
        fclose(a);
        return -1;
    }
    while ((n = fread(buf, 1, sizeof buf, a)) > 0)
        if (fwrite(buf, 1, n, b) != n)
            bad = 1;
    fclose(a);
    if (fclose(b) != 0)
        bad = 1;
    return bad ? -1 : 0;
}

/* COPY and MOVE: each file the first names, to the second (a folder, or a file's name). */
static int cmd_copy(int argc, char **argv, int move)
{
    char dir[260], pat[64], to[260], from[260], dst[260], line[80];
    struct gui_dirent *e;
    int n, i, done = 0, to_dir;
    if (argc < 2) {
        err("Required parameter missing\r\n");
        return 1;
    }
    split_pattern(argv[1], dir, sizeof dir, pat, sizeof pat);
    full_path(argc > 2 ? argv[2] : ".", to, sizeof to);
    to_dir = gui_is_dir(to);
    if ((n = gui_dir_read(dir, &e)) < 0) {
        err("File not found\r\n");
        return 1;
    }
    for (i = 0; i < n; i++) {
        if (e[i].dir || !match(pat, e[i].name))
            continue;
        gui_path_join(from, sizeof from, dir, e[i].name);
        if (to_dir)
            gui_path_join(dst, sizeof dst, to, e[i].name);
        else
            snprintf(dst, sizeof dst, "%s", to);
        if (same(from, dst)) {
            err("File cannot be copied onto itself\r\n");
            continue;
        }
        if ((move && gui_rename(from, dst) == 0)
            || (copy_file(from, dst) == 0 && (!move || gui_unlink(from) == 0))) {
            done++;
            if (argc > 2 && !to_dir)
                break;                          /* (several into one file: the first) */
        } else {
            snprintf(line, sizeof line, "%s: not %s\r\n", e[i].name, move ? "moved" : "copied");
            err(line);
        }
    }
    free(e);
    snprintf(line, sizeof line, "%9d file(s) %s\r\n", done, move ? "moved" : "copied");
    say(line);
    return done ? 0 : 1;
}

static int cmd_del(int argc, char **argv)
{
    char dir[260], pat[64], p[260], line[80];
    struct gui_dirent *e;
    int n, i, done = 0;
    if (argc < 2) {
        err("Required parameter missing\r\n");
        return 1;
    }
    split_pattern(argv[1], dir, sizeof dir, pat, sizeof pat);
    if ((n = gui_dir_read(dir, &e)) < 0) {
        err("File not found\r\n");
        return 1;
    }
    for (i = 0; i < n; i++)
        if (!e[i].dir && match(pat, e[i].name)) {
            gui_path_join(p, sizeof p, dir, e[i].name);
            if (gui_unlink(p) == 0)
                done++;
        }
    free(e);
    if (!done) {
        err("File not found\r\n");
        return 1;
    }
    snprintf(line, sizeof line, "%d file(s) deleted\r\n", done);
    say(line);
    return 0;
}

static int cmd_type(int argc, char **argv)
{
    char p[260], buf[1024];
    FILE *f;
    size_t n;
    if (argc < 2) {
        err("Required parameter missing\r\n");
        return 1;
    }
    full_path(argv[1], p, sizeof p);
    if (!(f = gui_fopen(p, "rb"))) {
        err("File not found\r\n");
        return 1;
    }
    while ((n = fread(buf, 1, sizeof buf - 1, f)) > 0) {
        buf[n] = 0;
        if (redirect)
            fwrite(buf, 1, n, redirect);
        else
            term_write(buf, (int)n);
    }
    fclose(f);
    return 0;
}

static int cmd_cd(int argc, char **argv)
{
    char p[260], line[300];
    if (argc < 2) {
        snprintf(line, sizeof line, "%s\r\n", cwd[drive]);
        say(line);
        return 0;
    }
    if (strlen(argv[1]) == 2 && argv[1][1] == ':') {    /* CD X: shows X's folder */
        int d = upper((unsigned char)argv[1][0]) - 'A';
        snprintf(line, sizeof line, "%s\r\n", d >= 0 && d < 26 ? cwd[d] : "");
        say(line);
        return 0;
    }
    full_path(argv[1], p, sizeof p);
    if (!gui_is_dir(p)) {
        err("Invalid directory\r\n");
        return 1;
    }
    snprintf(cwd[upper((unsigned char)p[0]) - 'A'], sizeof cwd[0], "%s", p);
    return 0;
}

static int cmd_set(int argc, char **argv, const char *raw)
{
    const char *eq;
    int i;
    char line[300];
    if (argc < 2) {
        for (i = 0; i < nvars; i++) {
            snprintf(line, sizeof line, "%s\r\n", vars[i]);
            say(line);
        }
        return 0;
    }
    raw = strchr(raw, ' ');                     /* (the rest of the line, spaces and all) */
    while (raw && *raw == ' ')
        raw++;
    if (!raw || !(eq = strchr(raw, '='))) {
        const char *v = var_get(argv[1]);
        if (!v) {
            err("Environment variable not defined\r\n");
            return 1;
        }
        snprintf(line, sizeof line, "%s=%s\r\n", argv[1], v);
        say(line);
        return 0;
    }
    {
        char name[64];
        size_t n = (size_t)(eq - raw) < sizeof name - 1 ? (size_t)(eq - raw) : sizeof name - 1;
        memcpy(name, raw, n);
        name[n] = 0;
        var_set(name, eq + 1);
    }
    return 0;
}

static const char *const builtins[] = { "CD", "CHDIR", "DIR", "COPY", "MOVE", "REN", "RENAME", "DEL", "ERASE", "MD",
    "MKDIR", "RD", "RMDIR", "TYPE", "ECHO", "SET", "PATH", "PROMPT", "CLS", "VER", "EXIT", "HISTORY", "HELP", "WHERE",
    "START", "SSH", "SFTP", "SSH-KEYGEN", 0 };

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
    "SSH [user@]host [command]  a session there (-p port, -i key, -l user)",
    "SFTP [user@]host    files there and here (help inside lists its commands)",
    "SSH-KEYGEN          a key of your own (SSH\\ID_ED25) for servers' authorized_keys",
    "VER, HELP, EXIT",
    "Programs run here until they want the screen. a && b: b after a has worked; a || b: b if it hasn't.",
    0 };

/* program: where it is (here, then along PATH; .COM, .EXE, .BAT), into found. 0, or -1. */
static int find_program(const char *name, char *found, size_t room)
{
    static const char *const exts[] = { ".COM", ".EXE", ".BAT" };
    char base[260], dirs[1024], cand[260];
    const char *path = var_get("PATH"), *dot = strrchr(gui_path_name(name), '.');
    struct gui_dirent e;
    int has_dir = strchr(name, '\\') || strchr(name, ':') || strchr(name, '/');
    char *d;
    snprintf(dirs, sizeof dirs, "%s;%s", cwd[drive], has_dir ? "" : path ? path : "");
    for (d = strtok(dirs, ";"); d; d = strtok(NULL, ";")) {
        int i;
        if (has_dir)
            full_path(name, base, sizeof base);
        else {
            char dd[260];
            full_path(d, dd, sizeof dd);
            gui_path_join(base, sizeof base, dd, name);
        }
        for (i = dot ? -1 : 0; i < 3; i++) {
            snprintf(cand, sizeof cand, "%s%s", base, i < 0 ? "" : exts[i]);
            for (char *q = cand; *q; q++)
                *q = (char)upper((unsigned char)*q);
            if (gui_stat(cand, &e) == 0 && !e.dir) {
                snprintf(found, room, "%s", cand);
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

static int is_builtin(const char *w)
{
    int i;
    for (i = 0; builtins[i]; i++)
        if (same(w, builtins[i]))
            return 1;
    return strlen(w) == 2 && w[1] == ':';
}

/* ---- running a program: a console job of this window's */

static int run_program(const char *line, int detach)
{
    struct glos_run r;
    char prog[260], first[128], profile[40], screen[16];
    const char *p = line, *args;
    size_t n = 0, e = 0;
    int i, id;
    memset(&r, 0, sizeof r);
    while (*p == ' ')
        p++;
    while (*p && *p != ' ' && n < sizeof first - 1)
        first[n++] = *p++;
    first[n] = 0;
    args = p;
    if (strpbrk(line, "<>|")) {                 /* a pipe or a redirection: COMMAND.COM's */
        snprintf(r.command, sizeof r.command, "%s", line);
    } else if (find_program(first, prog, sizeof prog) == 0) {
        snprintf(r.command, sizeof r.command, "%s%s", prog, args);
        snprintf(profile, sizeof profile, "program %s", gui_path_name(prog));
        if (glos_config_get(profile, "screen", screen, sizeof screen) > 0 && same(screen, "full"))
            r.flags |= GLOS_RUN_FULL;
    } else {
        err("Bad command or file name\r\n");
        return -1;
    }
    r.flags |= detach ? GLOS_RUN_DETACH : GLOS_RUN_CONSOLE;
    snprintf(r.dir, sizeof r.dir, "%s", cwd[drive]);
    for (i = 0; i < nvars; i++)                 /* the variables this shell has set */
        if (changed[i] && e + strlen(vars[i]) + 2 < sizeof r.env) {
            strcpy(r.env + e, vars[i]);
            e += strlen(vars[i]) + 1;
        }
    r.env[e] = 0;
    if ((id = glos_run(&r)) < 0) {
        err("GLOS can't run it here.\r\n");
        return -1;
    }
    if (detach)
        return 0;
    job = id;
    job_state = GLOS_JOB_QUEUED;
    term_title(gui_path_name(first));
    return 1;
}

/* ---- a command */

static int split_words(char *s, char **argv, int max)
{
    int n = 0;
    while (*s && n < max - 1) {
        while (*s == ' ' || *s == '\t')
            s++;
        if (!*s)
            break;
        if (*s == '"') {
            argv[n++] = ++s;
            while (*s && *s != '"')
                s++;
        } else {
            argv[n++] = s;
            while (*s && *s != ' ' && *s != '\t')
                s++;
        }
        if (*s)
            *s++ = 0;
    }
    argv[n] = NULL;
    return n;
}

/* One command: 0 or more for its exit code, or -2 when a program was started (its end comes later). */
static int run_one(const char *cmd)
{
    char line[512], copy[512], *argv[32], *gt, target[260];
    int argc, r = 0, append = 0;
    while (*cmd == ' ' || *cmd == '\t')
        cmd++;
    expand(cmd, line, sizeof line);
    {
        char *e = line + strlen(line);
        while (e > line && (e[-1] == ' ' || e[-1] == '\t'))
            *--e = 0;
    }
    snprintf(copy, sizeof copy, "%s", line);
    argc = split_words(copy, argv, 32);
    if (!argc)
        return errorlevel;
    if (!is_builtin(argv[0])) {
        int s = run_program(line, 0);
        return s > 0 ? -2 : s < 0 ? 1 : 0;
    }
    if ((gt = strchr(line, '>')) != NULL) {
        append = gt[1] == '>';                  /* a built-in's output to a file */
        full_path(gt + 1 + append + strspn(gt + 1 + append, " "), target, sizeof target);
        {
            char *t = target + strlen(target);
            while (t > target && t[-1] == ' ')
                *--t = 0;
        }
        *gt = 0;
        snprintf(copy, sizeof copy, "%s", line);
        argc = split_words(copy, argv, 32);
        if (!(redirect = gui_fopen(target, append ? "ab" : "wb"))) {
            err("Access denied\r\n");
            return 1;
        }
    }
    if (strlen(argv[0]) == 2 && argv[0][1] == ':') {
        int d = upper((unsigned char)argv[0][0]) - 'A';
        char root[4] = { (char)('A' + d), ':', '\\', 0 };
        if (d < 0 || d > 25 || !gui_is_dir(root)) {
            err("Invalid drive specification\r\n");
            r = 1;
        } else {
            drive = d;
            if (!cwd[d][0])
                snprintf(cwd[d], sizeof cwd[d], "%s", root);
        }
    } else if (same(argv[0], "CD") || same(argv[0], "CHDIR")) {
        r = cmd_cd(argc, argv);
    } else if (same(argv[0], "DIR")) {
        r = cmd_dir(argc, argv);
    } else if (same(argv[0], "COPY")) {
        r = cmd_copy(argc, argv, 0);
    } else if (same(argv[0], "MOVE")) {
        r = cmd_copy(argc, argv, 1);
    } else if (same(argv[0], "REN") || same(argv[0], "RENAME")) {
        char a[260], b[260];
        if (argc < 3) {
            err("Required parameter missing\r\n");
            r = 1;
        } else {
            char d[260];
            full_path(argv[1], a, sizeof a);
            snprintf(d, sizeof d, "%s", a);
            gui_path_up(d);
            gui_path_join(b, sizeof b, d, argv[2]);
            for (char *q = b; *q; q++)
                *q = (char)upper((unsigned char)*q);
            if (gui_rename(a, b) != 0) {
                err("Duplicate file name or file not found\r\n");
                r = 1;
            }
        }
    } else if (same(argv[0], "DEL") || same(argv[0], "ERASE")) {
        r = cmd_del(argc, argv);
    } else if (same(argv[0], "MD") || same(argv[0], "MKDIR") || same(argv[0], "RD") || same(argv[0], "RMDIR")) {
        char p[260];
        int make = upper((unsigned char)argv[0][0]) == 'M';
        if (argc < 2) {
            err("Required parameter missing\r\n");
            r = 1;
        } else {
            full_path(argv[1], p, sizeof p);
            if ((make ? gui_mkdir(p) : gui_rmdir(p)) != 0) {
                err(make ? "Unable to create directory\r\n" : "Invalid path, not directory, or directory not empty\r\n");
                r = 1;
            }
        }
    } else if (same(argv[0], "TYPE")) {
        r = cmd_type(argc, argv);
    } else if (same(argv[0], "ECHO")) {
        const char *t = line + 4;
        if (*t == '.') {
            say("\r\n");
        } else {
            while (*t == ' ')
                t++;
            if (!*t || same(t, "ON") || same(t, "OFF")) {
                if (!*t)
                    say("ECHO is on\r\n");
            } else {
                say(t);
                say("\r\n");
            }
        }
    } else if (same(argv[0], "SET")) {
        r = cmd_set(argc, argv, line);
    } else if (same(argv[0], "PATH") || same(argv[0], "PROMPT")) {
        const char *t = line + strlen(argv[0]);
        char l[300];
        while (*t == ' ' || *t == '=')
            t++;
        if (!*t) {
            const char *v = var_get(argv[0]);
            snprintf(l, sizeof l, "%s=%s\r\n", argv[0], v ? v : "");
            say(l);
        } else {
            char name[8];
            snprintf(name, sizeof name, "%s", argv[0]);
            for (char *q = name; *q; q++)
                *q = (char)upper((unsigned char)*q);
            var_set(name, t);
        }
    } else if (same(argv[0], "CLS")) {
        term_clear();
    } else if (same(argv[0], "VER")) {
        say("GLOS shell 1.0 (GLOS API 1.2)\r\n");
    } else if (same(argv[0], "EXIT")) {
        term_quit(0);
    } else if (same(argv[0], "HISTORY")) {
        int i;
        char l[300];
        for (i = 0; i < nhist; i++) {
            snprintf(l, sizeof l, "%3d  %s\r\n", i + 1, hist[i]);
            say(l);
        }
    } else if (same(argv[0], "HELP")) {
        int i;
        for (i = 0; helps[i]; i++) {
            say(helps[i]);
            say("\r\n");
        }
    } else if (same(argv[0], "WHERE")) {
        char found[260];
        if (argc < 2 || find_program(argv[1], found, sizeof found) != 0) {
            err("Not found\r\n");
            r = 1;
        } else {
            say(found);
            say("\r\n");
        }
    } else if (same(argv[0], "START")) {
        const char *t = line + 5;
        while (*t == ' ')
            t++;
        r = run_program(t, 1) < 0;
    } else if (same(argv[0], "SSH")) {
        r = ssh_command(argc, argv);            /* (-2: under way, as a program is) */
    } else if (same(argv[0], "SFTP")) {
        r = sftp_command(argc, argv);
    } else if (same(argv[0], "SSH-KEYGEN")) {
        r = keygen_command(argc, argv);
    }
    if (redirect) {
        fclose(redirect);
        redirect = NULL;
    }
    return r;
}

/* ---- a line: commands joined by && and || */

static void next_command(void)
{
    while (rest[0] && job < 0) {
        char cmd[512];
        const char *a = strstr(rest, "&&"), *o = strstr(rest, "||"), *cut = a && (!o || a < o) ? a : o;
        int op = cut == a && a ? 1 : cut ? 2 : 0, run, r;
        size_t n = cut ? (size_t)(cut - rest) : strlen(rest);
        memcpy(cmd, rest, n);
        cmd[n] = 0;
        run = rest_op == 0 || (rest_op == 1 && errorlevel == 0) || (rest_op == 2 && errorlevel != 0);
        memmove(rest, cut ? cut + 2 : rest + n, strlen(cut ? cut + 2 : rest + n) + 1);
        rest_op = op;
        if (!run)
            continue;
        r = run_one(cmd);
        if (r == -2)
            return;                             /* (a program: on when it ends) */
        errorlevel = r;
    }
    if (job < 0) {
        rest[0] = 0;
        shell_prompt();
    }
}

void shell_line(const char *line)
{
    const char *p = line;
    if (ssh_asking()) {                         /* (a question's answer, a password, sftp's command: not history) */
        ssh_answer(line);
        return;
    }
    while (*p == ' ')
        p++;
    if (*p && (!nhist || strcmp(hist[nhist - 1], p))) {
        if (nhist == HISTORY) {
            free(hist[0]);
            memmove(hist, hist + 1, (HISTORY - 1) * sizeof *hist);
            nhist--;
        }
        hist[nhist++] = strdup(p);
    }
    hist_at = nhist;
    snprintf(rest, sizeof rest, "%s", p);
    rest_op = 0;
    next_command();
}

/* ---- the running program */

void shell_resume(int code)
{
    errorlevel = code;
    next_command();
}

void shell_type(const unsigned short *keys, int n)
{
    if (job >= 0)
        glos_job_type((unsigned)job, keys, (unsigned)n);
}

void shell_break(void)
{
    if (job >= 0) {
        glos_job_kill((unsigned)job);
        err("^Break\r\n");
    }
}

void shell_poll(void)
{
    char buf[1024];
    unsigned st, code;
    int n;
    while (job >= 0) {
        n = glos_job_read((unsigned)job, buf, sizeof buf, 0, &st, &code);
        if (n < 0) {
            job = -1;
            break;
        }
        if (n > 0)
            term_write(buf, n);
        job_state = st;
        if (st == GLOS_JOB_DONE || st == GLOS_JOB_APP) {
            unsigned short left[64];
            int nleft = 0;
            if (n == (int)sizeof buf)
                continue;                       /* (more of its output first) */
            errorlevel = st == GLOS_JOB_DONE ? (int)code : 0;
            if (st == GLOS_JOB_APP)
                glos_job_forget((unsigned)job); /* (a GLOS app goes on beside: nothing of it is ours) */
            else
                nleft = glos_job_keys((unsigned)job, left, 64);     /* (typed ahead, unread: ours again) */
            job = -1;
            term_title("");
            next_command();
            if (nleft > 0)
                term_keys(left, nleft);
            return;
        }
        if (n < (int)sizeof buf)
            break;
    }
}

/* ---- the prompt, history, completion */

void shell_prompt(void)
{
    const char *p = var_get("PROMPT");
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
        case 'P': n += (size_t)snprintf(t + n, sizeof t - n, "%s", cwd[drive]); break;
        case 'N': t[n++] = (char)('A' + drive); break;
        case 'G': t[n++] = '>'; break;
        case 'L': t[n++] = '<'; break;
        case 'B': t[n++] = '|'; break;
        case 'Q': t[n++] = '='; break;
        case '$': t[n++] = '$'; break;
        case '_': t[n++] = '\r', t[n++] = '\n'; break;
        case 'E': t[n++] = 27; break;
        }
    }
    t[n] = 0;
    term_attr(T_PROMPT);
    term_print(t);
    term_attr(T_NORMAL);
    term_prompt_done();
}

void shell_history(int by)
{
    if (!nhist)
        return;
    hist_at += by;
    if (hist_at < 0)
        hist_at = 0;
    if (hist_at >= nhist) {
        hist_at = nhist;
        term_input_set("");
        return;
    }
    term_input_set(hist[hist_at]);
}

void shell_complete(void)
{
    char line[256], dir[260], pre[64], common[64], full[300];
    const char *in = term_input(), *word;
    struct gui_dirent *e;
    int n, i, found = 0;
    size_t wl;
    snprintf(line, sizeof line, "%s", in);
    word = strrchr(line, ' ');
    word = word ? word + 1 : line;
    wl = strlen(word);
    {                                           /* the folder it is in, and what of its name is typed */
        const char *slash = strrchr(word, '\\');
        char part[260];
        if (slash) {
            snprintf(part, sizeof part, "%.*s", (int)(slash - word + 1), word);
            full_path(part, dir, sizeof dir);
            snprintf(pre, sizeof pre, "%s", slash + 1);
        } else if (wl >= 2 && word[1] == ':') {
            snprintf(part, sizeof part, "%.2s", word);
            full_path(part, dir, sizeof dir);
            snprintf(pre, sizeof pre, "%s", word + 2);
        } else {
            snprintf(dir, sizeof dir, "%s", cwd[drive]);
            snprintf(pre, sizeof pre, "%s", word);
        }
    }
    if (word == line && !strpbrk(word, "\\:")) {     /* the first word: a built-in command's name too */
        int k, hits = 0;
        char only[16];
        for (k = 0; builtins[k]; k++)
            if (wl) {
                size_t j;
                for (j = 0; j < wl && upper((unsigned char)word[j]) == builtins[k][j]; j++)
                    ;
                if (j == wl && !hits++)
                    snprintf(only, sizeof only, "%s", builtins[k]);
            }
        if (hits == 1) {
            snprintf(full, sizeof full, "%s ", only);
            term_input_set(full);
            return;
        }
    }
    if ((n = gui_dir_read(dir, &e)) <= 0)
        return;
    common[0] = 0;
    for (i = 0; i < n; i++) {
        size_t k;
        for (k = 0; pre[k] && upper((unsigned char)pre[k]) == upper((unsigned char)e[i].name[k]); k++)
            ;
        if (pre[k])
            continue;
        if (!found++) {
            snprintf(common, sizeof common, "%s%s", e[i].name, e[i].dir ? "\\" : "");
        } else {
            for (k = 0; common[k] && upper((unsigned char)common[k]) == upper((unsigned char)e[i].name[k]); k++)
                ;
            common[k] = 0;
        }
    }
    if (found > 1 && strlen(common) == strlen(pre)) {  /* several, and nothing more in common: they're listed */
        term_print("\r\n");
        for (i = 0; i < n; i++) {
            size_t k;
            for (k = 0; pre[k] && upper((unsigned char)pre[k]) == upper((unsigned char)e[i].name[k]); k++)
                ;
            if (!pre[k]) {
                snprintf(full, sizeof full, e[i].dir ? "%s\\  " : "%s  ", e[i].name);
                term_print(full);
            }
        }
        term_print("\r\n");
        free(e);
        snprintf(full, sizeof full, "%s", line);
        shell_prompt();
        term_input_set(full);
        return;
    }
    free(e);
    if (found) {
        size_t keep = (size_t)(word - line) + wl - strlen(pre);
        snprintf(full, sizeof full, "%.*s%s", (int)keep, line, common);
        term_input_set(full);
    }
}

void shell_start(void)
{
    char **e;
    int d;
    for (d = 0; d < 26; d++)
        cwd[d][0] = 0;
    for (e = environ; e && *e && nvars < VARS; e++)
        if (strchr(*e, '=')) {
            vars[nvars] = strdup(*e);
            changed[nvars++] = 0;
        }
    snprintf(cwd[2], sizeof cwd[2], "C:\\");
    term_print("GLOS shell 1.0. HELP lists its commands.\r\n\r\n");
}
