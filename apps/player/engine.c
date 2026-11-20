/* The Player's playing (engine.h). A thread of its own looks every 15 ms at how much room the stream's ring has
 * and fills it from the file (apps/shared/source.c: a WAV read piece by piece, a module played); when the file
 * has given its last and the ring has played out, the stream is closed and the window is told (eng_now's
 * `ended`). The window's calls and the thread share one lock; the thread holds it only while it fills.
 *
 * A packed file (MP3, Ogg Vorbis) costs sums a 486 may not have the time for: the thread does a tenth of a
 * second's work at most in a turn (the window's calls wait for it no longer than that), and when the stream has
 * run dry at twenty looks in a row the file is stopped and the window told (`slow`): said, rather than broken
 * sound. The unpackers want room on the thread's stack (a quarter of a megabyte).
 *
 * A seek while it plays closes the stream and opens another: what was queued (a third of a second) is not heard
 * after the new place has been asked for. A module is played at the mixer's own rate (it costs the least there:
 * on a 486 the mixer runs at 22050 Hz).
 *
 * A MIDI file (apps/shared/smf.c) is no samples: its messages go to a MIDI port (AUDIO's: the card's FM chip,
 * played by GLOS's own driver, or else an MPU-401), each with its time on GLOS's clock, and GLOS plays each when
 * its time comes. The thread sends those of the next 400 ms at each look. To the window it is a file of 1000
 * "frames" a second. A pause drops what waits and ends the notes; going on, or a seek, first tells the port what
 * the file had said up to there (each channel's instrument, controllers, bend and bend range) and then sends on
 * from there: no note from before the place is struck late. The Player's volume is the force of the notes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/glos.h>

#include "../shared/smf.h"
#include "engine.h"

#define FRAMES 2048u
#define MIDI_MAX (8ul << 20)            /* a MIDI file at most */
#define MIDI_AHEAD 400u                 /* its messages are sent so long before their times: ms */
#define MIDI_TAIL 800ul                 /* ... and its last notes given so long to die away */

static struct {
    glos_handle lock, thread;
    int demo, quit;
    struct source *src;
    struct source_info info;
    int state, ended, eof, volume;
    int slow, dry;                      /* stopped as too slow (told once); looks in a row at a stream run dry */
    unsigned long fed;                  /* frames written since it began to play (the first second is no measure) */
    glos_handle out;
    struct glos_audio_ring *ring;
    unsigned id;
    unsigned long demo_at;
    struct smf *mid;                    /* a MIDI file (no src then, and no stream) */
    glos_handle port;                   /* ... its port, while it plays or is paused */
    unsigned long m_next;               /* the next of its messages to send */
    unsigned long m_at;                 /* where it is while it does not play: ms */
    unsigned m_base;                    /* while it plays: GLOS's clock at what would have been its start */
    short buf[FRAMES * 2];
} E = { .volume = 256 };

static void take(void) { if (E.lock) glos_take(E.lock, 0xFFFFFFFFu); }
static void give(void) { if (E.lock) glos_give(E.lock); }

static unsigned mixer_rate(void)
{
    struct glos_audio_device dev[8];
    unsigned n = 0;
    if (!E.demo && glos_audio_devices(dev, sizeof dev, &n) == 0 && n && dev[0].kind == GLOS_AUDIO_DEV_OUT && dev[0].rate >= 8000)
        return dev[0].rate;
    return 44100;
}

static int stream_open(void)            /* (the lock held) */
{
    E.out = glos_audio_out(E.info.rate, E.info.channels, 65536, &E.ring, &E.id);
    if (!E.out)
        return -1;
    if (E.volume != 256)
        glos_set(E.out, GLOS_PROP_STREAM_VOLUME, E.volume);
    return 0;
}

static void stream_close(void)
{
    if (E.out)
        glos_close(E.out);
    E.out = 0, E.ring = NULL, E.id = 0;
}

