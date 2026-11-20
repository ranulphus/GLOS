/* Packed sound (apps/shared/packed.c, through source.c): the MP3 and Ogg Vorbis files of tests/host/sndfix, whose
 * tones and lengths are known (make.sh there): what each says of itself, its sound from end to end (the pitch of
 * each channel by its zero crossings, how loud, how long), a seek to its middle and back to its start, and
 * damaged copies of each, which must do no harm.
 *
 *   packed_test FIXTURES WORKDIR      (prints "packed_test: N failures (M checks)") */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../apps/shared/source.h"

static int fails, checks;

#define CHECK(c, ...) do { checks++; if (!(c)) { printf("packed_test: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static short pcm[4 * 48000 * 2];

/* A channel's pitch in the frames from..to, by the times it goes from below nothing to above: hertz, and its
   loudest sample. */
static void pitch(const short *p, unsigned channels, unsigned ch, unsigned long from, unsigned long to, unsigned rate,
                  unsigned *hz, int *peak)
{
    unsigned long i, ups = 0, first = 0, last = 0;
    int pk = 0;
    for (i = from + 1; i < to; i++) {
        int a = p[(i - 1) * channels + ch], b = p[i * channels + ch];
        if (a < 0 && b >= 0) {
            if (!ups)
                first = i;
            last = i;
            ups++;
        }
        if (b > pk)
            pk = b;
        if (-b > pk)
            pk = -b;
    }
    *hz = ups > 1 ? (unsigned)(((unsigned long long)(ups - 1) * rate + (last - first) / 2) / (last - first)) : 0;
    *peak = pk;
}

static void play(const char *dir, const char *name, const char *kind, unsigned rate, unsigned channels, unsigned long frames,
                 unsigned long slack, const char *title, unsigned hz0, unsigned hz1)
{
    char path[400];
    struct source *s;
    const struct source_info *in;
    unsigned long n, total = 0, mid;
    unsigned hz;
    int err = 0, peak;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    s = source_open(path, 44100, &err);
    CHECK(s != NULL, "%s: not opened: %s", name, source_error(err));
    if (!s)
        return;
    in = source_info(s);
    CHECK(!strcmp(in->kind, kind) && in->rate == rate && in->channels == channels && !strcmp(in->title, title),
          "%s: kind %s rate %u channels %u title \"%s\" detail \"%s\"", name, in->kind, in->rate, in->channels, in->title,
          in->detail);
    CHECK(in->frames + slack >= frames && in->frames <= frames + slack, "%s: said to be %lu frames, not about %lu", name,
          in->frames, frames);
    while ((n = source_read(s, pcm + total * channels, 1000)) > 0 && total + 1000 < sizeof pcm / sizeof pcm[0] / channels)
        total += n;                             /* (in pieces, as a player takes it) */
    CHECK(total + slack >= frames && total <= frames + slack && source_tell(s) == total, "%s: %lu frames came, not about %lu",
          name, total, frames);
    pitch(pcm, channels, 0, total / 8, total - total / 8, rate, &hz, &peak);
    CHECK(hz + 4 >= hz0 && hz <= hz0 + 4 && peak > 2000, "%s: its first channel is %u Hz at %d, not %u", name, hz, peak, hz0);
    if (channels == 2) {
        pitch(pcm, channels, 1, total / 8, total - total / 8, rate, &hz, &peak);
        CHECK(hz + 8 >= hz1 && hz <= hz1 + 8 && peak > 2000, "%s: its second channel is %u Hz at %d, not %u", name, hz, peak, hz1);
    }
    /* to its middle: the sound goes on from there, as long as is left */
    mid = in->frames / 2;
    CHECK(source_seek(s, mid) == 0 && source_tell(s) == mid, "%s: no seek to its middle", name);
    for (total = 0; (n = source_read(s, pcm + total * channels, 777)) > 0; total += n)
        ;
    CHECK(total + slack + 2400 >= in->frames - mid && total <= in->frames - mid + slack + 2400, "%s: %lu frames from its middle, not about %lu",
          name, total, in->frames - mid);
    pitch(pcm, channels, 0, total / 4, total - total / 8, rate, &hz, &peak);
    CHECK(hz + 4 >= hz0 && hz <= hz0 + 4 && peak > 2000, "%s: from its middle %u Hz at %d, not %u", name, hz, peak, hz0);
    /* back to its start: all of it again */
    CHECK(source_seek(s, 0) == 0 && source_tell(s) == 0, "%s: no seek to its start", name);
    for (total = 0; (n = source_read(s, pcm + total * channels, 4096)) > 0; total += n)
        ;
    CHECK(total + slack >= frames && total <= frames + slack, "%s: %lu frames from its start again, not about %lu", name, total,
          frames);
    source_close(s);
}

