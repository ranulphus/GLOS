/* The Terminal's side of GLOS's shell (PRD D50; docs/milestones-m9.md step E; docs/v1/shell.md, step S1).
 *
 * The interpreter is a library (sdk/shell): DOS's commands and syntax, the variables, the folders, the
 * history, completion. This is its host here: what it writes goes to the window (term.c), its files are the
 * toolkit's, and a program is started by GLOS as a console job of this window's (PRD D51), in the shell's
 * folder, with the variables it has set; a line with a pipe or a redirection, and a batch file, through
 * COMMAND.COM /C (D40). A GLOS app goes on beside the terminal. A program whose profile says screen = full
 * runs full-screen from the start. SSH, SFTP and SSH-KEYGEN (ssh.c) are this host's own commands.
 *
 * Up and Down go through the history, Tab completes a file's name (term.c's keys). */
#include <stdio.h>
#ifdef __DJGPP__
#include <dos.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <glos/gui.h>

#include "term.h"
#include "../../sdk/shell/shell.h"

static struct sh sh;

/* what runs */
static glos_handle job;                 /* the program's job, or 0 */
static unsigned job_state;

extern char **environ;

int term_busy(void) { return job != 0; }

static void err(const char *s)
{
    term_attr(T_ERROR);
    term_write_dos(s, -1);
    term_attr(T_NORMAL);
}

/* ---- what the shell asks of its host */

/* (the shell's own output is DOS's, as COMMAND.COM's would be: names and files' text in its code page) */
static void h_write(void *ctx, int kind, const char *s, int n)
{
    (void)ctx;
    if (kind == SH_TEXT) {
        term_write(s, n);
        return;
    }
    if (kind != SH_OUT)
        term_attr(kind == SH_ERR ? T_ERROR : T_PROMPT);
    term_write_dos(s, n);
    if (kind != SH_OUT)
        term_attr(T_NORMAL);
}

static int h_clear(void *ctx) { (void)ctx; term_clear(); return 0; }
static void h_prompted(void *ctx) { (void)ctx; term_prompt_done(); }
static void h_input(void *ctx, const char *line) { (void)ctx; term_input_set(line); }
static void h_quit(void *ctx, int code) { (void)ctx; term_quit(code); }

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

/* A program: a console job of this window's. */
static int h_run(void *ctx, const char *command, const char *prog, const char *name, const char *cwd, const char *env,
                 int detach)
{
    char profile[40], screen[16], title[128];
    unsigned how = GLOS_RUN_WINDOW;
    glos_handle h;
    int set = 0;
    (void)ctx;
    if (prog) {
        snprintf(profile, sizeof profile, "program %s", gui_path_name(prog));
        if (glos_setting_get(profile, "screen", screen, sizeof screen, &set) == 0 && set && !strcasecmp(screen, "full"))
            how = GLOS_RUN_FULL;
    }
    h = glos_job_run(command, cwd, env, how, detach ? GLOS_RUNF_DETACH : 0, 0);
    if (detach ? glos_error() != 0 : !h)
        return -1;
    if (detach)
        return 0;
    job = h;
    job_state = GLOS_JS_QUEUED;
    term_title(gui_dos_text(gui_path_name(name), title, sizeof title));
    return 1;
}

static const char *const extra_names[] = { "SSH", "SFTP", "SSH-KEYGEN", 0 };
static const char *const extra_help[] = {
    "SSH [user@]host [command]  a session there (-p port, -i key, -l user)",
    "SFTP [user@]host    files there and here (help inside lists its commands)",
    "SSH-KEYGEN          a key of your own (SSH\\ID_ED25) for servers' authorized_keys",
    0 };

static int h_extra(void *ctx, int argc, char **argv, const char *line)
{
    (void)ctx, (void)line;
    if (!strcasecmp(argv[0], "SSH"))
        return ssh_command(argc, argv);         /* (-2: under way, as a program is) */
    if (!strcasecmp(argv[0], "SFTP"))
        return sftp_command(argc, argv);
    if (!strcasecmp(argv[0], "SSH-KEYGEN"))
        return keygen_command(argc, argv);
    return SH_NOT_MINE;
}

