/* glos shot (PRD §7.5; M3 item 10, M5 A2): a picture of the screen in
 * whatever mode the program has set.
 *
 * The capture runs on the VM thread, as work posted to it (vm_post): the
 * program is stopped between two instructions where it would take an
 * interrupt, so what the capture does to the card is what an interrupt
 * handler might do. It copies the visible frame into kernel frames, with
 * the state that turns it into colours; the ssh thread then makes the PNG
 * from the copy, a piece at a time, while the program runs on.
 *
 * What mode it is:
 *   - a VBE mode if INT 10h 4F02h set one last (shot_int10 sees every INT
 *     10h; v86.c makes the 4F02h itself so that its answer is seen). The
 *     BIOS describes the mode (4F01h) and says how long a scan line is
 *     now (4F06h), where the display starts (4F07h) and how wide the DAC
 *     is (4F08h); the frame is read through
 *     the linear framebuffer, or bank by bank (4F05h);
 *   - otherwise what the VGA registers say: text; the CGA modes; 16-colour
 *     planar modes; chained 256 colours (13h); unchained ("mode X"), with
 *     the size from the CRTC, the split screen from its line compare.
 *
 * What it changes and puts back: the index registers (3C4h, 3CEh, 3D4h,
 * the attribute controller's, whose flip-flop is left at "index"), and
 * for the planes of an unchained mode the read map, the modes and the
 * memory map. Reading a plane loads the VGA's latches, and a program
 * stopped between its read and its write of a pixel needs them: they are
 * written first to plane offset FFFFh (write mode 1; the four bytes there
 * are lost, a VGA has nowhere else) and read back at the end. The DAC is
 * read as it is at that moment, not from a shadow: trapping 3C8h and 3C9h
 * would cost a trap per byte of every palette fade. The DAC's index comes
 * back, the place within an entry's three bytes does not: a program
 * stopped there, interrupts on, gets that entry wrong.
 *
 * Text is drawn with the video BIOS's ROM fonts (found by GLOS.EXE through
 * INT 10h 1130h), 8 pixels to a cell, without the cursor; a font the
 * program loaded is not seen. In direct-colour modes the picture has the
 * framebuffer's values, not what a gamma ramp in the DAC makes of them. */
#include <string.h>

#include "glos/bootinfo.h"
#include "dpmi.h"
#include "io.h"
#include "display.h"
#include "kprintf.h"
#include "mgadisp.h"
#include "mm.h"
#include "pci.h"
#include "png.h"
#include "sched.h"
#include "shot.h"
#include "timer.h"
#include "vm.h"

#define PATIENCE  256u                          /* ticks for the program to take interrupts */
#define BIOS_WAIT 2048u                         /* ticks for the video BIOS to end what it is at: a mode set takes
                                                   a Matrox BIOS more than a quarter of a second, and its
                                                   registers halfway are no mode at all */
#define TIMEOUT   3072u                         /* ticks until glos shot gives up */
#define SHOT_MAX  (16u << 20)                   /* bytes of a frame */
#define SPARE     256u                          /* frames left to the system */
#define BREATH    0x4000u                       /* bytes copied between two openings for interrupts */
#define GROUP     16000u                        /* scanline bytes in a piece, about */

enum { K_TEXT, K_CGA, K_PLANAR, K_MODEX, K_PACKED, K_DIRECT };
enum { P_NEW, P_POSTED, P_HEAD, P_ROWS, P_FAILED };

struct shot {
    struct vm_work work;                        /* (first: capture() gets it) */
    u8 raw, phase;
    int got;                                    /* the capture's result: 1, or -1 with why */
    const char *why;
    u32 t0;
    /* what the capture found */
    u8 kind;
    u8 bits;                                    /* K_CGA: 1 or 2 a pixel; K_DIRECT: 15 to 32 */
    u8 ch, fh;                                  /* K_TEXT: the cell's height, the font's */
    u8 blink;                                   /* K_TEXT: attribute bit 7 blinks */
    u8 planes_on;                               /* K_PLANAR: the planes the attribute controller shows */
    u8 rpos, rsize, gpos, gsize, bpos, bsize;   /* K_DIRECT */
    u32 w, h;                                   /* pixels */
    u32 rowlen;                                 /* a row's bytes in the copy (one plane's, K_PLANAR and K_MODEX) */
    u8 pal[256][3];                             /* RGB by pixel value */
    /* the card's cursor, where the monitor shows it over the picture (M7 step F) */
    u8 cursor;
    s32 cx, cy;
    const u8 *cand, *cxor;
    u8 black, white;                            /* K_PACKED: colours 0 and 1 as the palette has them nearest */
    /* the copy */
    u32 *frames, nframes, len, fill, since;
    /* the reader */
    struct png png;
    u32 y, group;
    u8 *src, *pix, *lines;
};

/* The VBE mode set last, and the BIOS's description of it. */
static struct {
    u16 mode;                                   /* 4F02h's BX; 0 after a plain mode set */
    u8 known;
    u8 info[0x40];                              /* the start of 4F01h's block */
} vbe;

