/* inflate.c: see inflate.h. Huffman codes are looked up 9 bits at a time in a table (a code that long or
 * shorter in one step, as nearly all are), longer ones bit by bit by their canonical order. */
#include "inflate.h"

#define FAST 9
#define MAXBITS 15

struct huff {
    unsigned short fast[1 << FAST];     /* (length << 12) | symbol for the codes of FAST bits or fewer; 0: longer */
    unsigned short count[MAXBITS + 1];  /* codes of each length */
    unsigned short symbol[288];         /* symbols in their codes' order */
};

static struct huff lencode, distcode;
static unsigned char lengths[320];

static struct {
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
} s;

static void need(void)                  /* 25 bits or more, while the stream has them */
{
    while (s.nbits <= 24) {
        if (!s.in_n) {
            s.in_n = s.fill(&s.in, s.ctx);
            if (!s.in_n)
                return;
        }
        s.bits |= (unsigned long)*s.in++ << s.nbits;
        s.nbits += 8;
        s.in_n--;
    }
}

static void drop(unsigned n)
{
    if (n > s.nbits) {                  /* (past the stream's end) */
        s.err = -1;
        s.nbits = 0;
        s.bits = 0;
        return;
    }
    s.bits >>= n;
    s.nbits -= n;
}

static unsigned take(unsigned n)        /* n bits, 16 at most */
{
    unsigned v;
    need();
    v = (unsigned)(s.bits & ((1UL << n) - 1));
    drop(n);
    return v;
}

static void put(unsigned char c)
{
    s.win[s.wpos++] = c;
    if (s.wpos == INFL_WINDOW) {
        if (!s.err && s.flush(INFL_WINDOW, s.ctx) != 0)
            s.err = -2;
        s.wpos = 0;
        s.full = 1;
    }
}

/* The table for n code lengths: 0, or -1 for lengths no code can have. */
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
            return -1;                  /* more codes than the lengths have room for */
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

static unsigned decode(const struct huff *h)
{
    unsigned e, code, first, index, len, count;
    need();
    e = h->fast[(unsigned)s.bits & ((1u << FAST) - 1)];
    if (e) {
        drop(e >> 12);
        return e & 0x0FFF;
    }
    code = first = index = 0;
    for (len = 1; len <= MAXBITS; len++) {
        code |= (unsigned)(s.bits >> (len - 1)) & 1u;
        count = h->count[len];
        if (code - first < count) {
            drop(len);
            return h->symbol[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s.err = -1;
    return 0;
}

static void codes(void)                 /* a block's symbols, by lencode and distcode */
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
    while (!s.err) {
        sym = decode(&lencode);
        if (sym < 256) {
            put((unsigned char)sym);
        } else if (sym == 256) {
            return;
        } else {
            sym -= 257;
            if (sym >= 29) {
                s.err = -1;
                return;
            }
            len = lbase[sym] + take(lext[sym]);
            sym = decode(&distcode);
            if (sym >= 30) {
                s.err = -1;
                return;
            }
            dist = dbase[sym] + take(dext[sym]);
            if (dist > INFL_WINDOW || (!s.full && dist > s.wpos)) {
                s.err = -1;             /* before the stream's start */
                return;
            }
            src = (s.wpos - dist) & (INFL_WINDOW - 1);
            while (len-- && !s.err) {
                put(s.win[src]);
                src = (src + 1) & (INFL_WINDOW - 1);
            }
        }
    }
}

static void stored(void)
{
    unsigned len, nlen;
    drop(s.nbits & 7);                  /* (to a byte's start) */
    len = take(16);
    nlen = take(16);
    if (len != (unsigned)(~nlen & 0xFFFFu)) {
        s.err = -1;
        return;
    }
    while (len-- && !s.err)
        put((unsigned char)take(8));
}

static void fixed(void)
{
    unsigned i;
    for (i = 0; i < 144; i++) lengths[i] = 8;
    for (; i < 256; i++) lengths[i] = 9;
    for (; i < 280; i++) lengths[i] = 7;
    for (; i < 288; i++) lengths[i] = 8;
    build(&lencode, lengths, 288);
    for (i = 0; i < 30; i++) lengths[i] = 5;
    build(&distcode, lengths, 30);
    codes();
}

static void dynamic(void)
{
    static const unsigned char order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    unsigned nlen = take(5) + 257, ndist = take(5) + 1, ncode = take(4) + 4, i, sym, rep, prev;
    if (nlen > 286 || ndist > 30) {
        s.err = -1;
        return;
    }
    for (i = 0; i < 19; i++)
        lengths[i] = 0;
    for (i = 0; i < ncode; i++)
        lengths[order[i]] = (unsigned char)take(3);
    if (build(&lencode, lengths, 19) != 0) {
        s.err = -1;
        return;
    }
    for (i = 0; i < nlen + ndist && !s.err; ) {
        sym = decode(&lencode);
        if (sym < 16) {
            lengths[i++] = (unsigned char)sym;
            continue;
        }
        prev = 0;
        if (sym == 16) {
            if (!i) {
                s.err = -1;
                return;
            }
            prev = lengths[i - 1];
            rep = 3 + take(2);
        } else if (sym == 17) {
            rep = 3 + take(3);
        } else {
            rep = 11 + take(7);
        }
        if (i + rep > nlen + ndist) {
            s.err = -1;
            return;
        }
        while (rep--)
            lengths[i++] = (unsigned char)prev;
    }
    if (s.err || !lengths[256] || build(&lencode, lengths, nlen) != 0 || build(&distcode, lengths + nlen, ndist) != 0) {
        s.err = -1;
        return;
    }
    codes();
}

int inflate_stream(infl_fill fill, infl_flush flush, void *ctx, unsigned char INFL_FAR *window)
{
    unsigned last, type;
    s.fill = fill, s.flush = flush, s.ctx = ctx;
    s.in = 0, s.in_n = 0;
    s.bits = 0, s.nbits = 0;
    s.win = window, s.wpos = 0, s.full = 0, s.err = 0;
    do {
        last = take(1);
        type = take(2);
        if (s.err)
            break;
        if (type == 0)
            stored();
        else if (type == 1)
            fixed();
        else if (type == 2)
            dynamic();
        else
            s.err = -1;
    } while (!last && !s.err);
    if (!s.err && s.wpos && s.flush(s.wpos, s.ctx) != 0)
        s.err = -2;
    return s.err;
}
