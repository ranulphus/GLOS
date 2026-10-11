/* SHOTTEST (GLOS M5 A2): a picture the host can work out for itself, in one
 * video mode, for glos shot to take. Built twice: by Open Watcom for real
 * mode (SHOTTEST.EXE) and by DJGPP as a DPMI client (SHOTDJ.EXE), which
 * reaches the BIOS and the screen through the host.
 *
 *     SHOTTEST mode
 *
 * It sets the mode, draws, and says on COM1 (not the screen, which is the
 * picture)
 *     HX-SHOT mode=NAME w=W h=H kind=K [bpp=N r=S/P g=S/P b=S/P] ready
 * or
 *     HX-SHOT mode=NAME skip why=...
 * when the card's BIOS hasn't the mode; then it waits for a key, a minute at
 * most, sets mode 3 again and ends.
 *
 * The pictures (tests/loopa/jobs.py computes the same):
 *   pat(x, y) = (x + 3y + 5 * ((x >> 4) ^ (y >> 3))) & 255
 *   kind=pal256  the pixel is pat; DAC entry i is (i, 5i + 9, 255 - i), of
 *                which the DAC keeps the low 6 bits of the first two and
 *                the top 6 of the third ("wide": all 8 bits of each)
 *   kind=pal16   the pixel is pat & 15 (& 3, or bit 1 of pat, with fewer
 *                colours); the attribute controller sends value v to DAC
 *                entry 50h + 3v
 *   kind=text    cell (x, y) has attribute a = (7x + 13y) & 255 and is a
 *                full block (its foreground, a & 15) if x + y is odd, else
 *                a space (its background: a >> 4, or (a >> 4) & 7 where
 *                attribute bit 7 blinks); value v is DAC entry 50h + 3v
 *   kind=rgb     red x & 255, green y & 255, blue (x ^ y) & 255, each
 *                cut to the mode's bits
 * Modes: text textb text50 cga4 cga2 ega0d ega0e ega10 vga12 vga13
 *   vga13c (as vga13, then five seconds with its interrupts off and no call
 *   to anyone, timed by the PIT's counter; "HX-SHOT mode=vga13c resumed"
 *   when they are on again)
 *   x (320x240 unchained)  xpage (its second page shown; the first holds
 *   the inverse)  xsplit (as xpage, and from row 200 the first page again,
 *   by the line compare)
 *   vNNN (VBE mode NNNh through its window)  vNNNl (set with the linear
 *   framebuffer and drawn into that: by INT 15h 87h in real mode, through
 *   a mapping of it as a DPMI client)  vNNNs (the display started 64
 *   rows down)  vNNNd (the DAC 8 bits wide)  vNNNp (scan lines made 1024
 *   pixels long with 4F06h)  vNNNz (SHOTDJ on a Matrox card: linear, and
 *   zoomed by two as MGA-Glide's HAL does it, XZOOMCTRL and the CRTC's
 *   maximum scan line: the top left quarter is what shows) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __DJGPP__
#include <dos.h>
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <sys/farptr.h>
#include <sys/movedata.h>
#define pout(p, v) outportb(p, v)
#define pin(p) inportb(p)
#else
#include <bios.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>
#define pout(p, v) outp(p, v)
#define pin(p) inp(p)
#endif

struct R { unsigned short ax, bx, cx, dx; };
static unsigned char info[256];                 /* 4F01h's block */
static unsigned char rowbuf[4416];
static const char *name;

