/* EHCI handed over (GLOS v1, phase H tier 1, 2026-11-23; docs/v1/hardware-audit.md). A PC of USB 2.0's time has an
 * EHCI controller beside its UHCI (or OHCI) companions, and its BIOS has usually routed every port to EHCI (its
 * CONFIGFLAG set) and drives the keyboard there by SMI. GLOS drives USB 1.1 by UHCI alone (uhci.c), so before it
 * takes the companions each EHCI is:
 *   - taken from the BIOS by its legacy-support capability (USBLEGSUP, in PCI configuration space at EECP):
 *     the OS's semaphore set, the BIOS's awaited (a second; then cleared by force), its SMIs turned off;
 *   - stopped (Run/Stop cleared, halted awaited) and its interrupts turned off;
 *   - told that no software configured it (CONFIGFLAG 0): every port goes back to its companion controller, where
 *     the devices plugged in are found again as UHCI's own. USB 2.0's speed is not used (out of v1's scope).
 * At glos exit the BIOS gets it back as it was: CONFIGFLAG and the SMI enables put back, the OS's semaphore given
 * up (a BIOS that wants its keyboard again takes it then, as it took it at its start).
 *
 * From the EHCI specification (1.0, sections 2.1.7, 2.3, 5.1). Log:
 *   GLOS-USB ehci 00:1d.7 handed bios=1|0 forced=0|1 ports=N   (taken: whether the BIOS had it, by force)
 *   GLOS-USB ehci 00:1d.7 refused no-memory|io-bar             (left alone)
 *   GLOS-USB ehci 00:1d.7 given back                           (glos exit) */
#include <string.h>
#include "devlist.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "pci.h"
#include "sched.h"
#include "timer.h"
#include "usb.h"

#define MAX_EHCI 4

#define CAP_LENGTH   0x00               /* (byte) where the operational registers begin */
#define CAP_HCSPARAMS 0x04
#define CAP_HCCPARAMS 0x08
#define OP_USBCMD    0x00
#define OP_USBSTS    0x04
#define OP_USBINTR   0x08
#define OP_CONFIGFLAG 0x40
#define CMD_RUN      0x01
#define STS_HALTED   0x1000

#define LEGSUP_BIOS  (1u << 16)         /* USBLEGSUP: the BIOS owns it */
#define LEGSUP_OS    (1u << 24)         /* ... the OS asks for it */

static struct ehci {
    struct pci_dev *pci;
    volatile u32 *op;
    u32 eecp;                           /* the legacy-support capability's place (0: none) */
    u32 legctl, configflag, intr;       /* as the BIOS left them: given back at exit */
} eh[MAX_EHCI];
static int neh;

static void pause_ms(u32 ms, int can_sleep)
{
    if (!can_sleep) {                   /* (glos exit: the tick has stopped; a port read takes about a microsecond) */
        u32 n = ms * 1000;
        while (n--)
            inb(0x80);
        return;
    }
    thread_sleep_until(timer_ticks() + (ms * 1024 + 999) / 1000 + 1);
}

/* USBLEGSUP's place among the controller's extended capabilities (in PCI configuration space): 0 if none. */
static u32 legsup_at(const struct pci_dev *pd, u32 hccparams)
{
    u32 at = (hccparams >> 8) & 0xFF, n;
    for (n = 0; at >= 0x40 && n < 16; n++) {
        u32 v = pci_read32(pd->bus, pd->dev, pd->fn, at);
        if ((v & 0xFF) == 1)
            return at;
        at = (v >> 8) & 0xFF;
    }
    return 0;
}

