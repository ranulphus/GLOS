/* fat_test: kernel/fs/fat.c (N1a, reading) on the host, against volumes others made (tests/host/fat_test.sh).
 *
 *     fat_test format IMAGE SECTORS FAT          a new volume by kernel/disk's fat_format (FAT 12, 16, 32 or 0)
 *     fat_test check IMAGE MANIFEST CP LABEL     the volume read through kernel/fs: every file of MANIFEST
 *                                                ("path<TAB>size<TAB>crc32", '/' between parts, UTF-8) found by
 *                                                walking the folders, with that size and CRC, read in odd pieces
 *                                                and with seeks; no file the manifest hasn't got; each found by
 *                                                its short names too and in upper case; the label LABEL ("-":
 *                                                none); "FREE n" (bytes) printed for the script to compare
 *     fat_test write IMAGE MIRROR [CUT]          N1b: one script of writes (files of every size around a cluster,
 *                                                odd pieces, gaps, overwrites, appends, truncation both ways, long,
 *                                                Unicode and colliding names, a folder over many clusters, the
 *                                                root filled, renames within and across folders, folders moved,
 *                                                deletion, attributes, times, the disk filled) done through
 *                                                kernel/fs on IMAGE and through the host's own calls in folder
 *                                                MIRROR, for the script to compare (mtools, fsck.fat); the short
 *                                                names Windows 9x gives checked. "WRITES n": sector writes made;
 *                                                "CUTTABLE n": those before the disk is filled (cut runs skip it).
 *                                                CUT: writes after the CUT-th dropped (a power cut), for fsck.fat
 *                                                to judge; MIRROR then unused
 * "fat_test: 0 failures" at the end when all held. */
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
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

/* ---- writing (N1b) */

static const char *mirror;
static unsigned long writes, cut;

/* Sector writes counted, and dropped after the cut (the cache in fat.c keeps them: the program goes on). */
static int wr_counted(void *ctx, unsigned long lba, const unsigned char *data, unsigned count)
{
    if (cut && writes++ >= cut)
        return 0;
    if (!cut)
        writes++;
    return wr(ctx, lba, data, count);
}

/* 2026-11-21 12:34:56, DOS's way. */
static unsigned long fixed_now(void *ctx)
{
    (void)ctx;
    return (unsigned long)((46u << 9) | (11u << 5) | 21u) << 16 | (12u << 11) | (34u << 5) | 28u;
}

/* The mirror's path of a DOS path ("\a\b" -> MIRROR/a/b). */
static void host_path(char *out, size_t size, const char *dos)
{
    size_t n = (size_t)snprintf(out, size, "%s/", mirror), i;

    for (i = 0; dos[i] && n + 1 < size; i++)
        if (!(i == 0 && (dos[i] == '\\')))
            out[n++] = dos[i] == '\\' ? '/' : dos[i];
    out[n] = 0;
}

static unsigned long rnd_state = 20261121;

static unsigned rnd(unsigned n)
{
    rnd_state = rnd_state * 1103515245UL + 12345;
    return (unsigned)((rnd_state >> 8) % n);
}

static void fill_bytes(unsigned char *p, unsigned long n, unsigned seed)
{
    unsigned long i, x = seed * 2654435761UL + 1;

    for (i = 0; i < n; i++) {
        x = x * 1664525UL + 1013904223UL;
        p[i] = (unsigned char)(x >> 13);
    }
}

static void expect(const char *what, const char *path, int got, int want)
{
    char d[1200];

    if (got != want) {
        snprintf(d, sizeof d, "%s: %d, want %d", path, got, want);
        fail(what, d);
    }
}

/* path written: n bytes of seed's, from offset at (the file created when `create`), in pieces of up to `piece`
   (0: random sizes). The mirror the same. */