/* Mode sets under way: INT 10h calls GLOS makes for a program (every one of
   a DPMI client's; a V86 program's mode sets), counted from their start to
   their return. The card's registers halfway through one are no mode at
   all, wherever the CPU is just then (an interrupt handler on top of the
   BIOS as likely as the BIOS itself), so a capture waits for the count. */
static u8 int10_busy;

void shot_int10_begin(void) { int10_busy++; }

void shot_session_end(void) { int10_busy = 0; }     /* (a program killed inside one) */

void shot_int10(u32 ax, u32 bx, u32 ax_out)
{
    if (int10_busy)
        int10_busy--;
    ax &= 0xFFFF;
    if (!(ax >> 8)) {                           /* AH 00h: a plain mode */
        vbe.mode = 0;
    } else if (ax == 0x4F02 && (ax_out & 0xFFFF) == 0x004F) {
        vbe.mode = (bx & 0x1FF) > 0x13 ? (u16)bx : 0;
        vbe.known = 0;
    }
}

/* ---- the copy */

/* Interrupts in for a moment (the tick, the network; the program's wait
   for its return), without a switch to another thread. */
static void breathe(void)
{
    preempt_disable();
    sti();
    __asm__ volatile("nop");
    cli();
    preempt_enable();
}

static int cap_alloc(struct shot *s, u32 len)
{
    u32 n = (len + 0xFFF) >> 12, i;
    s->why = "no memory";
    if (!len || len > SHOT_MAX || pmm_free_frames() < n + SPARE || !(s->frames = kmalloc(n * 4)))
        return -1;
    for (i = 0; i < n; i++)
        if (!(s->frames[i] = pmm_alloc())) {
            while (i--)
                pmm_free(s->frames[i]);
            kfree(s->frames);
            s->frames = 0;
            return -1;
        }
    s->nframes = n;
    s->len = len;
    s->fill = 0;
    return 0;
}

/* n bytes at p onto the end of the copy. */
static void put(struct shot *s, const u8 *p, u32 n)
{
    while (n && s->fill < s->len) {
        u32 in = s->fill & 0xFFF, k = 0x1000 - in;
        u8 *d;
        if (k > n)
            k = n;
        if (k > s->len - s->fill)
            k = s->len - s->fill;
        d = kmap(s->frames[s->fill >> 12]);
        memcpy(d + in, p, k);
        kunmap(d);
        s->fill += k;
        p += k;
        n -= k;
        if ((s->since += k) >= BREATH) {
            s->since = 0;
            breathe();
        }
    }
}

/* n bytes of physical memory (a linear framebuffer). */
static void put_phys(struct shot *s, u32 phys, u32 n)
{
    while (n) {
        u32 k = 0x1000 - (phys & 0xFFF);
        u8 *p;
        if (k > n)
            k = n;
        p = kmap(phys);
        put(s, p, k);
        kunmap(p);
        phys += k;
        n -= k;
    }
}

/* n bytes from offset off of a window of `size` bytes (a power of two) at
   base, wrapping at its end. */
static void put_wrap(struct shot *s, u32 base, u32 off, u32 n, u32 size)
{
    while (n) {
        u32 k;
        off &= size - 1;
        k = size - off;
        if (k > n)
            k = n;
        put(s, vm_ptr(base + off), k);
        off += k;
        n -= k;
    }
}

/* The reader's side: n bytes of the copy at off. A window is the kernel's
   for a moment only (kmap), so interrupts stay off across each. */
static void get(const struct shot *s, u32 off, u8 *dst, u32 n)
{
    while (n && off < s->len) {
        u32 in = off & 0xFFF, k = 0x1000 - in, f;
        u8 *p;
        if (k > n)
            k = n;
        f = irq_save();
        p = kmap(s->frames[off >> 12]);
        memcpy(dst, p + in, k);
        kunmap(p);
        irq_restore(f);
        dst += k;
        off += k;
        n -= k;
    }
}

/* ---- the VGA's registers */

struct vga {
    u8 c[0x19];                                 /* the CRTC */
    u8 sq1, sq2, sq4, gc4, gc5, gc6;
    u8 at[0x15];                                /* the attribute controller */
};

static u8 rdx(u16 port, u8 i)
{
    outb(port, i);
    return inb((u16)(port + 1));
}

static void wrx(u16 port, u8 i, u8 v)
{
    outb(port, i);
    outb((u16)(port + 1), v);
}

static void vga_read(struct vga *v)
{
    u16 crt = (inb(0x3CC) & 1) ? 0x3D4 : 0x3B4, st = (u16)(crt + 6);
    u8 ci = inb(crt), si = inb(0x3C4), gi = inb(0x3CE), ai = inb(0x3C0), i;

    for (i = 0; i < sizeof v->c; i++)
        v->c[i] = rdx(crt, i);
    v->sq1 = rdx(0x3C4, 1);
    v->sq2 = rdx(0x3C4, 2);
    v->sq4 = rdx(0x3C4, 4);
    v->gc4 = rdx(0x3CE, 4);
    v->gc5 = rdx(0x3CE, 5);
    v->gc6 = rdx(0x3CE, 6);
    for (i = 0; i < sizeof v->at; i++) {        /* the palette can be read only with the screen off (bit 5): */
        inb(st);                                /* for these sixteen reads */
        outb(0x3C0, i < 0x10 ? i : (u8)(i | 0x20));
        v->at[i] = inb(0x3C1);
    }
    inb(st);
    outb(0x3C0, ai);
    inb(st);                                    /* the flip-flop: at "index" */
    outb(crt, ci);
    outb(0x3C4, si);
    outb(0x3CE, gi);
}

