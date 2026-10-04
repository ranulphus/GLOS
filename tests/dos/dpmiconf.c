/* DPMICONF-32: conformance checks for a DPMI host's services to a 32-bit
 * client (GLOS M4a, docs/milestones-m0-m4.md), as a DJGPP program. Each
 * check prints
 *     HX-TEST dpmi-<name> ok|FAIL [detail]
 * on COM1 (or INFO for what hosts may choose), and the run ends with
 *     HX-TEST dpmi-end fails=<n>
 * and exit code 0, or 1 if anything failed. A check must pass on CWSDPMI r7
 * and HDPMI32i before it may judge GLOS: one that fails on both is wrong. */
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

static int fails;

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

int main(void)
{
    ser("HX-TEST dpmi-start INFO\r\n");
    t_misc();
    t_ldt();
    t_dos();
    t_rm();
    t_mem();
    if (dos_seg > 0)
        __dpmi_free_dos_memory(dos_sel);
    {
        char line[64];
        snprintf(line, sizeof line, "HX-TEST dpmi-end fails=%d\r\n", fails);
        ser(line);
    }
    return fails ? 1 : 0;
}
