/* Host test for the refresh rate's timings (kernel/gfx/timing.c): GTF's against the values its tables (and the
 * XFree86 gtf tool) give; the clock the card makes and the refresh worked out again; an EDID's range limits and
 * name, a bad one refused, the checks against the limits; VBE's CRTCInfoBlock's bytes. */
#include <stdio.h>
#include <string.h>
#include "timing.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void gtf(u32 w, u32 h, u32 hz, u32 pclk, u16 hss, u16 hse, u16 ht, u16 vss, u16 vse, u16 vt)
{
    struct timing t;
    CHECK(timing_gtf(w, h, hz, &t) == 0);
    if (t.pclk != pclk || t.hss != hss || t.hse != hse || t.htotal != ht || t.vss != vss || t.vse != vse || t.vtotal != vt) {
        printf("FAIL gtf %ux%u@%u: %u %u %u %u %u %u %u\n", w, h, hz, t.pclk, t.hss, t.hse, t.htotal, t.vss, t.vse, t.vtotal);
        fails++;
    }
    CHECK(t.flags == TIMING_HNEG && t.refresh == hz * 100);
}

int main(void)
{
    u8 e[128], b[64];
    struct edid_limits l;
    struct timing t;
    u32 i;
    u8 sum = 0;
    /* (Modeline "640x480_60.00" 23.86 640 656 720 800 480 481 484 497 -HSync +Vsync, and so on) */
    gtf(640, 480, 60, 23856000, 656, 720, 800, 481, 484, 497);
    gtf(800, 600, 60, 38215680, 832, 912, 1024, 601, 604, 622);
    gtf(1024, 768, 75, 81804000, 1080, 1192, 1360, 769, 772, 802);
    gtf(1024, 768, 85, 94386720, 1088, 1200, 1376, 769, 772, 807);
    CHECK(timing_gtf(100, 100, 60, &t) == -1 && timing_gtf(640, 480, 10, &t) == -1);

    CHECK(timing_gtf(1024, 768, 75, &t) == 0);
    timing_clock(&t, 81000000);                 /* (a card's nearest clock: the refresh a little lower) */
    CHECK(t.pclk == 81000000 && t.refresh == 7426);

    memset(e, 0, sizeof e);                     /* an analogue monitor: 50-120 Hz, 30-96 kHz, 160 MHz, "TEST CRT" */
    memcpy(e, "\0\377\377\377\377\377\377\0", 8);
    e[18] = 1, e[19] = 3, e[20] = 0x08;
    memcpy(e + 54 + 18, "\0\0\0\xFD\0\x32\x78\x1E\x60\x10\0\x0A\x20\x20\x20\x20\x20\x20", 18);
    memcpy(e + 54 + 36, "\0\0\0\xFC\0TEST CRT\x0A\x20\x20\x20\x20", 18);
    for (i = 0; i < 127; i++)
        sum = (u8)(sum + e[i]);
    e[127] = (u8)(0 - sum);
    CHECK(edid_parse(e, 128, &l) == 0 && l.valid && l.range && !l.digital);
    CHECK(l.vmin == 50 && l.vmax == 120 && l.hmin == 30 && l.hmax == 96 && l.pclk_max == 160);
    CHECK(!strcmp(l.name, "TEST CRT"));
    CHECK(timing_gtf(1024, 768, 85, &t) == 0 && timing_check(&t, &l) == 0);        /* (68.6 kHz) */
    CHECK(timing_gtf(1600, 1200, 85, &t) == 0 && timing_check(&t, &l) != 0);       /* (107 kHz: too fast) */
    l.vmax = 75;
    CHECK(timing_gtf(800, 600, 85, &t) == 0 && !strcmp(timing_check(&t, &l), "the monitor's vertical range"));
    CHECK(timing_gtf(800, 600, 75, &t) == 0 && timing_check(&t, &l) == 0);
    l.range = 0;
    CHECK(timing_gtf(1600, 1200, 85, &t) == 0 && timing_check(&t, &l) == 0);       /* (no limits: not checked) */
    e[127]++;
    CHECK(edid_parse(e, 128, &l) == -1 && !l.valid);                               /* (its sum wrong) */
    CHECK(edid_parse(e, 64, &l) == -1);

    CHECK(timing_gtf(640, 480, 60, &t) == 0);
    timing_block(&t, b);
    CHECK(b[0] == 0x20 && b[1] == 0x03 && b[2] == 0x90 && b[3] == 0x02 && b[6] == 0xF1 && b[7] == 0x01);
    CHECK(b[12] == TIMING_HNEG && b[13] == (u8)23856000 && b[16] == (u8)(23856000 >> 24) && b[17] == 0x70 && b[18] == 0x17);
    for (i = 19, sum = 0; i < 64; i++)
        sum |= b[i];
    CHECK(!sum);
    printf("timing_test: %d failures\n", fails);
    return fails != 0;
}
