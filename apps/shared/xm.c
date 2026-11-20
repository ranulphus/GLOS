/* FastTracker 2 modules played into samples (xm.h), for the Player (phase F).
 *
 * An XM (Triton's FastTracker 2, 1994): a header with the song's name, its channels (up to 32), its first
 * speed and tempo and the order its patterns are played in; patterns of 1 to 256 rows, packed; and instruments,
 * each with up to 16 samples (which of them a note plays is the instrument's to say), two envelopes (how loud
 * and where between the sides a note is as it goes on), a vibrato of its own and a fade for when the key is let
 * go. Samples are 8 or 16 bits, stored as each one's difference from the one before, and loop forwards or back
 * and forth.
 *
 * It is played in ticks as a MOD is (a row lasts `speed` ticks, a tick 2.5 / tempo seconds). A note's pitch is
 * a period: in most songs a "linear" one, 64 to the semitone (the rate is 8363 * 2^((4608 - period) / 768)
 * samples a second); in the others an Amiga one four times as fine (8363 * 1712 / period).
 *
 * Written from the format's public descriptions (FastTracker's own XM.TXT and what is told of how it plays).
 * All of it is whole numbers. Whatever the file's bytes are, nothing is read outside it. */
#include <stdlib.h>
#include <string.h>

#include "trkmix.h"
#include "xm.h"

#define MAXCH 32
#define MAXINS 128
#define MAXSMP 32                       /* an instrument's samples (FastTracker's own make 16 at most) */
#define PERIOD_MAX 32000
#define ROOT768 1074711351ul            /* 2^(1/768), times 2^30 */
#define NO_FINE 1000

static const short sine[64] = {
    0, 24, 49, 74, 97, 120, 141, 161, 180, 197, 212, 224, 235, 244, 250, 253, 255, 253, 250, 244, 235, 224, 212, 197, 180, 161,
    141, 120, 97, 74, 49, 24, 0, -24, -49, -74, -97, -120, -141, -161, -180, -197, -212, -224, -235, -244, -250, -253, -255,
    -253, -250, -244, -235, -224, -212, -197, -180, -161, -141, -120, -97, -74, -49, -24 };

struct xsample {
    struct trk_sample smp;
    int volume, finetune, pan, relnote;
};

struct xenv {
    unsigned short x[12];               /* its points: the tick, and the value there (0 to 64) */
    unsigned char y[12];
    int n, sustain, loop_start, loop_end;
    int on, held, looped;
};

struct xinst {
    unsigned char map[96];              /* which of its samples each note plays */
    struct xenv vol, pan;
    int vib_type, vib_sweep, vib_depth, vib_rate, fadeout;
    unsigned nsamples;
    struct xsample *smp;
};

struct chan {
    struct trk_voice v;
    const struct xinst *ins;
    const struct xsample *smp;
    int finetune;
    long period, target;                /* period 0: nothing struck yet */
    int volume, pan;                    /* 0 to 64; 0 (left) to 255 */
    int keyoff;                         /* the key let go */
    long fade;                          /* 32768 down to 0 from then on */
    int venv, penv;                     /* where in the envelopes: ticks */
    int avib_pos, avib_tick;            /* the instrument's own vibrato */
    int note, inst, vol, fx, param;     /* this row's cell */
    int vibrato;                        /* this row has it */
    int vib_speed, vib_depth, vib_pos, vib_wave;
    int trem_speed, trem_depth, trem_pos, trem_wave;
    int retrig, tremor, silent;         /* ticks since struck again (R); into the tremor's turn (T), and in its
                                           silent part */
    int loop_row, loop_count;           /* E6x */
    int wait;                           /* EDx: the tick the cell waits for (0: none) */
    /* the commands' last numbers */
    int m_up, m_down, m_tone, m_slide, m_fine_up, m_fine_down, m_vol_up, m_vol_down, m_xfine_up, m_xfine_down, m_offset,
        m_global, m_pan, m_retrig, m_tremor;
};

struct state {
    struct chan ch[MAXCH];
    int order, row, tick, speed, tempo, global;
    int jump, brk, loop_to;
    int delay, again;                   /* EEx: times the row is still to be gone through again; in one now */
    int begun, ended, stop;
    int flow;                           /* only where the song goes is wanted (its length): no voices */
    int next_order, next_row;           /* the row whose cells begin at next_at in its pattern (next_row 0: none) */
    unsigned long next_at;
    unsigned long left, frac, frames, ticks;
    unsigned char seen[256 * 256 / 8];
};

