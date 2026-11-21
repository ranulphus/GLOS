/* exe_test: kernel/ndos/exe.c, .COM and .EXE files as DOS loads them (RBIL INT 21h 4Bh and table 01594): the
 * image's size from the pages and the last page's bytes, memory needed and wanted, loading high, relocations,
 * the registers a program starts with. "exe_test: 0 failures" when all held. */
#include <stdio.h>
#include <string.h>

#include "../../kernel/ndos/exe.h"

static int fails;

#define CHECK(cond, ...)                                                                                       \
    do {                                                                                                       \
        if (!(cond)) {                                                                                         \
            printf("FAIL %s:%d: ", __func__, __LINE__);                                                        \
            printf(__VA_ARGS__);                                                                               \
            printf("\n");                                                                                      \
            fails++;                                                                                           \
        }                                                                                                      \
    } while (0)

static void w16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

/* An .EXE header: signature, last page's bytes, pages, relocations, header paragraphs, min, max, SS, SP, IP, CS,
   relocation table offset. */
static void header(unsigned char *h, const char *sig, unsigned last, unsigned pages, unsigned nrel, unsigned hdr,
                   unsigned min, unsigned max, unsigned ss, unsigned sp, unsigned ip, unsigned cs, unsigned reloff)
{
    memset(h, 0, 64);
    h[0] = (unsigned char)sig[0];
    h[1] = (unsigned char)sig[1];
    w16(h + 2, last);
    w16(h + 4, pages);
    w16(h + 6, nrel);
    w16(h + 8, hdr);
    w16(h + 10, min);
    w16(h + 12, max);
    w16(h + 14, ss);
    w16(h + 16, sp);
    w16(h + 20, ip);
    w16(h + 22, cs);
    w16(h + 24, reloff);
}

static void t_com(void)
{
    unsigned char h[64] = { 0xB8, 0x00, 0x4C, 0xCD, 0x21 };     /* mov ax,4C00h; int 21h */
    struct dos_exe x;
    struct dos_regs0 r;
    unsigned long need, want;

    CHECK(dos_exe_header(h, 5, 1000, &x) == 0 && !x.exe && x.image_len == 1000, "a .COM");
    dos_exe_memory(&x, 1000, &need, &want);
    CHECK(need == 0x10 + 63 && want == 0xFFFF, "memory: %lx %lx", need, want);
    dos_exe_regs(&x, 0x1234, 0x9000, 1, 0, &r);
    CHECK(r.cs == 0x1234 && r.ds == 0x1234 && r.es == 0x1234 && r.ss == 0x1234 && r.ip == 0x100 &&
          r.sp == 0xFFFE && r.push0 && r.ax == 0xFF00, "registers: cs %x ip %x sp %x ax %x", r.cs, r.ip, r.sp, r.ax);
    dos_exe_regs(&x, 0x1234, 0x800, 0, 0, &r);
    CHECK(r.sp == 0x7FFE && r.ax == 0xFFFF, "a small block: sp %x ax %x", r.sp, r.ax);
    CHECK(dos_exe_header(h, 1, 1, &x) == 0 && !x.exe, "a 1-byte file");
}

