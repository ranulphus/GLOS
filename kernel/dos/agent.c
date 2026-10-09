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
 * innermost first, until the job has ended; its output is then dropped. */
#include "glos/bootinfo.h"
#include "agent.h"
#include "dos.h"
#include "session.h"
#include "io.h"
#include "kprintf.h"
#include "sched.h"
#include "timer.h"
#include "vm.h"

#define NJOBS    4
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
    char profile[16];                           /* glos run --profile NAME, or empty */
    volatile u8 kill;                           /* end it: glos kill, or its client has gone */
    u32 seq;
    u32 result;                                 /* INT 21h 4Dh's AX, or 10000h + the EXEC error */
    void *owner;                                /* the ssh side's channel; NULL once it has gone */
    char cmd[128];
    char prog[80];                              /* the command's first word */
    const char *args;                           /* the rest, from the separator on */
    char path[80];                              /* what the stub is EXECing */
    struct ring out[2];                         /* stdout, stderr */
};

static struct agent_job jobs[NJOBS];
static struct agent_job *running;
static u32 next_seq;
static struct waitq roomq;                      /* the VM thread, waiting for ring space */
static u32 last_kill;
static volatile u32 exit_at;
static volatile u8 exit_req;
static void (*kick_ssh)(void);

static u32 irq_save(void)
{
    u32 f;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(f) :: "memory");
    return f;
}

static void irq_restore(u32 f)
{
    if (f & 0x200)
        sti();
}

static u32 ring_used(const struct ring *r) { return r->head - r->tail; }

static char upper(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 32) : c; }

/* ---- the ssh thread's side */

void agent_init(void (*kick)(void)) { kick_ssh = kick; }

int agent_post(void *owner, const char *cmd) { return agent_post_run(owner, cmd, 0, ""); }

int agent_post_run(void *owner, const char *cmd, int direct, const char *profile)
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
    j->direct = (u8)(direct != 0);
    for (i = 0; profile[i] && i < sizeof j->profile - 1; i++)
        j->profile[i] = profile[i];
    j->profile[i] = 0;
    j->out[0].head = j->out[0].tail = j->out[1].head = j->out[1].tail = 0;
    f = irq_save();
    j->seq = next_seq++;
    j->state = J_QUEUED;
    vm_kick();                                  /* the stub may be halted, waiting */
    irq_restore(f);
    return 0;
}

void agent_drop(void *owner)
{
    u32 f = irq_save(), i;
    for (i = 0; i < NJOBS; i++)
        if (jobs[i].state != J_FREE && jobs[i].owner == owner) {
            jobs[i].owner = NULL;
            if (jobs[i].state == J_QUEUED)
                jobs[i].state = J_FREE;
            else
                jobs[i].kill = 1;
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
        if (j->state != J_RUNNING && j->state != J_DONE)
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

u32 agent_ps(char *buf, u32 max)
{
    static const char *const names[] = { "free", "queued", "running", "done" };
    u32 n = 0, i, f = irq_save();
    for (i = 0; i < NJOBS && n < max; i++)
        if (jobs[i].state == J_QUEUED || jobs[i].state == J_RUNNING)
            n += (u32)ksnprintf(buf + n, max - n, "job %u %s%s %s\n", jobs[i].seq, names[jobs[i].state],
                                jobs[i].kill ? " killing" : "", jobs[i].cmd);
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

int agent_vm_next(u32 result, const char *comspec, struct agent_exec *x)
{
    struct agent_job *j = running;
    u32 i;
    if (j) {
        u32 err = result & 0xFFFF;
        if (!j->shell && (result & 0x10000) && (err == 2 || err == 3 || err == 5)) {
            if (candidate(j, ++j->cand))        /* not there: the next place */
                goto exec;
            j->shell = 1;                       /* not found anywhere: COMMAND.COM says so */
            goto exec;
        }
        j->result = result;
        j->state = J_DONE;
        kprintf("GLOS-AGENT done seq=%u code=%u via=%s%s\n", j->seq, result & 0xFF, j->shell ? "comspec" : j->path,
                (result & 0x10000) ? " cannot-run" : "");
        running = NULL;
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
        kprintf("GLOS-AGENT run seq=%u cmd=\"%s\"%s%s%s\n", j->seq, j->cmd, j->direct ? " direct=1" : "",
                j->profile[0] ? " profile=" : "", j->profile);
        j->shell = (u8)split(j);
        if (!j->shell && !candidate(j, 0))
            j->shell = 1;
        goto exec;
    }
    session_job(0, "");
    if (exit_req && (s32)(timer_ticks() - exit_at) >= 0)
        return -1;
    return 0;
exec:
    j = running;
    session_job(j->direct, j->profile);         /* for the session this EXEC begins */
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

/* Every tick (interrupts off): a job being killed gets the next kill. */
void agent_vm_tick(u32 now)
{
    if (running && running->kill && !vm.kill_req && now - last_kill >= 512) {
        last_kill = now;
        vm.kill_req = 1;
        vm.kill_since = now;
        vm_kick();
    }
}

static void put(int stream, u8 c)
{
    struct ring *r = &running->out[stream];
    while (ring_used(r) == RING) {
        if (!running->owner || running->kill || vm.kill_req)
            return;                             /* nobody reads it: dropped */
        if (kick_ssh)
            kick_ssh();
        thread_wait(&roomq, timer_ticks() + 256);
    }
    r->buf[r->head & (RING - 1)] = c;
    __asm__ volatile("" ::: "memory");
    r->head++;
}

static u32 far_lin(u32 fp) { return ((fp >> 16) << 4) + (fp & 0xFFFF); }

/* Handle h of the current program is open on the console: its SFT entry's
   device information has bit 7 (a character device) and bit 1 (the console
   output device). Without the List of Lists, handles 1 and 2 are taken on
   trust. */
static int is_console(u16 h)
{
    u16 psp = vm_current_psp(), n, count;
    u32 p, sft, hops = 0;
    u8 idx;
    if (!psp || !vm.bi->lol)
        return h == 1 || h == 2;
    p = (u32)psp << 4;
    n = vm_rd16(p + 0x32);
    if (h >= n)
        return 0;
    idx = vm_rd8(far_lin(vm_rd32(p + 0x34)) + h);
    if (idx == 0xFF)
        return 0;
    sft = far_lin(vm_rd32(vm.bi->lol + 4));
    while (hops++ < 16 && sft < 0x110000) {
        count = vm_rd16(sft + 4);
        if (idx < count) {
            u16 info = vm_rd16(sft + 6 + (u32)idx * SFT_SIZE + 5);
            return (info & 0x80) && (info & 0x02);
        }
        idx = (u8)(idx - count);
        if ((vm_rd32(sft) & 0xFFFF) == 0xFFFF)
            break;
        sft = far_lin(vm_rd32(sft));
    }
    return 0;
}

void agent_vm_int21(const struct trapframe *tf)
{
    u32 ah = (tf->eax >> 8) & 0xFF, i, n;
    if (!running)
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

void agent_vm_int29(const struct trapframe *tf)
{
    if (running && (!vm.indos || !vm_rd8(vm.indos)))
        put(0, (u8)tf->eax);
}
