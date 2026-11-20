/* Tracker modules played into samples: the MOD format (ProTracker's, and its descendants with more channels), for
   the Player (phase F; docs/v1/apps/audio-player.md). One code for the app and for the host's test
   (tests/host/mod_test.c). The sound is 16 bits, two channels, at the rate asked for. */
#ifndef MOD_H
#define MOD_H

enum {
    MOD_OK = 0,
    MOD_E_KIND = -1,                    /* no module of a kind played here */
    MOD_E_DAMAGED = -2,                 /* cut short, or its numbers make no sense */
    MOD_E_MEMORY = -3
};

struct mod;

/* The module in `data` (size bytes: the whole file, which the caller keeps as it is until mod_free), to be played
   at `rate` frames a second. NULL and *err. */
struct mod *mod_open(const unsigned char *data, unsigned long size, unsigned rate, int *err);
void mod_free(struct mod *m);
const char *mod_title(const struct mod *m);             /* its name, as the file has it (may be empty) */
unsigned mod_channels(const struct mod *m);
const char *mod_sample_name(const struct mod *m, unsigned i);  /* sample i's (1 to 31; their texts are the
                                                           composers' notes); NULL: no such sample */
void mod_smooth(struct mod *m, int on);                 /* 1 (as it begins): each sample on the line between the
                                                           two it falls between; 0: the nearer, for a slow PC */

/* The next frames of it, as many as asked for unless the song ends first: how many were made. A song that would
   go round for ever ends where it comes to a row it has played before. */
unsigned long mod_render(struct mod *m, short *pcm, unsigned long frames);
unsigned long mod_length(struct mod *m);                /* frames from its start to its end (found by going
                                                           through it without making sound: once, then kept) */
void mod_seek(struct mod *m, unsigned long frame);      /* as if that many frames had been made from the start */
unsigned long mod_tell(const struct mod *m);            /* frames made (or sought to) */
void mod_where(const struct mod *m, int *order, int *pattern, int *row);       /* the row being played */

#endif
