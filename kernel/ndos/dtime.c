/* Date and time in GLOS's own DOS (dtime.h). */
#include "dtime.h"

static int leap(unsigned y)
{
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static unsigned month_days(unsigned y, unsigned m)
{
    static const unsigned char n[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    return m == 2 && leap(y) ? 29 : n[m - 1];
}

long dos_days(unsigned year, unsigned month, unsigned day)
{
    long days = 0;
    unsigned y, m;

    if (year < 1980 || year > 2099 || month < 1 || month > 12 || day < 1 || day > month_days(year, month))
        return -1;
    for (y = 1980; y < year; y++)
        days += leap(y) ? 366 : 365;
    for (m = 1; m < month; m++)
        days += month_days(year, m);
    return days + day - 1;
}

void dos_from_seconds(unsigned long secs, struct dos_date *d, struct dos_time *t)
{
    unsigned long days = secs / 86400UL, s = secs % 86400UL;
    unsigned y = 1980, m = 1;

    t->hour = (unsigned)(s / 3600);
    t->minute = (unsigned)(s / 60 % 60);
    t->second = (unsigned)(s % 60);
    t->hundredths = 0;
    d->dow = (unsigned)((days + 2) % 7);        /* (1980-01-01 was a Tuesday) */
    while (days >= (unsigned long)(leap(y) ? 366 : 365)) {
        days -= leap(y) ? 366 : 365;
        y++;
    }
    while (days >= month_days(y, m)) {
        days -= month_days(y, m);
        m++;
    }
    d->year = y;
    d->month = m;
    d->day = (unsigned)days + 1;
}

unsigned long dos_to_seconds(const struct dos_date *d, const struct dos_time *t)
{
    long days = dos_days(d->year, d->month, d->day);

    return days < 0 ? 0 : (unsigned long)days * 86400UL + t->hour * 3600UL + t->minute * 60UL + t->second;
}

/* The machine's seconds: GLOS's, and its offset, kept within DOS's years. */
static unsigned long machine(unsigned long now, long offset)
{
    long long s = (long long)now + offset;
    const long long last = (long long)dos_days(2099, 12, 31) * 86400LL + 86399LL;

    return s < 0 ? 0 : s > last ? (unsigned long)last : (unsigned long)s;
}

void dos_get(unsigned long now, unsigned hundredths, long offset, struct dos_date *d, struct dos_time *t)
{
    dos_from_seconds(machine(now, offset), d, t);
    t->hundredths = hundredths % 100;
}

int dos_set_date(unsigned long now, long *offset, unsigned year, unsigned month, unsigned day)
{
    struct dos_date d;
    struct dos_time t;
    long days = dos_days(year, month, day);

    if (days < 0)
        return 0xFF;
    dos_from_seconds(machine(now, *offset), &d, &t);    /* (the time of day stays) */
    *offset = (long)((long long)days * 86400LL + t.hour * 3600L + t.minute * 60L + t.second - (long long)now);
    return 0;
}

int dos_set_time(unsigned long now, long *offset, unsigned hour, unsigned minute, unsigned second,
                 unsigned hundredths)
{
    unsigned long s = machine(now, *offset);

    if (hour > 23 || minute > 59 || second > 59 || hundredths > 99)
        return 0xFF;
    s = s / 86400UL * 86400UL + hour * 3600UL + minute * 60UL + second;   /* (the date stays) */
    *offset = (long)((long long)s - (long long)now);
    return 0;
}

unsigned dos_pack_date(const struct dos_date *d)
{
    return ((d->year - 1980) << 9) | (d->month << 5) | d->day;
}

unsigned dos_pack_time(const struct dos_time *t)
{
    return (t->hour << 11) | (t->minute << 5) | (t->second / 2);
}
