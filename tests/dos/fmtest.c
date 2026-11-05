/* FMTEST: the FM synthesiser (OPL2/OPL3, AdLib-compatible) of a Sound Blaster or a Crystal chip, for GLOS's audio
 * suite and the bench (docs/crystal-audio.md):
 *     FMTEST BASE HZ MS [HZ MS ...]   BASE in hex (388 the AdLib ports; 220 the SB Pro's): found by its timer, then
 *                                     each note in turn on one voice, a near-sine (the modulator silent), held MS
 * One line on COM1 and stdout:
 *     HX-FM base=388 found=1 notes=N         (found=0: nothing answered the timer test, and nothing is played)
 * Registers as the AdLib and OPL2 programming guides give them: 01h wave select, 20h-95h the operators, A0h/B0h the
 * frequency number, block and key-on, C0h feedback and connection (OPL3: both outputs). Open Watcom, real mode. */
#include <conio.h>
#include <dos.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned base;

static void ser(const char *s)
{
    for (; *s; s++) {
        unsigned n = 0;
        while (!(inp(0x3FD) & 0x20) && ++n < 60000u) ;
        outp(0x3F8, *s);
    }
}

static void wr(unsigned reg, unsigned v)
{
    int i;
    outp(base, reg);
    for (i = 0; i < 6; i++)                     /* (3.3 us after the address, an OPL2's) */
        inp(base);
    outp(base + 1, v);
    for (i = 0; i < 35; i++)                    /* (23 us after the data) */
        inp(base);
}

static int found(void)
{
    unsigned s1, s2, i;
    wr(0x04, 0x60);                             /* the timers reset */
    wr(0x04, 0x80);                             /* the interrupt reset */
    s1 = inp(base);
    wr(0x02, 0xFF);                             /* timer 1 at its shortest */
    wr(0x04, 0x21);                             /* started */
    for (i = 0; i < 200; i++)                   /* (80 us at least) */
        inp(base);
    s2 = inp(base);
    wr(0x04, 0x60);
    wr(0x04, 0x80);
    return (s1 & 0xE0) == 0x00 && (s2 & 0xE0) == 0xC0;
}

static void wait_ms(unsigned ms)
{
    volatile unsigned long __far *ticks = (volatile unsigned long __far *)MK_FP(0x40, 0x6C);
    unsigned long t = *ticks, n = (ms * 182UL + 9999) / 10000;     /* BIOS ticks of 54.9 ms */
    while (*ticks - t < n) ;
}

static void note(unsigned long hz, unsigned ms)
{
    unsigned block = 0;
    unsigned long fnum;
    while (block < 7 && hz * ((1UL << 20) >> block) / 49716UL > 1023)
        block++;
    fnum = hz * ((1UL << 20) >> block) / 49716UL;
    wr(0xA0, (unsigned)(fnum & 0xFF));
    wr(0xB0, 0x20 | (block << 2) | (unsigned)(fnum >> 8));     /* key on */
    wait_ms(ms);
    wr(0xB0, (block << 2) | (unsigned)(fnum >> 8));            /* key off */
    wait_ms(60);
}

int main(int argc, char **argv)
{
    char line[64];
    int ok, i, n = 0;
    unsigned r;
    base = argc > 1 ? (unsigned)strtoul(argv[1], 0, 16) : 0x388;
    ok = found();
    if (ok) {
        for (r = 0x01; r <= 0xF5; r++)
            wr(r, 0);
        wr(0x01, 0x20);                         /* wave select allowed */
        wr(0x20, 0x21), wr(0x23, 0x21);         /* modulator, carrier: sustained, multiple 1 */
        wr(0x40, 0x3F), wr(0x43, 0x00);         /* the modulator silent: the carrier a sine at full level */
        wr(0x60, 0xF0), wr(0x63, 0xF0);         /* attack fastest, no decay */
        wr(0x80, 0x05), wr(0x83, 0x05);         /* sustain at full, release 5 */
        wr(0xC0, 0x30);                         /* both outputs (OPL3), FM connection */
        for (i = 2; i + 1 < argc; i += 2, n++)
            note(strtoul(argv[i], 0, 10), (unsigned)atoi(argv[i + 1]));
    }
    sprintf(line, "HX-FM base=%x found=%d notes=%d\r\n", base, ok, n);
    ser(line);
    fputs(line, stdout);
    return ok ? 0 : 1;
}