struct xm {
    unsigned char *data;
    unsigned long size;
    unsigned rate, channels, norders, nins, npat;
    int linear, speed0, tempo0, smooth;
    long gain;
    unsigned char orders[256];
    unsigned short rows[256];           /* each pattern's */
    unsigned long pat_at[256];          /* where its packed rows begin (0: none: it is empty), and their length */
    unsigned short pat_len[256];
    struct xinst ins[MAXINS + 1];
    unsigned long pow2[768];            /* 2^(i / 768), times 2^30 */
    char title[21];
    unsigned long total;
    struct state s;
};

static unsigned le16(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }
static unsigned long le32(const unsigned char *p) { return le16(p) | (unsigned long)le16(p + 2) << 16; }

static void reset(const struct xm *m, struct state *s)
{
    unsigned c;
    memset(s, 0, sizeof *s);
    s->speed = m->speed0, s->tempo = m->tempo0, s->global = 64;
    s->jump = s->brk = s->loop_to = -1;
    for (c = 0; c < m->channels; c++) {
        s->ch[c].pan = 128;
        s->ch[c].fade = 32768;
    }
}

static void env_read(struct xenv *e, const unsigned char *points, unsigned n, unsigned sustain, unsigned from, unsigned to,
                     unsigned type)
{
    unsigned i;
    for (i = 0; i < 12; i++) {
        unsigned y = le16(points + 4 * i + 2);
        e->x[i] = (unsigned short)le16(points + 4 * i);
        e->y[i] = (unsigned char)(y > 64 ? 64 : y);
    }
    e->n = (int)n, e->sustain = (int)sustain, e->loop_start = (int)from, e->loop_end = (int)to;
    e->on = (type & 1) && n >= 1 && n <= 12;
    e->held = e->on && (type & 2) && sustain < n;
    e->looped = e->on && (type & 4) && from <= to && to < n;
}

void xm_free(struct xm *m)
{
    unsigned i;
    if (!m)
        return;
    for (i = 0; i <= MAXINS; i++)
        free(m->ins[i].smp);
    free(m);
}

