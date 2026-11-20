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
 * A MIDI file (apps/shared/smf.c) is no samples: its messages go to a MIDI port, each with its time on GLOS's
 * clock (apps/shared/midiplay.c, which MIDI Studio plays by too); the thread sends those of the next 400 ms at
 * each look. To the window it is a file of 1000 "frames" a second. The Player's volume is the force of the
 * notes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/glos.h>

#include "../shared/midiplay.h"
#include "../shared/smf.h"
#include "engine.h"

#define FRAMES 2048u
#define MIDI_MAX (8ul << 20)            /* a MIDI file at most */
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
    struct midiplay mp;                 /* ... and its playing (apps/shared/midiplay.c) */
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
        else if (E.state == ENG_PLAYING && E.mid && mp_feed(&E.mp))
            E.state = ENG_STOPPED, E.ended = 1; /* played out: at its start again, and the window told */
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
    mp_stop(&E.mp);
    if (E.src)
        source_close(E.src);
    smf_free(E.mid);
    E.src = NULL, E.mid = NULL;
    E.state = ENG_STOPPED, E.eof = 0, E.demo_at = 0;
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
        mp_init(&E.mp, m, in.frames);
        E.mp.volume = E.volume;
        E.ended = 0;
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
        if (E.state != ENG_PLAYING) {
            if (mp_play(&E.mp) == 0)
                E.state = ENG_PLAYING;
            else
                r = -2;
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
        if (E.mid)                              /* what waits dropped, the notes ended */
            mp_pause(&E.mp);
        E.state = ENG_PAUSED;
    } else if (E.state == ENG_PAUSED) {
        if (E.out)
            glos_set(E.out, GLOS_PROP_STREAM_PAUSED, 0);
        if (E.mid)
            mp_play(&E.mp);
        E.state = ENG_PLAYING;
    }
    give();
}

void eng_stop(void)
{
    take();
    stream_close();
    mp_stop(&E.mp);
    if (E.src)
        source_seek(E.src, 0);
    E.state = ENG_STOPPED, E.eof = 0, E.demo_at = 0;
    give();
}

void eng_seek(unsigned long frame)
{
    take();
    if (E.mid) {
        E.demo_at = frame > E.info.frames ? E.info.frames : frame;
        mp_seek(&E.mp, frame);
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
    E.mp.volume = E.volume;
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
            n->at = mp_where(&E.mp);
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
