/* mkvol: a FAT volume made by GLOS's own formatter (kernel/disk/fatfmt.c) in a disk image, at sector START for
 * SECTORS sectors (HEADS heads, 16 if not given, by 63), with GLOS's boot code (kernel/disk/fdboot.S) naming GLOSLDR.SYS instead of
 * KERNEL.SYS: the volume a native boot starts from (docs/v1/native-dos.md, native boot, NB1;
 * tools/ndos/native-image.sh). Its kind of FAT by its size, as DISK FORMAT chooses. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../kernel/disk/disk.h"

static FILE *img;
static unsigned long base;

static int put(void *ctx, unsigned long lba, const unsigned char *buf, unsigned count)
{
    (void)ctx;
    return fseek(img, (long)((base + lba) * 512UL), SEEK_SET) != 0 || fwrite(buf, 512, count, img) != count;
}

static int get(unsigned long lba, unsigned char *buf)
{
    return fseek(img, (long)((base + lba) * 512UL), SEEK_SET) != 0 || fread(buf, 512, 1, img) != 1;
}

/* GLOS's boot code into the volume's sector at lba, naming GLOSLDR.SYS. */
static int boot(unsigned long lba, int fat)
{
    unsigned char s[512];
    unsigned i;

    if (get(lba, s) != 0 || fat_boot(s, fat) != 0)
        return -1;
    for (i = 0; i + 11 <= 510; i++)
        if (!memcmp(s + i, "KERNEL  SYS", 11)) {
            memcpy(s + i, "GLOSLDR SYS", 11);
            return put(0, lba, s, 1);
        }
    return -1;
}

int main(int argc, char **argv)
{
    struct fat_geo g;
    struct fat_made m;

    if (argc < 5) {
        fprintf(stderr, "usage: mkvol IMAGE START SECTORS LABEL [HEADS [DRIVE]]\n");
        return 2;
    }
    if (!(img = fopen(argv[1], "r+b"))) {
        perror(argv[1]);
        return 1;
    }
    base = strtoul(argv[2], 0, 0);
    memset(&g, 0, sizeof g);
    g.sectors = strtoul(argv[3], 0, 0);
    g.hidden = base;
    g.heads = argc > 5 ? (unsigned)strtoul(argv[5], 0, 0) : 16, g.spt = 63;
    g.drive = argc > 6 ? (unsigned)strtoul(argv[6], 0, 16) : 0x80;      /* (00: a floppy, of DOS's own shapes) */
    g.serial = 0x474C4F53UL;
    g.label = argv[4];
    if (fat_format(&g, put, 0, &m) != 0 || boot(0, m.fat) != 0 || (m.fat == 32 && boot(6, 32) != 0)) {
        fprintf(stderr, "mkvol: the volume couldn't be made\n");
        return 1;
    }
    fclose(img);
    printf("FAT%d, %u sectors a cluster, %lu clusters\n", m.fat, m.cluster_sectors, m.clusters);
    return 0;
}
