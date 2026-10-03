/* PCI configuration mechanism 1 (supervisor.md §10): enumeration, and the
 * devices GLOS takes for itself, which the system VM no longer sees. */
#ifndef K_PCI_H
#define K_PCI_H
#include "types.h"

#define PCI_MAX 32

struct pci_dev {
    u8 bus, dev, fn, irq;               /* irq: the interrupt line register (FFh: none) */
    u16 vendor, device;
    u8 class, subclass, progif, owned;
    u32 bar[6];
};

u32 pci_read32(u32 bus, u32 dev, u32 fn, u32 reg);
void pci_write32(u32 bus, u32 dev, u32 fn, u32 reg, u32 v);
int pci_scan(void);                     /* the number of functions found */
struct pci_dev *pci_find(u16 vendor, u16 device, int nth);
struct pci_dev *pci_at(int i);          /* NULL past the end */
void pci_claim(struct pci_dev *d);
int pci_owned_addr(u32 cf8);            /* 1 if a CF8h address selects a claimed device */
int pci_irq_shared(const struct pci_dev *d);    /* 1 if another function has the same IRQ line */

#endif
