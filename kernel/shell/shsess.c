/* A shell's session in the kernel (shsess.h; docs/v1/shell.md, step S2a): GLOS's shell (sdk/shell) on a
 * thread of its own, its terminal two rings of bytes.
 *
 * The thread reads what is typed, a byte at a time: UTF-8 from the terminal, turned into the PC's code page,
 * which is what the shell's text is (as DOS's names and files are). With a line under way the bytes are the
 * line editor's (sdk/shell/edit.c); the line it gives is the shell's to run. What the shell and the editor
 * write goes the other way, the code page turned into UTF-8, errors in red and the prompt in bold where
 * there is a terminal.
 *
 * The shell's host here:
 *   - files: INT 21h through the DOS server (kernel/dos/dos.c), a request at a time, waited for; the server
 *     is held while the session lasts, as an SFTP session holds it (32 KB of DOS's memory);
 *   - programs: the agent's jobs (kernel/dos/agent.c), as an app's console jobs are: what one writes goes to
 *     the terminal, what is typed meanwhile is typed to it (a character with its key's scan code where the
 *     key is a common one), Ctrl+C is the program's Ctrl+C, and its end is the shell's to go on from;
 *   - memory: the kernel's heap.
 *
 * Two threads touch a session: its own, and its terminal's side (the ssh thread), which only moves bytes in
 * and out of the rings, says the size, and frees it. The session's memory is freed by that side, once the
 * session's thread has said it has ended. */
#include <string.h>

#include "glos/bootinfo.h"
#include "glos/cp.h"
#include "glos/region.h"
#include "glos/api.h"
#include "agent.h"
#include "dos.h"
#include "io.h"
#include "kapi.h"
#include "kcfg.h"
#include "kprintf.h"
#include "mixer.h"
#include "mm.h"
#include "sched.h"
#include "shsess.h"
#include "timer.h"
#include "vm.h"
#include "../../sdk/shell/shell.h"
#include "../../sdk/shell/edit.h"

#define IN_RING  4096u                  /* powers of two */
#define OUT_RING 16384u
#define ERR_RING 4096u                  /* (with no terminal: what is written as errors, kept apart) */
#define PATH_AT  0u                     /* in the DOS server's buffer: a path, */
#define PATH2_AT 128u                   /* a second, */
#define DTA_AT   256u                   /* the find's state, */
#define DATA_AT  512u                   /* a file's bytes */

struct shsess {
    struct sh sh;
    struct ed ed;
    struct sh_host host;
    struct waitq wq;                    /* its thread waits here: for keys, for room to write, for its end */
    void (*kick)(void);
    u8 in[IN_RING], out[OUT_RING], err[ERR_RING];
    volatile u32 ih, it, oh, ot, eh, et;
    volatile u32 cols, rows;
    volatile u8 closing, ended, resized;
    u8 tty, editing, quit;
    u8 starting;                        /* (the shell's first words: a terminal's alone) */
    char *line;                         /* with no terminal: the one line it is there to run */
    int code, page;
    u32 utf, utf_left;                  /* a UTF-8 character under way from the terminal */
    u32 job;                            /* the program that runs: the agent's number for it */
    u8 job_on;
    volatile u8 job_evt;                /* ... it has written, or moved on */
    u8 job_esc;                         /* a key's sequence under way, typed to it */
    struct dos_req q;
    u8 xfer[256];                       /* a request's paths */
    u8 dta[43];
    struct shsess *next;                /* (freed by its terminal's side, its thread yet to end) */
};

static struct shsess *leaving;

/* ---- to the terminal */

/* Bytes as they are, to the terminal's side: what is written (stream 0), or with no terminal what is written
   as errors (1). The thread waits for room. */