/* The DAC entry a 4-bit pixel value comes to, through the attribute
   controller's palette and its colour select. */
static u8 attr_map(const struct vga *v, u32 i)
{
    u8 a = v->at[i & 15] & 0x3F;
    if (v->at[0x10] & 0x80)
        a = (u8)((a & 0x0F) | ((v->at[0x14] & 3) << 4));
    return (u8)(a | ((v->at[0x14] & 0x0C) << 4));
}

/* The palette: the DAC's 256 entries (`wide`: 8 bits a colour, else 6), for
   16 colours through the attribute controller. */
static void palette(struct shot *s, const struct vga *v, int sixteen, int wide)
{
    u8 st = inb(0x3C7) & 3, idx = inb(0x3C8), mask = inb(0x3C6), dac[256][3];
    u32 i, c;

    outb(0x3C7, 0);
    for (i = 0; i < 256; i++)
        for (c = 0; c < 3; c++) {
            u8 x = inb(0x3C9);
            dac[i][c] = wide ? x : (u8)(((x & 0x3F) << 2) | ((x & 0x3F) >> 4));
        }
    outb(st == 3 ? 0x3C7 : 0x3C8, idx);         /* the index and its direction, as they were */
    for (i = 0; i < 256; i++)
        memcpy(s->pal[i], dac[(sixteen ? attr_map(v, i) : i) & mask], 3);
}

/* The planes of an unchained mode one at a time at A0000h: the latches
   saved first. planes_end() undoes it. */
static void planes_begin(const struct vga *v)
{
    wrx(0x3C4, 4, (u8)((v->sq4 & ~0x08) | 0x06));       /* no chain-4, no odd/even, all of the memory */
    wrx(0x3CE, 6, (u8)((v->gc6 & 0x01) | 0x04));        /* A0000h, 64 KB, no odd/even chain */
    wrx(0x3C4, 2, 0x0F);
    wrx(0x3CE, 5, (u8)((v->gc5 & 0x60) | 0x01));        /* write mode 1: the latches, */
    *(volatile u8 *)vm_ptr(0xAFFFF) = 0;                /* into every plane's last byte */
    wrx(0x3C4, 2, v->sq2);
    wrx(0x3CE, 5, (u8)(v->gc5 & 0x60));                 /* read mode 0, no odd/even */
}

static void planes_end(const struct vga *v)
{
    u8 si = inb(0x3C4), gi = inb(0x3CE);
    (void)*(volatile u8 *)vm_ptr(0xAFFFF);              /* the latches again */
    wrx(0x3C4, 4, v->sq4);
    wrx(0x3CE, 4, v->gc4);
    wrx(0x3CE, 5, v->gc5);
    wrx(0x3CE, 6, v->gc6);
    outb(0x3C4, si);
    outb(0x3CE, gi);
}

/* ---- text */

static int grab_text(struct shot *s, const struct vga *v)
{
    const struct bootinfo *bi = vm.bi;
    u32 vde = v->c[0x12] | ((v->c[7] & 2u) << 7) | ((v->c[7] & 0x40u) << 3), ch = (v->c[9] & 0x1Fu) + 1;
    u32 cols = v->c[1] + 1u, rows = (vde + 1) / ((v->c[9] & 0x80) ? 2 : 1) / ch, pitch = v->c[0x13] * 2u;
    u32 map = (v->gc6 >> 2) & 3, base = map == 2 ? 0xB0000u : map == 3 ? 0xB8000u : 0xA0000u;
    u32 start = ((u32)v->c[0x0C] << 8) | v->c[0x0D], y;

    if (cols < 20 || cols > 132 || rows < 4 || rows > 100 || ch < 6) {     /* registers that make no sense: */
        cols = vm_rd16(0x44A);                                              /* what the BIOS says it set */
        rows = vm_rd8(0x484) + 1u;
        ch = vm_rd16(0x485);
        pitch = cols;
        if (cols < 20 || cols > 132 || rows < 4 || rows > 100 || ch < 6 || ch > 32)
            cols = pitch = 80, rows = 25, ch = 16;
    }
    if (pitch < cols)
        pitch = cols;
    s->fh = ch >= 16 && bi->font16 ? 16 : ch >= 14 && bi->font14 ? 14 : 8;
    if (!(s->fh == 16 ? bi->font16 : s->fh == 14 ? bi->font14 : bi->font8)) {
        s->why = "no ROM font";
        return -1;
    }
    s->kind = K_TEXT;
    s->ch = (u8)ch;
    s->blink = (v->at[0x10] & 0x08) != 0;
    s->w = cols * 8;
    s->h = rows * ch;
    s->rowlen = cols * 2;
    if (cap_alloc(s, rows * s->rowlen) != 0)
        return -1;
    for (y = 0; y < rows; y++)
        put_wrap(s, base, (start + y * pitch) * 2, s->rowlen, 0x8000);
    palette(s, v, 1, 0);
    return 1;
}

