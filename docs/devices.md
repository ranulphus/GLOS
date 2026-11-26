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
  - Second: **OHCI USB** (2026-11-14, for the Pentium III bench: the ALi board's OHCI shares IRQ 9 with the
    network card). Its registers are in memory (BAR 0).
    - With `HcControl`'s InterruptRouting set, its interrupts are SMIs for the BIOS's keyboard emulation and never
      reach the PCI line: it is quiet as it is, and nothing is written (`how=smi`).
    - Otherwise every interrupt is disabled (`HcInterruptDisable`, the master bit with the rest: `how=intr`),
      unless the controller is operational with its master interrupt enable set. Then somebody drives it, and
      it stays DOS's (`why=driven`).
    - A controller whose memory isn't decoded can't interrupt (`how=off`).
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
  (OHCI, UHCI, EHCI), IDE, PCI sound. The first is under way: **UHCI** (docs/gamepad.md J2, D74), after which a
  UHCI's keeper is GLOS (its quiet driver only holds it until GLOS.CFG is read). More quiet drivers as bench PCs need them: AC'97, HDA, bus-master IDE,
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

`tests/host/dev_test.c` checks the rule and the UHCI and OHCI drivers against made-up functions, ports and
registers (in `make host-test`). `glos pci` (`PCI` in the shell) lists every function with its line and its keeper, and
`glos pci BUS:DEV.FN` prints one function's configuration space: what a PC's log doesn't say about a card GLOS
has no driver for. Loop A's 86Box machines don't share these lines, so there the ssh suite only shows nothing broke.

## UHCI's keeper becomes GLOS (docs/gamepad.md J2, 2026-11-05)

Once GLOS.CFG is read (`[usb] enabled`, on by default), `kernel/drv/uhci.c` takes each UHCI: its legacy support
register cleared (no SMI, no PCI line), the controller stopped, reset, and run with GLOS's own schedule, which GLOS
polls, so the shared line stays the other device's; `pci_claim` and `vdev_hide` then hide it from DOS. bench-g400:

    GLOS-DEV quiet 00:02.2 id=8086:7112 class=uhci irq=11 how=pirq
    GLOS-PCI claim=00:02.2 id=8086:7112 irq=11
    GLOS-USB uhci 00:02.2 io=ff00 ports=2 legsup=0000

A UHCI whose BIOS is emulating a USB keyboard on it (the legacy register's SMI enable) is taken too: GLOS's own USB
keyboard and mouse drivers (J4) stand in for the emulation while GLOS runs. At `glos exit` the register is put back
as found; whether a BIOS's emulation then picks up again is still to be seen on a PC that has it on (bench-g400's
BIOS doesn't use USB).

## PCI sound cards and the line rule (phase H, 2026-11-24)

GLOS's PCI sound drivers (`cmi8738.c`, `es1371.c`, `ich97.c`, `via686.c`) are found late, by the mixer, after the
network card and the USB controllers have their lines. They took their interrupt line without asking until
2026-11-24. A line has one handler (`vm_claim_irq` sets it), so on a PC whose sound card shares the network
card's line the sound driver would have taken the network's interrupts away. No bench has that sharing: bench
G's VIA sound shares IRQ 12 with a UHCI that is quiet, bench B's CMI8738 has IRQ 10 to itself.

- **Now:** each of them asks first (`mixer_pci_line`, which is `dev_take_line`): with another function on the
  line that isn't quiet, the sound card is refused (`GLOS-CMI refused shared-irq irq=11 with=00:0b.0`, and
  `shared-irq:...` in Devices), and the line stays the other device's. Once found whole, the card is GLOS's
  (`pci_claim`: DOS no longer finds it on the PCI bus while GLOS runs).

## Step D2, its first half: GLOS's own drivers on one line (2026-11-24)

Reading the rule again for the sound drivers showed that it was too strict for them as it stood: a USB
controller of GLOS's own counted as in the way, though GLOS polls it and it uses no line, and sound chips often
share a line with one. So the first half of D2 was built the same morning:

- **A chain of handlers** (`vm_claim_irq`, kernel/vm/v86.c): a second driver's claim of a line adds its handler
  to the first's; each is called at every interrupt of the line and looks at its own device (the sound
  drivers read their status registers; the network's wakes its thread, which finds nothing if the card had
  nothing). Four at most. With one driver on a line its handler is the line's, as before:
  `GLOS-DEV line irq=11 drivers=2` is said when a chain is made.
