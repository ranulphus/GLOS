/* Linear and physical memory (supervisor.md §12.3, §12.4; 0500h-0503h,
 * 0800h/0801h). Blocks live in the user region from 4 MB, kept in address
 * order; a new one goes in the lowest gap that fits at or above the
 * context's first block, never below it (DJGPP's sbrk counts on that).
 * Pages are committed and zeroed at once: GLOS has no paging to disk. A
 * resize stays in place when the pages after the block are free, and
 * otherwise moves the block by mapping its frames elsewhere. 0500h keeps a
 * reserve back for page tables and the kernel, and reports the rest
 * exactly: programs allocate all of it.
 *
 * Physical mappings (0800h): linear = physical below 110000h (the identity
 * map) and in the PCI window E0000000h-FFBFFFFFh, uncached; elsewhere at a
 * fresh linear range. */
#include "arch.h"
#include "dpmi.h"
#include "kprintf.h"
#include "mm.h"

#define RESERVE   256u                          /* frames kept back: page tables and the kernel's own */


static u32 pages_of(u32 bytes) { return (bytes + 4095) >> 12; }

static u32 floor_lin(void) { return dctx->lin_floor ? dctx->lin_floor : USER_BASE; }

/* The lowest place for size bytes at or above floor, or 0. */
static u32 place(u32 size, u32 floor, const struct block *skip)
{
    u32 at = floor;
    struct block *b;
    for (b = dctx->blocks; b; b = b->next) {
        if (b == skip || b->lin + b->size <= at || b->lin >= PHYS_WINDOW)
            continue;
        if (b->lin >= at + size)
            break;
        at = b->lin + b->size;
    }
    return at >= floor && at + size <= USER_END && at + size > at ? at : 0;
}

static void insert(struct block *n)
{
    struct block **pp = &dctx->blocks;
    while (*pp && (*pp)->lin < n->lin)
        pp = &(*pp)->next;
    n->next = *pp;
    *pp = n;
}

static void unlink(struct block *n)
{
    struct block **pp;
    for (pp = &dctx->blocks; *pp; pp = &(*pp)->next)
        if (*pp == n) {
            *pp = n->next;
            return;
        }
}

static struct block *by_handle(u32 h)
{
    struct block *b;
    for (b = dctx->blocks; b && b->handle != h; b = b->next) ;
    return b && b->kind == BK_MEM ? b : 0;
}

/* Fresh zeroed frames for pages [from, to) of a block at lin; -1 (with
   those pages unmapped again) when memory runs out. */
static int commit(u32 lin, u32 from, u32 to)
{
    u32 i;
    for (i = from; i < to; i++) {
        u32 f = pmm_free_frames() > RESERVE / 2 ? pmm_alloc() : 0;
        if (!f || mm_map(lin + (i << 12), f, MM_W | MM_U) != 0) {
            if (f)
                pmm_free(f);
            while (i-- > from)
                pmm_free(mm_unmap(lin + (i << 12)));
            return -1;
        }
        memset((void *)(lin + (i << 12)), 0, 4096);
        dctx->frames++;
    }
    return 0;
}

static void release(u32 lin, u32 from, u32 to)
{
    u32 i, f, pte;
    for (i = from; i < to; i++) {
        pte = mm_lookup(lin + (i << 12));
        if ((f = mm_unmap(lin + (i << 12))) != 0 && !(pte & MM_MAPPED)) {
            pmm_free(f);
            dctx->frames--;
        }
    }
}

int lin_alloc(u32 size, struct block **out)
{
    struct block *b;
    u32 lin;
    if (!size)
        return 0x8021;                          /* invalid value */
    size = pages_of(size) << 12;
    if (!(lin = place(size, floor_lin(), 0)))
        return 0x8012;                          /* linear memory unavailable */
    if (!(b = kmalloc(sizeof *b)))
        return 0x8013;
    if (commit(lin, 0, size >> 12) != 0) {
        kfree(b);
        return 0x8013;                          /* physical memory unavailable */
    }
    b->handle = ++dctx->next_handle;
    b->lin = lin;
    b->size = size;
    b->kind = BK_MEM;
    b->level = (u8)dctx->nlv;
    insert(b);
    if (!dctx->lin_floor)
        dctx->lin_floor = lin;
    *out = b;
    return 0;
}

int lin_free(u32 handle)
{
    struct block *b = by_handle(handle);
    if (!b)
        return 0x8023;                          /* invalid handle */
    release(b->lin, 0, b->size >> 12);
    unlink(b);
    kfree(b);
    return 0;
}

/* A linear address inside one of the client's own memory blocks. */
int lin_in_block(u32 lin)
{
    struct block *b;
    for (b = dctx->blocks; b; b = b->next)
        if (b->kind == BK_MEM && lin >= b->lin && lin < b->lin + b->size)
            return 1;
    return 0;
}

/* 0504h ([DPMI1.0]): size bytes at lin (0: the lowest place above the
   floor), committed now or left for 0507h to commit; no floor of its own. */
