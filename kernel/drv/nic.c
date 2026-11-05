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
static struct pci_dev *pdev;            /* the PCI card nic_pci_on set going, and its command register before */
static u32 pcmd;

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

void nic_pci_on(struct pci_dev *d)
{
    pdev = d;
    pcmd = pci_read32(d->bus, d->dev, d->fn, 4) & 0xFFFF;
    pci_write32(d->bus, d->dev, d->fn, 4, pcmd | 0x05);    /* I/O, bus master */
}

/* The card stopped before GLOS goes: left running, a PCI card went on writing
   the frames it received into GLOS's memory, DOS's again by then, and an
   Intel 8255x still at work may hang on the next driver's reset. Its PCI
   command register goes back as it was; the status half written as 0
   clears nothing. */
void nic_leave(void)
{
    if (nic0 && nic0->stop)
        nic0->stop(nic0);
    if (pdev)
        pci_write32(pdev->bus, pdev->dev, pdev->fn, 4, pcmd);
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
