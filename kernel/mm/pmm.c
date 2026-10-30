/* The physical frame bitmap. Frames start used; the loader's free ranges are
   released and the kernel image and everything below 110000h reserved again
   (supervisor.md §5). */
#include "mm.h"

#define FRAME(a) ((a) >> 12)

void pmm_setup(struct pmm *p, u32 *bits, u32 frames)
{
    u32 i;
    p->bits = bits;
    p->frames = frames;
    p->free = 0;
    p->next = 0;
    p->top = 0;
    for (i = 0; i < (frames + 31) / 32; i++)
        bits[i] = 0xFFFFFFFFu;
}

/* Free ranges round inwards (only whole frames become free), reserved ranges
   outwards (any frame they touch becomes used). */
static void set_range(struct pmm *p, u32 base, u32 length, int used)
{
    u32 end = base + length, f, last;
    if (end < base)
        end = 0xFFFFF000u;
    if (used) {
        f = FRAME(base);
        last = end > 0xFFFFF000u ? FRAME(0xFFFFF000u) + 1 : FRAME(end + 0xFFF);
    } else {
        f = FRAME(base + 0xFFF);
        last = FRAME(end);
        if (f < last && last > p->top)
            p->top = last < p->frames ? last : p->frames;
    }
    for (; f < last && f < p->frames; f++) {
        u32 w = f / 32, b = 1u << (f % 32);
        if (used && !(p->bits[w] & b)) { p->bits[w] |= b; p->free--; }
        if (!used && (p->bits[w] & b)) { p->bits[w] &= ~b; p->free++; }
    }
}

void pmm_add_free(struct pmm *p, u32 base, u32 length) { set_range(p, base, length, 0); }
void pmm_reserve(struct pmm *p, u32 base, u32 length) { set_range(p, base, length, 1); }

/* The scans stop at p->top: no frame past the RAM's end is ever free (the
   bitmap covers 1 GB, a machine in Loop A 64 MB). */
u32 pmm_take(struct pmm *p)
{
    u32 n, f;
    for (n = 0; n < p->top; n++) {
        f = (p->next + n) % p->top;
        if (!(p->bits[f / 32] & (1u << (f % 32)))) {
            p->bits[f / 32] |= 1u << (f % 32);
            p->free--;
            p->next = f + 1;
            return f << 12;
        }
    }
    return 0;
}

void pmm_give(struct pmm *p, u32 phys)
{
    u32 f = FRAME(phys);
    if (f < p->frames && (p->bits[f / 32] & (1u << (f % 32)))) {
        p->bits[f / 32] &= ~(1u << (f % 32));
        p->free++;
    }
}

/* Contiguous runs, for XMS blocks (supervisor.md §16): lowest address first. */
static int is_used(const struct pmm *p, u32 f) { return (p->bits[f / 32] >> (f % 32)) & 1; }

static void mark(struct pmm *p, u32 f, u32 n, int used)
{
    for (; n; n--, f++) {
        u32 w = f / 32, b = 1u << (f % 32);
        if (used && !(p->bits[w] & b)) { p->bits[w] |= b; p->free--; }
        if (!used && (p->bits[w] & b)) { p->bits[w] &= ~b; p->free++; }
    }
}

u32 pmm_take_run(struct pmm *p, u32 n)
{
    u32 f = 0, len = 0;
    if (!n)
        return 0;
    while (f < p->top) {
        if (!(f & 31) && p->bits[f / 32] == 0xFFFFFFFFu) {     /* a whole word in use */
            len = 0;
            f += 32;
            continue;
        }
        len = is_used(p, f) ? 0 : len + 1;
        f++;
        if (len == n) {
            mark(p, f - n, n, 1);
            return (f - n) << 12;
        }
    }
    return 0;
}

int pmm_take_at(struct pmm *p, u32 phys, u32 n)
{
    u32 f = FRAME(phys), i;
    if (f + n > p->frames || f + n < f)
        return -1;
    for (i = 0; i < n; i++)
        if (is_used(p, f + i))
            return -1;
    mark(p, f, n, 1);
    return 0;
}

void pmm_give_run(struct pmm *p, u32 phys, u32 n)
{
    u32 f = FRAME(phys);
    if (f < p->frames)
        mark(p, f, n > p->frames - f ? p->frames - f : n, 0);
}

u32 pmm_largest_run(const struct pmm *p)
{
    u32 f = 0, len = 0, best = 0;
    while (f < p->top) {
        if (!(f & 31) && p->bits[f / 32] == 0xFFFFFFFFu) {
            len = 0;
            f += 32;
            continue;
        }
        len = is_used(p, f) ? 0 : len + 1;
        if (len > best)
            best = len;
        f++;
    }
    return best;
}

/* The highest free frame (the disk cache's: its frames stay out of the way
   of the runs taken from the bottom); 0 if none. */
u32 pmm_take_high(struct pmm *p)
{
    u32 f = p->top;
    while (f--) {
        if (!((f + 1) & 31) && p->bits[f / 32] == 0xFFFFFFFFu) {       /* a whole word in use */
            f -= 31;
            continue;
        }
        if (!is_used(p, f)) {
            mark(p, f, 1, 1);
            return f << 12;
        }
    }
    return 0;
}

/* The lowest window of n frames that are free or, by also(), the disk
   cache's to give back: its first frame's number, or FFFFFFFFh. */
u32 pmm_window(const struct pmm *p, u32 n, int (*also)(u32 frame, void *arg), void *arg)
{
    u32 f, len = 0;
    if (!n)
        return 0xFFFFFFFFu;
    for (f = 0; f < p->top; f++) {
        len = (!is_used(p, f) || also(f, arg)) ? len + 1 : 0;
        if (len == n)
            return f + 1 - n;
    }
    return 0xFFFFFFFFu;
}

/* The longest run of frames free or, by also(), the cache's to give back. */
u32 pmm_longest(const struct pmm *p, int (*also)(u32 frame, void *arg), void *arg)
{
    u32 f, len = 0, best = 0;
    for (f = 0; f < p->top; f++) {
        len = (!is_used(p, f) || also(f, arg)) ? len + 1 : 0;
        if (len > best)
            best = len;
    }
    return best;
}