#ifdef __DJGPP__
static void vint(struct R *r)                   /* INT 10h, ES:DI at a buffer for the BIOS's answer */
{
    __dpmi_regs d;
    int describe = r->ax == 0x4F01;
    memset(&d, 0, sizeof d);
    d.x.ax = r->ax, d.x.bx = r->bx, d.x.cx = r->cx, d.x.dx = r->dx;
    d.x.es = __tb >> 4, d.x.di = __tb & 15;
    __dpmi_int(0x10, &d);
    r->ax = d.x.ax, r->bx = d.x.bx, r->cx = d.x.cx, r->dx = d.x.dx;
    if (describe)
        dosmemget(__tb, 256, info);
}
static void vput(unsigned long lin, const unsigned char *p, unsigned n) { dosmemput(p, n, lin); }
static unsigned long ticks(void)
{
    unsigned long t;
    dosmemget(0x46C, 4, &t);
    return t;
}
static int key(int take)                        /* a key waits; take: read it */
{
    __dpmi_regs d;
    memset(&d, 0, sizeof d);
    d.h.ah = 1;
    __dpmi_int(0x16, &d);
    if (d.x.flags & 0x40)
        return 0;
    if (take) {
        d.h.ah = 0;
        __dpmi_int(0x16, &d);
    }
    return 1;
}
#else
static void vint(struct R *r)
{
    union REGS in, out;
    struct SREGS s;
    segread(&s);
    in.x.ax = r->ax, in.x.bx = r->bx, in.x.cx = r->cx, in.x.dx = r->dx;
    in.x.di = FP_OFF((void far *)info);
    s.es = FP_SEG((void far *)info);
    int86x(0x10, &in, &out, &s);
    r->ax = out.x.ax, r->bx = out.x.bx, r->cx = out.x.cx, r->dx = out.x.dx;
}
static void vput(unsigned long lin, const unsigned char *p, unsigned n)
{
    _fmemcpy(MK_FP((unsigned)(lin >> 4), (unsigned)(lin & 15)), p, n);
}
static unsigned long ticks(void) { return *(unsigned long far *)MK_FP(0x40, 0x6C); }
static int key(int take)
{
    if (!_bios_keybrd(_KEYBRD_READY))
        return 0;
    if (take)
        _bios_keybrd(_KEYBRD_READ);
    return 1;
}
#endif

static void ser(const char *s)
{
    for (; *s; s++) {
        long n = 0;
        while (!(pin(0x3FD) & 0x20) && ++n < 100000L) ;
        pout(0x3F8, *s);
    }
}

static void mode(unsigned m)
{
    struct R r;
    memset(&r, 0, sizeof r);
    r.ax = m;
    vint(&r);
}

static void skip(const char *why)
{
    char line[100];
    mode(3);
    sprintf(line, "HX-SHOT mode=%s skip why=%s\r\n", name, why);
    ser(line);
    exit(3);
}

static void ready(unsigned w, unsigned h, const char *rest)
{
    char line[160];
    unsigned long t0 = ticks();
    sprintf(line, "HX-SHOT mode=%s w=%u h=%u %s ready\r\n", name, w, h, rest);
    ser(line);
    while (ticks() - t0 < 1092 && !key(0)) ;    /* a minute */
    mode(3);
}

static unsigned pat(unsigned x, unsigned y) { return (x + 3 * y + 5 * ((x >> 4) ^ (y >> 3))) & 255; }

static void dac_ramp(int wide)
{
    unsigned i;
    for (i = 0; i < 256; i++) {
        pout(0x3C8, i);
        pout(0x3C9, wide ? i : i & 63);
        pout(0x3C9, wide ? (5 * i + 9) & 255 : (5 * i + 9) & 63);
        pout(0x3C9, wide ? 255 - i : (255 - i) >> 2);
    }
}

/* Palette register v: 10h + 3v; the colour select adds 40h. */
static void attr_ramp(void)
{
    unsigned i;
    pin(0x3DA);
    for (i = 0; i < 16; i++) {
        pout(0x3C0, i);
        pout(0x3C0, 0x10 + 3 * i);
    }
    pout(0x3C0, 0x14);
    pout(0x3C0, 0x04);
    pout(0x3C0, 0x20);
}

/* ---- the VGA's modes */

static void text(int blink, int fifty)
{
    unsigned x, y, rows = fifty ? 50 : 25;
    struct R r;
    mode(3);
    memset(&r, 0, sizeof r);
    if (fifty) {
        r.ax = 0x1112;                          /* the 8x8 font: 50 rows */
        vint(&r);
    }
    r.ax = 0x1003;
    r.bx = blink;
    vint(&r);
    r.ax = 0x0100;                              /* no cursor */
    r.cx = 0x2000;
    vint(&r);
    attr_ramp();
    dac_ramp(0);
    for (y = 0; y < rows; y++) {
        for (x = 0; x < 80; x++) {
            rowbuf[x * 2] = ((x + y) & 1) ? 0xDB : 0x20;
            rowbuf[x * 2 + 1] = (unsigned char)(7 * x + 13 * y);
        }
        vput(0xB8000L + y * 160u, rowbuf, 160);
    }
    ready(640, 400, fifty ? "kind=text blink=0 rows=50" : blink ? "kind=text blink=1 rows=25" : "kind=text blink=0 rows=25");
}

