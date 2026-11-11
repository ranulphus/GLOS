/* glos gdb (PRD §7.8; M5 A7): gdb's remote protocol for the program in
 * front, a 32-bit DPMI client, over an ssh direct-tcpip channel.
 *
 * Two halves. On the VM thread, the program stops: at the first return to
 * its 32-bit code once the debugger is armed (glos gdb) or gdb interrupts
 * it, and at its breakpoints (INT3) and single steps (#DB), which the DPMI
 * host hands over before the client's own handlers see them. Stopped, the
 * VM thread waits in the kernel with the program's frame kept; the rest of
 * GLOS runs on. On the ssh thread, gdb's packets are answered from that
 * frame and from the program's memory, which is there for any thread while
 * the program is stopped (the client's address space stays current).
 *
 * Addresses are the program's own: DJGPP links at offsets in a segment
 * whose base is where its memory landed, so every address gdb gives is
 * taken from the base of the code segment the program first stopped in.
 * An address past the segment's end wraps, as the program's own "near
 * pointer" trick does; the kernel's memory is not reachable.
 *
 * Breakpoints are gdb's: it writes the INT3s itself (no Z packets), reads
 * EIP one past them and puts it back. The registers are the sixteen of an
 * i386 without its FPU.
 *
 * A stop inside a nested real-mode call can't be shown to gdb as anything
 * it understands, so a stop waits for the program's own 32-bit code: one
 * waiting at a DOS prompt answers when it comes back from DOS.
 *
 * API 2.0's DEBUG (kernel/app/api2dbg.c; phase B step 12) is a second way in, for an app debugging a program it
 * started. For it:
 * - The stops are a named program's: a job's (its number), on the VM thread, or the app that job became, on the
 *   app's first thread (the stub follows it there when it becomes one). An app's thread waits on a queue of the
 *   stub's own.
 * - A program stopped on the VM thread has the apps' DOS and BIOS calls made meanwhile (the debugger's own file
 *   reading, most of all), as a console job's program waiting for a key has (app_vm_idle_turn): it is in its
 *   own code, not in DOS, so DOS is free. Its registers are a copy kept here while it is stopped, put back when
 *   it goes on. */
#include <string.h>

#include "agent.h"
#include "app.h"
#include "arch.h"
#include "dpmi.h"
#include "gdbprog.h"
#include "io.h"
#include "kprintf.h"
#include "vm.h"

#define PKT 1024

static struct {
    u8 armed;                           /* glos gdb: stop when the program next runs its 32-bit code */
    u8 attached;                        /* gdb's channel is open */
    u8 want_stop;                       /* gdb's interrupt (Ctrl-C) */
    volatile u8 stopped;                /* the VM thread waits in stop() */
    volatile u8 news;                   /* a stop gdb hasn't been told of */
    u8 waiting;                         /* gdb waits for a stop reply (after ?, c, s) */
    u8 step;                            /* resume for one instruction */
    u8 based;
    u8 gone;                            /* the program ended while gdb was attached: its status */
    u8 gone_told;
    u8 gone_news;
    int sig;
    struct trapframe *tf;               /* the stopped program's registers: `frame`, or 0 */
    struct trapframe frame;
    struct thread *thread;              /* whose stops these are; 0: the VM thread's (the program in front) */
    void *app;                          /* ... the app whose first thread that is, or 0 */
    u32 job;                            /* only this job's program (its number); 0: whatever runs (glos gdb) */
    struct waitq wq;                    /* where an app's thread waits, stopped */
    u32 base;
    void (*wake)(void);
    void (*send)(const u8 *d, u32 n);
    char in[PKT];
    u32 in_len;
    u8 in_state;                        /* 0: between packets; 1: in one; 2, 3: its checksum */
    u8 sum, got;
} g;

static const char hexd[] = "0123456789abcdef";

static int hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* ---- the VM thread's side (interrupts off) */

/* The program's own 32-bit code: ring 3, a 32-bit code segment that isn't
   the host's trampolines. */
static int its_code(const struct trapframe *tf)
{
    return !(tf->eflags & FL_VM) && (tf->cs & 3) == 3 && (tf->cs & 0xFFFF) != SEL_TRAMP
        && (cpu_desc_hi(tf->cs) & (1u << 22));
}

