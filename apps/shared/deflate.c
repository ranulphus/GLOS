/* deflate.c: see deflate.h. The bytes are kept in a window of 64 KB: the 32 KB already coded, and what is to be.
   For each place the earlier places with the same three bytes are tried, newest first (a hash's chain), for the
   longest run of the same bytes; what is found is a length and a distance, what is not a byte as it is. A block's
   symbols are counted, and it is written with Huffman codes made from the counts (dynamic), with the format's
   own (fixed), or not coded at all (stored), whichever comes shortest. */
#include <string.h>

#include "deflate.h"

#define WSIZE 32768u
#define HBITS 15
#define HSIZE (1u << HBITS)
#define MINM 3u
#define MAXM 258u
#define LOOK (MAXM + MINM + 1)          /* what must lie ahead of a place before it is coded (but at the end) */
#define SYMS 16384u                     /* a block's symbols at most */
#define NIL 0xFFFFu
#define OUT 4096u
#define FAR 4096u                       /* three bytes found further back than this are cheaper as they are */

static const unsigned short lbase[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67,
                                          83, 99, 115, 131, 163, 195, 227, 258 };
static const unsigned char lext[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5,
                                        5, 0 };
static const unsigned short dbase[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                          1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const unsigned char dext[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11,
                                        12, 12, 13, 13 };
static const unsigned char order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

struct defl {
    int (*put)(void *ctx, const void *p, unsigned n);
    void *ctx;
    int err;
    unsigned chain, nice, lazy, insert; /* how hard matches are looked for (deflate_begin) */
    unsigned fill, pos, block;          /* bytes in win; the next to code; where the block in hand began */
    unsigned nsym;
    unsigned long bits;                 /* bits not yet a whole byte, the first in bit 0 */
    unsigned nbits;
    unsigned nout;
    unsigned short lfreq[286], dfreq[30];
    unsigned char len_sym[256];         /* a length's symbol less 257, by the length less 3 */
    unsigned char dist_lo[256];         /* a distance's symbol: by the distance less 1, below 257 ... */
    unsigned char dist_hi[256];         /* ... and by that over 128, above */
    unsigned short head[HSIZE];         /* the newest place of each hash */
    unsigned short prev[WSIZE];         /* the place before a place with its hash */
    unsigned short lit[SYMS];           /* the block's symbols: a byte, or a length ... */
    unsigned short dist[SYMS];          /* ... and its distance (0: a byte) */
    unsigned char win[2 * WSIZE];
    unsigned char out[OUT];
};

unsigned deflate_state_size(void) { return (unsigned)sizeof(struct defl); }

/* ---- bits out */

static void flush_out(struct defl *d)
{
    if (d->nout && !d->err)
        d->err = d->put(d->ctx, d->out, d->nout);
    d->nout = 0;
}

static void put_bits(struct defl *d, unsigned v, unsigned n)    /* 16 at most */
{
    d->bits |= (unsigned long)v << d->nbits;
    d->nbits += n;
    while (d->nbits >= 8) {
        d->out[d->nout++] = (unsigned char)d->bits;
        if (d->nout == OUT)
            flush_out(d);
        d->bits >>= 8;
        d->nbits -= 8;
    }
}

/* ---- Huffman codes */

/* The code lengths for n symbols counted in freq, none longer than maxbits; two symbols at least get a code
   (a decoder wants a whole code). */
static void make_lengths(const unsigned short *freq, unsigned n, unsigned maxbits, unsigned char *len)
{
    unsigned short sym[288];
    unsigned long weight[2 * 288];
    unsigned short parent[2 * 288];
    unsigned char depth[2 * 288];
    unsigned count[16];
    unsigned m = 0, i, j, leaf, inner, made, l;
    unsigned long total;
    memset(len, 0, n);
    for (i = 0; i < n; i++)
        if (freq[i])
            sym[m++] = (unsigned short)i;
    for (i = 0; m < 2; i++)             /* (a symbol or two that never come: 0 and 1, as zlib has it) */
        if (!freq[i])
            sym[m++] = (unsigned short)i;
    for (i = 1; i < m; i++) {           /* by count, the rarest first (and by number among equals) */
        unsigned short s = sym[i];
        for (j = i; j > 0 && (freq[sym[j - 1]] > freq[s] || (freq[sym[j - 1]] == freq[s] && sym[j - 1] > s)); j--)
            sym[j] = sym[j - 1];
        sym[j] = s;
    }
    for (i = 0; i < m; i++)
        weight[i] = freq[sym[i]] ? freq[sym[i]] : 1;
    /* The tree: the two lightest of what is left are joined, m - 1 times. Leaves are taken in their order, the
       joints in the order they were made (they come no lighter as they go). */
    leaf = 0, inner = m, made = m;
    while (made < 2 * m - 1) {
        unsigned a, b;
        a = (leaf < m && (inner >= made || weight[leaf] <= weight[inner])) ? leaf++ : inner++;
        b = (leaf < m && (inner >= made || weight[leaf] <= weight[inner])) ? leaf++ : inner++;
        weight[made] = weight[a] + weight[b];
        parent[a] = parent[b] = (unsigned short)made;
        made++;
    }
    depth[2 * m - 2] = 0;
    for (i = 2 * m - 2; i-- > 0; ) {
        unsigned dd = depth[parent[i]] + 1u;
        depth[i] = (unsigned char)(dd > 255 ? 255 : dd);
    }
    memset(count, 0, sizeof count);
    for (i = 0; i < m; i++)
        count[depth[i] > maxbits ? maxbits : depth[i]]++;
    /* Codes that came out too long were counted at the longest length, which that length has no room for: one of
       them is taken away and a shorter code is split in two, until all fit. */
    for (total = 0, l = 1; l <= maxbits; l++)
        total += (unsigned long)count[l] << (maxbits - l);
    while (total > 1ul << maxbits) {
        count[maxbits]--;
        for (l = maxbits - 1; l > 0; l--)
            if (count[l]) {
                count[l]--;
                count[l + 1] += 2;
                break;
            }
        total--;
    }
    for (l = maxbits, i = 0; l >= 1; l--)       /* the rarest get the longest */
        for (j = 0; j < count[l]; j++)
            len[sym[i++]] = (unsigned char)l;
}

static void make_codes(const unsigned char *len, unsigned n, unsigned short *code)
{
    unsigned count[16], next[16], i, c = 0;
    memset(count, 0, sizeof count);
    for (i = 0; i < n; i++)
        count[len[i]]++;
    count[0] = 0;
    for (i = 1; i < 16; i++) {
        c = (c + count[i - 1]) << 1;
        next[i] = c;
    }
    for (i = 0; i < n; i++) {
        unsigned v = len[i] ? next[len[i]]++ : 0, r = 0, b;
        for (b = 0; b < len[i]; b++)    /* (a code's first bit goes out first) */
            r |= ((v >> b) & 1u) << (len[i] - 1 - b);
        code[i] = (unsigned short)r;
    }
}

/* ---- a block written */

static unsigned dist_sym(const struct defl *d, unsigned dist)
{
    return dist <= 256 ? d->dist_lo[dist - 1] : d->dist_hi[(dist - 1) >> 7];
}

static void put_symbols(struct defl *d, const unsigned short *lcode, const unsigned char *llen, const unsigned short *dcode,
                        const unsigned char *dlen)
{
    unsigned i;
    for (i = 0; i < d->nsym; i++) {
        unsigned v = d->lit[i], dist = d->dist[i];
        if (!dist) {
            put_bits(d, lcode[v], llen[v]);
        } else {
            unsigned ls = d->len_sym[v - 3], ds = dist_sym(d, dist);
            put_bits(d, lcode[257 + ls], llen[257 + ls]);
            if (lext[ls])
                put_bits(d, v - lbase[ls], lext[ls]);
            put_bits(d, dcode[ds], dlen[ds]);
            if (dext[ds])
                put_bits(d, dist - dbase[ds], dext[ds]);
        }
    }
    put_bits(d, lcode[256], llen[256]);
}

/* The lengths of the two codes as the block's head has them: runs of a length told once (16: the one before,
   three to six times more; 17 and 18: zeros). out: the symbols; extra: each one's extra bits' value. How many. */
static unsigned run_lengths(const unsigned char *len, unsigned n, unsigned char *out, unsigned char *extra,
                            unsigned short *freq)
{
    unsigned i = 0, o = 0;
    memset(freq, 0, 19 * sizeof *freq);
    while (i < n) {
        unsigned v = len[i], run = 1;
        while (i + run < n && len[i + run] == v)
            run++;
        i += run;
        if (!v) {
            while (run >= 3) {
                unsigned k = run > 138 ? 138 : run;
                out[o] = (unsigned char)(k >= 11 ? 18 : 17);
                extra[o] = (unsigned char)(k >= 11 ? k - 11 : k - 3);
                freq[out[o++]]++;
                run -= k;
            }
        } else {
            out[o] = (unsigned char)v, extra[o] = 0;
            freq[out[o++]]++;
            run--;
            while (run >= 3) {
                unsigned k = run > 6 ? 6 : run;
                out[o] = 16, extra[o] = (unsigned char)(k - 3);
                freq[out[o++]]++;
                run -= k;
            }
        }
        while (run--) {
            out[o] = (unsigned char)v, extra[o] = 0;
            freq[out[o++]]++;
        }
    }
    return o;
}

static void end_block(struct defl *d, int last)
{
    static const unsigned char clextra[19] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 7 };
    unsigned char llen[288], dlen[30], cllen[19], all[286 + 30], runs[286 + 30], rextra[286 + 30];
    unsigned short lcode[288], dcode[30], clcode[19], clfreq[19];
    unsigned raw = d->pos - d->block, nl, nd, ncl, nr, i;
    unsigned long dyn, fix, sto;
    d->lfreq[256] = 1;
    make_lengths(d->lfreq, 286, 15, llen);
    make_lengths(d->dfreq, 30, 15, dlen);
    for (nl = 286; nl > 257 && !llen[nl - 1]; nl--)
        ;
    for (nd = 30; nd > 1 && !dlen[nd - 1]; nd--)
        ;
    memcpy(all, llen, nl);
    memcpy(all + nl, dlen, nd);
    nr = run_lengths(all, nl + nd, runs, rextra, clfreq);
    make_lengths(clfreq, 19, 7, cllen);
    for (ncl = 19; ncl > 4 && !cllen[order[ncl - 1]]; ncl--)
        ;
    /* What each way of writing it comes to, in bits. */
    dyn = 3 + 5 + 5 + 4 + 3ul * ncl;
    for (i = 0; i < 19; i++)
        dyn += (unsigned long)clfreq[i] * (cllen[i] + clextra[i]);
    fix = 3;
    for (i = 0; i < 286; i++) {
        unsigned ex = i >= 257 ? lext[i - 257] : 0;
        dyn += (unsigned long)d->lfreq[i] * (llen[i] + ex);
        fix += (unsigned long)d->lfreq[i] * ((i < 144 ? 8u : i < 256 ? 9u : i < 280 ? 7u : 8u) + ex);
    }
    for (i = 0; i < 30; i++) {
        dyn += (unsigned long)d->dfreq[i] * (dlen[i] + dext[i]);
        fix += (unsigned long)d->dfreq[i] * (5u + dext[i]);
    }
    sto = raw <= 65535u ? 3 + ((8 - (d->nbits + 3) % 8) % 8) + 32 + 8ul * raw : ~0ul;
    if (sto < dyn && sto < fix) {
        put_bits(d, (unsigned)last, 3);                 /* (the kind's two bits are 00) */
        if (d->nbits)
            put_bits(d, 0, 8 - d->nbits);
        put_bits(d, raw, 16);
        put_bits(d, ~raw & 0xFFFFu, 16);
        for (i = 0; i < raw; i++)
            put_bits(d, d->win[d->block + i], 8);
    } else if (fix <= dyn) {
        for (i = 0; i < 288; i++)
            llen[i] = (unsigned char)(i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
        memset(dlen, 5, 30);
        make_codes(llen, 288, lcode);
        make_codes(dlen, 30, dcode);
        put_bits(d, (unsigned)last | 2u, 3);
        put_symbols(d, lcode, llen, dcode, dlen);
    } else {
        make_codes(llen, 286, lcode);
        make_codes(dlen, 30, dcode);
        make_codes(cllen, 19, clcode);
        put_bits(d, (unsigned)last | 4u, 3);
        put_bits(d, nl - 257, 5);
        put_bits(d, nd - 1, 5);
        put_bits(d, ncl - 4, 4);
        for (i = 0; i < ncl; i++)
            put_bits(d, cllen[order[i]], 3);
        for (i = 0; i < nr; i++) {
            put_bits(d, clcode[runs[i]], cllen[runs[i]]);
            if (clextra[runs[i]])
                put_bits(d, rextra[i], clextra[runs[i]]);
        }
        put_symbols(d, lcode, llen, dcode, dlen);
    }
    memset(d->lfreq, 0, sizeof d->lfreq);
    memset(d->dfreq, 0, sizeof d->dfreq);
    d->nsym = 0;
    d->block = d->pos;
}

/* ---- matches */

static unsigned hash3(const unsigned char *p)
{
    return ((unsigned)p[0] << 10 ^ (unsigned)p[1] << 5 ^ p[2]) & (HSIZE - 1);
}

static void insert(struct defl *d, unsigned at)         /* (three bytes are there) */
{
    unsigned h = hash3(d->win + at);
    d->prev[at & (WSIZE - 1)] = d->head[h];
    d->head[h] = (unsigned short)at;
}

/* The longest run at `at` that an earlier place has too: its length (0: none worth having) and how far back. */
static unsigned find(const struct defl *d, unsigned at, unsigned *dist)
{
    const unsigned char *w = d->win, *here = w + at;
    unsigned max = d->fill - at < MAXM ? d->fill - at : MAXM, best = MINM - 1, bdist = 0, left = d->chain;
    unsigned limit = at > WSIZE ? at - WSIZE : 0, cur;
    if (max < MINM)
        return 0;
    cur = d->head[hash3(here)];
    while (cur != NIL && cur < at && cur >= limit && left--) {
        const unsigned char *there = w + cur;
        unsigned next;
        if (there[best] == here[best] && there[0] == here[0]) {
            unsigned n = 0;
            while (n < max && there[n] == here[n])
                n++;
            if (n > best) {
                best = n, bdist = at - cur;
                if (n >= d->nice || n == max)
                    break;
            }
        }
        next = d->prev[cur & (WSIZE - 1)];
        if (next == NIL || next >= cur)                 /* (the chain's end, or a newer place's in its slot) */
            break;
        cur = next;
    }
    if (best < MINM || (best == MINM && bdist > FAR))
        return 0;
    *dist = bdist;
    return best;
}

static void symbol(struct defl *d, unsigned v, unsigned dist)
{
    d->lit[d->nsym] = (unsigned short)v;
    d->dist[d->nsym++] = (unsigned short)dist;
    if (dist) {
        d->lfreq[257 + d->len_sym[v - 3]]++;
        d->dfreq[dist_sym(d, dist)]++;
    } else {
        d->lfreq[v]++;
    }
}

/* What is in the window coded, as far as there is enough ahead of it to choose well (all of it: at the end). */
static void compress(struct defl *d, int finish)
{
    for (;;) {
        unsigned avail = d->fill - d->pos, len, dist = 0, i;
        if (!avail || (!finish && avail < LOOK))
            break;
        if (d->nsym >= SYMS - 1)
            end_block(d, 0);
        len = find(d, d->pos, &dist);
        if (len && len < d->lazy && avail > len) {      /* is the next place's run longer? Then this byte as it is */
            unsigned dist2 = 0, len2;
            insert(d, d->pos);
            len2 = find(d, d->pos + 1, &dist2);
            if (len2 > len) {
                symbol(d, d->win[d->pos], 0);
                d->pos++;
                continue;
            }
            symbol(d, len, dist);
            for (i = 1; i < len; i++)
                if (d->pos + i + MINM <= d->fill)
                    insert(d, d->pos + i);
            d->pos += len;
        } else if (len) {
            symbol(d, len, dist);
            for (i = 0; i < (len <= d->insert ? len : 1); i++)
                if (d->pos + i + MINM <= d->fill)
                    insert(d, d->pos + i);
            d->pos += len;
        } else {
            symbol(d, d->win[d->pos], 0);
            if (avail >= MINM)
                insert(d, d->pos);
            d->pos++;
        }
    }
}

/* ---- the stream */

void deflate_begin(struct defl *d, int level, int (*put)(void *ctx, const void *p, unsigned n), void *ctx)
{
    unsigned s, v;
    d->put = put, d->ctx = ctx, d->err = 0;
    if (level <= 1)
        d->chain = 4, d->nice = 16, d->lazy = 0, d->insert = 16;
    else
        d->chain = 24, d->nice = 96, d->lazy = 24, d->insert = MAXM;
    d->fill = d->pos = d->block = 0;
    d->nsym = 0;
    d->bits = 0, d->nbits = 0, d->nout = 0;
    memset(d->lfreq, 0, sizeof d->lfreq);
    memset(d->dfreq, 0, sizeof d->dfreq);
    memset(d->head, 0xFF, sizeof d->head);
    memset(d->prev, 0xFF, sizeof d->prev);
    for (s = 0; s < 29; s++)
        for (v = lbase[s]; v < (s == 28 ? 259u : lbase[s + 1]); v++)
            d->len_sym[v - 3] = (unsigned char)s;
    for (s = 0; s < 30; s++) {
        unsigned end = s == 29 ? 32769u : dbase[s + 1];
        for (v = dbase[s]; v < end && v <= 256; v++)
            d->dist_lo[v - 1] = (unsigned char)s;
        for (; v < end; v += 128)
            d->dist_hi[(v - 1) >> 7] = (unsigned char)s;
    }
}

int deflate_data(struct defl *d, const void *data, unsigned long n)
{
    const unsigned char *p = data;
    while (n && !d->err) {
        unsigned room = 2 * WSIZE - d->fill, k, i;
        if (!room) {                    /* the window is full: its first half is done with */
            compress(d, 0);
            end_block(d, 0);            /* (while the block's own bytes are still here, should it be stored) */
            memmove(d->win, d->win + WSIZE, WSIZE);
            d->pos -= WSIZE, d->fill -= WSIZE, d->block = d->pos;
            for (i = 0; i < HSIZE; i++)
                d->head[i] = (unsigned short)(d->head[i] == NIL || d->head[i] < WSIZE ? NIL : d->head[i] - WSIZE);
            for (i = 0; i < WSIZE; i++)
                d->prev[i] = (unsigned short)(d->prev[i] == NIL || d->prev[i] < WSIZE ? NIL : d->prev[i] - WSIZE);
            continue;
        }
        k = room < n ? room : (unsigned)n;
        memcpy(d->win + d->fill, p, k);
        d->fill += k;
        p += k, n -= k;
        compress(d, 0);
    }
    return d->err;
}

int deflate_end(struct defl *d)
{
    compress(d, 1);
    end_block(d, 1);
    if (d->nbits)
        put_bits(d, 0, 8 - d->nbits);
    flush_out(d);
    return d->err;
}
