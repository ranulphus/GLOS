/* The DPMI host's core (supervisor.md §12, §14; M4a).
 *
 * The mode switch: INT 2Fh 1687h (from V86 mode) offers the stub's ARPL as
 * the entry; a program far-calls it with AX bit 0 for a 32-bit client and
 * ES = the block of RM_STACK_PARAS paragraphs it allocated for the host.
 * The host makes a context (an address space with the kernel's PDEs, an
 * LDT, a virtual IDT) and the selectors of [DPMI0.9 §4]: CS for the code it
 * came from, DS and SS (Big for a 32-bit client; one selector when DS ==
 * SS), ES for the PSP (limit FFFFh, as CWSDPMI), and an environment
 * selector put in PSP:2Ch. The VM thread then carries on at ring 3.
 *
 * At ring 3, IOPL is 0: CLI, STI, HLT and I/O to trapped ports fault and
 * are emulated against the virtual IF and the virtual devices, as in V86
 * mode. Software interrupts arrive through DPL-3 gates (vectors 0-1Fh and
 * 50h-5Fh as #GP) and go through the virtual IDT: to the client's handler,
 * or to the host's, which runs INT 31h, ends the client on INT 21h 4Ch and
 * reflects the rest to real mode. The host's handlers are HLTs in the
 * trampoline page (SEL_TRAMP), so a client that chains to the "previous
 * handler" 0204h gave it reaches them too.
 *
 * Interrupts: IRQs that arrive while the client runs go to the virtual PIC
 * as always; on the way back to ring 3, with the virtual IF on, each is run
 * by its real-mode handler, nested (M4b gives clients protected-mode IRQ
 * handlers first). Exceptions end the client (handlers: M4b).
 *
 * The end: INT 21h 4Ch in protected mode frees the context and goes on in
 * V86 mode, as the program, at the stub's INT 21h with AX as it was; DOS
 * then ends the program. A program that ends from real mode (after a raw
 * switch, or inside a nested call) frees the context when DOS sees its
 * 4Ch. One context at a time in M4a; children's own mode switches arrive
 * with M4c. */
#include "glos/bootinfo.h"
#include "arch.h"
#include "dpmi.h"
#include "kprintf.h"
#include "mm.h"
#include "sched.h"
#include "v86dec.h"
#include "vm.h"
#include "vpic.h"

struct dpmi_ctx *dctx;
static struct dpmi_ctx ctx_store;

void dpmi_init(void)
{
    u8 *t = mm_tramp_page(TRAMP_LIN);
    if (t)
        memset(t, 0xF4, 4096);                  /* HLT: every entry faults at ring 3 */
}

void dpmi_unimpl(struct trapframe *tf, const char *what)
{
    kprintf("GLOS-DPMI-UNIMPL %s ax=%04x\n", what, tf->eax & 0xFFFF);
}

/* ---- frames */

void dpmi_to_pm(struct trapframe *tf, u16 cs, u32 eip, u16 ss, u32 esp, u16 ds, u16 es)
{
    tf->cs = cs;
    tf->eip = eip;
    tf->ss = ss;
    tf->esp = esp;
    tf->ds = ds;
    tf->es = es;
    tf->fs = tf->gs = 0;
    tf->eflags = (tf->eflags & (FL_ARITH | FL_TF | FL_AC)) | FL_IF | 2;
}

void dpmi_to_v86(struct trapframe *tf, u16 cs, u32 ip, u16 ss, u32 sp, u16 ds, u16 es)
{
    tf->cs = cs;
    tf->eip = ip;
    tf->ss = ss;
    tf->esp = sp;
    tf->v86_ds = ds;
    tf->v86_es = es;
    tf->v86_fs = tf->v86_gs = 0;
    tf->ds = tf->es = tf->fs = tf->gs = 0;     /* the ring-0 pops before IRET: nothing of the LDT */
    tf->eflags = (tf->eflags & (FL_ARITH | FL_TF | FL_AC)) | FL_VM | FL_IF | 2;
}

static u32 sp_of(const struct trapframe *tf)
{
    return (cpu_desc_hi(tf->ss) & (1u << 22)) ? tf->esp : tf->esp & 0xFFFF;
}