static void fill(void)                  /* (the lock held) the ring filled from the file; the end seen */
{
    unsigned fb = E.info.channels * 2, room = glos_audio_room(E.ring) / fb, t0 = glos_ticks();
    int queued = 0;
    glos_get(E.out, GLOS_PROP_STREAM_QUEUED, &queued);
    if (E.fed > E.info.rate && !E.eof && queued < (int)(fb * 64))
        E.dry++;                                /* (it has played all it was given: the file is behind) */
    else
        E.dry = 0;
    if (E.dry >= 20) {                          /* too slow for this PC: stopped, and said */
        stream_close();
        source_seek(E.src, 0);
        E.eof = 0, E.state = ENG_STOPPED, E.slow = 1, E.dry = 0, E.fed = 0;
        return;
    }
    while (room && !E.eof && glos_ticks() - t0 < 100) {
        unsigned n = room < FRAMES ? room : FRAMES;
        unsigned long got = source_read(E.src, E.buf, n);
        if (got)
            glos_audio_write(E.ring, E.buf, (unsigned)got * fb);
        if (got < n)
            E.eof = 1;
        room -= (unsigned)got;
        E.fed += got;
    }
    if (E.eof) {
        int queued = 0;
        glos_get(E.out, GLOS_PROP_STREAM_QUEUED, &queued);
        if (queued <= (int)fb) {                /* played out: at its start again, and the window told */
            stream_close();
            source_seek(E.src, 0);
            E.eof = 0, E.state = ENG_STOPPED, E.ended = 1, E.fed = 0;
        }
    }
}

/* ---- a MIDI file (the lock held, all of it) */

static void midi_close(void)
{
    if (E.port) {
        glos_midi_flush(E.port);
        glos_close(E.port);
    }
    E.port = 0;
}

/* The port told what the file has said before message `upto`: the channels' instruments, controllers, bends and
   bend ranges as they stand there (what was never said: as General MIDI begins). */
static void midi_chase(unsigned long upto)
{
    static unsigned char cc[16][120], prog[16], range[16];
    static unsigned short bend[16];
    static struct glos_midi_msg m[GLOS_MIDI_QUEUE];
    static const unsigned char first[][2] = { { 1, 0 }, { 7, 100 }, { 10, 64 }, { 11, 127 }, { 64, 0 } };
    const struct smf_event *e = smf_events(E.mid);
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
#define PUT(s, a, b) (n < GLOS_MIDI_QUEUE ? (m[n].time = 0, m[n].status = (unsigned char)(s), m[n].data1 = (unsigned char)(a), \
                                             m[n].data2 = (unsigned char)(b), m[n].pad = 0, n++) : 0)
    for (ch = 0; ch < 16; ch++) {
        PUT(0xB0 | ch, 121, 0);                 /* every controller as it begins */
        for (k = 0; k < 120; k++)               /* (6, 38 and 96 to 101 are no settings by themselves) */
            if (cc[ch][k] != 0xFF && k != 6 && k != 38 && !(k >= 96 && k <= 101))
                PUT(0xB0 | ch, k, cc[ch][k]);
        if (range[ch] != 0xFF) {
            PUT(0xB0 | ch, 101, 0), PUT(0xB0 | ch, 100, 0), PUT(0xB0 | ch, 6, range[ch]);
            PUT(0xB0 | ch, 101, 127), PUT(0xB0 | ch, 100, 127);
        }
        PUT(0xC0 | ch, prog[ch], 0);
        PUT(0xE0 | ch, bend[ch] & 0x7F, bend[ch] >> 7);
    }
#undef PUT
    glos_midi_send(E.port, m, n, NULL, NULL);
}

static void midi_go(void)               /* on from m_at: the port told how things stand there */
{
    const struct smf_event *e = smf_events(E.mid);
    unsigned long lo = 0, hi = smf_count(E.mid);
    while (lo < hi) {                           /* the first message at or after m_at */
        unsigned long mid = lo + (hi - lo) / 2;
        if (e[mid].ms < E.m_at)
            lo = mid + 1;
        else
            hi = mid;
    }
    E.m_next = lo;
    glos_midi_flush(E.port);
    midi_chase(lo);
    E.m_base = glos_ticks() - (unsigned)E.m_at;
}

static void midi_feed(void)             /* the messages of the next while sent; the end seen */
{
    const struct smf_event *e = smf_events(E.mid);
    unsigned long count = smf_count(E.mid);
    unsigned at = glos_ticks() - E.m_base;
    for (;;) {
        struct glos_midi_msg m[64];
        unsigned n = 0, taken = 0;
        for (; n < 64 && E.m_next + n < count && e[E.m_next + n].ms < at + MIDI_AHEAD; n++) {
            const struct smf_event *v = &e[E.m_next + n];
            unsigned force = v->d2;
            if ((v->status & 0xF0) == 0x90) {   /* (the Player's volume) */
                force = force * (unsigned)E.volume / 256;
                force = force ? force : 1;
            }
            m[n].time = E.m_base + (unsigned)v->ms;
            m[n].time += !m[n].time;            /* (0 would be "now") */
            m[n].status = v->status, m[n].data1 = v->d1, m[n].data2 = (unsigned char)force, m[n].pad = 0;
        }
        if (!n)
            break;
        glos_midi_send(E.port, m, n, &taken, NULL);
        E.m_next += taken;
        if (taken < n)                          /* (the port's queue is full, or a session has the machine: the
                                                   rest at the next look) */
            break;
    }
    if (E.m_next >= count && at >= E.info.frames) {     /* played out: at its start again, and the window told */
        midi_close();
        E.m_at = 0, E.m_next = 0;
        E.state = ENG_STOPPED, E.ended = 1;
    }
}

