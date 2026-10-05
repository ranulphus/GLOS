/* DPMICONF-32: conformance checks for a DPMI host's services to a 32-bit
 * client (GLOS M4a, M4b; docs/milestones-m0-m4.md), as a DJGPP program,
 * with its handlers in dpmiconf_h.S. Each
 * check prints
 *     HX-TEST dpmi-<name> ok|FAIL [detail]
 * on COM1 (or INFO for what hosts may choose), and the run ends with
 *     HX-TEST dpmi-end fails=<n>
 * and exit code 0, or 1 if anything failed. A check must pass on CWSDPMI r7
 * and HDPMI32i before it may judge GLOS: one that fails on both is wrong.
 * Checks named glos-* are GLOS's own behaviour and run only under GLOS. */
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/farptr.h>
#include <sys/movedata.h>
#include <sys/nearptr.h>
#include <sys/exceptn.h>
#include <process.h>
#include <unistd.h>

static int fails;
static int is_glos;

static void ser(const char *s)
{
    for (; *s; s++) {
        int n = 0;
        while (!(inportb(0x3FD) & 0x20) && ++n < 100000) ;
        outportb(0x3F8, *s);
    }
}

static void say(const char *name, int ok, const char *fmt, ...)
{
    char line[200], detail[150] = "";
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(detail, sizeof detail, fmt, ap);
    va_end(ap);
    if (ok < 0)
        snprintf(line, sizeof line, "HX-TEST dpmi-%s INFO %s\r\n", name, detail);
    else
        snprintf(line, sizeof line, "HX-TEST dpmi-%s %s%s%s\r\n", name, ok ? "ok" : "FAIL", *detail ? " " : "",
                 detail);
    if (ok == 0)
        fails++;
    ser(line);
}

/* ---- descriptors */

static void t_ldt(void)
{
    int sel = __dpmi_allocate_ldt_descriptors(4), inc = __dpmi_get_selector_increment_value(), i, ok = 1;
    unsigned long base;
    unsigned char d[8], e[8];
    say("ldt-alloc", sel > 0 && inc == 8, "sel=%04x inc=%d", sel, inc);
    if (sel <= 0)
        return;
    for (i = 0; i < 4; i++) {
        __dpmi_set_segment_base_address(sel + i * inc, 0x123000 + i * 0x1000);
        __dpmi_set_segment_limit(sel + i * inc, 0xFFF);
        if (__dpmi_get_segment_base_address(sel + i * inc, &base) || base != 0x123000ul + i * 0x1000)
            ok = 0;
        if (__dpmi_get_segment_limit(sel + i * inc) != 0xFFF)
            ok = 0;
    }
    say("ldt-base-limit", ok, "");
    __dpmi_set_segment_limit(sel, 0x1FFFFF);                    /* page-granular above 1 MB */
    say("ldt-limit-big", __dpmi_get_segment_limit(sel) == 0x1FFFFF, "lsl=%lx", __dpmi_get_segment_limit(sel));
    __dpmi_set_segment_limit(sel, 0xFFF);
    ok = __dpmi_set_descriptor_access_rights(sel, 0x40F2) == 0;  /* 32-bit data, DPL 3 */
    ok = ok && __dpmi_get_descriptor(sel, d) == 0 && d[5] == 0xF2 && (d[6] & 0x40);
    say("ldt-rights", ok, "access=%02x flags=%02x", d[5], d[6] & 0xF0);
    memcpy(e, d, 8);
    e[2] = 0x56;                                                /* base 15:0 changes */
    ok = __dpmi_set_descriptor(sel + inc, e) == 0 && __dpmi_get_descriptor(sel + inc, d) == 0 && !memcmp(d, e, 8);
    say("ldt-set-desc", ok, "");
    ok = __dpmi_set_descriptor_access_rights(sel + 2 * inc, 0x4092) != 0;      /* DPL 0 (CWSDPMI takes it) */
    say("ldt-dpl0", -1, "refused=%d", ok);
    {
        int alias = __dpmi_create_alias_descriptor(_my_cs());
        unsigned long cb = 1, ab = 2;
        __dpmi_get_segment_base_address(_my_cs(), &cb);
        __dpmi_get_segment_base_address(alias, &ab);
        say("ldt-alias", alias > 0 && cb == ab && __dpmi_get_segment_limit(alias) == __dpmi_get_segment_limit(_my_cs()),
            "alias=%04x", alias);
        if (alias > 0)
            __dpmi_free_ldt_descriptor(alias);
    }
    ok = 1;
    for (i = 0; i < 4; i++)
        if (__dpmi_free_ldt_descriptor(sel + i * inc) != 0)
            ok = 0;
    say("ldt-free", ok, "");
    {
        int s1 = __dpmi_segment_to_descriptor(0x40), s2 = __dpmi_segment_to_descriptor(0x40);
        unsigned long b = 0;
        __dpmi_get_segment_base_address(s1, &b);
        say("seg2desc", s1 > 0 && s1 == s2 && b == 0x400, "sel=%04x base=%lx", s1, b);
    }
}

/* ---- DOS memory */

