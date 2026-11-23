/* Host test for Disk Tools' FAT volumes (apps/shared/fatimg.c), on volumes kernel/disk/fatfmt.c makes in memory
 * (floppies of 1.44 MB and 360 KB: FAT12; a 16 MB volume: FAT16): files added and read back to the byte (empty,
 * small, a cluster exactly, many clusters), folders in folders, a folder grown by a cluster, a root folder
 * filled, names that are not DOS's, a volume with no room; what is listed; the FATs written at the close, both
 * the same. Then the volumes are left in OUT-DIR for fatimg_test.sh, which holds them against fsck.fat and
 * mtools, and has this program read volumes mtools made (long names):
 *
 *     fatimg_test OUT-DIR                 the tests; writes OUT-DIR/NAME.img and what should be in them
 *     fatimg_test list IMAGE PATH         a folder's entries, a line each: D or F, the size, the DOS name, the name
 *     fatimg_test read IMAGE PATH FILE    a file's bytes into FILE
 *
 * and whatever a volume's bytes are: no harm. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../apps/shared/fatimg.h"
#include "../../kernel/disk/disk.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); \
                                                printf("\n"); fails++; } } while (0)

struct disk {
    unsigned char *b;
    unsigned long sectors;
    int fail_writes;
};

static int mem_io(void *ctx, unsigned long lba, unsigned char *data, unsigned count, int write)
{
    struct disk *d = ctx;
    if (lba + count > d->sectors || (write && d->fail_writes))
        return -1;
    if (write)
        memcpy(d->b + lba * 512, data, count * 512ul);
    else
        memcpy(data, d->b + lba * 512, count * 512ul);
    return 0;
}

static int fmt_write(void *ctx, unsigned long lba, const unsigned char *data, unsigned count)
{
    struct disk *d = ctx;
    if (lba + count > d->sectors)
        return -1;
    memcpy(d->b + lba * 512, data, count * 512ul);
    return 0;
}

static unsigned long seed = 20261121ul;
static unsigned rnd(void)
{
    seed = seed * 1103515245ul + 12345ul;
    return (unsigned)((seed >> 8) & 0xFFFFFFul);
}

static struct disk make(unsigned long sectors, unsigned heads, unsigned spt, unsigned drive)
{
    struct disk d = { calloc(sectors, 512), sectors, 0 };
    struct fat_geo g;
    struct fat_made made;
    memset(&g, 0, sizeof g);
    g.sectors = sectors, g.heads = heads, g.spt = spt, g.drive = drive, g.serial = 0x20261121ul, g.label = "GLOS TEST";
    if (!d.b || fat_format(&g, fmt_write, &d, &made) != 0) {
        printf("FAIL: a volume of %lu sectors not made\n", sectors);
        exit(1);
    }
    return d;
}

static void save(const char *dir, const char *name, const void *b, unsigned long n)
{
    char path[600];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if ((f = fopen(path, "wb")) != NULL) {
        fwrite(b, 1, n, f);
        fclose(f);
    }
}

static int has(const struct fatimg_entry *e, int n, const char *dos, unsigned long size, int dir)
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(e[i].dos, dos))
            return e[i].size == size && e[i].dir == dir;
    return 0;
}

static void a_volume(const char *dir, const char *name, unsigned long sectors, unsigned heads, unsigned spt, unsigned drive, int fat)
{
    static unsigned char big[70000], back[70000];
    struct disk d = make(sectors, heads, spt, drive);
    struct fatimg *v;
    struct fatimg_info info, after;
    struct fatimg_entry *e = NULL;
    char why[80] = "", path[40];
    unsigned i, cluster;
    int n, r;
    for (i = 0; i < sizeof big; i++)
        big[i] = (unsigned char)rnd();
    v = fatimg_open(mem_io, &d, why, sizeof why);
    CHECK(v != NULL, "%s: not opened: %s", name, why);
    if (!v)
        return;
    fatimg_info(v, &info);
    cluster = info.cluster_bytes;
    CHECK(info.fat == fat && info.sectors == sectors && info.free_clusters == info.clusters && !strcmp(info.label, "GLOS TEST"),
          "%s: FAT%d, %lu clusters, %lu free, \"%s\"", name, info.fat, info.clusters, info.free_clusters, info.label);
    CHECK(fatimg_list(v, "", &e) == 0, "%s: an empty root", name);
    free(e), e = NULL;
    /* files of every size, folders in folders */
    CHECK(fatimg_add(v, "EMPTY.TXT", "", 0, 0x5B75, 0x6000) == 0 && fatimg_add(v, "small.txt", "ten bytes!", 10, 0, 0) == 0
          && fatimg_add(v, "ONE.BIN", big, cluster, 0, 0) == 0 && fatimg_add(v, "BIG.BIN", big, sizeof big, 0, 0) == 0, "%s: files added", name);
    CHECK(fatimg_mkdir(v, "SUB", 0, 0) == 0 && fatimg_mkdir(v, "sub\\Deep", 0, 0) == 0 && fatimg_add(v, "SUB/DEEP/MORE.BIN", big + 100, 5000, 0, 0) == 0
          && fatimg_add(v, "SUB\\A", "a", 1, 0, 0) == 0, "%s: folders in folders", name);
    n = fatimg_list(v, "", &e);
    CHECK(n == 5 && has(e, n, "EMPTY.TXT", 0, 0) && has(e, n, "SMALL.TXT", 10, 0) && has(e, n, "ONE.BIN", cluster, 0)
          && has(e, n, "BIG.BIN", sizeof big, 0) && has(e, n, "SUB", 0, 1) && e[0].date == 0x5B75 && e[0].time == 0x6000,
          "%s: the root listed: %d entries", name, n);
    free(e), e = NULL;
    n = fatimg_list(v, "SUB", &e);
    CHECK(n == 2 && has(e, n, "DEEP", 0, 1) && has(e, n, "A", 1, 0), "%s: a folder listed: %d entries", name, n);
    free(e), e = NULL;
    CHECK(fatimg_list(v, "sub/deep", &e) == 1 && !strcmp(e[0].dos, "MORE.BIN") && e[0].size == 5000, "%s: a folder in a folder", name);
    free(e), e = NULL;
    CHECK(fatimg_list(v, "NONE", &e) == -1 && fatimg_list(v, "SMALL.TXT", &e) == -1 && fatimg_list(v, "SMALL.TXT\\X", &e) == -1,
          "%s: what is no folder", name);
    /* read back, whole and in parts */
    memset(back, 0, sizeof back);
    CHECK(fatimg_read(v, "BIG.BIN", 0, back, sizeof back) == (long)sizeof big && !memcmp(back, big, sizeof big), "%s: a big file read back", name);
    CHECK(fatimg_read(v, "big.bin", 12345, back, 30000) == 30000 && !memcmp(back, big + 12345, 30000)
          && fatimg_read(v, "BIG.BIN", sizeof big - 7, back, 100) == 7 && !memcmp(back, big + sizeof big - 7, 7)
          && fatimg_read(v, "BIG.BIN", sizeof big, back, 100) == 0 && fatimg_read(v, "BIG.BIN", sizeof big + 5, back, 100) == 0,
          "%s: parts of it, its end, beyond its end", name);
    CHECK(fatimg_read(v, "SMALL.TXT", 0, back, 100) == 10 && !memcmp(back, "ten bytes!", 10) && fatimg_read(v, "EMPTY.TXT", 0, back, 100) == 0
          && fatimg_read(v, "ONE.BIN", 0, back, sizeof back) == (long)cluster && !memcmp(back, big, cluster)
          && fatimg_read(v, "SUB\\DEEP\\MORE.BIN", 4990, back, 100) == 10 && !memcmp(back, big + 5090, 10)
          && fatimg_read(v, "SUB", 0, back, 10) == -1 && fatimg_read(v, "NONE.BIN", 0, back, 10) == -1, "%s: the others; what is no file", name);
    /* names that are not DOS's; what is there already; folders that are not there */
    CHECK(fatimg_add(v, "TOOLONGNAME.TXT", "x", 1, 0, 0) == -1 && fatimg_add(v, "A.B.C", "x", 1, 0, 0) == -1
          && fatimg_add(v, "A.LONG", "x", 1, 0, 0) == -1 && fatimg_add(v, "", "x", 1, 0, 0) == -1 && fatimg_add(v, "A B.TXT", "x", 1, 0, 0) == -1
          && fatimg_add(v, ".TXT", "x", 1, 0, 0) == -1 && fatimg_add(v, "A*.TXT", "x", 1, 0, 0) == -1 && fatimg_add(v, "SMALL.TXT", "x", 1, 0, 0) == -1
          && fatimg_add(v, "Small.Txt", "x", 1, 0, 0) == -1 && fatimg_add(v, "NONE\\A.TXT", "x", 1, 0, 0) == -1
          && fatimg_add(v, "SMALL.TXT\\A.TXT", "x", 1, 0, 0) == -1 && fatimg_mkdir(v, "SUB", 0, 0) == -1 && fatimg_mkdir(v, "NONE\\X", 0, 0) == -1
          && fatimg_mkdir(v, "BAD NAME", 0, 0) == -1, "%s: refused: names, what is there, folders that are not", name);
    /* a folder grown: more entries than its first cluster holds */
    for (i = 0, r = 0; i < 200 && r == 0; i++) {
        sprintf(path, "SUB\\F%03u.DAT", i);
        r = fatimg_add(v, path, path, (unsigned long)strlen(path), 0, 0);
    }
    n = fatimg_list(v, "SUB", &e);
    CHECK(r == 0 && n == 202 && has(e, n, "F199.DAT", 12, 0) && fatimg_read(v, "SUB\\F123.DAT", 0, back, 20) == 12
          && !memcmp(back, "SUB\\F123.DAT", 12), "%s: a folder of %d entries", name, n);
    free(e), e = NULL;
    /* no room: a file larger than what is free leaves nothing; the root folder full */
    fatimg_info(v, &info);
    {
        unsigned long want = (info.free_clusters + 1) * (unsigned long)cluster;
        unsigned char *huge = want < (64ul << 20) ? calloc(1, want) : NULL;
        if (huge) {
            CHECK(fatimg_add(v, "HUGE.BIN", huge, want, 0, 0) == -2, "%s: a file with no room refused", name);
            fatimg_info(v, &after);
            CHECK(after.free_clusters == info.free_clusters && fatimg_read(v, "HUGE.BIN", 0, back, 1) == -1, "%s: ... and nothing of it stays", name);
            free(huge);
        }
    }
    for (i = 0, r = 0; i < 600 && r == 0; i++) {
        sprintf(path, "R%04u.X", i);
        r = fatimg_add(v, path, "r", 1, 0, 0);
    }
    fatimg_info(v, &after);
    /* (the root has its label's entry and the five made above; the smallest floppy has no cluster left before its
       root is full) */
    n = fatimg_list(v, "", &e);
    CHECK(r == -2 && ((i - 1 == after.root_entries - 6 && n == (int)after.root_entries - 1) || (!after.free_clusters && n == (int)i + 4)),
          "%s: the root folder full, or the volume, after %u more files: %u entries, %lu clusters free, %d listed", name, i - 1,
          after.root_entries, after.free_clusters, n);
    free(e), e = NULL;
    /* a disk that fails as it is written: said, and the volume still reads */
    d.fail_writes = 1;
    CHECK(fatimg_add(v, "SUB\\FAIL.BIN", big, 3000, 0, 0) == -2 && fatimg_mkdir(v, "SUB\\FAILDIR", 0, 0) == -1, "%s: a failing disk", name);
    d.fail_writes = 0;
    fatimg_info(v, &info);
    CHECK(info.free_clusters == after.free_clusters && fatimg_read(v, "BIG.BIN", 0, back, sizeof back) == (long)sizeof big,
          "%s: ... takes nothing", name);
    /* (2026-11-23) taken away and renamed: clusters free again, an empty folder only, names DOS's and nobody's */
    {
        struct fatimg_info before, now;
        int listed = fatimg_list(v, "", &e);
        free(e), e = NULL;
        for (i = 0, r = 0; i < 20; i++) {       /* (room first: the root, and perhaps the volume, is full) */
            sprintf(path, "R%04u.X", i);
            r |= fatimg_remove(v, path);
        }
        fatimg_info(v, &now);
        CHECK(r == 0 && fatimg_list(v, "", &e) == listed - 20 && now.free_clusters == info.free_clusters + 20
              && fatimg_read(v, "R0005.X", 0, back, 1) == -1 && fatimg_read(v, "R0020.X", 0, back, 1) == 1,
              "%s: twenty files taken away: %lu clusters free, %lu before", name, now.free_clusters, info.free_clusters);
        free(e), e = NULL;
        fatimg_info(v, &before);
        CHECK(fatimg_add(v, "GONE.BIN", big, 5000, 0, 0) == 0 && fatimg_mkdir(v, "GONEDIR", 0, 0) == 0
              && fatimg_add(v, "GONEDIR\\IN.TXT", "in", 2, 0, 0) == 0, "%s: things to take away", name);
        CHECK(fatimg_remove(v, "GONEDIR") == -1 && fatimg_remove(v, "") == -1 && fatimg_remove(v, "NONE.TXT") == -1
              && fatimg_remove(v, "GONEDIR\\NONE") == -1, "%s: refused: a folder with things in it, the root, nothing", name);
        CHECK(fatimg_remove(v, "gone.bin") == 0 && fatimg_remove(v, "GONEDIR/IN.TXT") == 0 && fatimg_remove(v, "GoneDir") == 0,
              "%s: a file, a file in a folder, the folder emptied", name);
        fatimg_info(v, &now);
        CHECK(now.free_clusters == before.free_clusters && fatimg_read(v, "GONE.BIN", 0, back, 10) == -1
              && fatimg_list(v, "GONEDIR", &e) == -1, "%s: all taken away: %lu free, %lu before", name, now.free_clusters,
              before.free_clusters);
        free(e), e = NULL;
        CHECK(fatimg_rename(v, "SMALL.TXT", "TINY.TXT") == 0 && fatimg_read(v, "tiny.txt", 0, back, 100) == 10
              && !memcmp(back, "ten bytes!", 10) && fatimg_read(v, "SMALL.TXT", 0, back, 10) == -1, "%s: a file renamed", name);
        CHECK(fatimg_rename(v, "TINY.TXT", "ONE.BIN") == -1 && fatimg_rename(v, "TINY.TXT", "A LONG NAME.TXT") == -1
              && fatimg_rename(v, "NONE.TXT", "X.TXT") == -1 && fatimg_rename(v, "TINY.TXT", "tiny.txt") == 0
              && fatimg_rename(v, "TINY.TXT", "SMALL.TXT") == 0, "%s: renames refused (another's, no DOS name, nothing) "
              "and its own name again", name);
        CHECK(fatimg_rename(v, "SUB\\DEEP", "DEEPER") == 0 && fatimg_read(v, "SUB\\DEEPER\\MORE.BIN", 0, back, 5000) == 5000
              && fatimg_rename(v, "SUB\\DEEPER", "DEEP") == 0, "%s: a folder renamed and back", name);
        fatimg_info(v, &now);
        CHECK(now.free_clusters == before.free_clusters, "%s: renames take nothing", name);
    }
    CHECK(fatimg_close(v) == 0, "%s: closed", name);
    {                                           /* its FATs: written, and both the same */
        unsigned fat_sectors = d.b[22] | (unsigned)d.b[23] << 8, reserved = d.b[14] | (unsigned)d.b[15] << 8;
        CHECK(d.b[16] == 2 && !memcmp(d.b + reserved * 512ul, d.b + (reserved + fat_sectors) * 512ul, fat_sectors * 512ul)
              && d.b[reserved * 512ul + 3] != 0, "%s: both FATs the same", name);
    }
    v = fatimg_open(mem_io, &d, why, sizeof why);                      /* opened again: all of it there */
    CHECK(v && fatimg_read(v, "BIG.BIN", 0, back, sizeof back) == (long)sizeof big && !memcmp(back, big, sizeof big)
          && fatimg_read(v, "SUB\\DEEP\\MORE.BIN", 0, back, 5000) == 5000 && !memcmp(back, big + 100, 5000), "%s: opened again", name);
    fatimg_close(v);
    snprintf(path, sizeof path, "%s.img", name);
    save(dir, path, d.b, d.sectors * 512);
    snprintf(path, sizeof path, "%s.big", name);       /* (what is in it, for fatimg_test.sh's comparing) */
    save(dir, path, big, sizeof big);
    snprintf(path, sizeof path, "%s.more", name);
    save(dir, path, big + 100, 5000);
    printf("VOLUME %s %d\n", name, fat);
    /* whatever the volume's bytes: its first sectors, FAT and folders changed at random; opened, listed, read, added to */
    {
        unsigned long r0, k, start = (d.b[14] | (unsigned)d.b[15] << 8) * 512ul, span = 80 * 512ul;
        unsigned char *copy = malloc(d.sectors * 512);
        struct disk dd = { copy, d.sectors, 0 };
        for (r0 = 0; copy && r0 < 300; r0++) {
            memcpy(copy, d.b, d.sectors * 512);
            for (k = 1 + rnd() % 12; k; k--) {
                unsigned long at = rnd() % 3 == 0 ? rnd() % 64 : rnd() % 3 == 0 ? start + rnd() % 2000 : rnd() % (start + span);
                copy[at] = (unsigned char)rnd();
            }
            if ((v = fatimg_open(mem_io, &dd, why, sizeof why)) != NULL) {
                if ((n = fatimg_list(v, "", &e)) > 0)
                    for (i = 0; i < (unsigned)n && i < 12; i++)
                        fatimg_read(v, e[i].dos, rnd() % 100000, back, 5000);
                free(e), e = NULL;
                if ((n = fatimg_list(v, "SUB", &e)) > 0) {
                    snprintf(path, sizeof path, "SUB\\%s", e[rnd() % (unsigned)n].dos);
                    fatimg_read(v, path, 0, back, 20000);
                }
                free(e), e = NULL;
                fatimg_add(v, "SUB\\NEW.BIN", big, rnd() % 20000, 0, 0);
                fatimg_mkdir(v, "NEWDIR", 0, 0);
                fatimg_info(v, &info);
                fatimg_close(v);
            }
        }
        free(copy);
    }
    free(d.b);
}