static void set_sp(struct trapframe *tf, u32 sp)
{
    if (cpu_desc_hi(tf->ss) & (1u << 22))
        tf->esp = sp;
    else
        SET16(tf->esp, sp);
}

/* Push an interrupt frame (FLAGS, CS, IP: 32-bit for a 32-bit client) on
   the client's stack; -1 if the stack isn't writable there. */
static int push_frame(struct trapframe *tf, u32 eip)
{
    u32 f = (tf->eflags & (FL_ARITH | FL_TF | FL_AC)) | (vm.vif ? FL_IF : 0) | 2, sp = sp_of(tf);
    if (dctx->bits32) {
        u32 v[3] = { eip, tf->cs, f };
        if (user_wr((u16)tf->ss, sp - 12, v, 12) != 0)
            return -1;
        set_sp(tf, sp - 12);
    } else {
        u16 v[3] = { (u16)eip, (u16)tf->cs, (u16)f };
        if (user_wr((u16)tf->ss, sp - 6, v, 6) != 0)
            return -1;
        set_sp(tf, sp - 6);
    }
    return 0;
}

/* IRET for the client, as its handler would: EIP, CS, FLAGS off its stack.
   keep: the arithmetic flags stay as the host's work left them (a host
   handler's result in CF, which the client's chained handler returns). */
static void iret_frame(struct trapframe *tf, int keep)
{
    u32 sp = sp_of(tf), eip, cs, f;
    if (dctx->bits32) {
        u32 v[3];
        if (user_rd((u16)tf->ss, sp, v, 12) != 0)
            return;
        eip = v[0], cs = v[1] & 0xFFFF, f = v[2];
        set_sp(tf, sp + 12);
    } else {
        u16 v[3];
        if (user_rd((u16)tf->ss, sp, v, 6) != 0)
            return;
        eip = v[0], cs = v[1], f = v[2];
        set_sp(tf, sp + 6);
    }
    tf->eip = eip;
    tf->cs = cs;
    if (keep)
        f = (f & ~FL_ARITH) | (tf->eflags & FL_ARITH);
    tf->eflags = (f & (FL_ARITH | FL_TF | FL_AC)) | FL_IF | 2;
    vm.vif = (f & FL_IF) != 0;
}

void dpmi_iret(struct trapframe *tf) { iret_frame(tf, 0); }

/* A far return (RETF), for the trampolines that are far-called. */
static void retf(struct trapframe *tf)
{
    u32 sp = sp_of(tf);
    if (dctx->bits32) {
        u32 v[2];
        if (user_rd((u16)tf->ss, sp, v, 8) == 0) {
            tf->eip = v[0];
            tf->cs = v[1] & 0xFFFF;
            set_sp(tf, sp + 8);
        }
    } else {
        u16 v[2];
        if (user_rd((u16)tf->ss, sp, v, 4) == 0) {
            tf->eip = v[0];
            tf->cs = v[1];
            set_sp(tf, sp + 4);
        }
    }
}

/* ---- contexts */

static void ctx_destroy(void)
{
    struct dpmi_ctx *c = dctx;
    if (!c)
        return;
    vm_wr8(c->psp * 16u + 0x2C, (u8)c->env_seg);       /* PSP:2Ch as it was */
    vm_wr8(c->psp * 16u + 0x2D, (u8)(c->env_seg >> 8));
    lin_free_all(c);
    while (c->dosblks) {                        /* DOS frees the blocks with the program */
        struct dosblk *d = c->dosblks;
        c->dosblks = d->next;
        kfree(d);
    }
    mm_space_enter(0);
    mm_space_free(c->cr3);
    cpu_set_ldt(0, 0);
    kfree(c->ldt);
    kfree(c->ldt_used);
    dctx = 0;
}