static int dos_sel, dos_seg;

static void t_dos(void)
{
    int sel, seg, max = 0, ok;
    seg = __dpmi_allocate_dos_memory(0x100, &sel);
    ok = seg > 0;
    if (ok) {
        _farpokel(sel, 0, 0x12345678);
        ok = _farpeekl(_dos_ds, seg * 16) == 0x12345678 && __dpmi_get_segment_limit(sel) == 0xFFF;
    }
    say("dos-alloc", ok, "seg=%04x sel=%04x", seg, sel);
    if (seg > 0) {
        say("dos-resize", __dpmi_resize_dos_memory(sel, 0x80, &max) == 0, "");
        say("dos-free", __dpmi_free_dos_memory(sel) == 0, "");
    }
    seg = __dpmi_allocate_dos_memory(0xFFFF, &max);             /* too much: CF, and the largest in BX */
    say("dos-too-big", seg == -1 && max > 0 && max < 0xFFFF, "largest=%04x", max);
    dos_seg = __dpmi_allocate_dos_memory(0x40, &dos_sel);       /* for the real-mode code below */
}

/* ---- vectors and real-mode calls */

static volatile int hits;
static void pm60(void) { hits++; }

static void t_rm(void)
{
    static const unsigned char code[] = {
        0xB8, 0x34, 0x12, 0xCB,                                 /* 0: mov ax,1234h / retf */
        0xBB, 0x78, 0x56, 0xCF,                                 /* 4: mov bx,5678h / iret */
        0x89, 0xE5, 0x8B, 0x46, 0x04, 0xCB,                     /* 8: mov bp,sp / mov ax,[bp+4] / retf */
        0xB9, 0x21, 0x43, 0xCF,                                 /* 14: mov cx,4321h / iret (INT 60h) */
    };
    __dpmi_regs r;
    __dpmi_raddr old60, v;
    __dpmi_paddr pold, pnew;
    _go32_dpmi_seginfo w;
    unsigned short words[2] = { 0xBEEF, 0xCAFE };

    if (dos_seg <= 0) {
        say("rm-setup", 0, "no DOS memory");
        return;
    }
    movedata(_my_ds(), (unsigned)code, dos_sel, 0, sizeof code);
    memset(&r, 0, sizeof r);
    r.x.cs = dos_seg;
    r.x.ip = 0;
    say("rmcall-far", __dpmi_simulate_real_mode_procedure_retf(&r) == 0 && r.x.ax == 0x1234, "ax=%04x", r.x.ax);
    memset(&r, 0, sizeof r);
    r.x.cs = dos_seg;
    r.x.ip = 4;
    say("rmcall-iret", __dpmi_simulate_real_mode_procedure_iret(&r) == 0 && r.x.bx == 0x5678, "bx=%04x", r.x.bx);
    memset(&r, 0, sizeof r);
    r.x.cs = dos_seg;
    r.x.ip = 8;
    say("rmcall-stack", __dpmi_simulate_real_mode_procedure_retf_stack(&r, 2, words) == 0 && r.x.ax == 0xBEEF,
        "ax=%04x", r.x.ax);
    memset(&r, 0, sizeof r);
    r.h.ah = 0x30;
    say("rmint-21-30", __dpmi_simulate_real_mode_interrupt(0x21, &r) == 0 && r.h.al >= 2, "ver=%d.%02d",
        r.h.al, r.h.ah);

    __dpmi_get_real_mode_interrupt_vector(0x60, &old60);
    v.segment = dos_seg;
    v.offset16 = 14;
    __dpmi_set_real_mode_interrupt_vector(0x60, &v);
    __dpmi_get_real_mode_interrupt_vector(0x60, &v);
    say("rm-vector", v.segment == dos_seg && v.offset16 == 14, "");
    memset(&r, 0, sizeof r);
    say("rmint-hooked", __dpmi_simulate_real_mode_interrupt(0x60, &r) == 0 && r.x.cx == 0x4321, "cx=%04x", r.x.cx);
    {                                                           /* INT 60h in PM, unhooked: to real mode */
        unsigned short cx = 0;
        __asm__ volatile("xorl %%ecx, %%ecx; int $0x60; movw %%cx, %0" : "=r"(cx) :: "ecx", "memory");
        say("pmint-reflected", cx == 0x4321, "cx=%04x", cx);
    }
    __dpmi_get_protected_mode_interrupt_vector(0x60, &pold);
    w.pm_offset = (unsigned long)pm60;
    w.pm_selector = _my_cs();
    if (_go32_dpmi_allocate_iret_wrapper(&w) == 0) {
        pnew.offset32 = w.pm_offset;
        pnew.selector = w.pm_selector;
        __dpmi_set_protected_mode_interrupt_vector(0x60, &pnew);
        __asm__ volatile("int $0x60" ::: "memory");
        __asm__ volatile("int $0x60" ::: "memory");
        say("pm-vector", hits == 2, "hits=%d", hits);
        __dpmi_set_protected_mode_interrupt_vector(0x60, &pold);
        _go32_dpmi_free_iret_wrapper(&w);
        {
            unsigned short cx = 0;
            __asm__ volatile("xorl %%ecx, %%ecx; int $0x60; movw %%cx, %0" : "=r"(cx) :: "ecx", "memory");
            say("pm-vector-restored", cx == 0x4321 && hits == 2, "cx=%04x", cx);
        }
    } else {
        say("pm-vector", 0, "no wrapper");
    }
    __dpmi_set_real_mode_interrupt_vector(0x60, &old60);
    {
        unsigned short ax;
        __asm__ volatile("movw $0x3000, %%ax; int $0x21; movw %%ax, %0" : "=r"(ax) :: "eax", "ebx", "ecx", "memory");
        say("int21-reflected", (ax & 0xFF) >= 2, "ver=%d.%02d", ax & 0xFF, ax >> 8);
    }
}

