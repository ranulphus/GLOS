/* The agent shell and output capture (PRD §7.2, §7.4; supervisor.md §17.3,
 * §17.4; M3 items 7 and 8).
 *
 * Headless (GLOS.EXE with no mode option, or /AGENT), the resident stub
 * asks the kernel what to run (NEXT); while there is nothing it halts until
 * an IRQ or a command wakes it, so DOS keeps its clock. An SSH exec that is
 * not a built-in "glos ..." command becomes a job, and jobs run one at a
 * time in the order they came:
 *   - a program (NAME, NAME.COM or NAME.EXE, in the current directory and
 *     then along PATH, as COMMAND.COM looks) is EXECed directly, so its
 *     exit code arrives exactly (MS-DOS's COMMAND /C drops it);
 *   - anything else (an internal command, a batch file, a redirection or a
 *     pipe, or no program found) runs as COMSPEC /C <command>.
 * The exit status is INT 21h 4Dh's code; 126 when not even COMSPEC ran.
 *
 * While a job runs, what its programs write to the console through DOS is
 * copied into the job's rings: INT 21h 02h, 06h and 09h (DOS writes them to
 * handle 1) and 40h, when the handle's file is the console device (its SFT
 * entry, not its number); handle 2 is stderr, the rest stdout. INT 29h
 * counts only outside DOS (InDOS clear): DOS's console driver calls it for
 * output already counted. Text written straight to the screen is not
 * captured; nothing reaches the program's stdin yet.
 *
 * The VM thread (trap context, interrupts off) starts a job, fills its rings
 * and ends it; the ssh thread posts it, empties the rings into the channel
 * and frees it. Each ring has one writer and one reader, and the ssh side
 * changes a job's state with interrupts off, so the VM never sees half a
 * change. A full ring holds the program (the VM thread waits) until the ssh
 * thread makes room.
 *
 * glos run [--direct] [--profile NAME] COMMAND (M4e) posts a job whose
 * session (supervisor.md §11) runs in direct mode or takes that profile.
 *
 * glos kill, or the client going away, kills a running job: every half
 * second the program in front is killed (the kill of supervisor.md §9.6),
 * innermost first, until the job has ended; its output is then dropped.
 *
 * A job whose program becomes a GLOS app (M6, kernel/app/app.c) stays that
 * app's until the app ends: its output while the VM thread makes its DOS
 * calls, its exit code, its kill. The stub is free meanwhile, and the next
 * job runs. */
#include <string.h>

#include "kapi.h"
#include "glos/bootinfo.h"
#include "agent.h"
#include "app.h"
#include "dos.h"
#include "session.h"
#include "io.h"
#include "kcfg.h"
#include "gdbprog.h"
#include "kprintf.h"
#include "sched.h"
#include "timer.h"
#include "vm.h"
#include "watch.h"

#define NJOBS    16                             /* (the ssh's and the apps': a launched app's lasts as long as it) */
#define RING     0x4000u                        /* a power of two */
#define JOB_CMD  1024
#define JOB_ENV  1536                           /* (a job's own strings, 511 at most, and CMDLINE for a long line) */
#define IN_RING  0x1000u                        /* a job's standard input from its app: a power of two */
#define SFT_SIZE 0x3B                           /* an SFT entry, DOS 4 and later */

enum { J_FREE, J_QUEUED, J_RUNNING, J_DONE };

struct ring {
    u8 buf[RING];
    volatile u32 head, tail;
};

struct agent_job {
    volatile u8 state;
    u8 cand;                                    /* the next program to try */
    u8 shell;                                   /* running as COMSPEC /C */
    u8 direct;                                  /* glos run --direct */
    u8 exclusive;                               /* glos run: 1 --exclusive, 2 --app */
    u8 app;                                     /* its program runs on as a GLOS app */
    char profile[16];                           /* glos run --profile NAME, or empty */
    volatile u8 kill;                           /* end it: glos kill, or its client has gone */
    u32 seq;
    u32 result;                                 /* INT 21h 4Dh's AX, or 10000h + the EXEC error */
    void *owner;                                /* the ssh side's channel; NULL once it has gone */
    char cmd[JOB_CMD];                          /* (a program's path, up to 79, and its arguments: past DOS's 126 for
                                                   an app's job, which are then given in CMDLINE too) */
    char prog[80];                              /* the command's first word */
    const char *args;                           /* the rest, from the separator on */
    char path[80];                              /* what the stub is EXECing */
    struct ring out[2];                         /* stdout, stderr */
    /* M9: an app's job (GLOS_FN_RUN) */
    u8 from_app;                                /* its output is its app's to read, its keys the app's to type */
    u8 console;                                 /* KRUN_CONSOLE: its program writes into the app's window */
    u8 full;                                    /* ... until it wanted the screen (or KRUN_FULL) */
    u8 detach;                                  /* KRUN_DETACH: nobody's; no output kept */
    u8 dir_step;                                /* starting in dir: 1 its drive set, 2 its folder, 0 done */
    u8 told;                                    /* the GLOS_JOB_ state its app last heard of, FFh none */
    void *app_owner;                            /* its app (struct app), or NULL */
    char dir[80];
    char env[JOB_ENV];                          /* NAME=VALUE strings, a NUL after each and one more: its own */
    u16 keys[64];                               /* typed by its app, on their way into the BIOS's buffer */
    volatile u32 kh, kt;
    u16 left[64];                               /* typed, but not read by the time it ended: its app's again */
    u8 nleft;
    struct waitq readq;                         /* its app, waiting for output */
    /* API 2.0's JOB (kernel/app/api2job.c) */
    u8 keep;                                    /* its slot is its app's until the app lets it go (STATUS after its end) */
    u8 debug;                                   /* it stops at its start for its app's debugger (gdbprog.c) */
    u8 hidden;                                  /* a console job with no window: it runs beside the desktop as one in
                                                   a window does, but what it writes through DOS is kept by stream,
                                                   and nobody types to it */
    u8 in_pipe;                                 /* its standard input is what its app writes ... */
    volatile u8 in_eof;                         /* ... and the app has written its last */
    u8 in[IN_RING];
    volatile u32 ih, it;
    char file[3][80];                           /* files for its standard input, output and error ("": none) */
    u8 fappend;                                 /* bit s: stream s's file is added to, not made anew */
    u8 fstep, fs;                               /* FS_: where the opening (or closing) of them is; at which stream */
    u8 fset;                                    /* bit s: the stub's handle s is the file's for now ... */
    u8 fold[3];                                 /* ... and was this entry of the stub's handle table */
    u16 fh[3];                                  /* the handles DOS gave for the files (FFFFh: another stream's) */
    u32 t_start, t_end;                         /* the ticks it began to run and ended at */
};

static struct agent_job jobs[NJOBS];
static struct agent_job *running;
static u32 next_seq;
static struct waitq roomq;                      /* the VM thread, waiting for ring space */
static struct agent_job *closer;                /* the job whose streams' files are being closed */
static u16 ans_ax;                              /* what DOS answered the call made last for a job ... */
static u8 ans_cf;                               /* ... and whether it refused */
enum { FS_NONE, FS_OPEN, FS_CREATE, FS_SEEK, FS_CLOSE };
static u32 last_kill;
static u8 sig_left, sig_brk;                    /* a job's Ctrl+C or Ctrl+Break on its way: the keys still to go */
static u32 sig_at;
static volatile u32 exit_at;
static volatile u8 exit_req, exit_code;
static void (*kick_ssh)(void);

