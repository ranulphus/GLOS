/* The skiing game's sounds (sound.h). 11025 samples a second, one channel, written a tenth of a second ahead of
 * what is being played. The hiss is noise through a one-pole filter (open on ice, close in powder); the rest are
 * a few voices of a sine, a square or noise that die away. */
#include <math.h>
#include <string.h>

#include <glos/glos.h>

#include "../shared/skirun.h"
#include "sound.h"

#define RATE  11025
#define AHEAD 1300                      /* samples kept written ahead of the mixer */

enum { V_BELL = 1, V_BUZZ, V_WHOOSH, V_THUMP, V_CRASH, V_CHIME };

static glos_handle out;
static struct glos_audio_ring *ring;
static short sine[256];
static unsigned seed = 12345;
static int low, loud;                   /* the hiss: the filter's last sample; how loud it is now (it follows) */
static struct { int kind; unsigned at, phase; } voice[4];

int sound_start(void)
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

void sound_stop(void)
{
    if (out)
        glos_close(out);
    out = 0;
    memset(voice, 0, sizeof voice);
}

static void begin(int kind)
{
    int i, at = 0;
    for (i = 0; i < 4; i++) {           /* a voice that is free, or the one furthest along */
        if (!voice[i].kind) {
            at = i;
            break;
        }
        if (voice[i].at > voice[at].at)
            at = i;
    }
    voice[at].kind = kind, voice[at].at = 0, voice[at].phase = 0;
}

void sound_events(unsigned ev)
{
    if (!out)
        return;
    if (ev & SKI_EV_GATE)
        begin(V_BELL);
    if (ev & SKI_EV_MISS)
        begin(V_BUZZ);
    if (ev & SKI_EV_JUMP)
        begin(V_WHOOSH);
    if (ev & SKI_EV_LAND)
        begin(V_THUMP);
    if (ev & SKI_EV_FALL)
        begin(V_CRASH);
    if (ev & SKI_EV_FINISH)
        begin(V_CHIME);
}

static int noise(void)                  /* -32768 to 32767 */
{
    seed = seed * 1103515245u + 12345u;
    return (int)(seed >> 16 & 0xFFFF) - 32768;
}

/* A voice's next sample; it ends itself. */
static int sample(int k)
{
    unsigned at = voice[k].at++, len = 1;
    int v = 0;
#define TONE(hz) (voice[k].phase += (unsigned)(hz) * 65536u / RATE, sine[voice[k].phase >> 8 & 255])
#define FADE(n) (len = (n), (int)(len - at))
    switch (voice[k].kind) {
    case V_BELL:                        /* a gate: two high notes together, a quarter of a second */
        v = (TONE(1320) + sine[(voice[k].phase * 3 / 2) >> 8 & 255]) / 2 * FADE(RATE / 4) / (int)len / 5;
        break;
    case V_BUZZ:                        /* a gate missed: a low square */
        voice[k].phase += 150u * 65536u / RATE;
        v = (voice[k].phase & 0x8000 ? 5000 : -5000) * FADE(RATE / 4) / (int)len;
        break;
    case V_WHOOSH:                      /* off the ground: air, rising */
        len = RATE / 8;
        v = noise() / 6 * (int)at / (int)len;
        break;
    case V_THUMP:                       /* down again: a low note that dies fast */
        v = TONE(90) / 3 * FADE(RATE / 6) / (int)len;
        break;
    case V_CRASH:                       /* a fall: a burst, and a note sliding down */
        len = RATE / 3;
        v = (noise() / 5 + TONE(400 - 300 * (int)at / (int)len) / 6) * (int)(len - at) / (int)len;
        break;
    case V_CHIME:                       /* the valley: three notes up */
        len = RATE * 9 / 20;
        v = TONE(at < len / 3 ? 660 : at < 2 * len / 3 ? 880 : 1320) / 5 * (int)(len / 3 - at % (len / 3)) / (int)(len / 3);
        break;
    }
#undef TONE
#undef FADE
    if (at + 1 >= len)
        voice[k].kind = 0;
    return v;
}

void sound_frame(int speed, int snow)
{
    static short buf[512];
    unsigned queued;
    int want, open, n, i, k;
    if (!out)
        return;
    /* how loud the skis are, and how open the filter: fast is loud, ice is bright, powder is dull */
    want = snow < 0 ? 0 : (speed > 500 ? 500 : speed) * (snow == SKI_SNOW_ICE ? 13 : snow == SKI_SNOW_PISTE ? 9 : 6);
    open = snow == SKI_SNOW_ICE ? 200 : snow == SKI_SNOW_PISTE ? 90 : 36;
    queued = (ring->write - ring->read) / 2;
    while (queued < AHEAD) {
        n = (int)(AHEAD - queued) < 512 ? (int)(AHEAD - queued) : 512;
        for (i = 0; i < n; i++) {
            int v;
            loud += (want - loud) / 64 + (want > loud) - (want < loud);
            low += (noise() - low) * open / 256;
            v = low * loud / 8192;
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
