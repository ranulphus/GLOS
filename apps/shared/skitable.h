/* The skiing game's tables (docs/v1/apps/skiing.md): the best ten runs of each grade, a slalom's by its time
   and a free run's by its points, and the best of the mountain of the day; kept in a text file beside the
   program. */
#ifndef SKITABLE_H
#define SKITABLE_H

#include <stdio.h>

#define SKI_TABLE_ROWS 10

struct ski_entry {
    char name[20];
    unsigned long ms;                   /* the run's time */
    long score;                         /* a free run's points */
    unsigned seed;                      /* the mountain's number */
    int day;                            /* when: yyyymmdd */
};

struct ski_table {
    int n;
    struct ski_entry e[SKI_TABLE_ROWS];
};

struct ski_tables {
    struct ski_table grade[2][4];       /* [SKI_SLALOM or SKI_FREE][the grade] */
    struct ski_table day[2];            /* the mountain of the day's, slalom and free */
    int day_date;                       /* the day those are of */
    char player[20];                    /* the name last put in a table, and the choices last made */
    int grade_last, mode_last, jacket;
};

void skitables_init(struct ski_tables *t);
/* The file read (lines that are not a table's are passed over): how many entries it had. */
int skitables_read(struct ski_tables *t, FILE *f);
int skitables_write(const struct ski_tables *t, FILE *f);      /* 0, or -1 */
/* A new day: the day's tables begin empty. */
void skitables_today(struct ski_tables *t, int yyyymmdd);
/* Where a run would stand in a table (0 the best), or -1 if it is not among the ten. `mode` says how they are
   ranked: SKI_SLALOM the shortest time first, SKI_FREE the most points. */
int skitable_place(const struct ski_table *t, int mode, unsigned long ms, long score);
/* The run put in at its place (the last one out if the table was full): the place, or -1. */
int skitable_add(struct ski_table *t, int mode, const struct ski_entry *e);

#endif
