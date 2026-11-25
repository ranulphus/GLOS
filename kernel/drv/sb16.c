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
#include "devlist.h"
#include "glos/bootinfo.h"
#include "arch.h"
#include "io.h"
#include "kprintf.h"
#include "midi.h"
#include "mixer.h"
#include "mm.h"
#include "nic.h"
#include "pnpbus.h"
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
    /* recording (API 2.0's AUDIO IN): 8 bits on the low DMA channel, beside the 16-bit output on the high one */
    u8 dma8, rec;
    u8 midi;                                    /* the card's MPU-401 is GLOS's (midi.c): its interrupts come here */
    u8 *rbuf;                                   /* two halves of unsigned 8-bit stereo */
    u32 rphys, rframes, rhalf, rirqs;
    mixer_got got;
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

/* Its resources, as the Devices list writes them (devlist.h). */
const char *sb16_res(void)
{
    static char r[48];
    if (sb.dma8 <= 3)
        ksnprintf(r, sizeof r, "io=%04x-%04x irq=%u dma=%u,%u", sb.base, sb.base + 15, sb.irq, sb.dma8, sb.dma);
    else
        ksnprintf(r, sizeof r, "io=%04x-%04x irq=%u dma=%u", sb.base, sb.base + 15, sb.irq, sb.dma);
    return r;
}

/* [devices] off names it: not looked at, only noted, with the resources BLASTER gives. */
void sb16_left(const struct bootinfo *bi)
{
    sb.base = (u16)bi->sb_port;
    sb.irq = (u8)bi->sb_irq;
    sb.dma = (u8)bi->sb_dma16;
    sb.dma8 = (u8)(bi->sb_dma8 <= 3 ? bi->sb_dma8 : 0xFF);
    dev_note("sb16", "sb16", DEVS_OFF, "", sb16_res());
}

static int found_at(void);

int sb16_init(const struct bootinfo *bi)
{
    if (!bi->sb_port || bi->sb_irq > 15 || bi->sb_dma16 < 5 || bi->sb_dma16 > 7)
        return -1;
    sb.base = (u16)bi->sb_port;
    sb.irq = (u8)bi->sb_irq;
    sb.dma = (u8)bi->sb_dma16;
    sb.dma8 = (u8)(bi->sb_dma8 <= 3 ? bi->sb_dma8 : 0xFF);
    return found_at();
}

/* With no BLASTER (phase H, 2026-11-23): a Plug and Play Sound Blaster 16 or a clone of one that nothing has set
   up, or that a BIOS has without a word: Creative's own (CTL) and Avance Logic's ALS100 (ALS: the card meant for
   bench C, whose 486 has no PnP BIOS). Its audio device is the maker's one with ports, an interrupt and two DMA
   channels, the second a 16-bit one. Unproven until a bench has such a card (86Box's want their PnP ROMs). */
int sb16_init_pnp(void)
{
    static const unsigned long makers[] = { PNP_VENDOR(PNP_ID('C', 'T', 'L', 0)), PNP_VENDOR(PNP_ID('A', 'L', 'S', 0)) };
    unsigned m;
    if (dev_turned_off("sb16"))
        return -1;
    for (m = 0; m < sizeof makers / sizeof makers[0]; m++) {
        struct pnp_dev *d = 0;
        while ((d = pnpbus_find(makers[m], 1, 1, 2, d)) != 0) {
            u8 hi = d->dma_opt[1] & 0xE0 ? 1 : 0;       /* (its second channel can be 5, 6 or 7) */
            if (!hi || pnpbus_on(d) != 0 || !d->io[0] || !d->irq[0] || d->dma[1] < 5 || d->dma[1] > 7)
                continue;
            sb.base = d->io[0], sb.irq = d->irq[0], sb.dma = d->dma[1];
            sb.dma8 = (u8)(d->dma[0] <= 3 ? d->dma[0] : 0xFF);
            if (found_at() == 0)
                return 0;
        }
    }
    return -1;
}

/* With no BLASTER on a native boot (no DOS, no AUTOEXEC.BAT to say one: the native-dos session's finding,
   2026-11-24): a jumpered Sound Blaster 16, which most are, looked for where one can be, 220h, 240h, 260h, 280h.
   Carefully: a port range a found network card or a Plug and Play device that is on holds is passed over (an
   NE2000 at 240h or 280h would take a DSP reset as a write to its own registers). A DSP that answers 4.xx is an
   SB16: its interrupt and DMA channels from its mixer (80h: bit 0 IRQ 2/9, 1 IRQ 5, 2 IRQ 7, 3 IRQ 10; 81h: bits
   0, 1, 3 the 8-bit channel 0, 1, 3, bits 5-7 the 16-bit 5, 6, 7), as Creative's hardware guide has them. An
   older DSP, or an ESS's (it answers E7h, not an SB16's mixer), is said and passed over. Hosted, BLASTER says. */
