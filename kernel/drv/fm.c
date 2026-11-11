/* GLOS's FM driver (API 2.0's AUDIO MIDI_OUT, port FM; docs/v1/phase-b.md step 10): MIDI channel messages played
 * on an OPL3 (YMF262: 18 two-operator voices, stereo) or an OPL2 (YM3812: 9, mono).
 *
 * The instruments are Freedoom's GENMIDI (fmbank.c), in DMX's format, which the documentation of that format
 * describes: 36 bytes each: flags (1 a fixed pitch, 4 two voices), a fine tuning for the second voice (128: none;
 * in 64ths of a semitone), the fixed note, and two voices of 16 bytes: the modulator's register values (20h, 60h,
 * 80h, E0h, 40h's key scaling, 40h's level), the feedback (C0h), the carrier's six, a spare byte, and the
 * voice's note offset. A patch is written for a player whose notes sound an octave above MIDI's, so 12 is added
 * to what it says. Percussion (channel 10) takes patch 128 + key - 35 at its fixed note.
 *
 * Levels: the velocity, the channel's volume and its expression each add attenuation in the chip's 0.75 dB steps
 * (fm_atten) to the carrier's own, and to the modulator's where the patch adds the two (C0h bit 0).
 * Pitch: fm_fnum has an octave from F sharp in 32nds of a semitone; the block is the octave.
 * Voices: one free the longest is taken; with none free, the one keyed on the longest ago. */
#include <string.h>

#include "fm.h"

#define VOICES 18
#define P_FIXED  1
#define P_DOUBLE 4

struct chan {
    u8 program, volume, expression, pan, sustain, range, rpn_msb, rpn_lsb;
    s16 bend;                                   /* 32nds of a semitone */
};
struct voice {
    u8 on;                                      /* keyed on */
    u8 held;                                    /* ... by the pedal alone: the key is up */
    u8 chan, key, vel, second;
    s16 note;                                   /* what it plays: the key and the patch's offset, or its fixed note */
    const u8 *patch;
    u32 age;                                    /* when it was keyed on or off (fm.clock) */
};
static struct {
    fm_write wr;
    u8 opl3, nvoices;
    u8 away;                                    /* the chip is a program's for now (fm_leave) */
    u32 clock;
    struct chan ch[16];
    struct voice v[VOICES];
} fm;

static const u8 op_slot[9] = { 0, 1, 2, 8, 9, 10, 16, 17, 18 };

static void w(u32 n, u8 reg, u8 v) { fm.wr((u16)((n >= 9 ? 0x100u : 0u) | reg), v); }
static u8 op(u32 n, int carrier) { return (u8)(op_slot[n % 9] + (carrier ? 3 : 0)); }
static const u8 *half(const struct voice *v) { return v->patch + 4 + (v->second ? 16 : 0); }

static void levels(u32 n)
{
    const struct voice *v = &fm.v[n];
    const struct chan *c = &fm.ch[v->chan];
    const u8 *p = half(v);
    u32 at = (u32)fm_atten[v->vel] + fm_atten[c->volume] + fm_atten[c->expression];
    u32 car = (p[12] & 0x3Fu) + at, mod = (p[5] & 0x3Fu) + ((p[6] & 1) ? at : 0);
    w(n, (u8)(0x40 + op(n, 0)), (u8)((p[4] & 0xC0) | (mod > 63 ? 63 : mod)));
    w(n, (u8)(0x40 + op(n, 1)), (u8)((p[11] & 0xC0) | (car > 63 ? 63 : car)));
}

static void pan(u32 n)
{
    const struct voice *v = &fm.v[n];
    u8 p = fm.ch[v->chan].pan, sides = p <= 42 ? 0x10 : p >= 86 ? 0x20 : 0x30;     /* (bit 4 left, bit 5 right) */
    w(n, (u8)(0xC0 + n % 9), (u8)((half(v)[6] & 0x0F) | (fm.opl3 ? sides : 0)));
}

