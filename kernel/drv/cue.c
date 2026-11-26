/* A CUE sheet's disc laid out (cue.h). */
#include <string.h>

#include "cue.h"

static int up(int ch) { return ch >= 'a' && ch <= 'z' ? ch - 32 : ch; }

static const char *skip(const char *p)
{
    while (*p == ' ' || *p == '\t')
        p++;
    return p;
}

/* The keyword w at *p (in any case, then a space or the line's end): 1 with *p past it, else 0. */
static int key(const char **p, const char *w)
{
    const char *q = *p;
    for (; *w; w++, q++)
        if (up((unsigned char)*q) != *w)
            return 0;
    if (*q && *q != ' ' && *q != '\t' && *q != '\r' && *q != '\n')
        return 0;
    *p = skip(q);
    return 1;
}

/* mm:ss:ff as sectors (75 a second), or -1. */
static long msf(const char *p)
{
    long v[3] = { 0, 0, 0 };
    int k;
    for (k = 0; k < 3; k++) {
        if (*p < '0' || *p > '9')
            return -1;
        while (*p >= '0' && *p <= '9')
            v[k] = v[k] * 10 + (*p++ - '0');
        if (k < 2 && *p++ != ':')
            return -1;
    }
    if (v[1] > 59 || v[2] > 74)
        return -1;
    return (v[0] * 60 + v[1]) * 75 + v[2];
}

int cue_parse(const char *text, long (*size)(void *ctx, const char *name), void *ctx, struct cue *c,
              const char **why)
{
    static long pre[CUE_TRACKS], post[CUE_TRACKS], idx0[CUE_TRACKS], idx1[CUE_TRACKS];
    long fsect[CUE_FILES];
    const char *p = text;
    unsigned long base = 0, gap = 0, shift;
    int n = -1, f = -1, k;

    memset(c, 0, sizeof *c);
    while (*p) {
        const char *e = p, *q;
        while (*e && *e != '\n')
            e++;
        q = skip(p);
        if (key(&q, "FILE")) {
            const char *a = q, *b;
            unsigned len;
            if (*a == '"') {
                b = ++a;
                while (b < e && *b != '"')
                    b++;
            } else {
                b = a;
                while (b < e && *b != ' ' && *b != '\t')
                    b++;
            }
            len = (unsigned)(b - a);
            q = skip(b < e && *b == '"' ? b + 1 : b);
            if (++f >= CUE_FILES || !len || len >= CUE_NAME) {
                *why = "a FILE's name, or too many";
                return -1;
            }
            if (!key(&q, "BINARY")) {
                *why = "a FILE that isn't BINARY";
                return -1;
            }
            memcpy(c->file[f], a, len);
            c->file[f][len] = 0;
            c->nfiles = f + 1;
            fsect[f] = -1;
        } else if (key(&q, "TRACK")) {
            int num = 0;
            while (*q >= '0' && *q <= '9')
                num = num * 10 + (*q++ - '0');
            q = skip(q);
            if (f < 0 || ++n >= CUE_TRACKS || num < 1 || num > 99 || (n && num <= c->t[n - 1].number)) {
                *why = "a TRACK out of order, or before a FILE";
                return -1;
            }
            c->t[n].number = num, c->t[n].file = f;
            pre[n] = post[n] = 0, idx0[n] = idx1[n] = -1;
            if (key(&q, "AUDIO"))
                c->t[n].type = CUE_AUDIO, c->t[n].bps = 2352;
            else if (key(&q, "MODE1/2048"))
                c->t[n].type = CUE_MODE1_2048, c->t[n].bps = 2048;
            else if (key(&q, "MODE1/2352"))
                c->t[n].type = CUE_MODE1_2352, c->t[n].bps = 2352;
            else if (key(&q, "MODE2/2352"))
                c->t[n].type = CUE_MODE2_2352, c->t[n].bps = 2352;
            else {
                *why = "a TRACK's mode";
                return -1;
            }
            c->ntracks = n + 1;
        } else if (key(&q, "INDEX")) {
            int num = 0;
            long at;
            while (*q >= '0' && *q <= '9')
                num = num * 10 + (*q++ - '0');
            at = msf(skip(q));
            if (n < 0 || at < 0) {
                *why = "an INDEX";
                return -1;
            }
            if (num == 0)
                idx0[n] = at;
            else if (num == 1)
                idx1[n] = at;
        } else if (key(&q, "PREGAP") || key(&q, "POSTGAP")) {
            int is_pre = up((unsigned char)skip(p)[1]) == 'R';     /* (PREGAP, not POSTGAP) */
            long at = msf(q);
            if (n < 0 || at < 0) {
                *why = "a PREGAP or POSTGAP";
                return -1;
            }
            if (is_pre)
                pre[n] = at;
            else
                post[n] = at;
        }
        p = *e ? e + 1 : e;
    }
    if (n < 0) {
        *why = "no TRACK";
        return -1;
    }
    /* the files' sectors, each in its first track's size (one file of two sizes is refused) */
    for (k = 0; k <= n; k++) {
        struct cue_track *t = &c->t[k];
        if (idx1[k] < 0 || (idx0[k] >= 0 && idx0[k] > idx1[k])) {
            *why = "a TRACK with no INDEX 01, or its 00 after it";
            return -1;
        }
        if (fsect[t->file] < 0) {
            long bytes = size(ctx, c->file[t->file]);
            if (bytes < 0) {
                *why = "a FILE that isn't there";
                return -1;
            }
            fsect[t->file] = bytes / (long)t->bps;
        } else if (t->bps != c->t[k - 1].bps) {
            *why = "one FILE with sectors of two sizes";
            return -1;
        }
        if (idx1[k] >= fsect[t->file] && !(idx1[k] == 0 && fsect[t->file] == 0)) {
            *why = "an INDEX past its FILE's end";
            return -1;
        }
    }
    /* The disc: each file after the last one's end, the gaps between, each track's stretch of its file. A track's
       shift is the disc's sector of its file's sector 0 as the track sees it: the gaps before it in the file
       move it on. */
    for (k = 0; k <= n; k++) {
        struct cue_track *t = &c->t[k];
        unsigned long from = (unsigned long)(idx0[k] >= 0 ? idx0[k] : idx1[k]);
        int same = k && t->file == c->t[k - 1].file;
        if (!same) {
            if (k)                              /* (the last file's end, and its last track's postgap) */
                base = c->t[k - 1].end + (unsigned long)post[k - 1];
            gap = 0;
        } else {
            gap += (unsigned long)post[k - 1];
        }
        gap += (unsigned long)pre[k];
        shift = base + gap;
        if (same)                               /* (the track before ends where this one's sectors begin) */
            c->t[k - 1].end = (c->t[k - 1].start - c->t[k - 1].off) + from;
        t->start = shift + (unsigned long)idx1[k];
        t->first = shift + from;
        t->off = (unsigned long)idx1[k];
        t->end = shift + (unsigned long)fsect[t->file];         /* (until a track after it in the file says) */
    }
    c->leadout = c->t[n].end + (unsigned long)post[n];
    return 0;
}

int cue_find(const struct cue *c, unsigned long lba, long *file_sector)
{
    int k;
    *file_sector = -1;
    for (k = 0; k < c->ntracks; k++) {
        const struct cue_track *t = &c->t[k];
        if (lba < t->first)
            return k;                           /* (its pregap, or the previous track's postgap: silence) */
        if (lba < t->end) {
            *file_sector = (long)(t->off + lba - t->start);
            return k;
        }
    }
    return lba < c->leadout ? c->ntracks - 1 : -1;
}
