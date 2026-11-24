/* C-Media CMI8738 (GLOS v1, phase H tier 1, 2026-11-23; docs/v1/hardware-audit.md): the PCI sound chip of bench B
 * (onboard there) and of many boards and cards of 1999-2003, as a card for GLOS's mixer (mixer.c): its playback
 * and recording by bus master, its SB16-like mixer, and its FM synthesiser and MPU-401 UART for midi.c.
 *
 * From C-Media's "CMI-8738/PCI-6CH Series Audio Chip Specification" (bitsavers.org's copy, CMI8738.PDF, sha256
 * a9207f27...a84dc4231): the registers in its I/O space (BAR 0):
 *   00h FUNCTRL0   channels' direction (bit 0/1: 1 records), enable (16/17), reset (18/19)
 *   04h FUNCTRL1   rates (ASFC 12:10 channel 0, DSFC 15:13 channel 1), BREQ (4: bus master), VOICE_EN (3: the
 *                  legacy SB16 and FM on channel 0), UART_EN (2), JYSTK_EN (1)
 *   08h CHFORMAT   CH0FMT 1:0, CH1FMT 3:2 (3: 16-bit stereo)
 *   0Ch INT_HLDCLR CH0_INT_EN (16), CH1_INT_EN (17): 1 lets the channel interrupt, 0 clears it
 *   10h INT_STATUS Chint0 (0), Chint1 (1), UARTint (16), INTR (31)
 *   18h MISC_CTRL  RESET (30: the bus master and DSP engine), FM_EN (19)
 *   22h/23h        the SB16-compatible mixer's data and index
 *   24h            Fmmute (7), Wsmute (6)
 *   40h-4Fh        the MPU-401; 50h-5Fh the FM synthesiser (an OPL3), both also at their legacy ports
 *   80h/88h        a channel's buffer's address; 84h/8Ch its counts less one: the buffer's (15:0) and the
 *                  interrupt's period (31:16), in samples (a 16-bit stereo frame each)
 * Eight rates only: 5512, 11025, 22050, 44100, 8000, 16000, 32000, 48000 a second (another is refused: the mixer
 * would play at the wrong pitch).
 *
 * The legacy SB on channel 0 is turned off while GLOS plays (its virtual SB16, vsbdev.c, is what DOS programs
 * find), and put back as it was when the mixer stops and at glos exit, where both channels are stopped (a bus
 * master that records would go on writing into memory that DOS has back).
 *
 * Log: GLOS-CMI found io=XXXX irq=N rev=R | refused no-irq|no-io; GLOS-CMI start rate=N frames=N; GLOS-CMI stop. */
#include <string.h>
#include "devlist.h"
#include "io.h"
#include "kprintf.h"
#include "midi.h"
#include "mixer.h"
#include "mm.h"
#include "pci.h"
#include "vm.h"

#define FUNCTRL0   0x00
#define FUNCTRL1   0x04
#define CHFORMAT   0x08
#define INT_HLDCLR 0x0C
#define INT_STATUS 0x10
#define MISC_CTRL  0x18
#define SB_DATA    0x22
#define SB_INDEX   0x23
#define MUTES      0x24
#define MPU_PORT   0x40
#define FM_PORT    0x50
#define CH_ADDR(c) (0x80 + (c) * 8)
#define CH_COUNT(c) (0x84 + (c) * 8)

#define F0_ADC(c)  (1u << (c))
#define F0_EN(c)   (1u << (16 + (c)))
#define F0_RST(c)  (1u << (18 + (c)))
#define F1_BREQ    0x10u
#define F1_VOICE   0x08u
#define F1_UART    0x04u
#define F1_JOY     0x02u
#define INT_EN(c)  (1u << (16 + (c)))
#define ST_CH(c)   (1u << (c))
#define ST_UART    (1u << 16)
#define MISC_RESET (1u << 30)
#define MISC_FM    (1u << 19)

static struct {
    int found, running, rec;
    struct pci_dev *pci;
    u16 io;
    u8 irq, claimed;
    u32 f1_was, misc_was;               /* (FUNCTRL1 and MISC_CTRL as they were: the legacy parts put back) */
    u32 legacy_was;                     /* (... and the legacy control register, 14h) */
    u8 *buf, *rbuf;
    u32 phys, rphys, frames, rframes, irqs, half, rhalf;
    mixer_fill fill;
    mixer_got got;
} cm;

static u32 rd32(u32 r) { return inl((u16)(cm.io + r)); }
static void wr32(u32 r, u32 v) { outl((u16)(cm.io + r), v); }

