/* GLOS's shell for the host's shim (docs/v1/shell.md, step S2c): API 2.1's GLOS_RUN_SHELL, for the Terminal
 * app's tests on the host, where there is no kernel. The same interpreter and line editor as the kernel's
 * sessions (sdk/shell), with the toolkit's files (the test's drives) and no programs. The terminal's side is
 * kernel/shell/shsess.c's, byte for byte: the pictures made here are compared with the real Terminal's in
 * Loop A, so a difference between the two shows there.
 *
 * Nothing waits and there is no thread: what is typed (glos_job_write) is acted on at once, and what the shell
 * wrote is there for the next glos_job_read. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/cp.h>
#include <glos/gui.h>

#include "../../sdk/shell/edit.h"
#include "../../sdk/shell/shell.h"

#define SHELL_JOB ((glos_handle)0x08FE0001)

static struct sh sh;
static struct ed ed;
static int open_, editing, quit, code, page = 437, out_cr;
static unsigned char *out;
static size_t out_n, out_room;

static void term_bytes(const void *d, int n)
{
    if (out_n + (size_t)n > out_room) {
        out_room = (out_n + (size_t)n) * 2 + 4096;
        out = realloc(out, out_room);
    }
    memcpy(out + out_n, d, (size_t)n);
    out_n += (size_t)n;
}

/* Text in the PC's code page as UTF-8; lf: a line feed with no carriage return before it gets one. */
static void term_text(const char *t, int n, int lf)
{
    unsigned char b[8];
    int i, k;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)t[i];
        k = 0;
        if (c < 0x80) {
            if (lf && c == '\n' && (i ? t[i - 1] != '\r' : !out_cr))
                b[k++] = '\r';
            b[k++] = c;
        } else {
            unsigned u = cp_char(page, c);
            if (u < 0x800) {
                b[k++] = (unsigned char)(0xC0 | u >> 6);
            } else {
                b[k++] = (unsigned char)(0xE0 | u >> 12);
                b[k++] = (unsigned char)(0x80 | ((u >> 6) & 0x3F));
            }
            b[k++] = (unsigned char)(0x80 | (u & 0x3F));
        }
        term_bytes(b, k);
    }
    if (n)
        out_cr = t[n - 1] == '\r';
}

static void ed_put(void *ctx, const char *t, int n) { (void)ctx; term_text(t, n, 0); }

/* ---- what the shell asks of its host */

static void h_write(void *ctx, int kind, const char *t, int n)
{
    (void)ctx;
    if (kind == SH_PROMPT) {
        char p[ED_PROMPT_MAX];
        int k = 0;
        if (quit)
            return;
        if (n > ED_PROMPT_MAX - 8)
            t += n - (ED_PROMPT_MAX - 8), n = ED_PROMPT_MAX - 8;
        memcpy(p, "\033[1m", 4), k = 4;
        memcpy(p + k, t, (size_t)n), k += n;
        memcpy(p + k, "\033[0m", 4), k += 4;
        ed_begin(&ed, p, k);
        editing = 1;
        return;
    }
    if (editing) {
        ed_away(&ed);
        editing = 0;
    }
    if (kind == SH_ERR)
        term_bytes("\033[31m", 5);
    term_text(t, n, kind == SH_TEXT);
    if (kind == SH_ERR)
        term_bytes("\033[0m", 4);
}

static int h_clear(void *ctx) { (void)ctx; term_bytes("\033[H\033[2J", 7); return 0; }
static void h_prompted(void *ctx) { (void)ctx; }

static void h_input(void *ctx, const char *line)
{
    (void)ctx;
    if (editing)
        ed_set(&ed, line);
}

static void h_quit(void *ctx, int c) { (void)ctx; quit = 1, code = c; }

static void entry(struct sh_dirent *d, const struct gui_dirent *e)
{
    snprintf(d->name, sizeof d->name, "%s", e->name);
    d->size = e->size, d->date = e->date, d->time = e->time, d->dir = e->dir;
}

static int h_dir_read(void *ctx, const char *dir, struct sh_dirent **out)
{
    struct gui_dirent *e;
    struct sh_dirent *d;
    int n = gui_dir_read(dir, &e), i;
    (void)ctx;
    *out = NULL;
    if (n < 0)
        return -1;
    if (!(d = malloc(((size_t)n + 1) * sizeof *d))) {
        free(e);
        return -1;
    }
    for (i = 0; i < n; i++)
        entry(&d[i], &e[i]);
    free(e);
    *out = d;
    return n;
}

static void h_dir_free(void *ctx, struct sh_dirent *e) { (void)ctx; free(e); }
static int h_is_dir(void *ctx, const char *path) { (void)ctx; return gui_is_dir(path); }

static int h_stat(void *ctx, const char *path, struct sh_dirent *d)
{
    struct gui_dirent e;
    (void)ctx;
    if (gui_stat(path, &e) != 0)
        return -1;
    entry(d, &e);
    return 0;
}

static void *h_open(void *ctx, const char *path, int mode)
{
    (void)ctx;
    return gui_fopen(path, mode == 'r' ? "rb" : mode == 'w' ? "wb" : "ab");
}

