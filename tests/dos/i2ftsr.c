/* I2FTSR - a resident program that hooks INT 2Fh, as DOSLFN does, for GLOS's pass-through at the leave
 * (kernel/vm/v86.c int2f_unhook):
 *     I2FTSR          hooks INT 2Fh (AX=C7FEh comes back 55AAh; the rest goes on down the chain it found)
 *                     and stays resident, its whole block
 *     I2FTSR CHECK    INT 2Fh C7FEh (the TSR there) and 4300h (an XMS driver down the chain), and the vector
 * One line on COM1 each:
 *     HX-I2FTSR psp=SSSS prev=SSSS:OOOO
 *     HX-I2F tsr=0|1 xms=XX vec=SSSS:OOOO
 * Open Watcom, real mode. */
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <string.h>

static void (__interrupt __far *old2f)(void);

static void ser(const char *s)
{
    for (; *s; s++) {
        unsigned n = 0;
        while (!(inp(0x3FD) & 0x20) && ++n < 60000u) ;
        outp(0x3F8, *s);
    }
}

static void __interrupt __far new2f(union INTPACK r)
{
    if (r.w.ax == 0xC7FE) {
        r.w.ax = 0x55AA;
        return;
    }
    _chain_intr(old2f);
}

int main(int argc, char **argv)
{
    union REGS r;
    char line[80];
    unsigned psp;
    void (__interrupt __far *v)(void) = _dos_getvect(0x2F);

    if (argc > 1 && !stricmp(argv[1], "CHECK")) {
        unsigned tsr, xms;
        r.x.ax = 0xC7FE;
        int86(0x2F, &r, &r);
        tsr = r.x.ax == 0x55AA;
        r.x.ax = 0x4300;
        int86(0x2F, &r, &r);
        xms = r.h.al;
        sprintf(line, "HX-I2F tsr=%u xms=%02X vec=%04X:%04X\r\n", tsr, xms, FP_SEG(v), FP_OFF(v));
        ser(line);
        fputs(line, stdout);
        return 0;
    }
    r.h.ah = 0x62;
    intdos(&r, &r);
    psp = r.x.bx;
    old2f = v;
    _dos_setvect(0x2F, new2f);
    sprintf(line, "HX-I2FTSR psp=%04X prev=%04X:%04X\r\n", psp, FP_SEG(v), FP_OFF(v));
    ser(line);
    fputs(line, stdout);
    fflush(stdout);
    _dos_keep(0, *(unsigned __far *)MK_FP(psp - 1, 3));        /* the block as DOS gave it: code, data, stack */
    return 0;
}
