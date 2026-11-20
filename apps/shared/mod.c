/* MOD modules played into samples (mod.h), for the Player (phase F).
 *
 * A MOD (ProTracker's format of 1990, and its descendants with more channels): a title, 31 samples (8 bits,
 * each with a volume, a fine tuning and a loop), the order its patterns are played in, the patterns (64 rows of
 * a cell for each channel: a sample, a note as the Amiga's period, an effect and its number), then the samples'
 * bytes. It is played in ticks: a row lasts `speed` of them (6), a tick 2.5 / tempo seconds (tempo 125: a fiftieth);
 * a note is struck on a row's first tick and the effects work on the others (slides, vibrato, arpeggio ...).
 * A period's pitch is the Amiga's clock over it: 3546895 / period samples a second.
 *
 * Written from the format's public descriptions. All of it is whole numbers: the same samples from every
 * compiler. Whatever the file's bytes are, nothing is read outside it (samples that the file is too short for
 * are cut to what there is). */
#include <stdlib.h>
#include <string.h>

#include "mod.h"

#define MAXCH 32
#define CLOCK 3546895ul                 /* the Amiga's (PAL): a period's frequency is this / period */
#define NOTES 36

static const short base[NOTES] = {      /* three octaves' periods, C-1 to B-3, at no fine tuning */
    856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453, 428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
    214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113 };
/* A fine tuning's step is an eighth of a semitone: the period times 2^(-f/96), here times 65536, for f = 0 to 7
   and then -8 to -1 (as the file's four bits count them). */
static const unsigned long tune[16] = { 65536, 65065, 64596, 64132, 63670, 63212, 62757, 62306,
                                        69433, 68933, 68438, 67945, 67456, 66971, 66489, 66011 };
static const unsigned char sine[32] = { 0, 24, 49, 74, 97, 120, 141, 161, 180, 197, 212, 224, 235, 244, 250, 253,
                                        255, 253, 250, 244, 235, 224, 212, 197, 180, 161, 141, 120, 97, 74, 49, 24 };

struct sample {
    const signed char *data;
    unsigned long length, loop_start, loop_len;         /* bytes (loop_len 0: no loop) */
    int volume, finetune;
    char name[23];
};

struct chan {
    const struct sample *smp;           /* the sample sounding (NULL: none) */
    int named;                          /* the sample its cells named last (1 to 31; 0: none yet) */
    int volume, finetune, pan;          /* pan: 0 left to 256 right */
    int period;                         /* its pitch now, before vibrato and arpeggio */
    int note;                           /* the note it was struck at (its place in the periods; -1: none there) */
    int target, porta;                  /* a slide to a note: where to, how fast */
    int vib_pos, vib_speed, vib_depth, vib_wave;
    int trem_pos, trem_speed, trem_depth, trem_wave;
    int effect, param;
    int offset;                         /* 9xx's last number */
    int loop_row, loop_count;           /* E6x */
    int wait_period;                    /* EDx: the note that waits for its tick (0: none) */
    int out_period, out_volume;         /* what is heard this tick */
    unsigned long long pos, step;       /* in the sample, and a frame's step: 32.32 */
};

struct state {                          /* all that moves as the song plays */
    struct chan ch[MAXCH];
    int order, row, tick, speed, bpm;
    int jump, brk, loop_to;             /* at this row's end: to that order, to that row, back to that row (-1: no) */
    int delay, again;                   /* EEx: the row so many times more; this is such a time */
    int begun, ended;
    unsigned long left;                 /* frames left of this tick */
    unsigned long frac;                 /* what is over of a tick's length, in 65536ths of a frame */
    unsigned long frames;               /* made so far */
    unsigned long ticks;                /* (a song that never ends by itself is ended after very many) */
    unsigned char seen[128 * 64 / 8];   /* the rows played */
};

struct mod {
    const unsigned char *patterns;
    unsigned rate, channels, npat, length;
    unsigned char orders[128];
    struct sample smp[32];
    char title[21];
    short periods[16][NOTES];
    int smooth, low, high;              /* low, high: the periods a slide stops at */
    unsigned long total;                /* its length in frames (0: not yet found) */
    struct state s;
};

static unsigned be16(const unsigned char *p) { return (unsigned)p[0] << 8 | p[1]; }

static void text(char *out, const unsigned char *in, unsigned n)
{
    unsigned i;
    for (i = 0; i < n && in[i]; i++)
        out[i] = in[i] >= 32 && in[i] < 127 ? (char)in[i] : ' ';
    while (i && out[i - 1] == ' ')
        i--;
    out[i] = 0;
}