static void cga(unsigned m)
{
    unsigned x, y, w = m == 6 ? 640 : 320;
    mode(m);
    attr_ramp();
    dac_ramp(0);
    for (y = 0; y < 200; y++) {
        memset(rowbuf, 0, 80);
        for (x = 0; x < w; x++)
            if (m == 6)
                rowbuf[x >> 3] |= (unsigned char)(((pat(x, y) >> 1) & 1) << (7 - (x & 7)));
            else
                rowbuf[x >> 2] |= (unsigned char)((pat(x, y) & 3) << (6 - 2 * (x & 3)));
        vput(0xB8000L + (y & 1) * 0x2000u + (y >> 1) * 80u, rowbuf, 80);
    }
    ready(w, 200, m == 6 ? "kind=pal16 colours=2" : "kind=pal16 colours=4");
}

static void planar(unsigned m, unsigned w, unsigned h)
{
    unsigned x, y, p, bytes = w / 8;
    mode(m);
    attr_ramp();
    dac_ramp(0);
    for (p = 0; p < 4; p++) {
        pout(0x3C4, 2);
        pout(0x3C5, 1 << p);
        for (y = 0; y < h; y++) {
            memset(rowbuf, 0, bytes);
            for (x = 0; x < w; x++)
                rowbuf[x >> 3] |= (unsigned char)(((pat(x, y) >> p) & 1) << (7 - (x & 7)));
            vput(0xA0000L + (unsigned long)y * bytes, rowbuf, bytes);
        }
    }
    pout(0x3C5, 0x0F);
    ready(w, h, "kind=pal16 colours=16");
}

/* Five seconds by the PIT's channel 0 (it wraps 18.2 times a second),
   interrupts off. */
static void deaf(void)
{
    unsigned wraps = 0, last = 0xFFFF, now;
#ifdef __DJGPP__
    disable();
#else
    _disable();
#endif
    while (wraps < 91) {
        pout(0x43, 0x00);
        now = pin(0x40);
        now |= (unsigned)pin(0x40) << 8;
        if (now > last)
            wraps++;
        last = now;
    }
#ifdef __DJGPP__
    enable();
#else
    _enable();
#endif
}

static void vga13(int cli)
{
    unsigned x, y;
    mode(0x13);
    dac_ramp(0);
    for (y = 0; y < 200; y++) {
        for (x = 0; x < 320; x++)
            rowbuf[x] = (unsigned char)pat(x, y);
        vput(0xA0000L + y * 320u, rowbuf, 320);
    }
    if (cli) {
        ser("HX-SHOT mode=vga13c w=320 h=200 kind=pal256 deaf\r\n");
        deaf();
        ser("HX-SHOT mode=vga13c resumed\r\n");
        mode(3);
        return;
    }
    ready(320, 200, "kind=pal256");
}

