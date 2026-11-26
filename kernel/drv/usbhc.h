/* GLOS's USB stack's host controllers, for the files that drive one kind each (kernel/drv/uhci.c, which also has
 * everything above the controllers: enumeration, hubs, HID, pads, disks; kernel/drv/ohci.c). Internal: not a
 * kernel-wide header. */
#ifndef K_USBHC_H
#define K_USBHC_H
#include "pci.h"
#include "sched.h"
#include "usb.h"

#define MAX_HC          4
#define MAX_DEV         16
#define MAX_PORTS       16              /* a root hub's ports (UHCI has 2; OHCI up to 15) */
#define NTD             40
#define NPIPE           8               /* interrupt IN pipes, a controller */

/* (U3, 2026-11-26) What each kind of host controller does for the rest (enumeration, hubs, HID, pads, disks: all
   of this file but the UHCI part): UHCI's below, OHCI's in ohci.c. Control transfers come one at a time (the
   caller holds the lock); a pipe's report, bulk pieces from and into h->bulk_buf, the root ports. */
struct hc;
struct hcd_ops {
    const char *name;
    int (*control)(struct hc *h, u32 addr, u32 maxp, int low, const u8 setup[8], void *data, u32 *got, u32 *status);
    void (*intr_arm)(struct hc *h, struct usb_dev *d);         /* the pipe's next report asked for */
    int (*intr_done)(struct hc *h, struct usb_dev *d, const u8 **r, u32 *len, u32 *status);  /* 0 not yet, 1 a
                                                   report (r, len; the toggle the controller's or flipped here),
                                                   -1 an error (status) */
    void (*intr_off)(struct hc *h, struct usb_dev *d);
    int (*intr_out)(struct hc *h, struct usb_dev *d, const u8 *data, u32 len, u32 *status);  /* 0, or -1 */
    int (*bulk_ready)(struct hc *h);                            /* h->bulk_buf made: 0, or -1 */
    int (*bulk)(struct hc *h, struct usb_dev *d, int in, u32 n, u32 *got);     /* as msc_ops' bulk, n <= BULK_BUF */
    int (*port_changed)(struct hc *h, int p);                  /* 1: its connection changed (the change cleared) */
    int (*port_connected)(struct hc *h, int p);
    int (*port_reset)(struct hc *h, int p, int *low);          /* reset and enabled: 0 (low: its speed), or -1 */
    void (*leave)(struct hc *h);                                /* glos exit: as the BIOS had it */
    u32 (*stat)(struct hc *h, char *b, u32 room);              /* glos usb's line for it */
    u32 (*detail)(struct hc *h, char *b, u32 room);            /* the Devices list's */
};

struct hc {
    const struct hcd_ops *ops;
    void *priv;                         /* another kind's own (OHCI's: ohci.c) */
    struct pci_dev *pci;
    u16 io;
    u32 legsup;                         /* as found, for usb_leave */
    u32 *frames;                        /* the frame list (1024 links) */
    u32 frames_phys;
    u8 *pool;                           /* a page: the control QH, the TDs, the setup packet and a data buffer */
    u32 pool_phys;
    struct uhci_qh *qh_ctrl, *qh_int;   /* the control QH; the pipes' QHs (NPIPE), in the frames' chain first */
    struct uhci_td *td, *td_int;        /* a control transfer's TDs; a TD a pipe */
    u8 *setup, *data, *int_buf;         /* the setup packet, a control transfer's data, 64 bytes a pipe */
    u8 ports, slow_reset;
    u8 port_dev[MAX_PORTS];             /* the device on each root port: its index + 1, 0 none */
    u8 tries[MAX_PORTS];                /* attempts at the device there since it connected (3 at most) */
    /* (U1) control transfers from more than one thread (the usb thread's, a disk's reader's): one at a time */
    volatile u8 ctrl_busy, bulk_busy;
    struct waitq ctrl_q, bulk_q;
    /* bulk transfers (a mass-storage device's), a block of their own: the QH after the control one, BULK_TDS TDs
       and a BULK_BUF buffer; one transfer at a time (bulk_busy) */
    u8 *bulk;
    u32 bulk_phys;
    struct uhci_qh *qh_bulk;
    struct uhci_td *td_bulk;
    u8 *bulk_buf;
};
#define BULK_TDS 256
#define BULK_BUF 16384                  /* (256 packets of 64) */
#define BPHYS(h, p) ((h)->bulk_phys + (u32)((const u8 *)(p) - (h)->bulk))

void usb_wait_ms(u32 ms);               /* (uhci.c's wait: a sleep, or at glos exit a busy wait) */
void usb_nap(void);                     /* a tick's wait in a transfer (on the VM's thread: vdos_disk_breathe) */

#endif
