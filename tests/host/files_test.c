/* files_test: kernel/ndos/files.c, files by handle over a volume in memory: the first process's five handles, the
 * system file table's fields as a program reads them, duplicates sharing a position, 46h redirecting stdout into
 * a file, DOS 7's sharing between two programs (go ahead, 05h, INT 24h), handles inherited but not the private
 * ones, a child's handles closed at its exit, full tables, access modes, seeking, NUL, a 0-byte write cutting
 * the file. "files_test: 0 failures" when all held. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../kernel/disk/disk.h"
#include "../../kernel/ndos/files.h"
#include "../../kernel/ndos/psp.h"

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

#define SECTORS 65536UL
#define SFT 0x00600UL                           /* the table's linear address */
#define NSFT 30
#define PSP_A 0x1000u
#define PSP_B 0x2000u

static unsigned char *disk, *mem;
static struct fsv_vol vol;
static struct dos_files t;

static int rd(void *c, unsigned long lba, unsigned char *b, unsigned n) { (void)c; memcpy(b, disk + lba * 512, n * 512UL); return 0; }
static int wr(void *c, unsigned long lba, const unsigned char *b, unsigned n) { (void)c; memcpy(disk + lba * 512, b, n * 512UL); return 0; }

static unsigned w(unsigned long a) { return mem[a] | (unsigned)mem[a + 1] << 8; }
static unsigned long dw(unsigned long a) { return w(a) | (unsigned long)w(a + 2) << 16; }
static unsigned long ent(unsigned i) { return SFT + 6 + (unsigned long)i * DOS_SFT_SIZE; }
static unsigned jft(unsigned psp, unsigned h) { return mem[(unsigned long)psp * 16 + 0x18 + h]; }

static void make_psp(unsigned seg, unsigned parent, const unsigned char *j)
{
    struct dos_psp_init in;

    memset(&in, 0, sizeof in);
    in.seg = seg;
    in.top = seg + 0x1000;
    in.parent = parent;
    in.jft = j;
    in.version = 0x0A07;
    in.tail = "";
    dos_psp_make(mem + (unsigned long)seg * 16, &in);
}

static void fresh(void)
{
    struct fat_geo g = { SECTORS, 0, 16, 63, 0x80, 1, "FILES", 16 };
    struct fat_made m;

    memset(disk, 0, SECTORS * 512);
    memset(mem, 0, 0x110000);
    fat_format(&g, wr, 0, &m);
    fsv_mount(&vol, rd, wr, 0, 0, 437, 0);
    dos_files_init(&t, mem, SFT, NSFT);
    make_psp(PSP_A, PSP_A, 0);
    dos_files_first_jft(&t, PSP_A);
}

static void t_first(void)
{
    fresh();
    CHECK(jft(PSP_A, 0) == 1 && jft(PSP_A, 1) == 1 && jft(PSP_A, 2) == 1 && jft(PSP_A, 3) == 0 &&
              jft(PSP_A, 4) == 2 && jft(PSP_A, 5) == 0xFF, "the first five handles");
    CHECK(w(ent(1)) == 3 && w(ent(0)) == 1 && w(ent(2)) == 1, "their counts: CON %u AUX %u PRN %u", w(ent(1)),
          w(ent(0)), w(ent(2)));
    CHECK(!memcmp(mem + ent(1) + 0x20, "CON        ", 11) && w(ent(1) + 5) == 0x80D3, "CON's entry");
    CHECK(w(SFT + 4) == NSFT && w(SFT) == 0xFFFF, "the table's header");
}

