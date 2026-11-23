/* diskjob_test OUTDIR: Disk Tools' long jobs (apps/shared/diskjob.c) on a disk in memory whose sectors can be
 * told to fail: always, a number of times, on writing, or by keeping other bytes than those written. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../apps/shared/diskjob.h"
#include "../../kernel/disk/disk.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define SECTORS 1000u                   /* (not a multiple of the 32 a run has: the last run is 8) */

struct disk {
    unsigned char data[SECTORS * 512];
    unsigned char unreadable[SECTORS];  /* reads fail so many times more (255: for ever) */
    unsigned char unwritable[SECTORS];  /* ... writes */
    unsigned char forgets[SECTORS];     /* a write is taken, the old bytes kept */
    unsigned long reads, writes, runs;  /* sectors read and written; calls */
    unsigned most;                      /* the most sectors a call asked for */
};

static int disk_io(void *ctx, unsigned long lba, unsigned char *data, unsigned count, int write)
{
    struct disk *d = ctx;
    unsigned i;
    int bad = 0;
    d->runs++;
    if (count > d->most)
        d->most = count;
    if (lba >= SECTORS || count > SECTORS - lba)
        return 1;
    for (i = 0; i < count; i++) {
        unsigned char *left = write ? &d->unwritable[lba + i] : &d->unreadable[lba + i];
        if (*left) {
            if (*left != 255)
                --*left;
            bad = 1;
        }
    }
    if (bad)
        return 4;                       /* (as INT 13h: nothing of the run is to be trusted) */
    if (write) {
        for (i = 0; i < count; i++)
            if (!d->forgets[lba + i])
                memcpy(d->data + (lba + i) * 512, data + i * 512, 512);
        d->writes += count;
    } else {
        memcpy(data, d->data + lba * 512, (size_t)count * 512);
        d->reads += count;
    }
    return 0;
}

/* (2026-11-23) A floppy's full format: a disk of 2880 sectors whose tracks are formatted by a call that notes
   them, some of their sectors failing the read back; and a volume made on it, its bad sectors' clusters marked. */
#define FL_SECTORS 2880u
static unsigned char fl[FL_SECTORS * 512];
static unsigned fl_tracks, fl_order_bad, fl_fail_at = 999;
static unsigned long long fl_bad[160];

static int fl_track(void *ctx, unsigned cyl, unsigned head, unsigned long long *bad)
{
    unsigned t = cyl * 2 + head;
    (void)ctx;
    if (t != fl_tracks)
        fl_order_bad = 1;
    if (t == fl_fail_at)
        return 3;                       /* (write-protected, as INT 13h says it) */
    memset(fl + t * 18 * 512, 0xF6, 18 * 512);
    *bad = fl_bad[t];
    fl_tracks++;
    return 0;
}

static int fl_io(void *ctx, unsigned long lba, unsigned char *data, unsigned count, int write)
{
    (void)ctx;
    if (lba >= FL_SECTORS || count > FL_SECTORS - lba)
        return 4;
    if (write)
        memcpy(fl + lba * 512, data, (size_t)count * 512);
    else
        memcpy(data, fl + lba * 512, (size_t)count * 512);
    return 0;
}

static int fl_write(void *ctx, unsigned long lba, const unsigned char *data, unsigned count)
{
    return fl_io(ctx, lba, (unsigned char *)(size_t)data, count, 1);
}

static unsigned fat12_get(unsigned long fat_lba, unsigned cl)
{
    const unsigned char *f = fl + fat_lba * 512 + cl * 3 / 2;
    unsigned v = (unsigned)f[0] | (unsigned)f[1] << 8;
    return cl & 1 ? v >> 4 : v & 0xFFF;
}

static int fl_stop(void *arg, unsigned long done, unsigned long total)
{
    (void)arg, (void)total;
    return done >= 360;
}

