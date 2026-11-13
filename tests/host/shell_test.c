/* shell_test ROOT SCRIPT: GLOS's shell (sdk/shell) on the host, against files under ROOT (ROOT/C is C:\).
 * What the shell writes, and what it asks of its host, is printed as a transcript, which the Makefile compares
 * with the one kept beside the script (tests/shell/NAME.out).
 *
 * The script's lines:
 *   > LINE          a line typed (the prompt and the output follow in the transcript)
 *   ended CODE      the program under way has ended
 *   up, down        the history: the line the shell puts in the input
 *   tab TEXT        completion of what is typed
 *   mkdir PATH      a folder made (DOS path)
 *   file PATH TEXT  a file made, with TEXT and a CR LF
 *   cat PATH        a file's bytes shown (CR and LF as \r and \n)
 *   # ...           a comment
 * In the transcript: errors are in <<...>>, the prompt in [[...]], a file's own text (TYPE) in {{...}};
 * RUN lines are programs handed to the host (A.COM under way, B.COM too; anything else is refused with -1
 * unless detached); XTRA is a host's command that ends with 7, XWAIT one that goes on. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../sdk/shell/shell.h"

static const char *root;
static struct sh sh;

static void native(const char *dos, char *out, size_t room)
{
    char *p;
    snprintf(out, room, "%s/%c%s", root, dos[0], dos + 2);
    for (p = out + strlen(root); *p; p++)
        if (*p == '\\')
            *p = '/';
    while (strlen(out) > strlen(root) + 2 && out[strlen(out) - 1] == '/')
        out[strlen(out) - 1] = 0;
}

static void h_write(void *ctx, int kind, const char *s, int n)
{
    static const char *const open[] = { "", "<<", "[[", "{{" }, *const close[] = { "", ">>", "]]", "}}" };
    (void)ctx;
    printf("%s%.*s%s", open[kind], n, s, close[kind]);
}

static int h_clear(void *ctx) { (void)ctx; printf("(CLEAR)\n"); return 0; }
static void h_prompted(void *ctx) { (void)ctx; printf("\n"); }
static void h_input(void *ctx, const char *line) { (void)ctx; printf("(INPUT \"%s\")\n", line); }
static void h_quit(void *ctx, int code) { (void)ctx; printf("(QUIT %d)\n", code); }

static int by_kind_name(const void *a, const void *b)
{
    const struct sh_dirent *x = a, *y = b;
    return x->dir != y->dir ? y->dir - x->dir : strcmp(x->name, y->name);
}

static int fill(const char *path, const char *name, struct sh_dirent *e)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return -1;
    memset(e, 0, sizeof *e);
    snprintf(e->name, sizeof e->name, "%s", name);
    e->dir = S_ISDIR(st.st_mode);
    e->size = e->dir ? 0 : (unsigned long)st.st_size;
    e->date = (unsigned short)((2026 - 1980) << 9 | 11 << 5 | 13);     /* (the same for all: the transcript is kept) */
    e->time = (unsigned short)(12 << 11 | 34 << 5);
    return 0;
}

static int h_dir_read(void *ctx, const char *dir, struct sh_dirent **out)
{
    char path[600], full[900];
    struct sh_dirent *e = NULL;
    struct dirent *de;
    DIR *d;
    int n = 0;
    (void)ctx;
    native(dir, path, sizeof path);
    *out = NULL;
    if (!(d = opendir(path)))
        return -1;
    while ((de = readdir(d)) != NULL) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
            continue;
        e = realloc(e, (size_t)(n + 1) * sizeof *e);
        snprintf(full, sizeof full, "%s/%s", path, de->d_name);
        if (fill(full, de->d_name, &e[n]) == 0)
            n++;
    }
    closedir(d);
    if (n)
        qsort(e, (size_t)n, sizeof *e, by_kind_name);
    *out = e;
    return n;
}

static void h_dir_free(void *ctx, struct sh_dirent *e) { (void)ctx; free(e); }

static int h_stat(void *ctx, const char *path, struct sh_dirent *e)
{
    char p[600];
    const char *name = strrchr(path, '\\');
    (void)ctx;
    native(path, p, sizeof p);
    return fill(p, name ? name + 1 : path, e);
}

static int h_is_dir(void *ctx, const char *path)
{
    struct sh_dirent e;
    return h_stat(ctx, path, &e) == 0 && e.dir;
}

static void *h_open(void *ctx, const char *path, int mode)
{
    char p[600];
    (void)ctx;
    native(path, p, sizeof p);
    return fopen(p, mode == 'r' ? "rb" : mode == 'w' ? "wb" : "ab");
}

static int h_read(void *ctx, void *f, void *buf, int n) { (void)ctx; return (int)fread(buf, 1, (size_t)n, f); }
static int h_put(void *ctx, void *f, const void *buf, int n) { (void)ctx; return fwrite(buf, 1, (size_t)n, f) == (size_t)n ? 0 : -1; }
static int h_close(void *ctx, void *f) { (void)ctx; return fclose(f) != 0 ? -1 : 0; }

static int h_mkdir(void *ctx, const char *path)
{
    char p[600];
    (void)ctx;
    native(path, p, sizeof p);
    return mkdir(p, 0777);
}

static int h_rmdir(void *ctx, const char *path)
{
    char p[600];
    (void)ctx;
    native(path, p, sizeof p);
    return rmdir(p);
}