static void stream_bytes(struct shsess *s, int stream, const u8 *d, u32 n)
{
    u8 *ring = stream ? s->err : s->out;
    volatile u32 *head = stream ? &s->eh : &s->oh, *tail = stream ? &s->et : &s->ot;
    u32 size = stream ? ERR_RING : OUT_RING;
    while (n) {
        u32 f = irq_save(), room, k, at;
        while ((room = size - (*tail - *head)) == 0 && !s->closing)
            thread_wait(&s->wq, timer_ticks() + 256);
        if (s->closing) {                       /* (nobody to read it) */
            irq_restore(f);
            return;
        }
        k = n < room ? n : room;
        at = *tail % size;
        if (k > size - at)
            k = size - at;
        memcpy(ring + at, d, k);
        *tail += k;
        irq_restore(f);
        d += k, n -= k;
        s->kick();
    }
}

static void term_bytes(struct shsess *s, const u8 *d, u32 n) { stream_bytes(s, 0, d, n); }

/* Text in the PC's code page (and the sequences among it, which are ASCII), as UTF-8. lf: a line feed with no
   carriage return before it gets one (a file's own text). */
static void term_text(struct shsess *s, const char *t, int n, int lf)
{
    u8 b[256];
    u32 k = 0;
    int i;
    for (i = 0; i < n; i++) {
        u8 c = (u8)t[i];
        if (k > sizeof b - 4) {
            term_bytes(s, b, k);
            k = 0;
        }
        if (c < 0x80) {
            if (lf && c == '\n' && (!i || t[i - 1] != '\r'))
                b[k++] = '\r';
            b[k++] = c;
        } else {
            u32 u = cp_char(s->page, c);
            if (u < 0x800) {
                b[k++] = (u8)(0xC0 | u >> 6);
            } else {
                b[k++] = (u8)(0xE0 | u >> 12);
                b[k++] = (u8)(0x80 | ((u >> 6) & 0x3F));
            }
            b[k++] = (u8)(0x80 | (u & 0x3F));
        }
    }
    term_bytes(s, b, k);
}

static void ed_put(void *ctx, const char *t, int n) { term_text(ctx, t, n, 0); }

static void h_write(void *ctx, int kind, const char *t, int n)
{
    struct shsess *s = ctx;
    if (!s->tty) {                              /* no terminal: bytes as they are, errors apart; no prompt, and
                                                   none of the shell's greeting */
        if (kind != SH_PROMPT && !s->starting)
            stream_bytes(s, kind == SH_ERR, (const u8 *)t, (u32)n);
        return;
    }
    if (kind == SH_PROMPT) {
        char p[ED_PROMPT_MAX];
        int k = 0;
        if (s->quit)                            /* (EXIT: nothing more to ask) */
            return;
        if (n > ED_PROMPT_MAX - 8)
            t += n - (ED_PROMPT_MAX - 8), n = ED_PROMPT_MAX - 8;
        memcpy(p, "\033[1m", 4), k = 4;
        memcpy(p + k, t, (size_t)n), k += n;
        memcpy(p + k, "\033[0m", 4), k += 4;
        ed_begin(&s->ed, p, k);
        s->editing = 1;
        return;
    }
    if (s->editing) {                           /* (completion's names: below what is typed) */
        ed_away(&s->ed);
        s->editing = 0;
    }
    if (kind == SH_ERR)
        term_bytes(s, (const u8 *)"\033[31m", 5);
    term_text(s, t, n, kind == SH_TEXT);
    if (kind == SH_ERR)
        term_bytes(s, (const u8 *)"\033[0m", 4);
}

static void h_clear(void *ctx)
{
    struct shsess *s = ctx;
    if (s->tty)
        term_bytes(s, (const u8 *)"\033[H\033[2J", 7);
}

static void h_prompted(void *ctx) { (void)ctx; }

static void h_input(void *ctx, const char *line)
{
    struct shsess *s = ctx;
    if (s->tty && s->editing)
        ed_set(&s->ed, line);
}

static void h_quit(void *ctx, int code)
{
    struct shsess *s = ctx;
    s->quit = 1;
    s->code = code;
}

/* ---- files: INT 21h, through the DOS server */

static void req(struct shsess *s) { memset(&s->q, 0, sizeof s->q); }

