/* Scream Tracker 3 modules played into samples (s3m.h), for the Player (phase F).
 *
 * An S3M (Future Crew's Scream Tracker 3, 1994): a header with the song's name, its first speed and tempo, its
 * global volume and what each of 32 channels is (left, right, or one of the AdLib's, which are not played
 * here); the order its patterns are played in; instruments, each a sample (8 or 16 bits, with a volume, a loop
 * and the rate at which it sounds a middle C: "C2SPD") and patterns of 64 rows, packed: a cell names its
 * channel and has a note and an instrument, a volume, and a command with its number, as each is there.
 *
 * It is played in ticks as a MOD is (a row lasts `speed` ticks, a tick 2.5 / tempo seconds). A note's pitch is
 * a period, as on the Amiga but four times as fine: 8363 * 16 * (the note's period in the octave) / C2SPD, and
 * its rate is 14317056 / period samples a second. The commands are letters, A to Z; most of them remember
 * their last number, in one memory a channel.
 *
 * Written from the format's public descriptions (Scream Tracker's own TECH.DOC and what is told of how it
 * plays). All of it is whole numbers. Whatever the file's bytes are, nothing is read outside it. */
#include <stdlib.h>
#include <string.h>

#include "s3m.h"
#include "trkmix.h"

#define MAXCH 32
#define CLOCK 14317056ul
#define PERIOD_MIN 64
#define PERIOD_MAX 32767

static const unsigned short octave0[12] = { 1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016, 960, 907 };
static const short sine[64] = {
    0, 24, 49, 74, 97, 120, 141, 161, 180, 197, 212, 224, 235, 244, 250, 253, 255, 253, 250, 244, 235, 224, 212, 197, 180, 161,
    141, 120, 97, 74, 49, 24, 0, -24, -49, -74, -97, -120, -141, -161, -180, -197, -212, -224, -235, -244, -250, -253, -255,
    -253, -250, -244, -235, -224, -212, -197, -180, -161, -141, -120, -97, -74, -49, -24 };
/* S2x: a fine tuning as the rate of a middle C */
static const unsigned short tunes[16] = { 8363, 8413, 8463, 8529, 8581, 8651, 8723, 8757, 7895, 7941, 7985, 8046, 8107,
                                          8169, 8232, 8280 };

struct inst {
    struct trk_sample smp;
    unsigned long c2spd;
    int volume;
};

struct chan {
    struct trk_voice v;
    const struct inst *ins;
    unsigned long c2spd;
    int volume, pan;                    /* 0 to 64; 0 (left) to 256 */
    int note;                           /* octave * 12 + note of what was struck last (-1: nothing) */
    long period;                        /* before vibrato and arpeggio (0: silent) */
    long target;                        /* a slide to a note: its period */
    int cmd, info;                      /* this row's */
    int mem;                            /* the last number of the commands that share one */
    int porta;                          /* G's own */
    int offset;                         /* O's own */
    int vib_speed, vib_depth, vib_pos;
    int trem_pos;
    int tremor;                         /* I: ticks into its turn */
    int retrig;                         /* Q: ticks since the last */
    int wait, wait_note, wait_ins, wait_vol;    /* SDx: the tick the cell waits for (0: none), and the cell */
    int cut;                            /* SCx: the tick the note is cut at (0: none) */
};

struct state {
    struct chan ch[MAXCH];
    int order, row, tick, speed, tempo, global;
    int jump, brk, loop_to;
    int loop_row, loop_count;           /* SBx: one for the song, as in Scream Tracker */
    int delay, again;                   /* SEx: times the row is still to be gone through again; in one now */
    int begun, ended;
    int flow;                           /* only where the song goes is wanted (its length): no voices */
    int next_order, next_row;           /* the row whose cells begin at next_at in its pattern (next_row 0: none) */
    unsigned long next_at;
    unsigned long left, frac, frames, ticks;
    unsigned char seen[256 * 64 / 8];
};

struct s3m {
    const unsigned char *data;
    unsigned long size;
    unsigned rate, channels, norders, nins, npat;
    unsigned char orders[256];
    signed char slot[32];               /* a pattern's channel number as one of ours (-1: not played) */
    unsigned short pan0[MAXCH];         /* each channel's place as the song begins: 0 to 256 */
    unsigned long pat_at[256];          /* where each pattern's packed rows begin (0: none), and their length */
    unsigned short pat_len[256];
    struct inst ins[100];
    char title[29];
    int speed0, tempo0, global0, fast_slides, smooth;
    long gain;                          /* the mix's, in 256ths: less the more channels there are */
    unsigned long total;
    struct state s;
};

