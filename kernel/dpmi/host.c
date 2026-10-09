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
 * Interrupts: IRQs go to the virtual PIC as always; when the virtual IF
 * lets one through, a client's protected-mode handler gets it first, in
 * either mode (deliver.c), and the end of its chain, the host's handler,
 * runs the real-mode one nested. Exceptions go to the client's handlers
 * (deliver.c), and those it has none for end it with a crash report.
 *
 * The end: INT 21h 4Ch in protected mode frees the context and goes on in
 * V86 mode, as the program, at the stub's INT 21h with AX as it was; DOS
 * then ends the program. A program that ends from real mode (after a raw
 * switch, or inside a nested call) frees the context when DOS sees its
 * 4Ch. Any other way DOS ends it (a Ctrl-C or critical-error abort) is
 * caught at its terminate address, PSP:0Ah, which the mode switch points at
 * the stub's term ARPL (M4b). A program a client starts that switches to
 * protected mode itself joins the context as a client level (level.c, M4c),
 * and its end ends only that level. */
#include "glos/bootinfo.h"
#include "arch.h"
#include "dpmi.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "sched.h"
#include "session.h"
#include "v86dec.h"
#include "vm.h"
#include "vpic.h"

struct dpmi_ctx *dctx;
static struct dpmi_ctx ctx_store;

/* CR0.MP and EM for the code that runs next: a client's own (0E01h), or DOS's
   as GLOS found them. Written only when they change. */
u32 fpu_msw_dos = ~0u;
static u32 fpu_msw_now = ~0u;

void fpu_msw_set(u32 msw)
{
    u32 cr0;
    if (fpu_msw_dos == ~0u)
        fpu_msw_dos = fpu_msw_now = (read_cr0() >> 1) & 3;
    if (msw == fpu_msw_now)
        return;
    cr0 = read_cr0();
    write_cr0((cr0 & ~6u) | (msw & 3) << 1);
    fpu_msw_now = msw;
}
/* Terminate addresses DOS is about to go to: each client the host has seen
   end leaves its own here, for the term ARPL (they outlive the context). */
static u32 term_pend[NLEVEL];
static u32 nterm;

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

u32 sp_of(const struct trapframe *tf)
{
    return (cpu_desc_hi(tf->ss) & (1u << 22)) ? tf->esp : tf->esp & 0xFFFF;
}

void set_sp(struct trapframe *tf, u32 sp)
{
    if (cpu_desc_hi(tf->ss) & (1u << 22))
        tf->esp = sp;
    else
        SET16(tf->esp, sp);
}

/* Push an interrupt frame (FLAGS, CS, IP: 32-bit for a 32-bit handler) on
   the client's stack; -1 if the stack isn't writable there. */
