/* fsv_test: kernel/fs/fsv.c (N1d: many callers on a FAT volume) on a volume in memory made by kernel/disk's
 * fat_format: one file through two handles, a cut under another handle's feet, sharing, an open file kept from
 * deletion and renaming, an open file's entry staying right while its folder fills, empties, is renamed and is
 * moved, the open-file limit, read-only files and handles, a folder listed while it changes, and the lock taken
 * for every call, never twice, always given back. "fsv_test: 0 failures" when all held. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../kernel/disk/disk.h"
#include "../../kernel/fs/fsv.h"

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

#define SECTORS 65536UL                 /* 32 MB: FAT16 */

static unsigned char *disk;
static int held, takes;

static int rd(void *ctx, unsigned long lba, unsigned char *buf, unsigned count)
{
    (void)ctx;
    if (lba + count > SECTORS)
        return 1;
    memcpy(buf, disk + lba * 512, count * 512UL);
    return 0;
}

static int wr(void *ctx, unsigned long lba, const unsigned char *buf, unsigned count)
{
    (void)ctx;
    if (lba + count > SECTORS)
        return 1;
    memcpy(disk + lba * 512, buf, count * 512UL);
    return 0;
}

static unsigned long now(void *ctx)
{
    (void)ctx;
    return (unsigned long)((46u << 9) | (11u << 5) | 21u) << 16;
}

static void lk_take(void *ctx)
{
    (void)ctx;
    if (held)
        printf("FAIL lock: taken while held\n"), fails++;
    held = 1;
    takes++;
}

static void lk_give(void *ctx)
{
    (void)ctx;
    if (!held)
        printf("FAIL lock: given back while not held\n"), fails++;
    held = 0;
}

static const struct fsv_lock lk = { lk_take, lk_give, 0 };
static struct fsv_vol vol;

static void fill(unsigned char *p, unsigned long n, unsigned seed)
{
    unsigned long i;

    for (i = 0; i < n; i++)
        p[i] = (unsigned char)(i * 31 + seed * 7 + (i >> 9));
}

static void fresh(void)
{
    struct fat_geo g = { SECTORS, 0, 16, 63, 0x80, 0x12345678UL, "FSV TEST", 16 };
    struct fat_made m;
    int r;

    memset(disk, 0, SECTORS * 512);
    r = fat_format(&g, wr, 0, &m);
    CHECK(r == 0, "format: %d", r);
    r = fsv_mount(&vol, rd, wr, now, 0, 437, &lk);
    CHECK(r == 0, "mount: %d", r);
}

static void t_two_handles(void)
{
    static unsigned char a[30000], b[30000];
    struct fsv_file x, y;
    long n;

    fresh();
    CHECK(fsv_create(&vol, "\\shared.bin", 0, 0, FSV_RW, FSV_DENY_NONE, &x) == 0, "create");
    CHECK(fsv_open(&vol, "\\shared.bin", FSV_READ, FSV_DENY_NONE, &y) == 0, "open again");
    CHECK(x.n == y.n, "two nodes for one file");
    fill(a, 10000, 1);
    CHECK(fsv_write(&x, a, 10000) == 10000, "write");
    CHECK(fsv_size(&y) == 10000, "the other handle's size: %lu", fsv_size(&y));
    n = fsv_read(&y, b, 30000);
    CHECK(n == 10000 && !memcmp(a, b, 10000), "the other handle's read: %ld", n);
    /* y's place is deep in the chain; x cuts the file short, then writes new data past where y was. */
    fsv_seek(&x, 100);
    CHECK(fsv_truncate(&x) == 0 && fsv_size(&y) == 100, "cut: %lu", fsv_size(&y));
    fsv_seek(&y, 8000);
    CHECK(fsv_read(&y, b, 100) == 0, "past the cut: data");
    /* Another file takes the freed clusters first, so the file grows again into others: y's old place in the
       chain is now another file's. */
    {
        struct fsv_file o;

        CHECK(fsv_create(&vol, "\\other.bin", 0, 0, FSV_RW, FSV_DENY_NONE, &o) == 0, "create other");
        fill(b, 9000, 9);
        CHECK(fsv_write(&o, b, 9000) == 9000, "write other");
        fsv_close(&o);
    }
    fill(a, 20000, 2);
    fsv_seek(&x, 0);
    CHECK(fsv_write(&x, a, 20000) == 20000, "write again");
    fsv_seek(&y, 15000);
    n = fsv_read(&y, b, 1000);
    CHECK(n == 1000 && !memcmp(a + 15000, b, 1000), "the new data through the old handle: %ld", n);
    fsv_seek(&y, 0);
    n = fsv_read(&y, b, 30000);
    CHECK(n == 20000 && !memcmp(a, b, 20000), "all of it: %ld", n);
    CHECK(fsv_write(&y, a, 1) == FAT_EACCES, "write on a read-only handle");
    CHECK(fsv_close(&x) == 0 && fsv_close(&y) == 0, "close");
    CHECK(fsv_close(&y) == FSV_EBADF, "closed twice");
    CHECK(fsv_read(&y, b, 1) == FSV_EBADF, "read when closed");
}

