/* Native machines (ndm.h; docs/v1/native-dos.md, native-DOS sessions, NS1-NS2): GLOS's second kind of V86
 * machine, with no DOS and no BIOS under it.
 *
 * Its memory is its own: 640 KB of fresh frames from address 0 behind a page table of its own in its own address
 * space (mm_space_new_low), loaded while its thread runs (TU_NDOS: dpmi/host.c's user_switch). Nothing is at
 * A0000h and above: a program that reaches there faults and its machine ends, said in the log. The kernel reaches
 * that memory through the machine's window (mm_ndm_map: NDM_BASE) from any thread, so that a disk's I/O done by
 * the VM thread lands there; vm_ptr stays the system VM's.
 *
 * Every vector of its table points at an ARPL in its DOS area at 0060:0000 (two bytes a vector), so an INT that
 * goes through the table (VME, a vector not trapped) raises #UD at 0060:2n; one that traps (the global bitmap's
 * vectors, or every one without VME) raises #GP at the INT. Either way GLOS answers it here: INT 20h and 21h are
 * GLOS's own DOS's (kernel/ndos/dos21.c: files, folders, searches, memory, EXEC, the machine's clock), answered
 * with interrupts on (a disk's I/O is the VM thread's work, waited for); INT 10h's teletype and mode and INT 1Ah's
 * ticks are the BIOS's few here (NS3 is the rest), INT 2Fh 1680h gives the machine's time slice up; anything else is logged ("GLOS-NDOS unanswered") and answered
 * as MS-DOS 7.1 answers a function it hasn't got. No port is the machine's (IN reads all ones, OUT goes
 * nowhere), no IRQ is delivered to it, and its CLI, STI, PUSHF, POPF and IRET (when they trap) keep a virtual IF
 * of its own. */
#include <string.h>

#include "arch.h"
#include "dpmi.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "ndm.h"
#include "sched.h"
#include "v86dec.h"
#include "vm.h"
#include "clock.h"
#include "../ndos/dos21.h"

#define LOW_PAGES 160                   /* 0-9FFFFh */
#define LOW_END   0xA0000u
#define STUB_SEG  0x0060u               /* the ARPLs every vector points at: 0060:0000-01FFh */
#define OUT_RING  8192u
#define EFL_VM    0x00020000u
#define EFL_IF    0x00000200u
#define EFL_CF    0x00000001u

void vm_enter(struct trapframe *tf) __attribute__((noreturn));

struct ndm {
    u32 space;
    u32 frame[LOW_PAGES];
    u32 slot;                           /* its memory's window in the kernel (mm_ndm_map) */
    u8 *mem;
    struct dos_m dos;
    struct thread *t;
    struct trapframe start;
    char out[OUT_RING];
    u32 head, tail, lost;
    volatile int done;
    int code, gone;
    u8 vif;
    void (*kick)(void);
    char name[16];
    u32 unanswered, ports;
};

static struct ndm *machine(void) { return current->ctx; }

/* ---- its memory, through its window from any thread (what's past A0000h reads 0 and takes no write) */

static u8 rd8(u32 lin) { return lin < LOW_END ? machine()->mem[lin] : 0; }
static u32 rd16(u32 lin) { return rd8(lin) | (u32)rd8(lin + 1) << 8; }

static void wr8(u32 lin, u32 v)
{
    if (lin < LOW_END)
        machine()->mem[lin] = (u8)v;
}

static void wr16(u32 lin, u32 v)
{
    wr8(lin, v);
    wr8(lin + 1, v >> 8);
}

static u32 lin_of(u32 seg, u32 off) { return ((seg & 0xFFFF) << 4) + (off & 0xFFFF); }

static void push16(struct trapframe *tf, u32 v)
{
    tf->esp = (tf->esp & 0xFFFF0000u) | ((tf->esp - 2) & 0xFFFF);
    wr16(lin_of(tf->ss, tf->esp), v);
}

static u32 pop16(struct trapframe *tf)
{
    u32 v = rd16(lin_of(tf->ss, tf->esp));

    tf->esp = (tf->esp & 0xFFFF0000u) | ((tf->esp + 2) & 0xFFFF);
    return v;
}