struct xm *xm_open(unsigned char *data, unsigned long size, unsigned rate, int *err)
{
    struct xm *m;
    unsigned long at, hsize, x;
    unsigned i, k, speed, tempo;
    int dummy;
    if (!err)
        err = &dummy;
    *err = XM_E_KIND;
    if (size < 80 || rate < 4000 || rate > 96000 || memcmp(data, "Extended Module: ", 17) || le16(data + 58) < 0x0104)
        return NULL;
    *err = XM_E_MEMORY;
    if (!(m = calloc(1, sizeof *m)))
        return NULL;
    m->data = data, m->size = size, m->rate = rate, m->smooth = 1;
    for (i = 0; i < 20 && data[17 + i]; i++)
        m->title[i] = data[17 + i] >= 32 && data[17 + i] < 127 ? (char)data[17 + i] : ' ';
    while (i && m->title[i - 1] == ' ')
        i--;
    m->title[i] = 0;
    for (x = 1ul << 30, i = 0; i < 768; i++) {
        m->pow2[i] = x;
        x = (unsigned long)((unsigned long long)x * ROOT768 >> 30);
    }
    *err = XM_E_DAMAGED;
    hsize = le32(data + 60);
    m->norders = le16(data + 64), m->channels = le16(data + 68), m->npat = le16(data + 70), m->nins = le16(data + 72);
    m->linear = le16(data + 74) & 1;
    speed = le16(data + 76), tempo = le16(data + 78);
    m->speed0 = speed >= 1 && speed <= 255 ? (int)speed : 6;
    m->tempo0 = tempo >= 32 && tempo <= 255 ? (int)tempo : 125;
    if (hsize < 20 || hsize > size - 60 || m->channels < 1 || m->channels > MAXCH || m->npat > 256 || m->nins > MAXINS
        || m->norders > 256)
        goto bad;
    memcpy(m->orders, data + 80, m->norders < hsize - 20 ? m->norders : hsize - 20);
    m->gain = trk_gain(m->channels);
    at = 60 + hsize;
    for (i = 0; i < m->npat; i++) {
        unsigned long hlen, rows, packed;
        if (size - at < 9)
            goto bad;
        hlen = le32(data + at), rows = le16(data + at + 5), packed = le16(data + at + 7);
        if (hlen < 9 || hlen > size - at || rows < 1 || rows > 256)
            goto bad;
        at += hlen;
        if (packed > size - at)                 /* (cut short: what there is of it) */
            packed = size - at;
        m->rows[i] = (unsigned short)rows;
        m->pat_at[i] = packed ? at : 0, m->pat_len[i] = (unsigned short)packed;
        at += packed;
    }
    for (i = 1; i <= m->nins; i++) {            /* (a file cut short among its instruments: those there are) */
        struct xinst *in = &m->ins[i];
        unsigned char h[263];
        unsigned long isize, shsize, heads;
        unsigned n;
        if (size - at < 29)
            break;
        isize = le32(data + at);
        if (isize < 29)
            isize = 29;
        if (isize > size - at)
            break;
        memset(h, 0, sizeof h);
        memcpy(h, data + at, isize < sizeof h ? isize : sizeof h);
        n = le16(h + 27);
        at += isize;
        if (!n)
            continue;
        if (n > MAXSMP)
            goto bad;
        shsize = le32(h + 29);
        if (shsize < 18 || shsize > 256)
            shsize = 40;
        if ((size - at) / shsize < n)
            break;
        *err = XM_E_MEMORY;
        if (!(in->smp = calloc(n, sizeof *in->smp)))
            goto bad;
        *err = XM_E_DAMAGED;
        in->nsamples = n;
        memcpy(in->map, h + 33, 96);
        env_read(&in->vol, h + 129, h[225], h[227], h[228], h[229], h[233]);
        env_read(&in->pan, h + 177, h[226], h[230], h[231], h[232], h[234]);
        in->vib_type = h[235], in->vib_sweep = h[236], in->vib_depth = h[237] & 15, in->vib_rate = h[238] & 63;
        in->fadeout = (int)le16(h + 239);
        heads = at;
        at += shsize * n;
        for (k = 0; k < n; k++) {               /* the samples' headers, then their sounds one after another */
            const unsigned char *sh = data + heads + shsize * k;
            struct xsample *xs = &in->smp[k];
            unsigned long bytes = le32(sh), from = le32(sh + 4), len = le32(sh + 8), here;
            unsigned type = sh[14], wide = (type & 16) != 0;
            xs->volume = sh[12] > 64 ? 64 : sh[12];
            xs->finetune = (signed char)sh[13];
            xs->pan = sh[15];
            xs->relnote = (signed char)sh[16];
            here = bytes > size - at ? size - at : bytes;
            xs->smp.data = data + at;
            xs->smp.bits16 = (unsigned char)wide;
            xs->smp.length = here >> wide;
            from >>= wide, len >>= wide;
            if ((type & 3) && len && from < xs->smp.length) {
                xs->smp.loop_start = from;
                xs->smp.loop_end = len > xs->smp.length - from ? xs->smp.length : from + len;
                xs->smp.pingpong = (type & 3) >= 2;
            }
            at += here;
        }
    }
    /* All of it is there: the samples from differences to themselves, where they lie. */
    for (i = 1; i <= m->nins; i++)
        for (k = 0; k < m->ins[i].nsamples; k++) {
            struct trk_sample *sp = &m->ins[i].smp[k].smp;
            unsigned char *p = (unsigned char *)sp->data;
            unsigned long j;
            unsigned now = 0;
            if (sp->bits16)
                for (j = 0; j < sp->length; j++) {
                    now = (now + le16(p + 2 * j)) & 0xFFFF;
                    p[2 * j] = (unsigned char)now, p[2 * j + 1] = (unsigned char)(now >> 8);
                }
            else
                for (j = 0; j < sp->length; j++)
                    p[j] = (unsigned char)(now += p[j]);
        }
    reset(m, &m->s);
    *err = XM_OK;
    return m;
bad:
    xm_free(m);
    return NULL;
}

const char *xm_title(const struct xm *m) { return m->title; }
unsigned xm_channels(const struct xm *m) { return m->channels; }
void xm_smooth(struct xm *m, int on) { m->smooth = on; }
unsigned long xm_tell(const struct xm *m) { return m->s.frames; }

/* ---- pitches */

static long period_of(const struct xm *m, int note, int fine)   /* note: 0 is C-0; fine: 128ths of a semitone */
{
    long steps;
    if (note < 0 || note > 118)
        return 0;
    if (m->linear)
        return 7680 - note * 64 - fine / 2;
    steps = (long)note * 128 + fine;            /* 1536 to the octave; C-0 is the period 27392 */
    if (steps < 0)
        steps = 0;
    return (long)((((unsigned long long)27392 << 30) / m->pow2[steps % 1536 / 2]) >> (steps / 1536));
}

static long semitones_up(const struct xm *m, long period, int n)        /* (arpeggio) */
{
    if (m->linear)
        return period - n * 64;
    return (long)((((unsigned long long)period << 30) / m->pow2[n * 64 % 768]) >> (n * 64 / 768));
}

static unsigned long long step_of(const struct xm *m, long period)
{
    unsigned long long f;
    long e, oct;
    if (period <= 0)
        return 0;
    if (!m->linear)
        return trk_step(8363ul * 1712, (unsigned long long)period * m->rate);
    e = 4608 - period;                          /* 768ths of an octave above the 8363 a second of a middle C */
    oct = e >= 0 ? e / 768 : -((767 - e) / 768);
    f = (unsigned long long)8363 * m->pow2[e - oct * 768];      /* samples a second, times 2^30 */
    oct += 2;                                   /* (as 32.32) */
    f = oct >= 0 ? f << oct : oct > -60 ? f >> -oct : 0;
    return f / m->rate;
}

