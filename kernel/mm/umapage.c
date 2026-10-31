/* Which upper-memory page GLOS's stub may take (docs/upper-memory.md, Which page). A 4 KB page from C8000h to
 * EFFFFh qualifies when every byte reads FFh twice (the bus with nothing on it), no option ROM (55AAh at a
 * 2 KB boundary from C0000h, with its length byte) touches it, and no PCI BAR or expansion-ROM BAR starts in
 * it. BARs aren't sized here (that takes a write), so a claim covers the page it starts in. */
#include "umapage.h"

static u8 a[4096], b[4096];

static int rom_over(const struct upage_env *e, u32 page)
{
    u32 at;
    u8 h[3];
    for (at = UP_LO; at < page + 4096; ) {
        e->read(at, h, 3);
        if (h[0] == 0x55 && h[1] == 0xAA && h[2]) {
            if (at + h[2] * 512u > page)        /* it starts at or before the page and reaches into it */
                return 1;
            at += (h[2] * 512u + 2047) & ~2047u;
        } else {
            at += 2048;
        }
    }
    return 0;
}

int upage_check(const struct upage_env *e, u32 phys)
{
    u32 i;
    if ((phys & 4095) || phys < UP_FIRST || phys >= UP_END)
        return UP_RANGE;
    if (rom_over(e, phys))
        return UP_ROM;
    for (i = 0; i < e->nclaims; i++)
        if ((e->claims[i] & ~4095u) == phys)
            return UP_BAR;
    e->read(phys, a, 4096);
    e->read(phys, b, 4096);
    for (i = 0; i < 4096; i++)
        if (a[i] != 0xFF || b[i] != 0xFF)
            return UP_DATA;
    return UP_OK;
}

u32 upage_auto(const struct upage_env *e)
{
    u32 p;
    for (p = UP_FIRST; p < UP_AUTO_END; p += 4096)
        if (upage_check(e, p) == UP_OK)
            return p;
    return 0;
}

const char *upage_why(int r)
{
    static const char *const why[] = { "ok", "range", "rom", "bar", "data" };
    return r >= 0 && r <= UP_DATA ? why[r] : "?";
}