static u32 ring_used(const struct ring *r) { return r->head - r->tail; }

static char upper(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 32) : c; }

/* ---- the ssh thread's side */

void agent_init(void (*kick)(void)) { kick_ssh = kick; }

int agent_post(void *owner, const char *cmd) { return agent_post_run(owner, cmd, 0, 0, ""); }

int agent_post_run(void *owner, const char *cmd, int direct, int exclusive, const char *profile)
{
    struct agent_job *j = NULL;
    u32 f, i;
    for (i = 0; i < NJOBS; i++)
        if (jobs[i].state == J_FREE) {
            j = &jobs[i];
            break;
        }
    if (!j)
        return -1;
    while (*cmd == ' ' || *cmd == '\t')
        cmd++;
    for (i = 0; cmd[i] && i < sizeof j->cmd - 1; i++)
        j->cmd[i] = cmd[i];
    j->cmd[i] = 0;
    j->owner = owner;
    j->result = 0;
    j->cand = 0;
    j->shell = 0;
    j->kill = 0;
    j->app = 0;
    j->direct = (u8)(direct != 0);
    j->exclusive = (u8)exclusive;
    for (i = 0; profile[i] && i < sizeof j->profile - 1; i++)
        j->profile[i] = profile[i];
    j->profile[i] = 0;
    j->out[0].head = j->out[0].tail = j->out[1].head = j->out[1].tail = 0;
    j->from_app = j->console = j->full = j->detach = j->dir_step = 0;
    j->told = 0xFF;
    j->app_owner = NULL;
    j->dir[0] = 0;
    j->env[0] = j->env[1] = 0;
    j->kh = j->kt = 0;
    j->nleft = 0;
    j->keep = j->in_pipe = j->in_eof = j->hidden = j->debug = 0;
    j->file[0][0] = j->file[1][0] = j->file[2][0] = 0;
    j->fappend = j->fstep = j->fs = j->fset = 0;
    j->ih = j->it = 0;
    j->t_start = j->t_end = 0;
    f = irq_save();
    j->seq = next_seq++;
    j->state = J_QUEUED;
    vm_kick();                                  /* the stub may be halted, waiting */
    irq_restore(f);
    return 0;
}

/* ---- apps' jobs (GLOS_FN_RUN, GLOS_FN_JOB; M9 step C), on the app's thread */

static u32 app_state(const struct agent_job *j)
{
    if (j->state == J_QUEUED)
        return GLOS_JS_QUEUED;
    if (j->state == J_DONE)
        return GLOS_JS_DONE;
    return j->app ? GLOS_JS_APP : j->full || !j->console ? GLOS_JS_FULL : GLOS_JS_RUNNING;
}

static u32 code_of(const struct agent_job *j) { return (j->result & 0x10000) ? 126 : (j->result & 0xFF); }

/* Its app told it has moved on (interrupts off or on: app_event is safe in either). */
static void tell(struct agent_job *j)
{
    struct kevent e;
    u32 st = app_state(j);
    thread_wake(&j->readq);
    if (!j->app_owner || st == j->told)
        return;
    j->told = (u8)st;
    memset(&e, 0, sizeof e);
    e.type = KEV_JOB;
    e.key = j->seq;
    e.x = (int)st;
    e.y = (int)code_of(j);
    app_event(j->app_owner, &e);
}

int agent_post_app(void *app, const struct krun *r, u32 *seq) { return agent_post_job(app, r, 0, seq); }

/* ... and the API's (o: what struct krun has no word for; NULL: none of it). */
int agent_post_job(void *app, const struct krun *r, const struct agent_opts *o, u32 *seq)
{
    struct agent_job *j = NULL;
    u32 f, i;
    const char *cmd = o && o->command ? o->command : r->command;
    for (i = 0; i < NJOBS; i++)
        if (jobs[i].state == J_FREE) {
            j = &jobs[i];
            break;
        }
    if (!j)
        return GLOS_E_MEMORY;
    while (*cmd == ' ' || *cmd == '\t')
        cmd++;
    if (!*cmd)
        return GLOS_E_ARGUMENT;
    for (i = 0; cmd[i] && i < sizeof j->cmd - 1; i++)
        j->cmd[i] = cmd[i];
    j->cmd[i] = 0;
    for (i = 0; r->dir[i] && i < sizeof j->dir - 1; i++)
        j->dir[i] = r->dir[i];
    j->dir[i] = 0;
    for (i = 0; i < sizeof j->env - 2 && (r->env[i] || (i && r->env[i - 1])); i++)
        j->env[i] = r->env[i];                  /* (to its double NUL) */
    j->env[i] = 0;
    j->env[i + 1 < sizeof j->env ? i + 1 : i] = 0;
    j->owner = NULL;
    j->result = 0;
    j->cand = j->shell = 0;
    j->kill = 0;
    j->app = 0;
    j->direct = (u8)(o && o->direct);
    j->exclusive = (u8)(o ? o->exclusive : 0);
    for (i = 0; o && o->profile && o->profile[i] && i < sizeof j->profile - 1; i++)
        j->profile[i] = o->profile[i];
    j->profile[i] = 0;
    j->keep = (u8)(o && o->keep);
    j->fappend = (u8)(o ? o->append : 0);
    j->fstep = j->fs = j->fset = 0;
    for (i = 0; i < 3; i++) {
        u32 k = 0;
        for (; o && o->file[i] && o->file[i][k] && k < sizeof j->file[i] - 1; k++)
            j->file[i][k] = o->file[i][k];
        j->file[i][k] = 0;
    }
    j->hidden = (u8)(o && o->hidden);
    j->debug = (u8)(o && o->debug);
    j->in_pipe = (u8)(o && o->stdin_pipe);
    j->in_eof = 0;
    j->ih = j->it = 0;
    j->t_start = j->t_end = 0;
    j->out[0].head = j->out[0].tail = j->out[1].head = j->out[1].tail = 0;
    j->detach = (u8)((r->flags & KRUN_DETACH) != 0);
    j->from_app = (u8)!j->detach;
    j->console = (u8)(!j->detach && (r->flags & KRUN_CONSOLE));
    j->full = (u8)(j->console && (r->flags & KRUN_FULL));
    j->dir_step = 0;
    j->told = 0xFF;
    j->app_owner = j->detach ? NULL : app;
    j->kh = j->kt = 0;
    j->nleft = 0;
    f = irq_save();
    j->seq = next_seq++;
    j->state = J_QUEUED;
    *seq = j->seq;
    vm_kick();
    irq_restore(f);
    kprintf("GLOS-AGENT post seq=%u app=%u%s%s%s cmd=\"%s\"\n", j->seq, app ? app_id(app) : 0, j->console ? " console=1" : "",
            j->full ? " full=1" : "", j->detach ? " detach=1" : "", j->cmd);
    return 0;
}

/* A job of nobody's (the kernel's: the desktop's start list): run, its output not kept. */
int agent_post_detached(const char *cmd, const char *dir)
{
    static struct krun r;                   /* (the VM thread's alone: no other caller) */
    u32 seq, i;
    memset(&r, 0, sizeof r);
    for (i = 0; cmd[i] && i < sizeof r.command - 1; i++)
        r.command[i] = cmd[i];
    for (i = 0; dir[i] && i < sizeof r.dir - 1; i++)
        r.dir[i] = dir[i];
    r.flags = KRUN_DETACH;
    return agent_post_app(0, &r, &seq);
}

