/* The INT 31h functions (supervisor.md §13; M4a, M4b). Registers are the
 * client's own, a 16-bit client using the low words of the index
 * registers; pointers are selector:offset in its LDT. Success clears CF;
 * failure sets it with the DPMI error in AX. Functions GLOS doesn't have
 * are logged (GLOS-DPMI-UNIMPL) and fail with 8001h. Handlers the client
 * runs (IRQs, exceptions, callbacks) may call any of them (deliver.c). */
#include <string.h>

#include "glos/bootinfo.h"
#include "arch.h"
#include "dpmi.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "vm.h"
#include "vpic.h"

static u32 r_di(const struct trapframe *tf) { return dctx->bits32 ? tf->edi : tf->edi & 0xFFFF; }
static u32 r_si(const struct trapframe *tf) { return dctx->bits32 ? tf->esi : tf->esi & 0xFFFF; }
static u32 r_dx(const struct trapframe *tf) { return dctx->bits32 ? tf->edx : tf->edx & 0xFFFF; }
static u16 bx(const struct trapframe *tf) { return (u16)tf->ebx; }
static u16 cx(const struct trapframe *tf) { return (u16)tf->ecx; }
static u16 dx(const struct trapframe *tf) { return (u16)tf->edx; }
static u32 pair(u16 hi, u16 lo) { return (u32)hi << 16 | lo; }

static void ok(struct trapframe *tf) { tf->eflags &= ~FL_CF; }

static void fail(struct trapframe *tf, u32 code)
{
    SET16(tf->eax, code);
    tf->eflags |= FL_CF;
}

static void set_pair(u32 *hi, u32 *lo, u32 v)
{
    SET16(*hi, v >> 16);
    SET16(*lo, v);
}

/* ---- descriptors */

static void fn_alloc(struct trapframe *tf)
{
    int i, k;
    if (!cx(tf))
        return fail(tf, 0x8021);                /* none: an invalid value */
    if ((i = ldt_alloc(cx(tf))) < 0)
        return fail(tf, 0x8011);
    for (k = 0; k < cx(tf); k++) {             /* present data, base and limit 0 */
        u32 *d = &dctx->ldt[(i + k) * 2];
        d[0] = 0;
        d[1] = 0xF200 | (dctx->bits32 ? 0x400000u : 0);
    }
    SET16(tf->eax, (u32)i * 8 | 7);
    ok(tf);
}

static void fn_free(struct trapframe *tf)
{
    u16 s = bx(tf);
    if (ldt_free(s) != 0)
        return fail(tf, 0x8022);
    if ((tf->ds & 0xFFFF) == s) tf->ds = 0;    /* as CWSDPMI does, and DPMI 1.0 asks */
    if ((tf->es & 0xFFFF) == s) tf->es = 0;
    if ((tf->fs & 0xFFFF) == s) tf->fs = 0;
    if ((tf->gs & 0xFFFF) == s) tf->gs = 0;
    ok(tf);
}

static void fn_seg2desc(struct trapframe *tf)
{
    u32 i, free_i = NSEGSEL;
    u16 seg = bx(tf), s;
    for (i = 0; i < NSEGSEL; i++) {
        if (dctx->segsel[i].sel && dctx->segsel[i].seg == seg) {
            SET16(tf->eax, dctx->segsel[i].sel);
            return ok(tf);
        }
        if (!dctx->segsel[i].sel && free_i == NSEGSEL)
            free_i = i;
    }
    if (free_i == NSEGSEL || !(s = ldt_new(seg * 16u, 0xFFFF, 0xF2, 0)))
        return fail(tf, 0x8011);
    dctx->segsel[free_i].seg = seg;
    dctx->segsel[free_i].sel = s;
    SET16(tf->eax, s);
    ok(tf);
}

