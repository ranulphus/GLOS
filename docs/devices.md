# GLOS's devices: who keeps each one, and shared interrupt lines

Design note, 2026-10-26, after the first bench PC (an IBM PC 300PL: Pentium II, 440BX) refused its network card.
That card's PCI interrupt line, IRQ 11, was shared with the chipset's USB controller, and GLOS refused any card on
a shared line (supervisor.md §10).

## Why sharing is harder for GLOS than for Windows

PCI interrupt lines are meant to be shared. Windows 98 and Linux own a driver for every device on a line. When
the line fires, each driver reads its own device's status and claims the interrupt if it was its own.

GLOS shares the machine with DOS. It drives a few devices itself (a network card, the Matrox engine, the SB16
mixer). The rest are DOS's, run by DOS's drivers in the V86 machine, which GLOS can't ask "was it yours?". Worse,
PCI interrupts are level-triggered. A device that asserts its line while nothing services it holds the line high
for ever. Windows never meets that case; GLOS must.

GLOS also doesn't want to own everything. DOS's compatibility rests on programs reaching hardware themselves (the
Sound Blaster, timers, joysticks), on DOS drivers the user installs, and on direct-mode sessions.

## Three keepers

Every device has one keeper (`pci_dev.keeper`; for ISA PnP devices too, later):

| Keeper | What it means | Its interrupts |
|---|---|---|
| **GLOS** | One of GLOS's own drivers serves it (network cards, display, SB16). | GLOS's driver. Its PCI function reads as absent to DOS (as now). |
| **quiet** | Nobody drives it: GLOS turned its interrupts off at boot. Example: a USB controller with no DOS USB driver. | Off. Still visible to DOS, so a DOS driver can find it and take it back. |
| **DOS** | Left as the BIOS set it up, for DOS's drivers and programs. | DOS's, through the virtual PIC. |

The drivers that make a device "quiet" are a few lines per device class. They are not drivers: they only know how
to switch a device's interrupts off, and (from D2) whether it is asserting.

## The steps

- **D1 (2026-10-26).** Quiet drivers at boot, and a line rule.
  - Quiet drivers run at boot, before GLOS's own drivers probe.
  - GLOS may take a PCI line if every other function on it is GLOS's (with its interrupts unused) or quiet.
    Functions whose interrupt pin is 0 use no line, whatever their line register says.
  - If a DOS program later writes the configuration space of a quiet device (GLOS traps CF8h-CFFh), GLOS logs it
    (`GLOS-DEV given-back`) and the device's keeper becomes DOS. Before D2, a line GLOS shares with it is then
    at risk, and the log says so.
  - First quiet driver: **UHCI USB.** On Intel's (PIIX3/4, ICH), clear `USBPIRQDEN` (legacy support register, config
    C0h, bit 13). The controller then never drives its PCI line, while the BIOS's USB-keyboard emulation (SMI)
    goes on. On other makers' UHCI, interrupts are switched off (`USBINTR`) only while the controller isn't
    running; otherwise it stays DOS's.
- **D2.** Chained handlers and pass-through.
  - Each GLOS driver on a line answers "was it mine?" from its device's status register.
  - What none of them claims goes on to DOS's handler for that IRQ, through the virtual PIC.
  - A storm guard covers a line that keeps firing with nothing claiming it: GLOS logs it, masks the line and gives
    the GLOS drivers on it a polled fallback.
  - With D2, GLOS can share a line with a device a DOS driver uses (a PCI sound card's DOS TSR, say).
- **D3.** ISA PnP, as UNISOUND does for sound cards.
  - A second bus beside PCI: GLOS isolates the cards and reads their resources.
  - It assigns I/O, IRQ and DMA, respecting `BLASTER` and GLOS.CFG's `[sound]`, then activates the logical
    devices.
  - It sets the common chips' Sound Blaster mode and mixer: Crystal CS4232/4236/4237, OPTi 82C93x, ESS
    ES1868/1869, Yamaha OPL3-SAx, Analog Devices AD1816, and Creative's SB16/AWE PnP.
  - Each card is then the SB16 mixer's (keeper GLOS) or DOS's.
- **Later.** Full GLOS drivers, a class at a time, where GLOS gains something: USB keyboard, mouse and storage
  (OHCI, UHCI, EHCI), IDE, PCI sound. More quiet drivers as bench PCs need them: OHCI, AC'97, HDA, bus-master IDE,
  SMBus.

## What each step logs

- `GLOS-DEV quiet 00:02.2 id=8086:7112 class=uhci irq=11 how=pirq`: a device made quiet, and how.
- `GLOS-DEV keep 00:02.2 id=8086:7112 class=uhci irq=11 why=running`: a device that couldn't be made quiet stays
  DOS's.
- `GLOS-DEV given-back 00:02.2 id=8086:7112 to=dos`: DOS wrote a quiet device's configuration.
- The card's own lines are as before (`GLOS-PCI claim=`, `GLOS-NET card=`): the `GLOS-DEV quiet` lines before them
  say which functions share its line.
- The refusal stays, with the function in the way: `GLOS-NET refuse card=... reason=shared-irq irq=11
  with=00:02.2`.

## D1 on the bench (2026-10-26)

The IBM PC 300PL, under MS-DOS 7.1 with GLOS in agent mode:

    GLOS-DEV quiet 00:02.2 id=8086:7112 class=uhci irq=11 how=pirq
    GLOS-PCI claim=00:03.0 id=8086:1229 irq=11
    GLOS-NET card=i8255x base=7c60 irq=11 mac=00:06:29:26:d0:1b
    GLOS-NET dhcp ip=192.168.1.33 gw=192.168.1.1 mask=255.255.255.0 ticks=1052

`tests/host/dev_test.c` checks the rule and the UHCI driver against made-up functions and ports (in `make
host-test`). Loop A's 86Box machines don't share these lines, so there the ssh suite only shows nothing broke.