static u32 *reg(struct trapframe *tf, u32 n);

static struct thread *target(void) { return g.thread ? g.thread : vm.thread; }

/* The running thread is the one whose stops are the debugger's: the VM thread with the job named (or any), or
   the app's first thread. */
static int mine(void)
{
    if (current != target())
        return 0;
    return current != vm.thread || !g.job || agent_job_seq(agent_vm_job()) == g.job;
}

static void stop(struct trapframe *tf, int sig)
{
    static const u8 regs[] = { 0, 1, 2, 3, 5, 6, 7, 8 };        /* (what gdb may have set: all but ESP and the flags) */
    int killed;
    u32 i;
    g.armed = g.want_stop = 0;
    if (!g.based) {
        g.base = sel_base((u16)tf->cs);
        g.based = 1;
        kprintf("GLOS-GDB stop cs=%04x base=%p eip=%p\n", tf->cs & 0xFFFF, g.base, tf->eip);
    }
    tf->eflags &= ~FL_TF;
    g.frame = *tf;
    g.tf = &g.frame;
    g.sig = sig;
    g.stopped = 1;
    g.news = 1;
    if (g.wake)
        g.wake();
    if (current == vm.thread) {
        while (g.stopped && !vm.kill_req) {     /* (vm_kick wakes this queue: gdb's resume, a kill, any IRQ, an
                                                   app's call queued) */
            if (app_vm_pending())               /* the apps' DOS and BIOS calls: made now, as while a console
                                                   job's program waits for a key (in DOS, if a handler of the
                                                   program's was stopped inside a call of DOS's) */
                app_vm_idle_turn(vm.indos && vm_rd8(vm.indos));
            if (g.stopped && !vm.kill_req)
                thread_block(&vm.waitq);
        }
        killed = vm.kill_req != 0;
    } else {
        while (g.stopped && !app_ending(g.app))
            thread_block(&g.wq);
        killed = app_ending(g.app);
    }
    g.stopped = 0;
    g.tf = 0;
    for (i = 0; i < sizeof regs; i++)           /* the registers as gdb left them */
        *reg(tf, regs[i]) = *reg(&g.frame, regs[i]);
    *reg(tf, 4) = *reg(&g.frame, 4);
    tf->eflags = (tf->eflags & ~(FL_ARITH | 0x400u)) | (g.frame.eflags & (FL_ARITH | 0x400u));
    if (g.step && !killed)
        tf->eflags |= FL_TF;
    g.step = 0;
}

int gdbp_trap(struct trapframe *tf)
{
    if ((!g.attached && !g.armed) || !mine())
        return 0;
    if (tf->vec == 1 && !(tf->eflags & FL_TF) && !g.attached)
        return 0;
    stop(tf, 5);                                /* SIGTRAP */
    return 1;
}

void gdbp_return(struct trapframe *tf)
{
    if ((g.armed || g.want_stop) && mine() && its_code(tf))
        stop(tf, g.want_stop ? 2 : 5);          /* SIGINT for gdb's interrupt */
}

int gdbp_stopped(void) { return g.stopped && target() == vm.thread; }

static void ended(u32 code)
{
    if (g.attached && !g.gone_told) {
        g.gone_told = 1;                        /* (once: an app's end is its job's end too) */
        g.gone = (u8)code;
        g.gone_news = 1;
        if (g.wake)
            g.wake();
    }
    g.armed = g.want_stop = g.step = g.based = 0;
    g.stopped = 0;
    g.thread = 0;
    g.app = 0;
}

void gdbp_program_end(u32 code)
{
    if (target() == vm.thread && !g.job)        /* (glos gdb's program; a job's end is told with its code) */
        ended(code);
}

/* A job has ended (the agent, with its exit code): the debugger's. */
void gdbp_job_end(u32 job, u32 code)
{
    if (g.job && g.job == job)
        ended(code);
}

/* The program on the VM thread has become an app, on a thread of its own: the debugger's stops are that
   thread's from here (interrupts off; before the thread first runs). */
