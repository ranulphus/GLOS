/* xpad_read (kernel/drv/xpad.c, docs/gamepad.md J5): the owner's PowerA Xbox Series pad's reports as captured in
 * J0 (docs/gamepad-j0/guided-20d6-200c.txt), and an Xbox 360 report built from the notes' layout. */
#include <stdio.h>
#include <string.h>
#include "joy.h"
#include "xpad.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

/* A GIP report: the captured rest, with buttons and sticks put in. */
static void gip(u8 *r, u16 buttons, s16 lx, s16 ly, s16 rx, s16 ry)
{
    static const u8 rest[36] = { 0x20, 0x00, 0x03, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4e, 0xfd, 0xa6, 0x03,
                                 0x91, 0xf3, 0xb3, 0xfe };
    memcpy(r, rest, 36);
    r[4] = (u8)buttons, r[5] = (u8)(buttons >> 8);
    if (lx || ly || rx || ry) {
        r[10] = (u8)lx, r[11] = (u8)((u16)lx >> 8), r[12] = (u8)ly, r[13] = (u8)((u16)ly >> 8);
        r[14] = (u8)rx, r[15] = (u8)((u16)rx >> 8), r[16] = (u8)ry, r[17] = (u8)((u16)ry >> 8);
    }
}

int main(void)
{
    u8 r[36];
    struct joy_pad p;
    static const u8 status[8] = { 0x03, 0x20, 0x04, 0x04, 0x80, 0x00, 0x00, 0x00 };
    u8 x360[20];

    gip(r, 0, 0, 0, 0, 0);                      /* at rest: -690, 934, -3183, -333, all inside the dead zones */
    CHECK(xpad_read(XPAD_GIP, r, 36, &p) == 0 && p.lx == 128 && p.ly == 128 && p.rx == 128 && p.ry == 128);
    CHECK(p.right && p.buttons == 0);
    CHECK(xpad_read(XPAD_GIP, status, 8, &p) == -1);                  /* a status message: not input */
    gip(r, 0x0010, 0, 0, 0, 0);                 /* A */
    CHECK(xpad_read(XPAD_GIP, r, 36, &p) == 0 && p.buttons == 0x001);
    gip(r, 0x00F0, 0, 0, 0, 0);                 /* A B X Y */
    CHECK(xpad_read(XPAD_GIP, r, 36, &p) == 0 && p.buttons == 0x00F);
    gip(r, 0x3000 | 0x0008 | 0x0004 | 0xC000, 0, 0, 0, 0);       /* LB RB View Menu, both clicks */
    CHECK(xpad_read(XPAD_GIP, r, 36, &p) == 0 && p.buttons == (0x030 | 0x040 | 0x080 | 0x300));
    gip(r, 0x0100 | 0x0800, 0, 0, 0, 0);        /* D-pad up and right */
    CHECK(xpad_read(XPAD_GIP, r, 36, &p) == 0 && p.ly == 0 && p.lx == 255);
    gip(r, 0, -32768, 32767, 32767, -32768);    /* left stick far left and up, right far right and down */
    CHECK(xpad_read(XPAD_GIP, r, 36, &p) == 0 && p.lx == 0 && p.ly == 0 && p.rx == 255 && p.ry == 255);
    gip(r, 0, -21186, 1162, 20721, -2182);      /* (the capture's left-stick left, right-stick right) */
    CHECK(xpad_read(XPAD_GIP, r, 36, &p) == 0 && p.lx < 80 && p.ly == 128 && p.rx > 170 && p.ry == 128);
    CHECK(xpad_read(XPAD_GIP, r, 17, &p) == -1);                      /* too short */

    memset(x360, 0, sizeof x360);               /* 360: A and LB, Start, D-pad left; left stick far right */
    x360[1] = 0x14, x360[3] = 0x10 | 0x01, x360[2] = 0x10 | 0x04;
    x360[6] = 0xFF, x360[7] = 0x7F;
    CHECK(xpad_read(XPAD_360, x360, 20, &p) == 0 && p.buttons == (0x001 | 0x010 | 0x080));
    CHECK(p.lx == 0 && p.ly == 128);            /* (the D-pad's left over the stick) */
    x360[2] = 0, x360[3] = 0;
    CHECK(xpad_read(XPAD_360, x360, 20, &p) == 0 && p.lx == 255 && p.buttons == 0);
    x360[0] = 0x01;                             /* an LED message: not input */
    CHECK(xpad_read(XPAD_360, x360, 20, &p) == -1);

    printf("xpad_test: %d failures\n", fails);
    return fails != 0;
}
