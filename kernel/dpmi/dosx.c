/* DOS API translation for 16-bit clients (supervisor.md §12.1d; M4d), as
 * Windows' DOSX gives its 16-bit clients and HDPMI16 copies: INT 21h from
 * protected mode with selectors where DOS wants segments. Borland's RTM
 * relies on it (TPX: INT 21h 50h with its PSP's selector, 34h and 5D06h to
 * find the InDOS flag through a selector); 32-bit clients bring their own
 * extender (DJGPP, DOS/4GW) and get plain reflection, as under CWSDPMI.
 *
 * Segments DOS returns become selectors (0002h's, which last as long as the
 * context), selectors the client passes become segments, and buffers the
 * call reads or writes go through an 8 KB block of DOS memory taken the
 * first time one is needed. Pointer functions not listed are logged once
 * (GLOS-DPMI-UNIMPL dosx-21-NN) and reflected as they are. */
#include <string.h>

#include "glos/bootinfo.h"
#include "arch.h"
#include "dpmi.h"
#include "kprintf.h"
#include "vm.h"

#define XBUF_PARAS 0x200u                       /* 8 KB ... */
#define XBUF_DTA   0x1F00u                      /* ... the DOS DTA the client's stands for, at its end */
#define XBUF_DATA  0x1E00u                      /* bytes of data per DOS call (3Fh, 40h, 09h) */

static u16 xbuf(struct trapframe *tf)
{
    struct rmregs r;
    if (dctx->xbuf_seg)
        return dctx->xbuf_seg;
    memset(&r, 0, sizeof r);
    r.eax = 0x4800;
    r.ebx = XBUF_PARAS;
    r.flags = 2;
    if (rm_call(tf, &r, RM_INT, 0x21, 0, 0) != 0 || (r.flags & FL_CF))
        return 0;
    dctx->xbuf_seg = (u16)r.eax;
    return dctx->xbuf_seg;
}

/* INT 21h in real mode with tf's general registers and arithmetic flags,
   DS and ES as given, and in place of the client's BX, DX, SI or DI (ovr: 1,
   2, 4, 8) the buffer offsets given. The results come back to tf, but for
   those, which keep the client's own. -1 if no real-mode call could be made. */
static int dos(struct trapframe *tf, u16 ds, u16 es, int ovr, u32 bx, u32 dx, u32 si, u32 di)
{
    struct rmregs r;
    memset(&r, 0, sizeof r);
    r.eax = tf->eax & 0xFFFF;
    r.ebx = ovr & 1 ? bx : tf->ebx & 0xFFFF;
    r.ecx = tf->ecx & 0xFFFF;
    r.edx = ovr & 2 ? dx : tf->edx & 0xFFFF;
    r.esi = ovr & 4 ? si : tf->esi & 0xFFFF;
    r.edi = ovr & 8 ? di : tf->edi & 0xFFFF;
    r.ebp = tf->ebp & 0xFFFF;
    r.ds = ds;
    r.es = es;
    r.flags = (u16)((tf->eflags & FL_ARITH) | (vm.vif ? FL_IF : 0) | 2);
    if (rm_call(tf, &r, RM_INT, 0x21, 0, 0) != 0)
        return -1;
    SET16(tf->eax, r.eax);
    SET16(tf->ecx, r.ecx);
    if (!(ovr & 1))
        SET16(tf->ebx, r.ebx);
    if (!(ovr & 2))
        SET16(tf->edx, r.edx);
    if (!(ovr & 4))
        SET16(tf->esi, r.esi);
    if (!(ovr & 8))
        SET16(tf->edi, r.edi);
    tf->eflags = (tf->eflags & ~FL_ARITH) | (r.flags & FL_ARITH);
    dctx->xret_ds = r.ds;
    dctx->xret_es = r.es;
    dctx->xret_si = (u16)r.esi;
    return 0;
}

/* The client's bytes into the buffer, or the buffer's out to the client. */
static int to_dos(u16 sel, u32 off, u16 seg, u32 at, u32 n) { return user_rd(sel, off & 0xFFFF, vm_ptr(vm_lin(seg, 0) + at), n); }
static int from_dos(u16 seg, u32 at, u16 sel, u32 off, u32 n) { return user_wr(sel, off & 0xFFFF, vm_ptr(vm_lin(seg, 0) + at), n); }

