/* GLOS's virtual Sound Blaster: the DSP and the mixer chip (vsb.h; docs/v1/vsb.md). Written from Creative's
 * documentation of the DSP's commands and the mixer's registers (the Sound Blaster Series Hardware Programming
 * Guide); SBEMU and DOSBox were looked at for what programs do, never for code (the route's licence principle).
 *
 * The ports, counted from the card's base: 4 and 5 the mixer chip's index and data; 6 the DSP's reset; Ah what
 * the DSP has for the program to read; Ch a command or its bytes (read: bit 7 clear when it can take one); Eh
 * bit 7 set when there is something to read, and reading it acknowledges the 8-bit interrupt; Fh acknowledges
 * the 16-bit one.
 *
 * Not done: recording, ADPCM (such a block's interrupt comes at once and nothing sounds), the direct DAC (a
 * sample written by hand, 10h), the DMA identification (E2h). */
#include <string.h>

#include "vsb.h"

/* The mixer chip's volumes are five bits, a step 2 dB: as a gain of 256 for the loudest. */
static const u16 gain5[32] = { 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 3, 3, 4, 5, 6, 8, 10, 13, 16, 20, 26, 32, 41, 51, 64, 81,
                               102, 128, 162, 203, 256 };

static void mixer_reset(struct vsb *s)
{
    memset(s->mix, 0, sizeof s->mix);
    s->mix[0x30] = s->mix[0x31] = 0xC0;         /* master, left and right */
    s->mix[0x32] = s->mix[0x33] = 0xC0;         /* voice: what the DSP plays */
    s->mix[0x34] = s->mix[0x35] = 0xC0;         /* the synthesiser */
    s->mix[0x3C] = 0x1F;
    s->mix[0x44] = s->mix[0x45] = s->mix[0x46] = s->mix[0x47] = 0x80;
}

/* The older cards' registers: two volumes of four bits in a byte, kept in the SB16's own (five bits, at the
   register's top). `at`: the SB16's left register. */
static int legacy(unsigned index)
{
    switch (index) {
    case 0x04: return 0x32;
    case 0x22: return 0x30;
    case 0x26: return 0x34;
    case 0x28: return 0x36;
    case 0x2E: return 0x38;
    }
    return -1;
}

static u8 mixer_read(struct vsb *s)
{
    unsigned i = s->mix_index;
    int at = legacy(i);
    if (at >= 0)
        return (u8)((s->mix[at] & 0xF0) | (s->mix[at + 1] >> 4));
    switch (i) {
    case 0x80: return (u8)(s->irq == 2 || s->irq == 9 ? 1 : s->irq == 5 ? 2 : s->irq == 7 ? 4 : s->irq == 10 ? 8 : 0);
    case 0x81: return (u8)(1u << s->dma8 | (s->dma16 >= 5 ? 1u << s->dma16 : 0));
    case 0x82: return (u8)((s->irq8 ? 1 : 0) | (s->irq16 ? 2 : 0));
    }
    return s->mix[i];
}

static void mixer_write(struct vsb *s, u8 v)
{
    unsigned i = s->mix_index;
    int at = legacy(i);
    if (i == 0x00) {
        mixer_reset(s);
    } else if (at >= 0) {
        s->mix[at] = (u8)((v & 0xF0) | 0x08);
        s->mix[at + 1] = (u8)((v & 0x0F) << 4 | 0x08);
    } else if (i != 0x80 && i != 0x81 && i != 0x82) {   /* (its interrupt and channels are as BLASTER says) */
        s->mix[i] = v;
    }
}

/* ---- the DSP */

static void push(struct vsb *s, u8 b)
{
    if (s->out_at == s->out_n)
        s->out_at = s->out_n = 0;
    if (s->out_n < sizeof s->out)
        s->out[s->out_n++] = b;
}

static void raise(struct vsb *s, int wide)
{
    if (wide)
        s->irq16 = 1;
    else
        s->irq8 = 1;
    if (s->h.irq)
        s->h.irq(s->h.ctx);
}

