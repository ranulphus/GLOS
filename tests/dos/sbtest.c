/* SBTEST (GLOS's virtual Sound Blaster, docs/v1/vsb.md): a Sound Blaster found and played the way a DOS game
 * does it, in real mode (Open Watcom). It talks to whatever card BLASTER names: GLOS's virtual one, or a real one
 * (86Box's SB16 with [sound] virtual = off), which is how the test itself is known to be right.
 *
 *   SBTEST          every case, each a line "SBTEST case=NAME ok=N ...", then "SBTEST done pass=N fail=M"
 *
 * The cases: found (BLASTER, the DSP's reset and version), ident (the identification and test bytes, the mixer
 * chip's interrupt and DMA registers), irq (an interrupt asked for comes on BLASTER's line), single (one 8-bit
 * block by a time constant: its interrupt after as long as the block takes), auto (auto-initialised blocks: an
 * interrupt a block, the DMA's count moving, and no more after the end is asked for), chain (single blocks one
 * after another, each asked for in the interrupt of the one before, as many games play), sb16 (16 bits, two
 * channels, by the high DMA channel, where BLASTER has one). The exit code is the failures. */
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned base = 0x220, irq = 5, dma8 = 1, dma16 = 0;
static volatile unsigned irqs;
static int wide_ack;
static int pass, fails;

static volatile unsigned chain_left;            /* single blocks still to ask for from the handler */
static unsigned long chain_phys;
static void dsp_write(unsigned char b);
static void dma8_setup(unsigned long phys, unsigned bytes, int auto_init);

static void __interrupt __far handler(void)
{
    irqs++;
    inp(base + (wide_ack ? 0xF : 0xE));         /* the card's interrupt acknowledged */
    if (chain_left) {                           /* the next block of 1024 bytes: the channel again, and the command */
        chain_left--;
        dma8_setup(chain_phys, 1024, 0);
        dsp_write(0x14);
        dsp_write(0xFF);
        dsp_write(0x03);
    }
    if (irq >= 8)
        outp(0xA0, 0x20);
    outp(0x20, 0x20);
}

static unsigned long ticks(void)
{
    volatile unsigned long far *t = (volatile unsigned long far *)MK_FP(0x40, 0x6C);
    return *t;
}

static void check(const char *name, int ok, const char *detail)
{
    printf("SBTEST case=%s ok=%d %s\r\n", name, ok != 0, detail);
    if (ok)
        pass++;
    else
        fails++;
}

static void dsp_write(unsigned char b)
{
    unsigned i;
    for (i = 0; i < 20000 && (inp(base + 0xC) & 0x80); i++)
        ;
    outp(base + 0xC, b);
}

static int dsp_read(void)
{
    unsigned i;
    for (i = 0; i < 20000; i++)
        if (inp(base + 0xE) & 0x80)
            return inp(base + 0xA);
    return -1;
}

static int dsp_reset(void)
{
    unsigned i;
    outp(base + 6, 1);
    for (i = 0; i < 8; i++)
        inp(base + 6);
    outp(base + 6, 0);
    return dsp_read() == 0xAA;
}

static unsigned mixer_get(unsigned char index)
{
    outp(base + 4, index);
    return inp(base + 5);
}

/* The DMA channels, set up as every game does it. */
static void dma8_setup(unsigned long phys, unsigned bytes, int auto_init)
{
    static const unsigned page[4] = { 0x87, 0x83, 0x81, 0x82 };
    outp(0x0A, 4 | dma8);
    outp(0x0C, 0);
    outp(0x0B, (auto_init ? 0x58 : 0x48) | dma8);
    outp(dma8 * 2, (unsigned)phys & 0xFF);
    outp(dma8 * 2, (unsigned)(phys >> 8) & 0xFF);
    outp(page[dma8], (unsigned)(phys >> 16) & 0xFF);
    outp(dma8 * 2 + 1, (bytes - 1) & 0xFF);
    outp(dma8 * 2 + 1, (bytes - 1) >> 8);
    outp(0x0A, dma8);
}

static unsigned dma8_count(void)
{
    unsigned lo;
    outp(0x0C, 0);
    lo = inp(dma8 * 2 + 1);
    return lo | inp(dma8 * 2 + 1) << 8;
}

