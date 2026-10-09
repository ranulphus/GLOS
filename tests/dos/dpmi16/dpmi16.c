/* DPMICONF-16: conformance checks for a DPMI host's services to a 16-bit
 * client (GLOS M4d; docs/milestones-m0-m4.md), the 16-bit counterpart of
 * tests/dos/dpmiconf.c, with the same check names. Open Watcom, small model;
 * dpmi16a.asm has the mode switch, INT 31h and the handlers. main() starts in
 * real mode and switches; from then on nothing calls DOS or the C library's
 * I/O (they would pass selectors to real mode): output goes to COM1 by port.
 * Each check prints
 *     HX-TEST dpmi16-<name> ok|FAIL|INFO [detail]
 * and the run ends with HX-TEST dpmi16-end fails=<n>, exit code 0 or 1. A
 * check must pass on HDPMI16 and HDPMI16i before it may judge GLOS; glos-*
 * checks are GLOS's own behaviour and run only under it.
 *
 *     DPMI16 [child leave|fault]
 * As a child (M4c's levels): take descriptors, memory and INT 61h, then exit
 * with 42 without giving them back, or fault. */
#include <i86.h>
#include <conio.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#pragma pack(1)
struct r32 { unsigned long eax, ebx, ecx, edx, esi, edi; unsigned short ds, es, fl; };
struct rmregs {
    unsigned long edi, esi, ebp, res, ebx, edx, ecx, eax;
    unsigned short flags, es, ds, fs, gs, ip, cs, sp, ss;
};
#pragma pack()

extern int __cdecl dpmi_enter(void);
extern int __cdecl i31(struct r32 *r);
extern int __cdecl i31_words(struct r32 *r, unsigned w0, unsigned w1);
extern int __cdecl i21(struct r32 *r);
extern void __cdecl dos_exit(int code);
extern unsigned __cdecl get_cs(void);
extern unsigned __cdecl get_ds(void);
extern unsigned long __cdecl lsl32(unsigned sel);
extern void __cdecl init_handlers(unsigned alias);
extern void __cdecl set_chain(unsigned alias, int which, unsigned off, unsigned sel);
extern unsigned __cdecl espfix_hi(void);
extern unsigned __cdecl fault_gp(unsigned bx);
extern unsigned __cdecl fault_resume(void);
extern unsigned __cdecl fault_pf(unsigned sel, unsigned off);
extern unsigned __cdecl int60(void);
extern unsigned __cdecl int61(void);
extern unsigned __cdecl dos_ver(void);
extern void __cdecl fpu_divzero(void);
extern int __cdecl raw_roundtrip(void);
extern void __cdecl state_save(int restore);
/* Labels in dpmi16a.asm (one per declaration: __cdecl binds to the first name only). */
extern void __cdecl gp_insn(void);
extern void __cdecl exc_resume(void);
extern void __cdecl pf_insn(void);
extern void __cdecl exc_handler(void);
extern void __cdecl irq8_handler(void);
extern void __cdecl i1c_handler(void);
extern void __cdecl pm60_handler(void);
extern void __cdecl pm61_handler(void);
extern void __cdecl irq13_handler(void);
extern void __cdecl mf_handler(void);
extern void __cdecl rmcb_far(void);
extern void __cdecl rmcb_int(void);

extern unsigned __cdecl psp_sel;
extern volatile unsigned __cdecl exc_hits;
extern volatile unsigned __cdecl exc_err;
extern volatile unsigned __cdecl exc_ip;
extern volatile unsigned __cdecl exc_cs;
extern volatile unsigned __cdecl exc_fl;
extern volatile unsigned __cdecl exc_sp;
extern volatile unsigned __cdecl exc_ss;
extern volatile unsigned __cdecl exc_hss;
extern unsigned __cdecl exc_skip;
extern unsigned __cdecl exc_new_ip;
extern unsigned __cdecl exc_new_sp;
extern unsigned __cdecl exc_sp_before;
extern unsigned __cdecl gp_len;
extern unsigned __cdecl pf_len;
extern volatile unsigned __cdecl irq8_hits;
extern volatile unsigned __cdecl irq8_vif;
extern volatile unsigned __cdecl irq8_ss;
extern volatile unsigned __cdecl irq8_mode;
extern volatile unsigned __cdecl i1c_hits;
extern volatile unsigned __cdecl hits60;
extern volatile unsigned __cdecl hits61;
extern volatile unsigned __cdecl irq13_hits;
extern volatile unsigned __cdecl mf_hits;
extern volatile unsigned __cdecl rmcb_hits;
extern volatile unsigned __cdecl rmcb_si;
extern volatile unsigned __cdecl rmcb_ds;
extern volatile unsigned long __cdecl rmcb_eax;
extern unsigned long __cdecl rr_pm2rm;
extern unsigned long __cdecl rr_rm2pm;
extern unsigned long __cdecl rr_save;
extern unsigned __cdecl rr_dgroup;
extern unsigned __cdecl rr_code;
extern unsigned __cdecl rr_count;
extern unsigned __cdecl rr_dosver;
extern volatile unsigned __cdecl x10_hits;
extern unsigned __cdecl x10_words[48];
extern unsigned long __cdecl x10_handle;
extern unsigned long __cdecl x10_off;
extern unsigned __cdecl x10_ret32;
extern void __cdecl x10_handler(void);

static int fails, is_glos;
static unsigned my_cs, my_ds, alias, sel40;
static struct r32 R;

static void ser(const char *s)
{
    for (; *s; s++) {
        unsigned n = 0;
        while (!(inp(0x3FD) & 0x20) && ++n < 60000u) ;
        outp(0x3F8, *s);
    }
}