static struct agent_job *app_job(void *app, u32 seq)
{
    u32 i;
    for (i = 0; i < NJOBS; i++)
        if (jobs[i].state != J_FREE && jobs[i].seq == seq && jobs[i].app_owner == app && app)
            return &jobs[i];
    return NULL;
}

/* The BIOS's keyboard buffer (40:1A head, 40:1C tail, 40:80 and 40:82 its bounds): a console job's typed keys go
   in while there is room (interrupts off: the VM thread's tick and the app's call both feed it). */
static void feed(struct agent_job *j)
{
    u32 f = irq_save();
    while (j->kh != j->kt && j->state == J_RUNNING && j->console && !j->full) {
        u16 head = vm_rd16(0x41A), tail = vm_rd16(0x41C), start = vm_rd16(0x480), end = vm_rd16(0x482), next;
        if (start < 0x1E || end > 0x100 || start >= end)
            start = 0x1E, end = 0x3E;
        next = (u16)(tail + 2);
        if (next >= end)
            next = start;
        if (next == head)
            break;                              /* full: the next tick */
        vm_wr8(0x400u + tail, (u8)j->keys[j->kh % 64]);
        vm_wr8(0x400u + tail + 1, (u8)(j->keys[j->kh % 64] >> 8));
        vm_wr8(0x41C, (u8)next);
        vm_wr8(0x41D, (u8)(next >> 8));
        j->kh++;
    }
    irq_restore(f);
}

/* A console job has ended (the VM thread): what is left in the BIOS's buffer (typed in its window, or on the
   keyboard while it had the screen), then its app's keys not yet fed, are its app's again; the buffer is
   emptied. */
static void keys_left(struct agent_job *j)
{
    u32 f = irq_save();
    u16 head = vm_rd16(0x41A), tail = vm_rd16(0x41C), start = vm_rd16(0x480), end = vm_rd16(0x482);
    if (start < 0x1E || end > 0x100 || start >= end)
        start = 0x1E, end = 0x3E;
    j->nleft = 0;
    while (head != tail && j->nleft < 64) {
        j->left[j->nleft++] = vm_rd16(0x400u + head);
        head = (u16)(head + 2);
        if (head >= end)
            head = start;
    }
    vm_wr8(0x41A, (u8)tail);
    vm_wr8(0x41B, (u8)(tail >> 8));
    while (j->kh != j->kt && j->nleft < 64)
        j->left[j->nleft++] = j->keys[j->kh++ % 64];
    j->kh = j->kt;
    irq_restore(f);
}

/* ---- API 2.0's JOB (kernel/app/api2job.c), on the app's thread */

/* What it has written to one stream, as far as there is room: the bytes, or -1 (no such job of the app's). */
int agent_app_read1(void *app, u32 seq, int stream, u8 *out, u32 room)
{
    struct agent_job *j = app_job(app, seq);
    struct ring *r;
    u32 n = 0, f;
    if (!j || stream < 0 || stream > 1)
        return -1;
    r = &j->out[stream];
    while (ring_used(r) && n < room) {
        out[n++] = r->buf[r->tail & (RING - 1)];
        __asm__ volatile("" ::: "memory");
        r->tail++;
    }
    if (n) {
        f = irq_save();
        thread_wake(&roomq);                    /* (the VM thread may be waiting for the room) */
        irq_restore(f);
    }
    return (int)n;
}

/* Its standard input: n bytes more (those taken are the answer; fewer when it is full), or its end (eof). -1: no
   such job, or its input is not the app's. */
int agent_app_write(void *app, u32 seq, const u8 *d, u32 n, int eof)
{
    struct agent_job *j = app_job(app, seq);
    u32 k = 0, f;
    if (!j || !j->in_pipe)
        return -1;
    while (k < n && j->it - j->ih < IN_RING && !j->in_eof) {
        j->in[j->it & (IN_RING - 1)] = d[k++];
        __asm__ volatile("" ::: "memory");
        j->it++;
    }
    if (eof)
        j->in_eof = 1;
    f = irq_save();
    vm_kick();                                  /* (its program may be waiting to read) */
    irq_restore(f);
    return (int)k;
}

int agent_app_status(void *app, u32 seq, u32 *state, u32 *code, u32 *nout, u32 *nerr, u32 *ticks)
{
    struct agent_job *j = app_job(app, seq);
    if (!j)
        return -1;
    *state = app_state(j);
    *code = code_of(j);
    *nout = ring_used(&j->out[0]);
    *nerr = ring_used(&j->out[1]);
    *ticks = !j->t_start ? 0 : (j->t_end ? j->t_end : timer_ticks()) - j->t_start;
    return 0;
}

/* Ctrl+C (brk 0) or Ctrl+Break for the job's program, as from the keyboard: the keys go to the VM's keyboard
   whatever has the real one. 0; -1: no such job; -2: it is not the one running, or it is an app now. */
int agent_app_signal(void *app, u32 seq, int brk)
{
    struct agent_job *j = app_job(app, seq);
    u32 f;
    if (!j)
        return -1;
    if (j != running || j->app)
        return -2;
    f = irq_save();
    if (sig_left) {                             /* (one is on its way still) */
        irq_restore(f);
        return -2;
    }
    sig_brk = (u8)(brk != 0);
    sig_left = 4;                               /* Ctrl down, the key down and up, Ctrl up: a key every 20 ms, as */
    sig_at = timer_ticks();                     /* fingers would (agent_vm_tick). All at once, a slow PC's BIOS */
    irq_restore(f);                             /* missed Ctrl: the program's own handler had read it, and the next */
    kprintf("GLOS-AGENT signal seq=%u %s\n", seq, brk ? "break" : "int");     /* key was there before the BIOS looked */
    return 0;
}

int agent_app_type(void *app, u32 seq, const u16 *keys, u32 n)
{
    struct agent_job *j = app_job(app, seq);
    u32 i;
    if (!j || !j->console || j->hidden)
        return -1;                              /* (only a job in a window of its app's is typed to) */
    if (j->state == J_DONE) {                   /* (typed after its end, before its app has seen that: type-ahead) */
        u32 f = irq_save();
        for (i = 0; i < n && j->nleft < 64; i++)
            j->left[j->nleft++] = keys[i];
        irq_restore(f);
        return (int)i;
    }
    for (i = 0; i < n && j->kt - j->kh < 64; i++)
        j->keys[j->kt++ % 64] = keys[i];
    feed(j);
    vm_kick();
    return (int)i;
}

/* A console job that has ended: the keys typed into it that it didn't read (type-ahead, for its app's own prompt
   or next job, as DOS's BIOS buffer keeps them): their number. The job is gone after, if its output is too. */
int agent_app_keys(void *app, u32 seq, u16 *out, u32 room)
{
    struct agent_job *j = app_job(app, seq);
    u32 n, f;
    if (!j)
        return -1;
    if (j->state != J_DONE)
        return 0;
    f = irq_save();
    n = j->nleft < room ? j->nleft : room;
    memcpy(out, j->left, n * sizeof *out);
    j->nleft = 0;                               /* (the job stays the app's until its handle is closed) */
    irq_restore(f);
    return (int)n;
}

int agent_app_kill(void *app, u32 seq)
{
    struct agent_job *j = app_job(app, seq);
    return j ? agent_kill_seq(seq) : -1;
}

