/* Host test for the MOD player (apps/shared/mod.c): the small songs tests/host/mod_cases.py writes, each held to
 * what must be heard: which pitch when (the tone's cycles counted), how loud on which side, for how long (a row
 * is six ticks of 882 frames at 44100 Hz unless the song says otherwise), where a song ends; seeking gives the
 * same samples as playing to there; files cut short and files that are no modules; a module with bytes changed
 * at random comes to no harm.
 *
 *     mod_test DIR        (the .mod files mod_cases.py wrote) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../apps/shared/mod.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); \
                                                printf("\n"); fails++; } } while (0)

#define RATE 44100
#define TICK 882ul
#define ROW (6 * TICK)
#define MAXF (RATE * 30ul)

static const char *dir;
static unsigned char *file;
static unsigned long file_size;
static short *pcm;                      /* a song played: left, right, left ... */
static unsigned long frames;

static struct mod *open_mod(const char *name, int *err)
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
    return mod_open(file, file_size, RATE, err);
}

static struct mod *play(const char *name)       /* the whole song into pcm; the module (still open) or NULL */
{
    int err = 0;
    struct mod *m = open_mod(name, &err);
    unsigned long n;
    frames = 0;
    CHECK(m != NULL, "%s: not opened: %d", name, err);
    if (!m)
        return NULL;
    while (frames < MAXF && (n = mod_render(m, pcm + frames * 2, 4096 < MAXF - frames ? 4096 : MAXF - frames)) != 0)
        frames += n;
    CHECK(mod_length(m) == frames && mod_tell(m) == frames, "%s: %lu frames played, its length %lu", name, frames, mod_length(m));
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

static int near(double got, double want, double part)
{
    return fabs(got - want) <= want * part;
}

int main(int argc, char **argv)
{
    struct mod *m;
    double a, b, c, d;
    int err, o, p, r;
    dir = argc > 1 ? argv[1] : ".";
    pcm = malloc(MAXF * 2 * sizeof *pcm);

    /* three notes and a soft one: the pitches, the loudness, the sides, the length */
    if ((m = play("notes.mod")) != NULL) {
        a = hz(ROW, 15 * ROW, 0), b = hz(17 * ROW, 31 * ROW, 0), c = hz(33 * ROW, 47 * ROW, 0), d = hz(49 * ROW, 62 * ROW, 0);
        CHECK(near(a, 258.97, 0.005) && near(b, 517.9, 0.005) && near(c, 129.5, 0.01) && near(d, 258.97, 0.005),
              "notes: %.1f %.1f %.1f %.1f Hz", a, b, c, d);
        a = loud(ROW, 15 * ROW, 0), b = loud(49 * ROW, 62 * ROW, 0), c = loud(ROW, 15 * ROW, 1);
        CHECK(near(b / a, 0.25, 0.03), "notes: volume 16 of 64 is %.3f of full", b / a);
        CHECK(near(a / c, 3.0, 0.02), "notes: the left channel is %.2f times on the left what it is on the right", a / c);
        CHECK(near(a, 16384 * 0.75 * 0.7071, 0.03), "notes: one channel at its loudest is %.0f", a);
        CHECK(frames == 64 * ROW, "notes: %lu frames", frames);
        CHECK(!strcmp(mod_title(m), "test song") && mod_channels(m) == 4 && !strcmp(mod_sample_name(m, 1), "sample 1")
              && mod_sample_name(m, 32) == NULL, "notes: its texts: '%s'", mod_title(m));
        /* sought to, it is the same samples as played to */
        {
            static short part[2 * 5000];
            unsigned long at = 100000;
            mod_seek(m, at);
            mod_where(m, &o, &p, &r);
            CHECK(mod_tell(m) == at && o == 0 && p == 0 && r == (int)(at / ROW), "sought: at %lu, row %d", mod_tell(m), r);
            CHECK(mod_render(m, part, 5000) == 5000 && !memcmp(part, pcm + at * 2, sizeof part), "sought to %lu: other samples", at);
            mod_seek(m, 0);
            CHECK(mod_render(m, part, 5000) == 5000 && !memcmp(part, pcm, sizeof part), "sought to the start: other samples");
            mod_seek(m, frames + 1000);
            CHECK(mod_tell(m) == frames && mod_render(m, part, 100) == 0, "sought past the end: at %lu", mod_tell(m));
            mod_seek(m, 0);                     /* the nearer sample, not the line between two: the same song */
            mod_smooth(m, 0);
            CHECK(mod_render(m, part, 5000) == 5000 && memcmp(part, pcm, sizeof part) != 0, "not smoothed: the same samples");
        }
        mod_free(m);
    }
    if ((m = play("right.mod")) != NULL) {
        a = loud(ROW, 15 * ROW, 0), c = loud(ROW, 15 * ROW, 1);
        CHECK(near(c / a, 3.0, 0.02) && frames == 64 * ROW, "right: %.2f, %lu frames", c / a, frames);
        mod_free(m);
    }
    /* speeds and tempos: the songs' lengths */
    if ((m = play("speed.mod")) != NULL) {
        CHECK(frames == 32 * 6 * TICK + 32 * 3 * TICK, "speed: %lu frames", frames);
        mod_free(m);
    }
    if ((m = play("tempo.mod")) != NULL) {
        CHECK(frames == 64 * 6 * 735ul, "tempo 150: %lu frames", frames);
        mod_free(m);
    }
    if ((m = play("jump.mod")) != NULL) {
        a = hz(ROW, 7 * ROW, 0), b = hz(9 * ROW, 15 * ROW, 0);
        CHECK(frames == 16 * ROW && near(a, 258.97, 0.01) && near(b, 517.9, 0.01), "jump: %lu frames, %.1f then %.1f Hz", frames, a, b);
        mod_free(m);
    }
    if ((m = play("loop.mod")) != NULL) {
        CHECK(frames == 16 * ROW, "loop: %lu frames (%lu rows)", frames, frames / ROW);
        mod_free(m);
    }
    if ((m = play("delay.mod")) != NULL) {
        CHECK(frames == 24 * TICK, "delay: %lu frames (%lu ticks)", frames, frames / TICK);
        mod_free(m);
    }
    if ((m = play("speed0.mod")) != NULL) {
        CHECK(frames == 4 * ROW, "speed 0: %lu frames", frames);
        mod_free(m);
    }
    /* a sample with no loop ends by itself: 1000 bytes at 8287 a second are 5321 frames */
    if ((m = play("blip.mod")) != NULL) {
        CHECK(loud(500, 4500, 0) > 5000 && loud(5400, 20000, 0) == 0 && frames == 16 * ROW, "blip: %.0f then %.0f", loud(500, 4500, 0),
              loud(5400, 20000, 0));
        mod_free(m);
    }
    /* slides: up by 5 a tick for eleven rows (428 to 153), then to a note (428 to 214) and no further */
    if ((m = play("slide.mod")) != NULL) {
        a = hz(12 * ROW, 16 * ROW, 0), b = hz(25 * ROW, 40 * ROW, 0), c = hz(16 * ROW + 100, 17 * ROW, 0);
        CHECK(near(a, 3546895.0 / 153 / 32, 0.01) && near(b, 517.9, 0.005) && near(c, 258.97, 0.03) && frames == 41 * ROW,
              "slide: %.1f Hz after the slide up, %.1f at the note slid to, %.1f before; %lu rows", a, b, c, frames / ROW);
        mod_free(m);
    }
    /* arpeggio: the note, four and seven semitones above, a tick each: their mean is heard over a row */
    if ((m = play("arp.mod")) != NULL) {
        a = hz(0, 15 * ROW, 0);
        b = (3546895.0 / 428 + 3546895.0 / 339 + 3546895.0 / 285) / 3 / 32;
        CHECK(near(a, b, 0.03), "arpeggio: %.1f Hz in the mean, %.1f wanted", a, b);
        mod_free(m);
    }
    if ((m = play("fine.mod")) != NULL) {
        a = hz(ROW, 15 * ROW, 0);
        CHECK(near(a, 3546895.0 / 407 / 32, 0.005), "fine tuning +7: %.1f Hz", a);
        mod_free(m);
    }
    /* eight channels: a note on the first (left) and an octave above on the sixth (right), at half the loudness */
    if ((m = play("eight.mod")) != NULL) {
        a = hz(ROW, 15 * ROW, 0), b = hz(ROW, 15 * ROW, 1), c = loud(ROW, 15 * ROW, 0);
        CHECK(mod_channels(m) == 8 && near(a, 258.97, 0.01) && near(b, 517.9, 0.01), "eight channels: %.1f left, %.1f right", a, b);
        CHECK(c > 3000 && c < 16384 * 0.5, "eight channels: %.0f loud", c);
        mod_free(m);
    }
    /* cut in its samples: it plays what there is (400 bytes: 2129 frames); in its patterns, and no module: refused */
    if ((m = play("cutsample.mod")) != NULL) {
        CHECK(loud(200, 1800, 0) > 5000 && loud(2300, 20000, 0) == 0, "cut in its samples: %.0f then %.0f", loud(200, 1800, 0),
              loud(2300, 20000, 0));
        mod_free(m);
    }
    m = open_mod("cutpattern.mod", &err);
    CHECK(!m && err == MOD_E_DAMAGED, "cut in its patterns: %d", err);
    m = open_mod("nomod.mod", &err);
    CHECK(!m && err == MOD_E_KIND, "no module: %d", err);
    CHECK(!mod_open(file, 100, RATE, &err) && err == MOD_E_KIND && !mod_open(file, file_size, 10, &err), "too short; no rate");

    /* A module with bytes changed: whatever comes of it, the player comes back and reads nothing outside it. */
    {
        unsigned long seed = 20261120ul, made = 0, opened = 0, round, k;
        static short some[2 * 4096];
        m = open_mod("slide.mod", &err);
        if (m)
            mod_free(m);
        for (round = 0; round < 400 && file_size > 1100; round++) {
            unsigned char *copy = malloc(file_size);
            unsigned long size = file_size, changes;
#define RND() (seed = seed * 1103515245ul + 12345ul, (seed >> 8) & 0xFFFFFFul)
            memcpy(copy, file, file_size);
            changes = 1 + RND() % 12;
            for (k = 0; k < changes; k++) {
                unsigned long at = RND() % 3 ? 20 + RND() % 1064 : RND() % size;       /* (mostly in its tables) */
                copy[at] = (unsigned char)RND();
            }
            if (RND() % 4 == 0)
                size = 1084 + RND() % (size - 1084);
            made++;
            if ((m = mod_open(copy, size, 8000 + RND() % 40000, &err)) != NULL) {
                unsigned long n, total = 0;
                opened++;
                mod_smooth(m, (int)(RND() & 1));
                while (total < 400000 && (n = mod_render(m, some, 4096)) != 0)
                    total += n;
                mod_seek(m, RND() % 100000);
                mod_render(m, some, 4096);
                mod_length(m);
                mod_free(m);
            }
            free(copy);
        }
        CHECK(made == 400 && opened > 100, "%lu changed modules, %lu of them opened", made, opened);
        printf("mod_test: %lu changed modules, %lu of them opened\n", made, opened);
    }
    printf("mod_test: %d failures (%d checks)\n", fails, checks);
    return fails != 0;
}