/* ---- the windows */

static u8 slot_used[NDM_SLOTS];

static int slot_take(struct ndm *m)
{
    u32 i, f = irq_save();

    for (i = 0; i < NDM_SLOTS && slot_used[i]; i++)
        ;
    if (i < NDM_SLOTS)
        slot_used[i] = 1;
    irq_restore(f);
    if (i == NDM_SLOTS)
        return -1;
    m->slot = i;
    m->mem = mm_ndm_map(i, m->frame, LOW_PAGES);
    return 0;
}

static void slot_free(struct ndm *m)
{
    if (!m->mem)
        return;
    mm_ndm_unmap(m->slot);
    m->mem = 0;
    slot_used[m->slot] = 0;
}

/* ---- what it writes */

static void say(struct ndm *m, const char *s, u32 n)
{
    u32 i;

    for (i = 0; i < n; i++) {
        if (m->head - m->tail >= OUT_RING) {
            m->lost++;
            continue;
        }
        m->out[m->head++ % OUT_RING] = s[i];
    }
    if (m->kick)
        m->kick();
}

/* ---- its end: every file it left open closed (that may write, so with interrupts on), its memory freed */

static void end(struct ndm *m, int code, const char *why)
{
    kprintf("GLOS-NDOS end prog=%s code=%d why=%s unanswered=%u lost=%u\n", m->name, code, why, m->unanswered,
            m->lost);
    sti();
    dos_m_close(&m->dos);
    cli();
    fpu_drop(current);                          /* (its FPU state goes with its thread) */
    dpmi_native_leave();                        /* (the kernel's space, before its own goes) */
    slot_free(m);
    mm_space_free_low(m->space);
    m->space = 0;
    m->code = code;
    m->done = 1;
    if (m->gone)
        kfree(m);
    else if (m->kick)
        m->kick();
    thread_exit();
}

/* ---- INT n: GLOS's own DOS (kernel/ndos/dos21.c); a little of the BIOS's (NS3 is the rest) */

static void unanswered(struct ndm *m, struct trapframe *tf, u32 n)
{
    if (m->unanswered++ < 20)
        kprintf("GLOS-NDOS unanswered int=%02x ax=%04x prog=%s\n", n, tf->eax & 0xFFFF, m->name);
}

#define LO16(x, v) ((x) = ((x) & 0xFFFF0000u) | ((v) & 0xFFFF))

static void to_regs(const struct trapframe *tf, struct dos_regs86 *r)
{
    r->ax = tf->eax & 0xFFFF, r->bx = tf->ebx & 0xFFFF, r->cx = tf->ecx & 0xFFFF, r->dx = tf->edx & 0xFFFF;
    r->si = tf->esi & 0xFFFF, r->di = tf->edi & 0xFFFF, r->bp = tf->ebp & 0xFFFF;
    r->ds = tf->v86_ds & 0xFFFF, r->es = tf->v86_es & 0xFFFF;
    r->cs = tf->cs & 0xFFFF, r->ip = tf->eip & 0xFFFF, r->ss = tf->ss & 0xFFFF, r->sp = tf->esp & 0xFFFF;
    r->flags = tf->eflags & 0xFFFF;
}

static void from_regs(struct trapframe *tf, const struct dos_regs86 *r)
{
    LO16(tf->eax, r->ax), LO16(tf->ebx, r->bx), LO16(tf->ecx, r->cx), LO16(tf->edx, r->dx);
    LO16(tf->esi, r->si), LO16(tf->edi, r->di), LO16(tf->ebp, r->bp);
    tf->v86_ds = r->ds & 0xFFFF, tf->v86_es = r->es & 0xFFFF;
    tf->cs = r->cs & 0xFFFF, tf->eip = r->ip & 0xFFFF, tf->ss = r->ss & 0xFFFF;
    LO16(tf->esp, r->sp);
    tf->eflags = (tf->eflags & ~0x0FD5u) | (r->flags & 0x0FD5u);
}

