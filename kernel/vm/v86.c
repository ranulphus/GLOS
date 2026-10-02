/* The system VM's monitor (supervisor.md §9). GLOS.EXE comes back from
 * pm_enter in virtual-8086 mode at IOPL 0, and from then on the kernel runs
 * only in traps from it:
 *   #GP     CLI, STI, PUSHF, POPF, INT n, IRET, HLT and I/O to trapped ports,
 *           emulated against the virtual interrupt flag and the virtual devices;
 *   #UD     ARPL: GLOS.EXE's calls (glos_call) and its XMS entry point;
 *           otherwise reflected as INT 06h, like the other exceptions a
 *           real-mode CPU raises;
 *   IRQs    the physical line is masked and passed to the virtual PIC, and
 *           unmasked when the program's EOI ends it there.
 * Before every IRET to V86 mode, vm_return() delivers the highest pending
 * virtual IRQ when the virtual IF allows, and carries out a kill. M2 has no
 * threads and no VME: every software INT traps. */
#include "glos/bootinfo.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "timer.h"
#include "v86dec.h"
#include "vm.h"

#define KERNEL_LINES ((1u << 1) | (1u << 2) | (1u << 8) | (1u << 12))  /* keyboard, cascade, clock, AUX */

struct vm vm;
static struct trapframe start_frame;
static u32 reset_reqs;

extern void vm_enter(struct trapframe *tf) __attribute__((noreturn));

/* ---- the V86 stack (16-bit SP, wrapping within SS) */

static void push16(struct trapframe *tf, u32 v)
{
    u32 sp = (tf->esp - 2) & 0xFFFF;
    vm_wr8(vm_lin(tf->ss, sp), (u8)v);
    vm_wr8(vm_lin(tf->ss, sp + 1), (u8)(v >> 8));
    SET16(tf->esp, sp);
}

static void push32(struct trapframe *tf, u32 v)
{
    push16(tf, v >> 16);
    push16(tf, v);
}

static u32 pop16(struct trapframe *tf)
{
    u32 sp = tf->esp & 0xFFFF;
    u32 v = vm_rd8(vm_lin(tf->ss, sp)) | ((u32)vm_rd8(vm_lin(tf->ss, sp + 1)) << 8);
    SET16(tf->esp, sp + 2);
    return v;
}

static u32 pop32(struct trapframe *tf)
{
    u32 lo = pop16(tf);
    return lo | (pop16(tf) << 16);
}

/* ---- FLAGS as the program sees them: the real arithmetic flags, TF, AC and
   ID; the virtual IF; IOPL and NT as last written (a 386 in real mode keeps
   them, which CPU detection code checks). VM, IF and IOPL 0 stay real. */

static u32 flags_image(const struct trapframe *tf)
{
    return (tf->eflags & (FL_ARITH | FL_TF | FL_AC | FL_ID)) | (vm.vif ? FL_IF : 0) | vm.vflags_hi | 2;
}

static void flags_apply(struct trapframe *tf, u32 f, int wide)
{
    u32 keep = wide ? (FL_ARITH | FL_TF | FL_AC | FL_ID) : (FL_ARITH | FL_TF);
    tf->eflags = (tf->eflags & ~keep) | (f & keep);
    vm.vif = (f & FL_IF) != 0;
    vm.vflags_hi = f & FL_HI;
}

/* ---- interrupts */

void vm_int(struct trapframe *tf, u8 n, u32 ret_ip)
{
    u32 v = vm_rd32(n * 4u);
    push16(tf, flags_image(tf));
    push16(tf, tf->cs);
    push16(tf, ret_ip);
    vm.vif = 0;
    tf->eflags &= ~(FL_TF | FL_AC);
    tf->cs = v >> 16;
    tf->eip = v & 0xFFFF;
}

void vm_idle(u32 until)
{
    for (;;) {
        if (vm.kill_req || (vm.vif && vpic_pending(&vm.pic)))
            return;
        if (until && (s32)(timer_ticks() - until) >= 0)
            return;
        __asm__ volatile("sti; hlt; cli" ::: "memory");
    }
}

