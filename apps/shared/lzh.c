/* The unpacker of LHA's -lh5-, -lh6- and -lh7- and of ARJ's methods 1 to 4 (lzh.h), for Files' archives
 * (apps/shared/arc.c).
 *
 * The first three, and ARJ's 1 to 3, are one method (Okumura's and Yoshizaki's, of 1988-91, as LHA and ARJ both
 * have it): the bytes are a run of blocks; a block begins with how many codes it has and three tables of code
 * lengths (one for the lengths of the second's codes, itself run-length coded; the second for literals and match
 * lengths, 510 codes; the third for match distances, as a count of bits); then the codes. A code under 256 is a
 * byte; one above is a match of (code - 253) bytes, 3 to 256, at a distance the next code and that many bits give,
 * back in a window of 8 KB (lh5), 32 KB (lh6), 64 KB (lh7) or 26,624 bytes (ARJ). The archives differ only in the
 * window and in how many bits two of the counts take.
 *
 * ARJ's method 4 has no tables: a bit says a literal byte follows or a match, whose length and distance are each
 * a run of one bits saying how many bits the number has.
 *
 * Written from the methods' descriptions. Whatever the bytes are, nothing is read or written out of bounds: a
 * table that is no code, a code that leads nowhere and a match from farther back than the window all end it
 * (LZH_DAMAGED). */
#include <stdlib.h>
#include <string.h>

#include "lzh.h"

#define NC 510                          /* literals and lengths: 256 + (256 - 3 + 1) */
#define NT 19                           /* the lengths' lengths */
#define NPMAX 18                        /* distances' bit counts, at most (lh7: 17; one over for safety) */
#define CBIT 9
#define TBIT 5
#define WIN 65536ul                     /* the window kept: the largest of them */

struct lzh {
    int (*get)(void *ctx);
    int (*put)(void *ctx, const void *p, unsigned n);
    void *ctx;
    unsigned long packed;               /* packed bytes not yet taken */
    unsigned bitbuf;                    /* the next 16 bits */
    unsigned sub;                       /* bytes not yet shifted in whole */
    int nsub;
    int np, pbit;
    unsigned blocksize;
    int bad;
    unsigned short left[2 * NC], right[2 * NC];
    unsigned char c_len[NC], pt_len[NT > NPMAX ? NT : NPMAX];
    unsigned short c_table[4096], pt_table[256];
    unsigned char win[WIN];
};

static void fill(struct lzh *z, int n)  /* n bits gone, n more in */
{
    z->bitbuf = (z->bitbuf << n) & 0xFFFF;
    while (n > z->nsub) {
        n -= z->nsub;
        z->bitbuf |= (z->sub << n) & 0xFFFF;
        if (z->packed) {
            int c = z->get(z->ctx);
            z->packed--;
            z->sub = c < 0 ? 0 : (unsigned)c;
        } else {
            z->sub = 0;
        }
        z->nsub = 8;
    }
    z->nsub -= n;
    z->bitbuf |= z->sub >> z->nsub;
    z->bitbuf &= 0xFFFF;
}

static unsigned bits(struct lzh *z, int n)
{
    unsigned x;
    if (n == 0)
        return 0;
    x = z->bitbuf >> (16 - n);
    fill(z, n);
    return x;
}

/* The decoding table for nchar codes of the given lengths: tablebits of a code looked up at once, the longer
   ones' tails as a tree in left and right. -1: the lengths are no code. */