/* ---- the VGA's graphics modes */

static int grab_vga(struct shot *s, const struct vga *v)
{
    u32 vde = v->c[0x12] | ((v->c[7] & 2u) << 7) | ((v->c[7] & 0x40u) << 3);
    u32 lc = v->c[0x18] | ((v->c[7] & 0x10u) << 4) | ((v->c[9] & 0x40u) << 3);
    u32 msl = (v->c[9] & 0x1Fu) + 1, dbl = (v->c[9] & 0x80) ? 2 : 1;
    u32 two = !(v->c[0x17] & 1) && msl >= 2;    /* the CGA's two banks: a row's scan line picks address bit 13 */
    u32 lpr = dbl * msl / (two ? 2 : 1);        /* scan lines to a row of the picture */
    u32 start = ((u32)v->c[0x0C] << 8) | v->c[0x0D], pitch = v->c[0x13] * 2u, chars = v->c[1] + 1u;
    u32 split, y, p, planes = 1, unit = 1, size = 0x10000, base = 0xA0000;

    s->h = (vde + 1) / lpr;
    split = lc / lpr + 1;                       /* the first row past the line compare: from address 0 */
    if (v->gc5 & 0x40) {                        /* 256 colours */
        s->w = chars * 4;
        if (v->sq4 & 8) {                       /* chain-4 (13h): a byte a pixel as the CPU sees them */
            s->kind = K_PACKED;
            s->rowlen = s->w;
            unit = 4;
        } else {                                /* unchained: pixel x in plane x & 3 */
            s->kind = K_MODEX;
            s->rowlen = (s->w + 3) / 4;
            planes = 4;
        }
    } else if (((v->gc6 >> 2) & 3) == 3) {      /* at B8000h: the CGA's modes, as the CPU sees them */
        s->kind = K_CGA;
        s->bits = (v->gc5 & 0x20) ? 2 : 1;
        s->w = chars * 8;
        s->rowlen = (s->w * s->bits + 7) / 8;
        unit = (v->c[0x17] & 0x40) ? 1 : 2;     /* word mode: the address counts in twos */
        size = 0x8000;
        base = 0xB8000;
    } else {                                    /* 16 colours: a bit a pixel in each plane */
        s->kind = K_PLANAR;
        s->w = chars * 8;
        s->rowlen = (s->w + 7) / 8;
        s->planes_on = v->at[0x12] & 0x0F;
        planes = 4;
    }
    if (s->w < 8 || s->w > 2048 || s->h < 8 || s->h > 2048) {
        s->why = "a video mode glos shot doesn't know";
        return -1;
    }
    if (cap_alloc(s, s->rowlen * s->h * planes) != 0)
        return -1;
    if (planes > 1)
        planes_begin(v);
    for (p = 0; p < planes; p++) {
        if (planes > 1)
            wrx(0x3CE, 4, (u8)p);
        for (y = 0; y < s->h; y++) {
            u32 row = y < split ? y : y - split, a = (y < split ? start : 0) + (two ? row / 2 : row) * pitch;
            a *= unit;
            if (two) {
                a &= 0x1FFF;
                if (row & 1)
                    a |= 0x2000;
            }
            put_wrap(s, base, a, s->rowlen, size);
        }
    }
    if (planes > 1)
        planes_end(v);
    palette(s, v, s->kind == K_CGA || s->kind == K_PLANAR, 0);
    return 1;
}

/* ---- VBE modes */

static u32 le16(const u8 *p) { return p[0] | ((u32)p[1] << 8); }
static u32 le32(const u8 *p) { return le16(p) | (le16(p + 2) << 16); }

/* INT 10h from the capture, on the DPMI host's real-mode stack for a client
   or the stub's spare one: 1 if the BIOS answers 004Fh. */
static int vbios(struct trapframe *tf, struct rmregs *r)
{
    r->flags = 2;
    r->ss = r->sp = 0;
    if (!dctx) {
        r->ss = (u16)vm.loader_cs;
        r->sp = (u16)vm.bi->kill_sp;
    }
    return rm_call((tf->eflags & FL_VM) ? 0 : tf, r, RM_INT, 0x10, 0, 0) == 0 && (r->eax & 0xFFFF) == 0x004F;
}

/* 4F01h needs 256 bytes of conventional memory: the stub's EXEC buffers,
   which nothing reads while this runs, lent and put back. */