static unsigned le16(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }
static unsigned long le32(const unsigned char *p) { return le16(p) | (unsigned long)le16(p + 2) << 16; }

static int place(unsigned x)            /* a channel's place of Scream Tracker's 16 (0 to 15) as ours: 0 to 256 */
{
    return (int)((x * 256 + 7) / 15);
}

static void reset(const struct s3m *m, struct state *s)
{
    unsigned c;
    memset(s, 0, sizeof *s);
    s->speed = m->speed0, s->tempo = m->tempo0, s->global = m->global0;
    s->jump = s->brk = s->loop_to = -1;
    for (c = 0; c < m->channels; c++) {
        s->ch[c].pan = m->pan0[c];
        s->ch[c].note = -1;
        s->ch[c].c2spd = 8363;
    }
}

struct s3m *s3m_open(const unsigned char *data, unsigned long size, unsigned rate, int *err)
{
    struct s3m *m;
    unsigned i, nord, flags, cwt, ffi, stereo;
    unsigned long at;
    int dummy;
    if (!err)
        err = &dummy;
    *err = S3M_E_KIND;
    if (size < 0x60 || rate < 4000 || rate > 96000 || memcmp(data + 0x2C, "SCRM", 4) || data[0x1D] != 16)
        return NULL;
    *err = S3M_E_MEMORY;
    if (!(m = calloc(1, sizeof *m)))
        return NULL;
    m->data = data, m->size = size, m->rate = rate, m->smooth = 1;
    for (i = 0; i < 28 && data[i]; i++)
        m->title[i] = data[i] >= 32 && data[i] < 127 ? (char)data[i] : ' ';
    while (i && m->title[i - 1] == ' ')
        i--;
    m->title[i] = 0;
    nord = le16(data + 0x20), m->nins = le16(data + 0x22), m->npat = le16(data + 0x24);
    flags = le16(data + 0x26), cwt = le16(data + 0x28), ffi = le16(data + 0x2A);
    m->global0 = data[0x30] > 64 ? 64 : data[0x30];
    m->speed0 = data[0x31] ? data[0x31] : 6;
    m->tempo0 = data[0x32] >= 33 ? data[0x32] : 125;
    m->fast_slides = (flags & 0x40) || cwt == 0x1300;
    stereo = data[0x33] & 0x80;
    *err = S3M_E_DAMAGED;
    at = 0x60;
    if (nord > 256 || m->nins > 99 || m->npat > 256 || at + nord + 2ul * m->nins + 2ul * m->npat > size)
        goto bad;
    for (i = 0; i < 32; i++) {                  /* the channels that play samples: 0 to 7 left, 8 to 15 right */
        unsigned k = data[0x40 + i];
        m->slot[i] = -1;
        if (k < 16 && m->channels < MAXCH) {
            m->slot[i] = (signed char)m->channels;
            m->pan0[m->channels++] = (unsigned short)(!stereo ? 128 : place(k < 8 ? 3 : 12));
        }
    }
    if (!m->channels)
        goto bad;
    memcpy(m->orders, data + at, nord);
    m->norders = nord;
    at += nord;
    for (i = 0; i < m->nins; i++) {
        unsigned long p = (unsigned long)le16(data + at + 2 * i) * 16, where, len, ls, le;
        const unsigned char *h;
        struct inst *in = &m->ins[i + 1];
        unsigned f;
        if (!p || p + 0x50 > size)
            continue;
        h = data + p;
        if (h[0] != 1 || h[0x1E])               /* (an AdLib instrument, none, or packed some way) */
            continue;
        where = ((unsigned long)h[0x0D] << 16 | le16(h + 0x0E)) * 16;
        len = le32(h + 0x10);
        f = h[0x1F];
        in->smp.bits16 = (f & 4) != 0;
        in->smp.unsign = ffi != 1;
        in->volume = h[0x1C] > 64 ? 64 : h[0x1C];
        in->c2spd = le32(h + 0x20);
        if (in->c2spd < 1000 || in->c2spd > 100000)
            in->c2spd = 8363;
        if (!where || where >= size)
            continue;
        if (len > (size - where) >> in->smp.bits16)             /* (cut to what the file has) */
            len = (size - where) >> in->smp.bits16;
        in->smp.data = data + where, in->smp.length = len;
        ls = le32(h + 0x14), le = le32(h + 0x18);
        if (le > len)
            le = len;
        if ((f & 1) && ls < le)
            in->smp.loop_start = ls, in->smp.loop_end = le;
    }
    at += 2ul * m->nins;
    for (i = 0; i < m->npat; i++) {
        unsigned long p = (unsigned long)le16(data + at + 2 * i) * 16;
        if (p && p + 2 <= size) {
            unsigned long n = le16(data + p), rest = size - p - 2;
            m->pat_at[i] = p + 2;               /* (a length that says nothing: to the file's end, and 64 rows) */
            m->pat_len[i] = (unsigned short)(n >= 2 && n - 2 <= rest ? n - 2 : rest > 0xFFFF ? 0xFFFF : rest);
        }
    }
    at += 2ul * m->npat;
    if (stereo && data[0x35] == 0xFC && at + 32 <= size)        /* each channel's place, where the file says them */
        for (i = 0; i < 32; i++)
            if ((data[at + i] & 0x20) && m->slot[i] >= 0)
                m->pan0[(int)m->slot[i]] = (unsigned short)place(data[at + i] & 15);
    m->gain = trk_gain(m->channels);
    reset(m, &m->s);
    *err = S3M_OK;
    return m;
bad:
    free(m);
    return NULL;
}

