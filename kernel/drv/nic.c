/* The network card GLOS takes (PRD §8.1): the first of an RTL8139, an Intel
 * 8255x and an NE2000 that is there and can be had.
 *
 * None is taken when a packet driver is loaded (it owns the card). A PCI
 * card is refused when it has no interrupt line, or when another function
 * shares the line and can't be made quiet (kernel/drv/dev.c: the IRQ would
 * no longer reach the device's DOS driver; docs/devices.md). */
#include "dev.h"
#include "io.h"
#include "kprintf.h"
#include "nic.h"
#include "pci.h"
#include "vm.h"

struct nic *nic0;

/* A packet driver's signature at any of the vectors they use: the vector. */
static int packet_driver(void)
{
    u32 v, x, lin;
    for (v = 0x60; v <= 0x80; v++) {
        x = vm_rd32(v * 4);
        lin = (x >> 16) * 16 + (x & 0xFFFF) + 3;
        if (x && lin + 8 < 0x110000 && !memcmp(vm_ptr(lin), "PKT DRVR", 8))
            return (int)v;
    }
    return 0;
}

int nic_pci(struct pci_dev *d, const char *name)
{
    char with[12];
    if (d->irq == 0 || d->irq == 2 || d->irq >= 16) {
        kprintf("GLOS-NET refuse card=%s reason=no-irq irq=%u\n", name, d->irq);
        return -1;
    }
    if (dev_take_line(d, with, sizeof with) != 0) {     /* (others on its line: quiet, or it's refused) */
        kprintf("GLOS-NET refuse card=%s reason=shared-irq irq=%u with=%s\n", name, d->irq, with);
        return -1;
    }
    return 0;
}

void nic_claimed(struct nic *n, struct pci_dev *d)
{
    vm_claim_irq(n->irq, net_irq);
    if (d)
        pci_claim(d);
    nic0 = n;
    kprintf("GLOS-NET card=%s base=%x irq=%u mac=%02x:%02x:%02x:%02x:%02x:%02x\n", n->name, n->base, n->irq, n->mac[0],
            n->mac[1], n->mac[2], n->mac[3], n->mac[4], n->mac[5]);
}

int nic_probe(void)
{
    int pd = packet_driver();
    if (pd) {
        kprintf("GLOS-NET refuse reason=packet-driver int=%02x\n", pd);
        return -1;
    }
    return rtl8139_probe() == 0 || e100_probe() == 0 || ne2k_probe() == 0 ? 0 : -1;
}