static int vbe_describe(struct trapframe *tf)
{
    u32 off = __builtin_offsetof(struct stub_data, path), buf = vm_lin(vm.loader_cs, off);
    struct rmregs r;
    u8 keep[256];
    int ok;

    memcpy(keep, vm_ptr(buf), 256);
    memset(vm_ptr(buf), 0, 256);
    memset(&r, 0, sizeof r);
    r.eax = 0x4F01;
    r.ecx = vbe.mode & 0x1FFu;
    r.es = (u16)vm.loader_cs;
    r.edi = off;
    ok = vbios(tf, &r);
    if (ok)
        memcpy(vbe.info, vm_ptr(buf), sizeof vbe.info);
    memcpy(vm_ptr(buf), keep, 256);
    return ok;
}

struct bank {
    struct trapframe *tf;
    u32 win, gran, size, base;
    u32 pos, home;
};

static void bank_set(struct bank *b, u32 pos)
{
    struct rmregs r;
    if (pos == b->pos)
        return;
    memset(&r, 0, sizeof r);
    r.eax = 0x4F05;
    r.ebx = b->win;
    r.edx = pos;
    vbios(b->tf, &r);
    b->pos = pos;
}

static void put_banked(struct shot *s, struct bank *b, u32 off, u32 n)
{
    while (n) {
        u32 pos = off / b->gran, in = off - pos * b->gran, k = b->size - in;
        if (k > n)
            k = n;
        bank_set(b, pos);
        put(s, vm_ptr(b->base + in), k);
        off += k;
        n -= k;
    }
}

/* A Matrox card can repeat pixels and lines (XZOOMCTRL in its DAC, the
   CRTC's maximum scan line): its BIOS makes its low modes that way, and
   MGA-Glide's HAL, which DOS-GL shares, shows a small frame in a larger
   VBE mode. What is shown is then what the CRTC and the zoom come to, not
   the mode's own size: *w and *h are made that, when they are smaller. */
static void mga_zoom(const struct vga *v, u32 *w, u32 *h)
{
    struct pci_dev *d = 0;
    u32 base = 0, vde, cols, lines, hz;
    volatile u8 *m;
    u8 xi, e2, e3, idx, zoom;
    int i;

    for (i = 0; (d = pci_at(i)) != 0; i++)
        if (d->vendor == 0x102B && d->class == 3)
            break;
    if (!d)
        return;
    for (i = 0; i < 2; i++)                     /* its registers: the memory range that isn't prefetchable */
        if (!(d->bar[i] & 9) && (d->bar[i] & ~0xFu))
            base = d->bar[i] & ~0xFu;
    xi = inb(0x3DE);                            /* CRTCEXT */
    e2 = rdx(0x3DE, 2);
    e3 = rdx(0x3DE, 3);
    outb(0x3DE, xi);
    if (!base || !(e3 & 0x80))                  /* (not in its own mode) */
        return;
    m = kmap(base + 0x3C00);                    /* the DAC: XZOOMCTRL is indexed register 38h */
    idx = m[0x00];
    m[0x00] = 0x38;
    zoom = m[0x0A];
    m[0x00] = idx;
    kunmap((void *)m);
    hz = (zoom & 3) == 1 ? 2 : (zoom & 3) == 3 ? 4 : 1;
    vde = v->c[0x12] | ((v->c[7] & 2u) << 7) | ((v->c[7] & 0x40u) << 3) | ((e2 & 4u) << 8);
    cols = (v->c[1] + 1u) * 8 / hz;
    lines = (vde + 1) / ((v->c[9] & 0x80) ? 2 : 1) / ((v->c[9] & 0x1Fu) + 1);
    if (cols >= 8 && cols < *w)
        *w = cols;
    if (lines >= 8 && lines < *h)
        *h = lines;
}