static void bound(struct chan *c)       /* a period that is one stays one */
{
    if (c->period)
        c->period = c->period < 1 ? 1 : c->period > PERIOD_MAX ? PERIOD_MAX : c->period;
    c->volume = c->volume < 0 ? 0 : c->volume > 64 ? 64 : c->volume;
    c->pan = c->pan < 0 ? 0 : c->pan > 255 ? 255 : c->pan;
}

static int wave(int pos, int type)      /* a vibrato's or a tremolo's shape at pos (0 to 255): -255 to 255 */
{
    switch (type & 3) {
    case 1: return 255 - pos * 2;               /* a ramp down */
    case 2: return pos < 128 ? 255 : -255;      /* a square */
    default: return sine[(pos >> 2) & 63];
    }
}

/* ---- envelopes */

static int env_value(const struct xenv *e, int pos)
{
    int i, dx;
    if (pos >= e->x[e->n - 1])
        return e->y[e->n - 1];
    for (i = 0; i < e->n - 2 && pos >= e->x[i + 1]; i++)
        ;
    dx = e->x[i + 1] - e->x[i];
    if (dx <= 0 || pos <= e->x[i])
        return e->y[i];
    return e->y[i] + (e->y[i + 1] - e->y[i]) * (pos - e->x[i]) / dx;
}

static void env_step(const struct xenv *e, int *pos, int let_go)
{
    if (e->held && !let_go && *pos == e->x[e->sustain])
        return;                                 /* (held there while the key is down) */
    if (*pos < 0x7FFF)
        (*pos)++;
    /* its loop; which a key let go leaves where the loop ends at the point the envelope was held at */
    if (e->looped && *pos == e->x[e->loop_end] && !(let_go && e->held && e->sustain == e->loop_end))
        *pos = e->x[e->loop_start];
}

/* ---- notes */

static void key_off(struct chan *c)
{
    c->keyoff = 1;
    if (!c->ins || !c->ins->vol.on)             /* (no envelope to let it die away by: it stops) */
        c->volume = 0;
}

static void key_on(struct chan *c)      /* envelopes, fade and the instrument's vibrato from their beginnings */
{
    c->keyoff = 0;
    c->fade = 32768;
    c->venv = c->penv = 0;
    c->avib_pos = c->avib_tick = 0;
    c->retrig = c->tremor = c->silent = 0;
    if (!(c->vib_wave & 4))
        c->vib_pos = 0;
    if (!(c->trem_wave & 4))
        c->trem_pos = 0;
}

static void own_volume(struct chan *c)  /* the sample's own volume and place */
{
    if (c->smp)
        c->volume = c->smp->volume, c->pan = c->smp->pan;
}

/* A note (1 to 96) of the channel's instrument. slide: to be slid to, not struck. 1: struck. */
static int strike(const struct xm *m, struct chan *c, int note, int slide, int fine)
{
    const struct xsample *xs;
    int real;
    if (!c->ins || c->ins->map[note - 1] >= c->ins->nsamples) {  /* (no such instrument or sample: silence) */
        c->v.smp = 0;
        return 0;
    }
    xs = &c->ins->smp[c->ins->map[note - 1]];
    if (slide && c->v.smp && c->period && c->smp) {
        real = note - 1 + c->smp->relnote;
        if (real >= 0 && real <= 118)
            c->target = period_of(m, real, c->finetune);
        return 0;
    }
    real = note - 1 + xs->relnote;
    if (real < 0 || real > 118)
        return 0;
    c->smp = xs;
    c->finetune = fine != NO_FINE ? fine : xs->finetune;
    c->period = period_of(m, real, c->finetune);
    c->target = 0;
    c->v.smp = xs->smp.length ? &xs->smp : 0;
    c->v.pos = 0;
    return 1;
}

/* A cell's note, instrument and volume column taken: on the row's first tick, or the tick an EDx puts it off
   to. 1: a note was struck. */
