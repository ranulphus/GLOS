# Control Panel

Every setting of GLOS, a page for each kind, the pages listed at the left. Type in **Find a setting**
(Ctrl+F) to find one by a word. Under the pages you can read what the chosen setting does and when a change
takes effect: at once, when a program next starts, or when GLOS next starts.

**OK** keeps your changes and closes, **Apply** keeps them and stays, **Cancel** forgets them. A new screen
mode is tried for fifteen seconds and kept only if you say so, in case the monitor cannot show it.

| Page | What is there |
|---|---|
| Display | The screen's size and colours, smooth text, the drawing engine |
| Input | How fast keys repeat, the pointer's speed, the double-click time; a place to try them |
| Joystick | Game pads and how their sticks and buttons become a joystick's; the pads as they move |
| USB | Whether GLOS runs the USB ports itself |
| Sound | The mixer, the volume, the card, the Sound Blaster that DOS programs find; a test sound |
| Network | The PC's name, the network card, the cable link to another PC; the address now |
| Devices | Every device GLOS finds, what serves each one, and devices to leave to DOS (below) |
| Date and Time | The time zone, the clock set from the network, or by hand |
| Region | How dates and numbers are written, the code page, the keyboard's layout |
| Disk Cache | Memory used to make disks faster |
| Memory | Upper memory |
| Remote Desktop | Whether the screen can be seen from another computer |
| Desktop | The theme, the text's size, the wallpaper, the task bar, the Recycle folder, what starts with the desktop |
| Saver | The screen saver, its wait, and when the screen is turned off |
| Services | Programs GLOS starts with itself and keeps running |
| Shell | The command line and what DOS programs get as COMMAND.COM |
| File Types | Which program opens which kind of file |
| Programs | A DOS program's own settings: see [DOS programs and games](DOSPROGS.MD) |
| Secrets | The store of passwords that programs keep, and its master password |

The Start menu finds these pages by name: type "sound" or "desktop" there.

## Devices

The Devices page lists every device of the PC that GLOS finds, by kind: display, network, sound, USB (with
what is plugged into each controller), storage, ports, the keyboard and mouse, GLOS's own devices for DOS
programs (its Sound Blaster), bridges and the PC's system devices. Choose one to read, under the list:

- its name and maker (PCI devices are named from the PCI ID Project's list, PCIIDS.DAT beside GLOS, and USB
  devices from the list of USB IDs, USBIDS.DAT);
- where it is (its PCI place, its USB port, or built into the PC);
- what serves it: **Working** (one of GLOS's drivers has it), **Turned off** (left to DOS), **Not working**
  (GLOS's driver could not use it, and why), **DOS's** (GLOS has no driver of its own for it: DOS's drivers
  and programs use it as the BIOS set it up), or **Kept quiet** (nothing drives it, and GLOS keeps its
  interrupts off so that it can share a line with a device GLOS drives);
- its interrupt, DMA channels, I/O ports and memory, its ID numbers, and what its driver says of it (a
  network card's address, a sound card's state).

**Turn off** leaves a device that one of GLOS's drivers can have to DOS: a DOS packet driver can then have the
network card, or a DOS game its own driver for the sound card. **Turn on** gives it back to GLOS. Either
writes the setting at the top of the page, **Left to DOS**; press OK or Apply, and it takes effect when GLOS
next starts. Before you turn off the network card GLOS uses, or a USB controller, you are asked: without the
card nobody can reach the PC over the network, so turn it on again at the PC itself.

The same list is the command `DEVICES` at GLOS's prompt.

The same settings are lines in the file GLOS.CFG, for when there is no desktop to change them from: see
[Settings](SETTINGS.MD).