/* An ASCIIZ string of the client's into the buffer at at; its length, or -1. */
static int str_to_dos(u16 sel, u32 off, u16 seg, u32 at, u32 max)
{
    u32 i;
    u8 c;
    for (i = 0; i < max; i++) {
        if (user_rd(sel, (off + i) & 0xFFFF, &c, 1) != 0)
            return -1;
        vm_wr8(vm_lin(seg, 0) + at + i, c);
        if (!c)
            return (int)i;
    }
    return -1;
}

static void fail21(struct trapframe *tf, u16 err)
{
    SET16(tf->eax, err);
    tf->eflags |= FL_CF;
}

/* INT 31h on a copy of the frame (0100h-0102h, 0204h/0205h): the copy. */
static struct trapframe call31(const struct trapframe *tf, u32 ax, u32 bx, u32 cx, u32 dx)
{
    struct trapframe t = *tf;
    t.eax = ax;
    t.ebx = bx;
    t.ecx = cx;
    t.edx = dx;
    int31(&t);
    return t;
}

static void unimpl(struct trapframe *tf, u8 ah)
{
    static u8 seen[256];
    if (!seen[ah]++)
        kprintf("GLOS-DPMI-UNIMPL dosx-21-%02x ax=%04x from=%04x:%08x\n", ah, tf->eax & 0xFFFF, tf->cs & 0xFFFF,
                tf->eip);
}

static int dosx_call(struct trapframe *tf);

/* 1 if translated (tf has the results), 0 for plain reflection; under
   /DPMITRACE the first ones are logged with their results. */
int dosx_int21(struct trapframe *tf)
{
    static u32 n;
    u32 ax = tf->eax & 0xFFFF, bx = tf->ebx & 0xFFFF, dx = tf->edx & 0xFFFF;
    u16 ds = (u16)tf->ds, es = (u16)tf->es;
    int r = dosx_call(tf);
    if (r && (vm.bi->flags & BI_F_DPMITRACE) && n++ < 200)
        kprintf("GLOS-DPMI dosx ax=%04x bx=%04x dx=%04x ds=%04x es=%04x -> cf=%u ax=%04x bx=%04x ds=%04x es=%04x\n",
                ax, bx, dx, ds, es, tf->eflags & FL_CF, tf->eax & 0xFFFF, tf->ebx & 0xFFFF, tf->ds & 0xFFFF,
                tf->es & 0xFFFF);
    return r;
}

