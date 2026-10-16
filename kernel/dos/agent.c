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

#define GLOS_KERNEL
#include "glos/api.h"
#include "glos/bootinfo.h"
#include "agent.h"
#include "app.h"
#include "dos.h"
#include "session.h"
#include "io.h"
#include "kprintf.h"
#include "sched.h"
#include "timer.h"
#include "vm.h"
#include "watch.h"

#define NJOBS    16                             /* (the ssh's and the apps': a launched app's lasts as long as it) */
#define RING     0x4000u                        /* a power of two */
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
    char cmd[256];                              /* (a program's path, up to 79, and a tail of up to 126) */
    char prog[80];                              /* the command's first word */
    const char *args;                           /* the rest, from the separator on */
    char path[80];                              /* what the stub is EXECing */
    struct ring out[2];                         /* stdout, stderr */
    /* M9: an app's job (GLOS_FN_RUN) */
    u8 from_app;                                /* its output is its app's to read, its keys the app's to type */
    u8 console;                                 /* GLOS_RUN_CONSOLE: its program writes into the app's window */
    u8 full;                                    /* ... until it wanted the screen (or GLOS_RUN_FULL) */
    u8 detach;                                  /* GLOS_RUN_DETACH: nobody's; no output kept */
    u8 dir_step;                                /* starting in dir: 1 its drive set, 2 its folder, 0 done */
    u8 told;                                    /* the GLOS_JOB_ state its app last heard of, FFh none */
    void *app_owner;                            /* its app (struct app), or NULL */
    char dir[80];
    char env[512];                              /* NAME=VALUE strings, a NUL after each and one more: its own */
    u16 keys[64];                               /* typed by its app, on their way into the BIOS's buffer */
    volatile u32 kh, kt;
    u16 left[64];                               /* typed, but not read by the time it ended: its app's again */
    u8 nleft;
    struct waitq readq;                         /* its app, waiting for output */
};

static struct agent_job jobs[NJOBS];
static struct agent_job *running;
static u32 next_seq;
static struct waitq roomq;                      /* the VM thread, waiting for ring space */
static u32 last_kill;
static volatile u32 exit_at;
static volatile u8 exit_req;
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
        return GLOS_JOB_QUEUED;
    if (j->state == J_DONE)
        return GLOS_JOB_DONE;
    return j->app ? GLOS_JOB_APP : j->full || !j->console ? GLOS_JOB_FULL : GLOS_JOB_RUNNING;
}

static u32 code_of(const struct agent_job *j) { return (j->result & 0x10000) ? 126 : (j->result & 0xFF); }

/* Its app told it has moved on (interrupts off or on: app_event is safe in either). */
static void tell(struct agent_job *j)
{
    struct glos_event e;
    u32 st = app_state(j);
    thread_wake(&j->readq);
    if (!j->app_owner || st == j->told)
        return;
    j->told = (u8)st;
    memset(&e, 0, sizeof e);
    e.type = GLOS_EV_JOB;
    e.key = j->seq;
    e.x = (int)st;
    e.y = (int)code_of(j);
    app_event(j->app_owner, &e);
}