/* 1: taken; 0: not a VBE graphics mode after all; -1: failed. */
static int grab_vbe(struct shot *s, struct trapframe *tf, const struct vga *v)
{
    const u8 *m = vbe.info;
    u32 pitch, bpp, model, bytes, lfb, x0 = 0, y0 = 0, planes = 1, total, p, y;
    int lin = 0, wide = 0;
    struct bank b;
    struct rmregs r;

    if (!vbe.known && !vbe_describe(tf))
        return 0;
    vbe.known = 1;
    if (!(le16(m) & 0x10))                      /* a text mode: as any other */
        return 0;
    s->w = le16(m + 0x12);
    s->h = le16(m + 0x14);
    pitch = le16(m + 0x10);
    bpp = m[0x19];
    model = m[0x1B];
    lfb = (vbe.mode & 0x4000) ? le32(m + 0x28) : 0;
    bytes = (bpp + 7) / 8;
    s->why = "a video mode glos shot doesn't know";
    if (s->w < 8 || s->w > 4096 || s->h < 8 || s->h > 4096)
        return -1;
    if (lfb && le16(m + 0x32) >= s->w * bytes) {       /* VBE 3: the linear framebuffer's own layout */
        pitch = le16(m + 0x32);
        lin = 1;
    }
    if (model != 3)
        mga_zoom(v, &s->w, &s->h);
    if (model == 3 && bpp <= 4) {               /* planar, 16 colours */
        s->kind = K_PLANAR;
        s->rowlen = (s->w + 7) / 8;
        s->planes_on = (u8)((1u << (m[0x18] > 4 || !m[0x18] ? 4 : m[0x18])) - 1) & (v->at[0x12] | 0xF0);
        planes = 4;
        bytes = 0;
        lfb = 0;                                /* (the planes are the VGA's: through the window) */
    } else if (model == 4 && bpp == 8) {
        s->kind = K_PACKED;
        s->rowlen = s->w;
    } else if ((model == 6 || model == 4) && bpp >= 15 && bpp <= 32) {
        const u8 *f = m + (lin ? 0x36 : 0x1F);
        s->kind = K_DIRECT;
        s->bits = (u8)bpp;
        s->rowlen = s->w * bytes;
        s->rsize = f[0], s->rpos = f[1], s->gsize = f[2], s->gpos = f[3], s->bsize = f[4], s->bpos = f[5];
        if (!s->rsize || !s->gsize || !s->bsize) {      /* VBE 1.2 leaves them out: the usual layouts */
            s->rsize = s->bsize = bpp > 16 ? 8 : 5;
            s->gsize = bpp > 16 ? 8 : bpp == 16 ? 6 : 5;
            s->bpos = 0;
            s->gpos = s->bsize;
            s->rpos = (u8)(s->gpos + s->gsize);
        }
        if (s->rsize > 8 || s->gsize > 8 || s->bsize > 8 || s->rpos + s->rsize > 32 || s->gpos + s->gsize > 32
            || s->bpos + s->bsize > 32)
            return -1;
    } else {
        return -1;
    }
    if (pitch < s->rowlen)
        pitch = s->rowlen;

    memset(&r, 0, sizeof r);                    /* the scan line's length now: 4F06h can have changed it */
    r.eax = 0x4F06;
    r.ebx = 0x0001;
    if (vbios(tf, &r) && (r.ebx & 0xFFFF) >= s->rowlen)
        pitch = r.ebx & 0xFFFF;
    memset(&r, 0, sizeof r);                    /* where the display starts (a program that flips pages) */
    r.eax = 0x4F07;
    r.ebx = 0x0001;
    if (vbios(tf, &r)) {
        x0 = r.ecx & 0xFFFF;
        y0 = r.edx & 0xFFFF;
    }
    if (s->kind == K_PACKED) {
        memset(&r, 0, sizeof r);
        r.eax = 0x4F08;
        r.ebx = 0x0001;
        wide = vbios(tf, &r) && ((r.ebx >> 8) & 0xFF) >= 8;
    }
    total = s->rowlen * s->h;
    if (cap_alloc(s, total * planes) != 0)
        return -1;
    x0 = s->kind == K_PLANAR ? x0 / 8 : x0 * bytes;

    if (lfb) {
        for (y = 0; y < s->h; y++)
            put_phys(s, lfb + (y0 + y) * pitch + x0, s->rowlen);
    } else {
        b.tf = tf;
        b.win = (m[2] & 3) == 3 || (m[3] & 3) != 3 ? 0 : 1;    /* window A if it can be read, else B */
        b.base = le16(m + (b.win ? 0x0A : 0x08)) << 4;
        b.gran = le16(m + 4) << 10;
        b.size = le16(m + 6) << 10;
        if (!b.base)
            b.base = 0xA0000;
        if (!b.gran || b.gran > 0x10000)
            b.gran = 0x10000;
        if (b.size < b.gran || b.size > 0x10000)
            b.size = b.gran;
        memset(&r, 0, sizeof r);
        r.eax = 0x4F05;
        r.ebx = 0x0100 | b.win;
        b.home = b.pos = vbios(tf, &r) ? r.edx & 0xFFFF : 0;
        if (planes > 1) {
            bank_set(&b, (total - 1) / b.gran); /* the latches go past the frame's end, when it has one */
            planes_begin(v);
        }
        for (p = 0; p < planes; p++) {
            if (planes > 1)
                wrx(0x3CE, 4, (u8)p);
            for (y = 0; y < s->h; y++)
                put_banked(s, &b, (y0 + y) * pitch + x0, s->rowlen);
        }
        if (planes > 1) {
            bank_set(&b, (total - 1) / b.gran);
            planes_end(v);
        }
        bank_set(&b, b.home);
    }
    if (s->kind != K_DIRECT)
        palette(s, v, s->kind == K_PLANAR, wide);
    return 1;
}

/* ---- the capture: on the VM thread */

static int capture(struct vm_work *w, struct trapframe *tf)
{
    struct shot *s = (struct shot *)w;
    u32 cs = tf->cs & 0xFFFF;
    struct vga v;
    int r = 0;

    if ((int10_busy || ((tf->eflags & FL_VM) && cs >= 0xC000 && cs < 0xC800)) && timer_ticks() - w->posted < BIOS_WAIT)
        return 1;                               /* the video BIOS is at work: when it has done */
    vga_read(&v);
    if (vbe.mode)
        r = grab_vbe(s, tf, &v);
    if (!r)
        r = (v.gc6 & 1) ? grab_vga(s, &v) : grab_text(s, &v);
    s->got = r;
    if (r > 0 && (s->kind == K_DIRECT || s->kind == K_PACKED) && mga_cursor_state(&s->cx, &s->cy, &s->cand, &s->cxor)) {
        s->cursor = 1;
        s->black = gfx_pal_nearest(0x000000);
        s->white = gfx_pal_nearest(0xFFFFFF);
    }
    if (r > 0) {                                /* what GLOS took the screen for */
        static const char *const kinds[] = { "text", "cga", "planar", "unchained", "packed", "direct" };
        kprintf("GLOS-SHOT kind=%s size=%ux%u vbe=%04x\n", kinds[s->kind], s->w, s->h, vbe.mode);
    }
    return 0;
}