void gdbp_became_app(struct thread *t, void *app, u32 job)
{
    if (target() == vm.thread && (g.attached || g.armed) && (!g.job || g.job == job) && (g.job || g.attached)) {
        g.thread = t;
        g.app = app;
    }
}

/* An app has ended (interrupts off): the debugger's, with its exit code. */
void gdbp_app_end(void *app, u32 code)
{
    if (g.app && g.app == app)
        ended(code);
}

/* ---- the ssh thread's side */

static void put_packet(const char *s)
{
    static u8 out[PKT + 8];
    u32 n = 0;
    u8 sum = 0;
    out[n++] = '$';
    for (; *s && n < PKT + 1; s++) {
        out[n++] = (u8)*s;
        sum += (u8)*s;
    }
    out[n++] = '#';
    out[n++] = (u8)hexd[sum >> 4];
    out[n++] = (u8)hexd[sum & 15];
    g.send(out, n);
}

static u32 parse_hex(const char **p)
{
    u32 v = 0;
    int d;
    while ((d = hexv(**p)) >= 0) {
        v = (v << 4) | (u32)d;
        (*p)++;
    }
    return v;
}

static char *hex32(char *o, u32 v)              /* as it lies in memory: low byte first */
{
    int i;
    for (i = 0; i < 4; i++, v >>= 8) {
        *o++ = hexd[(v >> 4) & 15];
        *o++ = hexd[v & 15];
    }
    return o;
}

static u32 unhex32(const char *p)
{
    u32 v = 0;
    int i;
    for (i = 0; i < 4; i++)
        v |= (u32)((hexv(p[i * 2]) << 4) | hexv(p[i * 2 + 1])) << (8 * i);
    return v;
}

/* The program's address a as a linear one, n bytes of it: 0 if any of that
   is the kernel's. */
static u8 *at(u32 a, u32 n)
{
    u32 lin = g.base + a;
    if (lin >= 0xC0000000u || n > 0xC0000000u - lin)
        return 0;
    return (u8 *)lin;
}

/* A copy to or from the program's memory, on the ssh thread: with the VM
   thread's context loaded for it (an app's thread may have run since the
   program stopped, and left its own: M6). */
static u32 prog_copy(void *dst, const void *src, u32 n)
{
    u32 f = irq_save(), r;
    struct dpmi_ctx *was = dctx;
    dpmi_ctx_load(target()->ctx);
    r = ucopy(dst, src, n);
    dpmi_ctx_load(was);
    irq_restore(f);
    return r;
}

/* eax ecx edx ebx esp ebp esi edi eip eflags cs ss ds es fs gs */
static u32 *reg(struct trapframe *tf, u32 n)
{
    switch (n) {
    case 0: return &tf->eax;
    case 1: return &tf->ecx;
    case 2: return &tf->edx;
    case 3: return &tf->ebx;
    case 4: return &tf->esp;
    case 5: return &tf->ebp;
    case 6: return &tf->esi;
    case 7: return &tf->edi;
    case 8: return &tf->eip;
    case 9: return &tf->eflags;
    case 10: return &tf->cs;
    case 11: return &tf->ss;
    case 12: return &tf->ds;
    case 13: return &tf->es;
    case 14: return &tf->fs;
    case 15: return &tf->gs;
    }
    return 0;
}

static void set_reg(struct trapframe *tf, u32 n, u32 v)
{
    if (n < 9)                                  /* the general registers and EIP; nothing else can be set */
        *reg(tf, n) = v;
    else if (n == 9)                            /* the flags a program may set itself */
        tf->eflags = (tf->eflags & ~(FL_ARITH | 0x400u)) | (v & (FL_ARITH | 0x400u));
}                                               /* (the segment registers stay) */

static void resume(int step)
{
    u32 f = irq_save();
    g.step = (u8)step;
    g.waiting = 1;
    g.news = 0;
    g.stopped = 0;
    vm_kick();
    thread_wake(&g.wq);
    irq_restore(f);
}

static void stop_reply(void)
{
    char s[8];
    ksnprintf(s, sizeof s, "S%02x", (u32)g.sig);
    put_packet(s);
    g.waiting = 0;
    g.news = 0;
}