static int take(const struct xm *m, struct chan *c, int slide, int fine)
{
    int struck = 0, x = c->vol & 15;
    if (c->inst)
        c->ins = c->inst <= (int)m->nins ? &m->ins[c->inst] : 0;
    if (c->note == 97) {
        if (c->inst)
            own_volume(c);
        key_off(c);
    } else if (c->note >= 1 && c->note <= 96) {
        struck = strike(m, c, c->note, slide, fine);
        if (c->inst) {
            own_volume(c);
            key_on(c);
        }
    } else if (c->inst) {
        own_volume(c);
        key_on(c);
    }
    switch (c->vol >> 4) {
    case 1: case 2: case 3: case 4: case 5:
        if (c->vol <= 0x50)
            c->volume = c->vol - 0x10;
        break;
    case 8:
        c->volume -= x;
        break;
    case 9:
        c->volume += x;
        break;
    case 10:
        if (x)
            c->vib_speed = x;
        break;
    case 11:
        if (x)
            c->vib_depth = x;
        c->vibrato = 1;
        break;
    case 12:
        c->pan = x * 17;
        break;
    case 15:
        if (x)
            c->m_tone = x * 16;
        break;
    }
    return struck;
}

static void slide_volume(struct chan *c, int param)
{
    if (param >> 4)
        c->volume += param >> 4;
    else
        c->volume -= param & 15;
}

static void slide_to(struct chan *c)
{
    long by = (long)c->m_tone * 4;
    if (!c->target || !c->period)
        return;
    if (c->period < c->target)
        c->period = c->period + by > c->target ? c->target : c->period + by;
    else
        c->period = c->period - by < c->target ? c->target : c->period - by;
}

static void again(struct chan *c, int change)   /* R: the sample from its start, its volume changed */
{
    static const signed char add[16] = { 0, -1, -2, -4, -8, -16, 0, 0, 0, 1, 2, 4, 8, 16, 0, 0 };
    if (c->smp && c->period) {
        c->v.smp = c->smp->smp.length ? &c->smp->smp : 0;
        c->v.pos = 0;
    }
    if (change == 6)
        c->volume = c->volume * 2 / 3;
    else if (change == 7)
        c->volume /= 2;
    else if (change == 14)
        c->volume = c->volume * 3 / 2;
    else if (change == 15)
        c->volume *= 2;
    else
        c->volume += add[change];
}

/* A row's cells into the channels: the pattern unpacked to that row. */
static void cells(const struct xm *m, struct state *s)
{
    unsigned pat = m->orders[s->order], k;
    const unsigned char *base, *p, *end;
    int r = 0;
    for (k = 0; k < m->channels; k++)
        s->ch[k].note = s->ch[k].inst = s->ch[k].vol = s->ch[k].fx = s->ch[k].param = 0;
    if (pat >= m->npat || !m->pat_at[pat])
        return;
    base = p = m->data + m->pat_at[pat], end = p + m->pat_len[pat];
    if (s->next_row && s->next_order == s->order && s->next_row <= s->row)      /* (rows mostly come in turn) */
        r = s->next_row, p = base + s->next_at;
    for (; r <= s->row; r++)
        for (k = 0; k < m->channels; k++) {
            int note = 0, inst = 0, vol = 0, fx = 0, param = 0;
            unsigned b;
            if (p >= end)
                goto out;
            b = *p++;
            if (b & 0x80) {                     /* only those of its five bytes that say something */
                if ((b & 1) && p < end)
                    note = *p++;
                if ((b & 2) && p < end)
                    inst = *p++;
                if ((b & 4) && p < end)
                    vol = *p++;
                if ((b & 8) && p < end)
                    fx = *p++;
                if ((b & 16) && p < end)
                    param = *p++;
            } else {
                if (end - p < 4) {
                    p = end;
                    goto out;
                }
                note = (int)b, inst = p[0], vol = p[1], fx = p[2], param = p[3];
                p += 4;
            }
            if (r == s->row) {
                struct chan *c = &s->ch[k];
                c->note = note, c->inst = inst, c->vol = vol, c->fx = fx, c->param = param;
            }
        }
out:
    s->next_order = s->order, s->next_row = r, s->next_at = (unsigned long)(p - base);
}