static void put_file(struct fat_vol *v, const char *path, int create, unsigned long at, unsigned long n,
                     unsigned seed, unsigned long piece)
{
    static unsigned char buf[1 << 20];
    struct fat_file f;
    unsigned long done = 0;
    char hp[2048];
    int r, fd;

    if (n > sizeof buf)
        n = sizeof buf;
    fill_bytes(buf, n, seed);
    r = create ? fat_create(v, path, 0, &f) : fat_open(v, path, &f);
    if (r) {
        expect(create ? "create" : "open", path, r, 0);
        return;
    }
    fat_seek(&f, at);
    while (done < n) {
        unsigned long k = piece ? piece : 1 + rnd(rnd(3) ? 700 : 20000);
        long w;

        if (k > n - done)
            k = n - done;
        w = fat_write(&f, buf + done, k);
        if (w != (long)k) {
            char d[2100];

            snprintf(d, sizeof d, "%s: wrote %ld of %lu at %lu", path, w, k, done);
            fail("write", d);
            break;
        }
        done += k;
    }
    expect("close", path, fat_close(&f), 0);
    if (cut)
        return;
    host_path(hp, sizeof hp, path);
    fd = open(hp, O_WRONLY | O_CREAT, 0644);
    if (fd < 0 || pwrite(fd, buf, done, (off_t)at) != (ssize_t)done)
        fail("mirror write", hp);
    if (fd >= 0)
        close(fd);
}

static void do_mkdir(struct fat_vol *v, const char *path)
{
    char hp[2048];

    expect("mkdir", path, fat_mkdir(v, path), 0);
    host_path(hp, sizeof hp, path);
    if (!cut)
        mkdir(hp, 0755);
}

static void do_unlink(struct fat_vol *v, const char *path)
{
    char hp[2048];

    expect("unlink", path, fat_unlink(v, path), 0);
    host_path(hp, sizeof hp, path);
    if (!cut)
        unlink(hp);
}

static void do_rename(struct fat_vol *v, const char *from, const char *to)
{
    char a[2048], b[2048];
    unsigned long at = writes;

    expect("rename", from, fat_rename(v, from, to), 0);
    if (!cut)                                   /* (the writes it made: fat_test.sh cuts at each of them too) */
        printf("RENAMED %lu %lu\n", at + 1, writes);
    host_path(a, sizeof a, from);
    host_path(b, sizeof b, to);
    if (!cut)
        rename(a, b);
}

static void do_truncate(struct fat_vol *v, const char *path, unsigned long size)
{
    struct fat_file f;
    char hp[2048];

    if (fat_open(v, path, &f)) {
        expect("open", path, 1, 0);
        return;
    }
    expect("truncate", path, fat_truncate(&f, size), 0);
    host_path(hp, sizeof hp, path);
    if (!cut && truncate(hp, (off_t)size))
        fail("mirror truncate", hp);
}

static void want_short(struct fat_vol *v, const char *path, const char *sname, int has_long)
{
    struct fat_ent e;
    char d[1200];

    if (fat_lookup(v, path, &e)) {
        fail("short name: no entry", path);
        return;
    }
    if (strcmp(e.sname, sname) || e.has_long != has_long) {
        snprintf(d, sizeof d, "%s: %s%s, want %s%s", path, e.sname, e.has_long ? " +long" : "", sname,
                 has_long ? " +long" : "");
        fail("short name", d);
    }
}

