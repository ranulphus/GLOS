/* Handlers the host calls in the client (supervisor.md §14, §15; M4b):
 * hardware interrupts, the real-mode INTs 1Ch/23h/24h passed up,
 * exceptions, and real-mode callbacks. Each call is an entry (struct
 * pmentry) that keeps what it interrupted, V86 or protected mode, and
 * starts the handler with SEL_TRAMP:TR_RET + the entry's index as its
 * return address. The handler's IRET (an exception handler's RETF) comes
 * back to the host there, and the host carries on from the entry: the
 * virtual IF too, which an IRET at IOPL 0 can't restore (§14.3). Entries
 * above one that returns belong to handlers that never did, and go with it.
 *
 * The stack (§14.2): the locked host stack, from its top, when nothing is
 * on it; otherwise the stack protected-mode code was on, or, from V86 mode,
 * the one of the protected-mode code the real-mode call came from. A
 * handler that has moved to a stack of its own (DJGPP's IRET wrappers) so
 * never has what it left on the locked stack overwritten.
 *
 * Everything runs on the VM thread: a handler for something that happened
 * in V86 mode turns that V86 frame into a protected-mode one, and the
 * entry's return turns it back. Real-mode calls the handler makes nest
 * below it as always (rmcall.c). */
#include <string.h>

#include "glos/bootinfo.h"
#include "arch.h"
#include "dpmi.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "vm.h"

/* The stack a handler starts on; 1 if that is the locked stack's top (the
   entry then holds it), 0 if not, -1 if there is none. */
static int pick_stack(const struct trapframe *tf, u16 *ss, u32 *sp)
{
    const struct trapframe *pm = (tf->eflags & FL_VM) ? rm_pm_caller() : tf;
    if (!dctx->lstack_use) {
        *ss = dctx->lsel;
        *sp = LSTACK_SIZE;
        return 1;
    }
    if (!pm)
        return -1;
    *ss = (u16)pm->ss;
    *sp = sp_of(pm);
    return 0;
}

static int push(u16 ss, u32 *sp, const void *v, u32 n)
{
    u32 at = *sp - n;
    if (!(cpu_desc_hi(ss) & (1u << 22)))
        at &= 0xFFFF;
    if (user_wr(ss, at, v, n) != 0)
        return -1;
    *sp = at;
    return 0;
}

/* FLAGS as a handler's frame shows them: the real arithmetic flags and TF,
   the virtual IF. */
static u32 flags_image(const struct trapframe *tf, u32 vif)
{
    return (tf->eflags & (FL_ARITH | FL_TF)) | (vif ? FL_IF : 0) | 2;
}

static struct pmentry *entry_new(struct trapframe *tf, int kind, u16 *ss, u32 *sp)
{
    struct pmentry *e;
    int sw;
    if (dctx->ending || dctx->npe >= NENTRY || (sw = pick_stack(tf, ss, sp)) < 0)
        return 0;
    e = &dctx->pe[dctx->npe];
    e->kind = (u8)kind;
    e->switched = (u8)sw;
    e->vif = vm.vif;
    e->frame10 = 0;
    e->at = tf;
    e->saved = *tf;
    return e;
}

static u32 ret_off(const struct pmentry *e) { return TR_RET + (u32)(e - dctx->pe); }

/* The entry is live: its handler starts at cs:eip on ss:sp, with the
   virtual IF and TF off. */
static void entry_go(struct trapframe *tf, struct pmentry *e, u16 cs, u32 eip, u16 ss, u32 sp)
{
    dctx->npe++;
    if (e->switched)
        dctx->lstack_use++;
    if (tf->eflags & FL_VM)                     /* V86 segments mean nothing here */
        tf->ds = tf->es = tf->fs = tf->gs = 0;
    tf->cs = cs;
    tf->eip = eip;
    tf->ss = ss;
    tf->esp = sp;
    tf->eflags = (tf->eflags & FL_ARITH) | FL_IF | 2;
    vm.vif = 0;
}

/* /DPMITRACE: the first few calls of each kind into a handler. */
static void trace(const char *what, u32 n, u16 cs, u32 eip, const struct trapframe *tf)
{
    static u8 seen[4][256];
    u32 k = what[0] == 'i' ? 0 : what[0] == 'p' ? 1 : what[0] == 'e' ? 2 : 3;
    if (!(vm.bi->flags & BI_F_DPMITRACE) || seen[k][n & 0xFF] >= 4)
        return;
    seen[k][n & 0xFF]++;
    kprintf("GLOS-DPMI deliver %s=%02x to=%04x:%08x from=%s entries=%u lstack=%u\n", what, n & 0xFF, cs, eip,
            (tf->eflags & FL_VM) ? "v86" : "pm", dctx->npe, dctx->lstack_use);
}

