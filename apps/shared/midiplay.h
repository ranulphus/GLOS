/* A MIDI file's messages sent to a MIDI port as their times come: what the Player and MIDI Studio share
   (phase F). The port is AUDIO's (API 2.0): the card's FM chip, played by GLOS's own General MIDI driver, or else
   an MPU-401; each message goes with its time on GLOS's clock and GLOS plays it when that comes. The caller sends
   those of the next while by calling mp_feed often (every 15 ms or so, from a thread of its own if the music is
   to go on whatever its window does), and sees to it that no two of these calls run at once. */
#ifndef MIDIPLAY_H
#define MIDIPLAY_H

#include <glos/glos.h>

#include "smf.h"

struct midiplay {
    const struct smf *mid;              /* what it plays: the caller's, kept as long as this is used */
    glos_handle port;                   /* while it plays or is paused */
    unsigned long next;                 /* the next of its messages to send */
    unsigned long at;                   /* where it is while it does not play: milliseconds */
    unsigned long length;               /* when it has played out: milliseconds */
    unsigned base;                      /* while it plays: GLOS's clock at what would have been its start */
    int playing;
    int volume;                         /* 0 to 256: the force of the notes */
};

/* p made ready for a file, at its start, not playing. length: to where it plays (the file's own and whatever
   the last notes are given to die away). */
void mp_init(struct midiplay *p, const struct smf *m, unsigned long length);
/* It plays, from p->at: 0; or -1: there is no MIDI device (no FM chip and no MPU-401, or another program has
   it). The port is first told how every channel stands at that place: its controllers, instrument, bend and
   bend range as the file had said up to there, the rest as General MIDI begins. */
int mp_play(struct midiplay *p);
void mp_pause(struct midiplay *p);      /* what waits dropped, the notes ended; the port kept: mp_play goes on */
void mp_stop(struct midiplay *p);       /* ... the port closed, and at its start again */
void mp_seek(struct midiplay *p, unsigned long ms);     /* (while it plays: it goes on from there) */
/* The messages of the next 400 ms sent. 1 when it has played out: stopped then, at its start again. */
int mp_feed(struct midiplay *p);
unsigned long mp_where(const struct midiplay *p);       /* milliseconds */
/* (2026-11-23) A MIDI port out opened, the one GLOS.CFG's [sound] midi prefers (fm: the card's FM chip; port: its
   MPU-401), else the other: its handle, or 0 (none, or another program has it). The player's, and an app's own
   notes (MIDI Studio's played keys). */
glos_handle mp_open_out(void);

#endif