static int make_table(struct lzh *z, int nchar, const unsigned char *bitlen, int tablebits, unsigned short *table,
                      unsigned tablesize)
{
    unsigned count[17], weight[17], start[18], i, k, len, ch, jutbits, avail, nextcode, mask;
    unsigned short *p;
    for (i = 1; i <= 16; i++)
        count[i] = 0;
    for (i = 0; i < (unsigned)nchar; i++) {
        if (bitlen[i] > 16)
            return -1;
        count[bitlen[i]]++;
    }
    start[1] = 0;
    for (i = 1; i <= 16; i++)
        start[i + 1] = (start[i] + (count[i] << (16 - i))) & 0xFFFFFFFFu;
    if ((start[17] & 0xFFFF) != 0 || start[17] > 0x10000u)
        return -1;                              /* (not a whole code) */
    jutbits = 16 - (unsigned)tablebits;
    for (i = 1; i <= (unsigned)tablebits; i++) {
        start[i] >>= jutbits;
        weight[i] = 1u << ((unsigned)tablebits - i);
    }
    for (; i <= 16; i++)
        weight[i] = 1u << (16 - i);
    i = start[tablebits + 1] >> jutbits;
    if (i != 0) {
        k = 1u << tablebits;
        while (i != k && i < tablesize)
            table[i++] = 0;
    }
    avail = (unsigned)nchar;
    mask = 1u << (15 - (unsigned)tablebits);
    for (ch = 0; ch < (unsigned)nchar; ch++) {
        if ((len = bitlen[ch]) == 0)
            continue;
        nextcode = start[len] + weight[len];
        if (len <= (unsigned)tablebits) {
            if (nextcode > tablesize)
                return -1;
            for (i = start[len]; i < nextcode; i++)
                table[i] = (unsigned short)ch;
        } else {
            k = start[len];
            if ((k >> jutbits) >= tablesize)
                return -1;
            p = &table[k >> jutbits];
            i = len - (unsigned)tablebits;
            while (i != 0) {
                if (*p == 0) {
                    if (avail >= 2 * NC)
                        return -1;
                    z->right[avail] = z->left[avail] = 0;
                    *p = (unsigned short)avail++;
                }
                if (*p >= 2 * NC)
                    return -1;
                p = (k & mask) ? &z->right[*p] : &z->left[*p];
                k <<= 1;
                i--;
            }
            *p = (unsigned short)ch;
        }
        start[len] = nextcode;
    }
    return 0;
}

static void read_pt_len(struct lzh *z, int nn, int nbit, int special)
{
    int i, c, n = (int)bits(z, nbit);
    if (n == 0) {
        c = (int)bits(z, nbit);
        for (i = 0; i < nn; i++)
            z->pt_len[i] = 0;
        for (i = 0; i < 256; i++)
            z->pt_table[i] = (unsigned short)c;
        if (c >= nn)
            z->bad = 1;
        return;
    }
    if (n > nn) {
        z->bad = 1;
        return;
    }
    for (i = 0; i < n;) {
        c = (int)(z->bitbuf >> 13);
        if (c == 7) {
            unsigned mask = 1u << 12;
            while (mask & z->bitbuf) {
                mask >>= 1;
                c++;
            }
            if (c > 16) {
                z->bad = 1;
                return;
            }
        }
        fill(z, c < 7 ? 3 : c - 3);
        z->pt_len[i++] = (unsigned char)c;
        if (i == special) {
            c = (int)bits(z, 2);
            while (--c >= 0 && i < nn)
                z->pt_len[i++] = 0;
        }
    }
    while (i < nn)
        z->pt_len[i++] = 0;
    if (make_table(z, nn, z->pt_len, 8, z->pt_table, 256) != 0)
        z->bad = 1;
}

static void read_c_len(struct lzh *z)
{
    int i, c, n = (int)bits(z, CBIT);
    if (n == 0) {
        c = (int)bits(z, CBIT);
        for (i = 0; i < NC; i++)
            z->c_len[i] = 0;
        for (i = 0; i < 4096; i++)
            z->c_table[i] = (unsigned short)c;
        if (c >= NC)
            z->bad = 1;
        return;
    }
    if (n > NC) {
        z->bad = 1;
        return;
    }
    for (i = 0; i < n;) {
        c = z->pt_table[z->bitbuf >> 8];
        if (c >= NT) {
            unsigned mask = 1u << 7;
            do {
                if (c >= 2 * NC || !mask) {
                    z->bad = 1;
                    return;
                }
                c = (z->bitbuf & mask) ? z->right[c] : z->left[c];
                mask >>= 1;
            } while (c >= NT);
        }
        fill(z, z->pt_len[c]);
        if (c <= 2) {
            if (c == 0)
                c = 1;
            else if (c == 1)
                c = (int)bits(z, 4) + 3;
            else
                c = (int)bits(z, CBIT) + 20;
            while (--c >= 0 && i < NC)
                z->c_len[i++] = 0;
        } else {
            z->c_len[i++] = (unsigned char)(c - 2);
        }
    }
    while (i < NC)
        z->c_len[i++] = 0;
    if (make_table(z, NC, z->c_len, 12, z->c_table, 4096) != 0)
        z->bad = 1;
}

static int decode_c(struct lzh *z)
{
    unsigned j, mask;
    if (z->blocksize == 0) {
        z->blocksize = bits(z, 16);
        read_pt_len(z, NT, TBIT, 3);
        if (!z->bad)
            read_c_len(z);
        if (!z->bad)
            read_pt_len(z, z->np, z->pbit, -1);
        if (z->bad || !z->blocksize)
            return -1;
    }
    z->blocksize--;
    j = z->c_table[z->bitbuf >> 4];
    if (j >= NC) {
        mask = 1u << 3;
        do {
            if (j >= 2 * NC || !mask)
                return -1;
            j = (z->bitbuf & mask) ? z->right[j] : z->left[j];
            mask >>= 1;
        } while (j >= NC);
    }
    fill(z, z->c_len[j]);
    return (int)j;
}

