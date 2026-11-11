/* The mixer's arithmetic (kernel/drv/mixgain.c). */
#include <stdio.h>
#include <string.h>

#include "mixgain.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("mixgain_test: line %d: %s\n", __LINE__, #c); fails++; } } while (0)

int main(void)
{
    u32 l, r, p;
    s32 acc[8];
    s16 out[8];
    u16 peak[2];

    /* A stream's gains. */
    mix_gains(256, 0, 0, &l, &r);
    CHECK(l == 256 && r == 256);
    mix_gains(128, 0, 0, &l, &r);
    CHECK(l == 128 && r == 128);
    mix_gains(256, 256, 0, &l, &r);                     /* all right: the left is silent, the right as it was */
    CHECK(l == 0 && r == 256);
    mix_gains(256, -256, 0, &l, &r);
    CHECK(l == 256 && r == 0);
    mix_gains(256, 128, 0, &l, &r);
    CHECK(l == 128 && r == 256);
    mix_gains(200, -64, 0, &l, &r);
    CHECK(l == 200 && r == 150);
    mix_gains(256, 0, 1, &l, &r);                       /* muted */
    CHECK(l == 0 && r == 0);
    mix_gains(1000, 999, 0, &l, &r);                    /* (out of range: as the nearest) */
    CHECK(l == 0 && r == 256);
    mix_gains(0, 0, 0, &l, &r);
    CHECK(l == 0 && r == 0);

    /* The master: unity copies and clips; its peaks. */
    acc[0] = 1000, acc[1] = -2000, acc[2] = 40000, acc[3] = -40000, acc[4] = 0, acc[5] = 0, acc[6] = -5, acc[7] = 7;
    peak[0] = peak[1] = 0;
    mix_master(acc, out, 4, 256, 0, peak);
    CHECK(out[0] == 1000 && out[1] == -2000 && out[2] == 32767 && out[3] == -32768 && out[6] == -5 && out[7] == 7);
    CHECK(peak[0] == 32767 && peak[1] == 32767);
    peak[0] = peak[1] = 0;
    mix_master(acc, out, 1, 256, 0, peak);
    CHECK(peak[0] == 1000 && peak[1] == 2000);
    mix_master(acc + 6, out, 1, 256, 0, peak);          /* (peaks only rise, until whoever reads them puts them back) */
    CHECK(peak[0] == 1000 && peak[1] == 2000);
    /* Half volume: before the clip, so a sum too loud is brought back into range. */
    peak[0] = peak[1] = 0;
    mix_master(acc, out, 4, 128, 0, peak);
    CHECK(out[0] == 500 && out[1] == -1000 && out[2] == 20000 && out[3] == -20000 && peak[0] == 20000 && peak[1] == 20000);
    /* Muted, and volume 0. */
    peak[0] = peak[1] = 0;
    mix_master(acc, out, 4, 256, 1, peak);
    CHECK(out[0] == 0 && out[2] == 0 && out[3] == 0 && peak[0] == 0 && peak[1] == 0);
    mix_master(acc, out, 4, 0, 0, peak);
    CHECK(out[1] == 0 && out[3] == 0);
    mix_master(acc, out, 4, 9999, 0, peak);             /* (more than unity is unity) */
    CHECK(out[0] == 1000 && out[2] == 32767);

    /* Levels and percentages. */
    CHECK(mix_percent(0) == 0 && mix_percent(32767) == 100 && mix_percent(40000) == 100 && mix_percent(16384) == 50
          && mix_percent(328) == 1 && mix_percent(100) == 0);
    for (p = 0; p <= 100; p++)
        CHECK(mix_to_percent(mix_from_percent(p)) == p);
    CHECK(mix_from_percent(100) == 256 && mix_from_percent(0) == 0 && mix_from_percent(50) == 128 && mix_from_percent(200) == 256
          && mix_to_percent(300) == 100);

    printf("mixgain_test: %d failures\n", fails);
    return fails != 0;
}