static void fn_desc(struct trapframe *tf, u32 fn)
{
    u16 s = bx(tf);
    u32 lo, hi, v;
    u8 buf[8];
    if (!ldt_valid(s))
        return fail(tf, 0x8022);
    switch (fn) {
    case 0x0006:
        v = sel_base(s);
        SET16(tf->ecx, v >> 16);
        SET16(tf->edx, v);
        break;
    case 0x0007:
        sel_set_base(s, pair(cx(tf), dx(tf)));
        break;
    case 0x0008:
        v = pair(cx(tf), dx(tf));
        if (v > 0xFFFFF && (v & 0xFFF) != 0xFFF)
            return fail(tf, 0x8021);
        sel_set_limit(s, v);
        break;
    case 0x0009:                                /* CL: access rights; CH: G, D/B, AVL */
        sel_get_desc(s, &lo, &hi);
        hi = (hi & 0xFF0F00FFu) | ((u32)(tf->ecx & 0xFF) << 8) | ((u32)(tf->ecx & 0xD000) << 8);
        if (sel_set_desc(s, lo, hi) != 0)
            return fail(tf, 0x8021);
        break;
    case 0x000A: {                              /* a data alias (of code or data: DJGPP's DS alias) */
            u16 n;
            sel_get_desc(s, &lo, &hi);
            if (!(n = ldt_new(0, 0, 0xF2, 0)))
                return fail(tf, 0x8011);
            sel_set_desc(n, lo, (hi & ~0xFF00u) | 0xF200);
            SET16(tf->eax, n);
            break;
        }
    case 0x000B:
        sel_get_desc(s, &lo, &hi);
        memcpy(buf, &lo, 4);
        memcpy(buf + 4, &hi, 4);
        if (user_wr((u16)tf->es, r_di(tf), buf, 8) != 0)
            return fail(tf, 0x8021);
        break;
    case 0x000C:
        if (user_rd((u16)tf->es, r_di(tf), buf, 8) != 0)
            return fail(tf, 0x8021);
        memcpy(&lo, buf, 4);
        memcpy(&hi, buf + 4, 4);
        if (sel_set_desc(s, lo, hi) != 0)
            return fail(tf, 0x8021);
        break;
    }
    ok(tf);
}

static void fn_specific(struct trapframe *tf)
{
    u16 s = bx(tf);
    u32 i = s >> 3;
    if (!(s & 4) || i == 0 || i >= SEL_FIRST || dctx->ldt_used[i])
        return fail(tf, 0x8022);
    dctx->ldt_used[i] = (u8)dctx->nlv;
    dctx->ldt[i * 2] = 0;
    dctx->ldt[i * 2 + 1] = 0xF200 | (dctx->bits32 ? 0x400000u : 0);
    ok(tf);
}

/* ---- DOS memory: INT 21h 48h-4Ah in real mode, and selectors for the block */

/* A DOS block's selectors: for a 32-bit client one, its limit the whole
   block (as HDPMI; it can grow in place); for a 16-bit client one per 64 KB,
   tiled ([DPMI0.9] 0100h). */
static u32 dos_nsel(u32 paras)
{
    u32 n = (paras + 0xFFF) >> 12;
    return dctx->bits32 || !n ? 1 : n;
}

static void dos_sels(u16 first, u16 seg, u32 paras)
{
    u32 i, n = dos_nsel(paras), left = paras * 16u;
    if (dctx->bits32) {
        u32 *d = &dctx->ldt[(first >> 3) * 2];
        d[0] = 0;
        d[1] = 0xF200;
        sel_set_base(first, seg * 16u);
        sel_set_limit(first, left - 1);
        return;
    }
    for (i = 0; i < n; i++) {
        u16 s = (u16)(first + i * 8);
        u32 *d = &dctx->ldt[(s >> 3) * 2];
        d[0] = 0;
        d[1] = 0xF200;
        sel_set_base(s, seg * 16u + i * 0x10000);
        sel_set_limit(s, (i == 0 ? left : (left > 0x10000 ? 0x10000 : left)) - 1);
        left -= left > 0x10000 ? 0x10000 : left;
    }
}

