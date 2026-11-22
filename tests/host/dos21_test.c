/* dos21_test: kernel/ndos/dos21.c, GLOS's own INT 21h, with programs' calls simulated over a machine's memory and a
 * volume in memory: a .COM started (its PSP, environment, registers), console output, files made, written, read,
 * sought and closed, folders, searches, devices by name, memory, an .EXE run by EXEC (relocated, its handles
 * inherited, its end back into the parent with its code, the vectors it changed given back, its memory freed), an
 * overlay, date and time, and the answers for what isn't there. "dos21_test: 0 failures" when all held. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../kernel/disk/disk.h"
#include "../../kernel/ndos/dos21.h"
#include "../../kernel/ndos/dtime.h"

static int fails;
static unsigned char *disk, *mem;
static struct fsv_vol vol;
static struct dos_m m;
static char out[4096];
static unsigned outn;
static unsigned long now_secs;

#define SECTORS 65536UL
#define SIZE    0xA0000UL

static int rd(void *c, unsigned long lba, unsigned char *b, unsigned n) { (void)c; memcpy(b, disk + lba * 512, n * 512UL); return 0; }
static int wr(void *c, unsigned long lba, const unsigned char *b, unsigned n) { (void)c; memcpy(disk + lba * 512, b, n * 512UL); return 0; }
static void con(void *c, const char *s, unsigned n) { (void)c; memcpy(out + outn, s, n); outn += n; out[outn] = 0; }
static unsigned long now(void *c) { (void)c; return now_secs; }
static int no_more(void *c) { (void)c; return -1; }      /* (nothing else will be typed: a wait is told to end) */
static int idles;
static void idle(void *c) { (void)c; idles++; }
static void type(const char *s) { dos_type(&m, (const unsigned char *)s, (unsigned)strlen(s)); }

#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void file(const char *path, const void *data, unsigned long n)
{
    struct fsv_file f;

    fsv_create(&vol, path, 0, 1, FSV_RW, FSV_DENY_NONE, &f);
    fsv_write(&f, data, n);
    fsv_close(&f);
}

static unsigned long lin(unsigned seg, unsigned off) { return ((unsigned long)seg << 4) + off; }
static unsigned w(unsigned long a) { return mem[a] | mem[a + 1] << 8; }
static void put16(unsigned long a, unsigned v) { mem[a] = (unsigned char)v; mem[a + 1] = (unsigned char)(v >> 8); }

/* A call from the running program: AX and the rest as given; the registers after, its result. */
static int call(struct dos_regs86 *r, unsigned ax)
{
    r->ax = ax;
    return dos21(&m, r);
}

/* A string put in the program's memory at seg:off (for DS:DX). */
static void str(unsigned seg, unsigned off, const char *s) { memcpy(mem + lin(seg, off), s, strlen(s) + 1); }

/* An .EXE: a 32-byte header, a 16-byte image whose word at 1 is a segment (relocated), one relocation, min 10h
   paragraphs beyond it, max FFFFh, SS:SP 0001:0100, CS:IP 0000:0000. */
static unsigned long make_exe(unsigned char *b)
{
    unsigned long n = 32 + 16;

    memset(b, 0, n);
    b[0] = 'M', b[1] = 'Z';
    b[2] = (unsigned char)(n % 512), b[4] = 1;  /* (bytes in the last page; pages) */
    b[6] = 1;                                   /* relocations */
    b[8] = 2;                                   /* header paragraphs */
    b[10] = 0x10, b[12] = 0xFF, b[13] = 0xFF;   /* min, max */
    b[14] = 1, b[16] = 0x00, b[17] = 0x01;      /* SS 0001h, SP 0100h */
    b[24] = 28;                                 /* the relocation table at 28 */
    b[28] = 1, b[30] = 0;                       /* the word at 0000:0001 */
    b[32] = 0xB8, b[33] = 0x01, b[34] = 0x00;   /* MOV AX, seg (relative 0001h) */
    memcpy(b + 36, "EXEDATA", 7);
    return n;
}

