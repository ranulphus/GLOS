/* contest: the console of GLOS's own DOS (docs/v1/native-dos.md, native-DOS sessions, NS3), run in a native machine
 * by `glos ndos run` with what the test types on its standard input: a line by 0Ah, a key by INT 16h, a
 * character by 08h; Ctrl-C into a handler of its own (INT 23h, which returns by IRET: the call is made again and
 * gets the next key); the timer through INT 1Ch and the BIOS's tick count. One line of results; it exits 0 when
 * all held. "CONTEST CTRLC": Ctrl-C typed with no handler of its own, so DOS ends it ("^C"). */
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <string.h>

static volatile int c23, ticks;

static void __interrupt __far on_c23(void) { c23++; }

static void (__interrupt __far *old1c)(void);

static void __interrupt __far on_1c(void)
{
    ticks++;
    _chain_intr(old1c);
}

static unsigned dos_al(unsigned ax)
{
    union REGS r;

    r.x.ax = ax;
    intdos(&r, &r);
    return r.h.al;
}

int main(int argc, char **argv)
{
    static char line[40];
    union REGS r;
    struct SREGS s;
    unsigned key, ch, after, t0, bios0, bios1, sec0, fails = 0;
    unsigned long far *bios = (unsigned long far *)MK_FP(0x40, 0x6C);

    if (argc > 1 && !strcmp(argv[1], "CTRLC")) {
        printf("CONTEST waiting\n");
        dos_al(0x0100);                         /* (Ctrl-C here: DOS's own handler ends it) */
        printf("CONTEST not ended\n");
        return 9;
    }
    /* a line by 0Ah */
    line[0] = 30;
    segread(&s);
    r.x.ax = 0x0A00;
    r.x.dx = (unsigned)line;
    intdosx(&r, &r, &s);
    line[2 + (unsigned char)line[1]] = 0;
    /* a key by INT 16h, a character by 08h */
    r.h.ah = 0x00;
    int86(0x16, &r, &r);
    key = r.x.ax;
    ch = dos_al(0x0800);
    /* Ctrl-C into its own handler: the 08h made again gets the next key */
    _dos_setvect(0x23, on_c23);
    after = dos_al(0x0800);
    /* the timer: INT 1Ch and the tick count, for a little over half a second of DOS's clock */
    old1c = _dos_getvect(0x1C);
    _dos_setvect(0x1C, on_1c);
    bios0 = (unsigned)*bios;
    r.h.ah = 0x2C;
    intdos(&r, &r);
    sec0 = r.h.dh;
    t0 = 0;
    for (;;) {
        r.h.ah = 0x2C;
        intdos(&r, &r);
        if ((r.h.dh + 60 - sec0) % 60 >= 2 || ++t0 > 60000u)
            break;
    }
    bios1 = (unsigned)*bios;
    _dos_setvect(0x1C, old1c);
    if (strcmp(line + 2, "Ada"))
        fails++;
    if (key != 0x256B || ch != 'm' || c23 != 1 || after != 'x')
        fails++;
    if (ticks < 9 || bios1 - bios0 < 9)
        fails++;
    printf("CONTEST line=%s key=%04X ch=%c c23=%d after=%c ticks=%s bios=%s\n", line + 2, key, ch, c23, after,
           ticks >= 9 ? "ok" : "none", bios1 - bios0 >= 9 ? "ok" : "none");
    return fails;
}
