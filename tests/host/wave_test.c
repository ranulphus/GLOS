/* Host test for sound in memory and WAV files (apps/shared/wave.c): the edits on sounds whose every sample is
 * known; the WAV files tests/host/wave_cases.py wrote, each against the samples it must be read as; WAV files
 * written here, which wave_cases.py check then reads back with Python's own reader and its own ADPCM unpacker.
 *
 *     wave_test DIR        (DIR/MANIFEST and the files beside it; out-*.wav and out-*.pcm are written there) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../apps/shared/wave.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); \
                                                printf("\n"); fails++; } } while (0)

/* The tests' sound, as wave_cases.py has it: whole numbers only. */
static short sig(unsigned long i, unsigned c)
{
    long period = c == 0 ? 200 : 316, k = (long)(i % (unsigned long)period);
    long tri = k < period / 2 ? k : period - k;
    long swell = 2000 + (long)((i / 64) % 97) * 250;
    return (short)(tri * 2 * swell / (period / 2) - swell);
}

static void make(struct wave *w, unsigned rate, unsigned ch, unsigned long n)
{
    unsigned long i;
    unsigned c;
    short f[2];
    wave_init(w, rate, ch);
    for (i = 0; i < n; i++) {
        for (c = 0; c < ch; c++)
            f[c] = sig(i, c);
        wave_append(w, f, 1);
    }
}

static void ramp(struct wave *w, unsigned ch, long n, long from, long step)     /* from, from + step, ... (both channels) */
{
    long i;
    short f[2];
    wave_init(w, 8000, ch);
    for (i = 0; i < n; i++) {
        f[0] = f[1] = (short)(from + i * step);
        wave_append(w, f, 1);
    }
}