static int rate_code(u32 rate)          /* the chip's code for a rate, or -1 */
{
    static const u32 rates[8] = { 5512, 11025, 22050, 44100, 8000, 16000, 32000, 48000 };
    int i;
    for (i = 0; i < 8; i++)
        if (rates[i] == rate)
            return i;
    return -1;
}

int cmi8738_init(void)
{
    struct pci_dev *pd = pci_find(0x13F6, 0x0111, 0);
    char w[8], r[48];
    u32 cmd;
    if (!pd || dev_pci_off(pd, "cmi8738"))
        return -1;
    if (!(pd->bar[0] & 1) || !(pd->bar[0] & ~3u)) {
        kprintf("GLOS-CMI refused no-io\n");
        dev_note(pci_where(pd, w), "cmi8738", DEVS_REFUSED, "no-io", 0);
        return -1;
    }
    if (!pd->pin || pd->irq == 0 || pd->irq > 15) {
        kprintf("GLOS-CMI refused no-irq\n");
        dev_note(pci_where(pd, w), "cmi8738", DEVS_REFUSED, "no-irq", 0);
        return -1;
    }
    if (mixer_pci_line(pd, "cmi8738", "CMI") != 0)
        return -1;
    cm.pci = pd;
    cm.io = (u16)(pd->bar[0] & 0xFFFCu);
    cm.irq = pd->irq;
    cmd = pci_read32(pd->bus, pd->dev, pd->fn, 0x04);
    pci_write32(pd->bus, pd->dev, pd->fn, 0x04, (cmd & 0xFFFF) | 0x05);       /* (I/O and bus master) */
    cm.f1_was = rd32(FUNCTRL1);
    cm.misc_was = rd32(MISC_CTRL);
    wr32(FUNCTRL0, F0_RST(0) | F0_RST(1));      /* both channels reset, stopped */
    wr32(FUNCTRL0, 0);
    wr32(INT_HLDCLR, rd32(INT_HLDCLR) & ~(INT_EN(0) | INT_EN(1)));
    wr32(MISC_CTRL, cm.misc_was | MISC_FM);     /* the FM synthesiser on (at 50h, and at 388h) */
    wr32(FUNCTRL1, cm.f1_was | F1_UART);        /* the MPU-401 on */
    /* The legacy addresses (14h: VMPU 30:29, FMSEL 25:24) at their first choices, 330h and 388h: an older chip
       has its FM and its MPU-401 there alone, not at 40h and 50h of its own ports (bench B's onboard one, PCI
       revision 10h, 2026-11-23: both read 0 there; midi.c tries these after). */
    cm.legacy_was = rd32(0x14);
    wr32(0x14, cm.legacy_was & ~((3u << 29) | (3u << 24)));
    cm.found = 1;
    pci_claim(pd);                              /* (GLOS's: DOS no longer finds it on the PCI bus) */
    kprintf("GLOS-CMI found io=%x irq=%u rev=%u\n", cm.io, cm.irq, pd->rev);
    ksnprintf(r, sizeof r, "io=%04x-%04x irq=%u", cm.io, cm.io + 0xFF, cm.irq);
    dev_note(pci_where(pd, w), "cmi8738", DEVS_ON, "", r);
    return 0;
}

u16 cmi8738_fm(void) { return cm.found ? (u16)(cm.io + FM_PORT) : 0; }
u16 cmi8738_mpu(void) { return cm.found ? (u16)(cm.io + MPU_PORT) : 0; }
u16 cmi8738_fm_legacy(void) { return cm.found ? 0x388 : 0; }    /* (an older chip's: see the set-up) */
u16 cmi8738_mpu_legacy(void) { return cm.found ? 0x330 : 0; }

/* A channel's interrupt said: cleared (its enable bit to 0 and back to 1). */
static void ack(int c)
{
    u32 v = rd32(INT_HLDCLR);
    wr32(INT_HLDCLR, v & ~INT_EN(c));
    wr32(INT_HLDCLR, v | INT_EN(c));
}

/* The chip's interrupt: each source said and cleared, again until it says none, so that the line is low when
   this returns (an edge-triggered PIC sees no new edge from a line that stayed high). */
