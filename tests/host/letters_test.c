/* letters_test: kernel/disk/letters.c, drive letters in MS-DOS's order, over disks made of a few sectors in
 * memory: one partition; Windows 98 beside GLOS (bench G's disk) with either one active; a data partition
 * beside GLOS; two disks with primaries, logical drives and second primaries; partitions DOS gives no letter;
 * a disk formatted whole; a chain of tables that turns back on itself; a partition past the disk's end; more
 * volumes than letters. "letters_test: 0 failures" when all held. */
#include <stdio.h>
#include <string.h>

#include "../../kernel/disk/letters.h"

static int fails;

#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

/* A disk: the sectors that were written (the rest read as zeros). */
struct disk {
    int n;
    unsigned long lba[40];
    unsigned char s[40][512];
};

static unsigned char *sector(struct disk *d, unsigned long lba)
{
    int i;

    for (i = 0; i < d->n; i++)
        if (d->lba[i] == lba)
            return d->s[i];
    d->lba[d->n] = lba;
    memset(d->s[d->n], 0, 512);
    return d->s[d->n++];
}

static int rd(void *ctx, unsigned long lba, unsigned char *out)
{
    struct disk *d = ctx;
    int i;

    memset(out, 0, 512);
    for (i = 0; i < d->n; i++)
        if (d->lba[i] == lba)
            memcpy(out, d->s[i], 512);
    return 0;
}

/* Entry `slot` of the table in sector `at`: its type, whether active, its start (as the table counts it) and size. */
static void entry(struct disk *d, unsigned long at, int slot, unsigned type, int active, unsigned long start,
                  unsigned long sectors)
{
    unsigned char *s = sector(d, at), *e = s + 446 + slot * 16;

    s[510] = 0x55, s[511] = 0xAA;
    e[0] = active ? 0x80 : 0;
    e[4] = (unsigned char)type;
    e[8] = (unsigned char)start, e[9] = (unsigned char)(start >> 8), e[10] = (unsigned char)(start >> 16);
    e[11] = (unsigned char)(start >> 24);
    e[12] = (unsigned char)sectors, e[13] = (unsigned char)(sectors >> 8), e[14] = (unsigned char)(sectors >> 16);
    e[15] = (unsigned char)(sectors >> 24);
}

/* The letters as a string: each volume's "disk kind slot letter" ("0p0C 0l1E"; '-' no letter). */
static const char *show(const struct let_vol *v, int n)
{
    static char out[400];
    int i;

    out[0] = 0;
    for (i = 0; i < n; i++)
        sprintf(out + strlen(out), "%s%u%c%u%c", i ? " " : "", v[i].disk, "plw"[v[i].kind], v[i].slot,
                v[i].letter < 0 ? '-' : 'A' + v[i].letter);
    return out;
}