static void t_sharing(void)
{
    struct fsv_file a, b, c, d;

    fresh();
    CHECK(fsv_create(&vol, "\\s.dat", 0, 0, FSV_RW, FSV_DENY_WRITE, &a) == 0, "create");
    CHECK(fsv_open(&vol, "\\s.dat", FSV_READ, FSV_DENY_NONE, &b) == 0, "a reader beside deny-write");
    CHECK(fsv_open(&vol, "\\s.dat", FSV_WRITE, FSV_DENY_NONE, &c) == FSV_ESHARE, "a writer beside deny-write");
    CHECK(fsv_open(&vol, "\\s.dat", FSV_READ, FSV_DENY_READ, &c) == FSV_ESHARE, "deny-read beside readers");
    CHECK(fsv_open(&vol, "\\s.dat", FSV_READ, FSV_DENY_ALL, &c) == FSV_ESHARE, "deny-all beside others");
    fsv_close(&a);
    CHECK(fsv_open(&vol, "\\s.dat", FSV_WRITE, FSV_DENY_NONE, &c) == 0, "a writer once deny-write is gone");
    CHECK(fsv_open(&vol, "\\s.dat", FSV_READ, FSV_DENY_WRITE, &d) == FSV_ESHARE, "deny-write beside a writer");
    fsv_close(&b);
    fsv_close(&c);
    CHECK(fsv_open(&vol, "\\s.dat", FSV_READ, FSV_DENY_ALL, &d) == 0, "deny-all alone");
    CHECK(fsv_open(&vol, "\\s.dat", FSV_READ, FSV_DENY_NONE, &a) == FSV_ESHARE, "anything beside deny-all");
    fsv_close(&d);

    /* An open file can't go, be renamed, made again or have its attributes changed; closed, it can. */
    CHECK(fsv_open(&vol, "\\s.dat", FSV_READ, FSV_DENY_NONE, &a) == 0, "open");
    CHECK(fsv_unlink(&vol, "\\s.dat") == FSV_ESHARE, "delete while open");
    CHECK(fsv_rename(&vol, "\\s.dat", "\\t.dat") == FSV_ESHARE, "rename while open");
    CHECK(fsv_create(&vol, "\\s.dat", 0, 1, FSV_RW, FSV_DENY_NONE, &b) == FSV_ESHARE, "made again while open");
    CHECK(fsv_setattr(&vol, "\\s.dat", FAT_A_RDONLY) == FSV_ESHARE, "attributes while open");
    fsv_close(&a);
    CHECK(fsv_setattr(&vol, "\\s.dat", FAT_A_RDONLY) == 0, "attributes when closed");
    CHECK(fsv_open(&vol, "\\s.dat", FSV_RW, FSV_DENY_NONE, &a) == FAT_EACCES, "write access to a read-only file");
    CHECK(fsv_create(&vol, "\\s.dat", 0, 1, FSV_RW, FSV_DENY_NONE, &a) == FAT_EACCES, "a read-only file made again");
    CHECK(fsv_setattr(&vol, "\\s.dat", 0) == 0, "attributes back");
    CHECK(fsv_rename(&vol, "\\s.dat", "\\t.dat") == 0, "rename when closed");
    CHECK(fsv_unlink(&vol, "\\t.dat") == 0, "delete when closed");
    CHECK(fsv_open(&vol, "\\", FSV_READ, FSV_DENY_NONE, &a) == FAT_EISDIR, "the root opened");
}