static void irq(struct trapframe *tf)
{
    u32 st, rounds;
    for (rounds = 0; rounds < 4 && ((st = rd32(INT_STATUS)) & (ST_UART | ST_CH(0) | ST_CH(1))); rounds++) {
    if (st & ST_UART)
        midi_mpu_drain();
    if (st & ST_CH(1)) {                        /* recording: a half filled */
        ack(1);
        if (cm.rec && cm.got) {
            cm.got((const s16 *)(cm.rbuf + cm.rhalf * cm.rframes * 4), cm.rframes);
            cm.rhalf ^= 1;
        }
    }
    if (st & ST_CH(0)) {                        /* playing: a half played, filled again */
        ack(0);
        if (cm.running) {
            cm.irqs++;
            cm.fill((s16 *)(cm.buf + cm.half * cm.frames * 4), cm.frames, tf);
            cm.half ^= 1;
        }
    }
    }
}

static void claim(void)
{
    if (!cm.claimed) {
        vm_claim_irq(cm.irq, irq);
        cm.claimed = 1;
    }
}

/* A channel set going over its buffer of two halves of `frames` (16-bit stereo), interrupting at each half. */
static void channel(int c, int code, u32 phys, u32 frames, int rec)
{
    u32 f0 = rd32(FUNCTRL0), f1 = rd32(FUNCTRL1), fmt = rd32(CHFORMAT);
    wr32(FUNCTRL0, (f0 & ~F0_EN(c)) | F0_RST(c));
    wr32(FUNCTRL0, f0 & ~(F0_EN(c) | F0_RST(c)));
    f0 = rec ? (f0 | F0_ADC(c)) : (f0 & ~F0_ADC(c));
    f1 = c ? ((f1 & ~0xE000u) | ((u32)code << 13)) : ((f1 & ~0x1C00u) | ((u32)code << 10));
    wr32(FUNCTRL1, (f1 & ~F1_VOICE) | F1_BREQ);         /* (the legacy SB off: channel 0 is GLOS's) */
    wr32(CHFORMAT, (fmt & ~(3u << (c * 2))) | (3u << (c * 2)));
    wr32(CH_ADDR(c), phys);
    wr32(CH_COUNT(c), ((frames - 1) << 16) | (frames * 2 - 1));
    wr32(INT_HLDCLR, rd32(INT_HLDCLR) & ~INT_EN(c));   /* (anything left from before cleared, then on) */
    wr32(INT_HLDCLR, rd32(INT_HLDCLR) | INT_EN(c));
    wr32(FUNCTRL0, (f0 & ~F0_RST(c)) | F0_EN(c));
}

static void halt(int c)
{
    wr32(FUNCTRL0, rd32(FUNCTRL0) & ~F0_EN(c));
    wr32(INT_HLDCLR, rd32(INT_HLDCLR) & ~INT_EN(c));
    wr32(FUNCTRL0, rd32(FUNCTRL0) | F0_RST(c));
    wr32(FUNCTRL0, rd32(FUNCTRL0) & ~F0_RST(c));
}

static void legacy_back(void)           /* neither channel GLOS's: the legacy SB as it was */
{
    if (!cm.running && !cm.rec)
        wr32(FUNCTRL1, (rd32(FUNCTRL1) & ~F1_VOICE) | (cm.f1_was & F1_VOICE));
}

static u8 *buffer(u32 frames, u32 *phys)
{
    return dma_alloc((frames * 8 + 4095) / 4096, phys);        /* (any memory: the chip masters the bus itself) */
}

static int cmi_start(u32 rate, u32 frames, mixer_fill fill)
{
    int code = rate_code(rate);
    if (!cm.found || code < 0 || frames < 16 || frames * 2 > 65536) {
        kprintf("GLOS-CMI error=rate-or-size rate=%u frames=%u\n", rate, frames);
        return -1;
    }
    if (!cm.buf || frames > cm.frames) {
        if (!(cm.buf = buffer(frames, &cm.phys))) {
            kprintf("GLOS-CMI error=dma-buffer\n");
            return -1;
        }
    }
    cm.frames = frames;
    cm.fill = fill;
    cm.half = 0;
    fill((s16 *)cm.buf, frames, 0);
    fill((s16 *)(cm.buf + frames * 4), frames, 0);
    claim();
    cm.running = 1;
    channel(0, code, cm.phys, frames, 0);
    kprintf("GLOS-CMI start rate=%u frames=%u buf=%x\n", rate, frames, cm.phys);
    return 0;
}

static void cmi_stop(void)
{
    if (!cm.found || !cm.running)
        return;
    halt(0);
    cm.running = 0;
    legacy_back();
    kprintf("GLOS-CMI stop irqs=%u st=%x hld=%x f0=%x\n", cm.irqs, rd32(INT_STATUS), rd32(INT_HLDCLR), rd32(FUNCTRL0));
}