static int push_frame(struct trapframe *tf, u32 eip, int b32)
{
    u32 f = (tf->eflags & (FL_ARITH | FL_TF | FL_AC)) | (vm.vif ? FL_IF : 0) | 2, sp = sp_of(tf);
    if (b32) {
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
static void iret_frame(struct trapframe *tf, int keep, int b32)
{
    u32 sp = sp_of(tf), eip, cs, f;
    if (b32) {
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

void dpmi_iret(struct trapframe *tf) { iret_frame(tf, 0, dctx->bits32); }

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

/* psp: the PSP is still the program's (not when DOS has ended it). */
static void ctx_destroy(int psp)
{
    struct dpmi_ctx *c = dctx;
    if (!c)
        return;
    if (psp) {
        vm_wr8(c->psp * 16u + 0x2C, (u8)c->env_seg);   /* PSP:2Ch as it was */
        vm_wr8(c->psp * 16u + 0x2D, (u8)(c->env_seg >> 8));
    }
    lin_free_all(c);
    while (c->dosblks) {                        /* DOS frees the blocks with the program */
        struct dosblk *d = c->dosblks;
        c->dosblks = d->next;
        kfree(d);
    }
    wp_clear_level(1);
    mm_space_enter(0);
    mm_space_free(c->cr3);
    cpu_set_ldt(0, 0);
    kfree(c->ldt_mem);
    kfree(c->ldt_used);
    dctx = 0;
}

static void switch_fail(struct trapframe *tf, u32 ret_cs, u32 ret_ip, u32 err)
{
    tf->cs = ret_cs;
    tf->eip = ret_ip;
    SET16(tf->eax, err);
    tf->eflags |= FL_CF;
}

/* The far call to the mode-switch entry, from V86 mode: a new context, or,
   from a program a client started, a new level in that client's (M4c). */
static void mode_switch(struct trapframe *tf)
{
    struct dpmi_ctx *c = &ctx_store;
    u32 sp = tf->esp & 0xFFFF, ret_ip, ret_cs, env_size, e;
    u16 ds = (u16)tf->v86_ds, ss = (u16)tf->ss, cs_sel, ds_sel, ss_sel;
    u8 data_flags;
    int child = dctx != 0;

    ret_ip = vm_rd16(vm_lin(tf->ss, sp));
    ret_cs = vm_rd16(vm_lin(tf->ss, sp + 2));
    SET16(tf->esp, sp + 4);
    if (child) {
        if ((e = (u32)level_push(tf)) != 0) {
            switch_fail(tf, ret_cs, ret_ip, e);
            return;
        }
    } else {
        memset(c, 0, sizeof *c);
        c->ldt_mem = kmalloc(LDT_ENTRIES * 8 + 4096);
        c->ldt = c->ldt_mem ? (u32 *)(((u32)c->ldt_mem + 4095) & ~4095u) : 0;
        c->ldt_used = kmalloc(LDT_ENTRIES);
        c->cr3 = mm_space_new();
        if (!c->ldt || !c->ldt_used || !c->cr3) {
            if (c->ldt_mem) kfree(c->ldt_mem);
            if (c->ldt_used) kfree(c->ldt_used);
            if (c->cr3) mm_space_free(c->cr3);
            switch_fail(tf, ret_cs, ret_ip, 0x8011);    /* descriptor unavailable */
            return;
        }
        dctx = c;
        level_init(tf);
        ldt_init(c);
        c->bits32 = tf->eax & 1;
        c->next_handle = 0x1000;
        c->frame_cap = session_memory_kb() / 4; /* (in pages) */
        fpu_msw_set(fpu_msw_dos == ~0u ? (read_cr0() >> 1) & 3 : fpu_msw_dos);   /* (learns DOS's first) */
        c->fpu_msw = (u8)(fpu_msw_dos & ~2u);   /* the FPU, not enabled for it until 0E01h ([DPMI0.9]) */
        mm_space_enter(c->cr3);
        cpu_set_ldt(c->ldt, LDT_ENTRIES * 8 - 1);
    }
    c->psp = vm_current_psp();
    c->env_seg = vm_rd16(c->psp * 16u + 0x2C);
    c->rm_seg = (u16)tf->v86_es;
    c->rm_sp = RM_STACK_PARAS * 16;

    data_flags = c->bits32 ? 0x40 : 0x00;       /* Big for a 32-bit client */
    cs_sel = ldt_new(ret_cs * 16u, 0xFFFF, 0xFA, 0x00);
    ds_sel = ldt_new(ds * 16u, 0xFFFF, 0xF2, data_flags);
    ss_sel = ss == ds ? ds_sel : ldt_new(ss * 16u, 0xFFFF, 0xF2, data_flags);
    c->psp_sel = ldt_new(c->psp * 16u, 0xFFFF, 0xF2, 0x00);
    env_size = c->env_seg ? (u32)vm_rd16((c->env_seg - 1) * 16u + 3) * 16 : 16;
    c->env_sel = c->env_seg ? ldt_new(c->env_seg * 16u, env_size - 1, 0xF2, 0x00) : 0;
    if (!child) {
        if (lin_host(LSTACK_LIN, LSTACK_SIZE) != 0) {   /* the locked stack (§14.2), for every level */
            kprintf("GLOS-DPMI start failed why=memory\n");
            ctx_destroy(1);
            switch_fail(tf, ret_cs, ret_ip, 0x8013);
            return;
        }
        c->lsel[c->bits32] = ldt_new(LSTACK_LIN, LSTACK_SIZE - 1, 0xF2, data_flags);
    }
    vm_wr8(c->psp * 16u + 0x2C, (u8)c->env_sel);
    vm_wr8(c->psp * 16u + 0x2D, (u8)(c->env_sel >> 8));
    c->term_vec = vm_rd32(c->psp * 16u + 0x0A);    /* DOS goes there when the program ends, however it ends */
    vm_wr8(c->psp * 16u + 0x0A, (u8)vm.bi->bp_term_off);
    vm_wr8(c->psp * 16u + 0x0B, (u8)(vm.bi->bp_term_off >> 8));
    vm_wr8(c->psp * 16u + 0x0C, (u8)vm.loader_cs);
    vm_wr8(c->psp * 16u + 0x0D, (u8)(vm.loader_cs >> 8));

    dpmi_to_pm(tf, cs_sel, ret_ip, ss_sel, sp + 4, ds_sel, c->psp_sel);
    tf->eflags &= ~FL_CF;
    kprintf("GLOS-DPMI start bits=%u psp=%04x cs=%04x ds=%04x ss=%04x level=%u term=%08x parent=%04x\n",
            c->bits32 ? 32 : 16, c->psp, cs_sel, ds_sel, ss_sel, c->nlv, c->term_vec, vm_rd16(c->psp * 16u + 0x16));
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

/* The running client level ends: the level, or with the first client the
   context. *depth and *f: where its V86 code goes on (level.c). seen: DOS
   has yet to end the program, and will go to its terminate address. */
static void end_level(int psp, int seen, int *depth, struct trapframe **f)
{
    struct dpmi_level *l = &dctx->lv[dctx->nlv - 1];
    *depth = l->depth;
    *f = l->frame;
    if (seen && nterm < NLEVEL)
        term_pend[nterm++] = dctx->term_vec;
    if (dctx->nlv > 1)
        level_pop(psp);
    else
        ctx_destroy(psp);
}

/* The end from protected mode: INT 21h 4Ch, a kill, a crash. The program
   goes on in V86 mode at the stub's INT 21h 4Ch (or its kill) as itself,
   in the frame it switched in: deeper nested calls (it ended in a handler
   run from V86 mode, or inside its own real-mode call) are dropped. */
void dpmi_end(struct trapframe *tf, u8 code, int killed)
{
    struct trapframe *f;
    int d;
    kprintf("GLOS-DPMI exit code=%u%s level=%u\n", code, killed ? " killed" : "", dctx->nlv);
    end_level(1, 1, &d, &f);
    dpmi_to_v86(tf, vm.loader_cs, killed ? vm.bi->kill_off : vm.bi->int21_off, vm.loader_cs, vm.bi->kill_sp,
                vm.loader_cs, vm.loader_cs);
    SET16(tf->eax, 0x4C00 | code);
    vm.vif = 1;
    if (killed)
        vm_kill_now(tf);                        /* vectors and devices as the program found them */
    if (rm_nesting() > d) {
        vm_return(tf);
        rm_unwind_to(d, f, tf);
    }
}

/* A program's INT 21h 4Ch (or 00h) reaching DOS from V86 mode (n, next: the
   INT about to run): if it is the running client, its level ends; from
   inside its own nested calls, the INT runs in the frame it switched in. */
/* INT 21h 31h from the first client's own program, in either mode: the
   program stays resident, and its context stays with it (Borland's RTM:
   TPX.EXE runs RTM.EXE, which switches to protected mode, sets itself up
   and goes resident; TPX then reaches it through its INT 2Fh, and RTM
   raw-switches back into that context; M4d). */
void dpmi_tsr_seen(void)
{
    if (dctx && dctx->nlv == 1 && vm_current_psp() == dctx->psp)
        dctx->tsr_pending = 1;
}

void dpmi_dos_exit(struct trapframe *tf, u8 n, u32 next)
{
    struct trapframe *f;
    int d;
    if (dctx && dctx->resident_parent && dctx->nlv == 1 && vm_current_psp() == dctx->resident_parent) {
        kprintf("GLOS-DPMI exit resident psp=%04x with=%04x\n", dctx->psp, dctx->resident_parent);
        if (dctx->xbuf_seg) {                   /* (a block of the resident PSP's: DOS keeps it) */
            struct rmregs r;
            memset(&r, 0, sizeof r);
            r.eax = 0x4900;
            r.es = dctx->xbuf_seg;
            r.flags = 2;
            rm_call(tf, &r, RM_INT, 0x21, 0, 0);
        }
        end_level(0, 0, &d, &f);                /* (its PSP may be gone: no PSP:2Ch to put back) */
        if (rm_nesting() > d) {
            vm_int(tf, n, next);
            rm_unwind_to(d, f, tf);
        }
        return;
    }
    if (!dctx || vm_current_psp() != dctx->psp)
        return;
    kprintf("GLOS-DPMI exit code=%u real-mode level=%u\n", (tf->eax >> 8) == 0x4C ? tf->eax & 0xFF : 0, dctx->nlv);
    end_level(1, 1, &d, &f);
    if (rm_nesting() > d) {
        vm_int(tf, n, next);
        rm_unwind_to(d, f, tf);
    }
}

/* ---- traps from ring 3 */

/* The host's own handler for vector n (the default the virtual IDT holds). */
static void host_int(struct trapframe *tf, u8 n)
{
    u32 ax = tf->eax & 0xFFFF;
    if (n == 0x21 && (ax >> 8) == 0x31)
        dpmi_tsr_seen();                        /* (then reflected as any other) */
    if (n == 0x31) {
        int31(tf);
    } else if (n == 0x21 && (ax >> 8) == 0x4C) {
        dpmi_end(tf, (u8)ax, 0);
    } else if (n == 0x21 && dosx_int21(tf)) {
        /* a 16-bit client's call, translated (dosx.c) */
    } else if (n == 0x41) {
        /* the kernel debugger's notifications (DS_*; HX's loader sends them):
           no debugger, nothing to do. Real mode's 41h is a BIOS disk table. */
    } else if (n == 0x2F && ax == 0x168A) {     /* 1.0's vendor entries: "GLOS", Windows' "MS-DOS" */
        dpmi_vendor_2f(tf);
    } else if (n == 0x2F && ax == 0x1686) {
        SET16(tf->eax, 0);                      /* in protected mode */
    } else if (n == 0x2F && ax == 0x1680) {
        thread_yield();                         /* a real yield: the kernel's threads run; AL stays 80h (§13) */
    } else if (n == 0x2F && (ax == 0x1600 || ax == 0x160A)) {
        if (ax == 0x1600)                       /* never Windows: GLQuake gives up under it */
            SETLO(tf->eax, 0);
    } else {
        rm_reflect(tf, n);
    }
}

void pm_soft_int(struct trapframe *tf, u8 n, u32 next)
{
    struct farptr *h = &dctx->vidt[n];
    tf->eip = next;
    if (h->sel) {                               /* the client's handler, the virtual IF as it was: */
        if (push_frame(tf, next, h->b32) != 0) {        /* its IRET can't give it back at IOPL 0 (§14.3) */
            crash_report(tf, "no-stack");
            return;
        }
        tf->cs = h->sel;
        tf->eip = h->off;
        return;
    }
    host_int(tf, n);
}

/* A host trampoline (a HLT at SEL_TRAMP:off). */
static void trampoline(struct trapframe *tf, u32 off)
{
    if (off < TR_EXC) {                         /* a client chained to the host's handler */
        int ending;
        host_int(tf, (u8)off);
        ending = (tf->eflags & FL_VM) != 0;     /* INT 21h 4Ch: no frame to return through */
        if (!ending)                            /* the frame the chain's first handler got */
            iret_frame(tf, 1, dctx->vidt[off].sel ? dctx->vidt[off].b32 : dctx->bits32);
        return;
    }
    if (off < TR_EXC + 32) {
        dpmi_exc_default(tf, off - TR_EXC);
        return;
    }
    if (off >= TR_RET && off < TR_RET10 + NENTRY) {
        dpmi_entry_return(tf, (off - TR_RET) % NENTRY, off >= TR_RET10);
        return;
    }
    switch (off) {
    case TR_RAW:
        rm_raw_to_rm(tf);
        return;
    case TR_SAVE:
        retf(tf);
        return;
    case TR_MSDOS:                              /* Windows' "MS-DOS" extension: 0100h only */
        if ((tf->eax & 0xFFFF) == 0x0100 && ldt_alias_sel()) {
            SET16(tf->eax, dctx->ldt_alias);
            tf->eflags &= ~FL_CF;
        } else {
            tf->eflags |= FL_CF;
        }
        retf(tf);
        return;
    case TR_VENDOR:
        dpmi_unimpl(tf, "glos-api");
        SET16(tf->eax, 0x8001);
        tf->eflags |= FL_CF;
        retf(tf);
        return;
    default:
        crash_report(tf, "trampoline");
    }
}

static int at_return(const struct trapframe *tf)
{
    return dctx && !(tf->eflags & FL_VM) && (tf->cs & 0xFFFF) == SEL_TRAMP && tf->eip >= TR_RET
           && tf->eip < TR_RET10 + NENTRY;
}

static void general_protection(struct trapframe *tf)
{
    struct v86insn in;
    u8 b[V86_MAX_LEN];
    u32 lin, n = V86_MAX_LEN, v, port;

    if ((tf->cs & 0xFFFF) == SEL_TRAMP) {
        int k = 0;
        do                                      /* a chain to the host's handler often ends in an entry's return */
            trampoline(tf, tf->eip);
        while (at_return(tf) && ++k < 4);
        return;
    }
    while (n && sel_lin((u16)tf->cs, tf->eip, n, &lin) != 0)
        n--;                                    /* the instruction may end at the segment's limit */
    if (!n || ucopy(b, (const void *)lin, n)) {
        dpmi_exception(tf);
        return;
    }
    if ((tf->err & 3) == 2 && b[0] == 0xCD && b[1] == (tf->err >> 3)) {   /* INT n through a DPL-0 gate */
        pm_soft_int(tf, b[1], tf->eip + 2);
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
        dpmi_exception(tf);
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
    if (v == 14 && lin_fault(read_cr2()))
        return;                                 /* a page of its memory backed on its first touch */
    if (v == 14)
        dctx->cr2 = read_cr2();                 /* before anything else can fault */
    if (v == 1)
        dpmi_db_hit();                          /* a watchpoint's (0B00h), or a single step */
    if (v == 13)
        general_protection(tf);
    else if (v >= 0x20)
        pm_soft_int(tf, (u8)v, tf->eip);        /* a DPL-3 gate: EIP is past the INT already */
    else
        dpmi_exception(tf);
}

/* ---- back to ring 3 */

/* A selector the IRET or the ring-0 pops can load: 0 (data registers only),
   or present code (CS) or data (the rest) of DPL 3. CS and SS need RPL 3;
   a data register may hold RPL 0, as the BIOS selector 0040h does (M4c). */
enum { SEG_DATA, SEG_CODE, SEG_STACK };

static int loadable(u32 sel, int kind)
{
    u32 hi;
    int code = kind == SEG_CODE;
    sel &= 0xFFFF;
    if (kind == SEG_DATA && sel < 4)
        return 1;
    if ((sel & 3) != 3 && kind != SEG_DATA)
        return 0;
    if ((sel & 4) && !ldt_valid((u16)sel))
        return 0;
    hi = cpu_desc_hi(sel);
    if (!(hi & 0x8000) || !(hi & 0x1000) || ((hi >> 13) & 3) != 3)
        return 0;
    if (code)
        return (hi & 0x0800) != 0;
    if (kind == SEG_STACK)
        return !(hi & 0x0800) && (hi & 0x0200);    /* writable data only: the IRET would fault in ring 0 */
    return !(hi & 0x0800) || (hi & 0x0200);    /* data, or readable code */
}

void dpmi_return(struct trapframe *tf)
{
    if (vm.kill_req) {
        dpmi_end(tf, 0xFF, 1);
        vm_return(tf);
        return;
    }
    while (vm.vif && vpic_pending(&vm.pic) && dctx) {
        int vec = vpic_ack(&vm.pic);
        if (vm.direct)
            vm_sync_mask();                     /* (direct mode: ended at once, v86.c) */
        if (vec < 0)
            break;
        vm.n_irq++;
        if (dpmi_irq(tf, (u8)vec))              /* the client's handler first, its virtual IF off */
            break;
        rm_irq(tf, (u8)vec);
    }
    if (tf->eflags & FL_VM) {
        vm_return(tf);
        return;
    }
    if (!loadable(tf->ds, SEG_DATA)) tf->ds = 0;
    if (!loadable(tf->es, SEG_DATA)) tf->es = 0;
    if (!loadable(tf->fs, SEG_DATA)) tf->fs = 0;
    if (!loadable(tf->gs, SEG_DATA)) tf->gs = 0;
    if (!loadable(tf->cs, SEG_CODE) || !loadable(tf->ss, SEG_STACK)) {
        kprintf("GLOS-DPMI bad-frame cs=%04x ss=%04x\n", tf->cs & 0xFFFF, tf->ss & 0xFFFF);
        crash_report(tf, "bad-frame");
        vm_return(tf);
        return;
    }
    tf->eflags = (tf->eflags & ~(FL_HI | FL_VM)) | FL_IF;
    fpu_msw_set(dctx->fpu_msw);
    if (dctx->db_rf) {                          /* back at an execute watchpoint that fired: past it once */
        if (sel_base((u16)tf->cs) + tf->eip == dctx->db_rf) {
            tf->eflags |= 0x10000;              /* RF */
            dctx->db_rf = 0;
        } else if (!dctx->npe) {                /* (not into its handler: until that returns) */
            dctx->db_rf = 0;
        }
    }
}

/* ---- the stub's ARPLs from V86 mode */

/* DOS ended a client's program and went to its terminate address (the
   term ARPL): on to the program's own. If the host saw the end coming
   (term_pend), that's all; otherwise it was an abort (a critical error, a
   TSR exit) of the running level, which ends now, its vectors and devices
   as a kill leaves them, from wherever in its nested calls it was. */
static void terminated(struct trapframe *tf)
{
    struct trapframe *f;
    u32 vec;
    int d;
    if (dctx && dctx->tsr_pending) {            /* INT 21h 31h: resident, context and all */
        dctx->tsr_pending = 0;
        dctx->resident_parent = vm_current_psp();       /* DOS made its parent current */
        vec = dctx->term_vec;
        kprintf("GLOS-DPMI resident psp=%04x parent=%04x level=%u\n", dctx->psp, dctx->resident_parent, dctx->nlv);
        tf->cs = vec >> 16;
        tf->eip = vec & 0xFFFF;
        return;
    }
    if (nterm) {
        vec = term_pend[--nterm];
        tf->cs = vec >> 16;
        tf->eip = vec & 0xFFFF;
        return;
    }
    if (!dctx) {
        kprintf("GLOS-DPMI term-unknown\n");
        return;
    }
    vec = dctx->term_vec;
    kprintf("GLOS-DPMI exit terminated to=%08x psp=%04x restored=%u level=%u\n", vec, vm_current_psp(),
            vm_restore_child(), dctx->nlv);
    end_level(0, 0, &d, &f);
    tf->cs = vec >> 16;
    tf->eip = vec & 0xFFFF;
    if (rm_nesting() > d)
        rm_unwind_to(d, f, tf);
}

int dpmi_v86_bp(struct trapframe *tf)
{
    u32 ip = tf->eip & 0xFFFF;
    const struct bootinfo *bi = vm.bi;
    if (tf->cs != vm.loader_cs)
        return 0;
    if (ip == bi->bp_term_off) {
        terminated(tf);
        return 1;
    }
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
    if (ip >= bi->rmcb_off && ip < bi->rmcb_off + 2 * NRMCB && !((ip - bi->rmcb_off) & 1) && dctx) {
        if (dctx->rmcb[(ip - bi->rmcb_off) / 2].used && !dctx->ending) {
            dpmi_rmcb(tf, (ip - bi->rmcb_off) / 2);
            return 1;
        }
        if (!dctx->ending)                      /* a callback since freed, or one while the crash report */
            dpmi_unimpl(tf, "rmcb-free");       /* is written: as if it returned at once */
        tf->eip = vm_rd16(vm_lin(tf->ss, tf->esp & 0xFFFF));
        tf->cs = vm_rd16(vm_lin(tf->ss, (tf->esp + 2) & 0xFFFF));
        SET16(tf->esp, tf->esp + 6);
        return 1;
    }
    return 0;
}
