/* HID report descriptors for joysticks and gamepads (docs/gamepad.md J4): pure, no I/O, host tested
 * (tests/host/hid_test.c). From the Device Class Definition for HID 1.11 §6.2.2 (short items: a prefix of size,
 * type and tag; main, global and local items; Input's data/variable/constant bits; usages from Usage or Usage
 * Minimum/Maximum, the last one repeating) and the HID Usage Tables 1.12 (Generic Desktop 01h: Joystick 04h, Game
 * Pad 05h, Multi-axis 08h, X 30h to Rz 35h, Hat switch 39h; Button 09h).
 *
 * hid_pad_parse() finds the first Joystick, Game Pad or Multi-axis application collection and where its axes, hat
 * and buttons are in its input report; hid_pad_read() takes one report to a pad's state. */
#include "hid.h"

#define GD_X            0x00010030u
#define GD_HAT          0x00010039u
#define PAGE_BUTTON     0x0009u

static s32 sval(const u8 *p, u32 n)            /* an item's data, sign-extended */
{
    if (n == 1)
        return (signed char)p[0];
    if (n == 2)
        return (s16)(p[0] | p[1] << 8);
    if (n == 4)
        return (s32)(p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24);
    return 0;
}

static u32 uval(const u8 *p, u32 n)
{
    return n == 1 ? p[0] : n == 2 ? (u32)(p[0] | p[1] << 8) : n == 4 ? (p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24) : 0;
}

int hid_pad_parse(const u8 *d, u32 len, struct hid_pad *pad)
{
    u32 at = 0, page = 0, rsize = 0, rcount = 0, rid = 0, usage[16], nusage = 0, umin = 0, umax = 0, depth = 0;
    u32 i, appdepth = 0, lmax_raw = 0;
    s32 lmin = 0, lmax = 0;
    int in_app = 0, have_range = 0, found = 0, rid_known = 0;
    u32 pos[16];                                /* the input bit position, per report ID (0-15) */
    for (i = 0; i < 16; i++)
        pos[i] = 0;
    for (i = 0; i < sizeof *pad; i++)
        ((u8 *)pad)[i] = 0;
    while (at < len) {
        u8 pre = d[at];
        u32 n = pre & 3, type = pre >> 2 & 3, tag = pre >> 4;
        if (pre == 0xFE) {                      /* a long item: skipped */
            if (at + 2 >= len)
                break;
            at += 3 + d[at + 1];
            continue;
        }
        if (n == 3)
            n = 4;
        if (at + 1 + n > len)
            break;
        if (type == 0) {                        /* main */
            if (tag == 0xA) {                   /* Collection */
                u32 kind = n ? d[at + 1] : 0, u = nusage ? usage[0] : 0;
                depth++;
                if (kind == 1 && !in_app && !found && (u == 0x00010004u || u == 0x00010005u || u == 0x00010008u)) {
                    in_app = 1;
                    appdepth = depth;
                }
            } else if (tag == 0xC) {            /* End Collection */
                if (in_app && depth == appdepth) {
                    in_app = 0;
                    found = 1;
                }
                if (depth)
                    depth--;
            } else if (tag == 0x8) {            /* Input */
                u32 flags = n ? d[at + 1] : 0, k, p = rid < 16 ? pos[rid] : 0;
                int signed_ = lmin < 0;
                s32 max = lmax;
                if (!signed_ && lmax < lmin)    /* (Logical Maximum 0xFF in one byte: 255, not -1) */
                    max = (s32)lmax_raw;
                if (in_app && !rid_known) {     /* (the pad's report: the ID at its first Input, which commonly */
                    pad->report_id = (u8)rid;   /* follows the collection's item) */
                    rid_known = 1;
                }
                for (k = 0; k < rcount; k++, p += rsize) {
                    u32 u = !(flags & 1) && (flags & 2)
                                ? (have_range ? (umin + k <= umax ? umin + k : umax)
                                              : (nusage ? usage[k < nusage ? k : nusage - 1] : 0))
                                : 0;
                    if (!in_app || (flags & 1) || !(flags & 2) || (pad->report_id != rid))
                        continue;
                    if (u >= GD_X && u <= GD_X + 5 && rsize <= 32) {
                        struct hid_field *f = &pad->axis[u - GD_X];
                        if (!f->bits) {
                            f->bit = (u16)p, f->bits = (u8)rsize, f->min = lmin, f->max = max, f->is_signed = (u8)signed_;
                            pad->axes |= (u8)(1u << (u - GD_X));
                        }
                    } else if (u == GD_HAT && rsize <= 8 && !pad->hat.bits) {
                        pad->hat.bit = (u16)p, pad->hat.bits = (u8)rsize, pad->hat.min = lmin, pad->hat.max = max;
                    } else if ((u >> 16) == PAGE_BUTTON && rsize == 1 && (u & 0xFFFF) >= 1 && (u & 0xFFFF) <= 16) {
                        u32 b = (u & 0xFFFF) - 1;
                        if (!(pad->button_mask >> b & 1)) {
                            pad->button_bit[b] = (u16)p;
                            pad->button_mask |= (u16)(1u << b);
                        }
                    }
                }
                if (rid < 16)
                    pos[rid] = p;
            }
            nusage = 0, have_range = 0;         /* (a main item ends its locals) */
        } else if (type == 1) {                 /* global */
            switch (tag) {
            case 0x0: page = uval(d + at + 1, n); break;
            case 0x1: lmin = sval(d + at + 1, n); break;
            case 0x2: lmax = sval(d + at + 1, n), lmax_raw = uval(d + at + 1, n); break;
            case 0x7: rsize = uval(d + at + 1, n); break;
            case 0x8: rid = uval(d + at + 1, n); break;
            case 0x9: rcount = uval(d + at + 1, n); break;
            }
        } else if (type == 2) {                 /* local */
            u32 v = uval(d + at + 1, n);
            if (n < 4)
                v |= page << 16;                /* (a usage without its page: the current one) */
            if (tag == 0x0 && nusage < 16)
                usage[nusage++] = v;
            else if (tag == 0x1)
                umin = v, have_range = 1;
            else if (tag == 0x2)
                umax = v, have_range = 1;
        }
        at += 1 + n;
    }
    return found && (pad->axes || pad->button_mask) ? 0 : -1;
}