static void t_handles(void)
{
    unsigned h, h2, e;
    unsigned long pos;
    char buf[64];

    fresh();
    CHECK(dos_create(&t, PSP_A, &vol, 2, "\\test.txt", 0, 1, &h) == 0 && h == 5, "create: handle %u", h);
    e = jft(PSP_A, h);
    CHECK(w(ent(e)) == 1 && w(ent(e) + 2) == 2 && !memcmp(mem + ent(e) + 0x20, "TEST    TXT", 11) &&
              w(ent(e) + 0x31) == PSP_A && (w(ent(e) + 5) & 0x7F) == (0x40 | 2),
          "its entry: refs %u mode %x name %.11s info %x", w(ent(e)), w(ent(e) + 2), mem + ent(e) + 0x20,
          w(ent(e) + 5));
    CHECK(dos_write(&t, PSP_A, h, "hello ", 6) == 6, "write");
    CHECK(dw(ent(e) + 0x11) == 6 && dw(ent(e) + 0x15) == 6 && !(w(ent(e) + 5) & 0x40), "size, position, written");
    /* A duplicate shares the entry, and so the position. */
    CHECK(dos_dup(&t, PSP_A, h, &h2) == 0 && h2 == 6 && jft(PSP_A, h2) == e && w(ent(e)) == 2, "dup");
    CHECK(dos_write(&t, PSP_A, h2, "world", 5) == 5 && dw(ent(e) + 0x15) == 11, "write through the duplicate");
    /* stdout into the file (46h), as COMMAND.COM's > does. */
    CHECK(dos_dup2(&t, PSP_A, h, 1) == 0 && jft(PSP_A, 1) == e && w(ent(e)) == 3 && w(ent(1)) == 2,
          "46h: file %u CON %u", w(ent(e)), w(ent(1)));
    CHECK(dos_write(&t, PSP_A, 1, "!", 1) == 1, "stdout into the file");
    CHECK(dos_seek(&t, PSP_A, h, 0, 0, &pos) == 0 && pos == 0, "seek to the start");
    CHECK(dos_read(&t, PSP_A, h, buf, sizeof buf) == 12 && !memcmp(buf, "hello world!", 12), "read it back");
    CHECK(dos_seek(&t, PSP_A, h, 2, -6, &pos) == 0 && pos == 6, "seek from the end: %lu", pos);
    CHECK(dos_seek(&t, PSP_A, h, 1, -100, &pos) == 0 && pos == 0xFFFFFFA2UL, "before the start: kept as MS-DOS "
          "7.1 keeps it, %lx", pos);
    CHECK(dos_read(&t, PSP_A, h, buf, 4) == 0 && dos_write(&t, PSP_A, h, "x", 1) == 0, "nothing there");
    CHECK(dos_seek(&t, PSP_A, h, 0, 6, &pos) == 0 && pos == 6, "back");
    CHECK(dos_seek(&t, PSP_A, h, 3, 0, &pos) == 0x01, "an origin DOS has not");
    CHECK(dos_write(&t, PSP_A, h, "", 0) == 0 && dw(ent(e) + 0x11) == 6, "a 0-byte write cuts the file: %lu",
          dw(ent(e) + 0x11));
    CHECK(dos_close(&t, PSP_A, 1) == 0 && dos_close(&t, PSP_A, h) == 0 && w(ent(e)) == 1, "closes");
    CHECK(dos_close(&t, PSP_A, h) == DOS_EBADF, "closed twice: 06h");
    CHECK(dos_close(&t, PSP_A, h2) == 0 && w(ent(e)) == 0, "the last close frees the entry");
    CHECK(dos_read(&t, PSP_A, h2, buf, 1) == -DOS_EBADF, "read on a closed handle");
    CHECK(dos_dup2(&t, PSP_A, 0, 1) == 0 && jft(PSP_A, 1) == 1, "stdout back to CON");
}