void s3m_free(struct s3m *m) { free(m); }
const char *s3m_title(const struct s3m *m) { return m->title; }
unsigned s3m_channels(const struct s3m *m) { return m->channels; }
void s3m_smooth(struct s3m *m, int on) { m->smooth = on; }
unsigned long s3m_tell(const struct s3m *m) { return m->s.frames; }

/* ---- the playing */

static long period_of(int note, unsigned long c2spd)   /* note: octave * 12 + semitone */
{
    int oct = note / 12;
    unsigned long p;
    if (note < 0 || oct > 9 || !c2spd)
        return 0;
    p = ((unsigned long)octave0[note % 12] * 16 * 8363ul >> oct) / c2spd;
    return p < PERIOD_MIN ? PERIOD_MIN : p > PERIOD_MAX ? PERIOD_MAX : (long)p;
}

static void bound(struct chan *c)       /* a period that is one stays one */
{
    if (c->period)
        c->period = c->period < PERIOD_MIN ? PERIOD_MIN : c->period > PERIOD_MAX ? PERIOD_MAX : c->period;
}

static void take_ins(const struct s3m *m, struct chan *c, int i)        /* a cell's instrument */
{
    if (i <= 0)
        return;
    if (i <= (int)m->nins && m->ins[i].smp.data) {
        c->ins = &m->ins[i];
        c->volume = c->ins->volume;
        c->c2spd = c->ins->c2spd;
    } else {
        c->ins = 0;
    }
}

static void strike(struct chan *c, int note)
{
    c->note = note;
    c->period = period_of(note, c->c2spd);
    c->v.smp = c->ins && c->ins->smp.length && c->period ? &c->ins->smp : 0;
    c->v.pos = 0;
    c->vib_pos = c->trem_pos = 0;
    c->retrig = 0, c->tremor = 0;
}

/* A cell's note (its byte; -1 or 255: none). slide: to be slid to (G, L), not struck. 1: struck. */
static int take_note(struct chan *c, int n, int slide)
{
    int nn;
    if (n == 254) {                             /* ^^: the note cut */
        c->v.smp = 0;
        c->period = 0;
        return 0;
    }
    if (n < 0 || n > 254 || (n & 15) >= 12)
        return 0;
    nn = (n >> 4) * 12 + (n & 15);
    if (slide && c->v.smp && c->period) {
        c->target = period_of(nn, c->c2spd);
        return 0;
    }
    strike(c, nn);                              /* (nothing sounding to slide from: struck after all) */
    c->target = 0;
    return 1;
}

static void slide_volume(struct chan *c, int info, int first, int fast)
{
    int up = info >> 4, down = info & 15;
    if (down == 15 && up) {                     /* DxF: a fine slide up, once */
        if (first)
            c->volume += up;
    } else if (up == 15 && down) {              /* DFx: a fine slide down, once */
        if (first)
            c->volume -= down;
    } else if (!first || fast) {
        if (down)
            c->volume -= down;
        else
            c->volume += up;
    }
    c->volume = c->volume < 0 ? 0 : c->volume > 64 ? 64 : c->volume;
}

