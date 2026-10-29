/* XMSBIG - the disk cache gives back a run (docs/disk-cache.md C3): XMS's largest free block (AH=08h)
 * allocated whole (AH=09h) while the cache holds memory, its first and last KB written and read back
 * (AH=0Bh), then freed. One line on COM1:
 *     HX-XMSBIG largest=KB total=KB alloc=ok|err=XX moved=ok|bad */
#include <conio.h>
#include <dos.h>
#include <stdio.h>
#include <string.h>

static void put(char c)
{
    long spin = 0;
    while (!(inp(0x3FD) & 0x20) && ++spin < 100000L) ;
    outp(0x3F8, c);
}

static void say(const char *s)
{
    for (; *s; s++) put(*s);
    put('\r');
    put('\n');
}

static void (far *xms)(void);
static unsigned ax_, bx_, dx_;

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

#pragma pack(1)
struct move { unsigned long len; unsigned sh; unsigned long so; unsigned dh; unsigned long dof; };
#pragma pack()

static unsigned char buf[1024], back[1024];

static unsigned long far_off(void *p)
{
    struct SREGS s;
    segread(&s);
    return ((unsigned long)s.ds << 16) | (unsigned)p;
}

/* 1 KB to the block at KB offset at, and back: 1 if the same. */
static int round_trip(unsigned h, unsigned long at, unsigned char seed)
{
    struct move m;
    unsigned i;
    for (i = 0; i < sizeof buf; i++)
        buf[i] = (unsigned char)(i * 3 + seed);
    m.len = sizeof buf, m.sh = 0, m.so = far_off(buf), m.dh = h, m.dof = at * 1024;
    call(0x0B, 0, 0, &m);
    if (ax_ != 1)
        return 0;
    memset(back, 0, sizeof back);
    m.sh = h, m.so = at * 1024, m.dh = 0, m.dof = far_off(back);
    call(0x0B, 0, 0, &m);
    return ax_ == 1 && !memcmp(buf, back, sizeof buf);
}

int main(void)
{
    union REGS r;
    struct SREGS s;
    char line[96];
    unsigned largest, total, h;
    int moved = 0;
    r.x.ax = 0x4300;
    int86(0x2F, &r, &r);
    if (r.h.al != 0x80) {
        say("HX-XMSBIG none");
        return 1;
    }
    r.x.ax = 0x4310;
    int86x(0x2F, &r, &r, &s);
    xms = (void (far *)(void))MK_FP(s.es, r.x.bx);
    call(0x08, 0, 0, 0);
    largest = ax_, total = dx_;
    call(0x09, 0, largest, 0);
    h = dx_;
    if (ax_ == 1) {
        moved = round_trip(h, 0, 1) && round_trip(h, largest - 1, 2);
        call(0x0A, 0, h, 0);
        sprintf(line, "HX-XMSBIG largest=%u total=%u alloc=ok moved=%s", largest, total, moved ? "ok" : "bad");
    } else {
        sprintf(line, "HX-XMSBIG largest=%u total=%u alloc=err=%02x moved=bad", largest, total, bx_ & 0xFF);
    }
    say(line);
    return 0;
}