/* The far call to the mode-switch entry, from V86 mode. */
static void mode_switch(struct trapframe *tf)
{
    struct dpmi_ctx *c = &ctx_store;
    u32 sp = tf->esp & 0xFFFF, ret_ip, ret_cs, env_size;
    u16 ds = (u16)tf->v86_ds, ss = (u16)tf->ss, cs_sel, ds_sel, ss_sel;
    u8 data_flags;

    ret_ip = vm_rd16(vm_lin(tf->ss, sp));
    ret_cs = vm_rd16(vm_lin(tf->ss, sp + 2));
    SET16(tf->esp, sp + 4);
    if (dctx) {                                 /* a child's own client: M4c */
        tf->cs = ret_cs;
        tf->eip = ret_ip;
        SET16(tf->eax, 0x8011);
        tf->eflags |= FL_CF;
        dpmi_unimpl(tf, "nested-client");
        return;
    }
    memset(c, 0, sizeof *c);
    c->ldt = kmalloc(LDT_ENTRIES * 8);
    c->ldt_used = kmalloc(LDT_ENTRIES);
    c->cr3 = mm_space_new();
    if (!c->ldt || !c->ldt_used || !c->cr3) {
        if (c->ldt) kfree(c->ldt);
        if (c->ldt_used) kfree(c->ldt_used);
        if (c->cr3) mm_space_free(c->cr3);
        tf->cs = ret_cs;
        tf->eip = ret_ip;
        SET16(tf->eax, 0x8011);                 /* descriptor unavailable */
        tf->eflags |= FL_CF;
        return;
    }
    dctx = c;
    ldt_init(c);
    c->bits32 = tf->eax & 1;
    c->psp = vm_current_psp();
    c->env_seg = vm_rd16(c->psp * 16u + 0x2C);
    c->rm_seg = (u16)tf->v86_es;
    c->rm_sp = RM_STACK_PARAS * 16;
    c->next_handle = 0x1000;
    mm_space_enter(c->cr3);
    cpu_set_ldt(c->ldt, LDT_ENTRIES * 8 - 1);

    data_flags = c->bits32 ? 0x40 : 0x00;       /* Big for a 32-bit client */
    cs_sel = ldt_new(ret_cs * 16u, 0xFFFF, 0xFA, 0x00);
    ds_sel = ldt_new(ds * 16u, 0xFFFF, 0xF2, data_flags);
    ss_sel = ss == ds ? ds_sel : ldt_new(ss * 16u, 0xFFFF, 0xF2, data_flags);
    c->psp_sel = ldt_new(c->psp * 16u, 0xFFFF, 0xF2, 0x00);
    env_size = c->env_seg ? (u32)vm_rd16((c->env_seg - 1) * 16u + 3) * 16 : 16;
    c->env_sel = c->env_seg ? ldt_new(c->env_seg * 16u, env_size - 1, 0xF2, 0x00) : 0;
    vm_wr8(c->psp * 16u + 0x2C, (u8)c->env_sel);
    vm_wr8(c->psp * 16u + 0x2D, (u8)(c->env_sel >> 8));

    dpmi_to_pm(tf, cs_sel, ret_ip, ss_sel, sp + 4, ds_sel, c->psp_sel);
    tf->eflags &= ~FL_CF;
    kprintf("GLOS-DPMI start bits=%u psp=%04x cs=%04x ds=%04x ss=%04x\n", c->bits32 ? 32 : 16, c->psp, cs_sel,
            ds_sel, ss_sel);
}

void dpmi_1687(struct trapframe *tf)
{
    u32 cpu = vm.bi->cpu_family > 6 ? 6 : vm.bi->cpu_family;
    SET16(tf->eax, 0);
    SET16(tf->ebx, 1);                          /* 32-bit programs too */
    SETLO(tf->ecx, cpu);
    SET16(tf->edx, 0x005A);                     /* 0.90 */
    SET16(tf->esi, RM_STACK_PARAS);
    tf->v86_es = vm.loader_cs;
    SET16(tf->edi, vm.bi->bp_dpmi_off);
}

/* ---- the end */