static void edits(void)
{
    struct wave w, cut, piece;
    unsigned long i;
    int ok, r;
    short lo, hi;

    /* cut, insert, trim: a ramp whose every sample is its place */
    ramp(&w, 2, 1000, 0, 1);
    r = wave_cut(&w, 100, 300, &cut);
    ok = r == 0 && w.frames == 800 && cut.frames == 200 && cut.channels == 2 && cut.rate == 8000;
    for (i = 0; ok && i < 800; i++)
        ok = w.pcm[i * 2] == (short)(i < 100 ? i : i + 200) && w.pcm[i * 2 + 1] == w.pcm[i * 2];
    for (i = 0; ok && i < 200; i++)
        ok = cut.pcm[i * 2] == (short)(100 + i);
    CHECK(ok, "cut: frames %lu, cut %lu", w.frames, cut.frames);
    r = wave_insert(&w, 100, &cut);
    ok = r == 0 && w.frames == 1000;
    for (i = 0; ok && i < 1000; i++)
        ok = w.pcm[i * 2] == (short)i && w.pcm[i * 2 + 1] == (short)i;
    CHECK(ok, "insert puts back what cut took: frames %lu", w.frames);
    r = wave_insert(&w, 5000, &cut);                    /* (past the end: at the end) */
    CHECK(r == 0 && w.frames == 1200 && w.pcm[1000 * 2] == 100 && w.pcm[1199 * 2 + 1] == 299, "insert at the end: %lu", w.frames);
    wave_trim(&w, 1000, 1100);
    CHECK(w.frames == 100 && w.pcm[0] == 100 && w.pcm[99 * 2] == 199, "trim: %lu", w.frames);
    wave_cut(&w, 90, 5000, NULL);                       /* (to beyond the end: to the end) */
    CHECK(w.frames == 90 && w.pcm[89 * 2] == 189, "cut to the end: %lu", w.frames);
    wave_cut(&w, 50, 20, NULL);                         /* (backwards: nothing) */
    CHECK(w.frames == 90, "a cut of nothing: %lu", w.frames);
    wave_init(&piece, 11025, 2);
    CHECK(wave_insert(&w, 0, &piece) == WAVE_E_ARGUMENT, "a piece of another rate is refused");
    wave_free(&piece);
    wave_free(&cut);
    wave_free(&w);

    /* gain, with what goes over the top held there; silence; reverse */
    ramp(&w, 1, 201, -20000, 200);                      /* -20000 .. 20000 */
    wave_gain(&w, 0, 201, 500);
    ok = 1;
    for (i = 0; ok && i < 201; i++)
        ok = w.pcm[i] == (short)((-20000 + (long)i * 200) / 2);
    CHECK(ok, "gain to half");
    wave_gain(&w, 0, 201, 4000);
    CHECK(w.pcm[0] == -32768 && w.pcm[200] == 32767 && w.pcm[100] == 0 && w.pcm[120] == 8000, "gain of four, clipped: %d %d %d",
          w.pcm[0], w.pcm[200], w.pcm[120]);
    CHECK(wave_peak(&w, 0, 201) == 32768 && wave_peak(&w, 100, 121) == 8000, "peak: %u", wave_peak(&w, 0, 201));
    wave_range(&w, 100, 121, 0, &lo, &hi);
    CHECK(lo == 0 && hi == 8000, "range: %d %d", lo, hi);
    wave_silence(&w, 10, 20);
    CHECK(w.pcm[9] != 0 && w.pcm[10] == 0 && w.pcm[19] == 0 && w.pcm[20] != 0, "silence");
    wave_free(&w);
    ramp(&w, 2, 7, 1, 1);
    wave_reverse(&w, 1, 6);
    CHECK(w.pcm[0] == 1 && w.pcm[2] == 6 && w.pcm[4] == 5 && w.pcm[6] == 4 && w.pcm[10] == 2 && w.pcm[12] == 7 && w.pcm[3] == 6,
          "reverse: %d %d %d", w.pcm[2], w.pcm[6], w.pcm[10]);
    wave_free(&w);

    /* fades: a steady 10000 becomes a straight line */
    ramp(&w, 2, 1000, 10000, 0);
    wave_fade(&w, 0, 500, 1);
    wave_fade(&w, 500, 1000, 0);
    ok = 1;
    for (i = 0; ok && i < 500; i++)
        ok = w.pcm[i * 2] == (short)(10000L * (long)i / 500) && w.pcm[(500 + i) * 2 + 1] == (short)(10000L * (long)(499 - i) / 500);
    CHECK(ok && w.pcm[0] == 0 && w.pcm[999 * 2] == 0 && w.pcm[499 * 2] == 9980, "fade in and out: %d %d", w.pcm[499 * 2], w.pcm[500 * 2]);
    wave_free(&w);

    /* normalise: the loudest sample goes to the top, the rest in proportion; silence stays */
    ramp(&w, 1, 101, -5000, 100);
    r = wave_normalise(&w, 0, 101);
    CHECK(r == 6553 && w.pcm[0] == -32767 && w.pcm[100] == 32767 && w.pcm[50] == 0 && w.pcm[75] == (short)(2500L * 32767 / 5000),
          "normalise: gain %d, %d %d %d", r, w.pcm[0], w.pcm[100], w.pcm[75]);
    CHECK(wave_normalise(&w, 0, 101) == 1000, "normalised already");
    wave_silence(&w, 0, 101);
    CHECK(wave_normalise(&w, 0, 101) == 1000 && w.pcm[3] == 0, "silence is not made loud");
    wave_free(&w);

    /* one channel from two (their mean) and two from one */
    wave_init(&w, 8000, 2);
    for (i = 0; i < 100; i++) {
        short f[2];
        f[0] = (short)(i * 10), f[1] = (short)(1000 - (long)i * 30);
        wave_append(&w, f, 1);
    }
    r = wave_set_channels(&w, 1);
    ok = r == 0 && w.channels == 1 && w.frames == 100;
    for (i = 0; ok && i < 100; i++)
        ok = w.pcm[i] == (short)(((long)i * 10 + 1000 - (long)i * 30) / 2);
    CHECK(ok, "two channels to one");
    r = wave_set_channels(&w, 2);
    ok = r == 0 && w.channels == 2 && w.frames == 100;
    for (i = 0; ok && i < 100; i++)
        ok = w.pcm[i * 2] == w.pcm[i * 2 + 1] && w.pcm[i * 2] == (short)(((long)i * 10 + 1000 - (long)i * 30) / 2);
    CHECK(ok, "one channel to two");
    {
        static short more[100];                         /* (and it can still grow) */
        wave_append(&w, more, 50);
    }
    CHECK(w.frames == 150 && w.pcm[149 * 2 + 1] == 0 && wave_set_channels(&w, 3) == WAVE_E_ARGUMENT, "three channels refused");
    wave_free(&w);

    /* another rate: a straight line stays a straight line, twice as many of them or half */
    ramp(&w, 2, 1000, 0, 20);
    r = wave_resample(&w, 16000);
    ok = r == 0 && w.rate == 16000 && w.frames == 2000;
    for (i = 0; ok && i < 1999; i++)
        ok = w.pcm[i * 2] == (short)(i * 10) && w.pcm[i * 2 + 1] == (short)(i * 10);
    CHECK(ok, "resampled up: %lu frames at %u", w.frames, w.rate);
    r = wave_resample(&w, 4000);
    ok = r == 0 && w.rate == 4000 && w.frames == 500;
    for (i = 0; ok && i < 499; i++)                     /* (the mean of four: 0 10 20 30 is 15) */
        ok = w.pcm[i * 2] == (short)(i * 40 + 15);
    CHECK(ok, "resampled down: %lu frames, %d %d", w.frames, w.pcm[0], w.pcm[2]);
    CHECK(wave_resample(&w, 4000) == 0 && w.frames == 500 && wave_resample(&w, 12) == WAVE_E_ARGUMENT, "the same rate; no rate");
    wave_free(&w);
    make(&w, 44100, 2, 44100);                          /* a second of the tests' sound to 8000: its length and loudness */
    r = wave_resample(&w, 8000);
    CHECK(r == 0 && w.frames == 8000 && wave_peak(&w, 0, 8000) > 20000 && wave_peak(&w, 0, 8000) <= 26000, "44100 to 8000: %lu, %u",
          w.frames, wave_peak(&w, 0, 8000));
    wave_free(&w);

    /* an empty take takes every edit */
    wave_init(&w, 8000, 1);
    wave_gain(&w, 0, 10, 500);
    wave_fade(&w, 0, 10, 1);
    wave_reverse(&w, 0, 10);
    wave_cut(&w, 0, 10, NULL);
    wave_trim(&w, 0, 10);
    CHECK(wave_normalise(&w, 0, 10) == 1000 && wave_peak(&w, 0, 10) == 0 && wave_resample(&w, 16000) == 0 && w.frames == 0
          && wave_set_channels(&w, 2) == 0, "an empty take");
    wave_free(&w);
    CHECK(wave_init(&w, 8000, 3) == WAVE_E_ARGUMENT && wave_init(&w, 5, 1) == WAVE_E_ARGUMENT, "no such takes");
}

