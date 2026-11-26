/* EMSTEST: EMS asked as programs ask it (GLOS's own LIM 4.0 manager natively: kernel/vm/ems.c and emsvm.c, N4's E2-E4),
 * for loopa's ems case. Real mode, Open Watcom.
 *
 * Each check a line "EMSTEST name PASS|FAIL what"; then "EMSTEST done failures=N", N the exit code. The INT 67h
 * vector's segment holds EMMXXXX0 at 0Ah; EMMXXXX0 opens as a device (4400h: bit 7), ready (4407h); status,
 * version 4.0, the page frame, the pages; four pages allocated and mapped, each physical page written through the
 * frame with its own pattern; logical page 3 shown at physical page 0 then holds page 3's pattern (the page
 * tables, not a copy); 57h moves conventional memory into page 2, read back through the frame; the pages given
 * back, the count as it was. */
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <string.h>

static int fails;

static void check(const char *name, int ok, const char *what)
{
    printf("EMSTEST %s %s %s\n", name, ok ? "PASS" : "FAIL", what);
    fails += !ok;
}

static unsigned ems(unsigned ax, unsigned bx, unsigned dx, union REGS *o)
{
    union REGS r;
    r.x.ax = ax, r.x.bx = bx, r.x.dx = dx;
    int86(0x67, &r, o);
    return o->h.ah;
}

static unsigned char conv[1000];

#pragma pack(push, 1)
static struct {
    unsigned long len;
    unsigned char st;
    unsigned sh, so, ss;
    unsigned char dt;
    unsigned dh, dofs, ds;
} mv;
#pragma pack(pop)

int main(void)
{
    union REGS o;
    struct SREGS s;
    char what[96];
    unsigned frame, handle, free0, k, h;
    unsigned long i;
    int ok;
    unsigned char __far *v67 = (unsigned char __far *)MK_FP(*(unsigned __far *)MK_FP(0, 0x67 * 4 + 2), 10);

    check("name", !_fmemcmp(v67, "EMMXXXX0", 8), "INT 67h's segment: EMMXXXX0 at 0Ah");
    if (fails) {
        printf("EMSTEST done failures=%d\n", fails);
        return fails;
    }
    ok = _dos_open("EMMXXXX0", 0, (int *)&h) == 0;
    if (ok) {
        union REGS r;
        r.x.ax = 0x4400, r.x.bx = h;
        int86(0x21, &r, &r);
        ok = !r.x.cflag && (r.x.dx & 0x80);
        r.x.ax = 0x4407, r.x.bx = h;
        int86(0x21, &r, &r);
        ok = ok && !r.x.cflag && r.h.al == 0xFF;
        _dos_close(h);
    }
    check("device", ok, "EMMXXXX0 opened: a device, ready");
    check("40h", ems(0x4000, 0, 0, &o) == 0, "status");
    check("46h", ems(0x4600, 0, 0, &o) == 0 && o.h.al == 0x40, "version 4.0");
    ems(0x4100, 0, 0, &o);
    frame = o.x.bx;
    sprintf(what, "the page frame at %04x", frame);
    check("41h", o.h.ah == 0 && frame >= 0xC000 && frame < 0xF000, what);
    ems(0x4200, 0, 0, &o);
    free0 = o.x.bx;
    sprintf(what, "%u pages free of %u", o.x.bx, o.x.dx);
    check("42h", o.h.ah == 0 && o.x.bx >= 4 && o.x.dx >= o.x.bx, what);
    check("43h", ems(0x4300, 4, 0, &o) == 0, "4 pages allocated");
    handle = o.x.dx;
    for (k = 0, ok = 1; k < 4; k++)
        ok &= ems(0x4400 | k, k, handle, &o) == 0;
    check("44h", ok, "logical 0-3 at physical 0-3");
    for (k = 0; k < 4; k++) {
        unsigned char __far *p = (unsigned char __far *)MK_FP(frame + 0x400 * k, 0);
        for (i = 0; i < 16384; i++)
            p[(unsigned)i] = (unsigned char)(k * 50 + i % 251);
    }
    for (k = 0, ok = 1; k < 4; k++) {
        unsigned char __far *p = (unsigned char __far *)MK_FP(frame + 0x400 * k, 0);
        for (i = 0; i < 16384; i += 97)
            ok &= p[(unsigned)i] == (unsigned char)(k * 50 + i % 251);
    }
    check("written", ok, "each physical page its own pattern");
    ems(0x4400, 3, handle, &o);
    {
        unsigned char __far *p = (unsigned char __far *)MK_FP(frame, 0);
        for (i = 0, ok = o.h.ah == 0; i < 16384; i += 97)
            ok &= p[(unsigned)i] == (unsigned char)(3 * 50 + i % 251);
    }
    check("remap", ok, "logical 3 at physical 0: page 3's bytes there");
    for (i = 0; i < sizeof conv; i++)
        conv[i] = (unsigned char)(i * 3 + 7);
    segread(&s);
    mv.len = sizeof conv, mv.st = 0, mv.sh = 0, mv.so = FP_OFF((void __far *)conv), mv.ss = FP_SEG((void __far *)conv);
    mv.dt = 1, mv.dh = handle, mv.dofs = 100, mv.ds = 2;
    {
        union REGS r;
        r.x.ax = 0x5700, r.x.si = FP_OFF((void __far *)&mv);
        s.ds = FP_SEG((void __far *)&mv);
        int86x(0x67, &r, &o, &s);
    }
    ok = o.h.ah == 0;
    ems(0x4401, 2, handle, &o);
    {
        unsigned char __far *p = (unsigned char __far *)MK_FP(frame + 0x400, 100);
        for (i = 0; i < sizeof conv; i++)
            ok &= p[(unsigned)i] == conv[i];
    }
    check("57h", ok, "conventional memory moved into page 2, read through the frame");
    check("45h", ems(0x4500, 0, handle, &o) == 0, "the pages given back");
    ems(0x4200, 0, 0, &o);
    sprintf(what, "%u free again", o.x.bx);
    check("42h-again", o.x.bx == free0, what);
    printf("EMSTEST done failures=%d\n", fails);
    return fails;
}
