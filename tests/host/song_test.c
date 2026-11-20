/* Host test for MIDI Studio's song (apps/shared/song.c): songs made and changed by its calls; written as MIDI
 * files and read back the same, and their messages at the right times by the reader the Player uses (smf.c);
 * tests/host/smf_cases.py's files read as songs; what is heard with tracks muted and solo; and files with bytes
 * changed at random: read (or refused), written and read again, the song must be the same.
 *
 *     song_test DIR        (the .mid files smf_cases.py wrote) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../apps/shared/smf.h"
#include "../../apps/shared/song.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); \
                                                printf("\n"); fails++; } } while (0)

static const char *dir;

static unsigned char *slurp(const char *name, unsigned long *size)
{
    char path[600];
    unsigned char *b;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    *size = 0;
    if (!(f = fopen(path, "rb")))
        return NULL;
    b = malloc(1 << 16);
    *size = (unsigned long)fread(b, 1, 1 << 16, f);
    fclose(f);
    return b;
}

static const char *differ(const struct song *a, const struct song *b)   /* NULL: the same song */
{
    static char why[120];
    unsigned k;
    unsigned long i;
    if (strcmp(a->title, b->title) || a->beats != b->beats || a->unit != b->unit)
        return "the title or the time signature";
    if (a->ntempo != b->ntempo || memcmp(a->tempo, b->tempo, a->ntempo * sizeof a->tempo[0]))
        return "the tempo";
    if (a->ntracks != b->ntracks)
        return "how many tracks";
    for (k = 0; k < a->ntracks; k++) {
        const struct song_track *x = &a->track[k], *y = &b->track[k];
        snprintf(why, sizeof why, "track %u", k);
        if (strcmp(x->name, y->name) || x->channel != y->channel || x->program != y->program || x->volume != y->volume
            || x->pan != y->pan || x->nnotes != y->nnotes || x->nevents != y->nevents)
            return why;
        for (i = 0; i < x->nnotes; i++)
            if (x->notes[i].at != y->notes[i].at || x->notes[i].len != y->notes[i].len || x->notes[i].key != y->notes[i].key
                || x->notes[i].force != y->notes[i].force) {
                snprintf(why, sizeof why, "track %u, note %lu", k, i);
                return why;
            }
        for (i = 0; i < x->nevents; i++)
            if (x->events[i].at != y->events[i].at || x->events[i].status != y->events[i].status || x->events[i].d1 != y->events[i].d1
                || x->events[i].d2 != y->events[i].d2) {
                snprintf(why, sizeof why, "track %u, event %lu", k, i);
                return why;
            }
    }
    return NULL;
}

static int note_is(const struct song_track *t, unsigned long i, unsigned long at, unsigned long len, unsigned key, unsigned force)
{
    return i < t->nnotes && t->notes[i].at == at && t->notes[i].len == len && t->notes[i].key == key && t->notes[i].force == force;
}

