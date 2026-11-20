/* A tracker's voices mixed into samples: what the players of the later module formats share (s3m.c, xm.c; the
   MOD player, mod.c, was there first and has its own). A voice is a sample being played at some pitch and
   loudness; its place in the sample is a whole part and a 32-bit fraction, so that a pitch is as exact as it can
   be told. All in whole numbers: the same samples from every compiler.

   Included by each player (the functions are static: each has its own copy, of a few hundred bytes). */
#ifndef TRKMIX_H
#define TRKMIX_H

#include <string.h>

struct trk_sample {
    const unsigned char *data;          /* 8 bits a sample, or 16 (the low byte first) */
    unsigned long length;               /* samples */
    unsigned long loop_start, loop_end; /* loop_end > loop_start: it goes round between them (loop_end: the first
                                           sample not played) */
    unsigned char bits16, unsign, pingpong;     /* unsign: 0 is the lowest, not the middle; pingpong: the loop is
                                                   played forwards, then backwards, and so on */
};

struct trk_voice {
    const struct trk_sample *smp;       /* NULL: silent */
    unsigned long long pos;             /* 32.32. In a ping-pong loop it runs on past the loop's end for the way
                                           back, as if the loop were laid out there again the other way round */
    unsigned long long step;            /* a frame's (0: held) */
    long left, right;                   /* how loud, each side: 0 to 65536 (mixed to the nearest 64 below) */
};

static int trk_looped(const struct trk_sample *s)
{
    return s->loop_end > s->loop_start && s->loop_end <= s->length;
}

/* Two bytes as a number, the low one first: as the files have them, and as this PC does (one load). */
static unsigned short trk_word(const unsigned char *p)
{
    unsigned short v;
    memcpy(&v, p, 2);
    return v;
}

/* Sample i of s, as 16 bits signed (i within its length). */
static long trk_at(const struct trk_sample *s, unsigned long i)
{
    long v;
    if (s->bits16) {
        v = trk_word(s->data + 2 * i);
        return s->unsign ? v - 32768 : v >= 32768 ? v - 65536 : v;
    }
    v = s->data[i];
    return (s->unsign ? v - 128 : v >= 128 ? v - 256 : v) * 256;
}

/* The voice's place brought back into its sample where it has run past the end or the loop: 0 if it has
   stopped (a sample with no loop, played out). */
static int trk_wrap(struct trk_voice *v)
{
    const struct trk_sample *s = v->smp;
    if (trk_looped(s)) {
        unsigned long long start = (unsigned long long)s->loop_start << 32;
        unsigned long long span = (unsigned long long)(s->loop_end - s->loop_start) << 32;
        unsigned long long round = s->pingpong ? 2 * span : span;
        if (v->pos >= start + round)
            v->pos = start + (v->pos - start) % round;
        return 1;
    }
    if (v->pos >> 32 >= s->length) {
        v->smp = 0;
        return 0;
    }
    return 1;
}

/* n frames of the voice added to acc (two sums a frame, left and right: a sample's 16 bits times the side's
   loudness in 1024ths; the caller scales the sums down with trk_out: 32 voices at their loudest fit a sum).
   smooth: each on the line between the two samples it falls between.

   Most frames are done in runs that need no look at the sample's end or its loop: as many as surely lie before
   it (whole samples left, by the step's whole part and one). The frames at an end, and a loop's way back, go
   one at a time. Both ways make the same numbers. */
#define TRK_U8(d, i) (((long)(d)[i] - 128) * 256)
#define TRK_S8(d, i) ((long)(signed char)(d)[i] * 256)
#define TRK_U16(d, i) ((long)trk_word((d) + 2 * (i)) - 32768)
#define TRK_S16(d, i) ((long)(short)trk_word((d) + 2 * (i)))
#define TRK_RUN(GET)                                                                    \
    for (; k; k--, i++) {                                                               \
        long a = GET(d, hi);                                                            \
        unsigned lo2 = lo + slo;                                                        \
        if (smooth)                                                                     \
            a += ((GET(d, hi + 1) - a) * (long)(lo >> 24)) >> 8;                        \
        acc[2 * i] += a * l, acc[2 * i + 1] += a * r;                                   \
        hi += shi + (lo2 < lo), lo = lo2;                                               \
    }

