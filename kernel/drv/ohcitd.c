/* OHCI's endpoint and transfer descriptors (docs/v1/usb-storage.md, U3): pure, no I/O, host tested
 * (tests/host/ohci_test.c). From the OHCI specification rev 1.0a §4.2 (the ED's four words) and §4.3.1 (the general
 * TD's: R, DP, DI, T, EC, CC; CBP, NextTD, BE), and USB 1.1 §8.5.2 (a control transfer's stages and toggles). Every
 * TD has DI 7 (no interrupt: GLOS polls a TD's condition code, and the done queue is not read). */
#include "usb.h"

#define TD_R        (1u << 18)          /* buffer rounding: a short packet is not an error */
#define TD_SETUP    (0u << 19)
#define TD_OUT      (1u << 19)
#define TD_IN       (2u << 19)
#define TD_DI_NONE  (7u << 21)
#define TD_T_ED     (0u << 24)          /* the toggle from the ED's carry */
#define TD_T_DATA0  (2u << 24)
#define TD_T_DATA1  (3u << 24)
#define TD_CC_NEW   (0xFu << 28)        /* not accessed */

u32 ohci_ed_info(u32 addr, u32 ep, u32 dir, int low, u32 maxp)
{
    return (addr & 0x7F) | (ep & 0xF) << 7 | (dir & 3) << 11 | (low ? OHCI_ED_LOW : 0) | (maxp & 0x7FF) << 16;
}

static void td_set(struct ohci_td *t, u32 info, u32 buf, u32 len, u32 next)
{
    t->info = info | TD_DI_NONE | TD_CC_NEW;
    t->cbp = len ? buf : 0;
    t->be = len ? buf + len - 1 : 0;
    t->next = next;
    t->len = len;
}

u32 ohci_control_tds(struct ohci_td *td, u32 max, u32 td_phys, const u8 setup[8], u32 setup_phys, u32 data_phys,
                     u32 tail_phys)
{
    u32 in = setup[0] & 0x80, len = setup[6] | (u32)setup[7] << 8, n = 0, i;
    if (max < 3)
        return 0;
    td_set(&td[n++], TD_SETUP | TD_T_DATA0, setup_phys, 8, 0);
    if (len)
        td_set(&td[n++], (in ? TD_IN | TD_R : TD_OUT) | TD_T_DATA1, data_phys, len, 0);
    td_set(&td[n++], (in && len ? TD_OUT : TD_IN) | TD_T_DATA1, 0, 0, 0);     /* the status stage: no data */
    for (i = 0; i < n; i++)
        td[i].next = i + 1 < n ? td_phys + (i + 1) * (u32)sizeof *td : tail_phys;
    return n;
}

u32 ohci_bulk_tds(struct ohci_td *td, u32 max, u32 td_phys, int in, u32 data_phys, u32 len, u32 tail_phys)
{
    u32 n = 0, done = 0, i;
    if (!len || max < (len + 4095) / 4096)
        return 0;
    while (done < len) {                        /* (4 KB a TD: never more than one page crossed) */
        u32 now = len - done < 4096 ? len - done : 4096;
        td_set(&td[n++], (in ? TD_IN | TD_R : TD_OUT) | TD_T_ED, data_phys + done, now, 0);
        done += now;
    }
    for (i = 0; i < n; i++)
        td[i].next = i + 1 < n ? td_phys + (i + 1) * (u32)sizeof *td : tail_phys;
    return n;
}

u32 ohci_td_moved(const struct ohci_td *td)
{
    if (!td->len || !td->cbp)
        return td->len;                         /* (all of it, or none was asked) */
    return td->len - (td->be - td->cbp + 1);    /* (what was left: from the next byte to the last) */
}