void vm_sync_mask(void)
{
    u16 m = (u16)((vpic_imr(&vm.pic) | vm.pic.inflight) & ~KERNEL_LINES);
    if (m != vm.phys_mask) {
        vm.phys_mask = m;
        pic_set_mask(m);
    }
}

/* A physical IRQ for the VM: masked until the program's EOI (so a level-
   triggered device cannot storm), then raised on the virtual PIC. */
static void vm_irq_line(struct trapframe *tf)
{
    int irq = (int)(tf->vec - IRQ_BASE_MASTER);
    vm.phys_mask |= (u16)(1u << irq);
    pic_set_mask(vm.phys_mask);
    vpic_raise_hw(&vm.pic, irq);
}

void vm_set_a20(int on)
{
    vm.a20 = on ? 1 : 0;
    mm_set_a20(vm.a20);
}

void vm_reset_req(const char *source)
{
    if (reset_reqs++ < 8)
        kprintf("GLOS-RESET-REQ source=%s\n", source);
}

/* ---- DOS: the current program, and what it found when it started */

u16 vm_current_psp(void) { return vm.sda ? vm_rd16(vm.sda + 0x10) : 0; }

/* INT 21h 4B00h/4B01h: the snapshot a kill of the coming child restores. A
   parent has one child at a time, so an older snapshot taken for the same
   parent (and any above it) belongs to programs that have ended. */
static void vm_exec_snap(void)
{
    u16 p = vm_current_psp();
    struct vm_snap *s;
    int i, j;

    if (!p)
        return;
    for (i = 0; i < SNAP_LEVELS && vm.snap[i].parent_psp; i++)
        if (vm.snap[i].parent_psp == p)
            break;
    for (j = i; j < SNAP_LEVELS; j++)
        vm.snap[j].parent_psp = 0;
    if (i == SNAP_LEVELS) {                     /* full: forget the oldest */
        memcpy(&vm.snap[0], &vm.snap[1], sizeof vm.snap[0] * (SNAP_LEVELS - 1));
        i = SNAP_LEVELS - 1;
    }
    s = &vm.snap[i];
    s->parent_psp = p;
    memcpy(s->ivt, vm_ptr(0), sizeof s->ivt);
    s->pic = vm.pic;
    s->a20 = vm.a20;
    s->rtc_a = vm.rtc_a;
    s->rtc_b = vm.rtc_b;
    s->kbc_cmd = vkbc_cmd();
}

/* The kill (Ctrl-Alt-Shift-Esc): the current program's interrupt vectors
   and virtual devices as it found them, then INT 21h 4CFFh as that program
   from GLOS.EXE's stub, so DOS ends it and its parent carries on. Not while
   it is inside DOS, unless it stays there for two seconds (or force).
   1 when it did. */
