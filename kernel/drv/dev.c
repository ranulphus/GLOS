/* Who keeps each device, and the interrupt lines GLOS's drivers share with
 * devices nobody drives (docs/devices.md, step D1).
 *
 * A PCI line is GLOS's to take when every other function on it is quiet: its
 * interrupts turned off by its class's quiet driver, because nobody drives
 * it. Quiet drivers run only for the functions on a line GLOS wants; devices
 * elsewhere stay as the BIOS left them. A quiet device stays visible to DOS,
 * and if a DOS program writes its configuration space it is DOS's again
 * (logged: the line it shares with GLOS is then at risk until step D2's
 * chained handlers). */
#include "dev.h"
#include "io.h"
#include "kprintf.h"

/* ---- quiet drivers: switch a device's interrupts off, or say why not */

struct quiet {
    const char *name;
    int (*match)(const struct pci_dev *d);
    int (*quiet)(struct pci_dev *d, const char **how);  /* 0 done, -1 not (how says why) */
};

/* UHCI USB (class 0C 03 00). Intel's (PIIX3/4, ICH): the legacy support register's USBPIRQDEN (config C0h, bit
   13) off, so that the controller never drives its PCI line; the BIOS's USB keyboard emulation goes on, through
   SMI. Unless the controller runs with its SMI off: a driver other than the BIOS's has it. Other makers': USBINTR
   off, while the controller isn't running. */
#define UHCI_LEGSUP      0xC0
#define LEGSUP_SMIEN     0x0010         /* the USB interrupt raises SMI (the BIOS's legacy support) */
#define LEGSUP_PIRQDEN   0x2000         /* the USB interrupt drives PIRQD: the PCI line */
#define LEGSUP_RWC       0x8F00         /* status bits a 1 clears: written 0, unchanged */

static int uhci_match(const struct pci_dev *d) { return d->class == 0x0C && d->subclass == 0x03 && d->progif == 0x00; }

static int uhci_quiet(struct pci_dev *d, const char **how)
{
    u16 io = (d->bar[4] & 1) ? (u16)(d->bar[4] & 0xFFE0u) : 0;        /* USBBA (BAR 4), I/O space */
    int running = io && (inw(io) & 1);                                  /* USBCMD.RS */
    if (d->vendor == 0x8086) {
        u32 v = pci_read32(d->bus, d->dev, d->fn, UHCI_LEGSUP);
        if (running && (v & LEGSUP_PIRQDEN) && !(v & LEGSUP_SMIEN)) {
            *how = "driven";
            return -1;
        }
        pci_write32(d->bus, d->dev, d->fn, UHCI_LEGSUP, v & ~(LEGSUP_PIRQDEN | LEGSUP_RWC));
        *how = "pirq";
        return 0;
    }
    if (!io) {
        *how = "no-io";
        return -1;
    }
    if (running) {
        *how = "running";
        return -1;
    }
    outw((u16)(io + 4), 0);                     /* USBINTR: none */
    outw((u16)(io + 2), 0x3F);                  /* USBSTS: what it held, cleared */
    *how = "intr";
    return 0;
}

static const struct quiet quiets[] = {
    { "uhci", uhci_match, uhci_quiet },
};

/* ---- lines */

static const char *addr(const struct pci_dev *d, char *buf, u32 room)
{
    ksnprintf(buf, room, "%02x:%02x.%u", d->bus, d->dev, d->fn);
    return buf;
}

int dev_take_line(struct pci_dev *d, char *with, u32 room)
{
    struct pci_dev *o;
    char a[12];
    int i;
    u32 k;
    if (room)
        with[0] = 0;
    for (i = 0; (o = pci_at(i)) != NULL; i++) {
        const char *how = "no-driver";
        if (o == d || !pci_uses_line(o) || o->irq != d->irq || o->keeper == DEV_QUIET)
            continue;
        if (o->keeper == DEV_DOS)
            for (k = 0; k < sizeof quiets / sizeof quiets[0]; k++)
                if (quiets[k].match(o)) {
                    if (quiets[k].quiet(o, &how) == 0) {
                        o->keeper = DEV_QUIET;
                        kprintf("GLOS-DEV quiet %s id=%04x:%04x class=%s irq=%u how=%s\n", addr(o, a, sizeof a),
                                o->vendor, o->device, quiets[k].name, o->irq, how);
                    } else {
                        kprintf("GLOS-DEV keep %s id=%04x:%04x class=%s irq=%u why=%s\n", addr(o, a, sizeof a),
                                o->vendor, o->device, quiets[k].name, o->irq, how);
                    }
                    break;
                }
        if (o->keeper != DEV_QUIET) {           /* (GLOS's own on the same line: step D2) */
            if (room)
                ksnprintf(with, room, "%s", addr(o, a, sizeof a));
            return -1;
        }
    }
    return 0;
}

void dev_dos_wrote(u32 cf8)
{
    struct pci_dev *d = pci_by_addr(cf8);
    char a[12];
    if (!d || d->keeper != DEV_QUIET)
        return;
    d->keeper = DEV_DOS;
    kprintf("GLOS-DEV given-back %s id=%04x:%04x to=dos irq=%u\n", addr(d, a, sizeof a), d->vendor, d->device, d->irq);
}
