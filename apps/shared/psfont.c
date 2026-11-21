/* The PostScript kinds of font read (psfont.h): CFF and Type 1.
 *
 * A CFF font is tables ("INDEX": a count, offsets, the objects) and dictionaries of numbers each followed by
 * the operator that takes them: the top one says where the glyphs' programs are (CharStrings), which string
 * names each glyph (charset), the font's own encoding, and its private dictionary (the subroutines its programs
 * call, its two widths). A CID-keyed CFF has several private dictionaries and says which each glyph uses.
 *
 * A Type 1 font is PostScript text: a clear part (its matrix, its encoding) and, after "eexec", a part that is
 * encrypted (a running key: 55665), holding the subroutines and the glyphs' programs by name, each encrypted
 * again (4330), with a few bytes of padding before it (lenIV).
 *
 * The programs (Type 2 in a CFF, Type 1 in a Type 1 font) are numbers pushed and operators that use them:
 * moves, lines, curves (each relative to the last point), hints (passed over), and calls of subroutines. They
 * are run here straight into the caller's sink. Subroutines go ten deep at most; a program that runs off its
 * end, or asks for what isn't there, ends the glyph. */
#include <stdlib.h>
#include <string.h>

#include "psfont.h"

#define MAX_GLYPHS 65535u
#define STACK 48

struct index {                          /* a CFF INDEX */
    unsigned count, off_size;
    const unsigned char *offs, *base, *end;     /* base: the byte before its first object (offsets count from 1) */
};

struct priv {                           /* a private dictionary's part in the glyphs' programs */
    struct index subrs;
    double def_width, nom_width;
};

struct piece {                          /* a Type 1 font's program: a glyph's (with its name), or a subroutine's */
    const char *name;
    unsigned name_len;
    const unsigned char *p;
    unsigned len;
};

struct psfont {
    int kind;                           /* 1: CFF; 2: Type 1 */
    psfont_name_fn name_of;
    double scale;                       /* a unit as a part of the em */
    unsigned glyphs;
    unsigned long *uni;                 /* each glyph's character (0: not known) */
    unsigned short enc[256];            /* the font's own encoding: a code's glyph and one (0: none) */
    /* CFF */
    const unsigned char *d, *end;
    struct index strings, gsubrs, chars;
    unsigned short *sid;                /* each glyph's string number, or CID */
    int cid;
    struct priv *priv;
    unsigned nprivs;
    const unsigned char *fdselect;      /* which private dictionary a glyph uses (a CID-keyed font), or NULL */
    /* Type 1 */
    unsigned char *clear, *plain;       /* the clear part (a copy), and what was encrypted, decrypted */
    unsigned long nplain;
    struct piece *glyph, *subr;
    unsigned nsubr;
};

static unsigned u16(const unsigned char *p) { return (unsigned)p[0] << 8 | p[1]; }

/* ---- CFF: tables and dictionaries */

static int index_read(const unsigned char *p, const unsigned char *end, struct index *ix, const unsigned char **after)
{
    unsigned long last;
    unsigned k;
    memset(ix, 0, sizeof *ix);
    if (end - p < 2)
        return -1;
    ix->count = u16(p);
    if (!ix->count) {
        *after = p + 2;
        return 0;
    }
    if (end - p < 3 || p[2] < 1 || p[2] > 4 || (unsigned long)(end - p - 3) / p[2] < (unsigned long)ix->count + 1)
        return -1;
    ix->off_size = p[2], ix->offs = p + 3;
    ix->base = ix->offs + ((unsigned long)ix->count + 1) * ix->off_size - 1;
    for (last = 0, k = 0; k < ix->off_size; k++)
        last = last << 8 | ix->offs[(unsigned long)ix->count * ix->off_size + k];
    if (last < 1 || last > (unsigned long)(end - ix->base)) {
        ix->count = 0;
        return -1;
    }
    ix->end = ix->base + last;
    *after = ix->end;
    return 0;
}

static int index_get(const struct index *ix, unsigned i, const unsigned char **p, unsigned long *len)
{
    unsigned long a = 0, b = 0;
    unsigned k;
    if (i >= ix->count)
        return -1;
    for (k = 0; k < ix->off_size; k++) {
        a = a << 8 | ix->offs[(unsigned long)i * ix->off_size + k];
        b = b << 8 | ix->offs[((unsigned long)i + 1) * ix->off_size + k];
    }
    if (a < 1 || b < a || b > (unsigned long)(ix->end - ix->base))
        return -1;
    *p = ix->base + a, *len = b - a;
    return 0;
}

/* The numbers before operator `op` in a dictionary (two-byte operators: 0C00h and the second byte): how many
   (at most `max` are given), or -1 if the dictionary hasn't it. */
