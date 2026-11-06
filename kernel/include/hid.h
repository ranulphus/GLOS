/* HID report descriptors for joysticks and gamepads (kernel/drv/hidparse.c, docs/gamepad.md J4). */
#ifndef K_HID_H
#define K_HID_H
#include "types.h"

struct hid_field {
    u16 bit;                            /* where it starts in the report (after a report ID's byte) */
    u8 bits, is_signed;
    s32 min, max;                       /* its logical range */
};

struct hid_pad {
    u8 report_id;                       /* 0: the device has none */
    u8 axes;                            /* bit n: axis n is there (X, Y, Z, Rx, Ry, Rz) */
    u16 button_mask;                    /* bit n: button n + 1 is there */
    struct hid_field axis[6], hat;      /* (hat.bits 0: none) */
    struct hid_field wheel;             /* a mouse's wheel (Generic Desktop 38h; bits 0: none) */
    u16 button_bit[16];
};

#define HID_WANT_PAD    1               /* a Joystick, Game Pad or Multi-axis application collection */
#define HID_WANT_MOUSE  2               /* a Mouse application collection */

struct hid_pad_state {
    u8 axis[6];                         /* 0-255; 128 for an axis that isn't there */
    u8 hat;                             /* 0 up, then clockwise to 7 up-left; 8 centred or none */
    u16 buttons;                        /* bit n: button n + 1 pressed */
};

int hid_parse(const u8 *desc, u32 len, int want, struct hid_pad *pad);     /* 0, or -1: none of what was wanted */
int hid_pad_parse(const u8 *desc, u32 len, struct hid_pad *pad);       /* 0, or -1: no joystick or gamepad */
/* A mouse's report: its movement (signed, as reported: right and down positive) and buttons. -1: not its. */
int hid_mouse_read(const struct hid_pad *pad, const u8 *report, u32 len, int *dx, int *dy, int *wheel,
                   u16 *buttons);
int hid_pad_read(const struct hid_pad *pad, const u8 *report, u32 len, struct hid_pad_state *s);  /* -1: not its */

#endif