- **The rule:** another function of GLOS's own on the line is not in the way (`dev_take_line`).
- **Quiet drivers for EHCI controllers and Intel's SMBus controllers** (2026-11-24: bench B's CMI8738 was refused
  for a NEC EHCI on its line, bench F's CT5880 for the ICH6's SMBus): quiet while none of their interrupt enables
  is set (the SMBus's routed to SMI counts as quiet too).
- **Quiet drivers for the sound chips GLOS drives** (CMI8738, ES1371 family, Intel ICH AC'97, VIA AC'97): a
  network card that shares its line with one takes the line if no channel of the chip runs with its
  interrupt enabled (`how=no-intr`, or `how=intr` when an enable had been left on and was cleared); a chip
  that plays by interrupt is a DOS driver's and stays in the way (`why=driven`). The mixer's driver then
  joins the network's on the line.
- **A quiet driver for NVIDIA display cards** (2026-11-26: bench G's TNT2 Pro, in the G550's place, on the Rhine's
  IRQ 11; GLOS 11-24 refused the Rhine): RIVA 128 to GeForce 6 (vendor 10DE, and 12D2 for SGS-Thomson's RIVA
  128s) are quiet while their master control's interrupt enable (`PMC_INTR_EN_0`, 140h in BAR 0) is 0, as a BIOS
  and DOS's VGA and VESA use leave it (`how=no-intr`); nothing is written. Enabled, the card is a driver's
  (`why=driven`). The Matrox cards' was the same since 2026-11-23 (`mga`, IEN at 1E1Ch).
- The second half followed on 2026-11-25 (below).
- **Tests:**
  - host: `dev_test` (the rule; each sound chip's quiet driver idle, with an enable left on, driven, and with
    its I/O not decoded);
  - Loop A: no 86Box board shares a line by itself, so `GLOS.EXE /SHAREIRQ` (a test's switch) leads the first
    sound card's interrupt to the network card's line through the PIIX's routing registers. The audio suite's
    `es1371-share` case then runs whole, over SSH, with both drivers on IRQ 11: `GLOS-PCI shareirq irq=11
    was=12`, `GLOS-DEV quiet ... class=es1371 irq=11 how=no-intr`, `GLOS-DEV line irq=11 drivers=2`, every
    check as without sharing, no watchdog kick.
  - A real PC with a network card and a sound chip on one line has not been tried: none of the benches has
    that.

## Step D2, its second half: a line shared with a DOS driver's device (2026-11-25)

The owner's choice after phase H's tier 2: OPTi first, then this.

- **The rule:** a function on the line that is DOS's and can't be made quiet no longer stands in the way.
  `dev_take_line` says it shares the line (`GLOS-DEV shares ... keeper=dos`), and the line is marked a DOS
  device's (`vm_line_dos`, `GLOS-DEV line irq=N dos=1`). A quiet device that DOS takes back (its configuration
  written: `GLOS-DEV given-back`) marks its line so too.
- **Every interrupt to both** (`kernel/vm/v86.c`, `chained`): GLOS's handlers on the line, then the VM, through
  the virtual PIC, as PCI drivers on a shared line take them (each looks at its own device). The "was it mine?"
  of the first plan was not built: GLOS's network drivers do their card's work on their thread, not in the
  handler, and DOS's drivers for PCI devices look at their own status anyway. The line stays masked until both
  have done: GLOS's handlers have returned (a driver that masks its line while its thread works keeps it so), and
  the VM's program has ended it on the virtual PIC (in flight until then). A VM that has the line masked on its
  PIC (no handler of DOS's) is not given it.
- **Two guards** (`vm_lines_tick`, on the tick): a storm (20,000 interrupts in about a second) masks the line for
  good (`GLOS-WARN irq-storm`), and GLOS's handlers on it are called from the tick instead; while the VM holds the
  line in flight for 64 ticks (its interrupts off, a program stopped), GLOS's handlers are called from the tick
  too.
- **Tests:** host, `dev_test` (every case that was "in the way" now shares the line, `vm_line_dos` told). Loop A,
  the audio suite's `sharedos` case: 86Box's CMI8738 left to DOS (`[devices] off`), `GLOS.EXE /SHAREIRQ` leading
  its line to the network card's, and `SHAREDOS` (`tests/dos/sharedos.c`) playing silence on it by bus master with
  its interrupt at each half for five seconds, over SSH: its PCI BIOS write gives the card back, its handler gets
  all its interrupts (429 of 431) and is passed two of the network card's, which it ends, and the SSH session on
  the same line goes on. The storm guard has no test of its own yet: no emulated device can be made to hold a
  line up.

