/* The wall clock (docs/milestones-m10.md step A): UTC kept from the RTC and the tick, set by SNTP or by hand;
 * the RTC and DOS keep local time in GLOS.CFG's [time] zone. */
#ifndef CLOCK_H
#define CLOCK_H

#include "types.h"

void clock_init(void);                  /* the RTC read (taken as UTC0's local time until the zone is known) */
u32 clock_utc(void);                    /* seconds since 1970-01-01 UTC */
void clock_get_us(u32 *sec, u32 *us);   /* ... and microseconds (SNTP's originate time) */
void clock_sntp(u32 sec, u32 us);       /* SNTP's answer (the net thread) */
int clock_set(u32 utc, const char *how); /* set by hand (glos time set): 0 */
void clock_zone(const char *tz);        /* [time] zone (on load and when it changes) */
void clock_second(void);                /* about once a second (the desktop's thread or the net thread): summer time's
                                           changes; DOS's time when the DOS server was busy */
int clock_synced(void);                 /* set since GLOS started (SNTP or by hand) */
int clock_how(void);                    /* GLOS_TIME_: how it was last set (GLOS_FN_TIME) */
long clock_offset(void);                /* local time's offset now, seconds */
u32 clock_sets(void);                   /* how many times it has been set */
u32 clock_from_local(u32 local);        /* a local time in the zone, as UTC */
u32 clock_text(char *buf, u32 max);     /* glos time */
u32 clock_local(void);                  /* local time, seconds since 1970 (the task bar, files' dates) */

/* SNTP (kernel/net/net.c): the server and port asked, the interval. */
extern u16 clock_sntp_port;
u32 clock_sntp_interval(void);          /* ms */
void clock_set_interval(u32 hours);     /* [time] interval */
void clock_sntp_asked(const char *server);

#endif
