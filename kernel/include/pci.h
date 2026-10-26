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
    u32 bar[6];
};

u32 pci_read32(u32 bus, u32 dev, u32 fn, u32 reg);
void pci_write32(u32 bus, u32 dev, u32 fn, u32 reg, u32 v);
int pci_scan(void);                     /* the number of functions found */
struct pci_dev *pci_find(u16 vendor, u16 device, int nth);
struct pci_dev *pci_at(int i);          /* NULL past the end */
void pci_claim(struct pci_dev *d);      /* its keeper is GLOS */
int pci_owned_addr(u32 cf8);            /* 1 if a CF8h address selects a device GLOS keeps */
struct pci_dev *pci_by_addr(u32 cf8);   /* the function a CF8h address selects, or NULL */
int pci_uses_line(const struct pci_dev *d);     /* 1 if it drives an IRQ line (a pin, and a line 1-15) */

#endif
