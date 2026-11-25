/* MOUSEHND: a mouse driver's event routine (INT 33h 0Ch), for the input suite (tests/loopa/jobs.py) under GLOS's
 * own INT 33h on a native boot (kernel/vm/vmouse.c, N4's step 2) and under CuteMouse hosted, the same program.
 * Real mode, Open Watcom.
 *
 * Resets the driver, sets a routine for every event (mask 7Fh), says "HX-MOUSEHND ready" on COM1 (the harness
 * moves the mouse and presses and lets go its left button, timed from that line), and waits up to 20 seconds for a
 * move, a left press and a left release. Then: "MOUSEHND calls=N events=EE last=LL buttons=B x=X y=Y" (EE the
 * events seen, all calls' AX together), the routine taken away (0Ch, mask 0), and exit code 0 if all three came,
 * else 1 (2: no driver).
 *
 *   MOUSEHND cursor     the text cursor (N4's step 3), no mouse moved: in mode 3, a cell's word known, the
 *                       position set there (04h), the cursor shown (01h): the cell must read (word AND 77FFh)
 *                       XOR 7700h, the default masks; moved a row down (04h): the old cell as it was, the new one
 *                       turned round; hidden (02h): as it was; shown again and the cell then written by the
 *                       program, hidden: the program's word kept ("kept", reported, not counted: a driver may
 *                       restore its saved word blind). "MOUSECUR show=OK move=OK hide=OK kept=yes|no", exit code 0
 *                       if the first three are OK.
 *   MOUSEHND gcursor    the graphics cursor (N4's step 4), in modes 13h, 12h, 04h, 06h, 0Dh: a patch of colour 1,
 *                       09h's shape (screen mask 00FFh, cursor mask F0F0h on every row: the left half cleared,
 *                       quarters turned white), shown at the patch (04h, 01h) and read back by the BIOS (INT 10h
 *                       0Dh); a pixel under it drawn by the program, the cursor moved off (the patch as it was, the
 *                       program's pixel kept), hidden (the new place as it was); in 12h first the default arrow
 *                       (its corner cleared, its second row's second pixel white, the rest of its first row kept).
 *                       Back in mode 3: "MOUSEGFX 13=OK 12=OK 04=OK 06=OK 0D=OK arrow=OK", exit code 0 if all OK.
 *                       Natively only: the shape's pixels in 320-wide modes and the white differ between drivers. */
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

static unsigned __far *cell(int row, int col) { return (unsigned __far *)MK_FP(0xB800, (row * 80 + col) * 2); }

static void m33(unsigned ax, unsigned cx, unsigned dx)
{
    union REGS r;
    r.x.ax = ax, r.x.cx = cx, r.x.dx = dx;
    int86(0x33, &r, &r);
}

static int cursor_test(void)
{
    const unsigned w1 = 0x1741, w2 = 0x2E42, mine = 0x4F43;    /* ('A' blue, 'B' green, 'C' red) */
    int show, move, hide, kept;

    *cell(5, 10) = w1, *cell(6, 10) = w2;
    m33(0x0004, 10 * 8, 5 * 8);
    m33(0x0001, 0, 0);
    show = *cell(5, 10) == ((w1 & 0x77FF) ^ 0x7700);
    m33(0x0004, 10 * 8, 6 * 8);
    move = *cell(5, 10) == w1 && *cell(6, 10) == ((w2 & 0x77FF) ^ 0x7700);
    m33(0x0002, 0, 0);
    hide = *cell(6, 10) == w2 && *cell(5, 10) == w1;
    m33(0x0001, 0, 0);
    *cell(6, 10) = mine;
    m33(0x0002, 0, 0);
    kept = *cell(6, 10) == mine;
    printf("MOUSECUR show=%s move=%s hide=%s kept=%s (cells %04x %04x)\n", show ? "OK" : "BAD", move ? "OK" : "BAD",
           hide ? "OK" : "BAD", kept ? "yes" : "no", *cell(5, 10), *cell(6, 10));
    return show && move && hide ? 0 : 1;
}

static void setmode(unsigned mode)
{
    union REGS r;
    r.x.ax = mode & 0xFF;
    int86(0x10, &r, &r);
}

static unsigned getpx(int x, int y)
{
    union REGS r;
    r.h.ah = 0x0D, r.h.bh = 0, r.x.cx = (unsigned)x, r.x.dx = (unsigned)y;
    int86(0x10, &r, &r);
    return r.h.al;
}