static void dma16_setup(unsigned long phys, unsigned bytes, int auto_init)
{
    static const unsigned page[4] = { 0x8F, 0x8B, 0x89, 0x8A };
    unsigned ch = dma16 & 3, words = bytes / 2;
    outp(0xD4, 4 | ch);
    outp(0xD8, 0);
    outp(0xD6, (auto_init ? 0x58 : 0x48) | ch);
    outp(0xC0 + ch * 4, (unsigned)(phys >> 1) & 0xFF);
    outp(0xC0 + ch * 4, (unsigned)(phys >> 9) & 0xFF);
    outp(page[ch], (unsigned)(phys >> 16) & 0xFF);
    outp(0xC2 + ch * 4, (words - 1) & 0xFF);
    outp(0xC2 + ch * 4, (words - 1) >> 8);
    outp(0xD4, ch);
}

/* Until so many interrupts have come, or so many of the BIOS's ticks have gone: the ticks it took. */
static unsigned wait_irqs(unsigned n, unsigned max_ticks)
{
    unsigned long t0 = ticks();
    while (irqs < n && ticks() - t0 < max_ticks)
        ;
    return (unsigned)(ticks() - t0);
}

int main(void)
{
    const char *b = getenv("BLASTER");
    void (__interrupt __far *old)(void);
    unsigned seg = 0, vec, took, i, mask21, maskA1, moved = 0, after;
    unsigned long phys, skip;
    unsigned char far *buf;
    char line[100];
    int major, minor, a, c;

    for (; b && *b; b++) {                      /* BLASTER=A220 I5 D1 H5 T6 */
        char k = *b;
        if (k == ' ')
            continue;
        if (k == 'A' || k == 'a')
            base = (unsigned)strtoul(b + 1, NULL, 16);
        else if (k == 'I' || k == 'i')
            irq = (unsigned)atoi(b + 1);
        else if (k == 'D' || k == 'd')
            dma8 = (unsigned)atoi(b + 1);
        else if (k == 'H' || k == 'h')
            dma16 = (unsigned)atoi(b + 1);
        while (b[1] && b[1] != ' ')
            b++;
    }

    major = minor = -1;
    a = dsp_reset();
    if (a) {
        dsp_write(0xE1);
        major = dsp_read();
        minor = dsp_read();
    }
    sprintf(line, "blaster=%s port=%x irq=%u dma=%u high=%u dsp=%d.%02d", getenv("BLASTER") ? "set" : "none", base, irq, dma8,
            dma16, major, minor);
    check("found", getenv("BLASTER") && a && major >= 2, line);
    if (!a) {
        printf("SBTEST done pass=%d fail=%d\r\n", pass, fails);
        return fails;
    }

    dsp_write(0xE0);
    dsp_write(0x55);
    a = dsp_read();
    dsp_write(0xE4);
    dsp_write(0x5A);
    dsp_write(0xE8);
    c = dsp_read();
    sprintf(line, "e0=%x e8=%x mixer80=%x mixer81=%x", a, c, mixer_get(0x80), mixer_get(0x81));
    check("ident", a == 0xAA && c == 0x5A
          && (major < 4 || ((mixer_get(0x80) & (irq == 5 ? 2 : irq == 7 ? 4 : irq == 10 ? 8 : 1))
                            && (mixer_get(0x81) & (1 << dma8)))), line);

    /* Its interrupt: the vector, the line unmasked, an interrupt asked for. */
    vec = irq < 8 ? 8 + irq : 0x70 + irq - 8;
    old = _dos_getvect(vec);
    _dos_setvect(vec, handler);
    mask21 = inp(0x21), maskA1 = inp(0xA1);
    if (irq < 8) {
        outp(0x21, mask21 & ~(1 << irq));
    } else {
        outp(0xA1, maskA1 & ~(1 << (irq - 8)));
        outp(0x21, mask21 & ~4);
    }
    irqs = 0;
    dsp_write(0xF2);
    took = wait_irqs(1, 18);
    sprintf(line, "irqs=%u ticks=%u", irqs, took);
    check("irq", irqs == 1, line);

    /* A buffer for the DMA: 8 KB that do not cross a 64 KB boundary. */
    if (_dos_allocmem(0x800, &seg) != 0) {
        check("memory", 0, "no 32 KB of DOS memory");
        goto out;
    }
    phys = (unsigned long)seg << 4;
    skip = (phys & 0xFFFF) + 8192 > 0x10000UL ? 0x10000UL - (phys & 0xFFFF) : 0;
    phys += skip;
    buf = (unsigned char far *)MK_FP(seg + (unsigned)(skip >> 4), 0);

    /* One block of 4096 bytes, 8 bits, a square wave, at a time constant's 10989 a second: its interrupt after
       0.37 seconds, about 7 of the BIOS's ticks. */
    for (i = 0; i < 8192; i++)
        buf[i] = (i / 16) & 1 ? 0xF0 : 0x10;
    dsp_write(0xD1);
    dsp_write(0x40);
    dsp_write(165);
    dma8_setup(phys, 4096, 0);
    irqs = 0;
    dsp_write(0x14);
    dsp_write(0xFF);
    dsp_write(0x0F);
    took = wait_irqs(1, 60);
    for (i = 0; i < 3; i++)                     /* (and no second one) */
        wait_irqs(99, 2);
    sprintf(line, "irqs=%u ticks=%u count=%x", irqs, took, dma8_count());
    check("single", irqs == 1 && took >= 4 && took <= 16, line);

    /* Blocks of 2048 from the buffer's 4096, one after another: four interrupts in about 14 ticks, the DMA's
       count seen to move; the end asked for, and soon no more come. */
    dma8_setup(phys, 4096, 1);
    dsp_write(0x48);
    dsp_write(0xFF);
    dsp_write(0x07);
    irqs = 0;
    dsp_write(0x1C);
    {
        unsigned long t0 = ticks();
        unsigned first = dma8_count();
        while (irqs < 4 && ticks() - t0 < 80)
            if (dma8_count() != first)
                moved = 1;
        took = (unsigned)(ticks() - t0);
    }
    dsp_write(0xDA);
    wait_irqs(99, 12);
    after = irqs;
    wait_irqs(99, 8);
    sprintf(line, "ticks=%u moved=%u irqs=%u then=%u", took, moved, after, irqs);
    check("auto", took >= 8 && took <= 30 && moved && after >= 4 && after <= 7 && irqs == after, line);

    /* Eight single blocks of 1024 bytes one after another, each asked for in the interrupt of the one before:
       eight interrupts in the time eight blocks take, 0.75 seconds, about 14 ticks; and no ninth. */
    dsp_write(0x40);
    dsp_write(165);
    chain_phys = phys;
    dma8_setup(phys, 1024, 0);
    irqs = 0;
    chain_left = 7;
    dsp_write(0x14);
    dsp_write(0xFF);
    dsp_write(0x03);
    took = wait_irqs(8, 60);
    for (i = 0; i < 3; i++)
        wait_irqs(99, 2);
    sprintf(line, "irqs=%u ticks=%u", irqs, took);
    check("chain", irqs == 8 && took >= 11 && took <= 18, line);

    /* The SB16's 16 bits, two channels, signed, at 22050 a second, by the high DMA channel: blocks of 1024
       frames, eight interrupts in about 7 ticks. */
    if (dma16 >= 5 && major >= 4) {
        for (i = 0; i < 8192; i += 4) {
            int v = (i / 64) & 1 ? 24000 : -24000;
            buf[i] = (unsigned char)v, buf[i + 1] = (unsigned char)(v >> 8);
            buf[i + 2] = (unsigned char)-v, buf[i + 3] = (unsigned char)(-v >> 8);
        }
        dsp_reset();
        dma16_setup(phys, 8192, 1);
        dsp_write(0x41);
        dsp_write(22050 >> 8);
        dsp_write(22050 & 0xFF);
        wide_ack = 1;
        irqs = 0;
        dsp_write(0xB6);
        dsp_write(0x30);
        dsp_write(0xFF);
        dsp_write(0x07);
        took = wait_irqs(8, 60);
        sprintf(line, "irqs=%u ticks=%u", irqs, took);
        check("sb16", irqs >= 8 && took >= 4 && took <= 16, line);
        wide_ack = 0;
    }
    dsp_reset();
    _dos_freemem(seg);
out:
    outp(0x21, mask21);
    outp(0xA1, maskA1);
    _dos_setvect(vec, old);
    printf("SBTEST done pass=%d fail=%d\r\n", pass, fails);
    return fails;
}
