/* UHCI's transfer descriptors for a control transfer (docs/gamepad.md J2): pure, no I/O, host tested
 * (tests/host/uhci_test.c). From Intel's UHCI design guide rev 1.1 §3.2 (the TD's four words) and the USB 1.1
 * specification §8.5.2 (a control transfer: SETUP with DATA0, the data stage's packets toggling from DATA1, the
 * status stage the other way with DATA1 and no data). */
#include "usb.h"

static u32 token(u32 pid, u32 addr, u32 ep, u32 toggle, u32 len)
{
    return pid | (addr & 0x7F) << 8 | (ep & 0xF) << 15 | (toggle & 1) << 19 | ((len ? len - 1 : 0x7FF) & 0x7FF) << 21;
}

u32 uhci_control_tds(struct uhci_td *td, u32 max, u32 td_phys, const u8 setup[8], u32 setup_phys, u32 data_phys,
                     u32 addr, u32 maxp, int low)
{
    u32 in = setup[0] & 0x80, len = setup[6] | (u32)setup[7] << 8, n = 0, done = 0, toggle = 1, i;
    u32 base = UHCI_TD_ACTIVE | UHCI_TD_CERR3 | (low ? UHCI_TD_LS : 0);
    if (!maxp)
        maxp = 8;
    if (max < 2 + (len + maxp - 1) / maxp)
        return 0;
    td[n].ctrl = base;
    td[n].token = token(USB_PID_SETUP, addr, 0, 0, 8);
    td[n].buf = setup_phys;
    n++;
    while (done < len) {
        u32 now = len - done < maxp ? len - done : maxp;
        td[n].ctrl = base | (in ? UHCI_TD_SPD : 0);    /* (a short IN packet stops the queue there: uhci.c) */
        td[n].token = token(in ? USB_PID_IN : USB_PID_OUT, addr, 0, toggle, now);
        td[n].buf = data_phys + done;
        toggle ^= 1;
        done += now;
        n++;
    }
    td[n].ctrl = base | UHCI_TD_IOC;            /* the status stage: the other way, DATA1, no data */
    td[n].token = token(in && len ? USB_PID_OUT : USB_PID_IN, addr, 0, 1, 0);
    td[n].buf = 0;
    n++;
    for (i = 0; i < n; i++)
        td[i].link = i + 1 < n ? (td_phys + (i + 1) * (u32)sizeof *td) | UHCI_LP_VF : UHCI_LP_T;
    return n;
}