static int file_io(void *ctx, unsigned long lba, unsigned char *data, unsigned count, int write)
{
    FILE *f = ctx;
    if (fseek(f, (long)(lba * 512), SEEK_SET) != 0)
        return -1;
    return (write ? fwrite(data, 512, count, f) : fread(data, 512, count, f)) == count ? 0 : -1;
}

int main(int argc, char **argv)
{
    if (argc >= 4 && (!strcmp(argv[1], "remove") || !strcmp(argv[1], "rename"))) {     /* (fatimg_test.sh's) */
        FILE *f = fopen(argv[2], "r+b");
        char why[80];
        struct fatimg *v = f ? fatimg_open(file_io, f, why, sizeof why) : NULL;
        int r;
        if (!v)
            return 1;
        r = argv[1][2] == 'm' ? fatimg_remove(v, argv[3]) : argc > 4 ? fatimg_rename(v, argv[3], argv[4]) : -1;
        r |= fatimg_close(v);
        fclose(f);
        return r != 0;
    }
    if (argc >= 4 && (!strcmp(argv[1], "list") || !strcmp(argv[1], "read"))) {
        FILE *f = fopen(argv[2], "rb");
        char why[80];
        struct fatimg *v = f ? fatimg_open(file_io, f, why, sizeof why) : NULL;
        if (!v) {
            printf("not opened: %s\n", f ? why : "no such file");
            return 1;
        }
        if (argv[1][0] == 'l') {
            struct fatimg_entry *e;
            int n = fatimg_list(v, argv[3], &e), i;
            for (i = 0; i < n; i++)
                printf("%c %lu %s %s\n", e[i].dir ? 'D' : 'F', e[i].size, e[i].dos, e[i].name);
            return n < 0;
        } else {
            static unsigned char b[200000];
            long n = fatimg_read(v, argv[3], 0, b, sizeof b);
            FILE *o = argc > 4 && n >= 0 ? fopen(argv[4], "wb") : NULL;
            if (!o)
                return 1;
            fwrite(b, 1, (size_t)n, o);
            fclose(o);
            return 0;
        }
    }
    {
        const char *dir = argc > 1 ? argv[1] : ".";
        static unsigned char junk[4096];
        struct disk d = { junk, 8, 0 };
        char why[80];
        unsigned i;
        a_volume(dir, "f1440", 2880, 2, 18, 0, 12);
        a_volume(dir, "f360", 720, 2, 9, 0, 12);
        a_volume(dir, "h16m", 32768, 16, 63, 0x80, 16);
        for (i = 0; i < sizeof junk; i++)
            junk[i] = (unsigned char)rnd();
        CHECK(!fatimg_open(mem_io, &d, why, sizeof why) && why[0], "what is no volume: %s", why);
        memset(junk, 0, sizeof junk);
        CHECK(!fatimg_open(mem_io, &d, why, sizeof why), "a disk of nothing");
        d.sectors = 0;
        CHECK(!fatimg_open(mem_io, &d, why, sizeof why) && !strcmp(why, "its first sector can't be read"), "a disk that can't be read: %s", why);
    }
    printf("fatimg_test: %d failures (%d checks)\n", fails, checks);
    return fails != 0;
}