static void end_client(struct trapframe *tf, u8 code, int killed)
{
    kprintf("GLOS-DPMI exit code=%u%s\n", code, killed ? " killed" : "");
    ctx_destroy();
    dpmi_to_v86(tf, vm.loader_cs, killed ? vm.bi->kill_off : vm.bi->int21_off, vm.loader_cs, vm.bi->kill_sp,
                vm.loader_cs, vm.loader_cs);
    SET16(tf->eax, 0x4C00 | code);
    vm.vif = 1;
    if (killed)
        vm_kill_now(tf);                        /* vectors and devices as the program found them */
}

/* The program's INT 21h 4Ch (or 00h) reaching DOS from V86 mode. */
void dpmi_dos_exit(struct trapframe *tf)
{
    if (!dctx || vm_current_psp() != dctx->psp)
        return;
    kprintf("GLOS-DPMI exit code=%u real-mode\n", (tf->eax >> 8) == 0x4C ? tf->eax & 0xFF : 0);
    ctx_destroy();
}

/* ---- traps from ring 3 */

static void exception(struct trapframe *tf)
{
    kprintf("GLOS-DPMI exception vec=%x err=%x at=%04x:%p\n", tf->vec, tf->err, tf->cs & 0xFFFF, tf->eip);
    end_client(tf, 0xFF, 1);
}

/* The host's own handler for vector n (the default the virtual IDT holds). */
static void host_int(struct trapframe *tf, u8 n)
{
    u32 ax = tf->eax & 0xFFFF;
    if (n == 0x31) {
        int31(tf);
    } else if (n == 0x21 && (ax >> 8) == 0x4C) {
        end_client(tf, (u8)ax, 0);
    } else if (n == 0x2F && ax == 0x1686) {
        SET16(tf->eax, 0);                      /* in protected mode */
    } else {
        rm_reflect(tf, n);
    }
}

static void soft_int(struct trapframe *tf, u8 n, u32 next)
{
    struct farptr *h = &dctx->vidt[n];
    tf->eip = next;
    if (h->sel) {                               /* the client's handler, as an interrupt gate */
        if (push_frame(tf, next) != 0) {
            exception(tf);
            return;
        }
        vm.vif = 0;
        tf->cs = h->sel;
        tf->eip = h->off;
        return;
    }
    host_int(tf, n);
}

/* A host trampoline (a HLT at SEL_TRAMP:off). */
static void trampoline(struct trapframe *tf, u32 off)
{
    if (off < 0x100) {                          /* a client chained to the host's handler */
        int ending;
        host_int(tf, (u8)off);
        ending = (tf->eflags & FL_VM) != 0;     /* INT 21h 4Ch: no frame to return through */
        if (!ending)
            iret_frame(tf, 1);
        return;
    }
    switch (off) {
    case TR_RAW:
        rm_raw_to_rm(tf);
        return;
    case TR_SAVE:
        retf(tf);
        return;
    case TR_VENDOR:
        dpmi_unimpl(tf, "glos-api");
        SET16(tf->eax, 0x8001);
        tf->eflags |= FL_CF;
        retf(tf);
        return;
    default:
        exception(tf);
    }
}

static void general_protection(struct trapframe *tf)
{
    struct v86insn in;
    u8 b[V86_MAX_LEN];
    u32 lin, n = V86_MAX_LEN, v, port;

    if ((tf->cs & 0xFFFF) == SEL_TRAMP) {
        trampoline(tf, tf->eip);
        return;
    }
    while (n && sel_lin((u16)tf->cs, tf->eip, n, &lin) != 0)
        n--;                                    /* the instruction may end at the segment's limit */
    if (!n || ucopy(b, (const void *)lin, n)) {
        exception(tf);
        return;
    }
    if ((tf->err & 3) == 2 && b[0] == 0xCD && b[1] == (tf->err >> 3)) {   /* INT n through a DPL-0 gate */
        soft_int(tf, b[1], tf->eip + 2);
        return;
    }
    v86_decode_size(b, n, &in, (cpu_desc_hi(tf->cs) & (1u << 22)) ? 4 : 2);
    port = in.port_dx ? (tf->edx & 0xFFFF) : in.imm;
    switch (in.kind) {
    case V86_CLI:
        vm.vif = 0;
        break;
    case V86_STI:
        vm.vif = 1;
        break;
    case V86_HLT:
        tf->eip += in.len;
        vm_idle(0);
        return;
    case V86_IN:
        v = vdev_in((u16)port, in.width);
        if (in.width == 1) SETLO(tf->eax, v);
        else if (in.width == 2) SET16(tf->eax, v);
        else tf->eax = v;
        break;
    case V86_OUT:
        vdev_out((u16)port, tf->eax, in.width);
        break;
    default:
        exception(tf);
        return;
    }
    tf->eip += in.len;
}

