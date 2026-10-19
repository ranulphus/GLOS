# GLOS release notes

What each milestone added. Everything here is built and tested in 86Box (Loop A, `make loopa-*`); the bench
(real PCs and Matrox cards) re-verifies before a release (PRD §19). Each milestone's plan, status and evidence is
in its `docs/milestones-*.md`.

## M10: community features (2026-10-18)

Each feature ships when its own gate passes (PRD D13). All of them have.

| Feature | Gate | Passed |
|---|---|---|
| Network time (SNTP), local time by `[time] zone` | `make loopa-time` | yes |
| HTTPS (`FETCH.EXE`, BearSSL, Mozilla's roots) | fetches from GitHub and a FreeDOS mirror with certificate checks (`make loopa-https`) | yes |
| The terminal's SSH and SFTP client | `make loopa-sshc` against OpenSSH | yes |
| Packages (`PKG.EXE`) | a signed package installs and uninstalls cleanly (`make loopa-pkg`) | yes, with the suite's test key |
| VNC through SSH | a stock VNC client views and drives the desktop through an SSH tunnel (`make loopa-vnc`) | yes (vncdotool, 8, 16 and 32 bits) |
| Capture (`glos rec`) | a capture's frames are identical to `glos shot` (`make loopa-vnc`) | yes |
| UTF-8 fonts, DOS's code pages, smoothed text | goldens with Greek and Cyrillic, names through 850 and 866, smoothing at 16 and 32 bits (`make loopa-gui`) | yes |

- **Time.** The RTC and DOS keep local time. `[time] zone` is a POSIX TZ string, which DJGPP programs get as
  `TZ`. SNTP asks `[time] server` when the network comes up, and `glos time` sets the time by hand. The Control
  Panel has a "Date and Time" page.
- **API 1.3.** Apps get TCP clients (`GLOS_FN_NET`, with BSD sockets in the SDK), random bytes and the time.
- **HTTPS.** `FETCH.EXE URL [FILE]` checks certificates against `CERTS.DAT` (121 of Mozilla's roots). It
  refuses, with the reason, until the clock has been set.
- **SSH and SFTP.** `ssh`, `sftp` and `ssh-keygen` in the terminal. The screen is xterm-compatible (vi and top
  work). Login is by Ed25519 key or password, and host keys are kept in `SSH\KNOWNHST.TXT`.
- **Packages.** `PKG` installs, upgrades and removes packages, as a command or a window. A GLOS source is signed
  with Ed25519. FreeDOS's repositories work too, unsigned and said so.
- **VNC.** Turn it on with `[vnc] enable` (the Control Panel's "Remote Desktop"), then use `ssh -L
  5900:127.0.0.1:5900`. Up to four viewers; the task bar says VNC while one is connected. `glos rec` streams PNG
  frames, and `tools/rec.py` writes them out for ffmpeg.
- **Text.** The fonts now cover Latin Extended-A, Greek, Cyrillic and common signs. DOS's names and DOS
  programs' output go through code page 437, 850, 852, 866 or 737 (GLOSCP, or what DOS says). Smoothed text is
  `[display] font_smoothing`, at 16 and 32 bits.

Still to come from the owner: the GLOS packages repository on GitHub and the offline signing key (PRD D58).
PKG's default GLOS source (`ranulphus/glos-packages`) doesn't exist yet, and GLOS ships no key for it.
FreeDOS's base and util repositories, PKG's other default sources, work now.

The regression (the host tests and 42 Loop A suites) and the gate (133 parts) pass on M10's last commit. They
found two older races, now fixed:
- `glos rec` or a VNC viewer could keep a half-drawn frame;
- an app's `GLOS_FN_WINVRAM` was refused while another program's session had the screen.

Needs MGA-Glide's 86Box patch 0118 (the PIIX4 RTC alias). Without it, the bf6 machine's clock stops after
SNTP's first change of century.

## M9: native apps and the Control Panel (2026-10-18)

The retained-mode toolkit (`sdk/gui`) and the apps, each tested by its pictures on the host and in Loop A:
- the file manager, which is also the desktop's shell;
- Notepad, the calculator and the image viewer;
- the terminal, with GLOS's shell (DOS, modernised) and console jobs;
- the monitor;
- the Control Panel, which can change every setting in GLOS.CFG (one schema for the loader, the kernel and the
  apps; `glos config`).

## M8: windowed DOS-GL, SDL3 and the mixer (2026-10-15)

- The GPU lock: an app's turn with the engine ends only at a safe point, with a watchdog.
- Apps' VRAM, and DOS-GL and SDL3 programs in windows on a 16-bit Matrox desktop.
- The SB16 driver and the desktop's mixer.
- Apps' COM1 lines in GLOS's log.

## M7: Matrox 2D (2026-10-15)

The desktop on Matrox cards' drawing engine (the shared HAL): copies, fills, colour expansion, the hardware
cursor and a VRAM manager. `[display] accel = off` turns it off.

## M6: the GUI core (2026-10-14)

- App threads and the GLOS API.
- The 2D library, the VBE display and the window manager.
- The desktop in the "Slate" look, with DejaVu fonts.

## M5: the agent completed (2026-10-11)

- `glos shot` in every mode, and input through `glos key`, `type` and `mouse`.
- `glos stat`, `log --follow`, `kill` and `reboot`.
- RTL8139 and Intel 8255x drivers, gdb through SSH, and mDNS.

## M4: the DPMI host (2026-10-04 to 2026-10-09)

- 32- and 16-bit clients, exceptions, IRQs, callbacks and DOS/4GW.
- Sessions and per-program profiles, with direct mode (IOPL 3).
- `glos run`, and the gate's baseline matrix: CWSDPMI, HDPMI, DOS/4GW, RTM and the games.

## M1 to M3: the supervisor and the agent (2026-10-02 to 2026-10-03)

- The loader and the ring-0 kernel.
- DOS in a V86 system VM, with virtual devices, XMS and INT 15h.
- Threads, NE2000, lwIP, the SSH server, SFTP, and the agent's shell and capture.

## M0: the harness (MGA-Glide)

Loop A's machines (bf6, the 486s), cards, network, COM2, boot variants and V86TEST. Plus 86Box patches for the V86
faults it found.