static void say(const char *name, int ok, const char *fmt, ...)
{
    char line[200], detail[150];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(detail, fmt, ap);
    va_end(ap);
    if (ok < 0)
        sprintf(line, "HX-TEST dpmi16-%s INFO %s\r\n", name, detail);
    else
        sprintf(line, "HX-TEST dpmi16-%s %s%s%s\r\n", name, ok ? "ok" : "FAIL", *detail ? " " : "", detail);
    if (ok == 0)
        fails++;
    ser(line);
}

/* INT 31h with 16-bit register values (DS = ES = ours); 0 or CF. */
static int c31(unsigned ax, unsigned bx, unsigned cx, unsigned dx, unsigned si, unsigned di)
{
    memset(&R, 0, sizeof R);
    R.eax = ax;
    R.ebx = bx;
    R.ecx = cx;
    R.edx = dx;
    R.esi = si;
    R.edi = di;
    R.ds = R.es = my_ds;
    return i31(&R);
}

#define AX ((unsigned)R.eax)
#define BX ((unsigned)R.ebx)
#define CX ((unsigned)R.ecx)
#define DX ((unsigned)R.edx)
#define SI ((unsigned)R.esi)
#define DI ((unsigned)R.edi)

static unsigned long get_base(unsigned sel)
{
    return c31(0x0006, sel, 0, 0, 0, 0) ? 0xFFFFFFFFul : ((unsigned long)CX << 16) | DX;
}

static int set_base(unsigned sel, unsigned long b) { return c31(0x0007, sel, (unsigned)(b >> 16), (unsigned)b, 0, 0); }
static int set_limit(unsigned sel, unsigned long l) { return c31(0x0008, sel, (unsigned)(l >> 16), (unsigned)l, 0, 0); }
static unsigned alloc_sel(unsigned n) { return c31(0x0000, 0, n, 0, 0, 0) ? 0 : AX; }
static int free_sel(unsigned s) { return c31(0x0001, s, 0, 0, 0, 0); }

#define PEEKL(s, o) (*(volatile unsigned long __far *)MK_FP(s, o))
#define PEEKW(s, o) (*(volatile unsigned __far *)MK_FP(s, o))

static unsigned long ticks(void) { return PEEKL(sel40, 0x6C); }

static int wait_ticks(int n)
{
    unsigned long t = ticks(), k;
    for (k = 0; k < 40000000ul; k++)
        if (ticks() - t >= (unsigned long)n)
            return 1;
    return 0;
}

/* 0204h/0205h */
static void get_pmvec(int v, unsigned *sel, unsigned *off)
{
    c31(0x0204, v, 0, 0, 0, 0);
    *sel = CX;
    *off = DX;
}
static int set_pmvec(int v, unsigned sel, unsigned off) { return c31(0x0205, v, sel, off, 0, 0); }

/* 0300h-0302h with a register structure of ours. */
static int rmcall(unsigned fn, int vec, struct rmregs *r)
{
    memset(&R, 0, sizeof R);
    R.eax = fn;
    R.ebx = vec;
    R.edi = (unsigned)r;
    R.ds = R.es = my_ds;
    return i31(&R);
}

/* ---- descriptors */

static void t_ldt(void)
{
    unsigned sel, inc, i, a;
    int ok = 1;
    unsigned char d[8], e[8];

    sel = alloc_sel(4);
    c31(0x0003, 0, 0, 0, 0, 0);
    inc = AX;
    say("ldt-alloc", sel != 0 && inc == 8, "sel=%04x inc=%u", sel, inc);
    if (!sel)
        return;
    for (i = 0; i < 4; i++) {
        set_base(sel + i * inc, 0x123000ul + i * 0x1000);
        set_limit(sel + i * inc, 0xFFF);
        if (get_base(sel + i * inc) != 0x123000ul + i * 0x1000 || lsl32(sel + i * inc) != 0xFFF)
            ok = 0;
    }
    say("ldt-base-limit", ok, "");
    set_limit(sel, 0x1FFFFFul);
    say("ldt-limit-big", lsl32(sel) == 0x1FFFFFul, "lsl=%lx", lsl32(sel));
    set_limit(sel, 0xFFF);
    ok = c31(0x0009, sel, 0x00F2, 0, 0, 0) == 0;               /* 16-bit data, DPL 3 */
    memset(&R, 0, sizeof R);
    R.eax = 0x000B;
    R.ebx = sel;
    R.edi = (unsigned)d;
    R.ds = R.es = my_ds;
    ok = ok && i31(&R) == 0 && d[5] == 0xF2 && !(d[6] & 0x40);
    say("ldt-rights", ok, "access=%02x flags=%02x", d[5], d[6] & 0xF0);
    memcpy(e, d, 8);
    e[2] = 0x56;
    memset(&R, 0, sizeof R);
    R.eax = 0x000C;
    R.ebx = sel + inc;
    R.edi = (unsigned)e;
    R.ds = R.es = my_ds;
    ok = i31(&R) == 0;
    memset(&R, 0, sizeof R);
    R.eax = 0x000B;
    R.ebx = sel + inc;
    R.edi = (unsigned)d;
    R.ds = R.es = my_ds;
    ok = ok && i31(&R) == 0 && !memcmp(d, e, 8);
    say("ldt-set-desc", ok, "");
    a = c31(0x000A, my_cs, 0, 0, 0, 0) ? 0 : AX;
    say("ldt-alias", a && get_base(a) == get_base(my_cs) && lsl32(a) == lsl32(my_cs), "alias=%04x", a);
    if (a)
        free_sel(a);
    ok = 1;
    for (i = 0; i < 4; i++)
        if (free_sel(sel + i * inc))
            ok = 0;
    say("ldt-free", ok, "");
    {
        unsigned s1, s2;
        s1 = c31(0x0002, 0x40, 0, 0, 0, 0) ? 0 : AX;
        s2 = c31(0x0002, 0x40, 0, 0, 0, 0) ? 0 : AX;
        say("seg2desc", s1 && s1 == s2 && get_base(s1) == 0x400, "sel=%04x base=%lx", s1, get_base(s1));
    }
}