int main(void)
{
    static struct disk d0, d1;
    struct let_vol v[24];
    int n, i;

    /* one disk, one partition */
    memset(&d0, 0, sizeof d0);
    entry(&d0, 0, 0, 0x0C, 1, 63, 1000000);
    n = let_scan(0, 2000000, rd, &d0, v, 0, 24);
    let_assign(v, n, 2);
    CHECK(n == 1 && !strcmp(show(v, n), "0p0C") && v[0].start == 63 && v[0].sectors == 1000000 && v[0].type == 0x0C
          && v[0].active, "one partition: %s", show(v, n));

    /* Windows 98 and GLOS (bench G): Windows's active, then GLOS's */
    memset(&d0, 0, sizeof d0);
    entry(&d0, 0, 0, 0x0C, 1, 63, 19551042);
    entry(&d0, 0, 1, 0x0C, 0, 19551105, 19551105);
    n = let_scan(0, 39102336, rd, &d0, v, 0, 24);
    v[0].dos = 1, v[1].own = 1;
    let_assign(v, n, 2);
    CHECK(n == 2 && !strcmp(show(v, n), "0p0C 0p1D"), "Windows active, GLOS second: %s", show(v, n));
    v[0].active = 0, v[1].active = 1;           /* (GLOS's made the active one: Windows's letters stay) */
    let_assign(v, n, 2);
    CHECK(!strcmp(show(v, n), "0p0C 0p1D"), "GLOS active beside Windows: %s", show(v, n));
    v[0].dos = 0;                               /* (a data partition beside GLOS, no DOS: GLOS's is C:) */
    let_assign(v, n, 2);
    CHECK(!strcmp(show(v, n), "0p0D 0p1C"), "GLOS active beside a data partition: %s", show(v, n));
    v[0].active = v[1].active = 0;              /* (none active: the first in the table) */
    let_assign(v, n, 2);
    CHECK(!strcmp(show(v, n), "0p0C 0p1D"), "none active: %s", show(v, n));

    /* two disks: a primary, an extended partition with two logical drives and another primary; a primary and one
       logical drive. Hidden and other systems' partitions have no letter. */
    memset(&d0, 0, sizeof d0);
    memset(&d1, 0, sizeof d1);
    entry(&d0, 0, 0, 0x06, 1, 63, 100000);
    entry(&d0, 0, 1, 0x0F, 0, 200000, 300000);
    entry(&d0, 0, 2, 0x0B, 0, 600000, 100000);
    entry(&d0, 0, 3, 0x83, 0, 800000, 100000);
    entry(&d0, 200000, 0, 0x06, 0, 63, 99937);          /* the first logical drive, and the next table */
    entry(&d0, 200000, 1, 0x05, 0, 100000, 100000);
    entry(&d0, 300000, 0, 0x0B, 0, 63, 99937);
    entry(&d1, 0, 0, 0x1B, 0, 63, 50000);               /* (hidden) */
    entry(&d1, 0, 1, 0x0E, 0, 60000, 100000);
    entry(&d1, 0, 2, 0x05, 0, 200000, 100000);
    entry(&d1, 200000, 0, 0x01, 0, 63, 20000);
    n = let_scan(0, 1000000, rd, &d0, v, 0, 24);
    n = let_scan(1, 1000000, rd, &d1, v, n, 24);
    let_assign(v, n, 2);
    CHECK(n == 6 && !strcmp(show(v, n), "0p0C 0p2H 0l0E 0l1F 1p1D 1l0G"), "two disks: %s", show(v, n));
    CHECK(v[2].start == 200063 && v[2].sectors == 99937 && v[3].start == 300063 && v[5].start == 200063,
          "logical drives' places: %lu %lu %lu", v[2].start, v[3].start, v[5].start);

    /* a disk formatted whole (a USB stick): its first sector is the volume's */
    memset(&d0, 0, sizeof d0);
    {
        unsigned char *s = sector(&d0, 0);

        s[0] = 0xEB, s[1] = 0x3C, s[2] = 0x90;
        s[11] = 0, s[12] = 2, s[13] = 8, s[16] = 2, s[21] = 0xF8, s[510] = 0x55, s[511] = 0xAA;
    }
    n = let_scan(0, 1966080, rd, &d0, v, 0, 24);
    let_assign(v, n, 2);
    CHECK(n == 1 && !strcmp(show(v, n), "0w0C") && v[0].start == 0 && v[0].sectors == 1966080, "a whole disk: %s", show(v, n));

    /* a chain that turns back; a partition past the disk's end; a sector with no table */
    memset(&d0, 0, sizeof d0);
    entry(&d0, 0, 0, 0x06, 1, 63, 100000);
    entry(&d0, 0, 1, 0x05, 0, 200000, 300000);
    entry(&d0, 0, 2, 0x06, 0, 900000, 200000);          /* (ends past the disk) */
    entry(&d0, 200000, 0, 0x06, 0, 63, 5000);
    entry(&d0, 200000, 1, 0x05, 0, 0x10000, 5000);
    entry(&d0, 200000 + 0x10000, 0, 0x06, 0, 63, 5000);
    entry(&d0, 200000 + 0x10000, 1, 0x05, 0, 0, 5000);  /* (back to the first table) */
    n = let_scan(0, 1000000, rd, &d0, v, 0, 24);
    let_assign(v, n, 2);
    CHECK(n == 3 && !strcmp(show(v, n), "0p0C 0l0D 0l1E"), "a chain that turns back: %s", show(v, n));
    memset(&d0, 0, sizeof d0);
    n = let_scan(0, 1000000, rd, &d0, v, 0, 24);
    CHECK(n == 0, "no table: %d", n);

    /* more volumes than letters: GLOS's own keeps one */
    memset(&d0, 0, sizeof d0);
    entry(&d0, 0, 0, 0x06, 1, 63, 1000);
    entry(&d0, 0, 1, 0x06, 0, 2000, 1000);
    entry(&d0, 0, 2, 0x06, 0, 4000, 1000);
    n = let_scan(0, 100000, rd, &d0, v, 0, 24);
    v[2].own = 1;
    let_assign(v, n, 24);
    CHECK(n == 3 && !strcmp(show(v, n), "0p0Y 0p1- 0p2Z"), "more volumes than letters: %s", show(v, n));
    for (i = 0; i < n; i++)
        v[i].own = 0;
    let_assign(v, n, 24);
    CHECK(!strcmp(show(v, n), "0p0Y 0p1Z 0p2-"), "... and none GLOS's: %s", show(v, n));

    /* no room in the list: what fits */
    memset(&d0, 0, sizeof d0);
    entry(&d0, 0, 0, 0x06, 1, 63, 1000);
    entry(&d0, 0, 1, 0x06, 0, 2000, 1000);
    n = let_scan(0, 100000, rd, &d0, v, 0, 1);
    CHECK(n == 1, "a list of one: %d", n);

    printf("letters_test: %d failures\n", fails);
    return fails != 0;
}