static void pitch(u32 n, int key_on)
{
    const struct voice *v = &fm.v[n];
    s32 q = ((s32)v->note + 12) * 32 + fm.ch[v->chan].bend - 6 * 32;    /* from F sharp -1 (MIDI's note 6) */
    s32 block;
    u32 f;
    if (v->second)
        q += v->patch[2] / 2 - 64;
    while (q < 0)
        q += 384;
    block = q / 384 - 1;
    f = fm_fnum[q % 384];
    if (block < 0)
        f >>= 1, block = 0;
    if (block > 7)
        block = 7;
    w(n, (u8)(0xA0 + n % 9), (u8)f);
    w(n, (u8)(0xB0 + n % 9), (u8)((key_on ? 0x20 : 0) | (block << 2) | (f >> 8)));
}

static void key_off(u32 n)
{
    struct voice *v = &fm.v[n];
    v->on = v->held = 0;
    v->age = ++fm.clock;
    pitch(n, 0);
}

static u32 take(void)
{
    u32 i, best = VOICES;
    for (i = 0; i < fm.nvoices; i++)
        if (!fm.v[i].on && (best == VOICES || fm.v[i].age < fm.v[best].age))
            best = i;
    if (best != VOICES)
        return best;
    for (i = 0, best = 0; i < fm.nvoices; i++)
        if (fm.v[i].age < fm.v[best].age)
            best = i;
    key_off(best);
    return best;
}

static void note_off(u8 ch, u8 key, int again)
{
    u32 i;
    for (i = 0; i < fm.nvoices; i++) {
        struct voice *v = &fm.v[i];
        if (!v->on || v->chan != ch || v->key != key || (v->held && !again))
            continue;
        if (fm.ch[ch].sustain && !again)
            v->held = 1;
        else
            key_off(i);
    }
}

static void note_on(u8 ch, u8 key, u8 vel)
{
    const u8 *patch;
    u32 k, flags;
    if (ch == 9) {
        if (key < 35 || key > 81)
            return;
        patch = fm_bank[128 + key - 35];
    } else {
        patch = fm_bank[fm.ch[ch].program];
    }
    flags = patch[0];
    note_off(ch, key, 1);                       /* (the same key again: its voices first) */
    for (k = 0; k < ((flags & P_DOUBLE) && fm.opl3 ? 2u : 1u); k++) {
        u32 n = take();
        struct voice *v = &fm.v[n];
        const u8 *p;
        u8 m = op(n, 0), c = op(n, 1), wave = fm.opl3 ? 7 : 3;
        v->chan = ch, v->key = key, v->vel = vel, v->second = (u8)k, v->patch = patch;
        p = half(v);
        v->note = (flags & P_FIXED) ? patch[3] : (s16)(key + (s16)(p[14] | p[15] << 8));
        w(n, (u8)(0x20 + m), p[0]); w(n, (u8)(0x60 + m), p[1]); w(n, (u8)(0x80 + m), p[2]); w(n, (u8)(0xE0 + m), p[3] & wave);
        w(n, (u8)(0x20 + c), p[7]); w(n, (u8)(0x60 + c), p[8]); w(n, (u8)(0x80 + c), p[9]); w(n, (u8)(0xE0 + c), p[10] & wave);
        levels(n);
        pan(n);
        v->on = 1, v->held = 0;
        v->age = ++fm.clock;
        pitch(n, 1);
    }
}

static void each(u8 ch, void (*fn)(u32 n))
{
    u32 i;
    for (i = 0; i < fm.nvoices; i++)
        if (fm.v[i].on && fm.v[i].chan == ch)
            fn(i);
}

static void bent(u32 n) { pitch(n, 1); }
static void unheld(u32 n) { if (fm.v[n].held) key_off(n); }

static void controls_reset(struct chan *c)
{
    c->expression = 127, c->sustain = 0, c->bend = 0, c->rpn_msb = c->rpn_lsb = 127;
}