static void full_format(void)
{
    struct diskjob_report r;
    struct fat_geo g;
    struct fat_made made;
    int err, rc;
    unsigned long bad[3];
    unsigned cl, marked = 0, k;
    memset(fl_bad, 0, sizeof fl_bad);
    fl_bad[0] = 0;
    fl_bad[10] = 1ull << 4;             /* track 10's fifth sector: 10 * 18 + 4 = 184 */
    fl_bad[159] = 1ull << 17 | 1ull;    /* the last track's first and last: 2862 and 2879 */
    fl_tracks = 0, fl_order_bad = 0, fl_fail_at = 999;
    rc = diskjob_format(fl_track, NULL, 80, 2, 18, NULL, NULL, &r, &err);
    CHECK(rc == 0 && err == 0 && fl_tracks == 160 && !fl_order_bad && r.done == FL_SECTORS);
    CHECK(r.bad == 3 && r.first_bad == 184 && r.bad_list[0] == 184 && r.bad_list[1] == 2862 && r.bad_list[2] == 2879);
    memset(&g, 0, sizeof g);
    g.sectors = FL_SECTORS, g.heads = 2, g.spt = 18;
    CHECK(fat_format(&g, fl_write, NULL, &made) == 0 && made.fat == 12);
    rc = diskjob_mark_bad(fl_io, NULL, r.bad_list, (unsigned)r.bad);
    CHECK(rc == 0);
    for (k = 0; k < 2; k++) {           /* both FATs: the three clusters bad, every other one as it was */
        unsigned long at = 1 + k * made.fat_sectors;
        for (cl = 2, marked = 0; cl < made.clusters + 2; cl++) {
            unsigned v = fat12_get(at, cl);
            unsigned long first = made.data_start + (cl - 2) * made.cluster_sectors;
            int want = (184 >= first && 184 < first + made.cluster_sectors) || (2862 >= first && 2862 < first + made.cluster_sectors)
                       || (2879 >= first && 2879 < first + made.cluster_sectors);
            if (want)
                marked++;
            CHECK(v == (want ? 0xFF7u : 0));
        }
        CHECK(marked == 3);
        CHECK(fat12_get(at, 0) == 0xFF0 && fat12_get(at, 1) == 0xFFF);   /* (the media byte's entries kept) */
    }
    /* a bad sector where the file system is: refused, nothing written */
    bad[0] = 3;
    CHECK(diskjob_mark_bad(fl_io, NULL, bad, 1) == -1);
    /* a track that can't be formatted: stopped there, its error said */
    fl_tracks = 0, fl_fail_at = 7;
    rc = diskjob_format(fl_track, NULL, 80, 2, 18, NULL, NULL, &r, &err);
    CHECK(rc == -1 && err == 3 && fl_tracks == 7 && strstr(r.why, "error 03h at cylinder 3, side 1"));
    /* stopped by the tick (at sector 360: the twenty-first track's start) */
    fl_tracks = 0, fl_fail_at = 999;
    rc = diskjob_format(fl_track, NULL, 80, 2, 18, fl_stop, NULL, &r, &err);
    CHECK(rc == -1 && r.stopped && fl_tracks == 20 && err == 0);
}

static unsigned long seed = 12345;
static unsigned rnd(void)
{
    seed = seed * 1103515245u + 12345u;
    return (unsigned)(seed >> 16) & 0x7FFF;
}

static struct disk *disk_new(void)
{
    struct disk *d = calloc(1, sizeof *d);
    unsigned long i;
    for (i = 0; i < sizeof d->data; i++)
        d->data[i] = (unsigned char)rnd();
    return d;
}

struct ticks {
    unsigned long calls, last, total, stop_at;
    int order;                          /* never went back */
};

static int tick(void *arg, unsigned long done, unsigned long total)
{
    struct ticks *t = arg;
    if (t->calls && done < t->last)
        t->order = 0;
    t->calls++;
    t->last = done;
    t->total = total;
    return t->stop_at && done >= t->stop_at;
}

static unsigned char *slurp(const char *name, unsigned long *n)
{
    FILE *f = fopen(name, "rb");
    unsigned char *p;
    long size;
    *n = 0;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    rewind(f);
    p = malloc((size_t)size + 1);
    *n = fread(p, 1, (size_t)size, f);
    fclose(f);
    return p;
}

