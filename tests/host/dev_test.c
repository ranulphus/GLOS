/* The device layer's line rule and its UHCI and OHCI quiet drivers (kernel/drv/dev.c;
 * docs/devices.md, D1) against made-up PCI functions and ports: an Intel
 * UHCI under the BIOS's legacy support made quiet and its SMI kept; one a
 * driver runs refused; a stale line register with no pin ignored; another
 * maker's UHCI made quiet only while it isn't running; a device with no
 * quiet driver in the way; one of GLOS's own in the way; a line elsewhere
 * untouched; DOS taking a quiet device back. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "dev.h"
#include "io.h"
#include "kprintf.h"

u16 fake_ports[65536];
static struct pci_dev fn[8];
static int nfn;
static u32 legsup[8];                           /* config C0h of each function */
static u32 command[8];                          /* config 04h: I/O and memory decoded unless a case says not */
static u32 ohci[4096];                          /* an OHCI's registers (a Matrox card's 16 KB), where mm_dev_map leads */
static u32 mapped;                              /* the address mm_dev_map was last asked for */

void *mm_dev_map(u32 phys, u32 bytes)
{
    (void)bytes;
    mapped = phys;
    return ohci;
}
static char logbuf[4096];
static int fails;
static int heard = 1;                           /* the VM's interrupt controller has the lines open */

int dev_line_heard(int irq) { (void)irq; return heard; }

#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

void kprintf(const char *fmt, ...)
{
    va_list ap;
    size_t n = strlen(logbuf);
    va_start(ap, fmt);
    vsnprintf(logbuf + n, sizeof logbuf - n, fmt, ap);
    va_end(ap);
}