static int write_test(const char *image, const char *mirror_dir, unsigned long cut_at)
{
    struct fat_vol v;
    struct fat_file f;
    struct fat_ent e;
    unsigned long csize, free0, free1, i, n;
    char path[1200], name[600];
    int r;

    mirror = mirror_dir;
    cut = cut_at;
    img = fopen(image, "r+b");
    if (!img) {
        fail("image", image);
        return 1;
    }
    r = fat_mount_rw(&v, rd, wr_counted, fixed_now, 0, 850);
    if (r) {
        printf("FAIL mount: %d\n", r);
        return 1;
    }
    csize = v.spc * 512UL;
    fat_free(&v, &free0);
    printf("VOLUME fat=%d spc=%u clusters=%lu free=%lu\n", v.fat, v.spc, v.clusters, free0);

    /* Names, and the short names Windows 9x gives them. */
    put_file(&v, "\\short.txt", 1, 0, 100, 1, 0);
    put_file(&v, "\\README", 1, 0, 0, 2, 0);
    put_file(&v, "\\A Longer Name With Spaces.data", 1, 0, 5000, 3, 0);
    put_file(&v, "\\many.dots.in.this.name.txt", 1, 0, 1234, 4, 0);
    put_file(&v, "\\.hidden", 1, 0, 77, 5, 0);
    put_file(&v, "\\Ünïcödé Ελληνικά кириллица.txt", 1, 0, 4321, 6, 0);
    put_file(&v, "\\café.txt", 1, 0, 10, 7, 0);
    put_file(&v, "\\x+y=z.txt", 1, 0, 11, 8, 0);
    for (i = 1; i <= 6; i++) {
        snprintf(path, sizeof path, "\\Long File Name %lu.txt", i);
        put_file(&v, path, 1, 0, 50 + i, 10 + (unsigned)i, 0);
    }
    memset(name, 'n', 255);
    name[255] = 0;
    memcpy(name, "\\Two hundred and fifty-five", 27);
    put_file(&v, name, 1, 0, 255, 17, 0);
    if (!cut) {
        /* A folder of the volume's own name is that name, as under DOS: the label's entry is no file's (DISK n
           INSTALL's \GLOS on a volume named GLOS was GLOS~1 until 2026-11-23). Made and removed again, its writes
           not counted: the cuts' places stay where they were. */
        if (v.label[0] && strcmp(v.label, "NO NAME") && !strchr(v.label, ' ') && strlen(v.label) <= 8) {
            unsigned long w0 = writes;

            snprintf(path, sizeof path, "\\%s", v.label);
            expect("mkdir of the label's name", path, fat_mkdir(&v, path), 0);
            want_short(&v, path, v.label, 0);
            expect("rmdir of it", path, fat_rmdir(&v, path), 0);
            writes = w0;
        }
        want_short(&v, "\\short.txt", "SHORT.TXT", 1);
        want_short(&v, "\\README", "README", 0);
        want_short(&v, "\\A Longer Name With Spaces.data", "ALONGE~1.DAT", 1);
        want_short(&v, "\\many.dots.in.this.name.txt", "MANYDO~1.TXT", 1);
        want_short(&v, "\\.hidden", "HIDDEN~1", 1);
        want_short(&v, "\\x+y=z.txt", "X_Y_Z~1.TXT", 1);
        want_short(&v, "\\Long File Name 1.txt", "LONGFI~1.TXT", 1);
        want_short(&v, "\\Long File Name 6.txt", "LONGFI~6.TXT", 1);
        want_short(&v, "\\café.txt", "CAF\x90.TXT", 1);
        expect("create twice", "\\README", fat_create(&v, "\\readme", 0, &f), FAT_EEXIST);
        expect("bad name", "\\a*b", fat_create(&v, "\\a*b", 0, &f), FAT_EINVAL);
        expect("in a file", "\\README\\x", fat_create(&v, "\\README\\x", 0, &f), FAT_ENOTDIR);
        expect("no folder", "\\nope\\x", fat_create(&v, "\\nope\\x", 0, &f), FAT_ENOENT);
    }

    /* Sizes around a cluster, written in pieces of every kind. */
    do_mkdir(&v, "\\sizes");
    {
        const unsigned long sizes[] = { 0, 1, 511, 512, 513, csize - 1, csize, csize + 1, 3 * csize + 7,
                                        40000, 200000 };
        const unsigned long pieces[] = { 0, 1, 512, 4096, 0, 0, 1000, 0, 0, 65536, 0 };

        for (i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
            snprintf(path, sizeof path, "\\sizes\\s%07lu.bin", sizes[i]);
            if (fat_lookup(&v, path, &e) == FAT_OK)
                continue;                       /* (512-byte clusters: the same sizes twice) */
            put_file(&v, path, 1, 0, sizes[i], 100 + (unsigned)i, pieces[i]);
        }
    }

    /* Folders deep, and one over many clusters of entries (holes made, then filled by long names). */
    do_mkdir(&v, "\\deep");
    do_mkdir(&v, "\\deep\\er");
    do_mkdir(&v, "\\deep\\er\\and deeper");
    put_file(&v, "\\deep\\er\\and deeper\\leaf.bin", 1, 0, 999, 200, 0);
    put_file(&v, "\\deep\\er\\sibling.txt", 1, 0, 1, 201, 0);
    do_mkdir(&v, "\\many");
    n = v.clusters > 10000 ? 300 : 120;
    for (i = 0; i < n; i++) {
        snprintf(path, sizeof path, "\\many\\file number %03lu.txt", i);
        put_file(&v, path, 1, 0, rnd(600), 300 + (unsigned)i, 0);
    }
    for (i = 0; i < n; i += 3) {
        snprintf(path, sizeof path, "\\many\\file number %03lu.txt", i);
        do_unlink(&v, path);
    }
    for (i = 0; i < n / 4; i++) {
        snprintf(path, sizeof path, "\\many\\a later and rather longer name, number %03lu.txt", i);
        put_file(&v, path, 1, 0, rnd(300), 700 + (unsigned)i, 0);
    }

    /* Gaps, overwrites, appends, truncation. */
    put_file(&v, "\\sparse.bin", 1, 10000, 100, 900, 0);
    put_file(&v, "\\sizes\\s0040000.bin", 0, 1000, 3000, 901, 0);
    put_file(&v, "\\sizes\\s0040000.bin", 0, 39000, 5000, 902, 0);
    put_file(&v, "\\sizes\\s0000001.bin", 0, 1, csize * 2, 903, 777);
    do_truncate(&v, "\\sizes\\s0200000.bin", 1000);
    do_truncate(&v, "\\sizes\\s0200000.bin", 5000);
    do_truncate(&v, "\\sizes\\s0200000.bin", csize);
    do_truncate(&v, "\\sizes\\s0000513.bin", 0);
    do_truncate(&v, "\\README", 3);

    /* Renames: within a folder, across, case only, folders moved (their ".." follows); refusals. */
    do_rename(&v, "\\short.txt", "\\a much longer name now.txt");
    do_rename(&v, "\\A Longer Name With Spaces.data", "\\SHORTER.DAT");
    do_rename(&v, "\\README", "\\ReadMe");
    do_rename(&v, "\\many.dots.in.this.name.txt", "\\deep\\er\\moved here.txt");
    do_rename(&v, "\\deep\\er\\and deeper", "\\moved folder");
    do_rename(&v, "\\moved folder", "\\deep\\moved back");
    if (!cut) {
        expect("into itself", "\\deep", fat_rename(&v, "\\deep", "\\deep\\er\\x"), FAT_EINVAL);
        expect("onto another", "\\ReadMe", fat_rename(&v, "\\ReadMe", "\\SHORTER.DAT"), FAT_EEXIST);
        expect("the root", "\\", fat_rename(&v, "\\", "\\x"), FAT_EACCES);
        if (!fat_lookup(&v, "\\deep", &e)) {
            struct fat_ent d2;

            r = fat_lookup(&v, "\\deep\\moved back\\..", &d2);
            expect("\"..\" is its new parent", "\\deep\\moved back", r == FAT_OK && d2.cluster == e.cluster, 1);
        }
        want_short(&v, "\\ReadMe", "README", 1);
    }

    /* Removal, attributes and times. */
    do_mkdir(&v, "\\empty");
    if (!cut) {
        expect("rmdir full", "\\deep", fat_rmdir(&v, "\\deep"), FAT_ENOTEMPTY);
        expect("rmdir a file", "\\ReadMe", fat_rmdir(&v, "\\ReadMe"), FAT_ENOTDIR);
        expect("unlink a folder", "\\deep", fat_unlink(&v, "\\deep"), FAT_EISDIR);
        expect("rmdir the root", "\\", fat_rmdir(&v, "\\"), FAT_EACCES);
    }
    expect("rmdir", "\\empty", fat_rmdir(&v, "\\empty"), 0);
    if (!cut) {
        char hp[2048];

        host_path(hp, sizeof hp, "\\empty");
        rmdir(hp);
    }
    expect("setattr", "\\ReadMe", fat_setattr(&v, "\\ReadMe", FAT_A_RDONLY | FAT_A_HIDDEN), 0);
    if (!cut) {
        expect("unlink read-only", "\\ReadMe", fat_unlink(&v, "\\ReadMe"), FAT_EACCES);
        r = fat_lookup(&v, "\\ReadMe", &e);
        expect("attributes", "\\ReadMe", r ? -1 : e.attr, FAT_A_RDONLY | FAT_A_HIDDEN);
    }
    expect("setattr", "\\ReadMe", fat_setattr(&v, "\\ReadMe", FAT_A_ARCHIVE), 0);
    expect("settime", "\\x+y=z.txt", fat_settime(&v, "\\x+y=z.txt", (20u << 9) | (6u << 5) | 15u, 0x6000), 0);
    if (!cut) {
        r = fat_lookup(&v, "\\x+y=z.txt", &e);
        expect("time", "\\x+y=z.txt", r ? -1 : e.wdate == ((20u << 9) | (6u << 5) | 15u) && e.wtime == 0x6000, 1);
        r = fat_lookup(&v, "\\café.txt", &e);
        expect("written now", "\\café.txt", r ? -1 : (e.wdate == (fixed_now(0) >> 16)), 1);
    }

    /* The root filled (FAT12 and FAT16: its fixed entries; FAT32's grows), then emptied. */
    for (i = 0; i < 600; i++) {
        snprintf(path, sizeof path, "\\R%05lu", i);
        r = fat_create(&v, path, 0, &f);
        if (r)
            break;
        if (!cut) {
            char hp[2048];
            int fd;

            host_path(hp, sizeof hp, path);
            fd = open(hp, O_WRONLY | O_CREAT, 0644);
            if (fd >= 0)
                close(fd);
        }
    }
    if (!cut)
        expect("root full", "\\", v.fat == 32 ? r : r == FAT_ENOSPC && i < 600 ? 0 : r, 0);
    while (i--) {
        snprintf(path, sizeof path, "\\R%05lu", i);
        do_unlink(&v, path);
    }

    /* The disk filled: the last write short, nothing free; then given back. (Not in a power-cut run: the writes
       before it are the ones cut.) */
    printf("CUTTABLE %lu\n", writes);
    fat_free(&v, &free1);
    if (!cut) {
        static unsigned char buf[65536];
        unsigned long total = 0;
        long w;
        char hp[2048];
        int fd;

        expect("create", "\\fill.bin", fat_create(&v, "\\fill.bin", 0, &f), 0);
        host_path(hp, sizeof hp, "\\fill.bin");
        fd = open(hp, O_WRONLY | O_CREAT, 0644);
        fill_bytes(buf, sizeof buf, 999);
        for (;;) {
            w = fat_write(&f, buf, sizeof buf);
            if (w > 0) {
                if (pwrite(fd, buf, (size_t)w, (off_t)total) != w)
                    fail("mirror write", hp);
                total += (unsigned long)w;
            }
            if (w != (long)sizeof buf)
                break;
        }
        close(fd);
        expect("disk full", "\\fill.bin", w < 0 ? (int)w : 0, w < 0 ? FAT_ENOSPC : 0);
        fat_free(&v, &n);
        expect("nothing free", "\\fill.bin", (int)n, 0);
        expect("filled", "\\fill.bin", total == free1 * csize, 1);
        if (v.free_count != 0xFFFFFFFFUL)
            expect("free count", "\\fill.bin", (int)v.free_count, 0);
        do_unlink(&v, "\\fill.bin");
        fat_free(&v, &n);
        expect("given back", "\\fill.bin", n == free1, 1);
    }

    fat_free(&v, &n);
    if (!cut && v.free_count != 0xFFFFFFFFUL && v.free_count != n) {
        char d[100];

        snprintf(d, sizeof d, "kept %lu, FAT %lu", v.free_count, n);
        fail("free count", d);
    }
    expect("flush", "", fat_flush(&v), 0);
    printf("WRITES %lu\n", writes);
    fclose(img);
    return 0;
}

