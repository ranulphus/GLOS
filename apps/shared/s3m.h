/* Scream Tracker 3 modules (S3M) played into samples, for the Player (phase F; docs/v1/apps/audio-player.md): as
   mod.h is for MOD files, and with the same calls. The sound is 16 bits, two channels, at the rate asked for. */
#ifndef S3M_H
#define S3M_H

enum {
    S3M_OK = 0,
    S3M_E_KIND = -1,                    /* no S3M module */
    S3M_E_DAMAGED = -2,                 /* cut short, or its numbers make no sense */
    S3M_E_MEMORY = -3
};

struct s3m;

/* The module in `data` (size bytes: the whole file, which the caller keeps as it is until s3m_free), to be played
   at `rate` frames a second. NULL and *err. */
struct s3m *s3m_open(const unsigned char *data, unsigned long size, unsigned rate, int *err);
void s3m_free(struct s3m *m);
const char *s3m_title(const struct s3m *m);
unsigned s3m_channels(const struct s3m *m);             /* those that play samples */
void s3m_smooth(struct s3m *m, int on);

/* The next frames of it, as many as asked for unless the song ends first: how many were made. A song that would
   go round for ever ends where it comes to a row it has played before. */
unsigned long s3m_render(struct s3m *m, short *pcm, unsigned long frames);
unsigned long s3m_length(struct s3m *m);
void s3m_seek(struct s3m *m, unsigned long frame);
unsigned long s3m_tell(const struct s3m *m);

#endif