static struct dosblk *dos_find(u16 sel)
{
    struct dosblk *d;
    for (d = dctx->dosblks; d && d->sel != sel; d = d->next) ;
    return d;
}

static void fn_dos(struct trapframe *tf, u32 fn)
{
    struct rmregs r;
    struct dosblk *d = 0, **pp;
    u32 paras = bx(tf), n, k;
    int i;
    memset(&r, 0, sizeof r);
    if (fn != 0x0100 && !(d = dos_find(dx(tf))))
        return fail(tf, 0x8022);
    if (fn == 0x0102 && dos_nsel(paras) > d->nsel)
        return fail(tf, 0x8011);                /* (before DOS resizes it) */
    if (fn == 0x0100) {
        r.eax = 0x4800;
        r.ebx = paras;
    } else if (fn == 0x0101) {
        r.eax = 0x4900;
        r.es = d->seg;
    } else {
        r.eax = 0x4A00;
        r.ebx = paras;
        r.es = d->seg;
    }
    r.flags = 2;
    if (rm_call(tf, &r, RM_INT, 0x21, 0, 0) != 0)
        return fail(tf, 0x8010);
    if (r.flags & FL_CF) {                      /* DOS's error, and the largest block (the DJGPP stub reads BX) */
        SET16(tf->ebx, r.ebx);
        return fail(tf, r.eax & 0xFFFF);
    }
    if (fn == 0x0100) {
        n = dos_nsel(paras);
        if ((i = ldt_alloc(n)) < 0 || !(d = kmalloc(sizeof *d))) {
            u16 seg = (u16)r.eax;               /* no selectors: give the block back */
            if (i >= 0)
                for (k = 0; k < n; k++)
                    dctx->ldt_used[(u32)i + k] = 0;
            memset(&r, 0, sizeof r);
            r.eax = 0x4900;
            r.es = seg;
            r.flags = 2;
            rm_call(tf, &r, RM_INT, 0x21, 0, 0);
            return fail(tf, 0x8011);
        }
        d->seg = (u16)r.eax;
        d->level = (u8)dctx->nlv;
        d->sel = (u16)((u32)i * 8 | 7);
        d->nsel = (u16)n;
        d->next = dctx->dosblks;
        dctx->dosblks = d;
        dos_sels(d->sel, d->seg, paras ? paras : 1);
        SET16(tf->eax, d->seg);
        SET16(tf->edx, d->sel);
    } else if (fn == 0x0101) {
        for (k = 0; k < d->nsel; k++)
            ldt_free((u16)(d->sel + k * 8));
        for (pp = &dctx->dosblks; *pp != d; pp = &(*pp)->next) ;
        *pp = d->next;
        kfree(d);
    } else {
        dos_sels(d->sel, d->seg, paras ? paras : 1);
    }
    ok(tf);
}

/* ---- real-mode calls */

static void fn_rmcall(struct trapframe *tf, u32 fn)
{
    struct rmregs r;
    u16 words[64];
    u32 n = cx(tf), sp;
    if (n > 64)
        return fail(tf, 0x8021);
    if (user_rd((u16)tf->es, r_di(tf), &r, sizeof r) != 0)
        return fail(tf, 0x8021);
    sp = (cpu_desc_hi(tf->ss) & (1u << 22)) ? tf->esp : tf->esp & 0xFFFF;
    if (n && user_rd((u16)tf->ss, sp, words, n * 2) != 0)
        return fail(tf, 0x8021);
    if (rm_call(tf, &r, fn == 0x0300 ? RM_INT : fn == 0x0301 ? RM_FAR : RM_IRET, (u8)tf->ebx, words, n) != 0)
        return fail(tf, 0x8012);
    if (user_wr((u16)tf->es, r_di(tf), &r, sizeof r) != 0)
        return fail(tf, 0x8021);
    ok(tf);
}