/* 320x240 unchained, from mode 13h. One page is 19,200 bytes of each plane. */
static void modex(int page, int split)
{
    static const unsigned short crtc[] = { 0x0D06, 0x3E07, 0x4109, 0xEA10, 0xAC11, 0xDF12, 0x0014, 0xE715, 0x0616,
                                           0xE317 };
    unsigned x, y, p, i, pg;
    mode(0x13);
    pout(0x3C4, 4), pout(0x3C5, 0x06);          /* no chain-4 */
    pout(0x3C4, 0), pout(0x3C5, 0x01);
    pout(0x3C2, 0xE3);                          /* 480 lines at 25 MHz */
    pout(0x3C4, 0), pout(0x3C5, 0x03);
    pout(0x3D4, 0x11), pout(0x3D5, pin(0x3D5) & 0x7F);
    for (i = 0; i < sizeof crtc / sizeof crtc[0]; i++)
        pout(0x3D4, crtc[i] & 0xFF), pout(0x3D5, crtc[i] >> 8);
    dac_ramp(0);
    for (pg = 0; pg < 2; pg++)                  /* the page shown: the picture; the other: its inverse */
        for (p = 0; p < 4; p++) {
            pout(0x3C4, 2), pout(0x3C5, 1 << p);
            for (y = 0; y < 240; y++) {
                for (x = p; x < 320; x += 4)
                    rowbuf[x >> 2] = (unsigned char)(pg == (unsigned)page ? pat(x, y) : pat(x, y) ^ 0xFF);
                vput(0xA0000L + pg * 19200u + y * 80u, rowbuf, 80);
            }
        }
    if (page) {
        pout(0x3D4, 0x0C), pout(0x3D5, 19200 >> 8);
        pout(0x3D4, 0x0D), pout(0x3D5, 19200 & 0xFF);
    }
    if (split) {                                /* line compare 399: picture rows from 200 on are page 0's */
        pout(0x3D4, 0x18), pout(0x3D5, 0x8F);
        pout(0x3D4, 0x07), pout(0x3D5, pin(0x3D5) | 0x10);
        pout(0x3D4, 0x09), pout(0x3D5, pin(0x3D5) & ~0x40);
    }
    ready(320, 240, split ? "kind=pal256 split=200" : "kind=pal256");
}

/* ---- VBE */

static unsigned w16(unsigned o) { return info[o] | (info[o + 1] << 8); }

static unsigned bank_now = 0xFFFF;
static unsigned long gran, wsize, wbase;

static void vbe_put(unsigned long off, const unsigned char *p, unsigned n)
{
    while (n) {
        unsigned pos = (unsigned)(off / gran), k;
        unsigned long in = off - (unsigned long)pos * gran;
        struct R r;
        k = wsize - in < n ? (unsigned)(wsize - in) : n;
        if (pos != bank_now) {
            memset(&r, 0, sizeof r);
            r.ax = 0x4F05;
            r.dx = pos;
            vint(&r);
            bank_now = pos;
        }
        vput(wbase + in, p, k);
        off += k;
        p += k;
        n -= k;
    }
}

/* Into the linear framebuffer at physical address `phys`. */
#ifdef __DJGPP__
static int lfb_sel;
static unsigned long lfb_at;

static void lfb_put(unsigned long phys, const unsigned char *p, unsigned n)
{
    if (!lfb_sel) {                             /* 8 MB from the first address asked for */
        __dpmi_meminfo mi;
        mi.address = phys;
        mi.size = 0x800000;
        if (__dpmi_physical_address_mapping(&mi) != 0 || (lfb_sel = __dpmi_allocate_ldt_descriptors(1)) < 0)
            return;
        __dpmi_set_segment_base_address(lfb_sel, mi.address);
        __dpmi_set_segment_limit(lfb_sel, 0x7FFFFF);
        lfb_at = phys;
    }
    movedata(_my_ds(), (unsigned)p, lfb_sel, phys - lfb_at, n);
}
#else
static void lfb_put(unsigned long phys, const unsigned char *p, unsigned n)
{
    static unsigned char gdt[48];               /* INT 15h 87h's: source at 10h, destination at 18h */
    unsigned long src = ((unsigned long)FP_SEG((void far *)p) << 4) + FP_OFF((void far *)p);
    union REGS r;
    struct SREGS s;
    memset(gdt, 0, sizeof gdt);
    gdt[0x10] = gdt[0x11] = gdt[0x18] = gdt[0x19] = 0xFF;
    gdt[0x15] = gdt[0x1D] = 0x93;
    gdt[0x12] = (unsigned char)src, gdt[0x13] = (unsigned char)(src >> 8), gdt[0x14] = (unsigned char)(src >> 16);
    gdt[0x17] = (unsigned char)(src >> 24);
    gdt[0x1A] = (unsigned char)phys, gdt[0x1B] = (unsigned char)(phys >> 8);
    gdt[0x1C] = (unsigned char)(phys >> 16), gdt[0x1F] = (unsigned char)(phys >> 24);
    segread(&s);
    s.es = FP_SEG((void far *)gdt);
    r.x.si = FP_OFF((void far *)gdt);
    r.x.cx = (n + 1) / 2;
    r.h.ah = 0x87;
    int86x(0x15, &r, &r, &s);
}
#endif