/* ---- DOS memory */

static unsigned dos_sel, dos_seg;

static void t_dos(void)
{
    unsigned sel, seg, s2;
    int ok;

    ok = c31(0x0100, 0x100, 0, 0, 0, 0) == 0;
    seg = AX;
    sel = DX;
    if (ok) {
        unsigned flat = c31(0x0002, seg, 0, 0, 0, 0) ? 0 : AX;       /* the same memory, by its segment */
        PEEKL(sel, 0) = 0x12345678ul;
        ok = flat && PEEKL(flat, 0) == 0x12345678ul && lsl32(sel) == 0xFFF;
    }
    say("dos-alloc", ok, "seg=%04x sel=%04x", seg, sel);
    if (ok) {
        say("dos-resize", c31(0x0102, 0x80, 0, sel, 0, 0) == 0, "");
        say("dos-free", c31(0x0101, 0, 0, sel, 0, 0) == 0, "");
    }
    ok = c31(0x0100, 0x1800, 0, 0, 0, 0) == 0;                  /* 96 KB: two tiled selectors */
    seg = AX;
    sel = DX;
    s2 = sel + 8;
    say("dos-tiled", ok && get_base(s2) == get_base(sel) + 0x10000ul && lsl32(s2) == 0x7FFF
        && get_base(sel) == (unsigned long)seg * 16, "sel=%04x limits=%lx/%lx", sel, lsl32(sel), lsl32(s2));
    if (ok)
        c31(0x0101, 0, 0, sel, 0, 0);
    ok = c31(0x0100, 0xFFFF, 0, 0, 0, 0) != 0;
    say("dos-too-big", ok && BX > 0 && BX < 0xFFFF && AX == 8, "ax=%04x largest=%04x", AX, BX);
    if (c31(0x0100, 0x40, 0, 0, 0, 0) == 0) {                     /* for the real-mode code below */
        dos_seg = AX;
        dos_sel = DX;
    }
}

/* ---- vectors and real-mode calls */

static void t_rm(void)
{
    static const unsigned char code[] = {
        0xB8, 0x34, 0x12, 0xCB,                                 /* 0: mov ax,1234h / retf */
        0xBB, 0x78, 0x56, 0xCF,                                 /* 4: mov bx,5678h / iret */
        0x89, 0xE5, 0x8B, 0x46, 0x04, 0xCB,                     /* 8: mov bp,sp / mov ax,[bp+4] / retf */
        0xB9, 0x21, 0x43, 0xCF,                                 /* 14: mov cx,4321h / iret (INT 60h) */
    };
    static struct rmregs r;
    unsigned oseg, ooff, psel, poff, cx;

    if (!dos_seg) {
        say("rm-setup", 0, "no DOS memory");
        return;
    }
    _fmemcpy(MK_FP(dos_sel, 0), code, sizeof code);
    memset(&r, 0, sizeof r);
    r.cs = dos_seg;
    r.ip = 0;
    say("rmcall-far", rmcall(0x0301, 0, &r) == 0 && (unsigned)r.eax == 0x1234, "ax=%04x", (unsigned)r.eax);
    memset(&r, 0, sizeof r);
    r.cs = dos_seg;
    r.ip = 4;
    say("rmcall-iret", rmcall(0x0302, 0, &r) == 0 && (unsigned)r.ebx == 0x5678, "bx=%04x", (unsigned)r.ebx);
    memset(&r, 0, sizeof r);
    r.cs = dos_seg;
    r.ip = 8;
    memset(&R, 0, sizeof R);
    R.eax = 0x0301;
    R.ecx = 2;
    R.edi = (unsigned)&r;
    R.ds = R.es = my_ds;
    say("rmcall-stack", i31_words(&R, 0xBEEF, 0xCAFE) == 0 && (unsigned)r.eax == 0xBEEF, "ax=%04x",
        (unsigned)r.eax);
    memset(&r, 0, sizeof r);
    r.eax = 0x3000;
    say("rmint-21-30", rmcall(0x0300, 0x21, &r) == 0 && (r.eax & 0xFF) >= 2, "ver=%u.%02u",
        (unsigned)(r.eax & 0xFF), (unsigned)(r.eax >> 8) & 0xFF);

    c31(0x0200, 0x60, 0, 0, 0, 0);
    oseg = CX;
    ooff = DX;
    c31(0x0201, 0x60, dos_seg, 14, 0, 0);
    c31(0x0200, 0x60, 0, 0, 0, 0);
    say("rm-vector", CX == dos_seg && DX == 14, "");
    memset(&r, 0, sizeof r);
    say("rmint-hooked", rmcall(0x0300, 0x60, &r) == 0 && (unsigned)r.ecx == 0x4321, "cx=%04x", (unsigned)r.ecx);
    cx = int60();
    say("pmint-reflected", cx == 0x4321, "cx=%04x", cx);
    get_pmvec(0x60, &psel, &poff);
    hits60 = 0;
    set_pmvec(0x60, my_cs, (unsigned)pm60_handler);
    int60();
    int60();
    say("pm-vector", hits60 == 2, "hits=%u", hits60);
    set_pmvec(0x60, psel, poff);
    cx = int60();
    say("pm-vector-restored", cx == 0x4321 && hits60 == 2, "cx=%04x", cx);
    c31(0x0201, 0x60, oseg, ooff, 0, 0);
    cx = dos_ver();
    say("int21-reflected", (cx & 0xFF) >= 2, "ver=%u.%02u", cx & 0xFF, cx >> 8);
}

/* ---- linear memory */

