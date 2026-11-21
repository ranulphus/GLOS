/* The skiing game's tables (skitable.h). The file: a line "SKI 1", then a line for each entry:
 *     T mode grade ms score seed day name        (a grade's table)
 *     D mode day ms score seed name              (the day's)
 *     P grade mode jacket name                   (the player's last choices)
 * in each table's order. A name is the line's rest. */
#include <stdlib.h>
#include <string.h>

#include "skimount.h"
#include "skitable.h"

void skitables_init(struct ski_tables *t) { memset(t, 0, sizeof *t); }

int skitable_place(const struct ski_table *t, int mode, unsigned long ms, long score)
{
    int i;
    for (i = 0; i < t->n; i++)          /* (a tie stands behind the one that was there first) */
        if (mode == SKI_SLALOM ? ms < t->e[i].ms : score > t->e[i].score)
            break;
    return i < SKI_TABLE_ROWS ? i : -1;
}

int skitable_add(struct ski_table *t, int mode, const struct ski_entry *e)
{
    int at = skitable_place(t, mode, e->ms, e->score), i;
    if (at < 0)
        return -1;
    if (t->n < SKI_TABLE_ROWS)
        t->n++;
    for (i = t->n - 1; i > at; i--)
        t->e[i] = t->e[i - 1];
    t->e[at] = *e;
    t->e[at].name[sizeof t->e[at].name - 1] = 0;
    return at;
}

void skitables_today(struct ski_tables *t, int yyyymmdd)
{
    if (t->day_date != yyyymmdd) {
        memset(t->day, 0, sizeof t->day);
        t->day_date = yyyymmdd;
    }
}

static void name_of(struct ski_entry *e, const char *p)
{
    size_t n;
    while (*p == ' ')
        p++;
    n = strcspn(p, "\r\n");
    if (n >= sizeof e->name)
        n = sizeof e->name - 1;
    memcpy(e->name, p, n);
    e->name[n] = 0;
}

int skitables_read(struct ski_tables *t, FILE *f)
{
    char line[160];
    int count = 0;
    skitables_init(t);
    if (!fgets(line, sizeof line, f) || strncmp(line, "SKI 1", 5))
        return 0;
    while (fgets(line, sizeof line, f)) {
        struct ski_entry e;
        struct ski_table *to = NULL;
        int mode, grade, day, used = 0;
        unsigned long ms;
        long score;
        unsigned seed;
        memset(&e, 0, sizeof e);
        if (sscanf(line, "T %d %d %lu %ld %u %d %n", &mode, &grade, &ms, &score, &seed, &day, &used) >= 6 && used
            && mode >= 0 && mode < 2 && grade >= 0 && grade < 4) {
            to = &t->grade[mode][grade];
        } else if (sscanf(line, "D %d %d %lu %ld %u %n", &mode, &day, &ms, &score, &seed, &used) >= 5 && used && mode >= 0 && mode < 2) {
            to = &t->day[mode];
            t->day_date = day;
        }
        if (!to && sscanf(line, "P %d %d %d %n", &grade, &mode, &day, &used) >= 3 && used && grade >= 0 && grade < 4 && mode >= 0
            && mode < 2 && day >= 0 && day < 16) {
            t->grade_last = grade, t->mode_last = mode, t->jacket = day;
            name_of(&e, line + used);
            memcpy(t->player, e.name, sizeof t->player);
            continue;
        }
        if (!to || to->n >= SKI_TABLE_ROWS)
            continue;
        e.ms = ms, e.score = score, e.seed = seed, e.day = day;
        name_of(&e, line + used);
        to->e[to->n++] = e;             /* (in the file's order, which is the table's) */
        count++;
    }
    return count;
}

int skitables_write(const struct ski_tables *t, FILE *f)
{
    int mode, grade, i;
    fprintf(f, "SKI 1\n");
    fprintf(f, "P %d %d %d %s\n", t->grade_last, t->mode_last, t->jacket, t->player);
    for (mode = 0; mode < 2; mode++)
        for (grade = 0; grade < 4; grade++)
            for (i = 0; i < t->grade[mode][grade].n; i++) {
                const struct ski_entry *e = &t->grade[mode][grade].e[i];
                fprintf(f, "T %d %d %lu %ld %u %d %s\n", mode, grade, e->ms, e->score, e->seed, e->day, e->name);
            }
    for (mode = 0; mode < 2; mode++)
        for (i = 0; i < t->day[mode].n; i++) {
            const struct ski_entry *e = &t->day[mode].e[i];
            fprintf(f, "D %d %d %lu %ld %u %s\n", mode, t->day_date, e->ms, e->score, e->seed, e->name);
        }
    return ferror(f) ? -1 : 0;
}
