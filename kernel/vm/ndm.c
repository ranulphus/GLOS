/* Native machines (ndm.h; docs/v1/native-dos.md, native-DOS sessions, NS1): GLOS's second kind of V86 machine,
 * with no DOS and no BIOS under it.
 *
 * Its memory is its own: 640 KB of fresh frames from address 0 behind a page table of its own in its own address
 * space (mm_space_new_low), loaded while its thread runs (TU_NDOS: dpmi/host.c's user_switch). Nothing is at
 * A0000h and above: a program that reaches there faults and its machine ends, said in the log. The kernel's
 * accesses here are to that memory, at 0, from its own thread; vm_ptr stays the system VM's.
 *
 * Every vector of its table points at an ARPL in its DOS area at 0060:0000 (two bytes a vector), so an INT that
 * goes through the table (VME, a vector not trapped) raises #UD at 0060:2n; one that traps (the global bitmap's
 * vectors, or every one without VME) raises #GP at the INT. Either way GLOS answers it here: NS1 answers INT 21h
 * 02h, 09h, 30h and 4Ch and INT 20h; anything else is logged ("GLOS-NDOS unanswered") and answered as MS-DOS 7.1
 * answers a function it hasn't got. No port is the machine's (IN reads all ones, OUT goes nowhere), no IRQ is
 * delivered to it, and its CLI, STI, PUSHF, POPF and IRET (when they trap) keep a virtual IF of its own. */
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
#include "../ndos/exe.h"
#include "../ndos/psp.h"

#define LOW_PAGES 160                   /* 0-9FFFFh */
#define LOW_END   0xA0000u
#define STUB_SEG  0x0060u               /* the ARPLs every vector points at: 0060:0000-01FFh */
#define PSP_SEG   0x0100u               /* the program's PSP (its memory up to A000h) */
#define OUT_RING  8192u
#define EFL_VM    0x00020000u
#define EFL_IF    0x00000200u
#define EFL_CF    0x00000001u

void vm_enter(struct trapframe *tf) __attribute__((noreturn));