static void cmi_kick(void) { irq(0); }

static int cmi_running(void) { return cm.running; }
static u32 cmi_irqs(void) { return cm.irqs; }
static u32 cmi_errors(void) { return 0; }

static int cmi_rec_start(u32 rate, u32 frames, mixer_got got)
{
    int code = rate_code(rate);
    if (!cm.found || code < 0 || frames < 16 || frames * 2 > 65536)
        return -1;
    if (!cm.rbuf || frames > cm.rframes) {
        if (!(cm.rbuf = buffer(frames, &cm.rphys)))
            return -1;
    }
    cm.rframes = frames;
    cm.got = got;
    cm.rhalf = 0;
    claim();
    cm.rec = 1;
    channel(1, code, cm.rphys, frames, 1);
    return 0;
}

static void cmi_rec_stop(void)
{
    if (!cm.found || !cm.rec)
        return;
    halt(1);
    cm.rec = 0;
    legacy_back();
}

/* ---- the chip's mixer: the SB16's registers (30h-3Eh) through 23h (index) and 22h (data); wave and FM also
   muted by 24h's own bits */
static u8 mix_rd(u8 reg)
{
    outb((u16)(cm.io + SB_INDEX), reg);
    return inb((u16)(cm.io + SB_DATA));
}

static void mix_wr(u8 reg, u8 v)
{
    outb((u16)(cm.io + SB_INDEX), reg);
    outb((u16)(cm.io + SB_DATA), v);
}

static u8 from5(u8 reg) { return (u8)((reg & 0xF8) | (reg >> 5)); }     /* five bits as 0 to 255 */

static int cmi_chan(int chan, int set, u8 *left, u8 *right, u8 *mute)
{
    static const u8 reg[MX_CHANNELS] = { 0x30, 0x32, 0x34, 0x36, 0x38, 0x3A };
    static const u8 sw[MX_CHANNELS] = { 0, 0, 0, 0x06, 0x18, 0x01 };   /* their bits in the output switches */
    static const u8 own[MX_CHANNELS] = { 0, 0x40, 0x80, 0, 0, 0 };     /* ... or in 24h (wave, FM) */
    u32 f;
    u8 s, m;
    if (!cm.found || chan < 0 || chan >= MX_CHANNELS)
        return -1;
    f = irq_save();
    s = mix_rd(0x3C);
    m = inb((u16)(cm.io + MUTES));
    if (set) {
        mix_wr(reg[chan], (u8)(*left & 0xF8));
        if (chan != MX_CH_MIC)
            mix_wr((u8)(reg[chan] + 1), (u8)(*right & 0xF8));
        if (sw[chan])
            mix_wr(0x3C, (u8)(*mute ? s & ~sw[chan] : s | sw[chan]));
        if (own[chan])
            outb((u16)(cm.io + MUTES), (u8)(*mute ? m | own[chan] : m & ~own[chan]));
    }
    *left = from5(mix_rd(reg[chan]));
    *right = chan == MX_CH_MIC ? *left : from5(mix_rd((u8)(reg[chan] + 1)));
    *mute = (u8)((sw[chan] && !(mix_rd(0x3C) & sw[chan])) || (own[chan] && (inb((u16)(cm.io + MUTES)) & own[chan])));
    irq_restore(f);
    return 0;
}

static int cmi_input(int set, int *input)
{
    static const u8 l[4] = { 0x01, 0x10, 0x04, 0x55 }, r[4] = { 0x01, 0x08, 0x02, 0x2B };      /* mic, line, CD, all */
    u32 f, i;
    u8 a, b;
    if (!cm.found || (set && (*input < 0 || *input > MX_IN_MIX)))
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

/* glos exit: both channels stopped (a bus master never left running), the legacy parts as they were. */
void cmi8738_leave(void)
{
    if (!cm.found)
        return;
    halt(0);
    halt(1);
    cm.running = cm.rec = 0;
    wr32(FUNCTRL1, cm.f1_was);
    wr32(MISC_CTRL, cm.misc_was & ~MISC_RESET);
    wr32(0x14, cm.legacy_was);
    kprintf("GLOS-CMI left\n");
}

const struct sound_card cmi8738_card = { "cmi8738", cmi_start, cmi_stop, cmi_running, cmi_irqs, cmi_errors, cmi_chan,
                                         cmi_input, cmi_rec_start, cmi_rec_stop, cmi_kick };
