/* GLOS.EXE - the loader (docs/supervisor.md §2). 16-bit real mode, Open
 * Watcom small model. Checks the machine, finds the memory GLOS may use
 * (raw: INT 15h E820h/E801h/88h; XMS: locked blocks), reads GLOSK.BIN, builds
 * the first page tables and enters the kernel; when the kernel returns, it
 * restores A20, frees what it took and exits with the kernel's result.
 *     GLOS [/ROUNDTRIP] [/GDB] [/RUN program [args...]]
 * /RUN keeps DOS running in the kernel's system VM (V86 mode), runs the
 * program there and leaves when it ends, with its exit code.
 * Progress goes to COM1 as GLOS-BOOT lines; refusals as GLOS-REFUSE. */
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <process.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "glos/bootinfo.h"

#ifndef GLOS_BUILD
#define GLOS_BUILD "unknown"
#endif

extern int __cdecl cpu_is486(void);
extern int __cdecl cpu_v86(void);
extern int __cdecl cpu_id1(unsigned long *eax, unsigned long *edx);
extern int __cdecl bios_e820(void *buf20, unsigned long *cont);
extern unsigned long __cdecl pm_enter(unsigned gdtr, unsigned long cr3, unsigned long src, unsigned long dst,
                                      unsigned long file_dwords, unsigned long bss_dwords,
                                      unsigned long entry, unsigned long arg);
extern void __cdecl pm_ret(void);
extern unsigned long __cdecl glos_call(unsigned fn, unsigned long arg);
/* Labels in the code segment: declared as functions so that their offsets
   are taken against it, not DGROUP. */
extern void __cdecl vm_resume(void);
extern void __cdecl vm_state(void);
extern void __cdecl glos_bp_call(void);
extern void __cdecl glos_bp_xms(void);
extern void __cdecl glos_kill(void);
extern void __cdecl glos_kill_top(void);

/* ---- COM1 */

static void put(char c)
{
    long spin = 0;
    while (!(inp(0x3FD) & 0x20) && ++spin < 100000L) ;
    outp(0x3F8, c);
}

static void say(const char *fmt, ...)
{
    char b[160];
    const char *p;
    va_list ap;
    va_start(ap, fmt);
    vsprintf(b, fmt, ap);
    va_end(ap);
    for (p = b; *p; p++) put(*p);
    put('\r'); put('\n');
}

static int refuse(const char *why)
{
    say("GLOS-REFUSE reason=%s", why);
    printf("GLOS: cannot start: %s\n", why);
    return 2;
}

/* ---- XMS */

static void (far *xms)(void);
static unsigned xms_ax, xms_bx, xms_dx;

static void xms_call(unsigned fn, unsigned arg)
{
    unsigned a, b, d;
    _asm {
        mov ax, fn
        mov dx, arg
        call dword ptr xms
        mov a, ax
        mov b, bx
        mov d, dx
    }
    xms_ax = a; xms_bx = b; xms_dx = d;
}

/* ---- A20 */

static int a20_on(void)
{
    unsigned char far *lo = (unsigned char far *)MK_FP(0x0000, 0x0500);
    unsigned char far *hi = (unsigned char far *)MK_FP(0xFFFF, 0x0510);
    unsigned char sl = *lo, sh = *hi;
    int on;
    *lo = 0x5A;
    *hi = 0xA5;
    on = *lo == 0x5A;
    *hi = sh;
    *lo = sl;
    return on;
}

static void kbc_wait(void)
{
    long spin = 0;
    while ((inp(0x64) & 2) && ++spin < 100000L) ;
}

static void a20_set(int on)
{
    unsigned char p92 = (unsigned char)inp(0x92);
    _disable();
    outp(0x92, on ? ((p92 | 2) & ~1) : (p92 & ~3));     /* fast A20, never the reset bit */
    if (a20_on() != on) {
        kbc_wait(); outp(0x64, 0xD1);
        kbc_wait(); outp(0x60, on ? 0xDF : 0xDD);
        kbc_wait();
    }
    _enable();
}

/* ---- memory */

static struct bootinfo bi;
static unsigned xms_handles[4], n_xms;

static void add_range(unsigned long base, unsigned long len, unsigned long type)
{
    if (bi.n_ranges < BI_MEM_RANGES && len) {
        bi.range[bi.n_ranges].base = base;
        bi.range[bi.n_ranges].length = len;
        bi.range[bi.n_ranges].type = type;
        bi.n_ranges++;
    }
}