struct ndm {
    u32 space;
    u32 frame[LOW_PAGES];
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

/* ---- its memory, from its own thread (the kernel never faults on a program's address: what's past A0000h reads
   0 and takes no write) */

static u8 *at(u32 lin)
{
    u8 *p;

    __asm__("" : "=r"(p) : "0"(lin));
    return p;
}

static u8 rd8(u32 lin) { return lin < LOW_END ? *at(lin) : 0; }
static u32 rd16(u32 lin) { return rd8(lin) | (u32)rd8(lin + 1) << 8; }

static void wr8(u32 lin, u32 v)
{
    if (lin < LOW_END)
        *at(lin) = (u8)v;
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

/* ... and from the thread that makes it, before it runs: through its frames. */
static void put(struct ndm *m, u32 lin, const void *src, u32 n)
{
    const u8 *s = src;

    while (n) {
        u32 page = lin >> 12, off = lin & 0xFFF, k = 4096 - off;
        u8 *p;

        if (page >= LOW_PAGES)
            return;
        if (k > n)
            k = n;
        p = kmap(m->frame[page]);
        memcpy(p + off, s, k);
        kunmap(p);
        lin += k, s += k, n -= k;
    }
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

/* ---- its end */

static void end(struct ndm *m, int code, const char *why)
{
    kprintf("GLOS-NDOS end prog=%s code=%d why=%s unanswered=%u lost=%u\n", m->name, code, why, m->unanswered,
            m->lost);
    cli();
    fpu_drop(current);                          /* (its FPU state goes with its thread) */
    dpmi_native_leave();                        /* (the kernel's space, before its own goes) */
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

/* ---- INT n: GLOS's own DOS (NS1's few calls) */

static void unanswered(struct ndm *m, struct trapframe *tf, u32 n)
{
    u32 ah = (tf->eax >> 8) & 0xFF;

    if (m->unanswered++ < 20)
        kprintf("GLOS-NDOS unanswered int=%02x ax=%04x prog=%s\n", n, tf->eax & 0xFFFF, m->name);
    if (n == 0x21) {                            /* (MS-DOS 7.1's way with a function it hasn't got) */
        tf->eax &= 0xFFFFFF00u;
        if (ah >= 0x2F) {
            tf->eax = (tf->eax & 0xFFFF0000u) | 0x0001;
            tf->eflags |= EFL_CF;
        }
    }
}

static void answer(struct ndm *m, struct trapframe *tf, u32 n)
{
    u32 ah = (tf->eax >> 8) & 0xFF, i;

    if (n == 0x20)
        end(m, 0, "int20");
    if (n != 0x21) {
        unanswered(m, tf, n);
        return;
    }
    switch (ah) {
    case 0x00:
        end(m, 0, "int21-00");
        break;
    case 0x02: {                                /* a character */
        char c = (char)(tf->edx & 0xFF);

        say(m, &c, 1);
        tf->eax = (tf->eax & 0xFFFFFF00u) | (u8)c;
        break;
    }
    case 0x09: {                                /* a string to '$' */
        u32 at0 = lin_of(tf->v86_ds, tf->edx);
        char c;

        for (i = 0; i < 65536 && (c = (char)rd8(at0 + i)) != '$'; i++)
            say(m, &c, 1);
        tf->eax = (tf->eax & 0xFFFFFF00u) | '$';
        break;
    }
    case 0x30:                                  /* MS-DOS 7.10 (the plan's identity), OEM FFh */
        tf->eax = (tf->eax & 0xFFFF0000u) | 0x0A07;
        tf->ebx = (tf->ebx & 0xFFFF0000u) | 0xFF00;
        tf->ecx &= 0xFFFF0000u;
        break;
    case 0x4C:
        end(m, (int)(tf->eax & 0xFF), "int21-4c");
        break;
    default:
        unanswered(m, tf, n);
    }
}

/* ---- the traps */

static struct ndm *machine(void) { return current->ctx; }

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

struct ndm *ndm_start(const u8 *image, u32 len, const char *name, const char *tail, void (*kick)(void))
{
    static const u8 arpl[2] = { 0x63, 0xC0 };   /* ARPL AX, AX: #UD in V86 mode */
    u8 psp[256];
    u32 ivt[256], i;
    struct dos_exe x;
    struct dos_regs0 r;
    struct dos_psp_init pin;
    struct ndm *m;

    if (len > 0xFF00 - 0x100)
        return 0;
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
    /* The vector table, every vector at its ARPL; the ARPLs. */
    for (i = 0; i < 256; i++)
        ivt[i] = STUB_SEG << 16 | (2 * i);
    put(m, 0, ivt, sizeof ivt);
    for (i = 0; i < 256; i++)
        put(m, STUB_SEG * 16 + 2 * i, arpl, 2);
    /* Its PSP, its image at PSP:0100h, the word 0000h on top of its stack (a RET ends it through PSP:0000h). */
    memset(&pin, 0, sizeof pin);
    pin.seg = PSP_SEG;
    pin.top = LOW_END >> 4;
    pin.parent = PSP_SEG;
    pin.version = 0x0A07;
    pin.tail = tail ? tail : "";
    dos_psp_make(psp, &pin);
    put(m, PSP_SEG * 16, psp, sizeof psp);
    put(m, PSP_SEG * 16 + 0x100, image, len);
    dos_exe_header(image, len, len, &x);
    dos_exe_regs(&x, PSP_SEG, (LOW_END >> 4) - PSP_SEG, 1, 1, &r);
    if (r.push0) {
        static const u8 zero[2] = { 0, 0 };

        put(m, PSP_SEG * 16 + r.sp, zero, 2);
    }
    memset(&m->start, 0, sizeof m->start);
    m->start.eax = r.ax;
    m->start.eip = r.ip;
    m->start.cs = r.cs;
    m->start.esp = r.sp;
    m->start.ss = r.ss;
    m->start.v86_ds = r.ds;
    m->start.v86_es = r.es;
    m->start.eflags = EFL_VM | EFL_IF | 2;
    if (!(m->t = thread_new("ndos", PRIO_NORMAL, ndm_main, m, TU_NDOS))) {
        mm_space_free_low(m->space);
        kfree(m);
        return 0;
    }
    m->t->ctx = m;
    m->t->space = m->space;
    m->t->vif = 1;
    fpu_fork(m->t);
    kprintf("GLOS-NDOS start prog=%s bytes=%u psp=%04x\n", m->name, len, PSP_SEG);
    {
        u32 fl = irq_save();

        thread_go(m->t);
        irq_restore(fl);
    }
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
