/* GLOS.EXE - the loader (docs/supervisor.md §2). 16-bit real mode, Open
 * Watcom small model. Checks the machine, finds the memory GLOS may use
 * (raw: INT 15h E820h/E801h/88h; XMS: locked blocks), reads GLOSK.BIN, builds
 * the first page tables and enters the kernel; when the kernel returns, it
 * restores A20, frees what it took and exits with the kernel's result.
 *     GLOS [/ROUNDTRIP] [/GDB] [/SELFTEST] [/NOVME] [/RUN program [args...]]
 *     SHELL=GLOS.EXE /SHELL [/COMSPEC=path] [/P=autoexec] [/E:bytes] [/CON=command]
 * /RUN keeps DOS running in the kernel's system VM (V86 mode): GLOS.EXE then
 * shrinks to its resident stub (stub.asm), which runs the program and leaves
 * when it ends, with its exit code. As the shell (/SHELL, or started by DOS
 * as its own parent), it runs AUTOEXEC.BAT and then the console instead, and
 * never ends: if GLOS can't start, the stub runs COMMAND.COM (supervisor.md
 * §2.2). The [shell] section of GLOS.CFG, next to GLOS.EXE, gives the same
 * settings as comspec=, autoexec=, console= and envsize=; its [program
 * NAME.EXT] sections, read in every mode, are profiles (§11.2).
 * Progress goes to COM1 as GLOS-BOOT lines; refusals as GLOS-REFUSE.
 *
 * Built a second way, with -dNATIVE, it is GLOSLDR.SYS's loader (docs/v1/native-dos.md, native boot): no DOS
 * under it (GLOS's boot sector loads it, loader/nprelude.S runs it), files by kernel/fs/fat.c over INT 13h and
 * memory from loader/nlib.c, no refusal that asks DOS or its drivers, raw mode always, no DOS pointers in
 * bootinfo (BI_F_NATIVE: the kernel's own DOS gives them); native_main() in place of main(). */
#include <conio.h>
#include <dos.h>
#include <io.h>
#include <i86.h>
#include <process.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "glos/bootinfo.h"
#include "glos/config.h"
#include "inflate.h"
#ifdef NATIVE
#include "../kernel/disk/letters.h"
#endif

#ifndef GLOS_BUILD
#define GLOS_BUILD "unknown"
#endif

extern int __cdecl cpu_is486(void);
extern int __cdecl cpu_v86(void);
extern int __cdecl cpu_id1(unsigned long *eax, unsigned long *edx);
extern int __cdecl bios_e820(void *buf20, unsigned long *cont);
extern unsigned long __cdecl vga_font(unsigned which);

/* The resident stub (stub.asm), in a segment of its own. */
extern struct stub_data __far __cdecl stub_data;
extern unsigned long __cdecl __far pm_enter(unsigned gdtr, unsigned long cr3, unsigned long src, unsigned long dst,
                                            unsigned long file_dwords, unsigned long bss_dwords,
                                            unsigned long entry, unsigned long arg);
extern unsigned long __cdecl __far glos_call(unsigned fn, unsigned long arg);
extern void __cdecl __far stub_resident(void);
/* Labels in the stub: declared as far functions, for their offsets in its segment. */
extern void __cdecl __far pm_ret(void);
extern void __cdecl __far vm_resume(void);
extern void __cdecl __far vm_state(void);
extern void __cdecl __far glos_bp_call(void);
extern void __cdecl __far glos_bp_xms(void);
extern void __cdecl __far glos_kill(void);
extern void __cdecl __far glos_kill_top(void);
extern void __cdecl __far glos_bp_dpmi(void);
extern void __cdecl __far glos_bp_nest(void);
extern void __cdecl __far glos_bp_raw(void);
extern void __cdecl __far glos_retf(void);
extern void __cdecl __far glos_int21(void);
extern void __cdecl __far glos_rmcb(void);
extern void __cdecl __far glos_bp_term(void);
extern void __cdecl __far glos_detached(void);
extern void __cdecl __far stub_end(void);

static unsigned stub_off(void (__cdecl __far *f)(void))
{
    union { void (__cdecl __far *f)(void); unsigned long l; } u;
    u.f = f;
    return (unsigned)u.l;
}

static unsigned long far_addr(const void __far *p)
{
    return ((unsigned long)FP_SEG(p) << 16) | FP_OFF(p);
}

/* ---- COM1 */

static void put(char c)
{
    long spin = 0;
    while (!(inp(0x3FD) & 0x20) && ++spin < 100000L) ;
    outp(0x3F8, c);
}

#ifdef NATIVE
/* GLOSLDR.SYS has no DOS to print through, and a PC it is first tried on has no serial cable as a rule: its lines
   go to the screen too, by the BIOS's teletype, so that a start that stops says where (the owner's PCChips M925G,
   2026-11-23: a USB stick that didn't start, and nothing to tell how far it came). */
static void screen(char c)
{
    union REGS r;
    r.h.ah = 0x0E, r.h.al = (unsigned char)c;
    r.x.bx = 0x0007;
    int86(0x10, &r, &r);
}
#else
#define screen(c) ((void)0)
#endif

static void say(const char *fmt, ...)
{
    char b[160];
    const char *p;
    va_list ap;
    va_start(ap, fmt);
    vsprintf(b, fmt, ap);
    va_end(ap);
    for (p = b; *p; p++) put(*p), screen(*p);
    put('\r'); put('\n');
    screen('\r'); screen('\n');
}