static void trk_mix(struct trk_voice *v, long *acc, unsigned long n, int smooth)
{
    unsigned long i = 0;
    long l, r;
    if (!v->smp || !v->step)
        return;
    l = v->left >> 6, r = v->right >> 6;
    while (i < n) {
        const struct trk_sample *s;
        const unsigned char *d;
        unsigned long hi, shi, limit, k, at, next;
        unsigned lo, slo;
        long a;
        if (!trk_wrap(v))
            return;
        s = v->smp, d = s->data;
        hi = (unsigned long)(v->pos >> 32), lo = (unsigned)v->pos;
        shi = (unsigned long)(v->step >> 32), slo = (unsigned)v->step;
        limit = trk_looped(s) ? s->loop_end : s->length;
        k = hi < limit && limit - hi > 1 ? (limit - hi - 1) / (shi + 1) : 0;
        if (k > n - i)
            k = n - i;
        if (k) {                                /* a run: forwards, well before the end */
            if (s->bits16) {
                if (s->unsign)
                    TRK_RUN(TRK_U16)
                else
                    TRK_RUN(TRK_S16)
            } else if (s->unsign) {
                TRK_RUN(TRK_U8)
            } else {
                TRK_RUN(TRK_S8)
            }
            v->pos = (unsigned long long)hi << 32 | lo;
            continue;
        }
        at = hi, next = at + 1;                 /* one frame, at an end or on a loop's way back */
        if (trk_looped(s) && at >= s->loop_end) {       /* a ping-pong's way back: the same samples from the loop's
                                                           end down, the one after `at` being the one below it */
            unsigned long back = at - s->loop_end;
            at = s->loop_end - 1 - back;
            next = at > s->loop_start ? at - 1 : at;
        } else if (trk_looped(s) && next >= s->loop_end) {
            next = s->pingpong ? s->loop_end - 1 : s->loop_start;
        }
        a = trk_at(s, at);
        if (smooth)
            a += (((next < s->length ? trk_at(s, next) : 0) - a) * (long)(lo >> 24)) >> 8;
        acc[2 * i] += a * l, acc[2 * i + 1] += a * r;
        v->pos += v->step;
        i++;
    }
    trk_wrap(v);                        /* (so that the voice is left as trk_skip would leave it) */
}

/* n frames gone by unheard: the voice's place as if they had been played. */
static void trk_skip(struct trk_voice *v, unsigned long n)
{
    if (!v->smp || !v->step)
        return;
    v->pos += v->step * n;
    trk_wrap(v);
}

/* How loud a song of so many channels is mixed, in 256ths: four channels at their loudest are the top, and more
   of them are seldom loud together: 2 / the root of their number. */
static long trk_gain(unsigned channels)
{
    unsigned long root;
    if (channels <= 4)
        return 256;
    for (root = 1; root * root < ((unsigned long)channels << 16); root++)
        ;
    return (long)(2ul * 256 * 256 / root);
}

/* The sums of n frames (what trk_mix added up) as samples: a voice at its loudest (65536), all on one side, is
   half the top. */
static void trk_out(const long *acc, short *pcm, unsigned long n, long gain)
{
    unsigned long i;
    for (i = 0; i < n * 2; i++) {
        long v = (acc[i] >> 11) * gain >> 8;
        pcm[i] = (short)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
}

/* The step of a sample played at num / den samples for each frame: 32.32 (num below 2^32). */
static unsigned long long trk_step(unsigned long num, unsigned long long den)
{
    return den ? ((unsigned long long)num << 32) / den : 0;
}

#endif