static void call(struct shsess *s, u32 k, u16 ax, u16 bx, u16 cx, u16 dx)
{
    s->q.r[k].ax = ax, s->q.r[k].bx = bx, s->q.r[k].cx = cx, s->q.r[k].dx = dx;
    s->q.r[k].ds = s->q.r[k].es = DOS_XFER;
    if (k + 1 > s->q.n)
        s->q.n = k + 1;
}

/* A request that sends a path (and a second) to DOS. -1: too long for DOS anyway. */
static int paths(struct shsess *s, const char *a, const char *b)
{
    req(s);
    memset(s->xfer, 0, sizeof s->xfer);
    if (strlen(a) >= PATH2_AT - PATH_AT || (b && strlen(b) >= DTA_AT - PATH2_AT))
        return -1;
    strcpy((char *)s->xfer + PATH_AT, a);
    if (b)
        strcpy((char *)s->xfer + PATH2_AT, b);
    s->q.in = s->xfer, s->q.in_len = sizeof s->xfer, s->q.in_at = 0;
    return 0;
}

/* The request made: 0, or -1 (DOS's error in s->q.error). */
static int made(struct shsess *s) { return dos_call(&s->q) != 0 || s->q.error ? -1 : 0; }

static int is_root(const char *p) { return p[0] && p[1] == ':' && p[2] == '\\' && !p[3]; }

static int h_is_dir(void *ctx, const char *path)
{
    struct shsess *s = ctx;
    if (is_root(path)) {                        /* a drive: its free space is asked for (FFFFh: no such drive) */
        req(s);
        call(s, 0, 0x3600, 0, 0, (u16)((path[0] | 32) - 'a' + 1));
        return made(s) == 0 && s->q.r[0].ax != 0xFFFF;
    }
    if (paths(s, path, 0) != 0)
        return 0;
    call(s, 0, 0x4300, 0, 0, PATH_AT);
    return made(s) == 0 && (s->q.r[0].cx & 0x10) != 0;
}

static void entry(struct sh_dirent *e, const u8 *dta)
{
    u32 i;
    memset(e, 0, sizeof *e);
    for (i = 0; i < 12 && dta[0x1E + i]; i++)
        e->name[i] = (char)dta[0x1E + i];
    e->dir = (dta[0x15] & 0x10) != 0;
    e->time = (u16)(dta[0x16] | dta[0x17] << 8);
    e->date = (u16)(dta[0x18] | dta[0x19] << 8);
    e->size = e->dir ? 0 : (u32)dta[0x1A] | (u32)dta[0x1B] << 8 | (u32)dta[0x1C] << 16 | (u32)dta[0x1D] << 24;
}

/* Find, into s->dta: the first of a path's, or the next after what s->dta holds. 0, or -1. */
static int find(struct shsess *s, const char *first)
{
    if (first) {
        if (paths(s, first, 0) != 0)
            return -1;
    } else {
        req(s);
        s->q.in = s->dta, s->q.in_len = sizeof s->dta, s->q.in_at = DTA_AT;
    }
    call(s, 0, 0x1A00, 0, 0, DTA_AT);
    if (first)
        call(s, 1, 0x4E00, 0, 0x37, PATH_AT);
    else
        call(s, 1, 0x4F00, 0, 0, 0);
    s->q.out = s->dta, s->q.out_len = sizeof s->dta, s->q.out_at = DTA_AT;
    return made(s);
}

static int h_stat(void *ctx, const char *path, struct sh_dirent *e)
{
    struct shsess *s = ctx;
    if (is_root(path)) {
        memset(e, 0, sizeof *e);
        e->dir = 1;
        return h_is_dir(ctx, path) ? 0 : -1;
    }
    if (find(s, path) != 0)
        return -1;
    entry(e, s->dta);
    return 0;
}

static int before(const struct sh_dirent *a, const struct sh_dirent *b)
{
    return a->dir != b->dir ? a->dir > b->dir : strcmp(a->name, b->name) < 0;
}

