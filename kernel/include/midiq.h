/* MIDI messages in time (kernel/drv/midiq.c): a port out's queue, played when each one's time comes, and the
 * parser of a port in's bytes. No hardware: kernel/drv/midi.c has that (tests/host/midiq_test.c). */
#ifndef K_MIDIQ_H
#define K_MIDIQ_H
#include "types.h"

struct midi_msg {                               /* (API 2.0's struct glos_midi_msg) */
    u32 time;                                   /* milliseconds on GLOS's clock */
    u8 status, d1, d2, pad;
};

#define MIDIQ_MAX 512
struct midiq {
    u32 head, n;
    struct midi_msg m[MIDIQ_MAX];               /* a ring from `head`, in the order of their times */
};

u32 midi_len(u8 status);                        /* a channel message's bytes (2 or 3); 0: not one */
int midiq_put(struct midiq *q, const struct midi_msg *m, u32 now);     /* 0; -1 full; -2 not a channel message.
                                                   A time of 0, or one gone by, is `now`'s; among equal times the
                                                   order they were put in is kept */
int midiq_due(struct midiq *q, u32 now, struct midi_msg *out);          /* 1: the first one whose time has come,
                                                                           taken out */
void midiq_clear(struct midiq *q);

struct midi_parse {
    u8 status;                                  /* the running status, 0 none */
    u8 have, d1, sysex;
};
int midi_parse(struct midi_parse *p, u8 byte, struct midi_msg *out);    /* 1: a channel message is complete (its
                                                   time is the caller's to set). System messages are dropped:
                                                   real-time ones without a trace, exclusive ones to their end */

#endif