static void row(const struct xm *m, struct state *s)    /* the row's first tick */
{
    unsigned k;
    cells(m, s);
    for (k = 0; k < m->channels; k++) {
        struct chan *c = &s->ch[k];
        int fx = c->fx, param = c->param, x = param >> 4, y = param & 15, struck;
        c->vibrato = fx == 4 || fx == 6;
        c->wait = 0;
        if (fx == 14 && x == 13 && y) {         /* EDx: all of the cell waits for tick x (which never comes, if
                                                   the row has not so many) */
            if (y < s->speed)
                c->wait = y;
            continue;
        }
        struck = take(m, c, fx == 3 || fx == 5 || (c->vol >> 4) == 15, fx == 14 && x == 5 ? y * 16 - 128 : NO_FINE);
        switch (fx) {
        case 1:                                 /* up in pitch */
            if (param)
                c->m_up = param;
            break;
        case 2:
            if (param)
                c->m_down = param;
            break;
        case 3:                                 /* to the note */
            if (param)
                c->m_tone = param;
            break;
        case 4:                                 /* vibrato: each half of its number remembered by itself */
            if (x)
                c->vib_speed = x;
            if (y)
                c->vib_depth = y;
            break;
        case 5:                                 /* 3 goes on, 4 goes on: with A's slide */
        case 6:
        case 10:
            if (param)
                c->m_slide = param;
            break;
        case 7:                                 /* tremolo */
            if (x)
                c->trem_speed = x;
            if (y)
                c->trem_depth = y;
            break;
        case 8:
            c->pan = param;
            break;
        case 9:                                 /* the note from further into its sample (past its end: not at all) */
            if (param)
                c->m_offset = param;
            if (struck && c->v.smp) {
                unsigned long off = (unsigned long)c->m_offset * 256;
                if (off >= c->v.smp->length)
                    c->v.smp = 0;
                else
                    c->v.pos = (unsigned long long)off << 32;
            }
            break;
        case 11:                                /* B: to an order */
            s->jump = param;
            s->brk = -1;
            break;
        case 12:
            c->volume = param > 64 ? 64 : param;
            break;
        case 13:                                /* D: to a row of the next (two decimal digits) */
            s->brk = x * 10 + y;
            break;
        case 14:
            switch (x) {
            case 1:                             /* finely up, once */
                if (y)
                    c->m_fine_up = y;
                if (c->period)
                    c->period -= c->m_fine_up * 4;
                break;
            case 2:
                if (y)
                    c->m_fine_down = y;
                if (c->period)
                    c->period += c->m_fine_down * 4;
                break;
            case 4:
                c->vib_wave = y;
                break;
            case 6:                             /* rows gone through again; E60 says from where */
                if (!y)
                    c->loop_row = s->row;
                else if (!c->loop_count)
                    c->loop_count = y, s->loop_to = c->loop_row;
                else if (--c->loop_count)
                    s->loop_to = c->loop_row;
                break;
            case 7:
                c->trem_wave = y;
                break;
            case 8:
                c->pan = y * 17;
                break;
            case 10:                            /* the volume finely up, once */
                if (y)
                    c->m_vol_up = y;
                c->volume += c->m_vol_up;
                break;
            case 11:
                if (y)
                    c->m_vol_down = y;
                c->volume -= c->m_vol_down;
                break;
            case 12:                            /* cut at tick x */
                if (!y)
                    c->volume = 0;
                break;
            case 14:                            /* the row so many times more */
                s->delay = y;
                break;
            }
            break;
        case 15:                                /* F: the speed, or (from 32) the tempo; F00 stops the song */
            if (!param)
                s->stop = 1;
            else if (param < 32)
                s->speed = param;
            else
                s->tempo = param;
            break;
        case 16:                                /* G: the song's volume */
            s->global = param > 64 ? 64 : param;
            break;
        case 17:                                /* H: ... slid */
            if (param)
                c->m_global = param;
            break;
        case 20:                                /* K: the key let go at a tick */
            if (!param)
                key_off(c);
            break;
        case 21:                                /* L: the envelopes' place */
            c->venv = c->penv = param;
            break;
        case 25:                                /* P: the place slid */
            if (param)
                c->m_pan = param;
            break;
        case 27:                                /* R: struck again every y ticks, its volume changed by x */
            if (x)
                c->m_retrig = (c->m_retrig & 15) | x << 4;
            if (y)
                c->m_retrig = (c->m_retrig & 0xF0) | y;
            break;
        case 29:                                /* T: tremor */
            if (param)
                c->m_tremor = param;
            break;
        case 33:                                /* X1x, X2x: extra finely up and down, once */
            if (x == 1) {
                if (y)
                    c->m_xfine_up = y;
                if (c->period)
                    c->period -= c->m_xfine_up;
            } else if (x == 2) {
                if (y)
                    c->m_xfine_down = y;
                if (c->period)
                    c->period += c->m_xfine_down;
            }
            break;
        }
        bound(c);
    }
}

