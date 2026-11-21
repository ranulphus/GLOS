/* PCI configuration mechanism 1 (supervisor.md §10): enumeration, and the
 * devices GLOS takes for itself, which the system VM no longer sees. */
#ifndef K_PCI_H
#define K_PCI_H
#include "types.h"

#define PCI_MAX 32

/* Who keeps a device (docs/devices.md). */
#define DEV_DOS   0                     /* left as the BIOS set it up, for DOS's drivers and programs */
#define DEV_GLOS  1                     /* one of GLOS's drivers serves it: DOS sees no function there */
#define DEV_QUIET 2                     /* nobody drives it: its interrupts are kept off (kernel/drv/dev.c) */

struct pci_dev {
    u8 bus, dev, fn, irq;               /* irq: the interrupt line register (FFh: none) */
    u16 vendor, device;
    u8 class, subclass, progif, keeper;
    u8 pin;                             /* the interrupt pin: 0 none (the line register means nothing), 1-4 INTA-D */
    u8 hdr, rev;                        /* the header's type (0 a device, 1 a PCI bridge, 2 a CardBus bridge); revision */
    u16 sub_vendor, sub_device;         /* a device's subsystem (the board's maker and model; 0 none) */
    u32 bar[6];                         /* its base address registers (a bridge has 2, a CardBus bridge 1: the rest 0) */
    u32 bar_size[6];                    /* ... their sizes, found as GLOS started (0: none, or a 64-bit BAR's upper half) */
};

u32 pci_read32(u32 bus, u32 dev, u32 fn, u32 reg);
void pci_write32(u32 bus, u32 dev, u32 fn, u32 reg, u32 v);
int pci_scan(void);                     /* the number of functions found */
struct pci_dev *pci_find(u16 vendor, u16 device, int nth);
struct pci_dev *pci_at(int i);          /* NULL past the end */
void pci_claim(struct pci_dev *d);      /* its keeper is GLOS */
int pci_owned_addr(u32 cf8);            /* 1 if a CF8h address selects a device GLOS keeps */
struct pci_dev *pci_by_addr(u32 cf8);   /* the function a CF8h address selects, or NULL */
u32 pci_lfb_range(u32 *base, const struct pci_dev **card);     /* the video card's framebuffer: its size, 0 none */
char *pci_where(const struct pci_dev *d, char *b);      /* "00:03.0" (b: 8 bytes at least) */
int pci_video_mem(u32 base, u32 bytes); /* 1 if all of it is inside one memory BAR of a video card's */
u32 pci_low_claims(u32 *out, u32 max);  /* where memory and expansion-ROM BARs start below 1 MB; how many */
int pci_uses_line(const struct pci_dev *d);     /* 1 if it drives an IRQ line (a pin, and a line 1-15) */

#endif
