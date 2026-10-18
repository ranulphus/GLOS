/* sdk/config/tz.c on the host: zones parsed or refused, offsets around each change, civil dates both ways
 * against the C library's, a time shown twice and one never shown. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "glos/tz.h"

static int fails;

#define CHECK(c, ...) do { if (!(c)) { printf("tz_test: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static unsigned long utc(int y, int mo, int d, int h, int mi, int s)
{
    struct tz_tm t = { y, mo, d, h, mi, s, 0 };
    return tz_join(&t);
}

/* the offset just before and at a change */
static void change(const char *zone, unsigned long at, long before, long after)
{
    struct tz z;
    long b, a;
    if (tz_parse(zone, &z) != 0) {
        CHECK(0, "%s refused", zone);
        return;
    }
    b = tz_offset(&z, at - 1, 0);
    a = tz_offset(&z, at, 0);
    CHECK(b == before && a == after, "%s at %lu: %ld then %ld, not %ld then %ld", zone, at, b, a, before, after);
}

int main(void)
{
    static const char *const good[] = { "UTC0", "GMT0BST,M3.5.0/1,M10.5.0", "CET-1CEST,M3.5.0,M10.5.0/3",
                                        "EST5EDT,M3.2.0,M11.1.0", "EST5EDT", "AEST-10AEDT,M10.1.0,M4.1.0/3", "JST-9",
                                        "<+0330>-3:30", "NZST-12NZDT,M9.5.0,M4.1.0/3", "IST-5:30",
                                        "WART4WARST,J1/0,J365/25", "XXX3YYY,0/0,365/24", 0 };
    static const char *const bad[] = { "", "U0", "UTC", "UTC+", "UTC25", "GMT0BST,M13.1.0,M10.5.0",
                                       "GMT0BST,M3.6.0,M10.5.0", "GMT0BST,M3.5.7,M10.5.0", "GMT0BST,M3.5.0",
                                       "GMT0BST,J0,J365", "<AB>0", "GMT0BST,M3.5.0/1,M10.5.0x", 0 };
    struct tz z;
    unsigned long t;
    int i, dst;
    for (i = 0; good[i]; i++)
        CHECK(tz_parse(good[i], &z) == 0, "\"%s\" refused", good[i]);
    for (i = 0; bad[i]; i++)
        CHECK(tz_parse(bad[i], &z) != 0, "\"%s\" taken", bad[i]);

    /* civil dates against gmtime, every 37 hours and a bit from 1970 to 2106 */
    for (t = 0; t < (sizeof(time_t) > 4 ? 0xFFFF0000UL : 0x7FFF0000UL); t += 133337UL) {   /* (a 32-bit time_t: 2038) */
        struct tz_tm tm;
        time_t tt = (time_t)t;
        struct tm *g = gmtime(&tt);
        tz_split(t, &tm);
        if (tm.year != g->tm_year + 1900 || tm.mon != g->tm_mon + 1 || tm.day != g->tm_mday || tm.hour != g->tm_hour
            || tm.min != g->tm_min || tm.sec != g->tm_sec || tm.wday != g->tm_wday) {
            CHECK(0, "split %lu", t);
            break;
        }
        if (tz_join(&tm) != t) {
            CHECK(0, "join %lu", t);
            break;
        }
    }

    /* the changes in 2026 */
    change("GMT0BST,M3.5.0/1,M10.5.0", utc(2026, 3, 29, 1, 0, 0), 0, 3600);
    change("GMT0BST,M3.5.0/1,M10.5.0", utc(2026, 10, 25, 1, 0, 0), 3600, 0);
    change("CET-1CEST,M3.5.0,M10.5.0/3", utc(2026, 3, 29, 1, 0, 0), 3600, 7200);
    change("CET-1CEST,M3.5.0,M10.5.0/3", utc(2026, 10, 25, 1, 0, 0), 7200, 3600);
    change("EST5EDT,M3.2.0,M11.1.0", utc(2026, 3, 8, 7, 0, 0), -18000, -14400);
    change("EST5EDT,M3.2.0,M11.1.0", utc(2026, 11, 1, 6, 0, 0), -14400, -18000);
    change("EST5EDT", utc(2026, 3, 8, 7, 0, 0), -18000, -14400);
    change("AEST-10AEDT,M10.1.0,M4.1.0/3", utc(2026, 10, 3, 16, 0, 0), 36000, 39600);    /* (4 October, 02:00) */
    change("AEST-10AEDT,M10.1.0,M4.1.0/3", utc(2026, 4, 4, 16, 0, 0), 39600, 36000);     /* (5 April, 03:00) */
    change("NZST-12NZDT,M9.5.0,M4.1.0/3", utc(2026, 9, 26, 14, 0, 0), 43200, 46800);
    change("GMT0BST,M3.5.0/1,M10.5.0", utc(2027, 3, 28, 1, 0, 0), 0, 3600);
    change("GMT0BST,M3.5.0/1,M10.5.0", utc(2100, 3, 28, 1, 0, 0), 0, 3600);           /* (after 2038) */
    tz_parse("IST-5:30", &z);
    CHECK(tz_offset(&z, utc(2026, 6, 1, 0, 0, 0), &dst) == 19800 && !dst, "IST");
    tz_parse("<+0330>-3:30", &z);
    CHECK(tz_offset(&z, utc(2026, 6, 1, 0, 0, 0), 0) == 12600, "+0330");

    /* local to UTC: an ordinary time, the first of one shown twice, one never shown */
    tz_parse("GMT0BST,M3.5.0/1,M10.5.0", &z);
    CHECK(tz_local_to_utc(&z, utc(2026, 7, 1, 13, 0, 0)) == utc(2026, 7, 1, 12, 0, 0), "summer");
    CHECK(tz_local_to_utc(&z, utc(2026, 1, 1, 13, 0, 0)) == utc(2026, 1, 1, 13, 0, 0), "winter");
    CHECK(tz_local_to_utc(&z, utc(2026, 10, 25, 1, 30, 0)) == utc(2026, 10, 25, 0, 30, 0), "twice");
    CHECK(tz_local_to_utc(&z, utc(2026, 3, 29, 1, 30, 0)) == utc(2026, 3, 29, 1, 30, 0), "never");
    tz_parse("AEST-10AEDT,M10.1.0,M4.1.0/3", &z);
    CHECK(tz_local_to_utc(&z, utc(2026, 1, 15, 12, 0, 0)) == utc(2026, 1, 15, 1, 0, 0), "sydney summer");
    CHECK(tz_local_to_utc(&z, utc(2026, 7, 15, 12, 0, 0)) == utc(2026, 7, 15, 2, 0, 0), "sydney winter");

    printf("tz_test: %d failures\n", fails);
    return fails != 0;
}
