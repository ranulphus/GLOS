/* POSIX time zones and civil dates (include/glos/tz.h): no C library, for the
 * loader (Open Watcom), the kernel, DJGPP and the host. */
#include "glos/tz.h"

static long days_from_civil(long y, int m, int d)       /* (H. Hinnant's algorithm; 1970-01-01 is 0) */
{
    long era, yoe, doy, doe;
    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153L * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097L + doe - 719468L;
}

static void civil_from_days(long z, int *y, int *m, int *d)
{
    long era, doe, yoe, doy, mp;
    z += 719468L;
    era = (z >= 0 ? z : z - 146096L) / 146097L;
    doe = z - era * 146097L;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096L) / 365;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}

void tz_split(unsigned long t, struct tz_tm *tm)
{
    long days = (long)(t / 86400UL), s = (long)(t % 86400UL);
    civil_from_days(days, &tm->year, &tm->mon, &tm->day);
    tm->hour = (int)(s / 3600);
    tm->min = (int)(s / 60 % 60);
    tm->sec = (int)(s % 60);
    tm->wday = (int)((days + 4) % 7);           /* (1970-01-01 was a Thursday) */
}

unsigned long tz_join(const struct tz_tm *tm)
{
    long y = tm->year, m = tm->mon;
    y += (m - 1) / 12;                          /* (a month past December carries) */
    m = (m - 1) % 12 + 1;
    return (unsigned long)days_from_civil(y, (int)m, 1) * 86400UL + (unsigned long)(tm->day - 1) * 86400UL
           + (unsigned long)tm->hour * 3600UL + (unsigned long)tm->min * 60UL + (unsigned long)tm->sec;
}

/* ---- parsing */

static int is_alpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static int is_digit(char c) { return c >= '0' && c <= '9'; }

static const char *name(const char *p, char *out, int room)
{
    int n = 0;
    if (*p == '<') {
        for (p++; *p && *p != '>'; p++)
            if (n < room - 1)
                out[n++] = *p;
        if (*p != '>' || n < 3)
            return 0;
        p++;
    } else {
        for (; is_alpha(*p); p++)
            if (n < room - 1)
                out[n++] = *p;
        if (n < 3)
            return 0;
    }
    out[n] = 0;
    return p;
}

static const char *number(const char *p, long *v, long max)
{
    long n = 0;
    if (!is_digit(*p))
        return 0;
    while (is_digit(*p)) {
        n = n * 10 + (*p++ - '0');
        if (n > max)
            return 0;
    }
    *v = n;
    return p;
}

/* [+|-]hh[:mm[:ss]], hours up to `hmax`: seconds, the sign kept. */
static const char *hms(const char *p, long *secs, long hmax)
{
    long h, m = 0, s = 0;
    int neg = 0;
    if (*p == '+' || *p == '-')
        neg = *p++ == '-';
    if (!(p = number(p, &h, hmax)))
        return 0;
    if (*p == ':') {
        if (!(p = number(p + 1, &m, 59)))
            return 0;
        if (*p == ':' && !(p = number(p + 1, &s, 59)))
            return 0;
    }
    *secs = (h * 3600 + m * 60 + s) * (neg ? -1 : 1);
    return p;
}

static const char *rule(const char *p, struct tz_rule *r)
{
    long a, b, c;
    if (*p == 'M') {
        if (!(p = number(p + 1, &a, 12)) || *p != '.' || !(p = number(p + 1, &b, 5)) || *p != '.'
            || !(p = number(p + 1, &c, 6)) || a < 1 || b < 1)
            return 0;
        r->kind = 'M', r->m = (int)a, r->w = (int)b, r->d = (int)c;
    } else if (*p == 'J') {
        if (!(p = number(p + 1, &a, 365)) || a < 1)
            return 0;
        r->kind = 'J', r->d = (int)a;
    } else {
        if (!(p = number(p, &a, 365)))
            return 0;
        r->kind = 'D', r->d = (int)a;
    }
    r->at = 7200;
    if (*p == '/' && !(p = hms(p + 1, &r->at, 167)))
        return 0;
    return p;
}

int tz_parse(const char *s, struct tz *z)
{
    const char *p = s;
    long off;
    if (!(p = name(p, z->std_name, sizeof z->std_name)) || !(p = hms(p, &off, 24)))
        return -1;
    z->std_off = -off;
    z->has_dst = 0;
    z->dst_name[0] = 0;
    if (!*p)
        return 0;
    if (!(p = name(p, z->dst_name, sizeof z->dst_name)))
        return -1;
    z->has_dst = 1;
    z->dst_off = z->std_off + 3600;
    if (*p && *p != ',') {
        if (!(p = hms(p, &off, 24)))
            return -1;
        z->dst_off = -off;
    }
    if (!*p) {                                  /* (no rules: the United States') */
        z->start.kind = 'M', z->start.m = 3, z->start.w = 2, z->start.d = 0, z->start.at = 7200;
        z->end.kind = 'M', z->end.m = 11, z->end.w = 1, z->end.d = 0, z->end.at = 7200;
        return 0;
    }
    if (*p != ',' || !(p = rule(p + 1, &z->start)) || *p != ',' || !(p = rule(p + 1, &z->end)) || *p)
        return -1;
    return 0;
}

/* ---- in force */

static int leap(long y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

/* The local day (days since 1970) on which rule r falls in year y. */
static long rule_day(const struct tz_rule *r, long y)
{
    if (r->kind == 'J')
        return days_from_civil(y, 1, 1) + r->d - 1 + (leap(y) && r->d >= 60);
    if (r->kind == 'D')
        return days_from_civil(y, 1, 1) + r->d;
    {
        long first = days_from_civil(y, r->m, 1), wd = (first + 4) % 7, day;
        int mdays = r->m == 2 ? 28 + leap(y) : (r->m == 4 || r->m == 6 || r->m == 9 || r->m == 11) ? 30 : 31;
        day = first + (r->d - wd + 7) % 7 + (long)(r->w - 1) * 7;
        while (day - first >= mdays)
            day -= 7;
        return day;
    }
}

long tz_offset(const struct tz *z, unsigned long utc, int *dst)
{
    unsigned long start, end;
    int in = 0, yy, mm, dd;
    if (z->has_dst) {
        long local_days = (long)((utc + (unsigned long)z->std_off) / 86400UL);
        civil_from_days(local_days, &yy, &mm, &dd);
        /* the changes as UTC seconds this year: the start in standard time, the end in summer time (unsigned:
           good until 2106) */
        start = (unsigned long)rule_day(&z->start, yy) * 86400UL + (unsigned long)(z->start.at - z->std_off);
        end = (unsigned long)rule_day(&z->end, yy) * 86400UL + (unsigned long)(z->end.at - z->dst_off);
        if (start < end)
            in = utc >= start && utc < end;
        else                                    /* (the southern hemisphere: summer spans the new year) */
            in = utc >= start || utc < end;
    }
    if (dst)
        *dst = in;
    return in ? z->dst_off : z->std_off;
}

unsigned long tz_local_to_utc(const struct tz *z, unsigned long local)
{
    unsigned long a = local - (unsigned long)z->std_off;
    long off;
    if (!z->has_dst)
        return a;
    off = tz_offset(z, local - (unsigned long)z->dst_off, 0);
    if (off == z->dst_off)
        return local - (unsigned long)z->dst_off;       /* (the first of a time shown twice: summer time's) */
    return a;
}