static int dict_get(const unsigned char *p, unsigned long len, unsigned op, double *v, int max)
{
    const unsigned char *end = p + len;
    double st[STACK];
    int n = 0, i;
    while (p < end) {
        unsigned b = *p++;
        if (b <= 21) {
            unsigned this = b;
            if (b == 12) {
                if (p >= end)
                    return -1;
                this = 0x0C00 | *p++;
            }
            if (this == op) {
                for (i = 0; i < n && i < max; i++)
                    v[i] = st[i];
                return n;
            }
            n = 0;
            continue;
        }
        if (n == STACK)
            return -1;
        if (b == 28) {
            if (end - p < 2)
                return -1;
            st[n++] = (short)u16(p);
            p += 2;
        } else if (b == 29) {
            if (end - p < 4)
                return -1;
            st[n++] = (double)((unsigned long)u16(p) << 16 | u16(p + 2)) - (p[0] & 0x80 ? 4294967296.0 : 0);
            p += 4;
        } else if (b == 30) {                   /* a real: its digits a nibble each */
            double x = 0, scale = 0, ex = 0;
            int neg = 0, eneg = 0, in_e = 0, over = 0;
            while (p < end && !over) {
                unsigned byte = *p++, h;
                for (h = 0; h < 2 && !over; h++) {
                    unsigned nib = h ? byte & 15 : byte >> 4;
                    if (nib < 10) {
                        if (in_e)
                            ex = ex * 10 + nib;
                        else if (scale > 0)
                            x += nib * (scale /= 10);
                        else
                            x = x * 10 + nib;
                    } else if (nib == 10) {
                        scale = 1;
                    } else if (nib == 11 || nib == 12) {
                        in_e = 1, eneg = nib == 12;
                    } else if (nib == 14) {
                        neg = 1;
                    } else if (nib == 15) {
                        over = 1;
                    }
                }
            }
            for (; ex > 0 && ex < 40; ex--)
                x = eneg ? x / 10 : x * 10;
            st[n++] = neg ? -x : x;
        } else if (b >= 32 && b <= 246) {
            st[n++] = (int)b - 139;
        } else if (b >= 247 && b <= 254) {
            if (p >= end)
                return -1;
            st[n++] = b < 251 ? (int)(b - 247) * 256 + *p + 108 : -(int)(b - 251) * 256 - *p - 108;
            p++;
        } else {
            return -1;
        }
    }
    return -1;
}

/* A standard string number's character (1 to 228 are the letters every font of Adobe's Latin set has; the 162
   after them are the expert set's small capitals and the like: not known here). */
static unsigned long sid_unicode(unsigned sid)
{
    static const unsigned short upper[54] = {   /* 96 to 149: the standard encoding's codes from A1h, in turn */
        0xA1, 0xA2, 0xA3, 0x2044, 0xA5, 0x192, 0xA7, 0xA4, 0x27, 0x201C, 0xAB, 0x2039, 0x203A, 0xFB01, 0xFB02, 0x2013, 0x2020,
        0x2021, 0xB7, 0xB6, 0x2022, 0x201A, 0x201E, 0x201D, 0xBB, 0x2026, 0x2030, 0xBF, 0x60, 0xB4, 0x2C6, 0x2DC, 0xAF, 0x2D8,
        0x2D9, 0xA8, 0x2DA, 0xB8, 0x2DD, 0x2DB, 0x2C7, 0x2014, 0xC6, 0xAA, 0x141, 0xD8, 0x152, 0xBA, 0xE6, 0x131, 0x142, 0xF8,
        0x153, 0xDF };
    static const unsigned short rest[79] = {    /* 150 to 228 */
        0xB9, 0xAC, 0xB5, 0x2122, 0xD0, 0xBD, 0xB1, 0xDE, 0xBC, 0xF7, 0xA6, 0xB0, 0xFE, 0xBE, 0xB2, 0xAE, 0x2212, 0xF0, 0xD7, 0xB3,
        0xA9, 0xC1, 0xC2, 0xC4, 0xC0, 0xC5, 0xC3, 0xC7, 0xC9, 0xCA, 0xCB, 0xC8, 0xCD, 0xCE, 0xCF, 0xCC, 0xD1, 0xD3, 0xD4, 0xD6,
        0xD2, 0xD5, 0x160, 0xDA, 0xDB, 0xDC, 0xD9, 0xDD, 0x178, 0x17D, 0xE1, 0xE2, 0xE4, 0xE0, 0xE5, 0xE3, 0xE7, 0xE9, 0xEA, 0xEB,
        0xE8, 0xED, 0xEE, 0xEF, 0xEC, 0xF1, 0xF3, 0xF4, 0xF6, 0xF2, 0xF5, 0x161, 0xFA, 0xFB, 0xFC, 0xF9, 0xFD, 0xFF, 0x17E };
    if (sid >= 1 && sid <= 95)
        return sid == 8 ? 0x2019 : sid == 65 ? 0x2018 : 31 + sid;       /* (its quotes are the curly ones) */
    if (sid >= 96 && sid <= 149)
        return upper[sid - 96];
    if (sid >= 150 && sid <= 228)
        return rest[sid - 150];
    return 0;
}

/* A string of the font's own (numbers from 391 on): its bytes. */
static int own_string(const struct psfont *f, unsigned sid, const unsigned char **p, unsigned long *len)
{
    return sid >= 391 ? index_get(&f->strings, sid - 391, p, len) : -1;
}

static void priv_read(const struct psfont *f, const unsigned char *dict, unsigned long len, struct priv *pr)
{
    double v[2];
    const unsigned char *at, *after;
    memset(pr, 0, sizeof *pr);
    if (dict_get(dict, len, 18, v, 2) == 2 && v[0] >= 0 && v[1] >= 0 && v[1] <= (double)(f->end - f->d)
        && v[0] <= (double)(f->end - f->d) - v[1]) {
        double w[1];
        at = f->d + (unsigned long)v[1];
        if (dict_get(at, (unsigned long)v[0], 20, w, 1) == 1)
            pr->def_width = w[0];
        if (dict_get(at, (unsigned long)v[0], 21, w, 1) == 1)
            pr->nom_width = w[0];
        if (dict_get(at, (unsigned long)v[0], 19, w, 1) == 1 && w[0] > 0 && w[0] < (double)(f->end - at))
            index_read(at + (unsigned long)w[0], f->end, &pr->subrs, &after);
    }
}

