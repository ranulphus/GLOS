/* The hovercraft game's sounds (hvsound.h). 11025 samples a second, one channel, written a tenth of a second
 * ahead. The fans are a saw whose pitch follows their work, with air (noise) that follows the craft's speed; set
 * down, the skirt scrapes while the craft still moves. The rest are a few voices that die away. */
#include <math.h>
#include <string.h>

#include <glos/glos.h>

#include "../shared/hvgame.h"
#include "hvsound.h"

#define RATE  11025
#define AHEAD 1300

enum { V_BUMP = 1, V_WALL, V_SPLASH, V_TAKEN, V_DROP, V_SCORE, V_BEACON };

static glos_handle out;
static struct glos_audio_ring *ring;
static short sine[256];
static unsigned seed = 4242, fan_phase;
static int pitch = 60, air, low;        /* the fans' pitch now (it follows the work), the air's loudness, its filter */
static struct { int kind; unsigned at, phase; } voice[4];

int hvsound_start(void)
{
    int i;
    if (out)
        return 1;
    if (!(out = glos_audio_out(RATE, 1, 8192, &ring, NULL)) || !ring) {
        if (out)
            glos_close(out);
        out = 0;
        return 0;
    }
    for (i = 0; i < 256; i++)
        sine[i] = (short)(32000.0 * sin((double)i * 6.283185307179586 / 256.0));
    return 1;
}

void hvsound_stop(void)
{
    if (out)
        glos_close(out);
    out = 0;
    memset(voice, 0, sizeof voice);
}

static void begin(int kind)
{
    int i, at = 0;
    for (i = 0; i < 4; i++) {
        if (!voice[i].kind) {
            at = i;
            break;
        }
        if (voice[i].at > voice[at].at)
            at = i;
    }
    voice[at].kind = kind, voice[at].at = 0, voice[at].phase = 0;
}

void hvsound_events(unsigned ev)
{
    if (!out)
        return;
    if (ev & HV_EV_SPLASH)
        begin(V_SPLASH);
    else if (ev & HV_EV_BUMP)
        begin(V_BUMP);
    else if (ev & HV_EV_WALL)
        begin(V_WALL);
    if (ev & HV_EV_SCORE)
        begin(V_SCORE);
    else if (ev & HV_EV_TAKEN)
        begin(V_TAKEN);
    else if (ev & HV_EV_DROP)
        begin(V_DROP);
    if (ev & HV_EV_BEACON)
        begin(V_BEACON);
}

static int noise(void)
{
    seed = seed * 1103515245u + 12345u;
    return (int)(seed >> 16 & 0xFFFF) - 32768;
}

static int sample(int k)
{
    unsigned at = voice[k].at++, len = 1;
    int v = 0;
#define TONE(hz) (voice[k].phase += (unsigned)(hz) * 65536u / RATE, sine[voice[k].phase >> 8 & 255])
#define FADE(n) (len = (n), (int)(len - at))
    switch (voice[k].kind) {
    case V_BUMP:                        /* craft on craft: a knock */
        v = (TONE(140) / 3 + noise() / 10) * FADE(RATE / 8) / (int)len;
        break;
    case V_WALL:                        /* a wall: duller */
        v = TONE(70) / 4 * FADE(RATE / 10) / (int)len;
        break;
    case V_SPLASH:                      /* the water */
        v = noise() / 5 * FADE(RATE / 2) / (int)len;
        break;
    case V_TAKEN:                       /* a flag or the lamp taken: two notes up */
        len = RATE / 4;
        v = TONE(at < len / 2 ? 660 : 990) / 6;
        break;
    case V_DROP:                        /* ... lost: two notes down */
        len = RATE / 4;
        v = TONE(at < len / 2 ? 660 : 440) / 6;
        break;
    case V_SCORE:                       /* a point: three notes up */
        len = RATE * 9 / 20;
        v = TONE(at < len / 3 ? 660 : at < 2 * len / 3 ? 880 : 1320) / 5 * (int)(len / 3 - at % (len / 3)) / (int)(len / 3);
        break;
    case V_BEACON:                      /* a beacon turned: a chime */
        v = TONE(1100) / 6 * FADE(RATE / 5) / (int)len;
        break;
    }
#undef TONE
#undef FADE
    if (at + 1 >= len)
        voice[k].kind = 0;
    return v;
}

void hvsound_frame(int work, int speed, int down, int wet)
{
    static short buf[512];
    unsigned queued;
    int want_pitch = wet ? 0 : down ? 34 : 60 + 26 * work, want_air = wet ? 0 : (speed > 24000 ? 24000 : speed) / 5, n, i, k;
    if (!out)
        return;
    queued = (ring->write - ring->read) / 2;
    while (queued < AHEAD) {
        n = (int)(AHEAD - queued) < 512 ? (int)(AHEAD - queued) : 512;
        for (i = 0; i < n; i++) {
            int v;
            if (!(i & 31))              /* (the fans spin up and down in a fraction of a second) */
                pitch += (want_pitch > pitch) - (want_pitch < pitch);
            air += (want_air - air) / 128 + (want_air > air) - (want_air < air);
            fan_phase += (unsigned)pitch * 65536u / RATE;
            v = pitch ? ((int)(fan_phase & 0xFFFF) - 32768) / 14 : 0;   /* a saw: the fans' hum */
            low += (noise() - low) * (down ? 160 : 60) / 256;           /* the air; set down, the skirt's scrape */
            v += low * air / 8192;
            for (k = 0; k < 4; k++)
                if (voice[k].kind)
                    v += sample(k);
            buf[i] = (short)(v > 32000 ? 32000 : v < -32000 ? -32000 : v);
        }
        if (glos_audio_write(ring, buf, (unsigned)n * 2) != (unsigned)n * 2)
            break;
        queued += (unsigned)n;
    }
}
