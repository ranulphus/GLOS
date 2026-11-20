/* Standard MIDI Files read: a .MID's channel messages, each with its time, in the order they are to be sent, for
   the Player (phase F; docs/v1/apps/audio-player.md). Type 0 and type 1 files (and those in a RIFF "RMID" wrapper);
   the tempo changes are worked into the times. One code for the app and for the host's tests. */
#ifndef SMF_H
#define SMF_H

enum {
    SMF_OK = 0,
    SMF_E_KIND = -1,                    /* no MIDI file */
    SMF_E_DAMAGED = -2,                 /* cut short before anything of it could be read, or its numbers make no sense */
    SMF_E_MEMORY = -3,
    SMF_E_FORMAT = -4                   /* a type 2 file (songs side by side): not played here */
};

struct smf_event {
    unsigned long ms;                   /* when, from the file's start */
    unsigned char status, d1, d2;       /* a channel message (80h to EFh; a note on with no force comes as a note off) */
    unsigned char track;
};

struct smf;

/* The file in `data` (size bytes), which need not be kept after. NULL and *err. A file cut short gives what
   there is of it. */
struct smf *smf_open(const unsigned char *data, unsigned long size, int *err);
void smf_free(struct smf *m);
unsigned long smf_count(const struct smf *m);
const struct smf_event *smf_events(const struct smf *m);        /* smf_count of them, their times never going back */
unsigned long smf_length(const struct smf *m);                  /* milliseconds: to the end of its longest track */
const char *smf_title(const struct smf *m);                     /* its first track's name, or "" */
unsigned smf_tracks(const struct smf *m);
unsigned smf_channels(const struct smf *m);                     /* how many of the 16 have notes */

#endif
