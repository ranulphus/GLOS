/* USB keyboards' boot reports to PC keystrokes (kernel/drv/usbkbd.c, docs/gamepad.md J4). */
#ifndef K_USBKBD_H
#define K_USBKBD_H
#include "types.h"

struct usbkbd_event {
    u16 code;                           /* set 1, as vkbc_key() takes it (100h and above: E0 first) */
    u8 down;
};

u32 usbkbd_set1(u8 usage);              /* a Keyboard/Keypad usage's set 1 code; 0 if none */
/* The keystrokes between two 8-byte boot reports, into ev (max of them); *repeat (if given): the key that repeats
   now (set 1), 0 for none. Returns how many. */
u32 usbkbd_diff(const u8 *was, const u8 *now, struct usbkbd_event *ev, u32 max, u32 *repeat);

#endif
