# J0: the owner's pads and bench-g400's USB controller (2026-11-05)

Captured on bench-g400 (IBM PC 300PL, PIIX4 UHCI 00:02.2). For docs/gamepad.md; read-only throughout. The Xbox
pad's serial number is left out of every file.

| File | What |
|---|---|
| `lsusb-081f-e401.txt`, `lsusb-20d6-200c.txt` | `lsusb -v` under Ubuntu 16.04 (Linux 4.4) |
| `rdesc-081f-e401.bin` | the HID pad's report descriptor, from `/sys/bus/hid/devices/…/report_descriptor` |
| `enum-*.usbmon.txt` | Linux enumerating each pad (usbmon's text format: control transfers only) |
| `guided-20d6-200c.txt` | the Xbox pad's reports while the owner pressed each control in turn (`gip.py`, below) |
| `guided-081f-e401.txt` | the HID pad's reports, the same way (from `/dev/hidraw0`) |
| `gip.py`, `hid.py` | the two capture tools (Python 3, run as root under Linux) |

The guided captures print a report only when it differs from the one before, with seconds since the start.

## The HID pad: 081f:e401 "USB gamepad"

- **The device:** an SNES-style pad with a D-pad, four face buttons, L, R, Select and Start. USB 1.0, low speed,
  control packets of 8 bytes. One interface, HID 1.10 (03h/00h/00h), interrupt IN 81h, 8 bytes, every 10 ms.
- **The report descriptor** (98 bytes): a Joystick (Generic Desktop 04h) collection, with no report IDs. The
  8-byte input report:

  | Bytes | Contents |
  |---|---|
  | 0, 1 | X and Y, 0-255. The D-pad: 0, 127 or 255; up is Y 0, left is X 0 |
  | 2-4 | constant (80h 80h 80h) |
  | 5, low 4 bits | constant (Fh) |
  | 5 high 4 bits, 6, 7 low 2 bits | buttons 1-10 |
  | 7 high 6 bits | vendor bits (page FF00h; always 0) |

  There is also a 7-byte output report (vendor), which does nothing useful.
- **The buttons** (the owner's labels, confirmed by the order pressed):

  | Button | 1 | 2 | 3 | 4 | 5 | 6 | 9 | 10 | 7, 8 |
  |---|---|---|---|---|---|---|---|---|---|
  | Label | X (up) | A (right) | B (down) | Y (left) | L | R | Select | Start | none on the pad |

## The Xbox pad: 20d6:200c, PowerA (BDA) "Xbox Series X EnWired Controller"

- **The device:** USB 2.0 but full speed (12 Mbit/s), so UHCI drives it. Device and interfaces are class
  FFh/47h/D0h (GIP), and control packets are 64 bytes.
  - **Interface 0:** interrupt OUT 01h and IN 81h, 64 bytes, every 4 ms.
  - **Interface 1:** the headset's audio, isochronous, in alternate setting 1. Not used.
- **Ubuntu 16.04's xpad doesn't know it**, so no driver was bound, and Linux sent it nothing after enumeration.
  `gip.py` (a scratch tool talking to `/dev/bus/usb` directly) showed:
  - **Before a start packet:** the pad sends nothing.
  - **After `05 20 00 01 00`** (GIP command 05h, flags 20h, sequence 0, length 1, payload 00h: power on):
    - **Status messages:** `03 20 nn 04 80 00 00 00`, the first at once, then every 19.5 s.
    - **Input reports** on every change: `20 00 nn 20` and 32 bytes of payload (36 in all).
- **The input report** (offsets from the start of the message):

  | Bytes | Contents |
  |---|---|
  | 0-3 | 20h, flags 00h, sequence, length 20h |
  | 4-5 | buttons, little-endian |
  | 6-7, 8-9 | LT, RT: 0-1023 |
  | 10-11, 12-13 | left stick X, Y: signed 16-bit, full range, up positive |
  | 14-15, 16-17 | right stick X, Y: the same |
  | 18-35 | always 0 in the capture |

  **The button bits** (bits 0 and 1 never set):

  | Bit | 2 | 3 | 4-7 | 8-11 | 12, 13 | 14, 15 |
  |---|---|---|---|---|---|---|
  | Button | Menu | View | A, B, X, Y | D-pad up, down, left, right | LB, RB | left-stick click, right-stick click |
- **The sticks at rest** sit up to about ±7,000 off centre, so a dead zone is needed.
- **Not seen:**
  - **The Xbox button.** On GIP pads it comes as a message of its own (07h). With only the power-on packet, this
    pad sent none: it likely needs more of GIP's start-up exchange. The game port doesn't need it.
  - **The back button the owner caught.** The EnWired's back buttons are programmable, and send nothing while
    unassigned.
- **Still to capture:** the owner's Xbox 360 pad, which is at home (due 2026-11-06). Linux 4.4's xpad knows 360
  pads, so its start-up traffic can be captured.

## bench-g400's UHCI under DOS (USBPEEK, with GLOS running)

```
HX-USBPEEK pci=00:02.2 id=8086:7112 cmd=0005 irq=11 legsup=0000 io=FF00 usbcmd=0000 usbsts=0020 usbintr=0000
           frnum=0000 frbase=12FAF000 sof=40 port1=0183 port2=0083
  means: stopped; legacy SMI off, 60h/64h traps off; port1=connected,low-speed port2=connected
```

- **The IBM BIOS doesn't use the controller.** It is stopped and halted, and the legacy support register is 0. GLOS
  clears only bit 13 (PIRQDEN), and that bit doesn't show whether the BIOS had it, but the SMI enable and the
  traps are off. So on this PC there is no BIOS USB emulation to take over or hand back.
- **The frame list base is Linux's leftover:** the boot came straight from Ubuntu (a warm reboot), and the BIOS
  didn't touch the controller.
- **Both pads are connected but neither port is enabled.**
