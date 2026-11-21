/* GLOS's own IDE driver (ide.h; docs/v1/native-dos.md, N1c): the channels found, kernel/drv/ata.c given their
 * ports and a clock.
 *
 * The channels: each PCI IDE function's two, at the legacy ports (1F0h/3F6h, 170h/376h) in compatibility mode or
 * at its BARs in native mode (progif bits 0 and 2); with no PCI IDE function, the ISA ports. At most four.
 *
 * The clock: GLOS's tick (1,024 Hz, near enough a millisecond for the time allowed). The disk cache drives the
 * driver from a trap's path, where interrupts are off: each look at the clock lets them in for a moment
 * (breathe), so a long transfer neither loses the tick nor stalls the network, and nothing else gets the
 * processor meanwhile. */
#include "ide.h"
#include "io.h"
#include "kprintf.h"
#include "pci.h"
#include "sched.h"
#include "timer.h"

#define IDE_CHANS 4

static struct ata_chan chans[IDE_CHANS];
static struct ata_dev devs[IDE_CHANS * 2];
static int nchans, probed;

static unsigned char k_in8(void *ctx, unsigned port)
{
    (void)ctx;
    return inb((u16)port);
}

static void k_out8(void *ctx, unsigned port, unsigned char v)
{
    (void)ctx;
    outb((u16)port, v);
}

static void k_in16s(void *ctx, unsigned port, unsigned short *buf, unsigned count)
{
    (void)ctx;
    __asm__ volatile("cld; rep insw" : "+D"(buf), "+c"(count) : "d"((u16)port) : "memory");
}

static void k_out16s(void *ctx, unsigned port, const unsigned short *buf, unsigned count)
{
    (void)ctx;
    __asm__ volatile("cld; rep outsw" : "+S"(buf), "+c"(count) : "d"((u16)port) : "memory");
}

static unsigned long k_ms(void *ctx)
{
    (void)ctx;
    preempt_disable();                  /* (interrupts in for a moment, no other thread) */
    sti();
    __asm__ volatile("nop");
    cli();
    preempt_enable();
    return timer_ticks();
}

static const struct ata_io kio = { k_in8, k_out8, k_in16s, k_out16s, k_ms, 0 };

static void add_chan(unsigned base, unsigned ctl)
{
    int i;

    for (i = 0; i < nchans; i++)
        if (chans[i].base == base)
            return;
    if (nchans == IDE_CHANS || !base || !ctl)
        return;
    chans[nchans].io = &kio;
    chans[nchans].base = base;
    chans[nchans].ctl = ctl;
    chans[nchans].selected = -1;
    nchans++;
}

static void find_chans(void)
{
    struct pci_dev *p;
    int i, pci = 0;

    for (i = 0; (p = pci_at(i)) != 0; i++) {
        if (p->class != 0x01 || p->subclass != 0x01)
            continue;
        pci = 1;
        if ((p->progif & 1) && (p->bar[0] & 1) && (p->bar[1] & 1))
            add_chan(p->bar[0] & ~3u, (p->bar[1] & ~3u) + 2);
        else
            add_chan(0x1F0, 0x3F6);
        if ((p->progif & 4) && (p->bar[2] & 1) && (p->bar[3] & 1))
            add_chan(p->bar[2] & ~3u, (p->bar[3] & ~3u) + 2);
        else
            add_chan(0x170, 0x376);
    }
    if (!pci) {
        add_chan(0x1F0, 0x3F6);
        add_chan(0x170, 0x376);
    }
}

int ide_probe(int own)
{
    int i, k, disks = 0;

    if (probed)
        goto count;
    probed = 1;
    find_chans();
    for (i = 0; i < nchans; i++) {
        int r = ata_probe(&chans[i], &devs[2 * i], own);

        for (k = 0; k < 2; k++) {
            struct ata_dev *d = &devs[2 * i + k];

            if (r || !d->present)
                continue;
            kprintf("GLOS-IDE dev=%d chan=%03x/%03x %s %s sectors=%u hi=%u mult=%u lba=%d lba48=%d model=\"%s\"\n",
                    2 * i + k, chans[i].base, chans[i].ctl, k ? "slave" : "master", d->atapi ? "atapi" : "ata",
                    (u32)d->sectors_lo, (u32)d->sectors_hi, d->multiple, d->lba, d->lba48, d->model);
        }
        if (r)
            kprintf("GLOS-IDE chan=%03x/%03x %s\n", chans[i].base, chans[i].ctl, r == ATA_ENODEV ? "none" : "timeout");
    }
count:
    for (i = 0; i < 2 * nchans; i++)
        disks += devs[i].present && !devs[i].atapi;
    return disks;
}

struct ata_dev *ide_dev(int i)
{
    return i >= 0 && i < 2 * nchans ? &devs[i] : 0;
}

unsigned ide_bios_error(const struct ata_dev *d, int r)
{
    if (r == ATA_ETIMEOUT)
        return 0x80;                    /* time out (not ready) */
    if (r == ATA_ERANGE)
        return 0x04;                    /* sector not found */
    if (r != ATA_EDEVICE)
        return 0x20;                    /* controller failure */
    if (d->status & 0x20)
        return 0xCC;                    /* write fault */
    if (d->err & 0x40)
        return 0x10;                    /* uncorrectable */
    if (d->err & 0x10)
        return 0x04;                    /* ID not found */
    if (d->err & 0x80)
        return 0x0A;                    /* bad sector flag */
    if (d->err & 0x01)
        return 0x02;                    /* address mark not found */
    return 0xBB;                        /* undefined */
}
