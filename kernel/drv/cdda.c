/* CD audio played digitally: the ring between the reader and the mixer (cdda.h). Counters run from a play's
 * start, in bytes: a disc's longest play (80 minutes, 847 MB) never wraps them. The ring's size is whole sectors,
 * so a run the reader is given never crosses its end, nor does a frame. */
#include "cdda.h"

void cdda_init(struct cdda *c, unsigned char *ring, unsigned long size)
{
    c->ring = ring;
    c->size = size / CDDA_SECTOR * CDDA_SECTOR;
    c->rd = c->wr = 0;
    c->start = c->count = c->done = 0;
    c->played = 0;
    c->phase = 0;
    c->state = CDDA_IDLE;
    c->gen = 0;
    c->vol[0] = c->vol[1] = 256;
}

void cdda_play(struct cdda *c, unsigned long start, unsigned long count)
{
    c->gen++;
    c->rd = c->wr = 0;
    c->done = 0;
    c->played = 0;
    c->phase = 0;
    c->start = start;
    c->count = count;
    c->state = count ? CDDA_PLAYING : CDDA_DONE;
}

void cdda_stop(struct cdda *c)
{
    c->gen++;
    c->state = CDDA_IDLE;
}

void cdda_pause(struct cdda *c, int resume)
{
    if (resume && c->state == CDDA_PAUSED)
        c->state = CDDA_PLAYING;
    else if (!resume && c->state == CDDA_PLAYING)
        c->state = CDDA_PAUSED;
}

void cdda_volume(struct cdda *c, unsigned left, unsigned right)
{
    c->vol[0] = left > 256 ? 256 : left;
    c->vol[1] = right > 256 ? 256 : right;
}

unsigned long cdda_want(struct cdda *c, unsigned long max, unsigned long *lba, unsigned long *gen,
                        unsigned char **into)
{
    unsigned long room, at, to_end, n;
    if ((c->state != CDDA_PLAYING && c->state != CDDA_PAUSED) || c->done >= c->count || !c->size)
        return 0;
    room = c->size - (c->wr - c->rd);           /* (rd moves on meanwhile: the room only grows) */
    at = c->wr % c->size;
    to_end = c->size - at;
    n = (room < to_end ? room : to_end) / CDDA_SECTOR;
    if (n > c->count - c->done)
        n = c->count - c->done;
    if (n > max)
        n = max;
    if (!n)
        return 0;
    *lba = c->start + c->done;
    *gen = c->gen;
    *into = c->ring + at;
    return n;
}

int cdda_commit(struct cdda *c, unsigned long gen, unsigned long n)
{
    if (gen != c->gen || (c->state != CDDA_PLAYING && c->state != CDDA_PAUSED))
        return 0;
    c->done += n;
    c->wr += n * CDDA_SECTOR;
    return 1;
}

static long sample(const unsigned char *p) { return (short)(p[0] | p[1] << 8); }

/* The frames the resampler has passed, given up as soon as it has (the room is the reader's, the position
   what has been played): 0, or 1 when it must wait for the reader or the play has ended. */
static int pass(struct cdda *c, unsigned long total)
{
    while (c->phase >= 0x10000 && c->played < total) {
        if (c->wr == c->rd)
            return 1;                           /* (the reader is late: silence, the position held) */
        c->phase -= 0x10000;
        c->rd += 4;
        c->played++;
    }
    if (c->played >= total) {
        c->state = CDDA_DONE;
        return 1;
    }
    return 0;
}

void cdda_mix(struct cdda *c, int *acc, unsigned long frames, unsigned long rate)
{
    unsigned long step, total, i, avail;
    int ch;
    if (c->state != CDDA_PLAYING || !rate)
        return;
    step = (CDDA_RATE << 16) / rate;           /* (44,100 << 16 fits in 32 bits) */
    total = c->count * CDDA_FRAMES;
    for (i = 0; i < frames; i++) {
        const unsigned char *p0, *p1;
        if (pass(c, total))
            return;
        avail = (c->wr - c->rd) >> 2;
        if (!avail || (avail < 2 && c->done < c->count))
            return;                             /* (the next frame too, to go between them; the play's last alone) */
        p0 = c->ring + c->rd % c->size;
        p1 = avail >= 2 ? c->ring + (c->rd + 4) % c->size : p0;
        for (ch = 0; ch < 2; ch++) {
            long s0 = sample(p0 + 2 * ch), s1 = sample(p1 + 2 * ch);
            long s = s0 + (((s1 - s0) * (long)(c->phase >> 1)) >> 15);
            acc[2 * i + ch] += (int)(s * (long)c->vol[ch] >> 8);
        }
        c->phase += step;
        if (pass(c, total))
            return;
    }
}

unsigned long cdda_position(const struct cdda *c, unsigned *frame)
{
    unsigned long played = c->played, total = c->count * CDDA_FRAMES;
    if (played > total)
        played = total;
    if (frame)
        *frame = (unsigned)(played % CDDA_FRAMES);
    return c->start + played / CDDA_FRAMES;
}

int cdda_state(const struct cdda *c) { return c->state; }
