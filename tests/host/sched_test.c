/* A scheduled command's times (sdk/config/sched.c). */
#include <stdio.h>
#include <string.h>

#include <glos/sched.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("sched_test: line %d: %s\n", __LINE__, #c); fails++; } } while (0)

#define DAY 86400u
/* 2026-11-10 was a Tuesday: day 20767 since 1970. */
#define TUE (20767u * DAY)

int main(void)
{
    struct sched_when w;
    char t[SCHED_WHEN];

    CHECK(sched_parse("start", &w) == 0 && w.kind == SCHED_START);
    CHECK(sched_parse("  Start  ", &w) == 0);
    CHECK(sched_parse("start now", &w) == -1);
    CHECK(sched_parse("daily 03:00", &w) == 0 && w.kind == SCHED_DAILY && w.minute == 180);
    CHECK(sched_parse("DAILY 9:05", &w) == 0 && w.minute == 545);
    CHECK(sched_parse("daily 23:59", &w) == 0 && w.minute == 1439);
    CHECK(sched_parse("daily 24:00", &w) == -1);
    CHECK(sched_parse("daily 12:60", &w) == -1);
    CHECK(sched_parse("daily 12:5", &w) == -1);
    CHECK(sched_parse("daily 12:30 x", &w) == -1);
    CHECK(sched_parse("daily", &w) == -1);
    CHECK(sched_parse("weekly wed 23:59", &w) == 0 && w.kind == SCHED_WEEKLY && w.day == 3 && w.minute == 1439);
    CHECK(sched_parse("weekly Sun 0:00", &w) == 0 && w.day == 0 && w.minute == 0);
    CHECK(sched_parse("weekly 12:00", &w) == -1);
    CHECK(sched_parse("weekly wednesday 12:00", &w) == -1);
    CHECK(sched_parse("hourly", &w) == -1);
    CHECK(sched_parse("", &w) == -1);

    sched_parse("weekly wed 23:59", &w);
    CHECK(sched_format(&w, t, sizeof t) == 16 && !strcmp(t, "weekly wed 23:59"));
    CHECK(sched_format(&w, t, 10) == -1);
    sched_parse("daily 3:07", &w);
    CHECK(sched_format(&w, t, sizeof t) == 11 && !strcmp(t, "daily 03:07"));
    sched_parse("start", &w);
    CHECK(sched_format(&w, t, sizeof t) == 5 && !strcmp(t, "start"));

    CHECK(sched_name_ok("backup") && sched_name_ok("Disk-Tools_1") && sched_name_ok("a"));
    CHECK(!sched_name_ok("") && !sched_name_ok("two words") && !sched_name_ok("tab\tbed") && !sched_name_ok("a.b"));
    CHECK(sched_name_ok("abcdefghijklmnopqrstuvw") && !sched_name_ok("abcdefghijklmnopqrstuvwx"));

    /* Daily at 03:00. */
    sched_parse("daily 03:00", &w);
    CHECK(sched_latest(&w, TUE + 3 * 3600) == TUE + 3 * 3600);
    CHECK(sched_latest(&w, TUE + 3 * 3600 - 1) == TUE - DAY + 3 * 3600);
    CHECK(sched_latest(&w, TUE + 14 * 3600) == TUE + 3 * 3600);
    CHECK(!sched_due(&w, TUE + 14 * 3600, TUE + 15 * 3600));            /* set at 14:00: not until tomorrow */
    CHECK(!sched_due(&w, TUE + 14 * 3600, TUE + DAY + 3 * 3600 - 1));
    CHECK(sched_due(&w, TUE + 14 * 3600, TUE + DAY + 3 * 3600));        /* its time */
    CHECK(!sched_due(&w, TUE + DAY + 3 * 3600, TUE + DAY + 3 * 3600 + 30));     /* run: not again */
    CHECK(sched_due(&w, TUE + 14 * 3600, TUE + 5 * DAY + 9 * 3600));    /* GLOS not running for days: once */
    CHECK(!sched_due(&w, TUE + 5 * DAY + 9 * 3600, TUE + 5 * DAY + 10 * 3600));
    CHECK(!sched_due(&w, TUE + 2 * 3600, TUE + 1 * 3600));              /* the clock put back: nothing */

    /* Weekly, Wednesday at 23:59. */
    sched_parse("weekly wed 23:59", &w);
    CHECK(sched_latest(&w, TUE + 12 * 3600) == TUE - 6 * DAY + 1439 * 60);
    CHECK(sched_latest(&w, TUE + DAY + 1439 * 60) == TUE + DAY + 1439 * 60);
    CHECK(sched_latest(&w, TUE + DAY + 1439 * 60 - 1) == TUE - 6 * DAY + 1439 * 60);
    CHECK(sched_latest(&w, TUE + 2 * DAY) == TUE + DAY + 1439 * 60);
    CHECK(!sched_due(&w, TUE, TUE + DAY + 1438 * 60));
    CHECK(sched_due(&w, TUE, TUE + DAY + 1439 * 60));
    CHECK(sched_due(&w, TUE, TUE + 30 * DAY));
    CHECK(!sched_due(&w, TUE + 2 * DAY, TUE + 7 * DAY));

    /* Sunday at midnight; the first days of 1970. */
    sched_parse("weekly sun 0:00", &w);
    CHECK(sched_latest(&w, TUE) == TUE - 2 * DAY);
    CHECK(sched_latest(&w, 2 * DAY) == 0);                              /* (a Saturday: no Sunday before it) */
    CHECK(sched_latest(&w, 3 * DAY) == 3 * DAY);
    sched_parse("daily 12:00", &w);
    CHECK(sched_latest(&w, 3600) == 0 && !sched_due(&w, 0, 3600));

    /* At start: never by the clock. */
    sched_parse("start", &w);
    CHECK(sched_latest(&w, TUE) == 0 && !sched_due(&w, 0, TUE));

    printf("sched_test: %d failures\n", fails);
    return fails != 0;
}