static int vm_try_kill(struct trapframe *tf, int force)
{
    u16 psp = vm_current_psp(), parent = 0;
    u32 at_cs = tf->cs, at_ip = tf->eip & 0xFFFF;
    struct vm_snap *s;
    u8 irr[2];
    int i;

    if (!force && vm.indos && vm_rd8(vm.indos) && timer_ticks() - vm.kill_since < 2048)
        return 0;
    vm.kill_req = 0;
    if (psp)
        parent = vm_rd16(psp * 16u + 0x16);
    for (i = SNAP_LEVELS - 1; i >= 0; i--)
        if (vm.snap[i].parent_psp && vm.snap[i].parent_psp == parent)
            break;
    if (!psp || psp == vm.loader_psp || i < 0) {
        kprintf("GLOS-KILL none psp=%04x\n", psp);
        return 0;
    }
    s = &vm.snap[i];
    memcpy(vm_ptr(0), s->ivt, sizeof s->ivt);
    irr[0] = vm.pic.p[0].irr;
    irr[1] = vm.pic.p[1].irr;
    vm.pic = s->pic;
    vm.pic.p[0].irr = irr[0];
    vm.pic.p[1].irr = irr[1];
    vm.pic.p[0].isr = vm.pic.p[1].isr = 0;
    vm.pic.inflight = 0;
    vm_sync_mask();
    vm_set_a20(s->a20);
    vm.rtc_a = s->rtc_a;
    vm.rtc_b = s->rtc_b;
    timer_write_b(vm.rtc_b);
    vkbc_restore(s->kbc_cmd);
    outb(0x43, 0x34);                           /* PIT channel 0: mode 2, count FFFFh (§8.2) */
    outb(0x40, 0xFF);
    outb(0x40, 0xFF);
    for (; i < SNAP_LEVELS; i++)
        vm.snap[i].parent_psp = 0;
    reset_reqs = 0;

    vm.vif = 1;
    vm.vflags_hi = 0;
    tf->eflags = (tf->eflags & FL_ARITH & ~0x400u) | FL_VM | FL_IF | 2;
    tf->cs = vm.loader_cs;
    tf->eip = vm.bi->kill_off;
    tf->ss = vm.loader_cs;
    tf->esp = vm.bi->kill_sp;
    tf->v86_ds = tf->v86_es = vm.loader_cs;
    kprintf("GLOS-KILL psp=%04x at=%04x:%04x ticks=%u\n", psp, at_cs, at_ip, timer_ticks());
    return 1;
}

void vm_return(struct trapframe *tf)
{
    int vec;
    if (vm.kill_req)
        vm_try_kill(tf, 0);
    if (vm.vif && vpic_pending(&vm.pic)) {
        vec = vpic_ack(&vm.pic);
        if (vec >= 0) {
            vm.n_irq++;
            vm_int(tf, (u8)vec, tf->eip & 0xFFFF);
        }
    }
}

/* ---- GLOS.EXE's calls and leaving */

static void vm_leave(u32 code) __attribute__((noreturn));
static void vm_leave(u32 code)
{
    vkbc_leave();
    vdev_leave();
    pic_init(vm.pic.p[0].base, vm.pic.p[1].base, vpic_imr(&vm.pic));
    kprintf("GLOS-VM leave code=%u ticks=%u gp=%u int=%u irq=%u spurious=%u\n", code, timer_ticks(), vm.n_gp,
            vm.n_int, vm.n_irq, timer_spurious());
    vm.bi->result = code;
    leave_to_loader(code, mm_cr3());
}

static void vm_call(struct trapframe *tf)
{
    u32 fn = tf->eax & 0xFFFF, arg = tf->ebx;
    tf->eip = (tf->eip + 2) & 0xFFFF;
    switch (fn) {
    case GLOS_CALL_LEAVE:
        vm_leave(arg);
    case GLOS_CALL_DOSPTR:
        vm.indos = arg;
        vm.sda = vm.bi->sda;
        kprintf("GLOS-VM dos indos=%05x sda=%05x psp=%04x\n", vm.indos, vm.sda, vm_current_psp());
        tf->eax = 0;
        break;
    case GLOS_CALL_EXEC:
        vm.loader_psp = (u16)arg;
        tf->eax = 0;
        break;
    default:
        tf->eax = 0xFFFFFFFFu;
    }
}

/* ---- #GP: the instruction at CS:IP */

static u32 *seg_reg(struct trapframe *tf, u8 seg)
{
    switch (seg) {
    case 0: return &tf->v86_es;
    case 1: return &tf->cs;
    case 2: return &tf->ss;
    case 4: return &tf->v86_fs;
    case 5: return &tf->v86_gs;
    default: return &tf->v86_ds;
    }
}

static void step_index(u32 *r, int step, int wide)
{
    if (wide)
        *r += (u32)step;
    else
        SET16(*r, *r + (u32)step);
}

