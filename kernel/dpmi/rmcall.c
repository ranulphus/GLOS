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
 * RM_SLICE per nesting level, NEST_MAX of them. (Until 2026-11-03 the block
 * was 2 KB, four slices: a fifth nested call, an IRQ's INT 1Ch under
 * DOS/4GW, wrapped SP and wrote 62 KB past the block.) A call with no slice
 * left is refused, never wrapped.
 *
 * The raw switches (0306h) change the VM thread's mode at the top level:
 * the stub's raw ARPL goes to protected mode, SEL_TRAMP:TR_RAW back. */
#include <string.h>

#include "glos/bootinfo.h"
#include "agent.h"
#include "app.h"
#include "arch.h"
#include "cache.h"
#include "display.h"
#include "dpmi.h"
#include "kprintf.h"
#include "sched.h"
#include "session.h"
#include "shot.h"
#include "timer.h"
#include "vm.h"

#define NEST_MAX (RM_STACK_PARAS * 16 / RM_SLICE)

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
    u32 here, ss, sp, i, flags, ivt, vif = current->vif, ax = r->eax, bx = r->ebx;

    if (current != vm.thread)                   /* an app's: the VM thread makes it, when the system VM is idle */
        return app_rm_call(from, r, kind, vec, words, nwords);
    if (vec == 0x15 && kind == RM_INT && (u8)(r->eax >> 8) != 0x86) {   /* GLOS's INT 15h (int15.c: A20, the */
        struct trapframe t;                     /* block move's guard, no extended memory to report, the joystick), */
        memset(&t, 0, sizeof t);                /* as a V86 INT 15h gets it; a client's simulated one went to the */
        t.eax = r->eax, t.ebx = r->ebx, t.ecx = r->ecx, t.edx = r->edx;     /* BIOS until 2026-11-05. (86h's wait */
        t.esi = r->esi, t.edi = r->edi, t.v86_es = r->es, t.v86_ds = r->ds; /* runs the INT again until its */
        t.eflags = r->flags;                    /* deadline, in V86: the BIOS's, on the VM's virtual RTC) */
        if (vm_int15(&t, 0)) {
            r->eax = t.eax, r->ebx = t.ebx, r->ecx = t.ecx, r->edx = t.edx, r->esi = t.esi, r->edi = t.edi;
            r->flags = (u16)((r->flags & ~(FL_CF | FL_ZF)) | (t.eflags & (FL_CF | FL_ZF)));
            return 0;
        }
    }
    if (depth >= NEST_MAX || (!dctx && !r->ss && !r->sp))
        return -1;                              /* (without a client, the caller gives a stack: sessions) */
    if (vec == 0x13 && kind != RM_FAR && cache_int13(from, r, kind))
        return 0;                               /* the disk cache's (0300h, a client's INT 13h reflected) */
    if (vec == 0x21 && kind != RM_FAR) {        /* the hooks first, on a frame in this function's own: below */
        struct trapframe t;                     /* `here` is the nested frame's place, 256 bytes down, which a */
        memset(&t, 0, sizeof t);                /* call made after would overwrite (the watches' did, M9) */
        t.v86_ds = r->ds;
        t.v86_es = r->es;
        t.eax = r->eax;
        t.ebx = r->ebx;
        t.ecx = r->ecx;
        t.edx = r->edx;
        t.esi = r->esi;
        t.edi = r->edi;
        t.eflags = r->flags;
        for (;;) {                              /* a job's console input from its app (API 2.0's JOB; as v86.c's) */
            struct rmregs k;
            int s = agent_vm_stdin(&t);
            if (s == 1) {                       /* answered: DOS is not asked */
                r->eax = t.eax;
                r->flags = (u16)((r->flags & ~(FL_CF | FL_ZF)) | (t.eflags & (FL_CF | FL_ZF)));
                return 0;
            }
            if (s != 2 || vm.kill_req)
                break;
            if (!vm.indos || !vm_rd8(vm.indos))
                app_vm_idle_turn(0);            /* (apps' own DOS calls meanwhile) */
            {                                   /* nothing written yet: a wait in real mode, where the program's */
                u32 off = __builtin_offsetof(struct stub_data, comspec) + 76;  /* interrupts are taken (its timer, */
                u32 code = ((u32)vm.loader_cs << 4) + off;      /* Ctrl+C from the keyboard): STI, HLT, RETF in the */
                vm_wr8(code, 0xFB);             /* stub's own segment (the end of its COMSPEC buffer, past the */
                vm_wr8(code + 1, 0xF4);         /* INT 10h stub of v86.c). No BIOS call: nothing of the PC's own */
                vm_wr8(code + 2, 0xCB);         /* runs for the wait */
                k = *r;
                k.cs = vm.loader_cs;
                k.ip = (u16)off;
                k.ss = k.sp = 0;
                if (rm_call(from, &k, RM_FAR, 0, 0, 0) != 0)
                    break;
            }
        }
        agent_vm_int21(&t);                     /* a client's DOS output, for an agent job's capture (§17.4) */
    }
    if (vec == 0x10 && kind != RM_FAR) {
        shot_int10_begin();                     /* (shot_int10 at its return, below) */
        if (!((ax >> 8) & 0xFF) || (ax & 0xFFFF) == 0x4F02)
            session_video();                    /* a mode set: the program is a session for sure (session.c) */
    }
    __asm__ volatile("movl %%esp, %0" : "=r"(here));
    f = (struct trapframe *)((here - 256 - sizeof *f) & ~15u);
    memset(f, 0, sizeof *f);
    if (r->ss || r->sp) {
        ss = r->ss;
        sp = r->sp;
    } else {
        if ((u32)(depth + 1) * RM_SLICE > dctx->rm_sp)
            return -1;                          /* (no slice left) */
        ss = dctx->rm_seg;
        sp = dctx->rm_sp - (u32)depth * RM_SLICE;
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
        current->vif = 0;
    }                                           /* a far procedure keeps the caller's IF ([DPMI0.9] gives
                                                   the structure's FLAGS no part in 0301h; as HDPMI32i) */
    f->eflags = (flags & FL_ARITH) | FL_VM | FL_IF | 2;
    if (vm.vme && current->vif)                       /* (nest_enter's IRET skips vm_return(), which sets VIF) */
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
    if (vec == 0x10 && kind != RM_FAR)
        shot_int10(ax, bx, f->eax);             /* the video mode, for glos shot */

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
    r->flags = (u16)((f->eflags & FL_ARITH) | (current->vif ? FL_IF : 0) | 2);
    current->vif = vif;
    return 0;
}