static int h_dir_read(void *ctx, const char *dir, struct sh_dirent **out)
{
    struct shsess *s = ctx;
    struct sh_dirent *e, one;
    char pat[SH_PATH + 8];
    u32 n = 0, room = 64, i, j;
    int r;
    *out = 0;
    if (!h_is_dir(ctx, dir))
        return -1;
    sh_fmt(pat, sizeof pat, dir[strlen(dir) - 1] == '\\' ? "%s*.*" : "%s\\*.*", dir);
    if (!(e = kmalloc(room * sizeof *e)))
        return -1;
    for (r = find(s, pat); r == 0; r = find(s, 0)) {
        if ((s->dta[0x15] & 0x08) || !strcmp((const char *)s->dta + 0x1E, ".")
            || !strcmp((const char *)s->dta + 0x1E, ".."))
            continue;                           /* (the volume's name; the folder itself and the one above) */
        if (n == room) {
            struct sh_dirent *more = kmalloc(room * 2 * sizeof *e);
            if (!more)
                break;
            memcpy(more, e, n * sizeof *e);
            kfree(e);
            e = more, room *= 2;
        }
        entry(&one, s->dta);
        for (i = n; i && before(&one, &e[i - 1]); i--)  /* folders first, then by name */
            ;
        for (j = n; j > i; j--)
            e[j] = e[j - 1];
        e[i] = one;
        n++;
    }
    *out = e;
    return (int)n;
}

static void h_dir_free(void *ctx, struct sh_dirent *e)
{
    (void)ctx;
    if (e)
        kfree(e);
}

/* A file is DOS's handle, and one more (0 is no file). */
static void *h_open(void *ctx, const char *path, int mode)
{
    struct shsess *s = ctx;
    u16 h;
    if (paths(s, path, 0) != 0)
        return 0;
    if (mode == 'a') {                          /* there already: opened to write, its end sought */
        call(s, 0, 0x3D02, 0, 0, PATH_AT);
        if (made(s) == 0) {
            h = s->q.r[0].ax;
            req(s);
            call(s, 0, 0x4202, h, 0, 0);
            if (made(s) == 0)
                return (void *)(u32)(h + 1u);
            req(s);
            call(s, 0, 0x3E00, h, 0, 0);
            made(s);
            return 0;
        }
        if (paths(s, path, 0) != 0)
            return 0;
        mode = 'w';
    }
    if (mode == 'w')
        call(s, 0, 0x3C00, 0, 0, PATH_AT);
    else
        call(s, 0, 0x3D00, 0, 0, PATH_AT);
    return made(s) == 0 ? (void *)(u32)(s->q.r[0].ax + 1u) : 0;
}

static int h_read(void *ctx, void *f, void *buf, int n)
{
    struct shsess *s = ctx;
    u32 most = dos_xfer_size() - DATA_AT;
    if (n <= 0)
        return 0;
    if ((u32)n > most)
        n = (int)most;
    req(s);
    call(s, 0, 0x3F00, (u16)((u32)f - 1), (u16)n, DATA_AT);
    s->q.out = buf, s->q.out_len = (u32)n, s->q.out_at = DATA_AT;
    return made(s) == 0 ? s->q.r[0].ax : 0;
}

static int h_put(void *ctx, void *f, const void *buf, int n)
{
    struct shsess *s = ctx;
    const u8 *p = buf;
    u32 most = dos_xfer_size() - DATA_AT;
    while (n > 0) {
        u32 k = (u32)n < most ? (u32)n : most;
        req(s);
        call(s, 0, 0x4000, (u16)((u32)f - 1), (u16)k, DATA_AT);
        s->q.in = p, s->q.in_len = k, s->q.in_at = DATA_AT;
        if (made(s) != 0 || s->q.r[0].ax != k)
            return -1;                          /* (a full disk writes less) */
        p += k, n -= (int)k;
    }
    return 0;
}

static int h_close(void *ctx, void *f)
{
    struct shsess *s = ctx;
    req(s);
    call(s, 0, 0x3E00, (u16)((u32)f - 1), 0, 0);
    return made(s);
}

static int one_path(struct shsess *s, u16 ax, const char *path)
{
    if (paths(s, path, 0) != 0)
        return -1;
    call(s, 0, ax, 0, 0, PATH_AT);
    return made(s);
}