static void control(u8 ch, u8 n, u8 v)
{
    struct chan *c = &fm.ch[ch];
    switch (n) {
    case 6:                                     /* data entry: the bend's range, in semitones */
        if (c->rpn_msb == 0 && c->rpn_lsb == 0)
            c->range = v > 24 ? 24 : v;
        break;
    case 7: c->volume = v; each(ch, levels); break;
    case 10: c->pan = v; each(ch, pan); break;
    case 11: c->expression = v; each(ch, levels); break;
    case 64:
        c->sustain = v >= 64;
        if (!c->sustain)
            each(ch, unheld);
        break;
    case 100: c->rpn_lsb = v; break;
    case 101: c->rpn_msb = v; break;
    case 121:
        controls_reset(c);
        each(ch, unheld), each(ch, levels), each(ch, bent);
        break;
    case 120: case 123:                         /* all sound off, all notes off */
        each(ch, key_off);
        break;
    }
}

void fm_message(u8 status, u8 d1, u8 d2)
{
    u8 ch = status & 0x0F;
    if (!fm.wr || fm.away)
        return;
    d1 &= 0x7F, d2 &= 0x7F;
    switch (status & 0xF0) {
    case 0x80: note_off(ch, d1, 0); break;
    case 0x90:
        if (d2)
            note_on(ch, d1, d2);
        else
            note_off(ch, d1, 0);
        break;
    case 0xB0: control(ch, d1, d2); break;
    case 0xC0: fm.ch[ch].program = d1; break;
    case 0xE0:
        fm.ch[ch].bend = (s16)((((s32)(d1 | d2 << 7) - 8192) * fm.ch[ch].range * 32) / 8192);
        each(ch, bent);
        break;
    }
}

static void quiet(void)                         /* every voice: its key off, both operators at their quietest */
{
    u32 i;
    for (i = 0; i < fm.nvoices; i++) {
        w(i, (u8)(0xB0 + i % 9), 0);
        w(i, (u8)(0x40 + op(i, 0)), 0x3F);
        w(i, (u8)(0x40 + op(i, 1)), 0x3F);
    }
}

static void chip(void)
{
    if (fm.opl3) {
        fm.wr(0x105, 0x01);                     /* the OPL3's registers */
        fm.wr(0x104, 0x00);                     /* no four-operator voices */
    }
    fm.wr(0x01, 0x20);                          /* (an OPL2: the waveforms may be chosen) */
    fm.wr(0x08, 0x40);
    fm.wr(0xBD, 0x00);                          /* no rhythm mode; the shallow tremolo and vibrato */
    quiet();
}

void fm_reset(fm_write wr, int opl3)
{
    u32 i;
    memset(&fm, 0, sizeof fm);
    fm.wr = wr;
    fm.opl3 = opl3 != 0;
    fm.nvoices = opl3 ? 18 : 9;
    chip();
    for (i = 0; i < 16; i++) {
        fm.ch[i].volume = 100, fm.ch[i].pan = 64, fm.ch[i].range = 2;
        controls_reset(&fm.ch[i]);
    }
}

void fm_silence(void)
{
    u32 i;
    if (!fm.wr)
        return;
    for (i = 0; i < fm.nvoices; i++)
        if (fm.v[i].on)
            key_off(i);
}

void fm_leave(void)
{
    if (!fm.wr || fm.away)
        return;
    quiet();
    fm.wr(0x01, 0x00);
    if (fm.opl3)
        fm.wr(0x105, 0x00);
    memset(fm.v, 0, sizeof fm.v);
    fm.away = 1;
}

void fm_back(void)
{
    if (!fm.wr || !fm.away)
        return;
    fm.away = 0;
    chip();
}

void fm_end(void)
{
    fm_leave();
    memset(&fm, 0, sizeof fm);
}

u32 fm_sounding(void)
{
    u32 i, n = 0;
    for (i = 0; i < fm.nvoices; i++)
        n += fm.v[i].on;
    return n;
}