/* ---- linear memory */

static void t_mem(void)
{
    __dpmi_meminfo a, b, c;
    __dpmi_free_mem_info fi;
    int sel = __dpmi_allocate_ldt_descriptors(1), ok;
    unsigned long i;

    ok = __dpmi_get_free_memory_information(&fi) == 0;
    say("mem-info", ok && fi.largest_available_free_block_in_bytes >= 1024 * 1024, "largest=%lu free_pages=%lu",
        fi.largest_available_free_block_in_bytes, fi.total_number_of_free_pages);

    a.size = 0x10000;
    b.size = 0x10000;
    ok = __dpmi_allocate_memory(&a) == 0 && __dpmi_allocate_memory(&b) == 0;
    say("mem-alloc", ok, "a=%08lx b=%08lx", a.address, b.address);
    say("mem-ascending", ok && b.address > a.address, "");
    if (!ok || sel <= 0)
        return;
    __dpmi_set_segment_base_address(sel, a.address);
    __dpmi_set_segment_limit(sel, 0xFFFF);
    _farpokel(sel, 0, 0xA5A5A5A5);
    _farpokel(sel, 0xFFFC, 0x5A5A5A5A);
    c = a;
    c.size = 4 * 1024 * 1024;
    ok = __dpmi_resize_memory(&c) == 0;
    if (ok) {
        __dpmi_set_segment_base_address(sel, c.address);
        __dpmi_set_segment_limit(sel, c.size - 1);
        ok = _farpeekl(sel, 0) == 0xA5A5A5A5 && _farpeekl(sel, 0xFFFC) == 0x5A5A5A5A;
        _farpokel(sel, c.size - 4, 0x01020304);                 /* the new end is there */
        ok = ok && _farpeekl(sel, c.size - 4) == 0x01020304;
    }
    say("mem-resize", ok, "from=%08lx to=%08lx", a.address, c.address);
    say("mem-free", __dpmi_free_memory(ok ? c.handle : a.handle) == 0 && __dpmi_free_memory(b.handle) == 0, "");

    ok = __dpmi_get_free_memory_information(&fi) == 0;
    a.size = fi.largest_available_free_block_in_bytes / 2;      /* of real memory: a host may count swap */
    if (a.size > fi.total_number_of_free_pages * 4096ul / 2)
        a.size = fi.total_number_of_free_pages * 4096ul / 2;
    if (a.size > 32ul << 20)
        a.size = 32ul << 20;
    ok = ok && __dpmi_allocate_memory(&a) == 0;
    if (ok) {                                                   /* touch every page */
        __dpmi_set_segment_base_address(sel, a.address);
        __dpmi_set_segment_limit(sel, (a.size - 1) | 0xFFF);
        for (i = 0; i < a.size; i += 4096)
            _farpokel(sel, i, i);
        for (i = 0; i < a.size && ok; i += 4096)
            ok = _farpeekl(sel, i) == i;
        __dpmi_free_memory(a.handle);
    }
    say("mem-half-of-largest", ok, "bytes=%lu", a.size);
    __dpmi_free_ldt_descriptor(sel);

    a.address = 0xE0000000ul;                                   /* above 1 MB, where 0800h is for (PCI BARs) */
    a.size = 0x10000;
    say("phys-map", __dpmi_physical_address_mapping(&a) == 0, "linear=%08lx", a.address);
}

/* ---- the rest */

