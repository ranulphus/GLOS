/* A MIDI file's messages sent to a MIDI port as their times come (midiplay.h).
 *
 * A pause drops what waits at the port and ends the notes. Going on, or a seek, first tells the port how things
 * stand at that place (for each channel: every controller as it begins, then what the file had set up to there,
 * its instrument, its bend, and the bend's range where the file set it), then sends on from there: no note from
 * before the place is struck late, and a seek backwards doesn't keep a later instrument. */
#include <string.h>

#include "midiplay.h"

#define AHEAD 400u                      /* messages are sent so long before their times: ms */

void mp_init(struct midiplay *p, const struct smf *m, unsigned long length)
{
    memset(p, 0, sizeof *p);
    p->mid = m, p->length = length, p->volume = 256;
}

/* The port told what the file has said before message `upto`. */
static void chase(struct midiplay *p, unsigned long upto)
{
    static unsigned char cc[16][120], prog[16], range[16];      /* (one at a time: the caller's rule) */
    static unsigned short bend[16];
    static struct glos_midi_msg m[GLOS_MIDI_QUEUE];
    static const unsigned char first[][2] = { { 1, 0 }, { 7, 100 }, { 10, 64 }, { 11, 127 }, { 64, 0 } };
    const struct smf_event *e = smf_events(p->mid);
    unsigned long i;
    unsigned ch, k, n = 0;
    memset(cc, 0xFF, sizeof cc);
    memset(prog, 0, sizeof prog);
    memset(range, 0xFF, sizeof range);
    for (ch = 0; ch < 16; ch++) {
        bend[ch] = 0x2000;
        for (k = 0; k < sizeof first / sizeof first[0]; k++)
            cc[ch][first[k][0]] = first[k][1];
    }
    for (i = 0; i < upto; i++) {
        ch = e[i].status & 15;
        switch (e[i].status & 0xF0) {
        case 0xB0:
            if (e[i].d1 == 6 && cc[ch][101] == 0 && cc[ch][100] == 0)   /* (data for parameter 0: the bend's range) */
                range[ch] = e[i].d2;
            else if (e[i].d1 < 120)
                cc[ch][e[i].d1] = e[i].d2;
            break;
        case 0xC0:
            prog[ch] = e[i].d1;
            break;
        case 0xE0:
            bend[ch] = (unsigned short)(e[i].d1 | e[i].d2 << 7);
            break;
        }
    }
#define PUT(s, a, b) do { if (n < GLOS_MIDI_QUEUE) { m[n].time = 0, m[n].status = (unsigned char)(s), m[n].data1 = (unsigned char)(a), \
                                                     m[n].data2 = (unsigned char)(b), m[n].pad = 0, n++; } } while (0)
    for (ch = 0; ch < 16; ch++) {
        PUT(0xB0 | ch, 121, 0);                 /* every controller as it begins */
        for (k = 0; k < 120; k++)               /* (6, 38 and 96 to 101 are no settings by themselves) */
            if (cc[ch][k] != 0xFF && k != 6 && k != 38 && !(k >= 96 && k <= 101))
                PUT(0xB0 | ch, k, cc[ch][k]);
        if (range[ch] != 0xFF) {
            PUT(0xB0 | ch, 101, 0);
            PUT(0xB0 | ch, 100, 0);
            PUT(0xB0 | ch, 6, range[ch]);
            PUT(0xB0 | ch, 101, 127);
            PUT(0xB0 | ch, 100, 127);
        }
        PUT(0xC0 | ch, prog[ch], 0);
        PUT(0xE0 | ch, bend[ch] & 0x7F, bend[ch] >> 7);
    }
#undef PUT
    glos_midi_send(p->port, m, n, NULL, NULL);
}

static void go(struct midiplay *p)      /* on from p->at: the port told how things stand there */
{
    const struct smf_event *e = smf_events(p->mid);
    unsigned long lo = 0, hi = smf_count(p->mid);
    while (lo < hi) {                           /* the first message at or after p->at */
        unsigned long mid = lo + (hi - lo) / 2;
        if (e[mid].ms < p->at)
            lo = mid + 1;
        else
            hi = mid;
    }
    p->next = lo;
    glos_midi_flush(p->port);
    chase(p, lo);
    p->base = glos_ticks() - (unsigned)p->at;
}

glos_handle mp_open_out(void)
{
    char v[16] = "";
    int set = 0;
    unsigned first = GLOS_MIDI_PORT_FM, other = GLOS_MIDI_PORT_MPU;
    glos_handle h;
    if (glos_setting_get("sound", "midi", v, sizeof v, &set) == 0 && !strcmp(v, "port"))
        first = GLOS_MIDI_PORT_MPU, other = GLOS_MIDI_PORT_FM;
    if (!(h = glos_midi_out(first)))
        h = glos_midi_out(other);
    return h;
}

int mp_play(struct midiplay *p)
{
    if (p->playing)
        return 0;
    if (!p->port)
        p->port = mp_open_out();        /* (the one the settings prefer, else the other) */
    if (!p->port)
        return -1;
    go(p);
    p->playing = 1;
    mp_feed(p);
    return 0;
}

void mp_pause(struct midiplay *p)
{
    if (!p->playing)
        return;
    p->at = mp_where(p);
    p->playing = 0;
    if (p->port)
        glos_midi_flush(p->port);
}

void mp_stop(struct midiplay *p)
{
    if (p->port) {
        glos_midi_flush(p->port);
        glos_close(p->port);
    }
    p->port = 0, p->playing = 0, p->at = 0, p->next = 0;
}

void mp_seek(struct midiplay *p, unsigned long ms)
{
    p->at = ms > p->length ? p->length : ms;
    if (p->playing)
        go(p);
}

unsigned long mp_where(const struct midiplay *p)
{
    unsigned long at = p->playing ? glos_ticks() - p->base : p->at;
    return at > p->length ? p->length : at;
}

int mp_feed(struct midiplay *p)
{
    const struct smf_event *e;
    unsigned long count;
    unsigned at;
    if (!p->playing || !p->port)
        return 0;
    e = smf_events(p->mid), count = smf_count(p->mid);
    at = glos_ticks() - p->base;
    for (;;) {
        struct glos_midi_msg m[64];
        unsigned n = 0, taken = 0;
        for (; n < 64 && p->next + n < count && e[p->next + n].ms < at + AHEAD; n++) {
            const struct smf_event *v = &e[p->next + n];
            unsigned force = v->d2;
            if ((v->status & 0xF0) == 0x90) {   /* (the volume) */
                force = force * (unsigned)p->volume / 256;
                force = force ? force : 1;
            }
            m[n].time = p->base + (unsigned)v->ms;
            m[n].time += !m[n].time;            /* (0 would be "now") */
            m[n].status = v->status, m[n].data1 = v->d1, m[n].data2 = (unsigned char)force, m[n].pad = 0;
        }
        if (!n)
            break;
        glos_midi_send(p->port, m, n, &taken, NULL);
        p->next += taken;
        if (taken < n)                          /* (the port's queue is full, or a session has the machine: the
                                                   rest at the next look) */
            break;
    }
    if (p->next >= count && at >= p->length) {  /* played out */
        mp_stop(p);
        return 1;
    }
    return 0;
}