static void slide_to(struct chan *c)
{
    long by = (long)c->porta * 4;
    if (!c->target || !c->period)
        return;
    if (c->period < c->target)
        c->period = c->period + by > c->target ? c->target : c->period + by;
    else
        c->period = c->period - by < c->target ? c->target : c->period - by;
}

/* A row's cells into the channels' cmd, info and what they strike: the pattern unpacked to that row. */
static void cells(const struct s3m *m, struct state *s, int note[MAXCH], int ins[MAXCH], int vol[MAXCH])
{
    unsigned pat = m->orders[s->order], c;
    const unsigned char *base, *p, *end;
    int r = 0;
    for (c = 0; c < m->channels; c++) {
        note[c] = ins[c] = vol[c] = -1;
        s->ch[c].cmd = s->ch[c].info = 0;
    }
    if (pat >= m->npat || !m->pat_at[pat])
        return;
    base = p = m->data + m->pat_at[pat], end = p + m->pat_len[pat];
    if (s->next_row && s->next_order == s->order && s->next_row <= s->row)      /* (rows mostly come in turn) */
        r = s->next_row, p = base + s->next_at;
    while (p < end && r <= s->row) {
        unsigned what = *p++;
        int k, n = -1, i = -1, v = -1, cmd = 0, info = 0;
        if (!what) {
            r++;
            continue;
        }
        if (what & 32) {
            if (end - p < 2)
                break;
            n = p[0], i = p[1];
            p += 2;
        }
        if (what & 64) {
            if (end - p < 1)
                break;
            v = *p++;
        }
        if (what & 128) {
            if (end - p < 2)
                break;
            cmd = p[0], info = p[1];
            p += 2;
        }
        k = m->slot[what & 31];
        if (r == s->row && k >= 0) {
            note[k] = n, ins[k] = i, vol[k] = v;
            s->ch[k].cmd = cmd, s->ch[k].info = info;
        }
    }
    s->next_order = s->order, s->next_row = r, s->next_at = (unsigned long)(p - base);
}

static void row(const struct s3m *m, struct state *s)   /* the row's first tick */
{
    int note[MAXCH], ins[MAXCH], vol[MAXCH];
    unsigned k;
    cells(m, s, note, ins, vol);
    for (k = 0; k < m->channels; k++) {
        struct chan *c = &s->ch[k];
        int cmd = c->cmd, info = c->info, x, y, struck;
        /* (The commands that share a memory: no number means the last.) */
        if (cmd == 4 || cmd == 5 || cmd == 6 || cmd == 9 || cmd == 10 || cmd == 11 || cmd == 12 || cmd == 17 || cmd == 18
            || cmd == 19) {
            if (info)
                c->mem = info;
            else
                info = c->info = c->mem;
        }
        x = info >> 4, y = info & 15;
        c->cut = c->wait = 0;
        /* SDx: all of the cell waits for tick x (which never comes, if the row has not so many) */
        if (cmd == 19 && x == 13 && y) {
            if (y < s->speed)
                c->wait = y, c->wait_note = note[k], c->wait_ins = ins[k], c->wait_vol = vol[k];
            continue;
        }
        take_ins(m, c, ins[k]);
        if (cmd == 19 && x == 2)                /* S2x: a tuning, for this note and those after */
            c->c2spd = tunes[y];
        struck = take_note(c, note[k], cmd == 7 || cmd == 12);
        if (vol[k] >= 0 && vol[k] <= 64)
            c->volume = vol[k];
        switch (cmd) {
        case 1:                                 /* A: the speed */
            if (info)
                s->speed = info;
            break;
        case 2:                                 /* B: to an order */
            s->jump = info;
            break;
        case 3:                                 /* C: to a row of the next (two decimal digits) */
            s->brk = x * 10 + y;
            if (s->brk > 63)
                s->brk = 0;
            break;
        case 4:                                 /* D */
        case 11:                                /* K: vibrato goes on, with D's slide */
        case 12:                                /* L: the slide to a note goes on, with D's slide */
            slide_volume(c, info, 1, m->fast_slides);
            break;
        case 5:                                 /* E: down in pitch; EFx fine, EEx extra fine, once */
            if (c->period && x >= 14)
                c->period += x == 15 ? y * 4 : y;
            break;
        case 6:                                 /* F: up */
            if (c->period && x >= 14)
                c->period -= x == 15 ? y * 4 : y;
            break;
        case 7:                                 /* G: its own memory */
            if (info)
                c->porta = info;
            break;
        case 8:                                 /* H: vibrato; each half of its number remembered by itself */
        case 21:                                /* U: a quarter as deep */
            if (x)
                c->vib_speed = x;
            if (y)
                c->vib_depth = y;
            break;
        case 15:                                /* O: the note from further into its sample */
            if (info)
                c->offset = info;
            if (struck && c->v.smp) {
                unsigned long off = (unsigned long)c->offset * 256;
                if (off >= c->v.smp->length)
                    off = c->v.smp->length - 1;
                c->v.pos = (unsigned long long)off << 32;
            }
            break;
        case 19:                                /* S */
            if (x == 8) {
                c->pan = place((unsigned)y);
            } else if (x == 11) {               /* SBx: rows gone through again; SB0 says from where */
                if (!y) {
                    s->loop_row = s->row;
                } else if (!s->loop_count) {
                    s->loop_count = y;
                    s->loop_to = s->loop_row;
                } else if (--s->loop_count) {
                    s->loop_to = s->loop_row;
                } else {
                    s->loop_row = s->row + 1;   /* (done: the next begins after it, as Scream Tracker has it) */
                }
            } else if (x == 12 && y) {          /* SCx: cut at tick x */
                c->cut = y;
            } else if (x == 14) {               /* SEx: the row so many times more */
                s->delay = y;
            }
            break;
        case 20:                                /* T: the tempo */
            if (info >= 33)
                s->tempo = info;
            break;
        case 22:                                /* V: the song's volume */
            s->global = info > 64 ? 64 : info;
            break;
        case 24:                                /* X: the channel's place, 0 to 128 */
            if (info <= 128)
                c->pan = info * 2;
            break;
        }
        bound(c);
    }
}