static u32 bits_at(const u8 *r, u32 len, u32 bit, u32 n)
{
    u32 v = 0, i;
    for (i = 0; i < n; i++, bit++)
        if (bit / 8 < len && (r[bit / 8] >> (bit % 8) & 1))
            v |= 1u << i;
    return v;
}

static u8 scale(const struct hid_field *f, const u8 *r, u32 len)
{
    s32 v = (s32)bits_at(r, len, f->bit, f->bits);
    u32 span, at;
    if (f->is_signed && f->bits < 32 && (v >> (f->bits - 1) & 1))
        v -= (s32)(1u << f->bits);
    if (f->max <= f->min)
        return 128;
    if (v < f->min)
        v = f->min;
    if (v > f->max)
        v = f->max;
    span = (u32)(f->max - f->min), at = (u32)(v - f->min);
    while (span > 0xFFFFFFu)                    /* (32-bit arithmetic: 255 times a 24-bit span at most) */
        span >>= 1, at >>= 1;
    return (u8)((at * 255 + span / 2) / span);
}

int hid_pad_read(const struct hid_pad *pad, const u8 *r, u32 len, struct hid_pad_state *s)
{
    u32 i;
    if (pad->report_id) {
        if (!len || r[0] != pad->report_id)
            return -1;
        r++, len--;
    }
    for (i = 0; i < 6; i++)
        s->axis[i] = pad->axes >> i & 1 ? scale(&pad->axis[i], r, len) : 128;
    s->buttons = 0;
    for (i = 0; i < 16; i++)
        if ((pad->button_mask >> i & 1) && bits_at(r, len, pad->button_bit[i], 1))
            s->buttons |= (u16)(1u << i);
    s->hat = 8;                                 /* centred */
    if (pad->hat.bits) {
        s32 v = (s32)bits_at(r, len, pad->hat.bit, pad->hat.bits) - pad->hat.min;
        if (v >= 0 && v < 8 && pad->hat.max - pad->hat.min == 7)
            s->hat = (u8)v;
    }
    return 0;
}
