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
#include "mm.h"

/* ---- quiet drivers: switch a device's interrupts off, or say why not */

struct quiet {
    const char *name;
    int (*match)(const struct pci_dev *d);
    int (*quiet)(struct pci_dev *d, const char **how);  /* 0 done, -1 not (how says why) */
};

/* UHCI USB (class 0C 03 00). Intel's (PIIX3/4, ICH): the legacy support register's USBPIRQDEN (config C0h, bit
   13) off, so that the controller never drives its PCI line; the BIOS's USB keyboard emulation goes on, through
   SMI. Unless the controller runs with its SMI off: a driver other than the BIOS's has it. Other makers': USBINTR
   off, while the controller isn't running. One that is running (the owner's PCChips M925G, 2026-11-23: VIA's,
   which the BIOS ran for a USB keyboard and the stick GLOS started from, on the network card's line): the same
   register where it has it (the UHCI design guide's: the bit reads back off), unless the bits say a driver has
   the line (PIRQ on, SMI off) and the VM's interrupt controller has the line open, so somebody listens there; or
   nothing to do where the controller's own interrupt enables are all off. Else it stays as it is. */
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
        u32 v = pci_read32(d->bus, d->dev, d->fn, UHCI_LEGSUP);
        u16 intr = inw((u16)(io + 4));
        int heard = dev_line_heard(d->irq);
        kprintf("GLOS-DEV uhci %02x:%02x.%u running legsup=%04x usbintr=%04x line=%s\n", d->bus, d->dev, d->fn,
                v & 0xFFFF, intr, heard ? "heard" : "masked");
        if ((v & 0xFFFF) != 0xFFFF && (v & LEGSUP_PIRQDEN)) {
            if (!(v & LEGSUP_SMIEN) && heard) {
                *how = "driven";
                return -1;
            }
            pci_write32(d->bus, d->dev, d->fn, UHCI_LEGSUP, v & ~(LEGSUP_PIRQDEN | LEGSUP_RWC));
            if (!(pci_read32(d->bus, d->dev, d->fn, UHCI_LEGSUP) & LEGSUP_PIRQDEN)) {
                *how = "pirq";
                return 0;
            }
        }
        if ((v & 0xFFFF) != 0xFFFF && (v & LEGSUP_SMIEN)) {
            *how = "smi";                       /* (PIRQ off already and SMI on: its interrupts are the BIOS's SMIs,
                                                   never the line. The M925G's BIOS leaves it so: legsup=0030) */
            return 0;
        }
        if (!intr) {                            /* (it can't interrupt as it is) */
            *how = "no-intr";
            return 0;
        }
        *how = "running";
        return -1;
    }
    outw((u16)(io + 4), 0);                     /* USBINTR: none */
    outw((u16)(io + 2), 0x3F);                  /* USBSTS: what it held, cleared */
    *how = "intr";
    return 0;
}

/* OHCI USB (class 0C 03 10): its registers are in memory (BAR 0). With HcControl's InterruptRouting set its
   interrupts go to SMI, for the BIOS's keyboard emulation, and never to the PCI line: nothing to do. Otherwise
   every interrupt is disabled (HcInterruptDisable, the master bit with the rest), unless the controller is
   running with its interrupts on the line: somebody drives it (a BIOS that serves its keyboard by IRQ, a DOS
   driver). (The P3 bench: the ALi board's own OHCI shares IRQ 9 with the network card.) */
#define OHCI_CONTROL  0x04
#define OHCI_INTEN    0x10
#define OHCI_INTDIS   0x14
#define OHCI_IR       0x00000100u       /* HcControl: interrupts are routed to SMI */
#define OHCI_HCFS     0x000000C0u       /* ... the controller's state: 80h operational */
#define OHCI_MIE      0x80000000u

static int ohci_match(const struct pci_dev *d) { return d->class == 0x0C && d->subclass == 0x03 && d->progif == 0x10; }

static int ohci_quiet(struct pci_dev *d, const char **how)
{
    volatile u32 *r;
    u32 control;
    if (!(pci_read32(d->bus, d->dev, d->fn, 4) & 2)) {         /* its memory isn't decoded: it can't be running */
        *how = "off";
        return 0;
    }
    if ((d->bar[0] & 1) || !(d->bar[0] & ~15u) || !(r = mm_dev_map(d->bar[0] & ~15u, 4096))) {
        *how = "no-memory";
        return -1;
    }
    control = r[OHCI_CONTROL / 4];
    if (control & OHCI_IR) {
        *how = "smi";
        return 0;
    }
    if ((control & OHCI_HCFS) == 0x80 && (r[OHCI_INTEN / 4] & OHCI_MIE)) {
        *how = "driven";
        return -1;
    }
    r[OHCI_INTDIS / 4] = OHCI_MIE | 0x4000007Fu;
    *how = "intr";
    return 0;
}

/* A Matrox G-series display card (G100 to G550: its registers are its second memory range, 16 KB). Its one
   source of interrupts is what its interrupt enable register (IEN, 1E1Ch) lets through: the vertical retrace's,
   the drawing engine's. Nothing enables them for a BIOS or a DOS program's VGA and VESA use (the owner's PCChips
   M925G: a G550 on the network card's line, 2026-11-23): with the register 0 the card is quiet as it is, and
   nothing is written. If something has enabled one, the card is somebody's and stays so. (GLOS's own Matrox
   driver, kernel/gfx/mga.c, uses none.) */
#define MGA_IEN 0x1E1C

static int mga_match(const struct pci_dev *d)
{
    return d->class == 0x03 && d->vendor == 0x102B
           && (d->device == 0x1000 || d->device == 0x1001 || d->device == 0x0520 || d->device == 0x0521
               || d->device == 0x0525 || d->device == 0x2527);
}

static int mga_quiet(struct pci_dev *d, const char **how)
{
    volatile u32 *r;
    if (!(pci_read32(d->bus, d->dev, d->fn, 4) & 2)) {         /* its memory isn't decoded */
        *how = "off";
        return 0;
    }
    if ((d->bar[1] & 1) || !(d->bar[1] & ~15u) || !(r = mm_dev_map(d->bar[1] & ~15u, 16384))) {
        *how = "no-memory";
        return -1;
    }
    if (r[MGA_IEN / 4]) {
        *how = "driven";
        return -1;
    }
    *how = "no-intr";
    return 0;
}

static const struct quiet quiets[] = {
    { "uhci", uhci_match, uhci_quiet },
    { "ohci", ohci_match, ohci_quiet },
    { "mga", mga_match, mga_quiet },
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
    int i, r = 0;
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
            if (room && !r)
                ksnprintf(with, room, "%s", addr(o, a, sizeof a));
            /* (every one of them said, the first named: a PC's log then tells all that stands in the way) */
            kprintf("GLOS-DEV shares %s id=%04x:%04x class=%02x%02x%02x irq=%u keeper=%s\n", addr(o, a, sizeof a),
                    o->vendor, o->device, o->class, o->subclass, o->progif, o->irq,
                    o->keeper == DEV_GLOS ? "glos" : "dos");
            r = -1;
        }
    }
    return r;
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