static void t_mem(void)
{
    unsigned long info[12], a, b, c, i;
    unsigned ha[2], hb[2], sel = alloc_sel(1);
    int ok;

    memset(&R, 0, sizeof R);
    R.eax = 0x0500;
    R.edi = (unsigned)info;
    R.ds = R.es = my_ds;
    ok = i31(&R) == 0;
    say("mem-info", ok && info[0] >= 1024ul * 1024, "largest=%lu", info[0]);
    ok = c31(0x0501, 1, 0, 0, 0, 0) == 0;                       /* BX:CX 64 KB */
    a = ((unsigned long)BX << 16) | CX;
    ha[0] = SI;
    ha[1] = DI;
    ok = ok && c31(0x0501, 1, 0, 0, 0, 0) == 0;
    b = ((unsigned long)BX << 16) | CX;
    hb[0] = SI;
    hb[1] = DI;
    say("mem-alloc", ok, "a=%08lx b=%08lx", a, b);
    say("mem-ascending", ok && b > a, "");
    if (!ok || !sel)
        return;
    set_base(sel, a);
    set_limit(sel, 0xFFFF);
    PEEKL(sel, 0) = 0xA5A5A5A5ul;
    PEEKL(sel, 0xFFFC) = 0x5A5A5A5Aul;
    ok = c31(0x0503, 0x40, 0, 0, ha[0], ha[1]) == 0;            /* to 4 MB */
    c = ((unsigned long)BX << 16) | CX;
    if (ok) {
        ha[0] = SI;
        ha[1] = DI;
        set_base(sel, c);
        ok = PEEKL(sel, 0) == 0xA5A5A5A5ul && PEEKL(sel, 0xFFFC) == 0x5A5A5A5Aul;
        for (i = 0; i < 64 && ok; i++) {                        /* every 64 KB of it, through the selector */
            set_base(sel, c + i * 0x10000ul);
            PEEKL(sel, 0x1230) = i;
        }
        for (i = 0; i < 64 && ok; i++) {
            set_base(sel, c + i * 0x10000ul);
            ok = PEEKL(sel, 0x1230) == i;
        }
    }
    say("mem-resize", ok, "from=%08lx to=%08lx", a, c);
    say("mem-free", c31(0x0502, 0, 0, 0, ha[0], ha[1]) == 0 && c31(0x0502, 0, 0, 0, hb[0], hb[1]) == 0, "");
    free_sel(sel);
    say("phys-map", c31(0x0800, 0xE000, 0, 0, 1, 0) == 0, "linear=%04x%04x", BX, CX);  /* 64 KB at E0000000h */
}

/* ---- the rest */

static void t_misc(void)
{
    int was, now;
    unsigned long base = get_base(my_ds);

    c31(0x0400, 0, 0, 0, 0, 0);
    say("version", AX == 0x005A, "%u.%02u flags=%x cpu=%u pic=%02x/%02x", AX >> 8, AX & 0xFF, BX, CX & 0xFF,
        DX >> 8, DX & 0xFF);
    say("pic-bases", DX == 0x0870, "");
    c31(0x0900, 0, 0, 0, 0, 0);
    was = AX & 0xFF;
    c31(0x0902, 0, 0, 0, 0, 0);
    now = AX & 0xFF;
    c31(0x0901, 0, 0, 0, 0, 0);
    c31(0x0902, 0, 0, 0, 0, 0);
    say("vif", was == 1 && now == 0 && (AX & 0xFF) == 1, "was=%d off=%d", was, now);
    say("page-size", c31(0x0604, 0, 0, 0, 0, 0) == 0 && BX == 0 && CX == 4096, "page=%u", CX);
    say("lock", c31(0x0600, (unsigned)(base >> 16), (unsigned)base, 0, 0, 4) == 0
        && c31(0x0601, (unsigned)(base >> 16), (unsigned)base, 0, 0, 4) == 0, "");
    say("raw-switch", c31(0x0306, 0, 0, 0, 0, 0) == 0 && BX && SI, "rm=%04x:%04x pm=%04x:%04x", BX, CX, SI, DI);
    c31(0x0305, 0, 0, 0, 0, 0);
    say("state-save", -1, "bytes=%u rm=%04x:%04x pm=%04x:%04x", AX, BX, CX, SI, DI);
    {
        static const char name[] = "RATIONAL DOS/4G";
        say("vendor-other", c31(0x0A00, 0, 0, 0, (unsigned)name, 0) != 0, "");
    }
    c31(0x0E00, 0, 0, 0, 0, 0);
    say("fpu", -1, "status=%04x", AX);
    {                                                           /* no ring-0 bits: ours kept, or zero (HDPMI16) */
        unsigned hi = espfix_hi();
        say("espfix16", hi == 0x1234 || hi == 0, "hi=%04x", hi);
    }
}

/* ---- exceptions */

static unsigned alt_stack[256];

static void set_exc(int n, unsigned off, unsigned *osel, unsigned *ooff)
{
    if (osel) {
        c31(0x0202, n, 0, 0, 0, 0);
        *osel = CX;
        *ooff = DX;
    }
    c31(0x0203, n, my_cs, off, 0, 0);
}

