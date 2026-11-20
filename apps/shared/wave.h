/* Sound as samples in memory, and WAV files read and written: for the Sound Recorder's takes and its edits, and
   the Player's WAV files (phase F; docs/v1/apps/sound-recorder.md, audio-player.md). One code for the apps and for
   the host's test (tests/host/wave_test.c).

   A take is 16-bit samples, one or two channels, at a rate; places in it are counted in frames (a sample of each
   channel). Every function that takes `from` and `to` works on the frames from..to-1, both cut to the take's
   length. Those that return int: 0, or a WAVE_E_. */
#ifndef WAVE_H
#define WAVE_H

enum {
    WAVE_OK = 0,
    WAVE_E_OPEN = -1,                   /* the file cannot be read or written (a full disc among the reasons) */
    WAVE_E_KIND = -2,                   /* no WAV file */
    WAVE_E_FORMAT = -3,                 /* a WAV of a kind not read here (a-law, MP3 in a WAV ...) */
    WAVE_E_DAMAGED = -4,
    WAVE_E_MEMORY = -5,
    WAVE_E_ARGUMENT = -6
};

struct wave {
    unsigned rate;                      /* frames a second */
    unsigned channels;                  /* 1 or 2 */
    unsigned long frames, room;         /* those there are; those pcm has room for */
    short *pcm;                         /* frames * channels samples (two channels: left, right, left ...) */
};

int wave_init(struct wave *w, unsigned rate, unsigned channels);       /* an empty take */
void wave_free(struct wave *w);
int wave_copy(struct wave *out, const struct wave *w, unsigned long from, unsigned long to);   /* out: made here */
int wave_append(struct wave *w, const short *pcm, unsigned long frames);

/* ---- edits */
int wave_cut(struct wave *w, unsigned long from, unsigned long to, struct wave *cut);  /* taken out (to `cut`, made
                                           here, if not NULL: the clipboard's) */
int wave_trim(struct wave *w, unsigned long from, unsigned long to);    /* all but that taken away */
int wave_insert(struct wave *w, unsigned long at, const struct wave *piece);    /* (of w's rate and channels:
                                           WAVE_E_ARGUMENT otherwise) */
void wave_gain(struct wave *w, unsigned long from, unsigned long to, int permille);    /* louder or softer: 1000 as
                                           it is, 500 half; what would be over the top stays at the top */
void wave_fade(struct wave *w, unsigned long from, unsigned long to, int in);   /* from nothing up (in), or down to
                                           nothing, in a straight line */
int wave_normalise(struct wave *w, unsigned long from, unsigned long to);       /* as loud as fits: the gain given, in
                                           thousandths (1000: nothing done; silence is left as it is) */
void wave_silence(struct wave *w, unsigned long from, unsigned long to);
void wave_reverse(struct wave *w, unsigned long from, unsigned long to);
int wave_set_channels(struct wave *w, unsigned channels);       /* two from one (the same in both), one from two
                                           (their mean) */
int wave_resample(struct wave *w, unsigned rate);       /* the same sound at another rate (straight lines between
                                           the samples; down: the mean of those that fall together first) */

/* ---- looking */
unsigned wave_peak(const struct wave *w, unsigned long from, unsigned long to);         /* the largest |sample|,
                                           0 to 32768 */
void wave_range(const struct wave *w, unsigned long from, unsigned long to, unsigned channel, short *low,
                short *high);           /* the lowest and highest sample of a channel there (a waveform's column) */

/* ---- WAV files: PCM of 8 and 16 bits and IMA ADPCM (4 bits a sample) read; written as asked */
enum { WAVE_PCM16, WAVE_PCM8, WAVE_ADPCM };
int wave_load(const char *path, struct wave *w);        /* w: made here */
int wave_save(const char *path, const struct wave *w, int format);
const char *wave_error(int err);        /* a few words for the user */

/* How files are opened (as fopen does) and taken away: the C library's own way unless an app says otherwise
   (the toolkit's gui_fopen and gui_unlink, for the host). */
void wave_files(void *(*open)(const char *path, const char *mode), int (*unlink)(const char *path));

/* A WAV file read piece by piece (the Player's: a file longer than memory). rate, channels, frames: what it is
   (any may be NULL); detail: a few words of how it is kept ("16 bits", "ADPCM"; may be NULL). NULL and *err. */
struct wave_in;
struct wave_in *wave_in_open(const char *path, unsigned *rate, unsigned *channels, unsigned long *frames, char *detail,
                             unsigned room, int *err);
unsigned long wave_in_read(struct wave_in *in, short *pcm, unsigned long frames);       /* those read: fewer at its end */
int wave_in_seek(struct wave_in *in, unsigned long frame);     /* the next read begins there */
void wave_in_close(struct wave_in *in);

/* A WAV file written as its samples come (a recording that is longer than memory): begun, given samples, ended
   (its sizes are written then). PCM of 16 bits. */
struct wave_out;
struct wave_out *wave_out_begin(const char *path, unsigned rate, unsigned channels, int *err);
int wave_out_write(struct wave_out *o, const short *pcm, unsigned long frames);
int wave_out_end(struct wave_out *o);   /* WAVE_OK: the file is whole */

#endif