static void fn_rmcb(struct trapframe *tf, u32 fn)
{
    u32 i;
    if (fn == 0x0303) {
        for (i = 0; i < NRMCB && dctx->rmcb[i].used; i++) ;
        if (i == NRMCB || !(dctx->rmcb[i].stack_sel = ldt_new(0, 0xFFFF, 0xF2, 0)))
            return fail(tf, 0x8015);
        dctx->rmcb[i].used = (u8)dctx->nlv;
        dctx->rmcb[i].pm.sel = (u16)tf->ds;
        dctx->rmcb[i].pm.off = r_si(tf);
        dctx->rmcb[i].regs.sel = (u16)tf->es;
        dctx->rmcb[i].regs.off = r_di(tf);
        SET16(tf->ecx, vm.loader_cs);
        SET16(tf->edx, vm.bi->rmcb_off + 2 * i);
        return ok(tf);
    }
    i = (dx(tf) - vm.bi->rmcb_off) / 2;
    if (cx(tf) != vm.loader_cs || dx(tf) < vm.bi->rmcb_off || i >= NRMCB || !dctx->rmcb[i].used)
        return fail(tf, 0x8024);
    dctx->rmcb[i].used = 0;
    ldt_free(dctx->rmcb[i].stack_sel);
    ok(tf);
}

/* ---- the rest */

static int is_rmcb(u32 v)
{
    u32 ip = v & 0xFFFF;
    return (v >> 16) == vm.loader_cs && ip >= vm.bi->rmcb_off && ip < vm.bi->rmcb_off + 2 * NRMCB;
}

/* A handler taking a code selector of the client's (or the host's own default back). */
static int code_sel(u16 sel) { return ldt_valid(sel) && (cpu_desc_hi(sel) & 0x0800); }

static void fn_vectors(struct trapframe *tf, u32 fn)
{
    u8 n = (u8)tf->ebx;
    struct farptr *p;
    u32 ivt = vm_rd32(n * 4u);
    switch (fn) {
    case 0x0200:
        SET16(tf->ecx, vm_rd16(n * 4u + 2));
        SET16(tf->edx, vm_rd16(n * 4u));
        break;
    case 0x0201:
        if (is_rmcb(pair(cx(tf), dx(tf))) && !is_rmcb(ivt))
            dctx->rm_prev[n] = ivt;             /* an RMCB takes it over: the pass-up guard's way on (§14.5) */
        vm_wr8(n * 4u, (u8)tf->edx);
        vm_wr8(n * 4u + 1, (u8)(tf->edx >> 8));
        vm_wr8(n * 4u + 2, (u8)tf->ecx);
        vm_wr8(n * 4u + 3, (u8)(tf->ecx >> 8));
        break;
    case 0x0202:
    case 0x0203:
    case 0x0210:
    case 0x0212:
        if (n >= 32 || (fn >= 0x0210 && !dctx->bits32))
            return fail(tf, fn >= 0x0210 ? 0x8001 : 0x8021);    /* the 1.0 frame for 16-bit clients: M4d */
        p = &dctx->exc[n];
        if (fn == 0x0202 || fn == 0x0210) {
            u32 off = p->sel ? p->off : TR_EXC + n;
            SET16(tf->ecx, p->sel ? p->sel : SEL_TRAMP);
            if (dctx->bits32)
                tf->edx = off;
            else
                SET16(tf->edx, off);
        } else if (cx(tf) == SEL_TRAMP && r_dx(tf) == TR_EXC + n) {
            p->sel = 0;                         /* the host's own again */
            dctx->exc10[n] = 0;
        } else {
            if (!code_sel(cx(tf)))
                return fail(tf, 0x8022);
            p->sel = cx(tf);
            p->off = r_dx(tf);
            dctx->exc10[n] = fn == 0x0212;
        }
        break;
    case 0x0204:
        p = &dctx->vidt[n];
        SET16(tf->ecx, p->sel ? p->sel : SEL_TRAMP);
        if (dctx->bits32)
            tf->edx = p->sel ? p->off : TR_VEC + n;
        else
            SET16(tf->edx, p->sel ? p->off : TR_VEC + n);
        break;
    case 0x0205:
        p = &dctx->vidt[n];
        if (cx(tf) == SEL_TRAMP && r_dx(tf) == TR_VEC + n) {
            p->sel = 0;                         /* the host's own again */
        } else {
            if (!code_sel(cx(tf)))
                return fail(tf, 0x8022);
            if (!is_rmcb(ivt))
                dctx->rm_prev[n] = ivt;         /* the pass-up guard's way on (§14.5) */
            p->sel = cx(tf);
            p->off = r_dx(tf);
        }
        break;
    }
    ok(tf);
}

