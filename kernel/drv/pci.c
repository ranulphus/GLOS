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

/* Where PCI memory BARs and enabled expansion-ROM BARs start below 1 MB, for
   the stub's page of upper memory (docs/upper-memory.md, Which page). Read
   only: a BAR isn't sized (that takes a write), so it claims the page it
   starts in. A 64-bit BAR's upper half must be 0. */
u32 pci_low_claims(u32 *out, u32 max)
{
    u32 n = 0, k, v, type, nbar;
    int i;
    for (i = 0; i < ndevs; i++) {
        struct pci_dev *d = &devs[i];
        type = (pci_read32(d->bus, d->dev, d->fn, 0x0C) >> 16) & 0x7F;
        nbar = type == 0 ? 6 : type == 1 ? 2 : 0;
        for (k = 0; k < nbar; k++) {
            v = pci_read32(d->bus, d->dev, d->fn, 0x10 + k * 4);
            if (v & 1)                          /* I/O */
                continue;
            if ((v & 6) == 4 && pci_read32(d->bus, d->dev, d->fn, 0x10 + ++k * 4))
                continue;                       /* 64-bit, above 4 GB */
            if ((v & ~0xFu) && (v & ~0xFu) < 0x100000u && n < max)
                out[n++] = v & ~0xFu;
        }
        if (nbar) {
            v = pci_read32(d->bus, d->dev, d->fn, type == 0 ? 0x30 : 0x38);
            if ((v & 1) && (v & ~0x7FFu) < 0x100000u && n < max)
                out[n++] = v & ~0x7FFu;
        }
    }
    return n;
}

int pci_uses_line(const struct pci_dev *d) { return d->pin >= 1 && d->pin <= 4 && d->irq >= 1 && d->irq < 16; }

/* The video card's linear framebuffer, by PCI: the display device's first prefetchable memory BAR (the
   framebuffer; registers are never prefetchable), sized as PCI sizes one. Its base in *base, the card in *card
   (if asked), and its size, capped: 32 MB (no card of the period has more behind it), and 16 MB for S3's, whose
   64 MB range has the card's registers at its sixteenth megabyte. 0: none found. (kernel/arch/mtrr.c's
   write-combining: the MTRR command and [display] write_combining.) */
/* A memory BAR's size, by the all-ones write (interrupts off for it: nothing decodes there meanwhile). */
static u32 bar_size(const struct pci_dev *d, int k)
{
    u32 reg = 0x10 + 4u * (u32)k, size, f = irq_save();
    pci_write32(d->bus, d->dev, d->fn, reg, 0xFFFFFFFFu);
    size = ~(pci_read32(d->bus, d->dev, d->fn, reg) & ~15u) + 1;
    pci_write32(d->bus, d->dev, d->fn, reg, d->bar[k]);
    irq_restore(f);
    return size;
}

/* 1 if the bytes from base are all inside one memory range of a video card's own (a 32-bit memory BAR of a
   display-class function, above 1 MB): what may be made write-combining a page at a time (kernel/arch/mtrr.c's
   mtrr_wc_page), and nothing else may. */
int pci_video_mem(u32 base, u32 bytes)
{
    struct pci_dev *d;
    int i, k;
    if (!bytes || base + bytes < base)
        return 0;
    for (i = 0; (d = pci_at(i)) != 0; i++) {
        if (d->class != 0x03)
            continue;
        for (k = 0; k < 6; k++) {
            u32 b = d->bar[k], size;
            if (!b || (b & 1) || (b & 6) || (b & ~15u) < 0x100000u || base < (b & ~15u))
                continue;
            size = bar_size(d, k);
            if (size >= 0x1000u && !(size & (size - 1)) && base - (b & ~15u) < size && bytes <= size - (base - (b & ~15u)))
                return 1;
        }
    }
    return 0;
}

u32 pci_lfb_range(u32 *base, const struct pci_dev **card)
{
    struct pci_dev *d;
    int i, k;
    for (i = 0; (d = pci_at(i)) != 0; i++) {
        if (d->class != 0x03)
            continue;
        for (k = 0; k < 6; k++) {
            u32 b = d->bar[k], size, cap = d->vendor == 0x5333 ? 16u << 20 : 32u << 20;
            int matrox_fb = d->vendor == 0x102B && k == 0;      /* (Matrox's G-series: BAR 0 is the framebuffer,
                                                   whatever its flags say: 86Box's G100 has it not prefetchable) */
            if (!b || (b & 1) || (!(b & 8) && !matrox_fb) || (b & 6))   /* (I/O; not prefetchable; 64-bit or below 1 MB) */
                continue;
            size = bar_size(d, k);
            if (size < (1u << 20) || (size & (size - 1)))
                continue;
            *base = b & ~15u;
            if (card)
                *card = d;
            return size < cap ? size : cap;
        }
    }
    return 0;
}