static void t_misc(void)
{
    __dpmi_version_ret v;
    __dpmi_raddr rm;
    __dpmi_paddr pm;
    __dpmi_meminfo m;
    unsigned long page = 0;
    int was, now, sz;

    __dpmi_get_version(&v);
    say("version", v.major == 0 && v.minor == 90, "%d.%02d flags=%x cpu=%d pic=%02x/%02x", v.major, v.minor,
        v.flags, v.cpu, v.master_pic, v.slave_pic);
    say("pic-bases", v.master_pic == 0x08 && v.slave_pic == 0x70, "");
    was = __dpmi_get_and_disable_virtual_interrupt_state();
    now = __dpmi_get_virtual_interrupt_state();
    __dpmi_get_and_enable_virtual_interrupt_state();
    say("vif", was == 1 && now == 0 && __dpmi_get_virtual_interrupt_state() == 1, "was=%d off=%d", was, now);
    say("page-size", __dpmi_get_page_size(&page) == 0 && page == 4096, "page=%lu", page);
    m.address = __djgpp_base_address + (unsigned long)&fails;
    m.size = 4;
    say("lock", __dpmi_lock_linear_region(&m) == 0 && __dpmi_unlock_linear_region(&m) == 0, "");
    say("raw-switch", __dpmi_get_raw_mode_switch_addr(&rm, &pm) == 0 && rm.segment && pm.selector,
        "rm=%04x:%04x pm=%04x:%08lx", rm.segment, rm.offset16, pm.selector, pm.offset32);
    sz = __dpmi_get_state_save_restore_addr(&rm, &pm);
    say("state-save", -1, "bytes=%d rm=%04x:%04x pm=%04x:%08lx", sz, rm.segment, rm.offset16, pm.selector,
        pm.offset32);
    say("vendor-other", __dpmi_get_vendor_specific_api_entry_point("RATIONAL DOS/4G", &pm) != 0, "");
    say("fpu", -1, "status=%04x", __dpmi_get_coprocessor_status());
    if (__djgpp_nearptr_enable()) {                             /* a 4 GB DS (0008h), checked with LSL */
        unsigned long t = *(volatile unsigned long *)(0x46C + __djgpp_conventional_base);
        say("nearptr", t == _farpeekl(_dos_ds, 0x46C) || t + 1 == _farpeekl(_dos_ds, 0x46C), "tick=%lu", t);
        __djgpp_nearptr_disable();
    } else {
        say("nearptr", 0, "refused");
    }
}


/* ---- M4b: exceptions (dpmiconf_h.S has the handlers) */

extern unsigned short hd_ds;
extern unsigned long old8[2], old1c[2], old75[2], irq8_mode;
extern volatile unsigned long irq8_hits, irq8_vif, irq8_ss, irq8_base, i1c_hits, irq13_hits, mf_hits;
extern volatile unsigned long exc_hits, exc_err, exc_eip, exc_cs, exc_flags, exc_esp, exc_ss, exc_hss;
extern unsigned long exc_skip, exc_ebx, exc_new_eip, exc_new_esp, exc_esp_before;
extern volatile unsigned long x10_hits, x10_eip, x10_cs, x10_info, x10_flags, x10_es, x10_ds, x10_cr2, x10_pte;
extern volatile unsigned long rmcb_hits, rmcb_eax, rmcb_sp, rmcb_ss_base;
extern char exc_handler[], x10_handler[], gp_insn[], pf_insn[], exc_resume[];
extern char irq8_handler[], i1c_handler[], irq13_handler[], mf_handler[], rmcb_far[], rmcb_int[];
unsigned fault_gp(unsigned ebx);
unsigned fault_pf(unsigned sel, unsigned off);
unsigned fault_resume(void);
void fpu_divzero(void);

static unsigned long alt_stack[1024];

static void set_exc(int n, void *h, __dpmi_paddr *old)
{
    __dpmi_paddr p;
    if (old)
        __dpmi_get_processor_exception_handler_vector(n, old);
    p.selector = _my_cs();
    p.offset32 = (unsigned long)h;
    __dpmi_set_processor_exception_handler_vector(n, &p);
}

