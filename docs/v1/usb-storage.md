# USB mass storage (phase G, G1)

The owner (2026-11-25): native EMS and USB mass storage come **before v1**, so hosted mode can go at v1. The
G plan's scope: UHCI, OHCI and EHCI storage, Bulk-Only. The native-dos session builds EMS, the CD-ROM's rest and the
installer; this is the GLOS session's (agreed 2026-11-26), on its USB stack (`kernel/drv/uhci.c`: UHCI, hubs, HID,
Xbox pads; `ehci.c`: the BIOS hand-off only; no OHCI yet).

## Sources (clean room)

USB Mass Storage Class, Bulk-Only Transport rev 1.0; the SCSI commands as SPC-2 and SBC-2 define them (INQUIRY,
TEST UNIT READY, REQUEST SENSE, READ CAPACITY(10), READ(10), WRITE(10)); Intel's UHCI design guide rev 1.1; the
OHCI specification rev 1.0a; the EHCI specification rev 1.0; USB 2.0 (clear halt, the endpoints' toggles). No
other system's code is looked at.

## Design

- **`kernel/drv/msc.c`: Bulk-Only and SCSI, with no controller in it.** A device's two bulk pipes reach it as calls
  (`bulk(ctx, ep, buf, len, &got, timeout)`, `clear_halt(ctx, ep)`, `class_reset(ctx)`): a command block (CBW),
  its data, its status (CSW); a stall cleared and the status read again; a phase error or a bad CSW, the reset
  recovery (the class's reset, both halts cleared); REQUEST SENSE after a failed status (a unit attention at a
  stick's first command, a medium not present). Host tested against a fake device behind the same calls
  (`tests/host/msc_test.c`): good transfers, short ones, stalls in each phase, a wrong tag, a device that
  never answers.
- **The disk:** 512-byte blocks only at first (a device that says otherwise is refused, said so); its size from
  READ CAPACITY; reads and writes in runs of at most 64 KB a command.
- **Into GLOS's DOS:** the native-dos session's `vdos_disk_attach(read, write, ctx, sectors, ...)` mounts each FAT
  volume on it (a superfloppy too) at the next free letters; `vdos_disk_detach` when it is taken out politely, and
  `vdos_disk_gone` when it went without warning (its volumes answer "not ready" from then on). Native only, as the
  CD's drives are. Not through the disk cache at first: a write held there would be lost when the stick goes.
  The read and write calls come on a thread with interrupts on, never from an interrupt handler: mostly the VM's
  (GLOS's DOS, the DOS server for apps and SSH/SFTP too), sometimes a kernel thread's (DISK's). They may sleep; a
  long wait calls `vdos_disk_breathe()` (on the VM's thread the VM's own pending interrupts run, then a tick's wait,
  as IDE's waits do; elsewhere a yield). A device gone without warning: its volumes answer 15h, each letter free
  once nothing has a file open on it (agreed with the native-dos session, 2026-11-26).
- **Hot-plug:** the USB thread's port and hub polling already sees a device come and go; a mass-storage
  interface (class 08h, subclass 06h SCSI, protocol 50h Bulk-Only) starts the disk, its going ends it.
- **In the Devices list and `glos usb`:** `kind=storage`, the stick's vendor and product (INQUIRY's), its size,
  its letters.

## Steps

1. **U1, UHCI bulk and the class driver:** bulk transfers on UHCI (a queue head of its own after the control one;
   TDs in runs, the data toggle kept per endpoint, polled as control transfers are); `msc.c` and its host test.
   `glos usb` shows a stick's INQUIRY and capacity. Intel's and VIA's chipsets (benches A, D?, F, G).
2. **U2, the drive letters:** the attach and hot-plug above; reads and writes through GLOS's DOS (DIR, COPY, a
   program run from the stick); a stick pulled with a file open. Proven on a bench with a real stick.
3. **U3, OHCI:** a host controller driver for OHCI (control, interrupt and bulk: so HID keyboards, mice and pads
   too) for ALi, SiS, NEC and OPTi chipsets and NEC's add-in cards. The P3 (ALi M5237) is its bench.
   *Built 2026-11-26:* uhci.c's controller part went behind `kernel/drv/usbhc.h`'s calls (struct hcd_ops: control,
   interrupt pipes, a report out, bulk, root ports, leave, stat), and `kernel/drv/ohci.c` gives OHCI's; its EDs and
   TDs are built by `ohcitd.c` (host tested: `ohci_test`). Unproven on silicon: 86Box's OHCI is a register stub
   (it walks no schedule), so the P3 is its first real test (a keyboard or mouse, then a stick).
4. **U4, EHCI:** high speed (480 Mbit/s) for USB 2.0 sticks on a root port or a high-speed hub; full- and low-speed
   devices stay on the companion controllers as now (no split transactions).
   *Built 2026-11-26, behind `[usb] ehci = on` (off by default until a real PC has shown it):* `ehcitd.c` builds
   the QHs and qTDs (host tested: `ehci_test`); `ehci.c`, after the hand-over, drives the controller when the
   setting is on: reset, an asynchronous schedule of a head QH and a QH for each endpoint used (linked in, never
   changed; the toggles from the qTDs), CONFIGFLAG set, the ports powered. It keeps a device only if it is a
   high-speed disk on a root port: low speed (the line's K state) and full speed (not enabled after the reset)
   go to the companion at once, and a high-speed device that isn't mass storage, a USB 2.0 hub included, once
   its configuration is read (the core asks `keeps`); so keyboards behind a USB 2.0 hub still work, at full
   speed, as before. (A stick behind a high-speed hub stays at full speed: the hub is given away.) Polled, one
   transfer at a time, a 16 KB piece each; a transfer given up on stops the asynchronous schedule while its QH is
   emptied. MAX_HC is 8 (an ICH5's four UHCIs and one EHCI). 86Box has no EHCI on Loop A's machines: the benches
   with one (F and G) are its first test.

## Tests

86Box has the controllers but no USB devices, so Loop A can prove only that nothing changed for machines without
a stick (the usb cases as they are), and the DOS side: the native-dos session's `glos disktest attach FILE` (a disk
image on C: behind the same calls, as a fake device; then detach and gone, with DIR, COPY and an open file across
each). The class driver is proven by host tests against a fake device; each
controller by its host model where it has one (`uhcitd.c` builds TDs without I/O, host tested); the whole on real
PCs with real sticks (the owner's: FAT16 and FAT32, a USB 1.1 and a USB 2.0 one).