int main(void)
{
    static const unsigned char com[] = { 0xB4, 0x09, 0xBA, 0x08, 0x01, 0xCD, 0x21, 0xC3, 'H', 'i', '$' };
    struct fat_geo g = { SECTORS, 0, 16, 63, 0x80, 1, "DOS21", 16 };
    struct fat_made fm;
    struct dos_regs86 r, saved;
    struct dos_date dd = { 2026, 11, 22, 0 };
    struct dos_time tt = { 12, 34, 56, 0 };
    unsigned char exe[64];
    unsigned psp, h, h2, seg, largest0, i;
    unsigned long env;
    int res;

    setvbuf(stdout, 0, _IONBF, 0);
    disk = calloc(SECTORS, 512);
    mem = calloc(SIZE, 1);
    if (!disk || !mem)
        return 2;
    fat_format(&g, wr, 0, &fm);
    fsv_mount(&vol, rd, wr, 0, 0, 437, 0);
    file("\\HELLO.COM", com, sizeof com);
    file("\\PROG.EXE", exe, make_exe(exe));
    file("\\DATA.TXT", "0123456789", 10);
    make_exe(exe);
    exe[8] = 0x40;                              /* (a header past the file's end) */
    file("\\BAD.EXE", exe, 48);
    fsv_mkdir(&vol, "\\SUB");
    for (i = 0; i < 256; i++) {                 /* (the vectors at their ARPLs, as ndm.c makes them) */
        put16(i * 4, i * 2);
        put16(i * 4 + 2, 0x0060);
    }
    dos_m_init(&m, mem, SIZE, &dos_layout_machine, &vol, 2, 437);
    m.out = con;
    m.now = now;
    m.wait = no_more;
    m.idle = idle;
    now_secs = dos_to_seconds(&dd, &tt);

    /* ---- a .COM started */
    res = dos_m_start(&m, "C:\\HELLO.COM", " a b", &r);
    psp = dos_cur_psp(&m);
    if (res) {
        printf("FAIL start %d\n", res);
        return 1;
    }
    CHECK(r.cs == psp && r.ds == psp && r.es == psp && r.ss == psp && r.ip == 0x100 && r.sp == 0xFFFE,
          "COM regs cs=%04x ds=%04x ss:sp=%04x:%04x ip=%04x psp=%04x", r.cs, r.ds, r.ss, r.sp, r.ip, psp);
    CHECK(!memcmp(mem + lin(psp, 0x100), com, sizeof com), "image");
    CHECK(mem[lin(psp, 0)] == 0xCD && mem[lin(psp, 1)] == 0x20, "PSP INT 20h");
    CHECK(mem[lin(psp, 0x80)] == 4 && !memcmp(mem + lin(psp, 0x81), " a b\r", 5), "tail");
    CHECK(w(lin(psp, 0x16)) == dos_layout_machine.root_psp, "parent %04x", w(lin(psp, 0x16)));
    CHECK(w(lin(r.ss, r.sp)) == 0, "the 0 word on its stack");
    env = lin(w(lin(psp, 0x2C)), 0);
    CHECK(!strcmp((char *)mem + env, "PATH=C:\\"), "env first string \"%s\"", (char *)mem + env);
    {
        unsigned long e = env;

        while (mem[e] || mem[e + 1])
            e++;
        CHECK(w(e + 2) == 1 && !strcmp((char *)mem + e + 4, "C:\\HELLO.COM"), "env's program \"%s\"",
              (char *)mem + e + 4);
    }
    CHECK(!memcmp(mem + lin(psp - 1, 8), "HELLO\0\0\0", 8), "MCB name");
    CHECK(w(lin(psp - 1, 1)) == psp, "MCB owner");

    /* ---- the console, the version */
    str(psp, 0x200, "Hello$");
    r.ds = psp, r.dx = 0x200;
    call(&r, 0x0900);
    r.dx = '!';
    call(&r, 0x0200);
    str(psp, 0x210, "[out]");
    r.bx = 1, r.cx = 5, r.dx = 0x210;
    res = call(&r, 0x4000);
    CHECK(res == DOS21_DONE && !(r.flags & 1) && r.ax == 5, "40h to CON %d ax=%x", res, r.ax);
    CHECK(!strcmp(out, "Hello![out]"), "console \"%s\"", out);
    call(&r, 0x3000);
    CHECK(r.ax == 0x0A07 && (r.bx >> 8) == 0xFF, "30h ax=%x bx=%x", r.ax, r.bx);
    r.bx = 1;
    call(&r, 0x4400);
    CHECK((r.dx & 0x83) == 0x83, "44h CON's word %x", r.dx);

    /* ---- files */
    str(psp, 0x300, "OUT.TXT");
    r.dx = 0x300, r.cx = 0;
    res = call(&r, 0x3C00);
    h = r.ax;
    CHECK(!(r.flags & 1) && h == 5, "3Ch handle %u cf=%u", h, r.flags & 1);
    str(psp, 0x400, "written by a program");
    r.bx = h, r.cx = 20, r.dx = 0x400;
    call(&r, 0x4000);
    CHECK(r.ax == 20, "40h %u", r.ax);
    r.bx = h, r.cx = 0, r.dx = 8;
    call(&r, 0x4200);
    CHECK(r.ax == 8 && r.dx == 0, "42h %x:%x", r.dx, r.ax);
    r.bx = h, r.cx = 100, r.dx = 0x500;
    call(&r, 0x3F00);
    CHECK(r.ax == 12 && !memcmp(mem + lin(psp, 0x500), "by a program", 12), "3Fh %u", r.ax);
    r.bx = h, r.cx = 0xFFFF, r.dx = 0xFFFC;     /* (-4 from the end) */
    call(&r, 0x4202);
    CHECK(r.ax == 16, "42h from the end %u", r.ax);
    r.bx = h;
    call(&r, 0x5700);
    CHECK(r.dx == ((2026 - 1980) << 9 | 11 << 5 | 22) || r.dx == 0, "57h date %x (the volume's clock)", r.dx);
    r.bx = h;
    call(&r, 0x3E00);
    CHECK(!(r.flags & 1), "3Eh");
    r.bx = h;
    call(&r, 0x3E00);
    CHECK((r.flags & 1) && r.ax == 6, "3Eh twice: %x", r.ax);
    str(psp, 0x300, "NOFILE.TXT");
    r.dx = 0x300;
    call(&r, 0x3D00);
    CHECK((r.flags & 1) && r.ax == 2, "3Dh no file %x", r.ax);
    str(psp, 0x300, "\\NODIR\\X.TXT");
    r.dx = 0x300;
    call(&r, 0x3D00);
    CHECK((r.flags & 1) && r.ax == 3, "3Dh no folder %x", r.ax);
    str(psp, 0x300, "Q:\\X.TXT");
    r.dx = 0x300;
    call(&r, 0x3D00);
    CHECK((r.flags & 1) && r.ax == 0x0F, "3Dh no drive %x", r.ax);
    str(psp, 0x300, "c:\\data.txt");
    r.dx = 0x300;
    call(&r, 0x3D00);
    h = r.ax;
    CHECK(!(r.flags & 1), "3Dh lower case");
    r.bx = h, r.cx = 4, r.dx = 0x500;
    call(&r, 0x4000);
    CHECK((r.flags & 1) && r.ax == 5, "40h on a read-only handle %x", r.ax);
    call(&r, 0x5900);
    CHECK(r.ax == 5, "59h %x", r.ax);
    r.bx = h;
    call(&r, 0x4406);
    CHECK((r.ax & 0xFF) == 0xFF, "44h 06h before the end");
    r.bx = h;
    call(&r, 0x4500);
    h2 = r.ax;
    CHECK(!(r.flags & 1) && h2 != h, "45h");
    r.bx = h, r.cx = 0, r.dx = 10;
    call(&r, 0x4200);
    r.bx = h2;
    call(&r, 0x4406);
    CHECK((r.ax & 0xFF) == 0, "44h 06h at the end (a dup shares the place)");
    r.bx = h2;
    call(&r, 0x3E00);
    /* (h stays open: the child inherits it) */

    /* ---- devices by name */
    str(psp, 0x300, "\\SUB\\NUL.TXT");
    r.dx = 0x300;
    call(&r, 0x3D02);
    h2 = r.ax;
    CHECK(!(r.flags & 1), "NUL opened");
    r.bx = h2, r.cx = 7, r.dx = 0x400;
    call(&r, 0x4000);
    CHECK(r.ax == 7, "NUL takes it all");
    r.bx = h2;
    call(&r, 0x4400);
    CHECK((r.dx & 0x84) == 0x84, "NUL's word %x", r.dx);
    r.bx = h2;
    call(&r, 0x3E00);

    /* ---- folders */
    str(psp, 0x300, "NEW");
    r.dx = 0x300;
    call(&r, 0x3900);
    CHECK(!(r.flags & 1), "39h");
    call(&r, 0x3900);
    CHECK((r.flags & 1) && r.ax == 5, "39h again %x", r.ax);
    call(&r, 0x3B00);
    CHECK(!(r.flags & 1), "3Bh");
    r.dx = 0, r.si = 0x600;
    call(&r, 0x4700);
    CHECK(!strcmp((char *)mem + lin(psp, 0x600), "NEW"), "47h \"%s\"", (char *)mem + lin(psp, 0x600));
    str(psp, 0x300, "F.TXT");
    r.dx = 0x300, r.cx = 0;
    call(&r, 0x3C00);
    r.bx = r.ax;
    call(&r, 0x3E00);
    {
        struct fat_ent fe;

        CHECK(fsv_lookup(&vol, "\\NEW\\F.TXT", &fe) == 0, "relative create went to \\NEW");
    }
    str(psp, 0x300, "\\NEW");
    r.dx = 0x300;
    call(&r, 0x3A00);
    CHECK((r.flags & 1) && r.ax == 0x10, "3Ah the current folder %x", r.ax);
    str(psp, 0x300, "\\");
    r.dx = 0x300;
    call(&r, 0x3B00);
    str(psp, 0x300, "\\NEW");
    r.dx = 0x300;
    call(&r, 0x3A00);
    CHECK((r.flags & 1) && r.ax == 5, "3Ah not empty %x", r.ax);
    str(psp, 0x300, "NEW\\F.TXT");
    r.dx = 0x300;
    call(&r, 0x4100);
    CHECK(!(r.flags & 1), "41h");
    str(psp, 0x300, "\\NEW");
    r.dx = 0x300;
    call(&r, 0x3A00);
    CHECK(!(r.flags & 1), "3Ah");
    str(psp, 0x300, "DATA.TXT");
    str(psp, 0x340, "MOVED.TXT");
    r.dx = 0x300, r.es = psp, r.di = 0x340;
    call(&r, 0x5600);
    CHECK(!(r.flags & 1), "56h of an open file %x (MS-DOS 7.1 renames one)", r.ax);
    str(psp, 0x300, "MOVED.TXT");
    str(psp, 0x340, "DATA.TXT");
    call(&r, 0x5600);
    CHECK(!(r.flags & 1), "56h back");
    str(psp, 0x300, "OUT.TXT");
    str(psp, 0x340, "MOVED.TXT");
    r.dx = 0x300, r.es = psp, r.di = 0x340;
    call(&r, 0x5600);
    CHECK(!(r.flags & 1), "56h %x", r.ax);
    str(psp, 0x300, "MOVED.TXT");
    r.dx = 0x300, r.cx = 0x01;
    call(&r, 0x4301);
    call(&r, 0x4300);
    CHECK(!(r.flags & 1) && (r.cx & 0x01), "43h %x", r.cx);
    r.cx = 0x10;
    call(&r, 0x4301);
    CHECK((r.flags & 1) && r.ax == 5, "43h 01h a folder's bit");

    /* ---- searches */
    str(psp, 0x300, "*.COM");
    r.dx = 0x300, r.cx = 0;
    call(&r, 0x4E00);
    CHECK(!(r.flags & 1) && !strcmp((char *)mem + lin(psp, 0x80 + 0x1E), "HELLO.COM"), "4Eh \"%s\"",
          (char *)mem + lin(psp, 0x9E));
    call(&r, 0x4F00);
    CHECK((r.flags & 1) && r.ax == 0x12, "4Fh at the end %x", r.ax);
    r.ds = psp, r.dx = 0x700;
    call(&r, 0x1A00);
    str(psp, 0x300, "C:\\SUB\\*.*");
    r.dx = 0x300, r.cx = 0x10;
    call(&r, 0x4E00);
    CHECK(!(r.flags & 1) && !strcmp((char *)mem + lin(psp, 0x700 + 0x1E), "."), "4Eh in SUB \"%s\"",
          (char *)mem + lin(psp, 0x71E));
    call(&r, 0x2F00);
    CHECK(r.es == psp && r.bx == 0x700, "2Fh");
    str(psp, 0x300, "\\NODIR\\*.*");
    r.dx = 0x300;
    call(&r, 0x4E00);
    CHECK((r.flags & 1) && r.ax == 3, "4Eh no folder %x", r.ax);

    /* ---- memory */
    r.es = psp, r.bx = 0x1000;
    call(&r, 0x4A00);
    CHECK(!(r.flags & 1), "4Ah");
    r.bx = 0xFFFF;
    call(&r, 0x4800);
    largest0 = r.bx;
    CHECK((r.flags & 1) && r.ax == 8 && largest0 > 0x7000, "48h FFFFh %x largest %x", r.ax, largest0);
    r.bx = 0x100;
    call(&r, 0x4800);
    seg = r.ax;
    CHECK(!(r.flags & 1) && seg > psp, "48h %x", seg);
    r.es = seg;
    call(&r, 0x4900);
    CHECK(!(r.flags & 1), "49h");
    r.es = seg;
    call(&r, 0x4900);
    CHECK(!(r.flags & 1), "49h twice: DOS frees a free block again (its MCB is good)");

    /* ---- EXEC: an .EXE as the child */
    str(psp, 0x300, "C:\\PROG.EXE");
    mem[lin(psp, 0x800)] = 3, memcpy(mem + lin(psp, 0x801), " xy\r", 4);
    memset(mem + lin(psp, 0x810), 0, 64);
    put16(lin(psp, 0x900), 0);                  /* the parameter block: the parent's environment */
    put16(lin(psp, 0x902), 0x800), put16(lin(psp, 0x904), psp);
    put16(lin(psp, 0x906), 0x810), put16(lin(psp, 0x908), psp);
    put16(lin(psp, 0x90A), 0x820), put16(lin(psp, 0x90C), psp);
    r.ds = psp, r.dx = 0x300, r.es = psp, r.bx = 0x900;
    r.cs = psp, r.ip = 0x123, r.ss = psp, r.sp = 0xFFF0;
    saved = r;
    res = call(&r, 0x4B00);
    CHECK(res == DOS21_SWITCH, "4B00h %d ax=%x", res, r.ax);
    {
        unsigned c = dos_cur_psp(&m), img = c + 0x10;

        CHECK(c != psp && w(lin(c, 0x16)) == psp, "child's parent %04x", w(lin(c, 0x16)));
        CHECK(r.cs == img && r.ip == 0 && r.ss == img + 1 && r.sp == 0x100 && r.ds == c && r.es == c,
              "EXE regs cs:ip=%04x:%04x ss:sp=%04x:%04x ds=%04x", r.cs, r.ip, r.ss, r.sp, r.ds);
        CHECK(w(lin(img, 1)) == img + 1, "relocated %04x", w(lin(img, 1)));
        CHECK(!memcmp(mem + lin(img, 4), "EXEDATA", 7), "EXE image");
        CHECK(mem[lin(c, 0x80)] == 3 && !memcmp(mem + lin(c, 0x81), " xy\r", 4), "child's tail");
        CHECK(mem[lin(c, 0x18 + h)] == mem[lin(psp, 0x18 + h)], "the open file inherited");
        CHECK(w(lin(c, 0x0A)) == 0x123 && w(lin(c, 0x0C)) == psp, "INT 22h at the parent's return point");
        CHECK(w(lin(psp, 0x2E)) == 0xFFF0 && w(lin(psp, 0x30)) == psp, "the parent's SS:SP kept");
        CHECK(!memcmp(mem + lin(c - 1, 8), "PROG\0\0\0\0", 8), "child's MCB name");
        call(&r, 0x6200);
        CHECK(r.bx == c, "62h");
        /* the child changes INT 23h, writes to the inherited handle's file, reads it, and ends with 7 */
        r.ds = 0x1234, r.dx = 0x5678;
        call(&r, 0x2523);
        r.bx = h, r.cx = 0, r.dx = 0;
        call(&r, 0x4200);
        r.ds = c, r.bx = h, r.cx = 4, r.dx = 0x80;
        call(&r, 0x3F00);
        CHECK(r.ax == 4 && !memcmp(mem + lin(c, 0x80), "0123", 4), "child reads the inherited file");
        r.bx = h;
        call(&r, 0x3E00);                       /* (its copy closed: the parent's stays) */
        r.bx = 0x100;
        call(&r, 0x4800);                       /* (its memory: freed at its end) */
        res = call(&r, 0x4C07);
    }
    CHECK(res == DOS21_SWITCH && dos_cur_psp(&m) == psp, "child's end %d psp %04x", res, dos_cur_psp(&m));
    CHECK(r.cs == saved.cs && r.ip == saved.ip && r.ss == saved.ss && r.sp == saved.sp && !(r.flags & 1),
          "back in the parent cs:ip=%04x:%04x ss:sp=%04x:%04x", r.cs, r.ip, r.ss, r.sp);
    CHECK(w(0x23 * 4) == 0x46 && w(0x23 * 4 + 2) == 0x60, "INT 23h given back %04x:%04x", w(0x8E), w(0x8C));
    call(&r, 0x4D00);
    CHECK(r.ax == 7, "4Dh %x", r.ax);
    call(&r, 0x4D00);
    CHECK(r.ax == 0, "4Dh twice %x", r.ax);
    r.bx = 0xFFFF;
    call(&r, 0x4800);
    CHECK(r.bx == largest0, "the child's memory back %x, was %x", r.bx, largest0);
    CHECK(dos_mem_check(&m.mcb) == 0, "the chain");
    r.bx = h, r.cx = 0, r.dx = 0;
    call(&r, 0x4200);
    CHECK(!(r.flags & 1), "the parent's handle still open");
    str(psp, 0x300, "C:\\BAD.EXE");
    r.ds = psp, r.dx = 0x300, r.es = psp, r.bx = 0x900;
    call(&r, 0x4B00);
    CHECK((r.flags & 1) && r.ax == 0x0B, "EXEC of a broken .EXE %x", r.ax);
    str(psp, 0x300, "C:\\NONE.EXE");
    call(&r, 0x4B00);
    CHECK((r.flags & 1) && r.ax == 2, "EXEC of nothing %x", r.ax);
    str(psp, 0x300, "C:\\NODIR\\NONE.EXE");
    call(&r, 0x4B00);
    CHECK((r.flags & 1) && r.ax == 3, "EXEC in no folder %x", r.ax);
    str(psp, 0x300, "\\NODIR\\F.TXT");
    r.dx = 0x300, r.cx = 0;
    call(&r, 0x3C00);
    CHECK((r.flags & 1) && r.ax == 3, "3Ch in no folder %x", r.ax);

    /* ---- an overlay */
    str(psp, 0x300, "C:\\PROG.EXE");
    r.ds = psp, r.dx = 0x300, r.es = psp, r.bx = 0x900;
    put16(lin(psp, 0x900), psp + 0x2000), put16(lin(psp, 0x902), 0x4000);
    call(&r, 0x4B03);
    CHECK(!(r.flags & 1) && w(lin(psp + 0x2000, 1)) == 0x4001, "4B03h %x reloc %04x", r.ax,
          w(lin(psp + 0x2000, 1)));

    /* ---- date and time */
    call(&r, 0x2A00);
    CHECK(r.cx == 2026 && r.dx == (11 << 8 | 22) && (r.ax & 0xFF) == 0, "2Ah %u-%x dow %u", r.cx, r.dx, r.ax & 0xFF);
    call(&r, 0x2C00);
    CHECK(r.cx == (12 << 8 | 34) && (r.dx >> 8) == 56, "2Ch %x %x", r.cx, r.dx);
    r.cx = 2000, r.dx = 1 << 8 | 1;
    call(&r, 0x2B00);
    CHECK((r.ax & 0xFF) == 0, "2Bh");
    now_secs += 3600;
    call(&r, 0x2A00);
    CHECK(r.cx == 2000 && r.dx == (1 << 8 | 1), "2Ah after 2Bh %u-%x", r.cx, r.dx);
    call(&r, 0x2C00);
    CHECK((r.cx >> 8) == 13, "the clock goes on %x", r.cx);
    r.cx = 2026, r.dx = 2 << 8 | 30;
    call(&r, 0x2B00);
    CHECK((r.ax & 0xFF) == 0xFF, "2Bh 30 February");

    /* ---- 29h */
    {
        static const struct { const char *s; unsigned al; const char *fcb; unsigned ret, used; } p[] = {
            { "  c:foo.bar rest", 1, "\003FOO     BAR", 0, 11 },
            { ",*.c", 1, "\000????????C  ", 1, 4 },
            { "file name", 0, "\000FILE       ", 0, 4 },
            { "q:x", 0, "\021X          ", 0xFF, 3 },
            { "longername.text", 0, "\000LONGERNATEX", 0, 15 },
            { "a?b", 0, "\000A?B        ", 1, 3 },
        };
        unsigned k;

        for (k = 0; k < sizeof p / sizeof p[0]; k++) {
            str(psp, 0x300, p[k].s);
            memset(mem + lin(psp, 0x380), 'z', 16);
            r.ds = psp, r.si = 0x300, r.es = psp, r.di = 0x380;
            call(&r, 0x2900 | p[k].al);
            CHECK((r.ax & 0xFF) == p[k].ret && r.si == 0x300 + p[k].used && !memcmp(mem + lin(psp, 0x380), p[k].fcb, 12),
                  "29h \"%s\": al=%x si+%u fcb \"%.12s\"", p[k].s, r.ax & 0xFF, r.si - 0x300, (char *)mem + lin(psp, 0x380));
        }
        str(psp, 0x300, ".x");                  /* (no name given, bit 2: the FCB's own kept) */
        memcpy(mem + lin(psp, 0x380), "\000KEEPNAMEOLD", 12);
        r.ds = psp, r.si = 0x300, r.es = psp, r.di = 0x380;
        call(&r, 0x2906);
        CHECK(!memcmp(mem + lin(psp, 0x380), "\000KEEPNAMEX  ", 12), "29h bit 2 \"%.12s\"", (char *)mem + lin(psp, 0x380));
    }

    /* ---- the SDA's current PSP and DTA are DOS's own: a writer of them (an app's switch, app.c's adopt) is obeyed */
    {
        unsigned long sda = dos_layout_machine.sda;

        CHECK(w(sda + 0x10) == psp && w(sda + 0x0E) == dos_cur_psp(&m), "the SDA's PSP %04x", w(sda + 0x10));
        put16(sda + 0x10, dos_layout_machine.root_psp);
        call(&r, 0x6200);
        CHECK(r.bx == dos_layout_machine.root_psp, "62h after the SDA was written: %04x", r.bx);
        put16(sda + 0x10, psp);
        put16(sda + 0x0C, 0x123), put16(sda + 0x0E, psp);
        call(&r, 0x2F00);
        CHECK(r.es == psp && r.bx == 0x123, "2Fh after the SDA was written");
        r.ds = psp, r.dx = 0x700;
        call(&r, 0x1A00);
        CHECK(w(sda + 0x0C) == 0x700, "1Ah writes the SDA's DTA");
        r.dx = 2;
        call(&r, 0x0E00);
        CHECK(mem[sda + 0x16] == 2, "0Eh: the SDA's current drive");
    }

    /* ---- the census's next: 6Ch, 60h, 52h, 5D06h, 6800h, 7303h, 4411h */
    str(psp, 0x300, "EXT.TXT");
    r.ds = psp, r.si = 0x300, r.bx = 0x0002, r.cx = 0, r.dx = 0x0010;  /* (make it if it isn't there, else fail) */
    call(&r, 0x6C00);
    h = r.ax;
    CHECK(!(r.flags & 1) && r.cx == 2, "6Ch made: cx=%u ax=%x", r.cx, r.ax);
    r.bx = h, r.cx = 3, r.dx = 0x210;
    call(&r, 0x4000);
    r.bx = h;
    call(&r, 0x3E00);
    r.si = 0x300, r.bx = 0x0000, r.dx = 0x0010;
    call(&r, 0x6C00);
    CHECK((r.flags & 1) && r.ax == 0x50, "6Ch there, fail: %x", r.ax);
    r.si = 0x300, r.bx = 0x0000, r.dx = 0x0001;
    call(&r, 0x6C00);
    h = r.ax;
    CHECK(!(r.flags & 1) && r.cx == 1, "6Ch opened: cx=%u", r.cx);
    r.bx = h, r.cx = 0, r.dx = 0;
    call(&r, 0x4202);
    CHECK(r.ax == 3, "6Ch opened the file as it was: %u bytes", r.ax);
    r.bx = h;
    call(&r, 0x3E00);
    r.si = 0x300, r.bx = 0x0002, r.dx = 0x0012;
    call(&r, 0x6C00);
    h = r.ax;
    CHECK(!(r.flags & 1) && r.cx == 3, "6Ch replaced: cx=%u", r.cx);
    r.bx = h, r.cx = 0, r.dx = 0;
    call(&r, 0x4202);
    CHECK(r.ax == 0, "6Ch replaced: %u bytes", r.ax);
    r.bx = h;
    call(&r, 0x6800);
    CHECK(!(r.flags & 1), "6800h");
    r.bx = h;
    call(&r, 0x3E00);
    str(psp, 0x300, "NOTHERE.TXT");
    r.si = 0x300, r.bx = 0, r.dx = 0x0001;
    call(&r, 0x6C00);
    CHECK((r.flags & 1) && r.ax == 2, "6Ch not there, fail: %x", r.ax);
    str(psp, 0x300, "c:sub\\..\\ext.txt");
    r.ds = psp, r.si = 0x300, r.es = psp, r.di = 0x400;
    call(&r, 0x6000);
    CHECK(!(r.flags & 1) && !strcmp((char *)mem + lin(psp, 0x400), "C:\\EXT.TXT"), "60h \"%s\"",
          (char *)mem + lin(psp, 0x400));
    call(&r, 0x5200);
    CHECK(w(lin(r.es, r.bx) - 2) == dos_layout_machine.first_mcb && w(lin(r.es, r.bx) + 4) == (dos_layout_machine.sft & 0xF)
          && w(lin(r.es, r.bx) + 6) == (dos_layout_machine.sft >> 4) && !memcmp(mem + lin(r.es, r.bx) + 0x2C, "NUL     ", 8),
          "52h's list");
    call(&r, 0x5D06);
    CHECK(!(r.flags & 1) && lin(r.ds, r.si) == dos_layout_machine.sda && r.cx == DOS_SDA_SIZE, "5D06h");
    r.ds = psp;
    str(psp, 0x300, "C:\\");
    r.dx = 0x300, r.es = psp, r.di = 0x400, r.cx = 0x30;
    call(&r, 0x7303);
    CHECK(!(r.flags & 1) && w(lin(psp, 0x400)) == 0x24 && w(lin(psp, 0x410)) == vol.fat.clusters
          && w(lin(psp, 0x408)) == 512, "7303h");
    r.bx = 1;
    call(&r, 0x4411);
    CHECK((r.flags & 1) && r.ax == 1, "4411h");
    r.bx = 3;
    call(&r, 0x4408);
    CHECK(!(r.flags & 1) && r.ax == 1, "4408h C: fixed");
    r.bx = 3;
    call(&r, 0x4409);
    CHECK(!(r.flags & 1) && r.dx == 0, "4409h C: local");
    r.ds = psp, r.dx = 0x400, r.bx = 3;
    call(&r, 0x6900);
    CHECK(!(r.flags & 1) && (w(lin(psp, 0x402)) | (unsigned long)w(lin(psp, 0x404)) << 16) == vol.fat.serial
          && !memcmp(mem + lin(psp, 0x406), "DOS21      FAT16   ", 19), "6900h \"%.19s\"", (char *)mem + lin(psp, 0x406));
    str(psp, 0x300, "nul.txt");
    r.ds = psp, r.si = 0x300, r.es = psp, r.di = 0x400;
    call(&r, 0x6000);
    CHECK(!(r.flags & 1) && !strcmp((char *)mem + lin(psp, 0x400), "C:/NUL"), "60h of a device \"%s\"",
          (char *)mem + lin(psp, 0x400));
    str(psp, 0x300, "\\");
    r.dx = 0x300;
    call(&r, 0x3A00);
    CHECK((r.flags & 1) && r.ax == 5, "3Ah the root: 05h (%x)", r.ax);

    /* ---- 4B01h: loaded, not run; then a TSR's end (31h) */
    str(psp, 0x300, "C:\\PROG.EXE");
    put16(lin(psp, 0x900), 0);
    put16(lin(psp, 0x902), 0x800), put16(lin(psp, 0x904), psp);
    put16(lin(psp, 0x906), 0x810), put16(lin(psp, 0x908), psp);
    put16(lin(psp, 0x90A), 0x820), put16(lin(psp, 0x90C), psp);
    r.ds = psp, r.dx = 0x300, r.es = psp, r.bx = 0x900, r.cs = psp, r.ip = 0x200, r.ss = psp, r.sp = 0xFF00;
    res = call(&r, 0x4B01);
    {
        unsigned c = dos_cur_psp(&m), img = c + 0x10;

        CHECK(res == DOS21_DONE && !(r.flags & 1) && c != psp, "4B01h %d psp %04x", res, c);
        CHECK(w(lin(psp, 0x912)) == 0 && w(lin(psp, 0x914)) == img && w(lin(psp, 0x910)) == img + 1
              && w(lin(psp, 0x90E)) == 0xFE, "4B01h's block: cs:ip %04x:%04x ss:sp %04x:%04x", w(lin(psp, 0x914)),
              w(lin(psp, 0x912)), w(lin(psp, 0x910)), w(lin(psp, 0x90E)));
        /* the loaded child "runs" (the caller jumps to it) and stays resident with 20h paragraphs */
        r.dx = 0x20;
        res = call(&r, 0x3100);
        CHECK(res == DOS21_SWITCH && dos_cur_psp(&m) == psp && r.cs == psp && r.ip == 0x200, "31h back %d %04x:%04x", res,
              r.cs, r.ip);
        CHECK(w(lin(c - 1, 1)) == c && w(lin(c - 1, 3)) == 0x20, "31h: its block kept, 20h paragraphs (%x)",
              w(lin(c - 1, 3)));
        call(&r, 0x4D00);
        CHECK(r.ax == 0x0300, "4Dh after 31h %x", r.ax);
        /* (what a TSR keeps, as under DOS, given back: its handles, its block, its environment) */
        r.bx = c;
        call(&r, 0x5000);
        for (i = 5; i < 20; i++) {              /* (its files: those it inherited) */
            r.bx = i;
            call(&r, 0x3E00);
        }
        r.bx = psp;
        call(&r, 0x5000);
        r.es = w(lin(c, 0x2C));
        call(&r, 0x4900);
        r.es = c;
        call(&r, 0x4900);
        CHECK(!(r.flags & 1), "the TSR's block freed");
    }

    /* ---- the console: keys typed, the input calls, Ctrl-C */
    outn = 0, out[0] = 0;
    type("ab\r");
    call(&r, 0x0100);
    CHECK((r.ax & 0xFF) == 'a' && !strcmp(out, "a"), "01h 'a' echoed \"%s\"", out);
    call(&r, 0x0700);
    CHECK((r.ax & 0xFF) == 'b' && !strcmp(out, "a"), "07h 'b' not echoed");
    call(&r, 0x0800);
    CHECK((r.ax & 0xFF) == 0x0D, "08h Enter");
    type("\x1b[A\x1b[5~\x1bOP");
    call(&r, 0x0700);
    res = r.ax & 0xFF;
    call(&r, 0x0700);
    CHECK(res == 0 && (r.ax & 0xFF) == 0x48, "an arrow: 00h then 48h (%x %x)", res, r.ax & 0xFF);
    CHECK(dos_key_get(&m) == 0x4900 && dos_key_get(&m) == 0x3B00, "Page Up, F1");
    r.ax = 0x0100;
    res = call(&r, 0x0100);
    CHECK(res == DOS21_GONE && r.ax == 0x0100, "a wait told to end: %d", res);
    outn = 0, out[0] = 0;
    type("hel\x7flo\r");
    mem[lin(psp, 0x600)] = 20;
    r.ds = psp, r.dx = 0x600;
    call(&r, 0x0A00);
    CHECK(mem[lin(psp, 0x601)] == 4 && !memcmp(mem + lin(psp, 0x602), "helo\r", 5) && !strcmp(out, "hel\b \blo\r"),
          "0Ah: %u \"%.5s\" echo \"%s\"", mem[lin(psp, 0x601)], (char *)mem + lin(psp, 0x602), out);
    type("ab\x1b" "cd\r");
    call(&r, 0x0A00);
    CHECK(mem[lin(psp, 0x601)] == 2 && !memcmp(mem + lin(psp, 0x602), "cd\r", 3), "0Ah after Esc");
    mem[lin(psp, 0x600)] = 3;
    outn = 0, out[0] = 0;
    type("wxyz\r");
    call(&r, 0x0A00);
    CHECK(mem[lin(psp, 0x601)] == 2 && !memcmp(mem + lin(psp, 0x602), "wx\r", 3) && strchr(out, '\a'),
          "0Ah full: a bell, the rest refused");
    idles = 0;
    call(&r, 0x0B00);
    CHECK((r.ax & 0xFF) == 0 && idles == 1, "0Bh none");
    type("k");
    call(&r, 0x0B00);
    CHECK((r.ax & 0xFF) == 0xFF, "0Bh one");
    r.dx = 0xFF;
    r.flags = 0;
    call(&r, 0x0600);
    CHECK((r.ax & 0xFF) == 'k' && !(r.flags & 0x40), "06h FFh: 'k'");
    r.dx = 0xFF;
    call(&r, 0x0600);
    CHECK((r.flags & 0x40) && (r.ax & 0xFF) == 0, "06h FFh: none, ZF");
    type("zz");
    r.dx = 0xFF;
    call(&r, 0x0C06);
    CHECK((r.flags & 0x40), "0Ch: what was typed ahead flushed");
    /* 3Fh from CON, cooked */
    type("line one\r");
    r.ds = psp, r.bx = 0, r.cx = 4, r.dx = 0x700;
    call(&r, 0x3F00);
    CHECK(r.ax == 4 && !memcmp(mem + lin(psp, 0x700), "line", 4), "3Fh CON 4: %u", r.ax);
    r.bx = 0, r.cx = 100, r.dx = 0x700;
    call(&r, 0x3F00);
    CHECK(r.ax == 6 && !memcmp(mem + lin(psp, 0x700), " one\r\n", 6), "3Fh CON the rest: %u", r.ax);
    type("\x1a\r");
    r.bx = 0, r.cx = 100, r.dx = 0x700;
    call(&r, 0x3F00);
    CHECK(!(r.flags & 1) && r.ax == 0, "3Fh CON Ctrl-Z: the end (%u)", r.ax);
    r.bx = 0;
    call(&r, 0x4406);
    CHECK((r.ax & 0xFF) == 0, "4406h CON: nothing typed");
    /* Ctrl-C */
    outn = 0, out[0] = 0;
    type("\x03");
    r.ax = 0x0100, r.bx = 0x1234;
    res = call(&r, 0x0100);
    CHECK(res == DOS21_CTRLC && r.ax == 0x0100 && r.bx == 0x1234 && !strcmp(out, "^C\r\n"), "01h Ctrl-C %d \"%s\"",
          res, out);
    type("\x03");
    r.ds = psp, r.dx = 0x200;
    res = call(&r, 0x0900);
    CHECK(res == DOS21_CTRLC, "09h with Ctrl-C waiting: %d", res);
    type("\x03");
    res = call(&r, 0x0700);
    CHECK(res == DOS21_DONE && (r.ax & 0xFF) == 3, "07h takes Ctrl-C as a key");
    r.dx = 0x0001;
    call(&r, 0x3301);
    type("\x03");
    res = call(&r, 0x2A00);
    CHECK(res == DOS21_CTRLC, "BREAK ON: any call: %d", res);
    r.dx = 0;
    call(&r, 0x3301);
    CHECK(dos_key_push(&m, 0x1C0D) == 0 && dos_key_peek(&m) == 0x1C0D && dos_key_get(&m) == 0x1C0D, "05h-style push");

    /* ---- what isn't there */
    r.flags = 0;
    res = call(&r, 0x7143);
    CHECK(res == DOS21_UNANSWERED && (r.flags & 1) && r.ax == 0x7100, "71h %x", r.ax);
    r.flags = 0;
    res = call(&r, 0x5F02);
    CHECK(res == DOS21_UNANSWERED && (r.flags & 1) && r.ax == 1, "5Fh (not yet) %x", r.ax);

    /* ---- the end */
    res = call(&r, 0x4C2A);
    CHECK(res == DOS21_END && m.ended && (m.exit_code & 0xFF) == 0x2A, "the end %d %x", res, m.exit_code);
    CHECK(dos_mem_check(&m.mcb) == 0, "the chain at the end");
    for (i = 0; i < DOS_FILES; i++)
        if (m.files.k[i].f.n)
            CHECK(0, "entry %u still open", i);
    /* ... and another program in the same machine after it */
    res = dos_m_start(&m, "HELLO.COM", "", &r);
    CHECK(res == 0 && r.cs == psp, "a second program %d at %04x (the first at %04x)", res, r.cs, psp);
    res = dos20(&m, &r);
    CHECK(res == DOS21_END, "INT 20h");
    /* ... one from the kernel's memory, which leaves a file open when its machine goes */
    res = dos_m_start_image(&m, "HELLO", com, sizeof com, " k", &r);
    psp = dos_cur_psp(&m);                                /* (a paragraph lower: its environment's name is shorter) */
    CHECK(res == 0 && r.cs == psp && !memcmp(mem + lin(psp, 0x100), com, sizeof com), "an image %d", res);
    {
        unsigned long e = lin(w(lin(psp, 0x2C)), 0);

        while (mem[e] || mem[e + 1])
            e++;
        CHECK(!strcmp((char *)mem + e + 4, "C:\\HELLO"), "the image's name \"%s\"", (char *)mem + e + 4);
    }
    str(psp, 0x300, "LEFT.TXT");
    r.ds = psp, r.dx = 0x300, r.cx = 0;
    call(&r, 0x3C00);
    CHECK(!(r.flags & 1), "3Ch before the machine goes");
    dos_m_close(&m);
    for (i = 0; i < FSV_NODES; i++)
        CHECK(vol.node[i].refs == 0, "node %u still open after dos_m_close", i);

    /* ---- the system VM's DOS on a native boot: the loader there adopted (dos_m_init_vm) */
    {
        struct dos_m *vm = &m;
        unsigned top, size_paras, largest;

        memset(mem, 0, SIZE);
        memset(mem + 0x790, 0x55, (0x1744 - 0x79) * 16);           /* (the loader's code and data) */
        mem[0x790] = 0xCD, mem[0x791] = 0x20;
        res = dos_m_init_vm(vm, mem, SIZE, 0x79, 0x1744, 0x9FC0, &vol, 2, 437);
        vm->out = con, vm->now = now, vm->wait = no_more, vm->idle = idle;
        CHECK(res == 0 && dos_mem_check(&vm->mcb) == 0, "init_vm %d", res);
        CHECK(mem[0x780] == 'M' && w(0x781) == 0x79 && w(0x783) == 0x1744 - 0x79, "the loader's block %c %04x %04x",
              mem[0x780], w(0x781), w(0x783));
        CHECK(dos_cur_psp(vm) == 0x79 && w(0x790 + 0x16) == 0x79 && mem[0x790 + 0x18] == 1 && w(0x790 + 0x32) == 20,
              "the loader the running program, its own parent, its handles");
        top = (unsigned)((vm->lay.lol - 0x12) >> 4);
        CHECK(vm->lay.lol > 0x90000 && w((top - 1) * 16 + 1) == 8 && mem[(top - 1) * 16] == 'Z', "DOS's tables at "
              "the top %05lx", vm->lay.lol);
        CHECK(memcmp(mem + 0x890, "\x55\x55", 2) == 0, "the loader's memory past its PSP left as it was");
        /* the stub's own first calls: its block made small, an environment, a program run and back */
        r.es = 0x79, r.bx = 0xC0;
        r.cs = 0x89, r.ip = 0x10, r.ss = 0x89, r.sp = 0x800, r.ds = 0x89;
        call(&r, 0x4A00);
        CHECK(!(r.flags & 1), "4Ah: the stub's block made small");
        r.bx = 0x40;
        call(&r, 0x4800);
        CHECK(!(r.flags & 1) && r.ax > 0x79 + 0xC0, "48h: the master environment at %04x", r.ax);
        r.bx = 0xFFFF;
        call(&r, 0x4800);
        size_paras = r.bx;
        CHECK(size_paras > 0x9000, "the rest free: %x paragraphs", size_paras);
        str(0x89, 0x300, "C:\\HELLO.COM");
        mem[lin(0x89, 0x400)] = 0, mem[lin(0x89, 0x401)] = 0x0D;
        memset(mem + lin(0x89, 0x500), 0, 16);
        put16(lin(0x89, 0x500), 0), put16(lin(0x89, 0x502), 0x400), put16(lin(0x89, 0x504), 0x89);
        put16(lin(0x89, 0x506), 0x420), put16(lin(0x89, 0x508), 0x89), put16(lin(0x89, 0x50A), 0x420);
        put16(lin(0x89, 0x50C), 0x89);
        r.ds = 0x89, r.dx = 0x300, r.es = 0x89, r.bx = 0x500;
        r.ax = 0x4B00;
        res = dos21(vm, &r);
        CHECK(res == DOS21_SWITCH && dos_cur_psp(vm) != 0x79, "the stub's EXEC %d", res);
        r.ax = 0x4C05;
        res = dos21(vm, &r);
        CHECK(res == DOS21_SWITCH && dos_cur_psp(vm) == 0x79 && r.cs == 0x89 && r.ip == 0x10 && !vm->ended,
              "the child's end back to the stub, which runs on: %d %04x:%04x", res, r.cs, r.ip);
        r.ax = 0x4D00;
        dos21(vm, &r);
        CHECK(r.ax == 5, "4Dh %x", r.ax);
        r.bx = 0xFFFF;
        call(&r, 0x4800);
        CHECK(r.bx == size_paras, "the child's memory back: %x, was %x", r.bx, size_paras);
        CHECK(dos_mem_check(&vm->mcb) == 0, "the chain");
        (void)largest;
    }

    printf("dos21_test: %d failures\n", fails);
    return fails != 0;
}
