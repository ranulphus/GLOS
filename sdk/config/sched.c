/* A scheduled command's times: include/glos/sched.h. No C library. */
#include <glos/sched.h>

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int word(const char **p, const char *w)         /* the word w next in *p (then a space or the end): past it */
{
    const char *s = *p;
    while (*w && lower((unsigned char)*s) == *w)
        s++, w++;
    if (*w || (*s && *s != ' '))
        return 0;
    while (*s == ' ')
        s++;
    *p = s;
    return 1;
}

static int clock_of(const char *s, int *minute)        /* "HH:MM" (or "H:MM"), and nothing after */
{
    int h = 0, m = 0, n = 0;
    for (; *s >= '0' && *s <= '9' && n < 2; s++, n++)
        h = h * 10 + (*s - '0');
    if (!n || *s++ != ':' || s[0] < '0' || s[0] > '5' || s[1] < '0' || s[1] > '9' || s[2])
        return -1;
    m = (s[0] - '0') * 10 + (s[1] - '0');
    if (h > 23)
        return -1;
    *minute = h * 60 + m;
    return 0;
}

static const char *const days[7] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };

int sched_parse(const char *text, struct sched_when *w)
{
    const char *p = text;
    int d;
    w->kind = w->day = w->minute = 0;
    while (*p == ' ')
        p++;
    if (word(&p, "start")) {
        w->kind = SCHED_START;
        return *p ? -1 : 0;
    }
    if (word(&p, "daily")) {
        w->kind = SCHED_DAILY;
        return clock_of(p, &w->minute);
    }
    if (word(&p, "weekly")) {
        w->kind = SCHED_WEEKLY;
        for (d = 0; d < 7; d++)
            if (word(&p, days[d])) {
                w->day = d;
                return clock_of(p, &w->minute);
            }
    }
    return -1;
}

static int put(char *out, int room, int n, const char *s)
{
    while (*s && n < room - 1)
        out[n++] = *s++;
    out[n] = 0;
    return *s ? -1 : n;
}

int sched_format(const struct sched_when *w, char *out, int room)
{
    char t[8];
    int n = 0;
    if (room < 1)
        return -1;
    out[0] = 0;
    if (w->kind == SCHED_START)
        return put(out, room, 0, "start");
    if (w->kind != SCHED_DAILY && w->kind != SCHED_WEEKLY)
        return -1;
    if ((n = put(out, room, 0, w->kind == SCHED_DAILY ? "daily " : "weekly ")) < 0)
        return -1;
    if (w->kind == SCHED_WEEKLY && ((n = put(out, room, n, days[w->day % 7])) < 0 || (n = put(out, room, n, " ")) < 0))
        return -1;
    t[0] = (char)('0' + w->minute / 600), t[1] = (char)('0' + w->minute / 60 % 10), t[2] = ':';
    t[3] = (char)('0' + w->minute % 60 / 10), t[4] = (char)('0' + w->minute % 10), t[5] = 0;
    return put(out, room, n, t);
}

int sched_name_ok(const char *name)
{
    int n = 0;
    for (; name[n]; n++)
        if (!((name[n] >= 'A' && name[n] <= 'Z') || (name[n] >= 'a' && name[n] <= 'z') || (name[n] >= '0' && name[n] <= '9')
              || name[n] == '-' || name[n] == '_'))
            return 0;
    return n >= 1 && n < SCHED_NAME;
}

unsigned sched_latest(const struct sched_when *w, unsigned now)
{
    unsigned day = now / 86400u, at;
    if (w->kind == SCHED_DAILY) {
        at = day * 86400u + (unsigned)w->minute * 60u;
        return at <= now ? at : at >= 86400u ? at - 86400u : 0;
    }
    if (w->kind == SCHED_WEEKLY) {
        unsigned dow = (day + 4) % 7;                   /* (1970-01-01 was a Thursday) */
        unsigned back = (dow + 7 - (unsigned)w->day) % 7;
        if (back > day)
            return 0;
        at = (day - back) * 86400u + (unsigned)w->minute * 60u;
        return at <= now ? at : at >= 7 * 86400u ? at - 7 * 86400u : 0;
    }
    return 0;
}

int sched_due(const struct sched_when *w, unsigned ran, unsigned now)
{
    unsigned at = sched_latest(w, now);
    return at != 0 && ran < at;
}
