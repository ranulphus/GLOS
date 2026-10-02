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
    }
    for (; f < last && f < p->frames; f++) {
        u32 w = f / 32, b = 1u << (f % 32);
        if (used && !(p->bits[w] & b)) { p->bits[w] |= b; p->free--; }
        if (!used && (p->bits[w] & b)) { p->bits[w] &= ~b; p->free++; }
    }
}

void pmm_add_free(struct pmm *p, u32 base, u32 length) { set_range(p, base, length, 0); }
void pmm_reserve(struct pmm *p, u32 base, u32 length) { set_range(p, base, length, 1); }

u32 pmm_take(struct pmm *p)
{
    u32 n, f;
    for (n = 0; n < p->frames; n++) {
        f = (p->next + n) % p->frames;
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