static void field(unsigned v, unsigned size, unsigned pos, unsigned long *px)
{
    *px |= (unsigned long)(v >> (8 - size)) << pos;
}

/* A Matrox card's zoom by two, the way MGA-Glide's HAL sets it: 0 without
   such a card (or in real mode, which can't reach its registers). */
static int mga_zoom2(void)
{
#ifdef __DJGPP__
    unsigned long base = 0, id, bar;
    unsigned bus, dev, i;
    __dpmi_meminfo mi;
    int sel;
    for (bus = 0; bus < 4 && !base; bus++)
        for (dev = 0; dev < 32 && !base; dev++) {
            outportl(0xCF8, 0x80000000UL | ((unsigned long)bus << 16) | (dev << 11));
            id = inportl(0xCFC);
            outportl(0xCF8, 0x80000000UL | ((unsigned long)bus << 16) | (dev << 11) | 8);
            if ((id & 0xFFFF) != 0x102B || (inportl(0xCFC) >> 24) != 3)
                continue;
            for (i = 0; i < 2; i++) {           /* its registers: the range that isn't prefetchable */
                outportl(0xCF8, 0x80000000UL | ((unsigned long)bus << 16) | (dev << 11) | (0x10 + 4 * i));
                bar = inportl(0xCFC);
                if (!(bar & 9) && (bar & ~0xFUL))
                    base = bar & ~0xFUL;
            }
        }
    if (!base)
        return 0;
    mi.address = base;
    mi.size = 0x4000;
    if (__dpmi_physical_address_mapping(&mi) != 0 || (sel = __dpmi_allocate_ldt_descriptors(1)) < 0)
        return 0;
    __dpmi_set_segment_base_address(sel, mi.address);
    __dpmi_set_segment_limit(sel, 0x3FFF);
    pout(0x3D4, 0x09);
    pout(0x3D5, (pin(0x3D5) & 0xE0) | 1);       /* each line twice */
    _farpokeb(sel, 0x3C00, 0x38);               /* XZOOMCTRL: each pixel twice */
    _farpokeb(sel, 0x3C0A, 1);
    return 1;
#else
    return 0;
#endif
}

