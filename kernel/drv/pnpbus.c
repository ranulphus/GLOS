/* ISA Plug and Play in the kernel (GLOS v1, phase H tier 2, 2026-11-23): isapnp.c's scan over the PC's ports, made
 * when a driver first asks for a card (or by `glos pnp`), never by itself at start: a PC whose cards are all
 * found by other means (BLASTER, a chip's own key, PCI) is not touched. A logical device that is on already (a
 * BIOS's or a DOS program's doing) is used as it is; one that is off is given its preferred I/O ranges, an
 * interrupt line no PCI device and none of the PC's own devices has, and DMA channels, and turned on.
 * Log: GLOS-PNP cards=N devices=M rd=XXX; GLOS-PNP on id=ESS1868 io=220 irq=5 dma=1 | GLOS-PNP error=... */
#include <string.h>

#include "io.h"
#include "isapnp.h"
#include "kprintf.h"
#include "pci.h"
#include "pnpbus.h"
#include "types.h"
#include "vm.h"

static struct pnp bus;
static u8 scanned;

static unsigned char k_in(void *ctx, unsigned port) { (void)ctx; return inb((u16)port); }
static void k_out(void *ctx, unsigned port, unsigned char v) { (void)ctx; outb((u16)port, v); }

static void k_wait(void *ctx, unsigned us)       /* (a read of port 80h: about a microsecond on any PC) */
{
    (void)ctx;
    while (us--)
        inb(0x80);
}

static const struct pnp_io k_io = { k_in, k_out, k_wait };

static void scan(void)
{
    bus.io = &k_io;
    pnp_scan(&bus);
    scanned = 1;
    kprintf("GLOS-PNP cards=%u devices=%u rd=%x\n", bus.ncards, bus.ndevs, bus.rd);
}

/* The first logical device of a maker's (PNP_VENDOR of its ID) that can have at least so many I/O ranges,
   interrupts and DMA channels; after `after` (NULL: from the start). NULL: none. */
struct pnp_dev *pnpbus_find(unsigned long vendor, unsigned io, unsigned irq, unsigned dma, const struct pnp_dev *after)
{
    unsigned i;
    if (!scanned)
        scan();
    for (i = after ? (unsigned)(after - bus.dev) + 1 : 0; i < bus.ndevs; i++)
        if (PNP_VENDOR(bus.dev[i].id) == vendor && bus.dev[i].n_io >= io && bus.dev[i].n_irq >= irq
            && bus.dev[i].n_dma >= dma)
            return &bus.dev[i];
    return 0;
}

/* The interrupt lines an ISA card must not be given: the PC's own (timer, keyboard, cascade, floppy, clock,
   mouse, coprocessor, both IDE channels), each PCI device's, and the serial ports' that the BIOS found. */
static u16 lines_taken(void)
{
    u16 m = 0x0001 | 0x0002 | 0x0004 | 0x0040 | 0x0100 | 0x1000 | 0x2000 | 0x4000 | 0x8000;
    struct pci_dev *d;
    int i;
    for (i = 0; (d = pci_at(i)) != 0; i++)
        if (d->pin && d->irq && d->irq < 16)
            m |= (u16)(1u << d->irq);
    if (vm_rd16(0x400))                         /* (the BIOS's COM1: IRQ 4; its COM2: IRQ 3) */
        m |= 0x0010;
    if (vm_rd16(0x402))
        m |= 0x0008;
    return m;
}

/* A logical device on: as it is if it is on already, else given what it prefers. 0, or -1. */
int pnpbus_on(struct pnp_dev *d)
{
    static const u8 irq_order[] = { 5, 7, 10, 11, 9, 3, 4 }, dma_order[] = { 1, 3, 0, 5, 6, 7 };
    unsigned short io[4] = { 0, 0, 0, 0 };
    unsigned char irq[2] = { 0, 0 }, dma[2] = { 4, 4 };
    u16 taken = lines_taken();
    char t[8];
    unsigned k, j;
    pnp_id_text(d->id, t);
    if (d->active && d->io[0]) {
        kprintf("GLOS-PNP as-found id=%s io=%x irq=%u dma=%u\n", t, d->io[0], d->irq[0], d->dma[0]);
        return 0;
    }
    for (k = 0; k < d->n_io; k++)
        io[k] = d->io_opt[k].min;
    for (k = 0; k < d->n_irq; k++) {
        for (j = 0; j < sizeof irq_order; j++)
            if ((d->irq_opt[k] & (1u << irq_order[j])) && !(taken & (1u << irq_order[j]))) {
                irq[k] = irq_order[j];
                taken |= (u16)(1u << irq_order[j]);
                break;
            }
        if (!irq[k]) {
            kprintf("GLOS-PNP error=no-irq id=%s can=%04x taken=%04x\n", t, d->irq_opt[k], taken);
            return -1;
        }
    }
    for (k = 0; k < d->n_dma; k++) {
        for (j = 0; j < sizeof dma_order; j++)
            if ((d->dma_opt[k] & (1u << dma_order[j])) && (k == 0 || dma[0] != dma_order[j])) {
                dma[k] = dma_order[j];
                break;
            }
        if (dma[k] == 4 && k == 0) {
            kprintf("GLOS-PNP error=no-dma id=%s can=%02x\n", t, d->dma_opt[k]);
            return -1;
        }
    }
    if (pnp_set(&bus, d, io, irq, dma) != 0) {
        kprintf("GLOS-PNP error=not-set id=%s io=%x\n", t, io[0]);
        return -1;
    }
    kprintf("GLOS-PNP on id=%s io=%x irq=%u dma=%u\n", t, d->io[0], d->irq[0], d->dma[0]);
    return 0;
}

/* glos pnp: the bus scanned again and listed. */
u32 pnpbus_stat(char *b, u32 room)
{
    u32 n, i, k;
    scan();
    n = (u32)ksnprintf(b, room, "pnp cards=%u devices=%u rd=%x\n", bus.ncards, bus.ndevs, bus.rd);
    for (i = 0; i < bus.ndevs && n + 160 < room; i++) {
        const struct pnp_dev *d = &bus.dev[i];
        char card[8], id[8], compat[8];
        pnp_id_text(d->card, card);
        pnp_id_text(d->id, id);
        pnp_id_text(d->compat, compat);
        n += (u32)ksnprintf(b + n, room - n, "pnp csn=%u ld=%u card=%s id=%s compat=%s %s", d->csn, d->ldn, card, id,
                            d->compat ? compat : "-", d->active ? "on" : "off");
        for (k = 0; k < 4; k++)
            if (d->io[k])
                n += (u32)ksnprintf(b + n, room - n, " io%u=%x", k, d->io[k]);
        for (k = 0; k < 2; k++)
            if (d->irq[k])
                n += (u32)ksnprintf(b + n, room - n, " irq%u=%u", k, d->irq[k]);
        for (k = 0; k < 2; k++)
            if (d->dma[k] != 4)
                n += (u32)ksnprintf(b + n, room - n, " dma%u=%u", k, d->dma[k]);
        n += (u32)ksnprintf(b + n, room - n, " can io=%u irq=%u dma=%u\n", d->n_io, d->n_irq, d->n_dma);
    }
    return n;
}
