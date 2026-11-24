/* fcb_test: the FCB calls of GLOS's own INT 21h (kernel/ndos/dos21.c's fcb, kernel/ndos/files.c's dos_fcb_*),
 * a program's calls simulated over a machine's memory and a volume in memory: open, create and close, records
 * read and written in sequence, at random and in blocks, a file's size in records, the size set, read-only and
 * hidden files, patterns, searches (plain and extended FCBs), delete and rename by pattern, a device, the DTA's
 * segment's end, and FCBs left open: more than hold an entry of the system file table at once (opened again
 * at their next call), more than are known at once (the oldest forgotten), handles opened beside them, and a
 * program's end. "fcb_test: 0 failures" when all held. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../kernel/disk/disk.h"
#include "../../kernel/ndos/dos21.h"

static int fails;
static unsigned char *disk, *mem;
static struct fsv_vol vol;
static struct dos_m m;
static char out[4096];
static unsigned outn, psp;

#define SECTORS 65536UL
#define SIZE    0xA0000UL
#define DTA     0x400                   /* the DTA, in the program's segment */
#define FCB     0x300                   /* an FCB there */

static int rd(void *c, unsigned long lba, unsigned char *b, unsigned n) { (void)c; memcpy(b, disk + lba * 512, n * 512UL); return 0; }
static int wr(void *c, unsigned long lba, const unsigned char *b, unsigned n) { (void)c; memcpy(disk + lba * 512, b, n * 512UL); return 0; }
static void con(void *c, const char *s, unsigned n) { (void)c; memcpy(out + outn, s, n); outn += n; out[outn] = 0; }
static unsigned long now(void *c) { (void)c; return 1000000; }
static int no_more(void *c) { (void)c; return -1; }

#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void file(const char *path, const void *data, unsigned long n, unsigned char attr)
{
    struct fsv_file f;

    fsv_create(&vol, path, 0, 1, FSV_RW, FSV_DENY_NONE, &f);
    fsv_write(&f, data, n);
    fsv_close(&f);
    if (attr)
        fsv_setattr(&vol, path, attr);
}

static unsigned char *at(unsigned off) { return mem + ((unsigned long)psp << 4) + off; }
static unsigned w(unsigned off) { return at(off)[0] | at(off)[1] << 8; }
static void put16(unsigned off, unsigned v) { at(off)[0] = (unsigned char)v; at(off)[1] = (unsigned char)(v >> 8); }

/* An unopened FCB at off: the drive (0: the current one) and a name of 11 bytes; the rest zero. */
static void fcb(unsigned off, unsigned drive, const char *n11)
{
    memset(at(off), 0, 0x25);
    at(off)[0] = (unsigned char)drive;
    memcpy(at(off) + 1, n11, 11);
}

/* An extended one: FFh, five zeros, the attribute, then the FCB (at off + 7). */
static void xfcb(unsigned off, unsigned attr, unsigned drive, const char *n11)
{
    memset(at(off), 0, 7);
    at(off)[0] = 0xFF;
    at(off)[6] = (unsigned char)attr;
    fcb(off + 7, drive, n11);
}

/* A call with DS:DX the FCB at off: AL, and CX and the flags after in *r. */
static struct dos_regs86 r;

static unsigned fc(unsigned ah, unsigned off)
{
    r.ax = ah << 8 | 0x55;
    r.ds = psp, r.dx = off;
    r.flags |= 1;                               /* (the carry flag as it came: FCB calls leave it) */
    if (dos21(&m, &r) != DOS21_DONE || !(r.flags & 1))
        CHECK(0, "call %02x: not done, or the carry flag changed", ah);
    return r.ax & 0xFF;
}

static void set_dta(unsigned off)
{
    r.ax = 0x1A00, r.ds = psp, r.dx = off;
    dos21(&m, &r);
}

static void chdir_(const char *p)
{
    strcpy((char *)at(0x280), p);
    r.ax = 0x3B00, r.ds = psp, r.dx = 0x280;
    dos21(&m, &r);
    CHECK(!(r.flags & 1), "3Bh %s", p);
}

static unsigned long size_of(const char *path)
{
    struct fat_ent fe;

    return fsv_lookup(&vol, path, &fe) == 0 ? fe.size : 0xFFFFFFFFUL;
}

static int there(const char *path)
{
    struct fat_ent fe;

    return fsv_lookup(&vol, path, &fe) == 0;
}