int agent_app_forget(void *app, u32 seq)
{
    struct agent_job *j = app_job(app, seq);
    u32 f;
    if (!j)
        return -1;
    f = irq_save();
    j->app_owner = NULL;
    j->detach = 1;
    if (j->debug)
        gdbp_forget_job(j->seq);                /* (stopped for a debugger that never came: it runs on) */
    if (j->state == J_DONE || j->state == J_QUEUED)
        j->state = J_FREE;
    irq_restore(f);
    return 0;
}

void agent_app_gone(void *app)
{
    u32 i;
    for (i = 0; i < NJOBS; i++) {
        struct agent_job *j = &jobs[i];
        if (j->state == J_FREE || j->app_owner != app || !app)
            continue;
        if (j->state == J_RUNNING && !j->app)
            agent_kill_seq(j->seq);             /* (its console's window has gone) */
        {
            u32 f = irq_save();
            j->app_owner = NULL;
            j->detach = 1;
            if (j->state == J_DONE || j->state == J_QUEUED)
                j->state = J_FREE;
            irq_restore(f);
        }
    }
}

u32 agent_job_seq(const void *job) { return ((const struct agent_job *)job)->seq; }

u32 agent_job_by(u32 seq)                       /* the app that started a job (its id), or 0 */
{
    u32 i, f = irq_save(), id = 0;
    for (i = 0; i < NJOBS; i++)
        if (jobs[i].state != J_FREE && jobs[i].seq == seq && jobs[i].app_owner)
            id = app_id(jobs[i].app_owner);
    irq_restore(f);
    return id;
}

u32 agent_jobs_stat(void *out, u32 room)
{
    u32 n = 0, i;
    for (i = 0; i < NJOBS && n + sizeof(struct kprog) <= room; i++) {
        const struct agent_job *j = &jobs[i];
        struct kprog *a = (struct kprog *)((u8 *)out + n);
        u32 k;
        if (j->state != J_QUEUED && j->state != J_RUNNING)
            continue;
        memset(a, 0, sizeof *a);
        a->size = sizeof *a;
        a->kind = 1;
        a->id = j->seq;
        a->state = app_state(j);
        for (k = 0; j->cmd[k] && j->cmd[k] != ' ' && k < sizeof a->name - 1; k++)
            a->name[k] = j->cmd[k];
        n += sizeof *a;
    }
    return n;
}

void agent_drop(void *owner)
{
    u32 f = irq_save(), i;
    for (i = 0; i < NJOBS; i++)
        if (jobs[i].state != J_FREE && jobs[i].owner == owner) {
            jobs[i].owner = NULL;
            if (jobs[i].state == J_QUEUED) {
                jobs[i].state = J_FREE;
            } else {
                jobs[i].kill = 1;
                if (jobs[i].app)
                    app_kill_job(&jobs[i]);
            }
        }
    thread_wake(&roomq);
    irq_restore(f);
}

void agent_service(u32 (*write)(void *owner, int stream, const u8 *d, u32 n), void (*done)(void *owner, u32 code))
{
    u32 i, s, f;
    for (i = 0; i < NJOBS; i++) {
        struct agent_job *j = &jobs[i];
        int moved = 0;
        if ((j->state != J_RUNNING && j->state != J_DONE) || j->from_app || j->detach)
            continue;
        for (s = 0; s < 2; s++) {
            struct ring *r = &j->out[s];
            while (ring_used(r)) {
                u32 t = r->tail & (RING - 1), n = ring_used(r), k;
                if (n > RING - t)
                    n = RING - t;
                k = j->owner ? write(j->owner, (int)s, r->buf + t, n) : n;
                if (!k)
                    break;
                __asm__ volatile("" ::: "memory");
                r->tail += k;
                moved = 1;
            }
        }
        if (moved) {
            f = irq_save();
            thread_wake(&roomq);
            irq_restore(f);
        }
        if (j->state == J_DONE && !ring_used(&j->out[0]) && !ring_used(&j->out[1])) {
            if (j->owner)
                done(j->owner, (j->result & 0x10000) ? 126 : (j->result & 0xFF));
            f = irq_save();
            j->owner = NULL;
            j->state = J_FREE;
            irq_restore(f);
        }
    }
}

int agent_kill(void)
{
    u32 f = irq_save();
    int r = running ? 0 : -1;
    if (running)
        running->kill = 1;
    irq_restore(f);
    return r;
}

/* glos kill SEQ: the running job killed, a queued one taken out (its client
   gets status 255); -1 if there is no such job. */
int agent_kill_seq(u32 seq)
{
    u32 f = irq_save(), i;
    int r = -1;
    for (i = 0; i < NJOBS; i++) {
        struct agent_job *j = &jobs[i];
        if (j->seq != seq || (j->state != J_QUEUED && j->state != J_RUNNING))
            continue;
        if (j->state == J_RUNNING) {
            j->kill = 1;
            if (j->app)
                app_kill_job(j);
        } else {
            j->result = 0xFF;
            j->state = J_DONE;                  /* (agent_service tells its client) */
        }
        r = 0;
    }
    irq_restore(f);
    return r;
}

/* A job runs or waits. */
int agent_busy(void)
{
    u32 i;
    for (i = 0; i < NJOBS; i++)
        if (jobs[i].state == J_QUEUED || jobs[i].state == J_RUNNING)
            return 1;
    return 0;
}

u32 agent_ps(char *buf, u32 max)
{
    static const char *const names[] = { "free", "queued", "running", "done" };
    u32 n = 0, i, f = irq_save();
    for (i = 0; i < NJOBS && n < max; i++)
        if (jobs[i].state == J_QUEUED || jobs[i].state == J_RUNNING)
            n += (u32)ksnprintf(buf + n, max - n, "job %u %s%s%s %s\n", jobs[i].seq, names[jobs[i].state],
                                jobs[i].app ? " app" : "", jobs[i].kill ? " killing" : "", jobs[i].cmd);
    irq_restore(f);
    return n < max ? n : max;
}

void agent_exit(u32 delay_ticks, u8 code)
{
    u32 f = irq_save();
    exit_at = timer_ticks() + delay_ticks;
    exit_code = code;
    exit_req = 1;
    vm_kick();
    irq_restore(f);
}

/* glos exit refused (the stub had nowhere to go, docs/upper-memory.md): GLOS
   stays, and a later glos exit asks again. */
void agent_exit_cancel(void) { exit_req = 0; }

u8 agent_exit_code(void) { return exit_code; }

/* ---- the VM thread's side (trap context) */

static const char *const internal[] = {
    "BREAK", "CALL", "CD", "CHCP", "CHDIR", "CLS", "COPY", "CTTY", "DATE", "DEL", "DIR", "ECHO", "ERASE",
    "EXIT", "FOR", "GOTO", "IF", "LH", "LOADHIGH", "MD", "MKDIR", "PATH", "PAUSE", "PROMPT", "RD", "REM",
    "REN", "RENAME", "RMDIR", "SET", "SHIFT", "TIME", "TRUENAME", "TYPE", "VER", "VERIFY", "VOL", 0
};

