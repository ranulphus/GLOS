/* TrueType fonts read (ttf.h): the table directory, the character maps (formats 0, 4, 6 and 12), the advances and
 * the outlines of "glyf": simple glyphs (contours of points, on the curve or off it: two off-curve points in a
 * row have an on-curve one between them, unsaid) and glyphs put together from others, each placed by a matrix.
 * Written from the TrueType reference; every offset and count is checked against the table it lies in, and
 * glyphs within glyphs go eight deep at most. */
#include <string.h>

#include "ttf.h"

#define MAX_POINTS 8192

static unsigned u16(const unsigned char *p) { return (unsigned)p[0] << 8 | p[1]; }
static int s16(const unsigned char *p) { return (int)(short)u16(p); }
static unsigned long u32(const unsigned char *p) { return (unsigned long)u16(p) << 16 | u16(p + 2); }

int ttf_open(struct ttf *t, const unsigned char *d, unsigned long n)
{
    unsigned long head = 0, maxp = 0, hhea = 0, cmap = 0, cmap_len = 0, at, i;
    unsigned tables;
    memset(t, 0, sizeof *t);
    if (n < 12)
        return -1;
    at = 0;
    if (!memcmp(d, "ttcf", 4)) {                /* a collection: its first font */
        if (n < 16 || (at = u32(d + 12)) > n - 12)
            return -1;
    }
    if (u32(d + at) != 0x00010000ul && memcmp(d + at, "true", 4))
        return -1;
    tables = u16(d + at + 4);
    if (tables > 256 || (n - at - 12) / 16 < tables)
        return -1;
    t->d = d, t->n = n;
    for (i = 0; i < tables; i++) {
        const unsigned char *e = d + at + 12 + 16 * i;
        unsigned long off = u32(e + 8), len = u32(e + 12);
        if (off > n || len > n - off)           /* (a table that isn't all there: as if it had none) */
            continue;
        if (!memcmp(e, "head", 4) && len >= 54)
            head = off;
        else if (!memcmp(e, "maxp", 4) && len >= 6)
            maxp = off;
        else if (!memcmp(e, "hhea", 4) && len >= 36)
            hhea = off;
        else if (!memcmp(e, "hmtx", 4))
            t->hmtx = off, t->hmtx_len = len;
        else if (!memcmp(e, "loca", 4))
            t->loca = off, t->loca_len = len;
        else if (!memcmp(e, "glyf", 4))
            t->glyf = off, t->glyf_len = len;
        else if (!memcmp(e, "cmap", 4) && len >= 4)
            cmap = off, cmap_len = len;
    }
    if (!head || !maxp || !t->loca || !t->glyf)
        return -1;
    t->em = u16(d + head + 18);
    t->long_loca = s16(d + head + 50) != 0;
    t->glyphs = u16(d + maxp + 4);
    t->advances = hhea ? u16(d + hhea + 34) : 0;
    if (t->em < 16 || t->em > 16384)
        t->em = 1000;
    if (t->glyphs > t->loca_len / (t->long_loca ? 4 : 2))       /* (no more glyphs than it says where they are) */
        t->glyphs = (unsigned)(t->loca_len / (t->long_loca ? 4 : 2));
    if (t->glyphs)
        t->glyphs--;                            /* (the table has one entry more: where the last glyph ends) */
    if (cmap) {
        unsigned maps = u16(d + cmap + 2);
        for (i = 0; i < maps && 4 + 8 * i + 8 <= cmap_len; i++) {
            const unsigned char *e = d + cmap + 4 + 8 * i;
            unsigned platform = u16(e), encoding = u16(e + 2);
            unsigned long off = u32(e + 4);
            if (off >= cmap_len || cmap_len - off < 6)
                continue;
            if ((platform == 3 && (encoding == 1 || encoding == 10)) || platform == 0) {
                if (!t->map_unicode || encoding == 10)
                    t->map_unicode = cmap + off, t->len_unicode = cmap_len - off;
            } else if (platform == 3 && encoding == 0) {
                t->map_symbol = cmap + off, t->len_symbol = cmap_len - off;
            } else if (platform == 1 && encoding == 0) {
                t->map_mac = cmap + off, t->len_mac = cmap_len - off;
            }
        }
    }
    return 0;
}