static int h_mkdir(void *ctx, const char *path) { return one_path(ctx, 0x3900, path); }
static int h_rmdir(void *ctx, const char *path) { return one_path(ctx, 0x3A00, path); }
static int h_unlink(void *ctx, const char *path) { return one_path(ctx, 0x4100, path); }

static int h_rename(void *ctx, const char *from, const char *to)
{
    struct shsess *s = ctx;
    if (paths(s, from, to) != 0)
        return -1;
    call(s, 0, 0x5600, 0, 0, PATH_AT);
    s->q.r[0].di = PATH2_AT;
    return made(s);
}

/* ---- programs: the agent's jobs */

static void job_notify(void *owner)             /* (the agent's: any thread, interrupts on or off) */
{
    struct shsess *s = owner;
    u32 f = irq_save();
    s->job_evt = 1;
    thread_wake(&s->wq);
    irq_restore(f);
}

static int h_run(void *ctx, const char *command, const char *prog, const char *name, const char *cwd, const char *env,
                 int detach)
{
    struct shsess *s = ctx;
    struct krun r;
    struct agent_opts o;
    char typed[1024];
    u32 seq, i;
    memset(&r, 0, sizeof r);
    memset(&o, 0, sizeof o);
    r.size = sizeof r;
    r.flags = (s->tty ? KRUN_CONSOLE : 0) | (detach ? KRUN_DETACH : 0);    /* (at a terminal: typed to, its output
                                                   one stream; with none: nobody at it, what it writes as
                                                   errors kept apart) */
    if (prog && strlen(command) >= strlen(prog)) {      /* the program as it was named, its arguments after: the
                                                           agent finds it as it always has (a batch file through
                                                           COMMAND.COM), and its log says what was asked */
        sh_fmt(typed, sizeof typed, "%s%s", name, command + strlen(prog));
        command = typed;
    }
    if (strlen(command) < sizeof r.command)
        strcpy(r.command, command);
    else
        o.command = command;
    if (strlen(cwd) < sizeof r.dir)
        strcpy(r.dir, cwd);
    for (i = 0; i + 2 < sizeof r.env && (env[i] || (i && env[i - 1])); i++)
        r.env[i] = env[i];                      /* (NAME=VALUE strings, to their double NUL) */
    o.notify = job_notify;
    if (agent_post_job(s, &r, &o, &seq) != 0)
        return -1;
    if (detach)
        return 0;
    s->job = seq;
    s->job_on = 1;
    s->job_esc = 0;
    return 1;
}

/* What the program has written, to the terminal; its end, to the shell. */
static void job_turn(struct shsess *s)
{
    u8 buf[512];
    u32 st, code, nout, nerr, ticks;
    int stream, n, gone;
    gone = agent_app_status(s, s->job, &st, &code, &nout, &nerr, &ticks) != 0;  /* (its state first: once it has
                                                   ended, what is read is all of it) */
    for (stream = 0; !gone && stream < 2; stream++)
        while ((n = agent_app_read1(s, s->job, stream, buf, sizeof buf)) > 0) {
            if (s->tty)
                term_text(s, (const char *)buf, n, 1);
            else
                stream_bytes(s, stream, buf, (u32)n);
        }
    if (gone || st == GLOS_JS_DONE || st == GLOS_JS_APP) {
        if (!gone)
            agent_app_forget(s, s->job);        /* (an app goes on beside: nothing of it is the session's) */
        s->job_on = 0;
        sh_ended(&s->sh, gone ? 1 : st == GLOS_JS_DONE ? (int)code : 0);
    }
}

/* (The agent's calls for a job's keys and its end wake the VM thread: interrupts off, as an app's thread has
   them there.) */
static void type_key(struct shsess *s, u16 k)
{
    u32 f = irq_save();
    agent_app_type(s, s->job, &k, 1);
    irq_restore(f);
}

/* A byte typed while the program runs: its key, as the BIOS has keys (the character, and the scan code of the
   key that makes it on a US keyboard where that matters: letters, digits, Enter, Esc, Backspace, Tab, the
   arrows). Ctrl+C is the program's Ctrl+C. */