/* label IMAGE: the volume's label through fat.c's calls, as DOS's FCB calls (16h, 17h, 13h), 440Dh 0846h and
   LABEL.EXE change it, ending with "GLOS LABEL" in the root folder and the boot sector and serial number
   1234-ABCD: fat_test.sh then asks mtools and fsck.fat what they make of the volume. */
static int label_test(const char *image)
{
    struct fat_vol v;
    struct fat_dir d;
    struct fat_ent e;
    unsigned char n[11];
    int r, seen = 0;

    img = fopen(image, "r+b");
    if (!img) {
        fail("image", image);
        return 1;
    }
    if ((r = fat_mount_rw(&v, rd, wr, fixed_now, 0, 850)) != 0) {
        printf("FAIL mount: %d\n", r);
        return 1;
    }
    r = fat_label_delete(&v);                   /* (the volume's own, if it has one) */
    if (r && r != FAT_ENOENT)
        printf("FAIL delete the first: %d\n", r), fails++;
    if (fat_label_get(&v, n, 0) != 0 || fat_label_delete(&v) != FAT_ENOENT)
        fail("label", "one left after deleting");
    if (!r && strcmp(v.label, "NO NAME"))
        printf("FAIL boot sector after deleting: \"%s\"\n", v.label), fails++;
    if (fat_label_make(&v, (const unsigned char *)"FIRST      ") != 0 || fat_label_get(&v, n, 0) != 1
        || memcmp(n, "FIRST      ", 11) || strcmp(v.label, "FIRST"))
        printf("FAIL make: \"%s\"\n", v.label), fails++;
    if (fat_label_make(&v, (const unsigned char *)"SECOND     ") != FAT_EEXIST)
        fail("make", "a second label");
    if (fat_label_rename(&v, (const unsigned char *)"RENAMED    ") != 0 || fat_label_get(&v, n, 0) != 1
        || memcmp(n, "RENAMED    ", 11) || strcmp(v.label, "FIRST"))
        printf("FAIL rename: \"%.11s\" boot \"%s\"\n", n, v.label), fails++;
    if (fat_label_rename(&v, (const unsigned char *)"BAD?NAME   ") != FAT_EINVAL
        || fat_label_rename(&v, (const unsigned char *)"           ") != FAT_EINVAL)
        fail("rename", "a '?' or blanks taken");
    if (fat_set_media(&v, 0x1234ABCDUL, (const unsigned char *)"MEDIA ID   ", 0) != 0 || v.serial != 0x1234ABCDUL
        || strcmp(v.label, "MEDIA ID") || fat_label_get(&v, n, 0) != 1 || memcmp(n, "RENAMED    ", 11))
        printf("FAIL set media: \"%s\" %08lx\n", v.label, v.serial), fails++;
    if (fat_label_delete(&v) != 0 || fat_label_make(&v, (const unsigned char *)"GLOS LABEL ") != 0)
        fail("label", "the last made");
    if (fat_opendir(&v, "\\", &d) == 0)         /* (no folder listing shows it) */
        while (fat_readdir(&d, &e) == 1)
            seen += (e.attr & FAT_A_LABEL) != 0;
    if (seen)
        fail("readdir", "the label listed");
    if ((r = fat_flush(&v)) != 0)
        printf("FAIL flush: %d\n", r), fails++;
    fclose(img);
    printf("LABEL fat=%d\n", v.fat);
    return 0;
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
    if ((argc == 4 || argc == 5) && !strcmp(argv[1], "write")) {
        write_test(argv[2], argv[3], argc == 5 ? strtoul(argv[4], 0, 10) : 0);
        printf("fat_test: %d failures\n", fails);
        return fails != 0;
    }
    if (argc == 3 && !strcmp(argv[1], "label")) {
        label_test(argv[2]);
        printf("fat_test: %d failures\n", fails);
        return fails != 0;
    }
    fprintf(stderr, "fat_test format IMAGE SECTORS FAT | check IMAGE MANIFEST CP LABEL | write IMAGE MIRROR [CUT]"
            " | label IMAGE\n");
    return 2;
}
