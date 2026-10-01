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

Design stage. [PRD.md](PRD.md) is the full requirements document: decisions, architecture, milestones. The
build is a 16-bit loader stub that proves the toolchain and the 86Box test loop. The supervisor starts at
milestone M1.

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

GLOS is meant for trusted LANs. Its SSH server accepts public keys only. Don't expose a GLOS PC to the internet.

## Licence

MIT (see [LICENSE](LICENSE)). Third-party components keep their own licences (PRD §17).
