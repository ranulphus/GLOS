/* fdbootdisk floppy|disk|disk32 OUT, fdbootdisk say OUT.COM: the volumes for the test of GLOS's FAT boot code
 * (kernel/disk/fdboot.S; Loop A's floppy suite, fdboot_case in tests/loopa/jobs.py).
 *   floppy  a 1.44 MB floppy formatted by fat_format (FAT12), GLOS's boot code in its first sector (fat_boot)
 *   disk    a 20 MB disk (16 heads, 63 sectors a track, as Loop A's are): one active partition (mbr_one),
 *           GLOS's code before the table (mbr_boot), the partition formatted (FAT16) with GLOS's boot code
 *   disk32  the same of 40 MB, its partition FAT32 (type 0Bh)
 *   say     a DOS program that says "HX-START fdboot", "HX-FDBOOT-OK" and "HX-DONE 0" on COM1
 * The Makefile puts FreeDOS's KERNEL.SYS and COMMAND.COM, the program and an AUTOEXEC.BAT that runs it on the
 * volumes with mtools. Prints "fdbootdisk fat=N start=S sectors=M". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../kernel/disk/disk.h"

struct out {
    FILE *f;
    unsigned long base;
    unsigned char first[512];           /* the volume's first sector, as written */
};

static int put(void *ctx, unsigned long lba, const unsigned char *data, unsigned count)
{
    struct out *o = ctx;
    if (!lba)
        memcpy(o->first, data, 512);
    return fseek(o->f, (long)((o->base + lba) * 512), SEEK_SET) != 0 || fwrite(data, 512, count, o->f) != count;
}

int main(int argc, char **argv)
{
    static const unsigned char say[] = { 0xBE, 0x1F, 0x01, 0xAC, 0x08, 0xC0, 0x74, 0x12, 0x88, 0xC4, 0xBA, 0xFD, 0x03, 0xEC,
                                         0xA8, 0x20, 0x74, 0xF8, 0xBA, 0xF8, 0x03, 0x88, 0xE0, 0xEE, 0xEB, 0xE9, 0xB8, 0x00,
                                         0x4C, 0xCD, 0x21 };
    static const char text[] = "HX-START fdboot\r\nHX-FDBOOT-OK\r\nHX-DONE 0\r\n";
    static unsigned char zero[512];
    struct out o;
    struct fat_geo g;
    struct fat_made made;
    struct mbr_part part;
    unsigned char s[512];
    unsigned long total;
    int disk, fat32;
    if (argc != 3 || (strcmp(argv[1], "floppy") && strcmp(argv[1], "disk") && strcmp(argv[1], "disk32")
                      && strcmp(argv[1], "say")) || !(o.f = fopen(argv[2], "wb"))) {
        fprintf(stderr, "usage: fdbootdisk floppy|disk|disk32 OUT, fdbootdisk say OUT.COM\n");
        return 2;
    }
    if (!strcmp(argv[1], "say"))
        return fwrite(say, 1, sizeof say, o.f) != sizeof say || fwrite(text, 1, sizeof text, o.f) != sizeof text
               || fclose(o.f) != 0;
    fat32 = !strcmp(argv[1], "disk32");
    disk = fat32 || !strcmp(argv[1], "disk");
    total = fat32 ? 63UL * 16 * 80 : disk ? 63UL * 16 * 40 : 2880;
    memset(&g, 0, sizeof g);
    memset(&part, 0, sizeof part);
    o.base = 0;
    if (disk) {
        memset(s, 0, sizeof s);
        if (mbr_one(s, total, 16, 63, &part) != 0)
            return 1;
        mbr_boot(s);
        if (fat32)
            s[446 + 4] = 0x0B;                  /* (mbr_one names a partition this small FAT16) */
        if (fwrite(s, 1, 512, o.f) != 512)
            return 1;
        o.base = part.start;
    }
    g.sectors = disk ? part.sectors : total;
    g.hidden = o.base;
    g.heads = disk ? 16 : 2, g.spt = disk ? 63 : 18;
    g.drive = disk ? 0x80 : 0x00;
    g.serial = 0x474C4F53UL;
    g.label = "GLOSBOOT";
    g.fat = fat32 ? 32 : 0;
    if (fat_format(&g, put, &o, &made) != 0 || fat_boot(o.first, made.fat) != 0 || put(&o, 0, o.first, 1) != 0
        || (made.fat == 32 && put(&o, 6, o.first, 1) != 0)
        || fseek(o.f, (long)((total - 1) * 512), SEEK_SET) != 0 || fwrite(zero, 1, 512, o.f) != 512 || fclose(o.f) != 0)
        return 1;
    printf("fdbootdisk fat=%d start=%lu sectors=%lu\n", made.fat, o.base, g.sectors);
    return 0;
}