static int legacy;
int sb16_legacy(void) { return legacy; }

static int found_at(void);

int sb16_init_legacy(void)
{
    static const u16 ports[] = { 0x220, 0x240, 0x260, 0x280 };
    u32 i;
    if (dev_turned_off("sb16"))
        return -1;
    for (i = 0; i < sizeof ports / sizeof ports[0]; i++) {
        u16 p = ports[i];
        int hi = -1, lo = -1;
        u8 irqs, dmas, irq, dma8, dma16;
        if ((nic0 && nic0->base && nic0->base <= p + 0x0F && nic0->base + 0x1F >= p) || pnpbus_io_busy(p, p + 0x0F)) {
            kprintf("GLOS-SB legacy skip port=%x why=taken\n", p);
            continue;
        }
        sb.base = p;
        if (dsp_reset() != 0)
            continue;
        if (dsp_write(0xE1) != 0 || (hi = dsp_read()) < 0 || (lo = dsp_read()) < 0 || hi != 4) {
            kprintf("GLOS-SB legacy port=%x dsp=%d.%d not-sb16\n", p, hi, lo);
            continue;
        }
        outb((u16)(p + 4), 0x80);
        irqs = inb((u16)(p + 5));
        outb((u16)(p + 4), 0x81);
        dmas = inb((u16)(p + 5));
        irq = irqs & 2 ? 5 : irqs & 8 ? 10 : irqs & 4 ? 7 : irqs & 1 ? 9 : 0;
        dma8 = dmas & 2 ? 1 : dmas & 1 ? 0 : dmas & 8 ? 3 : 0xFF;
        dma16 = dmas & 0x20 ? 5 : dmas & 0x40 ? 6 : dmas & 0x80 ? 7 : 0;
        kprintf("GLOS-SB legacy port=%x irq=%u dma=%u dma16=%u mixer=%02x,%02x\n", p, irq, dma8 == 0xFF ? 0 : dma8,
                dma16, irqs, dmas);
        if (!irq || !dma16)
            continue;
        sb.irq = irq, sb.dma = dma16, sb.dma8 = dma8;
        if (found_at() == 0) {
            legacy = 1;
            return 0;
        }
    }
    return -1;
}

static int found_at(void)
{
    int hi, lo;
    if (dsp_reset() != 0 || dsp_write(0xE1) != 0 || (hi = dsp_read()) < 0 || (lo = dsp_read()) < 0) {
        kprintf("GLOS-SB none port=%x\n", sb.base);
        return -1;
    }
    sb.major = (u8)hi, sb.minor = (u8)lo;
    if (hi < 4) {
        kprintf("GLOS-SB refuse port=%x dsp=%u.%02u reason=not-sb16\n", sb.base, hi, lo);
        dev_note("sb16", "sb16", DEVS_REFUSED, "not-sb16", sb16_res());
        return -1;
    }
    sb.found = 1;
    kprintf("GLOS-SB found port=%x irq=%u dma16=%u dsp=%u.%02u\n", sb.base, sb.irq, sb.dma, hi, lo);
    dev_note("sb16", "sb16", DEVS_ON, "", sb16_res());
    return 0;
}

static void rec_half(void);

/* The card's one interrupt is the 16-bit output's, the 8-bit input's and the MPU-401's: its mixer's register 82h
   says which (bit 1 the 16-bit, bit 0 the 8-bit, bit 2 the MPU-401); each is acknowledged by reading its own
   port. With nothing recording and the MPU-401 not GLOS's it is the output's, as before. */
/* The card's interrupt: what raised it from the mixer chip's status register (82h), read every time, and each
   source acknowledged. (It was read only while recording or MIDI was on: an 8-bit block that ended as a
   recording stopped then raised its interrupt after sb.rec was cleared, was never acknowledged, and held the
   card's line up, so that an edge-triggered PIC saw no more edges and playback stopped for good: the gate's
   audio-486dx2-vbe, 2026-11-22, the Player's stream never mixed after the Sound Recorder had recorded. The
   mixer's register is written with interrupts off everywhere else, so this read can't come between.) */
