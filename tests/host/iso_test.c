/* iso_test: kernel/fs/iso.c (N4's CD-ROM, C3) against an image made by tools/ndos/mkiso.py from a folder this test
 * writes: every file found by walking the image is the same, byte for byte, as the one it came from, and every
 * one is found; lookups (case, "." and "..", slashes) and their errors; reads at odd places across sectors; a
 * folder of more than a sector of records; an image that isn't ISO 9660. Run from the repository's root (it calls
 * python3 tools/ndos/mkiso.py). "iso_test: 0 failures" when all held. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../../kernel/fs/iso.h"

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

#define TOP "build/host/iso"
#define SRC TOP "/src"

static FILE *img;
static unsigned long reads;

static int f_read(void *ctx, unsigned long lba, unsigned count, unsigned char *buf)
{
    (void)ctx;
    reads++;
    return fseek(img, (long)(lba * ISO_SECTOR), SEEK_SET) != 0 || fread(buf, ISO_SECTOR, count, img) != count;
}

static int zero_read(void *ctx, unsigned long lba, unsigned count, unsigned char *buf)
{
    (void)ctx, (void)lba;
    memset(buf, 0, (size_t)count * ISO_SECTOR);
    return 0;
}

static void put(const char *path, const unsigned char *data, unsigned long n)
{
    char p[256];
    FILE *f;
    snprintf(p, sizeof p, SRC "/%s", path);
    f = fopen(p, "wb");
    if (!f || fwrite(data, 1, n, f) != n) {
        printf("FAIL: %s not written\n", p);
        fails++;
    }
    if (f)
        fclose(f);
}

static unsigned char *slurp(const char *path, unsigned long *n)
{
    FILE *f = fopen(path, "rb");
    unsigned char *b;
    long len;
    if (!f)
        return 0;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc(len ? (size_t)len : 1);
    *n = fread(b, 1, (size_t)len, f);
    fclose(f);
    return b;
}

static int nfiles, ndirs;
static const char *src_top = SRC;

/* Every file under dir (its path on the image in at) compared with the source's. */
static void walk(struct iso_vol *v, const struct iso_entry *dir, const char *at)
{
    struct iso_entry e;
    unsigned long pos = 0, n = 0, i;
    char path[256], src[300];
    int r;
    while ((r = iso_next(v, dir, &pos, &e)) == ISO_OK) {
        snprintf(path, sizeof path, "%s/%s", at, e.name);
        if (e.dir) {
            ndirs++;
            CHECK(e.attr == 0x11, "%s: a folder's attributes %02x", path, e.attr);
            walk(v, &e, path);
            continue;
        }
        nfiles++;
        snprintf(src, sizeof src, "%s%s", src_top, path);
        {
            unsigned char *want = slurp(src, &n), *got = malloc(e.size + 1);
            long k = iso_read(v, &e, 0, got, e.size + 100);
            CHECK(want && (unsigned long)k == n && e.size == n && !memcmp(want, got, n), "%s: read %ld of %lu", path, k, n);
            CHECK(e.attr == 0x01, "%s: a file's attributes %02x", path, e.attr);
            CHECK(e.date >> 9 >= 46, "%s: its date %04x (a year past 2025)", path, e.date);
            for (i = 0; e.name[i]; i++)
                CHECK(e.name[i] != ';', "%s: a version left in its name", path);
            free(want);
            free(got);
        }
    }
    CHECK(r == 1, "%s: the folder's walk ended %d", at, r);
}

