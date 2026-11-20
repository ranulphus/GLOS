/* fat_test: kernel/fs/fat.c (N1a, reading) on the host, against volumes others made (tests/host/fat_test.sh).
 *
 *     fat_test format IMAGE SECTORS FAT          a new volume by kernel/disk's fat_format (FAT 12, 16, 32 or 0)
 *     fat_test check IMAGE MANIFEST CP LABEL     the volume read through kernel/fs: every file of MANIFEST
 *                                                ("path<TAB>size<TAB>crc32", '/' between parts, UTF-8) found by
 *                                                walking the folders, with that size and CRC, read in odd pieces
 *                                                and with seeks; no file the manifest hasn't got; each found by
 *                                                its short names too and in upper case; the label LABEL ("-":
 *                                                none); "FREE n" (bytes) printed for the script to compare
 * "fat_test: 0 failures" at the end when all held. */
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <glos/cp.h>
#include "../../kernel/disk/disk.h"
#include "../../kernel/fs/fat.h"

static int fails;
static FILE *img;

static void fail(const char *what, const char *detail)
{
    printf("FAIL %s: %s\n", what, detail);
    fails++;
}

static int rd(void *ctx, unsigned long lba, unsigned char *buf, unsigned count)
{
    (void)ctx;
    if (fseeko(img, (off_t)lba * 512, SEEK_SET))
        return 1;
    return fread(buf, 512, count, img) != count;
}

static int wr(void *ctx, unsigned long lba, const unsigned char *data, unsigned count)
{
    (void)ctx;
    if (fseeko(img, (off_t)lba * 512, SEEK_SET))
        return 1;
    return fwrite(data, 512, count, img) != count;
}

static unsigned long crc_table[256];

static unsigned long crc32(unsigned long crc, const unsigned char *p, unsigned long n)
{
    unsigned long i;

    if (!crc_table[1])
        for (i = 0; i < 256; i++) {
            unsigned long c = i;
            int k;

            for (k = 0; k < 8; k++)
                c = c & 1 ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
            crc_table[i] = c;
        }
    crc ^= 0xFFFFFFFFUL;
    while (n--)
        crc = crc_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFUL;
}

/* ---- the manifest */

struct want {
    char path[1024];
    unsigned long size, crc;
    int seen;
};
static struct want *wants;
static int nwants;

static void load(const char *path)
{
    FILE *f = fopen(path, "r");
    char line[1200];

    if (!f) {
        fail("manifest", path);
        return;
    }
    while (fgets(line, sizeof line, f)) {
        char *t1 = strchr(line, '\t'), *t2 = t1 ? strchr(t1 + 1, '\t') : 0;

        if (!t2)
            continue;
        wants = realloc(wants, (nwants + 1) * sizeof *wants);
        *t1 = 0;
        snprintf(wants[nwants].path, sizeof wants[nwants].path, "%s", line);
        wants[nwants].size = strtoul(t1 + 1, 0, 10);
        wants[nwants].crc = strtoul(t2 + 1, 0, 16);
        wants[nwants].seen = 0;
        nwants++;
    }
    fclose(f);
}

static struct want *wanted(const char *path)
{
    int i;

    for (i = 0; i < nwants; i++)
        if (!strcmp(wants[i].path, path))
            return &wants[i];
    return 0;
}

/* ---- one file */

static unsigned long read_all(struct fat_vol *v, const char *path, unsigned long *size, int *err)
{
    static unsigned char buf[70000];
    static const unsigned pieces[] = { 1, 511, 512, 513, 4096, 65536, 7, 1000 };
    struct fat_file f;
    unsigned long crc = 0, total = 0;
    long n;
    int k = 0, r = fat_open(v, path, &f);

    *err = r;
    if (r)
        return 0;
    while ((n = fat_read(&f, buf, pieces[k++ % 8])) > 0) {
        crc = crc32(crc, buf, (unsigned long)n);
        total += (unsigned long)n;
    }
    if (n < 0)
        *err = (int)n;
    *size = total;
    /* A seek back and a read across what came before: the same bytes as a fresh read. */
    if (!*err && total > 3) {
        unsigned char a[3000], b[3000];
        unsigned long at = total / 3, len = total - at < sizeof a ? total - at : sizeof a;
        struct fat_file g;

        fat_seek(&f, at);
        if (fat_read(&f, a, len) != (long)len)
            *err = FAT_ECORRUPT;
        if (!*err && !fat_open(v, path, &g)) {
            unsigned long skip = at;

            while (skip) {
                unsigned long s = skip < sizeof buf ? skip : sizeof buf;

                if (fat_read(&g, buf, s) != (long)s) {
                    *err = FAT_ECORRUPT;
                    break;
                }
                skip -= s;
            }
            if (!*err && (fat_read(&g, b, len) != (long)len || memcmp(a, b, len)))
                *err = FAT_ECORRUPT;
        }
    }
    return crc;
}

static void upper_ascii(char *s)
{
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z')
            *s -= 32;
}

/* ---- the walk */

static int files, folders;