static void dump(const char *dir, const char *name, const struct wave *w)       /* the take's samples, as they are */
{
    char path[600];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "wb");
    CHECK(f && fwrite(w->pcm, sizeof(short) * w->channels, w->frames, f) == w->frames, "%s written", name);
    if (f)
        fclose(f);
}

int main(int argc, char **argv)
{
    static char line[400], path[600], name[200];
    const char *dir = argc > 1 ? argv[1] : ".";
    struct wave w, back;
    struct wave_out *o;
    FILE *m;
    unsigned files = 0;
    unsigned long i;
    int r, ok, err;

    edits();

    /* The files of the list. */
    snprintf(path, sizeof path, "%s/MANIFEST", dir);
    m = fopen(path, "r");
    CHECK(m != NULL, "no %s", path);
    while (m && fgets(line, sizeof line, m)) {
        unsigned rate = 0, ch = 0;
        unsigned long frames = 0;
        int n = sscanf(line, "%199s\t%u\t%u\t%lu", name, &rate, &ch, &frames);
        files++;
        snprintf(path, sizeof path, "%s/%s", dir, name);
        r = wave_load(path, &w);
        if (n == 2) {                                   /* (the second number is the error it must give) */
            int want = (int)strtol(strchr(line, '\t') + 1, NULL, 10);
            CHECK(r == want, "%s: %d, not %d", name, r, want);
            CHECK(r != 0 && w.pcm == NULL, "%s: refused, and nothing left of it", name);
            continue;
        }
        CHECK(r == 0 && w.rate == rate && w.channels == ch && w.frames == frames, "%s: %d, %u Hz, %u channels, %lu frames (%lu wanted)",
              name, r, w.rate, w.channels, w.frames, frames);
        if (r == 0 && w.frames == frames && w.channels == ch) {
            short *want = malloc((frames * ch + 1) * sizeof *want);
            FILE *f;
            size_t got;
            unsigned long bad = 0, first = 0;
            snprintf(path, sizeof path, "%s/%.*s.pcm", dir, (int)strlen(name) - 4, name);
            f = fopen(path, "rb");
            got = f ? fread(want, sizeof *want, frames * ch, f) : 0;
            if (f)
                fclose(f);
            for (i = 0; i < frames * ch && i < got; i++)
                if (w.pcm[i] != want[i] && !bad++)
                    first = i;
            CHECK(got == frames * ch && !bad, "%s: %lu samples are others (the first at %lu: %d, not %d)", name, bad, first,
                  bad ? w.pcm[first] : 0, bad ? want[first] : 0);
            free(want);
        }
        if (r == 0)
            wave_free(&w);
    }
    if (m)
        fclose(m);
    CHECK(files >= 20, "%u files in the list", files);
    snprintf(path, sizeof path, "%s/no-such-file.wav", dir);
    CHECK(wave_load(path, &w) == WAVE_E_OPEN, "a file that is not there");

    /* Files written: read back here to the sample, and by wave_cases.py check after. */
    make(&w, 22050, 2, 5000);
    snprintf(path, sizeof path, "%s/out-pcm16.wav", dir);
    r = wave_save(path, &w, WAVE_PCM16);
    ok = r == 0 && wave_load(path, &back) == 0 && back.frames == 5000 && back.rate == 22050 && back.channels == 2
         && !memcmp(back.pcm, w.pcm, 5000 * 2 * sizeof(short));
    CHECK(ok, "PCM of 16 bits written and read: %d", r);
    if (ok)
        wave_free(&back);
    wave_free(&w);
    make(&w, 11025, 1, 3001);
    snprintf(path, sizeof path, "%s/out-pcm8.wav", dir);
    r = wave_save(path, &w, WAVE_PCM8);
    ok = r == 0 && wave_load(path, &back) == 0 && back.frames == 3001 && back.channels == 1;
    for (i = 0; ok && i < 3001; i++)
        ok = back.pcm[i] == (short)((w.pcm[i] >> 8) * 256);
    CHECK(ok, "PCM of 8 bits written and read: %d", r);
    if (r == 0 && back.pcm)
        wave_free(&back);
    CHECK(wave_save(path, &w, 99) == WAVE_E_ARGUMENT, "no such way to write");
    wave_free(&w);
    {
        static const struct { const char *name; unsigned ch, rate; unsigned long n; } a[] = {
            { "out-adpcm1", 1, 8000, 5000 }, { "out-adpcm2", 2, 44100, 6000 }, { "out-adpcm3", 2, 22050, 4041 } };
        unsigned k;
        for (k = 0; k < 3; k++) {
            unsigned long worst = 0;
            make(&w, a[k].rate, a[k].ch, a[k].n);
            snprintf(path, sizeof path, "%s/%s.wav", dir, a[k].name);
            r = wave_save(path, &w, WAVE_ADPCM);
            ok = r == 0 && wave_load(path, &back) == 0 && back.frames == a[k].n && back.rate == a[k].rate && back.channels == a[k].ch;
            for (i = 0; ok && i < a[k].n * a[k].ch; i++) {
                unsigned long d = (unsigned long)labs((long)back.pcm[i] - w.pcm[i]);
                worst = d > worst ? d : worst;
            }
            CHECK(ok && worst < 1500, "%s written and read: %d, %lu off at worst", a[k].name, r, worst);
            if (ok) {
                snprintf(name, sizeof name, "%s.pcm", a[k].name);
                dump(dir, name, &back);
                wave_free(&back);
            }
            wave_free(&w);
        }
    }
    /* ... and one written as its samples come, in pieces of every size */
    make(&w, 44100, 2, 7000);
    snprintf(path, sizeof path, "%s/out-stream.wav", dir);
    o = wave_out_begin(path, 44100, 2, &err);
    CHECK(o != NULL && err == 0, "a file begun: %d", err);
    for (i = 0; o && i < 7000; ) {
        unsigned long n = 1 + i % 613;
        if (n > 7000 - i)
            n = 7000 - i;
        CHECK(wave_out_write(o, w.pcm + i * 2, n) == 0, "written at %lu", i);
        i += n;
    }
    r = o ? wave_out_end(o) : -1;
    ok = r == 0 && wave_load(path, &back) == 0 && back.frames == 7000 && !memcmp(back.pcm, w.pcm, 7000 * 2 * sizeof(short));
    CHECK(ok, "a file written as its samples came: %d", r);
    if (ok)
        wave_free(&back);
    wave_free(&w);
    snprintf(path, sizeof path, "%s/no-such-folder/x.wav", dir);
    CHECK(wave_out_begin(path, 44100, 2, &err) == NULL && err == WAVE_E_OPEN, "a file that cannot be begun: %d", err);
    CHECK(wave_out_begin(path, 44100, 5, &err) == NULL && err == WAVE_E_ARGUMENT, "five channels: %d", err);

    printf("wave_test: %d failures (%u files, %d checks)\n", fails, files, checks);
    return fails != 0;
}