struct psfont *psfont_cff(const unsigned char *d, unsigned long n, psfont_name_fn name)
{
    struct psfont *f;
    struct index names, tops, fds;
    const unsigned char *p, *top, *after;
    unsigned long top_len;
    double v[6];
    unsigned i, g;
    if (n < 4 || d[0] != 1 || d[2] < 4 || d[2] > n || !(f = calloc(1, sizeof *f)))
        return NULL;
    f->kind = 1, f->name_of = name, f->d = d, f->end = d + n, f->scale = 0.001;
    if (index_read(d + d[2], f->end, &names, &p) != 0 || index_read(p, f->end, &tops, &p) != 0
        || index_read(p, f->end, &f->strings, &p) != 0 || index_read(p, f->end, &f->gsubrs, &p) != 0
        || index_get(&tops, 0, &top, &top_len) != 0 || dict_get(top, top_len, 17, v, 1) != 1 || v[0] < 4 || v[0] >= (double)n
        || index_read(d + (unsigned long)v[0], f->end, &f->chars, &after) != 0 || !f->chars.count)
        goto bad;
    f->glyphs = f->chars.count > MAX_GLYPHS ? MAX_GLYPHS : f->chars.count;
    if (dict_get(top, top_len, 0x0C07, v, 6) == 6 && v[0] > 1e-6 && v[0] < 1)
        f->scale = v[0];
    f->cid = dict_get(top, top_len, 0x0C1E, v, 3) >= 0;
    if (!(f->sid = calloc(f->glyphs, sizeof *f->sid)) || !(f->uni = calloc(f->glyphs, sizeof *f->uni)))
        goto bad;
    /* the charset: which string (or CID) each glyph is */
    if (dict_get(top, top_len, 15, v, 1) == 1 && v[0] > 2 && v[0] < (double)n) {
        p = d + (unsigned long)v[0];
        g = 1;
        if (*p == 0) {
            for (p++; g < f->glyphs && f->end - p >= 2; p += 2)
                f->sid[g++] = (unsigned short)u16(p);
        } else if (*p == 1 || *p == 2) {
            unsigned wide = *p == 2;
            for (p++; g < f->glyphs && (unsigned long)(f->end - p) >= 3 + wide; p += 3 + wide) {
                unsigned first = u16(p), left = wide ? u16(p + 2) : p[2], k;
                for (k = 0; k <= left && g < f->glyphs; k++)
                    f->sid[g++] = (unsigned short)(first + k);
            }
        }
    } else {
        for (g = 0; g < f->glyphs; g++)         /* (the standard set: a glyph's number is its string's) */
            f->sid[g] = (unsigned short)g;
    }
    if (!f->cid)
        for (g = 1; g < f->glyphs; g++) {
            const unsigned char *s;
            unsigned long len;
            f->uni[g] = sid_unicode(f->sid[g]);
            if (!f->uni[g] && name && own_string(f, f->sid[g], &s, &len) == 0 && len < 64) {
                char t[64];
                memcpy(t, s, len);
                t[len] = 0;
                f->uni[g] = name(t);
            }
        }
    /* its own encoding: codes in the glyphs' order, singly or in runs (the standard one: by the characters) */
    if (dict_get(top, top_len, 16, v, 1) == 1 && v[0] > 1 && v[0] < (double)n) {
        unsigned count;
        p = d + (unsigned long)v[0];
        g = 1;
        if ((*p & 0x7F) == 0 && f->end - p >= 2) {
            for (count = p[1], p += 2, i = 0; i < count && p < f->end && g < f->glyphs; i++, g++)
                f->enc[*p++] = (unsigned short)(g + 1);
        } else if ((*p & 0x7F) == 1 && f->end - p >= 2) {
            for (count = p[1], p += 2, i = 0; i < count && f->end - p >= 2; i++, p += 2) {
                unsigned k;
                for (k = 0; k <= p[1] && p[0] + k < 256 && g < f->glyphs; k++, g++)
                    f->enc[p[0] + k] = (unsigned short)(g + 1);
            }
        }
    } else if (!f->cid) {
        for (i = 32; i < 256; i++) {
            unsigned long u = i < 127 ? sid_unicode(i - 31) : 0;
            int at = u ? psfont_by_unicode(f, u) : -1;
            f->enc[i] = (unsigned short)(at + 1);
        }
    }
    /* the private dictionaries: the font's one, or (CID-keyed) one for each font it is made of */
    if (f->cid && dict_get(top, top_len, 0x0C24, v, 1) == 1 && v[0] > 0 && v[0] < (double)n
        && index_read(d + (unsigned long)v[0], f->end, &fds, &after) == 0 && fds.count) {
        f->nprivs = fds.count > 256 ? 256 : fds.count;
        if (!(f->priv = calloc(f->nprivs, sizeof *f->priv)))
            goto bad;
        for (i = 0; i < f->nprivs; i++) {
            const unsigned char *fd;
            unsigned long fd_len;
            if (index_get(&fds, i, &fd, &fd_len) == 0)
                priv_read(f, fd, fd_len, &f->priv[i]);
        }
        if (dict_get(top, top_len, 0x0C25, v, 1) == 1 && v[0] > 0 && v[0] < (double)n)
            f->fdselect = d + (unsigned long)v[0];
    } else {
        f->nprivs = 1;
        if (!(f->priv = calloc(1, sizeof *f->priv)))
            goto bad;
        priv_read(f, top, top_len, &f->priv[0]);
    }
    return f;
bad:
    psfont_close(f);
    return NULL;
}

static unsigned fd_of(const struct psfont *f, unsigned g)       /* the private dictionary glyph g uses */
{
    const unsigned char *p = f->fdselect;
    unsigned fd = 0;
    if (!p || f->nprivs < 2)
        return 0;
    if (*p == 0) {
        fd = (unsigned long)(f->end - p) > 1ul + g ? p[1 + g] : 0;
    } else if (*p == 3 && f->end - p >= 5) {
        unsigned ranges = u16(p + 1), i;
        for (i = 0; i < ranges && (unsigned long)(f->end - p) >= 3 + 3ul * i + 5; i++)
            if (g >= u16(p + 3 + 3 * i) && g < u16(p + 3 + 3 * i + 3)) {
                fd = p[3 + 3 * i + 2];
                break;
            }
    }
    return fd < f->nprivs ? fd : 0;
}

