/* inflate.c: see inflate.h. Huffman codes are looked up 9 bits at a time in a table (a code that long or
 * shorter in one step, as nearly all are), longer ones bit by bit by their canonical order. Everything a stream
 * being unpacked needs is in one structure, the caller's (inflate_stream_r) or the one here (inflate_stream). */
#include "inflate.h"

#define FAST 9
#define MAXBITS 15

struct huff {
    unsigned short fast[1 << FAST];     /* (length << 12) | symbol for the codes of FAST bits or fewer; 0: longer */
    unsigned short count[MAXBITS + 1];  /* codes of each length */
    unsigned short symbol[288];         /* symbols in their codes' order */
};

struct infl {
    struct huff lencode, distcode;
    unsigned char lengths[320];
    infl_fill fill;
    infl_flush flush;
    void *ctx;
    const unsigned char *in;
    unsigned in_n;
    unsigned long bits;                 /* the stream's next bits, the first in bit 0 */
    unsigned nbits;
    unsigned char INFL_FAR *win;
    unsigned wpos;
    int full;                           /* the window has been filled once: every distance reaches back */
    int err;
};

static void need(struct infl *z)                  /* 25 bits or more, while the stream has them */
{
    while (z->nbits <= 24) {
        if (!z->in_n) {
            z->in_n = z->fill(&z->in, z->ctx);
            if (!z->in_n)
                return;
        }
        z->bits |= (unsigned long)*z->in++ << z->nbits;
        z->nbits += 8;
        z->in_n--;
    }
}

static void drop(struct infl *z, unsigned n)
{
    if (n > z->nbits) {                  /* (past the stream's end) */
        z->err = -1;
        z->nbits = 0;
        z->bits = 0;
        return;
    }
    z->bits >>= n;
    z->nbits -= n;
}

static unsigned take(struct infl *z, unsigned n)        /* n bits, 16 at most */
{
    unsigned v;
    need(z);
    v = (unsigned)(z->bits & ((1UL << n) - 1));
    drop(z, n);
    return v;
}

static void put(struct infl *z, unsigned char c)
{
    z->win[z->wpos++] = c;
    if (z->wpos == INFL_WINDOW) {
        if (!z->err && z->flush(INFL_WINDOW, z->ctx) != 0)
            z->err = -2;
        z->wpos = 0;
        z->full = 1;
    }
}

/* The table for n code z->lengths: 0, or -1 for z->lengths no code can have. */
static int build(struct huff *h, const unsigned char *len, unsigned n)
{
    unsigned short offs[MAXBITS + 1];
    unsigned i, l, code, idx, k;
    long left = 1;                      /* (16 bits would overflow at the longest codes) */
    for (l = 0; l <= MAXBITS; l++)
        h->count[l] = 0;
    for (i = 0; i < n; i++)
        h->count[len[i]]++;
    h->count[0] = 0;
    for (l = 1; l <= MAXBITS; l++) {
        left <<= 1;
        left -= (long)h->count[l];
        if (left < 0)
            return -1;                  /* more codes than the z->lengths have room for */
    }
    offs[1] = 0;
    for (l = 1; l < MAXBITS; l++)
        offs[l + 1] = (unsigned short)(offs[l] + h->count[l]);
    for (i = 0; i < n; i++)
        if (len[i])
            h->symbol[offs[len[i]]++] = (unsigned short)i;
    for (i = 0; i < (1u << FAST); i++)
        h->fast[i] = 0;
    code = idx = 0;
    for (l = 1; l <= FAST; l++) {
        for (k = 0; k < h->count[l]; k++, code++, idx++) {
            unsigned rev = 0, b, j;
            for (b = 0; b < l; b++)     /* (the stream has a code's first bit first: the table's index is it reversed) */
                rev |= ((code >> b) & 1u) << (l - 1 - b);
            for (j = rev; j < (1u << FAST); j += 1u << l)
                h->fast[j] = (unsigned short)((l << 12) | h->symbol[idx]);
        }
        code <<= 1;
    }
    return 0;
}