static void reset(struct mod *m, struct state *s)
{
    unsigned c;
    memset(s, 0, sizeof *s);
    s->speed = 6, s->bpm = 125;
    s->jump = s->brk = s->loop_to = -1;
    for (c = 0; c < m->channels; c++) {
        s->ch[c].pan = (c & 3) == 0 || (c & 3) == 3 ? 64 : 192;        /* left, right, right, left, as the Amiga
                                                                           has them, a quarter of each in the other */
        s->ch[c].note = -1;
    }
}

struct mod *mod_open(const unsigned char *data, unsigned long size, unsigned rate, int *err)
{
    struct mod *m;
    unsigned long at, need;
    unsigned i, f, channels = 0;
    int dummy;
    if (!err)
        err = &dummy;
    *err = MOD_E_KIND;
    if (size < 1084 || rate < 4000 || rate > 96000)
        return NULL;
    {
        const unsigned char *sig = data + 1080;
        if (!memcmp(sig, "M.K.", 4) || !memcmp(sig, "M!K!", 4) || !memcmp(sig, "FLT4", 4) || !memcmp(sig, "4CHN", 4))
            channels = 4;
        else if (!memcmp(sig + 1, "CHN", 3) && sig[0] >= '1' && sig[0] <= '9')
            channels = (unsigned)(sig[0] - '0');
        else if ((!memcmp(sig + 2, "CH", 2) || !memcmp(sig + 2, "CN", 2)) && sig[0] >= '1' && sig[0] <= '3' && sig[1] >= '0'
                 && sig[1] <= '9')
            channels = (unsigned)(sig[0] - '0') * 10 + (unsigned)(sig[1] - '0');
        else if (!memcmp(sig, "CD81", 4) || !memcmp(sig, "OCTA", 4))
            channels = 8;
    }
    if (channels < 1 || channels > MAXCH)
        return NULL;
    *err = MOD_E_MEMORY;
    if (!(m = calloc(1, sizeof *m)))
        return NULL;
    m->rate = rate, m->channels = channels, m->smooth = 1;
    m->low = channels == 4 ? 113 : 28, m->high = channels == 4 ? 856 : 3424;
    text(m->title, data, 20);
    m->length = data[950];
    memcpy(m->orders, data + 952, 128);
    for (i = 0; i < 128; i++)                   /* (every pattern the table names is in the file) */
        if (m->orders[i] >= m->npat)
            m->npat = m->orders[i] + 1u;
    need = 1084 + (unsigned long)m->npat * 64 * channels * 4;
    *err = MOD_E_DAMAGED;
    if (m->length < 1 || m->length > 128 || need > size) {
        free(m);
        return NULL;
    }
    m->patterns = data + 1084;
    at = need;
    for (i = 1; i <= 31; i++) {
        const unsigned char *h = data + 20 + (i - 1) * 30;
        struct sample *sm = &m->smp[i];
        unsigned long length = be16(h + 22) * 2ul, ls = be16(h + 26) * 2ul, ll = be16(h + 28) * 2ul;
        text(sm->name, h, 22);
        sm->finetune = h[24] & 15;
        sm->volume = h[25] > 64 ? 64 : h[25];
        if (length > size - at)                 /* (the file is shorter than it says: what there is) */
            length = size - at;
        sm->data = (const signed char *)data + at;
        sm->length = length;
        at += length;
        if (ll > 2 && ls < length) {            /* (a loop of one word is none) */
            sm->loop_start = ls;
            sm->loop_len = ls + ll > length ? length - ls : ll;
        }
    }
    for (f = 0; f < 16; f++)
        for (i = 0; i < NOTES; i++)
            m->periods[f][i] = (short)(((unsigned long)base[i] * tune[f] + 32768) >> 16);
    reset(m, &m->s);
    *err = MOD_OK;
    return m;
}

void mod_free(struct mod *m) { free(m); }
const char *mod_title(const struct mod *m) { return m->title; }
unsigned mod_channels(const struct mod *m) { return m->channels; }
const char *mod_sample_name(const struct mod *m, unsigned i) { return i >= 1 && i <= 31 ? m->smp[i].name : NULL; }
void mod_smooth(struct mod *m, int on) { m->smooth = on; }
unsigned long mod_tell(const struct mod *m) { return m->s.frames; }

void mod_where(const struct mod *m, int *order, int *pattern, int *row)
{
    int o = m->s.order < (int)m->length ? m->s.order : (int)m->length - 1;
    if (order)
        *order = o;
    if (pattern)
        *pattern = m->orders[o];
    if (row)
        *row = m->s.row;
}