/* ---- Type 1: the file */

static void decrypt(unsigned char *p, unsigned long n, unsigned r)      /* in place: eexec's key, or a program's */
{
    unsigned long i;
    for (i = 0; i < n; i++) {
        unsigned c = p[i];
        p[i] = (unsigned char)(c ^ (r >> 8));
        r = ((c + r) * 52845u + 22719u) & 0xFFFF;
    }
}

static const unsigned char *find(const unsigned char *p, const unsigned char *end, const char *what)
{
    size_t n = strlen(what);
    for (; (size_t)(end - p) >= n; p++)
        if (*p == (unsigned char)what[0] && !memcmp(p, what, n))
            return p;
    return NULL;
}

static const unsigned char *blank(const unsigned char *p, const unsigned char *end)
{
    while (p < end && (*p == 32 || *p == 9 || *p == 10 || *p == 13))
        p++;
    return p;
}

static const unsigned char *number(const unsigned char *p, const unsigned char *end, long *v)  /* NULL: none there */
{
    int neg = 0, digits = 0;
    p = blank(p, end);
    if (p < end && *p == '-')
        neg = 1, p++;
    for (*v = 0; p < end && *p >= '0' && *p <= '9' && digits < 9; p++, digits++)
        *v = *v * 10 + (*p - '0');
    if (neg)
        *v = -*v;
    return digits ? p : NULL;
}

/* After a length and the word that follows it (RD, -|) and one space: so many bytes, a program. */
static const unsigned char *program(const unsigned char *p, const unsigned char *end, long len, int skip, struct piece *out)
{
    p = blank(p, end);
    while (p < end && *p > 32)                  /* (the word) */
        p++;
    if (p >= end || len < 0 || len > 65535 || (long)(end - p - 1) < len)
        return NULL;
    p++;
    decrypt((unsigned char *)p, (unsigned long)len, 4330);
    if (len < skip)
        skip = (int)len;
    out->p = p + skip, out->len = (unsigned)(len - skip);
    return p + len;
}

struct psfont *psfont_type1(const unsigned char *d, unsigned long n, psfont_name_fn name)
{
    struct psfont *f;
    const unsigned char *e, *p, *end, *at;
    unsigned char *cipher;
    unsigned long clear, i, k;
    long v, count, skip = 4;
    unsigned g, room;
    if (n < 64 || !(e = find(d, d + n, "eexec")) || !(f = calloc(1, sizeof *f)))
        return NULL;
    f->kind = 2, f->name_of = name, f->scale = 0.001;
    clear = (unsigned long)(e - d) + 5;
    if (!(f->clear = malloc(clear + 1)) || !(f->plain = malloc(n - clear + 1)))
        goto bad;
    memcpy(f->clear, d, clear);
    f->clear[clear] = 0;
    /* what follows "eexec" (and the line's end): bytes, or their hex; decrypted, its first four bytes padding */
    p = d + clear;
    while (p < d + n && (*p == 32 || *p == 9 || *p == 10 || *p == 13))
        p++;
    cipher = f->plain;
    for (i = 0, k = 0; i < 4 && p + i < d + n; i++)
        k += (p[i] >= '0' && p[i] <= '9') || (p[i] >= 'a' && p[i] <= 'f') || (p[i] >= 'A' && p[i] <= 'F') || p[i] <= 32;
    if (k == 4) {                               /* hex */
        int half = -1;
        for (f->nplain = 0; p < d + n; p++) {
            int h = *p >= '0' && *p <= '9' ? *p - '0' : *p >= 'a' && *p <= 'f' ? *p - 'a' + 10 : *p >= 'A' && *p <= 'F' ? *p - 'A' + 10 : -1;
            if (h < 0)
                continue;
            if (half < 0)
                half = h;
            else
                cipher[f->nplain++] = (unsigned char)(half << 4 | h), half = -1;
        }
    } else {
        f->nplain = (unsigned long)(d + n - p);
        memcpy(cipher, p, f->nplain);
    }
    decrypt(f->plain, f->nplain, 55665);
    p = f->plain + (f->nplain < 4 ? f->nplain : 4), end = f->plain + f->nplain;
    if ((at = find(f->clear, f->clear + clear, "/FontMatrix")) != NULL && (at = find(at, f->clear + clear, "[")) != NULL) {
        double x = 0, scale = 1;                /* (its first number: 0.001) */
        for (at = blank(at + 1, f->clear + clear); at < f->clear + clear && ((*at >= '0' && *at <= '9') || *at == '.'); at++) {
            if (*at == '.')
                scale = 0.1;
            else if (scale < 1)
                x += (*at - '0') * scale, scale /= 10;
            else
                x = x * 10 + (*at - '0');
        }
        if (x > 1e-6 && x < 1)
            f->scale = x;
    }
    if ((at = find(p, end, "/lenIV")) != NULL && number(at + 6, end, &v) && v >= 0 && v < 16)
        skip = v;
    /* /Subrs N array, then "dup I LEN RD ... NP" for each */
    if ((at = find(p, end, "/Subrs")) != NULL && (at = number(at + 6, end, &count)) != NULL && count > 0 && count < 65536) {
        const unsigned char *stop = find(at, end, "/CharStrings");
        if (!(f->subr = calloc((size_t)count, sizeof *f->subr)))
            goto bad;
        f->nsubr = (unsigned)count;
        while ((at = find(at, stop ? stop : end, "dup")) != NULL) {
            long which, len;
            const unsigned char *q = number(at + 3, end, &which), *next;
            struct piece pc;
            if (!q || !(q = number(q, end, &len)) || !(next = program(q, end, len, (int)skip, &pc)))
                break;
            if (which >= 0 && which < count)
                f->subr[which] = pc;
            at = next;
        }
    }
    /* /CharStrings N dict dup begin, then "/name LEN RD ... ND" for each */
    if (!(at = find(p, end, "/CharStrings")))
        goto bad;
    at += 12;
    room = 0;
    while ((at = find(at, end, "/")) != NULL) {
        const unsigned char *nm = at + 1, *q, *next;
        long len;
        struct piece pc;
        for (q = nm; q < end && *q > 32; q++)
            ;
        if (!(next = number(q, end, &len)) || !(next = program(next, end, len, (int)skip, &pc))) {
            at = q;                             /* (no program after it: some other name) */
            continue;
        }
        if (f->glyphs == room) {
            struct piece *more = realloc(f->glyph, (room ? room * 2 : 256) * sizeof *more);
            if (!more)
                goto bad;
            f->glyph = more, room = room ? room * 2 : 256;
        }
        pc.name = (const char *)nm, pc.name_len = (unsigned)(q - nm);
        f->glyph[f->glyphs++] = pc;
        at = next;
        if (f->glyphs >= MAX_GLYPHS)
            break;
    }
    if (!f->glyphs || !(f->uni = calloc(f->glyphs, sizeof *f->uni)))
        goto bad;
    for (g = 0; g < f->glyphs; g++)
        if (name && f->glyph[g].name_len < 64) {
            char t[64];
            memcpy(t, f->glyph[g].name, f->glyph[g].name_len);
            t[f->glyph[g].name_len] = 0;
            f->uni[g] = name(t);
        }
    /* its own encoding, in the clear part: "dup 65 /A put" for each code; or the standard one */
    if ((at = find(f->clear, f->clear + clear, "/Encoding")) != NULL) {
        const unsigned char *cend = f->clear + clear, *stop = find(at, cend, "def");
        const unsigned char *std = find(at, cend, "StandardEncoding");
        if (std && (!stop || std < stop)) {
            for (i = 32; i < 127; i++)
                f->enc[i] = (unsigned short)(psfont_by_unicode(f, sid_unicode((unsigned)i - 31)) + 1);
        } else {
            while ((at = find(at, cend, "dup")) != NULL) {
                long code;
                const unsigned char *q = number(at + 3, cend, &code), *nm;
                at += 3;
                if (!q)
                    continue;
                q = blank(q, cend);
                if (q >= cend || *q != '/')
                    continue;
                for (nm = ++q; q < cend && *q > 32; q++)
                    ;
                if (code >= 0 && code < 256)
                    for (g = 0; g < f->glyphs; g++)
                        if (f->glyph[g].name_len == (unsigned)(q - nm) && !memcmp(f->glyph[g].name, nm, (size_t)(q - nm))) {
                            f->enc[code] = (unsigned short)(g + 1);
                            break;
                        }
                at = q;
            }
        }
    }
    return f;
bad:
    psfont_close(f);
    return NULL;
}