static void irq(struct trapframe *tf)
{
    u8 st;
    outb((u16)(sb.base + 4), 0x82);
    st = inb((u16)(sb.base + 5)) & 7;
    if (!st)                                    /* (nothing said: the playback's, as a card without the register) */
        st = 2;
    if (st & 4)                                 /* (the MPU-401's: over once its bytes are read) */
        midi_mpu_drain();
    if (st & 1) {
        inb((u16)(sb.base + 0x0E));             /* (the 8-bit interrupt acknowledged, recording or not) */
        if (sb.rec)
            rec_half();
    }
    if (!(st & 2))
        return;
    inb((u16)(sb.base + 0x0F));                 /* (the 16-bit interrupt acknowledged) */
    if (!sb.running)
        return;
    sb.irqs++;
    sb.fill((s16 *)(sb.buf + sb.half * sb.frames * 4), sb.frames, tf);
    sb.half ^= 1;
}

void sb16_midi(int on) { sb.midi = (u8)(on != 0); }

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

/* ---- recording: DSP command CEh (8-bit input, auto-initialised, FIFO on) with mode 20h (stereo, unsigned) and
   a half's bytes less one, at the output's rate (42h: the card has one clock for both ways); the 8237's low
   controller writes the card's bytes to memory (mode 54h + the channel). From Creative's DSP documentation. */
static const u8 page8[4] = { 0x87, 0x83, 0x81, 0x82 };

static void rec_half(void)
{
    static s16 conv[512 * 2];
    const u8 *src = sb.rbuf + sb.rhalf * sb.rframes * 2;
    u32 i, n = sb.rframes > 512 ? 512 : sb.rframes;
    for (i = 0; i < n * 2; i++)
        conv[i] = (s16)(((int)src[i] - 128) << 8);
    sb.rhalf ^= 1;
    sb.rirqs++;
    sb.got(conv, n);
}

static int sb16_rec_start(u32 rate, u32 frames, mixer_got got)
{
    u32 bytes = frames * 2 * 2, ch = sb.dma8;
    if (!sb.found || !sb.running || ch > 3 || ch == 2 || frames > 512)
        return -1;                              /* (no 8-bit channel in BLASTER: no recording) */
    if (!sb.rbuf) {
        u32 phys;
        u8 *p = dma_alloc((bytes + 4095) / 4096, &phys);
        if (!p || phys + bytes > 0x1000000u || (phys >> 16) != ((phys + bytes - 1) >> 16)) {
            kprintf("GLOS-SB error=rec-buffer phys=%x\n", phys);
            return -1;
        }
        sb.rbuf = p, sb.rphys = phys;
    }
    sb.rframes = frames;
    sb.rhalf = 0;
    sb.got = got;
    outb(0x0A, (u8)(4 | ch));                   /* masked */
    outb(0x0C, 0);                              /* the flip-flop */
    outb(0x0B, (u8)(0x54 | ch));                /* single, auto-initialised, the card to memory */
    outb((u16)(ch * 2), (u8)sb.rphys);
    outb((u16)(ch * 2), (u8)(sb.rphys >> 8));
    outb(page8[ch], (u8)(sb.rphys >> 16));
    outb((u16)(ch * 2 + 1), (u8)(bytes - 1));
    outb((u16)(ch * 2 + 1), (u8)((bytes - 1) >> 8));
    outb(0x0A, (u8)ch);                         /* unmasked */
    sb.rec = 1;
    dsp_write(0x42);
    dsp_write((u8)(rate >> 8));
    dsp_write((u8)rate);
    dsp_write(0xCE);
    dsp_write(0x20);
    dsp_write((u8)(frames * 2 - 1));            /* a half's bytes less one */
    dsp_write((u8)((frames * 2 - 1) >> 8));
    kprintf("GLOS-SB record rate=%u frames=%u dma=%u buf=%x\n", rate, frames, ch, sb.rphys);
    return 0;
}

static void sb16_rec_stop(void)
{
    if (!sb.rec)
        return;
    dsp_write(0xD0);                            /* 8-bit DMA paused ... */
    dsp_write(0xDA);                            /* ... and its auto-initialisation left */
    outb(0x0A, (u8)(4 | sb.dma8));
    sb.rec = 0;
    kprintf("GLOS-SB record-stop halves=%u\n", sb.rirqs);
}

static void sb16_stop(void)
{
    if (!sb.running)
        return;
    sb16_rec_stop();
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
                                      sb16_input, sb16_rec_start, sb16_rec_stop, 0 };