static void dsp_reset(struct vsb *s)
{
    s->cmd = s->need = s->have = 0;
    s->out_n = s->out_at = 0;
    s->speaker = 0;
    s->irq8 = s->irq16 = 0;
    s->active = s->paused = s->auto_init = s->stop_after = s->silence = 0;
    s->set_rate = 22050;
    s->block_set = 1;
    s->buf_n = s->buf_at = 0;
    s->primed = s->next_ok = 0;
    s->frac = 0;
    s->played = 0;
    push(s, 0xAA);
}

/* A block begins: `bytes` of it to the interrupt, at the rate set, frames a second. */
static void start(struct vsb *s, int wide, int sign, int stereo, int auto_init, u32 bytes, u32 rate)
{
    s->wide = (u8)wide, s->sign = (u8)sign, s->stereo = (u8)stereo;
    s->auto_init = (u8)auto_init;
    s->stop_after = s->silence = 0;
    s->block = s->left = bytes ? bytes : 1;
    s->rate = rate < 1000 ? 1000 : rate > 96000 ? 96000 : rate;
    s->active = 1;
    s->paused = 0;
    s->buf_n = s->buf_at = 0;
    s->primed = s->next_ok = 0;
    s->frac = 0;
}

/* How many bytes follow a command. */
static int args_of(u8 cmd)
{
    if (cmd >= 0xB0 && cmd <= 0xCF)
        return 3;
    switch (cmd) {
    case 0x10: case 0x40: case 0xE0: case 0xE2: case 0xE4: case 0x04: case 0x0F:
        return 1;
    case 0x14: case 0x16: case 0x17: case 0x24: case 0x41: case 0x42: case 0x48: case 0x74: case 0x75: case 0x76:
    case 0x77: case 0x80: case 0x05: case 0x0E:
        return 2;
    }
    return 0;
}

static void run(struct vsb *s)
{
    static const char copyright[] = "COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992.";
    u8 c = s->cmd, *a = s->arg;
    int pro = (s->mix[0x0E] & 0x02) != 0;       /* the Pro's stereo: the older commands' bytes come in pairs */
    u32 old_rate = s->set_rate / (pro ? 2 : 1), n;
    if (c >= 0xB0 && c <= 0xCF) {               /* the SB16's: Bxh 16 bits, Cxh 8; bit 3 recording, bit 2 auto */
        int wide = c < 0xC0;
        if (c & 0x08)
            return;                             /* (recording: not done) */
        n = ((u32)a[1] | (u32)a[2] << 8) + 1;
        start(s, wide, (a[0] & 0x10) != 0, (a[0] & 0x20) != 0, (c & 0x04) != 0, wide ? n * 2 : n, s->set_rate);
        return;
    }
    switch (c) {
    case 0x14:                                  /* 8 bits, one block */
        start(s, 0, 0, pro, 0, ((u32)a[0] | (u32)a[1] << 8) + 1, old_rate);
        break;
    case 0x1C: case 0x90:                       /* 8 bits, a block after a block (90h: "high speed") */
        start(s, 0, 0, pro, 1, s->block_set, old_rate);
        break;
    case 0x91:                                  /* one block, high speed */
        start(s, 0, 0, pro, 0, s->block_set, old_rate);
        break;
    case 0x16: case 0x17: case 0x74: case 0x75: case 0x76: case 0x77: case 0x1F: case 0x7D: case 0x7F:
    case 0x24: case 0x2C: case 0x98: case 0x99:
        raise(s, 0);                            /* ADPCM, and recording: not done, and the program is not kept waiting */
        break;
    case 0x20:
        push(s, 0x80);                          /* (a sample read by hand: silence) */
        break;
    case 0x40:                                  /* the time constant: 256 less a million over the bytes a second */
        s->set_rate = 1000000u / (256u - a[0]);
        break;
    case 0x41: case 0x42:                       /* the SB16's rate, the high byte first */
        s->set_rate = (u32)a[0] << 8 | a[1];
        break;
    case 0x48:
        s->block_set = ((u32)a[0] | (u32)a[1] << 8) + 1;
        break;
    case 0x80:                                  /* silence, so many samples long, then the interrupt */
        start(s, 0, 0, 0, 0, ((u32)a[0] | (u32)a[1] << 8) + 1, s->set_rate);
        s->silence = 1;
        break;
    case 0xD0: case 0xD5:
        s->paused = 1;
        break;
    case 0xD4: case 0xD6:
        s->paused = 0;
        break;
    case 0xD1:
        s->speaker = 1;
        break;
    case 0xD3:
        s->speaker = 0;
        break;
    case 0xD8:
        push(s, s->speaker ? 0xFF : 0x00);
        break;
    case 0xD9: case 0xDA:                       /* auto-initialise ends with the block under way */
        s->stop_after = 1;
        break;
    case 0xE0:
        push(s, (u8)~a[0]);
        break;
    case 0xE1:                                  /* its version: 4.05, a Sound Blaster 16's */
        push(s, 4);
        push(s, 5);
        break;
    case 0xE3:
        for (n = 0; n < sizeof copyright; n++)  /* (with its NUL) */
            push(s, (u8)copyright[n]);
        break;
    case 0xE4:
        s->test = a[0];
        break;
    case 0xE8:
        push(s, s->test);
        break;
    case 0xF2:                                  /* an interrupt asked for: how programs find which one it is */
        raise(s, 0);
        break;
    case 0xF3:
        raise(s, 1);
        break;
    case 0xF8: case 0x0F:
        push(s, 0);
        break;
    }
}