static void command(char *p)
{
    static char out[PKT + 1];
    static u8 mem[PKT / 2];
    struct trapframe *tf = g.tf;
    const char *q = p + 1;
    u32 a, n, i;
    u8 *m;

    out[0] = 0;
    if (g.gone_news && *p != 'q' && *p != 'H') {        /* the program has gone */
        ksnprintf(out, sizeof out, "W%02x", (u32)g.gone);
        put_packet(out);
        g.gone_news = 0;
        return;
    }
    switch (*p) {
    case '?':
        if (g.stopped)
            stop_reply();
        else
            g.waiting = 1;                      /* when it stops */
        return;
    case 'g':
        if (!tf)
            break;
        for (i = 0, p = out; i < 16; i++)
            p = hex32(p, i >= 10 ? *reg(tf, i) & 0xFFFF : *reg(tf, i));
        *p = 0;
        break;
    case 'G':
        if (!tf)
            break;
        for (i = 0; i < 10 && q[i * 8] && q[i * 8 + 7]; i++)
            set_reg(tf, i, unhex32(q + i * 8));
        ksnprintf(out, sizeof out, "OK");
        break;
    case 'P':
        n = parse_hex(&q);
        if (!tf || *q++ != '=') {
            ksnprintf(out, sizeof out, "E01");
            break;
        }
        set_reg(tf, n, unhex32(q));             /* (one it hasn't, such as a Linux gdb's orig_eax, is let pass) */
        ksnprintf(out, sizeof out, "OK");
        break;
    case 'm':
        a = parse_hex(&q);
        n = *q++ == ',' ? parse_hex(&q) : 0;
        if (n > sizeof mem)
            n = sizeof mem;
        if (!tf || !(m = at(a, n)) || prog_copy(mem, m, n) != 0) {
            ksnprintf(out, sizeof out, "E14");
            break;
        }
        for (i = 0; i < n; i++) {
            out[i * 2] = hexd[mem[i] >> 4];
            out[i * 2 + 1] = hexd[mem[i] & 15];
        }
        out[n * 2] = 0;
        break;
    case 'M':
        a = parse_hex(&q);
        n = *q++ == ',' ? parse_hex(&q) : 0;
        if (!tf || *q++ != ':' || n > sizeof mem || !(m = at(a, n))) {
            ksnprintf(out, sizeof out, "E14");
            break;
        }
        for (i = 0; i < n && hexv(q[i * 2]) >= 0 && hexv(q[i * 2 + 1]) >= 0; i++)
            mem[i] = (u8)((hexv(q[i * 2]) << 4) | hexv(q[i * 2 + 1]));
        ksnprintf(out, sizeof out, i == n && prog_copy(m, mem, n) == 0 ? "OK" : "E14");
        break;
    case 'c':
    case 's':
        if (!g.stopped) {
            ksnprintf(out, sizeof out, "E01");
            break;
        }
        if (*q)
            tf->eip = parse_hex(&q);
        resume(*p == 's');
        return;                                 /* the answer is the next stop */
    case 'D':
        put_packet("OK");
        gdbp_detach();
        return;
    case 'k':
        if (g.app) {
            app_kill(g.app, "debugger");
        } else {
            vm.kill_reason = "agent";
            vm.kill_since = 0;
            vm.kill_req = 1;
        }
        gdbp_detach();
        return;
    case 'H':
        ksnprintf(out, sizeof out, "OK");
        break;
    case 'T':
        ksnprintf(out, sizeof out, "OK");
        break;
    case 'q':
        if (!strncmp(p, "qSupported", 10))
            ksnprintf(out, sizeof out, "PacketSize=%x", PKT - 16);
        else if (!strcmp(p, "qAttached"))
            ksnprintf(out, sizeof out, "1");
        else if (!strcmp(p, "qfThreadInfo"))
            ksnprintf(out, sizeof out, "m1");
        else if (!strcmp(p, "qsThreadInfo"))
            ksnprintf(out, sizeof out, "l");
        else if (!strcmp(p, "qC"))
            ksnprintf(out, sizeof out, "QC1");
        break;
    }
    put_packet(out);                            /* (empty: not supported) */
}