static void vbe(unsigned m, int flag)
{
    int lfb = flag == 'l' || flag == 'z', start = flag == 's', wide = flag == 'd';
    unsigned w, h, bpp, model, pitch, bytes, x, y, p, y0 = start ? 64 : 0, planes = 1;
    unsigned rs, rp, gs, gp, bs, bp;
    unsigned long phys;
    char rest[80];
    struct R r;

    memset(&r, 0, sizeof r);
    memset(info, 0, sizeof info);
    r.ax = 0x4F01;
    r.cx = m;
    vint(&r);
    if (r.ax != 0x004F || !(info[0] & 1))
        skip("no-mode");
    w = w16(0x12), h = w16(0x14), bpp = info[0x19], model = info[0x1B], pitch = w16(0x10);
    gran = (unsigned long)w16(4) << 10;
    wsize = (unsigned long)w16(6) << 10;
    wbase = (unsigned long)w16(8) << 4;
    rs = info[0x1F], rp = info[0x20], gs = info[0x21], gp = info[0x22], bs = info[0x23], bp = info[0x24];
    phys = w16(0x28) | ((unsigned long)w16(0x2A) << 16);
    if (!gran || !wsize || !wbase || w > 1100)
        skip("no-window");
    if (lfb && (!(info[0] & 0x80) || !phys || model == 3))
        skip("no-lfb");
    bytes = (bpp + 7) / 8;
    memset(&r, 0, sizeof r);
    r.ax = 0x4F02;
    r.bx = lfb ? m | 0x4000 : m;
    vint(&r);
    if (r.ax != 0x004F)
        skip("no-set");
    if (lfb) {                                  /* the scan line as it is now (VBE 3's linear modes) */
        memset(&r, 0, sizeof r);
        r.ax = 0x4F06;
        r.bx = 1;
        vint(&r);
        if (r.ax == 0x004F && r.bx >= w * bytes)
            pitch = r.bx;
    }
    if (flag == 'p') {
        memset(&r, 0, sizeof r);
        r.ax = 0x4F06;
        r.cx = 1024;
        vint(&r);
        if (r.ax != 0x004F || r.bx < w * bytes)
            skip("no-pitch");
        pitch = r.bx;
    }
    if (model == 3) {
        planes = 4;
        attr_ramp();
    } else if (bpp > 8 && (!rs || !gs || !bs)) {
        skip("no-masks");
    }
    for (p = 0; p < planes; p++) {
        if (planes > 1)
            pout(0x3C4, 2), pout(0x3C5, 1 << p);
        for (y = 0; y < h; y++) {
            if (planes > 1) {
                memset(rowbuf, 0, w / 8);
                for (x = 0; x < w; x++)
                    rowbuf[x >> 3] |= (unsigned char)(((pat(x, y) >> p) & 1) << (7 - (x & 7)));
            } else {
                for (x = 0; x < w; x++) {
                    unsigned long px = 0;
                    unsigned i;
                    if (bpp == 8) {
                        rowbuf[x] = (unsigned char)pat(x, y);
                        continue;
                    }
                    field(x & 255, rs, rp, &px);
                    field(y & 255, gs, gp, &px);
                    field((x ^ y) & 255, bs, bp, &px);
                    for (i = 0; i < bytes; i++)
                        rowbuf[x * bytes + i] = (unsigned char)(px >> (8 * i));
                }
            }
            if (lfb)
                lfb_put(phys + (unsigned long)(y0 + y) * pitch, rowbuf, w * bytes);
            else
                vbe_put((unsigned long)(y0 + y) * pitch, rowbuf, planes > 1 ? w / 8 : w * bytes);
        }
    }
    if (planes > 1)
        pout(0x3C4, 2), pout(0x3C5, 0x0F);
    if (wide) {
        memset(&r, 0, sizeof r);
        r.ax = 0x4F08;
        r.bx = 0x0800;
        vint(&r);
        if (r.ax != 0x004F || (r.bx >> 8) < 8)
            skip("no-wide-dac");
    }
    if (bpp <= 8)
        dac_ramp(wide);
    if (start) {
        memset(&r, 0, sizeof r);
        r.ax = 0x4F07;
        r.dx = y0;
        vint(&r);
        if (r.ax != 0x004F)
            skip("no-start");
    }
    if (flag == 'z') {
        if (!mga_zoom2())
            skip("no-zoom");
        w /= 2;
        h /= 2;
    }
    if (model == 3)
        sprintf(rest, "kind=pal16 colours=16");
    else if (bpp == 8)
        sprintf(rest, "kind=pal256%s", wide ? " wide=1" : "");
    else
        sprintf(rest, "kind=rgb bpp=%u r=%u/%u g=%u/%u b=%u/%u", bpp, rs, rp, gs, gp, bs, bp);
    ready(w, h, rest);
}

int main(int argc, char **argv)
{
    const char *m = argc > 1 ? argv[1] : "";
    name = m;
    while (key(1)) ;                            /* (keys left from the boot) */
    if (!strcmp(m, "text")) text(0, 0);
    else if (!strcmp(m, "textb")) text(1, 0);
    else if (!strcmp(m, "text50")) text(0, 1);
    else if (!strcmp(m, "cga4")) cga(4);
    else if (!strcmp(m, "cga2")) cga(6);
    else if (!strcmp(m, "ega0d")) planar(0x0D, 320, 200);
    else if (!strcmp(m, "ega0e")) planar(0x0E, 640, 200);
    else if (!strcmp(m, "ega10")) planar(0x10, 640, 350);
    else if (!strcmp(m, "vga12")) planar(0x12, 640, 480);
    else if (!strcmp(m, "vga13")) vga13(0);
    else if (!strcmp(m, "vga13c")) vga13(1);
    else if (!strcmp(m, "x")) modex(0, 0);
    else if (!strcmp(m, "xpage")) modex(1, 0);
    else if (!strcmp(m, "xsplit")) modex(1, 1);
    else if (m[0] == 'v' && strlen(m) >= 4) {
        char num[4];
        unsigned n;
        const char *f = m + 4;
        memcpy(num, m + 1, 3);
        num[3] = 0;
        n = (unsigned)strtoul(num, 0, 16);
        vbe(n, *f);
    } else {
        ser("HX-SHOT usage\r\n");
        return 2;
    }
    return 0;
}