static void fn_mem(struct trapframe *tf, u32 fn)
{
    struct block *b;
    u32 info[12], lin;
    int e;
    switch (fn) {
    case 0x0500:
        lin_info(info);
        if (user_wr((u16)tf->es, r_di(tf), info, sizeof info) != 0)
            return fail(tf, 0x8021);
        break;
    case 0x0501:
        if ((e = lin_alloc(pair(bx(tf), cx(tf)), &b)) != 0)
            return fail(tf, (u32)e);
        set_pair(&tf->ebx, &tf->ecx, b->lin);
        set_pair(&tf->esi, &tf->edi, b->handle);
        break;
    case 0x0502:
        if ((e = lin_free(pair((u16)tf->esi, (u16)tf->edi))) != 0)
            return fail(tf, (u32)e);
        break;
    case 0x0503:
        if ((e = lin_resize(pair((u16)tf->esi, (u16)tf->edi), pair(bx(tf), cx(tf)), &b)) != 0)
            return fail(tf, (u32)e);
        set_pair(&tf->ebx, &tf->ecx, b->lin);
        set_pair(&tf->esi, &tf->edi, b->handle);
        break;
    case 0x0800:
        if ((e = phys_map(pair(bx(tf), cx(tf)), pair((u16)tf->esi, (u16)tf->edi), &lin)) != 0)
            return fail(tf, (u32)e);
        set_pair(&tf->ebx, &tf->ecx, lin);
        break;
    case 0x0801:
        if ((e = phys_unmap(pair(bx(tf), cx(tf)))) != 0)
            return fail(tf, (u32)e);
        break;
    }
    ok(tf);
}

/* 0506h/0507h ([DPMI1.0]): ESI the handle, EBX the offset in the block,
   ECX the pages, ES:(E)DX the attribute words; ECX after a failure: the
   pages done. DJGPP uncommits its null page (crt0); DOS/4GW reads them. */
static void fn_pages(struct trapframe *tf, u32 fn)
{
    u16 attr[64];
    u32 left = tf->ecx, off = tf->ebx, at = r_dx(tf), done, all = 0;
    int e;
    while (left) {
        u32 n = left > 64 ? 64 : left;
        if (fn == 0x0507 && user_rd((u16)tf->es, at, attr, n * 2) != 0) {
            tf->ecx = all;
            return fail(tf, 0x8021);
        }
        e = page_attr(tf->esi, off, n, attr, fn == 0x0507, &done);
        all += done;
        if (e) {
            tf->ecx = all;
            return fail(tf, (u32)e);
        }
        if (fn == 0x0506 && user_wr((u16)tf->es, at, attr, n * 2) != 0)
            return fail(tf, 0x8021);
        left -= n;
        off += n << 12;
        at += n * 2;
    }
    ok(tf);
}

/* 0401h ([DPMI1.0]): what the host can do, and who it is. */
static void fn_caps(struct trapframe *tf)
{
    u8 buf[128];
    memset(buf, 0, sizeof buf);
    buf[0] = 0;                                 /* GLOS 0.4 */
    buf[1] = 4;
    memcpy(buf + 2, "GLOS", 5);
    if (user_wr((u16)tf->es, r_di(tf), buf, sizeof buf) != 0)
        return fail(tf, 0x8021);
    /* accessed/dirty (0506h), restartable exceptions, device and DOS memory
       mapping (0508h/0509h), read-only client pages (0507h); not demand
       zero-fill (bit 4): an uncommitted page faults, it isn't committed on
       touch. HDPMI32i says the same (2Fh). */
    SET16(tf->eax, 0x0001 | 0x0002 | 0x0004 | 0x0008 | 0x0020);
    SET16(tf->ecx, 0);
    SET16(tf->edx, 0);
    ok(tf);
}