void gdbp_input(const u8 *d, u32 n)
{
    for (; n; n--, d++) {
        char c = (char)*d;
        switch (g.in_state) {
        case 0:
            if (c == '$') {
                g.in_state = 1;
                g.in_len = 0;
                g.sum = 0;
            } else if (c == 3) {                /* gdb's interrupt */
                u32 f = irq_save();
                if (!g.stopped) {
                    g.want_stop = 1;
                    g.waiting = 1;
                }
                irq_restore(f);
            }
            break;
        case 1:
            if (c == '#') {
                g.in_state = 2;
            } else if (g.in_len < PKT - 1) {
                g.in[g.in_len++] = c;
                g.sum += (u8)c;
            }
            break;
        case 2:
            g.got = (u8)(hexv(c) << 4);
            g.in_state = 3;
            break;
        case 3:
            g.got |= (u8)hexv(c);
            g.in_state = 0;
            g.in[g.in_len] = 0;
            if (g.got != g.sum) {
                g.send((const u8 *)"-", 1);
                break;
            }
            g.send((const u8 *)"+", 1);
            command(g.in);
            break;
        }
    }
}

void gdbp_turn(void)
{
    if (!g.attached)
        return;
    if (g.gone_news && g.waiting) {
        char s[8];
        ksnprintf(s, sizeof s, "W%02x", (u32)g.gone);
        put_packet(s);
        g.gone_news = 0;
        g.waiting = 0;
    } else if (g.news && g.waiting && g.stopped) {
        stop_reply();
    }
}

void gdbp_arm(void (*wake)(void))
{
    u32 f = irq_save();
    g.wake = wake;
    g.armed = 1;
    irq_restore(f);
}

int gdbp_armed(void) { return g.armed || g.stopped || g.attached; }

int gdbp_attach(void (*send)(const u8 *d, u32 n))
{
    if (g.attached || !(g.armed || g.stopped))
        return -1;
    g.send = send;
    g.in_state = 0;
    g.waiting = g.gone_news = g.gone_told = 0;
    g.attached = 1;
    return 0;
}

void gdbp_detach(void)
{
    u32 f = irq_save();
    g.attached = g.armed = g.want_stop = g.step = g.waiting = 0;
    g.news = 0;
    g.stopped = 0;                              /* the program runs on */
    g.wake = 0;
    g.job = 0;
    vm_kick();
    thread_wake(&g.wq);
    g.thread = 0;
    g.app = 0;
    irq_restore(f);
}

/* API 2.0's DEBUG. A job is to stop at its program's first 32-bit code (JOB's RUN with GLOS_RUNF_DEBUG, as the
   job begins: the VM thread, interrupts off): it waits there for its debugger. -1: the debugger has another. */
int gdbp_arm_job(u32 job)
{
    if (g.attached && g.job == job && target() == vm.thread) {  /* (its debugger attached before it began) */
        g.armed = 1;
        return 0;
    }
    if (g.attached || g.armed || g.stopped)
        return -1;
    g.job = job;
    g.thread = 0;
    g.app = 0;
    g.wake = 0;
    g.based = 0;
    g.armed = 1;
    return 0;
}

/* ... and its debugger attaches: to the job stopped at its start, or to one that runs (then `t` and `app` say
   whose thread, if it has become an app; it stops when asked to). 0, or -1: the debugger has another. */
int gdbp_attach_job(u32 job, struct thread *t, void *app, void (*send)(const u8 *d, u32 n), void (*wake)(void))
{
    u32 f = irq_save();
    if (g.attached || ((g.armed || g.stopped) && g.job != job)) {
        irq_restore(f);
        return -1;
    }
    if (g.job != job) {                         /* (one that runs: nothing was armed for it) */
        g.job = job;
        g.thread = t;
        g.app = app;
        g.based = 0;
    }
    g.send = send;
    g.wake = wake;
    g.in_state = 0;
    g.waiting = g.gone_news = g.gone_told = 0;
    g.attached = 1;
    irq_restore(f);
    return 0;
}

/* The job's owner has let go of it without attaching (its handle closed, the owner gone): it runs on. */
void gdbp_forget_job(u32 job)
{
    if (g.job == job && !g.attached)
        gdbp_detach();
}