static unsigned map(const struct ttf *t, unsigned long at, unsigned long len, unsigned long code)
{
    const unsigned char *m = t->d + at;
    unsigned format, g = 0;
    if (!at || len < 6)
        return 0;
    format = u16(m);
    if (format == 0) {
        g = code < 256 && 6 + code < len ? m[6 + code] : 0;
    } else if (format == 6) {
        unsigned long first = u16(m + 6), count = len >= 10 ? u16(m + 8) : 0;
        if (code >= first && code - first < count && 10 + 2 * (code - first) + 2 <= len)
            g = u16(m + 10 + 2 * (code - first));
    } else if (format == 4 && len >= 16 && code < 0x10000) {
        unsigned long segs = u16(m + 6) / 2, i;
        if (16 + 8 * segs > len)
            return 0;
        for (i = 0; i < segs; i++) {            /* (a few hundred at most: gone through in turn) */
            unsigned long end = u16(m + 14 + 2 * i), start = u16(m + 16 + 2 * segs + 2 * i);
            if (code > end)
                continue;
            if (code >= start) {
                unsigned delta = u16(m + 16 + 4 * segs + 2 * i), range = u16(m + 16 + 6 * segs + 2 * i);
                if (!range) {
                    g = (unsigned)(code + delta) & 0xFFFF;
                } else {
                    unsigned long where = 16 + 6 * segs + 2 * i + range + 2 * (code - start);
                    g = where + 2 <= len ? u16(m + where) : 0;
                    g = g ? (g + delta) & 0xFFFF : 0;
                }
            }
            break;
        }
    } else if (format == 12 && len >= 16) {
        unsigned long groups = u32(m + 12), i;
        if (groups > (len - 16) / 12)
            groups = (len - 16) / 12;
        for (i = 0; i < groups; i++) {
            unsigned long start = u32(m + 16 + 12 * i), end = u32(m + 20 + 12 * i);
            if (code >= start && code <= end) {
                g = (unsigned)(u32(m + 24 + 12 * i) + (code - start));
                break;
            }
        }
    }
    return g <= t->glyphs && g < 0x10000 ? g : 0;
}

unsigned ttf_by_unicode(const struct ttf *t, unsigned long code) { return map(t, t->map_unicode, t->len_unicode, code); }
unsigned ttf_by_mac(const struct ttf *t, unsigned code) { return map(t, t->map_mac, t->len_mac, code); }

unsigned ttf_by_symbol(const struct ttf *t, unsigned code)
{
    unsigned g = map(t, t->map_symbol, t->len_symbol, code);
    return g ? g : map(t, t->map_symbol, t->len_symbol, 0xF000u + code);
}

int ttf_advance(const struct ttf *t, unsigned glyph)
{
    unsigned long at;
    if (!t->advances || !t->hmtx_len)
        return (int)t->em / 2;
    at = 4ul * (glyph < t->advances ? glyph : t->advances - 1);        /* (the last advance is that of all after it) */
    return at + 2 <= t->hmtx_len ? (int)u16(t->d + t->hmtx + at) : (int)t->em / 2;
}

/* Where glyph g lies in "glyf": 0 and its bytes, or -1 (none, or outside the table). An empty glyph has len 0. */
static int glyph_at(const struct ttf *t, unsigned g, const unsigned char **p, unsigned long *len)
{
    unsigned long a, b;
    if (g >= t->glyphs + 1 || g >= 0xFFFF)
        return -1;
    if (t->long_loca) {
        if (4ul * g + 8 > t->loca_len)
            return -1;
        a = u32(t->d + t->loca + 4ul * g), b = u32(t->d + t->loca + 4ul * g + 4);
    } else {
        if (2ul * g + 4 > t->loca_len)
            return -1;
        a = 2ul * u16(t->d + t->loca + 2ul * g), b = 2ul * u16(t->d + t->loca + 2ul * g + 2);
    }
    if (b < a || b > t->glyf_len)
        return -1;
    *p = t->d + t->glyf + a, *len = b - a;
    return 0;
}

/* A point of a glyph placed by the matrix it is drawn through. */
static void place(const double *m, double x, double y, double *ox, double *oy)
{
    *ox = m[0] * x + m[2] * y + m[4], *oy = m[1] * x + m[3] * y + m[5];
}

