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
    r.es = psp, r.di = 0x400, r.cx = 0x3F, r.dx = 3;
    call(&r, 0x7302);
    CHECK(!(r.flags & 1) && w(lin(psp, 0x400)) == 0x3D && w(lin(psp, 0x404)) == 512 && mem[lin(psp, 0x40A)] == vol.fat.nfats
          && w(lin(psp, 0x40F)) == vol.fat.clusters + 1, "7302h: its DPB");
    r.cx = 0x20;
    call(&r, 0x7302);
    CHECK((r.flags & 1) && r.ax == 0x18, "7302h: a buffer too small %x", r.ax);
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
    memset(mem + lin(psp, 0x400), 0, 32);       /* (the same by the drive's generic request 66h: DISK asks so) */
    r.ds = psp, r.dx = 0x400, r.bx = 3, r.cx = 0x0866;
    call(&r, 0x440D);
    CHECK(!(r.flags & 1) && (w(lin(psp, 0x402)) | (unsigned long)w(lin(psp, 0x404)) << 16) == vol.fat.serial
          && !memcmp(mem + lin(psp, 0x406), "DOS21      FAT16   ", 19), "440Dh 0866h");
    r.bx = 3, r.cx = 0x0860;
    call(&r, 0x440D);
    CHECK((r.flags & 1) && r.ax == 0x01, "440Dh 0860h refused %x", r.ax);
    str(psp, 0x300, "nul.txt");
    r.ds = psp, r.si = 0x300, r.es = psp, r.di = 0x400;
    call(&r, 0x6000);
    CHECK(!(r.flags & 1) && !strcmp((char *)mem + lin(psp, 0x400), "C:/NUL"), "60h of a device \"%s\"",
          (char *)mem + lin(psp, 0x400));

    /* ---- long names (71xxh): Windows 95's calls, answered with no Windows */
    {
        unsigned fh, sh;
        unsigned long rec = lin(psp, 0x600);
        char *out = (char *)mem + lin(psp, 0x400);

        str(psp, 0x300, "C:\\");
        r.ds = psp, r.dx = 0x300, r.es = psp, r.di = 0x400, r.cx = 32;
        call(&r, 0x71A0);
        CHECK(!(r.flags & 1) && r.bx == 0x4006 && r.cx == 255 && r.dx == 260 && !strcmp(out, "FAT"),
              "71A0h bx=%x cx=%u dx=%u \"%s\"", r.bx, r.cx, r.dx, out);
        str(psp, 0x300, "C:\\A long folder name");
        r.ds = psp, r.dx = 0x300;
        call(&r, 0x7139);
        CHECK(!(r.flags & 1), "7139h %x", r.ax);
        str(psp, 0x300, "C:\\A long folder name\\Read me first.text");
        r.ds = psp, r.si = 0x300, r.bx = 2, r.cx = 0, r.dx = 0x12, r.di = 0;
        call(&r, 0x716C);
        fh = r.ax;
        CHECK(!(r.flags & 1) && r.cx == 2, "716Ch made: cf=%u ax=%x cx=%u", r.flags & 1, r.ax, r.cx);
        str(psp, 0x500, "long names with no Windows");
        r.bx = fh, r.cx = 26, r.ds = psp, r.dx = 0x500;
        call(&r, 0x4000);
        CHECK(r.ax == 26, "40h on it %u", r.ax);
        r.bx = fh, r.ds = psp, r.dx = 0x700;
        r.flags |= 1;
        call(&r, 0x71A6);
        CHECK(!(r.flags & 1) && w(lin(psp, 0x700 + 0x24)) == 26 && w(lin(psp, 0x700 + 0x28)) == 1, "71A6h size %u",
              w(lin(psp, 0x700 + 0x24)));
        r.bx = fh;
        call(&r, 0x3E00);
        r.ds = psp, r.si = 0x300, r.bx = 0, r.cx = 0, r.dx = 0x10, r.di = 0;       /* (make it only if it isn't there) */
        call(&r, 0x716C);
        CHECK((r.flags & 1) && r.ax == 0x50, "716Ch: it is there %x", r.ax);
        /* a search by a pattern: the record's long and short names, its size, DOS's date and time */
        str(psp, 0x300, "C:\\A long folder name\\*.text");
        r.ds = psp, r.dx = 0x300, r.es = psp, r.di = 0x600, r.cx = 0x0000, r.si = 1;
        call(&r, 0x714E);
        sh = r.ax;
        CHECK(!(r.flags & 1) && sh >= 1 && !strcmp((char *)mem + rec + 0x2C, "Read me first.text")
              && !strcmp((char *)mem + rec + 0x130, "README~1.TEX") && w(rec + 0x20) == 26 && w(rec) == 0x20,
              "714Eh cf=%u \"%s\" \"%s\" size %u", r.flags & 1, (char *)mem + rec + 0x2C, (char *)mem + rec + 0x130,
              w(rec + 0x20));
        CHECK(w(rec + 0x14 + 2) == ((2026 - 1980) << 9 | 11 << 5 | 22) || w(rec + 0x14 + 2) == 0, "714Eh date %x",
              w(rec + 0x14 + 2));
        r.bx = sh, r.es = psp, r.di = 0x600, r.si = 1;
        call(&r, 0x714F);
        CHECK((r.flags & 1) && r.ax == 0x12, "714Fh: no more %x", r.ax);
        r.bx = sh;
        call(&r, 0x71A1);
        CHECK(!(r.flags & 1), "71A1h");
        r.bx = sh;
        call(&r, 0x71A1);
        CHECK((r.flags & 1) && r.ax == 6, "71A1h twice %x", r.ax);
        str(psp, 0x300, "C:\\A long folder name\\*.none");
        r.ds = psp, r.dx = 0x300, r.es = psp, r.di = 0x600, r.cx = 0, r.si = 1;
        call(&r, 0x714E);
        CHECK((r.flags & 1) && r.ax == 0x12, "714Eh: none %x", r.ax);
        /* the folder everything in the root by "*": the long folder among them, by its short name too */
        str(psp, 0x300, "C:\\alongf~1.*");
        r.ds = psp, r.dx = 0x300, r.es = psp, r.di = 0x600, r.cx = 0x0010, r.si = 0;
        call(&r, 0x714E);
        CHECK(!(r.flags & 1) && !strcmp((char *)mem + rec + 0x2C, "A long folder name") && (w(rec) & 0x10),
              "714Eh by the short name \"%s\"", (char *)mem + rec + 0x2C);
        CHECK(w(rec + 0x14 + 6) > 0x01D0 || w(rec + 0x14 + 6) == 0, "its time is Windows's count %04x", w(rec + 0x14 + 6));
        r.bx = r.ax;
        call(&r, 0x71A1);
        /* the current folder: short in the CDS (47h), long by 7147h */
        str(psp, 0x300, "C:\\A long folder name");
        r.ds = psp, r.dx = 0x300;
        call(&r, 0x713B);
        CHECK(!(r.flags & 1), "713Bh %x", r.ax);
        r.dx = 0, r.ds = psp, r.si = 0x400;
        call(&r, 0x4700);
        CHECK(!(r.flags & 1) && !strcmp(out, "ALONGF~1"), "47h \"%s\"", out);
        r.dx = 0, r.ds = psp, r.si = 0x400;
        call(&r, 0x7147);
        CHECK(!(r.flags & 1) && !strcmp(out, "A long folder name"), "7147h \"%s\"", out);
        /* a file by its long name from there; its short path and back */
        str(psp, 0x300, "read ME first.TEXT");
        r.ds = psp, r.si = 0x300, r.es = psp, r.di = 0x400, r.cx = 1;
        call(&r, 0x7160);
        CHECK(!(r.flags & 1) && !strcmp(out, "C:\\ALONGF~1\\README~1.TEX"), "7160h short \"%s\"", out);
        str(psp, 0x300, "C:\\ALONGF~1\\README~1.TEX");
        r.ds = psp, r.si = 0x300, r.es = psp, r.di = 0x400, r.cx = 2;
        call(&r, 0x7160);
        CHECK(!(r.flags & 1) && !strcmp(out, "C:\\A long folder name\\Read me first.text"), "7160h long \"%s\"", out);
        r.ds = psp, r.dx = 0x300;                   /* (the short path opens it, by 3Dh) */
        call(&r, 0x3D00);
        CHECK(!(r.flags & 1), "3Dh by the short path %x", r.ax);
        r.bx = r.ax;
        call(&r, 0x3E00);
        /* attributes, a new name, gone */
        str(psp, 0x300, "Read me first.text");
        r.ds = psp, r.dx = 0x300, r.bx = 0;
        call(&r, 0x7143);
        CHECK(!(r.flags & 1) && r.cx == 0x20, "7143h %x", r.cx);
        str(psp, 0x500, "Another name, with a comma.txt");
        r.ds = psp, r.dx = 0x300, r.es = psp, r.di = 0x500;
        call(&r, 0x7156);
        CHECK(!(r.flags & 1), "7156h %x", r.ax);
        r.ds = psp, r.dx = 0x500, r.bx = 2;
        call(&r, 0x7143);
        CHECK(!(r.flags & 1) && r.ax == 26 && r.dx == 0, "7143h size %u", r.ax);
        str(psp, 0x300, "C:\\A long folder name");
        r.ds = psp, r.dx = 0x300;
        call(&r, 0x713A);
        CHECK((r.flags & 1) && r.ax == 0x10, "713Ah of the current folder %x", r.ax);
        str(psp, 0x300, "\\");
        r.ds = psp, r.dx = 0x300;
        call(&r, 0x713B);
        str(psp, 0x300, "C:\\A long folder name");
        r.ds = psp, r.dx = 0x300;
        call(&r, 0x713A);
        CHECK((r.flags & 1) && r.ax == 0x05, "713Ah of a folder with a file %x", r.ax);
        str(psp, 0x300, "C:\\A long folder name\\Another name, with a comma.txt");
        r.ds = psp, r.dx = 0x300, r.si = 0;
        call(&r, 0x7141);
        CHECK(!(r.flags & 1), "7141h %x", r.ax);
        /* 7141h by a pattern (SI 1): the files it matches, by long or short name; not folders; not without SI 1 */
        file("\\A long folder name\\first of them.tmp", "1", 1);
        file("\\A long folder name\\second of them.tmp", "2", 1);
        file("\\A long folder name\\kept.txt", "3", 1);
        fsv_mkdir(&vol, "\\A long folder name\\a folder.tmp");
        str(psp, 0x300, "C:\\A long folder name\\*.tmp");
        r.ds = psp, r.dx = 0x300, r.si = 0, r.cx = 0;
        call(&r, 0x7141);
        CHECK((r.flags & 1) && r.ax == 2, "7141h, a pattern without SI 1: %x", r.ax);
        r.ds = psp, r.dx = 0x300, r.si = 1, r.cx = 0;
        call(&r, 0x7141);
        {
            struct fat_ent fe;

            CHECK(!(r.flags & 1) && fsv_lookup(&vol, "\\A long folder name\\first of them.tmp", &fe) != 0
                  && fsv_lookup(&vol, "\\A long folder name\\second of them.tmp", &fe) != 0
                  && fsv_lookup(&vol, "\\A long folder name\\kept.txt", &fe) == 0
                  && fsv_lookup(&vol, "\\A long folder name\\a folder.tmp", &fe) == 0, "7141h by a pattern: %x", r.ax);
        }
        r.ds = psp, r.dx = 0x300, r.si = 1, r.cx = 0;
        call(&r, 0x7141);
        CHECK((r.flags & 1) && r.ax == 2, "7141h by a pattern, none left: %x", r.ax);
        fsv_rmdir(&vol, "\\A long folder name\\a folder.tmp");
        fsv_unlink(&vol, "\\A long folder name\\kept.txt");
        str(psp, 0x300, "C:\\A long folder name");
        r.ds = psp, r.dx = 0x300;
        call(&r, 0x713A);
        CHECK(!(r.flags & 1), "713Ah %x", r.ax);
        /* DOS's date and time as Windows's count and back; a call there isn't */
        r.bx = 1, r.cx = 12 << 11 | 34 << 5 | 28, r.dx = (2026 - 1980) << 9 | 11 << 5 | 23, r.es = psp, r.di = 0x700;
        call(&r, 0x71A7);
        r.bx = 0, r.ds = psp, r.si = 0x700;
        call(&r, 0x71A7);
        CHECK(!(r.flags & 1) && r.cx == (12 << 11 | 34 << 5 | 28) && r.dx == ((2026 - 1980) << 9 | 11 << 5 | 23),
              "71A7h there and back %x %x", r.dx, r.cx);
        call(&r, 0x71FF);
        CHECK((r.flags & 1) && r.ax == 0x7100, "71FFh %x", r.ax);
    }
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
    /* a line whose call went back to wait for a key (the system VM's way) goes on where it was */
    outn = 0, out[0] = 0;
    mem[lin(psp, 0x600)] = 20;
    type("ab");
    r.ds = psp, r.dx = 0x600;
    res = call(&r, 0x0A00);
    CHECK(res == DOS21_GONE && !strcmp(out, "ab"), "0Ah waiting: %d \"%s\"", res, out);
    type("c\r");
    res = call(&r, 0x0A00);
    CHECK(res == DOS21_DONE && mem[lin(psp, 0x601)] == 3 && !memcmp(mem + lin(psp, 0x602), "abc\r", 4)
          && !strcmp(out, "abc\r"), "0Ah again: %u \"%.4s\" echo \"%s\"", mem[lin(psp, 0x601)],
          (char *)mem + lin(psp, 0x602), out);
    type("li");
    r.bx = 0, r.cx = 100, r.dx = 0x700;
    res = call(&r, 0x3F00);
    CHECK(res == DOS21_GONE, "3Fh CON waiting: %d", res);
    type("ne\r");
    r.bx = 0, r.cx = 100, r.dx = 0x700;
    call(&r, 0x3F00);
    CHECK(r.ax == 6 && !memcmp(mem + lin(psp, 0x700), "line\r\n", 6), "3Fh CON again: %u", r.ax);

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

    /* ---- the small calls: 37h, 03h-05h, 1Bh and 1Ch, 5Ah, 67h, 5Ch */
    {
        unsigned tp = dos_cur_psp(&m), th, th2, k, tseg, dups[40], ndups = 0;
        char first[32];

        r.dx = 0x1234, r.flags = 1;
        call(&r, 0x3700);
        CHECK((r.ax & 0xFF) == 0 && r.dx == 0x1200 + '/' && (r.flags & 1), "3700h: al %02x dx %04x", r.ax & 0xFF, r.dx);
        call(&r, 0x3701);                               /* (AL as it came, whatever is asked: MS-DOS 7.1's way) */
        CHECK((r.ax & 0xFF) == 1, "3701h: al %02x", r.ax & 0xFF);
        call(&r, 0x3702);
        CHECK((r.ax & 0xFF) == 2 && (r.dx & 0xFF) == 0xFF, "3702h: al %02x dl %02x", r.ax & 0xFF, r.dx & 0xFF);
        call(&r, 0x3709);
        CHECK((r.ax & 0xFF) == 9, "3709h: al %02x", r.ax & 0xFF);
        res = call(&r, 0x0300);
        CHECK(res == DOS21_DONE && (r.ax & 0xFF) == 0x1A, "03h: al %02x", r.ax & 0xFF);
        r.dx = 'x';
        CHECK(call(&r, 0x0400) == DOS21_DONE && call(&r, 0x0500) == DOS21_DONE, "04h, 05h");
        r.dx = 0;
        res = call(&r, 0x1C00);
        CHECK(res == DOS21_DONE && (r.ax & 0xFF) == vol.fat.spc && r.cx == 512 && mem[lin(r.ds, r.bx)] == 0xF8
              && r.dx == (vol.fat.clusters > 0xFFFF ? 0xFFFF : vol.fat.clusters),
              "1Ch: al %u cx %u dx %u media %02x", r.ax & 0xFF, r.cx, r.dx, mem[lin(r.ds, r.bx)]);
        call(&r, 0x1B00);
        CHECK((r.ax & 0xFF) == vol.fat.spc && mem[lin(r.ds, r.bx)] == 0xF8, "1Bh: al %u", r.ax & 0xFF);
        r.dx = 17;
        call(&r, 0x1C00);
        CHECK((r.ax & 0xFF) == 0xFF, "1Ch of Q: al %02x", r.ax & 0xFF);

        /* 5Ah: a name made in a folder (with and without its backslash), each one new; no such folder */
        str(tp, 0x300, "C:\\");
        memset(mem + lin(tp, 0x304), 0xEE, 16);
        r.ds = tp, r.dx = 0x300, r.cx = 0, r.flags = 0;
        call(&r, 0x5A00);
        th = r.ax;
        strcpy(first, (char *)mem + lin(tp, 0x300));
        CHECK(!(r.flags & 1) && strlen(first) == 11 && !memcmp(first, "C:\\", 3) && mem[lin(tp, 0x30C)] == 0xEE,
              "5Ah: cf %u ax %x \"%s\"", r.flags & 1, r.ax, first);
        str(tp, 0x340, "C:\\TMPD");
        r.dx = 0x340;
        call(&r, 0x3900);
        r.dx = 0x340, r.cx = 0, r.flags = 0;
        call(&r, 0x5A00);
        th2 = r.ax;
        CHECK(!(r.flags & 1) && strlen((char *)mem + lin(tp, 0x340)) == 16 && !memcmp(mem + lin(tp, 0x340), "C:\\TMPD\\", 8)
              && strcmp((char *)mem + lin(tp, 0x348), first + 3), "5Ah, no backslash given: \"%s\"", (char *)mem + lin(tp, 0x340));
        r.bx = th, r.cx = 3, r.dx = 0x300;
        call(&r, 0x4000);
        CHECK(!(r.flags & 1) && r.ax == 3, "5Ah's file written: %x", r.ax);
        r.bx = th;
        call(&r, 0x3E00);
        r.bx = th2;
        call(&r, 0x3E00);
        r.dx = 0x300;
        call(&r, 0x4100);
        CHECK(!(r.flags & 1), "5Ah's file deleted by the name given back: %x", r.ax);
        r.dx = 0x340;
        call(&r, 0x4100);
        str(tp, 0x340, "C:\\TMPD");
        r.dx = 0x340;
        call(&r, 0x3A00);
        CHECK(!(r.flags & 1), "5Ah's second file deleted, its folder removed: %x", r.ax);
        str(tp, 0x340, "C:\\NOSUCH\\");
        r.dx = 0x340, r.cx = 0;
        call(&r, 0x5A00);
        CHECK((r.flags & 1) && r.ax == 3, "5Ah in no folder: %x", r.ax);

        /* 67h: more handles than the PSP's 20, and back */
        r.bx = 10, r.flags = 1;
        call(&r, 0x6700);
        CHECK(!(r.flags & 1) && w(lin(tp, 0x32)) == 20 && w(lin(tp, 0x36)) == tp, "67h to 10: still the PSP's 20");
        r.bx = 40;
        call(&r, 0x6700);
        tseg = w(lin(tp, 0x36));
        CHECK(!(r.flags & 1) && w(lin(tp, 0x32)) == 40 && tseg != tp && w(lin(tp, 0x34)) == 0
              && !memcmp(mem + lin(tseg, 0), mem + lin(tp, 0x18), 20) && mem[lin(tseg, 20)] == 0xFF
              && mem[lin(tseg, 39)] == 0xFF, "67h to 40: %u at %04x", w(lin(tp, 0x32)), tseg);
        do {                                            /* (handle 1 again and again, to one past the PSP's own) */
            r.bx = 1, r.flags = 0;
            call(&r, 0x4500);
            CHECK(!(r.flags & 1), "45h past 20 handles: cf, ax %x after %u", r.ax, ndups);
            dups[ndups++] = r.ax;
        } while (!(r.flags & 1) && r.ax < 25 && ndups < 39);
        k = dups[ndups - 1];
        CHECK(k == 25, "45h past 20 handles: the last is %u", k);
        r.bx = k, r.cx = 2, r.ds = tp, r.dx = 0x300, outn = 0;
        call(&r, 0x4000);
        CHECK(!(r.flags & 1) && outn == 2, "a write through handle %u: %u", k, outn);
        r.bx = 20;
        call(&r, 0x6700);
        CHECK((r.flags & 1) && r.ax == 4 && w(lin(tp, 0x32)) == 40, "67h to 20 with handle %u open: %x", k, r.ax);
        for (k = 0; k < ndups; k++) {
            r.bx = dups[k];
            call(&r, 0x3E00);
        }
        r.bx = 20;
        call(&r, 0x6700);
        CHECK(!(r.flags & 1) && w(lin(tp, 0x32)) == 20 && w(lin(tp, 0x36)) == tp && w(lin(tp, 0x34)) == 0x18
              && dos_mem_check(&m.mcb) == 0, "67h back to 20: %u at %04x:%04x", w(lin(tp, 0x32)), w(lin(tp, 0x36)),
              w(lin(tp, 0x34)));

        /* 32h, 1Fh: a drive's parameter block; 55h, 26h: a PSP made by the program */
        r.dx = 3;
        call(&r, 0x3200);
        CHECK((r.ax & 0xFF) == 0 && mem[lin(r.ds, r.bx)] == 2 && w(lin(r.ds, r.bx) + 2) == 512
              && mem[lin(r.ds, r.bx) + 8] == vol.fat.nfats && mem[lin(r.ds, r.bx) + 0x17] == 0xF8,
              "32h: al %02x drive %u bps %u", r.ax & 0xFF, mem[lin(r.ds, r.bx)], w(lin(r.ds, r.bx) + 2));
        r.dx = 17;
        call(&r, 0x3200);
        CHECK((r.ax & 0xFF) == 0xFF, "32h of Q: al %02x", r.ax & 0xFF);
        call(&r, 0x1F00);
        CHECK((r.ax & 0xFF) == 0 && w(lin(r.ds, r.bx) + 2) == 512, "1Fh: al %02x", r.ax & 0xFF);
        r.bx = 0x20;
        call(&r, 0x4800);
        tseg = r.ax;
        r.dx = tseg, r.si = 0x9ABC;
        call(&r, 0x5500);
        CHECK(dos_cur_psp(&m) == tseg && w(lin(tseg, 0)) == 0x20CD && w(lin(tseg, 2)) == 0x9ABC && w(lin(tseg, 0x16)) == tp
              && w(lin(tseg, 0x32)) == 20 && w(lin(tseg, 0x34)) == 0x18 && w(lin(tseg, 0x36)) == tseg
              && mem[lin(tseg, 0x19)] == mem[lin(tp, 0x19)] && w(lin(tseg, 0x2C)) == w(lin(tp, 0x2C)),
              "55h: current %04x parent %04x top %04x", dos_cur_psp(&m), w(lin(tseg, 0x16)), w(lin(tseg, 2)));
        r.bx = 1, r.cx = 2, r.ds = tseg, r.dx = 0, outn = 0;
        call(&r, 0x4000);
        CHECK(!(r.flags & 1) && outn == 2, "55h: a write through the child's handle 1: %u", outn);
        dos_close_all(&m.files, tseg);
        r.bx = tp;
        call(&r, 0x5000);
        memset(mem + lin(tseg, 0), 0, 0x100);
        r.dx = tseg, r.cs = tp;                         /* (a .COM: its code segment is its PSP) */
        call(&r, 0x2600);
        CHECK(dos_cur_psp(&m) == tp && w(lin(tseg, 0)) == 0x20CD && w(lin(tseg, 0x16)) == w(lin(tp, 0x16))
              && w(lin(tseg, 0x32)) == 20 && w(lin(tseg, 0x34)) == 0x18 && w(lin(tseg, 0x36)) == tseg
              && w(lin(tseg, 2)) == w(lin(tp, 2)) && mem[lin(tseg, 0x80)] == mem[lin(tp, 0x80)], "26h: current %04x",
              dos_cur_psp(&m));
        r.es = tseg;
        call(&r, 0x4900);
        CHECK(!(r.flags & 1) && dos_mem_check(&m.mcb) == 0, "the PSPs' block freed");

        /* 5E00h, 5F02h: no network; 65h: the country's */
        memset(mem + lin(tp, 0x500), 0xEE, 32);
        r.ds = tp, r.dx = 0x500, r.cx = 0x1234, r.flags = 1;
        call(&r, 0x5E00);
        CHECK(!(r.flags & 1) && (r.cx >> 8) == 0 && mem[lin(tp, 0x500)] == ' ' && mem[lin(tp, 0x50E)] == ' '
              && mem[lin(tp, 0x50F)] == 0 && mem[lin(tp, 0x510)] == 0xEE, "5E00h: cx %04x", r.cx);
        r.flags = 0;
        res = call(&r, 0x5F02);
        CHECK(res == DOS21_DONE && (r.flags & 1) && r.ax == 1, "5F02h: %x", r.ax);
        memset(mem + lin(tp, 0x500), 0xEE, 64);
        r.es = tp, r.di = 0x500, r.bx = 0xFFFF, r.dx = 0xFFFF, r.cx = 41, r.flags = 1;
        call(&r, 0x6501);
        CHECK(!(r.flags & 1) && r.cx == 41 && mem[lin(tp, 0x500)] == 1 && w(lin(tp, 0x501)) == 38 && w(lin(tp, 0x503)) == 1
              && w(lin(tp, 0x505)) == 437 && mem[lin(tp, 0x507 + 2)] == '$' && mem[lin(tp, 0x500 + 41)] == 0xEE,
              "6501h: cx %u size %u", r.cx, w(lin(tp, 0x501)));
        memset(mem + lin(tp, 0x500), 0xEE, 16);
        r.di = 0x500, r.bx = 0xFFFF, r.dx = 0xFFFF, r.cx = 7;
        call(&r, 0x6501);
        CHECK(!(r.flags & 1) && r.cx == 7 && w(lin(tp, 0x501)) == 4 && mem[lin(tp, 0x507)] == 0xEE, "6501h, 7 bytes: cx %u",
              r.cx);
        r.di = 0x500, r.bx = 0xFFFF, r.dx = 0xFFFF, r.cx = 4;
        call(&r, 0x6501);
        CHECK((r.flags & 1) && r.ax == 1, "6501h, 4 bytes: %x", r.ax);
        r.bx = 999, r.dx = 0xFFFF, r.cx = 41;
        call(&r, 0x6501);
        CHECK((r.flags & 1) && r.ax == 1, "6501h, another code page: %x", r.ax);
        r.bx = 0xFFFF, r.dx = 49, r.cx = 41;
        call(&r, 0x6501);
        CHECK((r.flags & 1) && r.ax == 1, "6501h, another country: %x", r.ax);
        r.bx = 0xFFFF, r.dx = 0xFFFF, r.cx = 5, r.di = 0x500;
        call(&r, 0x6502);
        k = (unsigned)lin(w(lin(tp, 0x503)), w(lin(tp, 0x501)));
        CHECK(!(r.flags & 1) && r.cx == 5 && mem[lin(tp, 0x500)] == 2 && w(k) == 128 && mem[k + 2 + 2] == 0x45
              && mem[k + 2 + 0x61] == 0xE1, "6502h: the table at %x", k);
        r.bx = 0xFFFF, r.dx = 0xFFFF, r.cx = 5, r.di = 0x500;
        call(&r, 0x6504);
        k = (unsigned)lin(w(lin(tp, 0x503)), w(lin(tp, 0x501)));
        CHECK(!(r.flags & 1) && mem[lin(tp, 0x500)] == 4 && w(k) == 128, "6504h: the table at %x", k);
        r.bx = 0xFFFF, r.dx = 0xFFFF, r.cx = 5, r.di = 0x500;
        call(&r, 0x6505);
        k = (unsigned)lin(w(lin(tp, 0x503)), w(lin(tp, 0x501)));
        CHECK(!(r.flags & 1) && mem[lin(tp, 0x500)] == 5 && w(k) == 22 && mem[k + 9] == 14 && mem[k + 10] == '.'
              && mem[k + 23] == ',', "6505h: the table at %x", k);
        mem[lin(tp, 0x500)] = 0xEE;
        r.bx = 0xFFFF, r.dx = 0xFFFF, r.cx = 5, r.di = 0x500;
        call(&r, 0x6506);
        CHECK(!(r.flags & 1) && mem[lin(tp, 0x500)] == 0xEE, "6506h: done, nothing given (as MS-DOS with no COUNTRY.SYS)");
        r.bx = 0xFFFF, r.dx = 0xFFFF, r.cx = 5, r.di = 0x500;
        call(&r, 0x6507);
        CHECK(!(r.flags & 1) && mem[lin(tp, 0x500)] == 7, "6507h");
        r.bx = 0xFFFF, r.dx = 0xFFFF, r.cx = 41;
        call(&r, 0x6509);
        CHECK((r.flags & 1) && r.ax == 1, "6509h: %x", r.ax);
        r.dx = 'a', r.flags = 1;
        call(&r, 0x6520);
        CHECK(!(r.flags & 1) && (r.dx & 0xFF) == 'A', "6520h: %c", r.dx & 0xFF);
        memcpy(mem + lin(tp, 0x500), "abc\x82z", 6);
        r.ds = tp, r.dx = 0x500, r.cx = 4;
        call(&r, 0x6521);
        CHECK(!memcmp(mem + lin(tp, 0x500), "ABC\x45z", 6), "6521h: %s", (char *)mem + lin(tp, 0x500));
        memcpy(mem + lin(tp, 0x500), "abc\x82z", 6);
        r.ds = tp, r.dx = 0x500;
        call(&r, 0x6522);
        CHECK(!memcmp(mem + lin(tp, 0x500), "ABC\x45Z", 6), "6522h: %s", (char *)mem + lin(tp, 0x500));
        r.dx = 'y';
        call(&r, 0x6523);
        CHECK(r.ax == 1, "6523h y: %x", r.ax);
        r.dx = 'N';
        call(&r, 0x6523);
        CHECK(r.ax == 0, "6523h N: %x", r.ax);
        r.dx = 'x';
        call(&r, 0x6523);
        CHECK(r.ax == 2, "6523h x: %x", r.ax);

        /* 5Ch: a region locked through one handle is that handle's */
        str(tp, 0x300, "C:\\HELLO.COM");
        r.ds = tp, r.dx = 0x300;
        call(&r, 0x3D00);
        th = r.ax;
        r.dx = 0x300;
        call(&r, 0x3D00);
        th2 = r.ax;
        CHECK(!(r.flags & 1) && th2 != th, "two handles on a file");
        r.bx = th, r.cx = 0, r.dx = 0, r.si = 0, r.di = 4;
        call(&r, 0x5C00);
        CHECK(!(r.flags & 1), "5C00h: %x", r.ax);
        r.bx = th2, r.cx = 0, r.dx = 2, r.si = 0, r.di = 4;
        call(&r, 0x5C00);
        CHECK((r.flags & 1) && r.ax == 0x21, "5C00h over a lock: %x", r.ax);
        r.bx = th2, r.cx = 0, r.dx = 4, r.si = 0, r.di = 4;
        call(&r, 0x5C00);
        CHECK(!(r.flags & 1), "5C00h beside it: %x", r.ax);
        r.bx = th2, r.cx = 4, r.ds = tp, r.dx = 0x500;
        call(&r, 0x3F00);
        CHECK((r.flags & 1) && r.ax == 0x21, "a read of another handle's region: %x", r.ax);
        r.bx = th, r.cx = 4, r.dx = 0x500;
        call(&r, 0x3F00);
        CHECK(!(r.flags & 1) && r.ax == 4, "a read of its own: %x", r.ax);
        r.bx = th2, r.cx = 0, r.dx = 0, r.si = 0, r.di = 4;
        call(&r, 0x5C01);
        CHECK((r.flags & 1) && r.ax == 0x21, "5C01h of another's lock: %x", r.ax);
        r.bx = th, r.cx = 0, r.dx = 0, r.si = 0, r.di = 4;
        call(&r, 0x5C01);
        CHECK(!(r.flags & 1), "5C01h: %x", r.ax);
        r.bx = th2, r.cx = 3, r.dx = 0x500;
        call(&r, 0x3F00);
        CHECK(!(r.flags & 1) && r.ax == 3, "a read once it is given up: %x", r.ax);
        call(&r, 0x5C02);
        CHECK((r.flags & 1) && r.ax == 1, "5C02h: %x", r.ax);
        r.bx = th2;
        call(&r, 0x3E00);                               /* (its lock at 4 goes with it) */
        r.bx = th, r.cx = 0, r.dx = 4, r.si = 0, r.di = 4;
        call(&r, 0x5C00);
        CHECK(!(r.flags & 1), "5C00h where a closed handle's lock was: %x", r.ax);
        r.bx = th;
        call(&r, 0x3E00);
        r.bx = 1, r.cx = 0, r.dx = 0, r.si = 0, r.di = 1;
        call(&r, 0x5C00);
        CHECK((r.flags & 1) && r.ax == 1, "5C00h of CON: %x", r.ax);
        r.bx = 19;
        call(&r, 0x5C00);
        CHECK((r.flags & 1) && r.ax == 6, "5C00h of no handle: %x", r.ax);
        r.ds = tp;
    }

    /* ---- what isn't there */
    r.flags = 0;
    res = call(&r, 0x71AA);                     /* (a long-name call GLOS's DOS hasn't got: Windows's SUBST) */
    CHECK(res == DOS21_DONE && (r.flags & 1) && r.ax == 0x7100, "71AAh %x", r.ax);
    r.flags = 0;
    res = call(&r, 0x6100);
    CHECK(res == DOS21_UNANSWERED && (r.flags & 1) && r.ax == 1, "61h (not this DOS's) %x", r.ax);

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
        res = dos_m_init_vm(vm, mem, SIZE, 0x79, 0x1744, 0x9FC0, &vol, 2, 437, 0x840, &seg);
        CHECK(res == 0 && seg > 0x9000 && w((seg - 1) * 16 + 1) == 8 && w((seg - 1) * 16 + 3) == 0x840,
              "the caller's block %04x", seg);
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