static void take(struct pci_dev *pd)
{
    struct ehci *e = &eh[neh];
    char w[8];
    u32 bar = pd->bar[0], hcc, v, cmd, i;
    volatile u8 *cap;
    int bios = 0, forced = 0;
    if (bar & 1) {                      /* (EHCI's registers are memory, never I/O) */
        kprintf("GLOS-USB ehci %s refused io-bar\n", pci_where(pd, w));
        dev_note(pci_where(pd, w), "ehci", DEVS_REFUSED, "io-bar", 0);
        return;
    }
    bar &= ~0xFu;
    cmd = pci_read32(pd->bus, pd->dev, pd->fn, 0x04);
    pci_write32(pd->bus, pd->dev, pd->fn, 0x04, (cmd & 0xFFFF) | 0x02);       /* (its memory decoded) */
    if (!bar || !(cap = mm_dev_map(bar & ~0xFFFu, 0x1000))) {
        kprintf("GLOS-USB ehci %s refused no-memory\n", pci_where(pd, w));
        dev_note(pci_where(pd, w), "ehci", DEVS_REFUSED, "no-memory", 0);
        return;
    }
    cap += bar & 0xFFFu;
    hcc = *(volatile u32 *)(cap + CAP_HCCPARAMS);
    memset(e, 0, sizeof *e);
    e->pci = pd;
    e->op = (volatile u32 *)(cap + cap[CAP_LENGTH]);
    if ((e->eecp = legsup_at(pd, hcc)) != 0) {          /* the BIOS's: asked for, awaited, then taken */
        v = pci_read32(pd->bus, pd->dev, pd->fn, e->eecp);
        bios = (v & LEGSUP_BIOS) != 0;
        pci_write32(pd->bus, pd->dev, pd->fn, e->eecp, v | LEGSUP_OS);         /* (asked; the BIOS lets go) */
        if (bios) {
            for (i = 0; i < 100 && (pci_read32(pd->bus, pd->dev, pd->fn, e->eecp) & LEGSUP_BIOS); i++)
                pause_ms(10, 1);
            if (pci_read32(pd->bus, pd->dev, pd->fn, e->eecp) & LEGSUP_BIOS) {
                forced = 1;             /* (a BIOS that never lets go: its bit cleared, as Linux and the BSDs do) */
                v = pci_read32(pd->bus, pd->dev, pd->fn, e->eecp);
                pci_write32(pd->bus, pd->dev, pd->fn, e->eecp, (v & ~LEGSUP_BIOS) | LEGSUP_OS);
            }
        }
        e->legctl = pci_read32(pd->bus, pd->dev, pd->fn, e->eecp + 4);
        pci_write32(pd->bus, pd->dev, pd->fn, e->eecp + 4, 0xE0000000u);     /* (its SMIs off; their causes cleared) */
    }
    e->configflag = e->op[OP_CONFIGFLAG / 4];
    e->intr = e->op[OP_USBINTR / 4];
    e->op[OP_USBINTR / 4] = 0;
    e->op[OP_USBCMD / 4] &= ~CMD_RUN;           /* stopped, ... */
    for (i = 0; i < 20 && !(e->op[OP_USBSTS / 4] & STS_HALTED); i++)
        pause_ms(1, 1);
    e->op[OP_CONFIGFLAG / 4] = 0;               /* ... and every port its companion's */
    e->op[OP_USBSTS / 4] = e->op[OP_USBSTS / 4];         /* (what it had to say, cleared) */
    neh++;
    kprintf("GLOS-USB ehci %s handed bios=%d forced=%d ports=%u\n", pci_where(pd, w), bios, forced,
            *(volatile u32 *)(cap + CAP_HCSPARAMS) & 0xF);
    dev_note(pci_where(pd, w), "ehci", DEVS_ON, "ports-to-companions", 0);
    pause_ms(20, 1);                            /* (the ports settle on the companions before UHCI looks at them) */
}

void ehci_take_all(void)
{
    struct pci_dev *pd;
    int i;
    for (i = 0; (pd = pci_at(i)) != 0 && neh < MAX_EHCI; i++)
        if (pd->class == 0x0C && pd->subclass == 0x03 && pd->progif == 0x20 && !dev_pci_off(pd, "ehci"))
            take(pd);
}

void ehci_leave(void)
{
    int i;
    char w[8];
    for (i = 0; i < neh; i++) {
        struct ehci *e = &eh[i];
        struct pci_dev *pd = e->pci;
        e->op[OP_CONFIGFLAG / 4] = e->configflag;       /* the ports as the BIOS had them */
        pause_ms(5, 0);
        e->op[OP_USBINTR / 4] = e->intr;
        if (e->eecp) {
            u32 v = pci_read32(pd->bus, pd->dev, pd->fn, e->eecp);
            pci_write32(pd->bus, pd->dev, pd->fn, e->eecp + 4, e->legctl & 0x0000E03Fu);    /* (its SMI enables) */
            pci_write32(pd->bus, pd->dev, pd->fn, e->eecp, v & ~LEGSUP_OS);
        }
        kprintf("GLOS-USB ehci %s given back\n", pci_where(pd, w));
    }
}
