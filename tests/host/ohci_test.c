/* OHCI's descriptors (kernel/drv/ohcitd.c, docs/v1/usb-storage.md U3): an ED's first word; a control transfer's
 * TDs (SETUP DATA0, the data stage in one TD from DATA1, the status stage the other way); a bulk transfer in 4 KB
 * TDs with the ED's toggles; the bytes a retired TD moved. */
#include <stdio.h>
#include <string.h>
#include "usb.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static u32 dp(const struct ohci_td *t) { return t->info >> 19 & 3; }
static u32 tg(const struct ohci_td *t) { return t->info >> 24 & 3; }

int main(void)
{
    struct ohci_td td[8];
    const u8 get_dev[8] = { 0x80, 6, 0, 1, 0, 0, 18, 0 }, set_addr[8] = { 0x00, 5, 3, 0, 0, 0, 0, 0 };
    u32 n, i;

    CHECK(ohci_ed_info(5, 2, 2, 1, 64) == (5u | 2u << 7 | 2u << 11 | 1u << 13 | 64u << 16));

    memset(td, 0, sizeof td);                   /* GET_DESCRIPTOR(18): SETUP, IN 18 from DATA1, status OUT */
    n = ohci_control_tds(td, 8, 0x1000, get_dev, 0x2000, 0x3000, 0x9000);
    CHECK(n == 3);
    CHECK(dp(&td[0]) == 0 && tg(&td[0]) == 2 && td[0].cbp == 0x2000 && td[0].be == 0x2007);
    CHECK(dp(&td[1]) == 2 && tg(&td[1]) == 3 && td[1].cbp == 0x3000 && td[1].be == 0x3011 && (td[1].info & (1u << 18)));
    CHECK(dp(&td[2]) == 1 && tg(&td[2]) == 3 && td[2].cbp == 0 && td[2].be == 0);
    CHECK(td[0].next == 0x1020 && td[1].next == 0x1040 && td[2].next == 0x9000);
    for (i = 0; i < n; i++)
        CHECK(OHCI_TD_CC(td[i].info) == 0xF && (td[i].info >> 21 & 7) == 7);

    n = ohci_control_tds(td, 8, 0x1000, set_addr, 0x2000, 0x3000, 0x9000);  /* no data: SETUP, status IN */
    CHECK(n == 2 && dp(&td[1]) == 2 && tg(&td[1]) == 3 && td[1].next == 0x9000);

    n = ohci_bulk_tds(td, 8, 0x1000, 1, 0x40000, 10000, 0x9000);  /* 4096, 4096, 1808 */
    CHECK(n == 3 && td[0].cbp == 0x40000 && td[0].be == 0x40FFF && td[2].cbp == 0x42000 && td[2].be == 0x42000 + 1807);
    CHECK(tg(&td[0]) == 0 && dp(&td[0]) == 2 && (td[0].info & (1u << 18)) && td[2].next == 0x9000);
    n = ohci_bulk_tds(td, 8, 0x1000, 0, 0x40000, 31, 0x9000);     /* a CBW: one OUT TD, no rounding */
    CHECK(n == 1 && dp(&td[0]) == 1 && !(td[0].info & (1u << 18)) && td[0].len == 31);
    CHECK(ohci_bulk_tds(td, 2, 0x1000, 1, 0x40000, 10000, 0x9000) == 0);  /* (too many TDs for max) */

    td[0].len = 512, td[0].cbp = 0, td[0].be = 0x401FF;          /* moved: all, none, a short packet's 13 */
    CHECK(ohci_td_moved(&td[0]) == 512);
    td[0].cbp = 0x40000;
    CHECK(ohci_td_moved(&td[0]) == 0);
    td[0].cbp = 0x4000D;
    CHECK(ohci_td_moved(&td[0]) == 13);
    printf("ohci_test: %d failures\n", fails);
    return fails != 0;
}