static void t_exc(void)
{
    unsigned os13, of13, os14, of14, bx, sp, sel, h[2], attr[3];
    unsigned long lin;
    int ok;

    set_exc(13, (unsigned)exc_handler, &os13, &of13);
    exc_hits = 0;
    exc_skip = gp_len;
    bx = fault_gp(0x1357);
    say("exc-gp", exc_hits == 1 && exc_ip == (unsigned)gp_insn && exc_cs == my_cs, "hits=%u ip=%x(%x) cs=%x",
        exc_hits, exc_ip, (unsigned)gp_insn, exc_cs);
    say("exc-gp-err", -1, "err=%x", exc_err);
    say("exc-frame-stack", exc_ss == my_ds && exc_sp == exc_sp_before && (exc_fl & 0x200), "ss=%x sp=%x(%x) fl=%x",
        exc_ss, exc_sp, exc_sp_before, exc_fl);
    say("exc-regs-kept", bx == 0x1357, "bx=%x", bx);
    say("exc-host-stack", -1, "ss=%x (ours %x)", exc_hss, my_ds);
    exc_skip = 0;
    exc_new_ip = (unsigned)exc_resume;
    exc_new_sp = (unsigned)&alt_stack[256];
    sp = fault_resume();
    exc_new_ip = exc_new_sp = 0;
    say("exc-frame-edit", sp == (unsigned)&alt_stack[256] && exc_hits == 2, "sp=%x want=%x", sp,
        (unsigned)&alt_stack[256]);
    c31(0x0203, 13, os13, of13, 0, 0);

    /* 0507h/0506h: page 1 of a 3-page block uncommitted, a #PF there, back */
    sel = alloc_sel(1);
    if (!sel || c31(0x0501, 0, 3 * 4096, 0, 0, 0) != 0) {
        say("page-attr", 0, "no memory");
        return;
    }
    lin = ((unsigned long)BX << 16) | CX;
    h[0] = SI;
    h[1] = DI;
    set_base(sel, lin);
    set_limit(sel, 3 * 4096 - 1);
    PEEKL(sel, 0x1000) = 0x55AA55AAul;
    memset(&R, 0, sizeof R);
    attr[0] = 0;
    R.eax = 0x0507;
    R.esi = ((unsigned long)h[0] << 16) | h[1];
    R.ebx = 0x1000;
    R.ecx = 1;
    R.edx = (unsigned)attr;
    R.ds = R.es = my_ds;
    ok = i31(&R) == 0;
    memset(&R, 0, sizeof R);
    R.eax = 0x0506;
    R.esi = ((unsigned long)h[0] << 16) | h[1];
    R.ebx = 0;
    R.ecx = 3;
    R.edx = (unsigned)attr;
    R.ds = R.es = my_ds;
    ok = ok && i31(&R) == 0;
    say("page-attr", ok && (attr[0] & 7) == 1 && (attr[1] & 7) == 0 && (attr[0] & 8), "attr=%04x %04x %04x",
        attr[0], attr[1], attr[2]);
    if (ok) {
        unsigned v;
        set_exc(14, (unsigned)exc_handler, &os14, &of14);
        exc_hits = 0;
        exc_skip = pf_len;
        v = fault_pf(sel, 0x1000);
        c31(0x0203, 14, os14, of14, 0, 0);
        say("exc-pf", exc_hits == 1 && exc_ip == (unsigned)pf_insn && (exc_err & 5) == 4 && v == 0,
            "hits=%u err=%x ip=%x v=%x", exc_hits, exc_err, exc_ip, v);
        if (c31(0x0210, 14, 0, 0, 0, 0) == 0) {               /* the 1.0 frame, for a 16-bit client */
            unsigned xs = CX, i;
            unsigned long xo = R.edx;
            char line[120];
            memset(&R, 0, sizeof R);
            R.eax = 0x0212;
            R.ebx = 14;
            R.ecx = my_cs;
            R.edx = (unsigned)x10_handler;
            R.ds = R.es = my_ds;
            ok = i31(&R) == 0;
            x10_hits = 0;
            x10_handle = ((unsigned long)h[0] << 16) | h[1];
            x10_off = 0x1000;
            v = fault_pf(sel, 0x1234);
            memset(&R, 0, sizeof R);
            R.eax = 0x0212;
            R.ebx = 14;
            R.ecx = xs;
            R.edx = xo;
            R.ds = R.es = my_ds;
            i31(&R);
            /* the 0.9 words at 0, the 1.0 dwords at 20h: 16-bit return IP:CS and a zero
               dword, error code, EIP, CS, EFLAGS, ESP, SS, ES, DS, FS, GS, CR2, PTE */
            {
                const unsigned *w = x10_words;
                unsigned long cr2 = w[0x28] | ((unsigned long)w[0x29] << 16);
                say("exc-frame10", ok && x10_hits == 1 && !x10_ret32 && w[3] == (unsigned)pf_insn && w[4] == my_cs
                    && w[0x12] == 0 && w[0x13] == 0 && w[0x16] == (unsigned)pf_insn && w[0x18] == my_cs
                    && w[0x20] == sel && w[0x22] == my_ds && cr2 == lin + 0x1234 && (w[0x2A] & 1) == 0,
                    "hits=%u ret32=%u ip=%x/%x cs=%x es=%x ds=%x cr2=%lx pte=%x", x10_hits, x10_ret32, w[3],
                    w[0x16], w[0x18], w[0x20], w[0x22], cr2, w[0x2A]);
            }
            (void)i;
            (void)line;
        } else {
            say("exc-frame10", -1, "0210h refused");
        }
        attr[0] = 9;                                            /* committed, writable */
        memset(&R, 0, sizeof R);
        R.eax = 0x0507;
        R.esi = ((unsigned long)h[0] << 16) | h[1];
        R.ebx = 0x1000;
        R.ecx = 1;
        R.edx = (unsigned)attr;
        R.ds = R.es = my_ds;
        ok = i31(&R) == 0;
        if (ok)
            PEEKL(sel, 0x1004) = 0xC0FFEEul;
        say("page-recommit", ok && PEEKL(sel, 0x1004) == 0xC0FFEEul, "");
    }
    c31(0x0502, 0, 0, 0, h[0], h[1]);
    free_sel(sel);
}

/* ---- IRQs and INTs passed up */