/* ---- a row, and the ticks after it */

static int note_of(int period)          /* the note a cell's period is: its place among the periods (-1: none near) */
{
    int i, best = -1, d = 1 << 20;
    for (i = 0; i < NOTES; i++) {
        int e = period > base[i] ? period - base[i] : base[i] - period;
        if (e < d)
            d = e, best = i;
    }
    return d <= 8 ? best : -1;
}

static int tuned(const struct mod *m, const struct chan *c, int note, int period)
{
    return note >= 0 ? m->periods[c->finetune][note] : period;
}

static void strike(struct chan *c, const struct mod *m, int period, int note)
{
    c->note = note;
    c->period = tuned(m, c, note, period);
    c->smp = c->named ? &m->smp[c->named] : NULL;
    c->pos = 0;
    if (c->vib_wave < 4)
        c->vib_pos = 0;
    if (c->trem_wave < 4)
        c->trem_pos = 0;
}

static int wave(int kind, int pos)      /* a vibrato's or tremolo's wave at pos (0 to 63): -255 to 255 */
{
    int v;
    pos &= 63;
    switch (kind & 3) {
    case 0: v = sine[pos & 31]; break;
    case 1: v = 255 - (pos & 31) * 8; break;            /* a ramp down */
    default: v = 255; break;                            /* a square (and "random", which is one here) */
    }
    return pos < 32 ? v : -v;
}

static void slide_volume(struct chan *c)
{
    if (c->param >> 4)
        c->volume += c->param >> 4;
    else
        c->volume -= c->param & 15;
    c->volume = c->volume < 0 ? 0 : c->volume > 64 ? 64 : c->volume;
}

static void slide_to(struct chan *c)
{
    if (!c->target || !c->period)
        return;
    if (c->period < c->target) {
        c->period += c->porta;
        if (c->period > c->target)
            c->period = c->target;
    } else {
        c->period -= c->porta;
        if (c->period < c->target)
            c->period = c->target;
    }
}

static void row(struct mod *m, struct state *s)         /* the row's cells: its first tick */
{
    const unsigned char *p = m->patterns + ((unsigned long)m->orders[s->order] * 64 + (unsigned)s->row) * m->channels * 4;
    unsigned k;
    for (k = 0; k < m->channels; k++, p += 4) {
        struct chan *c = &s->ch[k];
        int smp = (p[0] & 0xF0) | p[2] >> 4, period = (p[0] & 15) << 8 | p[1], x = p[3], note;
        c->effect = p[2] & 15, c->param = x;
        c->wait_period = 0;
        if (smp >= 1 && smp <= 31) {
            c->named = smp;
            c->volume = m->smp[smp].volume;
            c->finetune = m->smp[smp].finetune;
        }
        if (period) {
            note = note_of(period);
            if (c->effect == 3 || c->effect == 5) {     /* a slide to this note: it is not struck */
                c->target = tuned(m, c, note, period);
            } else if (c->effect == 14 && x >> 4 == 13 && (x & 15)) {
                c->wait_period = period;                /* struck on a later tick */
            } else {
                strike(c, m, period, note);
                if (c->effect == 9) {
                    if (x)
                        c->offset = x;
                    c->pos = (unsigned long long)c->offset << 40;       /* (256 bytes a step) */
                }
            }
        }
        switch (c->effect) {
        case 3:
            if (x)
                c->porta = x;
            break;
        case 4:
            if (x >> 4)
                c->vib_speed = x >> 4;
            if (x & 15)
                c->vib_depth = x & 15;
            break;
        case 7:
            if (x >> 4)
                c->trem_speed = x >> 4;
            if (x & 15)
                c->trem_depth = x & 15;
            break;
        case 8:
            c->pan = x > 255 ? 256 : x + (x >> 7);      /* 0 to 255: left to right */
            break;
        case 11:
            s->jump = x;
            break;
        case 12:
            c->volume = x > 64 ? 64 : x;
            break;
        case 13:
            s->brk = (x >> 4) * 10 + (x & 15);
            if (s->brk > 63)
                s->brk = 0;
            break;
        case 15:
            if (!x)
                s->ended = 1;                           /* (a speed of nothing: the song stands still) */
            else if (x < 32)
                s->speed = x;
            else
                s->bpm = x;
            break;
        case 14:
            switch (x >> 4) {
            case 1: c->period -= x & 15; break;         /* a fine slide up, once */
            case 2: c->period += x & 15; break;
            case 4: c->vib_wave = x & 7; break;
            case 5: c->finetune = x & 15; break;
            case 6:
                if (!(x & 15)) {
                    c->loop_row = s->row;
                } else if (!c->loop_count) {
                    c->loop_count = x & 15;
                    s->loop_to = c->loop_row;
                } else if (--c->loop_count) {
                    s->loop_to = c->loop_row;
                }
                break;
            case 7: c->trem_wave = x & 7; break;
            case 8: c->pan = (x & 15) * 17 + ((x & 15) >> 3); break;
            case 10: c->volume = c->volume + (x & 15) > 64 ? 64 : c->volume + (x & 15); break;
            case 11: c->volume = c->volume - (x & 15) < 0 ? 0 : c->volume - (x & 15); break;
            case 12:
                if (!(x & 15))
                    c->volume = 0;
                break;
            case 14: s->delay = x & 15; break;
            }
            break;
        }
    }
}