static unsigned decode(struct infl *z, const struct huff *h)
{
    unsigned e, code, first, index, len, count;
    need(z);
    e = h->fast[(unsigned)z->bits & ((1u << FAST) - 1)];
    if (e) {
        drop(z, e >> 12);
        return e & 0x0FFF;
    }
    code = first = index = 0;
    for (len = 1; len <= MAXBITS; len++) {
        code |= (unsigned)(z->bits >> (len - 1)) & 1u;
        count = h->count[len];
        if (code - first < count) {
            drop(z, len);
            return h->symbol[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    z->err = -1;
    return 0;
}

static void codes(struct infl *z)                 /* a block's symbols, by lencode and distcode */
{
    static const unsigned short lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67,
                                              83, 99, 115, 131, 163, 195, 227, 258 };
    static const unsigned char lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5,
                                            5, 0 };
    static const unsigned short dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                              1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
    static const unsigned char dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11,
                                            12, 12, 13, 13 };
    unsigned sym, len, dist, src;
    while (!z->err) {
        sym = decode(z, &z->lencode);
        if (sym < 256) {
            put(z, (unsigned char)sym);
        } else if (sym == 256) {
            return;
        } else {
            sym -= 257;
            if (sym >= 29) {
                z->err = -1;
                return;
            }
            len = lbase[sym] + take(z, lext[sym]);
            sym = decode(z, &z->distcode);
            if (sym >= 30) {
                z->err = -1;
                return;
            }
            dist = dbase[sym] + take(z, dext[sym]);
            if (dist > INFL_WINDOW || (!z->full && dist > z->wpos)) {
                z->err = -1;             /* before the stream's start */
                return;
            }
            src = (z->wpos - dist) & (INFL_WINDOW - 1);
            while (len-- && !z->err) {
                put(z, z->win[src]);
                src = (src + 1) & (INFL_WINDOW - 1);
            }
        }
    }
}

static void stored(struct infl *z)
{
    unsigned len, nlen;
    drop(z, z->nbits & 7);                  /* (to a byte's start) */
    len = take(z, 16);
    nlen = take(z, 16);
    if (len != (unsigned)(~nlen & 0xFFFFu)) {
        z->err = -1;
        return;
    }
    while (len-- && !z->err)
        put(z, (unsigned char)take(z, 8));
}

static void fixed(struct infl *z)
{
    unsigned i;
    for (i = 0; i < 144; i++) z->lengths[i] = 8;
    for (; i < 256; i++) z->lengths[i] = 9;
    for (; i < 280; i++) z->lengths[i] = 7;
    for (; i < 288; i++) z->lengths[i] = 8;
    build(&z->lencode, z->lengths, 288);
    for (i = 0; i < 30; i++) z->lengths[i] = 5;
    build(&z->distcode, z->lengths, 30);
    codes(z);
}

static void dynamic(struct infl *z)
{
    static const unsigned char order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    unsigned nlen = take(z, 5) + 257, ndist = take(z, 5) + 1, ncode = take(z, 4) + 4, i, sym, rep, prev;
    if (nlen > 286 || ndist > 30) {
        z->err = -1;
        return;
    }
    for (i = 0; i < 19; i++)
        z->lengths[i] = 0;
    for (i = 0; i < ncode; i++)
        z->lengths[order[i]] = (unsigned char)take(z, 3);
    if (build(&z->lencode, z->lengths, 19) != 0) {
        z->err = -1;
        return;
    }
    for (i = 0; i < nlen + ndist && !z->err; ) {
        sym = decode(z, &z->lencode);
        if (sym < 16) {
            z->lengths[i++] = (unsigned char)sym;
            continue;
        }
        prev = 0;
        if (sym == 16) {
            if (!i) {
                z->err = -1;
                return;
            }
            prev = z->lengths[i - 1];
            rep = 3 + take(z, 2);
        } else if (sym == 17) {
            rep = 3 + take(z, 3);
        } else {
            rep = 11 + take(z, 7);
        }
        if (i + rep > nlen + ndist) {
            z->err = -1;
            return;
        }
        while (rep--)
            z->lengths[i++] = (unsigned char)prev;
    }
    if (z->err || !z->lengths[256] || build(&z->lencode, z->lengths, nlen) != 0
        || build(&z->distcode, z->lengths + nlen, ndist) != 0) {
        z->err = -1;
        return;
    }
    codes(z);
}

int inflate_stream_r(struct infl *z, infl_fill fill, infl_flush flush, void *ctx, unsigned char INFL_FAR *window)
{
    unsigned last, type;
    z->fill = fill, z->flush = flush, z->ctx = ctx;
    z->in = 0, z->in_n = 0;
    z->bits = 0, z->nbits = 0;
    z->win = window, z->wpos = 0, z->full = 0, z->err = 0;
    do {
        last = take(z, 1);
        type = take(z, 2);
        if (z->err)
            break;
        if (type == 0)
            stored(z);
        else if (type == 1)
            fixed(z);
        else if (type == 2)
            dynamic(z);
        else
            z->err = -1;
    } while (!last && !z->err);
    if (!z->err && z->wpos && z->flush(z->wpos, z->ctx) != 0)
        z->err = -2;
    return z->err;
}

unsigned inflate_state_size(void) { return (unsigned)sizeof(struct infl); }

int inflate_stream(infl_fill fill, infl_flush flush, void *ctx, unsigned char INFL_FAR *window)
{
    static struct infl one;
    return inflate_stream_r(&one, fill, flush, ctx, window);
}