static void string_io(struct trapframe *tf, const struct v86insn *in)
{
    u32 port = tf->edx & 0xFFFF, w = in->width, n = 1, i, j, v;
    int step = (tf->eflags & 0x400) ? -(int)w : (int)w, wide = in->adsize == 4;

    if (in->rep)
        n = wide ? tf->ecx : (tf->ecx & 0xFFFF);
    for (i = 0; i < n; i++) {
        if (in->kind == V86_INS) {
            v = vdev_in((u16)port, (int)w);
            for (j = 0; j < w; j++)
                vm_wr8(vm_lin(tf->v86_es, tf->edi + j), (u8)(v >> (8 * j)));
            step_index(&tf->edi, step, wide);
        } else {
            u32 seg = *seg_reg(tf, in->seg);
            for (v = 0, j = 0; j < w; j++)
                v |= (u32)vm_rd8(vm_lin(seg, tf->esi + j)) << (8 * j);
            vdev_out((u16)port, v, (int)w);
            step_index(&tf->esi, step, wide);
        }
    }
    if (in->rep) {
        if (wide)
            tf->ecx = 0;
        else
            SET16(tf->ecx, 0);
    }
}

static void soft_int(struct trapframe *tf, u8 n, u32 next)
{
    u32 ax = tf->eax & 0xFFFF;
    vm.n_int++;
    switch (n) {
    case 0x15:
        if (vm_int15(tf, next))
            return;
        break;
    case 0x2F:                                  /* XMS: GLOS's own server */
        if (ax == 0x4300) {
            SETLO(tf->eax, 0x80);
            tf->eip = next;
            return;
        }
        if (ax == 0x4310) {
            tf->v86_es = vm.loader_cs;
            SET16(tf->ebx, vm.bi->bp_xms_off - 5);
            tf->eip = next;
            return;
        }
        break;
    case 0x21:
        if ((ax >> 8) == 0x4B && (ax & 0xFF) <= 1)
            vm_exec_snap();
        break;
    }
    vm_int(tf, n, next);
}

static void vm_gp(struct trapframe *tf)
{
    struct v86insn in;
    u8 b[V86_MAX_LEN];
    u32 ip = tf->eip & 0xFFFF, next, port, v, i;

    for (i = 0; i < V86_MAX_LEN; i++)
        b[i] = vm_rd8(vm_lin(tf->cs, ip + i));
    v86_decode(b, V86_MAX_LEN, &in);
    next = (ip + in.len) & 0xFFFF;
    port = in.port_dx ? (tf->edx & 0xFFFF) : in.imm;
    vm.n_gp++;

    switch (in.kind) {
    case V86_CLI:
        vm.vif = 0;
        break;
    case V86_STI:
        vm.vif = 1;
        /* STI; HLT with an IRQ pending: the IRQ wakes the HLT at once. */
        if (vpic_pending(&vm.pic) && vm_rd8(vm_lin(tf->cs, next)) == 0xF4)
            next = (next + 1) & 0xFFFF;
        break;
    case V86_PUSHF:
        if (in.opsize == 4)
            push32(tf, flags_image(tf));
        else
            push16(tf, flags_image(tf));
        break;
    case V86_POPF:
        flags_apply(tf, in.opsize == 4 ? pop32(tf) : pop16(tf), in.opsize == 4);
        break;
    case V86_IRET:
        if (in.opsize == 4) {
            next = pop32(tf) & 0xFFFF;
            tf->cs = pop32(tf) & 0xFFFF;
            flags_apply(tf, pop32(tf), 1);
        } else {
            next = pop16(tf);
            tf->cs = pop16(tf);
            flags_apply(tf, pop16(tf), 0);
        }
        break;
    case V86_HLT:
        tf->eip = next;
        vm_idle(0);
        return;
    case V86_INT:
        soft_int(tf, in.imm, next);
        return;
    case V86_IN:
        v = vdev_in((u16)port, in.width);
        if (in.width == 1)
            SETLO(tf->eax, v);
        else if (in.width == 2)
            SET16(tf->eax, v);
        else
            tf->eax = v;
        break;
    case V86_OUT:
        v = in.width == 1 ? (tf->eax & 0xFF) : in.width == 2 ? (tf->eax & 0xFFFF) : tf->eax;
        vdev_out((u16)port, v, in.width);
        break;
    case V86_INS:
    case V86_OUTS:
        string_io(tf, &in);
        break;
    case V86_PRIV:                              /* MOV CRn, LMSW, LGDT...: no way to emulate */
        kprintf("GLOS-WARN v86-priv at=%04x:%04x op=0f%02x\n", tf->cs, ip, in.imm);
        if (!vm_try_kill(tf, 1))
            panic("v86-priv", tf);
        return;
    default:                                    /* a segment limit: a real-mode CPU raises INT 0Dh */
        vm_int(tf, 0x0D, ip);
        return;
    }
    tf->eip = next;
}