void psfont_close(struct psfont *f)
{
    if (!f)
        return;
    free(f->uni);
    free(f->sid);
    free(f->priv);
    free(f->clear);
    free(f->plain);
    free(f->glyph);
    free(f->subr);
    free(f);
}

/* ---- glyphs found */

unsigned psfont_glyphs(const struct psfont *f) { return f->glyphs; }
int psfont_is_cid(const struct psfont *f) { return f->cid; }
int psfont_by_code(const struct psfont *f, unsigned code) { return code < 256 ? (int)f->enc[code] - 1 : -1; }

int psfont_by_unicode(const struct psfont *f, unsigned long u)
{
    unsigned g;
    for (g = 0; u && g < f->glyphs; g++)
        if (f->uni[g] == u)
            return (int)g;
    return -1;
}

unsigned long psfont_unicode(const struct psfont *f, int glyph)
{
    return glyph >= 0 && (unsigned)glyph < f->glyphs ? f->uni[glyph] : 0;
}

int psfont_by_cid(const struct psfont *f, unsigned cid)
{
    unsigned g;
    if (f->kind != 1)
        return -1;
    if (cid < f->glyphs && f->sid[cid] == cid)  /* (most often a glyph's number is its CID) */
        return (int)cid;
    for (g = 0; g < f->glyphs; g++)
        if (f->sid[g] == cid)
            return (int)g;
    return -1;
}

int psfont_by_name(const struct psfont *f, const char *name)
{
    size_t n = strlen(name);
    unsigned u = f->name_of ? f->name_of(name) : 0, g;
    for (g = 0; g < f->glyphs; g++) {
        if (f->kind == 2) {
            if (f->glyph[g].name_len == n && !memcmp(f->glyph[g].name, name, n))
                return (int)g;
        } else {
            const unsigned char *s;
            unsigned long len;
            if (own_string(f, f->sid[g], &s, &len) == 0 && len == n && !memcmp(s, name, n))
                return (int)g;
        }
    }
    return u ? psfont_by_unicode(f, u) : -1;
}

/* ---- the glyphs' programs run */

struct run {
    const struct psfont *f;
    const struct ttf_sink *s;
    const struct priv *pr;
    double st[STACK];
    int n;
    double x, y, sx, sy;                /* the current point, and the contour's first */
    int open, stems, seen_width, depth, done, bad;
    /* Type 1 */
    double ps[16];                      /* what a call out of the program gave back, for "pop" */
    int nps, flex, nflex;
    double fx[8], fy[8];
    double ox, oy;                      /* (an accent of a glyph put together: where it goes) */
};

