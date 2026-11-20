/* A sound file as samples, had as they are wanted: what the Player plays (phase F; docs/v1/apps/audio-player.md).
   WAV files (wave.c reads them piece by piece), MOD modules (mod.c plays them), and MP3 and Ogg Vorbis files
   (packed.c: unpacked as they are wanted); the kind is told by the file's bytes, not its name. One code for the app and for the host's tests. Samples are 16 bits; a frame is a sample
   of each channel. */
#ifndef SOURCE_H
#define SOURCE_H

enum {
    SOURCE_OK = 0,
    SOURCE_E_OPEN = -1,                 /* the file cannot be read */
    SOURCE_E_KIND = -2,                 /* no sound file of a kind played here */
    SOURCE_E_FORMAT = -3,               /* of a kind played here, packed in a way that is not */
    SOURCE_E_DAMAGED = -4,
    SOURCE_E_MEMORY = -5
};

struct source;

struct source_info {
    unsigned rate, channels;            /* channels: 1 or 2 */
    unsigned long frames;               /* its length (a module's: to where its song ends or goes round) */
    char kind[8];                       /* "WAV", "MOD", "MP3" (or "MP2", "MP1"), "OGG" */
    char title[64];                     /* the song's own name where the file has one, else "" */
    char detail[40];                    /* "16 bits", "ADPCM", "4 channels" ... */
};

/* rate: what a module is to be played at (a WAV has its own). NULL and *err. */
struct source *source_open(const char *path, unsigned rate, int *err);
void source_close(struct source *s);
const struct source_info *source_info(const struct source *s);
unsigned long source_read(struct source *s, short *pcm, unsigned long frames);  /* those made: fewer at the end */
int source_seek(struct source *s, unsigned long frame);         /* 0, or -1 */
unsigned long source_tell(const struct source *s);
const char *source_error(int err);      /* a few words for the user */
/* How files are opened (as fopen does): the C library's way unless an app says otherwise (the toolkit's
   gui_fopen, for the host). */
void source_files(void *(*open)(const char *path, const char *mode));

#endif
