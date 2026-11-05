/* Xbox pads' input reports (kernel/drv/xpad.c, docs/gamepad.md J5). */
#ifndef K_XPAD_H
#define K_XPAD_H
#include "types.h"

struct joy_pad;

#define XPAD_360        1               /* interface FFh/5Dh/01h */
#define XPAD_GIP        2               /* interface FFh/47h/D0h: Xbox One and Series */

int xpad_read(int kind, const u8 *report, u32 len, struct joy_pad *jp);       /* 0, or -1: not an input report */

#endif
