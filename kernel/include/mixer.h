/* GLOS's mixer and the cards under it (kernel/drv/mixer.c; sb16.c, the Sound
 * Blaster 16; crystal.c, a Crystal CS423x's codec; PRD §10.5;
 * docs/milestones-m8.md step E, docs/crystal-audio.md). */
#ifndef K_MIXER_H
#define K_MIXER_H
#include "types.h"

struct app;
struct bootinfo;

void mixer_counts(u32 *rate, u32 *streams, u32 *underruns);     /* GLOS_FN_STAT's (M9) */
void mixer_enable(int on);              /* [sound] mixer (M9) */
void mixer_set_rate(u32 rate);          /* [sound] rate (M9) */
int mixer_init(const struct bootinfo *bi);      /* 0 when there is an SB16 to mix into (and [sound] mixer isn't off) */
int mixer_on(void);                             /* it plays now (no session has the card) */
int mixer_found(void);                          /* it has a card (a session may have it now) */
void mixer_desktop(int on);                     /* the desktop has the screen (1: the mixer plays), or a session
                                                   or nobody has (0: the card stopped, its line the system VM's) */
int mixer_open(struct app *a, u32 rate, u32 channels, u32 bytes, u32 *id, u32 *lin);   /* GLOS_FN_AUDIO: 0, GLOS_E_ */
int mixer_close(struct app *a, u32 id);
int mixer_volume(struct app *a, u32 id, u32 volume);
void mixer_app_gone(struct app *a);
u32 mixer_stat(char *buf, u32 max);             /* glos audio */

struct trapframe;
typedef void (*mixer_fill)(s16 *out, u32 frames, const struct trapframe *tf);  /* (tf: what the card's interrupt
                                                                                     broke into) */
struct sound_card {                             /* what the mixer plays through */
    const char *name;
    int (*start)(u32 rate, u32 frames, mixer_fill fill);        /* two halves of `frames`: 0 */
    void (*stop)(void);
    int (*running)(void);
    u32 (*irqs)(void);
    u32 (*errors)(void);                        /* periods the card reported an underrun in (0: it can't tell) */
};
extern const struct sound_card sb16_card, crystal_card;
void mixer_late(void);                          /* GLOS.CFG read: a Crystal chip set up when there's no SB16 */
int crystal_init(const char *cfg);              /* [sound] crystal (auto, off, or resources): the part set up: 0 */
u16 crystal_sb(void);                           /* its SB Pro's port, 0 if none set up */
const char *crystal_blaster(void);              /* BLASTER for programs (A220 I5 D1 T4), 0 if none set up */
int sb16_init(const struct bootinfo *bi);       /* the DSP found, version 4 or later: 0 */

#endif
