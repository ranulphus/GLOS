/* Host test for the S3M player (apps/shared/s3m.c): the small songs tests/host/s3m_cases.py writes, each held to
 * what must be heard: which pitch when (the tone's cycles counted), how loud on which side, for how long (a row
 * is six ticks of 882 frames at 44100 Hz unless the song says otherwise), where a song ends; seeking gives the
 * same samples as playing to there; files cut short and files that are no modules; a module with bytes changed
 * at random comes to no harm.
 *
 *     s3m_test DIR        (the .s3m files s3m_cases.py wrote) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../apps/shared/s3m.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); \
                                                printf("\n"); fails++; } } while (0)

#define RATE 44100
#define TICK 882ul
#define ROW (6 * TICK)
#define MAXF (RATE * 30ul)
#define C4 (8363.0 / 32)                /* the sine's middle C: Hz */
#define FULL (32512.0 * 0.7071)         /* the sine as loud as it can be on a side, all of it there */
#define LEFT (205.0 / 256)              /* a left channel's part on the left (its place is 3 of 15) */

static const char *dir;
static unsigned char *file;
static unsigned long file_size;
static short *pcm;                      /* a song played: left, right, left ... */
static unsigned long frames;

static struct s3m *open_s3m(const char *name, int *err)
{
    char path[600];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    free(file);
    file = NULL, file_size = 0;
    if (!(f = fopen(path, "rb"))) {
        *err = -99;
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    file_size = (unsigned long)ftell(f);
    rewind(f);
    file = malloc(file_size + 1);
    if (fread(file, 1, file_size, f) != file_size)
        file_size = 0;
    fclose(f);
    return s3m_open(file, file_size, RATE, err);
}

static struct s3m *play(const char *name)       /* the whole song into pcm; the module (still open) or NULL */
{
    int err = 0;
    struct s3m *m = open_s3m(name, &err);
    unsigned long n;
    frames = 0;
    CHECK(m != NULL, "%s: not opened: %d", name, err);
    if (!m)
        return NULL;
    while (frames < MAXF && (n = s3m_render(m, pcm + frames * 2, 4096 < MAXF - frames ? 4096 : MAXF - frames)) != 0)
        frames += n;
    CHECK(s3m_length(m) == frames && s3m_tell(m) == frames, "%s: %lu frames played, its length %lu", name, frames, s3m_length(m));
    return m;
}

static double hz(unsigned long from, unsigned long to, int side)        /* the tone there: its cycles a second */
{
    unsigned long i, n = 0;
    for (i = from + 1; i < to && i < frames; i++)
        if (pcm[(i - 1) * 2 + side] < 0 && pcm[i * 2 + side] >= 0)
            n++;
    return (double)n * RATE / (double)(to - from);
}

static double loud(unsigned long from, unsigned long to, int side)      /* root of the mean square */
{
    unsigned long i;
    double sum = 0;
    for (i = from; i < to && i < frames; i++)
        sum += (double)pcm[i * 2 + side] * pcm[i * 2 + side];
    return to > from ? sqrt(sum / (double)(to - from)) : 0;
}

/* How much the tone's cycles differ in length there: the longest less the shortest, in frames. */
static long spread(unsigned long from, unsigned long to, int side)
{
    unsigned long i, last = 0;
    long lo = 1 << 30, hi = 0;
    for (i = from + 1; i < to && i < frames; i++)
        if (pcm[(i - 1) * 2 + side] < 0 && pcm[i * 2 + side] >= 0) {
            if (last) {
                long d = (long)(i - last);
                lo = d < lo ? d : lo, hi = d > hi ? d : hi;
            }
            last = i;
        }
    return hi > lo ? hi - lo : 0;
}

static int near(double got, double want, double part)
{
    return fabs(got - want) <= want * part;
}

static int same(double a, double b)     /* (not ==: a sum kept in a register is finer than one put by) */
{
    return fabs(a - b) < 0.5;
}

int main(int argc, char **argv)
{
    struct s3m *m;
    double a, b, c, d, e, full = 0;
    int err;
    dir = argc > 1 ? argv[1] : ".";
    pcm = malloc(MAXF * 2 * sizeof *pcm);

    /* three notes and a soft one: the pitches, the loudness, the sides, the length */
    if ((m = play("notes.s3m")) != NULL) {
        a = hz(ROW, 15 * ROW, 0), b = hz(17 * ROW, 31 * ROW, 0), c = hz(33 * ROW, 47 * ROW, 0), d = hz(49 * ROW, 62 * ROW, 0);
        CHECK(near(a, C4, 0.005) && near(b, 2 * C4, 0.005) && near(c, C4 / 2, 0.01) && near(d, C4, 0.005),
              "notes: %.1f %.1f %.1f %.1f Hz", a, b, c, d);
        a = full = loud(ROW, 15 * ROW, 0), b = loud(49 * ROW, 62 * ROW, 0), c = loud(ROW, 15 * ROW, 1);
        CHECK(near(b / a, 0.25, 0.03), "notes: volume 16 of 64 is %.3f of full", b / a);
        CHECK(near(a / c, 205.0 / 51, 0.02), "notes: the left channel is %.2f times on the left what it is on the right", a / c);
        CHECK(near(a, FULL * LEFT / 2, 0.03), "notes: one channel at its loudest is %.0f", a);
        CHECK(frames == 64 * ROW, "notes: %lu frames", frames);
        CHECK(!strcmp(s3m_title(m), "test song") && s3m_channels(m) == 2, "notes: '%s', %u channels", s3m_title(m), s3m_channels(m));
        /* sought to, it is the same samples as played to */
        {
            static short part[2 * 5000];
            unsigned long at = 100000;
            s3m_seek(m, at);
            CHECK(s3m_tell(m) == at, "sought: at %lu", s3m_tell(m));
            CHECK(s3m_render(m, part, 5000) == 5000 && !memcmp(part, pcm + at * 2, sizeof part), "sought to %lu: other samples", at);
            s3m_seek(m, 0);
            CHECK(s3m_render(m, part, 5000) == 5000 && !memcmp(part, pcm, sizeof part), "sought to the start: other samples");
            s3m_seek(m, frames + 1000);
            CHECK(s3m_tell(m) == frames && s3m_render(m, part, 100) == 0, "sought past the end: at %lu", s3m_tell(m));
            s3m_seek(m, 0);                     /* the nearer sample, not the line between two: the same song */
            s3m_smooth(m, 0);
            CHECK(s3m_render(m, part, 5000) == 5000 && memcmp(part, pcm, sizeof part) != 0, "not smoothed: the same samples");
        }
        s3m_free(m);
    }
    if ((m = play("signed.s3m")) != NULL) {
        a = hz(ROW, 15 * ROW, 0), b = loud(ROW, 15 * ROW, 0);
        CHECK(near(a, C4, 0.005) && near(b, full, 0.001), "signed samples: %.1f Hz, %.0f loud (%.0f)", a, b, full);
        s3m_free(m);
    }
    if ((m = play("mono.s3m")) != NULL) {
        a = loud(ROW, 15 * ROW, 0), c = loud(ROW, 15 * ROW, 1);
        CHECK(same(a, c) && near(a, FULL / 4, 0.03), "not stereo: %.0f left, %.0f right", a, c);
        s3m_free(m);
    }
    if ((m = play("right.s3m")) != NULL) {
        a = loud(ROW, 15 * ROW, 0), c = loud(ROW, 15 * ROW, 1);
        CHECK(near(c / a, 205.0 / 51, 0.02) && frames == 64 * ROW, "right: %.2f, %lu frames", c / a, frames);
        s3m_free(m);
    }
    if ((m = play("pans.s3m")) != NULL) {
        a = loud(ROW, 7 * ROW, 0), b = loud(ROW, 7 * ROW, 1), c = loud(9 * ROW, 15 * ROW, 0), d = loud(9 * ROW, 15 * ROW, 1);
        CHECK(near(a, FULL / 2, 0.03) && b == 0 && c == 0 && near(d, FULL / 2, 0.03),
              "the file's places: %.0f and %.0f, then %.0f and %.0f", a, b, c, d);
        s3m_free(m);
    }
    /* speeds and tempos: the songs' lengths */
    if ((m = play("speed.s3m")) != NULL) {
        CHECK(frames == 32 * 6 * TICK + 32 * 3 * TICK, "speed: %lu frames", frames);
        s3m_free(m);
    }
    if ((m = play("tempo.s3m")) != NULL) {
        CHECK(frames == 64 * 6 * 735ul, "tempo 150: %lu frames", frames);
        s3m_free(m);
    }
    if ((m = play("header.s3m")) != NULL) {
        a = loud(3 * 735, 60 * 3 * 735, 0);
        CHECK(frames == 64 * 3 * 735ul && near(a, full / 2, 0.01), "the header's speed, tempo and volume: %lu frames, %.0f loud",
              frames, a);
        s3m_free(m);
    }
    if ((m = play("jump.s3m")) != NULL) {
        a = hz(ROW, 7 * ROW, 0), b = hz(9 * ROW, 15 * ROW, 0);
        CHECK(frames == 16 * ROW && near(a, C4, 0.01) && near(b, 2 * C4, 0.01), "jump: %lu frames, %.1f then %.1f Hz", frames, a, b);
        s3m_free(m);
    }
    if ((m = play("orders.s3m")) != NULL) {
        a = hz(100, 4 * ROW, 0), b = hz(4 * ROW + 100, 8 * ROW, 0);
        CHECK(frames == 8 * ROW && near(a, C4, 0.02) && near(b, 2 * C4, 0.02), "orders: %lu rows, %.1f then %.1f Hz", frames / ROW,
              a, b);
        s3m_free(m);
    }
    if ((m = play("loop.s3m")) != NULL) {
        CHECK(frames == 16 * ROW, "loop: %lu frames (%lu rows)", frames, frames / ROW);
        s3m_free(m);
    }
    if ((m = play("loop2.s3m")) != NULL) {
        CHECK(frames == 24 * ROW, "two loops: %lu frames (%lu rows)", frames, frames / ROW);
        s3m_free(m);
    }
    /* a row three times as long, its note (5273 frames of it) struck once */
    if ((m = play("delay.s3m")) != NULL) {
        CHECK(frames == 24 * TICK && loud(500, 4500, 0) > 5000 && loud(5400, 24 * TICK, 0) == 0,
              "delay: %lu frames (%lu ticks), %.0f then %.0f", frames, frames / TICK, loud(500, 4500, 0), loud(5400, 24 * TICK, 0));
        s3m_free(m);
    }
    /* a sample with no loop ends by itself: 1000 bytes at 8363 a second are 5273 frames */
    if ((m = play("blip.s3m")) != NULL) {
        CHECK(loud(500, 4500, 0) > 5000 && loud(5400, 20000, 0) == 0 && frames == 16 * ROW, "blip: %.0f then %.0f", loud(500, 4500, 0),
              loud(5400, 20000, 0));
        s3m_free(m);
    }
    /* ... from 512 bytes in: 448 bytes of tone are 2362 frames */
    if ((m = play("offset.s3m")) != NULL) {
        a = loud(200, 2200, 0), b = loud(2700, 4 * ROW, 0);
        CHECK(a > 5000 && b == 0, "offset: %.0f then %.0f", a, b);
        a = loud(4 * ROW + 2700, 4 * ROW + 4800, 0), b = loud(4 * ROW + 5400, 8 * ROW, 0);
        CHECK(a > 5000 && b == 0, "no offset: %.0f then %.0f", a, b);
        a = loud(8 * ROW + 200, 8 * ROW + 2200, 0), b = loud(8 * ROW + 2700, 12 * ROW, 0);
        CHECK(a > 5000 && b == 0 && frames == 13 * ROW, "the offset again: %.0f then %.0f", a, b);
        s3m_free(m);
    }
    /* slides: up by 20 a tick for eleven rows (1712 to 612), to a note (1712 to 856) and no further, down by 20
       a tick for five rows (to 2212), and finely up by 60 (to 1652) */
    if ((m = play("slide.s3m")) != NULL) {
        a = hz(12 * ROW, 16 * ROW, 0), b = hz(25 * ROW, 40 * ROW, 0), c = hz(16 * ROW + 100, 17 * ROW, 0);
        d = hz(46 * ROW, 52 * ROW, 0), e = hz(53 * ROW, 60 * ROW, 0);
        CHECK(near(a, 14317056.0 / 612 / 32, 0.01) && near(b, 2 * C4, 0.005) && near(c, C4, 0.03) && frames == 61 * ROW,
              "slide: %.1f Hz after the slide up, %.1f at the note slid to, %.1f before; %lu rows", a, b, c, frames / ROW);
        CHECK(near(d, 14317056.0 / 2212 / 32, 0.01) && near(e, 14317056.0 / 1652 / 32, 0.01),
              "slide: %.1f Hz after the slide down, %.1f after the fine one up", d, e);
        s3m_free(m);
    }
    /* arpeggio: the note, four and seven semitones above, a tick each: their mean is heard over a row */
    if ((m = play("arp.s3m")) != NULL) {
        a = hz(0, 15 * ROW, 0);
        b = (14317056.0 / 1712 + 14317056.0 / 1356 + 14317056.0 / 1140) / 3 / 32;
        CHECK(near(a, b, 0.03), "arpeggio: %.1f Hz in the mean, %.1f wanted", a, b);
        s3m_free(m);
    }
    /* volumes: 64, slid to 44, to 40 and back to 44, to 54; the song's halved; then the channel's places */
    if ((m = play("vols.s3m")) != NULL) {
        a = loud(100, ROW, 0);
        b = loud(3 * ROW, 8 * ROW, 0), c = loud(11 * ROW, 16 * ROW, 0), d = loud(17 * ROW, 21 * ROW, 0);
        CHECK(near(a, full, 0.02) && near(b / a, 44.0 / 64, 0.02) && near(c / a, 54.0 / 64, 0.02) && near(d / a, 27.0 / 64, 0.02),
              "volume slides: %.0f, then %.3f, %.3f and (the song's at half) %.3f of it", a, b / a, c / a, d / a);
        b = loud(23 * ROW, 27 * ROW, 0), c = loud(23 * ROW, 27 * ROW, 1);
        CHECK(same(b, c) && near(b, FULL / 4, 0.03), "in the middle: %.0f and %.0f", b, c);
        b = loud(28 * ROW, 32 * ROW, 0), c = loud(28 * ROW, 32 * ROW, 1);
        CHECK(near(b, FULL / 2, 0.03) && c == 0, "hard left: %.0f and %.0f", b, c);
        b = loud(33 * ROW, 37 * ROW, 0), c = loud(33 * ROW, 37 * ROW, 1);
        CHECK(b == 0 && near(c, FULL / 2, 0.03) && frames == 38 * ROW, "hard right: %.0f and %.0f", b, c);
        s3m_free(m);
    }
    if ((m = play("fast.s3m")) != NULL) {
        a = loud(100, ROW, 0), b = loud(3 * ROW, 8 * ROW, 0);
        CHECK(near(b / a, 40.0 / 64, 0.02), "slides on the first tick too: %.3f", b / a);
        s3m_free(m);
    }
    /* a note cut, cut at its third tick, struck at a third tick, and not at a seventh there is none of */
    if ((m = play("cut.s3m")) != NULL) {
        a = loud(ROW, 4 * ROW, 0), b = loud(4 * ROW, 8 * ROW, 0);
        CHECK(a > 5000 && b == 0, "cut: %.0f then %.0f", a, b);
        a = loud(8 * ROW, 8 * ROW + 3 * TICK, 0), b = loud(8 * ROW + 3 * TICK, 12 * ROW + 3 * TICK, 0);
        CHECK(a > 5000 && b == 0, "cut at a tick: %.0f then %.0f", a, b);
        a = loud(12 * ROW + 3 * TICK, 16 * ROW, 0), b = hz(12 * ROW + 3 * TICK, 16 * ROW, 0), c = hz(16 * ROW, 20 * ROW, 0);
        CHECK(a > 5000 && near(b, 2 * C4, 0.02) && near(c, 2 * C4, 0.02) && frames == 21 * ROW,
              "struck late: %.0f loud, %.1f Hz; and never: %.1f Hz", a, b, c);
        s3m_free(m);
    }
    /* instruments: of 16 bits; with a middle C an octave up; half as loud; and tuned down by S28 */
    if ((m = play("kinds.s3m")) != NULL) {
        a = hz(ROW, 7 * ROW, 0), b = loud(ROW, 7 * ROW, 0);
        CHECK(near(a, C4, 0.01) && near(b, full, 0.01), "16 bits: %.1f Hz, %.0f loud", a, b);
        a = hz(9 * ROW, 15 * ROW, 0), b = loud(17 * ROW, 23 * ROW, 0), c = hz(25 * ROW, 31 * ROW, 0);
        CHECK(near(a, 2 * C4, 0.01) && near(b, full / 2, 0.01) && near(c, 14317056.0 / 1813 / 32, 0.01) && frames == 33 * ROW,
              "instruments: %.1f Hz, %.0f loud, %.1f Hz", a, b, c);
        s3m_free(m);
    }
    /* struck again and again, a short sample goes on; tremor: four ticks of six; vibrato: the cycles' lengths
       differ, about the same pitch; tremolo: some ticks much louder than others */
    if ((m = play("fx.s3m")) != NULL) {
        unsigned long t;
        double lo = 1e9, hi = 0;
        a = loud(ROW + 200, 3 * ROW, 0), b = loud(6 * ROW, 8 * ROW, 0);
        CHECK(a > 4000 && b == 0, "struck again: %.0f, then %.0f", a, b);
        a = loud(8 * ROW, 12 * ROW, 0);
        CHECK(near(a / full, sqrt(4.0 / 6), 0.03), "tremor: %.3f of full", a / full);
        a = hz(16 * ROW, 24 * ROW, 0);
        CHECK(near(a, C4, 0.02) && spread(16 * ROW, 24 * ROW, 0) > 6 && spread(12 * ROW + 500, 16 * ROW, 0) <= 2,
              "vibrato: %.1f Hz, cycles differing by %ld frames (%ld with none)", a, spread(16 * ROW, 24 * ROW, 0),
              spread(12 * ROW + 500, 16 * ROW, 0));
        for (t = 24 * 6; t < 32 * 6; t++) {
            double l = loud(t * TICK, (t + 1) * TICK, 0);
            lo = l < lo ? l : lo, hi = l > hi ? l : hi;
        }
        CHECK(hi > 3 * lo && hi > full * 0.6 && frames == 33 * ROW, "tremolo: ticks from %.0f to %.0f loud", lo, hi);
        s3m_free(m);
    }
    /* eight channels: a note on the first (left) and an octave above on the sixth (right); quieter by the root
       of two */
    if ((m = play("eight.s3m")) != NULL) {
        a = hz(ROW, 15 * ROW, 0), b = hz(ROW, 15 * ROW, 1), c = loud(ROW, 15 * ROW, 0);
        CHECK(s3m_channels(m) == 8 && near(a, C4, 0.01) && near(b, 2 * C4, 0.01), "eight channels: %.1f left, %.1f right", a, b);
        CHECK(near(c, full * 0.7071 * sqrt(1 + 51.0 * 51 / (205.0 * 205)), 0.04), "eight channels: %.0f loud", c);
        s3m_free(m);
    }
    /* the AdLib's channel is not one of ours, and a note of its instrument is silence */
    if ((m = play("adlib.s3m")) != NULL) {
        a = hz(200, 4 * ROW, 0), b = loud(4 * ROW, 8 * ROW, 0);
        CHECK(s3m_channels(m) == 1 && near(a, C4, 0.02) && b == 0 && frames == 9 * ROW, "AdLib: %u channels, %.1f Hz, then %.0f",
              s3m_channels(m), a, b);
        s3m_free(m);
    }
    /* cut in its samples: it plays what there is (400 bytes: 2109 frames); in its tables, with no channel of
       samples, and no module: refused */
    if ((m = play("cutsample.s3m")) != NULL) {
        CHECK(loud(200, 1800, 0) > 5000 && loud(2300, 20000, 0) == 0, "cut in its samples: %.0f then %.0f", loud(200, 1800, 0),
              loud(2300, 20000, 0));
        s3m_free(m);
    }
    m = open_s3m("cuttable.s3m", &err);
    CHECK(!m && err == S3M_E_DAMAGED, "cut in its tables: %d", err);
    m = open_s3m("nochannel.s3m", &err);
    CHECK(!m && err == S3M_E_DAMAGED, "no channel of samples: %d", err);
    m = open_s3m("nos3m.s3m", &err);
    CHECK(!m && err == S3M_E_KIND, "no module: %d", err);
    CHECK(!s3m_open(file, 50, RATE, &err) && err == S3M_E_KIND && !s3m_open(file, file_size, 10, &err), "too short; no rate");

    /* A module with bytes changed: whatever comes of it, the player comes back and reads nothing outside it. */
    {
        static const char *const from[] = { "slide.s3m", "fx.s3m", "vols.s3m", "loop2.s3m" };
        unsigned long seed = 20261120ul, made = 0, opened = 0, round, k;
        static short some[2 * 4096];
        for (round = 0; round < 800; round++) {
            unsigned char *copy;
            unsigned long size, changes;
            if (round % 200 == 0) {
                m = open_s3m(from[round / 200], &err);
                CHECK(m != NULL, "%s: not opened", from[round / 200]);
                if (m)
                    s3m_free(m);
            }
            if (file_size < 0x70)
                continue;
            copy = malloc(file_size);
            size = file_size;
#define RND() (seed = seed * 1103515245ul + 12345ul, (seed >> 8) & 0xFFFFFFul)
            memcpy(copy, file, file_size);
            changes = 1 + RND() % 12;
            for (k = 0; k < changes; k++) {
                unsigned long at = RND() % 3 ? 0x20 + RND() % (size - 0x20) : RND() % size;
                if (at >= 0x2C && at < 0x30 && RND() % 8)       /* (mostly still a module) */
                    continue;
                copy[at] = (unsigned char)RND();
            }
            if (RND() % 4 == 0)
                size = 0x60 + RND() % (size - 0x60);
            made++;
            if ((m = s3m_open(copy, size, 8000 + RND() % 40000, &err)) != NULL) {
                unsigned long n, total = 0;
                opened++;
                s3m_smooth(m, (int)(RND() & 1));
                while (total < 400000 && (n = s3m_render(m, some, 4096)) != 0)
                    total += n;
                s3m_seek(m, RND() % 100000);
                s3m_render(m, some, 4096);
                s3m_length(m);
                s3m_free(m);
            }
            free(copy);
        }
        CHECK(made == 800 && opened > 300, "%lu changed modules, %lu of them opened", made, opened);
        printf("s3m_test: %lu changed modules, %lu of them opened\n", made, opened);
    }
    printf("s3m_test: %d failures (%d checks)\n", fails, checks);
    return fails != 0;
}