/* The entries of the system file table in use. */
static unsigned entries(void)
{
    unsigned i, n = 0;

    for (i = 0; i < m.files.n; i++)
        n += (mem[m.files.table + 6 + i * DOS_SFT_SIZE] | mem[m.files.table + 6 + i * DOS_SFT_SIZE + 1] << 8) != 0;
    return n;
}

int main(void)
{
    static const unsigned char com[] = { 0xC3 };
    struct fat_geo g = { SECTORS, 0, 16, 63, 0x80, 1, "FCBS", 16 };
    struct fat_made fm;
    unsigned char big[300];
    unsigned i, al, e0, h;
    char name[32], found[256];

    setvbuf(stdout, 0, _IONBF, 0);
    disk = calloc(SECTORS, 512);
    mem = calloc(SIZE, 1);
    if (!disk || !mem)
        return 2;
    fat_format(&g, wr, 0, &fm);
    fsv_mount(&vol, rd, wr, 0, 0, 437, 0);
    for (i = 0; i < sizeof big; i++)
        big[i] = (unsigned char)i;
    file("\\HELLO.COM", com, sizeof com, 0);
    file("\\DATA.TXT", "0123456789", 10, 0);
    file("\\BIG.DAT", big, sizeof big, 0);
    file("\\RO.TXT", "read only", 9, FAT_A_RDONLY);
    file("\\HIDE.SYS", "hidden", 6, FAT_A_HIDDEN);
    fsv_mkdir(&vol, "\\SUB");
    file("\\SUB\\A1.TXT", "a1", 2, 0);
    file("\\SUB\\A2.TXT", "a2", 2, 0);
    file("\\SUB\\AR.TXT", "ar", 2, FAT_A_RDONLY);
    file("\\SUB\\B1.TXT", "b1", 2, 0);
    file("\\SUB\\B2.TXT", "b2", 2, 0);
    fsv_mkdir(&vol, "\\MANY");
    for (i = 0; i < 30; i++) {
        sprintf(name, "\\MANY\\F%02u.DAT", i);
        file(name, name, strlen(name), 0);
    }
    for (i = 0; i < 256; i++) {
        mem[i * 4] = (unsigned char)(i * 2), mem[i * 4 + 1] = 0;
        mem[i * 4 + 2] = 0x60, mem[i * 4 + 3] = 0;
    }
    dos_m_init(&m, mem, SIZE, &dos_layout_machine, &vol, 2, 437);
    m.out = con;
    m.now = now;
    m.wait = no_more;
    if (dos_m_start(&m, "C:\\HELLO.COM", "", &r)) {
        printf("FAIL: the program didn't start\n");
        return 1;
    }
    psp = dos_cur_psp(&m);
    set_dta(DTA);
    e0 = entries();

    /* ---- open: the FCB filled */
    fcb(FCB, 0, "DATA    TXT");
    at(FCB)[0x20] = 0x77, at(FCB)[0x21] = 0x66;         /* (the record fields are the program's: left) */
    al = fc(0x0F, FCB);
    CHECK(al == 0 && at(FCB)[0] == 3 && w(FCB + 0x0C) == 0 && w(FCB + 0x0E) == 0x80 && w(FCB + 0x10) == 10
          && w(FCB + 0x12) == 0 && at(FCB)[0x20] == 0x77 && at(FCB)[0x21] == 0x66 && w(FCB + 0x1A) != 0,
          "open: al %02x drive %u block %u recsize %u size %u rec %02x ticket %u", al, at(FCB)[0], w(FCB + 0x0C),
          w(FCB + 0x0E), w(FCB + 0x10), at(FCB)[0x20], w(FCB + 0x1A));
    CHECK(entries() == e0 + 1, "open: %u entries, %u before", entries(), e0);
    for (i = 0, h = 0; i < m.files.n; i++)             /* (its entry's mode: bit 15, as DOS marks an FCB's) */
        h += (mem[m.files.table + 6 + i * DOS_SFT_SIZE + 3] & 0x80) != 0;
    CHECK(h == 1, "open: %u entries with the FCB bit", h);

    /* ---- sequential reads: a record in part (the rest zeros), then the end */
    at(FCB)[0x20] = 0;
    memset(at(DTA), 0xEE, 256);
    al = fc(0x14, FCB);
    CHECK(al == 3 && !memcmp(at(DTA), "0123456789\0\0\0", 13) && at(DTA)[127] == 0 && at(DTA)[128] == 0xEE
          && at(FCB)[0x20] == 1, "14h a short record: al %u rec %u \"%.10s\"", al, at(FCB)[0x20], (char *)at(DTA));
    al = fc(0x14, FCB);
    CHECK(al == 1 && at(FCB)[0x20] == 1, "14h at the end: al %u rec %u", al, at(FCB)[0x20]);
    put16(FCB + 0x0E, 4);
    at(FCB)[0x20] = 0;
    al = fc(0x14, FCB);
    CHECK(al == 0 && !memcmp(at(DTA), "0123", 4), "14h records of 4: al %u", al);
    al = fc(0x14, FCB);
    CHECK(al == 0 && !memcmp(at(DTA), "4567", 4) && at(FCB)[0x20] == 2, "14h the second: al %u", al);
    al = fc(0x14, FCB);
    CHECK(al == 3 && !memcmp(at(DTA), "89\0\0", 4) && at(FCB)[0x20] == 3, "14h the third, in part: al %u", al);
    al = fc(0x14, FCB);
    CHECK(al == 1 && at(FCB)[0x20] == 3, "14h past the end: al %u", al);

    /* ---- random reads: the place set, and left there; 24h; 27h's blocks; 23h */
    put16(FCB + 0x0E, 2);
    put16(FCB + 0x21, 3), at(FCB)[0x23] = 0, at(FCB)[0x24] = 0;
    al = fc(0x21, FCB);
    CHECK(al == 0 && !memcmp(at(DTA), "67", 2) && w(FCB + 0x0C) == 0 && at(FCB)[0x20] == 3 && w(FCB + 0x21) == 3,
          "21h: al %u block %u rec %u", al, w(FCB + 0x0C), at(FCB)[0x20]);
    al = fc(0x21, FCB);
    CHECK(al == 0 && !memcmp(at(DTA), "67", 2), "21h again: al %u", al);
    put16(FCB + 0x0C, 2), at(FCB)[0x20] = 5;
    put16(FCB + 0x21, 0xFFFF), at(FCB)[0x23] = 0xFF, at(FCB)[0x24] = 0xFF;
    fc(0x24, FCB);
    CHECK(w(FCB + 0x21) == 2 * 128 + 5 && at(FCB)[0x23] == 0 && at(FCB)[0x24] == 0, "24h: %u %u %u", w(FCB + 0x21),
          at(FCB)[0x23], at(FCB)[0x24]);
    put16(FCB + 0x0E, 100);                             /* (records of 64 bytes and more: three bytes of it) */
    at(FCB)[0x24] = 0xAA;
    fc(0x24, FCB);
    CHECK(w(FCB + 0x21) == 2 * 128 + 5 && at(FCB)[0x24] == 0xAA, "24h, long records: the fourth byte %02x", at(FCB)[0x24]);
    put16(FCB + 0x0E, 3);
    put16(FCB + 0x21, 1), at(FCB)[0x23] = 0, at(FCB)[0x24] = 0;
    memset(at(DTA), 0xEE, 64);
    r.cx = 5;
    al = fc(0x27, FCB);
    CHECK(al == 3 && r.cx == 3 && !memcmp(at(DTA), "3456789\0\0", 9) && at(DTA)[9] == 0xEE && w(FCB + 0x21) == 4
          && at(FCB)[0x20] == 4, "27h: al %u cx %u random %u rec %u", al, r.cx, w(FCB + 0x21), at(FCB)[0x20]);
    put16(FCB + 0x21, 0);
    r.cx = 2;
    al = fc(0x27, FCB);
    CHECK(al == 0 && r.cx == 2 && !memcmp(at(DTA), "012345", 6) && w(FCB + 0x21) == 2, "27h, whole: al %u cx %u", al, r.cx);
    put16(FCB + 0x21, 2);
    r.cx = 9;
    al = fc(0x27, FCB);
    CHECK(al == 3 && r.cx == 2, "27h to the end: al %u cx %u", al, r.cx);
    put16(FCB + 0x21, 4);
    r.cx = 1;
    al = fc(0x27, FCB);
    CHECK(al == 1 && r.cx == 0 && w(FCB + 0x21) == 4, "27h at the end: al %u cx %u", al, r.cx);
    CHECK(fc(0x10, FCB) == 0 && entries() == e0, "close: %u entries", entries());
    CHECK(fc(0x10, FCB) == 0, "close twice: 00h, as MS-DOS says of a closed FCB whose file is there");
    /* (a closed FCB reads on, as MS-DOS's do: its file is bound again by its name) */
    put16(FCB + 0x0E, 4), put16(FCB + 0x0C, 0), at(FCB)[0x20] = 1;
    al = fc(0x14, FCB);
    CHECK(al == 0 && !memcmp(at(DTA), "4567", 4) && at(FCB)[0x20] == 2 && entries() == e0 + 1,
          "a read of a closed FCB: al %u, %u entries", al, entries());
    CHECK(fc(0x10, FCB) == 0 && entries() == e0, "... closed again: %u entries", entries());
    fcb(FCB, 0, "NONE    DAT");
    CHECK(fc(0x10, FCB) == 0xFF && fc(0x14, FCB) == 1, "close and read of an FCB of no file");
    fcb(FCB, 0, "DATA    TXT");                          /* (never opened: its name is all it has) */
    put16(FCB + 0x0E, 4);
    CHECK(fc(0x14, FCB) == 1 && fc(0x10, FCB) == 0xFF && entries() == e0, "read and close of an FCB never opened");
    fcb(FCB, 3, "DATA    TXT");
    put16(FCB + 0x0E, 3);
    al = fc(0x23, FCB);
    CHECK(al == 0 && w(FCB + 0x21) == 4, "23h: al %u records %u", al, w(FCB + 0x21));
    fcb(FCB, 3, "BIG     DAT");                          /* (no record size: 128) */
    al = fc(0x23, FCB);
    CHECK(al == 0 && w(FCB + 0x21) == 3 && w(FCB + 0x0E) == 0, "23h, no record size: al %u records %u, size left %u", al,
          w(FCB + 0x21), w(FCB + 0x0E));
    fcb(FCB, 3, "NONE    DAT");
    CHECK(fc(0x23, FCB) == 0xFF, "23h of no file");

    /* ---- what an open refuses */
    fcb(FCB, 0, "NONE    TXT");
    CHECK(fc(0x0F, FCB) == 0xFF, "open: no file");
    fcb(FCB, 0, "DATA    T?T");                          /* (a pattern: its first match, the FCB's name left) */
    CHECK(fc(0x0F, FCB) == 0 && w(FCB + 0x10) == 10 && !memcmp(at(FCB) + 1, "DATA    T?T", 11), "open: a pattern");
    CHECK(fc(0x10, FCB) == 0, "close of it");
    fcb(FCB, 0, "NONE????T?T");
    CHECK(fc(0x0F, FCB) == 0xFF, "open: a pattern nothing matches");
    fcb(FCB, 0, "NEW?    DAT");
    CHECK(fc(0x16, FCB) == 0xFF, "create: a pattern");
    fcb(FCB, 17, "DATA    TXT");
    CHECK(fc(0x0F, FCB) == 0xFF, "open: drive Q:");
    fcb(FCB, 0, "SUB        ");
    CHECK(fc(0x0F, FCB) == 0xFF, "open: a folder");
    fcb(FCB, 0, "           ");
    CHECK(fc(0x0F, FCB) == 0xFF, "open: no name");
    fcb(FCB, 0, "HIDE    SYS");
    CHECK(fc(0x0F, FCB) == 0xFF, "open: a hidden file, a plain FCB");
    xfcb(FCB, FAT_A_HIDDEN, 0, "HIDE    SYS");
    CHECK(fc(0x0F, FCB) == 0 && w(FCB + 7 + 0x10) == 6, "open: a hidden file, an extended FCB");
    CHECK(fc(0x10, FCB) == 0, "close, extended");
    CHECK(entries() == e0, "after the refusals: %u entries", entries());

    /* ---- create, writes (sequential, random, blocks), the size set, close */
    fcb(FCB, 0, "NEW     DAT");
    al = fc(0x16, FCB);
    CHECK(al == 0 && w(FCB + 0x10) == 0 && w(FCB + 0x0E) == 0x80 && there("\\NEW.DAT"), "create: al %u", al);
    memset(at(DTA), 'A', 128);
    al = fc(0x15, FCB);
    CHECK(al == 0 && w(FCB + 0x10) == 128 && at(FCB)[0x20] == 1, "15h: al %u size %u rec %u", al, w(FCB + 0x10), at(FCB)[0x20]);
    memset(at(DTA), 'B', 128);
    put16(FCB + 0x21, 2), at(FCB)[0x23] = 0;
    al = fc(0x22, FCB);
    CHECK(al == 0 && w(FCB + 0x10) == 384 && at(FCB)[0x20] == 2, "22h: al %u size %u rec %u", al, w(FCB + 0x10), at(FCB)[0x20]);
    memset(at(DTA), 'C', 256);
    put16(FCB + 0x21, 3);
    r.cx = 2;
    al = fc(0x28, FCB);
    CHECK(al == 0 && r.cx == 2 && w(FCB + 0x10) == 640 && w(FCB + 0x21) == 5, "28h: al %u cx %u size %u random %u", al,
          r.cx, w(FCB + 0x10), w(FCB + 0x21));
    put16(FCB + 0x21, 1);
    al = fc(0x21, FCB);                                 /* (record 1 was never written: a gap, zeros) */
    CHECK(al == 0 && at(DTA)[0] == 0 && at(DTA)[127] == 0, "21h in the gap: al %u %02x", al, at(DTA)[0]);
    put16(FCB + 0x21, 4);
    al = fc(0x21, FCB);
    CHECK(al == 0 && at(DTA)[0] == 'C' && at(DTA)[127] == 'C', "21h of the block written: al %u %02x", al, at(DTA)[0]);
    put16(FCB + 0x21, 1);
    r.cx = 0;
    al = fc(0x28, FCB);
    CHECK(al == 0 && r.cx == 0 && w(FCB + 0x10) == 128 && w(FCB + 0x21) == 1, "28h, no records: al %u size %u", al,
          w(FCB + 0x10));
    put16(FCB + 0x21, 4);
    r.cx = 0;
    al = fc(0x28, FCB);
    CHECK(al == 0 && w(FCB + 0x10) == 512, "28h, no records, longer: al %u size %u", al, w(FCB + 0x10));
    CHECK(fc(0x10, FCB) == 0 && size_of("\\NEW.DAT") == 512, "close: the file is %lu bytes", size_of("\\NEW.DAT"));
    fcb(FCB, 0, "NEW     DAT");
    al = fc(0x16, FCB);                                 /* (made again: cut to nothing) */
    CHECK(al == 0 && w(FCB + 0x10) == 0, "create over it: al %u size %u", al, w(FCB + 0x10));
    CHECK(fc(0x10, FCB) == 0 && size_of("\\NEW.DAT") == 0, "create over it: %lu bytes", size_of("\\NEW.DAT"));

    /* ---- a read-only file: opened, read, not written */
    fcb(FCB, 0, "RO      TXT");
    CHECK(fc(0x0F, FCB) == 0, "open, read-only");
    put16(FCB + 0x0E, 9);
    CHECK(fc(0x14, FCB) == 0 && !memcmp(at(DTA), "read only", 9), "14h, read-only");
    CHECK(fc(0x15, FCB) == 1 && size_of("\\RO.TXT") == 9, "15h, read-only");
    CHECK(fc(0x10, FCB) == 0, "close, read-only");

    /* ---- the DTA's segment's end */
    fcb(FCB, 0, "BIG     DAT");
    fc(0x0F, FCB);
    put16(FCB + 0x0E, 1), at(FCB)[0x20] = 127;          /* (record 127 to 128: the block moves on) */
    al = fc(0x14, FCB);
    CHECK(al == 0 && at(DTA)[0] == 127 && w(FCB + 0x0C) == 1 && at(FCB)[0x20] == 0,
          "14h over a block's end: al %u block %u rec %u", al, w(FCB + 0x0C), at(FCB)[0x20]);
    al = fc(0x14, FCB);
    CHECK(al == 0 && at(DTA)[0] == 128 && at(FCB)[0x20] == 1, "14h in the second block: al %u", al);
    put16(FCB + 0x0E, 0x80), put16(FCB + 0x0C, 0), at(FCB)[0x20] = 0;
    set_dta(0xFFF0);
    CHECK(fc(0x14, FCB) == 2 && at(FCB)[0x20] == 0, "14h, a record past the segment's end");
    put16(FCB + 0x0E, 6);
    put16(FCB + 0x21, 0), at(FCB)[0x23] = 0;
    r.cx = 5;
    al = fc(0x27, FCB);
    CHECK(al == 2 && r.cx == 2 && at(0xFFF0)[0] == 0 && at(0xFFF0)[11] == 11 && w(FCB + 0x21) == 2,
          "27h, two of five fit: al %u cx %u", al, r.cx);
    set_dta(DTA);
    fc(0x10, FCB);

    /* ---- a device */
    fcb(FCB, 0, "CON        ");
    CHECK(fc(0x0F, FCB) == 0, "open CON");
    put16(FCB + 0x0E, 5);
    memcpy(at(DTA), "hello", 5);
    outn = 0;
    CHECK(fc(0x15, FCB) == 0 && outn == 5 && !memcmp(out, "hello", 5), "15h to CON: \"%.*s\"", (int)outn, out);
    CHECK(fc(0x14, FCB) == 1, "14h from CON");
    CHECK(fc(0x10, FCB) == 0 && entries() == e0, "close CON");

    /* ---- searches: a pattern, in the current folder; an extended FCB's attribute */
    fcb(FCB, 0, "????????TXT");
    found[0] = 0;
    for (al = fc(0x11, FCB), i = 0; al == 0 && i < 10; al = fc(0x12, FCB), i++) {
        CHECK(at(DTA)[0] == 3, "a found entry's drive %u", at(DTA)[0]);
        sprintf(found + strlen(found), "%.11s|", (char *)at(DTA) + 1);
        if (!memcmp(at(DTA) + 1, "DATA    TXT", 11))
            CHECK(at(DTA)[1 + 11] == 0x20 && w(DTA + 1 + 28) == 10 && w(DTA + 1 + 26) >= 2,
                  "DATA.TXT's entry: attr %02x size %u cluster %u", at(DTA)[12], w(DTA + 1 + 28), w(DTA + 1 + 26));
    }
    CHECK(!strcmp(found, "DATA    TXT|RO      TXT|") && al == 0xFF, "11h/12h *.TXT: \"%s\" al %02x", found, al);
    CHECK(fc(0x12, FCB) == 0xFF, "12h after the end");
    fcb(FCB, 0, "????????SYS");
    CHECK(fc(0x11, FCB) == 0xFF, "11h: a hidden file, a plain FCB");
    xfcb(FCB, FAT_A_HIDDEN | FAT_A_DIR, 0, "???????????");
    found[0] = 0;
    for (al = fc(0x11, FCB), i = 0; al == 0 && i < 20; al = fc(0x12, FCB), i++) {
        CHECK(at(DTA)[0] == 0xFF && at(DTA)[6] == (FAT_A_HIDDEN | FAT_A_DIR) && at(DTA)[7] == 3, "an extended entry");
        if (at(DTA)[7 + 12] & (FAT_A_DIR | FAT_A_HIDDEN))
            sprintf(found + strlen(found), "%.11s|", (char *)at(DTA) + 8);
    }
    CHECK(!strcmp(found, "HIDE    SYS|SUB        |MANY       |"), "11h/12h extended: \"%s\"", found);
    chdir_("\\SUB");                                    /* (the label: the root's, from any folder) */
    xfcb(FCB, FAT_A_LABEL, 0, "???????????");
    al = fc(0x11, FCB);
    CHECK(al == 0 && !memcmp(at(DTA) + 8, "FCBS       ", 11), "11h, the label, from a folder: al %02x", al);
    chdir_("\\");
    fcb(FCB, 0, "DATA    TXT");
    CHECK(fc(0x12, FCB) == 0xFF, "12h with no 11h before");
    xfcb(FCB, FAT_A_LABEL, 0, "???????????");
    al = fc(0x11, FCB);
    CHECK(al == 0 && !memcmp(at(DTA) + 8, "FCBS       ", 11) && at(DTA)[8 + 11] == FAT_A_LABEL,
          "11h, the label: al %02x \"%.11s\"", al, (char *)at(DTA) + 8);

    /* ---- delete and rename by pattern, in a folder */
    chdir_("\\SUB");
    fcb(FCB, 0, "A???????TXT");
    al = fc(0x13, FCB);
    CHECK(al == 0 && !there("\\SUB\\A1.TXT") && !there("\\SUB\\A2.TXT") && there("\\SUB\\AR.TXT")
          && there("\\SUB\\B1.TXT"), "13h A*.TXT: al %02x", al);
    CHECK(fc(0x13, FCB) == 0xFF && there("\\SUB\\AR.TXT"), "13h: only a read-only file left");
    fcb(FCB, 0, "NONE    TXT");
    CHECK(fc(0x13, FCB) == 0xFF, "13h of no file");
    fcb(FCB, 0, "B???????TXT");
    memcpy(at(FCB) + 0x11, "C?      BAK", 11);
    al = fc(0x17, FCB);
    CHECK(al == 0 && there("\\SUB\\C1.BAK") && there("\\SUB\\C2.BAK") && !there("\\SUB\\B1.TXT")
          && !there("\\SUB\\B2.TXT"), "17h B?.TXT to C?.BAK: al %02x", al);
    fcb(FCB, 0, "C1      BAK");
    memcpy(at(FCB) + 0x11, "C2      BAK", 11);
    CHECK(fc(0x17, FCB) == 0xFF && there("\\SUB\\C1.BAK"), "17h to a name that is there");
    fcb(FCB, 0, "NONE    TXT");
    memcpy(at(FCB) + 0x11, "OTHER   TXT", 11);
    CHECK(fc(0x17, FCB) == 0xFF, "17h of no file");

    /* ---- FCBs left open (CP/M's way): more than hold an entry at once, more than are known at once */
    chdir_("\\MANY");
    for (i = 0; i < 12; i++) {
        sprintf(name, "F%02u     DAT", i);
        fcb(0x1000 + i * 0x30, 0, name);
        CHECK(fc(0x0F, 0x1000 + i * 0x30) == 0, "open %u of 12", i);
    }
    CHECK(entries() == e0 + DOS_FCB_LIVE, "12 open: %u entries, %u before", entries(), e0);
    for (i = 0; i < 12; i++) {
        put16(0x1000 + i * 0x30 + 0x0E, 12);
        at(0x1000 + i * 0x30)[0x20] = 0;
        sprintf(name, "\\MANY\\F%02u.DAT", i);
        CHECK(fc(0x14, 0x1000 + i * 0x30) == 0 && !memcmp(at(DTA), name, 12), "a read of %u of 12", i);
    }
    CHECK(entries() == e0 + DOS_FCB_LIVE, "12 read: %u entries", entries());
    strcpy((char *)at(0x280), "C:\\DATA.TXT");          /* (handles beside them: the table isn't theirs) */
    for (i = 0; i < 8; i++) {
        r.ax = 0x3D00, r.ds = psp, r.dx = 0x280, r.flags &= ~1u;
        dos21(&m, &r);
        CHECK(!(r.flags & 1), "a handle opened beside the FCBs: %u, error %x", i, r.ax);
    }
    for (h = 5; h < 13; h++) {
        r.ax = 0x3E00, r.bx = h;
        dos21(&m, &r);
    }
    for (i = 12; i < 30; i++) {
        sprintf(name, "F%02u     DAT", i);
        fcb(0x1000 + i * 0x30, 0, name);
        CHECK(fc(0x0F, 0x1000 + i * 0x30) == 0, "open %u of 30", i);
    }
    /* (from another folder: an FCB still known has its file, wherever the program is now; a forgotten one's
       name is looked for where the program is, as a closed one's) */
    chdir_("\\SUB");
    put16(0x1000 + 0x0E, 12), at(0x1000)[0x20] = 0;
    CHECK(fc(0x14, 0x1000) == 1, "the first of 30, from another folder: forgotten");
    put16(0x1000 + 20 * 0x30 + 0x0E, 12), at(0x1000 + 20 * 0x30)[0x20] = 0;
    CHECK(fc(0x14, 0x1000 + 20 * 0x30) == 0 && !memcmp(at(DTA), "\\MANY\\F20.DAT", 12), "the 21st of 30: known");
    chdir_("\\MANY");
    put16(0x1000 + 0x0E, 12), at(0x1000)[0x20] = 0;
    CHECK(fc(0x14, 0x1000) == 0 && !memcmp(at(DTA), "\\MANY\\F00.DAT", 12), "the first of 30, in its folder: bound again");
    CHECK(entries() <= e0 + DOS_FCB_LIVE, "30 open: %u entries", entries());
    chdir_("\\SUB");

    /* ---- a program's end: its FCBs' entries given back */
    dos_close_all(&m.files, psp);
    CHECK(entries() == 0 || entries() <= 3, "after the end: %u entries", entries());
    CHECK(fc(0x14, 0x1000 + 20 * 0x30) == 1, "an FCB after its program's end");

    printf("fcb_test: %d failures\n", fails);
    return fails != 0;
}