static void dos(struct ndm *m, struct trapframe *tf, u32 n)
{
    struct dos_regs86 r;
    u32 ah = (tf->eax >> 8) & 0xFF;
    int res;

    to_regs(tf, &r);
    sti();                                      /* (its disk's I/O is the VM thread's work: it waits for it) */
    res = n == 0x20 ? dos20(&m->dos, &r) : dos21(&m->dos, &r);
    cli();
    if (res == DOS21_END)
        end(m, (int)(m->dos.exit_code & 0xFF), n == 0x20 ? "int20" : ah == 0x4C ? "int21-4c" : "int21-00");
    if (res == DOS21_UNANSWERED)
        unanswered(m, tf, n);
    if (res == DOS21_SWITCH)
        m->vif = 1;
    from_regs(tf, &r);
}

static void video(struct ndm *m, struct trapframe *tf)
{
    u32 ah = (tf->eax >> 8) & 0xFF;
    char c;

    switch (ah) {
    case 0x0E:                                  /* teletype: the character to what it writes */
        c = (char)(tf->eax & 0xFF);
        say(m, &c, 1);
        return;
    case 0x0F:                                  /* mode 3, 80 columns, page 0 */
        LO16(tf->eax, 0x5003);
        tf->ebx &= 0xFFFF00FFu;
        return;
    case 0x03:                                  /* the cursor: at 0,0, lines 6-7 */
        LO16(tf->ecx, 0x0607);
        LO16(tf->edx, 0);
        return;
    }
    unanswered(m, tf, 0x10);
}

static void clock_int(struct ndm *m, struct trapframe *tf)
{
    if (((tf->eax >> 8) & 0xFF) == 0x00) {      /* ticks since midnight (18.2065 a second), no midnight passed */
        u32 ticks = clock_local() % 86400u * 18206u / 1000u;

        LO16(tf->ecx, ticks >> 16);
        LO16(tf->edx, ticks);
        tf->eax &= 0xFFFFFF00u;
        return;
    }
    unanswered(m, tf, 0x1A);
}

static void answer(struct ndm *m, struct trapframe *tf, u32 n)
{
    if (n == 0x20 || n == 0x21)
        dos(m, tf, n);
    else if (n == 0x10)
        video(m, tf);
    else if (n == 0x1A)
        clock_int(m, tf);
    else if (n == 0x2F && (tf->eax & 0xFFFF) == 0x1680) {
        tf->eax &= 0xFFFFFF00u;                 /* (idle: its time slice given up) */
        sti();
        thread_yield();
        cli();
    } else
        unanswered(m, tf, n);                   /* (INT 2Fh's install checks: none installed, as plain DOS) */
}

/* ---- the traps */

