/* Host test for the MIDI file reader (apps/shared/smf.c): the small files tests/host/smf_cases.py writes, each
 * held to its messages and their times; files cut short, of a kind not played and that are no MIDI files; a file
 * with bytes changed at random comes to no harm, and its times never go back.
 *
 *     smf_test DIR        (the .mid files smf_cases.py wrote) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../apps/shared/smf.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); \
                                                printf("\n"); fails++; } } while (0)

static const char *dir;
static unsigned char *file;
static unsigned long file_size;

static struct smf *open_smf(const char *name, int *err)
{
    char path[600];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    free(file);
    file = NULL, file_size = 0;
    *err = -99;
    if (!(f = fopen(path, "rb")))
        return NULL;
    fseek(f, 0, SEEK_END);
    file_size = (unsigned long)ftell(f);
    rewind(f);
    file = malloc(file_size + 1);
    if (fread(file, 1, file_size, f) != file_size)
        file_size = 0;
    fclose(f);
    return smf_open(file, file_size, err);
}

struct want {
    unsigned long ms;
    unsigned status, d1, d2, track;
};

static void held(const char *name, const struct want *w, unsigned n, unsigned long length, const char *title, unsigned tracks,
                 unsigned channels)
{
    int err = 0;
    struct smf *m = open_smf(name, &err);
    const struct smf_event *e;
    unsigned i;
    CHECK(m != NULL, "%s: not opened: %d", name, err);
    if (!m)
        return;
    e = smf_events(m);
    CHECK(smf_count(m) == n, "%s: %lu messages, not %u", name, smf_count(m), n);
    for (i = 0; i < n && i < smf_count(m); i++)
        CHECK(e[i].ms == w[i].ms && e[i].status == w[i].status && e[i].d1 == w[i].d1 && e[i].d2 == w[i].d2 && e[i].track == w[i].track,
              "%s: message %u is %lu ms %02X %u %u of track %u", name, i, e[i].ms, e[i].status, e[i].d1, e[i].d2, e[i].track);
    CHECK(smf_length(m) == length && !strcmp(smf_title(m), title) && smf_tracks(m) == tracks && smf_channels(m) == channels,
          "%s: %lu ms, '%s', %u tracks, %u channels with notes", name, smf_length(m), smf_title(m), smf_tracks(m), smf_channels(m));
    smf_free(m);
}

int main(int argc, char **argv)
{
    /* 96 ticks at 500000 us a quarter are 500 ms; then at 1000000: 96 are 1000 ms, 48 are 500 */
    static const struct want one[] = { { 0, 0xC0, 73, 0, 0 }, { 0, 0x90, 66, 100, 0 }, { 500, 0x80, 66, 0, 0 },
                                       { 1500, 0x91, 60, 90, 0 }, { 2000, 0x81, 60, 64, 0 } };
    /* 480 ticks at 400000 us a quarter are 400 ms; from tick 960 (800 ms) at 800000: 480 are 800 ms */
    static const struct want three[] = { { 0, 0xC1, 5, 0, 1 }, { 0, 0xB2, 7, 100, 2 }, { 400, 0x91, 64, 80, 1 },
                                         { 400, 0x92, 40, 70, 2 }, { 400, 0x92, 44, 70, 2 }, { 800, 0x81, 64, 0, 1 },
                                         { 1600, 0x91, 65, 80, 1 }, { 2000, 0x81, 65, 0, 1 }, { 2400, 0xE2, 0, 64, 2 },
                                         { 2400, 0x82, 40, 0, 2 }, { 2400, 0x82, 44, 0, 2 } };
    static const struct want film[] = { { 500, 0x90, 60, 100, 0 }, { 2000, 0x80, 60, 0, 0 } };
    struct smf *m;
    int err;
    dir = argc > 1 ? argv[1] : ".";

    held("one.mid", one, 5, 3000, "A Tune", 1, 2);
    held("one.rmi", one, 5, 3000, "A Tune", 1, 2);
    held("three.mid", three, 11, 2400, "Three", 3, 2);
    held("film.mid", film, 2, 2250, "", 1, 1);
    held("cut.mid", one, 3, 1500, "A Tune", 1, 1);      /* (its second note's message is cut: what came before) */
    m = open_smf("cuthead.mid", &err);
    CHECK(!m && err == SMF_E_KIND, "cut in its header: %d", err);
    m = open_smf("two.mid", &err);
    CHECK(!m && err == SMF_E_FORMAT, "songs side by side: %d", err);
    m = open_smf("nomidi.mid", &err);
    CHECK(!m && err == SMF_E_KIND, "no MIDI file: %d", err);
    m = open_smf("notrack.mid", &err);
    CHECK(!m && err == SMF_E_DAMAGED, "no tracks: %d", err);

    /* A file with bytes changed: whatever comes of it, the reader comes back, reads nothing outside it, and
       the times of what it gives never go back. */
    {
        static const char *const from[] = { "one.mid", "three.mid", "one.rmi", "film.mid" };
        unsigned long seed = 20261120ul, made = 0, opened = 0, round, k, back = 0;
        for (round = 0; round < 2000; round++) {
            unsigned char *copy;
            unsigned long size, changes;
            if (round % 500 == 0) {
                m = open_smf(from[round / 500], &err);
                CHECK(m != NULL, "%s: not opened", from[round / 500]);
                smf_free(m);
            }
            if (file_size < 30)
                continue;
            copy = malloc(file_size);
            size = file_size;
#define RND() (seed = seed * 1103515245ul + 12345ul, (seed >> 8) & 0xFFFFFFul)
            memcpy(copy, file, file_size);
            changes = 1 + RND() % 8;
            for (k = 0; k < changes; k++) {
                unsigned long at = RND() % size;
                if (at < 8 && RND() % 8)                /* (mostly still a MIDI file) */
                    continue;
                copy[at] = (unsigned char)RND();
            }
            if (RND() % 4 == 0)
                size = 14 + RND() % (size - 14);
            made++;
            if ((m = smf_open(copy, size, &err)) != NULL) {
                const struct smf_event *e = smf_events(m);
                unsigned long i;
                opened++;
                for (i = 0; i < smf_count(m); i++)
                    if ((i && e[i].ms < e[i - 1].ms) || e[i].status < 0x80 || e[i].status >= 0xF0 || e[i].d1 > 127 || e[i].d2 > 127
                        || e[i].ms > smf_length(m))
                        back++;
                smf_free(m);
            }
            free(copy);
        }
        CHECK(made == 2000 && opened > 800 && !back, "%lu changed files, %lu of them opened, %lu messages out of order or shape", made,
              opened, back);
        printf("smf_test: %lu changed files, %lu of them opened\n", made, opened);
    }
    printf("smf_test: %d failures (%d checks)\n", fails, checks);
    return fails != 0;
}
