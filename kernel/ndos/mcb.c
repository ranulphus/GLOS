/* A machine's memory as GLOS's own DOS keeps it (mcb.h). */
#include <string.h>

#include "mcb.h"

static unsigned char *mcb(const struct dos_mem *d, unsigned seg)
{
    return d->m + (unsigned long)seg * 16;
}

static unsigned rd16(const unsigned char *p)
{
    return p[0] | (unsigned)p[1] << 8;
}

static void wr16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

static unsigned type_of(const struct dos_mem *d, unsigned seg) { return mcb(d, seg)[0]; }
static unsigned owner_of(const struct dos_mem *d, unsigned seg) { return rd16(mcb(d, seg) + 1); }
static unsigned size_of(const struct dos_mem *d, unsigned seg) { return rd16(mcb(d, seg) + 3); }
static unsigned next_of(const struct dos_mem *d, unsigned seg) { return seg + size_of(d, seg) + 1; }

/* An MCB at seg, inside the memory, 'M' or 'Z'. */
static int valid(const struct dos_mem *d, unsigned seg)
{
    if ((unsigned long)seg * 16 + 16 > d->size)
        return 0;
    return type_of(d, seg) == 'M' || type_of(d, seg) == 'Z';
}

static void put(struct dos_mem *d, unsigned seg, unsigned type, unsigned owner, unsigned size, const char *name)
{
    unsigned char *p = mcb(d, seg);

    p[0] = (unsigned char)type;
    wr16(p + 1, owner);
    wr16(p + 3, size);
    if (name) {
        memset(p + 8, 0, 8);
        memcpy(p + 8, name, strlen(name) < 8 ? strlen(name) : 8);
    }
}

void dos_mem_init(struct dos_mem *d, unsigned char *m, unsigned long size, unsigned first, unsigned low_end,
                  const unsigned *umb, int n_umb)
{
    int i;

    d->m = m;
    d->size = size;
    d->first = first;
    d->strategy = 0;
    d->linked = 0;
    d->link = 0;
    if (!n_umb) {
        put(d, first, 'Z', 0, low_end - first - 1, 0);
        return;
    }
    /* The low block ends below the link block at low_end - 1, which covers A000h up to the first UMB. */
    d->link = low_end - 1;
    put(d, first, 'Z', 0, d->link - first - 1, 0);
    put(d, d->link, 'M', 8, umb[0] - d->link - 1, "SC");
    for (i = 0; i < n_umb; i++) {
        if (i == n_umb - 1) {
            put(d, umb[2 * i], 'Z', 0, umb[2 * i + 1] - umb[2 * i] - 1, 0);
        } else {                                /* (its last paragraph: DOS's block over the gap to the next) */
            put(d, umb[2 * i], 'M', 0, umb[2 * i + 1] - umb[2 * i] - 2, 0);
            put(d, umb[2 * i + 1] - 1, 'M', 8, umb[2 * i + 2] - umb[2 * i + 1], "SC");
        }
    }
}

/* Free blocks from seg on joined into it, while the next is free. */
static void join(struct dos_mem *d, unsigned seg)
{
    while (type_of(d, seg) == 'M' && valid(d, next_of(d, seg)) && !owner_of(d, next_of(d, seg))) {
        unsigned n = next_of(d, seg), size = size_of(d, seg) + size_of(d, n) + 1;

        if (size > 0xFFFF)
            break;
        mcb(d, seg)[0] = (unsigned char)type_of(d, n);
        wr16(mcb(d, seg) + 3, size);
    }
}

/* The next block of the whole chain (upper memory too, linked or not), or 0 at its end; -1 if broken. */
static long step(const struct dos_mem *d, unsigned seg, int whole)
{
    if (type_of(d, seg) == 'Z') {
        if (whole && d->link && !d->linked && seg < d->link)
            return d->link;                     /* (unlinked: the upper memory goes on from the link block) */
        return 0;
    }
    seg = next_of(d, seg);
    return valid(d, seg) ? (long)seg : -1;
}

/* A walk for a block of paras: from the chain's start, joining free blocks as it goes; only blocks at min_seg
   and above count. The block chosen by fit (0 first, 1 best, 2 last), or 0; *largest the biggest seen. */
static int scan(struct dos_mem *d, unsigned min_seg, unsigned paras, unsigned fit, unsigned *chosen,
                unsigned *largest)
{
    long seg = d->first;

    *chosen = 0;
    *largest = 0;
    while (seg > 0) {
        if (!valid(d, (unsigned)seg))
            return DOS_EMCB;
        if (!owner_of(d, (unsigned)seg)) {
            unsigned s;

            join(d, (unsigned)seg);
            s = size_of(d, (unsigned)seg);
            if ((unsigned)seg >= min_seg) {
                if (s > *largest)
                    *largest = s;
                if (s >= paras && (!*chosen || fit >= 2 || (fit == 1 && s < size_of(d, *chosen)))) {
                    if (!(*chosen && fit == 0))
                        *chosen = (unsigned)seg;
                }
            }
        }
        seg = step(d, (unsigned)seg, 0);
    }
    return seg < 0 ? DOS_EMCB : 0;
}