static void hook(int vec, unsigned off, int chain, unsigned *osel, unsigned *ooff)
{
    get_pmvec(vec, osel, ooff);
    if (chain >= 0)
        set_chain(alias, chain, *ooff, *osel);
    set_pmvec(vec, my_cs, off);
}

static void t_irq(void)
{
    static const unsigned char wait3[] = {                      /* real mode: STI, wait 3 BIOS ticks, RETF */
        0xFB, 0x1E, 0x31, 0xC0, 0x8E, 0xD8, 0x8B, 0x1E, 0x6C, 0x04, 0x83, 0xC3, 0x03,
        0xA1, 0x6C, 0x04, 0x29, 0xD8, 0x78, 0xF9, 0x1F, 0xCB,
    };
    static struct rmregs r;
    unsigned s8, o8, s1c, o1c, s75, o75, s16, o16, h0;
    unsigned long t0, k;
    int ok;

    irq8_mode = 0;
    irq8_hits = 0;
    hook(8, (unsigned)irq8_handler, 0, &s8, &o8);
    t0 = ticks();
    ok = wait_ticks(5);
    say("irq-pm", ok && irq8_hits >= 5, "hits=%u ticks=%lu", irq8_hits, ticks() - t0);
    say("irq-pm-int31", irq8_vif == 0, "vif=%u", irq8_vif);
    c31(0x0902, 0, 0, 0, 0, 0);
    say("irq-pm-vif-after", (AX & 0xFF) == 1, "");
    say("irq-pm-stack", -1, "ss=%x (ours %x)", irq8_ss, my_ds);
    if (dos_seg) {
        _fmemcpy(MK_FP(dos_sel, 0x100), wait3, sizeof wait3);
        memset(&r, 0, sizeof r);
        r.cs = dos_seg;
        r.ip = 0x100;
        h0 = irq8_hits;
        ok = rmcall(0x0301, 0, &r) == 0;
        say("irq-pm-from-rm", ok && irq8_hits - h0 >= 2, "hits=%u", irq8_hits - h0);
    }
    irq8_mode = 1;
    h0 = irq8_hits;
    for (k = 0; irq8_hits - h0 < 3 && k < 40000000ul; k++) ;
    irq8_mode = 0;
    c31(0x0902, 0, 0, 0, 0, 0);
    say("irq-iret-vif", irq8_hits - h0 >= 3 && (AX & 0xFF) == 1, "hits=%u vif=%u", irq8_hits - h0, AX & 0xFF);
    set_pmvec(8, s8, o8);

    i1c_hits = 0;
    hook(0x1C, (unsigned)i1c_handler, 1, &s1c, &o1c);
    ok = wait_ticks(4);
    set_pmvec(0x1C, s1c, o1c);
    say("int1c-passup", ok && i1c_hits >= 3, "hits=%u", i1c_hits);

    irq13_hits = mf_hits = 0;
    hook(0x75, (unsigned)irq13_handler, -1, &s75, &o75);
    set_exc(16, (unsigned)mf_handler, &s16, &o16);
    fpu_divzero();
    c31(0x0203, 16, s16, o16, 0, 0);
    set_pmvec(0x75, s75, o75);
    say("fpu-error", irq13_hits + mf_hits == 1, "irq13=%u mf=%u", irq13_hits, mf_hits);
}

/* ---- real-mode callbacks */

static void t_rmcb(void)
{
    static struct rmregs cbr, r;
    unsigned cseg, coff, oseg, ooff;
    int ok;

    rmcb_hits = 0;
    memset(&R, 0, sizeof R);
    R.eax = 0x0303;
    R.esi = (unsigned)rmcb_far;
    R.edi = (unsigned)&cbr;
    R.ds = my_cs;
    R.es = my_ds;
    ok = i31(&R) == 0;
    cseg = CX;
    coff = DX;
    say("rmcb-alloc", ok, "at=%04x:%04x", cseg, coff);
    if (!ok)
        return;
    memset(&r, 0, sizeof r);
    r.cs = cseg;
    r.ip = coff;
    r.eax = 0x12345678ul;
    ok = rmcall(0x0301, 0, &r) == 0;
    say("rmcb-call", ok && rmcb_hits == 1 && rmcb_eax == 0x12345678ul && (unsigned)r.ebx == 0xBEEF,
        "hits=%u eax=%lx bx=%04x", rmcb_hits, rmcb_eax, (unsigned)r.ebx);
    say("rmcb-stack", get_base(rmcb_ds) / 16 == r.ss || r.ss == 0, "dsbase=%lx si=%x", get_base(rmcb_ds), rmcb_si);
    c31(0x0304, 0, cseg, coff, 0, 0);

    rmcb_hits = 0;
    memset(&R, 0, sizeof R);
    R.eax = 0x0303;
    R.esi = (unsigned)rmcb_int;
    R.edi = (unsigned)&cbr;
    R.ds = my_cs;
    R.es = my_ds;
    if (i31(&R) == 0) {
        cseg = CX;
        coff = DX;
        c31(0x0200, 0x66, 0, 0, 0, 0);
        oseg = CX;
        ooff = DX;
        c31(0x0201, 0x66, cseg, coff, 0, 0);
        memset(&r, 0, sizeof r);
        ok = rmcall(0x0300, 0x66, &r) == 0;
        say("rmcb-int", ok && rmcb_hits == 1 && (unsigned)r.ecx == 0xCAFE, "hits=%u cx=%04x", rmcb_hits,
            (unsigned)r.ecx);
        c31(0x0201, 0x66, oseg, ooff, 0, 0);
        c31(0x0304, 0, cseg, coff, 0, 0);
    } else {
        say("rmcb-int", 0, "no second callback");
    }
}

/* ---- 0305h/0306h: to real mode and back on our own selectors */

