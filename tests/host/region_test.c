/* Host test for the region's formats (sdk/config/region.c; API 2.0 step 5). */
#include <stdio.h>
#include <string.h>
#include <glos/region.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static struct region r;
static char b[40];

static const char *date(int y, int m, int d) { region_date(&r, b, sizeof b, y, m, d); return b; }
static const char *clock(int h, int m, int s) { region_time(&r, b, sizeof b, h, m, s); return b; }
static const char *num(long v, int dec) { region_number(&r, b, sizeof b, v, dec); return b; }

int main(void)
{
    char small[6];

    region_default(&r);
    CHECK(!strcmp(date(2026, 11, 10), "2026-11-10") && !strcmp(date(1999, 1, 2), "1999-01-02"));
    CHECK(!strcmp(clock(14, 5, -1), "14:05") && !strcmp(clock(0, 0, 9), "00:00:09") && !strcmp(clock(23, 59, 59), "23:59:59"));
    CHECK(!strcmp(num(1234567, 0), "1,234,567") && !strcmp(num(123456, 2), "1,234.56") && !strcmp(num(0, 0), "0"));
    CHECK(!strcmp(num(-1000, 0), "-1,000") && !strcmp(num(999, 0), "999") && !strcmp(num(5, 3), "0.005"));
    CHECK(!strcmp(num(-5, 1), "-0.5") && !strcmp(num(100000, 0), "100,000") && !strcmp(num(1000000, 3), "1,000.000"));
    CHECK(!strcmp(num(2147483647L, 0), "2,147,483,647") && !strcmp(num(-2147483647L - 1, 0), "-2,147,483,648"));
    CHECK(r.first_day == 1 && r.codepage == 0 && !strcmp(r.layout, "us") && !strcmp(r.language, "en"));

    /* Each date pattern the schema has. */
    CHECK(region_set(&r, "date", "dd/mm/yyyy") == 0 && !strcmp(date(2026, 11, 10), "10/11/2026"));
    CHECK(region_set(&r, "date", "mm/dd/yyyy") == 0 && !strcmp(date(2026, 11, 10), "11/10/2026"));
    CHECK(region_set(&r, "date", "dd.mm.yyyy") == 0 && !strcmp(date(2026, 1, 9), "09.01.2026"));
    CHECK(region_set(&r, "date", "dd-mm-yyyy") == 0 && !strcmp(date(2026, 1, 9), "09-01-2026"));
    CHECK(region_set(&r, "date", "yyyy-mm-dd") == 0 && !strcmp(date(2026, 1, 9), "2026-01-09"));
    CHECK(region_set(&r, "date", "yy/mm/dd") == -1 && !strcmp(date(2026, 1, 9), "2026-01-09"));   /* refused: as it was */

    /* Twelve hours: midnight and noon are 12. */
    CHECK(region_set(&r, "clock", "12") == 0);
    CHECK(!strcmp(clock(0, 5, -1), "12:05 AM") && !strcmp(clock(12, 0, -1), "12:00 PM") && !strcmp(clock(13, 7, 9), "1:07:09 PM"));
    CHECK(!strcmp(clock(11, 59, -1), "11:59 AM") && !strcmp(clock(23, 59, -1), "11:59 PM"));
    CHECK(region_set(&r, "clock", "24") == 0 && !strcmp(clock(13, 7, -1), "13:07") && region_set(&r, "clock", "25") == -1);

    /* Numbers: the marks. */
    CHECK(region_set(&r, "decimal", "comma") == 0 && region_set(&r, "thousands", "point") == 0);
    CHECK(!strcmp(num(123456789, 2), "1.234.567,89"));
    CHECK(region_set(&r, "thousands", "space") == 0 && !strcmp(num(1234567, 0), "1 234 567"));
    CHECK(region_set(&r, "thousands", "none") == 0 && !strcmp(num(1234567, 1), "123456,7"));
    CHECK(region_set(&r, "decimal", "dot") == -1 && region_set(&r, "thousands", "tab") == -1);

    CHECK(region_set(&r, "first_day", "sunday") == 0 && r.first_day == 0 && region_set(&r, "first_day", "friday") == -1);
    CHECK(region_set(&r, "codepage", "850") == 0 && r.codepage == 850 && region_set(&r, "codepage", "1252") == -1);
    CHECK(region_set(&r, "codepage", "437x") == -1 && r.codepage == 850);
    CHECK(region_set(&r, "codepage", "dos") == 0 && r.codepage == 0 && region_set(&r, "codepage", "737") == 0 && r.codepage == 737);
    CHECK(region_set(&r, "layout", "us") == 0 && region_set(&r, "layout", "dvorak") == -1);
    CHECK(region_set(&r, "language", "en") == 0 && region_set(&r, "language", "xx") == -1);
    CHECK(region_set(&r, "colour", "red") == -1);

    /* Too little room: cut, a NUL always, the whole length told. */
    region_default(&r);
    memset(small, 'x', sizeof small);
    CHECK(region_date(&r, small, 5, 2026, 11, 10) == 10 && !strcmp(small, "2026") && small[5] == 'x');
    CHECK(region_number(&r, small, 1, 12345, 0) == 6 && small[0] == 0);
    CHECK(region_time(&r, small, 0, 1, 2, 3) == 8);

    printf("region_test: %d failures\n", fails);
    return fails != 0;
}