/* The file at `path` as a MIDI file (no lock is needed): SOURCE_OK, the file read and what it says of itself; or
   a SOURCE_E_. */
static int midi_read(const char *path, struct smf **mid, struct source_info *info)
{
    unsigned long size = 0;
    unsigned char *bytes = source_whole(path, MIDI_MAX, &size);
    struct smf *m;
    int err = SMF_E_KIND;
    if (!bytes)
        return SOURCE_E_KIND;
    m = smf_open(bytes, size, &err);
    free(bytes);
    if (m && (!smf_count(m) || !smf_length(m))) {       /* (nothing to play in it) */
        smf_free(m);
        m = NULL, err = SMF_E_DAMAGED;
    }
    if (!m)
        return err == SMF_E_KIND ? SOURCE_E_KIND : err == SMF_E_FORMAT ? SOURCE_E_FORMAT
               : err == SMF_E_MEMORY ? SOURCE_E_MEMORY : SOURCE_E_DAMAGED;
    memset(info, 0, sizeof *info);
    info->rate = 1000, info->channels = 2;
    info->frames = smf_length(m) + MIDI_TAIL;
    strcpy(info->kind, "MID");
    snprintf(info->title, sizeof info->title, "%s", smf_title(m));
    snprintf(info->detail, sizeof info->detail, smf_tracks(m) == 1 ? "%u track" : "%u tracks", smf_tracks(m));
    *mid = m;
    return SOURCE_OK;
}

int eng_probe(const char *path, struct source_info *info)
{
    struct smf *m = NULL;
    int err;
    struct source *s = source_open(path, 44100, &err);
    if (s) {
        *info = *source_info(s);
        source_close(s);
        return SOURCE_OK;
    }
    if (err == SOURCE_E_KIND && (err = midi_read(path, &m, info)) == SOURCE_OK)
        smf_free(m);
    return err;
}

static void feeder(void *arg)
{
    (void)arg;
    for (;;) {
        take();
        if (E.quit) {
            give();
            return;
        }
        if (E.state == ENG_PLAYING && E.out && E.src)
            fill();
        else if (E.state == ENG_PLAYING && E.port && E.mid)
            midi_feed();
        give();
        glos_sleep(15);
    }
}

int eng_begin(int demo)
{
    E.demo = demo;
    if (demo)
        return 0;
    if (!(E.lock = glos_lock()) || !(E.thread = glos_thread(feeder, NULL, 256 * 1024, 0)))
        return -1;
    return 0;
}

void eng_end(void)
{
    eng_close();
    if (E.thread) {
        take();
        E.quit = 1;
        give();
        glos_join(E.thread, 2000, NULL);
        E.thread = 0;
    }
}

void eng_close(void)
{
    take();
    stream_close();
    midi_close();
    if (E.src)
        source_close(E.src);
    smf_free(E.mid);
    E.src = NULL, E.mid = NULL;
    E.state = ENG_STOPPED, E.eof = 0, E.demo_at = 0, E.m_at = 0, E.m_next = 0;
    give();
}

int eng_open(const char *path, struct source_info *info)
{
    struct source *s;
    int err;
    eng_close();
    s = source_open(path, mixer_rate(), &err);  /* (not under the lock: a module's length is found by going
                                                   through it) */
    if (!s && err == SOURCE_E_KIND) {           /* no samples: a MIDI file? */
        struct source_info in;
        struct smf *m = NULL;
        if ((err = midi_read(path, &m, &in)) != SOURCE_OK)
            return err;
        take();
        E.mid = m, E.info = in;
        E.m_at = 0, E.m_next = 0, E.ended = 0;
        give();
        if (info)
            *info = in;
        return SOURCE_OK;
    }
    if (!s)
        return err;
    take();
    E.src = s;
    E.info = *source_info(s);
    E.ended = 0;
    give();
    if (info)
        *info = E.info;
    return SOURCE_OK;
}

