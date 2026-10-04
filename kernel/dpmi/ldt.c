/* Descriptors and selectors (supervisor.md §12.1, §13: 0000h-000Dh). Each
 * context has an LDT of 8192 entries that only the host writes. A client's
 * selector has TI set and RPL 3; 0000h hands out indices from 16 up, so
 * selectors 04h-7Ch stay free for 000Dh. A descriptor a client sets must be
 * a code or data segment of DPL 3: no gates, no system segments. The base
 * is not checked: a segment over the kernel's addresses still meets its
 * supervisor-only pages. */
#include "arch.h"
#include "dpmi.h"
#include "kprintf.h"
#include "mm.h"

static u32 *slot(u16 sel) { return &dctx->ldt[(sel >> 3) * 2]; }

static void put(u32 i, u32 base, u32 limit, u8 access, u8 flags)
{
    u32 *d = &dctx->ldt[i * 2];
    if (limit > 0xFFFFF) {
        limit >>= 12;
        flags |= 0x80;                          /* G: in pages */
    } else {
        flags &= (u8)~0x80;
    }
    d[0] = (limit & 0xFFFF) | (base << 16);
    d[1] = ((base >> 16) & 0xFF) | ((u32)access << 8) | (limit & 0xF0000) | ((u32)(flags & 0xF0) << 16)
         | (base & 0xFF000000u);
}

void ldt_init(struct dpmi_ctx *c)
{
    memset(c->ldt, 0, LDT_ENTRIES * 8);
    memset(c->ldt_used, 0, LDT_ENTRIES);
    c->ldt_used[0] = 1;
}

int ldt_alloc(u32 n)
{
    u32 i, k;
    if (!n)
        return -1;
    for (i = SEL_FIRST; i + n <= LDT_ENTRIES; i++) {
        for (k = 0; k < n && !dctx->ldt_used[i + k]; k++) ;
        if (k == n) {
            for (k = 0; k < n; k++)
                dctx->ldt_used[i + k] = 1;
            return (int)i;
        }
        i += k;
    }
    return -1;
}

u16 ldt_new(u32 base, u32 limit, u8 access, u8 flags)
{
    int i = ldt_alloc(1);
    if (i < 0)
        return 0;
    put((u32)i, base, limit, access, flags);
    return (u16)((u32)i * 8 | 7);
}

int ldt_valid(u16 sel)
{
    u32 i = sel >> 3;
    return (sel & 4) && i < LDT_ENTRIES && dctx && dctx->ldt_used[i];
}

int ldt_free(u16 sel)
{
    if (!ldt_valid(sel))
        return -1;
    dctx->ldt_used[sel >> 3] = 0;
    slot(sel)[0] = slot(sel)[1] = 0;
    return 0;
}

static void get(u16 sel, u32 *lo, u32 *hi)
{
    if (sel & 4) {
        if (ldt_valid(sel)) {
            *lo = slot(sel)[0];
            *hi = slot(sel)[1];
            return;
        }
        *lo = *hi = 0;
        return;
    }
    *hi = cpu_desc_hi(sel);                     /* the GDT's ring-3 selectors (BIOS, trampoline data) */
    *lo = 0;
    if (sel == SEL_BIOS)
        *lo = 0xFFFF | (0x400u << 16);
    else if (sel == SEL_TRAMPD)
        *lo = 0xFFF | ((TRAMP_LIN & 0xFFFF) << 16);
    else
        *hi = 0;
}

u32 sel_base(u16 sel)
{
    u32 lo, hi;
    get(sel, &lo, &hi);
    return (lo >> 16) | ((hi & 0xFF) << 16) | (hi & 0xFF000000u);
}

u32 sel_limit(u16 sel)
{
    u32 lo, hi, l;
    get(sel, &lo, &hi);
    l = (lo & 0xFFFF) | (hi & 0xF0000);
    return (hi & (1u << 23)) ? (l << 12) | 0xFFF : l;
}

void sel_set_base(u16 sel, u32 base)
{
    u32 *d = slot(sel);
    d[0] = (d[0] & 0xFFFF) | (base << 16);
    d[1] = (d[1] & 0x00FFFF00u) | ((base >> 16) & 0xFF) | (base & 0xFF000000u);
}

void sel_set_limit(u16 sel, u32 limit)
{
    u32 *d = slot(sel), hi = d[1];
    if (limit > 0xFFFFF) {
        limit >>= 12;
        hi |= 1u << 23;
    } else {
        hi &= ~(1u << 23);
    }
    d[0] = (d[0] & 0xFFFF0000u) | (limit & 0xFFFF);
    d[1] = (hi & ~0xF0000u) | (limit & 0xF0000);
}

int sel_set_desc(u16 sel, u32 lo, u32 hi)
{
    u32 access = (hi >> 8) & 0xFF;
    if (!ldt_valid(sel) || !(access & 0x10) || ((access >> 5) & 3) != 3)
        return -1;                              /* a system descriptor or a gate, or not DPL 3 */
    slot(sel)[0] = lo;
    slot(sel)[1] = hi;
    return 0;
}

void sel_get_desc(u16 sel, u32 *lo, u32 *hi) { get(sel, lo, hi); }

/* sel:off and the len bytes after it, inside the segment (expand-down data
   segments included): the linear address. */
int sel_lin(u16 sel, u32 off, u32 len, u32 *lin)
{
    u32 lo, hi, limit, end = off + (len ? len - 1 : 0);
    get(sel, &lo, &hi);
    if (!(hi & 0x8000) || !(hi & 0x1000) || end < off)  /* not present, not code or data, or wraps */
        return -1;
    limit = sel_limit(sel);
    if ((hi & 0x0C00) == 0x0400) {              /* expand-down data */
        u32 top = (hi & (1u << 22)) ? 0xFFFFFFFFu : 0xFFFF;
        if (off <= limit || end > top)
            return -1;
    } else if (end > limit) {
        return -1;
    }
    *lin = sel_base(sel) + off;
    return 0;
}

/* The host's own copies reach only what the client may: conventional
   memory and the HMA, the user region, the PCI window. A segment over the
   kernel would otherwise have the host write there for it. */
static int user_range(u32 lin, u32 n)
{
    u32 end = lin + n;
    if (end < lin)
        return 0;
    return end <= 0x110000 || (lin >= USER_BASE && end <= USER_END) || (lin >= PHYS_WINDOW && end <= 0xFFC00000u);
}

int user_rd(u16 sel, u32 off, void *dst, u32 n)
{
    u32 lin;
    if (sel_lin(sel, off, n, &lin) != 0 || !user_range(lin, n))
        return -1;
    return ucopy(dst, (const void *)lin, n) ? -1 : 0;
}

int user_wr(u16 sel, u32 off, const void *src, u32 n)
{
    u32 lin;
    if (sel_lin(sel, off, n, &lin) != 0 || !user_range(lin, n))
        return -1;
    return ucopy((void *)lin, src, n) ? -1 : 0;
}