static long decode_p(struct lzh *z)
{
    unsigned j = z->pt_table[z->bitbuf >> 8], mask;
    if (j >= (unsigned)z->np) {
        mask = 1u << 7;
        do {
            if (j >= 2 * NC || !mask)
                return -1;
            j = (z->bitbuf & mask) ? z->right[j] : z->left[j];
            mask >>= 1;
        } while (j >= (unsigned)z->np);
    }
    fill(z, z->pt_len[j]);
    if (j != 0)
        j = (1u << (j - 1)) + bits(z, (int)(j - 1));
    return (long)j;
}

/* ARJ's method 4: a number as a run of one bits (how many bits more it has than `first`, `most` at most), then
   those bits. */
static unsigned run_number(struct lzh *z, int first, int most)
{
    unsigned plus = 0, pwr = 1u << first;
    int width;
    for (width = first; width < most; width++) {
        if (!bits(z, 1))
            break;
        plus += pwr;
        pwr <<= 1;
    }
    return plus + (width ? bits(z, width) : 0);
}

int lzh_unpack(int method, unsigned long packed, unsigned long size, int (*get)(void *ctx),
               int (*put)(void *ctx, const void *p, unsigned n), void *ctx)
{
    struct lzh *z;
    unsigned long done = 0, at = 0, from = 0;   /* at: the window's place for the next byte; from: not yet put */
    unsigned long window;
    int r = 0;
    switch (method) {
    case LZH_LH5: window = 1ul << 13; break;
    case LZH_LH6: window = 1ul << 15; break;
    case LZH_LH7: window = 1ul << 16; break;
    case LZH_ARJ:
    case LZH_ARJ4: window = 26624; break;
    default: return LZH_DAMAGED;
    }
    if (!(z = calloc(1, sizeof *z)))
        return LZH_MEMORY;
    z->get = get, z->put = put, z->ctx = ctx, z->packed = packed;
    z->np = method == LZH_LH5 ? 14 : method == LZH_LH6 ? 16 : 17;
    z->pbit = method == LZH_LH5 ? 4 : 5;
    fill(z, 16);
    while (done < size && !r) {
        unsigned len;
        unsigned long dist;
        if (method == LZH_ARJ4) {
            len = run_number(z, 0, 7);
            if (len == 0) {
                z->win[at] = (unsigned char)bits(z, 8);
                at = (at + 1) & (WIN - 1), done++;
                len = 0;
                dist = 0;
            } else {
                len += 2;
                dist = run_number(z, 9, 13);
            }
        } else {
            int c = decode_c(z);
            if (c < 0) {
                r = LZH_DAMAGED;
                break;
            }
            if (c < 256) {
                z->win[at] = (unsigned char)c;
                at = (at + 1) & (WIN - 1), done++;
                len = 0;
                dist = 0;
            } else {
                long p = decode_p(z);
                if (p < 0) {
                    r = LZH_DAMAGED;
                    break;
                }
                len = (unsigned)c - 253;
                dist = (unsigned long)p;
            }
        }
        if (len) {
            unsigned long src;
            /* From before the first byte: LHA's packers begin with a window of spaces, and a match may be of
               those (LHA and lhasa unpack it so); ARJ's window has nothing in it then. */
            if (dist >= window || (dist >= done && method > LZH_LH7)) {
                r = LZH_DAMAGED;
                break;
            }
            src = (at - dist - 1) & (WIN - 1);
            while (len-- && done < size) {
                z->win[at] = dist >= done ? ' ' : z->win[src];
                at = (at + 1) & (WIN - 1), src = (src + 1) & (WIN - 1), done++;
                if (at == 0) {                  /* the window's end: what is in it and not yet put, out */
                    if ((r = put(ctx, z->win + from, (unsigned)(WIN - from))) != 0)
                        break;
                    from = 0;
                }
            }
            continue;
        }
        if (at == 0) {
            if ((r = put(ctx, z->win + from, (unsigned)(WIN - from))) != 0)
                break;
            from = 0;
        }
    }
    if (!r && at != from)
        r = put(ctx, z->win + from, (unsigned)(at - from));
    if (!r && z->bad)
        r = LZH_DAMAGED;
    free(z);
    return r;
}
