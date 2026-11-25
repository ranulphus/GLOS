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

void vm_line_dos(int irq);                      /* (kernel/vm/v86.c: a line of GLOS's a DOS device shares) */

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

/* EHCI USB 2.0 (class 0C 03 20: its registers in memory, BAR 0; the operational ones after the capability length,
   the first byte). It drives its line only when a cause it has enabled (USBINTR) is up. With none enabled it is
   quiet as it is (bench B's NEC controller, which shares IRQ 10 with the CMI8738: the sound chip was refused
   for it on 2026-11-24, as the line rule came to the sound drivers); with some enabled and the controller
   stopped they are cleared; a controller that runs with them enabled is somebody's (a DOS USB driver's) and
   stays so. GLOS's own hand-off (ehci.c, later in the start) leaves every one stopped with none enabled. */
#define EHCI_USBCMD  0x00
#define EHCI_USBINTR 0x08

static int ehci_match(const struct pci_dev *d) { return d->class == 0x0C && d->subclass == 0x03 && d->progif == 0x20; }

static int ehci_quiet(struct pci_dev *d, const char **how)
{
    volatile u8 *cap;
    volatile u32 *op;
    if (!(pci_read32(d->bus, d->dev, d->fn, 4) & 2)) {         /* its memory isn't decoded */
        *how = "off";
        return 0;
    }
    if ((d->bar[0] & 1) || !(d->bar[0] & ~15u) || !(cap = mm_dev_map(d->bar[0] & ~15u, 4096))) {
        *how = "no-memory";
        return -1;
    }
    op = (volatile u32 *)(cap + cap[0]);
    if (!op[EHCI_USBINTR / 4]) {
        *how = "no-intr";
        return 0;
    }
    if (op[EHCI_USBCMD / 4] & 1) {
        *how = "driven";
        return -1;
    }
    op[EHCI_USBINTR / 4] = 0;
    *how = "intr";
    return 0;
}

/* The PCI sound chips GLOS has drivers for (phase H; step D2, 2026-11-24): one that sits on the network card's
   line is quiet while no channel of its runs with its interrupt enabled (the enables are cleared, if any is
   set); one that plays or records by interrupt is somebody's (a DOS driver's) and stays so. Their registers are
   in I/O space. From the same data sheets as the drivers (cmi8738.c, es1371.c, ich97.c, via686.c). */
static u16 snd_io(const struct pci_dev *d, int bar)
{
    if (!(pci_read32(d->bus, d->dev, d->fn, 4) & 1) || !(d->bar[bar] & 1))
        return 0;
    return (u16)(d->bar[bar] & 0xFFFCu);
}

static int cmi_match(const struct pci_dev *d)
{
    return d->vendor == 0x13F6 && (d->device == 0x0100 || d->device == 0x0101 || d->device == 0x0111
                                   || d->device == 0x0112);
}

static int cmi_quiet(struct pci_dev *d, const char **how)
{
    u16 io = snd_io(d, 0), on, ints;
    if (!io) {
        *how = "off";
        return 0;
    }
    on = inw((u16)(io + 0x02)) & 3;             /* FUNCTRL0 bits 17:16: the channels enabled */
    ints = inw((u16)(io + 0x0E)) & 3;           /* INT_HLDCLR bits 17:16: their interrupts */
    if (on & ints) {
        *how = "driven";
        return -1;
    }
    if (!ints) {
        *how = "no-intr";
        return 0;
    }
    outw((u16)(io + 0x0E), (u16)(inw((u16)(io + 0x0E)) & ~3));
    *how = "intr";
    return 0;
}

static int es_match(const struct pci_dev *d)
{
    return d->vendor == 0x1274 && (d->device == 0x1371 || d->device == 0x5880);
}

static int es_quiet(struct pci_dev *d, const char **how)
{
    u16 io = snd_io(d, 0), on, ints;
    if (!io) {
        *how = "off";
        return 0;
    }
    on = (inw(io) >> 4) & 7;                    /* CONTROL bits 6:4: DAC1, DAC2, ADC enabled */
    ints = (inw((u16)(io + 0x20)) >> 8) & 7;    /* SERIAL bits 10:8: P1, P2, R1 interrupts */
    if (((on >> 2) & ints & 1) | ((on >> 1) & (ints >> 1) & 1) | (on & (ints >> 2) & 1)) {
        *how = "driven";
        return -1;
    }
    if (!ints) {
        *how = "no-intr";
        return 0;
    }
    outw((u16)(io + 0x20), (u16)(inw((u16)(io + 0x20)) & ~0x0700));
    *how = "intr";
    return 0;
}

static int ich_match(const struct pci_dev *d)
{
    static const u16 ids[] = { 0x2415, 0x2425, 0x2445, 0x2485, 0x24C5, 0x24D5, 0x266E, 0x27DE, 0x7195 };
    u32 k;
    for (k = 0; k < sizeof ids / sizeof ids[0]; k++)
        if (d->vendor == 0x8086 && d->device == ids[k])
            return 1;
    return 0;
}