static int dosx_call(struct trapframe *tf)
{
    u8 ah = (u8)(tf->eax >> 8), al = (u8)tf->eax;
    u16 ds = (u16)tf->ds, es = (u16)tf->es, seg;
    u32 dx = tf->edx & 0xFFFF, n, done;
    struct trapframe t;
    int len;

    if (dctx->bits32)
        return 0;
    switch (ah) {
    /* segments out: selectors */
    case 0x34: case 0x52:                       /* InDOS, List of Lists: ES:BX */
        if (dos(tf, 0, 0, 0, 0, 0, 0, 0) != 0)
            return 0;
        tf->es = dpmi_seg_sel(dctx->xret_es);
        return 1;
    case 0x1B: case 0x1C: case 0x1F: case 0x32: /* DS:BX: a drive's tables */
        if (dos(tf, 0, 0, 0, 0, 0, 0, 0) != 0)
            return 0;
        if ((tf->eax & 0xFF) != 0xFF)
            tf->ds = dpmi_seg_sel(dctx->xret_ds);
        return 1;
    case 0x5D:
        if (al != 0x06)
            break;
        if (dos(tf, 0, 0, 0, 0, 0, 0, 0) != 0)    /* the swappable data area: DS:SI */
            return 0;
        if (!(tf->eflags & FL_CF))
            tf->ds = dpmi_seg_sel(dctx->xret_ds);
        return 1;
    /* the PSP, as a selector */
    case 0x51: case 0x62:
        if (dos(tf, 0, 0, 0, 0, 0, 0, 0) != 0)
            return 0;
        seg = (u16)tf->ebx;
        SET16(tf->ebx, seg == dctx->psp ? dctx->psp_sel : dpmi_seg_sel(seg));
        return 1;
    case 0x50:
        seg = (u16)tf->ebx;
        if (seg == dctx->psp_sel)
            seg = dctx->psp;
        else if (ldt_valid(seg) && !(sel_base(seg) & 0xF) && sel_base(seg) < 0x100000)
            seg = (u16)(sel_base(seg) >> 4);
        dos(tf, 0, 0, 1, seg, 0, 0, 0);
        return 1;
    /* the DTA: the client's own, and a DOS one in the buffer for 4Eh/4Fh to fill */
    case 0x1A:
        dctx->dta_sel = ds;
        dctx->dta_off = (u16)dx;
        return 1;
    case 0x2F:
        if (!dctx->dta_sel) {
            dctx->dta_sel = dctx->psp_sel;
            dctx->dta_off = 0x80;
        }
        tf->es = dctx->dta_sel;
        SET16(tf->ebx, dctx->dta_off);
        return 1;
    case 0x4E: case 0x4F:
        if (!(seg = xbuf(tf)))
            return 0;
        {
            struct trapframe s = *tf;           /* the DOS DTA: ours */
            SET16(s.eax, 0x1A00);
            dos(&s, seg, 0, 2, 0, XBUF_DTA, 0, 0);
        }
        if (ah == 0x4E && str_to_dos(ds, dx, seg, 0, 260) < 0)
            return fail21(tf, 3), 1;
        if (ah == 0x4F && dctx->dta_sel)        /* find-next carries on from the client's DTA */
            to_dos(dctx->dta_sel, dctx->dta_off, seg, XBUF_DTA, 43);
        if (dos(tf, seg, 0, 2, 0, 0, 0, 0) != 0)
            return 0;
        if (!(tf->eflags & FL_CF)) {
            if (!dctx->dta_sel) {
                dctx->dta_sel = dctx->psp_sel;
                dctx->dta_off = 0x80;
            }
            from_dos(seg, XBUF_DTA, dctx->dta_sel, dctx->dta_off, 43);
        }
        return 1;
    /* vectors: protected mode's, as 0204h/0205h */
    case 0x35:
        t = call31(tf, 0x0204, al, 0, 0);
        tf->es = (u16)t.ecx;
        SET16(tf->ebx, t.edx);
        return 1;
    case 0x25:
        call31(tf, 0x0205, al, ds, dx);
        return 1;
    /* memory: 0100h-0102h, the selector for the segment */
    case 0x48:
        t = call31(tf, 0x0100, tf->ebx & 0xFFFF, 0, 0);
        if (t.eflags & FL_CF) {
            SET16(tf->ebx, t.ebx);
            return fail21(tf, (u16)t.eax), 1;
        }
        SET16(tf->eax, t.edx);
        tf->eflags &= ~FL_CF;
        return 1;
    case 0x49:
        t = call31(tf, 0x0101, 0, 0, es);
        if (t.eflags & FL_CF)
            return fail21(tf, 9), 1;
        tf->es = 0;
        tf->eflags &= ~FL_CF;
        return 1;
    case 0x4A:
        t = call31(tf, 0x0102, tf->ebx & 0xFFFF, 0, es);
        if (t.eflags & FL_CF) {
            SET16(tf->ebx, t.ebx);
            return fail21(tf, (u16)t.eax), 1;
        }
        tf->eflags &= ~FL_CF;
        return 1;
    /* an ASCIIZ path in DS:DX */
    case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x41: case 0x43: case 0x5A: case 0x5B:
        if (!(seg = xbuf(tf)))
            return 0;
        if ((len = str_to_dos(ds, dx, seg, 0, 260)) < 0)
            return fail21(tf, 3), 1;
        if (dos(tf, seg, 0, 2, 0, 0, 0, 0) != 0)
            return 0;
        if (ah == 0x5A && !(tf->eflags & FL_CF))   /* the name it made, after the path */
            from_dos(seg, 0, ds, dx, (u32)len + 14);
        return 1;
    case 0x6C:                                  /* extended open: DS:SI */
        if (!(seg = xbuf(tf)))
            return 0;
        if (str_to_dos(ds, tf->esi, seg, 0, 260) < 0)
            return fail21(tf, 3), 1;
        if (dos(tf, seg, 0, 4, 0, 0, 0, 0) != 0)
            return 0;
        return 1;
    case 0x56:                                  /* rename DS:DX to ES:DI */
        if (!(seg = xbuf(tf)))
            return 0;
        if (str_to_dos(ds, dx, seg, 0, 260) < 0 || str_to_dos(es, tf->edi, seg, 0x200, 260) < 0)
            return fail21(tf, 3), 1;
        if (dos(tf, seg, seg, 2 | 8, 0, 0, 0, 0x200) != 0)
            return 0;
        return 1;
    case 0x47:                                  /* the current directory into DS:SI */
        if (!(seg = xbuf(tf)))
            return 0;
        if (dos(tf, seg, 0, 4, 0, 0, 0, 0) != 0)
            return 0;
        if (!(tf->eflags & FL_CF))
            from_dos(seg, 0, ds, tf->esi, 64);
        return 1;
    case 0x38:                                  /* country information into DS:DX */
        if (dx == 0xFFFF)
            break;
        if (!(seg = xbuf(tf)))
            return 0;
        if (dos(tf, seg, 0, 2, 0, 0, 0, 0) != 0)
            return 0;
        if (!(tf->eflags & FL_CF))
            from_dos(seg, 0, ds, dx, 34);
        return 1;
    case 0x09:                                  /* a '$' string */
        if (!(seg = xbuf(tf)))
            return 0;
        for (n = 0; n < XBUF_DATA - 1; n++) {
            u8 c;
            if (user_rd(ds, (dx + n) & 0xFFFF, &c, 1) != 0)
                break;
            vm_wr8(vm_lin(seg, 0) + n, c);
            if (c == '$')
                break;
        }
        vm_wr8(vm_lin(seg, 0) + n, '$');
        dos(tf, seg, 0, 2, 0, 0, 0, 0);
        return 1;
    case 0x3F: case 0x40:                       /* CX bytes from or to DS:DX, a buffer at a time */
        if (!(seg = xbuf(tf)))
            return 0;
        n = tf->ecx & 0xFFFF;
        done = 0;
        do {
            u32 k = n - done > XBUF_DATA ? XBUF_DATA : n - done, got;
            struct trapframe s = *tf;
            if (ah == 0x40 && to_dos(ds, dx + done, seg, 0, k) != 0)
                return fail21(tf, 5), 1;
            SET16(s.ecx, k);
            if (dos(&s, seg, 0, 2, 0, 0, 0, 0) != 0)
                return 0;
            if (s.eflags & FL_CF) {
                if (done)
                    break;                      /* what went before stands */
                SET16(tf->eax, s.eax);
                tf->eflags |= FL_CF;
                return 1;
            }
            got = s.eax & 0xFFFF;
            if (ah == 0x3F && got && from_dos(seg, 0, ds, dx + done, got) != 0)
                return fail21(tf, 5), 1;
            done += got;
            if (got < k)
                break;
        } while (done < n);
        SET16(tf->eax, done);
        tf->eflags &= ~FL_CF;
        return 1;
    case 0x29:                                  /* parse a name at DS:SI into the FCB at ES:DI */
        if (!(seg = xbuf(tf)))
            return 0;
        for (n = 0; n < 128; n++) {             /* (a command line's worth) */
            u8 c = 0;
            user_rd(ds, (tf->esi + n) & 0xFFFF, &c, 1);
            vm_wr8(vm_lin(seg, 0) + n, c);
            if (!c || c == 0x0D)
                break;
        }
        to_dos(es, tf->edi, seg, 0x100, 37);
        {
            u32 si = tf->esi & 0xFFFF;
            if (dos(tf, seg, seg, 4 | 8, 0, 0, 0, 0x100) != 0)
                return 0;
            SET16(tf->esi, si + (dctx->xret_si & 0xFFFF));  /* DS:SI past what it parsed */
        }
        from_dos(seg, 0x100, es, tf->edi, 37);
        return 1;
    case 0x44:                                  /* IOCTL: only some take a buffer */
        if (al < 2 || (al > 5 && al != 0x0C && al != 0x0D))
            break;
        unimpl(tf, ah);
        break;
    case 0x4B: case 0x0A: case 0x0C: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16:
    case 0x17: case 0x21: case 0x22: case 0x27: case 0x28: case 0x53: case 0x55:
    case 0x5E: case 0x5F: case 0x60: case 0x63: case 0x65: case 0x69: case 0x71: case 0x73:
        unimpl(tf, ah);                         /* pointers GLOS doesn't translate yet */
        break;
    }
    return 0;
}