static void t_exe(void)
{
    unsigned char h[64];
    struct dos_exe x;
    struct dos_regs0 r;
    unsigned long need, want;

    /* 3 pages, 80 bytes in the last: 1,104 bytes; a 2-paragraph header: a 1,072-byte image, 67 paragraphs. */
    header(h, "MZ", 80, 3, 2, 2, 0x10, 0xFFFF, 0x40, 0x200, 0x10, 0x01, 0x1C);
    CHECK(dos_exe_header(h, 64, 2000, &x) == 0 && x.exe && x.image_off == 32 && x.image_len == 1072 &&
          x.image_paras == 67 && x.nrel == 2 && x.rel_off == 0x1C && !x.high, "header: %d %lu %lu %lu", x.exe,
          x.image_off, x.image_len, x.image_paras);
    dos_exe_memory(&x, 2000, &need, &want);
    CHECK(need == 0x10 + 67 + 0x10 && want == 0xFFFF, "memory: %lx %lx", need, want);
    CHECK(dos_exe_image_seg(&x, 0x2000, 0x1000) == 0x2010, "image segment");
    dos_exe_regs(&x, 0x2000, 0x1000, 1, 1, &r);
    CHECK(r.cs == 0x2011 && r.ip == 0x10 && r.ss == 0x2050 && r.sp == 0x200 && r.ds == 0x2000 && r.es == 0x2000 &&
          !r.push0 && r.ax == 0, "registers: cs %x ip %x ss %x sp %x", r.cs, r.ip, r.ss, r.sp);

    /* A modest maximum: wanted is image + max; never less than needed. */
    header(h, "MZ", 80, 3, 0, 2, 0x10, 0x20, 0, 0, 0, 0, 0x1C);
    dos_exe_header(h, 64, 2000, &x);
    dos_exe_memory(&x, 2000, &need, &want);
    CHECK(want == 0x10 + 67 + 0x20, "want %lx", want);
    header(h, "MZ", 80, 3, 0, 2, 0x30, 0x20, 0, 0, 0, 0, 0x1C);
    dos_exe_header(h, 64, 2000, &x);
    dos_exe_memory(&x, 2000, &need, &want);
    CHECK(want == need, "max below min: want %lx need %lx", want, need);

    /* Last page 0: every page full. ZM signed. */
    header(h, "ZM", 0, 4, 0, 4, 0, 1, 0, 0, 0, 0, 0x1C);
    CHECK(dos_exe_header(h, 64, 4096, &x) == 0 && x.exe && x.image_len == 4 * 512 - 64, "ZM, last 0: %lu",
          x.image_len);

    /* Min and max both 0: loaded high, at the top of the block. */
    header(h, "MZ", 80, 3, 0, 2, 0, 0, 0x40, 0x200, 0, 0, 0x1C);
    dos_exe_header(h, 64, 2000, &x);
    CHECK(x.high, "high");
    dos_exe_memory(&x, 2000, &need, &want);
    CHECK(want == 0xFFFF, "high wants all: %lx", want);
    CHECK(dos_exe_image_seg(&x, 0x2000, 0x1000) == 0x2000 + 0x1000 - 67, "high: image at %x",
          dos_exe_image_seg(&x, 0x2000, 0x1000));
    dos_exe_regs(&x, 0x2000, 0x1000, 1, 1, &r);
    CHECK(r.ss == 0x2000 + 0x1000 - 67 + 0x40, "high: ss %x", r.ss);

    /* Headers that make no sense. */
    header(h, "MZ", 80, 1, 0, 40, 0, 0, 0, 0, 0, 0, 0x1C);
    CHECK(dos_exe_header(h, 64, 2000, &x) == -1, "an image ending before it starts");
    header(h, "MZ", 80, 3, 100, 2, 0, 0, 0, 0, 0, 0, 0x1C);
    CHECK(dos_exe_header(h, 64, 300, &x) == -1, "relocations past the file");
    CHECK(dos_exe_header(h, 20, 2000, &x) == -1, "MZ in a file shorter than its header");
}

static void t_relocate(void)
{
    unsigned char mem[64], rel[16];
    unsigned n;

    memset(mem, 0, sizeof mem);
    w16(mem + 2, 0x1000);
    w16(mem + 16, 0xFFF0);
    w16(rel + 0, 2), w16(rel + 2, 0);           /* 0000:0002 */
    w16(rel + 4, 0), w16(rel + 6, 1);           /* 0001:0000 = byte 16 */
    w16(rel + 8, 0x30), w16(rel + 10, 1);       /* 0001:0030 = byte 64: past the memory */
    w16(rel + 12, 62), w16(rel + 14, 0);        /* the last word */
    n = dos_exe_relocate(mem, sizeof mem, rel, 4, 0x2345);
    CHECK(n == 3, "applied %u", n);
    CHECK(mem[2] == 0x45 && mem[3] == 0x33, "word at 2: %02x%02x", mem[3], mem[2]);
    CHECK(mem[16] == 0x35 && mem[17] == 0x23, "word at 16 wraps: %02x%02x", mem[17], mem[16]);
    CHECK(mem[62] == 0x45 && mem[63] == 0x23, "the last word");
}

int main(void)
{
    t_com();
    t_exe();
    t_relocate();
    printf("exe_test: %d failures\n", fails);
    return fails != 0;
}