static void t_exc(void)
{
    __dpmi_paddr old13, old14, p;
    __dpmi_meminfo m;
    unsigned ebx, esp;
    unsigned short attr[3] = { 0, 0, 0 };
    int sel, ok;

    set_exc(13, exc_handler, &old13);
    exc_hits = 0;
    exc_skip = 3;
    exc_ebx = 0x13579BDF;
    ebx = fault_gp(0);
    say("exc-gp", exc_hits == 1 && exc_eip == (unsigned long)gp_insn && exc_cs == _my_cs(),
        "hits=%lu eip=%lx(%lx) cs=%lx", exc_hits, exc_eip, (unsigned long)gp_insn, exc_cs);
    say("exc-gp-err", -1, "err=%lx (the CPU's: 0; HDPMI32i gives 2E00h)", exc_err);
    say("exc-frame-stack", exc_ss == _my_ds() && exc_esp == exc_esp_before && (exc_flags & 0x200),
        "ss=%lx esp=%lx(%lx) flags=%lx", exc_ss, exc_esp, exc_esp_before, exc_flags);
    say("exc-regs-kept", ebx == 0x13579BDF, "ebx=%x", ebx);
    say("exc-host-stack", -1, "ss=%lx (ours %x)", exc_hss, _my_ds());
    exc_skip = 0;                                               /* the frame's CS:EIP and SS:ESP edited, as DJGPP does */
    exc_new_eip = (unsigned long)exc_resume;
    exc_new_esp = (unsigned long)&alt_stack[1024];
    esp = fault_resume();
    exc_new_eip = exc_new_esp = 0;
    say("exc-frame-edit", esp == (unsigned long)&alt_stack[1024] && exc_hits == 2, "esp=%x want=%lx", esp,
        (unsigned long)&alt_stack[1024]);
    __dpmi_set_processor_exception_handler_vector(13, &old13);

    /* 0507h/0506h: page 1 of a 3-page block uncommitted, a #PF there, back */
    m.size = 3 * 4096;
    sel = __dpmi_allocate_ldt_descriptors(1);
    if (sel <= 0 || __dpmi_allocate_memory(&m) != 0) {
        say("page-attr", 0, "no memory");
        return;
    }
    __dpmi_set_segment_base_address(sel, m.address);
    __dpmi_set_segment_limit(sel, 3 * 4096 - 1);
    _farpokel(sel, 0x1000, 0x55AA55AA);
    {
        __dpmi_meminfo a = m;
        unsigned short none = 0;
        a.address = 0x1000;                                     /* the offset in the block */
        a.size = 1;
        ok = __dpmi_set_page_attributes(&a, (short *)&none) == 0;
        a.address = 0;
        a.size = 3;
        ok = ok && __dpmi_get_page_attributes(&a, (short *)attr) == 0;
    }
    say("page-attr", ok && (attr[0] & 7) == 1 && (attr[1] & 7) == 0 && (attr[0] & 8),
        "attr=%04x %04x %04x", attr[0], attr[1], attr[2]);
    if (ok) {
        unsigned v;
        set_exc(14, exc_handler, &old14);
        exc_hits = 0;
        exc_skip = 3;
        v = fault_pf(sel, 0x1000);
        __dpmi_set_processor_exception_handler_vector(14, &old14);
        say("exc-pf", exc_hits == 1 && exc_eip == (unsigned long)pf_insn && (exc_err & 5) == 4 && v == 0,
            "hits=%lu err=%lx eip=%lx v=%x", exc_hits, exc_err, exc_eip, v);
        if (__dpmi_get_extended_exception_handler_vector_pm(14, &p) == 0) {
            __dpmi_paddr x;
            x.selector = _my_cs();
            x.offset32 = (unsigned long)x10_handler;
            if (__dpmi_set_extended_exception_handler_vector_pm(14, &x) == 0) {
                x10_hits = 0;
                v = fault_pf(sel, 0x1234);
                __dpmi_set_extended_exception_handler_vector_pm(14, &p);
                say("exc-frame10", x10_hits == 1 && x10_eip == (unsigned long)pf_insn && x10_cs == _my_cs()
                    && x10_cr2 == m.address + 0x1234 && x10_ds == _my_ds(),
                    "eip=%lx cs=%lx cr2=%lx(%lx) ds=%lx es=%lx info=%lx pte=%lx", x10_eip, x10_cs, x10_cr2,
                    m.address + 0x1234, x10_ds, x10_es, x10_info, x10_pte);
            } else {
                say("exc-frame10", -1, "0212h refused");
            }
        } else {
            say("exc-frame10", -1, "0210h refused");
        }
        {
            __dpmi_meminfo a = m;
            unsigned short rw = 9;                              /* committed, writable */
            a.address = 0x1000;
            a.size = 1;
            ok = __dpmi_set_page_attributes(&a, (short *)&rw) == 0;
            say("page-recommit-content", -1, "v=%lx", ok ? _farpeekl(sel, 0x1000) : 0);   /* hosts keep it, or not */
            if (ok)
                _farpokel(sel, 0x1004, 0xC0FFEE);
            say("page-recommit", ok && _farpeekl(sel, 0x1004) == 0xC0FFEE, "");
        }
    }
    __dpmi_free_memory(m.handle);
    __dpmi_free_ldt_descriptor(sel);
}

/* ---- M4b: IRQs and INTs passed up */

static unsigned long ticks(void) { return _farpeekl(_dos_ds, 0x46C); }

/* Until the BIOS tick moves n times, or a long count passes (a host that
   loses the tick must not hang the run). */
static int wait_ticks(int n)
{
    unsigned long t = ticks(), k;
    for (k = 0; k < 400000000ul; k++)
        if (ticks() - t >= (unsigned long)n)
            return 1;
    return 0;
}

static void hook(int vec, void *h, unsigned long *old, __dpmi_paddr *save)
{
    __dpmi_paddr p;
    __dpmi_get_protected_mode_interrupt_vector(vec, save);
    old[0] = save->offset32;
    old[1] = save->selector;
    p.selector = _my_cs();
    p.offset32 = (unsigned long)h;
    __dpmi_set_protected_mode_interrupt_vector(vec, &p);
}