static int h_unlink(void *ctx, const char *path)
{
    char p[600];
    (void)ctx;
    native(path, p, sizeof p);
    return unlink(p);
}

static int h_rename(void *ctx, const char *from, const char *to)
{
    char a[600], b[600];
    struct stat st;
    (void)ctx;
    native(from, a, sizeof a);
    native(to, b, sizeof b);
    if (stat(b, &st) == 0)
        return -1;                              /* (DOS renames onto nothing that is there) */
    return rename(a, b);
}

static int h_run(void *ctx, const char *command, const char *prog, const char *name, const char *cwd, const char *env,
                 int detach)
{
    const char *e;
    int r = prog && (strstr(prog, "\\A.COM") || strstr(prog, "\\B.COM")) ? 1 : -1;
    (void)ctx;
    if (detach)
        r = 0;
    printf("(RUN command=\"%s\" prog=%s name=%s cwd=%s env=", command, prog ? prog : "-", name, cwd);
    for (e = env; *e; e += strlen(e) + 1)
        printf("%s;", e);
    printf(" detach=%d -> %d)\n", detach, r);
    return r;
}

static const char *const extra_names[] = { "XTRA", "XWAIT", 0 };
static const char *const extra_help[] = { "XTRA                a command of the host's", 0 };

static int h_extra(void *ctx, int argc, char **argv, const char *line)
{
    (void)ctx, (void)line;
    printf("(EXTRA %s args=%d)\n", argv[0], argc - 1);
    return !strcmp(argv[0], "XTRA") || !strcmp(argv[0], "xtra") ? 7 : SH_UNDER_WAY;
}

static void *h_alloc(void *ctx, unsigned n) { (void)ctx; return malloc(n); }
static void h_free(void *ctx, void *p) { (void)ctx; free(p); }

/* Two drives: C: of 100 MB with a quarter free and a label, and a floppy with none; D: of 20 GB, nearly full. */
static int h_space(void *ctx, int drive, unsigned long *total, unsigned long *left, char label[12])
{
    (void)ctx;
    if (drive == 2)
        *total = 102400, *left = 25600, strcpy(label, "TESTDISK");
    else if (drive == 0)
        *total = 1424, *left = 1000, label[0] = 0;
    else if (drive == 3)
        *total = 20971520ul, *left = 104857, strcpy(label, "BIG");
    else
        return -1;
    return 0;
}

static const struct sh_host host = {
    0, h_write, h_clear, h_prompted, h_input, h_quit, h_dir_read, h_dir_free, h_is_dir, h_stat, h_open, h_read, h_put,
    h_close, h_mkdir, h_rmdir, h_unlink, h_rename, h_space, h_run, 0, extra_names, extra_help, h_extra, h_alloc, h_free };

int main(int argc, char **argv)
{
    static const char *const env[] = { "PATH=C:\\BIN;C:\\TOOLS", "COMSPEC=C:\\COMMAND.COM", 0 };
    char line[600], p[600], buf[40];
    FILE *script, *f;
    if (argc != 3 || !(script = fopen(argv[2], "r"))) {
        fprintf(stderr, "usage: shell_test ROOT SCRIPT\n");
        return 2;
    }
    root = argv[1];
    if (sh_fmt(buf, sizeof buf, "%-4s|%4d|%04u|%.2s|%lu|%c|%%|%.*s", "ab", -12, 34u, "xyz", 1234567ul, 'q', 1, "zz")
        != 31 || strcmp(buf, "ab  | -12|0034|xy|1234567|q|%|z")) {
        printf("sh_fmt: \"%s\"\n", buf);
        return 1;
    }
    sh_start(&sh, &host, env);
    sh_prompt(&sh);
    while (fgets(line, sizeof line, script)) {
        char *arg;
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0] || line[0] == '#')
            continue;
        arg = strchr(line, ' ');
        arg = arg ? arg + 1 : line + strlen(line);
        if (line[0] == '>') {
            printf("%s\n", arg);
            sh_line(&sh, arg);
        } else if (!strncmp(line, "ended", 5)) {
            printf("(ENDED %d)\n", atoi(arg));
            sh_ended(&sh, atoi(arg));
        } else if (!strcmp(line, "up") || !strcmp(line, "down")) {
            sh_history(&sh, line[0] == 'u' ? -1 : 1);
        } else if (!strncmp(line, "tab", 3)) {
            printf("(TAB \"%s\")\n", arg);
            sh_complete(&sh, arg);
        } else if (!strncmp(line, "mkdir ", 6)) {
            native(arg, p, sizeof p);
            mkdir(p, 0777);
        } else if (!strncmp(line, "file ", 5)) {
            char *text = strchr(arg, ' ');
            if (text)
                *text++ = 0;
            native(arg, p, sizeof p);
            if ((f = fopen(p, "wb")) != NULL) {
                fprintf(f, "%s\r\n", text ? text : "");
                fclose(f);
            }
        } else if (!strncmp(line, "cat ", 4)) {
            int c;
            native(arg, p, sizeof p);
            printf("(CAT %s: ", arg);
            if ((f = fopen(p, "rb")) != NULL) {
                while ((c = fgetc(f)) != EOF)
                    if (c == '\r')
                        printf("\\r");
                    else if (c == '\n')
                        printf("\\n");
                    else
                        putchar(c);
                fclose(f);
            } else {
                printf("no such file");
            }
            printf(")\n");
        } else {
            printf("shell_test: what is \"%s\"?\n", line);
            return 1;
        }
    }
    printf("\n");
    return 0;
}