static int ich_quiet(struct pci_dev *d, const char **how)
{
    u16 io = snd_io(d, 1), k;                   /* (the bus master block: BAR 1) */
    u8 cr[3], any = 0;
    if (!io) {
        *how = "off";
        return 0;
    }
    for (k = 0; k < 3; k++) {                   /* PCM in, PCM out, mic in: control at 0Bh, 1Bh, 2Bh */
        cr[k] = inb((u16)(io + 0x0B + k * 0x10));
        if ((cr[k] & 0x01) && (cr[k] & 0x1C)) { /* (running, with an interrupt enabled) */
            *how = "driven";
            return -1;
        }
        any |= cr[k] & 0x1C;
    }
    if (!any) {
        *how = "no-intr";
        return 0;
    }
    for (k = 0; k < 3; k++)
        outb((u16)(io + 0x0B + k * 0x10), (u8)(cr[k] & ~0x1C));
    *how = "intr";
    return 0;
}

static int via_match(const struct pci_dev *d) { return d->vendor == 0x1106 && (d->device == 0x3058 || d->device == 0x3059); }

static int via_quiet(struct pci_dev *d, const char **how)
{
    /* (the 686's three channels have their interrupt enables in the type register, +2; the VT8233 family's seven
       in the control register, +1; a channel's status, +0, bit 7: it is going) */
    static const u8 at686[] = { 0x00, 0x10, 0x20 }, at8233[] = { 0x00, 0x10, 0x20, 0x30, 0x40, 0x60, 0x70 };
    int v8233 = d->device == 0x3059;
    const u8 *at = v8233 ? at8233 : at686;
    u32 n = v8233 ? sizeof at8233 : sizeof at686, k;
    u16 io = snd_io(d, 0);
    u8 any = 0;
    if (!io) {
        *how = "off";
        return 0;
    }
    for (k = 0; k < n; k++) {
        u8 ints = inb((u16)(io + at[k] + (v8233 ? 1 : 2))) & 3;
        if (ints && (inb((u16)(io + at[k])) & 0x80)) {
            *how = "driven";
            return -1;
        }
        any |= ints;
    }
    if (!any) {
        *how = "no-intr";
        return 0;
    }
    for (k = 0; k < n; k++) {
        u16 p = (u16)(io + at[k] + (v8233 ? 1 : 2));
        outb(p, (u8)(inb(p) & ~3));
    }
    *how = "intr";
    return 0;
}

/* Intel's SMBus controllers (the ICH's, class 0C 05: bench F's ICH6 shares IRQ 5 with its CT5880, which was
   refused for it on 2026-11-24). Its host interrupts only with HST_CNT's INTREN (I/O base + 2, bit 0) or the
   host-notify enable (SLV_CMD, + 11h, bit 0) set; with HOSTC's SMB_SMI_EN (configuration 40h, bit 1) its
   interrupts are an SMI's, never the line's. Quiet if none can come; a transfer under way with INTREN set is
   somebody's (a DOS driver's); else the enables are cleared. Its I/O base is BAR 4 (configuration 20h). From
   Intel's ICH data sheets (the SMBus controller's registers). */
static int smb_match(const struct pci_dev *d) { return d->vendor == 0x8086 && d->class == 0x0C && d->subclass == 0x05; }

static int smb_quiet(struct pci_dev *d, const char **how)
{
    u32 hostc = pci_read32(d->bus, d->dev, d->fn, 0x40);
    u16 io = snd_io(d, 4);
    u8 cnt, slv;
    if (!io || !(hostc & 1)) {                  /* (its I/O not decoded, or the host off: it can't interrupt) */
        *how = "off";
        return 0;
    }
    if (hostc & 2) {
        *how = "smi";
        return 0;
    }
    cnt = inb((u16)(io + 2));
    slv = inb((u16)(io + 0x11));
    if (!(cnt & 1) && !(slv & 1)) {
        *how = "no-intr";
        return 0;
    }
    if ((cnt & 1) && (inb(io) & 1)) {           /* (HST_STS's HOST_BUSY: a transfer of somebody's) */
        *how = "driven";
        return -1;
    }
    outb((u16)(io + 2), (u8)(cnt & ~1));
    outb((u16)(io + 0x11), (u8)(slv & ~1));
    *how = "intr";
    return 0;
}

static const struct quiet quiets[] = {
    { "uhci", uhci_match, uhci_quiet },
    { "ohci", ohci_match, ohci_quiet },
    { "mga", mga_match, mga_quiet },
    { "ehci", ehci_match, ehci_quiet },
    { "smbus", smb_match, smb_quiet },
    { "cmi8738", cmi_match, cmi_quiet },
    { "es1371", es_match, es_quiet },
    { "ich97", ich_match, ich_quiet },
    { "via-ac97", via_match, via_quiet },
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
        if (o->keeper == DEV_GLOS)              /* (step D2: another of GLOS's own on the line is not in the way: their
                                                   handlers are chained, vm_claim_irq; a USB controller of GLOS's
                                                   uses no line at all) */
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
        if (o->keeper != DEV_QUIET) {           /* (DOS's, and not to be made quiet: the line shared with it, step
                                                   D2's second half: every interrupt passed to the VM too, v86.c) */
            if (room && !r)
                ksnprintf(with, room, "%s", addr(o, a, sizeof a));
            /* (every one of them said, the first named: a PC's log then tells what shares the line) */
            kprintf("GLOS-DEV shares %s id=%04x:%04x class=%02x%02x%02x irq=%u keeper=%s\n", addr(o, a, sizeof a),
                    o->vendor, o->device, o->class, o->subclass, o->progif, o->irq,
                    o->keeper == DEV_GLOS ? "glos" : "dos");
            vm_line_dos(o->irq);
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
    if (pci_uses_line(d))
        vm_line_dos(d->irq);                    /* (a line of GLOS's it shares: DOS's driver is given its interrupts too) */
}
