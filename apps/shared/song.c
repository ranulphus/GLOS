/* A song as MIDI Studio holds it (song.h): made, changed, read from a MIDI file and written as one.
 *
 * Reading: smf.c gives the file's channel messages in their order, each with its track and its tick. A song
 * track is made for each (file track, channel) that has messages; a note on is held until its note off (or
 * until the same key is struck again, which ends it there; or the song's end); the first instrument, volume
 * (controller 7) and place (controller 10) a channel is given before its first note become the track's own;
 * everything else is kept as events. Ticks are scaled to SONG_PPQ.
 *
 * Writing: a type 1 file: a first track with the title, the time signature and the tempo's changes, then a
 * track for each of the song's: its name, its instrument, volume and place, and its notes and events in the
 * order of their times (at one time: notes' ends, then events, then notes' starts), with running status. */
#include <stdlib.h>
#include <string.h>

#include "smf.h"
#include "song.h"

static struct song *blank(void)         /* a song with no track yet */
{
    struct song *s = calloc(1, sizeof *s);
    if (!s || !(s->tempo = malloc(4 * sizeof *s->tempo))) {
        free(s);
        return NULL;
    }
    s->tempo_room = 4, s->ntempo = 1;
    s->tempo[0].at = 0, s->tempo[0].us = 500000ul;
    s->beats = 4, s->unit = 4;
    return s;
}

struct song *song_new(void)
{
    struct song *s = blank();
    if (s)
        song_track_add(s, -1);
    return s;
}

void song_free(struct song *s)
{
    unsigned i;
    if (!s)
        return;
    for (i = 0; i < s->ntracks; i++) {
        free(s->track[i].notes);
        free(s->track[i].events);
    }
    free(s->tempo);
    free(s);
}

int song_track_add(struct song *s, int channel)
{
    struct song_track *t;
    unsigned used = 0, i;
    if (s->ntracks == SONG_TRACKS)
        return SONG_E_ROOM;
    if (channel < 0) {                          /* a channel no track has, the percussion's apart */
        for (i = 0; i < s->ntracks; i++)
            used |= 1u << s->track[i].channel;
        for (channel = 0; channel < 16 && ((used >> channel & 1) || channel == SONG_DRUMS); channel++)
            ;
        if (channel == 16)
            return SONG_E_ROOM;
    }
    t = &s->track[s->ntracks];
    memset(t, 0, sizeof *t);
    t->channel = (unsigned char)(channel & 15), t->volume = 100, t->pan = 64;
    return (int)s->ntracks++;
}

void song_track_remove(struct song *s, unsigned t)
{
    if (t >= s->ntracks)
        return;
    free(s->track[t].notes);
    free(s->track[t].events);
    memmove(&s->track[t], &s->track[t + 1], (s->ntracks - t - 1) * sizeof s->track[0]);
    s->ntracks--;
}

static int note_room(struct song_track *t)
{
    if (t->nnotes == t->note_room) {
        unsigned long room = t->note_room ? t->note_room * 2 : 64;
        struct song_note *n = realloc(t->notes, room * sizeof *n);
        if (!n)
            return -1;
        t->notes = n, t->note_room = room;
    }
    return 0;
}

long song_note_add(struct song_track *t, unsigned long at, unsigned long len, unsigned key, unsigned force)
{
    unsigned long lo = 0, hi = t->nnotes;
    struct song_note *n;
    if (note_room(t) != 0)
        return SONG_E_MEMORY;
    while (lo < hi) {                           /* after those that start no later */
        unsigned long mid = lo + (hi - lo) / 2;
        if (t->notes[mid].at <= at)
            lo = mid + 1;
        else
            hi = mid;
    }
    memmove(&t->notes[lo + 1], &t->notes[lo], (t->nnotes - lo) * sizeof t->notes[0]);
    t->nnotes++;
    n = &t->notes[lo];
    memset(n, 0, sizeof *n);
    n->at = at, n->len = len ? len : 1;
    n->key = (unsigned char)(key > 127 ? 127 : key), n->force = (unsigned char)(force > 127 ? 127 : force ? force : 1);
    return (long)lo;
}

