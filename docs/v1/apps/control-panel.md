# Control Panel: plan (GLOS v1, phase A)

**Today:**
- **Pages from GLOS.CFG's schema (D53):** each section a page, each setting a widget by its kind.
- **Profiles and file kinds:** the programs' profiles; the kinds of file (`[open]`).
- **Applying:** Apply checks against the schema first; a new screen mode is tried for fifteen seconds.

**In v1:**
- **Category pages, as Windows' Control Panel has applets:** each made for its subject, with what goes with it
  (previews, tests, live status), not only the settings' widgets.
- **The schema still drives every setting:** a page draws its settings from it, and anything new there appears on
  the "Advanced" page until a page takes it.

## The pages

| Page | What it has |
|---|---|
| **Appearance** | The theme (colours, a few built in: Slate, light, dark, high contrast, a Windows 98-like one); window metrics (title height, border width); the desktop's wallpaper (picture, colour, centred, stretched, tiled); the UI font's size (the toolkit's sizes) and smoothing |
| **Screensaver** | The saver and its preview in a small screen; its settings (each saver's own: the starfield's speed and density, the pipes' style, the maze's walls); the wait; on resume, nothing or a password (optional) |
| **Display** | The screen mode (as today, tried for fifteen seconds); the card's details (Matrox model and memory, or VBE); the drawing engine; the GPU watchdog |
| **Sound** | The devices (SB16, Crystal and their resources, `[sound]`); the master volume and each stream's (the Mixer, `apps/mixer.md`); a test sound; the recording input and its level; MIDI (the OPL3, an MPU-401, the software synth's bank) |
| **Network** | Status (the card, its link, the address, DHCP's lease, DNS, the gateway), the traffic (as Task Manager's graph), the host name, SNTP, the SSH server's state and its keys' fingerprints; static addresses (optional, if GLOS gains them) |
| **Date, time and region** | The clock (set by hand, or from the network), the time zone (a list, not only the POSIX string), the formats (date, time, numbers, the first day of the week), the code page and the keyboard layout; opened by double-clicking the task bar's clock |
| **Mouse and keyboard** | The pointer's speed, double-click time (with a test area), the wheel's lines a notch, buttons swapped; the keys' repeat (as today) with a test field; the lock lights |
| **Game controllers** | The pads (`glos usb`'s), the game port's source and mapping (the gamepad plan's J6: which pad is joystick A and B, which buttons), a test page (axes and buttons live, as the game port reads them), the range (`[joystick] range`), calibration |
| **USB** | The controllers and the tree of devices (hubs, keyboards, mice, pads, MIDI, storage when it comes), each device's details; `[usb] enabled` |
| **Startup and default apps** | Programs and apps started with GLOS; the default app for each kind of file (today's `[open]`, as a list of kinds); the Start menu's pinned apps |
| **Programs** | The programs' profiles (today's), with a game's profile also reachable from the Games Library |
| **Memory and cache** | The disk cache (as today), upper memory's stub page (as today), the memory GLOS has and uses |
| **Remote** | VNC (as today), the SSH server, the clipboard's sharing over SSH |
| **Advanced** | Every setting in the schema not yet on another page, as today's generic pages |

- **Changes:** each page's changes are applied with Apply, as today. Settings marked "now" in the schema take effect
  at once, and the apps see them through SETTINGS' change event, so the theme changes open windows without a
  restart.
- **Search:** a field over the page list finds a setting by its label or description.
- **The keyboard:** every page fully usable without a mouse (D7).

## From toolkit 2

- **The frame:** a category list (icons and names) beside the page, and search.
- **Previews:** the screensaver's preview (a small DOS-GL or software surface in a widget); the wallpaper's and the
  theme's preview (a small desktop drawn by the toolkit).
- **Live displays:** the graph widget (network); live axes and buttons (pads); a level meter (recording); sliders
  with live values (volumes).
- **Lists:** the tree (USB) and the detail view (default apps).

## From API 2.0

- **SETTINGS:**
  - the schema;
  - read and write;
  - the change event, so that open apps follow;
  - themes as settings: colours and metrics, read by the toolkit.
- **IDLE:** the screensaver's wait and a preview run.
- **AUDIO:** volumes (master and each stream), the devices, recording levels, a test tone, MIDI's devices.
- **NET:** the network's state and figures.
- **SYS:** devices (USB, PCI, sound).
- **INPUT:** the pads' live state for the test page; the mouse and keyboard settings applied at once.
- **SHELL:** the Start menu's pins, default apps, startup entries.
- **DRAW:** the screen modes, the screensaver's preview surface (DOS-GL in a child window: DRAW and the DOS-GL
  windowed path).

## Libraries

None new.

## CPU floor

A 486DX2. The screensaver's preview uses DOS-GL's software renderer on a non-Matrox 486 at a small size.

## Tests

- **Host:** GUI scripts of every page at 8, 16 and 32 bits (today's control suite, grown); each page's settings
  against the schema.
- **Loop A smoke:** the control suite (every setting changed by keys alone, as today); the theme changed with an app
  open, its window redrawn in the new colours; the game controllers' test page with `glos joy`.
- **The P4:** the sound, network and USB pages against its real devices.
- **By hand:** volumes and MIDI with the owner.