/* 0B00h-0B03h: watchpoints in DR0-DR3 (the gdb stub uses software
   breakpoints only). BX:CX the linear address, DL its size (1, 2, 4), DH
   0 execute, 1 write, 2 read/write; the handle is the register's number.
   A hit is an exception 1 to the client, and 0B02h's AX bit 0. */
static void dr7_set(void)
{
    u32 v = 0x100, i;                           /* LE */
    for (i = 0; i < 4; i++)
        if (dctx && dctx->wp[i].level)
            v |= (2u << (i * 2)) | ((u32)dctx->wp[i].rw << (16 + i * 4));
    write_dr7(v);
}

static void fn_watch(struct trapframe *tf, u32 fn)
{
    u32 i = bx(tf), lin = pair(bx(tf), cx(tf)), len = tf->edx & 0xFF, type = (tf->edx >> 8) & 0xFF, rw;
    if (fn == 0x0B00) {
        if ((len != 1 && len != 2 && len != 4) || type > 2 || (lin & (len - 1)) || (type == 0 && len != 1))
            return fail(tf, 0x8021);
        for (i = 0; i < 4 && dctx->wp[i].level; i++) ;
        if (i == 4)
            return fail(tf, 0x8016);            /* too many breakpoints */
        rw = (type == 0 ? 0 : type == 1 ? 1 : 3) | ((len == 1 ? 0 : len == 2 ? 1 : 3) << 2);
        dctx->wp[i].level = (u8)dctx->nlv;
        dctx->wp[i].rw = (u8)rw;
        dctx->wp[i].hit = 0;
        dctx->wp[i].lin = lin;
        write_dr((int)i, lin);
        dr7_set();
        SET16(tf->ebx, i);
        return ok(tf);
    }
    if (i >= 4 || !dctx->wp[i].level)
        return fail(tf, 0x8023);
    if (fn == 0x0B01) {
        dctx->wp[i].level = 0;
        dr7_set();
    } else if (fn == 0x0B02) {
        dpmi_db_hit();                          /* (a hit still pending in DR6) */
        SET16(tf->eax, dctx->wp[i].hit);
    } else {
        dctx->wp[i].hit = 0;
    }
    ok(tf);
}

int dpmi_db_hit(void)
{
    u32 dr6 = read_dr6(), i, any = 0;
    if (!dctx || !(dr6 & 15))
        return 0;
    for (i = 0; i < 4; i++)
        if ((dr6 & (1u << i)) && dctx->wp[i].level) {
            dctx->wp[i].hit = 1;
            any = 1;
            if ((dctx->wp[i].rw & 3) == 0)
                dctx->db_rf = dctx->wp[i].lin;
        }
    write_dr6(dr6 & ~15u);
    return (int)any;
}

void wp_clear_level(u32 level)
{
    u32 i;
    for (i = 0; i < 4; i++)
        if (dctx->wp[i].level >= level)
            dctx->wp[i].level = 0;
    dr7_set();
}

static void fn_vendor(struct trapframe *tf)
{
    char name[16];
    u32 i;
    for (i = 0; i < sizeof name - 1; i++)
        if (user_rd((u16)tf->ds, r_si(tf) + i, &name[i], 1) != 0 || !name[i])
            break;
    name[i] = 0;
    if (strcmp(name, "GLOS") != 0)              /* DOS/4GW asks for its own: no */
        return fail(tf, 0x8001);
    tf->es = SEL_TRAMP;
    if (dctx->bits32)
        tf->edi = TR_VENDOR;
    else
        SET16(tf->edi, TR_VENDOR);
    ok(tf);
}