int eng_play(void)
{
    int r = 0;
    take();
    if (!E.src && !E.mid) {
        r = -1;
    } else if (E.demo) {
        E.state = ENG_PLAYING;
    } else if (E.mid) {
        if (E.state != ENG_PLAYING) {           /* (the FM chip where there is one, else an MPU-401) */
            if (!E.port && !(E.port = glos_midi_out(GLOS_MIDI_PORT_FM)))
                E.port = glos_midi_out(GLOS_MIDI_PORT_MPU);
            if (E.port) {
                midi_go();
                E.state = ENG_PLAYING;
                midi_feed();
            } else {
                r = -2;
            }
        }
    } else if (E.state == ENG_PAUSED && E.out) {
        glos_set(E.out, GLOS_PROP_STREAM_PAUSED, 0);
        E.state = ENG_PLAYING;
    } else if (E.state != ENG_PLAYING) {
        if (stream_open() == 0) {
            E.state = ENG_PLAYING;
            E.fed = 0, E.dry = 0, E.slow = 0;
            fill();                             /* (at once: no wait for the thread's next look) */
        } else {
            r = -1;
        }
    }
    give();
    return r;
}

void eng_pause(void)
{
    take();
    if (E.state == ENG_PLAYING) {
        if (E.out)
            glos_set(E.out, GLOS_PROP_STREAM_PAUSED, 1);
        if (E.port) {                           /* what waits dropped, the notes ended */
            E.m_at = glos_ticks() - E.m_base;
            E.m_at = E.m_at > E.info.frames ? E.info.frames : E.m_at;
            glos_midi_flush(E.port);
        }
        E.state = ENG_PAUSED;
    } else if (E.state == ENG_PAUSED) {
        if (E.out)
            glos_set(E.out, GLOS_PROP_STREAM_PAUSED, 0);
        if (E.port)
            midi_go();
        E.state = ENG_PLAYING;
    }
    give();
}

void eng_stop(void)
{
    take();
    stream_close();
    midi_close();
    if (E.src)
        source_seek(E.src, 0);
    E.state = ENG_STOPPED, E.eof = 0, E.demo_at = 0, E.m_at = 0, E.m_next = 0;
    give();
}

void eng_seek(unsigned long frame)
{
    take();
    if (E.mid) {
        E.m_at = E.demo_at = frame > E.info.frames ? E.info.frames : frame;
        if (E.port && E.state == ENG_PLAYING)
            midi_go();
    } else if (E.src) {
        source_seek(E.src, frame);
        E.eof = 0;
        E.demo_at = frame > E.info.frames ? E.info.frames : frame;
        if (E.out) {                            /* what was queued is of the old place: a stream anew */
            int paused = E.state == ENG_PAUSED;
            stream_close();
            if (stream_open() != 0) {
                E.state = ENG_STOPPED;
            } else if (paused) {
                glos_set(E.out, GLOS_PROP_STREAM_PAUSED, 1);
            } else {
                fill();
            }
        }
    }
    give();
}

void eng_volume(int v)
{
    take();
    E.volume = v < 0 ? 0 : v > 256 ? 256 : v;
    if (E.out)
        glos_set(E.out, GLOS_PROP_STREAM_VOLUME, E.volume);
    give();
}

void eng_now(struct eng_now *n)
{
    memset(n, 0, sizeof *n);
    take();
    n->state = E.state;
    n->ended = E.ended;
    n->slow = E.slow;
    E.ended = E.slow = 0;
    if (E.src || E.mid) {
        n->frames = E.info.frames, n->rate = E.info.rate;
        if (E.demo) {
            n->at = E.demo_at;
        } else if (E.mid) {
            n->at = E.state == ENG_PLAYING ? glos_ticks() - E.m_base : E.m_at;
            n->at = n->at > n->frames ? n->frames : n->at;
        } else {
            unsigned long at = source_tell(E.src);
            int queued = 0;
            if (E.out) {
                unsigned l = 0, r = 0;
                glos_get(E.out, GLOS_PROP_STREAM_QUEUED, &queued);
                glos_audio_levels(E.id, &l, &r);
                n->left = (int)(l * 1000 / 32767), n->right = (int)(r * 1000 / 32767);
            }
            queued = queued > 0 ? queued / (int)(E.info.channels * 2) : 0;
            n->at = at > (unsigned long)queued ? at - (unsigned long)queued : 0;
        }
    }
    give();
}

void eng_step(unsigned long frames)
{
    if (!E.demo || E.state != ENG_PLAYING)
        return;
    E.demo_at += frames;
    if (E.demo_at >= E.info.frames) {
        E.demo_at = 0;
        E.state = ENG_STOPPED, E.ended = 1;
    }
}