/* A tick after the row's first (and each tick of a row gone through again by SEx). */
static void effects(const struct s3m *m, struct state *s)
{
    unsigned k;
    for (k = 0; k < m->channels; k++) {
        struct chan *c = &s->ch[k];
        int cmd = c->cmd, info = c->info, x = info >> 4, y = info & 15;
        if (c->wait) {                          /* SDx: the cell's tick has come? */
            if (s->tick == c->wait) {
                c->wait = 0;
                take_ins(m, c, c->wait_ins);
                take_note(c, c->wait_note, 0);
                if (c->wait_vol >= 0 && c->wait_vol <= 64)
                    c->volume = c->wait_vol;
            }
            continue;
        }
        if (c->cut && s->tick == c->cut) {
            c->v.smp = 0;
            c->period = 0;
        }
        switch (cmd) {
        case 4:
            slide_volume(c, info, 0, m->fast_slides);
            break;
        case 5:
            if (x < 14 && c->period)
                c->period += info * 4;
            break;
        case 6:
            if (x < 14 && c->period)
                c->period -= info * 4;
            break;
        case 7:
            slide_to(c);
            break;
        case 11:
            slide_volume(c, info, 0, m->fast_slides);
            c->vib_pos = (c->vib_pos + c->vib_speed * 4) & 255;
            break;
        case 12:
            slide_volume(c, info, 0, m->fast_slides);
            slide_to(c);
            break;
        case 8:
        case 21:
            c->vib_pos = (c->vib_pos + c->vib_speed * 4) & 255;
            break;
        case 9:                                 /* I: on for x + 1 ticks, off for y + 1 */
            c->tremor = (c->tremor + 1) % (x + y + 2);
            break;
        case 17:                                /* Q: struck again every y ticks, its volume changed by x */
            if (y && ++c->retrig >= y && c->ins && c->period) {
                static const signed char add[16] = { 0, -1, -2, -4, -8, -16, 0, 0, 0, 1, 2, 4, 8, 16, 0, 0 };
                c->retrig = 0;
                c->v.smp = c->ins->smp.length ? &c->ins->smp : 0;
                c->v.pos = 0;
                if (x == 6)
                    c->volume = c->volume * 2 / 3;
                else if (x == 7)
                    c->volume /= 2;
                else if (x == 14)
                    c->volume = c->volume * 3 / 2;
                else if (x == 15)
                    c->volume *= 2;
                else
                    c->volume += add[x];
                c->volume = c->volume < 0 ? 0 : c->volume > 64 ? 64 : c->volume;
            }
            break;
        case 18:                                /* R: tremolo */
            c->trem_pos = (c->trem_pos + x * 4) & 255;
            break;
        }
        bound(c);
    }
}

