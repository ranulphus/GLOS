/* Xbox pads' input reports to a joystick pad (docs/gamepad.md J5): pure, no I/O, host tested (tests/host/
 * xpad_test.c). Neither kind is HID; the layouts are from public protocol notes, checked against the owner's pads
 * (docs/gamepad-j0/: the PowerA Xbox Series pad's reports control by control; the Xbox 360 pad's to come).
 *   Xbox One and Series (GIP, interface FFh/47h/D0h): a message of type 20h, flags, sequence, length 20h, then
 *     buttons (16 bits: 2 Menu, 3 View, 4-7 A B X Y, 8-11 D-pad up down left right, 12-13 LB RB, 14-15 the sticks'
 *     clicks), LT and RT (0-1023), the left stick's X and Y and the right's (signed 16 bits, up positive).
 *   Xbox 360 (interface FFh/5Dh/01h): type 00h, length 14h, then buttons (byte 2: D-pad up down left right, Start,
 *     Back, the sticks' clicks; byte 3: LB, RB, Guide, -, A, B, X, Y), LT and RT (8 bits), and the sticks as GIP's.
 * Buttons as the port takes them: 1 A, 2 B, 3 X, 4 Y, then 5 LB, 6 RB, 7 View/Back, 8 Menu/Start, 9-10 the sticks'
 * clicks. Each stick has a dead zone (XInput's: 7849 left, 8689 right), outside which it runs 0-255; the D-pad
 * overrides the left stick. */
#include "joy.h"
#include "xpad.h"

static s32 s16at(const u8 *r, u32 at) { return (s16)(r[at] | r[at + 1] << 8); }

static u8 axis(s32 v, s32 dead, int invert)
{
    s32 span = 32767 - dead, a;
    if (v < -32767)
        v = -32767;
    if (v > -dead && v < dead)
        return 128;
    a = v > 0 ? v - dead : v + dead;            /* -span..span */
    if (invert)
        a = -a;
    a = 128 + a * 128 / span;
    return (u8)(a < 0 ? 0 : a > 255 ? 255 : a);
}

static void sticks(const u8 *r, u32 at, struct joy_pad *jp)
{
    jp->lx = axis(s16at(r, at), 7849, 0);
    jp->ly = axis(s16at(r, at + 2), 7849, 1);    /* (up positive; a game port's Y is 0 at the top) */
    jp->rx = axis(s16at(r, at + 4), 8689, 0);
    jp->ry = axis(s16at(r, at + 6), 8689, 1);
    jp->right = 1;
}

static void dpad(int up, int down, int left, int right, struct joy_pad *jp)
{
    if (up)
        jp->ly = 0;
    else if (down)
        jp->ly = 255;
    if (left)
        jp->lx = 0;
    else if (right)
        jp->lx = 255;
}

int xpad_read(int kind, const u8 *r, u32 len, struct joy_pad *jp)
{
    u32 b;
    if (kind == XPAD_GIP) {
        if (len < 18 || r[0] != 0x20)
            return -1;
        b = r[4] | (u32)r[5] << 8;
        sticks(r, 10, jp);
        dpad(b >> 8 & 1, b >> 9 & 1, b >> 10 & 1, b >> 11 & 1, jp);
        jp->buttons = (u16)((b >> 4 & 0xF) | (b >> 12 & 3) << 4 | (b >> 3 & 1) << 6 | (b >> 2 & 1) << 7
                            | (b >> 14 & 3) << 8);
        return 0;
    }
    if (kind == XPAD_360) {
        if (len < 14 || r[0] != 0x00 || r[1] < 0x14)
            return -1;
        sticks(r, 6, jp);
        dpad(r[2] & 1, r[2] >> 1 & 1, r[2] >> 2 & 1, r[2] >> 3 & 1, jp);
        jp->buttons = (u16)((r[3] >> 4 & 0xF) | (r[3] & 3) << 4 | (r[2] >> 5 & 1) << 6 | (r[2] >> 4 & 1) << 7
                            | (r[2] >> 6 & 3) << 8);
        return 0;
    }
    return -1;
}
