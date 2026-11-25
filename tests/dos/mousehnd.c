/* MOUSEHND: a mouse driver's event routine (INT 33h 0Ch), for the input suite (tests/loopa/jobs.py) under GLOS's
 * own INT 33h on a native boot (kernel/vm/vmouse.c, N4's step 2) and under CuteMouse hosted, the same program.
 * Real mode, Open Watcom.
 *
 * Resets the driver, sets a routine for every event (mask 7Fh), says "HX-MOUSEHND ready" on COM1 (the harness
 * moves the mouse and presses and lets go its left button, timed from that line), and waits up to 20 seconds for a
 * move, a left press and a left release. Then: "MOUSEHND calls=N events=EE last=LL buttons=B x=X y=Y" (EE the
 * events seen, all calls' AX together), the routine taken away (0Ch, mask 0), and exit code 0 if all three came,
 * else 1 (2: no driver). */
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>

static volatile unsigned calls, events, last_ev, last_b, last_x, last_y;

/* The driver calls this far, the events in AX, the buttons in BX, the position in CX and DX; __loadds gives it
   this program's DS. */
#pragma aux on_mouse parm [ax] [bx] [cx] [dx]
static void __far __loadds on_mouse(unsigned ev, unsigned b, unsigned x, unsigned y)
{
    calls++;
    events |= ev;
    last_ev = ev, last_b = b, last_x = x, last_y = y;
}

static void com1(const char *s)
{
    for (; *s; s++) {
        unsigned n = 0;
        while (!(inp(0x3FD) & 0x20) && ++n < 60000u)
            ;
        outp(0x3F8, *s);
    }
}

int main(void)
{
    union REGS r;
    struct SREGS sr;
    volatile unsigned long __far *ticks = (volatile unsigned long __far *)MK_FP(0x40, 0x6C);
    unsigned long t0;
    void (__far *h)(unsigned, unsigned, unsigned, unsigned) = on_mouse;

    r.x.ax = 0x0000;
    int86(0x33, &r, &r);
    if (r.x.ax != 0xFFFF) {
        printf("MOUSEHND no driver (ax=%04x)\n", r.x.ax);
        return 2;
    }
    segread(&sr);
    r.x.ax = 0x000C;
    r.x.cx = 0x007F;
    r.x.dx = FP_OFF(h);
    sr.es = FP_SEG(h);
    int86x(0x33, &r, &r, &sr);
    com1("HX-MOUSEHND ready\r\n");
    t0 = *ticks;
    while (*ticks - t0 < 20UL * 182 / 10 && (events & 0x07) != 0x07)
        ;
    printf("MOUSEHND calls=%u events=%02x last=%02x buttons=%u x=%u y=%u\n", calls, events, last_ev, last_b, last_x,
           last_y);
    segread(&sr);
    r.x.ax = 0x000C;
    r.x.cx = 0;
    r.x.dx = 0;
    sr.es = 0;
    int86x(0x33, &r, &r, &sr);
    return (events & 0x07) == 0x07 ? 0 : 1;
}