static void job_key(struct shsess *s, int c)
{
    static const char row1[] = "qwertyuiop", row2[] = "asdfghjkl", row3[] = "zxcvbnm", digits[] = "1234567890";
    const char *at;
    u16 k;
    if (s->job_esc == 1) {
        s->job_esc = c == '[' || c == 'O' ? 2 : 0;
        if (s->job_esc)
            return;
        type_key(s, 0x011B);                    /* (Esc, then something else: Esc itself first) */
    } else if (s->job_esc == 2) {
        s->job_esc = 0;
        k = c == 'A' ? 0x4800 : c == 'B' ? 0x5000 : c == 'C' ? 0x4D00 : c == 'D' ? 0x4B00 : c == 'H' ? 0x4700
            : c == 'F' ? 0x4F00 : 0;
        if (k)
            type_key(s, k);
        return;
    }
    if (c == 27) {
        s->job_esc = 1;
        return;
    }
    if (c == 3) {
        u32 f = irq_save();
        agent_app_signal(s, s->job, 0);
        irq_restore(f);
        return;
    }
    if (c == '\n')
        return;                                 /* (Enter is its CR) */
    if (c == 127)
        c = 8;
    k = (u16)c;
    if (c == '\r')
        k |= 0x1C00;
    else if (c == 8)
        k |= 0x0E00;
    else if (c == '\t')
        k |= 0x0F00;
    else if (c == ' ')
        k |= 0x3900;
    else if ((at = strchr(row1, c | 32)) != 0 && (c | 32) >= 'a')
        k |= (u16)((0x10 + (at - row1)) << 8);
    else if ((at = strchr(row2, c | 32)) != 0 && (c | 32) >= 'a')
        k |= (u16)((0x1E + (at - row2)) << 8);
    else if ((at = strchr(row3, c | 32)) != 0 && (c | 32) >= 'a')
        k |= (u16)((0x2C + (at - row3)) << 8);
    else if (c >= '0' && c <= '9' && (at = strchr(digits, c)) != 0)
        k |= (u16)((0x02 + (at - digits)) << 8);
    type_key(s, k);
}

static void *h_alloc(void *ctx, unsigned n) { (void)ctx; return kmalloc(n); }
static void h_free(void *ctx, void *p) { (void)ctx; kfree(p); }

/* ---- from the terminal */

#define EV_GONE (-1)                    /* the terminal has gone */
#define EV_JOB  (-2)                    /* the program has written, or moved on */

/* The next byte typed, in the PC's code page; or EV_. */
static int key(struct shsess *s)
{
    for (;;) {
        u32 f = irq_save(), c;
        while (s->ih == s->it && !s->closing && !s->resized && !(s->job_on && s->job_evt))
            thread_block(&s->wq);
        if (s->closing) {
            irq_restore(f);
            return EV_GONE;
        }
        if (s->job_on && s->job_evt) {
            s->job_evt = 0;
            irq_restore(f);
            return EV_JOB;
        }
        if (s->resized) {
            s->resized = 0;
            irq_restore(f);
            if (s->editing)
                ed_size(&s->ed, (int)s->cols);
            continue;
        }
        c = s->in[s->ih++ % IN_RING];
        irq_restore(f);
        if (s->utf_left) {                      /* more of a character */
            if ((c & 0xC0) == 0x80) {
                s->utf = s->utf << 6 | (c & 0x3F);
                if (--s->utf_left)
                    continue;
                c = (u32)cp_byte(s->page, s->utf);
                return (int)c < 0 ? '?' : (int)c;
            }
            s->utf_left = 0;                    /* (cut short: what came instead is taken as it is) */
        }
        if (c < 0x80)
            return (int)c;
        if ((c & 0xE0) == 0xC0)
            s->utf = c & 0x1F, s->utf_left = 1;
        else if ((c & 0xF0) == 0xE0)
            s->utf = c & 0x0F, s->utf_left = 2;
        else if ((c & 0xF8) == 0xF0)
            s->utf = c & 0x07, s->utf_left = 3;
    }
}

/* ---- the session's thread */

