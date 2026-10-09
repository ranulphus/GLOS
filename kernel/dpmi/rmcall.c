/* Real-mode execution for the DPMI host (supervisor.md §9.5, §15; 0300h-
 * 0302h, 0306h). Only the system VM thread runs V86 code, so a real-mode
 * call from protected mode runs nested on that thread: a V86 frame built
 * below the caller's kernel stack, entered with nest_enter() and left when
 * the code comes back to the stub's nest breakpoint, which returns to the
 * caller with nest_leave(). Traps from the nested code build their frames
 * on that frame (ESP0 follows it, kernel/arch/cpu.c), so the caller's own
 * stack is never touched, and the scheduler may switch threads meanwhile.
 *
 * The real-mode stack, when the caller gives none (SS:SP 0), is the host's:
 * the block the client handed over at the mode switch (1687h's SI), one
 * 512-byte slice per nesting level.
 *
 * The raw switches (0306h) change the VM thread's mode at the top level:
 * the stub's raw ARPL goes to protected mode, SEL_TRAMP:TR_RAW back. */
#include <string.h>

#include "glos/bootinfo.h"
#include "arch.h"
#include "dpmi.h"
#include "kprintf.h"
#include "sched.h"
#include "vm.h"

#define NEST_MAX 8

static struct {
    u32 save;                                   /* nest_enter's stack, for nest_leave */
    struct trapframe *f;
    struct trapframe *from;                     /* the protected-mode frame that called, or 0 */
} nests[NEST_MAX];
static int depth;

int rm_nesting(void) { return depth; }

struct trapframe *rm_pm_caller(void) { return depth ? nests[depth - 1].from : 0; }

static void push16(u32 ss, u32 *sp, u32 v)
{
    *sp = (*sp - 2) & 0xFFFF;
    vm_wr8(vm_lin(ss, *sp), (u8)v);
    vm_wr8(vm_lin(ss, *sp + 1), (u8)(v >> 8));
}

int rm_call(struct trapframe *from, struct rmregs *r, int kind, u8 vec, const u16 *words, u32 nwords)
{
    struct trapframe *f;
    u32 here, ss, sp, i, flags, ivt, vif = vm.vif;

    if (depth >= NEST_MAX || (!dctx && !r->ss && !r->sp))
        return -1;                              /* (without a client, the caller gives a stack: sessions) */
    __asm__ volatile("movl %%esp, %0" : "=r"(here));
    f = (struct trapframe *)((here - 256 - sizeof *f) & ~15u);
    memset(f, 0, sizeof *f);
    if (r->ss || r->sp) {
        ss = r->ss;
        sp = r->sp;
    } else {
        ss = dctx->rm_seg;
        sp = (dctx->rm_sp - (u32)depth * 512) & 0xFFFF;
    }
    for (i = nwords; i-- > 0;)                  /* the caller's words: words[0] on top */
        push16(ss, &sp, words[i]);
    flags = r->flags;
    if (kind != RM_FAR)                         /* an interrupt's frame: FLAGS too */
        push16(ss, &sp, flags);
    push16(ss, &sp, vm.loader_cs);
    push16(ss, &sp, vm.bi->bp_nest_off);
    if (kind == RM_INT) {
        ivt = vm_rd32(vec * 4u);
        f->cs = ivt >> 16;
        f->eip = ivt & 0xFFFF;
    } else {
        f->cs = r->cs;
        f->eip = r->ip;
    }
    if (kind != RM_FAR) {                       /* handlers start with IF and TF clear */
        flags &= ~(FL_IF | FL_TF);
        vm.vif = 0;
    }                                           /* a far procedure keeps the caller's IF ([DPMI0.9] gives
                                                   the structure's FLAGS no part in 0301h; as HDPMI32i) */
    f->eflags = (flags & FL_ARITH) | FL_VM | FL_IF | 2;
    if (vm.vme && vm.vif)                       /* (nest_enter's IRET skips vm_return(), which sets VIF) */
        f->eflags |= FL_VIF;
    f->ss = ss;
    f->esp = sp;
    f->v86_ds = r->ds;
    f->v86_es = r->es;
    f->v86_fs = r->fs;
    f->v86_gs = r->gs;
    f->eax = r->eax;
    f->ebx = r->ebx;
    f->ecx = r->ecx;
    f->edx = r->edx;
    f->esi = r->esi;
    f->edi = r->edi;
    f->ebp = r->ebp;

    nests[depth].f = f;
    nests[depth].from = from && !(from->eflags & FL_VM) ? from : 0;
    depth++;
    current->esp0 = (u32)f + sizeof *f;
    cpu_set_esp0(current->esp0);
    nest_enter(f, &nests[depth - 1].save);      /* comes back from rm_nest_return() */
    depth--;

    r->eax = f->eax;
    r->ebx = f->ebx;
    r->ecx = f->ecx;
    r->edx = f->edx;
    r->esi = f->esi;
    r->edi = f->edi;
    r->ebp = f->ebp;
    r->ds = (u16)f->v86_ds;
    r->es = (u16)f->v86_es;
    r->fs = (u16)f->v86_fs;
    r->gs = (u16)f->v86_gs;
    r->flags = (u16)((f->eflags & FL_ARITH) | (vm.vif ? FL_IF : 0) | 2);
    vm.vif = vif;
    return 0;
}