void dpmi_trap(struct trapframe *tf)
{
    u32 v = tf->vec;
    if (!dctx) {                                /* ring 3 without a client: can't happen */
        panic("ring3-without-client", tf);
    }
    if (v == 13)
        general_protection(tf);
    else if (v >= 0x20)
        soft_int(tf, (u8)v, tf->eip);           /* a DPL-3 gate: EIP is past the INT already */
    else
        exception(tf);
}

/* ---- back to ring 3 */

/* A selector the IRET or the ring-0 pops can load: 0 (data registers only),
   or present code (CS) or data (the rest) of DPL 3. */
static int loadable(u32 sel, int code)
{
    u32 hi;
    sel &= 0xFFFF;
    if (!code && sel < 4)
        return 1;
    if ((sel & 3) != 3)
        return 0;
    if ((sel & 4) && !ldt_valid((u16)sel))
        return 0;
    hi = cpu_desc_hi(sel);
    if (!(hi & 0x8000) || !(hi & 0x1000) || ((hi >> 13) & 3) != 3)
        return 0;
    if (code)
        return (hi & 0x0800) != 0;
    return !(hi & 0x0800) || (hi & 0x0200);    /* data, or readable code */
}

void dpmi_return(struct trapframe *tf)
{
    if (vm.kill_req) {
        end_client(tf, 0xFF, 1);
        vm_return(tf);
        return;
    }
    while (vm.vif && vpic_pending(&vm.pic) && dctx) {   /* IRQs: their real-mode handlers (M4a) */
        int vec = vpic_ack(&vm.pic);
        if (vec < 0)
            break;
        vm.n_irq++;
        rm_irq((u8)vec);
    }
    if (tf->eflags & FL_VM) {
        vm_return(tf);
        return;
    }
    if (!loadable(tf->ds, 0)) tf->ds = 0;
    if (!loadable(tf->es, 0)) tf->es = 0;
    if (!loadable(tf->fs, 0)) tf->fs = 0;
    if (!loadable(tf->gs, 0)) tf->gs = 0;
    if (!loadable(tf->cs, 1) || !loadable(tf->ss, 0) || (tf->ss & 0xFFFF) < 4) {
        kprintf("GLOS-DPMI bad-frame cs=%04x ss=%04x\n", tf->cs & 0xFFFF, tf->ss & 0xFFFF);
        end_client(tf, 0xFF, 1);
        vm_return(tf);
        return;
    }
    tf->eflags = (tf->eflags & ~(FL_HI | FL_VM)) | FL_IF;
}

/* ---- the stub's ARPLs from V86 mode */

int dpmi_v86_bp(struct trapframe *tf)
{
    u32 ip = tf->eip & 0xFFFF;
    const struct bootinfo *bi = vm.bi;
    if (tf->cs != vm.loader_cs)
        return 0;
    if (ip == bi->bp_dpmi_off) {
        mode_switch(tf);
        return 1;
    }
    if (ip == bi->bp_nest_off)
        return rm_nest_return(tf);
    if (ip == bi->bp_raw_off && dctx) {
        rm_raw_to_pm(tf);
        return 1;
    }
    if (ip >= bi->rmcb_off && ip < bi->rmcb_off + 2 * NRMCB && dctx) {
        dpmi_unimpl(tf, "rmcb-call");           /* M4b; meanwhile as if it returned at once */
        tf->eip = vm_rd16(vm_lin(tf->ss, tf->esp & 0xFFFF));
        tf->cs = vm_rd16(vm_lin(tf->ss, (tf->esp + 2) & 0xFFFF));
        SET16(tf->esp, tf->esp + 6);
        return 1;
    }
    return 0;
}