static void t_raw(void)
{
    unsigned size, n;

    if (c31(0x0306, 0, 0, 0, 0, 0)) {
        say("raw-roundtrip", 0, "no 0306h");
        return;
    }
    rr_rm2pm = ((unsigned long)BX << 16) | CX;
    rr_pm2rm = ((unsigned long)SI << 16) | DI;
    c31(0x0305, 0, 0, 0, 0, 0);
    size = AX;
    rr_save = ((unsigned long)SI << 16) | DI;
    rr_dgroup = (unsigned)(get_base(my_ds) >> 4);
    rr_code = (unsigned)(get_base(my_cs) >> 4);
    if (size && size <= 64)
        state_save(0);
    n = raw_roundtrip();
    if (size && size <= 64)
        state_save(1);
    say("raw-roundtrip", n == 1 && (rr_dosver & 0xFF) >= 2 && get_cs() == my_cs && get_ds() == my_ds,
        "count=%u dos=%u.%02u state=%u", n, rr_dosver & 0xFF, rr_dosver >> 8, size);
}

/* ---- M4d: INT 21h from protected mode, translated by the host for a
   16-bit client (selectors for segments, buffers copied): as Windows' DOSX
   and HDPMI16 do, and Borland's RTM expects */

static int c21(unsigned ax, unsigned bx, unsigned cx, unsigned dx, unsigned si, unsigned di, unsigned ds, unsigned es)
{
    memset(&R, 0, sizeof R);
    R.eax = ax;
    R.ebx = bx;
    R.ecx = cx;
    R.edx = dx;
    R.esi = si;
    R.edi = di;
    R.ds = ds;
    R.es = es;
    return i21(&R);
}

static unsigned char fbuf[10000], gbuf[10000];

static void t_dosx(void)
{
    static const char name[] = "C:\\TEST\\D16.TMP", spec[] = "C:\\TEST\\DPMI16.EXE", parse[] = " FOO.BAR";
    static char cwd[64], dta[64];
    static unsigned char fcb[37];
    unsigned h, i, s, os, oo, sel;
    int ok;

    c21(0x5100, 0, 0, 0, 0, 0, my_ds, my_ds);
    say("dosx-psp", BX == psp_sel, "bx=%04x psp=%04x", BX, psp_sel);
    c21(0x3400, 0, 0, 0, 0, 0, my_ds, my_ds);
    s = R.es;
    say("dosx-indos", lsl32(s) != 0xFFFFFFFFul && PEEKW(s, BX) < 0x100, "es:bx=%04x:%04x", s, BX);
    ok = c21(0x5D06, 0, 0, 0, 0, 0, my_ds, my_ds) == 0;
    say("dosx-sda", ok && lsl32(R.ds) != 0xFFFFFFFFul && get_base(R.ds) == get_base(s), "ds:si=%04x:%04x", R.ds, SI);

    for (i = 0; i < sizeof fbuf; i++)
        fbuf[i] = (unsigned char)(i * 7 + (i >> 8));
    ok = c21(0x3C00, 0, 0, (unsigned)name, 0, 0, my_ds, my_ds) == 0;
    h = AX;
    ok = ok && c21(0x4000, h, sizeof fbuf, (unsigned)fbuf, 0, 0, my_ds, my_ds) == 0 && AX == sizeof fbuf;
    c21(0x3E00, h, 0, 0, 0, 0, my_ds, my_ds);
    ok = ok && c21(0x3D00, 0, 0, (unsigned)name, 0, 0, my_ds, my_ds) == 0;
    h = AX;
    memset(gbuf, 0, sizeof gbuf);
    ok = ok && c21(0x3F00, h, sizeof gbuf, (unsigned)gbuf, 0, 0, my_ds, my_ds) == 0 && AX == sizeof gbuf;
    i = AX;
    c21(0x3E00, h, 0, 0, 0, 0, my_ds, my_ds);
    say("dosx-file", ok && !memcmp(fbuf, gbuf, sizeof fbuf), "read=%u", i);
    say("dosx-delete", c21(0x4100, 0, 0, (unsigned)name, 0, 0, my_ds, my_ds) == 0
        && c21(0x3D00, 0, 0, (unsigned)name, 0, 0, my_ds, my_ds) != 0, "");
    ok = c21(0x4700, 0, 0, 0, (unsigned)cwd, 0, my_ds, my_ds) == 0;
    say("dosx-cwd", ok && !strcmp(cwd, "TEST"), "cwd=%s", cwd);
    c21(0x1A00, 0, 0, (unsigned)dta, 0, 0, my_ds, my_ds);
    ok = c21(0x4E00, 0, 0, (unsigned)spec, 0, 0, my_ds, my_ds) == 0;
    say("dosx-find", ok && !strcmp(dta + 0x1E, "DPMI16.EXE"), "name=%.13s", dta + 0x1E);
    c21(0x2F00, 0, 0, 0, 0, 0, my_ds, my_ds);
    say("dosx-dta", R.es == my_ds && BX == (unsigned)dta, "es:bx=%04x:%04x", R.es, BX);
    c21(0x3560, 0, 0, 0, 0, 0, my_ds, my_ds);
    os = R.es;
    oo = BX;
    hits60 = 0;
    c21(0x2560, 0, 0, (unsigned)pm60_handler, 0, 0, my_cs, my_ds);
    int60();
    c21(0x3560, 0, 0, 0, 0, 0, my_ds, my_ds);
    say("dosx-vector", hits60 == 1 && R.es == my_cs && BX == (unsigned)pm60_handler, "hits=%u", hits60);
    c21(0x2560, 0, 0, oo, 0, 0, os, my_ds);
    ok = c21(0x4800, 0x10, 0, 0, 0, 0, my_ds, my_ds) == 0;
    sel = AX;
    if (ok) {
        PEEKL(sel, 0xFC) = 0xA1B2C3D4ul;
        ok = PEEKL(sel, 0xFC) == 0xA1B2C3D4ul && c21(0x4A00, 0x20, 0, 0, 0, 0, my_ds, sel) == 0
             && c21(0x4900, 0, 0, 0, 0, 0, my_ds, sel) == 0;
    }
    say("dosx-memory", ok, "sel=%04x", sel);
    memset(fcb, 0, sizeof fcb);
    ok = c21(0x2901, 0, 0, 0, (unsigned)parse, (unsigned)fcb, my_ds, my_ds) == 0;
    say("dosx-parse", (AX & 0xFF) == 0 && !memcmp(fcb + 1, "FOO     BAR", 11) && SI == (unsigned)parse + 8,
        "al=%02x si=+%u fcb=%.11s", AX & 0xFF, SI - (unsigned)parse, fcb + 1);
}

