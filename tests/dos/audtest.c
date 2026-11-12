/* AUDTEST: a GLOS app that plays a tone through GLOS's mixer (M8 step E; the
 * "audio" suite of tests/loopa/jobs.py). DJGPP, with libglos.
 *
 *   AUDTEST tone NAME HZ SECONDS [RATE CHANNELS RING]
 *                              a sine of HZ for SECONDS through a stream of
 *                              RATE samples a second (22050), CHANNELS (1 or
 *                              2; 2) and a ring of RING bytes (16384): the
 *                              ring kept full, the app sleeping 10 ms
 *                              between fills (exit code 41)
 *   AUDTEST info               VERSION: the API's version and what the machine can do
 *
 * Lines: "AUD-START NAME stream=ID", "AUD-OK NAME periods=N underruns=U
 * frames=F" (the counts while the stream was still fed: its end, when the
 * app stops writing, is no underrun of the mixer's), "AUD-ERR ...". */
#include <crt0.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glos/glos.h>

int _crt0_startup_flags = _CRT0_FLAG_UNIX_SBRK;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    struct glos_audio_ring *s;
    struct glos_version v;
    glos_handle h;
    unsigned id, rate = 22050, ch = 2, ring = 16384, frames = 0, periods, under;
    const char *name;
    static short wave[1024];                    /* one cycle: a table, so a 486 keeps up at 44.1 kHz */
    unsigned phase = 0, step;                   /* 22.10 fixed point into the table */
    double hz;
    int secs, k;
    clock_t end;
    if (argc >= 2 && !strcmp(argv[1], "info")) {
        memset(&v, 0, sizeof v);
        v.size = sizeof v;
        glos_version(&v);
        say("AUD-INFO version=%x caps=%x\n", v.major << 16 | v.minor, v.machine);
        return 0;
    }
    if (argc < 5 || strcmp(argv[1], "tone"))
        return 2;
    name = argv[2];
    hz = atof(argv[3]);
    secs = atoi(argv[4]);
    if (argc >= 8)
        rate = (unsigned)atoi(argv[5]), ch = (unsigned)atoi(argv[6]), ring = (unsigned)atoi(argv[7]);
    if ((k = glos_init("AUDTEST", "", "")) != 0) {
        say("AUD-ERR %s init error=%d\n", name, k);
        return 44;
    }
    if (!(h = glos_audio_out(rate, ch, ring, &s, &id))) {
        say("AUD-ERR %s audio error=%d\n", name, glos_error());
        return 44;
    }
    say("AUD-START %s stream=%u rate=%u channels=%u mixer=%u period=%u\n", name, id, s->rate, s->channels,
        s->mixer_rate, s->period_frames);
    for (k = 0; k < 1024; k++)
        wave[k] = (short)(sin(2 * M_PI * k / 1024) * 12000.0);
    step = (unsigned)(hz * 1024.0 * 1024.0 / rate);
    end = clock() + (clock_t)secs * CLOCKS_PER_SEC;
    while (clock() < end) {
        unsigned room = s->bytes - (s->write - s->read), fb = ch * 2, n = room / fb, i, c;
        unsigned char *ring_p = (unsigned char *)s + s->ring;
        for (i = 0; i < n; i++) {
            short v = wave[(phase >> 10) & 1023];
            for (c = 0; c < ch; c++)
                *(short *)(ring_p + ((s->write + i * fb + c * 2) & (s->bytes - 1))) = v;
            phase += step;
        }
        s->write += n * fb;
        frames += n;
        glos_sleep(10);
    }
    periods = s->periods, under = s->underruns;
    end = clock() + CLOCKS_PER_SEC;             /* (what is in the ring, played: the last frame stays) */
    while (s->write - s->read > ch * 2 && clock() < end)
        glos_sleep(10);
    glos_close(h);
    say("AUD-OK %s periods=%u underruns=%u frames=%u\n", name, periods, under, frames);
    return 41;
}
