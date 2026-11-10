/* A scheduled command's times (API 2.0's SHELL SCHEDULE; docs/v1/phase-b.md step 8): the rules, apart from the
 * clock and from where the schedule is kept. One piece of code for the kernel and the host's tests
 * (sdk/config/sched.c). Times are local, in seconds since 1970-01-01. */
#ifndef GLOS_SCHED_H
#define GLOS_SCHED_H

#define SCHED_NAME 24           /* a scheduled command's name, with its NUL */
#define SCHED_WHEN 20           /* its times as text, with their NUL: "weekly wed 23:59" */

enum { SCHED_START = 1, SCHED_DAILY, SCHED_WEEKLY };

struct sched_when {
    int kind;                   /* SCHED_ */
    int day;                    /* WEEKLY: 0 Sunday to 6 Saturday */
    int minute;                 /* DAILY, WEEKLY: of the day, 0 to 1439 */
};

/* Its times from text: "start" (each time GLOS starts), "daily HH:MM", "weekly DAY HH:MM" (DAY: sun, mon, tue,
   wed, thu, fri, sat; letters in either case). 0, or -1. */
int sched_parse(const char *text, struct sched_when *w);
int sched_format(const struct sched_when *w, char *out, int room);     /* ... as text: its length, or -1 */
/* A name: 1 to 23 letters, digits, '-' and '_'. 1 if it is one. */
int sched_name_ok(const char *name);
/* The latest moment at or before `now` at which it was due (DAILY, WEEKLY); 0 for START. */
unsigned sched_latest(const struct sched_when *w, unsigned now);
/* Is it due: has a moment of its passed since it last ran (or since it was set), at `ran`? One run makes up for
   any number of moments missed while GLOS was not running. START is never due by the clock. */
int sched_due(const struct sched_when *w, unsigned ran, unsigned now);

#endif