/* Split the command; 1 if it goes straight to COMSPEC. */
static int split(struct agent_job *j)
{
    const char *c = j->cmd, *ext = 0;
    u32 n = 0, i;
    for (i = 0; c[i]; i++)
        if (c[i] == '<' || c[i] == '>' || c[i] == '|')
            return 1;
    while (*c && *c != ' ' && *c != '\t' && *c != '/' && *c != ',' && *c != ';' && *c != '='
           && n < sizeof j->prog - 6) {
        if (*c == '.')
            ext = &j->prog[n];
        else if (*c == '\\' || *c == ':')
            ext = 0;
        j->prog[n++] = upper(*c++);
    }
    j->prog[n] = 0;
    j->args = c;
    if (!n || (n == 2 && j->prog[1] == ':'))
        return 1;                               /* nothing, or a drive change */
    if (ext && ((ext[1] != 'C' || ext[2] != 'O' || ext[3] != 'M') && (ext[1] != 'E' || ext[2] != 'X' || ext[3] != 'E')))
        return 1;                               /* .BAT, or something COMMAND.COM must judge */
    if (ext && ext[4])
        return 1;
    for (i = 0; internal[i]; i++) {
        const char *a = internal[i], *b = j->prog;
        while (*a && *a == *b)
            a++, b++;
        if (!*a && !*b)
            return 1;
    }
    return 0;
}

/* PATH's entry k from the master environment (GLOS.EXE's), or 0. */
static int path_entry(u32 k, char *out, u32 max)
{
    u32 env = (u32)vm_rd16(((u32)vm.loader_psp << 4) + 0x2C) << 4, i = 0, n;
    if (!env)
        return 0;
    while (i < 32768 && vm_rd8(env + i)) {
        if (upper((char)vm_rd8(env + i)) == 'P' && upper((char)vm_rd8(env + i + 1)) == 'A'
            && upper((char)vm_rd8(env + i + 2)) == 'T' && upper((char)vm_rd8(env + i + 3)) == 'H'
            && vm_rd8(env + i + 4) == '=') {
            i += 5;
            while (k && vm_rd8(env + i))
                if (vm_rd8(env + i++) == ';')
                    k--;
            for (n = 0; vm_rd8(env + i) && vm_rd8(env + i) != ';' && n < max - 1; i++)
                out[n++] = (char)vm_rd8(env + i);
            out[n] = 0;
            return n || vm_rd8(env + i) == ';';
        }
        while (vm_rd8(env + i))
            i++;
        i++;
    }
    return 0;
}

/* The k-th place to look for the program: here, then each PATH directory;
   with no extension given, .COM before .EXE. 0 when there are no more. */
static int candidate(struct agent_job *j, u32 k)
{
    char dir[64];
    const char *p;
    int has_ext = 0, has_dir = 0, exts;
    u32 n = 0, d, i;
    for (p = j->prog; *p; p++) {
        if (*p == '.') has_ext = 1;
        if (*p == '\\' || *p == ':') has_dir = 1, has_ext = 0;
    }
    exts = has_ext ? 1 : 2;
    d = k / (u32)exts;
    dir[0] = 0;
    if (d > 0 && (has_dir || !path_entry(d - 1, dir, sizeof dir)))
        return 0;
    for (i = 0; dir[i] && n < sizeof j->path - 2; i++)
        j->path[n++] = dir[i];
    if (n && j->path[n - 1] != '\\' && j->path[n - 1] != ':')
        j->path[n++] = '\\';
    for (p = j->prog; *p && n < sizeof j->path - 5; p++)
        j->path[n++] = *p;
    if (!has_ext) {
        p = k % 2 ? ".EXE" : ".COM";
        while (*p)
            j->path[n++] = *p++;
    }
    j->path[n] = 0;
    return 1;
}

/* The DOS call that sets a job's drive (1) or folder (2) before its EXEC. */
/* ---- files for a job's standard input, output and error (API 2.0's JOB)
 *
 * Each is opened by a DOS call the stub makes before the job's EXEC (after its drive and folder are set, so a
 * relative path is the job's own), and the stub's handle 0, 1 or 2 is pointed at it: the entry of the stub's
 * handle table (in its PSP) is given the file's. DOS gives the program the stub's handles, so it reads and writes
 * the file; nothing of it is the console's, so nothing is kept for READ. After the job the entries are put back
 * and the files closed, a call each. The same file for output and error is opened once: both write through one
 * place in it. */
static u32 jft_lin(void)
{
    u32 p = (u32)vm.loader_psp << 4;
    u32 fp = vm_rd32(p + 0x34);
    return ((fp >> 16) << 4) + (fp & 0xFFFF);
}

static int same_file(const char *a, const char *b)
{
    while (*a && upper(*a) == upper(*b))
        a++, b++;
    return !*a && !*b;
}

/* The next call to make for its files: 3 and the call in x, or 0 when all are open. */
static int file_call(struct agent_job *j, struct agent_exec *x)
{
    while (j->fs < 3) {
        if (!j->file[j->fs][0]) {
            j->fs++;
            continue;
        }
        if (j->fs == 2 && (j->fset & 2) && same_file(j->file[2], j->file[1])) {     /* (one file for both) */
            u32 t = jft_lin();
            j->fold[2] = vm_rd8(t + 2);
            vm_wr8(t + 2, vm_rd8(t + 1));
            j->fh[2] = 0xFFFF;
            j->fset |= 4;
            j->fs++;
            continue;
        }
        break;
    }
    if (j->fs >= 3) {
        j->fstep = FS_NONE;
        return 0;
    }
    memset(&x->dos, 0, sizeof x->dos);
    x->dos_path = j->file[j->fs];
    if (j->fstep == FS_SEEK) {                  /* (to its end: what is written is added) */
        x->dos.ax = 0x4202;
        x->dos.bx = j->fh[j->fs];
        x->dos_path = NULL;
    } else if (j->fs == 0) {
        j->fstep = FS_OPEN;
        x->dos.ax = 0x3D00;                     /* its input: read */
    } else if (j->fstep != FS_CREATE && (j->fappend >> j->fs & 1)) {
        j->fstep = FS_OPEN;
        x->dos.ax = 0x3D01;                     /* added to: opened to write, if it is there */
    } else {
        j->fstep = FS_CREATE;
        x->dos.ax = 0x3C00;                     /* made, or emptied */
    }
    return 3;
}

/* DOS's answer to that call: 0 to go on, -1 if the file can't be had. */
static int file_answer(struct agent_job *j, u16 ax, int refused)
{
    u32 s = j->fs, t = jft_lin();
    if (j->fstep == FS_SEEK) {
        j->fs++;
        j->fstep = FS_OPEN;
        return 0;
    }
    if (refused) {
        if (j->fstep == FS_OPEN && s > 0) {     /* nothing there to add to: made */
            j->fstep = FS_CREATE;
            return 0;
        }
        kprintf("GLOS-AGENT file seq=%u stream=%u error=%u path=%s\n", j->seq, s, ax, j->file[s]);
        return -1;
    }
    if (ax >= vm_rd16(((u32)vm.loader_psp << 4) + 0x32))
        return -1;
    j->fh[s] = ax;
    j->fold[s] = vm_rd8(t + s);
    vm_wr8(t + s, vm_rd8(t + ax));              /* the stub's handle s is the file from now */
    j->fset |= (u8)(1u << s);
    if (j->fstep == FS_OPEN && s > 0) {
        j->fstep = FS_SEEK;
        return 0;
    }
    j->fs++;
    j->fstep = FS_OPEN;
    return 0;
}