static void t_irq(void)
{
    __dpmi_paddr s8, s1c, s75, old16;
    __dpmi_regs r;
    unsigned long t0, h0;
    int ok;
    static const unsigned char wait3[] = {                      /* real mode: STI, wait 3 BIOS ticks, RETF */
        0xFB, 0x1E, 0x31, 0xC0, 0x8E, 0xD8, 0x8B, 0x1E, 0x6C, 0x04, 0x83, 0xC3, 0x03,
        0xA1, 0x6C, 0x04, 0x29, 0xD8, 0x78, 0xF9, 0x1F, 0xCB,
    };

    irq8_mode = 0;
    irq8_hits = 0;
    hook(8, irq8_handler, old8, &s8);
    t0 = ticks();
    ok = wait_ticks(5);
    say("irq-pm", ok && irq8_hits >= 5, "hits=%lu ticks=%lu", irq8_hits, ticks() - t0);
    say("irq-pm-int31", irq8_vif == 0 && irq8_base == __djgpp_base_address, "vif=%lu base=%lx", irq8_vif,
        irq8_base);
    say("irq-pm-vif-after", __dpmi_get_virtual_interrupt_state() == 1, "");
    say("irq-pm-stack", -1, "ss=%lx (ours %x)", irq8_ss, _my_ds());
    if (dos_seg > 0) {                                          /* while the program is in real mode */
        movedata(_my_ds(), (unsigned)wait3, dos_sel, 0x100, sizeof wait3);
        memset(&r, 0, sizeof r);
        r.x.cs = dos_seg;
        r.x.ip = 0x100;
        h0 = irq8_hits;
        ok = __dpmi_simulate_real_mode_procedure_retf(&r) == 0;
        say("irq-pm-from-rm", ok && irq8_hits - h0 >= 2, "hits=%lu", irq8_hits - h0);
    }
    irq8_mode = 1;                                              /* EOI and IRET, no STI: the IF comes back anyway */
    h0 = irq8_hits;
    t0 = 0;
    while (irq8_hits - h0 < 3 && ++t0 < 400000000ul) ;
    irq8_mode = 0;
    say("irq-iret-vif", irq8_hits - h0 >= 3 && __dpmi_get_virtual_interrupt_state() == 1, "hits=%lu vif=%d",
        irq8_hits - h0, __dpmi_get_virtual_interrupt_state());
    __dpmi_set_protected_mode_interrupt_vector(8, &s8);

    i1c_hits = 0;                                               /* the BIOS's INT 1Ch, passed up */
    hook(0x1C, i1c_handler, old1c, &s1c);
    ok = wait_ticks(4);
    __dpmi_set_protected_mode_interrupt_vector(0x1C, &s1c);
    say("int1c-passup", ok && i1c_hits >= 3, "hits=%lu", i1c_hits);

    irq13_hits = mf_hits = 0;                                   /* an FPU error: IRQ13 (NE=0) or #MF (NE=1) */
    hook(0x75, irq13_handler, old75, &s75);
    set_exc(16, mf_handler, &old16);
    fpu_divzero();
    __dpmi_set_processor_exception_handler_vector(16, &old16);
    __dpmi_set_protected_mode_interrupt_vector(0x75, &s75);
    say("fpu-error", irq13_hits + mf_hits == 1, "irq13=%lu mf=%lu", irq13_hits, mf_hits);
}

/* ---- M4b: real-mode callbacks */

static void t_rmcb(void)
{
    __dpmi_regs cbr, r;
    __dpmi_raddr cb, old66;
    unsigned long base = 0;
    int ok;

    memset(&cbr, 0, sizeof cbr);
    rmcb_hits = 0;
    ok = __dpmi_allocate_real_mode_callback((void (*)(void))rmcb_far, &cbr, &cb) == 0;
    say("rmcb-alloc", ok, "at=%04x:%04x", cb.segment, cb.offset16);
    if (!ok)
        return;
    memset(&r, 0, sizeof r);
    r.x.cs = cb.segment;
    r.x.ip = cb.offset16;
    r.d.eax = 0x12345678;
    ok = __dpmi_simulate_real_mode_procedure_retf(&r) == 0;
    say("rmcb-call", ok && rmcb_hits == 1 && rmcb_eax == 0x12345678 && r.x.bx == 0xBEEF, "hits=%lu eax=%lx bx=%04x",
        rmcb_hits, rmcb_eax, r.x.bx);
    __dpmi_get_segment_base_address(rmcb_ss_base, &base);
    say("rmcb-stack", base / 16 == r.x.ss || r.x.ss == 0, "dsbase=%lx sp=%lx", base, rmcb_sp);
    __dpmi_free_real_mode_callback(&cb);

    memset(&cbr, 0, sizeof cbr);                                /* through the IVT, returning as an IRET */
    rmcb_hits = 0;
    if (__dpmi_allocate_real_mode_callback((void (*)(void))rmcb_int, &cbr, &cb) == 0) {
        __dpmi_get_real_mode_interrupt_vector(0x66, &old66);
        __dpmi_set_real_mode_interrupt_vector(0x66, &cb);
        memset(&r, 0, sizeof r);
        ok = __dpmi_simulate_real_mode_interrupt(0x66, &r) == 0;
        say("rmcb-int", ok && rmcb_hits == 1 && r.x.cx == 0xCAFE, "hits=%lu cx=%04x", rmcb_hits, r.x.cx);
        if (is_glos) {                                          /* §14.5: a PM INT 8 hook over an IVT pointing at our RMCB */
            __dpmi_paddr s8;
            __dpmi_raddr old8r;
            unsigned long t0;
            irq8_mode = 0;
            irq8_hits = 0;
            rmcb_hits = 0;
            hook(8, irq8_handler, old8, &s8);
            __dpmi_get_real_mode_interrupt_vector(8, &old8r);
            __dpmi_set_real_mode_interrupt_vector(8, &cb);
            t0 = ticks();
            ok = wait_ticks(3);
            __dpmi_set_real_mode_interrupt_vector(8, &old8r);
            __dpmi_set_protected_mode_interrupt_vector(8, &s8);
            say("glos-passup-guard", ok && irq8_hits >= 3 && rmcb_hits == 0, "hits=%lu rmcb=%lu ticks=%lu",
                irq8_hits, rmcb_hits, ticks() - t0);
        }
        __dpmi_set_real_mode_interrupt_vector(0x66, &old66);
        __dpmi_free_real_mode_callback(&cb);
    } else {
        say("rmcb-int", 0, "no second callback");
    }
}