static void t_entry_stays(void)
{
    static unsigned char a[5000], b[5000];
    struct fsv_file x;
    struct fat_ent e;
    char path[200];
    int i;

    fresh();
    CHECK(fsv_mkdir(&vol, "\\folder") == 0, "mkdir");
    for (i = 0; i < 20; i++) {
        struct fsv_file t;

        snprintf(path, sizeof path, "\\folder\\before %02d.txt", i);
        CHECK(fsv_create(&vol, path, 0, 0, FSV_RW, FSV_DENY_NONE, &t) == 0, "create %s", path);
        fsv_write(&t, path, strlen(path));
        fsv_close(&t);
    }
    CHECK(fsv_create(&vol, "\\folder\\open.bin", 0, 0, FSV_RW, FSV_DENY_NONE, &x) == 0, "create the open one");
    fill(a, 3000, 3);
    fsv_write(&x, a, 3000);
    /* The folder fills past a cluster, empties, its entries renamed; then it is renamed and moved. */
    for (i = 0; i < 120; i++) {
        struct fsv_file t;

        snprintf(path, sizeof path, "\\folder\\a rather long name, number %03d.txt", i);
        CHECK(fsv_create(&vol, path, 0, 0, FSV_RW, FSV_DENY_NONE, &t) == 0, "create %s", path);
        fsv_close(&t);
    }
    for (i = 0; i < 20; i += 2) {
        snprintf(path, sizeof path, "\\folder\\before %02d.txt", i);
        CHECK(fsv_unlink(&vol, path) == 0, "delete %s", path);
    }
    CHECK(fsv_rename(&vol, "\\folder\\before 01.txt", "\\folder\\renamed to something much longer.txt") == 0,
          "rename a neighbour");
    CHECK(fsv_rename(&vol, "\\folder", "\\renamed folder") == 0, "rename the folder");
    CHECK(fsv_mkdir(&vol, "\\outer") == 0, "mkdir outer");
    CHECK(fsv_rename(&vol, "\\renamed folder", "\\outer\\moved") == 0, "move the folder");
    /* The open file goes on: its entry is still its own. */
    fill(a + 3000, 2000, 4);
    CHECK(fsv_write(&x, a + 3000, 2000) == 2000, "write after it all");
    fsv_close(&x);
    CHECK(fsv_lookup(&vol, "\\outer\\moved\\open.bin", &e) == 0 && e.size == 5000, "its entry: size %lu",
          e.size);
    {
        struct fsv_file y;

        CHECK(fsv_open(&vol, "\\outer\\moved\\open.bin", FSV_READ, FSV_DENY_NONE, &y) == 0, "open where it is now");
        CHECK(fsv_read(&y, b, 5000) == 5000 && !memcmp(a, b, 5000), "its data");
        fsv_close(&y);
    }
    CHECK(fsv_lookup(&vol, "\\outer\\moved\\before 03.txt", &e) == 0 && e.size == strlen("\\folder\\before 03.txt"),
          "a neighbour's entry");
    CHECK(fsv_lookup(&vol, "\\outer\\moved\\renamed to something much longer.txt", &e) == 0, "the renamed one");
}

static void t_limit(void)
{
    static struct fsv_file h[FSV_NODES + 2];
    char path[40];
    int i, r;

    fresh();
    for (i = 0; i < FSV_NODES; i++) {
        snprintf(path, sizeof path, "\\f%03d", i);
        r = fsv_create(&vol, path, 0, 0, FSV_RW, FSV_DENY_NONE, &h[i]);
        CHECK(r == 0, "create %s: %d", path, r);
    }
    CHECK(fsv_create(&vol, "\\one too many", 0, 0, FSV_RW, FSV_DENY_NONE, &h[FSV_NODES]) == FSV_ENFILE,
          "past the limit");
    CHECK(fsv_open(&vol, "\\f000", FSV_READ, FSV_DENY_NONE, &h[FSV_NODES + 1]) == 0, "an open file again: shared");
    fsv_close(&h[FSV_NODES + 1]);
    fsv_close(&h[0]);
    CHECK(fsv_open(&vol, "\\one too many", FSV_READ, FSV_DENY_NONE, &h[0]) == 0, "room again (made before)");
    for (i = 0; i < FSV_NODES; i++)
        fsv_close(&h[i]);
}

static void t_listing(void)
{
    struct fat_dir d;
    struct fat_ent e;
    char path[60], seen[40];
    int i, r, n = 0;

    fresh();
    fsv_mkdir(&vol, "\\list");
    for (i = 0; i < 40; i++) {
        struct fsv_file t;

        snprintf(path, sizeof path, "\\list\\stay%02d", i);
        fsv_create(&vol, path, 0, 0, FSV_RW, FSV_DENY_NONE, &t);
        fsv_close(&t);
    }
    memset(seen, 0, sizeof seen);
    CHECK(fsv_opendir(&vol, "\\list", &d) == 0, "opendir");
    while ((r = fsv_readdir(&vol, &d, &e)) > 0) {
        int k;

        if (sscanf(e.name, "stay%d", &k) == 1 && k >= 0 && k < 40)
            seen[k]++;
        if (++n % 5 == 0) {             /* others change the folder as it is listed */
            struct fsv_file t;

            snprintf(path, sizeof path, "\\list\\new%03d with a long name", n);
            fsv_create(&vol, path, 0, 0, FSV_RW, FSV_DENY_NONE, &t);
            fsv_close(&t);
            snprintf(path, sizeof path, "\\list\\new%03d with a long name", n - 5);
            fsv_unlink(&vol, path);
        }
    }
    CHECK(r == 0, "readdir: %d", r);
    for (i = 0; i < 40; i++)
        CHECK(seen[i] == 1, "stay%02d seen %d times", i, seen[i]);
}

int main(void)
{
    disk = malloc(SECTORS * 512);
    if (!disk)
        return 2;
    t_two_handles();
    t_sharing();
    t_entry_stays();
    t_limit();
    t_listing();
    CHECK(!held, "the lock left held");
    CHECK(takes > 100, "the lock taken %d times", takes);
    printf("fsv_test: %d failures\n", fails);
    return fails != 0;
}