/* Raw mode: free memory above 1 MB from E820h, else E801h, else 88h. */
static int raw_memory(void)
{
    struct { unsigned long base_lo, base_hi, len_lo, len_hi, type; } e;
    unsigned long cont = 0;
    union REGS r;
    int n = 0;
    do {
        memset(&e, 0, sizeof e);
        if (!bios_e820(&e, &cont))
            break;
        n++;
        if (e.type == 1 && !e.base_hi && e.base_lo >= 0x100000UL)
            add_range(e.base_lo, e.len_hi ? 0xFFFFF000UL - e.base_lo : e.len_lo, BI_MEM_FREE);
    } while (cont && n < 64);
    if (n) {
        say("GLOS-BOOT step=memory mode=raw source=e820 entries=%d", n);
        return 0;
    }
    r.x.ax = 0xE801;
    int86(0x15, &r, &r);
    if (!r.x.cflag && (r.x.ax || r.x.cx)) {
        unsigned long below16 = r.x.ax ? r.x.ax : r.x.cx, above16 = r.x.ax ? r.x.bx : r.x.dx;
        add_range(0x100000UL, below16 * 1024UL, BI_MEM_FREE);
        if (above16) add_range(0x1000000UL, above16 * 65536UL, BI_MEM_FREE);
        say("GLOS-BOOT step=memory mode=raw source=e801");
        return 0;
    }
    r.h.ah = 0x88;
    int86(0x15, &r, &r);
    if (!r.x.cflag && r.x.ax) {
        add_range(0x100000UL, r.x.ax * 1024UL, BI_MEM_FREE);
        say("GLOS-BOOT step=memory mode=raw source=88");
        return 0;
    }
    return -1;
}

/* XMS mode: a locked block for the kernel, and one for GLOS's own memory. */
static int xms_block(unsigned kb, unsigned long *phys)
{
    xms_call(0x0900, kb);
    if (xms_ax != 1) return -1;
    xms_handles[n_xms++] = xms_dx;
    xms_call(0x0C00, xms_dx);
    if (xms_ax != 1) return -1;
    *phys = ((unsigned long)xms_dx << 16) | xms_bx;
    return 0;
}

static void xms_release(void)
{
    while (n_xms) {
        unsigned h = xms_handles[--n_xms];
        xms_call(0x0D00, h);
        xms_call(0x0A00, h);
    }
}

/* ---- the GDT the kernel starts with (supervisor.md §3.1: 08h, 10h, 38h, 40h) */

static unsigned char gdt[8 * 9];
static struct { unsigned short limit; unsigned long base; } gdtr;

static void set_desc(int sel, unsigned long base, unsigned long limit, unsigned char access, unsigned char flags)
{
    unsigned char *e = gdt + sel;
    e[0] = (unsigned char)limit; e[1] = (unsigned char)(limit >> 8);
    e[2] = (unsigned char)base; e[3] = (unsigned char)(base >> 8); e[4] = (unsigned char)(base >> 16);
    e[5] = access;
    e[6] = (unsigned char)(((limit >> 16) & 0x0F) | (flags & 0xF0));
    e[7] = (unsigned char)(base >> 24);
}

static void far *phys(unsigned long p) { return MK_FP((unsigned)(p >> 4), (unsigned)(p & 15)); }
static void poke32(unsigned long p, unsigned long v) { *(unsigned long far *)phys(p) = v; }

/* GLOSK.BIN next to GLOS.EXE */
static void kernel_path(char *out, const char *argv0)
{
    const char *slash = strrchr(argv0, '\\');
    size_t n = slash ? (size_t)(slash - argv0 + 1) : 0;
    memcpy(out, argv0, n);
    strcpy(out + n, "GLOSK.BIN");
}