void ndm_exception(struct trapframe *tf)
{
    struct ndm *m = machine();
    struct v86insn in;
    u8 code[V86_MAX_LEN];
    u32 ip = tf->eip & 0xFFFF, i, f;

    if (tf->vec == 6 && (tf->cs & 0xFFFF) == STUB_SEG && ip < 512 && !(ip & 1)) {
        /* Through the table: the INT's IP, CS and FLAGS on its stack; the answer's arithmetic flags kept. */
        u32 n = ip / 2;

        tf->eip = pop16(tf);
        tf->cs = pop16(tf);
        f = pop16(tf);
        tf->eflags = (tf->eflags & ~0xFFFu) | (f & 0xFFF & ~EFL_IF) | EFL_IF;
        m->vif = (f & EFL_IF) != 0;
        answer(m, tf, n);
        return;
    }
    if (tf->vec == 13) {
        for (i = 0; i < V86_MAX_LEN; i++)
            code[i] = rd8(lin_of(tf->cs, ip + i));
        switch (v86_decode(code, V86_MAX_LEN, &in)) {
        case V86_INT:
            tf->eip = (tf->eip + in.len) & 0xFFFF;
            answer(m, tf, in.imm);
            return;
        case V86_CLI:
        case V86_STI:
            m->vif = in.kind == V86_STI;
            tf->eip = (tf->eip + in.len) & 0xFFFF;
            return;
        case V86_PUSHF:
            push16(tf, (tf->eflags & 0xFFFF & ~EFL_IF) | (m->vif ? EFL_IF : 0));
            tf->eip = (tf->eip + in.len) & 0xFFFF;
            return;
        case V86_POPF:
            f = pop16(tf);
            m->vif = (f & EFL_IF) != 0;
            tf->eflags = (tf->eflags & ~0x0FD5u) | (f & 0x0FD5u) | EFL_IF;
            tf->eip = (tf->eip + in.len) & 0xFFFF;
            return;
        case V86_IRET:
            tf->eip = pop16(tf);
            tf->cs = pop16(tf);
            f = pop16(tf);
            m->vif = (f & EFL_IF) != 0;
            tf->eflags = (tf->eflags & ~0x0FD5u) | (f & 0x0FD5u) | EFL_IF;
            return;
        case V86_HLT:
            tf->eip = (tf->eip + in.len) & 0xFFFF;
            sti();
            thread_yield();
            cli();
            return;
        case V86_IN:
        case V86_INS:
            if (m->ports++ < 8)
                kprintf("GLOS-NDOS port in prog=%s port=%04x\n", m->name, in.port_dx ? tf->edx & 0xFFFF : in.imm);
            if (in.kind == V86_IN)
                tf->eax |= in.width == 1 ? 0xFF : in.width == 2 ? 0xFFFF : 0xFFFFFFFFu;
            tf->eip = (tf->eip + in.len) & 0xFFFF;
            return;
        case V86_OUT:
        case V86_OUTS:
            if (m->ports++ < 8)
                kprintf("GLOS-NDOS port out prog=%s port=%04x\n", m->name, in.port_dx ? tf->edx & 0xFFFF : in.imm);
            tf->eip = (tf->eip + in.len) & 0xFFFF;
            return;
        default:
            break;
        }
    }
    kprintf("GLOS-NDOS fault prog=%s vec=%u cs:ip=%04x:%04x err=%x cr2=%x\n", m->name, tf->vec, tf->cs & 0xFFFF, ip,
            tf->err, tf->vec == 14 ? read_cr2() : 0);
    end(m, 255, "fault");
}

void ndm_return(struct trapframe *tf)
{
    (void)tf;                                   /* (no IRQ is a native machine's yet) */
}

/* ---- starting one */

static void ndm_main(void *arg)
{
    struct ndm *m = arg;
    struct trapframe *tf = (struct trapframe *)(current->stack_top - sizeof *tf);

    cli();
    *tf = m->start;
    vm_enter(tf);
}

static void dos_out(void *ctx, const char *s, unsigned n) { say(ctx, s, n); }

static unsigned long dos_now(void *ctx)
{
    u32 local = clock_local();

    (void)ctx;
    return local > 315532800u ? local - 315532800u : 0;        /* (seconds since 1980) */
}

