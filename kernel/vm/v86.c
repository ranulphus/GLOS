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
 * virtual IRQ when the virtual IF allows, and carries out a kill. The VM is
 * a thread (core/sched.c): its V86 frame always sits at the top of its
 * stack, and its waits (HLT, INT 15h 86h) block it until vm_kick(). */
#include "glos/bootinfo.h"
#include "agent.h"
#include "dos.h"
#include "dpmi.h"
#include "session.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "nic.h"
#include "sched.h"
#include "timer.h"
#include "v86dec.h"
#include "vm.h"

/* The kernel's lines: keyboard, cascade, clock, AUX, and any it claims later
   (the network card); kernel_masked holds those a driver has masked. */
static u16 kernel_lines = (1u << 1) | (1u << 2) | (1u << 8) | (1u << 12);
static u16 kernel_masked;

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
   ID; the virtual IF; NT as last written; IOPL 3, as VME's PUSHF shows it
   (and an IOPL-3 host's real mode), so V86 code sees the same FLAGS on every
   profile (HDPMI's I3103022 on the 486DX2, M4c). CPU detection still finds a
   386 or later: bits 12-14 neither all clear nor all set after a POPF of 0.
   VM, IF and IOPL 0 stay real.
   ID only where the CPU has CPUID (the loader found it toggling): 86Box's
   IRET to V86 mode loads EFLAGS unmasked, so a 486DX2 there "kept" an ID a
   POPFD had set, and lDebugX's probe then ran CPUID into #UD (M4c). */

static u32 flags_wide(void)
{
    return FL_ARITH | FL_TF | FL_AC | (vm.bi->cpuid_edx ? FL_ID : 0);
}

static u32 flags_image(const struct trapframe *tf)
{
    return (tf->eflags & flags_wide()) | (vm.vif ? FL_IF : 0) | (vm.vflags_hi & FL_NT) | FL_IOPL3 | 2;
}

static void flags_apply(struct trapframe *tf, u32 f, int wide)
{
    u32 keep = wide ? flags_wide() : (FL_ARITH | FL_TF);
    tf->eflags = (tf->eflags & ~keep) | (f & keep);
    vm.vif = (f & FL_IF) != 0;
    vm.vflags_hi = f & FL_NT;
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

/* The VM thread waits, interrupts off, until an IRQ it can take, a kill or
   the tick `until` (0: no deadline). Other threads run meanwhile. */
void vm_idle(u32 until)
{
    vm.wait_deadline = until;
    vm.waiting = 1;
    while (!vm.kill_req && !(vm.vif && vpic_pending(&vm.pic))
           && !(until && (s32)(timer_ticks() - until) >= 0) && !(vm.idle_agent && agent_vm_wake_pending()))
        thread_block(&vm.waitq);
    vm.waiting = 0;
    vm.wait_deadline = 0;
}

/* Something the VM may be waiting for happened (an IRQ raised, a kill, a
   deadline): wake it, and let it take the CPU from bulk work. */
void vm_kick(void)
{
    thread_wake(&vm.waitq);
    if (current && current != vm.thread && current->prio > PRIO_NORMAL)
        sched_resched();
}

struct trapframe *vm_frame(void) { return (struct trapframe *)(vm.thread->stack_top - sizeof(struct trapframe)); }

/* ---- every entry from user mode, and every way out

   With VME the virtual IF lives in EFLAGS.VIF while V86 code runs: it is
   read at every trap from V86 mode and written back by vm_return(), with
   VIP set while an IRQ waits for it (so the STI or POPF that sets it traps).

   In a direct-mode session (supervisor.md §9.7) the program runs at IOPL 3:
   its CLI, STI, POPF and IRET act on the real IF, which is the virtual IF
   while it runs. It is read at every entry and written back on the way out
   (trap_exit). The program's protected-mode code also reaches the PIC and
   the RTC: at every entry GLOS takes a mask the program wrote into the
   virtual PIC and puts its own lines back, and puts the RTC's periodic
   interrupt back (checked at every 16th entry). The virtual PIC ends each
   IRQ it delivers at once, since the program's EOI goes to the real PIC
   (which GLOS ended when the IRQ came). */
static u32 direct_imr, direct_rtc;              /* what the session took back: for its end line */

void vm_user_entry(struct trapframe *tf)
{
    u16 m, phys, own;
    u8 a, b;
    if (!vm.direct) {
        if (vm.vme && (tf->eflags & FL_VM))
            vm.vif = (tf->eflags & FL_VIF) != 0;
        return;
    }
    vm.vif = (tf->eflags & FL_IF) != 0;
    phys = (u16)(inb(0x21) | (inb(0xA1) << 8));
    if (phys != vm.phys_mask) {                 /* the program's (the kernel's lines and those in flight */
        own = (u16)(kernel_lines | vm.pic.inflight);    /* are GLOS's) */
        m = (u16)((vpic_imr(&vm.pic) & own) | (phys & ~own));
        vm.pic.p[0].imr = (u8)m;
        vm.pic.p[1].imr = (u8)(m >> 8);
        vm.phys_mask = phys;                    /* (what the chip holds: vm_sync_mask() puts GLOS's back) */
        vm_sync_mask();
        direct_imr++;
    }
    if (!(vm.direct_rtc++ & 15) && timer_reclaim(&a, &b)) {
        vm.rtc_a = a;                           /* what it wrote: its own (virtual) RTC */
        vm.rtc_b = b;
        direct_rtc++;
    }
}

void vm_user_exit(struct trapframe *tf)
{
    if (!(tf->eflags & FL_VM) && !(tf->cs & 3))
        return;
    if (vm.direct)
        tf->eflags = (tf->eflags & ~(FL_IF | FL_VIF | FL_VIP)) | FL_IOPL3 | (vm.vif ? FL_IF : 0);
    else if (tf->eflags & FL_IOPL3)             /* a frame from a direct-mode session that has ended */
        tf->eflags = (tf->eflags & ~FL_IOPL3) | FL_IF;
}

/* A direct-mode session begins or ends (session.c). At the end the physical
   IMR is GLOS's again, and the RTC's rate and PIE. */
void vm_direct(int on)
{
    u8 a, b;
    if (on == vm.direct)
        return;
    if (on) {
        direct_imr = direct_rtc = 0;
        vm.direct_rtc = 0;
    } else {
        vm.phys_mask = (u16)(inb(0x21) | (inb(0xA1) << 8));
        if (timer_reclaim(&a, &b))
            direct_rtc++;
        kprintf("GLOS-SESSION direct-end imr=%u rtc=%u\n", direct_imr, direct_rtc);
    }
    vm.direct = (u8)on;
    vm.pic.auto_eoi = (u8)on;
    vkbc_direct(on);
    vm_sync_mask();
}

void vm_sync_mask(void)
{
    u16 m = (u16)(((vpic_imr(&vm.pic) | vm.pic.inflight) & ~kernel_lines) | (kernel_masked & kernel_lines));
    if (m != vm.phys_mask) {
        vm.phys_mask = m;
        pic_set_mask(m);
    }
}

/* A line becomes the kernel's: the VM never sees it again (supervisor.md §10). */
void vm_claim_irq(int irq, void (*fn)(struct trapframe *))
{
    kernel_lines |= (u16)(1u << irq);
    set_irq_handler(irq, fn);
    vm_sync_mask();
}

/* A kernel driver masks its line while its thread works (interrupts off). */
void vm_kmask(int irq, int masked)
{
    if (masked)
        kernel_masked |= (u16)(1u << irq);
    else
        kernel_masked &= (u16)~(1u << irq);
    vm_sync_mask();
}

/* A physical IRQ for the VM: masked until the program's EOI (so a level-
   triggered device cannot storm), then raised on the virtual PIC. */
static void vm_irq_line(struct trapframe *tf)
{
    int irq = (int)(tf->vec - IRQ_BASE_MASTER);
    vm.phys_mask |= (u16)(1u << irq);
    pic_set_mask(vm.phys_mask);
    vpic_raise_hw(&vm.pic, irq);
    vm_kick();
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
    s->mode = vm_rd8(0x449);
    s->leds = vm_rd8(0x417) & 0x70;
}

/* The kill (Ctrl-Alt-Shift-Esc): the current program's interrupt vectors
   and virtual devices as it found them, then INT 21h 4CFFh as that program
   from GLOS.EXE's stub, so DOS ends it and its parent carries on. Not while
   it is inside DOS, unless it stays there for two seconds (or force).
   1 when it did. */
/* The vectors and virtual devices as parent's child found them (the
   snapshot of its EXEC), and the PIT as GLOS keeps it: 1, or 0 if there is
   no snapshot. */
static int snap_restore(u16 parent)
{
    struct vm_snap *s;
    u8 irr[2];
    int i;

    for (i = SNAP_LEVELS - 1; i >= 0; i--)
        if (vm.snap[i].parent_psp && vm.snap[i].parent_psp == parent)
            break;
    if (i < 0)
        return 0;
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
    session_kill(s->mode, s->leds);             /* and what a session's end puts back (M4e) */
    for (; i < SNAP_LEVELS; i++)
        vm.snap[i].parent_psp = 0;
    reset_reqs = 0;
    return 1;
}

/* A program DOS has just ended without its INT 21h 4Ch (an abort): its
   parent is current again; what the program changed goes, as on a kill. */
int vm_restore_child(void) { return snap_restore(vm_current_psp()); }

static int vm_try_kill(struct trapframe *tf, int force)
{
    u16 psp = vm_current_psp(), parent = 0;
    u32 at_cs = tf->cs, at_ip = tf->eip & 0xFFFF;

    if (!force && vm.indos && vm_rd8(vm.indos) && timer_ticks() - vm.kill_since < 2048)
        return 0;
    vm.kill_req = 0;
    if (psp)
        parent = vm_rd16(psp * 16u + 0x16);
    if (!psp || psp == vm.loader_psp || !snap_restore(parent)) {
        u32 st = vm_lin(tf->ss, tf->esp & 0xFFFF);
        kprintf("GLOS-KILL none psp=%04x at=%04x:%04x ss:sp=%04x:%04x stack=%04x %04x %04x %04x %04x %04x %04x %04x"
                " reason=%s\n", psp, at_cs, at_ip, tf->ss & 0xFFFF, tf->esp & 0xFFFF, vm_rd16(st), vm_rd16(st + 2),
                vm_rd16(st + 4), vm_rd16(st + 6), vm_rd16(st + 8), vm_rd16(st + 10), vm_rd16(st + 12),
                vm_rd16(st + 14), vm.kill_reason ? vm.kill_reason : "crash");
        vm.kill_reason = 0;
                                                /* (where GLOS.EXE itself was, if it hung) */
        return 0;
    }

    vm.vif = 1;
    vm.vflags_hi = 0;
    tf->eflags = (tf->eflags & FL_ARITH & ~0x400u) | FL_VM | FL_IF | 2;
    tf->cs = vm.loader_cs;
    tf->eip = vm.bi->kill_off;
    tf->ss = vm.loader_cs;
    tf->esp = vm.bi->kill_sp;
    tf->v86_ds = tf->v86_es = vm.loader_cs;
    kprintf("GLOS-KILL psp=%04x at=%04x:%04x ticks=%u reason=%s\n", psp, at_cs, at_ip, timer_ticks(),
            vm.kill_reason ? vm.kill_reason : "crash");
    vm.kill_reason = 0;
    return 1;
}

/* A kill now, wherever the program is (the DPMI host's end of a client). */
void vm_kill_now(struct trapframe *tf) { vm_try_kill(tf, 1); }

void vm_return(struct trapframe *tf)
{
    int vec;
    if (fpu_msw_dos != ~0u)
        fpu_msw_set(fpu_msw_dos);               /* a client's 0E01h EM is its own */
    if (vm.kill_req)
        vm_try_kill(tf, 0);
    if (vm.vif && vpic_pending(&vm.pic)) {
        vec = vpic_ack(&vm.pic);
        if (vm.direct)
            vm_sync_mask();                     /* (ended at once: the line is unmasked again) */
        if (vec >= 0) {
            vm.n_irq++;
            if (dctx && dpmi_irq(tf, (u8)vec)) {    /* a DPMI client's handler first, in protected mode (§14.1) */
                dpmi_return(tf);
                return;
            }
            vm_int(tf, (u8)vec, tf->eip & 0xFFFF);
        }
    }
    if (vm.vme && !vm.direct) {
        tf->eflags &= ~(FL_VIF | FL_VIP);
        if (vm.vif)
            tf->eflags |= FL_VIF;
        else if (vpic_pending(&vm.pic))
            tf->eflags |= FL_VIP;
    }
}

/* ---- GLOS.EXE's calls and leaving */

static void int2f_hook(u16 old_cs);
static void int2f_unhook(void);
static void vm_leave(u32 code) __attribute__((noreturn));
static void vm_leave(u32 code)
{
    int2f_unhook();
    vkbc_leave();
    vdev_leave();
    pic_init(vm.pic.p[0].base, vm.pic.p[1].base, vpic_imr(&vm.pic));
    if (vm.vme)
        cpu_set_cr4(0, 1);
    sched_report();
    net_report();
    if (vm.bi->flags & BI_F_SELFTEST)
        selftest_report();
    kprintf("GLOS-VM leave code=%u ticks=%u gp=%u int=%u irq=%u spurious=%u\n", code, timer_ticks(), vm.n_gp,
            vm.n_int, vm.n_irq, timer_spurious());
    vm.bi->result = code;
    leave_to_loader(code, mm_cr3());
}

/* GLOS.EXE's stub moved to seg:0, just above its PSP; the rest of GLOS.EXE
   goes back to DOS. A vector the C runtime hooked would be left pointing at
   free memory: the ones a runtime hooks are checked (others may hold any
   value, and some do, by chance inside that range). */
static void vm_resident(u16 seg)
{
    static const u8 hooked[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x1B, 0x23, 0x24 };
    u16 psp = (u16)(seg - 0x10);
    u32 end = psp + vm_rd16(((u32)psp - 1) * 16 + 3), lo = (u32)(seg + vm.bi->stub_paras) << 4, i, v, x, lin;

    for (i = 0; i < sizeof hooked; i++) {
        v = hooked[i];
        x = vm_rd32(v * 4);
        lin = (x >> 16) * 16 + (x & 0xFFFF);
        if (lin >= lo && lin < end * 16)
            kprintf("GLOS-WARN ivt-into-loader vec=%02x at=%04x:%04x\n", v, x >> 16, x & 0xFFFF);
    }
    {
        u16 old = vm.loader_cs;
        vm.loader_cs = seg;
        int2f_hook(old);
    }
    vm.loader_psp = psp;
    vm.bi->cs_base = (u32)seg << 4;
    cpu_set_code16_base(vm.bi->cs_base);
    kprintf("GLOS-VM resident psp=%04x stub=%u freed=%u\n", psp, (vm.bi->stub_paras + 0x10) * 16,
            (end - psp - vm.bi->stub_paras - 0x10) * 16);
}

/* The stub's EXEC buffers (struct stub_data at offset 0 of its segment):
   path, and the tail t1 t2 as length, text, CR. */
static void stub_exec(const char *path, const char *t1, const char *t2)
{
    u32 p = ((u32)vm.loader_cs << 4) + __builtin_offsetof(struct stub_data, path);
    u32 t = ((u32)vm.loader_cs << 4) + __builtin_offsetof(struct stub_data, tail), i, n = 0;
    for (i = 0; path[i] && i < 79; i++)
        vm_wr8(p + i, (u8)path[i]);
    vm_wr8(p + i, 0);
    for (i = 0; t1[i] && n < 126; i++)
        vm_wr8(t + 1 + n++, (u8)t1[i]);
    for (i = 0; t2[i] && n < 126; i++)
        vm_wr8(t + 1 + n++, (u8)t2[i]);
    vm_wr8(t, (u8)n);
    vm_wr8(t + 1 + n, 0x0D);
}

/* As the shell (PRD D39, D40): AUTOEXEC.BAT through COMSPEC /C, then the
   console (COMSPEC, or COMSPEC /C the configured command), again and again. */
static void vm_shell_next(struct trapframe *tf, u32 result)
{
    struct bootinfo *bi = vm.bi;
    if (result != 0xFFFFFFFFu)
        kprintf("GLOS-VM %s code=%u%s\n", vm.shell_state == 1 ? "autoexec" : "console", result & 0xFF,
                (result & 0x10000) ? " cannot-run" : "");
    if (vm.shell_state == 0 && bi->autoexec[0]) {
        vm.shell_state = 1;
        stub_exec(bi->comspec, " /C ", bi->autoexec);
    } else {
        vm.shell_state = 2;
        stub_exec(bi->comspec, bi->console[0] ? " /C " : "", bi->console);
    }
    tf->eax = 1;
}

/* The COMMAND.COM running AUTOEXEC.BAT is ending (INT 21h 4Ch or 00h, before
   DOS frees its memory): its environment becomes the master environment,
   the one GLOS.EXE's PSP points at, as far as that block holds (PRD D40). */
static void vm_env_back(void)
{
    u16 psp = vm_current_psp();
    u32 s, d, size, i = 0, end = 0;

    if (!psp || vm_rd16(psp * 16u + 0x16) != vm.loader_psp)
        return;
    s = (u32)vm_rd16(psp * 16u + 0x2C) << 4;
    d = (u32)vm_rd16((u32)vm.loader_psp * 16 + 0x2C) << 4;
    if (!s || !d)
        return;
    size = (u32)vm_rd16(d - 16 + 3) << 4;               /* the master block, from its arena header */
    while (i < 32768 && vm_rd8(s + i)) {                /* whole strings that fit, with 3 bytes to end */
        u32 j = i;
        while (j < 32768 && vm_rd8(s + j))
            j++;
        if (j + 1 + 3 > size)
            break;
        i = end = j + 1;
    }
    for (i = 0; i < end; i++)
        vm_wr8(d + i, vm_rd8(s + i));
    vm_wr8(d + end, 0);                                 /* the end of the strings, no program name */
    vm_wr8(d + end + 1, 0);
    vm_wr8(d + end + 2, 0);
    kprintf("GLOS-VM env bytes=%u of %u\n", end + 1, size);
}

/* Headless: between jobs, the DOS server's calls (kernel/dos/dos.c, EAX =
   3); then the agent's next command (kernel/dos/agent.c), or a halt until
   one comes (EAX = 2), or glos exit. INT 29h traps only while a command
   runs, for the capture. */
static void vm_agent_next(struct trapframe *tf, u32 result)
{
    struct agent_exec x;
    int r;
    if (!agent_vm_capturing()) {
        u32 regs = ((u32)vm.loader_cs << 4) + __builtin_offsetof(struct stub_data, dregs);
        if (dos_vm_next(result == 0xFFFFFFFEu, regs)) {
            tf->eax = 3;
            return;
        }
        result = 0xFFFFFFFFu;
    }
    r = agent_vm_next(result, vm.bi->comspec, &x);
    if (vm.vme)
        cpu_int_redirect(0x29, !agent_vm_capturing());
    if (r < 0)
        vm_leave(0);
    if (r > 0)
        stub_exec(x.path, x.t1, x.t2);
    tf->eax = r > 0 ? 1 : 2;
}

/* What the stub does next: EXEC the /RUN program it holds, then leave with
   its exit code; as the shell, vm_shell_next(); headless, vm_agent_next(). */
static void vm_next(struct trapframe *tf, u32 result)
{
    if (vm.bi->flags & BI_F_AGENT) {
        vm_agent_next(tf, result);
        return;
    }
    if (vm.bi->flags & BI_F_SHELL) {
        vm_shell_next(tf, result);
        return;
    }
    if (result == 0xFFFFFFFFu) {
        tf->eax = 1;
        return;
    }
    if (result & 0x10000) {
        kprintf("GLOS-VM error=cannot-run dos_error=%u\n", result & 0xFFFF);
        vm_leave(126);
    }
    vm_leave(result & 0xFF);
}

static void vm_call(struct trapframe *tf)
{
    u32 fn = tf->eax & 0xFFFF, arg = tf->ebx;
    tf->eip = (tf->eip + 2) & 0xFFFF;
    switch (fn) {
    case GLOS_CALL_LEAVE:
        vm_leave(arg);
    case GLOS_CALL_EXEC:
        vm.loader_psp = (u16)arg;
        tf->eax = 0;
        break;
    case GLOS_CALL_RESIDENT:
        vm_resident((u16)arg);
        tf->eax = 0;
        break;
    case GLOS_CALL_NEXT:
        session_stub_next();
        vm_next(tf, arg);
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

/* GLOS's INT 2Fh (M4c): the DPMI host's 1687h, the XMS server's 4300h and
   4310h, a real yield for 1680h, and never Windows (1600h, 160Ah), answered
   at the bottom of the IVT's chain, as a DPMI host or an XMS driver that is
   a TSR answers them. A program that hooks INT 2Fh after GLOS started (a
   debugger wrapping the DPMI entry, lDebugX) sees these calls first, as it
   would under HDPMI or CWSDPMI; anything else goes on down the chain. The
   vector points at the stub's int2f ARPL, and follows the stub when it
   moves (vm_resident); the leave puts the old one back. */
static void int2f_hook(u16 old_cs)
{
    u32 ours = (u32)vm.loader_cs << 16 | vm.bi->int2f_off, cur = vm_rd32(0x2F * 4);
    if (!vm.int2f_prev)
        vm.int2f_prev = cur;
    else if (cur != ((u32)old_cs << 16 | vm.bi->int2f_off))
        return;                                 /* hooked over by now: the chain still reaches the old ARPL */
    vm_wr8(0x2F * 4, (u8)ours);
    vm_wr8(0x2F * 4 + 1, (u8)(ours >> 8));
    vm_wr8(0x2F * 4 + 2, (u8)(ours >> 16));
    vm_wr8(0x2F * 4 + 3, (u8)(ours >> 24));
}

static void int2f_unhook(void)
{
    u32 ours = (u32)vm.loader_cs << 16 | vm.bi->int2f_off, v = vm.int2f_prev;
    if (vm_rd32(0x2F * 4) != ours) {
        kprintf("GLOS-WARN int2f-hooked-over vec=%08x\n", vm_rd32(0x2F * 4));
        return;
    }
    vm_wr8(0x2F * 4, (u8)v);
    vm_wr8(0x2F * 4 + 1, (u8)(v >> 8));
    vm_wr8(0x2F * 4 + 2, (u8)(v >> 16));
    vm_wr8(0x2F * 4 + 3, (u8)(v >> 24));
}

static void vm_int2f(struct trapframe *tf)
{
    u32 ax = tf->eax & 0xFFFF, f;
    if (ax == 0x1687) {
        dpmi_1687(tf);
    } else if (ax == 0x4300) {
        SETLO(tf->eax, 0x80);
    } else if (ax == 0x4310) {
        tf->v86_es = vm.loader_cs;
        SET16(tf->ebx, vm.bi->bp_xms_off - 5);
    } else if (ax == 0x1680) {                  /* a real yield (§13): the kernel's threads run. AL */
        thread_yield();                         /* stays 80h, "not supported", as on plain DOS: DJGPP's */
                                                /* uclock() takes AL=0 for Windows 9x and waits for a */
                                                /* BIOS tick, forever if called with interrupts off */
    } else if (ax == 0x1600) {                  /* never Windows (§13); 160Ah: AX as it was */
        SETLO(tf->eax, 0);
    } else if (ax != 0x160A) {
        tf->cs = vm.int2f_prev >> 16;           /* on down the chain, the INT's frame as it is */
        tf->eip = vm.int2f_prev & 0xFFFF;
        return;
    }
    tf->eip = pop16(tf);                        /* IRET */
    tf->cs = pop16(tf);
    f = pop16(tf);
    flags_apply(tf, (f & ~FL_ARITH) | (tf->eflags & FL_ARITH), 0);    /* the answer's flags, the caller's IF */
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
    case 0x29:
        agent_vm_int29(tf);
        break;
    case 0x1C:
    case 0x23:
    case 0x24:
        if (dpmi_passup(tf, n, next))
            return;
        break;
    case 0x20:
        session_terminate();
        break;
    case 0x21:
        if (dctx && (vm.bi->flags & BI_F_DPMITRACE)) {     /* a client's own real-mode code, too */
            static u32 n21;
            if (n21++ < 200)
                kprintf("GLOS-DPMI v86-int21 ax=%04x bx=%04x cx=%04x dx=%04x ds=%04x es=%04x from=%04x:%04x\n", ax,
                        tf->ebx & 0xFFFF, tf->ecx & 0xFFFF, tf->edx & 0xFFFF, tf->v86_ds & 0xFFFF,
                        tf->v86_es & 0xFFFF, tf->cs & 0xFFFF, tf->eip & 0xFFFF);
        }
        agent_vm_int21(tf);
        if ((ax >> 8) == 0x31)
            dpmi_tsr_seen();
        if ((ax >> 8) == 0x4B)
            session_exec(tf);
        if ((ax >> 8) == 0x4D)
            session_exit_code();
        if ((ax >> 8) == 0x4B && (ax & 0xFF) <= 1)
            vm_exec_snap();
        if (vm.shell_state == 1 && ((ax >> 8) == 0x4C || (ax >> 8) == 0x00))
            vm_env_back();
        if ((ax >> 8) == 0x4C || (ax >> 8) == 0x00)
            session_terminate();
        if ((ax >> 8) == 0x4C || (ax >> 8) == 0x00)
            dpmi_dos_exit(tf, n, next);         /* a DPMI client's program ending (may not come back) */
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
        vm.idle_agent = (tf->cs & 0xFFFF) == vm.loader_cs;     /* the stub, waiting for a command */
        vm_idle(0);
        vm.idle_agent = 0;
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
        vm.kill_reason = "priv";
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
        if (tf->cs == vm.loader_cs && ip == vm.bi->int2f_off) {
            vm_int2f(tf);
            return;
        }
        if (dpmi_v86_bp(tf))
            return;
        vm_int(tf, 6, ip);
        return;
    case 0: case 1: case 3: case 4: case 5: case 7: case 12:
        if (tf->vec == 1)
            dpmi_db_hit();                      /* a client's watchpoint met in V86 code is still a hit */
        vm_int(tf, (u8)tf->vec, ip);            /* as a real-mode CPU would */
        return;
    default:
        if (tf->vec >= 0x20) {                  /* INT n at IOPL 3 (direct mode), through a DPL-3 gate: */
            soft_int(tf, (u8)tf->vec, ip);      /* EIP is already past it */
            return;
        }
        panic("v86-exception", tf);
    }
}

/* ---- starting: GLOS.EXE resumes in V86 mode at _vm_resume with SS, SP and
   DS as pm_enter saved them (bootinfo vm_state_off). */

/* Software INTs GLOS handles (supervisor.md §9.3): with VME, the rest go
   straight through the IVT. */
static const u8 trapped_ints[] = { 0x15, 0x1C, 0x21, 0x23, 0x24 };     /* 1Ch/23h/24h: a DPMI client's (§14.4) */

static const u16 trapped_ports[] = { 0x20, 0x21, 0xA0, 0xA1, 0x60, 0x64, 0x70, 0x71, 0x92,
                                     0xCF8, 0xCF9, 0xCFA, 0xCFB, 0xCFC, 0xCFD, 0xCFE, 0xCFF };

/* The VM thread: the start frame onto the top of its stack, then into V86
   mode; it comes back only through traps. */
static void vm_thread_main(void *arg)
{
    struct trapframe *tf = vm_frame();
    (void)arg;
    cli();
    *tf = start_frame;
    vm_enter(tf);
}

void vm_start(struct bootinfo *bi)
{
    struct trapframe *tf = &start_frame;
    u8 *st = vm_ptr(bi->cs_base + bi->vm_state_off);
    u32 i;

    vm.bi = bi;
    vm.indos = bi->indos;
    vm.sda = bi->sda;
    vm.loader_cs = (u16)(bi->cs_base >> 4);
    vm.a20 = bi->a20_initial ? 1 : 0;
    mm_vm_init(vm.a20);
    for (i = 0; i < ARRAY_SIZE(trapped_ports); i++)
        cpu_io_trap(trapped_ports[i], 1);

    dpmi_init();
    int2f_hook(vm.loader_cs);
    vpic_reset(&vm.pic, 0x08, 0x70, (u16)bi->pic_mask);
    for (i = 0; i < 16; i++)
        if (!(kernel_lines & (1u << i)))
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
    if ((cpu_cr4_bits & 1) && !(bi->flags & BI_F_NOVME)) {
        vm.vme = 1;
        for (i = 0; i < 256; i++)
            cpu_int_redirect(i, 1);
        for (i = 0; i < ARRAY_SIZE(trapped_ints); i++)
            cpu_int_redirect(trapped_ints[i], 0);
        cpu_set_cr4(1, 0);
    }

    memset(tf, 0, sizeof *tf);
    tf->eip = bi->vm_resume_off;
    tf->cs = vm.loader_cs;
    tf->eflags = FL_VM | FL_IF | 2;
    tf->ss = (u32)(st[0] | (st[1] << 8));
    tf->esp = (u32)(st[2] | (st[3] << 8));
    tf->v86_ds = tf->v86_es = (u32)(st[4] | (st[5] << 8));
    vm.vif = 0;                                 /* pm_enter's CLI; _vm_resume's POPF sets it */
    kprintf("GLOS-RING0 step=vm cs=%04x ss:sp=%04x:%04x a20=%u pic=%04x kbc=%02x xms=%s vme=%u\n", vm.loader_cs,
            tf->ss, tf->esp, vm.a20, vpic_imr(&vm.pic), vkbc_cmd(), bi->mode == BI_MODE_XMS ? "takeover" : "raw",
            vm.vme);
    kprintf("GLOS-VM dos indos=%05x sda=%05x psp=%04x\n", vm.indos, vm.sda, vm_current_psp());
    vm.thread = thread_create("vm", PRIO_NORMAL, vm_thread_main, NULL);
}
