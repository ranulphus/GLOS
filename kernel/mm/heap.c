/* kmalloc's allocator: first fit over a list of blocks laid end to end, each
   with a 16-byte header; neighbouring free blocks merge on free. The region
   grows through h->grow (the kernel maps frames; the host tests don't). */
#include "mm.h"

struct blk { u32 size; u32 used; u32 magic; u32 pad; };     /* size includes the header */
#define BLK_MAGIC 0x4B4C4247u

static struct blk *at(struct heap *h, u32 off) { return (struct blk *)(h->base + off); }

void *heap_alloc(struct heap *h, size_t n)
{
    u32 need = ALIGN_UP((u32)n, 16) + sizeof(struct blk), off = 0;
    struct blk *b;
    if (n == 0 || n > h->max)
        return NULL;
    for (off = 0; off < h->size; off += b->size) {
        b = at(h, off);
        if (!b->used && b->size >= need) {
            if (b->size - need >= 2 * sizeof(struct blk)) {         /* split */
                struct blk *rest = at(h, off + need);
                rest->size = b->size - need;
                rest->used = 0;
                rest->magic = BLK_MAGIC;
                b->size = need;
            }
            b->used = 1;
            return b + 1;
        }
    }
    if (h->size + need > h->max || h->grow(h, h->size + need) != 0)
        return NULL;
    b = at(h, h->size);
    b->size = need;
    b->used = 1;
    b->magic = BLK_MAGIC;
    h->size += need;
    return b + 1;
}

void heap_free(struct heap *h, void *p)
{
    struct blk *b, *n;
    u32 off;
    if (!p)
        return;
    b = (struct blk *)p - 1;
    if (b->magic != BLK_MAGIC || !b->used)
        return;
    b->used = 0;
    /* merge every run of free blocks */
    for (off = 0; off < h->size; off += b->size) {
        b = at(h, off);
        while (!b->used && off + b->size < h->size && !(n = at(h, off + b->size))->used)
            b->size += n->size;
    }
}

int heap_check(struct heap *h)
{
    u32 off = 0;
    struct blk *b;
    while (off < h->size) {
        b = at(h, off);
        if (b->magic != BLK_MAGIC || b->size < sizeof(struct blk) || off + b->size > h->size)
            return -1;
        off += b->size;
    }
    return off == h->size ? 0 : -1;
}