int main(int argc, char **argv)
{
    struct iso_vol v;
    struct iso_dev dev = { f_read, 0 };
    struct iso_entry e, d;
    unsigned char data[5000], b[6000];
    unsigned long pos = 0, i;
    int n;
    char name[32];

    if (argc > 1) {                             /* iso_test IMAGE [SRC]: another maker's image walked (and compared) */
        img = fopen(argv[1], "rb");
        if (!img || iso_mount(&v, &dev) != ISO_OK) {
            printf("iso_test: %s: not mounted\n", argv[1]);
            return 1;
        }
        src_top = argc > 2 ? argv[2] : 0;
        if (!src_top) {
            struct iso_entry q;
            unsigned long p2 = 0;
            while (iso_next(&v, &v.root, &p2, &q) == ISO_OK)
                printf("  %-12s %s %lu\n", q.name, q.dir ? "<DIR>" : "     ", q.size);
        } else
            walk(&v, &v.root, "");
        printf("iso_test: %s: label [%s], %d files, %d folders compared, %lu names passed over, %d failures\n", argv[1],
               v.label, nfiles, ndirs, v.skipped, fails);
        return fails != 0;
    }
    if (system("rm -rf " TOP " && mkdir -p " SRC "/SUB/DEEP " SRC "/EMPTY") != 0)
        return 1;
    for (i = 0; i < sizeof data; i++)
        data[i] = (unsigned char)(i * 131 + (i >> 7));
    put("README.TXT", (const unsigned char *)"hello, CD\r\n", 11);
    put("SUB/DATA.BIN", data, sizeof data);
    put("SUB/DEEP/X.TXT", (const unsigned char *)"deep", 4);
    put("NOEXT", (const unsigned char *)"x", 1);
    put("EMPTY.DAT", (const unsigned char *)"", 0);
    put("SECTOR.BIN", data, 2048);
    for (n = 1; n <= 90; n++) {
        snprintf(name, sizeof name, "SUB/F%d.TXT", n);
        put(name, (const unsigned char *)name, strlen(name));
    }
    if (system("python3 tools/ndos/mkiso.py " TOP "/a.iso " SRC " --label MY_CD > /dev/null") != 0) {
        printf("FAIL: mkiso.py\n");
        return 1;
    }
    img = fopen(TOP "/a.iso", "rb");
    CHECK(img != 0, "the image");
    if (!img)
        return 1;
    CHECK(iso_mount(&v, &dev) == ISO_OK, "mounted");
    CHECK(!strcmp(v.label, "MY_CD"), "its label [%s]", v.label);
    walk(&v, &v.root, "");
    CHECK(nfiles == 96 && ndirs == 3, "every file found: %d files, %d folders", nfiles, ndirs);
    CHECK(v.skipped == 0, "no names passed over: %lu", v.skipped);
    for (n = 0; iso_lookup(&v, "\\SUB", &d) == ISO_OK && iso_next(&v, &d, &pos, &e) == ISO_OK;)
        n++;
    CHECK(n == 92 && d.size > ISO_SECTOR, "a folder of more than a sector: %d entries, %lu bytes", n, d.size);
    CHECK(iso_lookup(&v, "\\", &e) == ISO_OK && e.dir && e.lba == v.root.lba, "\\: the root");
    CHECK(iso_lookup(&v, "\\sub\\deep\\x.txt", &e) == ISO_OK && e.size == 4, "a file, any case");
    CHECK(iso_lookup(&v, "sub/deep/../data.bin", &e) == ISO_OK && e.size == 5000, "slashes, ..");
    CHECK(iso_lookup(&v, "\\.\\SUB\\.\\F7.TXT", &e) == ISO_OK, ". in a path");
    CHECK(iso_lookup(&v, "\\NOEXT", &e) == ISO_OK && e.size == 1 && !strcmp(e.name, "NOEXT"), "a name with no "
          "extension: %s", e.name);
    CHECK(iso_lookup(&v, "\\EMPTY.DAT", &e) == ISO_OK && e.size == 0 && iso_read(&v, &e, 0, b, 10) == 0, "an empty file");
    CHECK(iso_lookup(&v, "\\NONE.TXT", &e) == ISO_ENOENT, "a file that isn't there: ENOENT");
    CHECK(iso_lookup(&v, "\\NOWHERE\\A.TXT", &e) == ISO_ENOPATH, "a folder that isn't there: ENOPATH");
    CHECK(iso_lookup(&v, "\\README.TXT\\A", &e) == ISO_ENOPATH, "a file as a folder: ENOPATH");
    CHECK(iso_lookup(&v, "\\TOOLONGNAME.TXT", &e) == ISO_ENOENT, "a name past 8.3: ENOENT");
    CHECK(iso_lookup(&v, "\\EMPTY", &e) == ISO_OK && e.dir, "an empty folder");
    pos = 0;
    CHECK(iso_next(&v, &e, &pos, &d) == 1, "... with nothing in it");
    iso_lookup(&v, "\\SUB\\DATA.BIN", &e);
    CHECK(iso_read(&v, &e, 1000, b, 3000) == 3000 && !memcmp(b, data + 1000, 3000), "a read across a sector's end");
    CHECK(iso_read(&v, &e, 4990, b, 100) == 10 && !memcmp(b, data + 4990, 10), "a read past the end: the rest");
    CHECK(iso_read(&v, &e, 5000, b, 10) == 0, "a read at the end: 0");
    iso_lookup(&v, "\\SECTOR.BIN", &e);
    reads = 0;
    CHECK(iso_read(&v, &e, 0, b, 2048) == 2048 && !memcmp(b, data, 2048) && reads == 1, "a whole sector: one read (%lu)",
          reads);
    fclose(img);
    dev.read = zero_read;
    CHECK(iso_mount(&v, &dev) == ISO_ENOTISO, "an image that isn't ISO 9660");
    printf("iso_test: %d failures\n", fails);
    return fails != 0;
}
