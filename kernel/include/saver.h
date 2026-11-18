/* A saver that is a program (kernel/wm/saver.c; phase D, D6). */
#ifndef K_SAVER_H
#define K_SAVER_H
#include "wm.h"

int saver_turn(const struct wm *m);     /* each turn while the saver is on: 1 its program has the screen (or may yet:
                                           the desktop goes on composing), 0 the blank saver */
void saver_end(void);                   /* the saver is over: its program ended */

#endif