static void session(void *arg)
{
    struct shsess *s = arg;
    const char *env[SH_VARS + 1];
    char extra[3][72];
    void (*kick)(void) = s->kick;
    u32 n = 0, at = (u32)vm_rd16(((u32)vm.loader_psp << 4) + 0x2C) << 4;
    int c;
    while (at && n < SH_VARS && vm_rd8(at)) {   /* DOS's environment, as GLOS.EXE was given it */
        env[n++] = (const char *)vm_ptr(at);
        at += strlen((const char *)vm_ptr(at)) + 1;
    }
    {                                           /* ... and what GLOS gives a DOS program besides (vm/session.c): the
                                                   time zone, the code page, a Crystal chip's BLASTER */
        static const char *const names[] = { "TZ=", "GLOSCP=", "BLASTER=" };
        char value[64];
        u32 i, k, have;
        for (k = 0; k < 3 && n < SH_VARS; k++) {
            for (i = 0, have = 0; i < n; i++)
                have |= !strncmp(env[i], names[k], strlen(names[k]));
            value[0] = 0;
            if (k == 0 && kcfg_loaded())
                kcfg_get("time", "zone", value, sizeof value);
            else if (k == 1 && kcfg_loaded() && kcfg_get("region", "codepage", value, sizeof value) != 1)
                value[0] = 0;
            else if (k == 2 && crystal_blaster())
                sh_fmt(value, sizeof value, "%s", crystal_blaster());
            if (!have && value[0]) {
                sh_fmt(extra[k], sizeof extra[k], "%s%s", names[k], value);
                env[n++] = extra[k];
            }
        }
    }
    env[n] = 0;
    dos_hold();
    ed_init(&s->ed, ed_put, s, (int)s->cols);
    s->starting = 1;
    sh_start(&s->sh, &s->host, env);
    s->starting = 0;
    {                                           /* the folder DOS is in: the shell's to start with */
        char at_dos[80];
        req(s);
        call(s, 0, 0x1900, 0, 0, 0);
        call(s, 1, 0x4700, 0, 0, 0);
        s->q.r[1].si = DATA_AT;
        s->q.out = (u8 *)at_dos + 3, s->q.out_len = 64, s->q.out_at = DATA_AT;
        if (made(s) == 0) {
            at_dos[0] = (char)('A' + (s->q.r[0].ax & 0xFF)), at_dos[1] = ':', at_dos[2] = '\\';
            at_dos[67] = 0;
            sh_folder(&s->sh, at_dos);
        }
    }
    if (!s->tty) {                              /* the one line, to its end */
        sh_line(&s->sh, s->line);
        while (s->sh.under_way && !s->quit && key(s) != EV_GONE)
            if (s->job_on)
                job_turn(s);
        if (!s->quit)
            s->code = s->sh.errorlevel;
        s->quit = 1;
    } else {
        sh_prompt(&s->sh);
    }
    while (!s->quit && (c = key(s)) != EV_GONE) {
        if (s->job_on) {                        /* a program runs: what is typed is its; what it writes, ours */
            if (c >= 0)
                job_key(s, c);
            job_turn(s);
            continue;
        }
        if (c < 0)
            continue;
        switch (ed_key(&s->ed, (unsigned char)c)) {
        case ED_ENTER:
            s->editing = 0;
            sh_line(&s->sh, s->ed.line);
            break;
        case ED_UP: sh_history(&s->sh, -1); break;
        case ED_DOWN: sh_history(&s->sh, 1); break;
        case ED_TAB: sh_complete(&s->sh, s->ed.line); break;
        case ED_BREAK:
            s->editing = 0;
            sh_prompt(&s->sh);
            break;
        case ED_END:
            s->quit = 1;
            s->code = s->sh.errorlevel;
            break;
        }
    }
    if (s->job_on) {                            /* (the terminal went from under its program) */
        u32 f = irq_save();
        agent_app_kill(s, s->job);
        agent_app_forget(s, s->job);
        irq_restore(f);
    }
    dos_release();
    if (s->tty)
        kprintf("GLOS-SHELL end code=%d%s\n", s->code, s->closing ? " terminal=gone" : "");
    s->ended = 1;                               /* (its memory is its terminal's side's from here) */
    kick();
    thread_exit();
}