long song_event_add(struct song_track *t, unsigned long at, unsigned status, unsigned d1, unsigned d2)
{
    unsigned long lo = 0, hi = t->nevents;
    struct song_event *e;
    while (lo < hi) {                   /* (after every event at its time or before) */
        unsigned long mid = (lo + hi) / 2;
        if (t->events[mid].at <= at)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (t->nevents == t->event_room) {
        unsigned long room = t->event_room ? t->event_room * 2 : 64;
        if (!(e = realloc(t->events, room * sizeof *e)))
            return SONG_E_MEMORY;
        t->events = e, t->event_room = room;
    }
    memmove(&t->events[lo + 1], &t->events[lo], (t->nevents - lo) * sizeof t->events[0]);
    e = &t->events[lo];
    e->at = at, e->status = (unsigned char)(status & 0xF0), e->d1 = (unsigned char)(d1 & 0x7F), e->d2 = (unsigned char)(d2 & 0x7F);
    e->pad = 0;
    t->nevents++;
    return (long)lo;
}

void song_event_remove(struct song_track *t, unsigned long i)
{
    if (i >= t->nevents)
        return;
    memmove(&t->events[i], &t->events[i + 1], (t->nevents - i - 1) * sizeof t->events[0]);
    t->nevents--;
}

void song_note_remove(struct song_track *t, unsigned long i)
{
    if (i >= t->nnotes)
        return;
    memmove(&t->notes[i], &t->notes[i + 1], (t->nnotes - i - 1) * sizeof t->notes[0]);
    t->nnotes--;
}

static int by_start(const void *a, const void *b)
{
    const struct song_note *x = a, *y = b;
    return x->at != y->at ? (x->at < y->at ? -1 : 1) : x->key != y->key ? (x->key < y->key ? -1 : 1)
           : x->len != y->len ? (x->len < y->len ? -1 : 1) : 0;
}

void song_sort(struct song_track *t)
{
    if (t->nnotes > 1)
        qsort(t->notes, t->nnotes, sizeof t->notes[0], by_start);
}

unsigned long song_ticks(const struct song *s)
{
    unsigned long end = 0, i;
    unsigned k;
    for (k = 0; k < s->ntracks; k++) {
        const struct song_track *t = &s->track[k];
        for (i = 0; i < t->nnotes; i++)
            if (t->notes[i].at + t->notes[i].len > end)
                end = t->notes[i].at + t->notes[i].len;
        if (t->nevents && t->events[t->nevents - 1].at > end)
            end = t->events[t->nevents - 1].at;
    }
    return end;
}

/* ---- the tempo */

unsigned long song_ms(const struct song *s, unsigned long tick)
{
    unsigned long long us = 0;                  /* microseconds times SONG_PPQ */
    unsigned i;
    for (i = 0; i < s->ntempo && s->tempo[i].at < tick; i++) {
        unsigned long to = i + 1 < s->ntempo && s->tempo[i + 1].at < tick ? s->tempo[i + 1].at : tick;
        us += (unsigned long long)(to - s->tempo[i].at) * s->tempo[i].us;
    }
    us /= SONG_PPQ * 1000ull;
    return us > 0x7FFFFFFFul ? 0x7FFFFFFFul : (unsigned long)us;
}

unsigned long song_tick(const struct song *s, unsigned long ms)
{
    unsigned long long left = (unsigned long long)ms * 1000 * SONG_PPQ, tick;
    unsigned i;
    for (i = 0; i + 1 < s->ntempo; i++) {
        unsigned long long span = (unsigned long long)(s->tempo[i + 1].at - s->tempo[i].at) * s->tempo[i].us;
        if (left < span)
            break;
        left -= span;
    }
    tick = s->tempo[i].at + left / s->tempo[i].us;
    return tick > 0x3FFFFFFFul ? 0x3FFFFFFFul : (unsigned long)tick;
}

static int tempo_put(struct song *s, unsigned long at, unsigned long us)       /* kept in order; one at a tick */
{
    unsigned i;
    for (i = 0; i < s->ntempo && s->tempo[i].at < at; i++)
        ;
    if (i < s->ntempo && s->tempo[i].at == at) {
        s->tempo[i].us = us;
        return SONG_OK;
    }
    if (s->ntempo == s->tempo_room) {
        struct song_tempo *t = realloc(s->tempo, s->tempo_room * 2 * sizeof *t);
        if (!t)
            return SONG_E_MEMORY;
        s->tempo = t, s->tempo_room *= 2;
    }
    memmove(&s->tempo[i + 1], &s->tempo[i], (s->ntempo - i) * sizeof s->tempo[0]);
    s->tempo[i].at = at, s->tempo[i].us = us;
    s->ntempo++;
    return SONG_OK;
}

int song_tempo_set(struct song *s, unsigned long at, unsigned bpm)
{
    bpm = bpm < 20 ? 20 : bpm > 400 ? 400 : bpm;
    return tempo_put(s, at, 60000000ul / bpm);
}

unsigned song_tempo_at(const struct song *s, unsigned long at)
{
    unsigned i;
    for (i = 0; i + 1 < s->ntempo && s->tempo[i + 1].at <= at; i++)
        ;
    return (unsigned)((60000000ul + s->tempo[i].us / 2) / s->tempo[i].us);
}

/* ---- from a MIDI file */

struct held {                           /* a track's notes on and not yet off */
    unsigned long at[128];
    unsigned char force[128];
    unsigned char began, program, volume, pan;  /* a note was struck; its instrument, volume and place are said */
};

static int event_add(struct song_track *t, unsigned long at, unsigned status, unsigned d1, unsigned d2)
{
    struct song_event *e;
    if (t->nevents == t->event_room) {
        unsigned long room = t->event_room ? t->event_room * 2 : 64;
        if (!(e = realloc(t->events, room * sizeof *e)))
            return -1;
        t->events = e, t->event_room = room;
    }
    e = &t->events[t->nevents++];
    e->at = at, e->status = (unsigned char)status, e->d1 = (unsigned char)d1, e->d2 = (unsigned char)d2, e->pad = 0;
    return 0;
}

static int let_go(struct song_track *t, struct held *h, unsigned key, unsigned long at)
{
    struct song_note *n;
    if (note_room(t) != 0)
        return -1;
    n = &t->notes[t->nnotes++];                 /* (in the order they end: sorted when all are in) */
    memset(n, 0, sizeof *n);
    n->at = h->at[key], n->len = at > h->at[key] ? at - h->at[key] : 1;
    n->key = (unsigned char)key, n->force = h->force[key];
    h->force[key] = 0;
    return 0;
}

static unsigned long scaled(unsigned long long tick)    /* (no further than a file's numbers can say) */
{
    return tick > 0x0FFFFFFFul ? 0x0FFFFFFFul : (unsigned long)tick;
}

struct song *song_read(const unsigned char *data, unsigned long size, int *err)
{
    struct smf *m;
    struct song *s = NULL;
    struct held *held = NULL;
    signed char *map = NULL;                    /* (file track, channel): the song's track, or -1 */
    const struct smf_event *e;
    const struct smf_tempo *tempo;
    unsigned long i, count, end;
    unsigned div, ntempo, k, key, tracks;
    int dummy, r;
    if (!err)
        err = &dummy;
    if (!(m = smf_open(data, size, &r))) {
        *err = r == SMF_E_KIND ? SONG_E_KIND : r == SMF_E_FORMAT ? SONG_E_FORMAT : r == SMF_E_MEMORY ? SONG_E_MEMORY
               : SONG_E_DAMAGED;
        return NULL;
    }
    *err = SONG_E_MEMORY;
    tracks = smf_tracks(m), div = smf_division(m);
    if (!(s = blank()) || !(held = calloc(SONG_TRACKS, sizeof *held)) || !(map = malloc(tracks * 16ul)))
        goto bad;
    memset(map, -1, tracks * 16ul);
    /* a file's tick as the song's (a file in film time: by its milliseconds, at the tempo a song begins with) */
#define TICK(ms, tick) scaled(div ? ((unsigned long long)(tick) * SONG_PPQ + div / 2) / div \
                                  : (unsigned long long)(ms) * SONG_PPQ / 500)
    e = smf_events(m), count = smf_count(m);
    for (i = 0; i < count; i++) {
        unsigned ch = e[i].status & 15, kind = e[i].status & 0xF0, d1 = e[i].d1, d2 = e[i].d2;
        unsigned long at = TICK(e[i].ms, e[i].tick);
        int ti = map[e[i].track * 16u + ch];
        struct song_track *t;
        struct held *h;
        if (ti < 0) {
            if ((ti = song_track_add(s, (int)ch)) >= 0) {
                if (tracks > 1)                 /* (a lone track's name is the song's) */
                    strncpy(s->track[ti].name, smf_track_name(m, e[i].track), sizeof s->track[ti].name - 1);
            } else {                            /* no room for another: with a track of its channel, if there is one */
                for (k = 0; k < s->ntracks && s->track[k].channel != ch; k++)
                    ;
                if (k == s->ntracks)
                    continue;
                ti = (int)k;
            }
            map[e[i].track * 16u + ch] = (signed char)ti;
        }
        t = &s->track[ti], h = &held[ti];
        r = 0;
        if (kind == 0x90) {
            if (h->force[d1])                   /* struck again while it is held: the first ends here */
                r = let_go(t, h, d1, at);
            h->at[d1] = at, h->force[d1] = (unsigned char)d2, h->began = 1;
        } else if (kind == 0x80) {
            if (h->force[d1])
                r = let_go(t, h, d1, at);
        } else if (kind == 0xC0 && !h->began && !h->program) {
            t->program = (unsigned char)d1, h->program = 1;
        } else if (kind == 0xB0 && d1 == 7 && !h->began && !h->volume) {
            t->volume = (unsigned char)d2, h->volume = 1;
        } else if (kind == 0xB0 && d1 == 10 && !h->began && !h->pan) {
            t->pan = (unsigned char)d2, h->pan = 1;
        } else {
            r = event_add(t, at, kind, d1, d2);
        }
        if (r != 0)
            goto bad;
    }
    end = TICK(smf_length(m), smf_ticks(m));
    for (k = 0; k < s->ntracks; k++) {          /* what is still held ends with the song */
        for (key = 0; key < 128; key++)
            if (held[k].force[key] && let_go(&s->track[k], &held[k], key, end) != 0)
                goto bad;
        song_sort(&s->track[k]);
    }
    tempo = smf_tempos(m, &ntempo);
    for (k = 0; k < ntempo && div; k++)
        if (tempo_put(s, TICK(0, tempo[k].tick), tempo[k].us) != SONG_OK)
            goto bad;
#undef TICK
    smf_meter(m, &s->beats, &s->unit);
    strncpy(s->title, smf_title(m), sizeof s->title - 1);
    if (!s->ntracks)
        song_track_add(s, -1);
    free(held);
    free(map);
    smf_free(m);
    *err = SONG_OK;
    return s;
bad:
    free(held);
    free(map);
    song_free(s);
    smf_free(m);
    return NULL;
}