static void t_access(void)
{
    unsigned h, n;
    char buf[8];

    fresh();
    CHECK(dos_create(&t, PSP_A, &vol, 2, "\\a.dat", 0, 1, &h) == 0, "create");
    dos_write(&t, PSP_A, h, "abc", 3);
    dos_close(&t, PSP_A, h);
    CHECK(dos_open(&t, PSP_A, &vol, 2, "\\a.dat", 0, 0x00, &h) == 0, "open read-only");
    CHECK(dos_write(&t, PSP_A, h, "x", 1) == -DOS_EACCES, "write on read-only: 05h");
    dos_close(&t, PSP_A, h);
    CHECK(dos_open(&t, PSP_A, &vol, 2, "\\a.dat", 0, 0x01, &h) == 0, "open write-only");
    CHECK(dos_read(&t, PSP_A, h, buf, 1) == -DOS_EACCES, "read on write-only: 05h");
    dos_close(&t, PSP_A, h);
    CHECK(dos_open(&t, PSP_A, &vol, 2, "\\nope.dat", 0, 0, &h) == DOS_ENOENT, "missing: 02h");
    CHECK(dos_open(&t, PSP_A, &vol, 2, "\\a.dat", 0, 0x03, &h) == DOS_EINVALID, "access 3: 0Ch");
    CHECK(dos_create(&t, PSP_A, &vol, 2, "\\a.dat", 0, 0, &h) == DOS_EEXIST, "5Bh on a file there: 50h");
    CHECK(dos_open(&t, PSP_A, 0, 0, 0, DOS_DEV_NUL, 0x02, &h) == 0, "NUL");
    CHECK(dos_write(&t, PSP_A, h, "abc", 3) == 3 && dos_read(&t, PSP_A, h, buf, 8) == 0, "NUL takes all, gives none");
    dos_close(&t, PSP_A, h);
    /* Every handle taken: 04h. */
    for (n = 0; n < 15 && dos_open(&t, PSP_A, 0, 0, 0, DOS_DEV_NUL, 0x02, &h) == 0; n++)
        ;
    CHECK(n == 15 && dos_open(&t, PSP_A, 0, 0, 0, DOS_DEV_NUL, 0x02, &h) == DOS_ENFILE, "the handle table full");
    dos_close_all(&t, PSP_A);
    CHECK(w(ent(1)) == 0 && w(ent(0)) == 0, "all closed at exit (the first five too)");
}

static void t_sharing(void)
{
    unsigned a, b;

    fresh();
    make_psp(PSP_B, PSP_A, 0);
    dos_create(&t, PSP_A, &vol, 2, "\\s.dat", 0, 1, &a);
    dos_close(&t, PSP_A, a);
    CHECK(dos_open(&t, PSP_A, &vol, 2, "\\s.dat", 0, 0x22, &a) == 0, "A: read-write, deny write");
    CHECK(dos_open(&t, PSP_B, &vol, 2, "\\s.dat", 0, 0x40, &b) == 0, "B: read, deny none: goes ahead");
    dos_close(&t, PSP_B, b);
    CHECK(dos_open(&t, PSP_B, &vol, 2, "\\s.dat", 0, 0x41, &b) == DOS_EACCES, "B: write, deny none: 05h");
    CHECK(dos_open(&t, PSP_B, &vol, 2, "\\s.dat", 0, 0x00, &b) == DOS_ESHARE, "B: compatibility: INT 24h");
    dos_close(&t, PSP_A, a);
    CHECK(dos_open(&t, PSP_B, &vol, 2, "\\s.dat", 0, 0x02, &b) == 0, "B once A has closed");
    dos_close(&t, PSP_B, b);
}

static void t_inherit(void)
{
    unsigned pub, priv, e1, e2;
    unsigned char child[20];

    fresh();
    dos_create(&t, PSP_A, &vol, 2, "\\pub.dat", 0, 1, &pub);
    dos_close(&t, PSP_A, pub);
    dos_create(&t, PSP_A, &vol, 2, "\\priv.dat", 0, 1, &priv);
    dos_close(&t, PSP_A, priv);
    dos_open(&t, PSP_A, &vol, 2, "\\pub.dat", 0, 0x42, &pub);
    dos_open(&t, PSP_A, &vol, 2, "\\priv.dat", 0, 0xC2, &priv);     /* (bit 7: private) */
    e1 = jft(PSP_A, pub);
    e2 = jft(PSP_A, priv);
    dos_inherit(&t, PSP_A, child);
    CHECK(child[pub] == e1 && child[priv] == 0xFF && child[1] == 1, "inherited: %x %x", child[pub], child[priv]);
    CHECK(w(ent(e1)) == 2 && w(ent(e2)) == 1 && w(ent(1)) == 6, "counts raised: %u %u CON %u", w(ent(e1)),
          w(ent(e2)), w(ent(1)));
    make_psp(PSP_B, PSP_A, child);
    dos_close_all(&t, PSP_B);
    CHECK(w(ent(e1)) == 1 && w(ent(1)) == 3, "the child's exit: %u CON %u", w(ent(e1)), w(ent(1)));
}

int main(void)
{
    disk = malloc(SECTORS * 512);
    mem = malloc(0x110000);
    if (!disk || !mem)
        return 2;
    t_first();
    t_handles();
    t_access();
    t_sharing();
    t_inherit();
    printf("files_test: %d failures\n", fails);
    return fails != 0;
}