int main(int argc, char **argv)
{
    struct song *s, *r;
    struct song_track *t;
    struct smf *m;
    unsigned char *bytes, *file;
    unsigned long size, fsize;
    const char *why;
    int err, k;
    dir = argc > 1 ? argv[1] : ".";

    /* a new song; notes put in out of order are kept in order; one taken out */
    s = song_new();
    CHECK(s && s->ntracks == 1 && s->track[0].channel == 0 && s->track[0].volume == 100 && s->track[0].pan == 64 && s->ntempo == 1
          && song_tempo_at(s, 0) == 120 && s->beats == 4 && s->unit == 4 && song_ticks(s) == 0, "a new song");
    t = &s->track[0];
    CHECK(song_note_add(t, 960, 240, 64, 90) == 0 && song_note_add(t, 0, 480, 60, 100) == 0 && song_note_add(t, 480, 480, 62, 0) == 1
          && song_note_add(t, 480, 120, 72, 200) == 2 && t->nnotes == 4, "notes added: %lu", t->nnotes);
    CHECK(note_is(t, 0, 0, 480, 60, 100) && note_is(t, 1, 480, 480, 62, 1) && note_is(t, 2, 480, 120, 72, 127)
          && note_is(t, 3, 960, 240, 64, 90) && song_ticks(s) == 1200, "notes in order; %lu ticks", song_ticks(s));
    song_note_remove(t, 2);
    CHECK(t->nnotes == 3 && note_is(t, 2, 960, 240, 64, 90), "a note taken out");
    t->notes[0].at = 2000;                      /* (moved in place: put in order again) */
    song_sort(t);
    CHECK(note_is(t, 0, 480, 480, 62, 1) && note_is(t, 2, 2000, 480, 60, 100), "notes put in order again");
    t->notes[2].at = 0;
    song_sort(t);

    /* the tempo: 120 to tick 960 (1000 ms), then 60 (a tick 2.083 ms) */
    CHECK(song_tempo_set(s, 960, 60) == SONG_OK && s->ntempo == 2 && song_tempo_at(s, 959) == 120 && song_tempo_at(s, 960) == 60
          && song_tempo_at(s, 5000) == 60, "a tempo change");
    CHECK(song_ms(s, 480) == 500 && song_ms(s, 960) == 1000 && song_ms(s, 1440) == 2000 && song_tick(s, 500) == 480
          && song_tick(s, 1000) == 960 && song_tick(s, 2000) == 1440, "ticks and milliseconds: %lu %lu %lu; %lu %lu %lu",
          song_ms(s, 480), song_ms(s, 960), song_ms(s, 1440), song_tick(s, 500), song_tick(s, 1000), song_tick(s, 2000));
    CHECK(song_tempo_set(s, 960, 90) == SONG_OK && s->ntempo == 2 && song_tempo_at(s, 960) == 90, "the change changed");
    song_tempo_set(s, 960, 60);

    /* more tracks: a flute to the left with a bend, and drums; channels found for them; written and read back */
    strcpy(s->title, "Built Here");
    s->beats = 3, s->unit = 8;
    strcpy(s->track[0].name, "Lead");
    k = song_track_add(s, -1);
    CHECK(k == 1 && s->track[1].channel == 1, "a second track: %d on channel %u", k, s->track[1].channel);
    t = &s->track[1];
    strcpy(t->name, "Flute"), t->program = 73, t->volume = 90, t->pan = 20;
    song_note_add(t, 240, 720, 76, 80);
    t->events = malloc(2 * sizeof t->events[0]), t->event_room = 2, t->nevents = 2;
    t->events[0].at = 300, t->events[0].status = 0xE0, t->events[0].d1 = 0, t->events[0].d2 = 80, t->events[0].pad = 0;
    t->events[1].at = 900, t->events[1].status = 0xB0, t->events[1].d1 = 64, t->events[1].d2 = 127, t->events[1].pad = 0;
    k = song_track_add(s, SONG_DRUMS);
    CHECK(k == 2 && s->track[2].channel == 9, "a drums track");
    song_note_add(&s->track[2], 0, 120, 36, 110);
    song_note_add(&s->track[2], 480, 120, 38, 110);
    CHECK(song_write(s, 0, &bytes, &size) == SONG_OK && size > 100, "written");
    m = smf_open(bytes, size, &err);
    CHECK(m && smf_tracks(m) == 4 && smf_division(m) == SONG_PPQ && !strcmp(smf_title(m), "Built Here")
          && !strcmp(smf_track_name(m, 2), "Flute") && smf_channels(m) == 3, "the file it is: %d", err);
    if (m) {
        const struct smf_event *e = smf_events(m);
        unsigned long i, bad = 0, flute_on = 0, flute_off = 0, last = 0;
        unsigned beats, unit;
        for (i = 0; i < smf_count(m); i++) {
            if (e[i].status == 0x91 && e[i].d1 == 76)
                flute_on = e[i].ms;
            if (e[i].status == 0x81 && e[i].d1 == 76)
                flute_off = e[i].ms;
            if (e[i].status == 0x90 && e[i].d1 == 60 && e[i].ms != 0)
                bad++;
            if (e[i].status == 0x99 && e[i].d1 == 38 && e[i].ms != 500)
                bad++;
            if (e[i].status == 0x80 && e[i].d1 == 64)   /* (the lead's last note: 960 to 1200, in the slow part) */
                last = e[i].ms;
        }
        smf_meter(m, &beats, &unit);
        CHECK(!bad && flute_on == 250 && flute_off == 1000 && last == 1500 && beats == 3 && unit == 8,
              "its messages' times: the flute %lu to %lu ms, the last note's end %lu; %u/%u", flute_on, flute_off, last, beats, unit);
        smf_free(m);
    }
    r = song_read(bytes, size, &err);
    why = r ? differ(s, r) : "not read";
    CHECK(r && !why, "read back: %s (%d)", why ? why : "", err);
    song_free(r);
    free(bytes);

    /* what is heard: a muted track is left out; with a track solo, it alone */
    s->track[1].mute = 1;
    CHECK(song_write(s, 1, &bytes, &size) == SONG_OK && (m = smf_open(bytes, size, &err)) != NULL && smf_tracks(m) == 3
          && smf_channels(m) == 2, "a track muted");
    smf_free(m);
    free(bytes);
    s->track[1].mute = 0, s->track[2].solo = 1;
    CHECK(song_write(s, 1, &bytes, &size) == SONG_OK && (m = smf_open(bytes, size, &err)) != NULL && smf_tracks(m) == 2
          && smf_channels(m) == 1 && smf_events(m)[smf_count(m) - 1].status == 0x89, "a track solo");
    smf_free(m);
    free(bytes);
    CHECK(song_write(s, 0, &bytes, &size) == SONG_OK && (m = smf_open(bytes, size, &err)) != NULL && smf_tracks(m) == 4,
          "all of it written whatever is muted");
    smf_free(m);
    free(bytes);
    song_track_remove(s, 0);
    CHECK(s->ntracks == 2 && !strcmp(s->track[0].name, "Flute") && s->track[1].channel == 9, "a track taken out");
    for (k = 0; k < 40 && song_track_add(s, -1) >= 0; k++)
        ;
    CHECK(k == 14 && s->ntracks == 16 && song_track_add(s, -1) == SONG_E_ROOM && song_track_add(s, 3) == 16,
          "tracks until the channels are used up: %d more", k);
    song_free(s);

    /* a type 0 file comes apart by channel: 96 ticks to the quarter note are scaled by five */
    file = slurp("one.mid", &fsize);
    s = song_read(file, fsize, &err);
    CHECK(s && s->ntracks == 2 && !strcmp(s->title, "A Tune") && !s->track[0].name[0] && s->track[0].channel == 0
          && s->track[0].program == 73 && s->track[1].channel == 1 && s->track[1].program == 0, "one.mid: %d", err);
    if (s) {
        CHECK(note_is(&s->track[0], 0, 0, 480, 66, 100) && note_is(&s->track[1], 0, 960, 240, 60, 90) && s->track[0].nnotes == 1
              && s->track[1].nnotes == 1 && !s->track[0].nevents, "one.mid: its notes");
        CHECK(s->ntempo == 2 && s->tempo[1].at == 480 && s->tempo[1].us == 1000000 && song_ms(s, 960) == 1500
              && song_ms(s, 1200) == 2000, "one.mid: its tempo");
        song_free(s);
    }
    free(file);
    /* a type 1 file: a track for each of its tracks with notes, named; the volume said before the notes is the
       track's, the bend an event */
    file = slurp("three.mid", &fsize);
    s = song_read(file, fsize, &err);
    CHECK(s && s->ntracks == 2 && !strcmp(s->title, "Three") && !strcmp(s->track[0].name, "not the song's") && s->track[0].channel == 1
          && s->track[0].program == 5 && s->track[1].channel == 2 && s->track[1].volume == 100, "three.mid: %d", err);
    if (s) {
        t = &s->track[1];
        CHECK(note_is(&s->track[0], 0, 480, 480, 64, 80) && note_is(&s->track[0], 1, 1440, 240, 65, 80) && note_is(t, 0, 480, 1440, 40, 70)
              && note_is(t, 1, 480, 1440, 44, 70) && t->nevents == 1 && t->events[0].at == 1920 && t->events[0].status == 0xE0
              && t->events[0].d2 == 64, "three.mid: its notes and its bend");
        CHECK(s->ntempo == 2 && s->tempo[0].us == 400000 && s->tempo[1].at == 960 && s->tempo[1].us == 800000, "three.mid: its tempo");
        song_free(s);
    }
    free(file);
    /* film time: a tick a millisecond, read at 120 to the minute (a millisecond is 0.96 of a song's tick) */
    file = slurp("film.mid", &fsize);
    s = song_read(file, fsize, &err);
    CHECK(s && s->ntracks == 1 && note_is(&s->track[0], 0, 480, 1440, 60, 100) && s->ntempo == 1, "film.mid: %d", err);
    song_free(s);
    free(file);
    /* cut in a note's message: the note before it is whole; a file that is none; songs side by side */
    file = slurp("cut.mid", &fsize);
    s = song_read(file, fsize, &err);
    CHECK(s && s->ntracks == 1 && note_is(&s->track[0], 0, 0, 480, 66, 100), "cut.mid: %d", err);
    song_free(s);
    free(file);
    file = slurp("nomidi.mid", &fsize);
    CHECK(!song_read(file, fsize, &err) && err == SONG_E_KIND, "no MIDI file: %d", err);
    free(file);
    file = slurp("two.mid", &fsize);
    CHECK(!song_read(file, fsize, &err) && err == SONG_E_FORMAT, "songs side by side: %d", err);
    free(file);

    /* Files with bytes changed: read or refused; what is read, written and read again, is the same song, and
       written again the same bytes. */
    {
        static const char *const from[] = { "one.mid", "three.mid", "one.rmi", "film.mid" };
        unsigned long seed = 20261120ul, made = 0, opened = 0, round, j, moved = 0;
        file = NULL, fsize = 0;
        for (round = 0; round < 2000; round++) {
            unsigned char *copy, *again;
            unsigned long csize, asize, changes;
            if (round % 500 == 0) {
                free(file);
                file = slurp(from[round / 500], &fsize);
                CHECK(file && fsize > 30, "%s: not read", from[round / 500]);
            }
            if (fsize < 30)
                continue;
            copy = malloc(fsize);
            csize = fsize;
#define RND() (seed = seed * 1103515245ul + 12345ul, (seed >> 8) & 0xFFFFFFul)
            memcpy(copy, file, fsize);
            changes = 1 + RND() % 8;
            for (j = 0; j < changes; j++) {
                unsigned long at = RND() % csize;
                if (at < 8 && RND() % 8)                /* (mostly still a MIDI file) */
                    continue;
                copy[at] = (unsigned char)RND();
            }
            if (RND() % 4 == 0)
                csize = 14 + RND() % (csize - 14);
            made++;
            if ((s = song_read(copy, csize, &err)) != NULL) {
                opened++;
                bytes = again = NULL;
                if (song_write(s, 0, &bytes, &size) != SONG_OK || !(r = song_read(bytes, size, &err))) {
                    moved++;
                } else {
                    if (differ(s, r) || song_write(r, 0, &again, &asize) != SONG_OK || asize != size || memcmp(again, bytes, size)) {
                        if (moved < 3)
                            printf("round %lu: %s\n", round, differ(s, r) ? differ(s, r) : "other bytes");
                        moved++;
                    }
                    song_free(r);
                }
                free(bytes);
                free(again);
                song_free(s);
            }
            free(copy);
        }
        free(file);
        CHECK(made == 2000 && opened > 800 && !moved, "%lu changed files, %lu of them read, %lu not the same after a round trip", made,
              opened, moved);
        printf("song_test: %lu changed files, %lu of them read\n", made, opened);
    }
    printf("song_test: %d failures (%d checks)\n", fails, checks);
    return fails != 0;
}