int dos_mem_alloc(struct dos_mem *d, unsigned paras, unsigned owner, unsigned *seg, unsigned *largest)
{
    unsigned fit = d->strategy & 3, hi = d->strategy & 0xC0, b = 0, big = 0, size, type;
    int r = 0;

    if (fit == 3)
        fit = 2;
    *largest = 0;
    if (hi && d->linked && d->link) {
        r = scan(d, d->link, paras, fit, &b, &big);
        if (r)
            return r;
        *largest = big;
        if (!b && hi == 0x40)
            return DOS_ENOMEM;
    }
    if (!b) {
        r = scan(d, 0, paras, fit, &b, &big);
        if (r)
            return r;
        if (big > *largest)
            *largest = big;
        if (!b)
            return DOS_ENOMEM;
    }
    size = size_of(d, b);
    type = type_of(d, b);
    if (fit == 2 && size > paras) {             /* last fit: the top of the block */
        unsigned n = b + size - paras;

        put(d, n, type, owner, paras, 0);
        mcb(d, b)[0] = 'M';
        wr16(mcb(d, b) + 3, size - paras - 1);
        *seg = n + 1;
        return 0;
    }
    if (size > paras) {
        put(d, b + 1 + paras, type, 0, size - paras - 1, 0);
        mcb(d, b)[0] = 'M';
        wr16(mcb(d, b) + 3, paras);
    }
    wr16(mcb(d, b) + 1, owner);
    *seg = b + 1;
    return 0;
}

int dos_mem_free(struct dos_mem *d, unsigned seg)
{
    if (!seg || !valid(d, seg - 1))
        return DOS_EBLOCK;
    wr16(mcb(d, seg - 1) + 1, 0);
    return 0;
}

int dos_mem_resize(struct dos_mem *d, unsigned seg, unsigned paras, unsigned *largest)
{
    unsigned b = seg - 1, size, avail, type, f;

    if (!seg || !valid(d, b))
        return DOS_EBLOCK;
    size = size_of(d, b);
    type = type_of(d, b);
    avail = size;
    f = 0;
    if (type == 'M') {
        f = next_of(d, b);
        if (!valid(d, f))
            return DOS_EMCB;
        if (!owner_of(d, f)) {
            join(d, f);
            avail = size + size_of(d, f) + 1;
            if (avail > 0xFFFF)
                avail = 0xFFFF;
        } else {
            f = 0;
        }
    }
    *largest = avail;
    if (paras <= size) {                        /* smaller: the rest a free block after it */
        if (paras < size) {
            put(d, b + 1 + paras, type, 0, size - paras - 1, 0);
            mcb(d, b)[0] = 'M';
            wr16(mcb(d, b) + 3, paras);
        }
        return 0;
    }
    {
        unsigned want = paras <= avail ? paras : avail;
        unsigned end_type = f ? type_of(d, f) : type;

        if (avail - want >= 1) {
            put(d, b + 1 + want, end_type, 0, avail - want - 1, 0);
            mcb(d, b)[0] = 'M';
        } else {
            mcb(d, b)[0] = (unsigned char)end_type;
        }
        wr16(mcb(d, b) + 3, want);
        return paras <= avail ? 0 : DOS_ENOMEM;
    }
}

void dos_mem_free_owner(struct dos_mem *d, unsigned owner)
{
    long seg = d->first;

    while (seg > 0) {
        if (owner_of(d, (unsigned)seg) == owner)
            wr16(mcb(d, (unsigned)seg) + 1, 0);
        seg = step(d, (unsigned)seg, 1);
    }
}

int dos_mem_strategy(struct dos_mem *d, unsigned s)
{
    if ((s & 0xFF00) || ((s & 3) == 3) || ((s & 0xC0) == 0xC0) || (s & 0x3C))
        return DOS_EFUNC;
    d->strategy = s;
    return 0;
}

int dos_mem_link(struct dos_mem *d, int on)
{
    long seg = d->first;

    if (!d->link)
        return DOS_EFUNC;
    while (seg > 0 && (unsigned)seg < d->link) {
        unsigned n;

        if (!valid(d, (unsigned)seg))
            return DOS_EMCB;
        n = next_of(d, (unsigned)seg);
        if (n == d->link) {
            mcb(d, (unsigned)seg)[0] = (unsigned char)(on ? 'M' : 'Z');
            d->linked = on;
            return 0;
        }
        if (type_of(d, (unsigned)seg) == 'Z')
            break;
        seg = (long)n;
    }
    return DOS_EMCB;
}

int dos_mem_check(const struct dos_mem *d)
{
    long seg = d->first;

    while (seg > 0) {
        if (!valid(d, (unsigned)seg))
            return DOS_EMCB;
        seg = step(d, (unsigned)seg, 1);
    }
    return seg < 0 ? DOS_EMCB : 0;
}
