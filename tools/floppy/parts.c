/* PARTS: the first hard disk's partition table, for GLOS's setup floppy (docs/floppy.md): what DOS itself can't
 * say, since a disk with only another system's partitions (Linux's, NTFS) has no drive letter and looks empty
 * to it. tools/floppy/install.py asks this before it partitions anything.
 *
 *   PARTS disk=80 sectors=N signature=1 used=K
 *   PARTS n=1 type=06 active=1 start=63 sectors=201537 mb=98 fat=1
 *   ...
 * One line a partition that is there (type not 0); `fat` 1 for the types DOS reads (01, 04, 06, 0B, 0C, 0E).
 * "PARTS error=NN" if the BIOS can't read the disk (NN its INT 13h code; 01 or 80: no such disk).
 * The exit code: how many partitions there are; 100 for an error. */
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <string.h>

static unsigned char sector[512];

int main(void)
{
    union REGS r;
    struct SREGS s;
    unsigned char far *p = sector;
    int i, tries, used = 0;
    unsigned long total = 0;
    for (tries = 0; tries < 3; tries++) {       /* INT 13h AH=02h: sector 0 (cylinder 0, head 0, sector 1) */
        segread(&s);
        s.es = FP_SEG(p);
        r.x.bx = FP_OFF(p);
        r.x.ax = 0x0201;
        r.x.cx = 0x0001;
        r.x.dx = 0x0080;
        int86x(0x13, &r, &r, &s);
        if (!r.x.cflag)
            break;
        r.x.ax = 0;                             /* (reset, and again) */
        r.x.dx = 0x0080;
        int86(0x13, &r, &r);
    }
    if (tries == 3) {
        printf("PARTS error=%02X\n", r.h.ah);
        return 100;
    }
    for (i = 0; i < 4; i++)
        if (sector[446 + i * 16 + 4])
            used++;
    r.h.ah = 0x08;                              /* (its size as the BIOS has it, for the eye) */
    r.x.dx = 0x0080;
    segread(&s);
    int86x(0x13, &r, &r, &s);
    if (!r.x.cflag)
        total = (unsigned long)((((r.h.cl & 0xC0u) << 2) | r.h.ch) + 1) * (r.h.dh + 1u) * (r.h.cl & 0x3Fu);
    printf("PARTS disk=80 sectors=%lu signature=%d used=%d\n", total, sector[510] == 0x55 && sector[511] == 0xAA, used);
    for (i = 0; i < 4; i++) {
        const unsigned char *e = sector + 446 + i * 16;
        unsigned long start, count;
        if (!e[4])
            continue;
        memcpy(&start, e + 8, 4);
        memcpy(&count, e + 12, 4);
        printf("PARTS n=%d type=%02X active=%d start=%lu sectors=%lu mb=%lu fat=%d\n", i + 1, e[4], e[0] == 0x80, start,
               count, count / 2048, e[4] == 1 || e[4] == 4 || e[4] == 6 || e[4] == 0x0B || e[4] == 0x0C || e[4] == 0x0E);
    }
    return used;
}
