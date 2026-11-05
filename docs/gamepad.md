# GLOS gamepads: USB pads as the PC's game-port joystick, a plan

**Status (2026-11-05): plan v0, nothing built (branch `gamepad`). The owner asked for it after the Crystal work:
there is no game-port joystick to hand, but there are USB gamepads, "Xbox-style" and generic HID, and "it would be
great if they could somehow emulate a gameport joystick through GLOS". The owner chose to plan both halves (a virtual
game port, and USB pads to drive it), with the work starting after the Crystal merge (GLOS main 2c8787e). Three
questions are open ([Decisions](#decisions)).**

DOS games read a joystick through the PC's game port at 201h, or through the BIOS. No DOS game knows USB.
- **A virtual game port in GLOS**: port 201h answered by GLOS in DOS sessions, in DOS programs' protected mode
  and in GLOS apps, from whatever source drives it, plus the BIOS's joystick call (INT 15h AH=84h).
- **USB pads as its source**: GLOS's own UHCI driver for the bench's PIIX4, enumeration, HID gamepads, and Xbox
  pads (which aren't HID).
- **A scripted source** (`glos joy` over SSH), so Loop A tests everything but the USB half, and the bench can be
  tested without anyone holding a pad.

PRD sections touched: the non-goals (USB stacks), §4.5 (input), §6.4's port table (201h "always passed through"),
§7.6 ("the gameport isn't injectable on silicon"), D48 (apps' gameport), D70 (devices' keepers). Docs to change at
the end: PRD, supervisor.md §10, docs/devices.md, the release notes.

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
- **USB pads** (J2-J5).

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

**The controller.** bench-g400's PIIX4 UHCI (00:02.2, 8086:7112, I/O at BAR 4, IRQ 11 shared with the e100).
GLOS made it *quiet* at boot (D70 D1: `USBPIRQDEN` off, `kernel/drv/dev.c`). The driver keeps it that way:
- **No interrupt at all.** GLOS's 1 ms tick checks the active bits of the few transfer descriptors (TDs) it has
  queued, and queues work for a thread when one has finished. Pads report every 1-10 ms. IRQ 11 stays the e100's
  alone, so D2 (chained handlers) isn't needed.
- **Taking it from the BIOS**:
  - through the legacy support register (config C0h): the SMI enables off, the controller stopped and reset;
  - only as [Decisions](#decisions) Q1 allows, because a BIOS-emulated USB keyboard stops working once GLOS has
    the controller (bench-g400's keyboard and mouse are PS/2);
  - at `glos exit` the controller is reset and its registers (legacy support, frame list base, `USBINTR`) put back
    as found, for the BIOS or a DOS driver.
- **Clean-room sources:**
  - Intel's *Universal Host Controller Interface Design Guide* rev 1.1 (1996): registers 00h-13h, a 1024-entry
    frame list (4 KB aligned), queue heads and TDs;
  - the PIIX4 datasheet for the legacy support register;
  - the USB 1.1 specification for port reset and timing.

**Steps on the wire:**
- **Root ports:** two on a PIIX4. A connect is seen in `PORTSC`; then the port is reset (50 ms), enabled, and its
  speed read (low or full).
- **Enumeration**, by control transfers on a queue head: the device descriptor (8 bytes, for the control
  endpoint's packet size), `SET_ADDRESS`, the full device and configuration descriptors, `SET_CONFIGURATION`.
- **No hubs in v1**: pads go into the root ports. A hub is logged and left alone.
- **Interrupt IN**: one queue head per pad, at its endpoint's interval. Each TD is re-armed with the data toggle
  flipped. Unplugging is seen in `PORTSC` and frees the pad's slot.
- Log lines: `GLOS-USB port=1 speed=full id=045e:028e class=ff/5d/01 pad=xbox360`; `GLOS-USB port=2 gone`.

**HID pads** (class 03h). From the *Device Class Definition for HID* 1.11 and the *HID Usage Tables*:
- **The report descriptor** (`GET_DESCRIPTOR`, type 22h) is parsed for a Joystick (04h) or Game Pad (05h)
  collection, the Generic Desktop page's X, Y, Z, Rx, Ry, Rz and Hat switch, and the Button page.
- **Each value is scaled** from its logical range to 0-255.
- **Report IDs are honoured.** `SET_IDLE(0)` is sent, ignoring a stall.
- **Host tests** run the parser on J0's descriptors and on hand-built ones from the spec's examples.

**Xbox pads** aren't HID; they use vendor classes:
- **Xbox 360 wired** (interface FFh/5Dh/01h): a 20-byte input report with the buttons, two 8-bit triggers and four
  16-bit stick axes; an LED command on the OUT endpoint.
- **Xbox One and Series wired** (FFh/47h/D0h, "GIP"): silent until a start packet is sent; then input reports of
  type 20h.
- **Sources:** public protocol notes, checked against the owner's pads in J0. Linux's `xpad.c` is GPL: behaviour
  reference only, as for every driver here.
- **Out of scope:** wireless receivers. The Xbox Wireless Adapter is a Wi-Fi device, not USB HID; the 360's
  receiver could come later.

**86Box can't test this half.** Its USB (`src/usb.c`) is registers with no devices. So UHCI, HID and Xbox pads
are tested by host tests and on the bench, unless an 86Box UHCI and gamepad model joins MGA-Glide's patch series
(Q2).

## The steps

- **J0, look (read-only):**
  - **On the bench under Ubuntu, with the owner's pads plugged in:** `lsusb -v` and each pad's HID report
    descriptor (`/sys/bus/usb/devices/*/…/report_descriptor`) are kept under docs/gamepad-j0/. The pads' vendor,
    product, interface classes, endpoints and intervals decide J4's order (360 or GIP first).
  - **Under DOS:** the UHCI's state before GLOS starts: `USBCMD.RS` and the legacy support register, which show
    whether the IBM BIOS's USB legacy emulation is on.
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
- **J3, HID pads:** the parser (host tests on J0's descriptors), interrupt IN, the pad as a source. On the bench:
  the owner moves each stick and presses each button as asked, and `glos joy` shows the result.
- **J4, Xbox pads:** 360 and GIP, whichever J0 shows first. Bench as J3.
- **J5, mapping and two pads:** `[joystick] map=` and `range=`, two pads at once, hot-plug.
- **J6, acceptance and docs:**
  - **On the bench:** a DOS game with joystick support played with a pad: one of ours, or one of the owner's that
    the owner names (C:\GAMES is the owner's, used read-only).
  - **Docs:** PRD (D74, the non-goal narrowed, §4.5, §6.4, §7.6, D48), supervisor.md §10, devices.md (the UHCI's
    keeper becomes GLOS while pads are on), release notes.

Order: J1 needs nothing else and is the part every machine gets. J2-J5 are bench-only work.

## The PRD change (proposed)

- **Non-goals:** "USB stacks … are out of scope" becomes "out of scope **except a UHCI driver for gamepads**
  (D74)". The note on USB keyboards stays, plus: "while GLOS has a UHCI controller, the BIOS's emulation on it stops".
- **D74** (once Q1-Q3 are answered):
  - **The decision:** "GLOS answers port 201h and INT 15h AH=84h itself while a joystick source is on: USB
    gamepads (HID, Xbox 360, Xbox One) through its own UHCI driver, polled with no interrupt, or `glos joy`. The
    one-shots are timed from the time of the write. Otherwise 201h is passed through as before."
  - **Why:** "The owner has USB pads and no game-port joystick; DOS games know only the game port."

## Decisions

Open, for the owner:
- **Q1. When does GLOS take the USB controller from the BIOS?** Recommended: only when GLOS.CFG says so
  (`[usb] pads = on`), because a BIOS-emulated USB keyboard stops working when GLOS takes the controller.
  bench-g400's GLOS.CFG would say so. The alternatives: whenever a UHCI is found; or only when the BIOS isn't using
  it (`USBCMD.RS` off at start).
- **Q2. An 86Box UHCI and gamepad model in MGA-Glide's patch series?** Recommended: not now; host tests and the
  bench, now that `glos restart` makes a bench update take seconds. Revisit if J2-J4 go slowly. A model is a
  sizeable patch (a UHCI schedule walker and a HID device) that only GLOS would use.
- **Q3. A keyboard-to-joystick source** (the numeric keypad as a stick)? Recommended: no. The owner has pads, and
  `glos joy` covers testing.

## Provenance

- **Clean-room sources:** IBM's technical reference (game control adapter, INT 15h AH=84h); Intel's UHCI design
  guide and the PIIX4 datasheet; usb.org's USB 1.1, HID 1.11 and HID Usage Tables; public Xbox protocol notes.
- **Behaviour reference only:** Linux's `uhci-hcd`, `usbhid` and `xpad`, and FreeBSD's equivalents. No code from
  them.
- **What goes in the repo:** J0's descriptor captures are facts about the hardware, so they go in the repo
  (docs/gamepad-j0/). Screenshots and traces of the owner's games stay local.