static void r_close(struct run *r)
{
    if (r->open && (r->x != r->sx || r->y != r->sy))
        r->s->line(r->s->ctx, r->sx * r->f->scale, r->sy * r->f->scale);
    r->open = 0;
}

static void r_move(struct run *r, double dx, double dy)
{
    r->x += dx, r->y += dy;
    if (r->flex)                                /* (Type 1's flex: the points are only noted) */
        return;
    r_close(r);
    r->s->move(r->s->ctx, r->x * r->f->scale, r->y * r->f->scale);
    r->sx = r->x, r->sy = r->y, r->open = 1;
}

static void r_line(struct run *r, double dx, double dy)
{
    if (!r->open)
        r_move(r, 0, 0);
    r->x += dx, r->y += dy;
    r->s->line(r->s->ctx, r->x * r->f->scale, r->y * r->f->scale);
}

static void r_curve(struct run *r, double dx1, double dy1, double dx2, double dy2, double dx3, double dy3)
{
    double k = r->f->scale, ax, ay, bx, by;
    if (!r->open)
        r_move(r, 0, 0);
    ax = r->x + dx1, ay = r->y + dy1, bx = ax + dx2, by = ay + dy2;
    r->x = bx + dx3, r->y = by + dy3;
    if (r->s->cubic)
        r->s->cubic(r->s->ctx, ax * k, ay * k, bx * k, by * k, r->x * k, r->y * k);
    else
        r->s->line(r->s->ctx, r->x * k, r->y * k);
}

static int subr_bias(unsigned count) { return count < 1240 ? 107 : count < 33900 ? 1131 : 32768; }

static void width_first(struct run *r, int usual)       /* a Type 2 program's first numbers may begin with its width */
{
    if (!r->seen_width) {
        r->seen_width = 1;
        if (usual < 0 ? r->n & 1 : r->n > usual) {      /* (usual below 0: an even count is usual) */
            memmove(r->st, r->st + 1, (size_t)(r->n - 1) * sizeof r->st[0]);
            r->n--;
        }
    }
}

static void type2(struct run *r, const unsigned char *p, unsigned long len)
{
    const unsigned char *end = p + len;
    double *a = r->st;
    int i;
    if (r->depth > 10) {
        r->bad = 1;
        return;
    }
    while (p < end && !r->done && !r->bad) {
        unsigned b = *p++;
        if (b >= 32 || b == 28) {
            double v;
            if (b == 28) {
                if (end - p < 2)
                    break;
                v = (short)u16(p);
                p += 2;
            } else if (b < 247) {
                v = (int)b - 139;
            } else if (b < 255) {
                if (p >= end)
                    break;
                v = b < 251 ? (int)(b - 247) * 256 + *p + 108 : -(int)(b - 251) * 256 - *p - 108;
                p++;
            } else {
                if (end - p < 4)
                    break;
                v = (double)(short)u16(p) + u16(p + 2) / 65536.0;
                p += 4;
            }
            if (r->n == STACK) {
                r->bad = 1;
                break;
            }
            r->st[r->n++] = v;
            continue;
        }
        switch (b) {
        case 1: case 3: case 18: case 23:       /* hints: counted (the masks' lengths go by them), passed over */
            width_first(r, -1);
            r->stems += r->n / 2;
            r->n = 0;
            break;
        case 19: case 20:
            width_first(r, -1);
            r->stems += r->n / 2;
            r->n = 0;
            if ((unsigned long)(end - p) < (unsigned long)(r->stems + 7) / 8)
                r->bad = 1;
            p += (r->stems + 7) / 8;
            break;
        case 21:
            width_first(r, 2);
            if (r->n >= 2)
                r_move(r, a[r->n - 2], a[r->n - 1]);
            r->n = 0;
            break;
        case 22:
            width_first(r, 1);
            if (r->n >= 1)
                r_move(r, a[r->n - 1], 0);
            r->n = 0;
            break;
        case 4:
            width_first(r, 1);
            if (r->n >= 1)
                r_move(r, 0, a[r->n - 1]);
            r->n = 0;
            break;
        case 5:
            for (i = 0; i + 1 < r->n; i += 2)
                r_line(r, a[i], a[i + 1]);
            r->n = 0;
            break;
        case 6: case 7:                         /* across and up by turns */
            for (i = 0; i < r->n; i++)
                if ((b == 6) ^ (i & 1))
                    r_line(r, a[i], 0);
                else
                    r_line(r, 0, a[i]);
            r->n = 0;
            break;
        case 8:
            for (i = 0; i + 5 < r->n; i += 6)
                r_curve(r, a[i], a[i + 1], a[i + 2], a[i + 3], a[i + 4], a[i + 5]);
            r->n = 0;
            break;
        case 24:                                /* curves, then a line */
            for (i = 0; i + 7 < r->n; i += 6)
                r_curve(r, a[i], a[i + 1], a[i + 2], a[i + 3], a[i + 4], a[i + 5]);
            if (i + 1 < r->n)
                r_line(r, a[i], a[i + 1]);
            r->n = 0;
            break;
        case 25:                                /* lines, then a curve */
            for (i = 0; i + 7 < r->n; i += 2)
                r_line(r, a[i], a[i + 1]);
            if (i + 5 < r->n)
                r_curve(r, a[i], a[i + 1], a[i + 2], a[i + 3], a[i + 4], a[i + 5]);
            r->n = 0;
            break;
        case 26: {                              /* curves that begin and end going up */
            double dx1 = 0;
            i = 0;
            if (r->n & 1)
                dx1 = a[i++];
            for (; i + 3 < r->n; i += 4, dx1 = 0)
                r_curve(r, dx1, a[i], a[i + 1], a[i + 2], 0, a[i + 3]);
            r->n = 0;
            break;
        }
        case 27: {                              /* ... going across */
            double dy1 = 0;
            i = 0;
            if (r->n & 1)
                dy1 = a[i++];
            for (; i + 3 < r->n; i += 4, dy1 = 0)
                r_curve(r, a[i], dy1, a[i + 1], a[i + 2], a[i + 3], 0);
            r->n = 0;
            break;
        }
        case 30: case 31: {                     /* curves that begin up and end across, and the other way, by turns */
            int across = b == 31;
            for (i = 0; i + 3 < r->n; i += 4, across = !across) {
                double last = r->n - i == 5 ? a[i + 4] : 0;
                if (across)
                    r_curve(r, a[i], 0, a[i + 1], a[i + 2], last, a[i + 3]);
                else
                    r_curve(r, 0, a[i], a[i + 1], a[i + 2], a[i + 3], last);
            }
            r->n = 0;
            break;
        }
        case 10: case 29: {                     /* a subroutine: the font's own, or the file's */
            const struct index *ix = b == 10 ? &r->pr->subrs : &r->f->gsubrs;
            const unsigned char *sp;
            unsigned long sl;
            long which;
            if (r->n < 1) {
                r->bad = 1;
                break;
            }
            which = (long)r->st[--r->n] + subr_bias(ix->count);
            if (which < 0 || index_get(ix, (unsigned)which, &sp, &sl) != 0) {
                r->bad = 1;
                break;
            }
            r->depth++;
            type2(r, sp, sl);
            r->depth--;
            break;
        }
        case 11:
            return;
        case 14:
            width_first(r, 0);
            r_close(r);
            r->done = 1;
            break;
        case 12:
            if (p >= end) {
                r->bad = 1;
                break;
            }
            b = *p++;
            if (b == 34 && r->n >= 7) {         /* the flexes: two curves each, said four ways */
                r_curve(r, a[0], 0, a[1], a[2], a[3], 0);
                r_curve(r, a[4], 0, a[5], -a[2], a[6], 0);
            } else if (b == 35 && r->n >= 12) {
                r_curve(r, a[0], a[1], a[2], a[3], a[4], a[5]);
                r_curve(r, a[6], a[7], a[8], a[9], a[10], a[11]);
            } else if (b == 36 && r->n >= 9) {
                r_curve(r, a[0], a[1], a[2], a[3], a[4], 0);
                r_curve(r, a[5], 0, a[6], a[7], a[8], -(a[1] + a[3] + a[7]));
            } else if (b == 37 && r->n >= 11) {
                double dx = a[0] + a[2] + a[4] + a[6] + a[8], dy = a[1] + a[3] + a[5] + a[7] + a[9];
                int wide = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy);
                r_curve(r, a[0], a[1], a[2], a[3], a[4], a[5]);
                r_curve(r, a[6], a[7], a[8], a[9], wide ? a[10] : -dx, wide ? -dy : a[10]);
            } else if (b != 34 && b != 35 && b != 36 && b != 37) {
                r->bad = 1;                     /* (its arithmetic: no font of text uses it) */
            }
            r->n = 0;
            break;
        default:
            r->n = 0;
        }
    }
}

