/* HOSTILE <case> - a real-mode program that takes the machine away in one
 * way, for GLOS's kill test (milestones-m0-m4.md, M2). It reports
 * "HX-HOSTILE <case> armed" on COM1, misbehaves and spins until killed
 * (Ctrl-Alt-Shift-Esc under GLOS). On bare metal most cases hang or reset
 * the machine. Cases:
 *   CLIJMP    CLI, then a jump to itself
 *   POPFIF    IF cleared through POPF, then the jump
 *   HLTCLI    CLI; HLT
 *   A20OFF    A20 off through port 92h, the 8042 and INT 15h 2400h
 *   PICREMAP  the master PIC moved to INT 60h (handlers that EOI)
 *   RTCWRITE  RTC register A at 2 Hz, B with PIE off and SET on
 *   PITPROG   PIT channel 0 at 1 kHz and an INT 08h that only EOIs
 *   KBCRESET  the 8042's reset pulse (FEh)
 *   CF9RESET  a hard reset through port CF9h
 *   CAD       nothing: the harness sends Ctrl-Alt-Del
 *   PRIV      MOV EAX,CR0, which V86 mode cannot run (GLOS kills it itself) */
#include <conio.h>
#include <dos.h>
#include <stdlib.h>
#include <string.h>

static void put(char c)
{
    long spin = 0;
    while (!(inp(0x3FD) & 0x20) && ++spin < 100000L) ;
    outp(0x3F8, c);
}

static void say(const char *s) { while (*s) put(*s++); }

static void __interrupt __far eoi_iret(void) { outp(0x20, 0x20); }

static void spin(void)
{
    _enable();
    for (;;)
        ;
}

static void kbc_wait(void)
{
    long n = 0;
    while ((inp(0x64) & 2) && ++n < 100000L) ;
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    union REGS r;
    unsigned v;
    unsigned char b;

    say("HX-HOSTILE ");
    say(c);
    say(" armed\r\n");

    if (!stricmp(c, "CLIJMP")) {
        _disable();
        for (;;)
            ;
    }
    if (!stricmp(c, "POPFIF")) {
        _asm {
            pushf
            pop ax
            and ax, 0FDFFh
            push ax
            popf
        }
        for (;;)
            ;
    }
    if (!stricmp(c, "HLTCLI")) {
        for (;;) {
            _asm {
                cli
                hlt
            }
        }
    }
    if (!stricmp(c, "A20OFF")) {
        unsigned char far *lo = (unsigned char far *)MK_FP(0x0000, 0x0080);
        unsigned char far *hi = (unsigned char far *)MK_FP(0xFFFF, 0x0090);
        unsigned char s;
        outp(0x92, inp(0x92) & ~2);
        kbc_wait(); outp(0x64, 0xD1);
        kbc_wait(); outp(0x60, 0xDD);
        kbc_wait();
        r.x.ax = 0x2400;
        int86(0x15, &r, &r);
        _disable();
        s = *lo;
        *lo = 0x5A;
        say(*hi == 0x5A ? "HX-HOSTILE a20 wrap=1\r\n" : "HX-HOSTILE a20 wrap=0\r\n");
        *lo = s;
        spin();
    }
    if (!stricmp(c, "PICREMAP")) {
        for (v = 0x60; v < 0x68; v++)
            _dos_setvect(v, eoi_iret);
        _disable();
        outp(0x20, 0x11); outp(0x21, 0x60); outp(0x21, 0x04); outp(0x21, 0x01);
        outp(0x21, 0x00);
        spin();
    }
    if (!stricmp(c, "RTCWRITE")) {
        _disable();
        outp(0x70, 0x0A); outp(0x71, 0x2F);
        outp(0x70, 0x0B); b = (unsigned char)inp(0x71);
        outp(0x70, 0x0B); outp(0x71, (b & ~0x40) | 0x80);
        spin();
    }
    if (!stricmp(c, "PITPROG")) {
        _dos_setvect(0x08, eoi_iret);
        _disable();
        outp(0x43, 0x36); outp(0x40, 0xA9); outp(0x40, 0x04);      /* 1193: 1 kHz */
        spin();
    }
    if (!stricmp(c, "KBCRESET")) {
        kbc_wait();
        outp(0x64, 0xFE);
        spin();
    }
    if (!stricmp(c, "CF9RESET")) {
        outp(0xCF9, 0x06);
        spin();
    }
    if (!stricmp(c, "CAD"))
        spin();
    if (!stricmp(c, "PRIV")) {
        _asm {
            db 0Fh, 20h, 0C0h       ; mov eax, cr0
        }
        say("HX-HOSTILE survived\r\n");
        return 0;
    }
    say("HX-HOSTILE unknown case\r\n");
    return 1;
}
