/* SHAREDOS (docs/devices.md, step D2's second half): a DOS driver's interrupts on a line GLOS shares. It plays
 * silence on a CMI8738 that GLOS has left to DOS ([devices] off), by bus master from a buffer of its own, with the
 * channel's interrupt at each half, and counts them in its own handler; the interrupts of the line that are not
 * its card's (GLOS's network card's, under GLOS.EXE /SHAREIRQ) are counted too, and ended as the BIOS's handler
 * would end them. The PCI BIOS's configuration write that turns the card's bus mastering on is what tells GLOS
 * the card is DOS's again (a quiet device given back: its line is then passed to DOS as well as to GLOS).
 *     SHAREDOS [seconds]          (5 by default)
 *     HX-SHAREDOS irq=N io=XXXX seconds=N irqs=N others=N
 * The CMI8738's registers (16-bit accesses: its PCI I/O decodes them): FUNCTRL0 00h (channel 0's enable bit 16, its
 * reset bit 18, its direction bit 0), FUNCTRL1 04h (channel 0's rate, bits 12-10; the bus master's request, bit 4;
 * the legacy SB's voice, bit 3), CHFORMAT 08h (channel 0's format, bits 1-0: 3, 16-bit stereo), INT_HLDCLR 0Ch
 * (channel 0's interrupt enable, bit 16: cleared and set again to acknowledge), INT_STATUS 10h (channel 0's, bit
 * 0), the channel's address 80h and its count 84h (the buffer's dwords less one, the interrupt's less one above).
 * Open Watcom, real mode. */
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAMES 256

static unsigned io;
static unsigned char line;
static volatile unsigned long irqs, others;
static void (__interrupt __far *old)(void);
static unsigned char buf[FRAMES * 2 * 4 + 16];

static void ser(const char *s)
{
    for (; *s; s++) {
        unsigned n = 0;
        while (!(inp(0x3FD) & 0x20) && ++n < 60000u)
            ;
        outp(0x3F8, *s);
    }
}

static void __interrupt __far isr(void)
{
    if (inpw(io + 0x10) & 1) {                  /* its channel's half: acknowledged */
        unsigned h = inpw(io + 0x0E);
        outpw(io + 0x0E, h & ~1u);
        outpw(io + 0x0E, h | 1u);
        irqs++;
    } else {
        others++;
    }
    if (line >= 8)
        outp(0xA0, 0x20);
    outp(0x20, 0x20);
}

int main(int argc, char **argv)
{
    union REGS r;
    unsigned char bus, devfn, vec, pic_was;
    unsigned seconds = argc > 1 ? atoi(argv[1]) : 5, h;
    unsigned long phys, bar, start;
    char out[120];
    volatile unsigned long far *ticks = (volatile unsigned long far *)MK_FP(0x40, 0x6C);

    memset(&r, 0, sizeof r);
    r.w.ax = 0xB102;                            /* the CMI8738 */
    r.w.cx = 0x0111, r.w.dx = 0x13F6, r.w.si = 0;
    int86(0x1A, &r, &r);
    if (r.h.ah) {
        puts("HX-SHAREDOS none");
        ser("HX-SHAREDOS none\r\n");
        return 2;
    }
    bus = r.h.bh, devfn = r.h.bl;
    memset(&r, 0, sizeof r);                    /* BAR 0 (B109h: a word is enough for an I/O base) */
    r.w.ax = 0xB109, r.h.bh = bus, r.h.bl = devfn, r.w.di = 0x10;
    int86(0x1A, &r, &r);
    bar = r.w.cx;
    io = (unsigned)(bar & 0xFFFC);
    memset(&r, 0, sizeof r);                    /* its line */
    r.w.ax = 0xB108, r.h.bh = bus, r.h.bl = devfn, r.w.di = 0x3C;
    int86(0x1A, &r, &r);
    line = r.h.cl;
    memset(&r, 0, sizeof r);                    /* its command: I/O and bus master (GLOS hears DOS take it back) */
    r.w.ax = 0xB109, r.h.bh = bus, r.h.bl = devfn, r.w.di = 0x04;
    int86(0x1A, &r, &r);
    h = r.w.cx;
    memset(&r, 0, sizeof r);
    r.w.ax = 0xB10C, r.h.bh = bus, r.h.bl = devfn, r.w.di = 0x04, r.w.cx = h | 5;
    int86(0x1A, &r, &r);
    if (!io || !line || line > 15) {
        printf("HX-SHAREDOS bad io=%x irq=%u\n", io, line);
        return 2;
    }
    vec = (unsigned char)(line < 8 ? 8 + line : 0x70 + line - 8);
    old = _dos_getvect(vec);
    _disable();
    _dos_setvect(vec, isr);
    if (line >= 8) {
        pic_was = (unsigned char)inp(0xA1);
        outp(0xA1, pic_was & ~(1 << (line - 8)));
        outp(0x21, inp(0x21) & ~4);
    } else {
        pic_was = (unsigned char)inp(0x21);
        outp(0x21, pic_was & ~(1 << line));
    }
    _enable();
    memset(buf, 0, sizeof buf);
    phys = ((unsigned long)FP_SEG(buf) << 4) + FP_OFF(buf);
    phys = (phys + 15) & ~15UL;
    outpw(io + 0x02, inpw(io + 0x02) | 4);      /* channel 0 reset, off */
    outpw(io + 0x02, inpw(io + 0x02) & ~5u);
    outpw(io + 0x00, inpw(io + 0x00) & ~1u);    /* playing */
    outpw(io + 0x04, (inpw(io + 0x04) & ~(0x1C00u | 0x08u)) | (2u << 10) | 0x10u);   /* 22050 Hz, bus master */
    outpw(io + 0x08, (inpw(io + 0x08) & ~3u) | 3u);
    outpw(io + 0x80, (unsigned)phys);
    outpw(io + 0x82, (unsigned)(phys >> 16));
    outpw(io + 0x84, FRAMES * 2 - 1);
    outpw(io + 0x86, FRAMES - 1);
    outpw(io + 0x0E, inpw(io + 0x0E) & ~1u);
    outpw(io + 0x0E, inpw(io + 0x0E) | 1u);     /* its interrupt */
    outpw(io + 0x02, (inpw(io + 0x02) & ~4u) | 1u);     /* on */
    start = *ticks;
    while (*ticks - start < (unsigned long)seconds * 182UL / 10UL) {
    }
    outpw(io + 0x02, inpw(io + 0x02) & ~1u);    /* off, its interrupt off, reset */
    outpw(io + 0x0E, inpw(io + 0x0E) & ~1u);
    outpw(io + 0x02, inpw(io + 0x02) | 4);
    outpw(io + 0x02, inpw(io + 0x02) & ~4u);
    _disable();
    if (line >= 8)
        outp(0xA1, pic_was);
    else
        outp(0x21, pic_was);
    _dos_setvect(vec, old);
    _enable();
    sprintf(out, "HX-SHAREDOS irq=%u io=%x seconds=%u irqs=%lu others=%lu\r\n", line, io, seconds, irqs, others);
    ser(out);
    fputs(out, stdout);
    return irqs ? 0 : 1;
}