static int refuse(const char *why)
{
    say("GLOS-REFUSE reason=%s", why);
#ifndef NATIVE
    printf("GLOS: cannot start: %s\n", why);
#endif
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

/* XMS 3.0's own calls (88h, 89h): sizes in kilobytes in 32 bits, where 2.0's (08h, 09h) have 16 and so stop at
   65535 KB a block. HIMEMX and MS-DOS 7.1's HIMEM have them. */
static unsigned long xms_eax, xms_edx;
static unsigned char xms_bl;
static int xms3;                                        /* the driver speaks XMS 3.0 and 88h answered */

static void xms_call32(unsigned fn, unsigned long arg)
{
    unsigned long a, d;
    unsigned b;
    _asm {
        mov ax, fn
        mov edx, arg
        call dword ptr xms
        mov a, eax
        mov b, bx
        mov d, edx
    }
    xms_eax = a; xms_bl = (unsigned char)b; xms_edx = d;
    xms_ax = (unsigned)a; xms_dx = (unsigned)d;
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
static unsigned buf_seg, tab_seg;                       /* the kernel image and the first page tables */
static int shell;                                       /* GLOS is the DOS shell */
static int own_shell;                                   /* ... and the shell is GLOS's own, not a COMMAND.COM under it */
static int cmd_dos;                                     /* [shell] command = dos */

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

/* XMS mode: a locked block for the kernel, and up to three for GLOS's own
   memory (the stub keeps four handles). With XMS 3.0's 89h a block can be
   any size, so the first holds everything. With 2.0's 09h a block is at most
   65535 KB, so three of them stopped GLOS at 192 MB (bench-g400's 448 MB,
   2026-11-09; M4e: a 128 MB machine already needed two). */
static int xms_block(unsigned long kb, unsigned long *phys)
{
    if (xms3)
        xms_call32(0x8900, kb);
    else
        xms_call(0x0900, (unsigned)kb);
    if (xms_ax != 1) return -1;
    xms_handles[n_xms++] = xms_dx;
    xms_call(0x0C00, xms_dx);
    if (xms_ax != 1) return -1;
    *phys = ((unsigned long)xms_dx << 16) | xms_bx;
    return 0;
}

#define KCHUNK 32768UL                          /* the kernel image goes up this much at a time */
#define KERNEL_PACKED 0x5A4F4C47UL              /* "GLOZ": GLOSK.BIN packed (tools/packkernel.py) */
#define KERNEL_PACKED_X 0x584F4C47UL            /* "GLOX": ... its near calls and jumps turned first (pk_calls) */
static unsigned long xms_kernel_block;          /* XMS mode: where the kernel's block begins */

/* bytes (even) of the buffer at seg:0 to the kernel's place in extended memory, at off: 0, or -1. */
static int kernel_up(unsigned seg, unsigned long off, unsigned long bytes)
{
    if (bi.mode == BI_MODE_XMS) {               /* the XMS driver's move (0Bh), into the kernel's block (handle 0) */
        static struct { unsigned long len; unsigned sh; unsigned long so; unsigned dh; unsigned long doff; } m;
        unsigned a, mp = (unsigned)&m;
        m.len = bytes;
        m.sh = 0;
        m.so = (unsigned long)seg << 16;
        m.dh = xms_handles[0];
        m.doff = off + (bi.kernel_phys - xms_kernel_block);
        _asm {
            mov ah, 0Bh
            mov si, mp
            call dword ptr xms
            mov a, ax
        }
        return a == 1 ? 0 : -1;
    } else {                                    /* INT 15h 87h: the BIOS's block move, a descriptor each way */
        static unsigned char g[48];
        unsigned long src = (unsigned long)seg << 4, dst = bi.kernel_phys + off;
        union REGS r;
        struct SREGS sr;
        memset(g, 0, sizeof g);
        g[16] = g[24] = 0xFF, g[17] = g[25] = 0xFF;             /* limits */
        g[18] = (unsigned char)src, g[19] = (unsigned char)(src >> 8), g[20] = (unsigned char)(src >> 16);
        g[26] = (unsigned char)dst, g[27] = (unsigned char)(dst >> 8), g[28] = (unsigned char)(dst >> 16);
        g[31] = (unsigned char)(dst >> 24);
        g[21] = g[29] = 0x93;                   /* data, writable, present */
        segread(&sr);
        sr.es = sr.ds;
        r.h.ah = 0x87;
        r.x.cx = (unsigned)(bytes >> 1);
        r.x.si = (unsigned)g;
        int86x(0x15, &r, &r, &sr);
        return r.x.cflag || r.h.ah ? -1 : 0;
    }
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

static unsigned char gdt[8 * 13];
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

/* The /RUN program for the stub to EXEC: its name in stub_data.path, its
   command tail in stub_data.tail. As COMMAND.COM: .COM, .EXE, then .BAT,
   here and then along PATH; a batch file runs under %COMSPEC% /C. */
static char run_path[80];

#ifndef NATIVE
static int find_program(char **args)
{
    static const char *const ext[] = { ".COM", ".EXE", ".BAT" };
    const char *base = args[0], *slash = strrchr(base, '\\'), *comspec;
    char name[80], found[_MAX_PATH], tail[130];     /* _searchenv() fills _MAX_PATH bytes: an 80-byte
                                                       found[] let it smash the stack (M4c's GTA hang) */
    int i, has_ext = strchr(slash ? slash : base, '.') != NULL;
    unsigned n;

    found[0] = 0;
    for (i = 0; i < 3 && !found[0]; i++) {
        if (strlen(base) + 5 > sizeof name)
            return -1;
        strcpy(name, base);
        if (!has_ext)
            strcat(name, ext[i]);
        if (strpbrk(name, "\\:")) {
            if (access(name, 0) == 0)
                strcpy(found, name);
        } else {
            _searchenv(name, "PATH", found);
        }
        if (has_ext)
            break;
    }
    if (!found[0])
        return -1;
    tail[0] = 0;
    n = strlen(found);
    if (n > 4 && !stricmp(found + n - 4, ".BAT")) {
        comspec = getenv("COMSPEC");
        if (!comspec || strlen(comspec) >= sizeof found || strlen(found) + 4 >= sizeof tail)
            return -1;
        strcpy(tail, " /C ");
        strcat(tail, found);
        strcpy(found, comspec);
    }
    for (i = 1; args[i]; i++) {
        if (strlen(tail) + 1 + strlen(args[i]) > 126)
            return -1;
        strcat(tail, " ");
        strcat(tail, args[i]);
    }
    n = strlen(tail);
    if (strlen(found) >= sizeof run_path)               /* the stub's EXEC path holds as much */
        return -1;
    strcpy(run_path, found);
    _fstrcpy(stub_data.path, found);
    stub_data.tail[0] = (unsigned char)n;
    _fmemcpy(stub_data.tail + 1, tail, n);
    stub_data.tail[n + 1] = 0x0D;
    return 0;
}
#else
static int find_program(char **args) { (void)args; return -1; }
#endif

/* KEYS\name beside GLOS.EXE, into buf (at most max bytes); the length read. */
static unsigned long read_key_file(const char *argv0, const char *name, void *buf, unsigned max)
{
    char path[128];
    const char *slash = strrchr(argv0, '\\');
    size_t n = slash ? (size_t)(slash - argv0 + 1) : 0;
    FILE *f;
    unsigned got;
    memcpy(path, argv0, n);
    strcpy(path + n, "KEYS\\");
    strcat(path, name);
    if (!(f = fopen(path, "rb")))
        return 0;
    got = (unsigned)fread(buf, 1, max, f);
    fclose(f);
    return got;
}

/* GLOSK.BIN next to GLOS.EXE */
static void kernel_path(char *out, const char *argv0)
{
    const char *slash = strrchr(argv0, '\\');
    size_t n = slash ? (size_t)(slash - argv0 + 1) : 0;
    memcpy(out, argv0, n);
    strcpy(out + n, "GLOSK.BIN");
}

/* [cache] enabled's words, and /CACHE='s: BI_CACHE_*, or -1. */
static int cache_word(const char *v)
{
    if (!stricmp(v, "off")) return BI_CACHE_OFF;
    if (!stricmp(v, "on")) return BI_CACHE_ON;
    if (!stricmp(v, "verify")) return BI_CACHE_VERIFY;
    if (!stricmp(v, "count")) return BI_CACHE_COUNT;
    return -1;
}

/* A flag that only sets a bootinfo bit: from the command line, or GLOS.CFG's
   [shell] options (a SHELL= line has no room for them). /CACHE=ON|OFF|VERIFY|COUNT
   overrides [cache] enabled (the tests' runs with the cache on and off), and
   /STUBPAGE=OFF|AUTO|SEGMENT [memory] stub_page (docs/upper-memory.md). */
static int flag_arg(const char *a)
{
    if (!stricmp(a, "/ROUNDTRIP")) bi.flags |= BI_F_ROUNDTRIP;
    else if (!stricmp(a, "/BIOSDISK")) bi.flags |= BI_F_BIOSDISK;
    else if (!stricmp(a, "/GDB")) bi.flags |= BI_F_GDB;
    else if (!stricmp(a, "/SELFTEST")) bi.flags |= BI_F_SELFTEST;
    else if (!stricmp(a, "/NOVME")) bi.flags |= BI_F_NOVME;
    else if (!stricmp(a, "/AGENT")) bi.flags |= BI_F_AGENT;
    else if (!stricmp(a, "/DPMITRACE")) bi.flags |= BI_F_DPMITRACE;
    else if (!stricmp(a, "/DIRECT")) bi.flags |= BI_F_DIRECT;
    else if (!stricmp(a, "/VERBOSE")) bi.flags |= BI_F_VERBOSE;
    else if (!stricmp(a, "/SHAREIRQ")) bi.flags |= BI_F_SHAREIRQ;
    else if (!strnicmp(a, "/CACHE=", 7) && cache_word(a + 7) >= 0) bi.cache_mode = (unsigned long)cache_word(a + 7);
    else if (!strnicmp(a, "/STUBPAGE=", 10) && cfg_page(a + 10) >= 0) bi.stub_page = (unsigned long)cfg_page(a + 10);
    else return 0;
    return 1;
}

static void opt_str(char *dst, unsigned n, const char *v);

static char cfg_blaster[48];                    /* [sound] blaster = ..., in place of the environment's BLASTER */

/* The BIOS's hard disks (INT 13h 08h, 41h, 48h; the count at 0040:0075) and
   whether SMARTDRV is there (INT 2Fh 4A10h): the disk cache's (bootinfo v5).
   Read only; ES:DI zero for 08h, as some BIOSes want. A disk whose 08h fails or
   says no sectors a track (bench F's Award BIOS of 2005: CX FFC0h for a 60 GB
   disk) is kept if the extensions (41h, 48h) give its size: its geometry is then
   0/0/0, and the cache takes only its LBA calls. */
static void disks_probe(void)
{
    static unsigned char p48[30];
    union REGS r;
    struct SREGS sr;
    unsigned n = *(unsigned char __far *)MK_FP(0x40, 0x75), i, fl;
    struct bi_disk *d;

    if (bi.n_disks)                             /* (asked already: a native start's letters) */
        return;
    memset(&r, 0, sizeof r);
#ifndef NATIVE                                  /* (no DOS: no INT 2Fh, and no SMARTDRV) */
    r.x.ax = 0x4A10;
    int86(0x2F, &r, &r);
    bi.smartdrv = r.x.ax == 0xBABE;
#endif
    for (i = 0; i < n && bi.n_disks < BI_DISKS; i++) {
        d = &bi.disks[bi.n_disks];
        segread(&sr);
        memset(&r, 0, sizeof r);
        sr.es = 0;
        r.h.ah = 0x08;
        r.h.dl = (unsigned char)(0x80 + i);
        int86x(0x13, &r, &r, &sr);
        d->dl = 0x80 + i;
        if (r.x.cflag || !(r.h.cl & 63)) {
            d->cyls = d->heads = d->spt = 0;
        } else {
            d->cyls = (unsigned long)(r.h.ch | ((r.h.cl & 0xC0) << 2)) + 1;
            d->heads = (unsigned long)r.h.dh + 1;
            d->spt = r.h.cl & 63;
        }
        d->sectors_lo = d->cyls * d->heads * d->spt;
        d->sectors_hi = 0;
        d->bps = 512;
        d->flags = 0;
        memset(&r, 0, sizeof r);
        r.h.ah = 0x41;
        r.x.bx = 0x55AA;
        r.h.dl = (unsigned char)d->dl;
        int86(0x13, &r, &r);
        if (!r.x.cflag && r.x.bx == 0xAA55 && (r.x.cx & 1)) {
            d->flags |= BI_DISK_EDD;
            memset(p48, 0, sizeof p48);
            *(unsigned *)p48 = 0x1A;
            segread(&sr);
            memset(&r, 0, sizeof r);
            r.h.ah = 0x48;
            r.h.dl = (unsigned char)d->dl;
            sr.ds = FP_SEG((void __far *)p48);
            r.x.si = FP_OFF((void __far *)p48);
            int86x(0x13, &r, &r, &sr);
            fl = *(unsigned *)(p48 + 2);
            if (!r.x.cflag && (*(unsigned long *)(p48 + 16) || *(unsigned long *)(p48 + 20))) {
                d->flags |= BI_DISK_PARAMS | ((fl & 4) ? BI_DISK_REMOVE : 0);
                d->sectors_lo = *(unsigned long *)(p48 + 16);
                d->sectors_hi = *(unsigned long *)(p48 + 20);
                d->bps = *(unsigned *)(p48 + 24);
            }
        }
        if (!d->spt && !(d->flags & BI_DISK_PARAMS))
            continue;                           /* (neither 08h's geometry nor 48h's size: not a disk to us) */
        say("GLOS-BOOT disk dl=%02lx chs=%lu/%lu/%lu sectors=%lu bps=%lu flags=%lx", d->dl, d->cyls, d->heads, d->spt,
            d->sectors_lo, d->bps, d->flags);
        bi.n_disks++;
    }
}

/* A packed GLOSK.BIN (tools/packkernel.py): unpacked through the low buffer, which is the unpacker's window, a
   buffer's worth at a time on to the image's place in extended memory. */
static FILE *pk_file;
static unsigned pk_seg;
static unsigned long pk_at, pk_left, pk_a = 1, pk_b;       /* (the image's Adler-32 as it goes by) */
static unsigned long pk_size;                   /* the image's bytes */
static int pk_turned;                           /* "GLOX": its calls are to be put back */

/* A block of the image (n bytes at w, the image's bytes from base on): its near calls' and jumps' distances from
   the targets' places packkernel.py gave them (back 1: to those places again). The rule is packkernel.py's,
   which says why: E8h or E9h with its four bytes inside the block and ten bytes or more before the image's end,
   the five bytes stepped over whether changed or not; a value v at place p of an image of size bytes changes if
   -p <= v < size. */
static void pk_calls(unsigned char far *w, unsigned n, unsigned long base, int back)
{
    unsigned i = 0;
    while (i + 5 <= n) {
        unsigned char op = w[i];
        long p = (long)(base + i), v, size = (long)pk_size;
        if ((op != 0xE8 && op != 0xE9) || p + 10 >= size) {
            i++;
            continue;
        }
        v = (long)((unsigned long)w[i + 1] | (unsigned long)w[i + 2] << 8 | (unsigned long)w[i + 3] << 16
                   | (unsigned long)w[i + 4] << 24);
        if (v >= -p && v < size) {
            if (back)
                v = v < size - p ? v + p : v - size;
            else
                v = v >= 0 ? v - p : v + size;
            w[i + 1] = (unsigned char)v, w[i + 2] = (unsigned char)(v >> 8);
            w[i + 3] = (unsigned char)(v >> 16), w[i + 4] = (unsigned char)(v >> 24);
        }
        i += 5;
    }
}

static unsigned pk_fill(const unsigned char **p, void *ctx)
{
    static unsigned char chunk[2048];
    unsigned n = pk_left > sizeof chunk ? sizeof chunk : (unsigned)pk_left;
    (void)ctx;
    n = (unsigned)fread(chunk, 1, n, pk_file);
    pk_left -= n;
    *p = chunk;
    return n;
}

static int pk_flush(unsigned n, void *ctx)
{
    unsigned char far *w = MK_FP(pk_seg, 0);
    unsigned i = 0, k;
    (void)ctx;
    if (pk_turned)                              /* (the calls put back in the window itself, for the sum and the
                                                   copy; then turned again: the stream's later bytes are copies
                                                   of the window's as the stream had them) */
        pk_calls(w, n, pk_at, 0);
    while (i < n) {                             /* (5552 bytes at most between the remainders: the sums fit 32 bits) */
        k = n - i > 5552 ? 5552 : n - i;
        while (k--) {
            pk_a += w[i++];
            pk_b += pk_a;
        }
        pk_a %= 65521UL;
        pk_b %= 65521UL;
    }
    if (kernel_up(pk_seg, pk_at, ((unsigned long)n + 1) & ~1UL) != 0)
        return -1;
    if (pk_turned)
        pk_calls(w, n, pk_at, 1);
    pk_at += n;
    return 0;
}

static int glos_main(int argc, char **argv)
{
    struct SREGS sr;
    union REGS r;
    struct { unsigned long magic, entry, file_size, total; } hdr;
    struct { unsigned long length, adler; } packed;
    int is_packed = 0;
    unsigned long eax_, edx_, buf_phys, tab_phys, a, ret;
    unsigned i, pages;
    char path[128];
    FILE *f;
    int xms_mode, run_at = 0;

    if (shell)
        bi.flags |= BI_F_SHELL | BI_F_VM;
    if (getenv("GLOS") != NULL) {                       /* SET GLOS=/DIRECT ...: flags in any mode (M4e) */
        static char env_flags[128];
        char *t;
        opt_str(env_flags, sizeof env_flags, getenv("GLOS"));
        for (t = strtok(env_flags, " \t"); t; t = strtok(NULL, " \t"))
            flag_arg(t);
    }
    for (i = 1; i < (unsigned)argc && !shell; i++) {
        if (flag_arg(argv[i]))
            ;
        else if (!stricmp(argv[i], "/RUN") && i + 1 < (unsigned)argc) {
            bi.flags |= BI_F_VM;
            run_at = (int)i + 1;
            break;
        }
    }
    if (run_at || (shell && !own_shell))
        bi.flags &= ~BI_F_AGENT;
    else if (!(bi.flags & BI_F_ROUNDTRIP))
        bi.flags |= BI_F_VM | BI_F_AGENT;               /* no mode: headless, the agent (supervisor.md §17.3) */

    /* Refusals (supervisor.md §2.1). */
    if (!cpu_is486()) return refuse("cpu");
    if (cpu_v86()) return refuse("v86");
#ifdef NATIVE
    bi.flags |= BI_F_NATIVE;
#else
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
#endif
    bi.cpu_family = 4;
    if (cpu_id1(&eax_, &edx_)) {
        bi.cpu_family = (eax_ >> 8) & 15;
        bi.cpuid_edx = edx_;
    }
    r.h.ah = 0x0F; int86(0x10, &r, &r);
    bi.video_mode = r.h.al & 0x7F;
    bi.font16 = vga_font(6);                                /* for glos shot: the card's own glyphs */
    bi.font14 = vga_font(2);
    bi.font8 = vga_font(3);                                 /* characters 00h-7Fh ... */
    bi.font8hi = vga_font(4);                               /* ... and 80h-FFh */
    bi.pic_mask = inp(0x21) | ((unsigned long)inp(0xA1) << 8);
    say("GLOS-BOOT step=checks cpu=%lu cpuid_edx=%08lx", bi.cpu_family, bi.cpuid_edx);

    /* The kernel image. */
    kernel_path(path, argv[0]);
    f = fopen(path, "rb");
    if (!f || fread(&hdr, sizeof hdr, 1, f) != 1
        || (hdr.magic != KERNEL_MAGIC && hdr.magic != KERNEL_PACKED && hdr.magic != KERNEL_PACKED_X))
        return refuse("no-kernel");
    if (hdr.magic != KERNEL_MAGIC) {            /* (the image's own header's figures, then the stream's) */
        if (fread(&packed, sizeof packed, 1, f) != 1)
            return refuse("no-kernel");
        is_packed = 1;
    }
    if (_dos_allocmem(KCHUNK >> 4, &buf_seg) != 0)     /* (the image goes up a piece at a time, below) */
        return refuse("no-low-memory");
    buf_phys = (unsigned long)buf_seg << 4;
    bi.kernel_size = hdr.file_size;
    bi.kernel_total = (hdr.total + 0xFFF) & ~0xFFFUL;
    if (is_packed)
        say("GLOS-BOOT step=kernel file=%lu total=%lu packed=%lu", hdr.file_size, bi.kernel_total, packed.length);
    else
        say("GLOS-BOOT step=kernel file=%lu total=%lu", hdr.file_size, bi.kernel_total);

    /* Memory, then A20. */
#ifdef NATIVE
    xms_mode = 0;                               /* (no DOS, no HIMEM: raw mode) */
#else
    r.x.ax = 0x4300; int86(0x2F, &r, &r);
    xms_mode = r.h.al == 0x80;
#endif
    bi.a20_initial = a20_on();
    if (xms_mode) {
        unsigned long pool;
        unsigned long pool_kb = 0;
        segread(&sr);
        r.x.ax = 0x4310; int86x(0x2F, &r, &r, &sr);
        xms = (void (far *)(void))MK_FP(sr.es, r.x.bx);
        bi.mode = BI_MODE_XMS;
        xms_call(0x0000, 0);                    /* the version: 3.0's 88h and 89h where it has them */
        if (xms_ax >= 0x0300) {
            xms_call32(0x8800, 0);
            xms3 = xms_bl == 0 && xms_eax != 0;
        }
        if (xms_block((unsigned)(bi.kernel_total >> 10) + 4, &xms_kernel_block) != 0) {
            xms_release();
            return refuse("xms-kernel");
        }
        bi.kernel_phys = (xms_kernel_block + 0xFFFUL) & ~0xFFFUL;     /* (a page's boundary: the page tables map
                                                   it from there; the block need not begin on one) */
        while (n_xms < 4) {                     /* the largest free block, KB, again and again; 64 KB */
            unsigned long kb, largest;          /* left for others */
            if (xms3) {
                xms_call32(0x8800, 0);
                largest = xms_bl == 0 ? xms_eax : 0;
                if (largest > 4190208UL)        /* (4092 MB: its length in bytes still fits 32 bits) */
                    largest = 4190208UL + 64;
                kb = largest - 64;
            } else {
                xms_call(0x0800, 0);
                largest = xms_ax;
                kb = xms_ax == 0xFFFF ? 0xFFFF : xms_ax - 64;
            }
            if (largest <= 64 || xms_block(kb, &pool) != 0)
                break;
            add_range(pool, (unsigned long)kb * 1024UL, BI_MEM_FREE);
            pool_kb += kb;
        }
        xms_call(0x0000, 0);                    /* what the kernel's XMS server reports as */
        bi.xms_ver = xms_ax; bi.xms_rev = xms_bx; bi.xms_hma = xms_dx;
        xms_call(0x0100, 0xFFFF);               /* is the HMA free? (DOS=HIGH takes it) */
        if (xms_ax == 1) xms_call(0x0200, 0);
        else bi.hma_used = (xms_bx & 0xFF) == 0x91;
        r.x.ax = 0x4309; int86x(0x2F, &r, &r, &sr);     /* the handle table, for handles made before GLOS */
        if (r.h.al == 0x43) bi.xms_table = ((unsigned long)sr.es << 4) + r.x.bx;
        xms_call(0x0500, 0);                    /* local A20 enable */
        say("GLOS-BOOT step=memory mode=xms kernel=%08lx pool_kb=%lu blocks=%u ver=%04lx xms3=%d hma_used=%lu table=%05lx",
            bi.kernel_phys, pool_kb, n_xms - 1, bi.xms_ver, xms3, bi.hma_used, bi.xms_table);
    } else {
        bi.mode = BI_MODE_RAW;
        if (raw_memory() != 0) return refuse("no-memory-map");
        bi.kernel_phys = 0x110000UL;            /* above the HMA */
        a20_set(1);
        if (!a20_on()) return refuse("a20");
        say("GLOS-BOOT step=a20 initial=%lu now=1", bi.a20_initial);
    }

    /* The image into its place in extended memory, 32 KB at a time through a buffer in low memory (by the
       XMS driver's move, or INT 15h 87h): the kernel outgrew the low memory a copy of it whole needed (M9). */
    if (is_packed) {
        int r;
        pk_file = f, pk_seg = buf_seg, pk_at = 0, pk_left = packed.length;
        pk_size = hdr.file_size, pk_turned = hdr.magic == KERNEL_PACKED_X;
        r = inflate_stream(pk_fill, pk_flush, 0, (unsigned char far *)MK_FP(buf_seg, 0));
        fclose(f);
        if (r == -2)
            return refuse("kernel-copy");
        if (r != 0 || pk_at != hdr.file_size || ((pk_b << 16) | pk_a) != packed.adler)
            return refuse("bad-kernel");        /* (the file is damaged: a floppy's read errors, a copy cut short) */
        a = hdr.file_size;
    } else {
    fseek(f, 0, SEEK_SET);
    for (a = 0; a < hdr.file_size; ) {
        static unsigned char chunk[2048];
        unsigned long piece = hdr.file_size - a > KCHUNK ? KCHUNK : hdr.file_size - a, b;
        for (b = 0; b < piece; ) {
            unsigned n = (unsigned)(piece - b > sizeof chunk ? sizeof chunk : piece - b);
            if (fread(chunk, 1, n, f) != n) break;
            _fmemcpy(phys(buf_phys + b), chunk, n);
            b += n;
        }
        if (b != piece) break;
        if (kernel_up(buf_seg, a, (piece + 1) & ~1UL) != 0) {
            fclose(f);
            return refuse("kernel-copy");
        }
        a += piece;
    }
    fclose(f);
    }
    if (a != hdr.file_size) return refuse("short-kernel");
    if (bi.mode == BI_MODE_RAW) {               /* (INT 15h 87h leaves A20 off on many BIOSes: on again) */
        a20_set(1);
        if (!a20_on()) return refuse("a20");
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

    bi.seed_len = read_key_file(argv[0], "SEED.BIN", bi.seed, sizeof bi.seed);
    bi.hostkey_len = read_key_file(argv[0], "HOSTKEY", bi.hostkey, sizeof bi.hostkey);
    bi.authkeys_len = read_key_file(argv[0], "AUTHKEYS", bi.authkeys, sizeof bi.authkeys);
    say("GLOS-BOOT step=keys seed=%lu hostkey=%lu authkeys=%lu", bi.seed_len, bi.hostkey_len, bi.authkeys_len);
#ifdef NATIVE
    bi.indos = bi.sda = bi.lol = 0;             /* (the kernel's own DOS's, when it makes them; boot_drive is
                                                   native_main's: the letter of the volume GLOS started from) */
    segread(&sr);
    bi.magic = BOOTINFO_MAGIC;
    bi.version = BOOTINFO_VERSION;
#else
    r.h.ah = 0x34; int86x(0x21, &r, &r, &sr);               /* InDOS flag */
    bi.indos = ((unsigned long)sr.es << 4) + r.x.bx;
    r.x.ax = 0x5D06; int86x(0x21, &r, &r, &sr);             /* swappable data area */
    bi.sda = r.x.cflag ? 0 : ((unsigned long)sr.ds << 4) + r.x.si;
    r.h.ah = 0x52; sr.es = 0; int86x(0x21, &r, &r, &sr);    /* List of Lists: the SFT chain */
    bi.lol = sr.es ? ((unsigned long)sr.es << 4) + r.x.bx : 0;
    segread(&sr);
    bi.magic = BOOTINFO_MAGIC;
    bi.version = BOOTINFO_VERSION;
    {                                           /* the drive DOS started from (DOS 4 on; an older one leaves DL) */
        union REGS b;
        b.x.ax = 0x3305;
        b.x.dx = 0;
        intdos(&b, &b);
        bi.boot_drive = b.h.dl >= 1 && b.h.dl <= 26 ? b.h.dl : 0;
    }
#endif
    bi.size = sizeof bi;
    bi.cs_base = (unsigned long)FP_SEG(&stub_data) << 4;    /* 38h: the stub's segment */
    bi.ds_base = (unsigned long)sr.ds << 4;
    bi.ret_off = stub_off(pm_ret);
    bi.vm_resume_off = stub_off(vm_resume);
    bi.vm_state_off = stub_off(vm_state);
    bi.bp_call_off = stub_off(glos_bp_call);
    bi.bp_xms_off = stub_off(glos_bp_xms);
    bi.kill_off = stub_off(glos_kill);
    bi.kill_sp = stub_off(glos_kill_top);
    {                                                   /* BLASTER=A220 I5 D1 H5 ...: the Sound Blaster */
        const char *b0 = cfg_blaster[0] ? (const char *)cfg_blaster : (const char *)getenv("BLASTER"), *b;
        bi.sb_irq = bi.sb_dma8 = bi.sb_dma16 = 0xFF;
        for (b = b0; b && *b; b++)
            if (b == b0 || b[-1] == ' ') {
                switch (*b) {
                case 'A': case 'a': bi.sb_port = strtoul(b + 1, 0, 16); break;
                case 'I': case 'i': bi.sb_irq = strtoul(b + 1, 0, 10); break;
                case 'D': case 'd': bi.sb_dma8 = strtoul(b + 1, 0, 10); break;
                case 'H': case 'h': bi.sb_dma16 = strtoul(b + 1, 0, 10); break;
                case 'P': case 'p': bi.sb_mpu = strtoul(b + 1, 0, 16); break;
                }
            }
    }
    bi.bp_dpmi_off = stub_off(glos_bp_dpmi);
    bi.bp_nest_off = stub_off(glos_bp_nest);
    bi.bp_raw_off = stub_off(glos_bp_raw);
    bi.retf_off = stub_off(glos_retf);
    bi.int21_off = stub_off(glos_int21);
    bi.rmcb_off = stub_off(glos_rmcb);
    bi.bp_term_off = stub_off(glos_bp_term);
    bi.int2f_entry = (unsigned long)_psp << 16 | BI_INT2F_PSP;       /* (not in the stub: it moves, or leaves) */
    bi.detached_off = stub_off(glos_detached);
    bi.stub_paras = (stub_off(stub_end) + 15) >> 4;
    disks_probe();                              /* the disk cache's block buffer (4 KB and its DAP) is kept after */
    bi.cache_paras = (bi.cache_mode == BI_CACHE_ON || bi.cache_mode == BI_CACHE_VERIFY) && !bi.smartdrv
                     && bi.n_disks ? BI_CACHE_PARAS : 0;        /* the stub when it goes resident (P7: 8 KB) */
    stub_data.extra_paras = (unsigned short)bi.cache_paras;
    stub_data.mode = bi.mode == BI_MODE_XMS;
    stub_data.a20init = (unsigned char)bi.a20_initial;
    stub_data.xms = far_addr((const void __far *)xms);
    for (i = 0; i < n_xms; i++)
        stub_data.handles[i] = xms_handles[i];
    stub_data.nxms = (unsigned char)n_xms;
    set_desc(0x08, 0, 0xFFFFF, 0x9A, 0xC0);
    set_desc(0x10, 0, 0xFFFFF, 0x92, 0xC0);
    set_desc(BOOT_SEL_CODE16, bi.cs_base, 0xFFFF, 0x9A, 0x00);
    set_desc(BOOT_SEL_DATA16, bi.ds_base, 0xFFFF, 0x92, 0x00);
    gdtr.limit = sizeof gdt - 1;
    gdtr.base = bi.ds_base + (unsigned)gdt;
    say("GLOS-BOOT step=enter entry=%08lx", KERNEL_LINK + hdr.entry);

    ret = pm_enter((unsigned)&gdtr, bi.pd_phys, buf_phys, KERNEL_LINK + ((hdr.file_size + 3) & ~3UL), 0,
                   (bi.kernel_total - ((hdr.file_size + 3) & ~3UL)) >> 2, KERNEL_LINK + hdr.entry,
                   bi.ds_base + (unsigned)&bi);     /* (the image is there already: only its bss to clear) */
#ifdef NATIVE
    if (ret != 0x10000UL) {                     /* (a round trip, /ROUNDTRIP's, comes back to real mode) */
        say("GLOS-BOOT step=back ret=%08lx", ret);
        return (int)ret;
    }
#endif
    if (ret == 0x10000UL) {
        /* Running in the system VM (V86 mode) under the kernel. The image and
           the first page tables go back to DOS, and so does the rest of
           GLOS.EXE: the stub, copied down to just above the PSP, runs the
           program and leaves when it ends. This code never runs again. The
           copy must not overlap the stub where it is now. */
        _dos_freemem(tab_seg);
        _dos_freemem(buf_seg);
        tab_seg = buf_seg = 0;
        if (shell || (bi.flags & BI_F_AGENT)) {
            if (FP_SEG(&stub_data) < _psp + 0x10 + bi.stub_paras) {
                say("GLOS-VM error=stub-layout seg=%04x psp=%04x", FP_SEG(&stub_data), _psp);
                return (int)glos_call(GLOS_CALL_LEAVE, 126);
            }
            if (shell && own_shell)
                say("GLOS-VM shell own autoexec=%s", bi.autoexec);
            else if (shell)
                say("GLOS-VM shell comspec=%s autoexec=%s", bi.comspec, bi.autoexec);
            else
                say("GLOS-VM agent comspec=%s", bi.comspec);
            stub_resident();
        }
        if (find_program(argv + run_at) != 0) {
            say("GLOS-VM error=cannot-run program=%s", argv[run_at]);
            ret = glos_call(GLOS_CALL_LEAVE, 126);
        } else if (FP_SEG(&stub_data) < _psp + 0x10 + bi.stub_paras) {
            say("GLOS-VM error=stub-layout seg=%04x psp=%04x", FP_SEG(&stub_data), _psp);
            ret = glos_call(GLOS_CALL_LEAVE, 126);
        } else {
            say("GLOS-VM run=%s", run_path);
            stub_resident();
        }
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

/* ---- GLOS as the DOS shell (supervisor.md §2.2) */

static int shell_p, shell_e;                            /* /P= and /E: were given */
static unsigned envsize = 1024;
static char envbuf[4096];

static void opt_str(char *dst, unsigned n, const char *v)
{
    strncpy(dst, v, n - 1);
    dst[n - 1] = 0;
}

/* A [program NAME.EXT] section's slot (a new one if it's not there yet), or
   NULL when they are all taken. */
static struct bi_profile *profile_slot(const char *name)
{
    struct bi_profile *p, *free_slot = NULL;
    for (p = bi.profiles; p < bi.profiles + BI_PROFILES; p++) {
        if (!p->name[0] && !free_slot)
            free_slot = p;
        if (p->name[0] && !stricmp(p->name, name))
            return p;
    }
    if (free_slot) {
        opt_str(free_slot->name, sizeof free_slot->name, name);
        strupr(free_slot->name);
    }
    return free_slot;
}

/* env = NAME=VALUE in a profile: after the strings it has (a later one for
   the same NAME wins in the EXEC's environment, which the kernel builds). */
static void profile_env(struct bi_profile *p, const char *v)
{
    unsigned n = 0, len = (unsigned)strlen(v);
    while (p->env[n])
        n += (unsigned)strlen(p->env + n) + 1;
    if (!strchr(v, '=') || n + len + 2 > sizeof p->env) {
        say("GLOS-WARN cfg-env program=%s env=\"%s\" (no '=' or no room)", p->name, v);
        return;
    }
    memcpy(p->env + n, v, len + 1);
    p->env[n + len + 1] = 0;
}

/* GLOS.CFG next to GLOS.EXE: [program NAME.EXT] sections (profiles), [net],
   [display], [sound], and as the shell the [shell] section; key = value lines. */
static void read_cfg(const char *argv0, int shell_keys)
{
    char line[160], path[128], *k, *v, *e;
    const char *slash = strrchr(argv0, '\\');
    size_t n = slash ? (size_t)(slash - argv0 + 1) : 0;
    int in_shell = 0, in_net = 0, in_display = 0, in_sound = 0, in_cache = 0, in_memory = 0, in_devices = 0;
    struct bi_profile *prof = NULL;
    char sec[24], why[64];
    const struct cfg_key *ck;
    FILE *f;

    memcpy(path, argv0, n);
    strcpy(path + n, "GLOS.CFG");
    strncpy(bi.cfg_path, path, sizeof bi.cfg_path - 1);        /* (M9: the kernel keeps and writes it) */
    if (!(f = fopen(path, "r")))
        return;
    while (fgets(line, sizeof line, f)) {
        for (k = line; *k == ' ' || *k == '\t'; k++) ;
        if (*k == ';' || *k == '#' || !*k)
            continue;
        if (*k == '[') {
            for (v = k + 1, n = 0; *v && *v != ']' && n < sizeof sec - 1; v++)
                if (*v != ' ' || (n && sec[n - 1] != ' '))
                    sec[n++] = *v;
            sec[n] = 0;
            in_shell = shell_keys && !strnicmp(k, "[shell]", 7);
            in_net = !strnicmp(k, "[net]", 5);
            in_display = !strnicmp(k, "[display]", 9);
            in_sound = !strnicmp(k, "[sound]", 7);
            in_cache = !strnicmp(k, "[cache]", 7);
            in_memory = !strnicmp(k, "[memory]", 8);
            in_devices = !strnicmp(k, "[devices]", 9);
            prof = NULL;
            if (!strnicmp(k, "[program", 8) && (e = strchr(k, ']')) != NULL) {
                *e = 0;
                for (v = k + 8; *v == ' ' || *v == '\t'; v++) ;
                if (*v && !(prof = profile_slot(v)))
                    say("GLOS-WARN cfg-profiles max=%d", BI_PROFILES);
            }
            continue;
        }
        if ((!in_shell && !prof && !in_net && !in_display && !in_sound && !in_cache && !in_memory && !in_devices)
            || !(v = strchr(k, '=')))
            continue;
        for (e = v; e > k && (e[-1] == ' ' || e[-1] == '\t'); e--) ;
        *e = 0;
        for (v++; *v == ' ' || *v == '\t'; v++) ;
        for (e = v + strlen(v); e > v && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '); e--) ;
        *e = 0;
        if (!(ck = cfg_find(sec, k)) || cfg_check(ck, v, why, sizeof why) != 0) {      /* (the schema's rules: M9) */
            say("GLOS-WARN cfg-key section=%s key=%s%s%s", sec, k, ck ? " why=" : "", ck ? why : "");
            continue;
        }
        if (in_display) {                       /* mode = WIDTHxHEIGHTxBITS: the desktop's (M6); none: text mode */
            unsigned w, h, b;                   /* accel = on | off: a Matrox card's drawing engine (M7) */
                                                /* gpu_watchdog = MS | off: how long the desktop waits for an app's
                                                   turn with the engine before ending it (M8; 500) */
            if (!stricmp(k, "mode") && !stricmp(v, "none")) {
                bi.display_w = bi.display_h = bi.display_bpp = 0;
            } else if (!stricmp(k, "mode") && sscanf(v, "%ux%ux%u", &w, &h, &b) == 3 && w >= 320 && w <= 1600 && h >= 200
                && h <= 1200 && (b == 8 || b == 16 || b == 32)) {
                bi.display_w = w;
                bi.display_h = h;
                bi.display_bpp = b;
            } else if (!stricmp(k, "accel") && (!stricmp(v, "on") || !stricmp(v, "off"))) {
                bi.display_flags = !stricmp(v, "off") ? bi.display_flags | BI_DISPLAY_NOACCEL
                                                      : bi.display_flags & ~BI_DISPLAY_NOACCEL;
            } else if (!stricmp(k, "gpu_watchdog") && (!stricmp(v, "off") || (atol(v) >= 50 && atol(v) <= 60000))) {
                bi.gpu_watchdog_ms = !stricmp(v, "off") ? 0xFFFFFFFFul : (unsigned long)atol(v);   /* (M8) */
            } else if (!stricmp(k, "mode") || !stricmp(k, "accel") || !stricmp(k, "gpu_watchdog")) {
                say("GLOS-WARN cfg-key section=display key=%s", k);
            }                                   /* (font_smoothing, M10: the programs', checked above) */
            continue;
        }
        if (in_sound) {                         /* (M8) mixer = on | off: GLOS drives the SB16 and mixes the apps'
                                                   sound; rate = 11025 | 22050 | 44100; blaster = A220 I5 D1 H5 */
            if (!stricmp(k, "mixer") && (!stricmp(v, "on") || !stricmp(v, "off")))
                bi.sound_flags = !stricmp(v, "off") ? bi.sound_flags | BI_SOUND_OFF : bi.sound_flags & ~BI_SOUND_OFF;
            else if (!stricmp(k, "rate") && (atol(v) == 11025 || atol(v) == 22050 || atol(v) == 44100))
                bi.sound_rate = (unsigned long)atol(v);
            else if (!stricmp(k, "blaster") && strlen(v) < sizeof cfg_blaster)
                strcpy(cfg_blaster, v);
            else if (!stricmp(k, "mixer") || !stricmp(k, "rate") || !stricmp(k, "blaster"))
                say("GLOS-WARN cfg-key section=sound key=%s", k);
            continue;                           /* (crystal: the kernel's, crystal.c) */
        }
        if (in_cache) {                         /* enabled = on | off | verify | count; size = auto | MB (the disk
                                                   cache, docs/disk-cache.md: the block buffer is kept at start) */
            if (!stricmp(k, "enabled") && cache_word(v) >= 0)
                bi.cache_mode = (unsigned long)cache_word(v);
            else if (!stricmp(k, "size"))
                bi.cache_size_mb = !stricmp(v, "auto") ? 0 : (unsigned long)atol(v);
            else if (!stricmp(k, "driver") && (!stricmp(v, "bios") || !stricmp(v, "glos") || !stricmp(v, "glos-read")))
                bi.cache_driver = !stricmp(v, "glos") ? 1 : !stricmp(v, "glos-read") ? 2 : 0;   /* (N1c) */
            else
                say("GLOS-WARN cfg-key section=cache key=%s", k);
            continue;
        }
        if (in_devices) {                       /* off = the devices GLOS's drivers leave to DOS (the Devices page;
                                                   the kernel's devlist.c reads the words) */
            if (!stricmp(k, "off") && strlen(v) < sizeof bi.dev_off)
                strcpy(bi.dev_off, v);
            continue;
        }
        if (in_memory) {                        /* stub_page = off | auto | SEGMENT: the stub in a page of upper
                                                   memory (docs/upper-memory.md); checked by the schema above */
            if (!stricmp(k, "stub_page"))
                bi.stub_page = (unsigned long)cfg_page(v);
            else
                say("GLOS-WARN cfg-key section=memory key=%s", k);
            continue;
        }
        if (in_net) {                           /* hostname = NAME: for mDNS (NAME.local) and DHCP */
            if (!stricmp(k, "hostname") && strlen(v) < sizeof bi.hostname
                && strspn(v, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-") == strlen(v)
                && *v && *v != '-')
                strcpy(bi.hostname, v);
            else if (!stricmp(k, "cable")) {    /* cable = off | server COMn [BAUD] | client COMn [BAUD]: IP over
                                                   a serial cable (docs/cable.md) */
                char role[8], port[8];
                unsigned long baud = 0;
                int n = sscanf(v, "%7s %7s %lu", role, port, &baud);
                if (n >= 2 && (!stricmp(role, "server") || !stricmp(role, "client")) && !strnicmp(port, "com", 3)
                    && port[3] >= '1' && port[3] <= '4' && !port[4] && (!baud || 115200UL % baud == 0)) {
                    bi.cable_role = (role[0] | 32) == 's' ? BI_CABLE_SERVER : BI_CABLE_CLIENT;
                    bi.cable_com = (unsigned long)(port[3] - '0');
                    bi.cable_baud = baud;
                } else if (n == 2 && (!stricmp(role, "server") || !stricmp(role, "client")) && !strnicmp(port, "lpt", 3)
                           && port[3] >= '1' && port[3] <= '3' && !port[4]) {
                    bi.cable_role = (role[0] | 32) == 's' ? BI_CABLE_SERVER : BI_CABLE_CLIENT;
                    bi.cable_com = 16UL + (unsigned long)(port[3] - '0');   /* (a parallel cable: LPT1 to LPT3) */
                    bi.cable_baud = 0;
                } else if (stricmp(v, "off"))
                    say("GLOS-WARN cfg-value section=net key=cable");
            } else if (!stricmp(k, "card")) {   /* card = auto | off: off, no network card is looked for (a PC
                                                   that is to be on the network by its cable link alone) */
                if (!stricmp(v, "off"))
                    bi.flags |= BI_F_NOCARD;
            } else if (stricmp(k, "cable_share") && stricmp(k, "cable_join"))   /* (those two are the kernel's) */
                say("GLOS-WARN cfg-key section=net key=%s", k);
            continue;
        }
        if (prof) {
            if (!stricmp(k, "direct")) prof->flags = atoi(v) ? prof->flags | BI_PROF_DIRECT
                                                             : prof->flags & ~BI_PROF_DIRECT;
            else if (!stricmp(k, "memory")) prof->memory_kb = strtoul(v, NULL, 10);
            else if (!stricmp(k, "env")) profile_env(prof, v);
            /* (the rest are the kernel's and the desktop's: M9, checked against the schema above) */
            continue;
        }
        if (!stricmp(k, "comspec")) opt_str(bi.comspec, sizeof bi.comspec, v);
        else if (!stricmp(k, "command")) cmd_dos = !stricmp(v, "dos");
        else if (!stricmp(k, "autoexec")) opt_str(bi.autoexec, sizeof bi.autoexec, v), shell_p = 1;
        else if (!stricmp(k, "console")) opt_str(bi.console, sizeof bi.console, v);
        else if (!stricmp(k, "envsize")) envsize = (unsigned)atoi(v), shell_e = 1;
        else if (!stricmp(k, "options"))                /* e.g. /DPMITRACE /NOVME */
            for (v = strtok(v, " \t"); v; v = strtok(NULL, " \t"))
                flag_arg(v);
    }
    fclose(f);
}

/* Is the COMMAND.COM at path GLOS's own (apps/command/command.asm; docs/v1/shell.md, S4c)? It has the four
   letters of its call to the kernel in it. */
#ifndef NATIVE
static int own_cmd(const char *path)
{
    static char head[1536];
    size_t n, i;
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    n = fread(head, 1, sizeof head, f);
    fclose(f);
    for (i = 0; i + 4 <= n; i++)
        if (!memcmp(head + i, "GCMD", 4))
            return 1;
    return 0;
}

/* The DOS's COMMAND.COM: COMSPEC as given, in the environment, on the boot drive (root,
   \\FREEDOS\\BIN, \\DOS), next to GLOS.EXE or along PATH. Never GLOS's own, which runs under GLOS only. */
static void find_comspec(const char *argv0)
{
    static char p[_MAX_PATH];                           /* _searchenv() fills _MAX_PATH bytes */
    union REGS r;
    const char *c;
    int i;

    r.x.ax = 0x3305;                                    /* the boot drive */
    intdos(&r, &r);
    if (!bi.comspec[0] && (c = getenv("COMSPEC")) != NULL && !own_cmd(c))
        opt_str(bi.comspec, sizeof bi.comspec, c);
    for (i = 0; i < 4 && !bi.comspec[0]; i++) {
        static const char *const dirs[] = { "\\", "\\FREEDOS\\BIN\\", "\\DOS\\" };
        if (i < 3) {
            sprintf(p, "%c:%sCOMMAND.COM", 'A' + r.h.dl - 1, dirs[i]);
        } else {
            kernel_path(p, argv0);
            strcpy(strrchr(p, '\\') ? strrchr(p, '\\') + 1 : p, "COMMAND.COM");
        }
        if (access(p, 0) == 0 && !own_cmd(p))
            opt_str(bi.comspec, sizeof bi.comspec, p);
    }
    if (!bi.comspec[0]) {
        _searchenv("COMMAND.COM", "PATH", p);
        opt_str(bi.comspec, sizeof bi.comspec, p[0] ? p : "COMMAND.COM");
    }
}

/* Is GLOS the shell, and with what? Options override GLOS.CFG; COMSPEC as
   find_comspec() finds it (the agent needs it too); AUTOEXEC.BAT as given or
   in the boot drive's root. Keep the SHELL= line short: FreeDOS ignores one much
   longer than 64 characters, so settings belong in GLOS.CFG. */
static void shell_setup(int argc, char **argv)
{
    union REGS r;
    int i;

    {                                                   /* GLOS's own COMMAND.COM beside GLOS.EXE: the kernel */
        static char own[_MAX_PATH];                     /* gives programs that one (S4c) */
        kernel_path(own, argv[0]);
        strcpy(strrchr(own, '\\') ? strrchr(own, '\\') + 1 : own, "COMMAND.COM");
        if (own_cmd(own))
            bi.flags |= BI_F_OWNCMD;
    }
    shell = *(unsigned far *)MK_FP(_psp, 0x16) == _psp;        /* DOS made us our own parent */
    for (i = 1; i < argc; i++)
        if (!stricmp(argv[i], "/SHELL"))
            shell = 1;
    read_cfg(argv[0], shell);
    if (!shell) {
        find_comspec(argv[0]);                          /* the agent runs commands through it */
        return;
    }
    for (i = 1; i < argc; i++) {
        if (!strnicmp(argv[i], "/COMSPEC=", 9)) opt_str(bi.comspec, sizeof bi.comspec, argv[i] + 9);
        else if (!strnicmp(argv[i], "/P=", 3)) opt_str(bi.autoexec, sizeof bi.autoexec, argv[i] + 3), shell_p = 1;
        else if (!strnicmp(argv[i], "/E:", 3) || !strnicmp(argv[i], "/E=", 3))
            envsize = (unsigned)atoi(argv[i] + 3), shell_e = 1;
        else if (!strnicmp(argv[i], "/CON=", 5)) opt_str(bi.console, sizeof bi.console, argv[i] + 5);
    }
    if (envsize < 256) envsize = 256;
    if (envsize > 32768U) envsize = 32768U;
    find_comspec(argv[0]);
    r.x.ax = 0x3305;                                    /* the boot drive */
    intdos(&r, &r);
    if (!bi.autoexec[0])
        sprintf(bi.autoexec, "%c:\\AUTOEXEC.BAT", 'A' + r.h.dl - 1);
    if (access(bi.autoexec, 0) != 0)
        bi.autoexec[0] = 0;
    /* With GLOS's own COMMAND.COM beside GLOS.EXE the shell is GLOS's (S4d): it runs AUTOEXEC.BAT itself and
       the prompt is its own, with no COMMAND.COM of the DOS's under it. [shell] command = dos: as before. */
    own_shell = (bi.flags & BI_F_OWNCMD) && !cmd_dos;
}

/* The master environment for the stub to install: ours, with COMSPEC. */
static void stage_env(void)
{
    unsigned seg = *(unsigned far *)MK_FP(_psp, 0x2C), n = 0, len;
    char far *e = (char far *)MK_FP(seg, 0);

    while (seg && *e) {
        len = _fstrlen(e);
        if (_fstrnicmp(e, "COMSPEC=", 8) && n + len + 1 < sizeof envbuf - sizeof bi.comspec - 16) {
            _fmemcpy(envbuf + n, e, len + 1);
            n += len + 1;
        }
        e += len + 1;
    }
    n += sprintf(envbuf + n, "COMSPEC=%s", bi.comspec) + 1;
    envbuf[n++] = 0;                                    /* the end of the strings */
    envbuf[n++] = 0;                                    /* and no program name after them */
    envbuf[n++] = 0;
    stub_data.env_src = far_addr(envbuf);
    stub_data.env_len = n;
    stub_data.env_paras = ((envsize > n ? envsize : n) + 15) >> 4;
}

static void set_tail(unsigned char __far *t, const char *text)
{
    unsigned n = strlen(text);
    t[0] = (unsigned char)n;
    _fmemcpy(t + 1, text, n);
    t[n + 1] = 0x0D;
}

/* GLOS couldn't start (or left): the stub runs COMSPEC as the permanent
   shell, from now on, without the kernel. */
static void shell_fallback(void)
{
    char tail[64];

    if (tab_seg) _dos_freemem(tab_seg);
    if (buf_seg) _dos_freemem(buf_seg);
    tab_seg = buf_seg = 0;
    if (n_xms) {
        xms_call(0x0600, 0);
        xms_release();
    } else if (bi.mode == BI_MODE_RAW && bi.a20_initial == 0 && a20_on()) {
        a20_set(0);
    }
    strcpy(tail, shell_p && bi.autoexec[0] ? " /P=" : " /P");
    if (shell_p && bi.autoexec[0])
        strcat(tail, bi.autoexec);
    if (shell_e)
        sprintf(tail + strlen(tail), " /E:%u", envsize);
    say("GLOS-SHELL fallback=%s%s", bi.comspec, tail);  /* (FreeDOS's kernel alone on the disk: the stub says so
                                                           and stops) */
    stub_data.mode = 0;
    stub_data.nxms = 0;
    stub_data.shell = 1;
    stub_data.realmode = 1;
    _fstrcpy(stub_data.path, bi.comspec);
    set_tail(stub_data.tail, tail);
    stage_env();
    if (FP_SEG(&stub_data) >= _psp + 0x10 + ((stub_off(stub_end) + 15) >> 4))
        stub_resident();
    for (;;)                                            /* last resort: COMSPEC under all of GLOS.EXE */
        spawnl(P_WAIT, bi.comspec, bi.comspec, "/P", NULL);
}
#endif

#ifdef NATIVE
extern unsigned native_psp;
extern unsigned char native_disk;
int native_mount(void);
unsigned long native_vol_lba(void);

/* ---- the start-up menu (docs/v1/native-dos.md, the boot manager)

   A hard disk GLOS started from may hold other systems: Windows 98 on the partition before GLOS's, for one. The
   first sector's code starts GLOS's partition whichever is active (kernel/disk/mbrboot.S), so this menu is always
   passed: each other primary partition whose first sector ends 55AAh is offered with GLOS, and GLOS starts after
   five seconds if no key chooses. Another system is started as the first sector's code would have: its partition
   made the active one if it isn't (Windows and DOS give their letters by it: two bytes of the table, the rest as
   it was), its first sector at 0000:7C00h, the table at 0000:07BEh with DS:SI at its entry, the disk in DL. The
   loader lies over 7C00h itself: a few bytes of code at 8000:0000h, with the sector and the table beside them,
   do the last step. */
int native_sector(unsigned long lba, void *buf);
int native_sector_out(unsigned long lba, void *buf);
extern void __cdecl native_far8000(void);       /* (nstart.asm) */

/* 1 if the 11 bytes of a file's name as a folder has them are somewhere in a boot sector: the file it loads. */
static int loads(const unsigned char *sector, const char *name)
{
    unsigned i;

    for (i = 0; i + 11 <= 510; i++)
        if (!memcmp(sector + i, name, 11))
            return 1;
    return 0;
}

static void native_chain(unsigned char *table, unsigned entry, const unsigned char *sector)
{
    static unsigned char stub[] = {
        0xFA,                                   /* cli */
        0x31, 0xC0,                             /* xor ax, ax */
        0x8E, 0xD0,                             /* mov ss, ax */
        0xBC, 0x00, 0x7C,                       /* mov sp, 7C00h */
        0x8E, 0xC0,                             /* mov es, ax */
        0x0E, 0x1F,                             /* push cs; pop ds */
        0xFC,                                   /* cld */
        0xBE, 0x00, 0x01,                       /* mov si, 0100h: the sector */
        0xBF, 0x00, 0x7C,                       /* mov di, 7C00h */
        0xB9, 0x00, 0x01,                       /* mov cx, 256 */
        0xF3, 0xA5,                             /* rep movsw */
        0xBE, 0x00, 0x03,                       /* mov si, 0300h: the table */
        0xBF, 0xBE, 0x07,                       /* mov di, 07BEh */
        0xB9, 0x20, 0x00,                       /* mov cx, 32 */
        0xF3, 0xA5,                             /* rep movsw */
        0x8E, 0xD8,                             /* mov ds, ax */
        0xBE, 0xBE, 0x07,                       /* mov si, 07BEh + 16 * entry (set below) */
        0x89, 0xF5,                             /* mov bp, si */
        0xB2, 0x80,                             /* mov dl, the disk (set below) */
        0xFB,                                   /* sti */
        0xEA, 0x00, 0x7C, 0x00, 0x00            /* jmp 0000:7C00h */
    };
    unsigned char __far *to = (unsigned char __far *)MK_FP(0x8000, 0);
    unsigned at = 0x07BE + 16 * entry;

    stub[38] = (unsigned char)at, stub[39] = (unsigned char)(at >> 8);
    stub[43] = native_disk;
    _fmemcpy(to, stub, sizeof stub);
    _fmemcpy(to + 0x100, sector, 512);
    _fmemcpy(to + 0x300, table, 64);
    native_far8000();
}

/* The system a volume's boot sector starts, by the sector's own words. */
static const char *system_name(const unsigned char *bs)
{
    return !memcmp(bs + 3, "MSWIN4", 6) ? "Windows 95/98"
           : !memcmp(bs + 3, "MSDOS", 5) || !memcmp(bs + 3, "IBM ", 4) ? "MS-DOS"
           : !memcmp(bs + 3, "FRDOS", 5) || !memcmp(bs + 3, "FreeDOS", 7) || loads(bs, "KERNEL  SYS") ? "FreeDOS"
           : loads(bs, "GLOSLDR SYS") ? "GLOS (another)"
           : !memcmp(bs + 3, "NTFS", 4) ? "Windows NT" : "another system";
}

/* A sector of one of the BIOS's hard disks (ctx: its bi_disk), for the volumes' letters: by the LBA call where
   the BIOS has it for that disk, else by cylinder, head and sector. */
static int disk_sector(void *ctx, unsigned long lba, unsigned char *sector)
{
    static struct { unsigned char size, zero; unsigned n, off, seg; unsigned long lo, hi; } dap;
    struct bi_disk *d = ctx;
    union REGS r;
    struct SREGS sr;
    int tries;

    for (tries = 0; tries < 3; tries++) {
        segread(&sr);
        memset(&r, 0, sizeof r);
        if (d->flags & BI_DISK_EDD) {
            dap.size = 16, dap.zero = 0, dap.n = 1, dap.off = (unsigned)sector, dap.seg = sr.ds;
            dap.lo = lba, dap.hi = 0;
            r.h.ah = 0x42;
            r.x.si = (unsigned)&dap;
        } else {
            unsigned long track = d->heads * d->spt, c, t;

            if (!track || (c = lba / track) > 1023)
                return -1;
            t = lba % track;
            r.x.ax = 0x0201;
            r.h.ch = (unsigned char)c;
            r.h.cl = (unsigned char)(((c >> 2) & 0xC0) | (t % d->spt + 1));
            r.h.dh = (unsigned char)(t / d->spt);
            sr.es = sr.ds;
            r.x.bx = (unsigned)sector;
        }
        r.h.dl = (unsigned char)d->dl;
        int86x(0x13, &r, &r, &sr);
        if (!r.x.cflag)
            return 0;
        memset(&r, 0, sizeof r);
        r.h.dl = (unsigned char)d->dl;
        int86(0x13, &r, &r);
    }
    return -1;
}

/* The drives of a native start (bootinfo's vols): every FAT volume of the BIOS's hard disks, lettered in
   MS-DOS's order (kernel/disk/letters.c), a DOS's or a Windows's own volume told by its boot sector. The letter
   of the volume GLOS started from: a floppy's (or a USB stick's that the BIOS starts as one) is A:. */
static char native_letters(void)
{
    static struct let_vol v[BI_VOLS];
    static unsigned char bs[512];
    int n = 0, i, own = -1;

    disks_probe();
    for (i = 0; i < (int)bi.n_disks; i++)
        n = let_scan((unsigned)i, (bi.disks[i].flags & BI_DISK_PARAMS) && !bi.disks[i].sectors_hi ? bi.disks[i].sectors_lo : 0,
                     disk_sector, &bi.disks[i], v, n, BI_VOLS);
    for (i = 0; i < n; i++) {
        struct bi_disk *d = &bi.disks[v[i].disk];

        if (d->dl == native_disk && v[i].start == native_vol_lba())
            v[i].own = 1, own = i;
        else if (disk_sector(d, v[i].start, bs) == 0 && bs[510] == 0x55 && bs[511] == 0xAA
                 && (!memcmp(system_name(bs), "Windows 9", 9) || !memcmp(system_name(bs), "MS-DOS", 6)
                     || !memcmp(system_name(bs), "FreeDOS", 7)))
            v[i].dos = 1;
    }
    let_assign(v, n, 2);
    bi.n_vols = (unsigned long)n;
    {
        /* The floppy drives the BIOS has (INT 11h's equipment word), as A: and B:: each a removable volume the
           kernel looks at when it is used, but the one GLOS started from, which is its own. */
        union REGS e;
        unsigned k, drives;

        int86(0x11, &e, &e);
        drives = (e.x.ax & 1) ? ((e.x.ax >> 6) & 3) + 1 : 0;
        for (k = 0; k < drives && k < 2 && bi.n_vols < BI_VOLS; k++) {
            struct bi_vol *f = &bi.vols[bi.n_vols];

            if (native_disk == k)
                continue;
            memset(f, 0, sizeof *f);
            f->dl = k, f->letter = k, f->flags = BI_VOL_REMOVABLE;
            bi.n_vols++;
            say("GLOS-BOOT volume %c: dl=%02x removable", 'A' + k, k);
        }
    }
    for (i = 0; i < n; i++) {
        bi.vols[i].dl = bi.disks[v[i].disk].dl;
        bi.vols[i].letter = v[i].letter < 0 ? 0xFFFFFFFFUL : (unsigned long)v[i].letter;
        bi.vols[i].type = v[i].type;
        bi.vols[i].flags = (v[i].own ? BI_VOL_OWN : 0) | (v[i].dos ? BI_VOL_DOS : 0) | (v[i].active ? BI_VOL_ACTIVE : 0)
                           | (v[i].kind == LET_LOGICAL ? BI_VOL_LOGICAL : 0);
        bi.vols[i].start = v[i].start;
        bi.vols[i].sectors = v[i].sectors;
        say("GLOS-BOOT volume %c: dl=%02lx start=%lu sectors=%lu type=%02x%s%s%s", v[i].letter < 0 ? '-' : 'A' + v[i].letter,
            bi.vols[i].dl, v[i].start, v[i].sectors, v[i].type, v[i].own ? " own" : "", v[i].dos ? " dos" : "",
            v[i].active ? " active" : "");
    }
    if (own >= 0 && v[own].letter >= 0)
        return (char)('A' + v[own].letter);
    if (native_disk < 0x80)
        return (char)('A' + (native_disk & 1));
    /* (a hard disk's volume that its table doesn't show as a FAT one, or one too many: the letter after the
       last given) */
    for (i = 0, own = 2; i < n; i++)
        if (v[i].letter >= own)
            own = v[i].letter + 1;
    return (char)('A' + (own > 25 ? 25 : own));
}

static void native_menu(void)
{
    static unsigned char mbr[512], bs[4][512];
    static char names[4][28];
    unsigned entry[4], n = 0, i, k;
    unsigned long mb[4], __far *ticks = (unsigned long __far *)MK_FP(0x40, 0x6C), t0, left = 99;
    union REGS r;
    int choice = 0;

    if (native_disk < 0x80 || native_sector(0, mbr) != 0 || mbr[510] != 0x55 || mbr[511] != 0xAA)
        return;
    for (i = 0; i < 4; i++) {
        unsigned char *e = mbr + 446 + i * 16;
        unsigned long start = *(unsigned long *)(e + 8);

        if (!e[4] || e[4] == 0x05 || e[4] == 0x0F || e[4] == 0x85 || start == native_vol_lba())
            continue;                           /* (none, a box of logical drives, GLOS's own) */
        if (native_sector(start, bs[n]) != 0 || bs[n][510] != 0x55 || bs[n][511] != 0xAA)
            continue;                           /* (nothing to start there) */
        entry[n] = i;
        mb[n] = *(unsigned long *)(e + 12) / 2048;
        strcpy(names[n], system_name(bs[n]));
        n++;
    }
    if (!n)
        return;
    say("");
    say("This disk can start:");
    say("  1  GLOS");
    for (k = 0; k < n; k++)
        say("  %u  %s (partition %u, %lu MB)", k + 2, names[k], entry[k] + 1, mb[k]);
    say("GLOS-BOOT menu choices=%u", n + 1);
    t0 = *ticks;
    for (;;) {
        unsigned long passed = (*ticks - t0) / 18;      /* (18.2 ticks a second) */

        if (passed >= 5)
            break;
        if (5 - passed != left) {
            left = 5 - passed;
            say("GLOS starts in %lu... (press 1 to %u, or Enter for GLOS)", left, n + 1);
        }
        if (*(unsigned __far *)MK_FP(0x40, 0x1A) == *(unsigned __far *)MK_FP(0x40, 0x1C))
            continue;                           /* (the BIOS's keyboard buffer: empty) */
        r.h.ah = 0x00;
        int86(0x16, &r, &r);
        if (r.h.al == '\r' || r.h.al == '1')
            break;
        if (r.h.al >= '2' && r.h.al < '2' + n) {
            choice = r.h.al - '1';
            break;
        }
    }
    if (!choice) {
        say("GLOS-BOOT menu chosen=1 glos");
        return;
    }
    k = (unsigned)choice - 1;
    say("GLOS-BOOT menu chosen=%d partition=%u %s", choice + 1, entry[k] + 1, names[k]);
    if (mbr[446 + entry[k] * 16] != 0x80) {     /* its partition the active one */
        for (i = 0; i < 4; i++)
            mbr[446 + i * 16] = i == entry[k] ? 0x80 : 0;
        if (native_sector_out(0, mbr) != 0)
            say("GLOS-BOOT menu active-flag error (started all the same)");
        else
            say("GLOS-BOOT menu active=%u", entry[k] + 1);
    }
    native_chain(mbr + 446, entry[k], bs[k]);
}

/* How the BIOS reaches the disk GLOS started from, where it says: INT 13h 48h's interface type (EDD 3.0: the
   device path, its key BEDDh at 1Eh, the type's name at 28h). The kernel leaves USB to the BIOS when its drive
   C: is the BIOS's disk and this doesn't say it is something else (kernel/vm/vdos.c). */
static unsigned long native_if(void)
{
    static unsigned char p[0x4A];
    union REGS r;
    struct SREGS sr;

    if (native_disk < 0x80) {
        /* A drive of the floppies' numbers: a real floppy drive (08h's type 1 to 6, two heads, a floppy's
           sectors a track), or a USB stick a BIOS starts as one (the PCChips M925G's boot menu): not said. */
        struct bi_disk *d = &bi.boot_disk;
        segread(&sr);
        memset(&r, 0, sizeof r);
        sr.es = 0;
        r.h.ah = 0x08, r.h.dl = native_disk;
        int86x(0x13, &r, &r, &sr);
        return !r.x.cflag && r.h.bl >= 1 && r.h.bl <= 6 && d->heads == 2 && d->spt <= 36 ? BI_IF_OTHER : BI_IF_UNKNOWN;
    }
    memset(&r, 0, sizeof r);
    r.h.ah = 0x41, r.x.bx = 0x55AA, r.h.dl = native_disk;
    int86(0x13, &r, &r);
    if (r.x.cflag || r.x.bx != 0xAA55 || !(r.x.cx & 1))
        return BI_IF_UNKNOWN;
    memset(p, 0, sizeof p);
    *(unsigned *)p = 0x42;
    segread(&sr);
    memset(&r, 0, sizeof r);
    r.h.ah = 0x48, r.h.dl = native_disk;
    sr.ds = FP_SEG((void __far *)p);
    r.x.si = FP_OFF((void __far *)p);
    int86x(0x13, &r, &r, &sr);
    if (r.x.cflag || *(unsigned *)p < 0x42 || *(unsigned *)(p + 0x1E) != 0xBEDD)
        return BI_IF_UNKNOWN;
    if (!memcmp(p + 0x28, "USB", 3))
        return BI_IF_USB;
    if (!memcmp(p + 0x28, "ATA", 3))
        return BI_IF_ATA;
    return BI_IF_OTHER;
}

/* The disk GLOS started from as the BIOS describes it, for the kernel (bootinfo's boot_disk): 08h's geometry;
   a hard disk's extensions and size as disks_probe() asks them. */
static void native_boot_disk(void)
{
    static unsigned char p48[30];
    struct bi_disk *d = &bi.boot_disk;
    union REGS r;
    struct SREGS sr;

    memset(d, 0, sizeof *d);
    d->dl = native_disk;
    d->bps = 512;
    segread(&sr);
    memset(&r, 0, sizeof r);
    sr.es = 0;
    r.h.ah = 0x08, r.h.dl = native_disk;
    int86x(0x13, &r, &r, &sr);
    if (!r.x.cflag && (r.h.cl & 63)) {
        d->cyls = (unsigned long)(r.h.ch | ((r.h.cl & 0xC0) << 2)) + 1;
        d->heads = (unsigned long)r.h.dh + 1;
        d->spt = r.h.cl & 63;
    }
    d->sectors_lo = d->cyls * d->heads * d->spt;
    if (native_disk >= 0x80) {
        memset(&r, 0, sizeof r);
        r.h.ah = 0x41, r.x.bx = 0x55AA, r.h.dl = native_disk;
        int86(0x13, &r, &r);
        if (!r.x.cflag && r.x.bx == 0xAA55 && (r.x.cx & 1)) {
            d->flags |= BI_DISK_EDD;
            memset(p48, 0, sizeof p48);
            *(unsigned *)p48 = 0x1A;
            segread(&sr);
            memset(&r, 0, sizeof r);
            r.h.ah = 0x48, r.h.dl = native_disk;
            sr.ds = FP_SEG((void __far *)p48);
            r.x.si = FP_OFF((void __far *)p48);
            int86x(0x13, &r, &r, &sr);
            if (!r.x.cflag && (*(unsigned long *)(p48 + 16) || *(unsigned long *)(p48 + 20))) {
                d->flags |= BI_DISK_PARAMS | ((*(unsigned *)(p48 + 2) & 4) ? BI_DISK_REMOVE : 0);
                d->sectors_lo = *(unsigned long *)(p48 + 16);
                d->sectors_hi = *(unsigned long *)(p48 + 20);
                d->bps = *(unsigned *)(p48 + 24);
            }
        }
    }
}

/* GLOSLDR.SYS's (nstart.asm calls it): GLOS's files in \GLOS on the volume it started from, as if GLOS.EXE were
   C:\GLOS\GLOS.EXE. */
void __cdecl native_main(void)
{
    static char self[80] = "C:\\GLOS\\GLOS.EXE", tail[128], env[224];
    static char *argv[2] = { self, 0 };
    unsigned char __far *t = (unsigned char __far *)MK_FP(native_psp, 0x80);
    unsigned n = t[0] < 127 ? t[0] : 126, i;
    char *w, letter;

    _psp = native_psp;
    outp(0x3FB, 0x80); outp(0x3F8, 1); outp(0x3F9, 0); outp(0x3FB, 0x03);
    outp(0x3FA, 0xC7); outp(0x3FC, 0x03);
    for (i = 0; i < n; i++)
        tail[i] = (char)t[1 + i];
    tail[n] = 0;
    say("GLOS-BOOT step=start build=" GLOS_BUILD " native=1 disk=%02x psp=%04x tail=%s", native_disk, native_psp, tail);
    if (native_mount() != 0) {
        refuse("no-volume");
        return;
    }
    native_menu();                              /* (another system chosen there never comes back) */
    bi.cache_mode = BI_CACHE_ON;
    bi.boot_dl = native_disk;
    bi.boot_lba = native_vol_lba();
    native_boot_disk();
    bi.boot_if = native_if();
    say("GLOS-BOOT step=disk dl=%02x if=%lu chs=%lu/%lu/%lu sectors=%lu flags=%lx volume=%lu", native_disk, bi.boot_if,
        bi.boot_disk.cyls, bi.boot_disk.heads, bi.boot_disk.spt, bi.boot_disk.sectors_lo, bi.boot_disk.flags,
        bi.boot_lba);
    /* Its command line (the prelude's CMDLINE): the folder GLOS's files are in (a word that begins with a
       backslash: \GLOS if none), where GLOS.EXE would be under DOS, and GLOS.EXE's flags. GLOS.CFG's [shell]
       section is read as when GLOS is DOS's shell: natively it is the shell. */
    for (w = strtok(tail, " \t"); w; w = strtok(NULL, " \t"))
        if (w[0] == '\\' && strlen(w) < sizeof self - 12)
            sprintf(self, "C:%s\\GLOS.EXE", strupr(w));
    /* The drives' letters, and this volume's in GLOS's own paths: it is C: only where MS-DOS would call it so. */
    letter = native_letters();
    self[0] = letter;
    bi.boot_drive = (unsigned long)(letter - 'A' + 1);
    say("GLOS-BOOT step=letters own=%c: volumes=%lu", letter, bi.n_vols);
    read_cfg(argv[0], 1);
    for (i = 0; i < n; i++)                     /* (strtok cut the tail up: the flags, a word at a time) */
        if (tail[i] == '/' && (i == 0 || !tail[i - 1]))
            flag_arg(tail + i);
    if (!(bi.flags & BI_F_ROUNDTRIP)) {
        /* The shell and the agent at once, as GLOS.EXE /SHELL with GLOS's own COMMAND.COM beside it (S4d): GLOS
           runs AUTOEXEC.BAT (the root's, or [shell] autoexec's) and then its prompt; COMSPEC is GLOS's own
           COMMAND.COM; the master environment is PATH and COMSPEC (there is no DOS's to copy). With nothing
           under GLOS its end is the PC's: the stub stops or restarts it (stub.asm, sd_shell 2). */
        kernel_path(env, argv[0]);
        strcpy(strrchr(env, '\\') + 1, "COMMAND.COM");
        if (!bi.comspec[0])                     /* ([shell] comspec = in GLOS.CFG first) */
            strcpy(bi.comspec, env);
        if (access(bi.comspec, 0) == 0)
            bi.flags |= BI_F_OWNCMD;
        if (!bi.autoexec[0]) {
            strcpy(bi.autoexec, "C:\\AUTOEXEC.BAT");
            bi.autoexec[0] = letter;
        }
        if (access(bi.autoexec, 0) != 0)
            bi.autoexec[0] = 0;
        shell = own_shell = 1;
        stub_data.shell = 2;
        kernel_path(env, argv[0]);
        *strrchr(env, '\\') = 0;
        memmove(env + 5, env, strlen(env) + 1);
        memcpy(env, "PATH=", 5);
        strcat(env, ";C:\\");
        env[strlen(env) - 3] = letter;
        n = strlen(env) + 1;
        n += (unsigned)sprintf(env + n, "COMSPEC=%s", bi.comspec) + 1;
        env[n++] = 0;                           /* (the strings' end, and no program's name after them) */
        env[n++] = 0;
        env[n++] = 0;
        if (envsize < 256) envsize = 256;
        if (envsize > 32768U) envsize = 32768U;
        stub_data.env_src = far_addr(env);
        stub_data.env_len = n;
        stub_data.env_paras = ((envsize > n ? envsize : n) + 15) >> 4;
        say("GLOS-BOOT step=shell comspec=%s autoexec=%s", bi.comspec, bi.autoexec);
    }
    say("GLOS-EXIT code=%d", glos_main(1, argv));
}
#else
int main(int argc, char **argv)
{
    int code;
    outp(0x3FB, 0x80); outp(0x3F8, 1); outp(0x3F9, 0); outp(0x3FB, 0x03);
    outp(0x3FA, 0xC7); outp(0x3FC, 0x03);
    say("GLOS-BOOT step=start build=" GLOS_BUILD);
    bi.cache_mode = BI_CACHE_ON;                /* the disk cache is on unless GLOS.CFG or /CACHE= says (D72) */
    shell_setup(argc, argv);
    if (shell) {
        char tail[64];
        say("GLOS-BOOT step=shell comspec=%s autoexec=%s console=%s", bi.comspec, bi.autoexec, bi.console);
        stub_data.shell = 1;
        _fstrcpy(stub_data.comspec, bi.comspec);
        strcpy(tail, shell_p && bi.autoexec[0] ? " /P=" : " /P");
        if (shell_p && bi.autoexec[0])
            strcat(tail, bi.autoexec);
        if (shell_e)
            sprintf(tail + strlen(tail), " /E:%u", envsize);
        set_tail(stub_data.fbtail, tail);
        stage_env();
    }
    code = glos_main(argc, argv);
    if (shell)
        shell_fallback();
    return code;
}
#endif
