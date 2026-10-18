/* Time zones as POSIX TZ strings (docs/milestones-m10.md step A), and civil
 * dates: plain C without a C library, shared by the settings' checker
 * (sdk/config/cfg.c), the kernel's clock and the host tests.
 *
 * A zone is "STDoffset[DST[offset][,start[/time],end[/time]]]", as POSIX and
 * glibc have it: GMT0BST,M3.5.0/1,M10.5.0 (London), CET-1CEST,M3.5.0,M10.5.0/3
 * (Paris), EST5EDT,M3.2.0,M11.1.0 (New York), AEST-10AEDT,M10.1.0,M4.1.0/3
 * (Sydney), UTC0. An offset is the time to add to local time to get UTC
 * (west of Greenwich positive); a DST without one is an hour ahead of
 * standard time; a DST without rules has the United States' (M3.2.0,M11.1.0).
 * Names are three or more letters, or anything but '>' in <...>.
 *
 * Times are seconds since 1970-01-01 00:00:00 UTC, unsigned 32 bits: until
 * 2106. */
#ifndef GLOS_TZ_H
#define GLOS_TZ_H

struct tz_rule {
    char kind;                          /* 'M' month, week, day; 'J' a day 1-365 without 29 February; 'D' 0-365 */
    int m, w, d;                        /* M: month 1-12, week 1-5 (5: the last), weekday 0-6 (Sunday 0); J, D: d */
    long at;                            /* seconds into that day, local time then in force (default 7200) */
};

struct tz {
    long std_off, dst_off;              /* seconds to add to UTC for local time (east positive) */
    int has_dst;
    struct tz_rule start, end;          /* standard to daylight time, and back */
    char std_name[12], dst_name[12];
};

struct tz_tm {
    int year, mon, day;                 /* 1970-2106, 1-12, 1-31 */
    int hour, min, sec;
    int wday;                           /* 0 Sunday */
};

int tz_parse(const char *s, struct tz *z);      /* 0, or -1 if it isn't a zone */
long tz_offset(const struct tz *z, unsigned long utc, int *dst);        /* local - UTC, seconds; *dst set if not NULL */
unsigned long tz_local_to_utc(const struct tz *z, unsigned long local); /* (a time shown twice: the first; one never
                                                   shown, the summer's skip: as if standard) */
void tz_split(unsigned long t, struct tz_tm *tm);
unsigned long tz_join(const struct tz_tm *tm);  /* the fields as they are (out of range: carried) */

#endif
