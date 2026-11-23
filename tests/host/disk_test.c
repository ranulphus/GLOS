/* disk_test OUTDIR: kernel/disk (the partition table, FAT volumes made new) on the host. Volumes of many sizes
 * are written as files in OUTDIR (NAME.img), a line each on stdout ("VOLUME name fat spc clusters"), and a whole
 * disk with a table and a partition (disk.img: "DISK start sectors type"); tests/host/disk_test.sh then has
 * fsck.fat and mtools read every one. Here: what the code itself must hold to (the table read back, the boot
 * code's size, sizes no FAT fits refused). */
#define _FILE_OFFSET_BITS 64                    /* (images past 2 GB, in a 32-bit build) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "../../kernel/disk/disk.h"

extern const unsigned char mbr_code[], mbr_code_end[];

static int fails;
static FILE *out;
static unsigned long base;

static void check(const char *what, int ok)
{
    if (!ok) {
        printf("FAIL %s\n", what);
        fails++;
    }
}

static int put(void *ctx, unsigned long lba, const unsigned char *data, unsigned count)
{
    (void)ctx;
    if (fseeko(out, (off_t)(base + lba) * 512, SEEK_SET) != 0)
        return 1;
    return fwrite(data, 512, count, out) != count;
}

static void volume(const char *dir, const char *name, unsigned long sectors, unsigned drive, int fat, int want)
{
    struct fat_geo g = { sectors, 0, 16, 63, drive, 0x12345678UL, "GLOS TEST", fat };
    struct fat_made m;
    char path[512];
    unsigned char last[512] = { 0 };
    int r;
    snprintf(path, sizeof path, "%s/%s.img", dir, name);
    out = fopen(path, "wb");
    base = 0;
    fseeko(out, (off_t)(sectors - 1) * 512, SEEK_SET);  /* (its whole size: the tools check it) */
    fwrite(last, 512, 1, out);
    r = fat_format(&g, put, 0, &m);
    fclose(out);
    if (r != 0) {
        printf("FAIL %s: fat_format %d\n", name, r);
        fails++;
        return;
    }
    if (want && m.fat != want) {
        printf("FAIL %s: FAT%d, not FAT%d\n", name, m.fat, want);
        fails++;
    }
    printf("VOLUME %s %d %u %lu\n", name, m.fat, m.cluster_sectors, m.clusters);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    unsigned char s[512];
    struct mbr_part p[4], made;
    struct fat_geo g;
    struct fat_made m;
    char path[512];
    unsigned i;

    /* The table. */
    memset(s, 0xEE, sizeof s);
    check("no table", mbr_parse(s, p) == -1);
    check("one", mbr_one(s, 201600UL, 16, 63, &made) == 0 && made.start == 63 && made.sectors == 201600UL - 63
          && made.type == 0x06);
    check("kept", s[0] == 0xEE && s[439] == 0xEE);
    check("parsed", mbr_parse(s, p) == 1 && p[0].active && p[0].type == 0x06 && p[0].start == 63
          && p[0].sectors == made.sectors && !p[1].type && !p[3].type);
    check("chs", s[446 + 1] == 1 && s[446 + 2] == 1 && s[446 + 3] == 0 && s[446 + 5] == 15 && s[446 + 6] == 63
          && s[446 + 7] == 199);
    check("fat types", mbr_is_fat(0x06) && mbr_is_fat(0x0C) && !mbr_is_fat(0x83) && !mbr_is_fat(0x07) && !mbr_is_fat(0));
    check("small", mbr_one(s, 20000, 16, 63, &made) == 0 && made.type == 0x01);
    check("fat16 small", mbr_one(s, 60000, 16, 63, &made) == 0 && made.type == 0x04);
    check("far fat16", mbr_one(s, 4000000UL, 16, 63, &made) == 0 && made.type == 0x0E && s[446 + 7] == 0xFF);
    check("past 2 GB is FAT32's", mbr_one(s, 4300000UL, 255, 63, &made) == 0 && made.type == 0x0B);
    check("fat32", mbr_one(s, 16000000UL, 255, 63, &made) == 0 && made.type == 0x0B);
    check("far fat32", mbr_one(s, 40000000UL, 255, 63, &made) == 0 && made.type == 0x0C);
    check("no room", mbr_one(s, 100, 16, 63, &made) == -1 && mbr_one(s, 100000, 0, 63, &made) == -1);
    check("code fits", mbr_code_end - mbr_code > 100 && mbr_code_end - mbr_code <= 440);
    mbr_boot(s);
    check("boot code", !memcmp(s, mbr_code, (size_t)(mbr_code_end - mbr_code)) && s[510] == 0x55 && s[511] == 0xAA
          && mbr_parse(s, p) == 1 && p[0].type == 0x0C);
    check("no partition kept", mbr_boot_part_of(s) == 0);
    mbr_boot_part(s, 2);
    check("GLOS's partition kept", mbr_boot_part_of(s) == 2 && mbr_parse(s, p) == 1 && p[0].type == 0x0C);
    s[3] ^= 0x55;
    check("another's code keeps none", mbr_boot_part_of(s) == 0);

    /* A partition added beside what is there: the owner's PCChips M925G, a 20 GB disk (the BIOS's 255 heads by
       63) with Windows 98 on its first 10 GB. The new one takes the rest, in whole cylinders, FAT32 by the LBA
       calls (past cylinder 1023), not active (Windows's is); the first entry as it was. */
    {
        unsigned char was[16];
        unsigned slot = 9;
        unsigned long cyl = 255UL * 63, win = 1274UL * cyl - 63;

        memset(s, 0, sizeof s);
        check("add: no table", mbr_add(s, 39102336UL, 255, 63, &made, &slot) == -1);
        s[510] = 0x55, s[511] = 0xAA;
        s[446] = 0x80, s[446 + 4] = 0x0C, s[446 + 8] = 63;
        s[446 + 12] = (unsigned char)win, s[446 + 13] = (unsigned char)(win >> 8), s[446 + 14] = (unsigned char)(win >> 16);
        s[446 + 15] = (unsigned char)(win >> 24);
        memcpy(was, s + 446, 16);
        check("add", mbr_add(s, 39102336UL, 255, 63, &made, &slot) == 0 && slot == 1 && made.start == 1274UL * cyl
              && made.sectors == 39102336UL / cyl * cyl - 1274UL * cyl && made.type == 0x0C && !made.active);
        check("add: the first kept", !memcmp(was, s + 446, 16) && mbr_parse(s, p) == 2 && p[0].active && !p[1].active
              && p[1].start == made.start && p[1].sectors == made.sectors && p[1].type == 0x0C);
        check("add: its end is the BIOS's last", s[446 + 16 + 5] == 254 && s[446 + 16 + 6] == 0xFF && s[446 + 16 + 7] == 0xFF);
        check("add: no room left", mbr_add(s, 39102336UL, 255, 63, &made, &slot) == -3);
        /* ... a stretch between two partitions is taken when it is the largest; a full table refuses */
        memset(s + 446, 0, 64);
        s[446 + 4] = 0x06, s[446 + 8] = 63, s[446 + 12] = (unsigned char)(1008 - 63), s[446 + 13] = (unsigned char)((1008 - 63) >> 8);
        s[446 + 32 + 4] = 0x83, s[446 + 32 + 10] = 0x08, s[446 + 32 + 14] = 0x01;     /* 80000h, 10000h sectors */
        check("add between", mbr_add(s, 0x90000UL + 1008, 16, 63, &made, &slot) == 0 && slot == 1 && made.start == 1008
              && made.sectors == 0x80000UL / 1008 * 1008 - 1008 && made.type == 0x06 && made.active);
        s[446 + 48 + 4] = 0x82, s[446 + 48 + 8] = 1;
        check("add: table full", mbr_add(s, 0x90000UL + 1008, 16, 63, &made, &slot) == -2);
    }

    /* Volumes: the floppies, then disks' sizes at the edges of each kind of FAT. */
    volume(dir, "f360", 720, 0, 0, 12);
    volume(dir, "f720", 1440, 0, 0, 12);
    volume(dir, "f1200", 2400, 0, 0, 12);
    volume(dir, "f1440", 2880, 0, 0, 12);
    volume(dir, "f2880", 5760, 0, 0, 12);
    volume(dir, "d2m", 4096, 0x80, 0, 12);
    volume(dir, "d10m", 20000, 0x80, 0, 12);
    volume(dir, "d16m", 32679, 0x80, 0, 12);
    volume(dir, "d16m1", 32680, 0x80, 0, 16);
    volume(dir, "d20m", 40000, 0x80, 0, 16);
    volume(dir, "d100m", 204800, 0x80, 0, 16);
    volume(dir, "d348m", 713639UL, 0x80, 0, 16);       /* (the 486 bench's partition) */
    volume(dir, "d500m", 1000000UL, 0x80, 0, 16);
    volume(dir, "d2g", 4194000UL, 0x80, 0, 16);
    volume(dir, "d2g1", 4194001UL, 0x80, 0, 32);
    volume(dir, "d4g", 8388608UL, 0x80, 0, 32);
    volume(dir, "d500m32", 1000000UL, 0x80, 32, 32);
    volume(dir, "d40m12", 80000, 0x80, 12, 12);
    memset(&g, 0, sizeof g);
    g.sectors = 30, g.heads = 16, g.spt = 63, g.drive = 0x80;
    check("too small", fat_format(&g, put, 0, &m) == -1);
    g.sectors = 20000, g.fat = 32;
    check("too small for FAT32", fat_format(&g, put, 0, &m) == -1);
    g.sectors = 8388608UL, g.fat = 12;
    check("too large for FAT12", fat_format(&g, put, 0, &m) == -1);
    g.fat = 16;
    check("too large for FAT16", fat_format(&g, put, 0, &m) == -1);

    /* A whole disk: the table, the boot code, the partition formatted where the table says. */
    snprintf(path, sizeof path, "%s/disk.img", dir);
    out = fopen(path, "wb");
    memset(s, 0, sizeof s);
    check("disk table", mbr_one(s, 201600UL, 16, 63, &made) == 0);
    mbr_boot(s);
    base = 0;
    put(0, 0, s, 1);
    memset(s, 0, sizeof s);
    put(0, 201600UL - 1, s, 1);
    memset(&g, 0, sizeof g);
    g.sectors = made.sectors, g.hidden = made.start, g.heads = 16, g.spt = 63, g.drive = 0x80, g.serial = 0xABCD0123UL;
    g.label = "GLOS";
    base = made.start;
    check("disk format", fat_format(&g, put, 0, &m) == 0 && m.fat == 16);
    fclose(out);
    printf("DISK %lu %lu %02x\n", made.start, made.sectors, made.type);
    for (i = 0; i < 4; i++)
        (void)p[i];
    printf("disk_test: %d failures\n", fails);
    return fails != 0;
}
