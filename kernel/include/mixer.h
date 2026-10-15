/* GLOS's mixer and the Sound Blaster 16 under it (kernel/drv/mixer.c, sb16.c;
 * PRD §10.5; docs/milestones-m8.md step E). */
#ifndef K_MIXER_H
#define K_MIXER_H
#include "types.h"

struct app;
struct bootinfo;

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

int sb16_init(const struct bootinfo *bi);       /* the DSP found, version 4 or later: 0 */
int sb16_start(u32 rate, u32 frames, void (*fill)(s16 *out, u32 frames));      /* two halves of `frames` */
void sb16_stop(void);
int sb16_running(void);
u32 sb16_irqs(void);

#endif
