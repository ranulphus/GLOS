/* STUBTSR (GLOS docs/upper-memory.md U2): memory left taken under GLOS, so that GLOS's stub, in a page of upper
 * memory, has to look elsewhere when GLOS leaves:
 *     STUBTSR          stays resident with its PSP alone (256 bytes), right after GLOS's block: the stub can't
 *                      grow that block back (AH=4Ah) and takes one anywhere (AH=48h)
 *     STUBTSR ALL S    stays resident with all of DOS's memory for S seconds (glos exit is refused meanwhile):
 *                      its own block as large as it goes, and every other free block (AH=48h) too; then gives
 *                      them back from INT 1Ch, unhooked again, with no DOS call
 * Before going resident:
 *     HX-STUBTSR psp=SSSS kept=BYTES other=BYTES strat=N
 * other: the largest free block left anywhere else; once ALL's memory is back, HX-STUBTSR freed on COM1.
 * Open Watcom, real mode. */
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned psp, first_mcb, ticks;
static void (__interrupt __far *old1c)(void);

static void ser(const char *s)
{
    for (; *s; s++) {
        unsigned n = 0;
        while (!(inp(0x3FD) & 0x20) && ++n < 60000u) ;
        outp(0x3F8, *s);
    }
}

/* The largest free block in DOS's arena chain, ours aside. */
static unsigned long other_free(void)
{
    unsigned seg = first_mcb, best = 0;
    for (;;) {
        unsigned char __far *m = MK_FP(seg, 0);
        unsigned owner = *(unsigned __far *)(m + 1), size = *(unsigned __far *)(m + 3);
        if (!owner && size > best)
            best = size;
        if (m[0] != 'M')
            break;
        seg += size + 1;
    }
    return (unsigned long)best * 16;
}

/* Every block of ours goes back (its owner 0, as DOS frees one), and INT 1Ch is DOS's again. */
static void __interrupt __far tick(void)
{
    if (ticks && !--ticks) {
        unsigned seg = first_mcb;
        _dos_setvect(0x1C, old1c);
        for (;;) {
            unsigned char __far *m = MK_FP(seg, 0);
            if (*(unsigned __far *)(m + 1) == psp)
                *(unsigned __far *)(m + 1) = 0;
            if (m[0] != 'M')
                break;
            seg += *(unsigned __far *)(m + 3) + 1;
        }
        ser("HX-STUBTSR freed\r\n");
    }
    _chain_intr(old1c);
}

int main(int argc, char **argv)
{
    union REGS r;
    struct SREGS s;
    char line[80];
    unsigned keep = 0x10;

    r.h.ah = 0x62;
    intdos(&r, &r);
    psp = r.x.bx;
    r.h.ah = 0x52;                              /* the first arena header is the word before ES:BX */
    segread(&s);
    intdosx(&r, &r, &s);
    first_mcb = *(unsigned __far *)MK_FP(s.es, r.x.bx - 2);
    if (argc > 2 && !stricmp(argv[1], "ALL")) {
        r.h.ah = 0x4A;                          /* as large as DOS will let our block be */
        r.x.bx = 0xFFFF;
        s.es = psp;
        intdosx(&r, &r, &s);
        keep = r.x.bx;
        r.h.ah = 0x4A;
        intdosx(&r, &r, &s);
        for (;;) {                              /* and the rest: holes an earlier program left */
            r.h.ah = 0x48;
            r.x.bx = 0xFFFF;
            intdos(&r, &r);                     /* (refused: BX the largest there is) */
            if (!r.x.bx)
                break;
            r.h.ah = 0x48;
            intdos(&r, &r);
            if (r.x.cflag)
                break;
        }
        ticks = (unsigned)(atoi(argv[2]) * 182L / 10);
        old1c = _dos_getvect(0x1C);
        _dos_setvect(0x1C, tick);
    }
    r.x.ax = 0x5800;                            /* DOS's allocation strategy (GLOS's DOS server sets and restores it) */
    intdos(&r, &r);
    sprintf(line, "HX-STUBTSR psp=%04X kept=%lu other=%lu strat=%u\r\n", psp, (unsigned long)keep * 16, other_free(),
            r.x.cflag ? 0xFFFFu : r.x.ax);
    ser(line);
    fputs(line, stdout);
    fflush(stdout);
    _dos_keep(0, keep);
    return 0;
}