static int outline(const struct ttf *t, unsigned g, const double *m, const struct ttf_sink *s, int depth)
{
    static short xs[MAX_POINTS], ys[MAX_POINTS];        /* (one outline at a time: the reader's caller sees to it) */
    static unsigned char on[MAX_POINTS];
    const unsigned char *p, *end, *q;
    unsigned long len;
    int contours, i, points, x, y;
    if (glyph_at(t, g, &p, &len) != 0)
        return -1;
    if (len < 10)
        return len ? -1 : 0;
    end = p + len;
    contours = s16(p);
    if (contours < 0) {                         /* put together from other glyphs */
        int more = 1, parts = 0;
        if (depth >= 8)
            return -1;
        for (q = p + 10; more && parts < 64; parts++) {
            unsigned flags, part;
            double sub[6] = { 1, 0, 0, 1, 0, 0 }, all[6];
            if (end - q < 4)
                return -1;
            flags = u16(q), part = u16(q + 2);
            q += 4;
            if (flags & 1) {                    /* where it goes: two words, or two bytes */
                if (end - q < 4)
                    return -1;
                if (flags & 2)
                    sub[4] = s16(q), sub[5] = s16(q + 2);
                q += 4;
            } else {
                if (end - q < 2)
                    return -1;
                if (flags & 2)
                    sub[4] = (signed char)q[0], sub[5] = (signed char)q[1];
                q += 2;
            }
            if (flags & 8) {                    /* one scale; or two; or a whole matrix (2.14 numbers) */
                if (end - q < 2)
                    return -1;
                sub[0] = sub[3] = s16(q) / 16384.0;
                q += 2;
            } else if (flags & 0x40) {
                if (end - q < 4)
                    return -1;
                sub[0] = s16(q) / 16384.0, sub[3] = s16(q + 2) / 16384.0;
                q += 4;
            } else if (flags & 0x80) {
                if (end - q < 8)
                    return -1;
                sub[0] = s16(q) / 16384.0, sub[1] = s16(q + 2) / 16384.0, sub[2] = s16(q + 4) / 16384.0, sub[3] = s16(q + 6) / 16384.0;
                q += 8;
            }
            all[0] = sub[0] * m[0] + sub[1] * m[2], all[1] = sub[0] * m[1] + sub[1] * m[3];
            all[2] = sub[2] * m[0] + sub[3] * m[2], all[3] = sub[2] * m[1] + sub[3] * m[3];
            all[4] = sub[4] * m[0] + sub[5] * m[2] + m[4], all[5] = sub[4] * m[1] + sub[5] * m[3] + m[5];
            if (outline(t, part, all, s, depth + 1) != 0)
                return -1;
            more = flags & 0x20;
        }
        return 0;
    }
    /* a simple glyph: where each contour ends, instructions (passed over), a flag a point, the x's, the y's */
    if (!contours)
        return 0;
    if (contours > 1024 || end - (p + 10) < 2 * contours + 2)
        return -1;
    points = (int)u16(p + 10 + 2 * (contours - 1)) + 1;
    q = p + 10 + 2 * contours;
    if (points > MAX_POINTS || (unsigned long)(end - q) < 2ul + u16(q))
        return -1;
    q += 2 + u16(q);
    for (i = 0; i < points; ) {
        unsigned f, again = 0;
        if (q >= end)
            return -1;
        f = *q++;
        if (f & 8) {
            if (q >= end)
                return -1;
            again = *q++;
        }
        for (on[i++] = (unsigned char)f; again && i < points; again--)
            on[i++] = (unsigned char)f;
    }
    for (i = 0, x = 0; i < points; i++) {
        if (on[i] & 2) {
            if (q >= end)
                return -1;
            x += on[i] & 16 ? *q : -(int)*q;
            q++;
        } else if (!(on[i] & 16)) {
            if (end - q < 2)
                return -1;
            x += s16(q);
            q += 2;
        }
        xs[i] = (short)x;
    }
    for (i = 0, y = 0; i < points; i++) {
        if (on[i] & 4) {
            if (q >= end)
                return -1;
            y += on[i] & 32 ? *q : -(int)*q;
            q++;
        } else if (!(on[i] & 32)) {
            if (end - q < 2)
                return -1;
            y += s16(q);
            q += 2;
        }
        ys[i] = (short)y;
    }
    {
        int first = 0, c;
        for (c = 0; c < contours; c++) {
            int last = (int)u16(p + 10 + 2 * c), n = last - first + 1, k, start;
            double sx, sy, cx = 0, cy = 0, px, py;
            int have = 0;                       /* a control point waits for its curve's end */
            if (last >= points || n < 1)
                return -1;
            if (n < 2) {
                first = last + 1;
                continue;
            }
            /* it begins at a point on the curve: its first if that is one, else its last, else between the two;
               the other points follow in their order */
            if (on[first] & 1)
                sx = xs[first], sy = ys[first], start = first + 1, n--;
            else if (on[last] & 1)
                sx = xs[last], sy = ys[last], start = first, n--;
            else
                sx = (xs[first] + xs[last]) / 2.0, sy = (ys[first] + ys[last]) / 2.0, start = first;
            place(m, sx, sy, &px, &py);
            s->move(s->ctx, px, py);
            for (k = 0; k < n; k++) {
                double x1 = xs[start + k], y1 = ys[start + k], ax, ay, bx, by;
                if (on[start + k] & 1) {
                    place(m, x1, y1, &bx, &by);
                    if (have) {
                        place(m, cx, cy, &ax, &ay);
                        s->curve(s->ctx, ax, ay, bx, by);
                    } else {
                        s->line(s->ctx, bx, by);
                    }
                    have = 0;
                } else {
                    if (have) {                 /* two controls in a row: a point on the curve between them */
                        place(m, cx, cy, &ax, &ay), place(m, (cx + x1) / 2, (cy + y1) / 2, &bx, &by);
                        s->curve(s->ctx, ax, ay, bx, by);
                    }
                    cx = x1, cy = y1, have = 1;
                }
            }
            if (have) {                         /* back to where it began: round the last control, or straight */
                double ax, ay;
                place(m, cx, cy, &ax, &ay);
                s->curve(s->ctx, ax, ay, px, py);
            } else {
                s->line(s->ctx, px, py);
            }
            first = last + 1;
        }
    }
    return 0;
}

int ttf_outline(const struct ttf *t, unsigned glyph, const struct ttf_sink *s)
{
    static const double same[6] = { 1, 0, 0, 1, 0, 0 };
    return outline(t, glyph, same, s, 0);
}