static int type1_glyph(struct run *r, unsigned g);

static void type1(struct run *r, const unsigned char *p, unsigned long len)
{
    const unsigned char *end = p + len;
    double *a = r->st;
    if (r->depth > 10) {
        r->bad = 1;
        return;
    }
    while (p < end && !r->done && !r->bad) {
        unsigned b = *p++;
        if (b >= 32) {
            double v;
            if (b < 247) {
                v = (int)b - 139;
            } else if (b < 255) {
                if (p >= end)
                    break;
                v = b < 251 ? (int)(b - 247) * 256 + *p + 108 : -(int)(b - 251) * 256 - *p - 108;
                p++;
            } else {
                if (end - p < 4)
                    break;
                v = (double)((unsigned long)u16(p) << 16 | u16(p + 2)) - (p[0] & 0x80 ? 4294967296.0 : 0);
                p += 4;
            }
            if (r->n == STACK) {
                r->bad = 1;
                break;
            }
            r->st[r->n++] = v;
            continue;
        }
        switch (b) {
        case 13:                                /* hsbw: where its left edge is (its width is the PDF's to say) */
            if (r->n >= 2)
                r->x = a[0] + r->ox, r->y = r->oy;
            r->n = 0;
            break;
        case 9:
            r_close(r);
            r->n = 0;
            break;
        case 21:
            if (r->n >= 2)
                r_move(r, a[r->n - 2], a[r->n - 1]);
            r->n = 0;
            break;
        case 22:
            if (r->n >= 1)
                r_move(r, a[r->n - 1], 0);
            r->n = 0;
            break;
        case 4:
            if (r->n >= 1)
                r_move(r, 0, a[r->n - 1]);
            r->n = 0;
            break;
        case 5:
            if (r->n >= 2)
                r_line(r, a[0], a[1]);
            r->n = 0;
            break;
        case 6:
            if (r->n >= 1)
                r_line(r, a[0], 0);
            r->n = 0;
            break;
        case 7:
            if (r->n >= 1)
                r_line(r, 0, a[0]);
            r->n = 0;
            break;
        case 8:
            if (r->n >= 6)
                r_curve(r, a[0], a[1], a[2], a[3], a[4], a[5]);
            r->n = 0;
            break;
        case 30:
            if (r->n >= 4)
                r_curve(r, 0, a[0], a[1], a[2], a[3], 0);
            r->n = 0;
            break;
        case 31:
            if (r->n >= 4)
                r_curve(r, a[0], 0, a[1], a[2], 0, a[3]);
            r->n = 0;
            break;
        case 10: {
            long which;
            if (r->n < 1) {
                r->bad = 1;
                break;
            }
            which = (long)r->st[--r->n];
            if (which < 0 || (unsigned long)which >= r->f->nsubr || !r->f->subr[which].p) {
                r->bad = 1;
                break;
            }
            r->depth++;
            type1(r, r->f->subr[which].p, r->f->subr[which].len);
            r->depth--;
            break;
        }
        case 11:
            return;
        case 14:
            r_close(r);
            r->done = 1;
            break;
        case 12:
            if (p >= end) {
                r->bad = 1;
                break;
            }
            b = *p++;
            if (b == 7 && r->n >= 4) {          /* sbw */
                r->x = a[0] + r->ox, r->y = a[1] + r->oy;
                r->n = 0;
            } else if (b == 12 && r->n >= 2) {  /* div */
                a[r->n - 2] = a[r->n - 1] != 0 ? a[r->n - 2] / a[r->n - 1] : 0;
                r->n--;
            } else if (b == 16 && r->n >= 2) {  /* a call out of the program: flex's three, and hints' */
                int which = (int)a[r->n - 1], args = (int)a[r->n - 2], k;
                r->n -= 2;
                if (args < 0 || args > r->n) {
                    r->bad = 1;
                    break;
                }
                if (which == 1) {               /* a flex begins: the moves after it are its points */
                    r->flex = 1, r->nflex = 0;
                    r->fx[7] = r->x, r->fy[7] = r->y;   /* (where it begins) */
                } else if (which == 2) {
                    if (r->nflex < 7)
                        r->fx[r->nflex] = r->x, r->fy[r->nflex] = r->y, r->nflex++;
                } else if (which == 0) {        /* ... and ends: two curves through the six points after the first
                                                   (which only says where the flex's middle is) */
                    r->flex = 0;
                    if (r->nflex >= 7) {
                        double ex = r->x, ey = r->y;
                        r->x = r->fx[7], r->y = r->fy[7];
                        r_curve(r, r->fx[1] - r->x, r->fy[1] - r->y, r->fx[2] - r->fx[1], r->fy[2] - r->fy[1], r->fx[3] - r->fx[2],
                                r->fy[3] - r->fy[2]);
                        r_curve(r, r->fx[4] - r->fx[3], r->fy[4] - r->fy[3], r->fx[5] - r->fx[4], r->fy[5] - r->fy[4],
                                r->fx[6] - r->fx[5], r->fy[6] - r->fy[5]);
                        r->x = ex, r->y = ey;
                    }
                }
                for (r->nps = 0, k = 0; k < args && r->nps < 16; k++)   /* (its numbers are there for "pop") */
                    r->ps[r->nps++] = a[r->n - 1 - k];
                r->n -= args;
            } else if (b == 17) {               /* pop */
                if (r->n < STACK)
                    r->st[r->n++] = r->nps ? r->ps[--r->nps] : 0;
            } else if (b == 33) {               /* setcurrentpoint: after a flex, where the point is already */
                r->n = 0;
            } else if (b == 6 && r->n >= 5) {   /* seac: a letter and its accent, each a glyph of the standard encoding */
                double asb = a[0], adx = a[1], ady = a[2], left = r->x - r->ox;
                int base = psfont_by_unicode(r->f, sid_unicode((unsigned)a[3] > 31 && (unsigned)a[3] < 127 ? (unsigned)a[3] - 31 : 0));
                int accent = -1;
                unsigned code = (unsigned)a[4], k;
                static const unsigned char codes[] = { 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF };
                static const unsigned short marks[] = { 0x60, 0xB4, 0x2C6, 0x2DC, 0xAF, 0x2D8, 0x2D9, 0xA8, 0x2DA, 0xB8, 0x2DD, 0x2DB,
                                                        0x2C7 };
                for (k = 0; k < sizeof codes; k++)
                    if (codes[k] == code)
                        accent = psfont_by_unicode(r->f, marks[k]);
                r->n = 0;
                r_close(r);
                if (r->depth < 2) {
                    double ox = r->ox, oy = r->oy;
                    r->depth += 5;
                    if (base >= 0)
                        type1_glyph(r, (unsigned)base);
                    r->done = 0;
                    r->ox = ox + left + adx - asb, r->oy = oy + ady;
                    if (accent >= 0)
                        type1_glyph(r, (unsigned)accent);
                    r->ox = ox, r->oy = oy;
                    r->depth -= 5;
                }
                r->done = 1;
            } else {
                r->n = 0;                       /* (dotsection, the stems in threes: hints) */
            }
            break;
        default:
            r->n = 0;                           /* (hstem, vstem) */
        }
    }
}

static int type1_glyph(struct run *r, unsigned g)
{
    r->n = 0, r->nps = 0, r->flex = 0;
    type1(r, r->f->glyph[g].p, r->f->glyph[g].len);
    r_close(r);
    return r->bad ? -1 : 0;
}

int psfont_outline(const struct psfont *f, int glyph, const struct ttf_sink *s)
{
    struct run *r;
    int bad;
    if (glyph < 0 || (unsigned)glyph >= f->glyphs || !(r = calloc(1, sizeof *r)))
        return -1;
    r->f = f, r->s = s;
    if (f->kind == 1) {
        const unsigned char *p;
        unsigned long len;
        r->pr = &f->priv[fd_of(f, (unsigned)glyph)];
        if (index_get(&f->chars, (unsigned)glyph, &p, &len) != 0)
            r->bad = 1;
        else
            type2(r, p, len);
        r_close(r);
    } else {
        type1_glyph(r, (unsigned)glyph);
    }
    bad = r->bad;
    free(r);
    return bad ? -1 : 0;
}