static void spit(const char *name, const unsigned char *p, unsigned long n)
{
    FILE *f = fopen(name, "wb");
    fwrite(p, 1, n, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    char name[512], other[512];
    struct diskjob_report r;
    struct ticks t;
    struct disk *d;
    unsigned char *got, zero[512] = { 0 };
    unsigned long n, i;
    FILE *f;
    int rc;
    if (argc < 2)
        return 2;
    snprintf(name, sizeof name, "%s/disk.img", argv[1]);
    snprintf(other, sizeof other, "%s/other.img", argv[1]);

    /* a sound disk to a file */
    d = disk_new();
    memset(&t, 0, sizeof t), t.order = 1;
    f = fopen(name, "wb");
    rc = diskjob_to_file(disk_io, d, SECTORS, 0, f, tick, &t, &r);
    fclose(f);
    got = slurp(name, &n);
    CHECK(rc == 0 && r.done == SECTORS && r.bad == 0 && !r.stopped && !r.why[0]);
    CHECK(n == SECTORS * 512 && !memcmp(got, d->data, n));
    CHECK(d->most == 32 && d->reads == SECTORS && d->runs == 32 && d->writes == 0);
    CHECK(t.order && t.last == SECTORS && t.total == SECTORS && t.calls == 33);
    free(got);

    /* three sectors that can't be read (two in one run), one that can at the third try, one at the fourth (so: not) */
    d->unreadable[5] = 255, d->unreadable[20] = 255, d->unreadable[999] = 255;
    d->unreadable[100] = 3;             /* the run's read, then two more: the third alone gets it */
    d->unreadable[200] = 4;
    f = fopen(name, "wb");
    rc = diskjob_to_file(disk_io, d, SECTORS, 0, f, NULL, NULL, &r);
    fclose(f);
    got = slurp(name, &n);
    CHECK(rc == 0 && r.done == SECTORS && r.bad == 4 && r.first_bad == 5);
    CHECK(r.bad_list[0] == 5 && r.bad_list[1] == 20 && r.bad_list[2] == 200 && r.bad_list[3] == 999);
    CHECK(n == SECTORS * 512);
    for (i = 0; i < SECTORS; i++) {
        int lost = i == 5 || i == 20 || i == 200 || i == 999;
        if (memcmp(got + i * 512, lost ? zero : d->data + i * 512, 512))
            break;
    }
    CHECK(i == SECTORS);                /* every sector at its place: the lost ones zeros, the rest (100 too) the disk's */
    free(got);

    /* the check, of the same disk: 200 has healed (its count ran out), three stay */
    rc = diskjob_check(disk_io, d, SECTORS, 0, NULL, NULL, &r);
    CHECK(rc == -1 && r.done == SECTORS && r.bad == 3 && r.first_bad == 5 && r.bad_list[2] == 999 && !r.stopped);
    d->unreadable[5] = d->unreadable[20] = d->unreadable[999] = 0;
    memset(&t, 0, sizeof t), t.order = 1;
    rc = diskjob_check(disk_io, d, SECTORS, 0, tick, &t, &r);
    CHECK(rc == 0 && r.bad == 0 && r.done == SECTORS && t.last == SECTORS && t.order);
    /* more bad sectors than the list holds: all counted, the first sixteen kept */
    for (i = 300; i < 340; i++)
        d->unreadable[i] = 255;
    rc = diskjob_check(disk_io, d, SECTORS, 0, NULL, NULL, &r);
    CHECK(rc == -1 && r.bad == 40 && r.first_bad == 300 && r.bad_list[15] == 315);
    for (i = 300; i < 340; i++)
        d->unreadable[i] = 0;

    /* by tracks of 18: a call each, and the same bytes */
    d->runs = d->reads = 0, d->most = 0;
    f = fopen(name, "wb");
    rc = diskjob_to_file(disk_io, d, SECTORS, 18, f, NULL, NULL, &r);
    fclose(f);
    got = slurp(name, &n);
    CHECK(rc == 0 && r.bad == 0 && d->most == 18 && d->runs == 56 && d->reads == SECTORS && n == SECTORS * 512
          && !memcmp(got, d->data, n));
    free(got);
    d->runs = 0;
    rc = diskjob_check(disk_io, d, SECTORS, 18, NULL, NULL, &r);
    CHECK(rc == 0 && d->runs == 56 && r.done == SECTORS);
    d->runs = 0;
    rc = diskjob_check(disk_io, d, SECTORS, 99, NULL, NULL, &r);       /* (more than a call's most: its most) */
    CHECK(rc == 0 && d->runs == 32 && d->most == 32);

    /* stopped on the way */
    memset(&t, 0, sizeof t), t.order = 1, t.stop_at = 320;
    f = fopen(name, "wb");
    rc = diskjob_to_file(disk_io, d, SECTORS, 0, f, tick, &t, &r);
    fclose(f);
    CHECK(rc == -1 && r.stopped && r.done == 320 && !r.why[0]);
    memset(&t, 0, sizeof t), t.order = 1, t.stop_at = 64;
    rc = diskjob_check(disk_io, d, SECTORS, 0, tick, &t, &r);
    CHECK(rc == -1 && r.stopped && r.done == 64 && r.bad == 0);

    /* a file that can't be written (opened for reading) */
    f = fopen(name, "rb");
    rc = diskjob_to_file(disk_io, d, SECTORS, 0, f, NULL, NULL, &r);
    fclose(f);
    CHECK(rc == -1 && !r.stopped && strstr(r.why, "could not be written"));

    /* an image written to a disk and read back the same */
    {
        struct disk *to = disk_new();           /* (other bytes: the generator has moved on) */
        spit(name, d->data, SECTORS * 512);
        CHECK(memcmp(to->data, d->data, sizeof d->data) != 0);
        memset(&t, 0, sizeof t), t.order = 1;
        f = fopen(name, "rb");
        rc = diskjob_from_file(disk_io, to, SECTORS, 0, f, 1, tick, &t, &r);
        fclose(f);
        CHECK(rc == 0 && r.bad == 0 && r.done == SECTORS && !r.why[0]);
        CHECK(!memcmp(to->data, d->data, sizeof d->data));
        CHECK(to->writes == SECTORS && to->reads == SECTORS && to->most == 32);
        CHECK(t.order && t.last == SECTORS && t.calls == 65);

        /* not verified: no sector read */
        memset(to->data, 0, sizeof to->data), to->reads = to->writes = 0;
        f = fopen(name, "rb");
        rc = diskjob_from_file(disk_io, to, SECTORS, 0, f, 0, NULL, NULL, &r);
        fclose(f);
        CHECK(rc == 0 && to->reads == 0 && to->writes == SECTORS && !memcmp(to->data, d->data, sizeof d->data));

        /* a sector that can't be written; one written at the second try */
        memset(to->data, 0, sizeof to->data);
        to->unwritable[77] = 255, to->unwritable[400] = 2;
        f = fopen(name, "rb");
        rc = diskjob_from_file(disk_io, to, SECTORS, 0, f, 0, NULL, NULL, &r);
        fclose(f);
        CHECK(rc == -1 && r.bad == 1 && r.first_bad == 77 && r.done == SECTORS && r.why[0] && !r.stopped);
        CHECK(!memcmp(to->data + 400 * 512, d->data + 400 * 512, 512));
        CHECK(!memcmp(to->data + 76 * 512, d->data + 76 * 512, 512) && !memcmp(to->data + 78 * 512, d->data + 78 * 512, 512));
        /* ... verified: read back as it isn't, and not counted a second time for that */
        f = fopen(name, "rb");
        rc = diskjob_from_file(disk_io, to, SECTORS, 0, f, 1, NULL, NULL, &r);
        fclose(f);
        CHECK(rc == -1 && r.bad == 1 && r.bad_list[0] == 77);
        to->unwritable[77] = 0;

        /* a sector that takes the write and keeps what it had: only the verifying sees it */
        memset(to->data, 0x55, sizeof to->data);
        to->forgets[901] = 1;
        f = fopen(name, "rb");
        rc = diskjob_from_file(disk_io, to, SECTORS, 0, f, 0, NULL, NULL, &r);
        fclose(f);
        CHECK(rc == 0 && r.bad == 0);
        f = fopen(name, "rb");
        rc = diskjob_from_file(disk_io, to, SECTORS, 0, f, 1, NULL, NULL, &r);
        fclose(f);
        CHECK(rc == -1 && r.bad == 1 && r.first_bad == 901 && strstr(r.why, "as the image has them"));
        to->forgets[901] = 0;
        /* ... and one that can't be read back */
        to->unreadable[902] = 255;
        f = fopen(name, "rb");
        rc = diskjob_from_file(disk_io, to, SECTORS, 0, f, 1, NULL, NULL, &r);
        fclose(f);
        CHECK(rc == -1 && r.bad == 1 && r.first_bad == 902);
        to->unreadable[902] = 0;

        /* an image of another size: refused before a sector is written */
        spit(other, d->data, (SECTORS - 1) * 512);
        to->writes = 0;
        f = fopen(other, "rb");
        rc = diskjob_from_file(disk_io, to, SECTORS, 0, f, 1, NULL, NULL, &r);
        fclose(f);
        CHECK(rc == -1 && to->writes == 0 && strstr(r.why, "not the disk's size") && r.done == 0);
        spit(other, d->data, SECTORS * 512 - 100);
        f = fopen(other, "rb");
        rc = diskjob_from_file(disk_io, to, SECTORS - 1, 0, f, 1, NULL, NULL, &r);
        fclose(f);
        CHECK(rc == -1 && to->writes == 0 && strstr(r.why, "not the disk's size"));

        /* stopped while writing; stopped while verifying */
        memset(&t, 0, sizeof t), t.order = 1, t.stop_at = 100;
        f = fopen(name, "rb");
        rc = diskjob_from_file(disk_io, to, SECTORS, 0, f, 1, tick, &t, &r);
        fclose(f);
        CHECK(rc == -1 && r.stopped && to->writes == 224 && t.order);       /* (told in halves: 100 of 1000 is sector 224's run) */
        memset(&t, 0, sizeof t), t.order = 1, t.stop_at = 600;
        to->writes = to->reads = 0;
        f = fopen(name, "rb");
        rc = diskjob_from_file(disk_io, to, SECTORS, 0, f, 1, tick, &t, &r);
        fclose(f);
        CHECK(rc == -1 && r.stopped && to->writes == SECTORS && to->reads == 224 && t.order);
        free(to);
    }
    free(d);
    remove(name);
    remove(other);
    full_format();
    printf("diskjob_test: %d failures (%d checks)\n", fails, checks);
    return fails != 0;
}
