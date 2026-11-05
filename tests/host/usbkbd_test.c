/* usbkbd_diff (kernel/drv/usbkbd.c, docs/gamepad.md J4): boot reports to set 1 makes and breaks, as a PS/2
 * keyboard would send them. */
#include <stdio.h>
#include <string.h>
#include "usbkbd.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void)
{
    u8 none[8] = { 0 }, a[8] = { 0, 0, 0x04 }, sa[8] = { 0x02, 0, 0x04 }, s[8] = { 0x02 }, ab[8] = { 0, 0, 0x04, 0x05 };
    u8 up[8] = { 0, 0, 0x52 }, ralt[8] = { 0x40 }, roll[8] = { 0, 0, 1, 1, 1, 1, 1, 1 }, kp[8] = { 0, 0, 0x58, 0x62 };
    struct usbkbd_event e[16];
    u32 n, rep = 0;

    n = usbkbd_diff(none, a, e, 16, &rep);                      /* a down */
    CHECK(n == 1 && e[0].code == 0x1E && e[0].down && rep == 0x1E);
    n = usbkbd_diff(a, sa, e, 16, &rep);                        /* left Shift down, a still held */
    CHECK(n == 1 && e[0].code == 0x2A && e[0].down && rep == 0x1E);
    n = usbkbd_diff(sa, s, e, 16, &rep);                        /* a up: nothing repeats */
    CHECK(n == 1 && e[0].code == 0x1E && !e[0].down && rep == 0);
    n = usbkbd_diff(s, none, e, 16, &rep);                      /* Shift up */
    CHECK(n == 1 && e[0].code == 0x2A && !e[0].down);
    n = usbkbd_diff(a, ab, e, 16, &rep);                        /* b down beside a: b repeats */
    CHECK(n == 1 && e[0].code == 0x30 && e[0].down && rep == 0x30);
    n = usbkbd_diff(ab, a, e, 16, &rep);                        /* b up: a doesn't take the repeat back */
    CHECK(n == 1 && e[0].code == 0x30 && !e[0].down && rep == 0);
    n = usbkbd_diff(none, up, e, 16, &rep);                     /* Up: E0 48 */
    CHECK(n == 1 && e[0].code == 0x148 && e[0].down);
    n = usbkbd_diff(none, ralt, e, 16, &rep);                   /* right Alt: E0 38 */
    CHECK(n == 1 && e[0].code == 0x138 && e[0].down);
    n = usbkbd_diff(a, roll, e, 16, &rep);                      /* rollover: nothing */
    CHECK(n == 0);
    n = usbkbd_diff(none, kp, e, 16, &rep);                     /* keypad Enter (E0 1C), keypad 0 (52) */
    CHECK(n == 2 && e[0].code == 0x11C && e[1].code == 0x52);
    CHECK(usbkbd_set1(0x29) == 0x01 && usbkbd_set1(0x45) == 0x58 && usbkbd_set1(0x48) == 0 && usbkbd_set1(0xE0) == 0);

    printf("usbkbd_test: %d failures\n", fails);
    return fails != 0;
}
