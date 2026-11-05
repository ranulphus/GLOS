/* uhci_control_tds (kernel/drv/uhcitd.c, docs/gamepad.md J2): the TDs of a control transfer, as Intel's UHCI design
 * guide lays them out and the USB 1.1 specification orders them. */
#include <stdio.h>
#include <string.h>
#include "usb.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static u32 pid(const struct uhci_td *t) { return t->token & 0xFF; }
static u32 addr(const struct uhci_td *t) { return t->token >> 8 & 0x7F; }
static u32 toggle(const struct uhci_td *t) { return t->token >> 19 & 1; }
static u32 maxlen(const struct uhci_td *t) { return ((t->token >> 21) + 1) & 0x7FF; }

int main(void)
{
    struct uhci_td td[16];
    const u8 get_dev[8] = { 0x80, 6, 0, 1, 0, 0, 18, 0 }, set_addr[8] = { 0x00, 5, 3, 0, 0, 0, 0, 0 };
    const u8 get_cfg[8] = { 0x80, 6, 0, 2, 0, 0, 64, 0 };
    u32 n, i;

    /* GET_DESCRIPTOR(device, 18) to address 0 with 8-byte packets, full speed: SETUP, IN x3 (8, 8, 2), status OUT */
    memset(td, 0, sizeof td);
    n = uhci_control_tds(td, 16, 0x1000, get_dev, 0x2000, 0x3000, 0, 8, 0);
    CHECK(n == 5);
    CHECK(pid(&td[0]) == USB_PID_SETUP && toggle(&td[0]) == 0 && maxlen(&td[0]) == 8 && td[0].buf == 0x2000);
    CHECK(pid(&td[1]) == USB_PID_IN && toggle(&td[1]) == 1 && maxlen(&td[1]) == 8 && td[1].buf == 0x3000);
    CHECK(pid(&td[2]) == USB_PID_IN && toggle(&td[2]) == 0 && maxlen(&td[2]) == 8 && td[2].buf == 0x3008);
    CHECK(pid(&td[3]) == USB_PID_IN && toggle(&td[3]) == 1 && maxlen(&td[3]) == 2 && td[3].buf == 0x3010);
    CHECK(pid(&td[4]) == USB_PID_OUT && toggle(&td[4]) == 1 && maxlen(&td[4]) == 0 && (td[4].ctrl & UHCI_TD_IOC));
    for (i = 0; i < n; i++) {
        CHECK(td[i].ctrl & UHCI_TD_ACTIVE);
        CHECK(!(td[i].ctrl & UHCI_TD_LS));
        CHECK(addr(&td[i]) == 0);
        CHECK(i + 1 < n ? td[i].link == ((0x1000 + (i + 1) * 32) | UHCI_LP_VF) : td[i].link == UHCI_LP_T);
    }
    CHECK((td[1].ctrl & UHCI_TD_SPD) && !(td[0].ctrl & UHCI_TD_SPD) && !(td[4].ctrl & UHCI_TD_SPD));
    CHECK(((td[0].token >> 21) & 0x7FF) == 7 && ((td[4].token >> 21) & 0x7FF) == 0x7FF);   /* (n - 1; none 7FFh) */

    /* SET_ADDRESS(3): no data stage, so the status stage is IN */
    memset(td, 0, sizeof td);
    n = uhci_control_tds(td, 16, 0x1000, set_addr, 0x2000, 0x3000, 0, 8, 1);
    CHECK(n == 2);
    CHECK(pid(&td[0]) == USB_PID_SETUP && pid(&td[1]) == USB_PID_IN && toggle(&td[1]) == 1 && maxlen(&td[1]) == 0);
    CHECK((td[0].ctrl & UHCI_TD_LS) && (td[1].ctrl & UHCI_TD_LS));

    /* GET_DESCRIPTOR(configuration, 64) from address 5 with 64-byte packets: one IN; and too few TDs: refused */
    memset(td, 0, sizeof td);
    n = uhci_control_tds(td, 16, 0x1000, get_cfg, 0x2000, 0x3000, 5, 64, 0);
    CHECK(n == 3 && addr(&td[1]) == 5 && maxlen(&td[1]) == 64 && pid(&td[2]) == USB_PID_OUT);
    CHECK(uhci_control_tds(td, 4, 0x1000, get_dev, 0x2000, 0x3000, 0, 8, 0) == 0);

    printf("uhci_test: %d failures\n", fails);
    return fails != 0;
}