/* After the job: the stub's handles as they were, and the files closed: 3 and a close in x, or 0 when done. */
static int files_close(struct agent_job *j, struct agent_exec *x)
{
    u32 s;
    for (s = 0; s < 3; s++)
        if (j->fset >> s & 1) {
            vm_wr8(jft_lin() + s, j->fold[s]);
            j->fset &= (u8)~(1u << s);
            if (j->fh[s] == 0xFFFF)
                continue;
            memset(&x->dos, 0, sizeof x->dos);
            x->dos_path = NULL;
            x->dos.ax = 0x3E00;
            x->dos.bx = j->fh[s];
            j->fstep = FS_CLOSE;
            return 3;
        }
    j->fstep = FS_NONE;
    return 0;
}

/* A command line past the 126 characters DOS's EXEC passes (an app's job: the IDE's compiler): the whole line
   in the environment's CMDLINE too, where a DJGPP program looks when its tail's length says 127 (Windows 9x's
   way). The program's path first, as it is being run. */
static void long_line(struct agent_job *j)
{
    u32 n = 0, k, need;
    if (strlen(j->args) <= 126)
        return;
    while (j->env[n])                           /* past its own strings, and a CMDLINE of an earlier try */
        n += (u32)strlen(j->env + n) + 1;
    for (k = 0; k < n; k += (u32)strlen(j->env + k) + 1)
        if (!strncmp(j->env + k, "CMDLINE=", 8)) {
            n = k;
            break;
        }
    need = 8 + (u32)strlen(j->path) + (u32)strlen(j->args) + 2;
    if (n + need > sizeof j->env) {
        kprintf("GLOS-WARN agent-cmdline seq=%u why=too-long\n", j->seq);
        j->env[n] = 0;
        return;
    }
    n += (u32)ksnprintf(j->env + n, sizeof j->env - n, "CMDLINE=%s%s", j->path, j->args) + 1;
    j->env[n] = 0;
}

void agent_vm_answer(u16 ax, int refused)
{
    ans_ax = ax;
    ans_cf = (u8)(refused != 0);
}

/* A job has ended (its files closed): its app and the SSH side are told. */
static void finish(struct agent_job *j)
{
    u32 result = j->result;
    if (j->console)
        keys_left(j);
    j->t_end = timer_ticks();
    j->state = J_DONE;
    kprintf("GLOS-AGENT done seq=%u code=%u via=%s%s\n", j->seq, result & 0xFF, j->shell ? "comspec" : j->path,
            (result & 0x10000) ? " cannot-run" : "");
    if (running == j)
        running = NULL;
    if (j->debug) {
        u32 f = irq_save();
        gdbp_job_end(j->seq, result & 0xFF);    /* (its debugger hears how it ended) */
        irq_restore(f);
    }
    tell(j);
    if (j->detach)
        j->state = J_FREE;                      /* (nobody to tell) */
    if (kick_ssh)
        kick_ssh();
}

static int dir_call(struct agent_job *j, struct agent_exec *x)
{
    memset(&x->dos, 0, sizeof x->dos);
    x->dos_path = NULL;
    if (j->dir_step == 1) {
        char d = upper(j->dir[0]);
        if (j->dir[1] != ':' || d < 'A' || d > 'Z') {
            j->dir_step = 2;
            return dir_call(j, x);
        }
        x->dos.ax = 0x0E00;
        x->dos.dx = (u16)(d - 'A');
        return 3;
    }
    if (j->dir_step == 2 && j->dir[j->dir[1] == ':' ? 2 : 0]) {
        x->dos.ax = 0x3B00;
        x->dos_path = j->dir;
        return 3;
    }
    j->dir_step = 0;
    return 0;
}

int agent_vm_next(u32 result, const char *comspec, struct agent_exec *x)
{
    struct agent_job *j = running;
    u32 i;
    x->dos_path = NULL;
    if (closer && result == 0xFFFFFFFEu) {      /* a stream's file closed: the next, or the job's end is told */
        struct agent_job *c = closer;
        if (files_close(c, x) == 3)
            return 3;
        closer = NULL;
        if (!c->app)
            finish(c);
        j = running;                            /* (NULL: the next job) */
    } else if (j && j->dir_step && result == 0xFFFFFFFEu) {     /* its drive or folder set: the next, or its EXEC */
        j->dir_step++;
        if (j->dir_step <= 2 && dir_call(j, x) == 3)
            return 3;
        j->dir_step = 0;
        goto files;
    } else if (j && j->fstep && result == 0xFFFFFFFEu) {        /* a stream's file opened, or not */
        if (file_answer(j, ans_ax, ans_cf) == 0)
            goto files;
        result = 0x1001F;                       /* (it can't be had: the job could not be run) */
    }
    if (j && result == NEXT_DETACHED) {         /* its program is an app now: the job goes with it */
        j->app = 1;
        kprintf("GLOS-AGENT app seq=%u via=%s\n", j->seq, j->shell ? "comspec" : j->path);
        running = NULL;
        if (j->kill)
            app_kill_job(j);
        tell(j);
        closer = j;                             /* (the stub's handles are its own again; the app has the files) */
        if (files_close(j, x) == 3)
            return 3;
        closer = NULL;
        j = NULL;
    }
    if (j) {
        u32 err = result & 0xFFFF;
        if (!j->shell && (result & 0x10000) && (err == 2 || err == 3 || err == 5)) {
            if (candidate(j, ++j->cand))        /* not there: the next place */
                goto exec;
            if (!j->keep) {
                j->shell = 1;                   /* not found anywhere: COMMAND.COM says so */
                goto exec;
            }                                   /* (API 2.0's: its app is told it could not be run, code 126) */
        }
        j->result = result;
        closer = j;                             /* its streams' files closed first, a call each */
        if (files_close(j, x) == 3)
            return 3;
        closer = NULL;
        finish(j);
    }
    j = 0;
    for (i = 0; i < NJOBS; i++)
        if (jobs[i].state == J_QUEUED && (!j || (s32)(jobs[i].seq - j->seq) < 0))
            j = &jobs[i];
    if (j) {
        j->state = J_RUNNING;
        j->t_start = timer_ticks() | 1;
        running = j;
        if (j->debug && gdbp_arm_job(j->seq) != 0)      /* (the debugger has another program: it just runs) */
            j->debug = 0;
        if (j->console) {                       /* its keyboard is its app's from now: what the BIOS's buffer */
            u16 tail = vm_rd16(0x41C);          /* held before (the harness's F1) isn't typed into it */
            vm_wr8(0x41A, (u8)tail);
            vm_wr8(0x41B, (u8)(tail >> 8));
        }
        kprintf("GLOS-AGENT run seq=%u cmd=\"%s\"%s%s%s%s%s\n", j->seq, j->cmd, j->direct ? " direct=1" : "",
                j->profile[0] ? " profile=" : "", j->profile, j->dir[0] ? " dir=" : "", j->dir);
        tell(j);
        j->fs = 0;
        j->fstep = FS_OPEN;
        if (j->dir[0]) {                        /* its drive and folder first (the system VM's: DOS has one) */
            j->dir_step = 1;
            if (dir_call(j, x) == 3)
                return 3;
        }
        goto files;
    }
    session_job(0, 0, "", "");
    if (exit_req && (s32)(timer_ticks() - exit_at) >= 0) {
        static u32 settle;                      /* the tick SSH's last news of a job will have gone out by */
        if (kcfg_pending() && (s32)(timer_ticks() - exit_at) < 5 * 1024)
            return 0;                           /* (a setting or GLOS.DAT is being written: the writer wakes this) */
        for (i = 0; i < NJOBS; i++)             /* a job's end that SSH has still to tell its client (an app that */
            if (jobs[i].state == J_DONE && jobs[i].owner && (s32)(timer_ticks() - exit_at) < 10 * 1024) {
                settle = (timer_ticks() + 300) | 1;     /* left when asked to at this exit: its last line and its */
                if (kick_ssh)                   /* exit code were lost on a busy 486) */
                    kick_ssh();
                return 0;
            }
        if (settle && (s32)(timer_ticks() - settle) < 0)
            return 0;
        settle = 0;
        return -1;
    }
    return 0;
files:                                          /* its streams' files, a call each; then its EXEC */
    j = running;
    if (file_call(j, x) == 3)
        return 3;
    j = running;
    j->shell = (u8)split(j);
    if (!j->shell && !candidate(j, 0)) {
        if (j->keep)                            /* (API 2.0's: no such program is an answer, not COMMAND.COM's words) */
            return agent_vm_next(0x10002, comspec, x);
        j->shell = 1;
    }
exec:
    j = running;
    if (!j->shell)
        long_line(j);
    session_job(j->direct, j->console ? (j->full ? 4 : 3) : j->exclusive, j->profile, j->env);     /* for the
                                                   session this EXEC begins (3: a console job's, 4: full-screen from
                                                   the start), with its own environment's strings */
    x->longtail = 0;
    if (j->shell) {
        x->path = comspec;
        x->t1 = " /C ";
        x->t2 = j->cmd;
    } else {
        x->path = j->path;
        x->t1 = j->args;
        x->t2 = "";
        x->longtail = (u8)(strlen(j->args) > 126);      /* (CMDLINE has all of it: long_line()) */
    }
    return 1;
}

