/* TIMECHK (GLOS M4e): the DOS clock against the RTC, and two more things
 * SESSTEST leaves behind (INT 60h's vector, the BIOS keyboard flags' lock
 * bits):
 *     HX-TIME rtc=HH:MM:SS dos=HH:MM:SS diff=N vec60=SSSSOOOO kbd=NN
 * N in seconds (across midnight it is meaningless). Open Watcom, real mode. */
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>

static void ser(const char *s)
{
    for (; *s; s++) {
        unsigned n = 0;
        while (!(inp(0x3FD) & 0x20) && ++n < 60000u) ;
        outp(0x3F8, *s);
    }
}

static unsigned bcd(unsigned char v) { return (v >> 4) * 10 + (v & 15); }

int main(void)
{
    union REGS r;
    long rs, ds;
    char m[80];
    unsigned rh, rm, rsec;

    r.h.ah = 0x02;
    int86(0x1A, &r, &r);
    rh = bcd(r.h.ch);
    rm = bcd(r.h.cl);
    rsec = bcd(r.h.dh);
    rs = rh * 3600L + rm * 60L + rsec;
    r.h.ah = 0x2C;
    intdos(&r, &r);
    ds = r.h.ch * 3600L + r.h.cl * 60L + r.h.dh;
    sprintf(m, "HX-TIME rtc=%02u:%02u:%02u dos=%02u:%02u:%02u diff=%ld vec60=%08lx kbd=%02x\r\n", rh, rm, rsec, r.h.ch,
            r.h.cl, r.h.dh, labs(ds - rs), *(unsigned long __far *)MK_FP(0, 0x60 * 4),
            *(unsigned char __far *)MK_FP(0x40, 0x17) & 0x70);
    ser(m);
    return 0;
}