int rm_nest_return(struct trapframe *tf)
{
    if (!depth || tf != nests[depth - 1].f)
        return 0;
    nest_leave(nests[depth - 1].save, 1);
}

/* The real-mode vector for a reflection from the host's own handler: the
   IVT's, unless that is one of the client's callbacks (DOS/4GW points the
   IVT at an RMCB that calls its PM handler: the "automatic pass-up"), when
   it is the vector the callback or the PM hook took over (§14.5). */
u32 rm_vector(u8 vec)
{
    u32 v = vm_rd32(vec * 4u), ip = v & 0xFFFF;
    if ((v >> 16) == vm.loader_cs && ip >= vm.bi->rmcb_off && ip < vm.bi->rmcb_off + 2 * NRMCB)
        return dctx->rm_prev[vec];
    return v;
}

/* A software INT from protected mode that the client hasn't hooked: to
   real mode with the general registers and the arithmetic flags; DS and ES
   there are the host's segment ([DPMI0.9]: segment registers don't go
   through). */
void rm_reflect(struct trapframe *tf, u8 vec)
{
    static u32 traced;
    struct rmregs r;
    u32 v = rm_vector(vec);
    if ((vm.bi->flags & BI_F_DPMITRACE) && traced++ < 200)     /* /DPMITRACE: the first ones */
        kprintf("GLOS-DPMI reflect int=%02x ax=%04x bx=%04x cx=%04x dx=%08x from=%04x:%08x\n", vec,
                tf->eax & 0xFFFF, tf->ebx & 0xFFFF, tf->ecx & 0xFFFF, tf->edx, tf->cs & 0xFFFF, tf->eip);
    memset(&r, 0, sizeof r);
    if (!v)
        return;                                 /* nowhere to go: as if handled */
    r.eax = tf->eax;
    r.ebx = tf->ebx;
    r.ecx = tf->ecx;
    r.edx = tf->edx;
    r.esi = tf->esi;
    r.edi = tf->edi;
    r.ebp = tf->ebp;
    r.flags = (u16)((tf->eflags & FL_ARITH) | (vm.vif ? FL_IF : 0) | 2);
    r.ds = r.es = dctx->rm_seg;
    r.cs = (u16)(v >> 16);
    r.ip = (u16)v;
    if (rm_call(tf, &r, RM_IRET, vec, 0, 0) != 0)
        return;
    tf->eax = r.eax;
    tf->ebx = r.ebx;
    tf->ecx = r.ecx;
    tf->edx = r.edx;
    tf->esi = r.esi;
    tf->edi = r.edi;
    tf->ebp = r.ebp;
    tf->eflags = (tf->eflags & ~FL_ARITH) | (r.flags & FL_ARITH);
}

void rm_irq(struct trapframe *tf, u8 vec)
{
    struct rmregs r;
    u32 v = rm_vector(vec);
    memset(&r, 0, sizeof r);
    if (!v)
        return;
    r.flags = 2;
    r.ds = r.es = dctx->rm_seg;
    r.cs = (u16)(v >> 16);
    r.ip = (u16)v;
    rm_call(tf, &r, RM_IRET, vec, 0, 0);
}

/* 0306h, real to protected: AX = DS, CX = ES, DX = SS, (E)BX = (E)SP,
   SI = CS, (E)DI = (E)IP, all selectors; EBP is kept. */
/* /DPMITRACE: the first raw switches, either way, and where they go. */
static void raw_trace(const struct trapframe *tf, const char *to)
{
    static u32 n;
    if ((vm.bi->flags & BI_F_DPMITRACE) && n++ < 40)
        kprintf("GLOS-DPMI raw to=%s from=%04x:%08x cs:ip=%04x:%08x ss:sp=%04x:%08x ds=%04x es=%04x\n", to,
                tf->cs & 0xFFFF, tf->eip, tf->esi & 0xFFFF, tf->edi, tf->edx & 0xFFFF, tf->ebx, tf->eax & 0xFFFF,
                tf->ecx & 0xFFFF);
}

void rm_raw_to_pm(struct trapframe *tf)
{
    u32 esp = dctx->bits32 ? tf->ebx : tf->ebx & 0xFFFF, eip = dctx->bits32 ? tf->edi : tf->edi & 0xFFFF;
    raw_trace(tf, "pm");
    dpmi_to_pm(tf, (u16)tf->esi, eip, (u16)tf->edx, esp, (u16)tf->eax, (u16)tf->ecx);
}

/* 0306h, protected to real: the same registers, as segments. */
void rm_raw_to_rm(struct trapframe *tf)
{
    raw_trace(tf, "rm");
    dpmi_to_v86(tf, (u16)tf->esi, tf->edi & 0xFFFF, (u16)tf->edx, tf->ebx & 0xFFFF, (u16)tf->eax, (u16)tf->ecx);
}

/* A client's program ended inside nested calls of its own (its INT 21h
   4Ch, a kill or an abort reached DOS from real mode, or it ended in a
   handler): what those levels left on the kernel stack is dropped, and the
   V86 code carries on in frame f, the one the client switched in, at the
   nesting depth it switched at (0 and the VM frame for a first client; its
   parent's EXEC call for a child). */
void rm_unwind_to(int d, struct trapframe *f, struct trapframe *tf)
{
    memmove(f, tf, sizeof *f);
    depth = d;
    current->esp0 = (u32)f + sizeof *f;
    cpu_set_esp0(current->esp0);
    frame_resume(f);
}