u8 vsb_in(struct vsb *s, unsigned reg)
{
    switch (reg & 0x0F) {
    case 0x4:
        return s->mix_index;
    case 0x5:
        return mixer_read(s);
    case 0xA:
        if (s->out_at < s->out_n)
            return s->out[s->out_at++];
        return s->out_n ? s->out[s->out_n - 1] : 0xFF;         /* (nothing new: the last again) */
    case 0xC:
        return 0x7F;                            /* (it can always take a byte) */
    case 0xE:
        s->irq8 = 0;
        return s->out_at < s->out_n ? 0xFF : 0x7F;
    case 0xF:
        s->irq16 = 0;
        return 0xFF;
    }
    return 0xFF;
}

void vsb_out(struct vsb *s, unsigned reg, u8 val)
{
    switch (reg & 0x0F) {
    case 0x4:
        s->mix_index = val;
        break;
    case 0x5:
        mixer_write(s, val);
        break;
    case 0x6:                                   /* a 1, then a 0: the DSP anew, and AAh to read */
        if (val & 1)
            s->reset = 1;
        else if (s->reset) {
            s->reset = 0;
            dsp_reset(s);
        }
        break;
    case 0xC:
        if (s->need && s->have < s->need) {     /* a byte of the command under way */
            s->arg[s->have++] = val;
            if (s->have < s->need)
                break;
        } else {
            s->cmd = val;
            s->have = 0;
            s->need = (u8)args_of(val);
            if (s->need)
                break;
        }
        s->need = 0;
        run(s);
        break;
    }
}

void vsb_init(struct vsb *s, const struct vsb_host *h, int irq, int dma8, int dma16)
{
    memset(s, 0, sizeof *s);
    if (h)
        s->h = *h;
    s->irq = (u8)irq, s->dma8 = (u8)dma8, s->dma16 = (u8)dma16;
    mixer_reset(s);
    dsp_reset(s);
    s->out_n = s->out_at = 0;                   /* (nothing to read until the program resets it) */
}

int vsb_playing(const struct vsb *s) { return s->active && !s->paused; }

/* ---- what it plays */

/* A block's last byte has been taken: its interrupt, and the next block or the end. */
static void block_end(struct vsb *s)
{
    raise(s, s->wide);
    if (s->auto_init && !s->stop_after)
        s->left = s->block;
    else
        s->auto_init = s->stop_after = 0;       /* (left stays 0: what is in hand is played, then it stops) */
}

