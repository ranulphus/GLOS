/* PCI configuration mechanism 1 (supervisor.md §10). Each access writes the
 * address to CF8h and uses CFCh with interrupts off, so the system VM's
 * own accesses (vm/vdev.c, which rewrites CF8h before every data access)
 * never interleave with the kernel's. */
#include "io.h"
#include "kprintf.h"
#include "pci.h"

static struct pci_dev devs[PCI_MAX];
static int ndevs;

static u32 irq_off(void)
{
    u32 f;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(f) :: "memory");
    return f;
}

static void irq_on(u32 f)
{
    if (f & 0x200)
        sti();
}

static u32 addr(u32 bus, u32 dev, u32 fn, u32 reg)
{
    return 0x80000000u | (bus << 16) | (dev << 11) | (fn << 8) | (reg & 0xFC);
}

u32 pci_read32(u32 bus, u32 dev, u32 fn, u32 reg)
{
    u32 f = irq_off(), v;
    outl(0xCF8, addr(bus, dev, fn, reg));
    v = inl(0xCFC);
    irq_on(f);
    return v;
}

void pci_write32(u32 bus, u32 dev, u32 fn, u32 reg, u32 v)
{
    u32 f = irq_off();
    outl(0xCF8, addr(bus, dev, fn, reg));
    outl(0xCFC, v);
    irq_on(f);
}

static void add(u32 bus, u32 dev, u32 fn)
{
    struct pci_dev *d = &devs[ndevs++];
    u32 id = pci_read32(bus, dev, fn, 0), cls = pci_read32(bus, dev, fn, 8), i;
    d->bus = (u8)bus;
    d->dev = (u8)dev;
    d->fn = (u8)fn;
    d->vendor = (u16)id;
    d->device = (u16)(id >> 16);
    d->class = (u8)(cls >> 24);
    d->subclass = (u8)(cls >> 16);
    d->progif = (u8)(cls >> 8);
    i = pci_read32(bus, dev, fn, 0x3C);
    d->irq = (u8)i;
    d->pin = (u8)(i >> 8);
    d->keeper = DEV_DOS;
    for (i = 0; i < 6; i++)
        d->bar[i] = pci_read32(bus, dev, fn, 0x10 + i * 4);
}

/* Every function on buses 0-7 (enough for the machines GLOS targets). */
int pci_scan(void)
{
    u32 bus, dev, fn, hdr;
    ndevs = 0;
    for (bus = 0; bus < 8; bus++)
        for (dev = 0; dev < 32; dev++) {
            if ((pci_read32(bus, dev, 0, 0) & 0xFFFF) == 0xFFFF)
                continue;
            hdr = pci_read32(bus, dev, 0, 0x0C) >> 16;
            for (fn = 0; fn < ((hdr & 0x80) ? 8u : 1u) && ndevs < PCI_MAX; fn++)
                if ((pci_read32(bus, dev, fn, 0) & 0xFFFF) != 0xFFFF)
                    add(bus, dev, fn);
        }
    kprintf("GLOS-PCI devices=%u\n", ndevs);
    return ndevs;
}

struct pci_dev *pci_find(u16 vendor, u16 device, int nth)
{
    int i;
    for (i = 0; i < ndevs; i++)
        if (devs[i].vendor == vendor && devs[i].device == device && nth-- == 0)
            return &devs[i];
    return NULL;
}

struct pci_dev *pci_at(int i) { return i >= 0 && i < ndevs ? &devs[i] : NULL; }

void pci_claim(struct pci_dev *d)
{
    d->keeper = DEV_GLOS;
    kprintf("GLOS-PCI claim=%02x:%02x.%u id=%04x:%04x irq=%u\n", d->bus, d->dev, d->fn, d->vendor, d->device,
            d->irq);
}

struct pci_dev *pci_by_addr(u32 cf8)
{
    int i;
    u32 bus = (cf8 >> 16) & 0xFF, dev = (cf8 >> 11) & 0x1F, fn = (cf8 >> 8) & 7;
    for (i = 0; i < ndevs; i++)
        if (devs[i].bus == bus && devs[i].dev == dev && devs[i].fn == fn)
            return &devs[i];
    return NULL;
}

int pci_owned_addr(u32 cf8)
{
    struct pci_dev *d = pci_by_addr(cf8);
    return d && d->keeper == DEV_GLOS;
}

int pci_uses_line(const struct pci_dev *d) { return d->pin >= 1 && d->pin <= 4 && d->irq >= 1 && d->irq < 16; }
