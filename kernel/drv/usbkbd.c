/* USB keyboards' boot reports to PC keystrokes (docs/gamepad.md J4): pure, no I/O, host tested
 * (tests/host/usbkbd_test.c). A boot report (Device Class Definition for HID 1.11, appendix B.1): a byte of
 * modifier bits, a reserved byte, then up to six keys held, as Keyboard/Keypad page usages (HID Usage Tables 1.12,
 * §10). Each report is compared with the one before; a key that went down or up becomes a scan code set 1 make or
 * break, as a PS/2 keyboard sends it, in the numbers vkbc_key() takes (100h and above: with the E0 prefix). A
 * report of phantom keys (usage 01h, rollover) changes nothing. */
#include "usbkbd.h"

/* Usages 04h-65h to set 1 (0: none). */
static const u16 to_set1[0x66] = {
    [0x04] = 0x1E, [0x05] = 0x30, [0x06] = 0x2E, [0x07] = 0x20, [0x08] = 0x12, [0x09] = 0x21, [0x0A] = 0x22,
    [0x0B] = 0x23, [0x0C] = 0x17, [0x0D] = 0x24, [0x0E] = 0x25, [0x0F] = 0x26, [0x10] = 0x32, [0x11] = 0x31,
    [0x12] = 0x18, [0x13] = 0x19, [0x14] = 0x10, [0x15] = 0x13, [0x16] = 0x1F, [0x17] = 0x14, [0x18] = 0x16,
    [0x19] = 0x2F, [0x1A] = 0x11, [0x1B] = 0x2D, [0x1C] = 0x15, [0x1D] = 0x2C,                 /* a-z */
    [0x1E] = 0x02, [0x1F] = 0x03, [0x20] = 0x04, [0x21] = 0x05, [0x22] = 0x06, [0x23] = 0x07, [0x24] = 0x08,
    [0x25] = 0x09, [0x26] = 0x0A, [0x27] = 0x0B,                                                /* 1-9, 0 */
    [0x28] = 0x1C, [0x29] = 0x01, [0x2A] = 0x0E, [0x2B] = 0x0F, [0x2C] = 0x39, [0x2D] = 0x0C, [0x2E] = 0x0D,
    [0x2F] = 0x1A, [0x30] = 0x1B, [0x31] = 0x2B, [0x32] = 0x2B, [0x33] = 0x27, [0x34] = 0x28, [0x35] = 0x29,
    [0x36] = 0x33, [0x37] = 0x34, [0x38] = 0x35, [0x39] = 0x3A,                                 /* Enter-CapsLock */
    [0x3A] = 0x3B, [0x3B] = 0x3C, [0x3C] = 0x3D, [0x3D] = 0x3E, [0x3E] = 0x3F, [0x3F] = 0x40, [0x40] = 0x41,
    [0x41] = 0x42, [0x42] = 0x43, [0x43] = 0x44, [0x44] = 0x57, [0x45] = 0x58,                 /* F1-F12 */
    [0x46] = 0x137, [0x47] = 0x46,                                                              /* PrtSc, ScrLk */
    [0x49] = 0x152, [0x4A] = 0x147, [0x4B] = 0x149, [0x4C] = 0x153, [0x4D] = 0x14F, [0x4E] = 0x151,
    [0x4F] = 0x14D, [0x50] = 0x14B, [0x51] = 0x150, [0x52] = 0x148,                             /* Ins-Up */
    [0x53] = 0x45, [0x54] = 0x135, [0x55] = 0x37, [0x56] = 0x4A, [0x57] = 0x4E, [0x58] = 0x11C,
    [0x59] = 0x4F, [0x5A] = 0x50, [0x5B] = 0x51, [0x5C] = 0x4B, [0x5D] = 0x4C, [0x5E] = 0x4D, [0x5F] = 0x47,
    [0x60] = 0x48, [0x61] = 0x49, [0x62] = 0x52, [0x63] = 0x53,                                 /* the keypad */
    [0x64] = 0x56, [0x65] = 0x15D,                                                              /* \|, Menu */
};

/* The modifier bits 0-7: left Ctrl, Shift, Alt, GUI; right Ctrl, Shift, Alt, GUI. */
static const u16 mod_set1[8] = { 0x1D, 0x2A, 0x38, 0x15B, 0x11D, 0x36, 0x138, 0x15C };

u32 usbkbd_set1(u8 usage) { return usage < 0x66 ? to_set1[usage] : 0; }

static int held(const u8 *r, u8 usage)
{
    int i;
    for (i = 2; i < 8; i++)
        if (r[i] == usage)
            return 1;
    return 0;
}

u32 usbkbd_diff(const u8 *was, const u8 *now, struct usbkbd_event *ev, u32 max, u32 *repeat)
{
    u32 n = 0, i;
    for (i = 2; i < 8; i++)
        if (now[i] == 0x01)                     /* (rollover: the keyboard couldn't tell) */
            return 0;
    for (i = 0; i < 8 && n < max; i++)          /* modifiers */
        if (((was[0] ^ now[0]) >> i) & 1) {
            ev[n].code = mod_set1[i];
            ev[n++].down = (u8)(now[0] >> i & 1);
        }
    for (i = 2; i < 8 && n < max; i++)          /* keys let go */
        if (was[i] > 0x03 && !held(now, was[i]) && usbkbd_set1(was[i])) {
            ev[n].code = usbkbd_set1(was[i]);
            ev[n++].down = 0;
            if (repeat && *repeat == usbkbd_set1(was[i]))
                *repeat = 0;
        }
    for (i = 2; i < 8 && n < max; i++)          /* keys pressed: the last of them repeats */
        if (now[i] > 0x03 && !held(was, now[i]) && usbkbd_set1(now[i])) {
            ev[n].code = usbkbd_set1(now[i]);
            ev[n++].down = 1;
            if (repeat)
                *repeat = usbkbd_set1(now[i]);
        }
    return n;
}