int main(int argc, char **argv)
{
    struct SREGS sr;
    union REGS r;
    struct { unsigned long magic, entry, file_size, total; } hdr;
    unsigned long eax_, edx_, buf_phys, tab_phys, a, ret;
    unsigned buf_seg = 0, tab_seg = 0, i, pages;
    char path[128];
    FILE *f;
    int xms_mode, run_at = 0;

    outp(0x3FB, 0x80); outp(0x3F8, 1); outp(0x3F9, 0); outp(0x3FB, 0x03);
    outp(0x3FA, 0xC7); outp(0x3FC, 0x03);
    say("GLOS-BOOT step=start build=" GLOS_BUILD);
    for (i = 1; i < (unsigned)argc; i++) {
        if (!stricmp(argv[i], "/ROUNDTRIP")) bi.flags |= BI_F_ROUNDTRIP;
        else if (!stricmp(argv[i], "/GDB")) bi.flags |= BI_F_GDB;
        else if (!stricmp(argv[i], "/RUN") && i + 1 < (unsigned)argc) {
            bi.flags |= BI_F_VM;
            run_at = (int)i + 1;
            break;
        }
    }

    /* Refusals (supervisor.md §2.1). */
    if (!cpu_is486()) return refuse("cpu");
    if (cpu_v86()) return refuse("v86");
    r.x.ax = 0x1687; int86(0x2F, &r, &r);
    if (r.x.ax == 0) return refuse("dpmi");
    r.x.ax = 0x1600; int86(0x2F, &r, &r);
    if (r.h.al != 0x00 && r.h.al != 0x80) return refuse("windows");
    {
        void far * far *ivt = (void far * far *)MK_FP(0, 0);
        char far *emm = (char far *)MK_FP(FP_SEG(ivt[0x67]), 0x0A);
        if (FP_SEG(ivt[0x67]) && !_fmemcmp(emm, "EMMXXXX0", 8)) {
            r.x.ax = 0xDE00; int86(0x67, &r, &r);
            if (r.h.ah == 0) return refuse("vcpi");
        }
    }
    bi.cpu_family = 4;
    if (cpu_id1(&eax_, &edx_)) {
        bi.cpu_family = (eax_ >> 8) & 15;
        bi.cpuid_edx = edx_;
    }
    r.h.ah = 0x0F; int86(0x10, &r, &r);
    bi.video_mode = r.h.al & 0x7F;
    bi.pic_mask = inp(0x21) | ((unsigned long)inp(0xA1) << 8);
    say("GLOS-BOOT step=checks cpu=%lu cpuid_edx=%08lx", bi.cpu_family, bi.cpuid_edx);

    /* The kernel image. */
    kernel_path(path, argv[0]);
    f = fopen(path, "rb");
    if (!f || fread(&hdr, sizeof hdr, 1, f) != 1 || hdr.magic != KERNEL_MAGIC)
        return refuse("no-kernel");
    if (_dos_allocmem((unsigned)((hdr.file_size + 15) >> 4), &buf_seg) != 0)
        return refuse("no-low-memory");
    buf_phys = (unsigned long)buf_seg << 4;
    fseek(f, 0, SEEK_SET);
    for (a = 0; a < hdr.file_size; ) {
        static unsigned char chunk[2048];
        unsigned n = (unsigned)(hdr.file_size - a > sizeof chunk ? sizeof chunk : hdr.file_size - a);
        if (fread(chunk, 1, n, f) != n) break;
        _fmemcpy(phys(buf_phys + a), chunk, n);
        a += n;
    }
    fclose(f);
    if (a != hdr.file_size) return refuse("short-kernel");
    bi.kernel_size = hdr.file_size;
    bi.kernel_total = (hdr.total + 0xFFF) & ~0xFFFUL;
    say("GLOS-BOOT step=kernel file=%lu total=%lu", hdr.file_size, bi.kernel_total);

    /* Memory, then A20. */
    r.x.ax = 0x4300; int86(0x2F, &r, &r);
    xms_mode = r.h.al == 0x80;
    bi.a20_initial = a20_on();
    if (xms_mode) {
        unsigned long pool;
        unsigned pool_kb = 0;
        segread(&sr);
        r.x.ax = 0x4310; int86x(0x2F, &r, &r, &sr);
        xms = (void (far *)(void))MK_FP(sr.es, r.x.bx);
        bi.mode = BI_MODE_XMS;
        if (xms_block((unsigned)(bi.kernel_total >> 10), &bi.kernel_phys) != 0) {
            xms_release();
            return refuse("xms-kernel");
        }
        xms_call(0x0800, 0);                    /* largest free block, KB; 64 KB left for others */
        if (xms_ax > 64 && xms_block(pool_kb = xms_ax - 64, &pool) == 0)
            add_range(pool, (unsigned long)pool_kb * 1024UL, BI_MEM_FREE);
        xms_call(0x0000, 0);                    /* what the kernel's XMS server reports as */
        bi.xms_ver = xms_ax; bi.xms_rev = xms_bx; bi.xms_hma = xms_dx;
        xms_call(0x0100, 0xFFFF);               /* is the HMA free? (DOS=HIGH takes it) */
        if (xms_ax == 1) xms_call(0x0200, 0);
        else bi.hma_used = (xms_bx & 0xFF) == 0x91;
        r.x.ax = 0x4309; int86x(0x2F, &r, &r, &sr);     /* the handle table, for handles made before GLOS */
        if (r.h.al == 0x43) bi.xms_table = ((unsigned long)sr.es << 4) + r.x.bx;
        xms_call(0x0500, 0);                    /* local A20 enable */
        say("GLOS-BOOT step=memory mode=xms kernel=%08lx pool_kb=%u ver=%04lx hma_used=%lu table=%05lx",
            bi.kernel_phys, pool_kb, bi.xms_ver, bi.hma_used, bi.xms_table);
    } else {
        bi.mode = BI_MODE_RAW;
        if (raw_memory() != 0) return refuse("no-memory-map");
        bi.kernel_phys = 0x110000UL;            /* above the HMA */
        a20_set(1);
        if (!a20_on()) return refuse("a20");
        say("GLOS-BOOT step=a20 initial=%lu now=1", bi.a20_initial);
    }

    /* Page directory, the 0-4 MB table (0-10FFFFh identity) and the kernel's
       table at 0xC0000000 (supervisor.md §4). */
    if (_dos_allocmem((3 * 4096 + 4096) >> 4, &tab_seg) != 0) return refuse("no-low-memory");
    tab_phys = (((unsigned long)tab_seg << 4) + 0xFFF) & ~0xFFFUL;
    for (a = 0; a < 3 * 4096UL; a += 4) poke32(tab_phys + a, 0);
    bi.pd_phys = tab_phys;
    poke32(tab_phys, (tab_phys + 0x1000) | 3);
    poke32(tab_phys + KERNEL_PDE * 4, (tab_phys + 0x2000) | 3);
    for (i = 0; i < 0x110; i++)
        poke32(tab_phys + 0x1000 + i * 4UL, ((unsigned long)i << 12) | 3);
    pages = (unsigned)(bi.kernel_total >> 12);
    for (i = 0; i < pages; i++)
        poke32(tab_phys + 0x2000 + (0x100 + i) * 4UL, (bi.kernel_phys + ((unsigned long)i << 12)) | 3);

    segread(&sr);
    bi.magic = BOOTINFO_MAGIC;
    bi.version = BOOTINFO_VERSION;
    bi.size = sizeof bi;
    bi.cs_base = (unsigned long)sr.cs << 4;
    bi.ds_base = (unsigned long)sr.ds << 4;
    bi.ret_off = (unsigned)pm_ret;
    bi.vm_resume_off = (unsigned)vm_resume;
    bi.vm_state_off = (unsigned)vm_state;
    bi.bp_call_off = (unsigned)glos_bp_call;
    bi.bp_xms_off = (unsigned)glos_bp_xms;
    bi.kill_off = (unsigned)glos_kill;
    bi.kill_sp = (unsigned)glos_kill_top;
    set_desc(0x08, 0, 0xFFFFF, 0x9A, 0xC0);
    set_desc(0x10, 0, 0xFFFFF, 0x92, 0xC0);
    set_desc(BOOT_SEL_CODE16, bi.cs_base, 0xFFFF, 0x9A, 0x00);
    set_desc(BOOT_SEL_DATA16, bi.ds_base, 0xFFFF, 0x92, 0x00);
    gdtr.limit = sizeof gdt - 1;
    gdtr.base = bi.ds_base + (unsigned)gdt;
    say("GLOS-BOOT step=enter entry=%08lx", KERNEL_LINK + hdr.entry);

    ret = pm_enter((unsigned)&gdtr, bi.pd_phys, buf_phys, KERNEL_LINK, (hdr.file_size + 3) >> 2,
                   (bi.kernel_total - ((hdr.file_size + 3) & ~3UL)) >> 2, KERNEL_LINK + hdr.entry,
                   bi.ds_base + (unsigned)&bi);
    if (ret == 0x10000UL) {
        /* Running in the system VM (V86 mode) under the kernel. The image and
           the first page tables are no longer needed; DOS gets the memory. */
        int code;
        _dos_freemem(tab_seg);
        _dos_freemem(buf_seg);
        tab_seg = buf_seg = 0;
        r.h.ah = 0x34; int86x(0x21, &r, &r, &sr);           /* InDOS flag */
        a = ((unsigned long)sr.es << 4) + r.x.bx;
        r.x.ax = 0x5D06; int86x(0x21, &r, &r, &sr);         /* swappable data area */
        bi.sda = r.x.cflag ? 0 : ((unsigned long)sr.ds << 4) + r.x.si;
        segread(&sr);
        glos_call(GLOS_CALL_DOSPTR, a);
        say("GLOS-VM run=%s", argv[run_at]);
        glos_call(GLOS_CALL_EXEC, _psp);
        code = spawnvp(P_WAIT, argv[run_at], (const char **)(argv + run_at));
        if (code < 0) {
            say("GLOS-VM error=cannot-run program=%s", argv[run_at]);
            code = 126;
        }
        ret = glos_call(GLOS_CALL_LEAVE, (unsigned long)code);
    }

    if (bi.mode == BI_MODE_XMS) {
        xms_call(0x0600, 0);                    /* local A20 disable */
        xms_release();
    } else if (!bi.a20_initial) {
        a20_set(0);
    }
    if (tab_seg) _dos_freemem(tab_seg);
    if (buf_seg) _dos_freemem(buf_seg);
    say("GLOS-EXIT code=%lu a20=%d", ret, a20_on());
    return (int)ret;
}