/* A tick after the row's first (and each tick of a row gone through again by EEx). */
static void effects(const struct xm *m, struct state *s)
{
    unsigned k;
    for (k = 0; k < m->channels; k++) {
        struct chan *c = &s->ch[k];
        int fx = c->fx, param = c->param, x = param >> 4, y = param & 15, v = c->vol & 15;
        if (c->wait) {                          /* EDx: the cell's tick has come? */
            if (s->tick == c->wait) {
                c->wait = 0;
                take(m, c, 0, NO_FINE);
                bound(c);
            }
            continue;
        }
        switch (c->vol >> 4) {
        case 6:
            c->volume -= v;
            break;
        case 7:
            c->volume += v;
            break;
        case 13:
            c->pan -= v;
            break;
        case 14:
            c->pan += v;
            break;
        case 15:
            if (fx != 3 && fx != 5)
                slide_to(c);
            break;
        }
        if (c->vibrato)
            c->vib_pos = (c->vib_pos + c->vib_speed * 4) & 255;
        switch (fx) {
        case 1:
            if (c->period)
                c->period -= c->m_up * 4;
            break;
        case 2:
            if (c->period)
                c->period += c->m_down * 4;
            break;
        case 3:
            slide_to(c);
            break;
        case 5:
            slide_to(c);
            slide_volume(c, c->m_slide);
            break;
        case 6:
        case 10:
            slide_volume(c, c->m_slide);
            break;
        case 7:
            c->trem_pos = (c->trem_pos + c->trem_speed * 4) & 255;
            break;
        case 14:
            if (x == 9 && y && s->tick % y == 0)        /* E9x: struck again every x ticks */
                again(c, 0);
            else if (x == 12 && s->tick == y)
                c->volume = 0;
            break;
        case 17:
            s->global += c->m_global >> 4 ? c->m_global >> 4 : -(c->m_global & 15);
            s->global = s->global < 0 ? 0 : s->global > 64 ? 64 : s->global;
            break;
        case 20:
            if (s->tick == param)
                key_off(c);
            break;
        case 25:
            c->pan += c->m_pan >> 4 ? c->m_pan >> 4 : -(c->m_pan & 15);
            break;
        case 27:
            if ((c->m_retrig & 15) && ++c->retrig >= (c->m_retrig & 15)) {
                c->retrig = 0;
                again(c, c->m_retrig >> 4);
            }
            break;
        case 29:                                /* on for x + 1 ticks, off for y + 1 */
            c->tremor = (c->tremor + 1) % ((c->m_tremor >> 4) + (c->m_tremor & 15) + 2);
            c->silent = c->tremor > c->m_tremor >> 4;
            break;
        }
        bound(c);
    }
}

static unsigned rows_of(const struct xm *m, int order)  /* (an order that names no pattern: an empty one) */
{
    return m->orders[order] < m->npat ? m->rows[m->orders[order]] : 64;
}

static void advance(const struct xm *m, struct state *s)        /* the next row */
{
    unsigned k;
    if (s->delay) {                             /* EEx: this row again */
        s->delay--;
        s->again = 1;
        return;
    }
    s->again = 0;
    if (s->stop) {
        s->ended = 1;
        return;
    }
    if (s->loop_to >= 0) {
        s->row = s->loop_to;
        s->loop_to = -1;
        s->jump = s->brk = -1;
        return;
    }
    if (s->jump >= 0 || s->brk >= 0) {
        s->order = s->jump >= 0 ? s->jump : s->order + 1;
        s->row = s->brk >= 0 ? s->brk : 0;
        s->jump = s->brk = -1;
    } else if (++s->row >= (int)rows_of(m, s->order)) {
        s->row = 0;
        s->order++;
    } else {
        return;                                 /* (the same pattern still: its loops stand) */
    }
    for (k = 0; k < m->channels; k++)
        s->ch[k].loop_row = s->ch[k].loop_count = 0;
    if (s->order >= (int)m->norders)
        s->ended = 1;
    else if (s->row >= (int)rows_of(m, s->order))
        s->row = 0;
}

/* The instrument's own vibrato this tick: what it adds to the period. */
static long auto_vibrato(struct chan *c)
{
    const struct xinst *in = c->ins;
    int w, amp = in->vib_depth;
    if (in->vib_sweep && c->avib_tick < in->vib_sweep)
        amp = amp * c->avib_tick++ / in->vib_sweep;
    switch (in->vib_type & 3) {
    case 1: w = c->avib_pos < 128 ? 64 : -64; break;
    case 2: w = 64 - c->avib_pos / 2; break;
    case 3: w = c->avib_pos / 2 - 64; break;
    default: w = sine[(c->avib_pos >> 2) & 63] / 4; break;
    }
    c->avib_pos = (c->avib_pos + in->vib_rate) & 255;
    return (long)w * amp / 64;
}

/* The next tick begun: the row's cells or the tick's effects, then what each channel sounds like, and how many
   frames the tick is. */
