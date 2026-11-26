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

/* OHCI's endpoint and transfer descriptors (the OHCI specification rev 1.0a, §4.2 and §4.3.1): 16 bytes each for the
   controller, 16-byte aligned; a TD here is 32 bytes on (its last 16 software's). */
struct ohci_ed {
    volatile u32 info;                  /* address, endpoint, direction, speed, skip, max packet size */
    volatile u32 tailp;                 /* the queue's last TD (a dummy: the controller stops at it) */
    volatile u32 headp;                 /* its first, | C (the toggle carried) and H (halted) */
    volatile u32 next;                  /* the next ED on the list */
};
struct ohci_td {
    volatile u32 info;                  /* buffer rounding, direction, delay interrupt, toggle, errors, condition */
    volatile u32 cbp;                   /* the next byte to move (0: all moved) */
    volatile u32 next;                  /* the next TD (the controller relinks it into its done queue when retired) */
    volatile u32 be;                    /* the buffer's last byte */
    u32 len, sw[3];                     /* (software's: the bytes it was given) */
};

#define OHCI_ED_SKIP    (1u << 14)
#define OHCI_ED_LOW     (1u << 13)
#define OHCI_ED_HALTED  0x1u            /* headp */
#define OHCI_ED_CARRY   0x2u            /* headp: DATA1 next */
#define OHCI_TD_CC(i)   ((i) >> 28)     /* the condition code: 0 no error, 4 stall, 5 no answer, Eh-Fh not yet */
#define OHCI_CC_NONE    0x0u
#define OHCI_CC_STALL   0x4u
#define OHCI_CC_NOT_YET 0xEu            /* (and Fh: not accessed) */

/* An ED's first word: addr, ep, its direction from its TDs (dir 0) or OUT (1) or IN (2), low speed, maxp. */
u32 ohci_ed_info(u32 addr, u32 ep, u32 dir, int low, u32 maxp);
/* A control transfer's TDs (SETUP with DATA0; the data stage in one TD from DATA1, its packets toggling; the status
   stage the other way with DATA1), the last linked to tail_phys (the ED's dummy). How many, or 0. (No I/O.) */
u32 ohci_control_tds(struct ohci_td *td, u32 max, u32 td_phys, const u8 setup[8], u32 setup_phys, u32 data_phys,
                     u32 tail_phys);
/* A bulk transfer's TDs, 4 KB a TD at most, the toggles the ED's; IN ones taking a short packet as the end. How
   many, or 0. */
u32 ohci_bulk_tds(struct ohci_td *td, u32 max, u32 td_phys, int in, u32 data_phys, u32 len, u32 tail_phys);
/* The bytes a retired TD moved. */
u32 ohci_td_moved(const struct ohci_td *td);

/* EHCI's queue head and queue element transfer descriptor (the EHCI specification 1.0, §3.6 and §3.5): a qTD is 32
   bytes, 32-byte aligned, five buffer pages (20 KB at most, less the first page's offset); a QH is 48 bytes for the
   controller (its overlay the qTD under way), 64 here so that an array keeps them 32-byte aligned. */
struct ehci_qtd {
    volatile u32 next;                  /* the next qTD, | T (1: none) */
    volatile u32 alt;                   /* where a short packet goes on to (T: the queue stops) */
    volatile u32 token;                 /* status, PID, errors, the page under way, bytes left, the toggle */
    volatile u32 buf[5];
};
struct ehci_qh {
    volatile u32 link;                  /* the next QH, | type QH (2) | T */
    volatile u32 ep1;                   /* address, endpoint, speed, DTC, H (the list's head), max packet, NAK reload */
    volatile u32 ep2;                   /* (interrupt and split fields) Mult */
    volatile u32 cur;                   /* the controller's: the qTD under way */
    volatile u32 next, alt, token;      /* the overlay: the qTD under way, as the controller has it */
    volatile u32 buf[5];
    u32 sw[4];
};

#define EHCI_T          0x1u
#define EHCI_LINK_QH    0x2u
#define EHCI_TOK_ACTIVE (1u << 7)
#define EHCI_TOK_HALTED (1u << 6)
#define EHCI_TOK_ERRORS 0x7Cu           /* halted, buffer error, babble, transaction error, missed micro-frame */
#define EHCI_TOK_LEFT(t) (((t) >> 16) & 0x7FFF)

/* A high-speed endpoint's QH words: ep1 (head: the asynchronous list's head, H) and ep2. The toggles come from
   the qTDs (DTC), so one QH serves every endpoint in turn. */
u32 ehci_qh_ep1(u32 addr, u32 ep, u32 maxp, int head);
u32 ehci_qh_ep2(void);
/* A control transfer's qTDs: SETUP (DATA0), the data stage in one qTD (DATA1; up to 16 KB), the status stage the
   other way (DATA1, its interrupt-on-complete set); linked, the last's next T. How many, or 0. (No I/O.) */
u32 ehci_control_qtds(struct ehci_qtd *q, u32 max, u32 q_phys, const u8 setup[8], u32 setup_phys, u32 data_phys);
/* A bulk transfer's qTDs, 16 KB each at most (a multiple of every high-speed max packet size, so that only the
   last can end short), each from the toggle its packets before it leave, *toggle the endpoint's; an IN one that
   ends short stops the queue (alt T). How many, or 0. */
u32 ehci_bulk_qtds(struct ehci_qtd *q, u32 max, u32 q_phys, int in, u32 data_phys, u32 len, u32 maxp, u32 toggle);
/* The bytes a qTD moved (asked: what it was given), and an endpoint's toggle after a transfer that moved `moved`
   bytes of `asked` from `toggle` (each packet flips it; a zero-length one too). */
u32 ehci_qtd_moved(const struct ehci_qtd *q, u32 asked);
u32 ehci_toggle_after(u32 toggle, u32 moved, u32 asked, u32 maxp);

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
