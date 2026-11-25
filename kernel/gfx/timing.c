/* A screen's timings for a refresh rate ([display] refresh: phase G's G2, the owner's choice of 2026-11-25, "VBE 3.0
 * + monitor limits").
 *
 * The timings are VESA's Generalized Timing Formula's (GTF 1.1, its default parameters M 600, C 40, K 128, J 20,
 * so C' 30 and M' 300; no margins, not interlaced): the vertical sync and back porch at least 550 us, a front porch
 * of one line, a vertical sync of three; the horizontal blank from the ideal duty cycle C' - M' x the line's period,
 * in cells of eight pixels, the sync 8% of the line, the front porch half the blank less the sync; the horizontal
 * sync negative, the vertical positive. With the line period made exact for the rate (1 / (rate x lines)), the
 * pixel clock is pixels x lines x rate exactly: 640x480 at 60 Hz is 800 by 497 at 23.856 MHz, as GTF's tables have
 * it. The card then makes the nearest clock it can (VBE 3.0's 4F0Bh), and the refresh is worked out again from it.
 *
 * The monitor's limits are its EDID's range limits (descriptor FDh: vertical Hz, horizontal kHz, the pixel clock
 * in 10 MHz); a timing outside them is refused. A monitor that gives none is not checked.
 *
 * Integers only, as the kernel has no FPU state of its own: 64-bit products, divided by a 32-bit number here (the
 * kernel links no libgcc). */
#include "timing.h"

static u64 div64(u64 n, u32 d)                  /* n / d, shift and subtract */
{
    u64 q = 0, r = 0;
    int i;
    for (i = 63; i >= 0; i--) {
        r = r << 1 | ((n >> i) & 1);
        if (r >= d) {
            r -= d;
            q |= 1ull << i;
        }
    }
    return q;
}

int timing_gtf(u32 w, u32 h, u32 hz, struct timing *t)
{
    u32 frame_ns, est_ns, vsync_bp, total_v, duty, blank, total_h, hsync;
    if (w < 320 || w > 4096 || h < 200 || h > 4096 || hz < 50 || hz > 200)
        return -1;
    w = (w + 4) / 8 * 8;                        /* (cells of eight) */
    frame_ns = 1000000000u / hz;
    if (frame_ns <= 550000u)
        return -1;
    est_ns = (frame_ns - 550000u) / (h + 1);    /* the line's period, first guess (one line of front porch) */
    if (!est_ns)
        return -1;
    vsync_bp = (550000u + est_ns / 2) / est_ns; /* the sync and the back porch: 550 us at least, in lines */
    total_v = h + vsync_bp + 1;
    /* The duty cycle in thousandths of a percent: 30 - 0.3 x the line's period in us, that period 1 / (hz x lines). */
    duty = 30000u - 300000000u / (hz * total_v);
    if ((int)duty <= 0 || duty >= 100000u)
        return -1;
    blank = (w * duty + (100000u - duty) * 8u) / ((100000u - duty) * 16u) * 16u;
    total_h = w + blank;
    hsync = (total_h + 50) / 100 * 8;           /* (8% of the line, in cells) */
    t->htotal = (u16)total_h;
    t->hss = (u16)(w + blank / 2 - hsync);
    t->hse = (u16)(t->hss + hsync);
    t->vtotal = (u16)total_v;
    t->vss = (u16)(h + 1);
    t->vse = (u16)(h + 1 + 3);
    t->flags = TIMING_HNEG;
    if ((u64)total_h * total_v * hz > 0xFFFFFFFFull)
        return -1;
    t->pclk = total_h * total_v * hz;
    t->refresh = (u16)(hz * 100);
    return 0;
}

void timing_clock(struct timing *t, u32 pclk)
{
    if (!pclk)
        return;
    t->pclk = pclk;
    t->refresh = (u16)div64((u64)pclk * 100u + (u32)t->htotal * t->vtotal / 2, (u32)t->htotal * t->vtotal);
}

int edid_parse(const u8 *e, u32 n, struct edid_limits *l)
{
    static const u8 head[8] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
    u32 i, k;
    u8 sum = 0;
    for (i = 0; i < sizeof *l; i++)
        ((u8 *)l)[i] = 0;
    if (n < 128)
        return -1;
    for (i = 0; i < 8; i++)
        if (e[i] != head[i])
            return -1;
    for (i = 0; i < 128; i++)
        sum = (u8)(sum + e[i]);
    if (sum)
        return -1;
    l->valid = 1;
    l->digital = (e[20] & 0x80) != 0;
    for (k = 0; k < 4; k++) {
        const u8 *dsc = e + 54 + 18 * k;
        if (dsc[0] || dsc[1] || dsc[2])
            continue;                           /* (a detailed timing) */
        if (dsc[3] == 0xFD) {
            l->range = 1;
            l->vmin = dsc[5], l->vmax = dsc[6], l->hmin = dsc[7], l->hmax = dsc[8];
            l->pclk_max = (u16)(dsc[9] * 10u);
        } else if (dsc[3] == 0xFC) {
            for (i = 0; i < 13 && dsc[5 + i] != 0x0A; i++)
                l->name[i] = (char)(dsc[5 + i] >= 0x20 && dsc[5 + i] < 0x7F ? dsc[5 + i] : '?');
            while (i && l->name[i - 1] == ' ')
                i--;
            l->name[i] = 0;
        }
    }
    return 0;
}

const char *timing_check(const struct timing *t, const struct edid_limits *l)
{
    u32 khz;
    if (!l->valid || !l->range)
        return 0;                               /* (nothing said: not checked) */
    khz = (t->pclk / t->htotal + 500) / 1000;
    if ((u32)t->refresh + 50 < l->vmin * 100u || (u32)t->refresh > l->vmax * 100u + 50)
        return "the monitor's vertical range";
    if (khz < l->hmin || khz > l->hmax)
        return "the monitor's horizontal range";
    if (l->pclk_max && t->pclk > l->pclk_max * 1000000u)
        return "the monitor's pixel clock";
    return 0;
}

void timing_block(const struct timing *t, u8 out[64])
{
    u32 i;
    const u16 w[6] = { t->htotal, t->hss, t->hse, t->vtotal, t->vss, t->vse };
    for (i = 0; i < 64; i++)
        out[i] = 0;
    for (i = 0; i < 6; i++)
        out[2 * i] = (u8)w[i], out[2 * i + 1] = (u8)(w[i] >> 8);
    out[12] = t->flags;
    for (i = 0; i < 4; i++)
        out[13 + i] = (u8)(t->pclk >> (8 * i));
    out[17] = (u8)t->refresh, out[18] = (u8)(t->refresh >> 8);
}