static int no_pattern(const struct s3m *m, int order)   /* an order that is a marker (254), or names no pattern */
{
    return m->orders[order] != 255 && m->orders[order] >= m->npat;
}

static void advance(const struct s3m *m, struct state *s)       /* the next row */
{
    if (s->delay) {                             /* SEx: this row again */
        s->delay--;
        s->again = 1;
        return;
    }
    s->again = 0;
    if (s->loop_to >= 0) {
        s->row = s->loop_to > 63 ? 63 : s->loop_to;
        s->loop_to = -1;
        s->jump = s->brk = -1;
        return;
    }
    if (s->jump >= 0 || s->brk >= 0) {
        s->order = s->jump >= 0 ? s->jump : s->order + 1;
        s->row = s->brk >= 0 ? s->brk : 0;
        s->jump = s->brk = -1;
    } else if (++s->row >= 64) {
        s->row = 0;
        s->order++;
    } else {
        return;                                 /* (the same pattern still: its loop stands) */
    }
    s->loop_row = s->loop_count = 0;
    while (s->order < (int)m->norders && no_pattern(m, s->order))
        s->order++;
    if (s->order >= (int)m->norders || m->orders[s->order] == 255)
        s->ended = 1;
}

/* The next tick begun: the row's cells or the tick's effects, then what each channel sounds like, and how many
   frames the tick is. */
static void tick(const struct s3m *m, struct state *s)
{
    unsigned long long len;
    unsigned k;
    if (!s->begun) {
        s->begun = 1;
        while (s->order < (int)m->norders && no_pattern(m, s->order))
            s->order++;
        if (s->order >= (int)m->norders || m->orders[s->order] == 255) {
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
        unsigned bit = (unsigned)s->order * 64 + (unsigned)s->row;
        /* a row played before, and not by a loop's leave: the song has gone round */
        if ((s->seen[bit / 8] & (1 << (bit % 8))) && !s->loop_count) {
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
        long p = c->period;
        int v = c->volume, cmd = c->cmd, x = c->info >> 4, y = c->info & 15;
        if (cmd == 8 || cmd == 11) {
            p += sine[(c->vib_pos >> 2) & 63] * c->vib_depth / 32;
        } else if (cmd == 21) {
            p += sine[(c->vib_pos >> 2) & 63] * c->vib_depth / 128;
        } else if (cmd == 10 && c->note >= 0 && c->info && p) { /* J: the note, then x and y semitones above */
            int step = s->tick % 3;
            long q = period_of(c->note + (step == 1 ? x : step == 2 ? y : 0), c->c2spd);
            if (q)
                p = q;
        } else if (cmd == 18) {
            v += sine[(c->trem_pos >> 2) & 63] * y / 64;
        } else if (cmd == 9 && c->tremor > x) {
            v = 0;
        }
        v = v < 0 ? 0 : v > 64 ? 64 : v;
        if (p >= PERIOD_MIN / 2 && c->v.smp) {
            c->v.step = trk_step(CLOCK, (unsigned long long)p * m->rate);
            /* (a channel at its loudest, the song's volume full: 65536 shared between the two sides) */
            c->v.left = (long)v * s->global * (256 - c->pan) / 16;
            c->v.right = (long)v * s->global * c->pan / 16;
        } else {
            c->v.step = 0;
            c->v.left = c->v.right = 0;
        }
    }
    len = (unsigned long long)m->rate * 5 * 65536 / ((unsigned)s->tempo * 2) + s->frac;        /* 2.5 / tempo seconds */
    s->left = (unsigned long)(len >> 16);
    s->frac = (unsigned long)(len & 0xFFFF);
}

static void mix(const struct s3m *m, struct state *s, short *pcm, unsigned long n)
{
    long long acc[2 * 256];
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

static unsigned long run(const struct s3m *m, struct state *s, short *pcm, unsigned long frames)
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

unsigned long s3m_render(struct s3m *m, short *pcm, unsigned long frames) { return run(m, &m->s, pcm, frames); }

unsigned long s3m_length(struct s3m *m)
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

void s3m_seek(struct s3m *m, unsigned long frame)
{
    reset(m, &m->s);
    run(m, &m->s, NULL, frame);
}
