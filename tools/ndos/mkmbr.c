/* mkmbr: a hard disk's first sector as GLOS makes one (kernel/disk/mbr.c: mbr_one and mbr_boot), for a disk of
 * SECTORS sectors of HEADS heads (16 if not given) by 63, its one partition of type TYPE (hex; 0: mbr_one's own
 * choice): the 512 bytes to stdout, "start sectors" to stderr. mkmbr SECTORS [TYPE [HEADS]]. For
 * tools/ndos/msdos-image.sh's private MS-DOS 7.1 reference disk and tools/ndos/native-image.sh's disks. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../kernel/disk/disk.h"

int main(int argc, char **argv)
{
    unsigned char s[512];
    struct mbr_part p;

    if (argc < 2)
        return 2;
    memset(s, 0, sizeof s);
    if (mbr_one(s, strtoul(argv[1], 0, 0), argc > 3 ? (unsigned)strtoul(argv[3], 0, 0) : 16, 63, &p) != 0)
        return 1;
    if (argc > 2 && strtoul(argv[2], 0, 16))
        s[446 + 4] = (unsigned char)strtoul(argv[2], 0, 16);
    mbr_boot(s);
    fwrite(s, 1, sizeof s, stdout);
    fprintf(stderr, "%lu %lu\n", p.start, p.sectors);
    return 0;
}