/* ---- M4c: a child client (this program again, "child ...") in the same context */

static volatile int hits61;
static void pm61(void) { hits61++; }

/* The child's part: take what it can and leave without giving it back. */
static int child(const char *how)
{
    __dpmi_meminfo m;
    __dpmi_paddr p;
    _go32_dpmi_seginfo w;
    __dpmi_allocate_ldt_descriptors(8);
    m.size = 1024 * 1024;
    __dpmi_allocate_memory(&m);
    w.pm_offset = (unsigned long)pm61;
    w.pm_selector = _my_cs();
    if (_go32_dpmi_allocate_iret_wrapper(&w) == 0) {
        p.offset32 = w.pm_offset;
        p.selector = w.pm_selector;
        __dpmi_set_protected_mode_interrupt_vector(0x61, &p);
    }
    if (!strcmp(how, "fault"))
        *(volatile int *)0 = 0;                 /* DJGPP's own SIGSEGV: exit 255 */
    _exit(42);
}

static void t_nest(const char *self)
{
    __dpmi_free_mem_info a, b;
    __dpmi_paddr old61, now, mine;
    _go32_dpmi_seginfo w;
    int code, sel, sel2, ok;

    __dpmi_get_protected_mode_interrupt_vector(0x61, &old61);
    w.pm_offset = (unsigned long)pm61;
    w.pm_selector = _my_cs();
    if (_go32_dpmi_allocate_iret_wrapper(&w) != 0) {
        say("nest", 0, "no wrapper");
        return;
    }
    mine.offset32 = w.pm_offset;
    mine.selector = w.pm_selector;
    __dpmi_set_protected_mode_interrupt_vector(0x61, &mine);
    sel = __dpmi_allocate_ldt_descriptors(1);
    __dpmi_set_segment_base_address(sel, 0x12340);
    __dpmi_get_free_memory_information(&a);

    code = spawnl(P_WAIT, self, self, "child", "leave", NULL);
    __dpmi_get_free_memory_information(&b);
    __dpmi_get_protected_mode_interrupt_vector(0x61, &now);
    say("nest-child-exit", code == 42, "code=%d", code);
    say("nest-vector-back", is_glos ? now.selector == mine.selector && now.offset32 == mine.offset32 : -1,
        "now=%04x:%08lx (CWSDPMI keeps the child's hook, into code that is gone)", now.selector, now.offset32);
    say("nest-memory-back", b.total_number_of_free_pages + 16 >= a.total_number_of_free_pages,
        "free pages before=%lu after=%lu", a.total_number_of_free_pages, b.total_number_of_free_pages);
    {
        unsigned long base = 0;
        __dpmi_get_segment_base_address(sel, &base);
        sel2 = __dpmi_allocate_ldt_descriptors(1);
        say("nest-selectors-kept", base == 0x12340 && sel2 > 0, "base=%lx", base);
        if (sel2 > 0)
            __dpmi_free_ldt_descriptor(sel2);
    }
    if (now.selector != mine.selector || now.offset32 != mine.offset32) {
        __dpmi_set_protected_mode_interrupt_vector(0x61, &mine);   /* (the child's is code that is gone) */
        say("nest-hook-works", -1, "skipped: the host kept the child's hook");
    } else {
        hits61 = 0;
        __asm__ volatile("int $0x61" ::: "memory");
        say("nest-hook-works", hits61 == 1, "hits=%d", hits61);
    }

    code = spawnl(P_WAIT, self, self, "child", "fault", NULL);
    __dpmi_get_protected_mode_interrupt_vector(0x61, &now);
    hits61 = 0;
    if (now.selector == mine.selector && now.offset32 == mine.offset32)
        __asm__ volatile("int $0x61" ::: "memory");
    ok = !is_glos || (now.selector == mine.selector && now.offset32 == mine.offset32 && hits61 == 1);
    say("nest-child-fault", code == 255 && ok, "code=%d hits=%d", code, hits61);

    __dpmi_set_protected_mode_interrupt_vector(0x61, &old61);
    _go32_dpmi_free_iret_wrapper(&w);
    __dpmi_free_ldt_descriptor(sel);
}

/* ---- M4c: DPMI 1.0 extras (0401h, 0508h/0509h, 0B00h-0B03h), INT 2Fh */

__attribute__((noinline)) static int watch_target(int x) { return x + 1; }
extern unsigned long exc_or_flags;

