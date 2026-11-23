/* An AC'97 codec's mixer and set-up (kernel/drv/ac97.c), behind a controller's own way of reaching it. */
#ifndef K_AC97_H
#define K_AC97_H

#include "types.h"

struct ac97 {
    u16 (*rd)(void *ctx, u8 reg);
    void (*wr)(void *ctx, u8 reg, u16 v);
    void (*pause)(void *ctx, u32 ms);   /* (the controller's wait: it knows whether it may sleep) */
    void *ctx;
    u32 vendor;                         /* -> the codec's vendor ID (7Ch, 7Eh) */
    int vra;                            /* -> it has variable rates */
};

int ac97_start(struct ac97 *c);         /* reset, ready, levels set: 0, or -1 */
int ac97_rate(struct ac97 *c, u32 rate);
/* mixer.h's chan and input, on the codec's registers (no FM channel: -1) */
int ac97_chan(struct ac97 *c, int chan, int set, u8 *left, u8 *right, u8 *mute);
int ac97_input(struct ac97 *c, int set, int *input);

#endif