static void tick(const struct xm *m, struct state *s)
{
    unsigned long long len;
    unsigned k;
    if (!s->begun) {
        s->begun = 1;
        if (!m->norders) {
            s->ended = 1;
            return;
        }
    } else if (++s->tick >= s->speed) {
        s->tick = 0;
        advance(m, s);
        if (s->ended)
            return;
    }
    if (!s->tick && !s->again) {
        unsigned bit = (unsigned)s->order * 256 + (unsigned)s->row;
        int looping = 0;
        for (k = 0; k < m->channels; k++)
            looping |= s->ch[k].loop_count;
        /* a row played before, and not by a loop's leave: the song has gone round */
        if ((s->seen[bit / 8] & (1 << (bit % 8))) && !looping) {
            s->ended = 1;
            return;
        }
        s->seen[bit / 8] |= (unsigned char)(1 << (bit % 8));
        row(m, s);
    } else {
        effects(m, s);
    }
    if (++s->ticks > 500000ul) {                /* (never by itself: enough) */
        s->ended = 1;
        return;
    }
    for (k = 0; k < m->channels && !s->flow; k++) {
        struct chan *c = &s->ch[k];
        long p = c->period, loud;
        int v = c->volume, env = 64, pan = c->pan;
        if (c->vibrato)
            p += wave(c->vib_pos, c->vib_wave) * c->vib_depth / 32;
        if (!c->fx && c->param && p && s->tick) {       /* arpeggio: the note, then y and x semitones above */
            int t = (s->speed - s->tick) % 3;
            p = semitones_up(m, p, t == 1 ? c->param >> 4 : t == 2 ? c->param & 15 : 0);
        }
        if (c->fx == 7)
            v += wave(c->trem_pos, c->trem_wave) * c->trem_depth / 64;
        if (c->fx == 29 && c->silent)
            v = 0;
        v = v < 0 ? 0 : v > 64 ? 64 : v;
        if (c->ins) {
            if (c->ins->vol.on) {
                env = env_value(&c->ins->vol, c->venv);
                env_step(&c->ins->vol, &c->venv, c->keyoff);
            }
            if (c->ins->pan.on) {
                int place = env_value(&c->ins->pan, c->penv);
                env_step(&c->ins->pan, &c->penv, c->keyoff);
                pan += (place - 32) * (128 - (pan < 128 ? 128 - pan : pan - 128)) / 32;
                pan = pan < 0 ? 0 : pan > 255 ? 255 : pan;
            }
            if (c->keyoff && (c->fade -= c->ins->fadeout) < 0)
                c->fade = 0;
            if (c->ins->vib_depth && p)
                p += auto_vibrato(c);
        }
        if (p > 0 && c->v.smp) {
            /* (a channel at its loudest: 65536, shared between the two sides) */
            loud = (long)((long long)v * env * s->global * c->fade >> 17);
            pan = pan * 256 / 255;              /* (128 is the middle; 255 all of it on the right) */
            c->v.step = step_of(m, p > PERIOD_MAX ? PERIOD_MAX : p);
            c->v.left = loud * (256 - pan) >> 8;
            c->v.right = loud * pan >> 8;
        } else {
            c->v.step = 0;
            c->v.left = c->v.right = 0;
        }
    }
    len = (unsigned long long)m->rate * 5 * 65536 / ((unsigned)s->tempo * 2) + s->frac;        /* 2.5 / tempo seconds */
    s->left = (unsigned long)(len >> 16);
    s->frac = (unsigned long)(len & 0xFFFF);
}

static void mix(const struct xm *m, struct state *s, short *pcm, unsigned long n)
{
    long acc[2 * 256];
    unsigned k;
    while (n) {
        unsigned long part = n > 256 ? 256 : n;
        memset(acc, 0, part * 2 * sizeof acc[0]);
        for (k = 0; k < m->channels; k++)
            trk_mix(&s->ch[k].v, acc, part, m->smooth);
        trk_out(acc, pcm, part, m->gain);
        pcm += part * 2, n -= part;
    }
}

static unsigned long run(const struct xm *m, struct state *s, short *pcm, unsigned long frames)
{
    unsigned long done = 0;
    unsigned k;
    while (done < frames && !s->ended) {
        unsigned long n;
        if (!s->left) {
            tick(m, s);
            continue;
        }
        n = frames - done < s->left ? frames - done : s->left;
        if (pcm)
            mix(m, s, pcm + done * 2, n);
        else
            for (k = 0; k < m->channels && !s->flow; k++)
                trk_skip(&s->ch[k].v, n);
        s->left -= n, s->frames += n, done += n;
    }
    return done;
}

unsigned long xm_render(struct xm *m, short *pcm, unsigned long frames) { return run(m, &m->s, pcm, frames); }

unsigned long xm_length(struct xm *m)
{
    if (!m->total) {
        struct state *s = malloc(sizeof *s);
        if (!s)
            return 0;
        reset(m, s);
        s->flow = 1;
        while (!s->ended)
            run(m, s, NULL, 1ul << 20);
        m->total = s->frames ? s->frames : 1;
        free(s);
    }
    return m->total;
}

void xm_seek(struct xm *m, unsigned long frame)
{
    reset(m, &m->s);
    run(m, &m->s, NULL, frame);
}