int rm_nest_return(struct trapframe *tf)
{
    if (!depth || tf != nests[depth - 1].f)
        return 0;
    nest_leave(nests[depth - 1].save, 1);
}

/* The frame of the nested call at depth d (1: the outermost). */
struct trapframe *rm_nest_frame(int d) { return d >= 1 && d <= depth ? nests[d - 1].f : 0; }

/* The nested call at depth d ends where the code inside it is, and those
   inside it with it: its rm_call() returns (an app's process has ended in
   the call the VM thread made for it, app.c). */
void rm_nest_end(int d)
{
    cache_nest_ended(d);                        /* (a disk cache call among them: the BIOS is free again) */
    depth = d;
    nest_leave(nests[d - 1].save, 2);
}

/* The real-mode vector for a reflection from the host's own handler: the
   IVT's, unless that is one of the client's callbacks (DOS/4GW points the
   IVT at an RMCB that calls its PM handler: the "automatic pass-up"), when
   it is the vector the callback or the PM hook took over (§14.5). */
u32 rm_vector(u8 vec)
{
    u32 v = app_ivt_get(vec), ip = v & 0xFFFF;
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
    if (vec == 0x10 && display_quiet(&tf->eax, &tf->ebx, &tf->ecx, &tf->edx, app_cur() || app_vm_psp()))
        return;                                 /* the desktop has the screen (gfx/display.c) */
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
    r.flags = (u16)((tf->eflags & FL_ARITH) | (current->vif ? FL_IF : 0) | 2);
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