/* ---- IRQs and INTs passed up: an interrupt frame that IRETs to TR_RET */

static int iret_entry(struct trapframe *tf, int kind, u8 vec, u32 next)
{
    struct farptr h = dctx->vidt[vec];
    struct pmentry *e;
    u16 ss;
    u32 sp, f = flags_image(tf, vm.vif);
    if (!h.sel || !(e = entry_new(tf, kind, &ss, &sp)))
        return 0;
    trace(kind == PE_INT ? "passup" : "irq", vec, h.sel, h.off, tf);
    if (kind == PE_INT)
        e->saved.eip = next;
    if (dctx->bits32) {
        u32 v[3] = { ret_off(e), SEL_TRAMP, f };
        if (push(ss, &sp, v, sizeof v) != 0)
            return 0;
    } else {
        u16 v[3] = { (u16)ret_off(e), SEL_TRAMP, (u16)f };
        if (push(ss, &sp, v, sizeof v) != 0)
            return 0;
    }
    entry_go(tf, e, h.sel, h.off, ss, sp);
    return 1;
}

/* IRQs go to the client's protected-mode handler first, from either mode
   (§14.1); its chain ends at the host's, which reflects to real mode. */
int dpmi_irq(struct trapframe *tf, u8 vec) { return iret_entry(tf, PE_IRQ, vec, 0); }

/* Real mode's INT 1Ch (the BIOS tick), 23h (Ctrl-C) and 24h (critical
   error) go up to protected-mode handlers [DPMI0.9]; the handler's general
   registers and arithmetic flags come back down (INT 24h's AL). An INT 23h
   that no PM handler takes is ignored, as CWSDPMI does: DOS's abort would
   end the client in the middle of its own Ctrl-C handling (DJGPP's SIGINT
   path, which restores what it changed), as it does under HDPMI32i. */
int dpmi_passup(struct trapframe *tf, u8 n, u32 next)
{
    if (!dctx || !(tf->eflags & FL_VM))
        return 0;
    if (n != 0x1C && (vm.bi->flags & BI_F_DPMITRACE))
        kprintf("GLOS-DPMI int%02x from=%04x:%04x hooked=%u ivt=%08x\n", n, tf->cs & 0xFFFF, tf->eip & 0xFFFF,
                dctx->vidt[n].sel != 0, vm_rd32(n * 4u));
    if (iret_entry(tf, PE_INT, n, next))
        return 1;
    if (n == 0x23 && !dctx->ending) {           /* IRET: DOS carries on */
        tf->eip = next;
        tf->eflags &= ~FL_CF;
        return 1;
    }
    return 0;
}

/* ---- exceptions */

/* The frame of [DPMI0.9]: return CS:EIP, error code, CS:EIP, EFLAGS, SS:ESP,
   in dwords for a 32-bit client. Above it at +20h, for a 32-bit client,
   the 1.0 frame: return address, error code, CS:EIP with info bits (none:
   the fault was the client's, in protected mode), EFLAGS, SS:ESP, ES, DS,
   FS, GS, and for a page fault CR2 and the PTE ([DPMI1.0] 0210h). */
void dpmi_exception(struct trapframe *tf)
{
    u32 v = tf->vec, sp, f = flags_image(tf, vm.vif);
    struct farptr h = dctx->exc[v];
    struct pmentry *e;
    u16 ss;

    if (!h.sel || dctx->ending) {
        dpmi_exc_unhandled(tf, v, tf->err);
        return;
    }
    if (dctx->exc_depth >= EXC_NEST_MAX) {
        crash_report(tf, "nested-exceptions");
        return;
    }
    if (!(e = entry_new(tf, PE_EXC, &ss, &sp))) {
        crash_report(tf, "no-stack");
        return;
    }
    if (dctx->bits32) {
        u32 r = ret_off(e), cr2 = v == 14 ? dctx->cr2 : 0, pte = v == 14 ? mm_lookup(cr2) : 0;
        u32 fr[22] = { r, SEL_TRAMP, tf->err, tf->eip, tf->cs & 0xFFFF, f, tf->esp, tf->ss & 0xFFFF,
                       r, SEL_TRAMP, tf->err, tf->eip, tf->cs & 0xFFFF, f, tf->esp, tf->ss & 0xFFFF,
                       tf->es & 0xFFFF, tf->ds & 0xFFFF, tf->fs & 0xFFFF, tf->gs & 0xFFFF, cr2, pte };
        if (push(ss, &sp, fr, sizeof fr) != 0) {
            crash_report(tf, "no-stack");
            return;
        }
    } else {
        u16 fr[8] = { (u16)ret_off(e), SEL_TRAMP, (u16)tf->err, (u16)tf->eip, (u16)tf->cs, (u16)f, (u16)tf->esp,
                      (u16)tf->ss };
        if (push(ss, &sp, fr, sizeof fr) != 0) {
            crash_report(tf, "no-stack");
            return;
        }
    }
    trace("exception", v, h.sel, h.off, tf);
    e->frame10 = dctx->exc10[v];
    dctx->exc_depth++;
    entry_go(tf, e, h.sel, h.off, ss, sp);
}