int ksnprintf(char *buf, size_t n, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

struct pci_dev *pci_at(int i) { return i >= 0 && i < nfn ? &fn[i] : NULL; }

static int find(u32 bus, u32 dev, u32 f)
{
    int i;
    for (i = 0; i < nfn; i++)
        if (fn[i].bus == bus && fn[i].dev == dev && fn[i].fn == f)
            return i;
    return -1;
}

u32 pci_read32(u32 bus, u32 dev, u32 f, u32 reg)
{
    int i = find(bus, dev, f);
    return i >= 0 && reg == 0xC0 ? legsup[i] : i >= 0 && reg == 4 ? command[i] : 0xFFFFFFFFu;
}

void pci_write32(u32 bus, u32 dev, u32 f, u32 reg, u32 v)
{
    int i = find(bus, dev, f);
    if (i >= 0 && reg == 0xC0)
        legsup[i] = v;
}

struct pci_dev *pci_by_addr(u32 cf8)
{
    int i = find((cf8 >> 16) & 0xFF, (cf8 >> 11) & 0x1F, (cf8 >> 8) & 7);
    return i >= 0 ? &fn[i] : NULL;
}

int pci_uses_line(const struct pci_dev *d) { return d->pin >= 1 && d->pin <= 4 && d->irq >= 1 && d->irq < 16; }

static struct pci_dev *add(u8 dev, u8 f, u16 vendor, u16 device, u8 cls, u8 sub, u8 progif, u8 pin, u8 irq, u16 io)
{
    struct pci_dev *d = &fn[nfn];
    memset(d, 0, sizeof *d);
    d->dev = dev, d->fn = f, d->vendor = vendor, d->device = device;
    d->class = cls, d->subclass = sub, d->progif = progif, d->pin = pin, d->irq = irq;
    d->bar[4] = io ? (u32)io | 1u : 0;
    legsup[nfn] = 0;
    command[nfn] = 7;
    nfn++;
    return d;
}

static void reset(void)
{
    nfn = 0;
    memset(fake_ports, 0, sizeof fake_ports);
    memset(ohci, 0, sizeof ohci);
    mapped = 0;
    logbuf[0] = 0;
}

int main(void)
{
    struct pci_dev *nic, *usb, *other;
    char with[12];

    /* The bench PC: the PRO/100 and the PIIX4's UHCI on IRQ 11, the BIOS's legacy support on (PIRQ and SMI) */
    reset();
    usb = add(2, 2, 0x8086, 0x7112, 0x0C, 0x03, 0x00, 4, 11, 0xE000);
    nic = add(3, 0, 0x8086, 0x1229, 0x02, 0x00, 0x00, 1, 11, 0);
    add(2, 1, 0x8086, 0x7111, 0x01, 0x01, 0x80, 0, 11, 0);   /* IDE: no pin, a stale line register */
    add(1, 0, 0x102B, 0x0525, 0x03, 0x00, 0x00, 1, 9, 0);    /* the G400, on another line */
    legsup[0] = 0x2010 | 0x0100;                /* PIRQ, SMI, and a status bit set */
    fake_ports[0xE000] = 1;                     /* running (the BIOS's) */
    CHECK(dev_take_line(nic, with, sizeof with) == 0);
    CHECK(usb->keeper == DEV_QUIET);
    CHECK(legsup[0] == 0x0010);                 /* PIRQ off, SMI kept, the status bit written 0 */
    CHECK(strstr(logbuf, "GLOS-DEV quiet 00:02.2 id=8086:7112 class=uhci irq=11 how=pirq\n") != NULL);
    CHECK(fn[3].keeper == DEV_DOS);             /* the G400: untouched */
    logbuf[0] = 0;
    CHECK(dev_take_line(nic, with, sizeof with) == 0 && logbuf[0] == 0);       /* again: quiet already */

    /* A driver other than the BIOS's runs it (PIRQ on, SMI off): it stays DOS's, the card is refused */
    reset();
    usb = add(2, 2, 0x8086, 0x7112, 0x0C, 0x03, 0x00, 4, 11, 0xE000);
    nic = add(3, 0, 0x8086, 0x1229, 0x02, 0x00, 0x00, 1, 11, 0);
    legsup[0] = 0x2000;
    fake_ports[0xE000] = 1;
    CHECK(dev_take_line(nic, with, sizeof with) == -1 && !strcmp(with, "00:02.2"));
    CHECK(usb->keeper == DEV_DOS && legsup[0] == 0x2000);
    CHECK(strstr(logbuf, "GLOS-DEV keep 00:02.2 id=8086:7112 class=uhci irq=11 why=driven\n") != NULL);

    /* Another maker's UHCI (VIA): quiet by USBINTR while it isn't running, kept while it is */
    reset();
    usb = add(7, 2, 0x1106, 0x3038, 0x0C, 0x03, 0x00, 4, 10, 0xD000);
    nic = add(9, 0, 0x10EC, 0x8139, 0x02, 0x00, 0x00, 1, 10, 0);
    fake_ports[0xD004] = 0x000F;
    fake_ports[0xD002] = 0x0001;
    CHECK(dev_take_line(nic, with, sizeof with) == 0 && usb->keeper == DEV_QUIET);
    CHECK(fake_ports[0xD004] == 0 && fake_ports[0xD002] == 0x3F);
    CHECK(strstr(logbuf, "how=intr") != NULL);
    reset();
    usb = add(7, 2, 0x1106, 0x3038, 0x0C, 0x03, 0x00, 4, 10, 0xD000);
    nic = add(9, 0, 0x10EC, 0x8139, 0x02, 0x00, 0x00, 1, 10, 0);
    fake_ports[0xD000] = 1;
    fake_ports[0xD004] = 0x000F;
    CHECK(dev_take_line(nic, with, sizeof with) == -1 && usb->keeper == DEV_DOS && fake_ports[0xD004] == 0x000F);
    CHECK(strstr(logbuf, "why=running") != NULL);
    CHECK(strstr(logbuf, "GLOS-DEV shares 00:07.2 id=1106:3038 class=0c0300 irq=10 keeper=dos\n") != NULL);

    /* The PCChips M925G: VIA's UHCI running for the BIOS (a USB keyboard, the stick GLOS started from) on the
       Rhine's line. With the legacy support register's PIRQ and SMI on: PIRQ off, the BIOS's SMI goes on */
    reset();
    usb = add(16, 0, 0x1106, 0x3038, 0x0C, 0x03, 0x00, 1, 11, 0xE400);
    nic = add(18, 0, 0x1106, 0x3065, 0x02, 0x00, 0x00, 1, 11, 0);
    legsup[0] = 0x2010;
    fake_ports[0xE400] = 1;
    fake_ports[0xE404] = 0x000F;
    CHECK(dev_take_line(nic, with, sizeof with) == 0 && usb->keeper == DEV_QUIET && legsup[0] == 0x0010);
    CHECK(strstr(logbuf, "how=pirq") != NULL && fake_ports[0xE404] == 0x000F);
    /* ... PIRQ on and SMI off: a driver's, if the VM's interrupt controller has the line open (kept) */
    reset();
    usb = add(16, 0, 0x1106, 0x3038, 0x0C, 0x03, 0x00, 1, 11, 0xE400);
    nic = add(18, 0, 0x1106, 0x3065, 0x02, 0x00, 0x00, 1, 11, 0);
    legsup[0] = 0x2000;
    fake_ports[0xE400] = 1;
    fake_ports[0xE404] = 0x000F;
    CHECK(dev_take_line(nic, with, sizeof with) == -1 && usb->keeper == DEV_DOS && legsup[0] == 0x2000);
    CHECK(strstr(logbuf, "why=driven") != NULL);
    /* ... nobody's if the line is masked there (the BIOS polls it): PIRQ off */
    reset();
    usb = add(16, 0, 0x1106, 0x3038, 0x0C, 0x03, 0x00, 1, 11, 0xE400);
    nic = add(18, 0, 0x1106, 0x3065, 0x02, 0x00, 0x00, 1, 11, 0);
    legsup[0] = 0x2000;
    fake_ports[0xE400] = 1;
    fake_ports[0xE404] = 0x000F;
    heard = 0;
    CHECK(dev_take_line(nic, with, sizeof with) == 0 && usb->keeper == DEV_QUIET && legsup[0] == 0);
    CHECK(strstr(logbuf, "legsup=2000 usbintr=000f line=masked") != NULL && strstr(logbuf, "how=pirq") != NULL);
    heard = 1;
    /* ... as the M925G's BIOS leaves it in fact: PIRQ off, SMI on (legsup=0030), interrupts enabled: they are the
       BIOS's SMIs. And a Matrox G550 on the same line, no interrupt of its own enabled: both quiet as they are,
       nothing written, the card taken */
    reset();
    usb = add(16, 0, 0x1106, 0x3038, 0x0C, 0x03, 0x00, 1, 11, 0xE400);
    other = add(0, 0, 0x102B, 0x2527, 0x03, 0x00, 0x00, 1, 11, 0);
    other->bus = 1;
    other->bar[1] = 0xDFE00000u;
    nic = add(18, 0, 0x1106, 0x3065, 0x02, 0x00, 0x00, 1, 11, 0);
    legsup[0] = 0x0030;
    fake_ports[0xE400] = 1;
    fake_ports[0xE404] = 0x0005;
    CHECK(dev_take_line(nic, with, sizeof with) == 0 && usb->keeper == DEV_QUIET && other->keeper == DEV_QUIET);
    CHECK(legsup[0] == 0x0030 && fake_ports[0xE404] == 0x0005 && mapped == 0xDFE00000u && ohci[0x1E1C / 4] == 0);
    CHECK(strstr(logbuf, "class=uhci irq=11 how=smi") != NULL && strstr(logbuf, "class=mga irq=11 how=no-intr") != NULL);
    /* ... the card with an interrupt enabled is somebody's: kept, the network card refused */
    reset();
    other = add(0, 0, 0x102B, 0x2527, 0x03, 0x00, 0x00, 1, 11, 0);
    other->bar[1] = 0xDFE00000u;
    nic = add(18, 0, 0x1106, 0x3065, 0x02, 0x00, 0x00, 1, 11, 0);
    ohci[0x1E1C / 4] = 0x20;
    CHECK(dev_take_line(nic, with, sizeof with) == -1 && other->keeper == DEV_DOS && ohci[0x1E1C / 4] == 0x20);
    CHECK(strstr(logbuf, "class=mga irq=11 why=driven") != NULL);
    /* ... no such register (it reads 0), but the controller's own interrupt enables all off: quiet as it is */
    reset();
    usb = add(16, 0, 0x1106, 0x3038, 0x0C, 0x03, 0x00, 1, 11, 0xE400);
    nic = add(18, 0, 0x1106, 0x3065, 0x02, 0x00, 0x00, 1, 11, 0);
    fake_ports[0xE400] = 1;
    CHECK(dev_take_line(nic, with, sizeof with) == 0 && usb->keeper == DEV_QUIET);
    CHECK(strstr(logbuf, "how=no-intr") != NULL);

    /* The P3 bench: the ALi board's OHCI and the VIA Rhine on IRQ 9. Nobody drives the OHCI (reset, its
       interrupts neither routed to SMI nor enabled): every interrupt disabled, the master bit with them */
    reset();
    usb = add(2, 0, 0x10B9, 0x5237, 0x0C, 0x03, 0x10, 1, 9, 0);
    usb->bar[0] = 0xDFFFC000u;
    nic = add(16, 0, 0x1106, 0x3043, 0x02, 0x00, 0x00, 1, 9, 0);
    CHECK(dev_take_line(nic, with, sizeof with) == 0 && usb->keeper == DEV_QUIET);
    CHECK(mapped == 0xDFFFC000u && ohci[0x14 / 4] == 0xC000007Fu && ohci[0x10 / 4] == 0);
    CHECK(strstr(logbuf, "GLOS-DEV quiet 00:02.0 id=10b9:5237 class=ohci irq=9 how=intr\n") != NULL);

    /* ... under the BIOS's keyboard emulation (HcControl's InterruptRouting): its interrupts are SMIs, never the
       line's. Quiet as it is, nothing written */
    reset();
    usb = add(2, 0, 0x10B9, 0x5237, 0x0C, 0x03, 0x10, 1, 9, 0);
    usb->bar[0] = 0xDFFFC000u;
    nic = add(16, 0, 0x1106, 0x3043, 0x02, 0x00, 0x00, 1, 9, 0);
    ohci[0x04 / 4] = 0x00000180u;               /* operational, routed to SMI */
    ohci[0x10 / 4] = 0x80000042u;
    CHECK(dev_take_line(nic, with, sizeof with) == 0 && usb->keeper == DEV_QUIET);
    CHECK(ohci[0x14 / 4] == 0 && ohci[0x10 / 4] == 0x80000042u && strstr(logbuf, "how=smi") != NULL);

    /* ... running with its interrupts on the line: somebody drives it. Kept, the card refused */
    reset();
    usb = add(2, 0, 0x10B9, 0x5237, 0x0C, 0x03, 0x10, 1, 9, 0);
    usb->bar[0] = 0xDFFFC000u;
    nic = add(16, 0, 0x1106, 0x3043, 0x02, 0x00, 0x00, 1, 9, 0);
    ohci[0x04 / 4] = 0x00000080u;
    ohci[0x10 / 4] = 0x80000002u;
    CHECK(dev_take_line(nic, with, sizeof with) == -1 && !strcmp(with, "00:02.0") && usb->keeper == DEV_DOS);
    CHECK(ohci[0x14 / 4] == 0 && strstr(logbuf, "why=driven") != NULL);

    /* ... its memory not decoded (switched off in the BIOS's setup): it can't interrupt, and isn't looked at;
       and one with no address there to look at is in the way */
    reset();
    usb = add(2, 0, 0x10B9, 0x5237, 0x0C, 0x03, 0x10, 1, 9, 0);
    usb->bar[0] = 0xDFFFC000u;
    nic = add(16, 0, 0x1106, 0x3043, 0x02, 0x00, 0x00, 1, 9, 0);
    command[0] = 0;
    CHECK(dev_take_line(nic, with, sizeof with) == 0 && usb->keeper == DEV_QUIET && mapped == 0);
    CHECK(strstr(logbuf, "how=off") != NULL);
    reset();
    usb = add(2, 0, 0x10B9, 0x5237, 0x0C, 0x03, 0x10, 1, 9, 0);
    nic = add(16, 0, 0x1106, 0x3043, 0x02, 0x00, 0x00, 1, 9, 0);
    CHECK(dev_take_line(nic, with, sizeof with) == -1 && usb->keeper == DEV_DOS && mapped == 0);
    CHECK(strstr(logbuf, "why=no-memory") != NULL);

    /* No quiet driver for it (a PCI sound card): in the way, untouched, and named in the log */
    reset();
    other = add(10, 0, 0x1274, 0x1371, 0x04, 0x01, 0x00, 1, 5, 0);
    nic = add(11, 0, 0x10EC, 0x8139, 0x02, 0x00, 0x00, 1, 5, 0);
    CHECK(dev_take_line(nic, with, sizeof with) == -1 && !strcmp(with, "00:0a.0") && other->keeper == DEV_DOS);
    CHECK(!strcmp(logbuf, "GLOS-DEV shares 00:0a.0 id=1274:1371 class=040100 irq=5 keeper=dos\n"));

    /* One of GLOS's own on the line: in the way until step D2 */
    reset();
    other = add(12, 0, 0x10EC, 0x8029, 0x02, 0x00, 0x00, 1, 9, 0);
    other->keeper = DEV_GLOS;
    nic = add(13, 0, 0x10EC, 0x8139, 0x02, 0x00, 0x00, 1, 9, 0);
    CHECK(dev_take_line(nic, with, sizeof with) == -1 && !strcmp(with, "00:0c.0"));

    /* DOS writes a quiet device's configuration: DOS's again; a write to one of DOS's changes nothing */
    reset();
    usb = add(2, 2, 0x8086, 0x7112, 0x0C, 0x03, 0x00, 4, 11, 0xE000);
    nic = add(3, 0, 0x8086, 0x1229, 0x02, 0x00, 0x00, 1, 11, 0);
    CHECK(dev_take_line(nic, with, sizeof with) == 0 && usb->keeper == DEV_QUIET);
    logbuf[0] = 0;
    dev_dos_wrote(0x80000000u | (2u << 11) | (2u << 8) | 0xC0);
    CHECK(usb->keeper == DEV_DOS);
    CHECK(strstr(logbuf, "GLOS-DEV given-back 00:02.2 id=8086:7112 to=dos irq=11\n") != NULL);
    logbuf[0] = 0;
    dev_dos_wrote(0x80000000u | (3u << 11) | 0x04);
    CHECK(nic->keeper == DEV_DOS && logbuf[0] == 0);

    printf("dev_test: %d failures\n", fails);
    return fails != 0;
}
