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
#include "arch.h"
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
    mixer_fill fill;
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
    inb((u16)(sb.base + 0x0F));                 /* (the 16-bit interrupt acknowledged) */
    if (!sb.running)
        return;
    sb.irqs++;
    sb.fill((s16 *)(sb.buf + sb.half * sb.frames * 4), sb.frames, tf);
    sb.half ^= 1;
}

static void dma_stop(void) { outb(0xD4, (u8)(4 | (sb.dma & 3))); }

static int sb16_start(u32 rate, u32 frames, mixer_fill fill)
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
    fill((s16 *)sb.buf, frames, 0);
    fill((s16 *)(sb.buf + frames * 4), frames, 0);
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

static void sb16_stop(void)
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

static int sb16_running(void) { return sb.running; }
static u32 sb16_irqs(void) { return sb.irqs; }

static u32 sb16_errors(void) { return 0; }

/* The card's mixer chip (CT1745), at base+4 (the register) and base+5 (its value). Levels are five bits, in
   bits 7 to 3, a register each side: the master 30h/31h, the voice 32h/33h, MIDI (the FM chip) 34h/35h, the CD
   36h/37h, the line 38h/39h; the microphone 3Ah, one for both. The output switches (3Ch): bit 0 the microphone,
   2 and 1 the CD's left and right, 4 and 3 the line's. The input switches, left 3Dh and right 3Eh: bit 0 the
   microphone, 2/1 the CD, 4/3 the line, 6/5 MIDI. From Creative's hardware programming guide. */
static u8 mix_rd(u8 reg)
{
    outb((u16)(sb.base + 4), reg);
    return inb((u16)(sb.base + 5));
}

static void mix_wr(u8 reg, u8 v)
{
    outb((u16)(sb.base + 4), reg);
    outb((u16)(sb.base + 5), v);
}

static u8 from5(u8 reg) { return (u8)((reg & 0xF8) | (reg >> 5)); }     /* five bits as 0 to 255 */

static int sb16_chan(int chan, int set, u8 *left, u8 *right, u8 *mute)
{
    static const u8 reg[MX_CHANNELS] = { 0x30, 0x32, 0x34, 0x36, 0x38, 0x3A };
    static const u8 sw[MX_CHANNELS] = { 0, 0, 0, 0x06, 0x18, 0x01 };   /* its bits in the output switches */
    u32 f;
    u8 s;
    if (!sb.found || chan < 0 || chan >= MX_CHANNELS)
        return -1;
    f = irq_save();
    s = mix_rd(0x3C);
    if (set) {
        mix_wr(reg[chan], (u8)(*left & 0xF8));
        if (chan != MX_CH_MIC)
            mix_wr((u8)(reg[chan] + 1), (u8)(*right & 0xF8));
        if (sw[chan])
            mix_wr(0x3C, (u8)(*mute ? s & ~sw[chan] : s | sw[chan]));
    }
    *left = from5(mix_rd(reg[chan]));
    *right = chan == MX_CH_MIC ? *left : from5(mix_rd((u8)(reg[chan] + 1)));
    *mute = (u8)(sw[chan] && !(mix_rd(0x3C) & sw[chan]));      /* (no switch: it can't be muted, only turned down) */
    irq_restore(f);
    return 0;
}

static int sb16_input(int set, int *input)
{
    static const u8 l[4] = { 0x01, 0x10, 0x04, 0x55 }, r[4] = { 0x01, 0x08, 0x02, 0x2B };      /* mic, line, CD, all */
    u32 f, i;
    u8 a, b;
    if (!sb.found || (set && (*input < 0 || *input > MX_IN_MIX)))
        return -1;
    f = irq_save();
    if (set) {
        mix_wr(0x3D, l[*input]);
        mix_wr(0x3E, r[*input]);
    }
    a = mix_rd(0x3D) & 0x7F, b = mix_rd(0x3E) & 0x7F;
    *input = MX_IN_MIX;
    for (i = 0; i < 3; i++)
        if (a == l[i] && b == r[i])
            *input = (int)i;
    irq_restore(f);
    return 0;
}

const struct sound_card sb16_card = { "sb16", sb16_start, sb16_stop, sb16_running, sb16_irqs, sb16_errors, sb16_chan,
                                      sb16_input };
