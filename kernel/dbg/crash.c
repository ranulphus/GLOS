/* Crash reports (supervisor.md §19; M4b). A DPMI client the host must end
 * (an exception it has no handler for, a frame it can't go back to,
 * exceptions nested too deep) is reported in GLOS-CRASH lines on COM1 and
 * in C:\GLOS\CRASH\CRASHnnn.TXT, then ended as a kill ends a program: its
 * vectors and devices as it found them, exit code FFh.
 *
 * The report has what tools/symcrash.py needs to name the code: the
 * registers, the bytes at CS:EIP, the stack, the segments' bases and
 * limits, and the program's path. A DJGPP image's addresses are offsets in
 * its CS (whose base is the block it was loaded into); a flat Watcom
 * image's are linear, CS's base being 0.
 *
 * The file is written by the program's own DOS calls, nested from here,
 * unless DOS was busy (InDOS) when the crash came: then the log has all of
 * it. While the report is written the client gets no more interrupts. */
#include "glos/bootinfo.h"
#include "arch.h"
#include "dpmi.h"
#include "kprintf.h"
#include "vm.h"

static char text[2048];
static u32 tlen;

/* One line: to COM1 as GLOS-CRASH, and to the file. */
static void line(const char *fmt, ...)
{
    char b[200];
    u32 n;
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    kvsnprintf(b, sizeof b, fmt, ap);
    __builtin_va_end(ap);
    kprintf("GLOS-CRASH %s\n", b);
    n = strlen(b);
    if (tlen + n + 2 < sizeof text) {
        memcpy(text + tlen, b, n);
        text[tlen + n] = '\r';
        text[tlen + n + 1] = '\n';
        tlen += n + 2;
    }
}

/* The program's path: after its environment's strings and their count. */
static void prog_path(char *out, u32 n)
{
    u32 a, end, i = 0;
    u8 c;
    out[0] = 0;
    if (!dctx->env_seg)
        return;
    a = dctx->env_seg * 16u;
    end = a + 0x8000;
    while (a + 1 < end && (vm_rd8(a) || vm_rd8(a + 1)))
        a++;
    if (a + 1 >= end || vm_rd16(a + 2) == 0)
        return;
    for (a += 4; i + 1 < n && (c = vm_rd8(a + i)) >= ' ' && c < 0x7F; i++)
        out[i] = (char)c;
    out[i] = 0;
}

static void hex(char *out, u32 n, const u8 *b, u32 k)
{
    static const char d[] = "0123456789abcdef";
    u32 i;
    for (i = 0; i < k && 3 * i + 3 < n; i++) {
        out[3 * i] = d[b[i] >> 4];
        out[3 * i + 1] = d[b[i] & 15];
        out[3 * i + 2] = ' ';
    }
    out[i ? 3 * i - 1 : 0] = 0;
}

static void seg(const char *name, u32 sel)
{
    sel &= 0xFFFF;
    if (sel < 4)
        line("seg %s=%04x null", name, sel);
    else
        line("seg %s=%04x base=%08x limit=%08x", name, sel, sel_base((u16)sel), sel_limit((u16)sel));
}

/* ---- the file */

static int dos(struct trapframe *tf, struct rmregs *r)
{
    r->flags = 2;
    if (rm_call(tf, r, RM_INT, 0x21, 0, 0) != 0)
        return -1;
    return (r->flags & FL_CF) ? -1 : 0;
}

static void put_str(u32 lin, const char *s)
{
    do
        vm_wr8(lin++, (u8)*s);
    while (*s++);
}

