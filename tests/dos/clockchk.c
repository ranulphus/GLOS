/* CLOCKCHK: what DOS and the BIOS say the date and time are (tests/loopa/jobs.py time; M10 step A).
 * stdout: "CLOCK dos=YYYY-MM-DDTHH:MM:SS rtc=YYYY-MM-DDTHH:MM:SS" (INT 21h 2Ah and 2Ch; INT 1Ah 04h and 02h,
 * the RTC's BCD as the BIOS gives it). */
#include <dos.h>
#include <stdio.h>

static int bcd(unsigned char v) { return (v >> 4) * 10 + (v & 15); }

int main(void)
{
    union REGS r;
    int y, mo, d, h, mi, s;
    r.h.ah = 0x2A;
    intdos(&r, &r);
    y = r.x.cx, mo = r.h.dh, d = r.h.dl;
    r.h.ah = 0x2C;
    intdos(&r, &r);
    h = r.h.ch, mi = r.h.cl, s = r.h.dh;
    printf("CLOCK dos=%04d-%02d-%02dT%02d:%02d:%02d", y, mo, d, h, mi, s);
    r.h.ah = 0x04;
    int86(0x1A, &r, &r);
    y = bcd(r.h.ch) * 100 + bcd(r.h.cl), mo = bcd(r.h.dh), d = bcd(r.h.dl);
    r.h.ah = 0x02;
    int86(0x1A, &r, &r);
    printf(" rtc=%04d-%02d-%02dT%02d:%02d:%02d\n", y, mo, d, bcd(r.h.ch), bcd(r.h.cl), bcd(r.h.dh));
    return 0;
}