int agent_vm_wake_pending(void)
{
    u32 i;
    if (exit_req || dos_vm_pending())
        return 1;
    for (i = 0; i < NJOBS; i++)
        if (jobs[i].state == J_QUEUED)
            return 1;
    return 0;
}

int agent_vm_capturing(void) { return running != 0 || closer != 0; }

/* The job that runs has nobody at the PC to answer it: its output is kept for whoever asked (SSH, an app), and
   it has no console of its own (it isn't a terminal's, nor full-screen). */
int agent_vm_unattended(void) { return running && !running->console && !running->full; }

/* The job whose program runs now (it may become an app's). */
void *agent_vm_job(void) { return running; }

/* The VM thread makes a call for an app (or has made it): output is that
   app's job's (or again the job's that was running). The one before. */
void *agent_vm_as(void *job)
{
    void *was = running;
    running = job;
    agent_vm_traps();
    return was;
}

/* The interrupts that trap only while a job runs (with VME; without it every INT traps): INT 29h for the
   capture; INT 16h and 28h while a console job's program may wait for keys, so that apps' calls are made
   meanwhile (v86.c). */
void agent_vm_traps(void)
{
    if (!vm.vme)
        return;
    cpu_int_redirect(0x29, !running);
    cpu_int_redirect(0x16, !(running && running->console));
    cpu_int_redirect(0x28, !(running && running->console));
}

/* An app that began as a job has ended: result as INT 21h 4Dh gave it. */
void agent_app_done(void *job, u32 result)
{
    struct agent_job *j = job;
    j->result = result;
    j->state = J_DONE;
    kprintf("GLOS-AGENT done seq=%u code=%u via=app\n", j->seq, result & 0xFF);
    tell(j);
    if (j->detach)
        j->state = J_FREE;
    if (kick_ssh)
        kick_ssh();
}

/* Every tick (interrupts off): a job being killed gets the next kill. */
void agent_vm_tick(u32 now)
{
    if (sig_left && (s32)(now - sig_at) >= 0) { /* a job's Ctrl+C or Ctrl+Break, a key at a time (JOB's SIGNAL) */
        u32 key = sig_brk ? 0x146 : 0x2E;       /* (Ctrl+Break is E0 46: the BIOS calls INT 1Bh; Ctrl+C a key) */
        if (!running && sig_left > 1)
            sig_left = sig_left == 4 ? 0 : 1;   /* (its program has gone: Ctrl is let go, if it went down) */
        if (sig_left && vkbc_vm_key(sig_left == 4 || sig_left == 1 ? 0x1D : key, sig_left > 2) == 0)
            sig_left--;
        sig_at = now + 20;
    }
    watch_tick(now);
    if (running && running->kh != running->kt)
        feed(running);                          /* (a console job's keys, as its BIOS buffer empties) */
    if (running && !running->app && running->kill && !vm.kill_req && now - last_kill >= 512) {     /* (an app's
                                                   kill is app.c's: one request, in the call under way) */
        last_kill = now;
        vm.kill_req = 1;
        vm.kill_reason = "agent";
        vm.kill_since = now;
        vm_kick();
    }
}

static void put(int stream, u8 c)
{
    struct ring *r = &running->out[stream];
    if (running->detach)
        return;
    while (ring_used(r) == RING) {
        if ((!running->owner && !running->app_owner) || running->kill || vm.kill_req)
            return;                             /* nobody reads it: dropped */
        if (kick_ssh)
            kick_ssh();
        thread_wake(&running->readq);
        thread_wait(&roomq, timer_ticks() + 256);
    }
    r->buf[r->head & (RING - 1)] = c;
    __asm__ volatile("" ::: "memory");
    r->head++;
    if (running->from_app && (c == '\n' || ring_used(r) > RING / 2))
        thread_wake(&running->readq);           /* (the app reads a line at a time, or a lot) */
    if (running->from_app && running->app_owner && ring_used(r) == 1 && !ring_used(&running->out[!stream])) {
        struct kevent e;                    /* output where there was none: its app is told (once until it
                                                   has read it all) */
        memset(&e, 0, sizeof e);
        e.type = KEV_JOB;
        e.key = running->seq;
        e.x = (int)app_state(running);
        app_event(running->app_owner, &e);
    }
}

static u32 far_lin(u32 fp) { return ((fp >> 16) << 4) + (fp & 0xFFFF); }

/* Handle h of the current program is open on the console: its SFT entry's
   device information has bit 7 (a character device) and bit 1 (the console
   output device). Without the List of Lists, handles 1 and 2 are taken on
   trust. */
int agent_vm_handle_info(u16 h)
{
    u16 psp = vm_current_psp(), n, count;
    u32 p, sft, hops = 0;
    u8 idx;
    if (!psp || !vm.bi->lol)
        return -1;
    p = (u32)psp << 4;
    n = vm_rd16(p + 0x32);
    if (h >= n)
        return -1;
    idx = vm_rd8(far_lin(vm_rd32(p + 0x34)) + h);
    if (idx == 0xFF)
        return -1;
    sft = far_lin(vm_rd32(vm.bi->lol + 4));
    while (hops++ < 16 && sft < 0x110000) {
        count = vm_rd16(sft + 4);
        if (idx < count)
            return vm_rd16(sft + 6 + (u32)idx * SFT_SIZE + 5);
        idx = (u8)(idx - count);
        if ((vm_rd32(sft) & 0xFFFF) == 0xFFFF)
            break;
        sft = far_lin(vm_rd32(sft));
    }
    return -1;
}

