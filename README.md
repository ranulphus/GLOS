# GLOS
Graphics Library Operating System (for DOS)

GLOS is a graphical environment for DOS PCs, in the Windows 3.x 386 enhanced mode tradition. `GLOS.EXE`, started
from DOS, loads a 32-bit supervisor:
- DOS keeps running in a virtual-8086 VM.
- The supervisor is the DPMI host for DOS programs.
- An SSH server, a network stack and a window system run inside it.

What it is for:

- **Debugging DOS software on real and emulated PCs.**
  - `ssh`, `scp`, `sftp` and `sshfs` into the PC.
  - Run programs and get their output.
  - Take screenshots and send keys and mouse movements.
  - Follow telemetry and attach gdb, even while a full-screen game owns the machine.
- **Running DOS-GL and SDL3 programs** in windows or full-screen, several at once. The same EXE still runs on
  plain DOS.
- **Bringing modern comforts to retro PCs:** HTTPS and packages, VNC and lossless capture, network time, local
  network names, UTF-8.

Minimum: a 486 with any VESA 2.0 card. Matrox G-series cards add 2D acceleration and windowed OpenGL through
[DOS-GL](https://github.com/ranulphus/DOSGL).

## Status

Milestones M0 to M10 are built and tested in 86Box (Loop A); the bench, real PCs and real Matrox cards come
before a release. [PRD.md](PRD.md) is the full requirements document: decisions, architecture, milestones.
[docs/release-notes.md](docs/release-notes.md) says what each milestone added and which M10 features have
passed their gates.

- The supervisor: DOS in a V86 machine, a DPMI host (32- and 16-bit clients, DOS/4GW), sessions, profiles.
- The agent: SSH, SFTP, `glos shot`, `glos rec`, input, logs, gdb.
- The desktop: VBE and Matrox 2D, windows, DOS-GL and SDL3 in windows, the mixer, the apps and the Control Panel.
- M10: network time, HTTPS (`FETCH`), the terminal's SSH and SFTP client, packages (`PKG`), VNC through SSH,
  DOS's code pages and smoothed text.

## Building

Needs Open Watcom v2 and a checkout of [MGA-Glide](https://github.com/ranulphus/MGA-Glide), whose Loop A harness
runs GLOS in 86Box. Paths are set in `config.mk`; override them in `config.local.mk`.

```
make                     # build/ow/GLOS.EXE
make check-deps          # MGA-Glide at or after the commit pinned in deps.mk
make loopa CARD=g450     # boot 86Box with a G450, run GLOS.EXE: out/loopa-g450/ (status, serial.log)
```

## Sibling projects

- [MGA-Glide](https://github.com/ranulphus/MGA-Glide): Glide 2 for Matrox cards; the shared Matrox HAL and the
  Loop A/B/C test harness.
- [DOS-GL](https://github.com/ranulphus/DOSGL): OpenGL 1.1 for Matrox cards on DOS, and the SDL3 bridge.
- [Fifth Wheel](https://github.com/ranulphus/FifthWheel): a DOS-GL game and the dgk kit.

## Security

GLOS is meant for trusted LANs. Its SSH server accepts public keys only. VNC is off by default and, when on, is
reached only through an SSH tunnel. Don't expose a GLOS PC to the internet.

## Licence

MIT (see [LICENSE](LICENSE)). Third-party components keep their own licences (PRD §17).
