/* Host test for the XM player (apps/shared/xm.c): the small songs tests/host/xm_cases.py writes, each held to
 * what must be heard: which pitch when (the tone's cycles counted), how loud on which side, for how long (a row
 * is six ticks of 882 frames at 44100 Hz unless the song says otherwise), where a song ends; seeking gives the
 * same samples as playing to there; files cut short and files that are no modules; a module with bytes changed
 * at random comes to no harm.
 *
 *     xm_test DIR        (the .xm files xm_cases.py wrote) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../apps/shared/xm.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); \
                                                printf("\n"); fails++; } } while (0)

#define RATE 44100
#define TICK 882ul
#define ROW (6 * TICK)
#define MAXF (RATE * 30ul)
#define C4 (8363.0 / 32)                /* the sine's middle C: Hz */
#define SIDE (32512.0 * 0.7071 / 4)     /* the sine at its loudest, in the middle: what each side has of it */

static const char *dir;
static unsigned char *file;
static unsigned long file_size;
static short *pcm;                      /* a song played: left, right, left ... */
static unsigned long frames;

static struct xm *open_xm(const char *name, int *err)
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
    return xm_open(file, file_size, RATE, err);
}

static struct xm *play(const char *name)        /* the whole song into pcm; the module (still open) or NULL */
{
    int err = 0;
    struct xm *m = open_xm(name, &err);
    unsigned long n;
    frames = 0;
    CHECK(m != NULL, "%s: not opened: %d", name, err);
    if (!m)
        return NULL;
    while (frames < MAXF && (n = xm_render(m, pcm + frames * 2, 4096 < MAXF - frames ? 4096 : MAXF - frames)) != 0)
        frames += n;
    CHECK(xm_length(m) == frames && xm_tell(m) == frames, "%s: %lu frames played, its length %lu", name, frames, xm_length(m));
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

static void slides(const char *name, double up, double down, double fine)
{
    struct xm *m = play(name);
    double a, b, c, d, e;
    if (!m)
        return;
    a = hz(12 * ROW, 16 * ROW, 0), b = hz(25 * ROW, 40 * ROW, 0), c = hz(16 * ROW + 100, 17 * ROW, 0);
    d = hz(46 * ROW, 52 * ROW, 0), e = hz(53 * ROW, 60 * ROW, 0);
    CHECK(near(a, up, 0.01) && near(b, 2 * C4, 0.005) && near(c, C4, 0.03) && frames == 61 * ROW,
          "%s: %.1f Hz after the slide up, %.1f at the note slid to, %.1f before; %lu rows", name, a, b, c, frames / ROW);
    CHECK(near(d, down, 0.01) && near(e, fine, 0.01), "%s: %.1f Hz after the slide down, %.1f after the fine one up", name, d, e);
    xm_free(m);
}

int main(int argc, char **argv)
{
    static const char *const both[] = { "notes.xm", "amiga.xm" }, *const arps[] = { "arp.xm", "amiarp.xm" };
    struct xm *m;
    double a, b, c, d;
    int err, i;
    dir = argc > 1 ? argv[1] : ".";
    pcm = malloc(MAXF * 2 * sizeof *pcm);

    /* three notes and a soft one: the pitches, the loudness, the sides, the length; by either kind of period */
    for (i = 0; i < 2; i++) {
        if (!(m = play(both[i])))
            continue;
        a = hz(ROW, 15 * ROW, 0), b = hz(17 * ROW, 31 * ROW, 0), c = hz(33 * ROW, 47 * ROW, 0), d = hz(49 * ROW, 62 * ROW, 0);
        CHECK(near(a, C4, 0.005) && near(b, 2 * C4, 0.005) && near(c, C4 / 2, 0.01) && near(d, C4, 0.005),
              "%s: %.1f %.1f %.1f %.1f Hz", both[i], a, b, c, d);
        a = loud(ROW, 15 * ROW, 0), b = loud(49 * ROW, 62 * ROW, 0), c = loud(ROW, 15 * ROW, 1);
        CHECK(near(b / a, 0.25, 0.03), "%s: volume 16 of 64 is %.3f of full", both[i], b / a);
        CHECK(same(a, c) && near(a, SIDE, 0.03), "%s: one channel at its loudest is %.0f and %.0f", both[i], a, c);
        CHECK(frames == 64 * ROW, "%s: %lu frames", both[i], frames);
        CHECK(!strcmp(xm_title(m), "test song") && xm_channels(m) == 2, "%s: '%s', %u channels", both[i], xm_title(m),
              xm_channels(m));
        if (!i) {                               /* sought to, it is the same samples as played to */
            static short part[2 * 5000];
            unsigned long at = 100000;
            xm_seek(m, at);
            CHECK(xm_tell(m) == at, "sought: at %lu", xm_tell(m));
            CHECK(xm_render(m, part, 5000) == 5000 && !memcmp(part, pcm + at * 2, sizeof part), "sought to %lu: other samples", at);
            xm_seek(m, 0);
            CHECK(xm_render(m, part, 5000) == 5000 && !memcmp(part, pcm, sizeof part), "sought to the start: other samples");
            xm_seek(m, frames + 1000);
            CHECK(xm_tell(m) == frames && xm_render(m, part, 100) == 0, "sought past the end: at %lu", xm_tell(m));
            xm_seek(m, 0);                      /* the nearer sample, not the line between two: the same song */
            xm_smooth(m, 0);
            CHECK(xm_render(m, part, 5000) == 5000 && memcmp(part, pcm, sizeof part) != 0, "not smoothed: the same samples");
        }
        xm_free(m);
    }
    if ((m = play("sides.xm")) != NULL) {
        a = loud(ROW, 7 * ROW, 0), b = loud(ROW, 7 * ROW, 1), c = loud(9 * ROW, 15 * ROW, 0), d = loud(9 * ROW, 15 * ROW, 1);
        CHECK(near(a, 2 * SIDE, 0.03) && b == 0 && c == 0 && near(d, 2 * SIDE, 0.03),
              "the samples' places: %.0f and %.0f, then %.0f and %.0f", a, b, c, d);
        xm_free(m);
    }
    /* speeds and tempos: the songs' lengths */
    if ((m = play("speed.xm")) != NULL) {
        CHECK(frames == 32 * 6 * TICK + 32 * 3 * TICK, "speed: %lu frames", frames);
        xm_free(m);
    }
    if ((m = play("tempo.xm")) != NULL) {
        CHECK(frames == 64 * 6 * 735ul, "tempo 150: %lu frames", frames);
        xm_free(m);
    }
    if ((m = play("header.xm")) != NULL) {
        CHECK(frames == 64 * 3 * 735ul, "the header's speed and tempo: %lu frames", frames);
        xm_free(m);
    }
    if ((m = play("stop.xm")) != NULL) {
        CHECK(frames == 10 * ROW, "F00: %lu rows", frames / ROW);
        xm_free(m);
    }
    if ((m = play("jump.xm")) != NULL) {
        a = hz(ROW, 7 * ROW, 0), b = hz(9 * ROW, 15 * ROW, 0);
        CHECK(frames == 16 * ROW && near(a, C4, 0.01) && near(b, 2 * C4, 0.01), "jump: %lu frames, %.1f then %.1f Hz", frames, a, b);
        xm_free(m);
    }
    /* 64 empty rows for the order with no pattern, then 16 rows of eight channels (quieter by the root of two):
       two notes, both in the middle */
    if ((m = play("rows.xm")) != NULL) {
        a = loud(0, 64 * ROW, 0), b = loud(65 * ROW, 79 * ROW, 0), c = loud(65 * ROW, 79 * ROW, 1);
        CHECK(frames == 80 * ROW && xm_channels(m) == 8 && a == 0 && same(b, c) && near(b, SIDE, 0.04),
              "rows: %lu of them, %u channels, %.0f then %.0f and %.0f", frames / ROW, xm_channels(m), a, b, c);
        xm_free(m);
    }
    if ((m = play("loop.xm")) != NULL) {
        CHECK(frames == 16 * ROW, "loop: %lu frames (%lu rows)", frames, frames / ROW);
        xm_free(m);
    }
    /* a row three times as long, its note (5273 frames of it) struck once */
    if ((m = play("delay.xm")) != NULL) {
        CHECK(frames == 24 * TICK && loud(500, 4500, 0) > 3000 && loud(5400, 24 * TICK, 0) == 0,
              "delay: %lu frames (%lu ticks), %.0f then %.0f", frames, frames / TICK, loud(500, 4500, 0), loud(5400, 24 * TICK, 0));
        xm_free(m);
    }
    /* a sample with no loop ends by itself: 1000 samples at 8363 a second are 5273 frames */
    if ((m = play("blip.xm")) != NULL) {
        CHECK(loud(500, 4500, 0) > 3000 && loud(5400, 20000, 0) == 0 && frames == 16 * ROW, "blip: %.0f then %.0f", loud(500, 4500, 0),
              loud(5400, 20000, 0));
        xm_free(m);
    }
    /* ... from 512 samples in: 448 of tone are 2362 frames; and from past its end, not at all */
    if ((m = play("offset.xm")) != NULL) {
        a = loud(200, 2200, 0), b = loud(2700, 4 * ROW, 0);
        CHECK(a > 3000 && b == 0, "offset: %.0f then %.0f", a, b);
        a = loud(4 * ROW + 2700, 4 * ROW + 4800, 0), b = loud(4 * ROW + 5400, 8 * ROW, 0);
        CHECK(a > 3000 && b == 0, "no offset: %.0f then %.0f", a, b);
        a = loud(8 * ROW + 200, 8 * ROW + 2200, 0), b = loud(8 * ROW + 2700, 12 * ROW, 0);
        CHECK(a > 3000 && b == 0, "the offset again: %.0f then %.0f", a, b);
        a = loud(12 * ROW, 16 * ROW, 0);
        CHECK(a == 0 && frames == 17 * ROW, "an offset past the end: %.0f", a);
        xm_free(m);
    }
    /* slides: FastTracker's periods are 64 to the semitone (up by 1100, down by 500, finely up by 60); the
       Amiga's as in a MOD, four times as fine (1712 to 612, to 2212, to 1652) */
    slides("slide.xm", C4 * pow(2, 1100.0 / 768), C4 * pow(2, -500.0 / 768), C4 * pow(2, 60.0 / 768));
    slides("amislide.xm", 14317456.0 / 612 / 32, 14317456.0 / 2212 / 32, 14317456.0 / 1652 / 32);
    /* arpeggio: the note, four and seven semitones above, a tick each: their mean is heard over a row */
    for (i = 0; i < 2; i++)
        if ((m = play(arps[i])) != NULL) {
            a = hz(ROW, 17 * ROW, 0);
            b = C4 * (1 + pow(2, 4.0 / 12) + pow(2, 7.0 / 12)) / 3;
            CHECK(near(a, b, 0.02), "%s: %.1f Hz in the mean, %.1f wanted", arps[i], a, b);
            xm_free(m);
        }
    /* volumes: 64, slid to 44, to 40 and back to 44, to 54; the song's halved; hard left and right; slid by the
       volume column; the place slid left; the volume set */
    if ((m = play("vols.xm")) != NULL) {
        a = loud(100, ROW, 0);
        b = loud(3 * ROW, 8 * ROW, 0), c = loud(11 * ROW, 16 * ROW, 0), d = loud(17 * ROW, 21 * ROW, 0);
        CHECK(near(a, SIDE, 0.03) && near(b / a, 44.0 / 64, 0.02) && near(c / a, 54.0 / 64, 0.02) && near(d / a, 27.0 / 64, 0.02),
              "volume slides: %.0f, then %.3f, %.3f and (the song's at half) %.3f of it", a, b / a, c / a, d / a);
        b = loud(23 * ROW, 27 * ROW, 0), c = loud(23 * ROW, 27 * ROW, 1);
        CHECK(near(b, 2 * SIDE, 0.03) && c == 0, "hard left: %.0f and %.0f", b, c);
        b = loud(28 * ROW, 32 * ROW, 0), c = loud(28 * ROW, 32 * ROW, 1);
        CHECK(b == 0 && near(c, 2 * SIDE, 0.03), "hard right: %.0f and %.0f", b, c);
        b = loud(34 * ROW, 38 * ROW, 0), c = loud(34 * ROW, 38 * ROW, 1);
        CHECK(same(b, c) && near(b / a, 44.0 / 64, 0.02), "slid by the volume column: %.3f of full, %.0f and %.0f", b / a, b, c);
        b = loud(40 * ROW, 44 * ROW, 0), c = loud(40 * ROW, 44 * ROW, 1), d = loud(45 * ROW, 49 * ROW, 0);
        CHECK(near(b / a, 2 * 44.0 / 64, 0.02) && c == 0 && near(d / a, 2 * 16.0 / 64, 0.02) && frames == 50 * ROW,
              "slid to the left: %.3f and %.0f; then set: %.3f", b / a, c, d / a);
        xm_free(m);
    }
    /* an envelope: half from the tenth tick while the key is down (48 ticks); let go, half for ten ticks more
       and to nothing in another ten */
    if ((m = play("env.xm")) != NULL) {
        a = loud(0, TICK, 0), b = loud(12 * TICK, 48 * TICK, 0), c = loud(49 * TICK, 58 * TICK, 0), d = loud(69 * TICK, 96 * TICK, 0);
        CHECK(a > SIDE * 0.9 && near(b, SIDE / 2, 0.03) && near(c, SIDE / 2, 0.03) && d == 0 && loud(62 * TICK, 64 * TICK, 0) > 100
              && frames == 17 * ROW, "envelope: %.0f, held at %.0f, let go %.0f, then %.0f", a, b, c, d);
        xm_free(m);
    }
    /* a fade of an eighth a tick from the key's letting go (tick 12): half at tick 15, nothing from tick 19 */
    if ((m = play("fade.xm")) != NULL) {
        a = loud(TICK, 12 * TICK, 0), b = loud(15 * TICK, 16 * TICK, 0), c = loud(20 * TICK, 48 * TICK, 0);
        CHECK(near(a, SIDE, 0.03) && near(b, SIDE / 2, 0.05) && c == 0, "fade: %.0f, %.0f, %.0f", a, b, c);
        xm_free(m);
    }
    if ((m = play("keyoff.xm")) != NULL) {
        a = loud(ROW, 4 * ROW, 0), b = loud(4 * ROW, 8 * ROW, 0);
        CHECK(near(a, SIDE, 0.03) && b == 0, "a key let go with no envelope: %.0f then %.0f", a, b);
        a = loud(8 * ROW, 12 * ROW + 3 * TICK, 0), b = loud(12 * ROW + 3 * TICK, 16 * ROW, 0);
        CHECK(near(a, SIDE, 0.03) && b == 0 && frames == 17 * ROW, "... at a tick: %.0f then %.0f", a, b);
        xm_free(m);
    }
    /* a note with no instrument: the pitch changes, the volume (16) stays; an instrument with no note: the
       sample's own volume, the same pitch */
    if ((m = play("bare.xm")) != NULL) {
        a = loud(ROW, 4 * ROW, 0), b = loud(5 * ROW, 8 * ROW, 0), c = loud(9 * ROW, 12 * ROW, 0);
        CHECK(near(a, SIDE / 4, 0.03) && near(b, SIDE / 4, 0.03) && near(c, SIDE, 0.03), "bare cells: %.0f, %.0f, %.0f loud", a, b, c);
        a = hz(5 * ROW, 8 * ROW, 0), b = hz(9 * ROW, 12 * ROW, 0);
        CHECK(near(a, 2 * C4, 0.02) && near(b, 2 * C4, 0.02), "bare cells: %.1f and %.1f Hz", a, b);
        xm_free(m);
    }
    /* an instrument's second sample (16 bits, half as loud, an octave up) from C-5; one half a semitone up and
       an octave down; and its tuning taken off by E58 */
    if ((m = play("multi.xm")) != NULL) {
        a = hz(ROW, 7 * ROW, 0), b = hz(9 * ROW, 15 * ROW, 0), c = hz(17 * ROW, 23 * ROW, 0), d = hz(25 * ROW, 31 * ROW, 0);
        CHECK(near(a, C4, 0.01) && near(b, 4 * C4, 0.01) && near(c, C4 / 2 * pow(2, 0.5 / 12), 0.01) && near(d, C4 / 2, 0.01),
              "samples: %.1f, %.1f, %.1f, %.1f Hz", a, b, c, d);
        a = loud(ROW, 7 * ROW, 0), b = loud(9 * ROW, 15 * ROW, 0);
        CHECK(near(a, SIDE, 0.03) && near(b, SIDE / 2, 0.03) && frames == 33 * ROW, "samples: %.0f and %.0f loud", a, b);
        xm_free(m);
    }
    /* a loop back and forth is twice as long a cycle as the same loop forwards */
    if ((m = play("ping.xm")) != NULL) {
        a = hz(ROW, 7 * ROW, 0), b = hz(9 * ROW, 15 * ROW, 0);
        CHECK(near(a, C4, 0.01) && near(b, 2 * C4, 0.01), "back and forth: %.1f Hz; forwards: %.1f", a, b);
        xm_free(m);
    }
    /* struck again and again, a short sample goes on; tremor: four ticks of six; vibrato: the cycles' lengths
       differ, about the same pitch; tremolo: some ticks much louder than others; the instrument's own vibrato;
       a note cut at its third tick, struck at a third tick, and not at a seventh there is none of; struck again
       by E93; and F00 */
    if ((m = play("fx.xm")) != NULL) {
        unsigned long t;
        double lo = 1e9, hi = 0;
        a = loud(ROW + 200, 3 * ROW, 0), b = loud(6 * ROW, 8 * ROW, 0);
        CHECK(a > 3000 && b == 0, "struck again: %.0f, then %.0f", a, b);
        a = loud(8 * ROW, 12 * ROW, 0);
        CHECK(near(a / SIDE, sqrt(4.0 / 6), 0.03), "tremor: %.3f of full", a / SIDE);
        a = hz(16 * ROW, 24 * ROW, 0);
        CHECK(near(a, C4, 0.02) && spread(16 * ROW, 24 * ROW, 0) > 6 && spread(12 * ROW + 500, 16 * ROW, 0) <= 2,
              "vibrato: %.1f Hz, cycles differing by %ld frames (%ld with none)", a, spread(16 * ROW, 24 * ROW, 0),
              spread(12 * ROW + 500, 16 * ROW, 0));
        for (t = 24 * 6; t < 32 * 6; t++) {
            double l = loud(t * TICK, (t + 1) * TICK, 0);
            lo = l < lo ? l : lo, hi = l > hi ? l : hi;
        }
        CHECK(hi > 3 * lo && hi > SIDE * 0.6, "tremolo: ticks from %.0f to %.0f loud", lo, hi);
        a = hz(32 * ROW, 40 * ROW, 0);
        CHECK(near(a, C4, 0.01) && spread(32 * ROW + 500, 40 * ROW, 0) >= 3, "the instrument's vibrato: %.1f Hz, cycles differing by %ld",
              a, spread(32 * ROW + 500, 40 * ROW, 0));
        a = loud(40 * ROW, 40 * ROW + 3 * TICK, 0), b = loud(40 * ROW + 3 * TICK, 44 * ROW + 3 * TICK, 0);
        CHECK(a > 3000 && b == 0, "cut at a tick: %.0f then %.0f", a, b);
        a = loud(44 * ROW + 3 * TICK, 48 * ROW, 0), b = hz(44 * ROW + 3 * TICK, 48 * ROW, 0), c = hz(48 * ROW, 52 * ROW, 0);
        CHECK(a > 3000 && near(b, 2 * C4, 0.02) && near(c, 2 * C4, 0.02), "struck late: %.0f loud, %.1f Hz; and never: %.1f Hz", a, b, c);
        a = loud(53 * ROW + 200, 55 * ROW, 0);
        CHECK(a > 3000 && frames == 61 * ROW, "struck again by E93: %.0f; %lu rows", a, frames / ROW);
        xm_free(m);
    }
    if ((m = play("empty.xm")) != NULL) {
        a = loud(ROW, 4 * ROW, 0), b = loud(4 * ROW, 8 * ROW, 0);
        CHECK(a > 3000 && b == 0 && frames == 9 * ROW, "an instrument with no samples: %.0f then %.0f", a, b);
        xm_free(m);
    }
    /* cut in its samples: it plays what there is (400 samples: 2109 frames); in its patterns, of an older form,
       and no module: refused, the bytes as they were */
    if ((m = play("cutsample.xm")) != NULL) {
        CHECK(loud(200, 1800, 0) > 3000 && loud(2300, 20000, 0) == 0, "cut in its samples: %.0f then %.0f", loud(200, 1800, 0),
              loud(2300, 20000, 0));
        xm_free(m);
    }
    m = open_xm("cutpattern.xm", &err);
    CHECK(!m && err == XM_E_DAMAGED, "cut in its patterns: %d", err);
    m = open_xm("old.xm", &err);
    CHECK(!m && err == XM_E_KIND, "an older form: %d", err);
    m = open_xm("noxm.xm", &err);
    CHECK(!m && err == XM_E_KIND, "no module: %d", err);
    CHECK(!xm_open(file, 50, RATE, &err) && err == XM_E_KIND && !xm_open(file, file_size, 10, &err), "too short; no rate");

    /* A module with bytes changed: whatever comes of it, the player comes back and reads nothing outside it. */
    {
        static const char *const from[] = { "slide.xm", "fx.xm", "vols.xm", "env.xm", "multi.xm" };
        unsigned long seed = 20261120ul, made = 0, opened = 0, round, k, orig_size = 0;
        unsigned char *orig = NULL;
        static short some[2 * 4096];
        for (round = 0; round < 1000; round++) {
            unsigned char *copy;
            unsigned long size, changes;
            if (round % 200 == 0) {             /* (the bytes as they are in the file: opening changes them) */
                char path[600];
                FILE *f;
                snprintf(path, sizeof path, "%s/%s", dir, from[round / 200]);
                free(orig);
                orig = NULL, orig_size = 0;
                if ((f = fopen(path, "rb")) != NULL) {
                    orig = malloc(1 << 16);
                    orig_size = (unsigned long)fread(orig, 1, 1 << 16, f);
                    fclose(f);
                }
                CHECK(orig_size > 400, "%s: not read", from[round / 200]);
            }
            if (orig_size < 400)
                continue;
            copy = malloc(orig_size);
            size = orig_size;
#define RND() (seed = seed * 1103515245ul + 12345ul, (seed >> 8) & 0xFFFFFFul)
            memcpy(copy, orig, orig_size);
            changes = 1 + RND() % 12;
            for (k = 0; k < changes; k++) {
                unsigned long at = RND() % 3 ? 58 + RND() % (size - 58) : RND() % size;
                if ((at < 17 || (at >= 58 && at < 64)) && RND() % 8)    /* (mostly still a module) */
                    continue;
                if (at >= 80 && at < 336 && RND() % 4)                  /* (and seldom in its unused orders) */
                    at = 336 + RND() % (size - 336);
                copy[at] = (unsigned char)RND();
            }
            if (RND() % 4 == 0)
                size = 336 + RND() % (size - 336);
            made++;
            if ((m = xm_open(copy, size, 8000 + RND() % 40000, &err)) != NULL) {
                unsigned long n, total = 0;
                opened++;
                xm_smooth(m, (int)(RND() & 1));
                while (total < 400000 && (n = xm_render(m, some, 4096)) != 0)
                    total += n;
                xm_seek(m, RND() % 100000);
                xm_render(m, some, 4096);
                xm_length(m);
                xm_free(m);
            }
            free(copy);
        }
        free(orig);
        CHECK(made == 1000 && opened > 400, "%lu changed modules, %lu of them opened", made, opened);
        printf("xm_test: %lu changed modules, %lu of them opened\n", made, opened);
    }
    printf("xm_test: %d failures (%d checks)\n", fails, checks);
    return fails != 0;
}
