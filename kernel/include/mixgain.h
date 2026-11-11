/* The mixer's arithmetic apart from the card and the streams (kernel/drv/mixer.c uses it in the card's interrupt;
 * tests/host/mixgain_test.c): a stream's gains from its volume, balance and mute; the master's gain over the sum;
 * levels. All in integers. */
#ifndef K_MIXGAIN_H
#define K_MIXGAIN_H
#include "types.h"

#define MIX_UNITY 256                   /* a volume or a gain of 1 */

/* A stream's left and right gains (0 to MIX_UNITY) from its volume (0 to 256), its balance (-256 all left to
   256 all right; 0 both as they are: the far side is turned down, the near side is not turned up) and its mute. */
void mix_gains(u32 volume, s32 balance, int mute, u32 *left, u32 *right);

/* frames of 16-bit stereo from the sum (acc: left, right, left ...), through the master's volume (0 to 256) and
   mute, clipped; the peaks of what came out are raised in peak[0] (left) and peak[1] (right), 0 to 32767. */
void mix_master(const s32 *acc, s16 *out, u32 frames, u32 volume, int mute, u16 *peak);

/* A level for people: a peak (0 to 32767) as a percentage of full scale, 0 to 100. */
u32 mix_percent(u32 peak);

/* A volume in GLOS.CFG's percent (0 to 100) as a gain (0 to MIX_UNITY), and back (rounded). */
u32 mix_from_percent(u32 percent);
u32 mix_to_percent(u32 gain);

#endif