static void walk(struct fat_vol *v, const char *dos, const char *rel, const char *shortp, int depth)
{
    struct fat_dir d;
    struct fat_ent e;
    int r = fat_opendir(v, dos, &d);

    if (r) {
        fail("opendir", dos);
        return;
    }
    if (depth > 40) {
        fail("depth", dos);
        return;
    }
    folders++;
    while ((r = fat_readdir(&d, &e)) > 0) {
        char ndos[2048], nrel[2048], nshort[2048], detail[2300], sname[64];

        if (!strcmp(e.name, ".") || !strcmp(e.name, ".."))
            continue;
        snprintf(ndos, sizeof ndos, "%s\\%s", dos, e.name);
        snprintf(nrel, sizeof nrel, "%s%s%s", rel, *rel ? "/" : "", e.name);
        cp_to_utf8(v->cp, e.sname, sname, sizeof sname);
        snprintf(nshort, sizeof nshort, "%s\\%s", shortp, sname);
        if (e.attr & FAT_A_DIR) {
            walk(v, ndos, nrel, nshort, depth + 1);
            continue;
        }
        files++;
        {
            struct want *w = wanted(nrel);
            unsigned long size = 0, crc;
            int err;
            struct fat_ent by_short, by_upper;
            char up[2048];

            crc = read_all(v, ndos, &size, &err);
            if (!w) {
                fail("not in the manifest", nrel);
                continue;
            }
            w->seen++;
            if (err) {
                snprintf(detail, sizeof detail, "%s: error %d", nrel, err);
                fail("read", detail);
                continue;
            }
            if (size != w->size || size != e.size || crc != w->crc) {
                snprintf(detail, sizeof detail, "%s: %lu bytes (entry %lu) crc %08lx, want %lu crc %08lx", nrel,
                         size, e.size, crc, w->size, w->crc);
                fail("contents", detail);
            }
            if (fat_lookup(v, nshort, &by_short) || by_short.cluster != e.cluster || by_short.index != e.index)
                fail("by short names", nshort);
            snprintf(up, sizeof up, "%s", ndos);
            upper_ascii(up);
            if (fat_lookup(v, up, &by_upper) || by_upper.index != e.index || by_upper.dir != e.dir)
                fail("in upper case", up);
        }
    }
    if (r < 0)
        fail("readdir", dos);
}

static int check(const char *image, const char *manifest, int cp, const char *label)
{
    struct fat_vol v;
    struct fat_ent e;
    unsigned long free;
    int r, i;

    img = fopen(image, "rb");
    if (!img) {
        fail("image", image);
        return 1;
    }
    load(manifest);
    r = fat_mount(&v, rd, 0, cp);
    if (r) {
        printf("FAIL mount: %d\n", r);
        return 1;
    }
    printf("VOLUME fat=%d spc=%u clusters=%lu label=%s serial=%08lx\n", v.fat, v.spc, v.clusters, v.label,
           v.serial);
    if (strcmp(label, "-") ? strcmp(v.label, label) : v.label[0] && strcmp(v.label, "NO NAME"))
        fail("label", v.label);
    walk(&v, "", "", "", 0);
    for (i = 0; i < nwants; i++)
        if (wants[i].seen != 1)
            fail(wants[i].seen ? "found twice" : "not found", wants[i].path);
    if (fat_lookup(&v, "\\no such file", &e) != FAT_ENOENT)
        fail("missing", "no FAT_ENOENT");
    if (nwants && fat_lookup(&v, wants[0].path, &e) == FAT_OK && !(e.attr & FAT_A_DIR)) {
        char beyond[1100];

        snprintf(beyond, sizeof beyond, "%s\\x", wants[0].path);
        if (fat_lookup(&v, beyond, &e) != FAT_ENOTDIR)
            fail("past a file", "no FAT_ENOTDIR");
    }
    if (fat_free(&v, &free))
        fail("free", "error");
    printf("FREE %lu\n", free * v.spc * 512);
    printf("fat_test: %d files, %d folders\n", files, folders);
    fclose(img);
    return 0;
}

static int format(const char *image, unsigned long sectors, int fat)
{
    struct fat_geo g = { sectors, 0, 16, 63, 0x80, 0x1A2B3C4DUL, "GLOS N1A", fat };
    struct fat_made m;
    int r;

    img = fopen(image, "w+b");
    if (!img || ftruncate(fileno(img), (off_t)sectors * 512))
        return 1;
    r = fat_format(&g, wr, 0, &m);
    printf("FORMAT fat=%d spc=%u clusters=%lu: %d\n", m.fat, m.cluster_sectors, m.clusters, r);
    fclose(img);
    return r != 0;
}

int main(int argc, char **argv)
{
    if (argc == 5 && !strcmp(argv[1], "format"))
        return format(argv[2], strtoul(argv[3], 0, 10), atoi(argv[4]));
    if (argc == 6 && !strcmp(argv[1], "check")) {
        check(argv[2], argv[3], atoi(argv[4]), argv[5]);
        printf("fat_test: %d failures\n", fails);
        return fails != 0;
    }
    fprintf(stderr, "fat_test format IMAGE SECTORS FAT | check IMAGE MANIFEST CP LABEL\n");
    return 2;
}
