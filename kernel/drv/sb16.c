/* The Sound Blaster 16 under GLOS's mixer (PRD §10.5; docs/milestones-m8.md
 * step E): its DSP (version 4) playing 16-bit stereo from a buffer of two
 * halves by auto-initialised DMA on the high channel, the card's interrupt
 * at the end of each half, when the mixer fills it again (mixer.c).
 *
 * BLASTER (or GLOS.CFG's [sound] blaster) says where it is: A the port, I the
 * interrupt, H the 16-bit DMA channel (5 to 7). The DSP is found by its
 * reset (it answers AAh) and its version (E1h). Commands, from Creative's
 * DSP documentation: 41h the output rate; B6h 16-bit output, auto-initialised,
 * FIFO on, with mode 30h (stereo, signed) and the block's samples less one;
 * D5h pause, D9h leave auto-initialisation; the 16-bit interrupt is
 * acknowledged by reading port base+0Fh. The 8237's high controller (C0h-DFh)
 * counts words: a buffer within one 128 KB block below 16 MB.
 *
 * The card is a session's while one runs (session.c): the mixer stops it and
 * gives the line back to the system VM first, and starts it again after. */
#include "glos/bootinfo.h"
#include "io.h"
#include "kprintf.h"
#include "mixer.h"
#include "mm.h"
#include "timer.h"
#include "vm.h"

static struct {
    u16 base;
    u8 irq, dma;                                /* the 16-bit channel */
    u8 found, running, major, minor;
    u8 *buf;                                    /* the DMA buffer: two halves */
    u32 phys;
    u32 frames;                                 /* a half's */
    u32 half;                                   /* the half the card plays next */
    u32 irqs;
    void (*fill)(s16 *out, u32 frames);
} sb;

static const u8 page_port[4] = { 0, 0x8B, 0x89, 0x8A };        /* channels 5, 6, 7 (4 is the cascade) */

static int dsp_write(u8 v)
{
    u32 n;
    for (n = 0; n < 100000; n++)
        if (!(inb((u16)(sb.base + 0x0C)) & 0x80)) {
            outb((u16)(sb.base + 0x0C), v);
            return 0;
        }
    return -1;
}

static int dsp_read(void)
{
    u32 n;
    for (n = 0; n < 100000; n++)
        if (inb((u16)(sb.base + 0x0E)) & 0x80)
            return inb((u16)(sb.base + 0x0A));
    return -1;
}

static int dsp_reset(void)
{
    u32 i;
    outb((u16)(sb.base + 6), 1);
    for (i = 0; i < 100; i++)                   /* (3 microseconds at least: a hundred port reads) */
        inb(0x80);
    outb((u16)(sb.base + 6), 0);
    for (i = 0; i < 1000; i++)
        if (dsp_read() == 0xAA)
            return 0;
    return -1;
}

int sb16_init(const struct bootinfo *bi)
{
    int hi, lo;
    if (!bi->sb_port || bi->sb_irq > 15 || bi->sb_dma16 < 5 || bi->sb_dma16 > 7)
        return -1;
    sb.base = (u16)bi->sb_port;
    sb.irq = (u8)bi->sb_irq;
    sb.dma = (u8)bi->sb_dma16;
    if (dsp_reset() != 0 || dsp_write(0xE1) != 0 || (hi = dsp_read()) < 0 || (lo = dsp_read()) < 0) {
        kprintf("GLOS-SB none port=%x\n", sb.base);
        return -1;
    }
    sb.major = (u8)hi, sb.minor = (u8)lo;
    if (hi < 4) {
        kprintf("GLOS-SB refuse port=%x dsp=%u.%02u reason=not-sb16\n", sb.base, hi, lo);
        return -1;
    }
    sb.found = 1;
    kprintf("GLOS-SB found port=%x irq=%u dma16=%u dsp=%u.%02u\n", sb.base, sb.irq, sb.dma, hi, lo);
    return 0;
}

static void irq(struct trapframe *tf)
{
    (void)tf;
    inb((u16)(sb.base + 0x0F));                 /* (the 16-bit interrupt acknowledged) */
    if (!sb.running)
        return;
    sb.irqs++;
    sb.fill((s16 *)(sb.buf + sb.half * sb.frames * 4), sb.frames);
    sb.half ^= 1;
}

static void dma_stop(void) { outb(0xD4, (u8)(4 | (sb.dma & 3))); }

int sb16_start(u32 rate, u32 frames, void (*fill)(s16 *out, u32 frames))
{
    u32 words = frames * 2 * 2, ch = sb.dma & 3, addr;
    if (!sb.found)
        return -1;
    if (!sb.buf) {
        u32 phys;
        u8 *p = dma_alloc((frames * 8 + 4095) / 4096, &phys);  /* (a page or two: never across 128 KB, as the
                                                   region's frames are allocated in a run from the bottom) */
        if (!p || phys + frames * 8 > 0x1000000u || (phys >> 17) != ((phys + frames * 8 - 1) >> 17)) {
            kprintf("GLOS-SB error=dma-buffer phys=%x\n", phys);
            return -1;
        }
        sb.buf = p, sb.phys = phys;
    }
    sb.frames = frames;
    sb.fill = fill;
    sb.half = 0;
    fill((s16 *)sb.buf, frames);
    fill((s16 *)(sb.buf + frames * 4), frames);
    if (dsp_reset() != 0)
        return -1;
    addr = sb.phys >> 1;                        /* (words) */
    dma_stop();
    outb(0xD8, 0);                              /* the flip-flop */
    outb(0xD6, (u8)(0x58 | ch));                /* single, auto-initialised, memory to the card */
    outb((u16)(0xC0 + ch * 4), (u8)addr);
    outb((u16)(0xC0 + ch * 4), (u8)(addr >> 8));
    outb(page_port[ch], (u8)((sb.phys >> 16) & 0xFE));
    outb((u16)(0xC2 + ch * 4), (u8)(words - 1));
    outb((u16)(0xC2 + ch * 4), (u8)((words - 1) >> 8));
    outb(0xD4, (u8)ch);                         /* unmasked */
    vm_claim_irq(sb.irq, irq);
    sb.running = 1;
    dsp_write(0xD1);                            /* (the speaker: no-op on an SB16) */
    dsp_write(0x41);
    dsp_write((u8)(rate >> 8));
    dsp_write((u8)rate);
    dsp_write(0xB6);
    dsp_write(0x30);
    dsp_write((u8)(frames * 2 - 1));            /* a half's samples less one */
    dsp_write((u8)((frames * 2 - 1) >> 8));
    kprintf("GLOS-SB start rate=%u frames=%u buf=%x\n", rate, frames, sb.phys);
    return 0;
}

void sb16_stop(void)
{
    if (!sb.running)
        return;
    dsp_write(0xD5);
    dsp_write(0xD9);
    dma_stop();
    sb.running = 0;
    dsp_reset();
    vm_unclaim_irq(sb.irq);
    kprintf("GLOS-SB stop irqs=%u\n", sb.irqs);
}

int sb16_running(void) { return sb.running; }
u32 sb16_irqs(void) { return sb.irqs; }