/* ---- to a MIDI file */

struct bytes {
    unsigned char *p;
    unsigned long n, room;
    int bad;
};

static void put(struct bytes *b, const void *data, unsigned long n)
{
    if (b->n + n > b->room) {
        unsigned long room = (b->room ? b->room * 2 : 1024) + n;
        unsigned char *p = realloc(b->p, room);
        if (!p) {
            b->bad = 1;
            return;
        }
        b->p = p, b->room = room;
    }
    memcpy(b->p + b->n, data, n);
    b->n += n;
}

static void put1(struct bytes *b, unsigned v)
{
    unsigned char c = (unsigned char)v;
    put(b, &c, 1);
}

static void put32(struct bytes *b, unsigned long v)
{
    unsigned char c[4] = { (unsigned char)(v >> 24), (unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v };
    put(b, c, 4);
}

static void put_number(struct bytes *b, unsigned long v)        /* seven bits a byte, the last with no top bit */
{
    unsigned char c[5];
    int n = 0;
    c[4 - n++] = (unsigned char)(v & 0x7F);
    while ((v >>= 7) != 0 && n < 5)
        c[4 - n++] = (unsigned char)(0x80 | (v & 0x7F));
    put(b, c + 5 - n, (unsigned long)n);
}

static void put_text(struct bytes *b, unsigned kind, const char *text)  /* (after its ticks) a name, if there is one */
{
    if (text[0]) {
        put1(b, 0), put1(b, 0xFF), put1(b, kind);
        put_number(b, (unsigned long)strlen(text));
        put(b, text, (unsigned long)strlen(text));
    }
}

static void put_track(struct bytes *b, const struct bytes *t)   /* a track's bytes as a chunk, its end added */
{
    static const unsigned char end[4] = { 0, 0xFF, 0x2F, 0 };
    put(b, "MTrk", 4);
    put32(b, t->n + 4);
    put(b, t->p, t->n);
    put(b, end, 4);
}

struct item {                           /* a message of a track, to be put in order */
    unsigned long at, seq;
    unsigned char rank, status, d1, d2;
};

static int in_order(const void *a, const void *b)
{
    const struct item *x = a, *y = b;
    return x->at != y->at ? (x->at < y->at ? -1 : 1) : x->rank != y->rank ? (x->rank < y->rank ? -1 : 1)
           : x->seq < y->seq ? -1 : x->seq > y->seq;
}

int song_write(const struct song *s, int heard, unsigned char **data, unsigned long *size)
{
    struct bytes out = { 0, 0, 0, 0 }, t = { 0, 0, 0, 0 };
    struct item *items = NULL;
    unsigned char head[14] = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, 0, SONG_PPQ >> 8, SONG_PPQ & 0xFF };
    unsigned long i, last;
    unsigned k, written = 1, solo = 0, log2unit = 0;
    for (k = 0; k < s->ntracks; k++)
        solo |= s->track[k].solo;
    put(&out, head, 14);
    /* the first track: the song's name, its time signature, its tempo and the changes of it */
    put_text(&t, 0x03, s->title);
    while ((1u << log2unit) < s->unit && log2unit < 6)
        log2unit++;
    put1(&t, 0), put1(&t, 0xFF), put1(&t, 0x58), put1(&t, 4), put1(&t, s->beats), put1(&t, log2unit), put1(&t, 24), put1(&t, 8);
    for (k = 0, last = 0; k < s->ntempo; k++) {
        put_number(&t, s->tempo[k].at - last);
        last = s->tempo[k].at;
        put1(&t, 0xFF), put1(&t, 0x51), put1(&t, 3);
        put1(&t, s->tempo[k].us >> 16), put1(&t, s->tempo[k].us >> 8), put1(&t, s->tempo[k].us);
    }
    put_track(&out, &t);
    for (k = 0; k < s->ntracks; k++) {
        const struct song_track *tr = &s->track[k];
        unsigned long n = 0;
        unsigned ch = tr->channel, running = 0;
        if (heard && (tr->mute || (solo && !tr->solo)))
            continue;
        if (!(items = malloc((tr->nnotes * 2 + tr->nevents + 1) * sizeof *items))) {
            t.bad = 1;
            break;
        }
        for (i = 0; i < tr->nnotes; i++) {      /* at one time: notes' ends, then events, then notes' starts */
            const struct song_note *nt = &tr->notes[i];
            items[n].at = nt->at, items[n].seq = n, items[n].rank = 2;
            items[n].status = (unsigned char)(0x90 | ch), items[n].d1 = nt->key, items[n].d2 = nt->force ? nt->force : 1;
            n++;
            items[n].at = nt->at + nt->len, items[n].seq = n, items[n].rank = 0;
            items[n].status = (unsigned char)(0x80 | ch), items[n].d1 = nt->key, items[n].d2 = 0;
            n++;
        }
        for (i = 0; i < tr->nevents; i++, n++) {
            items[n].at = tr->events[i].at, items[n].seq = n, items[n].rank = 1;
            items[n].status = (unsigned char)((tr->events[i].status & 0xF0) | ch);
            items[n].d1 = tr->events[i].d1 & 0x7F, items[n].d2 = tr->events[i].d2 & 0x7F;
        }
        qsort(items, n, sizeof *items, in_order);
        t.n = 0;
        put_text(&t, 0x03, tr->name);
        put1(&t, 0), put1(&t, 0xC0 | ch), put1(&t, tr->program & 0x7F);
        put1(&t, 0), put1(&t, 0xB0 | ch), put1(&t, 7), put1(&t, tr->volume & 0x7F);
        put1(&t, 0), put1(&t, 0xB0 | ch), put1(&t, 10), put1(&t, tr->pan & 0x7F);
        for (i = 0, last = 0; i < n; i++) {
            unsigned kind = items[i].status & 0xF0;
            if (kind < 0x80 || kind > 0xE0)     /* (no channel message: not written) */
                continue;
            put_number(&t, items[i].at - last);
            last = items[i].at;
            if (items[i].status != running)     /* (running status: the same first byte is left out) */
                put1(&t, running = items[i].status);
            put1(&t, items[i].d1);
            if (kind != 0xC0 && kind != 0xD0)
                put1(&t, items[i].d2);
        }
        free(items);
        put_track(&out, &t);
        written++;
    }
    free(t.p);
    if (out.bad || t.bad) {
        free(out.p);
        return SONG_E_MEMORY;
    }
    out.p[10] = (unsigned char)(written >> 8), out.p[11] = (unsigned char)written;
    *data = out.p, *size = out.n;
    return SONG_OK;
}