/* ---- the reader: on the ssh thread */

struct shot *shot_new(int raw, void (*wake)(void))
{
    struct shot *s = kmalloc(sizeof *s);
    if (!s)
        return 0;
    memset(s, 0, sizeof *s);
    s->raw = raw != 0;
    s->work.fn = capture;
    s->work.done = wake;
    s->work.patience = PATIENCE;
    s->t0 = timer_ticks();
    return s;
}

const char *shot_why(const struct shot *s) { return s->why ? s->why : "failed"; }

static int depth(const struct shot *s) { return s->kind == K_DIRECT ? 24 : s->kind >= K_MODEX ? 8 : 4; }

static int fail(struct shot *s, const char *why)
{
    s->why = why;
    s->phase = P_FAILED;
    return -1;
}

int shot_poll(struct shot *s)
{
    u32 row, planes;
    switch (s->phase) {
    case P_NEW:
        if (vm_post(&s->work) == 0)
            s->phase = P_POSTED;
        else if (timer_ticks() - s->t0 > TIMEOUT)       /* (another one's capture: it wakes this thread) */
            return fail(s, "busy");
        return 0;
    case P_POSTED:
        if (s->work.state != VW_DONE) {
            if (timer_ticks() - s->t0 > TIMEOUT && vm_unpost(&s->work))
                return fail(s, "the program doesn't stop (its interrupts are off)");
            return 0;
        }
        if (s->got <= 0)
            return fail(s, s->why);
        display_sync();                         /* (what a drawing engine had queued is on the screen) */
        row = s->raw ? s->w * 3 : 1 + (depth(s) == 24 ? s->w * 3 : depth(s) == 8 ? s->w : (s->w + 1) / 2);
        planes = s->kind == K_PLANAR || s->kind == K_MODEX ? 4 : 1;
        s->group = GROUP / row ? GROUP / row : 1;
        s->src = kmalloc(s->rowlen * planes + 4);
        s->pix = kmalloc(s->w + 4);
        s->lines = kmalloc(s->group * row);
        if (!s->src || !s->pix || !s->lines)
            return fail(s, "no memory");
        s->phase = P_HEAD;
        return 1;
    case P_FAILED:
        return -1;
    }
    return 1;
}

/* Row y of the picture: s->pix, a byte a pixel (K_DIRECT: nothing, s->src
   has the row as the card does). */
static void row(struct shot *s, u32 y)
{
    const struct bootinfo *bi = vm.bi;
    u32 x, p, plane = s->rowlen * s->h;

    switch (s->kind) {
    case K_TEXT: {
        u32 gy = y % s->ch, cols = s->w / 8, gx;
        get(s, (y / s->ch) * s->rowlen, s->src, s->rowlen);
        for (x = 0; x < cols; x++) {
            u8 c = s->src[x * 2], a = s->src[x * 2 + 1], fg = a & 15, bg = (u8)(s->blink ? (a >> 4) & 7 : a >> 4);
            u8 bits = 0;
            if (gy < s->fh)
                bits = vm_rd8(s->fh == 16 ? bi->font16 + c * 16u + gy : s->fh == 14 ? bi->font14 + c * 14u + gy
                              : c < 128 ? bi->font8 + c * 8u + gy : bi->font8hi + (c - 128u) * 8u + gy);
            for (gx = 0; gx < 8; gx++)
                s->pix[x * 8 + gx] = (bits & (0x80 >> gx)) ? fg : bg;
        }
        break;
    }
    case K_CGA:
        get(s, y * s->rowlen, s->src, s->rowlen);
        for (x = 0; x < s->w; x++)
            s->pix[x] = s->bits == 2 ? (s->src[x >> 2] >> (6 - 2 * (x & 3))) & 3 : (s->src[x >> 3] >> (7 - (x & 7))) & 1;
        break;
    case K_PLANAR:
        for (p = 0; p < 4; p++)
            get(s, p * plane + y * s->rowlen, s->src + p * s->rowlen, s->rowlen);
        for (x = 0; x < s->w; x++) {
            u8 px = 0;
            for (p = 0; p < 4; p++)
                px |= (u8)(((s->src[p * s->rowlen + (x >> 3)] >> (7 - (x & 7))) & 1) << p);
            s->pix[x] = px & s->planes_on;
        }
        break;
    case K_MODEX:
        for (p = 0; p < 4; p++)
            get(s, p * plane + y * s->rowlen, s->src + p * s->rowlen, s->rowlen);
        for (x = 0; x < s->w; x++)
            s->pix[x] = s->src[(x & 3) * s->rowlen + (x >> 2)];
        break;
    case K_PACKED:
        get(s, y * s->rowlen, s->pix, s->rowlen);
        break;
    default:
        get(s, y * s->rowlen, s->src, s->rowlen);
        break;
    }
}