/* Copies of a file with bytes changed, cut short, or with a piece of rubbish in them: opened or refused, played
   to their end if opened, and nothing breaks. */
static void damage(const char *dir, const char *name, const char *work)
{
    static unsigned char file[20000], copy[20000];
    char path[400], out[400];
    unsigned seed = 2026;
    FILE *f;
    long size;
    int i, opened = 0;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    snprintf(out, sizeof out, "%s/DAMAGED.BIN", work);
    if (!(f = fopen(path, "rb")))
        return;
    size = (long)fread(file, 1, sizeof file, f);
    fclose(f);
    for (i = 0; i < 300; i++) {
        struct source *s;
        long n = size, k, at;
        unsigned long total = 0, got;
        int err = 0, turns = 0;
        memcpy(copy, file, (size_t)size);
        seed = seed * 1103515245u + 12345u;
        switch (i % 3) {
        case 0:                                 /* a few bytes changed */
            for (k = 0; k < 1 + i % 7; k++) {
                seed = seed * 1103515245u + 12345u;
                copy[(seed >> 8) % (unsigned long)size] ^= (unsigned char)(1u << (seed >> 28 & 7));
            }
            break;
        case 1:                                 /* cut short */
            n = (long)((seed >> 8) % (unsigned long)size);
            break;
        default:                                /* a piece of it overwritten */
            at = (long)((seed >> 8) % (unsigned long)size);
            for (k = at; k < size && k < at + 40 + i; k++) {
                seed = seed * 1103515245u + 12345u;
                copy[k] = (unsigned char)(seed >> 16);
            }
        }
        f = fopen(out, "wb");
        fwrite(copy, 1, (size_t)n, f);
        fclose(f);
        s = source_open(out, 44100, &err);
        if (!s)
            continue;
        opened++;
        while ((got = source_read(s, pcm, 2000)) > 0 && ++turns < 2000)
            total += got;
        CHECK(turns < 2000 && total < 20ul * 48000, "%s damaged (%d): %lu frames and no end", name, i, total);
        source_seek(s, source_info(s)->frames / 3);
        source_read(s, pcm, 2000);
        source_close(s);
    }
    CHECK(opened > 20, "%s: only %d of its 300 damaged copies opened", name, opened);
    remove(out);
}

int main(int argc, char **argv)
{
    if (argc < 3)
        return 2;
    /* (An MP3's length: its encoder adds a frame or two of quiet at the ends; one with frames of one size and
       no count in it is reckoned from its size.) */
    play(argv[1], "TONE.MP3", "MP3", 22050, 1, 2 * 22050, 2400, "A Tone", 440, 0);
    play(argv[1], "STEREO.MP3", "MP3", 44100, 2, 2 * 44100, 3500, "", 440, 880);
    play(argv[1], "TONE.OGG", "OGG", 44100, 2, 2 * 44100, 0, "Two Tones", 440, 880);
    play(argv[1], "MONO.OGG", "OGG", 22050, 1, 22050, 0, "", 440, 0);
    /* The two modules beside them (sndfix/modules.py), by source.c's way to their players: 25 rows of six ticks
       of 882 frames. */
    play(argv[1], "PS3M.S3M", "S3M", 44100, 2, 25 * 6 * 882ul, 0, "a tone", 1320, 1320);
    play(argv[1], "PXM.XM", "XM", 44100, 2, 25 * 6 * 882ul, 0, "a tone", 1863, 1863);
    damage(argv[1], "TONE.MP3", argv[2]);
    damage(argv[1], "STEREO.MP3", argv[2]);
    damage(argv[1], "TONE.OGG", argv[2]);
    damage(argv[1], "MONO.OGG", argv[2]);
    printf("packed_test: %d failures (%d checks)\n", fails, checks);
    return fails != 0;
}