int lin_alloc_at(u32 lin, u32 size, int now, struct block **out)
{
    struct block *b;
    if (!size || (lin & 0xFFF))
        return lin ? 0x8025 : 0x8021;
    size = pages_of(size) << 12;
    if (lin) {
        if (lin < USER_BASE || lin + size > USER_END || lin + size < lin || place(size, lin, 0) != lin)
            return 0x8012;                      /* not free there */
    } else if (!(lin = place(size, floor_lin(), 0))) {
        return 0x8012;
    }
    if (!(b = kmalloc(sizeof *b)))
        return 0x8013;
    if (now && commit(lin, 0, size >> 12) != 0) {
        kfree(b);
        return 0x8013;
    }
    b->handle = ++dctx->next_handle;
    b->lin = lin;
    b->size = size;
    b->kind = BK_MEM;
    b->level = (u8)dctx->nlv;
    insert(b);
    *out = b;
    return 0;
}

int lin_resize(u32 handle, u32 size, struct block **out)
{
    return lin_resize2(handle, size, 1, out);
}

/* now: the pages it grows by are committed (0503h always; 0505h asks). */
int lin_resize2(u32 handle, u32 size, int now, struct block **out)
{
    struct block *b = by_handle(handle), *n;
    u32 old, want, lin, i;
    if (!b)
        return 0x8023;
    if (!size)
        return 0x8021;
    old = b->size >> 12;
    want = pages_of(size);
    *out = b;
    if (want <= old) {
        release(b->lin, want, old);
        b->size = want << 12;
        return 0;
    }
    n = b->next;
    if ((!n || n->lin >= b->lin + (want << 12)) && b->lin + (want << 12) <= USER_END) {
        if (now && commit(b->lin, old, want) != 0)      /* room after it: grow in place */
            return 0x8013;
        b->size = want << 12;
        return 0;
    }
    if (!(lin = place(want << 12, floor_lin(), b)))     /* move it: its frames go along */
        return 0x8012;
    if (now && commit(lin, old, want) != 0)
        return 0x8013;
    for (i = 0; i < old; i++) {                 /* with their attributes; uncommitted pages stay so (0507h) */
        u32 pte = mm_lookup(b->lin + (i << 12)), f = mm_unmap(b->lin + (i << 12));
        if (f)
            mm_map(lin + (i << 12), f, MM_U | (pte & (MM_W | MM_UC | MM_MAPPED)));
    }
    unlink(b);
    b->lin = lin;
    b->size = want << 12;
    insert(b);
    return 0;
}

void lin_free_all(struct dpmi_ctx *c)
{
    while (c->blocks) {
        struct block *b = c->blocks;
        c->blocks = b->next;
        if (b->kind != BK_PHYS)
            release(b->lin, 0, b->size >> 12);
        else
            for (u32 i = 0; i < b->size >> 12; i++)
                mm_unmap(b->lin + (i << 12));
        kfree(b);
    }
}

void lin_free_level(u32 level)
{
    struct block **pp = &dctx->blocks;
    while (*pp) {
        struct block *b = *pp;
        if (b->level < level) {
            pp = &b->next;
            continue;
        }
        if (b->kind != BK_PHYS)
            release(b->lin, 0, b->size >> 12);
        else
            for (u32 i = 0; i < b->size >> 12; i++)
                mm_unmap(b->lin + (i << 12));
        *pp = b->next;
        kfree(b);
    }
}

void lin_info(u32 *o)
{
    u32 avail = pmm_free_frames(), lin_free = 0, at = USER_BASE, largest = 0, gap;
    struct block *b;
    for (b = dctx->blocks; b; b = b->next) {    /* the user region's free pages, and its largest gap */
        if (b->lin >= PHYS_WINDOW)
            continue;
        if (b->lin > at) {
            gap = (b->lin - at) >> 12;
            lin_free += gap;
            if (b->lin >= floor_lin() && gap > largest)
                largest = gap;
        }
        if (b->lin + b->size > at)
            at = b->lin + b->size;
    }
    gap = (USER_END - at) >> 12;
    lin_free += gap;
    if (gap > largest)
        largest = gap;
    avail = avail > RESERVE ? avail - RESERVE : 0;
    gap = avail / 1024 + 1;                     /* page tables for that many pages */
    avail = avail > gap ? avail - gap : 0;
    if (largest > avail)
        largest = avail;
    o[0] = largest << 12;                       /* the largest block, in bytes */
    o[1] = avail;                               /* pages: most unlocked, */
    o[2] = avail;                               /* most locked, */
    o[3] = (USER_END - USER_BASE) >> 12;        /* linear space, */
    o[4] = avail + dctx->frames;                /* unlocked in all, */
    o[5] = avail;                               /* free, */
    o[6] = avail + dctx->frames;                /* physical in all, */
    o[7] = lin_free;                            /* free linear space */
    o[8] = 0xFFFFFFFFu;                         /* no paging file */
    o[9] = o[10] = o[11] = 0xFFFFFFFFu;
}

