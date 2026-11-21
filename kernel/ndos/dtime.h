/* Date and time in GLOS's own DOS (docs/v1/native-dos.md, N2 decided: 2Ah-2Dh): GLOS's clock in local time, and
 * a machine's own offset from it, which its programs' 2Bh and 2Dh move (never GLOS's clock). DOS's rules: years
 * 1980-2099, AL FFh for a date or time that can't be, the day of the week in 2Ah's AL. Plain C, host-tested
 * (tests/host/dtime_test.c). */
#ifndef K_NDOS_DTIME_H
#define K_NDOS_DTIME_H

struct dos_date { unsigned year, month, day, dow; };            /* dow: 0 Sunday */
struct dos_time { unsigned hour, minute, second, hundredths; };

/* Seconds since 1980-01-01 00:00 (local) and back; days and day of the week. */
long dos_days(unsigned year, unsigned month, unsigned day);        /* -1 if it can't be a DOS date */
void dos_from_seconds(unsigned long secs, struct dos_date *d, struct dos_time *t);
unsigned long dos_to_seconds(const struct dos_date *d, const struct dos_time *t);

/* 2Ah and 2Ch: the machine's date and time from GLOS's (now, seconds since 1980 local; hundredths of the second
   from GLOS's clock too) and the machine's offset (seconds). */
void dos_get(unsigned long now, unsigned hundredths, long offset, struct dos_date *d, struct dos_time *t);
/* 2Bh and 2Dh: the machine's offset made so that it reads the date (or time) given from now on. 0, or 0FFh if
   it can't be a DOS date (time) and nothing changes. */
int dos_set_date(unsigned long now, long *offset, unsigned year, unsigned month, unsigned day);
int dos_set_time(unsigned long now, long *offset, unsigned hour, unsigned minute, unsigned second,
                 unsigned hundredths);

/* A directory entry's packed date and time (FAT's: 5700h's DX, CX). */
unsigned dos_pack_date(const struct dos_date *d);
unsigned dos_pack_time(const struct dos_time *t);

#endif
