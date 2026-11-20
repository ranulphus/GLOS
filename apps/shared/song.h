/* A song as MIDI Studio holds it (phase F; docs/v1/apps/midi-studio.md): tracks of notes on a grid of ticks, each
   track an instrument on a channel, with the song's tempo and time signature; read from a MIDI file and written
   as one (apps/shared/smf.c reads). One code for the app and for the host's tests. */
#ifndef SONG_H
#define SONG_H

#define SONG_PPQ 480u                   /* a song's ticks to the quarter note (a file's own are scaled to it) */
#define SONG_TRACKS 32
#define SONG_DRUMS 9                    /* the channel General MIDI keeps for percussion */

enum {
    SONG_OK = 0,
    SONG_E_KIND = -1,                   /* no MIDI file */
    SONG_E_DAMAGED = -2,
    SONG_E_MEMORY = -3,
    SONG_E_FORMAT = -4,                 /* a MIDI file of a kind not read here (type 2) */
    SONG_E_ROOM = -5                    /* no more tracks, or no channel free for one */
};

struct song_note {
    unsigned long at, len;              /* ticks: where it starts, how long it is held (1 at least) */
    unsigned char key, force;           /* 0 to 127; 1 to 127 */
    unsigned char sel, pad;             /* (the editor's own: it is selected) */
};

struct song_event {                     /* what else the channel is sent: controllers, bends, pressure */
    unsigned long at;
    unsigned char status, d1, d2, pad;  /* (status: its kind alone, B0h, D0h, E0h ...: the channel is the track's) */
};

struct song_track {
    char name[32];
    unsigned char channel;              /* 0 to 15 */
    unsigned char program;              /* General MIDI's instrument, 0 to 127 */
    unsigned char volume, pan;          /* 0 to 127: controllers 7 and 10 as the track begins */
    unsigned char mute, solo;
    struct song_note *notes;            /* in the order of their starts */
    unsigned long nnotes, note_room;
    struct song_event *events;          /* in the order of their times */
    unsigned long nevents, event_room;
};

struct song_tempo {
    unsigned long at;
    unsigned long us;                   /* a quarter note's microseconds from there on */
};

struct song {
    char title[64];
    unsigned beats, unit;               /* the time signature: 4, 4 */
    struct song_tempo *tempo;           /* the tempo and its changes: tempo[0] is at tick 0, always there */
    unsigned ntempo, tempo_room;
    struct song_track track[SONG_TRACKS];
    unsigned ntracks;
};

struct song *song_new(void);            /* one empty track (a piano), 120 quarter notes a minute, 4/4; or NULL */
void song_free(struct song *s);

/* A MIDI file's bytes as a song: a track for each of the file's tracks' channels (a type 0 file's one track
   comes apart by channel), their notes paired up, the first instrument, volume and place of each taken as the
   track's, the rest kept as events. NULL and *err. */
struct song *song_read(const unsigned char *data, unsigned long size, int *err);
/* The song as a type 1 MIDI file: its bytes (the caller's to free) and *size; SONG_OK or SONG_E_MEMORY.
   heard: only what is to be heard now (no muted track; where any track is solo, those alone): for playing. */
int song_write(const struct song *s, int heard, unsigned char **data, unsigned long *size);

/* A new track, a piano: its index, or SONG_E_ROOM. channel: 0 to 15 (tracks may share one), or -1 for one that
   no track has (never the percussion's). */
int song_track_add(struct song *s, int channel);
void song_track_remove(struct song *s, unsigned t);
/* A note into a track, kept in order: its index, or SONG_E_MEMORY. */
long song_note_add(struct song_track *t, unsigned long at, unsigned long len, unsigned key, unsigned force);
void song_note_remove(struct song_track *t, unsigned long i);
void song_sort(struct song_track *t);                   /* (after notes' starts were changed in place) */
unsigned long song_ticks(const struct song *s);         /* to the end of its last note or event */
unsigned long song_ms(const struct song *s, unsigned long tick);        /* a tick's time, by the tempo's changes */
unsigned long song_tick(const struct song *s, unsigned long ms);
/* The tempo from `at` on, in quarter notes a minute (20 to 400): changed where a change is there already, else
   put in. SONG_OK or SONG_E_MEMORY. */
int song_tempo_set(struct song *s, unsigned long at, unsigned bpm);
unsigned song_tempo_at(const struct song *s, unsigned long at);         /* quarter notes a minute there */

#endif
