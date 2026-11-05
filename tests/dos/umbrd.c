/* UMBRD (docs/upper-memory.md B1): do the BIOS's disk reads reach an upper memory block? With UMBs made of the
 * chipset's shadow RAM (UMBPCI), a bus master's or the 8237's DMA may not reach it on every chipset, and GLOS
 * loaded high keeps the disk cache's block buffer there. Read-only: the same sectors are read by INT 13h AH=42h
 * into a buffer in a UMB (filled with A5h first) and into one in conventional memory, and compared. Nothing is
 * written to any disk.
 *     UMBRD [DRIVE]       DRIVE in hex, 80 by default
 *     HX-UMBRD umb=SSSS reads=N same=N differ=N errors=N sectors=N first=LBA|none
 * umb=none: no UMB to be had (no DOS=UMB, or none free). Open Watcom, real mode. */
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTS 16                                /* 8 KB a read */
#define READS 256

static unsigned char low[SECTS * 512];
static struct {
    unsigned char size, zero;
    unsigned short count, off, seg;
    unsigned long lba, lba_hi;
} dap;

static void ser(const char *s)
{
    for (; *s; s++) {
        unsigned n = 0;
        while (!(inp(0x3FD) & 0x20) && ++n < 60000u) ;
        outp(0x3F8, *s);
    }
}

/* A UMB of paras paragraphs: DOS's strategy "high only" with the UMBs linked, both put back after. 0 if none. */
static unsigned umb_alloc(unsigned paras)
{
    union REGS r;
    unsigned strat, link, seg = 0;
    r.x.ax = 0x5800;
    intdos(&r, &r);
    strat = r.x.ax;
    r.x.ax = 0x5802;
    intdos(&r, &r);
    link = r.h.al;
    r.x.ax = 0x5803;
    r.x.bx = 1;
    intdos(&r, &r);
    r.x.ax = 0x5801;
    r.x.bx = 0x40;                              /* high memory only, first fit */
    intdos(&r, &r);
    r.h.ah = 0x48;
    r.x.bx = paras;
    intdos(&r, &r);
    if (!r.x.cflag)
        seg = r.x.ax;
    r.x.ax = 0x5801;
    r.x.bx = strat;
    intdos(&r, &r);
    r.x.ax = 0x5803;
    r.x.bx = link;
    intdos(&r, &r);
    return seg;
}

static int bios_read(unsigned drive, unsigned long lba, unsigned seg, unsigned off)
{
    union REGS r;
    struct SREGS s;
    dap.size = 16;
    dap.zero = 0;
    dap.count = SECTS;
    dap.off = off;
    dap.seg = seg;
    dap.lba = lba;
    dap.lba_hi = 0;
    segread(&s);
    r.h.ah = 0x42;
    r.h.dl = (unsigned char)drive;
    r.x.si = FP_OFF(&dap);
    int86x(0x13, &r, &r, &s);
    return r.x.cflag ? r.h.ah : 0;
}

int main(int argc, char **argv)
{
    union REGS r;
    struct SREGS s;
    unsigned drive = argc > 1 ? (unsigned)strtoul(argv[1], 0, 16) : 0x80, seg, i, same = 0, differ = 0, errors = 0;
    unsigned long total = 0, lba, first = 0xFFFFFFFFul;
    static unsigned char params[0x1E];
    unsigned char __far *umb;
    char line[100];

    params[0] = sizeof params;
    params[1] = 0;
    segread(&s);
    r.h.ah = 0x48;                              /* the drive's size */
    r.h.dl = (unsigned char)drive;
    r.x.si = FP_OFF(params);
    int86x(0x13, &r, &r, &s);
    if (!r.x.cflag)
        total = *(unsigned long *)(params + 0x10);
    seg = umb_alloc(SECTS * 512 / 16);
    if (!seg || seg < 0xA000 || total < 2 * SECTS) {
        sprintf(line, "HX-UMBRD umb=%s reads=0 same=0 differ=0 errors=0 sectors=%lu first=none\r\n",
                seg ? "low" : "none", total);
        ser(line);
        fputs(line, stdout);
        return 1;
    }
    umb = MK_FP(seg, 0);
    srand(4242);
    for (i = 0; i < READS; i++) {
        lba = i < 16 ? (unsigned long)i * SECTS                                   /* the start: MBR, FATs */
                     : (((unsigned long)rand() << 15) ^ rand()) % (total - SECTS);
        _fmemset(umb, 0xA5, SECTS * 512);
        memset(low, 0x5A, sizeof low);
        if (bios_read(drive, lba, seg, 0) || bios_read(drive, lba, FP_SEG((void __far *)low),
                                                          FP_OFF((void __far *)low))) {
            errors++;
            continue;
        }
        if (!_fmemcmp(umb, (void __far *)low, SECTS * 512))
            same++;
        else {
            differ++;
            if (first == 0xFFFFFFFFul)
                first = lba;
        }
    }
    if (first == 0xFFFFFFFFul)
        sprintf(line, "HX-UMBRD umb=%04X reads=%u same=%u differ=%u errors=%u sectors=%lu first=none\r\n",
                seg, READS, same, differ, errors, total);
    else
        sprintf(line, "HX-UMBRD umb=%04X reads=%u same=%u differ=%u errors=%u sectors=%lu first=%lu\r\n",
                seg, READS, same, differ, errors, total, first);
    ser(line);
    fputs(line, stdout);
    r.h.ah = 0x49;
    s.es = seg;
    int86x(0x21, &r, &r, &s);
    return differ || errors ? 2 : 0;
}