/* The handler's RETF: on from the frame as it left it ([DPMI0.9]: CS:EIP,
   EFLAGS and SS:ESP; the 1.0 frame adds the segment registers); the
   general registers are the handler's own. */
static void exc_return(struct trapframe *tf, const struct pmentry *e)
{
    u32 sp = sp_of(tf), eip, cs, fl, esp, ss;
    if (dctx->bits32) {
        u32 v[10];
        if (user_rd((u16)tf->ss, sp + (e->frame10 ? 0x20 : 0), v, e->frame10 ? 40 : 24) != 0) {
            crash_report(tf, "exception-frame");
            return;
        }
        eip = v[1], cs = v[2] & 0xFFFF, fl = v[3], esp = v[4], ss = v[5] & 0xFFFF;
        if (e->frame10) {
            tf->es = v[6] & 0xFFFF;
            tf->ds = v[7] & 0xFFFF;
            tf->fs = v[8] & 0xFFFF;
            tf->gs = v[9] & 0xFFFF;
        }
    } else {
        u16 w[6];
        if (user_rd((u16)tf->ss, sp, w, sizeof w) != 0) {
            crash_report(tf, "exception-frame");
            return;
        }
        eip = w[1], cs = w[2], fl = w[3], esp = w[4], ss = w[5];
    }
    tf->eip = eip;
    tf->cs = cs;
    tf->esp = esp;
    tf->ss = ss;
    tf->eflags = (fl & (FL_ARITH | FL_TF)) | FL_IF | 2;
    vm.vif = (fl & FL_IF) != 0;
}

/* No handler of the client's: 1-4 (traps) go on as the interrupt, as on
   a real-mode CPU; 0, 5 and 7 too if the client has a protected-mode
   handler for that interrupt (a fault would only come back to the same
   instruction from real mode); the rest end the client with a report. */
void dpmi_exc_unhandled(struct trapframe *tf, u32 v, u32 err)
{
    if (!dctx->ending && ((v >= 1 && v <= 4) || ((v == 0 || v == 5 || v == 7) && dctx->vidt[v].sel))) {
        pm_soft_int(tf, (u8)v, tf->eip);
        return;
    }
    tf->vec = v;
    tf->err = err;
    crash_report(tf, "exception");
}

/* SEL_TRAMP:TR_EXC + v: a handler chained to the host's default (0202h's
   answer), with the frame as it got it, return address on top. */
void dpmi_exc_default(struct trapframe *tf, u32 v)
{
    u32 sp = sp_of(tf), r[3];
    if (dctx->bits32) {
        if (user_rd((u16)tf->ss, sp, r, 12) != 0)
            r[1] = 0;
        set_sp(tf, sp + 8);
    } else {
        u16 w[3];
        r[1] = 0;
        if (user_rd((u16)tf->ss, sp, w, 6) == 0)
            r[0] = w[0], r[1] = w[1], r[2] = w[2];
        set_sp(tf, sp + 4);
    }
    if ((r[1] & 0xFFFF) != SEL_TRAMP || r[0] < TR_RET || r[0] >= TR_RET + NENTRY) {
        crash_report(tf, "bad-chain");
        return;
    }
    dpmi_entry_return(tf, r[0] - TR_RET);
    if (!(tf->eflags & FL_VM) && dctx)
        dpmi_exc_unhandled(tf, v, r[2]);
}

/* ---- real-mode callbacks (0303h) */

/* V86 code called callback i: the client's handler gets DS:(E)SI = the
   real-mode SS:SP and ES:(E)DI = its register structure, filled in, and
   IRETs with ES:(E)DI at the structure to go back with ([DPMI0.9]). */
