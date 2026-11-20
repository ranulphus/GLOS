/* The Player's playing (engine.h). A thread of its own looks every 15 ms at how much room the stream's ring has
 * and fills it from the file (apps/shared/source.c: a WAV read piece by piece, a module played); when the file
 * has given its last and the ring has played out, the stream is closed and the window is told (eng_now's
 * `ended`). The window's calls and the thread share one lock; the thread holds it only while it fills.
 *
 * A seek while it plays closes the stream and opens another: what was queued (a third of a second) is not heard
 * after the new place has been asked for. A module is played at the mixer's own rate (it costs the least there:
 * on a 486 the mixer runs at 22050 Hz). */
#include <stdio.h>
#include <string.h>

#include <glos/glos.h>

#include "engine.h"

#define FRAMES 2048u

static struct {
    glos_handle lock, thread;
    int demo, quit;
    struct source *src;
    struct source_info info;
    int state, ended, eof, volume;
    glos_handle out;
    struct glos_audio_ring *ring;
    unsigned id;
    unsigned long demo_at;
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
    unsigned fb = E.info.channels * 2, room = glos_audio_room(E.ring) / fb;
    while (room && !E.eof) {
        unsigned n = room < FRAMES ? room : FRAMES;
        unsigned long got = source_read(E.src, E.buf, n);
        if (got)
            glos_audio_write(E.ring, E.buf, (unsigned)got * fb);
        if (got < n)
            E.eof = 1;
        room -= (unsigned)got;
    }
    if (E.eof) {
        int queued = 0;
        glos_get(E.out, GLOS_PROP_STREAM_QUEUED, &queued);
        if (queued <= (int)fb) {                /* played out: at its start again, and the window told */
            stream_close();
            source_seek(E.src, 0);
            E.eof = 0, E.state = ENG_STOPPED, E.ended = 1;
        }
    }
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
        give();
        glos_sleep(15);
    }
}

int eng_begin(int demo)
{
    E.demo = demo;
    if (demo)
        return 0;
    if (!(E.lock = glos_lock()) || !(E.thread = glos_thread(feeder, NULL, 0, 0)))
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
    if (E.src)
        source_close(E.src);
    E.src = NULL;
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
    if (!E.src) {
        r = -1;
    } else if (E.demo) {
        E.state = ENG_PLAYING;
    } else if (E.state == ENG_PAUSED && E.out) {
        glos_set(E.out, GLOS_PROP_STREAM_PAUSED, 0);
        E.state = ENG_PLAYING;
    } else if (E.state != ENG_PLAYING) {
        if (stream_open() == 0) {
            E.state = ENG_PLAYING;
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
        E.state = ENG_PAUSED;
    } else if (E.state == ENG_PAUSED) {
        if (E.out)
            glos_set(E.out, GLOS_PROP_STREAM_PAUSED, 0);
        E.state = ENG_PLAYING;
    }
    give();
}

void eng_stop(void)
{
    take();
    stream_close();
    if (E.src)
        source_seek(E.src, 0);
    E.state = ENG_STOPPED, E.eof = 0, E.demo_at = 0;
    give();
}

void eng_seek(unsigned long frame)
{
    take();
    if (E.src) {
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
    E.ended = 0;
    if (E.src) {
        n->frames = E.info.frames, n->rate = E.info.rate;
        if (E.demo) {
            n->at = E.demo_at;
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