void vm_exception(struct trapframe *tf)
{
    u32 ip = tf->eip & 0xFFFF;
    switch (tf->vec) {
    case 13:
        vm_gp(tf);
        return;
    case 6:
        if (tf->cs == vm.loader_cs && ip == vm.bi->bp_call_off) {
            vm_call(tf);
            return;
        }
        if (tf->cs == vm.loader_cs && ip == vm.bi->bp_xms_off) {
            xms_call(tf);
            tf->eip = (ip + 2) & 0xFFFF;
            return;
        }
        vm_int(tf, 6, ip);
        return;
    case 0: case 1: case 3: case 4: case 5: case 7: case 12:
        vm_int(tf, (u8)tf->vec, ip);            /* as a real-mode CPU would */
        return;
    default:
        panic("v86-exception", tf);
    }
}

/* ---- starting: GLOS.EXE resumes in V86 mode at _vm_resume with SS, SP and
   DS as pm_enter saved them (bootinfo vm_state_off). */

static const u16 trapped_ports[] = { 0x20, 0x21, 0xA0, 0xA1, 0x60, 0x64, 0x70, 0x71, 0x92,
                                     0xCF8, 0xCF9, 0xCFA, 0xCFB, 0xCFC, 0xCFD, 0xCFE, 0xCFF };

void vm_start(struct bootinfo *bi)
{
    struct trapframe *tf = &start_frame;
    u8 *st = vm_ptr(bi->cs_base + bi->vm_state_off);
    u32 i;

    vm.bi = bi;
    vm.loader_cs = (u16)(bi->cs_base >> 4);
    vm.a20 = bi->a20_initial ? 1 : 0;
    mm_vm_init(vm.a20);
    for (i = 0; i < ARRAY_SIZE(trapped_ports); i++)
        cpu_io_trap(trapped_ports[i], 1);

    vpic_reset(&vm.pic, 0x08, 0x70, (u16)bi->pic_mask);
    for (i = 0; i < 16; i++)
        if (!(KERNEL_LINES & (1u << i)))
            set_irq_handler((int)i, vm_irq_line);
    set_irq_handler(1, vkbc_irq);
    set_irq_handler(12, vkbc_irq);
    vm.phys_mask = 0xFFFF;
    pic_init(IRQ_BASE_MASTER, IRQ_BASE_SLAVE, 0xFFFF);
    timer_start();
    vdev_init();
    vkbc_init();
    xms_init(bi);
    vm_sync_mask();

    memset(tf, 0, sizeof *tf);
    tf->eip = bi->vm_resume_off;
    tf->cs = vm.loader_cs;
    tf->eflags = FL_VM | FL_IF | 2;
    tf->ss = (u32)(st[0] | (st[1] << 8));
    tf->esp = (u32)(st[2] | (st[3] << 8));
    tf->v86_ds = tf->v86_es = (u32)(st[4] | (st[5] << 8));
    vm.vif = 0;                                 /* pm_enter's CLI; _vm_resume's POPF sets it */
    kprintf("GLOS-RING0 step=vm cs=%04x ss:sp=%04x:%04x a20=%u pic=%04x kbc=%02x xms=%s\n", vm.loader_cs, tf->ss,
            tf->esp, vm.a20, vpic_imr(&vm.pic), vkbc_cmd(), bi->mode == BI_MODE_XMS ? "takeover" : "raw");
    vm_enter(tf);
}