int phys_map(u32 phys, u32 size, u32 *lin)
{
    struct block *b;
    u32 off = phys & 0xFFF, base = phys & ~0xFFFu, n = pages_of(size + off), at, i;
    if (!size || phys + size < phys)
        return 0x8021;
    if (phys + size <= 0x110000) {              /* the identity map */
        *lin = phys;
        return 0;
    }
    if (base >= PHYS_WINDOW && base + (n << 12) <= 0xFFC00000u && base + (n << 12) > base)
        at = base;
    else if (!(at = place(n << 12, USER_BASE, 0)))
        return 0x8012;
    if (!(b = kmalloc(sizeof *b)))
        return 0x8013;
    for (i = 0; i < n; i++)
        if (mm_map(at + (i << 12), base + (i << 12), MM_W | MM_U | MM_UC) != 0) {
            while (i--)
                mm_unmap(at + (i << 12));
            kfree(b);
            return 0x8013;
        }
    b->handle = 0;
    b->lin = at;
    b->size = n << 12;
    b->kind = BK_PHYS;
    b->level = (u8)dctx->nlv;
    insert(b);
    *lin = at + off;
    return 0;
}

int phys_unmap(u32 lin)
{
    struct block *b;
    u32 i;
    if (lin < 0x110000)
        return 0;
    for (b = dctx->blocks; b && !(b->kind == BK_PHYS && lin >= b->lin && lin < b->lin + b->size); b = b->next) ;
    if (!b)
        return 0x8025;                          /* invalid linear address */
    for (i = 0; i < b->size >> 12; i++)
        mm_unmap(b->lin + (i << 12));
    unlink(b);
    kfree(b);
    return 0;
}

/* Host memory inside the context (the locked stack): committed now, freed
   with the context, out of every handle's reach. */
int lin_host(u32 lin, u32 size)
{
    struct block *b = kmalloc(sizeof *b);
    if (!b)
        return -1;
    size = pages_of(size) << 12;
    if (commit(lin, 0, size >> 12) != 0) {
        kfree(b);
        return -1;
    }
    b->handle = 0;
    b->lin = lin;
    b->size = size;
    b->kind = BK_HOST;
    b->level = 1;
    insert(b);
    return 0;
}

/* 0506h/0507h ([DPMI1.0]): n pages of a block from byte off, one word each:
   bits 0-2 the type (0 uncommitted, 1 committed, 3 for 0507h: keep it),
   bit 3 writable, bit 4 with accessed (5) and dirty (6). GLOS commits at
   once, so a page is committed or not, and 0507h does it now. *done:
   pages handled, also on failure. */
int page_attr(u32 handle, u32 off, u32 n, u16 *attr, int set, u32 *done)
{
    struct block *b = by_handle(handle);
    u32 i;
    *done = 0;
    if (!b)
        return 0x8023;
    if ((off & 0xFFF) || off >= b->size || n > (b->size - off) >> 12)
        return 0x8025;                          /* invalid linear address */
    for (i = 0; i < n; i++, (*done)++) {
        u32 lin = b->lin + off + (i << 12), pte = mm_lookup(lin), type = attr[i] & 7;
        if (!set) {
            attr[i] = (pte & 1) ? (u16)(((pte & MM_MAPPED) ? 2 : 1) | ((pte & MM_W) ? 8 : 0) | 0x10
                                        | ((pte & 0x20) ? 0x20 : 0) | ((pte & 0x40) ? 0x40 : 0)) : 0;
            continue;
        }
        if (type == 2 || type > 3)
            return 0x8021;                      /* mapped pages: only 0508h/0509h make them */
        if (type == 0) {
            release(lin, 0, 1);
            continue;
        }
        if (!(pte & 1) || (type == 1 && (pte & MM_MAPPED))) {
            if (type == 3)
                continue;                       /* uncommitted, and stays so */
            release(lin, 0, 1);                 /* a mapping becomes memory of its own (I310508a) */
            if (commit(lin, 0, 1) != 0)
                return 0x8013;
            pte = mm_lookup(lin);
        }
        mm_map(lin, pte & ~0xFFFu, MM_U | ((attr[i] & 8) ? MM_W : 0) | (pte & (MM_UC | MM_MAPPED | 0x60)));
    }
    return 0;
}

/* 0508h/0509h ([DPMI1.0]): n pages of a block from byte off become the
   physical pages from phys (a device's, uncached; or conventional memory's,
   whose linear address is its physical one). What was committed there is
   freed; the pages are "mapped" (type 2) for 0506h. */
int page_map(u32 handle, u32 off, u32 n, u32 phys, int device)
{
    struct block *b = by_handle(handle);
    u32 i;
    if (!b)
        return 0x8023;
    if ((off & 0xFFF) || (phys & 0xFFF) || off >= b->size || !n || n > (b->size - off) >> 12)
        return 0x8025;
    if (!device && phys + (n << 12) > 0x110000)
        return 0x8025;                          /* 0509h: below 1 MB (and the HMA) only */
    for (i = 0; i < n; i++) {
        u32 lin = b->lin + off + (i << 12);
        release(lin, 0, 1);
        if (mm_map(lin, phys + (i << 12), MM_W | MM_U | MM_MAPPED | (device ? MM_UC : 0)) != 0)
            return 0x8013;
    }
    return 0;
}
