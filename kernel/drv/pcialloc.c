/* Places for PCI ranges the BIOS left unplaced (pcialloc.h). */
#include "pcialloc.h"

#define MB 0x100000UL

static int overlaps(unsigned long b, unsigned long l, const struct pa_range *r, int n)
{
    int k;
    for (k = 0; k < n; k++)
        if (r[k].base <= r[k].last && b <= r[k].last && r[k].base <= l)
            return 1;
    return 0;
}

/* The smallest window in whole megabytes holding [b, l] and every keep range: 0, or -1 when it would wrap. */
static int hull(unsigned long b, unsigned long l, const struct pa_range *keep, int nkeep, struct pa_range *w)
{
    int k;
    for (k = 0; k < nkeep; k++)
        if (keep[k].base <= keep[k].last) {
            if (keep[k].base < b)
                b = keep[k].base;
            if (keep[k].last > l)
                l = keep[k].last;
        }
    w->base = b & ~(MB - 1);
    w->last = l | (MB - 1);
    return w->last < l ? -1 : 0;
}

int pa_place(const struct pa_range *busy, int nbusy, const struct pa_range *keep, int nkeep, struct pa_range *win,
             unsigned long size, unsigned long floor, unsigned long ceil, unsigned long *at)
{
    unsigned long a;
    struct pa_range w;
    if (!size || (size & (size - 1)) || ceil <= floor || ceil - floor < size)
        return -1;
    /* in the window as it is, from its top down */
    if (win->base <= win->last && win->last - win->base + 1 >= size) {
        a = (win->last - size + 1) & ~(size - 1);
        for (;; a -= size) {
            if (a < win->base || a < floor)
                break;
            if (a + size - 1 <= win->last && a + size <= ceil && !overlaps(a, a + size - 1, busy, nbusy)
                && !overlaps(a, a + size - 1, keep, nkeep)) {
                *at = a;
                return 0;
            }
            if (a < size)
                break;
        }
    }
    /* anywhere free, the window made to hold it with what it holds */
    a = (ceil - size) & ~(size - 1);
    for (;; a -= size) {
        if (a < floor)
            break;
        if (!overlaps(a, a + size - 1, busy, nbusy) && !overlaps(a, a + size - 1, keep, nkeep)
            && hull(a, a + size - 1, keep, nkeep, &w) == 0 && !overlaps(w.base, w.last, busy, nbusy)) {
            *at = a;
            *win = w;
            return 0;
        }
        if (a < size)
            break;
    }
    return -1;
}
