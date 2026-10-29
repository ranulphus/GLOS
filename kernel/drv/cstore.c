/* The disk cache's store (docs/disk-cache.md): 4 KB blocks of disk sectors,
 * a physical frame each, found by (drive, LBA / 8) in a hash table and kept
 * in a segmented LRU. A new block joins probation; a hit there moves it to
 * protected, which holds at most three quarters of the blocks (its oldest go
 * back to probation); the oldest of probation goes first. So the blocks DOS
 * reads again and again (the FAT) outlast a long streaming read, which a
 * plain LRU (SMARTDRV's) lets push them out. A descriptor is indexed by its
 * frame's number, so whose a frame is shows at once (cache_reclaim_run).
 *
 * Pure: no kernel calls; the caller keeps it consistent (interrupts off).
 * tests/host/cache_test.c builds it as it is. */
#include "cstore.h"

static u32 bucket(const struct cstore *s, u32 key) { return (key * 2654435761u) >> (32 - s->hbits); }

void cs_init(struct cstore *s, struct cblk *b, u32 nframes, u32 *h, u32 hbits)
{
    u32 i;
    s->b = b;
    s->nframes = nframes;
    s->h = h;
    s->hbits = hbits;
    for (i = 0; i < (1u << hbits); i++)
        h[i] = CS_NIL;
    for (i = 0; i < nframes; i++)
        b[i].flags = 0;
    s->head[0] = s->head[1] = s->tail[0] = s->tail[1] = CS_NIL;
    s->n[0] = s->n[1] = 0;
}

u32 cs_find(const struct cstore *s, u32 key)
{
    u32 f = s->h[bucket(s, key)];
    while (f != CS_NIL && s->b[f].key != key)
        f = s->b[f].hnext;
    return f;
}

static void unlink(struct cstore *s, u32 f)
{
    struct cblk *b = &s->b[f];
    if (b->prev != CS_NIL)
        s->b[b->prev].next = b->next;
    else
        s->head[b->seg] = b->next;
    if (b->next != CS_NIL)
        s->b[b->next].prev = b->prev;
    else
        s->tail[b->seg] = b->prev;
    s->n[b->seg]--;
}

static void push(struct cstore *s, u32 f, u8 seg)
{
    struct cblk *b = &s->b[f];
    b->seg = seg;
    b->prev = CS_NIL;
    b->next = s->head[seg];
    if (b->next != CS_NIL)
        s->b[b->next].prev = f;
    else
        s->tail[seg] = f;
    s->head[seg] = f;
    s->n[seg]++;
}

void cs_insert(struct cstore *s, u32 key, u32 f)
{
    struct cblk *b = &s->b[f];
    u32 h = bucket(s, key);
    b->key = key;
    b->valid = 0;
    b->flags = CB_USED;
    b->hnext = s->h[h];
    s->h[h] = f;
    push(s, f, 0);
}

void cs_hit(struct cstore *s, u32 f)
{
    unlink(s, f);
    push(s, f, 1);
    while (s->n[1] * 4 > 3 * (s->n[0] + s->n[1]) && s->tail[1] != CS_NIL) {
        u32 t = s->tail[1];
        unlink(s, t);
        push(s, t, 0);
    }
}

void cs_remove(struct cstore *s, u32 f)
{
    u32 *p = &s->h[bucket(s, s->b[f].key)];
    while (*p != CS_NIL && *p != f)
        p = &s->b[*p].hnext;
    if (*p == f)
        *p = s->b[f].hnext;
    unlink(s, f);
    s->b[f].flags = 0;
    s->b[f].valid = 0;
}

u32 cs_victim(const struct cstore *s)
{
    int seg;
    u32 f;
    for (seg = 0; seg < 2; seg++)
        for (f = s->tail[seg]; f != CS_NIL; f = s->b[f].prev)
            if (!(s->b[f].flags & CB_PINNED))
                return f;
    return CS_NIL;
}

int cs_chs_lba(const struct cgeo *g, u32 cx, u32 dh, u32 *lba)
{
    u32 cyl = ((cx >> 8) & 0xFF) | ((cx & 0xC0) << 2), sec = cx & 63, head = dh & 0xFF;
    if (!sec || sec > g->spt || head >= g->heads || cyl >= g->cyls)
        return -1;
    *lba = (cyl * g->heads + head) * g->spt + sec - 1;
    return 0;
}

void cs_lba_chs(const struct cgeo *g, u32 lba, u32 *cx, u32 *dh)
{
    u32 sec = lba % g->spt + 1, t = lba / g->spt, head = t % g->heads, cyl = t / g->heads;
    *cx = ((cyl & 0xFF) << 8) | ((cyl >> 2) & 0xC0) | sec;
    *dh = head;
}