static void t_extras(void)
{
    unsigned char caps[128];
    __dpmi_meminfo m;
    __dpmi_paddr old1;
    int sel, cap, i, ok;
    unsigned short attr[2] = { 0, 0 };

    memset(caps, 0, sizeof caps);
    cap = __dpmi_get_capabilities(&i, (char *)caps);
    say("caps", cap == -1 ? -1 : (caps[2] != 0), "caps=%x host=%d.%d vendor=%.20s", i, caps[0], caps[1], caps + 2);

    m.size = 2 * 4096;
    sel = __dpmi_allocate_ldt_descriptors(1);
    if (sel > 0 && __dpmi_allocate_memory(&m) == 0) {
        __dpmi_set_segment_base_address(sel, m.address);
        __dpmi_set_segment_limit(sel, 2 * 4096 - 1);
        /* 0509h: page 1 shows the text screen */
        __asm__ volatile("int $0x31; sbbl %0, %0" : "=a"(ok) : "a"(0x0509), "S"(m.handle), "b"(0x1000), "c"(1),
                         "d"(0xB8000) : "memory", "cc");
        if (ok == 0) {
            _farpokew(_dos_ds, 0xB8000 + 158, 0x1F41);
            ok = _farpeekw(sel, 0x1000 + 158) == 0x1F41;
            {
                __dpmi_meminfo a = m;
                a.address = 0;
                a.size = 2;
                __dpmi_get_page_attributes(&a, (short *)attr);
            }
            say("map-dos", ok, "attr=%04x %04x", attr[0], attr[1]);
            say("map-dos-type", (attr[1] & 7) == 2, "page 1 type=%d (2: mapped)", attr[1] & 7);
        } else {
            say("map-dos", -1, "0509h refused");
        }
        __asm__ volatile("int $0x31; sbbl %0, %0" : "=a"(ok) : "a"(0x0508), "S"(m.handle), "b"(0), "c"(1),
                         "d"(0xFF000) : "memory", "cc");
        if (ok == 0)                                            /* 0508h: page 0 is the BIOS's last page */
            say("map-device", _farpeekl(sel, 0xFF0) == _farpeekl(_dos_ds, 0xFFFF0) && _farpeekb(sel, 0xFF0) == 0xEA,
                "reset vector %08lx", _farpeekl(sel, 0xFF0));
        else
            say("map-device", -1, "0508h refused");
        __dpmi_free_memory(m.handle);
        __dpmi_free_ldt_descriptor(sel);
    }

    /* an execute watchpoint: exception 1 at the function's first byte, the
       handler sets RF to run it, and 0B02h says it fired. INFO only: 86Box
       fires no DR0-DR3 breakpoint on these profiles, under any host (with or
       without the dynarec; supervisor.md §20), so silicon decides. */
    {
        unsigned long lin = __djgpp_base_address + (unsigned long)watch_target;
        int h = -1, v;
        __asm__ volatile("int $0x31; jc 1f; movzwl %%bx, %0; 1:" : "+r"(h) : "a"(0x0B00), "b"(lin >> 16),
                         "c"(lin & 0xFFFF), "d"(0x0001) : "memory", "cc");
        if (h < 0) {
            say("watch", -1, "0B00h refused");
        } else {
            int state = 0;
            set_exc(1, exc_handler, &old1);
            exc_hits = 0;
            exc_skip = 0;
            exc_or_flags = 0x10000;
            v = watch_target(41);
            exc_or_flags = 0;
            __dpmi_set_processor_exception_handler_vector(1, &old1);
            __asm__ volatile("int $0x31" : "=a"(state) : "a"(0x0B02), "b"(h) : "memory", "cc");
            __asm__ volatile("int $0x31" :: "a"(0x0B01), "b"(h) : "memory", "cc");
            say("watch", -1, "hits=%lu eip=%lx(%lx) v=%d state=%x (86Box fires no DR breakpoints: §20)", exc_hits,
                exc_eip, (unsigned long)watch_target, v, state & 0xFFFF);
        }
    }
    {
        unsigned short ax;
        __asm__ volatile("int $0x2f" : "=a"(ax) : "a"(0x1680) : "memory");
        /* a real yield, but "not supported" as on plain DOS (supervisor.md §13) */
        say("glos-yield", is_glos ? (ax & 0xFF) == 0x80 : -1, "al=%02x", ax & 0xFF);
    }
}

int main(int argc, char **argv)
{
    __dpmi_paddr g;
    if (argc > 2 && !strcmp(argv[1], "child"))
        return child(argv[2]);
    ser("HX-TEST dpmi-start INFO\r\n");
    hd_ds = __djgpp_ds_alias;
    is_glos = __dpmi_get_vendor_specific_api_entry_point("GLOS", &g) == 0;
    t_misc();
    t_ldt();
    t_dos();
    t_rm();
    t_mem();
    t_exc();
    t_irq();
    t_rmcb();
    t_extras();
    t_nest(argv[0]);
    if (dos_seg > 0)
        __dpmi_free_dos_memory(dos_sel);
    {
        char line[64];
        snprintf(line, sizeof line, "HX-TEST dpmi-end fails=%d\r\n", fails);
        ser(line);
    }
    return fails ? 1 : 0;
}