/* ---- the terminal's side */

static void release(struct shsess *s)
{
    if (s->line)
        kfree(s->line);
    kfree(s);
}

static struct shsess *make(u32 cols, u32 rows, const char *line, void (*kick)(void));

struct shsess *shsess_new(u32 cols, u32 rows, void (*kick)(void)) { return make(cols, rows, 0, kick); }

struct shsess *shsess_exec(const char *line, void (*kick)(void)) { return make(0, 0, line, kick); }

static struct shsess *make(u32 cols, u32 rows, const char *line, void (*kick)(void))
{
    static const struct sh_host host = { 0, h_write, h_clear, h_prompted, h_input, h_quit, h_dir_read, h_dir_free,
        h_is_dir, h_stat, h_open, h_read, h_put, h_close, h_mkdir, h_rmdir, h_unlink, h_rename, h_run, 0, 0, 0,
        h_alloc, h_free };
    struct shsess *s;
    if (!(vm.bi->flags & BI_F_AGENT) || !(s = kmalloc(sizeof *s)))
        return 0;
    memset(s, 0, sizeof *s);
    if (line) {
        if (strlen(line) >= SH_LINE || !(s->line = kmalloc((u32)strlen(line) + 1))) {
            kfree(s);
            return 0;
        }
        strcpy(s->line, line);
    }
    s->host = host;
    s->host.ctx = s;
    s->kick = kick;
    s->tty = cols != 0;
    s->cols = cols ? cols : 80, s->rows = rows ? rows : 24;
    s->page = kcfg_region()->codepage ? kcfg_region()->codepage : 437;
    if (!thread_create("shell", PRIO_NORMAL, session, s)) {
        release(s);
        return 0;
    }
    if (!line)
        kprintf("GLOS-SHELL begin terminal=%ux%u page=%d\n", cols, rows, s->page);
    return s;
}

u32 shsess_read(struct shsess *s, int stream, u8 *buf, u32 room)
{
    const u8 *ring = stream ? s->err : s->out;
    volatile u32 *head = stream ? &s->eh : &s->oh, *tail = stream ? &s->et : &s->ot;
    u32 size = stream ? ERR_RING : OUT_RING, f = irq_save(), n = *tail - *head, i;
    if (n > room)
        n = room;
    for (i = 0; i < n; i++)
        buf[i] = ring[(*head + i) % size];
    *head += n;
    if (n)
        thread_wake(&s->wq);
    irq_restore(f);
    return n;
}

u32 shsess_waiting(const struct shsess *s) { return (s->ot - s->oh) + (s->et - s->eh); }

u32 shsess_write(struct shsess *s, const u8 *d, u32 n)
{
    u32 f = irq_save(), room = IN_RING - (s->it - s->ih), i;
    if (n > room)
        n = room;
    for (i = 0; i < n; i++)
        s->in[(s->it + i) % IN_RING] = d[i];
    s->it += n;
    thread_wake(&s->wq);
    irq_restore(f);
    return n;
}

void shsess_resize(struct shsess *s, u32 cols, u32 rows)
{
    u32 f = irq_save();
    s->cols = cols, s->rows = rows;
    s->resized = 1;
    thread_wake(&s->wq);
    irq_restore(f);
}

int shsess_ended(const struct shsess *s, int *code)
{
    if (!s->ended)
        return 0;
    *code = s->code;
    return 1;
}

void shsess_free(struct shsess *s)
{
    u32 f = irq_save();
    if (s->ended) {
        irq_restore(f);
        release(s);
        return;
    }
    s->closing = 1;
    s->next = leaving;
    leaving = s;
    thread_wake(&s->wq);
    irq_restore(f);
}

void shsess_turn(void)
{
    struct shsess **p = &leaving, *s;
    while ((s = *p) != 0) {
        if (s->ended) {
            *p = s->next;
            release(s);
        } else {
            p = &s->next;
        }
    }
}