int agent_post_app(void *app, const struct glos_run *r, u32 *seq)
{
    struct agent_job *j = NULL;
    u32 f, i;
    const char *cmd = r->command;
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
    j->direct = 0;
    j->exclusive = 0;
    j->profile[0] = 0;
    j->out[0].head = j->out[0].tail = j->out[1].head = j->out[1].tail = 0;
    j->detach = (u8)((r->flags & GLOS_RUN_DETACH) != 0);
    j->from_app = (u8)!j->detach;
    j->console = (u8)(!j->detach && (r->flags & GLOS_RUN_CONSOLE));
    j->full = (u8)(j->console && (r->flags & GLOS_RUN_FULL));
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
    static struct glos_run r;                   /* (the VM thread's alone: no other caller) */
    u32 seq, i;
    memset(&r, 0, sizeof r);
    for (i = 0; cmd[i] && i < sizeof r.command - 1; i++)
        r.command[i] = cmd[i];
    for (i = 0; dir[i] && i < sizeof r.dir - 1; i++)
        r.dir[i] = dir[i];
    r.flags = GLOS_RUN_DETACH;
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

int agent_app_read(void *app, u32 seq, u8 *out, u32 room, u32 wait_ticks, u32 *state, u32 *code)
{
    struct agent_job *j = app_job(app, seq);
    u32 n = 0, s, until = timer_ticks() + wait_ticks, f;
    if (!j)
        return -1;
    for (;;) {
        for (s = 0; s < 2 && n < room; s++) {
            struct ring *r = &j->out[s];
            while (ring_used(r) && n < room) {
                out[n++] = r->buf[r->tail & (RING - 1)];
                __asm__ volatile("" ::: "memory");
                r->tail++;
            }
        }
        if (n || j->state == J_DONE || !wait_ticks || (wait_ticks != 0xFFFFFFFFu && (s32)(timer_ticks() - until) >= 0))
            break;
        thread_wait(&j->readq, wait_ticks == 0xFFFFFFFFu ? timer_ticks() + 1024 : until);
    }
    if (n) {
        f = irq_save();
        thread_wake(&roomq);
        irq_restore(f);
    }
    *state = app_state(j);
    *code = code_of(j);
    if (j->state == J_DONE && n < room && !ring_used(&j->out[0]) && !ring_used(&j->out[1]) && !j->nleft) {
        f = irq_save();                         /* (its end told, with room to spare: the slot is free again) */
        j->app_owner = NULL;
        j->state = J_FREE;
        irq_restore(f);
    }
    return (int)n;
}

int agent_app_type(void *app, u32 seq, const u16 *keys, u32 n)
{
    struct agent_job *j = app_job(app, seq);
    u32 i;
    if (!j)
        return -1;
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
    j->nleft = 0;
    if (!ring_used(&j->out[0]) && !ring_used(&j->out[1])) {
        j->app_owner = NULL;
        j->state = J_FREE;
    }
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

u32 agent_jobs_stat(void *out, u32 room)
{
    u32 n = 0, i;
    for (i = 0; i < NJOBS && n + sizeof(struct glos_stat_app) <= room; i++) {
        const struct agent_job *j = &jobs[i];
        struct glos_stat_app *a = (struct glos_stat_app *)((u8 *)out + n);
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

void agent_exit(u32 delay_ticks)
{
    u32 f = irq_save();
    exit_at = timer_ticks() + delay_ticks;
    exit_req = 1;
    vm_kick();
    irq_restore(f);
}

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
    if (j && j->dir_step && result == 0xFFFFFFFEu) {    /* its drive or folder set: the next, or its EXEC */
        j->dir_step++;
        if (j->dir_step <= 2 && dir_call(j, x) == 3)
            return 3;
        j->dir_step = 0;
        goto start;
    }
    if (j && result == NEXT_DETACHED) {         /* its program is an app now: the job goes with it */
        j->app = 1;
        kprintf("GLOS-AGENT app seq=%u via=%s\n", j->seq, j->shell ? "comspec" : j->path);
        running = NULL;
        if (j->kill)
            app_kill_job(j);
        tell(j);
        j = NULL;
    }
    if (j) {
        u32 err = result & 0xFFFF;
        if (!j->shell && (result & 0x10000) && (err == 2 || err == 3 || err == 5)) {
            if (candidate(j, ++j->cand))        /* not there: the next place */
                goto exec;
            j->shell = 1;                       /* not found anywhere: COMMAND.COM says so */
            goto exec;
        }
        j->result = result;
        if (j->console)
            keys_left(j);
        j->state = J_DONE;
        kprintf("GLOS-AGENT done seq=%u code=%u via=%s%s\n", j->seq, result & 0xFF, j->shell ? "comspec" : j->path,
                (result & 0x10000) ? " cannot-run" : "");
        running = NULL;
        tell(j);
        if (j->detach)
            j->state = J_FREE;                  /* (nobody to tell) */
        if (kick_ssh)
            kick_ssh();
    }
    j = 0;
    for (i = 0; i < NJOBS; i++)
        if (jobs[i].state == J_QUEUED && (!j || (s32)(jobs[i].seq - j->seq) < 0))
            j = &jobs[i];
    if (j) {
        j->state = J_RUNNING;
        running = j;
        if (j->console) {                       /* its keyboard is its app's from now: what the BIOS's buffer */
            u16 tail = vm_rd16(0x41C);          /* held before (the harness's F1) isn't typed into it */
            vm_wr8(0x41A, (u8)tail);
            vm_wr8(0x41B, (u8)(tail >> 8));
        }
        kprintf("GLOS-AGENT run seq=%u cmd=\"%s\"%s%s%s%s%s\n", j->seq, j->cmd, j->direct ? " direct=1" : "",
                j->profile[0] ? " profile=" : "", j->profile, j->dir[0] ? " dir=" : "", j->dir);
        tell(j);
        if (j->dir[0]) {                        /* its drive and folder first (the system VM's: DOS has one) */
            j->dir_step = 1;
            if (dir_call(j, x) == 3)
                return 3;
        }
        goto start;
    }
    session_job(0, 0, "", "");
    if (exit_req && (s32)(timer_ticks() - exit_at) >= 0)
        return -1;
    return 0;
start:
    j = running;
    j->shell = (u8)split(j);
    if (!j->shell && !candidate(j, 0))
        j->shell = 1;
exec:
    j = running;
    session_job(j->direct, j->console ? (j->full ? 4 : 3) : j->exclusive, j->profile, j->env);     /* for the
                                                   session this EXEC begins (3: a console job's, 4: full-screen from
                                                   the start), with its own environment's strings */
    if (j->shell) {
        x->path = comspec;
        x->t1 = " /C ";
        x->t2 = j->cmd;
    } else {
        x->path = j->path;
        x->t1 = j->args;
        x->t2 = "";
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

int agent_vm_capturing(void) { return running != 0; }

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
        struct glos_event e;                    /* output where there was none: its app is told (once until it
                                                   has read it all) */
        memset(&e, 0, sizeof e);
        e.type = GLOS_EV_JOB;
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
    watch_int21((u16)tf->eax, (u16)tf->ebx, (u16)tf->ecx, vm_lin(tf->v86_ds, tf->edx),
                vm_lin(tf->v86_ds, tf->esi));  /* (a directory's watchers, M9) */
    if (!running || running->console)           /* (a console job's: all of it through INT 29h, echo too) */
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

int agent_vm_int29(const struct trapframe *tf)
{
    if (agent_vm_console()) {                   /* a console job's: its window's only (DOS's console driver */
        put(0, (u8)tf->eax);                    /* would hand it to INT 10h 0Eh, which takes it again) */
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
    if (agent_vm_console())
        put(0, c);
}