static void dispatch(struct trapframe *tf);

/* /DPMITRACE: each call, then CF and AX after it. */
void int31(struct trapframe *tf)
{
    u32 fn = tf->eax & 0xFFFF, bx0 = tf->ebx & 0xFFFF, cx0 = tf->ecx & 0xFFFF, dx0 = tf->edx & 0xFFFF, if0 = vm.vif;
    dispatch(tf);
    if (vm.bi->flags & BI_F_DPMITRACE)
        kprintf("GLOS-DPMI call fn=%04x bx=%04x cx=%04x dx=%04x if=%u -> cf=%u ax=%04x bx=%04x cx=%04x dx=%04x\n", fn,
                bx0, cx0, dx0, if0, tf->eflags & FL_CF, tf->eax & 0xFFFF, tf->ebx & 0xFFFF, tf->ecx & 0xFFFF,
                tf->edx & 0xFFFF);
}

static void dispatch(struct trapframe *tf)
{
    u32 fn = tf->eax & 0xFFFF, v;
    switch (fn) {
    case 0x0000: fn_alloc(tf); return;
    case 0x0001: fn_free(tf); return;
    case 0x0002: fn_seg2desc(tf); return;
    case 0x0003: SET16(tf->eax, 8); ok(tf); return;
    case 0x0004: case 0x0005: ok(tf); return;   /* lock and unlock selector: nothing to do */
    case 0x0006: case 0x0007: case 0x0008: case 0x0009: case 0x000A: case 0x000B: case 0x000C:
        fn_desc(tf, fn);
        return;
    case 0x000D: fn_specific(tf); return;
    case 0x0100: case 0x0101: case 0x0102: fn_dos(tf, fn); return;
    case 0x0200: case 0x0201: case 0x0202: case 0x0203: case 0x0204: case 0x0205: case 0x0210: case 0x0212:
        fn_vectors(tf, fn);
        return;
    case 0x0300: case 0x0301: case 0x0302: fn_rmcall(tf, fn); return;
    case 0x0303: case 0x0304: fn_rmcb(tf, fn); return;
    case 0x0305:
        SET16(tf->eax, 0);                      /* no state to save */
        SET16(tf->ebx, vm.loader_cs);
        SET16(tf->ecx, vm.bi->retf_off);
        SET16(tf->esi, SEL_TRAMP);
        if (dctx->bits32) tf->edi = TR_SAVE; else SET16(tf->edi, TR_SAVE);
        ok(tf);
        return;
    case 0x0306:
        SET16(tf->ebx, vm.loader_cs);
        SET16(tf->ecx, vm.bi->bp_raw_off);
        SET16(tf->esi, SEL_TRAMP);
        if (dctx->bits32) tf->edi = TR_RAW; else SET16(tf->edi, TR_RAW);
        ok(tf);
        return;
    case 0x0400:
        SET16(tf->eax, 0x005A);                 /* 0.90 */
        SET16(tf->ebx, 0x0001);                 /* 32-bit host; reflection in V86 mode; no virtual memory */
        SETLO(tf->ecx, vm.bi->cpu_family > 6 ? 6 : vm.bi->cpu_family);
        SET16(tf->edx, (u32)vm.pic.p[0].base << 8 | vm.pic.p[1].base);
        ok(tf);
        return;
    case 0x0500: case 0x0501: case 0x0502: case 0x0503: case 0x0800: case 0x0801:
        fn_mem(tf, fn);
        return;
    case 0x0504: {                              /* [DPMI1.0]: EBX where (0: anywhere), ECX bytes, EDX bit 0 committed */
            struct block *b;
            int e = lin_alloc_at(tf->ebx, tf->ecx, tf->edx & 1, &b);
            if (e)
                return fail(tf, (u32)e);
            tf->ebx = b->lin;
            tf->esi = b->handle;
            ok(tf);
            return;
        }
    case 0x0505: {                              /* ESI, ECX; EDX bit 0 commit, bit 1: rebase EDI selectors at ES:EBX */
            struct block *b;
            u32 old = 0, i, base;
            int e;
            struct block *ob;
            for (ob = dctx->blocks; ob && ob->handle != tf->esi; ob = ob->next) ;
            if (ob)
                old = ob->lin;
            if ((e = lin_resize2(tf->esi, tf->ecx, tf->edx & 1, &b)) != 0)
                return fail(tf, (u32)e);
            if ((tf->edx & 2) && b->lin != old)
                for (i = 0; i < tf->edi && i < 8192; i++) {
                    u16 sel;
                    if (user_rd((u16)tf->es, tf->ebx + i * 2, &sel, 2) != 0)
                        break;
                    base = ldt_valid(sel) ? sel_base(sel) : 0;
                    if (ldt_valid(sel) && base >= old && base < old + b->size)
                        sel_set_base(sel, base - old + b->lin);
                }
            tf->ebx = b->lin;
            tf->esi = b->handle;
            ok(tf);
            return;
        }
    case 0x0506: case 0x0507: fn_pages(tf, fn); return;
    case 0x050B: {                              /* [DPMI1.0] memory information, as HDPMI32i lays it out */
            u32 info[12], o[32];
            lin_info(info);
            memset(o, 0, sizeof o);
            o[0] = o[1] = o[3] = o[5] = info[6] << 12;  /* physical (all the host's), virtual, the client's */
            o[2] = o[4] = o[6] = info[5] << 12;         /* free */
            o[7] = 0;                                   /* locked: nothing pages out */
            o[8] = info[5] << 12;
            o[9] = USER_END - 1;                        /* the highest address a client may have */
            o[10] = info[0];                            /* the largest block */
            o[11] = 1;                                  /* the smallest allocation, in pages */
            o[12] = 0x1000;                             /* the unit */
            if (user_wr((u16)tf->es, r_di(tf), o, sizeof o) != 0)
                return fail(tf, 0x8021);
            ok(tf);
            return;
        }
    case 0x0508: case 0x0509: {
            int e = page_map(tf->esi, tf->ebx, tf->ecx, tf->edx, fn == 0x0508);
            if (e)
                return fail(tf, (u32)e);
            ok(tf);
            return;
        }
    case 0x0401: fn_caps(tf); return;
    case 0x0B00: case 0x0B01: case 0x0B02: case 0x0B03: fn_watch(tf, fn); return;
    case 0x0600: case 0x0601: case 0x0602: case 0x0603: case 0x0702: case 0x0703:
        ok(tf);                                 /* locking and discarding: nothing pages out */
        return;
    case 0x0604:
        SET16(tf->ebx, 0);
        SET16(tf->ecx, 0x1000);
        ok(tf);
        return;
    case 0x0900: case 0x0901: case 0x0902:
        v = vm.vif;
        if (fn != 0x0902)
            vm.vif = fn == 0x0901;
        SETLO(tf->eax, v);
        ok(tf);
        return;
    case 0x0A00: fn_vendor(tf); return;
    case 0x0D00: case 0x0D01: case 0x0D02: case 0x0D03:
        fail(tf, 0x8001);                       /* shared memory (1.0): DOS/4GW probes it */
        return;
    case 0x0E00:                                /* the client's MP and EM; the FPU is real (MP), no host EM */
        SET16(tf->eax, (dctx->fpu_msw & 3) | 0x0004 | ((vm.bi->cpu_family >= 5 ? 4 : vm.bi->cpu_family) << 4));
        ok(tf);
        return;
    case 0x0E01:                                /* BX bit 0 MP, bit 1 EM: the client emulates (#NM to it) */
        dctx->fpu_msw = (u8)(tf->ebx & 3);
        ok(tf);
        return;
    default:
        dpmi_unimpl(tf, "int31");
        fail(tf, 0x8001);
    }
}