static void effects(struct mod *m, struct state *s)     /* a tick after the row's first */
{
    unsigned k;
    for (k = 0; k < m->channels; k++) {
        struct chan *c = &s->ch[k];
        int x = c->param;
        switch (c->effect) {
        case 1: c->period -= x; break;
        case 2: c->period += x; break;
        case 3: slide_to(c); break;
        case 4: c->vib_pos += c->vib_speed; break;
        case 5: slide_to(c), slide_volume(c); break;
        case 6: c->vib_pos += c->vib_speed, slide_volume(c); break;
        case 7: c->trem_pos += c->trem_speed; break;
        case 10: slide_volume(c); break;
        case 14:
            switch (x >> 4) {
            case 9:                             /* struck again every so many ticks */
                if ((x & 15) && s->tick % (x & 15) == 0)
                    c->pos = 0;
                break;
            case 12:
                if (s->tick == (x & 15))
                    c->volume = 0;
                break;
            case 13:
                if (s->tick == (x & 15) && c->wait_period) {
                    strike(c, m, c->wait_period, note_of(c->wait_period));
                    c->wait_period = 0;
                }
                break;
            }
            break;
        }
        if (c->effect == 1 || c->effect == 2) {
            if (c->period < m->low)
                c->period = m->low;
            if (c->period > m->high)
                c->period = m->high;
        }
    }
}

static void advance(struct mod *m, struct state *s)     /* the next row */
{
    int looped = s->loop_to >= 0;
    if (looped) {
        s->row = s->loop_to;
    } else if (s->jump >= 0 || s->brk >= 0) {
        s->order = s->jump >= 0 ? s->jump : s->order + 1;
        s->row = s->brk >= 0 ? s->brk : 0;
    } else if (++s->row >= 64) {
        s->row = 0;
        s->order++;
    }
    s->jump = s->brk = s->loop_to = -1;
    if (s->order >= (int)m->length) {
        s->ended = 1;
        return;
    }
    {                                           /* a row played before, and not inside one of E6's loops (which
                                                   play their rows again, a counted number of times): it goes round */
        unsigned bit = (unsigned)s->order * 64 + (unsigned)s->row, k;
        int looping = looped;
        for (k = 0; k < m->channels; k++)
            if (s->ch[k].loop_count)
                looping = 1;
        if (!looping && s->seen[bit >> 3] & 1u << (bit & 7))
            s->ended = 1;
        s->seen[bit >> 3] |= (unsigned char)(1u << (bit & 7));
    }
}

static void tick(struct mod *m, struct state *s)        /* the next tick begun: its effects, its pitches, its length */
{
    unsigned k;
    if (s->begun) {
        if (++s->tick >= s->speed) {
            s->tick = 0;
            if (s->delay > 0) {
                s->delay--, s->again = 1;
            } else {
                s->again = 0;
                advance(m, s);
            }
        }
    } else {
        s->begun = 1;
        s->seen[0] |= 1;
    }
    if (++s->ticks > 4000000ul)
        s->ended = 1;
    if (s->ended)
        return;
    if (s->tick == 0 && !s->again)
        row(m, s);
    else
        effects(m, s);
    for (k = 0; k < m->channels; k++) {
        struct chan *c = &s->ch[k];
        int period = c->period, volume = c->volume;
        if (c->effect == 0 && c->param && c->note >= 0) {       /* arpeggio: the note, and two above it, in turn */
            int n = c->note + (s->tick % 3 == 1 ? c->param >> 4 : s->tick % 3 == 2 ? c->param & 15 : 0);
            period = m->periods[c->finetune][n < NOTES ? n : NOTES - 1];
        }
        if (c->effect == 4 || c->effect == 6)
            period += wave(c->vib_wave, c->vib_pos) * c->vib_depth / 128;
        if (c->effect == 7) {
            volume += wave(c->trem_wave, c->trem_pos) * c->trem_depth / 64;
            volume = volume < 0 ? 0 : volume > 64 ? 64 : volume;
        }
        c->out_period = period, c->out_volume = volume;
        c->step = period > 0 ? ((unsigned long long)CLOCK << 32) / ((unsigned long long)period * m->rate) : 0;
    }
    {
        unsigned long long len = ((unsigned long long)m->rate * 5 << 16) / ((unsigned)s->bpm * 2u) + s->frac;
        s->left = (unsigned long)(len >> 16);
        s->frac = (unsigned long)(len & 0xFFFF);
    }
}

