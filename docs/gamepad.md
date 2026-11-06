# GLOS gamepads and USB input: a plan

**Status (2026-11-06): J0-J6 built and checked on bench-g400 with the owner's pads, keyboards, mice, a combined
keyboard and touchpad, and a hub; J7's docs done; on GLOS main. Owed: the Xbox 360 pad (written, untested), mice's
wheels, EHCI and OHCI with the PCs that need them, and the hand-back to a BIOS that used USB.**
- **The ask:** after the Crystal work, the owner said there is no game-port joystick to hand, but there are USB
  gamepads, "Xbox-style" and generic HID, and "it would be great if they could somehow emulate a gameport joystick
  through GLOS".
- **The owner's choices:** both halves planned (a virtual game port, and USB pads to drive it), starting after the
  Crystal merge (GLOS main 2c8787e).
- **v0's questions, answered:** GLOS takes the USB controller over outright, with its own drivers for gamepads,
  keyboards, mice and hubs, and flash drives later. No 86Box USB model for now, no keypad joystick
  ([Decisions](#decisions)).
- **J0 is done** ([Results](#results)), except the owner's Xbox 360 pad, which is at home (due 2026-11-06).
- **J1 is built** (the virtual game port, `glos joy`, `[joystick]`; `make loopa-joy` passes on bf6 and the 486):
  PRD D74.
- **J2-J6 are built** (GLOS's UHCI driver, hubs, HID keyboards, mice and pads, Xbox pads, two pads at once,
  hot-plug), in the order J2, J4's pads, J5, J3, J4's keyboards and mice: the owner's pads were plugged in first,
  the hub, keyboard and mouse came later. Each checked by hand on bench-g400 ([Results](#results)).

DOS games read a joystick through the PC's game port at 201h, or through the BIOS. No DOS game knows USB, and DOS
knows USB keyboards and mice only through the BIOS's emulation. This plan gives GLOS:
- **a virtual game port**: port 201h answered by GLOS in DOS sessions, in DOS programs' protected mode and in GLOS
  apps, from whatever source drives it, plus the BIOS's joystick call (INT 15h AH=84h);
- **a USB stack of its own for input**: UHCI, hubs, HID keyboards, mice and gamepads, and Xbox pads (which aren't
  HID). Keys and mouse movement enter GLOS where the PS/2 ports' bytes do, so the desktop, apps and DOS sessions all
  get them. Pads drive the virtual game port;
- **a scripted joystick source** (`glos joy` over SSH), so Loop A tests everything but the USB half, and the bench
  can be tested without anyone holding a pad.

USB flash drives (mass storage as a BIOS disk) come later, in a plan of their own.

PRD sections touched:
- the non-goals (USB stacks; USB keyboards only through the BIOS);
- §4.5 (input);
- §6.4's port table (201h "always passed through");
- §7.6 ("the gameport isn't injectable on silicon");
- D48 (apps' gameport); D70 (devices' keepers).

Docs to change at the end: PRD, supervisor.md §10, docs/devices.md, the release notes.

## What a DOS program sees

The IBM Game Control Adapter, from the PC's technical reference (the clean-room source for this half):
- **One write to 201h** (any value) fires four one-shots, one per axis. **Bits 0-3** of a read are 1 while each is
  timing: joystick A's X and Y, then B's. An axis times out after 24.2 µs + 0.011 µs per ohm of its potentiometer,
  0 to 100 kΩ: **24 µs to 1124 µs**.
- **Bits 4-7** are the four buttons (A1, A2, B1, B2), **0 while pressed**.
- **INT 15h AH=84h**: DX=0 returns the buttons in AL bits 4-7; DX=1 returns the four axes in AX, BX, CX, DX as the
  BIOS's own counts. CF=1 when there is no game port.

Programs measure an axis in one of three ways:
- most games count turns of a loop around `IN AL,DX` until the bit drops, then scale by a calibration taken in
  their set-up screen (centre, then the corners);
- some time the one-shot with the PIT or the TSC (SDL's DOS joystick, supervisor.md §12.6);
- some ask the BIOS.

## Why the PRD says never to trap 201h, and why this is different

The PRD passes 201h through everywhere ("trapping it breaks axis timing", §6.4; D48 for apps). That holds for
forwarding a real joystick through a trap: every read then costs a trap, and the real one-shot runs out while the
trap is handled. A **virtual** port has no real one-shot. GLOS notes the time of the write and answers each read
from the time now:
- **programs that time with the PIT or TSC** get the true value, to within one trap;
- **programs that count loop turns** get fewer turns, because each turn now costs a trap. Their scale stays
  consistent, because their calibration ran under the same trap. Resolution falls with the trap's cost. Estimates,
  to be measured in J1: a few µs a trap on the bench's Pentium II gives several hundred steps across the range;
  tens of µs on a 486DX2 gives about fifty;
- **a program with a fixed threshold and no calibration** could misread. `[joystick] range=` stretches the
  one-shots for it.

So the rule becomes: **201h is passed through while no virtual source is on (as today), and virtual while one is.**
The real port (the Crystal chip's at 200h-207h, an SB16's, an ISA card's) is then hidden. One limit stays: a
direct-mode program's protected-mode code (IOPL 3, D20) skips the I/O map and reads the real port.

Time for the answers: the TSC on a Pentium or later. Without one (a 486), GLOS reads PIT channel 0 while it is
GLOS's, and counts reads, scaled by the trap's measured cost, while an exclusive session owns the PIT.

## Sources and mapping

A source sets up to four axes (0-255, 128 the centre) and four buttons:
- **`glos joy`** over SSH: `glos joy a=128,128 b=0,255 buttons=1010`, or `glos joy` alone to print the live
  state. Loop A's source, and the bench's when nobody holds a pad.
- **USB pads** (J4-J6).

The default mapping, overridable with `[joystick] map=`:
- **One pad drives the whole port.** Left stick and D-pad → joystick A's X/Y; right stick → B's X/Y (where four-axis
  games look for a rudder or throttle); A, B, X, Y → buttons A1, A2, B1, B2 (as four-button pads such as Gravis's
  GamePad do).
- **With a second pad**, pad 2 becomes joystick B (its left stick → B's X/Y, its A and B → B1 and B2), and pad 1
  keeps A's axes and buttons A1 and A2.
- Triggers are unmapped by default.

GLOS apps get the same virtual port (D48 changes with it). Joystick events in the GLOS API (PRD §10) come later,
outside this plan.

## The USB half

### The controller

bench-g400's PIIX4 UHCI (00:02.2, 8086:7112, I/O at BAR 4, IRQ 11 shared with the e100). Today GLOS makes it
*quiet* at boot (D70 D1: `USBPIRQDEN` off, `kernel/drv/dev.c`). The driver keeps it that way:
- **No interrupt at all.** GLOS's 1 ms tick checks the active bits of the few transfer descriptors (TDs) it has
  queued, and queues work for a thread when one has finished. HID devices report every 1-10 ms. IRQ 11 stays the
  e100's alone, so D2 (chained handlers) isn't needed.
- **GLOS takes every UHCI it finds** (the owner's answer to Q1), unless `[usb] off` (an escape hatch for a PC where
  it goes wrong):
  - through the legacy support register (config C0h): the BIOS's SMI enables off, the controller stopped and
    reset;
  - its keeper (D70) becomes GLOS;
  - the BIOS's USB keyboard and mouse emulation stops, and GLOS's own drivers take over at once (J4), so a PC with
    only USB input keeps it.
- **At `glos exit`**: the controller is reset, and its registers (legacy support, frame list base, `USBINTR`) are
  put back as found, for the BIOS or a DOS driver. Whether a BIOS's emulation picks up again after that is checked
  on a PC that has it on (J0 shows whether bench-g400's BIOS does). If not, the release notes say so.
- **Clean-room sources:**
  - Intel's *Universal Host Controller Interface Design Guide* rev 1.1 (1996): registers 00h-13h, a 1024-entry
    frame list (4 KB aligned), queue heads and TDs;
  - the PIIX4 datasheet for the legacy support register;
  - the USB 1.1 specification for port reset, hubs and timing.

### Steps on the wire

- **Root ports:** two on a PIIX4. A connect is seen in `PORTSC`; then the port is reset (50 ms), enabled, and its
  speed read (low or full).
- **Enumeration**, by control transfers on a queue head: the device descriptor (8 bytes, for the control
  endpoint's packet size), `SET_ADDRESS`, the full device and configuration descriptors, `SET_CONFIGURATION`.
- **Hubs** (class 09h, USB 1.1 chapter 11):
  - the hub descriptor; each port powered;
  - the status-change endpoint polled; ports reset, enabled and enumerated as root ports are;
  - low-speed devices behind a full-speed hub get UHCI's low-speed TDs, with the preamble the controller adds;
  - hubs nest up to the specification's limit.
- **Interrupt IN**: one queue head per device, at its endpoint's interval. Each TD is re-armed with the data
  toggle flipped. Unplugging is seen in `PORTSC` or a hub's port status, and frees the device.
- **Log lines:** `GLOS-USB port=1 speed=full id=045e:028e class=ff/5d/01 is=xbox360`; `GLOS-USB port=1.3
  speed=low id=... is=keyboard`; `GLOS-USB port=1 gone`.

### HID devices (class 03h)

From the *Device Class Definition for HID* 1.11 and the *HID Usage Tables*:
- **Keyboards:**
  - **Protocol:** boot protocol (`SET_PROTOCOL` 0): an 8-byte report of modifier bits and up to six keys held.
  - **Turning reports into keystrokes:** each report is compared with the last; keys pressed and released become
    scan code set 1 make and break codes, fed in where the PS/2 keyboard's bytes are (the virtual 8042's input,
    `kernel/vm/vkbc.c`). The desktop and every DOS program see them as a PS/2 keyboard's.
  - **Repeat and LEDs:** typematic repeat is GLOS's, because USB keyboards don't repeat; the rate and delay come
    from the virtual 8042's F3h command. The LEDs follow its EDh command, by `SET_REPORT`.
- **Mice:**
  - **Protocol:** boot protocol: buttons and X/Y movement. The wheel comes in report protocol, where the
    descriptor has one.
  - **Delivery:** fed in where the PS/2 mouse's packets are, so the desktop, INT 33h drivers and the PS/2 BIOS
    (INT 15h C2xxh, Quake's) see a PS/2 mouse.
- **Gamepads and joysticks:**
  - **The report descriptor** (`GET_DESCRIPTOR`, type 22h) is parsed for a Joystick (04h) or Game Pad (05h)
    collection, the Generic Desktop page's X, Y, Z, Rx, Ry, Rz and Hat switch, and the Button page.
  - **Each value is scaled** from its logical range to 0-255. Report IDs are honoured. `SET_IDLE(0)` is sent,
    ignoring a stall.
- **Host tests** run the parser and the keyboard's report-to-scan-code step on J0's descriptors and on hand-built
  ones from the spec's examples.
- **The limit is the same as for the game port:** a direct-mode program's protected-mode code reads the real 8042
  (60h/64h aren't trapped there), so it sees only PS/2 input.

### Xbox pads

These aren't HID; they use vendor classes:
- **Xbox 360 wired** (interface FFh/5Dh/01h): a 20-byte input report with the buttons, two 8-bit triggers and four
  16-bit stick axes; an LED command on the OUT endpoint.
- **Xbox One and Series wired** (FFh/47h/D0h, "GIP"): silent until a start packet is sent; then input reports of
  type 20h.
- **Sources:** public protocol notes, checked against the owner's pads in J0 (their descriptors, and Linux's
  traffic to them captured with usbmon). Linux's `xpad.c` is GPL: behaviour reference only, as for every driver
  here.
- **Out of scope:** wireless receivers. The Xbox Wireless Adapter is a Wi-Fi device, not USB HID; the 360's
  receiver could come later.

### Other controllers

Not on bench-g400; each comes with the PC that needs it:
- **EHCI (USB 2.0, ICH4 and later):** GLOS takes it from the BIOS (the EHCI's own legacy support capability) and
  clears its configure flag, so every port falls back to the UHCI companions. GLOS's input devices are all low and
  full speed. This is likely on bench #2 (Pentium 4, G450), depending on its chipset.
- **OHCI (SiS, ALi, VIA's later chipsets, NEC cards):** a second host controller driver under the same hub and
  device layers.

**86Box can't test this half.** Its USB (`src/usb.c`) is registers with no devices. So UHCI, hubs, HID and Xbox
pads are tested by host tests and on the bench (the owner chose that over an 86Box model, Q2).

## The steps

- **J0, look (read-only):**
  - **On the bench under Ubuntu, with the owner's pads plugged in:** `lsusb -v`; each HID device's report
    descriptor (`/sys/bus/hid/devices/*/report_descriptor`); and a usbmon capture of Linux enumerating them and
    starting the Xbox pad, then of each pad's reports while the owner moves each stick and presses each button as
    asked. These are kept under docs/gamepad-j0/. The pads' vendor, product, interface classes, endpoints and
    intervals decide J5's order (360 or GIP first).
  - **Under DOS (GLOS's agent):** the UHCI's state before GLOS takes it: `USBCMD.RS` and the legacy support
    register, which show whether the IBM BIOS's USB emulation is on.
- **J1, the virtual game port:**
  - **The port:** 201h trapped when a source is on, in the system VM, sessions, DPMI clients and apps, plus INT 15h
    AH=84h.
  - **The source:** `glos joy`.
  - **Tests:** JOYTEST (Open Watcom, real mode: loop count, PIT timing and the BIOS call: `HX-JOY a=... b=...
    buttons=...`) and a DJGPP build of it (a DPMI client), then an app. Each runs with the trap's cost measured on
    bf6 and 486dx2.
  - **Loop A:** `make loopa-joy`.
  - **Bench:** JOYTEST on bench-g400 with `glos joy`. That brings the virtual port to silicon before any USB.
- **J2, UHCI:**
  - take-over and hand-back; the frame list, queue heads and TDs in GLOS's own memory (the controller reaches all
    of the first 4 GB), polled from the tick; the root ports; control transfers and enumeration with its log lines;
  - **Tests:** host tests for the schedule builder; on the bench, the owner plugs and unplugs as asked.
- **J3, hubs:** the hub driver; devices behind a hub, low speed among them. Bench: a hub the owner has, with a pad
  or keyboard behind it.
- **J4, HID:**
  - the parser; keyboards and mice into the PS/2 path; HID pads as a source. Host tests on J0's descriptors;
  - **On the bench:**
    - a USB keyboard typing at a DOS prompt and in the desktop, with repeat and LEDs; a USB mouse in the desktop
      and in a DOS program;
    - each pad's sticks and buttons as asked, shown by `glos joy`.
- **J5, Xbox pads:** 360 and GIP, whichever J0 shows first. Bench as J4.
- **J6, mapping and two pads:** `[joystick] map=` and `range=`, two pads at once, hot-plug.
- **J7, acceptance and docs:**
  - **On the bench:** a DOS game with joystick support played with a pad: one of ours, or one of the owner's that
    the owner names (C:\GAMES is the owner's, used read-only).
  - **Docs:** PRD (D74, the non-goals, §4.5, §6.4, §7.6, D48), supervisor.md §10, devices.md (the UHCI's keeper
    is GLOS), release notes.
- **Later, each with its PC:** EHCI hand-off and OHCI (above). USB flash drives, in a plan of their own.

Order: J1 needs nothing else and is the part every machine gets. J2-J6 are bench-only work.

## Results

**J0 (2026-11-05, bench-g400; details and captures in [gamepad-j0/](gamepad-j0/README.md)):**
- **The HID pad: 081f:e401 "USB gamepad"**, an SNES-style pad.
  - **USB:** low speed, an 8-byte report every 10 ms.
  - **Controls:** the D-pad is X/Y (0, 127, 255); X, A, B, Y are buttons 1-4; L and R are 5 and 6; Select and
    Start are 9 and 10.
  - **For GLOS:** a plain case for the HID parser, and the low-speed case for UHCI.
- **The Xbox pad: 20d6:200c, PowerA "Xbox Series X EnWired"**, a GIP pad at full speed.
  - **Start-up:** silent until the power-on packet `05 20 00 01 00`; then a 36-byte input report (type 20h) on
    every change, and a status message every 19.5 s.
  - **Report layout:** confirmed control by control. Sticks are signed 16-bit, triggers 0-1023, sticks rest up to
    about ±7,000 off centre (so a dead zone).
  - **The Xbox button sent nothing** with only the power-on packet: it needs more of GIP's start-up, not needed for
    the game port.
  - **So J5 starts with GIP.** The 360 pad follows when it is here.
- **bench-g400's UHCI:** the IBM BIOS leaves it stopped, with no legacy support (no SMI, no 60h/64h traps).
  - **On this PC:** nothing to take over or hand back. Both root ports have a pad, neither port enabled.
  - **Still to check:** take-over from an active BIOS, and hand-back at `glos exit`, need a PC whose BIOS has USB
    legacy support on: bench #2, perhaps.

**J1 (2026-11-05, Loop A, `make loopa-joy`):** `glos joy a=0,255 b=128,64 buttons=1010`, so one-shots of 24, 1124,
576 and 300 µs, as JOYTEST measured them by the PIT (the median of five readings):

| | bf6 (TSC) | 486DX2 (PIT channel 0) |
|---|---|---|
| Real mode (V86) | 36, 1142, 589, 312: 6.6 µs a read | 74, 1187, 626, 377: 45 µs a read |
| DPMI client (DJGPP) | 38, 1143, 592, 315: 8.5 µs a read | 116, 1247, 677, 372: 69 µs a read |
| The PIT set to 100 Hz first | 36, 1132, 583, 309 (still the TSC) | 65, 1086, 579, 302 (counted reads at 43.3 µs) |

- **Accuracy:** each length is the true one plus at most one read's interval, which is what a program polling the
  port can see.
- **Resolution:** the reads across the range a counting program gets: about 170 on bf6, 26 on the 486 in real mode
  and 18 as a DPMI client. These are 86Box's costs: silicon's come with the bench run.
- **INT 15h AH=84h** answers 6, 281, 144, 75 (4 µs units) in real mode and through a DPMI client's 0300h alike.
- **On bench-g400** (Pentium II, 398 MHz by the TSC; the Crystal chip's real game port, nothing plugged in, reads as
  nothing connected): the same sequence by hand.
  - **Real mode:** 32-33, 1129-1132, 581-588, 310 µs over six runs, at 6 µs a read: 186 reads across the range.
  - **As a DPMI client:** 33, 1138, 587, 308, at 7.5 µs a read.
  - **INT 15h, the app, `b=none`, a 100 Hz PIT, `glos joy off`:** as in Loop A.
  - **One fault found and fixed:** the TSC's rate had been measured a second at a time, and once read 381 MHz for
    398 (a tick late at a window's end), so the stick read 4% short. It is now measured from GLOS's first tick on.
- **Found on the way:** a client's simulated INT 15h (0300h, or a reflected one) went straight to the BIOS,
  bypassing GLOS's INT 15h layer (int15.c). J1 routed AH=84h; the rest followed the same day (GLOS main: all of
  int15.c but 86h's sleep; a DPMI program's block move had been killed for the BIOS's LGDT in V86).
- **An app's own timing is coarse:** an app's PIT steps in kernel ticks (976 µs) while a session runs, as the agent's
  jobs are. So the suite checks apps for order, buttons and the BIOS's answer only.

**J2 (2026-11-05, bench-g400; 86Box's machines show no UHCI, so Loop A checks only that nothing else broke):**

    GLOS-PCI claim=00:02.2 id=8086:7112 irq=11
    GLOS-USB uhci 00:02.2 io=ff00 ports=2 legsup=0000
    GLOS-USB port=1 speed=low id=081f:e401 class=00/00/00 if=03/00/00 in=81/8/10 is=hid
    GLOS-USB port=2 speed=full id=20d6:200c class=ff/47/d0 if=ff/47/d0 in=81/64/4 out=yes is=xbox-gip

- **Enumeration:** both pads addressed and configured, at the first attempt; their descriptors as Linux read them
  in J0.
- **Shared IRQ 11:** the e100 on it is unaffected, since GLOS polls the UHCI. These SSH sessions run over the e100.
- **Hand-back:** `glos restart` hands the controller back (stopped, reset, its legacy register put back) and takes
  it again.
- **Frame counter:** runs (`glos usb`).
- **Host test:** `tests/host/uhci_test.c` checks the TDs of three control transfers.

**J3-J6 (2026-11-05, bench-g400, by hand with the owner):**
- **HID pad (the SNES-style 081f:e401):** its report descriptor parsed (`hidparse.c`, host test on its own 98
  bytes); the pad drives the virtual game port: at rest JOYTEST reads 579 µs (127/255 of the range), the D-pad
  0/127/255 on each axis, buttons 1 and 2 as A1 and A2. It answers every poll (ignoring SET_IDLE 0), so only a
  changed report goes on.
- **Xbox Series pad (PowerA 20d6:200c, GIP):** woken by the power-on packet on its interrupt OUT endpoint; its
  reports (`xpad.c`, host test on J0's captured reports) as joystick B with the SNES pad as A: the left stick and
  D-pad to 0 and 255 each way, A and B as B1 and B2. JOYTEST read all four axes at rest: 581, 581, 588, 588 µs.
- **Hot-plug:** the SNES pad unplugged: gone within a second, the Xbox pad the whole port; plugged back: found again
  (a new address) and joystick A again.
- **Hub (Genesys Logic 05e3:0608, four ports, full speed):** the Xbox pad, a keyboard (0603:00f2) and a mouse
  (15ca:00c3), both low speed, found behind it as `port=2.1` to `2.3`.
- **Keyboard:** TYPEIN read `Hello GLOS 123xxxxxxxxxxxxx` (Shift's capitals; x held two seconds: GLOS's repeat).
- **Mouse:** the desktop's pointer into all four corners (1,683 reports, no errors); "seemed perfect" (the owner).
- **Lock lights (2026-11-06):** the byte after a program's EDh (the BIOS's INT 09h on a lock key) goes to every USB
  keyboard as its LED output report, and a keyboard plugged in gets the BIOS's locks at once: Caps, Num and Scroll
  Lock "worked perfectly" (the owner).
- **A combined keyboard and touchpad (05a4:1700, full speed, interfaces 0 and 1):** each interface an entry and a
  pipe of its own (`port=2.4`, `2.4:1`). Its keyboard typed `Combo test 456`. Its touchpad ignored SET_PROTOCOL 0
  and kept the report ID first, so read as a boot mouse a left-right swipe moved the pointer up and down and each
  click nudged it right (the button byte as X). Mice are now read by their report descriptors in the report
  protocol (`GLOS-USB port=2.4:1 mouse report=1 x=8/8 y=16/8 wheel=24/8 buttons=1f`), the boot reports a fallback:
  the touchpad "worked well", the other mouse still fine.
- **Loop A:** 86Box's bf6 has a PIIX4 UHCI with nothing on it; the take-over and hand-back run on every boot and
  `glos exit` there. That found a double fault: `usb_leave` slept after the kernel's tick had stopped (86Box's
  HCRESET never clears). It now busy-waits there.

## The PRD change (made with J1)

- **Non-goals:** "USB stacks … are out of scope. USB keyboards work only through BIOS legacy emulation in exclusive
  sessions." becomes "USB storage and USB 2.0 speeds are out of scope for now. GLOS has its own USB input stack
  (D74): UHCI, hubs, HID keyboards, mice and gamepads, Xbox pads."
- **D74** (now in the PRD):
  - **The decision:** "GLOS has its own USB input stack: every UHCI is taken from the BIOS, polled from the tick
    with no interrupt; hubs; HID keyboards and mice fed in as PS/2 ones; HID and Xbox pads drive a virtual game
    port. GLOS answers port 201h and INT 15h AH=84h itself while a joystick source is on (a pad, or `glos joy`),
    timing the one-shots from the time of the write. Otherwise 201h is passed through as before. (Owner,
    2026-11-05: USB pads as a game-port joystick; take USB over, with keyboards, mice and hubs too; flash drives
    later; no 86Box model for now.)"
  - **Why:** "The owner has USB pads and no game-port joystick. DOS games know only the game port, and DOS knows
    USB input only through the BIOS's emulation."

## Decisions

The owner, 2026-11-05 (the question tool):
- **Q1, when GLOS takes the USB controller from the BIOS.** The question offered: only when GLOS.CFG says so
  (recommended); whenever a UHCI is found; or only when the BIOS isn't using it. The owner chose neither, and
  wrote: "Can't we just take over USB and add drivers for gamepads, keyboards and mice (and USB hubs)? We could
  add flash drives at a later date." So:
  - GLOS takes every UHCI (`[usb] off` the escape hatch);
  - keyboards, mice and hubs join the plan (J3, J4);
  - flash drives come later.
- **Q2, an 86Box UHCI and gamepad model:** not now (the recommendation). Host tests and the bench; revisit if J2-J5
  go slowly.
- **Q3, the numeric keypad as a joystick source:** no (the recommendation).
- **J0:** now. The owner plugs the pads into bench-g400 under Ubuntu.

## Provenance

- **Clean-room sources:** IBM's technical reference (game control adapter, INT 15h AH=84h); Intel's UHCI design
  guide and the PIIX4 datasheet; usb.org's USB 1.1, HID 1.11 and HID Usage Tables; public Xbox protocol notes;
  usbmon captures of the owner's pads.
- **Behaviour reference only:** Linux's `uhci-hcd`, `hub`, `usbhid` and `xpad`, and FreeBSD's equivalents. No
  code from them.
- **What goes in the repo:** J0's descriptor captures and pad traffic are facts about the hardware, so they go in
  the repo (docs/gamepad-j0/). Screenshots and traces of the owner's games stay local.
