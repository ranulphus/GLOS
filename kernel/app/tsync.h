/* Locks and signals of an app's threads (API 2.0's THREAD, api2thr.c): what
 * each is, apart from the waiting. A thread is its number plus 1 here (0:
 * nobody). Built for the host too (tests/host/tsync_test.c). */
#ifndef K_TSYNC_H
#define K_TSYNC_H
#include "types.h"

struct tlock {
    u32 owner;                          /* who holds it, 0 free */
    u32 depth;                          /* how many times its holder took it */
};

struct tsignal {
    u8 set;
    u8 one;                             /* a wait that finds it set clears it */
};

int tlock_take(struct tlock *l, u32 who);       /* 1: who's (once more, if it was already); 0: another's */
int tlock_give(struct tlock *l, u32 who);       /* 1: free now; 0: who's still (taken more often); -1: not who's */
int tlock_drop(struct tlock *l, u32 who);       /* who has ended: 1 if it held it (free now) */
void tsignal_set(struct tsignal *s, int on);
int tsignal_take(struct tsignal *s);            /* 1 if it is set (a `one` signal is clear after) */

#endif
