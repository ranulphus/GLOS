/* GLOS's USB stack (docs/gamepad.md J2-J6, PRD D74): UHCI host controllers, the devices on their root ports, and
 * (later) hubs, HID keyboards, mice and pads, and Xbox pads. */
#ifndef K_USB_H
#define K_USB_H
#include "types.h"
#include "pci.h"

/* UHCI transfer descriptors and queue heads, as the controller reads them (Intel's UHCI design guide rev 1.1, §3). */
struct uhci_td {
    volatile u32 link;                  /* the next TD or QH (physical), | T, Q, Vf */
    volatile u32 ctrl;                  /* status, actual length, LS, C_ERR, IOC, SPD */
    volatile u32 token;                 /* PID, device address, endpoint, data toggle, maximum length */
    volatile u32 buf;                   /* the buffer (physical) */
    u32 sw[4];                          /* (software's: 32 bytes a TD) */
};

struct uhci_qh {
    volatile u32 head;                  /* the next QH (physical), | T, Q */
    volatile u32 elem;                  /* its first TD (physical), | T */
    u32 sw[2];
};

#define UHCI_LP_T       0x1u            /* link pointers: terminate */
#define UHCI_LP_Q       0x2u            /* ... a QH, not a TD */
#define UHCI_LP_VF      0x4u            /* ... depth first */

#define UHCI_TD_SPD     (1u << 29)      /* ctrl: short packet detect */
#define UHCI_TD_CERR3   (3u << 27)      /* ... three errors before the TD stops */
#define UHCI_TD_LS      (1u << 26)      /* ... a low-speed device */
#define UHCI_TD_IOC     (1u << 24)
#define UHCI_TD_ACTIVE  (1u << 23)
#define UHCI_TD_STALLED (1u << 22)
#define UHCI_TD_ERRORS  (0x7Eu << 16)   /* stalled, data buffer, babble, NAK, CRC/timeout, bitstuff */
#define UHCI_TD_ACTLEN(c) (((c) + 1) & 0x7FFu)   /* bytes moved (the field holds n - 1; 7FFh: none) */

#define USB_PID_SETUP   0x2Du
#define USB_PID_IN      0x69u
#define USB_PID_OUT     0xE1u

/* A control transfer's TDs, built into td[] (at most max of them, td_phys the first's physical address, each 32
   bytes on): SETUP from setup_phys, the data stage in packets of maxp at data_phys (IN when setup[0] bit 7), and
   the status stage the other way, all linked depth first. Returns how many, or 0 if max is too few. (No I/O: host
   tested.) */
u32 uhci_control_tds(struct uhci_td *td, u32 max, u32 td_phys, const u8 setup[8], u32 setup_phys, u32 data_phys,
                     u32 addr, u32 maxp, int low);

/* (USB mass storage, U1) A bulk transfer's TDs: len bytes in packets of maxp to or from data_phys on endpoint ep,
   the toggles from *toggle on (left at the next one); IN with short packet detect; the last with IOC. How many, or
   0. (No I/O: host tested.) */
u32 uhci_bulk_tds(struct uhci_td *td, u32 max, u32 td_phys, int in, u32 data_phys, u32 len, u32 addr, u32 ep,
                  u32 maxp, u32 *toggle);

struct usb_dev {
    u8 used, addr, port, low;
    u8 maxp0, config, nif;
    u16 vendor, product, release;
    u8 dclass, dsub, dproto;            /* the device descriptor's */
    u8 iclass, isub, iproto;            /* its first interface's */
    u8 ep_in, ep_in_maxp, ep_in_ival;   /* its first interrupt IN endpoint (0: none) */
    u8 ep_out, ep_out_maxp;
    u8 ifnum;                           /* its first interface's number */
    u16 hid_rlen;                       /* a HID interface's report descriptor's length (0: none) */
    const char *is;                     /* hid-keyboard, hid-mouse, hid, xbox360, xbox-gip, hub, other */
    /* an interrupt IN pipe (a pad's: J4) */
    u8 pipe, toggle, pad;               /* pipe: its slot + 1 (0 none); pad: the joystick pad it is + 1 */
    u8 kind, toggle_out;                /* kind: 0 a HID pad, XPAD_360 or XPAD_GIP (xpad.h), or USB_KIND_ */
    /* where it is: on a root port (port, parent 0), or on port hport of the hub that is device parent - 1 */
    u8 hc_i, parent, hport;
    u8 mouse_desc;                      /* a mouse read by its report descriptor (else its boot reports) */
    u8 owner;                           /* another of the interfaces of the device that is dev[owner - 1] (a
                                           combined keyboard and mouse's mouse, say); 0: the device itself */
    /* a hub's (J3) */
    u8 nports, hub_dirty;               /* its ports (8 at most); a change came on its status pipe */
    u8 child[8], ctries[8];             /* the device on each of its ports (index + 1), attempts at it */
    u32 reports, errors;
    /* a mass-storage interface's (U1: docs/v1/usb-storage.md): its bulk endpoints, their toggles, its disk */
    u8 ep_bin, ep_bout, tog_bin, tog_bout;
    u16 ep_bin_maxp, ep_bout_maxp;
    u8 store;                           /* its disk: the storage slot + 1 (0 none) */
};

#define USB_KIND_KBD    3               /* a boot keyboard: its keys as PS/2 ones (usbkbd.c) */
#define USB_KIND_MOUSE  4               /* a boot mouse: as a PS/2 one */

void usb_kbd_leds(u8 ps2);              /* the LEDs a program set (EDh's byte: 1 Scroll, 2 Num, 4 Caps Lock) */
void usb_late(void);                    /* after GLOS.CFG: [usb] enabled, every UHCI taken */
void usb_leave(void);                   /* glos exit: each controller stopped, reset and its registers given back */
/* (phase H, 2026-11-23; kernel/drv/ehci.c) Each EHCI taken from the BIOS and its ports given to its UHCI
   companions (before UHCI takes them); at exit given back as the BIOS had it. */
void ehci_take_all(void);
void ehci_leave(void);
int usb_stat(char *b, u32 room);        /* glos usb */
int usb_off(void);                      /* [usb] enabled = off */
int usb_left(void);                     /* left to the BIOS: drive C: is a disk of the BIOS's (a USB stick's start) */
u32 usb_hc_detail(const struct pci_dev *pd, char *b, u32 room);  /* the Devices list's: a controller of GLOS's */
u32 usb_dev_lines(char *b, u32 room);   /* ... every device plugged in, a line each */

#endif