/* ---- M4c/M4d: child clients, through INT 21h 4B00h from protected mode */

static int exec_prog(const char *path, const char *args)
{
    static struct rmregs r;
    unsigned n = strlen(args);
    unsigned char __far *m = MK_FP(dos_sel, 0);

    _fstrcpy((char __far *)(m + 0x200), path);
    m[0x280] = (unsigned char)(n + 1);
    m[0x281] = ' ';
    _fmemcpy(m + 0x282, args, n);
    m[0x282 + n] = 0x0D;
    _fmemset(m + 0x300, 0, 0x40);
    *(unsigned __far *)(m + 0x302) = 0x280;                     /* the tail, seg:off */
    *(unsigned __far *)(m + 0x304) = dos_seg;
    *(unsigned __far *)(m + 0x306) = 0x320;                     /* empty FCBs */
    *(unsigned __far *)(m + 0x308) = dos_seg;
    *(unsigned __far *)(m + 0x30A) = 0x320;
    *(unsigned __far *)(m + 0x30C) = dos_seg;
    memset(&r, 0, sizeof r);
    r.eax = 0x4B00;
    r.ds = r.es = dos_seg;
    r.edx = 0x200;
    r.ebx = 0x300;
    if (rmcall(0x0300, 0x21, &r) != 0 || (r.flags & 1))
        return -1 - (int)(r.eax & 0xFF);
    memset(&r, 0, sizeof r);
    r.eax = 0x4D00;
    rmcall(0x0300, 0x21, &r);
    return (int)(r.eax & 0xFF);
}

static void t_nest(const char *self)
{
    unsigned osel, ooff, sel, cx;
    unsigned long base;
    int code;

    if (!dos_seg)
        return;
    get_pmvec(0x61, &osel, &ooff);
    hits61 = 0;
    set_pmvec(0x61, my_cs, (unsigned)pm61_handler);
    sel = alloc_sel(1);
    set_base(sel, 0x12340);
    code = exec_prog(self, "child leave");
    say("nest", code == 42, "code=%d", code);
    if (code != 42)
        goto out;
    {
        unsigned s, o;
        get_pmvec(0x61, &s, &o);
        say("nest-vector-back", s == my_cs && o == (unsigned)pm61_handler, "61h=%04x:%04x", s, o);
    }
    cx = int61();
    base = get_base(sel);
    say("nest-hook-works", hits61 == 1 && cx == 1, "hits=%u", hits61);
    say("nest-selectors-kept", base == 0x12340, "base=%lx", base);
    code = exec_prog(self, "child fault");
    say("nest-child-fault", code != 42 && code >= 0, "code=%d", code);
    if (is_glos) {                                              /* a 32-bit child of this 16-bit client */
        code = exec_prog("C:\\TEST\\DPMICONF.EXE", "child leave");
        say("glos-nest-32in16", code == 42 || code == -3, "code=%d%s", code, code == -3 ? " (no DPMICONF.EXE)" : "");
        cx = int61();
        say("glos-nest-32in16-back", hits61 == 2 && cx == 1 && get_base(sel) == 0x12340, "hits=%u", hits61);
    }
out:
    free_sel(sel);
    set_pmvec(0x61, osel, ooff);
}

static int child(const char *how)
{
    unsigned sel = alloc_sel(8);
    (void)sel;
    c31(0x0501, 0x10, 0, 0, 0, 0);                              /* 1 MB */
    set_pmvec(0x61, my_cs, (unsigned)pm60_handler);
    if (!strcmp(how, "fault"))
        fault_gp(0);                                            /* no handler: the host ends us */
    dos_exit(42);
    return 42;
}

int main(int argc, char **argv)
{
    int e = dpmi_enter();
    if (e) {
        char m[60];
        sprintf(m, "HX-TEST dpmi16-enter FAIL step=%c\r\n", e);
        ser(m);
        return 1;
    }
    my_cs = get_cs();
    my_ds = get_ds();
    alias = c31(0x000A, my_cs, 0, 0, 0, 0) ? 0 : AX;
    init_handlers(alias);
    sel40 = c31(0x0002, 0x40, 0, 0, 0, 0) ? 0 : AX;
    {
        static const char glos[] = "GLOS";
        is_glos = c31(0x0A00, 0, 0, 0, (unsigned)glos, 0) == 0;
    }
    if (argc > 2 && !strcmp(argv[1], "child"))
        return child(argv[2]);
    say("enter", 1, "cs=%04x ds=%04x es(psp)=%04x glos=%d", my_cs, my_ds, psp_sel, is_glos);
    t_ldt();
    t_dos();
    t_rm();
    t_mem();
    t_misc();
    t_exc();
    t_irq();
    t_rmcb();
    t_raw();
    t_dosx();
    t_nest(argv[0]);
    {
        char m[60];
        sprintf(m, "HX-TEST dpmi16-end fails=%d\r\n", fails);
        ser(m);
    }
    dos_exit(fails ? 1 : 0);
    return 0;
}