void dpmi_rmcb(struct trapframe *tf, u32 i)
{
    struct rmcb *cb = &dctx->rmcb[i];
    struct pmentry *e;
    struct rmregs r;
    u32 sp, rsp = tf->esp & 0xFFFF, rss = tf->ss & 0xFFFF;
    u16 ss;

    memset(&r, 0, sizeof r);
    r.edi = tf->edi;
    r.esi = tf->esi;
    r.ebp = tf->ebp;
    r.ebx = tf->ebx;
    r.edx = tf->edx;
    r.ecx = tf->ecx;
    r.eax = tf->eax;
    r.flags = (u16)flags_image(tf, vm.vif);
    r.es = (u16)tf->v86_es;
    r.ds = (u16)tf->v86_ds;
    r.fs = (u16)tf->v86_fs;
    r.gs = (u16)tf->v86_gs;
    r.ip = (u16)tf->eip;
    r.cs = (u16)tf->cs;
    r.sp = (u16)rsp;
    r.ss = (u16)rss;
    if (user_wr(cb->regs.sel, cb->regs.off, &r, sizeof r) != 0 || !(e = entry_new(tf, PE_RMCB, &ss, &sp))) {
        kprintf("GLOS-DPMI rmcb-failed n=%u\n", i);
        crash_report(tf, "rmcb");
        return;
    }
    if (dctx->bits32) {
        u32 v[3] = { ret_off(e), SEL_TRAMP, r.flags };
        if (push(ss, &sp, v, sizeof v) != 0) {
            crash_report(tf, "rmcb");
            return;
        }
    } else {
        u16 v[3] = { (u16)ret_off(e), SEL_TRAMP, r.flags };
        if (push(ss, &sp, v, sizeof v) != 0) {
            crash_report(tf, "rmcb");
            return;
        }
    }
    trace("rmcb", i, cb->pm.sel, cb->pm.off, tf);
    sel_set_base(cb->stack_sel, rss * 16);
    entry_go(tf, e, cb->pm.sel, cb->pm.off, ss, sp);
    tf->ds = cb->stack_sel;
    tf->esi = rsp;
    tf->es = cb->regs.sel;
    tf->edi = cb->regs.off;
}

static void rmcb_return(struct trapframe *tf, const struct pmentry *e)
{
    struct rmregs r;
    if (user_rd((u16)tf->es, dctx->bits32 ? tf->edi : tf->edi & 0xFFFF, &r, sizeof r) != 0) {
        crash_report(tf, "rmcb-regs");
        return;
    }
    *tf = e->saved;
    tf->edi = r.edi;
    tf->esi = r.esi;
    tf->ebp = r.ebp;
    tf->ebx = r.ebx;
    tf->edx = r.edx;
    tf->ecx = r.ecx;
    tf->eax = r.eax;
    tf->v86_es = r.es;
    tf->v86_ds = r.ds;
    tf->v86_fs = r.fs;
    tf->v86_gs = r.gs;
    tf->cs = r.cs;
    tf->eip = r.ip;
    tf->ss = r.ss;
    tf->esp = r.sp;
    tf->eflags = (r.flags & (FL_ARITH | FL_TF)) | FL_VM | FL_IF | 2;
    vm.vif = (r.flags & FL_IF) != 0;
}

/* ---- back from a handler: SEL_TRAMP:TR_RET + i */

void dpmi_entry_return(struct trapframe *tf, u32 i)
{
    struct pmentry *e;
    if (i >= dctx->npe || dctx->pe[i].at != tf) {
        crash_report(tf, "stale-return");
        return;
    }
    while (dctx->npe > i) {                     /* this one, and any whose handlers never came back */
        struct pmentry *x = &dctx->pe[--dctx->npe];
        if (x->switched)
            dctx->lstack_use--;
        if (x->kind == PE_EXC)
            dctx->exc_depth--;
    }
    e = &dctx->pe[i];
    switch (e->kind) {
    case PE_IRQ:
        *tf = e->saved;
        vm.vif = e->vif;
        break;
    case PE_INT: {
            struct trapframe t = e->saved;
            t.eax = tf->eax;
            t.ebx = tf->ebx;
            t.ecx = tf->ecx;
            t.edx = tf->edx;
            t.esi = tf->esi;
            t.edi = tf->edi;
            t.ebp = tf->ebp;
            t.eflags = (t.eflags & ~FL_ARITH) | (tf->eflags & FL_ARITH);
            *tf = t;
            vm.vif = e->vif;
            break;
        }
    case PE_EXC:
        exc_return(tf, e);
        break;
    case PE_RMCB:
        rmcb_return(tf, e);
        break;
    }
}
