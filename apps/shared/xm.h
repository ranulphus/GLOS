/* FastTracker 2 modules (XM) played into samples, for the Player (phase F; docs/v1/apps/audio-player.md): as
   mod.h is for MOD files, and with the same calls. The sound is 16 bits, two channels, at the rate asked for. */
#ifndef XM_H
#define XM_H

enum {
    XM_OK = 0,
    XM_E_KIND = -1,                     /* no XM module (or one older than FastTracker 2.0's last form) */
    XM_E_DAMAGED = -2,                  /* cut short, or its numbers make no sense */
    XM_E_MEMORY = -3
};

struct xm;

/* The module in `data` (size bytes: the whole file), to be played at `rate` frames a second. Its samples are
   unpacked where they lie: the bytes are changed, and are the player's to read until xm_free (the caller frees
   them after). NULL and *err: the bytes are as they were. */
struct xm *xm_open(unsigned char *data, unsigned long size, unsigned rate, int *err);
void xm_free(struct xm *m);
const char *xm_title(const struct xm *m);
unsigned xm_channels(const struct xm *m);
void xm_smooth(struct xm *m, int on);

/* The next frames of it, as many as asked for unless the song ends first: how many were made. A song that would
   go round for ever ends where it comes to a row it has played before. */
unsigned long xm_render(struct xm *m, short *pcm, unsigned long frames);
unsigned long xm_length(struct xm *m);
void xm_seek(struct xm *m, unsigned long frame);
unsigned long xm_tell(const struct xm *m);

#endif
