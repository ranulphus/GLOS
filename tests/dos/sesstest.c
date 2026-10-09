/* SESSTEST (GLOS M4e): leaves behind what a session's end must undo
 * (supervisor.md §11): video mode 13h, IRQ5's PIC mask bit flipped (the
 * BIOS's default handler takes it if it was masked), PIT channel 0
 * at 16 times the BIOS rate for two seconds (the DOS clock runs ahead), Caps
 * Lock in the BIOS flags, and INT 60h hooked into its own code, which DOS
 * frees when it exits. On COM1: HX-SESS done. With the argument "hang" it
 * then says HX-SESS armed and spins until it is killed (M4e E5: the kill
 * puts the same things back). Open Watcom, real mode. */
#include <conio.h>
#include <dos.h>
#include <i86.h>

static void ser(const char *s)
{
    for (; *s; s++) {
        unsigned n = 0;
        while (!(inp(0x3FD) & 0x20) && ++n < 60000u) ;
        outp(0x3F8, *s);
    }
}

static void __interrupt __far int60(void) { }

int main(int argc, char **argv)
{
    union REGS r;
    volatile unsigned long __far *tick = MK_FP(0x40, 0x6C);
    unsigned long t;

    r.x.ax = 0x0013;                    /* mode 13h */
    int86(0x10, &r, &r);
    outp(0x21, inp(0x21) ^ 0x20);       /* IRQ5's mask bit */
    _disable();
    outp(0x43, 0x36);                   /* PIT channel 0: mode 3, 1000h (16 x 18.2 Hz) */
    outp(0x40, 0x00);
    outp(0x40, 0x10);
    _enable();
    t = *tick;
    while (*tick - t < 2 * 18 * 16) ;   /* two seconds: the BIOS clock gains 30 */
    *(unsigned char __far *)MK_FP(0x40, 0x17) |= 0x40;     /* Caps Lock */
    _dos_setvect(0x60, int60);          /* into memory DOS takes back at the exit */
    ser("HX-SESS done\r\n");
    if (argc > 1 && (argv[1][0] | 0x20) == 'h') {
        ser("HX-SESS armed\r\n");
        for (;;)
            ;
    }
    return 0;
}
