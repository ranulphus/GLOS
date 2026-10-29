/* Host test for the disk cache's store (kernel/drv/cstore.c; docs/disk-cache.md): finding blocks through
 * colliding hash chains, the segmented LRU (promotion, protected's three quarters, the victims' order, pins),
 * removal, and INT 13h's CHS addresses against the BIOS's geometry. */
#include <stdio.h>
#include <string.h>
#include "cstore.h"

#define FRAMES 4096
static struct cblk b[FRAMES];
static u32 h[16];                               /* 4 bits of hash: chains collide */
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int consistent(const struct cstore *s)   /* the lists, the counts and the chains agree */
{
    u32 seg, f, n, total = 0, i;
    for (seg = 0; seg < 2; seg++) {
        u32 prev = CS_NIL;
        for (f = s->head[seg], n = 0; f != CS_NIL; prev = f, f = s->b[f].next, n++) {
            if (!(s->b[f].flags & CB_USED) || s->b[f].seg != seg || s->b[f].prev != prev || n > FRAMES)
                return 0;
        }
        if (n != s->n[seg] || s->tail[seg] != prev)
            return 0;
        total += n;
    }
    for (i = 0, n = 0; i < (1u << s->hbits); i++)
        for (f = s->h[i]; f != CS_NIL; f = s->b[f].hnext)
            if (cs_find(s, s->b[f].key) != f || ++n > FRAMES)
                return 0;
    return n == total;
}

int main(void)
{
    struct cstore s;
    struct cgeo g;
    u32 f, i, lba, cx, dh;

    cs_init(&s, b, FRAMES, h, 4);
    CHECK(cs_blocks(&s) == 0 && cs_victim(&s) == CS_NIL);
    for (i = 0; i < 100; i++)                   /* blocks 0-99 of drive 1 in frames 1000-1099 */
        cs_insert(&s, CS_KEY(1, i * 8), 1000 + i);
    CHECK(cs_blocks(&s) == 100 && consistent(&s));
    for (i = 0; i < 100; i++)
        CHECK(cs_find(&s, CS_KEY(1, i * 8 + 3)) == 1000 + i);   /* any sector of the block finds it */
    CHECK(cs_find(&s, CS_KEY(0, 0)) == CS_NIL && cs_find(&s, CS_KEY(1, 800)) == CS_NIL);
    CHECK(CS_DRIVE(CS_KEY(3, 0x123458)) == 3 && CS_LBA(CS_KEY(3, 0x123458)) == 0x123458);
    CHECK(cs_victim(&s) == 1000);               /* the oldest goes first */

    /* Hits: read again, a block goes to protected; protected keeps at most three quarters. */
    cs_hit(&s, 1000);
    CHECK(s.n[1] == 1 && b[1000].seg == 1 && cs_victim(&s) == 1001 && consistent(&s));
    for (i = 0; i < 100; i++)
        cs_hit(&s, 1000 + i);
    CHECK(s.n[1] == 75 && s.n[0] == 25 && consistent(&s));
    CHECK(cs_victim(&s) == 1000);               /* (protected's oldest were demoted to probation, in order) */

    /* A streaming read: new blocks come and go in probation; protected's stay. */
    for (i = 0; i < 25; i++)
        cs_remove(&s, cs_victim(&s));
    for (i = 0; i < 500; i++) {
        f = 2000 + (i % 25);
        if (b[f].flags & CB_USED)
            cs_remove(&s, f);
        cs_insert(&s, CS_KEY(2, i * 8), f);
    }
    CHECK(s.n[1] == 75 && s.n[0] == 25 && consistent(&s));
    for (i = 25; i < 100; i++)
        CHECK(cs_find(&s, CS_KEY(1, i * 8)) == 1000 + i);

    /* Pins: a pinned block is never the victim. */
    f = cs_victim(&s);
    b[f].flags |= CB_PINNED;
    CHECK(cs_victim(&s) != f && cs_victim(&s) != CS_NIL);
    b[f].flags &= (u8)~CB_PINNED;
    CHECK(cs_victim(&s) == f);

    /* Removal from the middle of colliding chains, and of everything. */
    for (i = 30; i < 100; i += 7)
        cs_remove(&s, 1000 + i);
    CHECK(consistent(&s));
    for (i = 30; i < 100; i++)
        CHECK(cs_find(&s, CS_KEY(1, i * 8)) == ((i - 30) % 7 ? 1000 + i : CS_NIL));
    while ((f = cs_victim(&s)) != CS_NIL)
        cs_remove(&s, f);
    CHECK(cs_blocks(&s) == 0 && consistent(&s));
    for (i = 0; i < 16; i++)
        CHECK(h[i] == CS_NIL);

    /* CHS: the BIOS's translated geometry (86Box's 64 MB scratch disk: 65/32/63) and a big one (1024/255/63). */
    g.cyls = 65, g.heads = 32, g.spt = 63;
    CHECK(cs_chs_lba(&g, 0x0001, 0, &lba) == 0 && lba == 0);
    CHECK(cs_chs_lba(&g, (1 << 8) | 2, 3, &lba) == 0 && lba == (1 * 32 + 3) * 63 + 1);
    CHECK(cs_chs_lba(&g, 0x0000, 0, &lba) != 0);        /* sector 0 */
    CHECK(cs_chs_lba(&g, 0x0040, 0, &lba) != 0);        /* sector 0, cylinder 256 */
    CHECK(cs_chs_lba(&g, 0x0001, 32, &lba) != 0);       /* head 32 */
    CHECK(cs_chs_lba(&g, (65 << 8) | 1, 0, &lba) != 0); /* cylinder 65 */
    for (lba = 0; lba < 65u * 32 * 63; lba += 37) {
        u32 back;
        cs_lba_chs(&g, lba, &cx, &dh);
        CHECK(cs_chs_lba(&g, cx, dh, &back) == 0 && back == lba);
    }
    g.cyls = 1024, g.heads = 255, g.spt = 63;
    cs_lba_chs(&g, 1023u * 255 * 63 + 254 * 63 + 62, &cx, &dh);
    CHECK(cx == ((0xFF << 8) | 0xC0 | 63) && dh == 254);
    CHECK(cs_chs_lba(&g, cx, dh, &lba) == 0 && lba == 1024u * 255 * 63 - 1);

    printf("%s: %s\n", __FILE__, fails ? "FAILED" : "ok");
    return fails != 0;
}
