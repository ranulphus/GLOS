/* mbrdisk OUT SECTORS: the disk for the MBR boot code's test (Loop A's floppy suite, mbr_case in
 * tests/loopa/jobs.py). A disk of SECTORS sectors (16 heads, 63 sectors a track, as Loop A's disks are) with
 * one active partition over it (mbr_one), GLOS's boot code before the table (mbr_boot), and as the partition's
 * first sector a program that reports on COM1 what it was entered with (mbrstub.S). Sparse: two sectors are
 * written. Prints "mbrdisk start=N sectors=M type=TT". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../kernel/disk/disk.h"

extern const unsigned char stub_code[], stub_code_end[];

int main(int argc, char **argv)
{
    unsigned char s[512];
    struct mbr_part made;
    unsigned long total;
    unsigned n = (unsigned)(stub_code_end - stub_code);
    FILE *f;
    if (argc != 3 || (total = strtoul(argv[2], NULL, 0)) < 1008 || n > 510) {
        fprintf(stderr, "usage: mbrdisk OUT SECTORS\n");
        return 2;
    }
    memset(s, 0, sizeof s);
    if (mbr_one(s, total, 16, 63, &made) != 0) {
        fprintf(stderr, "mbrdisk: no room for a partition in %lu sectors\n", total);
        return 1;
    }
    mbr_boot(s);
    if (!(f = fopen(argv[1], "wb")) || fwrite(s, 1, 512, f) != 512)
        return 1;
    memset(s, 0, sizeof s);
    memcpy(s, stub_code, n);
    s[510] = 0x55, s[511] = 0xAA;
    if (fseek(f, (long)(made.start * 512), SEEK_SET) != 0 || fwrite(s, 1, 512, f) != 512 || fflush(f) != 0
        || ftruncate(fileno(f), (off_t)(total * 512)) != 0 || fclose(f) != 0)
        return 1;
    printf("mbrdisk start=%lu sectors=%lu type=%02x\n", made.start, made.sectors, made.type);
    return 0;
}
