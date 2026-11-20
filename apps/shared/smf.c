/* Standard MIDI Files read (smf.h), for the Player (phase F).
 *
 * A .MID: a header ("MThd": its type, how many tracks, and what a tick is: so many to the quarter note, or so
 * many to a frame of film time) and tracks ("MTrk"), each a run of events with the ticks between them: channel
 * messages (with "running status": a message of the same kind as the last leaves its first byte out), system
 * exclusive messages, and "meta" events that are for the reader (the tempo, a quarter note's microseconds;
 * names; the track's end). A type 1 file's tracks play together; the tempo is the song's, whichever track sets
 * it.
 *
 * The tracks are read side by side, the earliest event next, so that the messages come out in the order they
 * are to be sent and each tempo change is met in its turn. Whatever the file's bytes are, nothing is read
 * outside it. */
#include <stdlib.h>
#include <string.h>

#include "smf.h"

#define MAX_TRACKS 256
#define MAX_EVENTS 4000000ul

struct track {
    const unsigned char *p, *end;
    unsigned long tick;                 /* its next event's */
    unsigned char running, done;
};

struct smf {
    struct smf_event *ev;
    unsigned long count, room, length;
    unsigned tracks, channels;
    char title[64];
};

static unsigned long be32(const unsigned char *p)
{
    return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

/* A number of the file's own kind (seven bits a byte, the last with no top bit) at *p, not past end. */
static unsigned long number(const unsigned char **p, const unsigned char *end)
{
    unsigned long v = 0;
    int i;
    for (i = 0; i < 4 && *p < end; i++) {
        unsigned b = *(*p)++;
        v = v << 7 | (b & 0x7F);
        if (!(b & 0x80))
            break;
    }
    return v;
}

static void ahead(struct track *t)      /* the ticks to its next event read; done where there is none */
{
    unsigned long d;
    if (t->p >= t->end) {
        t->done = 1;
        return;
    }
    d = number(&t->p, t->end);
    if (t->p >= t->end || d > 0x3FFFFFFFul - t->tick)
        t->done = 1;
    else
        t->tick += d;
}

static int add(struct smf *m, unsigned long ms, unsigned status, unsigned d1, unsigned d2, unsigned track)
{
    struct smf_event *e;
    if (m->count == m->room) {
        unsigned long room = m->room ? m->room * 2 : 1024;
        if (room > MAX_EVENTS || !(e = realloc(m->ev, room * sizeof *e)))
            return -1;
        m->ev = e, m->room = room;
    }
    e = &m->ev[m->count++];
    e->ms = ms, e->status = (unsigned char)status, e->d1 = (unsigned char)d1, e->d2 = (unsigned char)d2;
    e->track = (unsigned char)track;
    return 0;
}

struct smf *smf_open(const unsigned char *data, unsigned long size, int *err)
{
    struct track *tr;
    struct smf *m;
    unsigned long hlen, at, last = 0, tempo = 500000ul, div;
    unsigned long long time = 0;                /* microseconds times div */
    unsigned format, want, n = 0, i, notes = 0;
    int dummy;
    if (!err)
        err = &dummy;
    *err = SMF_E_KIND;
    if (size >= 20 && !memcmp(data, "RIFF", 4) && !memcmp(data + 8, "RMID", 4)) {      /* in a wrapper: its "data" */
        for (at = 12; size - at >= 8; ) {
            unsigned long len = data[at + 4] | (unsigned long)data[at + 5] << 8 | (unsigned long)data[at + 6] << 16
                                | (unsigned long)data[at + 7] << 24;
            if (len > size - at - 8)
                len = size - at - 8;
            if (!memcmp(data + at, "data", 4)) {
                data += at + 8, size = len;
                break;
            }
            at += 8 + len + (len & 1);
            if (at > size)
                return NULL;
        }
    }
    if (size < 14 || memcmp(data, "MThd", 4))
        return NULL;
    hlen = be32(data + 4);
    format = data[8] << 8 | data[9], want = data[10] << 8 | data[11], div = data[12] << 8 | data[13];
    *err = SMF_E_FORMAT;
    if (format == 2)
        return NULL;
    *err = SMF_E_DAMAGED;
    if (hlen < 6 || hlen > size - 8 || format > 2 || !div)
        return NULL;
    if (div & 0x8000) {                         /* ticks to a frame of film time: no tempo in it */
        unsigned fps = 256 - (div >> 8), sub = div & 0xFF;
        if (fps == 29)
            fps = 30;
        if ((fps != 24 && fps != 25 && fps != 30) || !sub)
            return NULL;
        div = fps * sub, tempo = 1000000ul;
    }
    *err = SMF_E_MEMORY;
    if (!(tr = calloc(MAX_TRACKS, sizeof *tr)))
        return NULL;
    *err = SMF_E_DAMAGED;
    for (at = 8 + hlen; n < want && n < MAX_TRACKS && size - at >= 8; ) {      /* where each track is */
        unsigned long len = be32(data + at + 4);
        if (len > size - at - 8)                /* (cut short: what there is of it) */
            len = size - at - 8;
        if (!memcmp(data + at, "MTrk", 4)) {
            tr[n].p = data + at + 8, tr[n].end = tr[n].p + len;
            ahead(&tr[n]);
            n++;
        }
        at += 8 + len;
    }
    *err = n ? SMF_E_MEMORY : SMF_E_DAMAGED;
    if (!n || !(m = calloc(1, sizeof *m))) {
        free(tr);
        return NULL;
    }
    m->tracks = n;
    for (;;) {
        struct track *t = NULL;
        unsigned b, status, which;
        unsigned long len, ms;
        for (i = 0; i < n; i++)                 /* the track whose next event is the earliest */
            if (!tr[i].done && (!t || tr[i].tick < t->tick))
                t = &tr[i];
        if (!t)
            break;
        which = (unsigned)(t - tr);
        time += (unsigned long long)(t->tick - last) * tempo;
        last = t->tick;
        {
            unsigned long long q = time / ((unsigned long long)div * 1000);
            ms = q > 0x7FFFFFFFul ? 0x7FFFFFFFul : (unsigned long)q;
        }
        m->length = ms;
        b = *t->p;
        if (b == 0xFF) {                        /* for the reader */
            unsigned kind;
            t->p++;
            if (t->p >= t->end) {
                t->done = 1;
                continue;
            }
            kind = *t->p++;
            len = number(&t->p, t->end);
            if (len > (unsigned long)(t->end - t->p))
                len = (unsigned long)(t->end - t->p);
            if (kind == 0x51 && len >= 3 && !(data[12] & 0x80)) {
                tempo = (unsigned long)t->p[0] << 16 | (unsigned long)t->p[1] << 8 | t->p[2];
                if (!tempo)
                    tempo = 500000ul;
            } else if (kind == 0x03 && !which && !m->title[0]) {
                unsigned k, c = 0;
                for (k = 0; k < len && c < sizeof m->title - 1; k++)
                    if (t->p[k] >= 32 && t->p[k] < 127)
                        m->title[c++] = (char)t->p[k];
                while (c && m->title[c - 1] == ' ')
                    c--;
                m->title[c] = 0;
            }
            t->p += len;
            if (kind == 0x2F)
                t->done = 1;
            else
                ahead(t);
            continue;
        }
        if (b == 0xF0 || b == 0xF7) {           /* system exclusive: passed over */
            t->p++;
            len = number(&t->p, t->end);
            t->p += len > (unsigned long)(t->end - t->p) ? (unsigned long)(t->end - t->p) : len;
            ahead(t);
            continue;
        }
        if (b & 0x80)
            t->running = (unsigned char)b, t->p++;
        status = t->running;
        len = (status & 0xF0) == 0xC0 || (status & 0xF0) == 0xD0 ? 1 : 2;
        if (status < 0x80 || status >= 0xF0 || (unsigned long)(t->end - t->p) < len) {  /* (no sense: the track ends) */
            t->done = 1;
            continue;
        }
        {
            unsigned d1 = t->p[0] & 0x7F, d2 = len == 2 ? t->p[1] & 0x7F : 0;
            if ((status & 0xF0) == 0x90 && !d2)
                status -= 0x10;
            if ((status & 0xF0) == 0x90)
                notes |= 1u << (status & 15);
            if (add(m, ms, status, d1, d2, which) != 0) {
                smf_free(m);
                free(tr);
                return NULL;
            }
        }
        t->p += len;
        ahead(t);
    }
    free(tr);
    for (i = 0; i < 16; i++)
        m->channels += notes >> i & 1;
    *err = SMF_OK;
    return m;
}

void smf_free(struct smf *m)
{
    if (m)
        free(m->ev);
    free(m);
}

unsigned long smf_count(const struct smf *m) { return m->count; }
const struct smf_event *smf_events(const struct smf *m) { return m->ev; }
unsigned long smf_length(const struct smf *m) { return m->length; }
const char *smf_title(const struct smf *m) { return m->title; }
unsigned smf_tracks(const struct smf *m) { return m->tracks; }
unsigned smf_channels(const struct smf *m) { return m->channels; }
