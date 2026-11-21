/* dtime_test: kernel/ndos/dtime.c: DOS's dates (1980-2099, leap years, the day of the week), a machine's own
 * offset moved by 2Bh and 2Dh without touching GLOS's clock, FAT's packed forms. "dtime_test: 0 failures". */
#include <stdio.h>

#include "../../kernel/ndos/dtime.h"

static int fails;

#define CHECK(cond, ...)                                                                                       \
    do {                                                                                                       \
        if (!(cond)) {                                                                                         \
            printf("FAIL %s:%d: ", __func__, __LINE__);                                                        \
            printf(__VA_ARGS__);                                                                               \
            printf("\n");                                                                                      \
            fails++;                                                                                           \
        }                                                                                                      \
    } while (0)

int main(void)
{
    struct dos_date d = { 2026, 11, 21, 0 }, e;
    struct dos_time t = { 15, 55, 30, 0 }, u;
    unsigned long now;
    long off = 0;

    CHECK(dos_days(1980, 1, 1) == 0 && dos_days(1980, 12, 31) == 365, "1980");
    CHECK(dos_days(2000, 2, 29) >= 0 && dos_days(2100, 1, 1) < 0 && dos_days(1979, 12, 31) < 0 &&
              dos_days(2001, 2, 29) < 0 && dos_days(2026, 4, 31) < 0 && dos_days(2026, 13, 1) < 0,
          "what can't be a DOS date");
    now = dos_to_seconds(&d, &t);
    dos_from_seconds(now, &e, &u);
    CHECK(e.year == 2026 && e.month == 11 && e.day == 21 && u.hour == 15 && u.minute == 55 && u.second == 30,
          "there and back: %u-%u-%u %u:%u:%u", e.year, e.month, e.day, u.hour, u.minute, u.second);
    CHECK(e.dow == 6, "2026-11-21 is a Saturday: %u", e.dow);
    dos_from_seconds(0, &e, &u);
    CHECK(e.dow == 2, "1980-01-01 a Tuesday: %u", e.dow);
    dos_from_seconds(dos_to_seconds(&(struct dos_date){ 2099, 12, 31, 0 }, &(struct dos_time){ 23, 59, 59, 0 }), &e, &u);
    CHECK(e.year == 2099 && e.month == 12 && e.day == 31 && e.dow == 4, "2099-12-31, a Thursday: %u", e.dow);

    /* A program sets its machine's date: the time of day goes on; GLOS's clock is now + 0 still. */
    CHECK(dos_set_date(now, &off, 1995, 8, 24) == 0, "set the date");
    dos_get(now + 10, 37, off, &e, &u);
    CHECK(e.year == 1995 && e.month == 8 && e.day == 24 && u.hour == 15 && u.minute == 55 && u.second == 40 &&
              u.hundredths == 37, "the machine's: %u-%u-%u %u:%u:%u.%u", e.year, e.month, e.day, u.hour, u.minute,
          u.second, u.hundredths);
    CHECK(dos_set_time(now, &off, 9, 0, 0, 0) == 0, "set the time");
    dos_get(now + 60, 0, off, &e, &u);
    CHECK(e.year == 1995 && e.day == 24 && u.hour == 9 && u.minute == 1 && u.second == 0, "the date stays: %u:%u",
          u.hour, u.minute);
    CHECK(dos_set_date(now, &off, 2026, 2, 30) == 0xFF && dos_set_time(now, &off, 24, 0, 0, 0) == 0xFF &&
              dos_set_time(now, &off, 1, 60, 0, 0) == 0xFF, "what can't be: FFh");
    dos_get(now, 0, off, &e, &u);
    CHECK(e.year == 1995 && u.hour == 9, "and nothing changed");
    dos_get(now, 0, -(long)now - 1000, &e, &u);
    CHECK(e.year == 1980 && e.month == 1 && e.day == 1, "an offset before 1980: 1980-01-01");

    CHECK(dos_pack_date(&d) == ((46u << 9) | (11u << 5) | 21u) && dos_pack_time(&t) == ((15u << 11) | (55u << 5) | 15u),
          "packed");
    printf("dtime_test: %d failures\n", fails);
    return fails != 0;
}