static int h_read(void *ctx, void *f, void *buf, int n) { (void)ctx; return (int)fread(buf, 1, (size_t)n, f); }

static int h_put(void *ctx, void *f, const void *buf, int n)
{
    (void)ctx;
    return fwrite(buf, 1, (size_t)n, f) == (size_t)n ? 0 : -1;
}

static int h_close(void *ctx, void *f) { (void)ctx; return fclose(f) != 0 ? -1 : 0; }
static int h_mkdir(void *ctx, const char *path) { (void)ctx; return gui_mkdir(path); }
static int h_rmdir(void *ctx, const char *path) { (void)ctx; return gui_rmdir(path); }
static int h_unlink(void *ctx, const char *path) { (void)ctx; return gui_unlink(path); }
static int h_rename(void *ctx, const char *from, const char *to) { (void)ctx; return gui_rename(from, to); }

/* (nothing runs on the host) */
static int h_run(void *ctx, const char *command, const char *prog, const char *name, const char *cwd, const char *env,
                 int detach)
{
    (void)ctx, (void)command, (void)prog, (void)name, (void)cwd, (void)env, (void)detach;
    return -1;
}

static const char *const none[] = { 0 };

static int h_extra(void *ctx, int argc, char **argv, const char *line)
{
    (void)ctx, (void)argc, (void)argv, (void)line;
    return SH_NOT_MINE;
}

static void *h_alloc(void *ctx, unsigned n) { (void)ctx; return malloc(n); }
static void h_free(void *ctx, void *p) { (void)ctx; free(p); }

/* One drive, made up. */
static int h_space(void *ctx, int drive, unsigned long *total, unsigned long *left, char label[12])
{
    (void)ctx;
    if (drive != 2)
        return -1;
    *total = 102400, *left = 25600;
    label[0] = 0;
    return 0;
}

static const struct sh_host host = {
    0, h_write, h_clear, h_prompted, h_input, h_quit, h_dir_read, h_dir_free, h_is_dir, h_stat, h_open, h_read, h_put,
    h_close, h_mkdir, h_rmdir, h_unlink, h_rename, h_space, h_run, 0, none, none, h_extra, h_alloc, h_free };

/* ---- JOB's calls for it (the rest of JOB is glosshim.c's: nothing runs) */

glos_handle glos_job_shell_in(const char *line, const char *dir, unsigned cols, unsigned rows)
{
    static const char *const env[] = { "PATH=C:\\GLOS", "COMSPEC=C:\\COMMAND.COM", 0 };
    (void)rows;
    if (open_ || line)
        return 0;
    open_ = 1;
    quit = editing = code = 0;
    out_n = 0;
    page = cp_active();
    ed_init(&ed, ed_put, 0, (int)cols);
    sh_start(&sh, &host, env);
    if (dir)                                    /* ("\\": the boot drive's root: A:, as Loop A's machines, which
                                                   start from a floppy; the pictures are compared with theirs) */
        sh_folder(&sh, !strcmp(dir, "\\") ? "A:\\" : dir);
    sh_prompt(&sh);
    return SHELL_JOB;
}

int glos_job_write(glos_handle job, const void *data, unsigned length)
{
    const unsigned char *d = data;
    unsigned i;
    if (job != SHELL_JOB || !open_)
        return -1;
    for (i = 0; i < length && !quit; i++)
        switch (ed_key(&ed, d[i])) {
        case ED_ENTER:
            editing = 0;
            sh_line(&sh, ed.line);
            break;
        case ED_UP: sh_history(&sh, -1); break;
        case ED_DOWN: sh_history(&sh, 1); break;
        case ED_TAB: sh_complete(&sh, ed.line); break;
        case ED_BREAK:
            editing = 0;
            sh_prompt(&sh);
            break;
        case ED_END:
            quit = 1;
            code = sh.errorlevel;
            break;
        }
    return (int)length;
}

int glos_job_read(glos_handle job, unsigned stream, void *buf, unsigned room)
{
    size_t n = out_n < room ? out_n : room;
    if (job != SHELL_JOB || !open_ || stream != GLOS_JOB_OUT)
        return -1;
    memcpy(buf, out, n);
    memmove(out, out + n, out_n - n);
    out_n -= n;
    return (int)n;
}

int glos_job_size(glos_handle job, unsigned cols, unsigned rows)
{
    (void)rows;
    if (job != SHELL_JOB || !open_)
        return GLOS_E_HANDLE;
    if (editing)
        ed_size(&ed, (int)cols);
    return 0;
}

int glos_job_status(glos_handle job, struct glos_job_status *st)
{
    if (job != SHELL_JOB || !open_)
        return GLOS_E_HANDLE;
    memset(st, 0, sizeof *st);
    st->state = quit ? GLOS_JS_DONE : GLOS_JS_RUNNING;
    st->code = (unsigned)code;
    st->out = (unsigned)out_n;
    return 0;
}

int glos_job_signal(glos_handle job, unsigned what) { (void)what; return job == SHELL_JOB && open_ ? 0 : GLOS_E_HANDLE; }