struct ndm *ndm_start(struct fsv_vol *v, const char *path, const u8 *image, u32 len, const char *name,
                      const char *tail, void (*kick)(void), int *dos_err)
{
    struct dos_regs86 r;
    struct ndm *m;
    u32 i;
    int e;

    *dos_err = 0x08;
    if (!(m = kmalloc(sizeof *m)))
        return 0;
    memset(m, 0, sizeof *m);
    m->kick = kick;
    m->vif = 1;
    for (i = 0; name[i] && i < sizeof m->name - 1; i++)
        m->name[i] = name[i];
    if (!(m->space = mm_space_new_low(m->frame, LOW_PAGES))) {
        kfree(m);
        return 0;
    }
    if (slot_take(m) != 0) {
        mm_space_free_low(m->space);
        kfree(m);
        return 0;
    }
    /* The vector table, every vector at its ARPL (ARPL AX, AX: #UD in V86 mode); then DOS. */
    for (i = 0; i < 256; i++) {
        m->mem[i * 4] = (u8)(2 * i), m->mem[i * 4 + 1] = (u8)((2 * i) >> 8);
        m->mem[i * 4 + 2] = (u8)STUB_SEG, m->mem[i * 4 + 3] = (u8)(STUB_SEG >> 8);
        m->mem[STUB_SEG * 16 + 2 * i] = 0x63, m->mem[STUB_SEG * 16 + 2 * i + 1] = 0xC0;
    }
    dos_m_init(&m->dos, m->mem, LOW_END, v, 2, 437);
    m->dos.out = dos_out, m->dos.now = dos_now, m->dos.ctx = m;
    e = image ? dos_m_start_image(&m->dos, name, image, len, tail ? tail : "", &r)
              : dos_m_start(&m->dos, path, tail ? tail : "", &r);
    if (e) {
        kprintf("GLOS-NDOS refused prog=%s dos-error=%02x\n", m->name, e);
        *dos_err = e;
        slot_free(m);
        mm_space_free_low(m->space);
        kfree(m);
        return 0;
    }
    memset(&m->start, 0, sizeof m->start);
    m->start.eax = r.ax, m->start.ebx = r.bx, m->start.ecx = r.cx, m->start.edx = r.dx;
    m->start.esi = r.si, m->start.edi = r.di, m->start.ebp = r.bp;
    m->start.eip = r.ip, m->start.cs = r.cs, m->start.esp = r.sp, m->start.ss = r.ss;
    m->start.v86_ds = r.ds, m->start.v86_es = r.es;
    m->start.eflags = EFL_VM | EFL_IF | 2;
    if (!(m->t = thread_new("ndos", PRIO_NORMAL, ndm_main, m, TU_NDOS))) {
        dos_m_close(&m->dos);
        slot_free(m);
        mm_space_free_low(m->space);
        kfree(m);
        return 0;
    }
    m->t->ctx = m;
    m->t->space = m->space;
    m->t->vif = 1;
    fpu_fork(m->t);
    kprintf("GLOS-NDOS start prog=%s psp=%04x cs:ip=%04x:%04x\n", m->name, m->dos.psp, r.cs, r.ip);
    {
        u32 fl = irq_save();

        thread_go(m->t);
        irq_restore(fl);
    }
    *dos_err = 0;
    return m;
}

u32 ndm_read(struct ndm *m, char *buf, u32 room)
{
    u32 n = 0, f = irq_save();

    while (n < room && m->tail != m->head)
        buf[n++] = m->out[m->tail++ % OUT_RING];
    irq_restore(f);
    return n;
}

int ndm_done(const struct ndm *m, int *code)
{
    if (!m->done)
        return 0;
    *code = m->code;
    return 1;
}

void ndm_free(struct ndm *m)
{
    u32 f = irq_save();

    if (m->done) {
        irq_restore(f);
        kfree(m);
        return;
    }
    m->gone = 1;                                /* (its end frees it) */
    m->kick = 0;
    irq_restore(f);
}

/* HELLO: "Hello from GLOS's own DOS", then '!' by 02h, then 30h's version shown as "7.10", then 4Ch with 42. */
const u8 ndm_hello[] = {
    0xB4, 0x09, 0xBA, 0x25, 0x01,               /* mov ah,9; mov dx,msg */
    0xCD, 0x21,                                 /* int 21h */
    0xB4, 0x02, 0xB2, 0x21, 0xCD, 0x21,         /* mov ah,2; mov dl,'!'; int 21h */
    0xB4, 0x30, 0xCD, 0x21,                     /* mov ah,30h; int 21h: AL major, AH minor */
    0x04, 0x30, 0x88, 0xC2, 0xB4, 0x02, 0xCD, 0x21,     /* add al,'0'; mov dl,al; mov ah,2; int 21h */
    0xB4, 0x09, 0xBA, 0x41, 0x01, 0xCD, 0x21,   /* mov ah,9; mov dx,ver; int 21h */
    0xB8, 0x2A, 0x4C, 0xCD, 0x21,               /* mov ax,4C2Ah; int 21h */
    /* 0125h: msg */
    'H', 'e', 'l', 'l', 'o', ' ', 'f', 'r', 'o', 'm', ' ', 'G', 'L', 'O', 'S', '\'', 's', ' ', 'o', 'w', 'n', ' ',
    'D', 'O', 'S', '\r', '\n', '$',
    /* 0141h: ver */
    '.', '1', '0', '\r', '\n', '$',
};
const u32 ndm_hello_len = sizeof ndm_hello;