/* A field of a direct-colour pixel as 8 bits. */
static u8 field(u32 v, u32 pos, u32 size)
{
    u32 c = ((v >> pos) & ((1u << size) - 1)) << (8 - size), bits;
    for (bits = size; bits < 8; bits *= 2)
        c |= c >> bits;
    return (u8)c;
}

/* The cursor over row y: RGB (three bytes a pixel) or palette indices (one). */
static void cursor_over(const struct shot *s, u32 y, u8 *out, int rgb)
{
    s32 iy = (s32)y - s->cy, ix, x;
    if (!s->cursor || iy < 0 || iy >= 64)
        return;
    for (ix = 0; ix < 64; ix++) {
        u8 bit = (u8)(0x80 >> (ix & 7));
        int a = (s->cand[iy * 8 + ix / 8] & bit) != 0, o = (s->cxor[iy * 8 + ix / 8] & bit) != 0;
        if ((x = s->cx + ix) < 0 || (u32)x >= s->w || (a && !o))
            continue;
        if (rgb) {
            u8 *p = out + (u32)x * 3;
            if (a)
                p[0] = (u8)~p[0], p[1] = (u8)~p[1], p[2] = (u8)~p[2];
            else
                p[0] = p[1] = p[2] = o ? 0xFF : 0x00;
        } else if (!a) {                        /* (inverting a palette's colours has no index: not drawn) */
            out[x] = o ? s->white : s->black;
        }
    }
}

/* Row y as RGB. */
static void row_rgb(struct shot *s, u32 y, u8 *out)
{
    u8 *start = out;
    u32 x, bytes = (s->bits + 7u) / 8;
    row(s, y);
    if (s->kind == K_PACKED)
        cursor_over(s, y, s->pix, 0);
    for (x = 0; x < s->w; x++, out += 3) {
        if (s->kind == K_DIRECT) {
            const u8 *q = s->src + x * bytes;
            u32 v = q[0] | ((u32)q[1] << 8) | (bytes > 2 ? (u32)q[2] << 16 : 0) | (bytes > 3 ? (u32)q[3] << 24 : 0);
            out[0] = field(v, s->rpos, s->rsize);
            out[1] = field(v, s->gpos, s->gsize);
            out[2] = field(v, s->bpos, s->bsize);
        } else {
            memcpy(out, s->pal[s->pix[x]], 3);
        }
    }
    if (s->kind == K_DIRECT)
        cursor_over(s, y, start, 1);
}

u32 shot_read(struct shot *s, u8 *out)
{
    u32 n, i, x, d = (u32)depth(s);

    if (s->phase == P_HEAD) {
        s->phase = P_ROWS;
        if (s->raw) {                           /* "GLOSRAW1", the width, the height; then RGB, a byte each */
            memcpy(out, "GLOSRAW1", 8);
            for (i = 0; i < 4; i++) {
                out[8 + i] = (u8)(s->w >> (8 * i));
                out[12 + i] = (u8)(s->h >> (8 * i));
            }
            return 16;
        }
        return png_begin(&s->png, s->w, s->h, (int)d, (const u8 (*)[3])s->pal, d == 4 ? 16 : 256, out);
    }
    if (s->phase != P_ROWS || s->y >= s->h)
        return 0;
    n = s->h - s->y < s->group ? s->h - s->y : s->group;
    for (i = 0; i < n; i++, s->y++) {
        if (s->raw) {
            row_rgb(s, s->y, out + i * s->w * 3);
            continue;
        }
        if (d == 24) {
            u8 *l = s->lines + i * s->png.row;
            *l = 0;
            row_rgb(s, s->y, l + 1);
            continue;
        }
        row(s, s->y);
        if (d == 8) {
            u8 *l = s->lines + i * s->png.row;
            *l = 0;
            if (s->kind == K_PACKED)
                cursor_over(s, s->y, s->pix, 0);
            memcpy(l + 1, s->pix, s->w);
        } else {
            u8 *l = s->lines + i * s->png.row;
            *l++ = 0;
            s->pix[s->w] = 0;
            for (x = 0; x < s->w; x += 2)
                *l++ = (u8)((s->pix[x] << 4) | (s->pix[x + 1] & 15));
        }
    }
    return s->raw ? n * s->w * 3 : png_rows(&s->png, s->lines, n, out);
}

void shot_free(struct shot *s)
{
    u32 i;
    if (s->phase == P_POSTED && !vm_unpost(&s->work))
        while (s->work.state != VW_DONE)        /* being taken: it ends */
            thread_yield();
    for (i = 0; i < s->nframes; i++)
        pmm_free(s->frames[i]);
    if (s->frames) kfree(s->frames);
    if (s->src) kfree(s->src);
    if (s->pix) kfree(s->pix);
    if (s->lines) kfree(s->lines);
    kfree(s);
}