static void putpx(int x, int y, unsigned c)
{
    union REGS r;
    r.h.ah = 0x0C, r.h.al = (unsigned char)c, r.h.bh = 0, r.x.cx = (unsigned)x, r.x.dx = (unsigned)y;
    int86(0x10, &r, &r);
}

static unsigned shape[32];

/* The 16 x 16 box at x, y: each pixel what the test shape makes of colour bg (cursor 1) or bg itself (0); the
   pixel at kx, ky may instead be kc (the program's own). 1 if so. */
static int box_is(int x, int y, unsigned bg, unsigned white, int cursor, int kx, int ky, unsigned kc)
{
    int r, c;
    for (r = 0; r < 16; r++)
        for (c = 0; c < 16; c++) {
            unsigned want = bg, got = getpx(x + c, y + r);
            if (cursor)
                want = ((c < 8) ? 0 : bg) ^ ((c & 4) ? 0 : white);
            if (x + c == kx && y + r == ky)
                want = kc;
            if (got != want)
                return 0;
        }
    return 1;
}

static int gmode_test(unsigned mode, int xhalf, unsigned white)
{
    const int X = 40, Y = 40;
    unsigned mine = white == 1 ? 0 : 2;
    union REGS r;
    struct SREGS sr;
    int x, y, ok;

    setmode(mode);
    m33(0x0000, 0, 0);
    for (y = Y; y < Y + 32; y++)
        for (x = X; x < X + 32; x++)
            putpx(x, y, 1);
    segread(&sr);
    r.x.ax = 0x0009, r.x.bx = 0, r.x.cx = 0;
    r.x.dx = FP_OFF((unsigned __far *)shape);
    sr.es = FP_SEG((unsigned __far *)shape);
    int86x(0x33, &r, &r, &sr);
    m33(0x0004, (unsigned)X << xhalf, Y);
    m33(0x0001, 0, 0);
    ok = box_is(X, Y, 1, white, 1, -1, -1, 0);
    putpx(X + 1, Y, mine);                      /* (under the cursor: drawn white there) */
    m33(0x0004, (unsigned)(X + 16) << xhalf, Y + 16);
    ok = ok && box_is(X, Y, 1, white, 0, X + 1, Y, mine) && box_is(X + 16, Y + 16, 1, white, 1, -1, -1, 0);
    m33(0x0002, 0, 0);
    ok = ok && box_is(X + 16, Y + 16, 1, white, 0, -1, -1, 0);
    return ok;
}

static int arrow_test(void)
{
    const int X = 100, Y = 100;
    int x, y, ok;

    setmode(0x12);
    m33(0x0000, 0, 0);
    for (y = Y; y < Y + 16; y++)
        for (x = X; x < X + 16; x++)
            putpx(x, y, 1);
    m33(0x0004, X, Y);
    m33(0x0001, 0, 0);
    ok = getpx(X, Y) == 0 && getpx(X + 1, Y + 1) == 15 && getpx(X + 15, Y) == 1;
    m33(0x0002, 0, 0);
    return ok && getpx(X, Y) == 1 && getpx(X + 1, Y + 1) == 1;
}

static int gcursor_test(void)
{
    int i, ok13, ok12, ok04, ok06, ok0d, okar;
    for (i = 0; i < 16; i++)
        shape[i] = 0x00FF, shape[16 + i] = 0xF0F0;
    okar = arrow_test();
    ok13 = gmode_test(0x13, 1, 15);
    ok12 = gmode_test(0x12, 0, 15);
    ok04 = gmode_test(0x04, 1, 3);
    ok06 = gmode_test(0x06, 0, 1);
    ok0d = gmode_test(0x0D, 1, 15);
    setmode(3);
    m33(0x0000, 0, 0);
    printf("MOUSEGFX 13=%s 12=%s 04=%s 06=%s 0D=%s arrow=%s\n", ok13 ? "OK" : "BAD", ok12 ? "OK" : "BAD",
           ok04 ? "OK" : "BAD", ok06 ? "OK" : "BAD", ok0d ? "OK" : "BAD", okar ? "OK" : "BAD");
    return ok13 && ok12 && ok04 && ok06 && ok0d && okar ? 0 : 1;
}

int main(int argc, char **argv)
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
    if (argc > 1 && argv[1][0] == 'c')
        return cursor_test();
    if (argc > 1 && argv[1][0] == 'g')
        return gcursor_test();
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
