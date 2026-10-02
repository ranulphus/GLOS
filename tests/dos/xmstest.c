/* XMSTEST - XMS functions and their error codes, as lines on COM1 that are
 * the same under any correct XMS 3.0 driver: no addresses, no sizes that
 * depend on the machine (milestones-m0-m4.md M2: GLOS's server against
 * HIMEMX). "HX-XMSTEST none" without a driver. */
#include <conio.h>
#include <dos.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void put(char c)
{
    long spin = 0;
    while (!(inp(0x3FD) & 0x20) && ++spin < 100000L) ;
    outp(0x3F8, c);
}

static void line(const char *fmt, ...)
{
    char b[120];
    const char *p;
    va_list ap;
    va_start(ap, fmt);
    vsprintf(b, fmt, ap);
    va_end(ap);
    for (p = "HX-XMSTEST "; *p; p++) put(*p);
    for (p = b; *p; p++) put(*p);
    put('\r');
    put('\n');
}

static void (far *xms)(void);
static unsigned ax_, bx_, dx_;

/* AH = fn; BX, DX and DS:SI in. */
static void call(unsigned char fn, unsigned in_bx, unsigned in_dx, void *in_si)
{
    unsigned a, b, d;
    _asm {
        push si
        mov ah, fn
        mov bx, in_bx
        mov dx, in_dx
        mov si, in_si
        call dword ptr xms
        mov a, ax
        mov b, bx
        mov d, dx
        pop si
    }
    ax_ = a; bx_ = b; dx_ = d;
}

/* AX=1, or "err=XX" from BL. */
static const char *res(void)
{
    static char b[16];
    if (ax_ == 1)
        return "ok";
    sprintf(b, "err=%02x", bx_ & 0xFF);
    return b;
}

#pragma pack(1)
struct move {
    unsigned long len;
    unsigned sh;
    unsigned long so;
    unsigned dh;
    unsigned long dof;
};
#pragma pack()

static unsigned char buf[2048], back[2048];

static unsigned long far_off(void *p)
{
    struct SREGS s;
    segread(&s);
    return ((unsigned long)s.ds << 16) | (unsigned)p;
}

static void mv(unsigned long len, unsigned sh, unsigned long so, unsigned dh, unsigned long dof)
{
    struct move m;
    m.len = len; m.sh = sh; m.so = so; m.dh = dh; m.dof = dof;
    call(0x0B, 0, 0, &m);
}

int main(void)
{
    union REGS r;
    struct SREGS s;
    unsigned h, h2, i, largest, total;
    int same;

    outp(0x3FB, 0x80); outp(0x3F8, 1); outp(0x3F9, 0); outp(0x3FB, 0x03);
    outp(0x3FA, 0xC7); outp(0x3FC, 0x03);
    r.x.ax = 0x4300;
    int86(0x2F, &r, &r);
    if (r.h.al != 0x80) {
        line("none");
        return 0;
    }
    segread(&s);
    r.x.ax = 0x4310;
    int86x(0x2F, &r, &r, &s);
    xms = (void (far *)(void))MK_FP(s.es, r.x.bx);

    call(0x00, 0, 0, 0);
    line("ver=%04x hma=%u", ax_, dx_ ? 1 : 0);
    call(0x08, 0, 0, 0);
    largest = ax_;
    total = dx_;
    line("query sane=%u", largest <= total && largest >= 256);

    call(0x09, 0, 64, 0);
    line("alloc64 %s", res());
    h = dx_;
    for (i = 0; i < sizeof buf; i++)
        buf[i] = (unsigned char)(i * 7 + 3);
    mv(sizeof buf, 0, far_off(buf), h, 62UL * 1024);
    line("move-in %s", res());
    mv(sizeof back, h, 62UL * 1024, 0, far_off(back));
    line("move-out %s verify=%u", res(), memcmp(buf, back, sizeof buf) == 0);
    mv(3, 0, far_off(buf), h, 0);
    line("move-odd %s", res());
    mv(2, 0, far_off(buf), 0x7777, 0);
    line("move-dst-handle %s", res());
    mv(2, 0x7777, 0, 0, far_off(buf));
    line("move-src-handle %s", res());
    mv(2048, 0, far_off(buf), h, 63UL * 1024);
    line("move-past-end %s", res());

    call(0x0C, 0, h, 0);
    line("lock %s", res());
    call(0x0E, 0, h, 0);
    line("info %s locks=%u kb=%u", res(), bx_ >> 8, dx_);
    call(0x0F, 128, h, 0);
    line("realloc-locked %s", res());
    call(0x0A, 0, h, 0);
    line("free-locked %s", res());
    call(0x0D, 0, h, 0);
    line("unlock %s", res());
    call(0x0D, 0, h, 0);
    line("unlock-again %s", res());

    call(0x0F, 128, h, 0);
    line("realloc128 %s", res());
    memset(back, 0, sizeof back);
    mv(sizeof back, h, 62UL * 1024, 0, far_off(back));
    line("after-realloc %s verify=%u", res(), memcmp(buf, back, sizeof buf) == 0);
    call(0x0E, 0, h, 0);
    line("info %s kb=%u", res(), dx_);

    call(0x09, 0, 0, 0);
    line("alloc0 %s", res());
    h2 = dx_;
    call(0x0A, 0, h2, 0);
    line("free0 %s", res());
    call(0x09, 0, 0xFFFF, 0);
    line("alloc-huge %s", res());
    if (ax_ == 1) {
        call(0x0A, 0, dx_, 0);
    }

    call(0x0A, 0, h, 0);
    line("free %s", res());
    call(0x0A, 0, h, 0);
    line("free-again %s", res());
    call(0x08, 0, 0, 0);
    same = ax_ == largest && dx_ == total;
    line("query-after same=%u", same);

    call(0x01, 0, 0xFFFF, 0);
    line("hma %s", res());
    if (ax_ == 1) {
        call(0x02, 0, 0, 0);
        line("hma-release %s", res());
    }
    call(0x07, 0, 0, 0);
    line("a20 %u", ax_);
    call(0x05, 0, 0, 0);
    line("a20-local-on %s", res());
    call(0x07, 0, 0, 0);
    line("a20 %u", ax_);
    call(0x06, 0, 0, 0);
    line("a20-local-off done");
    call(0x10, 0, 0xFFFF, 0);
    line("umb %s", res());
    line("end");
    return 0;
}