/* ---- the samples mixed */

static unsigned long long end_of(const struct sample *sm)      /* where its bytes end, or its loop: 32.32 */
{
    return (unsigned long long)(sm->loop_len ? sm->loop_start + sm->loop_len : sm->length) << 32;
}

static void skip(struct mod *m, struct state *s, unsigned long n)       /* n frames gone by, unheard */
{
    unsigned k;
    for (k = 0; k < m->channels; k++) {
        struct chan *c = &s->ch[k];
        unsigned long long end;
        if (!c->smp || !c->step)
            continue;
        end = end_of(c->smp);
        c->pos += c->step * n;
        if (c->pos >= end) {
            if (c->smp->loop_len)
                c->pos = ((unsigned long long)c->smp->loop_start << 32)
                         + (c->pos - end) % ((unsigned long long)c->smp->loop_len << 32);
            else
                c->smp = NULL;
        }
    }
}

static void mix(struct mod *m, struct state *s, short *pcm, unsigned long n)
{
    long acc[2 * 256];
    unsigned long i;
    unsigned k;
    while (n) {
        unsigned long part = n > 256 ? 256 : n;
        memset(acc, 0, part * 2 * sizeof acc[0]);
        for (k = 0; k < m->channels; k++) {
            struct chan *c = &s->ch[k];
            const struct sample *sm = c->smp;
            unsigned long long end, loop;
            long left, right;
            if (!sm || !c->step)
                continue;
            end = end_of(sm), loop = (unsigned long long)sm->loop_len << 32;
            left = c->out_volume * (256 - c->pan), right = c->out_volume * c->pan;
            for (i = 0; i < part; i++) {
                unsigned long at;
                long v;
                if (c->pos >= end) {
                    if (!loop) {
                        c->smp = NULL;
                        break;
                    }
                    c->pos = ((unsigned long long)sm->loop_start << 32) + (c->pos - end) % loop;
                }
                at = (unsigned long)(c->pos >> 32);
                v = sm->data[at] * 256;
                if (m->smooth) {
                    unsigned long nx = at + 1;
                    long b = (unsigned long long)nx << 32 < end ? sm->data[nx] : loop ? sm->data[sm->loop_start] : 0;
                    v += (b * 256 - v) * (long)((c->pos >> 24) & 255) / 256;
                }
                acc[i * 2] += v * left / 64;
                acc[i * 2 + 1] += v * right / 64;
                c->pos += c->step;
            }
        }
        for (i = 0; i < part * 2; i++) {        /* a side's two channels of four at their loudest are the top */
            long v = acc[i] / 512;
            if (m->channels > 4)
                v = v * 4 / (long)m->channels;
            pcm[i] = (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        pcm += part * 2, n -= part;
    }
}

static unsigned long run(struct mod *m, struct state *s, short *pcm, unsigned long frames)
{
    unsigned long done = 0;
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
            skip(m, s, n);
        s->left -= n, s->frames += n, done += n;
    }
    return done;
}

unsigned long mod_render(struct mod *m, short *pcm, unsigned long frames)
{
    return run(m, &m->s, pcm, frames);
}

unsigned long mod_length(struct mod *m)
{
    if (!m->total) {
        struct state *s = malloc(sizeof *s);
        if (!s)
            return 0;
        reset(m, s);
        while (!s->ended && s->frames < 0x7FFF0000ul)
            run(m, s, NULL, 1ul << 20);
        m->total = s->frames ? s->frames : 1;
        free(s);
    }
    return m->total;
}

void mod_seek(struct mod *m, unsigned long frame)
{
    reset(m, &m->s);
    run(m, &m->s, NULL, frame);
}