static void *h_alloc(void *ctx, unsigned n) { (void)ctx; return malloc(n); }
static void h_free(void *ctx, void *p) { (void)ctx; free(p); }

/* A drive's size and free space, as DOS says them (a FAT32 volume's are DOS's 2 GB at most by this call; the
   kernel's sessions ask the newer one). No label. */
static int h_space(void *ctx, int drive, unsigned long *total, unsigned long *left, char label[12])
{
#ifdef __DJGPP__
    struct diskfree_t d;
    unsigned long per;
    (void)ctx;
    if (_dos_getdiskfree((unsigned)drive + 1, &d) != 0)
        return -1;
    per = (unsigned long)d.sectors_per_cluster * d.bytes_per_sector;
    *total = per >= 1024 ? d.total_clusters * (per >> 10) : d.total_clusters / (1024 / (per ? per : 512));
    *left = per >= 1024 ? d.avail_clusters * (per >> 10) : d.avail_clusters / (1024 / (per ? per : 512));
    label[0] = 0;
    return 0;
#else                                           /* (the host's build, for the pictures: one drive, made up) */
    (void)ctx;
    if (drive != 2)
        return -1;
    *total = 102400, *left = 25600;
    label[0] = 0;
    return 0;
#endif
}

static const struct sh_host host = {
    0, h_write, h_clear, h_prompted, h_input, h_quit, h_dir_read, h_dir_free, h_is_dir, h_stat, h_open, h_read, h_put,
    h_close, h_mkdir, h_rmdir, h_unlink, h_rename, h_space, h_run, 0, extra_names, extra_help, h_extra, h_alloc, h_free };

/* ---- what the terminal asks of the shell (term.h) */

void shell_start(void) { sh_start(&sh, &host, (const char *const *)environ); }
void shell_prompt(void) { sh_prompt(&sh); }
void shell_history(int by) { sh_history(&sh, by); }
void shell_complete(void) { sh_complete(&sh, term_input()); }
void shell_resume(int code) { sh_ended(&sh, code); }

void shell_line(const char *line)
{
    if (ssh_asking()) {                         /* (a question's answer, a password, sftp's command: not history) */
        ssh_answer(line);
        return;
    }
    sh_line(&sh, line);
}

/* ---- the running program */

void shell_type(const unsigned short *keys, int n)
{
    if (job)
        glos_job_keys(job, keys, (unsigned)n);
}

void shell_break(void)
{
    if (job) {
        glos_job_signal(job, GLOS_SIGNAL_END);
        err("^Break\r\n");
    }
}

void shell_poll(void)
{
    char buf[1024];
    struct glos_job_status js;
    unsigned st, code;
    int n;
    while (job) {
        if (glos_job_status(job, &js) != 0)     /* (its state first: once it is done, what is read is all of it) */
            n = -1;
        else
            n = glos_job_read(job, GLOS_JOB_OUT, buf, sizeof buf);
        if (n < 0) {
            glos_close(job);
            job = 0;
            sh_ended(&sh, 1);                   /* (the job is gone from under us: the line's rest, the prompt) */
            break;
        }
        st = js.state, code = js.code;
        if (n > 0)
            term_write_dos(buf, n);     /* (a DOS program's bytes, in its code page) */
        job_state = st;
        if (st == GLOS_JS_DONE || st == GLOS_JS_APP) {
            unsigned short left[64];
            int nleft = 0;
            if (n == (int)sizeof buf)
                continue;                       /* (more of its output first) */
            if (st == GLOS_JS_DONE)             /* (typed ahead, unread: ours again) */
                nleft = glos_job_read(job, GLOS_JOB_KEYS, left, sizeof left) / 2;
            glos_close(job);                    /* (a GLOS app goes on beside: nothing of it is ours) */
            job = 0;
            term_title("");
            sh_ended(&sh, st == GLOS_JS_DONE ? (int)code : 0);
            if (nleft > 0)
                term_keys(left, nleft);
            return;
        }
        if (n < (int)sizeof buf)
            break;
    }
}