static int is_console(u16 h)
{
    int info;
    if (!vm_current_psp() || !vm.bi->lol)
        return h == 1 || h == 2;
    info = agent_vm_handle_info(h);
    return info >= 0 && (info & 0x80) && (info & 0x02);
}

void agent_vm_int21(const struct trapframe *tf)
{
    u32 ah = (tf->eax >> 8) & 0xFF, i, n;
    watch_int21((u16)tf->eax, (u16)tf->ebx, (u16)tf->ecx, (u16)tf->edx, vm_lin(tf->v86_ds, tf->edx),
                vm_lin(tf->v86_ds, tf->esi));  /* (a directory's watchers, M9) */
    if (!running || (running->console && !running->hidden))    /* (a window's job: all of it through INT 29h, echo
                                                                   too; a hidden one's: by its handles, here) */
        return;
    if (ah == 0x02 || (ah == 0x06 && (tf->edx & 0xFF) != 0xFF)) {
        if (is_console(1))
            put(0, (u8)tf->edx);
    } else if (ah == 0x09) {
        if (is_console(1))
            for (i = 0; i < 4096; i++) {
                u8 c = vm_rd8(vm_lin(tf->v86_ds, tf->edx + i));
                if (c == '$')
                    break;
                put(0, c);
            }
    } else if (ah == 0x40 && is_console((u16)tf->ebx)) {
        n = tf->ecx & 0xFFFF;
        for (i = 0; i < n; i++)
            put((tf->ebx & 0xFFFF) == 2, vm_rd8(vm_lin(tf->v86_ds, tf->edx + i)));
    }
}

/* A job whose standard input is its app's (API 2.0's JOB, GLOS_RUNF_STDIN): DOS's console input calls on a handle
   that is the console are answered here, from what the app has written, and never reach DOS. 0: not such a call
   (DOS has it); 1: answered (the caller goes on after the INT); 2: nothing to give yet (the caller waits and
   makes the INT again). */
static u32 in_used(const struct agent_job *j) { return j->it - j->ih; }

static int in_line(const struct agent_job *j)   /* a whole line waits (or the input's end has come) */
{
    u32 i;
    for (i = j->ih; i != j->it; i++)
        if (j->in[i & (IN_RING - 1)] == '\n')
            return 1;
    return j->in_eof || in_used(j) == IN_RING;
}

static int console_in(u16 h)                    /* the handle is the console's input */
{
    int info;
    if (!vm_current_psp() || !vm.bi->lol)
        return h == 0;
    info = agent_vm_handle_info(h);
    return info >= 0 && (info & 0x80) && (info & 0x01);
}

int agent_vm_stdin(struct trapframe *tf)
{
    struct agent_job *j = running;
    u32 ah = (tf->eax >> 8) & 0xFF, n, max, i;
    u8 c;
    if (!j || !j->in_pipe || !j->hidden || j->full || j->app)
        return 0;
    if (ah == 0x0C) {                           /* (flush, then a function: nothing of the app's is thrown away) */
        ah = tf->eax & 0xFF;
        if (ah != 0x01 && ah != 0x06 && ah != 0x07 && ah != 0x08 && ah != 0x0A)
            return 0;
    }
    switch (ah) {
    case 0x3F:                                  /* read: a line at a time, as the console gives it */
        if (!console_in((u16)tf->ebx))
            return 0;
        if (!in_used(j) && !j->in_eof)
            return 2;
        max = tf->ecx & 0xFFFF;
        for (n = 0; n < max && in_used(j); ) {
            c = j->in[j->ih++ & (IN_RING - 1)];
            vm_wr8(vm_lin(tf->v86_ds, (tf->edx + n) & 0xFFFF), c);
            n++;
            if (c == '\n')
                break;
        }
        tf->eax = (tf->eax & ~0xFFFFu) | n;     /* (0: the end of the file) */
        tf->eflags &= ~FL_CF;
        return 1;
    case 0x0B:                                  /* is there input? */
        if (!console_in(0))
            return 0;
        tf->eax = (tf->eax & ~0xFFu) | (in_used(j) || j->in_eof ? 0xFF : 0);
        return 1;
    case 0x06:                                  /* direct console input (DL FFh): none is no wait */
        if ((tf->edx & 0xFF) != 0xFF || !console_in(0))
            return 0;
        if (!in_used(j) && !j->in_eof) {
            tf->eax &= ~0xFFu;
            tf->eflags |= FL_ZF;
            return 1;
        }
        tf->eflags &= ~FL_ZF;
        /* fall through */
    case 0x01: case 0x07: case 0x08:            /* a character (01h's echo is the app's to do) */
        if (!console_in(0))
            return 0;
        if (!in_used(j) && !j->in_eof)
            return 2;
        c = in_used(j) ? j->in[j->ih++ & (IN_RING - 1)] : 0x1A;
        tf->eax = (tf->eax & ~0xFFu) | c;
        return 1;
    case 0x0A:                                  /* a line into DS:DX's buffer: its room, its length, its text, a CR */
        if (!console_in(0))
            return 0;
        if (!in_line(j))
            return 2;
        max = vm_rd8(vm_lin(tf->v86_ds, tf->edx & 0xFFFF));
        for (n = 0; in_used(j); ) {
            c = j->in[j->ih++ & (IN_RING - 1)];
            if (c == '\n')
                break;
            if (c != '\r' && max && n + 1 < max)
                vm_wr8(vm_lin(tf->v86_ds, (tf->edx + 2 + n++) & 0xFFFF), c);
        }
        if (!n && !in_used(j) && j->in_eof && max > 1)
            vm_wr8(vm_lin(tf->v86_ds, (tf->edx + 2 + n++) & 0xFFFF), 0x1A);     /* (the input's end: Ctrl+Z) */
        if (max) {
            vm_wr8(vm_lin(tf->v86_ds, (tf->edx + 1) & 0xFFFF), (u8)n);
            vm_wr8(vm_lin(tf->v86_ds, (tf->edx + 2 + n) & 0xFFFF), '\r');
        }
        (void)i;
        return 1;
    }
    return 0;
}

int agent_vm_int29(const struct trapframe *tf)
{
    if (agent_vm_console()) {                   /* a console job's: its window's only (DOS's console driver */
        if (!running->hidden || !vm.indos || !vm_rd8(vm.indos))    /* would hand it to INT 10h 0Eh, which takes it */
            put(0, (u8)tf->eax);                /* again). A hidden job's through DOS was kept by its handle already */
        return 1;
    }
    if (running && (running->console || !vm.indos || !vm_rd8(vm.indos)))
        put(0, (u8)tf->eax);
    return 0;
}

/* ---- console jobs (M9; the VM thread's) */

int agent_vm_console(void) { return running && running->console && !running->full && !running->app; }

static void tell(struct agent_job *j);

void agent_vm_console_full(void)
{
    if (!agent_vm_console())
        return;
    running->full = 1;
    kprintf("GLOS-AGENT full seq=%u\n", running->seq);
    tell(running);
}

void agent_vm_console_char(u8 c)
{
    if (!agent_vm_console())
        return;
    if (running->hidden && vm.indos && vm_rd8(vm.indos))
        return;                                 /* (a hidden job's through DOS was kept by its handle already: MS-DOS's
                                                   console driver writes with the BIOS's teletype, not INT 29h, and
                                                   every character came twice on bench-g400) */
    put(0, c);
}