/* The next frame the program plays, as 16 bits signed, left and right: 0 if there is none now (the transfer
   has ended: s->active is 0 then; or the DMA channel is masked, and it may go on later). */
static int frame(struct vsb *s, s16 *f)
{
    u32 fb = (s->wide ? 2u : 1u) * (s->stereo ? 2u : 1u), ch;
    const u8 *p;
    if (s->silence) {
        if (!s->left) {
            s->active = 0;
            return 0;
        }
        f[0] = f[1] = 0;
        if (!--s->left)
            block_end(s);
        return 1;
    }
    if (s->buf_n - s->buf_at < fb) {            /* more of the program's bytes, by the DMA channel */
        u32 keep = s->buf_n - s->buf_at, want = sizeof s->buf - keep, got;
        memmove(s->buf, s->buf + s->buf_at, keep);
        s->buf_at = 0, s->buf_n = keep;
        if (want > s->left)
            want = s->left;
        got = want && s->h.dma ? s->h.dma(s->h.ctx, s->wide ? s->dma16 : s->dma8, s->buf + keep, want) : 0;
        if (got > want)
            got = want;
        s->buf_n += got;
        s->left -= got;
        if (got && !s->left)
            block_end(s);
        if (s->buf_n < fb) {
            if (!s->left)                       /* (a single block, all played) */
                s->active = 0;
            return 0;
        }
    }
    p = s->buf + s->buf_at;
    for (ch = 0; ch < (s->stereo ? 2u : 1u); ch++) {
        s32 v;
        if (s->wide) {
            v = (s16)(u16)(p[0] | p[1] << 8);
            if (!s->sign)
                v -= 32768;
            p += 2;
        } else {
            v = s->sign ? ((s32)(p[0] ^ 0x80) - 128) * 256 : ((s32)p[0] - 128) * 256;
            p++;
        }
        f[ch] = (s16)v;
    }
    if (!s->stereo)
        f[1] = f[0];
    s->buf_at += fb;
    return 1;
}

int vsb_mix(struct vsb *s, s32 *acc, u32 frames, u32 rate)
{
    u32 step, i, gl, gr;
    if (!vsb_playing(s) || !rate)
        return 0;
    step = (u32)(((unsigned long long)s->rate << 16) / rate);
    gl = (u32)gain5[s->mix[0x30] >> 3] * gain5[s->mix[0x32] >> 3] >> 8;
    gr = (u32)gain5[s->mix[0x31] >> 3] * gain5[s->mix[0x33] >> 3] >> 8;
    for (i = 0; i < frames; i++) {
        s32 f, l, r;
        if (!s->primed) {                       /* the first frame */
            if (!frame(s, s->cur))
                break;
            s->primed = 1;
            s->next_ok = 0;
        }
        if (!s->next_ok) {                      /* the frame after it, to go between; none yet: cur is held */
            s->next_ok = (u8)frame(s, s->next);
            if (!s->next_ok)
                s->next[0] = s->cur[0], s->next[1] = s->cur[1];
        }
        while (s->frac >= 0x10000 && s->next_ok) {      /* on to the frames this moment lies between */
            s->cur[0] = s->next[0], s->cur[1] = s->next[1];
            s->frac -= 0x10000;
            s->next_ok = (u8)frame(s, s->next);
            if (!s->next_ok)
                s->next[0] = s->cur[0], s->next[1] = s->cur[1];
        }
        if (s->frac >= 0x10000)                 /* past the last frame there is: its end, or the DMA channel has */
            break;                              /* nothing for it now (masked): it waits */
        f = (s32)(s->frac & 0xFFFF);
        l = s->cur[0] + (((s->next[0] - s->cur[0]) * f) >> 16);
        r = s->cur[1] + (((s->next[1] - s->cur[1]) * f) >> 16);
        acc[i * 2] += (l * (s32)gl) >> 8;
        acc[i * 2 + 1] += (r * (s32)gr) >> 8;
        s->frac += step;
        s->played++;
    }
    return i != 0;                              /* (at a transfer's end s->active went 0 with its last frame's fetch) */
}
