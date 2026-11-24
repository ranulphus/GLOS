/* An AC'97 codec's mixer (GLOS v1, phase H tier 1, 2026-11-23): the codec register set every AC'97 controller's
 * codec has (Intel's Audio Codec '97 specification, 2.x), behind the controller's own way of reading and writing
 * it (es1371.c's CODEC register; the ICH's and VIA's later). One code for the codec's part of a card for GLOS's
 * mixer (mixer.h's chan and input) and for setting it up:
 *   00h reset; 02h master (6 bits of attenuation a side, 1.5 dB steps, bit 15 mute); 0Eh mic (5 bits of gain,
 *   08h 0 dB, bit 15 mute); 10h line, 12h CD, 18h PCM out (5 bits a side, 08h 0 dB, 00h +12 dB, 1Fh -34.5 dB,
 *   bit 15 mute); 1Ah record select (2:0 right, 10:8 left: 0 mic, 1 CD, 4 line, 5 stereo mix); 1Ch record gain
 *   (4 bits a side, 0 dB at 0); 26h power-down control/status (3:0 the ADC, DAC, analog and Vref ready);
 *   28h/2Ah extended audio ID and control (bit 0 variable rate); 2Ch the front DAC's rate, 32h the ADC's.
 * Levels are GLOS's 0 to 255: the master's 255 is 0 dB, 0 the most attenuation; an input's 255 is 0 dB (no boost,
 * so nothing clips), 0 is -34.5 dB. */
#include "ac97.h"
#include "kprintf.h"
#include "mixer.h"

#define R_RESET   0x00
#define R_MASTER  0x02
#define R_MIC     0x0E
#define R_LINE    0x10
#define R_CD      0x12
#define R_PCM     0x18
#define R_RECSEL  0x1A
#define R_RECGAIN 0x1C
#define R_POWER   0x26
#define R_EXTID   0x28
#define R_EXTCTL  0x2A
#define R_DACRATE 0x2C
#define R_ADCRATE 0x32
#define R_VENDOR1 0x7C
#define R_VENDOR2 0x7E
#define MUTE      0x8000u

/* Reset and made ready: 0, or -1 (no codec answered, or never ready in about half a second). */
int ac97_start(struct ac97 *c)
{
    u32 i;
    c->wr(c->ctx, R_RESET, 0);
    for (i = 0; i < 500 && (c->rd(c->ctx, R_POWER) & 0x0F) != 0x0F; i++)
        c->pause(c->ctx, 1);
    if ((c->rd(c->ctx, R_POWER) & 0x0F) != 0x0F) {
        kprintf("GLOS-AC97 not-ready power=%x\n", c->rd(c->ctx, R_POWER));
        return -1;
    }
    c->vendor = ((u32)c->rd(c->ctx, R_VENDOR1) << 16) | c->rd(c->ctx, R_VENDOR2);
    if (c->vendor == 0xFFFFFFFFu || c->vendor == 0) {   /* (nobody answering: reads all ones or all zeros, the
                                                           power register's "ready" with them; bench F's CT5880
                                                           held in reset, 2026-11-24) */
        kprintf("GLOS-AC97 none vendor=%08x\n", c->vendor);
        return -1;
    }
    c->vra = (c->rd(c->ctx, R_EXTID) & 1) != 0;
    c->wr(c->ctx, R_MASTER, 0x0000);            /* the master at 0 dB, ... */
    c->wr(c->ctx, R_PCM, 0x0808);               /* ... the PCM out at 0 dB, ... */
    c->wr(c->ctx, R_LINE, MUTE | 0x0808);       /* ... the inputs muted to the output, at 0 dB */
    c->wr(c->ctx, R_CD, MUTE | 0x0808);
    c->wr(c->ctx, R_MIC, MUTE | 0x0008);
    c->wr(c->ctx, R_RECSEL, 0x0404);            /* recording the line in, at 0 dB */
    c->wr(c->ctx, R_RECGAIN, 0x0000);
    kprintf("GLOS-AC97 ready vendor=%08x vra=%d\n", c->vendor, c->vra);
    return 0;
}

/* A rate for the codec's DAC and ADC where it has variable rates (a controller that converts the rate itself,
   as the ES1371, leaves the codec at 48 kHz): 0, or -1 (not that rate). */
/* The front DAC's rate and the ADC's, each left alone if 0 (a controller that converts the rate itself keeps the
   DAC at 48000 and records at another: via686.c's VT8233): 0, or -1 if the codec can't. */
int ac97_rates(struct ac97 *c, u32 dac, u32 adc)
{
    if (!c->vra)
        return (!dac || dac == 48000) && (!adc || adc == 48000) ? 0 : -1;
    c->wr(c->ctx, R_EXTCTL, (u16)(c->rd(c->ctx, R_EXTCTL) | 1));
    if (dac)
        c->wr(c->ctx, R_DACRATE, (u16)dac);
    if (adc)
        c->wr(c->ctx, R_ADCRATE, (u16)adc);
    return (!dac || c->rd(c->ctx, R_DACRATE) == (u16)dac) ? 0 : -1;
}

int ac97_rate(struct ac97 *c, u32 rate) { return ac97_rates(c, rate, rate); }

static u8 level_of(u32 v, u32 bits, int gain)   /* a register's field as GLOS's 0 to 255 */
{
    u32 max = (1u << bits) - 1;
    if (gain)                                   /* (an input: 08h 0 dB up to 1Fh) */
        return (u8)(v <= 8 ? 255 : (31 - v) * 255 / 23);
    return (u8)((max - (v & max)) * 255 / max);
}

static u32 field_of(u8 level, u32 bits, int gain)
{
    u32 max = (1u << bits) - 1;
    if (gain)
        return 31 - (u32)level * 23 / 255;
    return max - (u32)level * max / 255;
}

int ac97_chan(struct ac97 *c, int chan, int set, u8 *left, u8 *right, u8 *mute)
{
    static const u8 reg[MX_CHANNELS] = { R_MASTER, R_PCM, 0, R_CD, R_LINE, R_MIC };
    u32 v, bits, gain;
    if (chan < 0 || chan >= MX_CHANNELS || !reg[chan])
        return -1;                              /* (no FM synthesiser in an AC'97 codec) */
    bits = chan == MX_CH_MASTER ? 6 : 5;
    gain = chan != MX_CH_MASTER;
    if (set) {
        v = (*mute ? MUTE : 0) | field_of(*right, bits, gain);
        if (chan != MX_CH_MIC)
            v |= field_of(*left, bits, gain) << 8;
        c->wr(c->ctx, reg[chan], (u16)v);
    }
    v = c->rd(c->ctx, reg[chan]);
    *right = level_of(v & 0x3F, bits, gain);
    *left = chan == MX_CH_MIC ? *right : level_of((v >> 8) & 0x3F, bits, gain);
    *mute = (v & MUTE) != 0;
    return 0;
}

int ac97_input(struct ac97 *c, int set, int *input)
{
    static const u16 sel[4] = { 0x0000, 0x0404, 0x0101, 0x0505 };      /* mic, line, CD, the stereo mix */
    u16 v;
    int i;
    if (set && (*input < 0 || *input > MX_IN_MIX))
        return -1;
    if (set)
        c->wr(c->ctx, R_RECSEL, sel[*input]);
    v = c->rd(c->ctx, R_RECSEL) & 0x0707;
    *input = MX_IN_MIX;
    for (i = 0; i < 4; i++)
        if (v == sel[i])
            *input = i;
    return 0;
}