/* 0, with the file's name in name; -1 with why there. */
static int write_file(struct trapframe *tf, char *name, u32 n)
{
    struct rmregs r;
    u32 lin, i, k, handle;
    u16 seg;

    if (vm.indos && vm_rd8(vm.indos)) {
        ksnprintf(name, n, "none why=indos");
        return -1;
    }
    memset(&r, 0, sizeof r);
    r.eax = 0x4800;
    r.ebx = (64 + tlen + 15) >> 4;
    if (dos(tf, &r) != 0) {
        ksnprintf(name, n, "none why=memory");
        return -1;
    }
    seg = (u16)r.eax;
    lin = seg * 16u;
    for (i = 0; i < tlen; i++)
        vm_wr8(lin + 64 + i, (u8)text[i]);
    put_str(lin, "C:\\GLOS");
    memset(&r, 0, sizeof r);
    r.eax = 0x3900;                             /* the directories may be there already */
    r.ds = seg;
    dos(tf, &r);
    put_str(lin, "C:\\GLOS\\CRASH");
    memset(&r, 0, sizeof r);
    r.eax = 0x3900;
    r.ds = seg;
    dos(tf, &r);
    for (k = 0; k < 1000; k++) {                /* the first free number */
        ksnprintf(name, n, "C:\\GLOS\\CRASH\\CRASH%03u.TXT", k);
        put_str(lin, name);
        memset(&r, 0, sizeof r);
        r.eax = 0x5B00;                         /* create a new file: fails with 50h if it exists */
        r.ds = seg;
        if (dos(tf, &r) == 0)
            break;
        if ((r.eax & 0xFFFF) != 0x50) {
            k = 1000;
            break;
        }
    }
    if (k < 1000) {
        handle = r.eax & 0xFFFF;
        memset(&r, 0, sizeof r);
        r.eax = 0x4000;
        r.ebx = handle;
        r.ecx = tlen;
        r.edx = 64;
        r.ds = seg;
        dos(tf, &r);
        memset(&r, 0, sizeof r);
        r.eax = 0x3E00;
        r.ebx = handle;
        dos(tf, &r);
    } else {
        ksnprintf(name, n, "none why=create");
    }
    memset(&r, 0, sizeof r);
    r.eax = 0x4900;
    r.es = seg;
    dos(tf, &r);
    return k < 1000 ? 0 : -1;
}

/* ---- the report */

void crash_report(struct trapframe *tf, const char *why)
{
    char path[80], b[140], name[48];
    u8 code[16];
    u32 st[12], sp, i, n, wide = dctx->bits32;

    dctx->ending = 1;
    tlen = 0;
    prog_path(path, sizeof path);
    line("why=%s vec=%02x err=%04x prog=%s psp=%04x bits=%u mode=%s", why, tf->vec & 0xFF, tf->err & 0xFFFF,
         path[0] ? path : "?", dctx->psp, wide ? 32 : 16, (tf->eflags & FL_VM) ? "v86" : "pm");
    line("cs:eip=%04x:%08x ss:esp=%04x:%08x eflags=%08x cr2=%08x", tf->cs & 0xFFFF, tf->eip, tf->ss & 0xFFFF,
         tf->esp, (tf->eflags & ~(FL_IF | FL_VIF | FL_VIP)) | (vm.vif ? FL_IF : 0), tf->vec == 14 ? dctx->cr2 : 0);
    line("eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x ebp=%08x", tf->eax, tf->ebx, tf->ecx, tf->edx,
         tf->esi, tf->edi, tf->ebp);
    for (n = 16; n && user_rd((u16)tf->cs, tf->eip, code, n) != 0; n--) ;
    hex(b, sizeof b, code, n);
    line("code=%s", n ? b : "?");
    sp = sp_of(tf);
    b[0] = 0;
    if (wide) {
        for (n = 12; n && user_rd((u16)tf->ss, sp, st, n * 4) != 0; n--) ;
        for (i = 0; i < n; i++)
            ksnprintf(b + strlen(b), sizeof b - strlen(b), i ? " %08x" : "%08x", st[i]);
    } else {
        u16 w[12];
        for (n = 12; n && user_rd((u16)tf->ss, sp, w, n * 2) != 0; n--) ;
        for (i = 0; i < n; i++)
            ksnprintf(b + strlen(b), sizeof b - strlen(b), i ? " %04x" : "%04x", w[i]);
    }
    line("stack=%s", n ? b : "?");
    seg("cs", tf->cs);
    seg("ds", tf->ds);
    seg("es", tf->es);
    seg("ss", tf->ss);
    line("handlers=%u lstack=%u nesting=%u", dctx->npe, dctx->lstack_use, rm_nesting());
    write_file(tf, name, sizeof name);
    kprintf("GLOS-CRASH file=%s\n", name);
    vm.kill_reason = "crash";
    dpmi_end(tf, 0xFF, 1);
}
