# Product Requirements Document — GLOS

**Graphics Library Operating System: a graphical environment for DOS on its own 32-bit supervisor**

| | |
|---|---|
| Status | v0.1 — draft (plan approved 2026-10-01) |
| Last updated | 2026-10-01 |
| Licence | MIT |
| Companion projects | MGA-Glide (`~/MGA-Glide`: the Loop A/B/C harness and the shared Matrox HAL), DOS-GL (`~/DOSGL`: OpenGL for Matrox cards and the SDL3 bridge), Fifth Wheel and the dgk kit (`~/FifthWheel`), DOSBench (`~/DOSBench`) |

> MS-DOS, Windows and OS/2 are trademarks of their respective owners. GLOS is an independent project. It contains no code from MS-DOS 5 or later, Windows or OS/2, and its developers never consult leaked Microsoft source (D26).

---

## 1. Overview & Value Proposition

GLOS turns a DOS PC into a networked, multitasking graphical system, in the way Windows 3.x did in 386 enhanced mode.

- `GLOS.EXE` is started from DOS. It loads a 32-bit ring-0 **supervisor** that takes over the CPU.
- DOS keeps running in a virtual-8086 **system VM**. It does what it does well: booting, the FAT file system and the BIOS.
- The supervisor is the **DPMI host** for DOS programs.
- A **window system**, **network stack** and **SSH server** run inside the supervisor.

GLOS has two jobs.

1. **A debugging platform for our DOS projects.** The SSH server stays alive whatever runs on the machine, including a retail game that takes the whole screen. It gives the Claude agent on the build server, or a person:
   - commands, with their stdout and stderr;
   - files;
   - screenshots;
   - remote keyboard and mouse;
   - telemetry;
   - gdb.

   The same commands work on an 86Box VM (Loop A) and a physical bench PC (Loop B). That turns Loop B from batch jobs that reboot after every run into an interactive session.
2. **A managed environment for DOS-GL and SDL3 programs.**
   - Programs run in windows or full-screen, several at once, under a preemptive scheduler.
   - The same EXE still runs full-screen on plain DOS: DOS-GL and SDL3 detect GLOS at run time.
   - Native GLOS apps (file manager, notepad, calculator, terminal, system monitor, Control Panel) use a versioned API that becomes the v2 SDK.

GLOS also brings things to retro PCs that their owners lack today:
- SSH, SFTP and sshfs access from a modern machine;
- HTTPS downloads and a package manager;
- VNC remote desktop and lossless screen capture without a capture card;
- network time, local network names, and UTF-8 text.

### What makes this credible

- **The design is proven.** WIN386, DESQview/386 and OS/2 2.x all ran DOS in V86 VMs under a 32-bit supervisor. Their techniques are documented: the DPMI 0.9 and 1.0 specifications, the Windows 3.0 DDK's *Virtual Device Adaptation Guide*, Ralf Brown's Interrupt List and the Intel manuals (Appendix C).
- **The programs GLOS must run are already built and tested.** Each suite already runs unattended in 86Box today, and becomes GLOS's acceptance gate (§14.3):
  - MGA-Glide's conformance tests and retail-game replays;
  - DOS-GL's conformance tests;
  - SDL3 on DOS-GL;
  - ClassiCube, GLQuake, Quake 2, Half-Life, PrBoom-plus and Fifth Wheel.
- **The census is done.** Appendix A lists every DPMI service, port, interrupt and BIOS call those programs use.
- **Good building blocks are MIT-compatible:**
  - TinySSH: modern SSH algorithms, public-domain-style licence;
  - lwIP: IPv4/IPv6, DHCP, mDNS, SNTP;
  - BearSSL: TLS;
  - OpenSSH's ISC-licensed `sftp-server.c`;
  - stb, FreeType and EtherDFS.
- **86Box emulates the hardware.** That covers the Matrox cards, NE2000, RTL8139 and Intel 8255x network cards, the SB16, PS/2 devices and a gameport. Its SLiRP networking can forward ports into the guest, so SSH reaches an emulated PC with no patch.
- **The bench exists in design** (`~/MGA-Glide/docs/bench.md`): serial lines, a capture device and a reset relay give an out-of-band path when GLOS itself is down.

### What makes this hard

- **Compatibility is unforgiving.**
  - A V86 monitor and DPMI host must behave closely enough to CWSDPMI and DOS/4GW's expectations that retail games and DJGPP programs produce identical frames.
  - 86Box's own CPU fault paths have had bugs here (local patch 0103).
- **DOS is not re-entrant.** Several preemptively scheduled programs must share one DOS (§6.8).
- **One graphics chip, several owners.** The HAL caches chip state (FIFO space, OPMODE, MACCESS), and that cache goes stale when another program uses the chip (§9.6).
- **A 486 is slow, and the minimum spec is a 486.**
  - There is no timestamp counter and no global pages, so every address-space switch flushes the TLB.
  - A Curve25519 or Ed25519 operation takes tens of milliseconds.
- **Licences.** Most DOS system software that could be reused is GPL, Artistic-licensed or freeware with no licence to modify: Jemm, HDPMI, CWSDPMI, the FreeDOS kernel, Watt-32, mTCP. GLOS writes its supervisor from specifications (§17).
- **Scope.** v1 includes community features (D13) as well as the core. They have their own release gates so they can't hold the core back (§19).

---

## 2. Goals & Non-Goals

### 2.1 Goals (v1)

| # | Goal |
|---|---|
| G1 | **An SSH agent that survives any program.** It provides commands with stdout/stderr and exit codes, SFTP, screenshots in any mode, keyboard and mouse injection, telemetry, gdb, killing a program, and unloading or rebooting. It works on 86Box VMs and bench PCs. |
| G2 | **Run unmodified programs under the supervisor with results identical to running them without GLOS.** This covers real-mode programs, DJGPP programs (which expect CWSDPMI) and DOS/4GW programs, including MGA-Glide inside retail games (the supervisor gate, §14.3). |
| G3 | **A desktop on any VESA 2.0 card** at 8, 16 and 32 bpp, with 2D acceleration and a hardware cursor on Matrox cards. |
| G4 | **DOS-GL and SDL3 programs** run windowed or full-screen and preemptively multitasked, from the same EXE that runs on plain DOS. |
| G5 | **GLOS API v1**, versioned and stable enough to become the v2 SDK, plus native apps: file manager, notepad, calculator, terminal, system monitor and a Control Panel with extensive configuration. |
| G6 | **Community features:** HTTPS plus a package manager; VNC remote desktop plus lossless capture; SNTP; mDNS; UTF-8; anti-aliased fonts as an option. |
| G7 | **Loop B over SSH.** Bench PCs running GLOS are driven over SSH; the BENCH.BAT poller, serial log, capture and relay stay as the fallback. |
| G8 | **Clean exits.** GLOS unloads back to a working DOS prompt, and VECCHK/VMODE confirm the machine is clean. A crashed app is killed without taking down GLOS. A supervisor panic writes its state to serial. |

### 2.2 Non-Goals (v1)

| Area | Position |
|---|---|
| DOS programs in windows | After v1 (D5). v1 runs unmodified DOS programs full-screen as exclusive sessions (§6.9). |
| Replacing DOS | GLOS runs on an existing DOS (D16); it doesn't modify or ship a patched kernel. |
| Win16/Win32 programs | Not supported. |
| EMS, VCPI and UMBs for programs | Not in v1 (D25). Running under EMM386, QEMM or JemmEx is refused. |
| Modern PCs | UEFI-only machines (no CSM), USB stacks and AHCI/NVMe drivers are out of scope. USB keyboards work only through BIOS legacy emulation in exclusive sessions. |
| CPUs below a 486 | Not supported. 486SX support is open (Q1). |
| Multi-monitor | After v1 (D15). The v1 display layer handles N screens. |
| SDK packaging | v2 (D14). The v1 API is versioned and frozen per version. |
| GLOS's own FAT driver | After v1. File access goes through DOS (§6.8). |
| 3D on other vendors' cards | After v1 (see the other-vendor study). Non-Matrox cards get the VBE desktop, and GL apps run full-screen through DOS-GL only where DOS-GL supports the card. |

### 2.3 After v1

- **v2:** the SDK (headers, import library, docs, packaging, samples).
- **Later:**
  - windowed DOS boxes (virtual VGA, PIT, keyboard, DMA, Sound Blaster);
  - G400/G450 DualHead and multi-card displays (needs CRTC2 in 86Box or bench-only tests);
  - GLOS's own 32-bit FAT driver and disk cache;
  - EMS for older games;
  - more NICs;
  - other vendors' 2D/3D drivers;
  - a TLS 1.3 client.

---

## 3. Decision Register

Decisions are recorded with their rationale so they are changed deliberately, not drifted from. "User" marks the user's own decisions (2026-10-01); the rest are proposals from the design review, open to change until the milestone named.

| ID | Decision | Rationale | Revisit at |
|---|---|---|---|
| **D1** | **GLOS has its own ring-0 supervisor from the start: a DPMI host and V86 monitor.** (User) | The agent must survive any program, including one that takes the machine. It's also the foundation for windowed DOS later. Running as a client of CWSDPMI or HDPMI would leave the agent silent under games. | M4 |
| **D2** | **The agent is a real SSH server.** (User) It is derived from TinySSH; SFTP comes from OpenSSH's `sftp-server.c`. Screenshots, input and telemetry are `glos` commands run over SSH, and gdb connects through a port forward. | `ssh`, `scp`, `sftp` and `sshfs` work from any machine with no custom client. The Claude agent already knows these tools. TinySSH has exactly the modern algorithms and a public-domain-style licence. | M3 |
| **D3** | **Minimum spec: a 486 with any VESA 2.0 card.** (User) Matrox G-series cards add 2D acceleration and windowed DOS-GL. The reference machine is a Pentium II with a Matrox card. | This reaches most retro PCs. Matrox is the accelerated showcase, through the shared HAL. | M6 |
| **D4** | **Agent first, GUI second.** (User) M1–M5 are headless. | The agent is useful to MGA-Glide, DOS-GL and Fifth Wheel immediately, and it speeds up building the GUI. | — |
| **D5** | **DOS programs in windows come after v1.** (User) v1 runs them full-screen as exclusive sessions. | Virtual VGA, PIT, DMA and SB for windowed DOS is the largest single piece of work. Exclusive sessions still keep the agent alive. | After v1 |
| **D6** | **Preemptive scheduling; the graphics chip changes owner only at swap or yield points.** (User) | No app can starve audio, the agent or other apps. Changing owner only at swaps means the HAL never has to save state in the middle of a frame (§9.6). | M8 |
| **D7** | **Our own period-styled look**, in the spirit of OS/2 Warp, Windows 3.x and System 7 without copying any of them. (User) | It avoids trade-dress problems and gives GLOS its own identity. | M6 design pass (Q5) |
| **D8** | **MIT licence.** (User) Third-party parts keep their own licences; FreeDOS ships alongside GLOS, not linked into it. | Matches the companion projects. | — |
| **D9** | **NICs:** native 32-bit drivers in the supervisor for Intel 8255x (e100) and RTL8139, plus NE2000 for 86Box. (User) | DOS packet drivers can't be relied on while a program owns the system VM, and calling them costs two mode switches per packet. 86Box emulates all three. | M5 |
| **D10** | **GLOS's agent replaces Loop B's BENCH.BAT poller** when GLOS is up. The poller, serial log, Epiphan capture and reset relay stay as the fallback. (User) | An interactive session replaces fetch, run, upload and reboot. The out-of-band path still covers boot and supervisor hangs. | M5 |
| **D11** | **Desktop colour depths:** 16 and 32 bpp, plus 8 bpp with a fixed palette. (User) | 1 MB cards on 486s can run 800×600 or 1024×768 at 8 bpp. | M6 |
| **D12** | **One EXE.** (User) A DOS-GL or SDL3 program is a normal DJGPP DPMI program in its own address space. DOS-GL and SDL3's DOS backend detect GLOS at run time and open a window. | No separate builds. Fifth Wheel needs no changes, which proves it. Separate address spaces contain crashes. | M8 |
| **D13** | **v1 includes:** HTTPS (BearSSL) plus a package manager; VNC plus capture; SNTP, mDNS, UTF-8; optional anti-aliased fonts; a Control Panel with extensive configuration. (User) | Real value for the retro community. Each feature has its own release gate (§19). | M10 |
| **D14** | **The SDK ships in v2, but the v1 API is SDK-ready:** versioned, append-only tables and structures with size fields, public headers. (User) | Developers outside the project can rely on it later without a break. | M6 |
| **D15** | **Multi-monitor comes after v1.** The v1 display layer is written for N screens. (Raised by the user) | 86Box doesn't emulate the G400/G450's second CRTC, so DualHead can only be verified on the bench. | After v1 |
| **D16** | **GLOS runs on an existing DOS.** The reference is FreeDOS 1.4 (Loop A); MS-DOS 6.22 and 7.1 are supported (bench). DOS is not patched. | Like Windows 3.x. DOS's instance-data hooks vary widely, so GLOS relies on serialisation instead (§6.8). | M2 |
| **D17** | **The loader has a raw mode** (E820/E801/88h memory, its own A20 control) **and an XMS mode** (locked blocks). It refuses to start under a V86 monitor or another DPMI host. | Loop A's FreeDOS boot has no HIMEM and must stay byte-identical; the bench uses HIMEM.SYS. | M1 |
| **D18** | **Toolchains:** the supervisor with host gcc `-m32 -march=i486 -ffreestanding` and GNU as (ELF, converted to a flat image); the loader and 16-bit tools with Open Watcom; GLOS apps and libraries with DJGPP (gcc 12.2). No NASM. | Every one of these is already in the dev container; nothing new to pin. | M1 |
| **D19** | **A monolithic ring-0 supervisor** holds memory, the scheduler, the V86 monitor, the DPMI host, the DOS server, virtual devices, drivers, lwIP, SSH, the window system and the API. HTTPS, the package manager, the Control Panel and font rasterisation are ring-3 GLOS apps. | One image is simpler to bring up and debug. Keeping the large parsers out of ring 0 limits the damage they can do. | M6 |
| **D20** | **IOPL=0 everywhere, with a virtual interrupt flag; VME/PVI where the CPU has them.** HLT is treated as "yield until the next virtual interrupt". | With IOPL=3, `cli; jmp $` would freeze the agent. 86Box offers both 486 profiles without VME and Pentium-class profiles with it, so both paths get tested. | M2 |
| **D21** | **The scheduler tick is the RTC periodic interrupt** (IRQ8, 1024 Hz). The RTC is virtualised per VM from the start; INT 15h 86h becomes a sleep; each app gets its own virtual PIT. | Programs reprogram PIT channel 0 (DJGPP's `uclock` does on first use) far more often than they touch the RTC. A 486 has no TSC, so the tick count is the supervisor's clock. | M2 |
| **D22** | **The DOS server:** one lock covers the whole system VM (every real-mode call, every BIOS and VBE call). Per-app DOS state is swapped on each call. Agent file operations wait until DOS is idle. | DOS isn't re-entrant and its per-process state is global (§6.8). | M6 |
| **D23** | **GLOS is detected through DPMI function 0A00h with the vendor string "GLOS"**, which returns a versioned entry point. GLOS never answers INT 2Fh 1600h and never broadcasts 1605h. | This is the standard DPMI vendor-extension mechanism. Claiming to be Windows would make TSRs and programs expect Windows behaviour. | M6 |
| **D24** | **GPU ownership is enforced by page faults.** A program's Matrox MMIO pages stay unmapped until it holds the GPU lock. The HAL gains `engine_resync()` for every change of owner, and a watchdog resets the engine and kills an app that holds the lock too long. | Every owner change is visible to the supervisor, and stale HAL state can't overflow the FIFO. | M8 |
| **D25** | **No EMS, VCPI or UMBs in v1.** GLOS provides its own XMS 3.0 server, and the first MB is identity-mapped in every address space. | Matches the HIMEM-only baselines. SDL's Sound Blaster driver assumes linear = physical below 1 MB for its DMA buffer. | After v1 |
| **D26** | **Provenance: write from specifications.** Code under GPL, Artistic or no licence (Jemm, HDPMI, CWSDPMI, DPMIONE, 386MAX, the FreeDOS kernel, Watt-32, mTCP) is reference and test baseline only, never copied. Leaked Microsoft source is never consulted. | An MIT licence that holds up (§17). | — |
| **D27** | **Changes to the shared HAL and harness go into MGA-Glide first, with its full regression.** GLOS pins MGA-Glide in `deps.mk` and vendors the HAL with `sync-hal` once the kernel needs it. | The project-wide rule that DOS-GL and DOSBench already follow. | — |
| **D28** | **Performance budgets come from silicon and CPU models, not 86Box speed.** | 86Box on the build host runs far below real hardware, and the emulated Matrox FIFO thread runs in host time. | — |
| **D29** | **SSH accepts public keys only.** Host keys are generated on first boot from IRQ-timing entropy; Loop A images carry a fixed test key; VNC is off by default and meant to be tunnelled over SSH. | Remote code execution must be authenticated. VNC's own authentication is weak. | M3 |

---

## 4. Target Hardware & Environment

### 4.1 CPUs

| Class | Minimum / reference | Features GLOS uses when present |
|---|---|---|
| 486DX, DX2, DX4 | Minimum (FPU needed for Matrox/HAL paths and DJGPP programs without emulation; 486SX open, Q1) | VME/PVI on some later 486s (CR4) |
| Pentium, Pentium MMX | Supported | TSC (a better clock), PSE, VME/PVI |
| Pentium Pro, Pentium II and later | Reference (bench PCs, Loop A's Pentium II 350) | PGE (global kernel pages), MTRR write-combining for framebuffers, FXSR (Q2) |

### 4.2 Memory

| Configuration | Target RAM (to be measured, §13) |
|---|---|
| Headless supervisor + network + SSH | 8 MB |
| Desktop and native apps | 16 MB |
| Windowed DOS-GL / SDL3 apps | 32 MB or more |
| Loop A default / Half-Life runs | 64 MB / 128 MB (`--mem`) |

### 4.3 Video

- **Generic:** any VESA 2.0 card with a linear framebuffer at 8, 16 or 32 bpp. A 1 MB card can run 1024×768×8, 800×600×8 or 640×480×16.
- **Matrox:** G100, G200, G400, G450 through the shared HAL, with mode planning, 2D acceleration, a hardware cursor and display start set through the CRTC. Windowed DOS-GL works on cards DOS-GL supports (G200 and later).

### 4.4 Network

| NIC | Driver | 86Box device |
|---|---|---|
| NE2000 (ISA, PCI) | GLOS | `ne2k`, `ne2kpci` (Loop A default) |
| Realtek RTL8139 | GLOS | `rtl8139c+` |
| Intel 8255x (EtherExpress Pro/100) | GLOS | `i82557`…`i82559er` |

GLOS refuses a NIC whose IRQ is shared with another active device, and a NIC already claimed by a loaded packet driver (§8.1).

### 4.5 Sound, input and storage

- **Sound:** SB16 (auto-init DMA) for the GLOS mixer. In exclusive sessions any card is passed through.
- **Input:** PS/2 keyboard and mouse through a full virtual 8042; gameport passed through. Serial mice come after v1.
- **Storage:** through DOS and the BIOS (INT 13h).

### 4.6 DOS

| DOS | Where | Notes |
|---|---|---|
| FreeDOS 1.4 (kernel 2043), no HIMEM | Loop A's golden boot floppy | Raw mode (D17). This boot must stay byte-identical. |
| FreeDOS + HIMEMX | Loop A variant (`--boot-cfg himemx`, M0) | XMS mode |
| MS-DOS 7.1 (FAT32) or 6.22, HIMEM.SYS only | Bench PCs (`docs/bench.md`) | XMS mode; packet driver unloaded or absent when GLOS owns the NIC |

### 4.7 Machines

- **Loop A:** ABIT BF6 (i440BX), Pentium II 350, Matrox card, COM1 to `serial.log`, the 86Box unit tester for exit codes. M0 adds a 486 profile and a generic VBE card (Q9).
- **Bench:** the PCs in `tools/bench/bench.toml` (G100, G200, G400, G450), with Intel 8255x or RTL8139 NICs (D9). A 486 bench PC is open (Q10).

---

## 5. Architecture

### 5.1 Layers

```
 ring 3   GLOS apps: DJGPP DPMI programs,           | system VM (virtual 8086):
          each in its own address space             |   DOS, BIOS, TSRs, COMMAND.COM,
          (DOS-GL/SDL3 apps, native apps,           |   real-mode parts of programs,
           Control Panel, pkg, HTTPS tools)         |   exclusive sessions
 ---------------------------------------------------+-------------------------------------
 ring 0   supervisor
          scheduler (RTC tick) . memory manager . V86 monitor . DPMI host . XMS server
          DOS server (lock, per-app state) . virtual devices (PIC, RTC, PIT, 8042, A20, CMOS)
          drivers: NIC, SB16 mixer, PS/2, display (VBE + Matrox HAL)
          lwIP . SSH agent . window system . GLOS API . gdb stub
```

### 5.2 Start and exit

1. `GLOS.EXE`, a 16-bit DOS program, checks for a 486 or better, memory (raw or XMS), the absence of a V86 monitor or DPMI host, and its own configuration.
2. It loads the kernel image into extended memory, records the real-mode interrupt table and the BIOS data area, then enters protected mode with paging.
3. The supervisor turns the interrupted real-mode state into the **system VM**. DOS resumes in V86 mode just after the point where `GLOS.EXE` started.
4. The system VM runs the start-up command from `GLOS.CFG` (the shell, the desktop, or a headless agent).
5. On exit, which can be requested over SSH: apps stop, hardware is restored (text mode, PIC masks, the RTC, the PIT), the supervisor leaves protected mode and `GLOS.EXE` returns to DOS. VECCHK and VMODE then confirm the machine is clean.

### 5.3 Address spaces

- The **kernel** lives at the top of the linear address space, in supervisor-only pages, mapped in every address space. Global pages are used where the CPU has PGE.
- Each **GLOS app** has its own page directory. Its DPMI memory blocks and physical mappings (0800h) go in its user region.
- The **first MB** is identity-mapped everywhere (D25).
- A DJGPP program's "fat DS" (4 GB limit, near pointers formed by wrapping around its base address) can only reach pages that are mapped user-accessible. That keeps the protection intact even with near pointers.

### 5.4 Processes

| Kind | Runs as | Scheduled | Hardware |
|---|---|---|---|
| Supervisor tasks (network, SSH, mixer, window manager) | Ring 0 | Preemptively, highest priority after interrupts | Owns its devices |
| GLOS apps | Ring 3 DPMI clients, own address space | Preemptively | Only through the GLOS API, plus GPU MMIO while holding the lock |
| System VM | V86 | Preemptively, but anything inside DOS holds the DOS lock | Virtualised |
| Exclusive session | A DOS program, or a DPMI program without the GLOS path, in the system VM | GLOS apps paused; supervisor tasks continue | Pass-through, except the always-trapped devices (§6.4) |

### 5.5 Interrupts

The supervisor owns the IDT and the physical PIC.
- Hardware IRQs go to supervisor drivers (RTC, NIC, PS/2, SB16 when the mixer owns it) or to the virtual PIC of the owning VM or app.
- Exceptions in ring 3 go to the DPMI client's handlers (0202h/0203h), then to DJGPP's signal machinery, and finally to the supervisor's crash handling (§12).
- Software interrupts from V86 code are reflected through the system VM's real-mode vector table, except those GLOS emulates: INT 15h 86h/87h/88h/E801h/E820h, INT 2Fh 43h (XMS), 1680h, 1687h.

### 5.6 The GLOS API entry

A program calls DPMI 0A00h with "GLOS" (D23). The result is a far entry point plus API version and capability bits. Plain DOS has no GLOS vendor extension, so a program that gets no answer runs exactly as it does today.

---

## 6. Supervisor

### 6.1 Loader

- Built with Open Watcom, 16-bit small model.
- Raw mode uses INT 15h E820h, then E801h, then 88h for memory; it enables A20 itself (port 92h, then the 8042).
- XMS mode allocates and locks blocks (XMS 0Ch) to learn their physical addresses, using the XMS 3.0 calls 88h and 89h above 64 MB.
- It refuses to start when a V86 monitor or DPMI host is present: V86 mode detected via SMSW or VCPI, or INT 2Fh 1687h answering.
- It writes `GLOS-` lines on COM1 at each step, so Loop A can see where it stopped.

### 6.2 Memory

- A physical page allocator, fed from the raw memory map or the XMS blocks.
- Per-process page directories.
- DPMI linear blocks (0501h–0503h) that never move when resized (0503h), so a fat-DS base stays valid.
- Physical mappings (0800h/0801h) in the caller's address space.
- Page locking (0600h/0601h) is accepted and recorded; GLOS doesn't page to disk in v1.
- DOS memory (0100h–0102h) is allocated in the system VM under the DOS lock.

### 6.3 CPU modes and protection

- **IOPL and the interrupt flag:**
  - The system VM and DPMI clients run at IOPL=0. CLI, STI, PUSHF, POPF, INT and IRET trap and are emulated against a virtual IF, or handled by VME/PVI where the CPU has them.
  - Known hazard: POPF in ring 3 at IOPL=0 silently ignores IF. Code that relies on it (SDL's mutexes use CLI/STI) is handled through the trapped instructions and the virtual-IF bookkeeping.
- **HLT** from V86 or ring 3 raises #GP; the supervisor treats it as "block until the next virtual interrupt". SDL's scheduler idles with HLT (`SDL_dos_scheduler.c:246`).
- **I/O permission:** a per-VM and per-app I/O bitmap decides which ports are trapped and which pass through (§6.4).

### 6.4 Virtual devices

| Device | Ports | v1 behaviour |
|---|---|---|
| PIC (8259 pair) | 20h/21h, A0h/A1h | Always trapped. A virtual PIC for each VM and app; GLOS owns the physical one. |
| RTC/CMOS | 70h/71h | Always trapped. Virtual index and registers A/B/C per VM; GLOS owns the periodic interrupt (D21). |
| PIT | 40h–43h | A virtual PIT per GLOS app (DJGPP's `uclock` programs channel 0). The system VM's PIT, and exclusive sessions, are passed through (§6.5). |
| 8042 keyboard controller, PS/2 mouse | 60h/64h | Always trapped, as a full virtual 8042: command/ACK, LEDs, command D1h (A20), the AUX stream. This enables input injection and Ctrl-Alt-Del interception. |
| A20 gate | 92h, the 8042's D1h | Always trapped. A20 stays physically on; the 1 MB wrap is emulated with page mappings. |
| PCI configuration | CF8h–CFFh | Always trapped, so the supervisor's own accesses never collide with a program's. Reads and writes are passed through, except to the BARs of devices GLOS owns. |
| ELCR, reset control | 4D0h/4D1h, CF9h | Always trapped. A reset request goes to the agent. |
| NIC owned by GLOS | Its I/O range | Always trapped; programs see no device. |
| VGA, VBE, Matrox | 3B0h–3DFh, MMIO | Passed through in exclusive sessions. For GLOS apps, only through the API, plus the GPU lock (§9.6). |
| Sound Blaster | 2x0h, DMA 0–7, its IRQ | Passed through in exclusive sessions; owned by the mixer otherwise. |
| Gameport | 201h | Always passed through: trapping it breaks axis timing. Loop A injects the joystick through 86Box. |
| COM1 | 3F8h–3FFh | Passed through. Writes are mirrored into agent telemetry (§7.7). |

### 6.5 Time

- **Tick:** the RTC periodic interrupt at 1024 Hz drives scheduling, timeouts and the supervisor clock.
- **Clock:** a Pentium uses the TSC to interpolate between ticks. A 486 uses the tick count alone; the supervisor never latches PIT channel 0 behind a program's back, because that would break its two-byte reads.
- **Virtual PIT:** each GLOS app's channel 0 runs from the tick, so virtual IRQ0 rates are rounded to 1 kHz. Programs that need faster rates (speaker sampling, Covox) run as exclusive sessions.
- **INT 15h 86h** (BIOS wait) is emulated as a sleep, because AT BIOSes implement it with the RTC periodic interrupt and would switch GLOS's tick off.
- **DOS time** is re-synchronised from the RTC after each exclusive session, and from SNTP when the network is up (§11.3).

### 6.6 DPMI host

- **Coverage:** all of DPMI 0.9, plus the DPMI 1.0 functions real programs use. Appendix A lists the calls our programs make; C runtime start-up code and DOS/4GW use more (for example 0303h mouse callbacks, and 0E00h/0E01h and 0507h, which need confirming). Unimplemented calls are logged with the caller's address and fail cleanly.
- **Clients:** 32-bit clients are required; 16-bit clients are open (they matter only for old Windows-era DOS tools).
- **Detection:** 1687h and the mode-switch entry follow the specification. 0A00h "GLOS" returns the API entry (D23).
- **Exceptions:** 0202h/0203h and 0210h–0213h; the first-chance and last-chance handling DJGPP and DOS/4GW expect.
- **Interrupts:** 0200h–0205h; hardware IRQs are delivered to client handlers through the virtual PIC.

### 6.7 XMS and INT 15h

- GLOS is the XMS 3.0 server for the system VM (INT 2Fh 4300h/4310h).
- **INT 15h 87h** (block move) is emulated. **88h, E801h and E820h report no free extended memory** (the supervisor owns it), so programs use XMS or DPMI.
- With an existing HIMEM, GLOS takes over its handles. Its own blocks were locked at load, and other handles are reported as in use.

### 6.8 DOS server

- **One lock covers the whole system VM.** Every reflected INT 21h call, every DPMI 0300h/0301h real-mode call and every BIOS call (INT 10h, 13h, 16h, 1Ah, VBE) takes the lock. DOS-GL's per-flip VBE 4F07h call (`vbe.c:145`) is therefore replaced by a CRTC display start under GLOS (Appendix B).
- **Per-app DOS state is swapped in and out around each call.** That state is:
  - the current PSP (50h/51h) and DTA (1Ah/2Fh);
  - the current drive and directory, because DOS's current-directory table is global;
  - extended error information (59h);
  - the INT 22h, 23h and 24h vectors;
  - the break flag;
  - the open-file table entries that belong to the app.
- **Per-app input.** INT 16h, INT 33h and port 60h are emulated for each GLOS app from its own event queue. SDL's keyboard interrupt handler (`SDL_dosevents.c`) and INT 33h polling then work unchanged inside a window.
- **Launching.** Each launch has its own INT 22h return point, so app A ending can't return through a stack frame saved by app B's later EXEC. Whether GLOS loads DJGPP images itself instead of using DOS EXEC is open (Q8).
- **When DOS is idle.** The agent's file operations run only when all of these hold:
  - InDOS is 0 and the critical-error flag is clear;
  - no INT 13h or INT 10h call is in progress (GLOS hooks both and keeps a busy flag);
  - the virtual PIC has no interrupt in service;
  - the VM is at an instruction boundary with virtual interrupts on.

  Those operations call DOS through the INT 21h entry captured when GLOS loaded, not through hooks a program installed later.
- **Critical errors.** INT 24h raised for a GLOS app becomes a GLOS dialog, or a "fail" for the agent. Ctrl-Break and INT 23h go to the foreground app only.
- **Configuration:** `FILES` of 40 or more is recommended; the loader warns below that.

### 6.9 Exclusive sessions

- **When:** an unmodified DOS program, a DPMI program without the GLOS path, or a GLOS app asking for exclusive full-screen.
- **What GLOS does:** pauses GLOS apps, saves the display state, and passes video, sound and input-port ownership to the session (except the always-trapped devices).
- **What keeps running:** the supervisor's tasks, on the RTC tick: network, SSH, screenshots, telemetry, kill.
- **On exit:**
  - restore the display mode and palette, PIC masks, the PIT, the RTC and the keyboard LEDs;
  - stop any Sound Blaster DMA (as SDL patch 0003 does);
  - restore GLOS apps.
- **Kill:** an agent request or a hotkey ends the session's process tree. Resources are recovered the same way as on exit.

### 6.10 FPU

- Lazy switching: CR0.TS is set on a task switch, and the first FPU use saves the previous owner's state.
- CR0.NE=1, with IRQ13 synthesised for V86 code that expects the old error interrupt.
- Kernel code that uses the FPU (HAL paths such as `engine_present`) runs inside explicit FPU sections.
- On Pentium II and later, whether to enable SSE state (OSFXSR) is open (Q2).

### 6.11 Keys and events GLOS reserves

| Event | What happens |
|---|---|
| Ctrl-Alt-Del | Never reaches the BIOS. Opens the GLOS task list, or is reported to the agent when headless. |
| Ctrl-Alt-Shift-Esc | Kills the foreground exclusive session or app. |
| Reset requested through CF9h or the 8042 | Reported to the agent. GLOS performs a controlled reboot. |

---

## 7. Debug Agent

### 7.1 Access

- A PC runs one SSH server on port 22. Loop A forwards it through SLiRP (`[SLiRP Port Forwarding #1]`, host port chosen per VM); bench PCs are on the LAN, with mDNS names (`glos-g450.local`).
- Users: `glos` (full access) only in v1, with keys from `C:\GLOS\KEYS\AUTHKEYS` (D29).

### 7.2 Commands

Each command is an SSH exec request. `ssh <pc> <command>` runs a DOS command line. Built-in commands start with `glos`.

| Command | Effect |
|---|---|
| `<DOS command line>` | Runs the program as an exclusive session or a GLOS app (detected; overridable with `glos run --exclusive`/`--app`). Streams stdout and stderr; returns the exit code. |
| `glos ps`, `glos kill <id>` | List processes, sessions and supervisor tasks; kill one. |
| `glos shot [--screen N] [--raw]` | A PNG of the visible screen on stdout, in any mode. |
| `glos key`, `glos type`, `glos mouse` | Inject keyboard and mouse input. Same syntax as Loop A's `--keys`, so scripts move between Loop A and the bench. |
| `glos stat [--follow]` | JSON lines: CPU per process, memory, IRQ counts, DOS-lock and GPU-lock wait and hold times, network counters, plus DGL-/MGL- statistics. |
| `glos log [--follow]` | The COM1 mirror (HX-, DGL-, MGL- and GLOS- lines) and the supervisor log. |
| `glos gdb <pid>` | Starts the gdb stub for a process; reached with `ssh -L`. |
| `glos config get/set`, `glos net`, `glos mem` | Configuration, network state, memory map. |
| `glos exit`, `glos reboot` | Unload GLOS back to DOS; controlled reboot. |

### 7.3 Files

- SFTP is a port of OpenSSH's `sftp-server.c` onto the DOS server. That gives `scp` and `sftp`, and `sshfs` mounts on the build server.
- Paths: `/C/TEST/FW.EXE` maps to `C:\TEST\FW.EXE`. Long names are refused in v1, because DOS gives no long-name API without LFN support.

### 7.4 Program output

- GLOS captures the output of the program it launched by watching INT 29h and INT 21h functions 02h, 06h, 09h and 40h, for handles that refer to the console (checked through the open-file table, not just handle numbers 1 and 2).
- Output written straight to video memory is visible only in screenshots.

### 7.5 Screenshots

- Text, VGA, VBE LFB and Matrox modes, including during exclusive sessions.
- The supervisor saves and restores the VGA index registers (3C4h, 3CEh, 3D4h and CF8h are readable), keeps a shadow copy of the DAC (the 3C9h step counter can't be read back), and saves the VGA latches for planar modes.
- PNG encoding uses stb_image_write.

### 7.6 Input injection

- Keyboard and mouse bytes go through the virtual 8042, with the keyboard interrupt (IRQ1) or mouse interrupt (IRQ12) raised for the target.
- In a GLOS app, events go straight into the app's queue.
- The gameport isn't injectable on silicon (§6.4).

### 7.7 Telemetry

- **The COM1 mirror:** bytes written to 3F8h still reach the UART, and are also split into lines for `glos log`. The harness protocol (HX-/DGL-/MGL-) is unchanged and simply becomes visible over SSH.
- **No heartbeat on COM1**, ever. Loop A declares a hang when the serial line goes quiet; heartbeats go over SSH.

### 7.8 Debugging

| Target | Transport | Milestone |
|---|---|---|
| Supervisor | gdb remote stub on COM2 (Loop A routes COM2 to a host pty or socket, M0) | M1 |
| A GLOS app or DPMI program | gdb stub per process, reached through an SSH `direct-tcpip` forward (written by us; TinySSH has no forwarding) | M5 |
| Crashes | A report with registers, stack, faulting instruction and the module map, written to `C:\GLOS\CRASH\` and `glos log`. The build host symbolises it with `addr2line` against DJGPP's COFF output. | M4 |

### 7.9 Out-of-band fallback

When SSH is down (boot, supervisor panic), the host tools fall back to the serial log, the Epiphan capture and the reset relay, as Loop B does today (D10).

### 7.10 Host integration (in MGA-Glide, D27)

- **Loop A:**
  - `--net` adds a NIC on SLiRP with port forwarding (M0);
  - a job can wait for the SSH server and run SSH commands as steps (M3).
- **Loop B:** `tools/bench/run.py` uses SSH when GLOS is up and BENCH.BAT otherwise (M5).

### 7.11 Security

- **Algorithms:** curve25519-sha256 key exchange, ssh-ed25519 host keys, chacha20-poly1305 encryption. sntrup761 is left out: it costs too much on a 486.
- **Cost on a 486:** Curve25519 and Ed25519 operations run in a preemptible supervisor task, so a handshake never stalls interrupts or the DOS session.
- **Entropy:** gathered from IRQ timing and saved as a seed file. Host keys are made on first boot; Loop A images ship a fixed, publicly known test key.

---

## 8. Networking & Security

### 8.1 NIC drivers

- NE2000 (ISA PIO, PCI), RTL8139 (PCI, bus-master receive ring), Intel 8255x (PCI, command and receive frame lists).
- Interrupt-driven, with a polled fallback for diagnosis.
- **Refused:** a NIC whose IRQ is shared with an active device, and a NIC already owned by a packet driver. The packet driver must be unloaded, or the NIC excluded in `GLOS.CFG`.

### 8.2 lwIP

- lwIP 2.2 in the supervisor, as one task, using the raw API internally.
- Features: IPv4 and IPv6, DHCP, DNS, an mDNS responder, an SNTP client, TCP sized for 8 MB machines.
- Ring-3 apps get BSD-style sockets through the GLOS API.

### 8.3 SSH server

- Built from TinySSH's protocol and cryptography code, reworked from one forked process per connection into a single event-driven supervisor task.
- Channels: `session` (exec, shell, the `sftp` subsystem) and `direct-tcpip` (ours).
- Several concurrent connections.

### 8.4 TLS

- BearSSL (TLS 1.2) client, for ring-3 apps only.
- A CA bundle maintained with the package manager.
- The clock must be set by SNTP before certificates are checked (§11).
- A TLS 1.3 client comes after v1.

### 8.5 Services exposed

| Service | Default |
|---|---|
| SSH | On |
| mDNS | On |
| VNC | Off; meant to be tunnelled over SSH, and a password is required when bound to the LAN |

GLOS is meant for trusted LANs. The README says not to expose it to the internet.

---

## 9. Graphics & Window System

### 9.1 Display drivers

| Driver | What it provides |
|---|---|
| VBE 2.0 (generic) | Mode list, linear framebuffer, 8/16/32 bpp, software 2D |
| Matrox (HAL) | HAL mode planner (native LCD modes where the BIOS has them), display start through the CRTC, 2D acceleration (blits, fills, mono expansion for text, image loads), hardware cursor |

The display layer handles N screens (D15); v1 drives one.

### 9.2 Colour

- **16 and 32 bpp:** direct colour.
- **8 bpp:** a fixed palette (a colour cube plus system colours and greys), dithered for images. With a shared, fixed palette, apps can't fight over the palette.
- **Windowed GL** needs a 16 or 32 bpp desktop. On an 8 bpp desktop, GL apps run full-screen (§9.7).

### 9.3 2D library

- Rectangles, lines, blits, clip regions, images and text.
- A software renderer for every depth, with Matrox acceleration behind the same interface. Accelerated output must match the software output pixel for pixel (M7 exit).
- **Text:** bitmap fonts by default. Anti-aliased TrueType rendering (stb_truetype, or FreeType) is optional and runs in ring 3 (D19).
- **Dirty rectangles** are tracked per screen. They drive redraws, VNC and capture.

### 9.4 Window manager

- Overlapping windows, z-order, clipping regions, decorations, focus, moving and resizing, and Alt-Tab between windows and full-screen apps.
- A desktop with icons, and a task list.
- Our own period design (D7, Q5).

### 9.5 VRAM

The supervisor owns VRAM allocation. Example for an 8 MB G200 at 1024×768×16:

| Use | Size |
|---|---|
| Desktop front buffer | 1.5 MB |
| Cursor and glyph cache | 0.25 MB |
| Two 640×480 GL windows (colour + Z each) | 2.4 MB |
| Left for textures and off-screen windows | about 3.8 MB |

DOS-GL asks GLOS for its buffers and texture heap through the API; it no longer lays out VRAM itself (Appendix B).

### 9.6 GPU ownership

- **Lock.** A program takes the GPU lock with its first access to the chip after a swap, and releases it at its next swap or yield.
- **Enforcement.** The Matrox MMIO pages are unmapped from every app that doesn't hold the lock. The first access faults, the supervisor hands over the lock, maps the pages and resumes the program (D24). The desktop's own 2D drawing takes the same lock.
- **Change of owner.** Each new owner gets `engine_resync()`: re-read FIFO status and drop cached OPMODE, MACCESS and DMA-window state (`fifo.c` keeps `fifo_free` in a static). Then `engine_restore` puts back the owner's own target and clip.
- **Watchdog.** If a lock is held past the limit (configurable, default 500 ms), GLOS resets the engine (`engine_reset`) and kills the app.
- **Cursor.** The hardware cursor keeps the pointer moving while a long GL frame holds the lock.

### 9.7 Windowed DOS-GL

- Each GL window gets its own colour and Z buffers in VRAM.
- `dglSwapBuffers` becomes a clipped `engine_present` (or a blit, when no scaling is needed) of the back buffer into the window's visible rectangles.
- Full-screen GL apps flip pages, setting the display start through the CRTC.
- A GL app's address space maps only its own VRAM, never the desktop's front buffer.
- Whether resizing a window reallocates the buffers or scales them is open (Q15).

### 9.8 Capture and VNC hooks

The display layer can export frames on request (`glos shot`, recording) and changed rectangles (VNC) without stopping the scheduler.

---

## 10. GLOS API & Apps

### 10.1 Detection and versioning

- DPMI 0A00h "GLOS" returns an entry point, the API version (major.minor) and capability bits.
- Functions are called by number through the entry point, from a function table that is only ever appended to.
- Every structure starts with its own size.
- A major version change is a new entry point; the old one stays.
- Public headers live in `include/glos/` from M6 on (D14).

### 10.2 What the API covers

| Area | Covers |
|---|---|
| Processes and threads | Start, end, wait, threads, priorities, sleep, yield |
| Windows and events | Create, show, move, resize, full-screen, focus; keyboard, mouse, joystick and window events |
| 2D | The §9.3 library on window surfaces |
| GL surfaces | Bind DOS-GL to a window: VRAM buffers, the GPU lock, present |
| Audio | Streams into the mixer: format, rate, volume |
| Files | Through the DOS server, with GLOS paths and change notification |
| Network | Sockets; TLS (ring-3 library) |
| Other | Clipboard, notifications, configuration, telemetry counters |

### 10.3 DOS-GL

DOS-GL gains a GLOS path, chosen at `dglInit` when 0A00h answers:
- buffers and texture heap come from GLOS;
- no VBE calls;
- swap becomes present through GLOS;
- the GPU lock is taken around each frame;
- `dglSetWaitHook` keeps yielding while draining the FIFO.

On plain DOS nothing changes.

### 10.4 SDL3

SDL patches 0007 onward (in `~/DOSGL/tools/sdl/patches`) add a GLOS backend to SDL's DOS port:
- **video:** windows; OpenGL through DOS-GL's GLOS path;
- **audio:** GLOS mixer streams;
- **input:** GLOS events.

Each app keeps SDL's cooperative threads in v1, and GLOS preempts between apps. Whether SDL threads should map onto GLOS threads is open (Q12).

### 10.5 Audio mixer

- The SB16 runs in auto-init DMA, and the mixer refills from the Sound Blaster interrupt. Refilling never depends on when apps yield.
- Apps queue samples into their streams. The mixer converts rates and applies per-app volumes.
- Exclusive sessions get the card directly.

### 10.6 Toolkit and native apps

- **Toolkit:** a widget toolkit on the 2D library, covering buttons, lists, menus, text editing, scrolling, dialogs and a file dialog. Whether it's our own retained-mode toolkit or built on microui or Nuklear is decided in M6 (Q11).
- **v1 native apps:**
  - file manager;
  - notepad;
  - calculator;
  - terminal (GLOS's shell, plus an SSH client);
  - system monitor (the `glos stat` data);
  - image viewer;
  - package manager;
  - Control Panel.

### 10.7 Control Panel and configuration

- All settings live in `C:\GLOS\GLOS.CFG`, an INI-style text file, readable and editable by hand, over SSH (`glos config`), and from the Control Panel.
- Every setting has a default, a description and a validation rule. The Control Panel is generated from that schema.
- Sections:

| Section | Settings |
|---|---|
| Display | Mode, depth, refresh, DPI, font smoothing (off by default), theme and colours |
| Input | Key repeat, mouse speed, joystick calibration |
| Sound | Mixer, per-app volume, card resources |
| Network | NIC, IP or DHCP, host name, mDNS, SNTP server, SSH keys, VNC |
| Date and time | Time zone, SNTP |
| Programs | Per-program settings for exclusive sessions: environment, memory, start-up commands |
| Packages | Package sources |
| Start-up | Start-up behaviour |
| Agent | Telemetry options |

---

## 11. Community Features (D13; each has its own release gate)

### 11.1 HTTPS and packages

- An HTTPS client library (BearSSL) and a package manager. Packages are zip files with a manifest and an Ed25519 signature.
- Sources: the GLOS repository and the FreeDOS repositories over HTTPS.
- The package format and hosting are open (Q4).

### 11.2 VNC and capture

- An RFB 3.8 server driven by the display layer's changed rectangles.
- Lossless recording of the screen to the build server through the agent (`glos rec`). Encodings and the recording format are open (Q13).

### 11.3 Small comforts

| Feature | Detail |
|---|---|
| SNTP | Sets DOS time and the RTC |
| mDNS | Host names on the LAN |
| UTF-8 | Text in apps and the toolkit, with fonts covering Latin, Greek and Cyrillic. DOS file names are mapped through the active codepage. |
| Font smoothing | Optional anti-aliased fonts |

### 11.4 Native resolutions

On Matrox cards the desktop uses the HAL mode planner, so LCD monitors get their native mode where the BIOS has it. Direct CRTC/PLL programming for widescreen modes comes after v1, shared with DOS-GL's plans.

---

## 12. Reliability, Debug and Safety

| Failure | GLOS's response |
|---|---|
| **Supervisor panic** | `GLOS-PANIC` lines on COM1 (registers, stack, last log lines), then a halt or a reboot as configured. The host side falls back to the relay (§7.9). |
| **App crash** | The app is killed and everything it held is released: the GPU lock (with an engine reset when it was mid-frame), VRAM, DOS handles and per-app DOS state, mixer streams, windows. Its crash report is written (§7.8). |
| **Exclusive session crash or kill** | Hardware is restored as in §6.9, and the desktop comes back. |
| **Watchdogs** | GPU lock hold time, DOS lock hold time, and how long a VM keeps virtual interrupts off (logged, never killed automatically in v1). |
| **Hostile programs** | A test suite (§14.4) checks that no ring-3 or V86 code can stop the tick or the agent. |
| **Exit cleanliness** | After `glos exit`, VECCHK and VMODE report a clean machine (§2.1 G8). |

**COM1 rule:** one line per event, no periodic output (§7.7).

---

## 13. Performance

Targets are measured on silicon or derived from CPU models, never from 86Box speed (D28).

| # | Target | Reference |
|---|---|---|
| P1 | Exclusive sessions within 3% of the same program without GLOS (DOSBench, frame times) | Pentium II + G200/G450, bench |
| P2 | Windowed DOS-GL at 90% or more of full-screen frame rate at the same size | Pentium II + G450 |
| P3 | A window drag at 1024×768×16 holds 30 fps (accelerated) | Pentium II + G200 |
| P4 | The desktop is usable at 640×480×8 (VBE) | 486DX2-66 (model; Q10) |
| P5 | An SSH handshake takes under 1 s; SFTP reaches 1 MB/s or more | Pentium II + RTL8139 |
| P6 | Mixer refill latency under 20 ms; zero underruns in Fifth Wheel windowed | Pentium II + SB16 |

**Levers:**
- VME/PVI;
- global kernel pages;
- fast paths for the most frequent traps (the PIC's end-of-interrupt write, CLI/STI);
- never trapping hot ports such as the gameport and the framebuffer;
- MTRR write-combining for framebuffers;
- crypto split into chunks on a 486.

---

## 14. Test Strategy

### 14.1 Host tests (Linux)

- lwIP glue, and the SSH protocol against OpenSSH's client.
- SFTP and config-file parsing.
- Window-manager logic, and 2D rasteriser golden images at each depth.
- Package signatures.

### 14.2 Loop A

MGA-Glide's harness, with the M0 extensions. Each job can run on:
- **machine profiles:** Pentium II BF6, plus a 486;
- **boots:** raw FreeDOS, or HIMEMX;
- **cards:** G450, G400, G200, G100, plus a generic VBE card;
- **NICs:** NE2000, RTL8139, 8255x.

GLOS jobs drive the agent over SSH from the host.

### 14.3 The supervisor gate (M4 exit)

Each suite runs with GLOS loaded, on raw and HIMEMX boots, and is compared with the same suite under CWSDPMI or DOS/4GW without GLOS. The suites are:
- MGA-Glide conformance (27 tests × 4 cards) and its retail-game replays;
- DOS-GL conformance;
- `loopa-sdl`;
- ClassiCube, GLQuake, Quake 2, Half-Life, PrBoom-plus;
- Fifth Wheel's checked replays;
- `loopa-selftest`.

Every suite must show:
- the same status;
- frames the program dumps itself that are identical (`samepix`);
- the same sets of HX-, DGL- and MGL- lines, with timing fields ignored;
- an SSH status probe answered at least every 2 s throughout, with a screenshot and a kill succeeding on request;
- VECCHK OK and VMODE 3 afterwards.

Timed `--shots` screenshots are left out, because they depend on timing.

### 14.4 Hostile programs

Small DOS and DPMI programs that try to take the machine. Each must leave the tick and the agent alive and be killable:
- `cli; jmp $` and `pushf/popf` games;
- turning A20 off through port 92h;
- remapping the PIC;
- writing RTC registers;
- reprogramming the PIT;
- Ctrl-Alt-Del and a reset through CF9h;
- HLT with interrupts off;
- touching unmapped MMIO.

### 14.5 GUI

- Desktop golden images per display driver and depth. Accelerated and VBE output must be identical (M7).
- Windowed DOS-GL conformance: the same render surfaces as full-screen (M8).
- App tests driven over SSH, with input injection and screenshots (M9).

### 14.6 Loop B

The bench runs the gate's suites through GLOS's agent (M5 on).

### 14.7 CI

- Host tests and the build: loader, kernel, libraries.
- A Loop A smoke job per card (boot, agent up, `glos shot`, exit).

---

## 15. 86Box & Harness Work Package (in MGA-Glide first, D27)

| # | Item | Milestone |
|---|---|---|
| H1 | `--net CARD`: a NIC on SLiRP, plus `[SLiRP Port Forwarding #1]` with a host port per VM. 86Box already reads this section (`src/network/net_slirp.c`). | M0 |
| H2 | `--machine`: a 486 profile (machine and CPU chosen in M0, Q9) next to the BF6 Pentium II | M0 |
| H3 | `--com2`: COM2 to a host pty or socket, for the kernel gdb stub | M0 |
| H4 | `--boot-cfg himemx`: a second golden boot image with HIMEMX. The default image stays byte-identical. | M0 |
| H5 | A CPU self-test for V86 mode, TSS and the I/O bitmap (like STACKPG, which found the bug behind patch 0103) | M0 |
| H6 | A generic VBE 2.0 card option, for "any VESA 2.0" coverage | M0 |
| H7 | SSH steps in Loop A jobs: wait for the SSH server, run commands, fetch files | M3 |
| H8 | Loop B's SSH transport in `tools/bench/run.py`, with BENCH.BAT kept as the fallback | M5 |
| H9 | Fixes for any 86Box CPU or device bug GLOS uncovers, as local patches (no fork) | As needed |

---

## 16. Build and Toolchain

| Item | Choice |
|---|---|
| Loader | Open Watcom v2 (`wcc -bt=dos -ms -0`, `wlink system dos`) → `GLOS.EXE` |
| Supervisor | Host gcc `-m32 -march=i486 -ffreestanding -fno-pic`, GNU as, `ld` with a linker script → ELF, then `objcopy` to a flat image the loader reads (D18) |
| GLOS libraries and apps | DJGPP gcc 12.2 (`i586-pc-msdosdjgpp-gcc`), as for DOS-GL |
| Shared HAL | Vendored from MGA-Glide (`hal-export`/`sync-hal`) once the kernel needs it, with a `hal/port/glos.c` port in MGA-Glide (Appendix B) |
| Host tests | Native gcc on Linux |
| Loop A | `$(MGA_GLIDE)/tools/dev` + `tools/loopa/run.py`, pinned in `deps.mk` (`make check-deps`) |
| Build system | Makefile, with `config.mk` and an uncommitted `config.local.mk` |
| CI | GitHub Actions: host tests, the build, Loop A smoke jobs (`MGA_NO_DOCKER=1`, as in the sibling repos) |

---

## 17. Licensing & Provenance

### 17.1 Project licence

MIT.

### 17.2 Reused code

Reused code lives in `third_party/` with its own licence file and an entry in `THIRD_PARTY.md`. The candidates are in Appendix C. Each licence is re-checked when the code is imported.

### 17.3 Reference only (D26)

- **Never copied:** Jemm/JemmEx (Artistic 1.0), HDPMI (freeware, no licence to modify), CWSDPMI (GPL), DPMIONE and 386MAX (GPL-3.0), the FreeDOS kernel (GPL-2.0), Watt-32 (no modified redistribution), mTCP (GPL-3.0), wolfSSL (GPL-3.0). Their binaries are used as behavioural baselines in tests (HDPMI and CWSDPMI for DPMI behaviour, FreeDOS as the system VM's DOS). GLOS code isn't derived from their source.
- **Write from:** the DPMI specifications, the Intel manuals, Ralf Brown's Interrupt List and the Windows 3.0 DDK documentation.

### 17.4 Microsoft material

- MS-DOS 1.25, 2.0 and 4.0 are MIT-licensed and may be read and, with attribution, reused.
- Leaked MS-DOS 6.0 or Windows source is never consulted by anyone working on GLOS.

### 17.5 Trademarks

GLOS doesn't use Microsoft, IBM or Apple marks or trade dress (D7).

---

## 18. Repository Layout

```
GLOS/
  PRD.md  README.md  LICENSE  THIRD_PARTY.md (from M3)
  Makefile  config.mk  deps.mk        config.local.mk is not committed
  loader/        GLOS.EXE (16-bit, Open Watcom)
  kernel/        supervisor (gcc -m32)
    arch/        GDT/IDT/TSS, paging, FPU, CPU features
    mm/          physical and linear memory
    vm/          V86 monitor, virtual devices (pic, rtc, pit, kbc, a20, cmos)
    dpmi/        DPMI host, XMS server, INT 15h emulation
    dos/         DOS server
    drv/         nic/, sb/, ps2/, display/ (vbe, matrox via HAL)
    net/         lwIP port
    ssh/         SSH server, SFTP, glos commands
    gfx/         2D library, window manager, VRAM manager
    api/         GLOS API dispatcher
    dbg/         gdb stubs, crash reports
  include/glos/  public API headers (SDK-ready, D14)
  lib/           ring-3 libraries (libglos for DJGPP, toolkit, TLS)
  apps/          native apps, Control Panel, package manager
  third_party/   tinyssh/, lwip/, bearssl/, stb/, mgahal/ (vendored)
  tests/         host/, dos/ (DPMI conformance, hostile suite), goldens/
  tools/         host tools (keys, package signing, gate runner)
  docs/          design notes
```

---

## 19. Milestones

Defined by their exit criteria. All exits are in Loop A unless marked "bench". v1 is M0–M9 plus the M10 gates; each M10 feature ships when it passes its own gate.

**M0: harness (in MGA-Glide, with its full regression).** H1–H6 from §15.
*Exit:* `loopa-selftest` and the full MGA-Glide regression unchanged. A NE2000 VM answers a TCP connection on its forwarded port. The 486 profile and the HIMEMX boot run the existing HX tools. The V86/TSS/IOPB self-test passes.

**M1: ring 0 round trip.** Loader (raw and XMS modes), kernel image, protected mode with paging, serial log, gdb stub on COM2, return to DOS.
*Exit:* a `GLOS-` line from ring 0, then back to DOS, with VECCHK clean, KEYWAIT working and VMODE 3. This holds on the Pentium II and 486 profiles and on raw and HIMEMX boots. gdb breaks in the kernel over COM2.

**M2: the system VM.** V86 monitor, virtual PIC, RTC, 8042, A20 and XMS; the RTC tick; DOS running under GLOS.
*Exit:* RUN.BAT runs under V86, and the HX tools give the same output as without GLOS. The hostile-program suite leaves the tick alive and every program killable (kill reported on COM1 at this stage).

**M3: network and a minimal agent.** NE2000 driver, lwIP with DHCP, the SSH server (exec, SFTP), text-mode screenshots, H7.
*Exit:* from the host, over SSH: a 16-bit command runs with its output captured and exit code returned, an SFTP round trip is byte-identical, and a text-mode screenshot is taken.

**M4: DPMI host and exclusive sessions.** Full DPMI 0.9 and the 1.0 subset, exceptions, crash reports, exclusive sessions with kill and restore.
*Exit:* the supervisor gate (§14.3).

**M5: the full agent and Loop B.** Graphics screenshots, input injection, telemetry, `glos gdb` through `direct-tcpip`, RTL8139 and 8255x drivers, mDNS, H8.
*Exit:* a Loop A job driven entirely over SSH (keys, shots, logs) matches its `--keys`/`--shots` equivalent. Bench: Loop B runs over SSH with BENCH.BAT as fallback, and DOSBench is within 3% of its numbers without GLOS (P1).

**M6: GUI core on VBE.** Display layer (N screens), 2D library at 8/16/32 bpp, window manager, events, the DOS server for concurrent apps, the RTC-driven virtual PIT per app, API v1, the look-and-feel design pass.
*Exit:* two GLOS apps run at once, and a busy-looping one never stalls the other or the agent. Desktop golden images match at 8, 16 and 32 bpp on the generic VBE card and on a Matrox card in VBE mode.

**M7: Matrox 2D.** The HAL additions (Appendix B), an accelerated 2D backend, hardware cursor, CRTC display start, the VRAM manager.
*Exit:* accelerated desktop frames are identical to VBE frames, on G200, G400 and G450.

**M8: windowed DOS-GL and SDL3.** The GPU lock enforced by MMIO faults, `engine_resync`, the DOS-GL GLOS path, SDL patches for GLOS video, audio and input, the mixer.
*Exit:*
- DOS-GL conformance run windowed gives render surfaces identical to full-screen;
- the same EXEs still pass on plain DOS;
- Fifth Wheel and PrBoom-plus run windowed with zero underruns (SDL patch 0005's counter);
- a forced GL hang is recovered by the watchdog.

**M9: native apps and the Control Panel.** Toolkit; file manager, notepad, calculator, terminal, system monitor, image viewer, Control Panel and its configuration schema.
*Exit:* each app's golden images and its SSH-driven tests pass. Every setting in `GLOS.CFG` can be changed from the Control Panel and over SSH.

**M10: community features (separate gates).**
- SNTP and mDNS (if not already done in M5).
- The BearSSL HTTPS client, after SNTP so certificate dates can be checked.
- The package manager.
- VNC and capture.
- UTF-8 and the optional anti-aliased fonts.

*Exit, per feature:*
- HTTPS fetches from GitHub and a FreeDOS mirror with certificate checks.
- A signed package installs and uninstalls cleanly.
- A stock VNC client views and drives the desktop through an SSH tunnel.
- A capture's frames are identical to `glos shot`.

**Release v1.0:** M0–M9 complete, plus each M10 feature that has passed its gate. Bench re-verification passes on every bench card.

**After v1:** v2 SDK; windowed DOS boxes; DualHead and multi-card displays; GLOS's own FAT driver; EMS; more NICs; other vendors' drivers.

---

## 20. Risks

| ID | Risk | Likelihood | Mitigation |
|---|---|---|---|
| R1 | Retail DOS/4GW games or DJGPP start-up code depend on host behaviour GLOS doesn't reproduce | High | The Appendix A census; log every unknown call; compare with CWSDPMI and HDPMI as baselines; the supervisor gate |
| R2 | 86Box mis-emulates V86 mode, TSS or I/O-bitmap paths (it has before: patch 0103) | Medium | H5 self-test in M0; local patches (no fork); re-verify on the bench |
| R3 | DOS state bugs with concurrent apps (current directories, PSPs, EXEC frames) | High | One system-VM lock; per-app state swapping; stress tests; fallback to one DOS-using app at a time |
| R4 | GPU sharing hangs the chip (stale HAL state, FIFO overflow) | Medium | MMIO-fault enforcement, `engine_resync`, the watchdog with `engine_reset` |
| R5 | 486 performance (crypto, trap overhead, TLB flushes) | Medium | Crypto split into chunks; VME/PVI; measure on silicon or with models; a 486 bench PC (Q10) |
| R6 | v1 scope is large | High | M10 features have independent gates; the headless milestones are useful alone |
| R7 | Licence contamination | Low (high impact) | D26 and §17; `THIRD_PARTY.md` reviewed at each import |
| R8 | Programs that use the RTC periodic interrupt themselves | Low | Full RTC virtualisation; an option to run such programs with the PIT as GLOS's tick |
| R9 | Hardware variety on real PCs: shared IRQs, BIOS and VBE quirks | Medium | Conservative defaults; a quirks table; refusal with a clear message; the bench |
| R10 | Behaviour differences between FreeDOS and MS-DOS | Medium | Loop A on FreeDOS, the bench on MS-DOS 6.22/7.1; GLOS relies on no undocumented DOS internals beyond InDOS, the critical-error flag and the open-file table |
| R11 | Supervisor bugs look like Loop A hangs | Medium | COM1 discipline (§7.7), the gdb stub, panic dumps |
| R12 | Several sessions changing shared checkouts | Medium | GLOS pins MGA-Glide (`deps.mk`); cross-repo work on branches in worktrees |

---

## 21. Open Questions

| # | Question | Needed by |
|---|---|---|
| Q1 | Support the 486SX? The core can avoid the FPU; DJGPP programs would need FPU emulation and Matrox paths need an FPU | M1 |
| Q2 | Enable SSE state (OSFXSR) for programs on Pentium III and later, matching what CWSDPMI and DOS/4GW do? | M4 |
| Q3 | Which FreeDOS kernel to recommend or bundle (2044 and later improve Windows-style hooks GLOS doesn't use), and whether GLOS ships a FreeDOS-based boot image | Before release |
| Q4 | Package format, signing keys and hosting | M10 |
| Q5 | Look-and-feel design: colours, decorations, icons, desktop metaphor | M6 |
| Q6 | A real-mode child process started by a GLOS app: run it as an exclusive session, or hold the DOS lock for its lifetime? | M6 |
| Q7 | EMS for older games (needs DMA virtualisation once memory isn't identity-mapped) | After v1 |
| Q8 | Launching apps: DOS EXEC through the DOS server, or GLOS loading DJGPP images itself as Windows' KERNEL loaded apps | M6 |
| Q9 | Which 86Box 486 machine and CPU (with and without VME), and which generic VBE 2.0 card, for Loop A | M0 |
| Q10 | A 486 bench PC, so the minimum spec is measured and not modelled | M5 |
| Q11 | Our own retained-mode toolkit, or microui/Nuklear underneath | M6 |
| Q12 | SDL threads under GLOS: keep SDL's cooperative scheduler per app, or map them onto GLOS threads | M8 |
| Q13 | VNC encodings and the recording format | M10 |
| Q14 | Support 16-bit DPMI clients? | M4 |
| Q15 | Resizing DOS-GL windows: reallocate the buffers, or keep the size and scale with `engine_present` | M8 |

---

## Appendix A: DPMI and hardware census of existing programs

Collected 2026-10-01 from DOS-GL, the shared HAL, SDL3's DOS port with the project's patches, and MGA-Glide.

### A.1 DPMI services

| Service | DJGPP side (DOS-GL, HAL `port/djgpp.c`, SDL3) | Open Watcom / DOS/4GW side (MGA-Glide in retail games, `hal/port/ow_dos4g.c`) |
|---|---|---|
| 0006h get segment base | DJGPP runtime | PSP and environment (`src/rt/env.c`) |
| 0008h set limit (4 GB) | `__djgpp_nearptr_enable`: HAL, `dglSwapBuffers`, SDL | — (flat zero-based model) |
| 0100h/0101h DOS memory | HAL VBE buffers; SDL conventional and DMA memory | VBE buffers |
| 0202h/0203h exceptions | DJGPP signals (HAL port, SDL patch 0003) | Exceptions 00h, 06h, 0Dh, 0Eh with a private 16 KB stack, chained with `jmp fword` |
| 0204h/0205h PM vectors | SDL: IRQ1 and the Sound Blaster IRQ | INT 21h (AH=4Ch exit hook) |
| 0300h real-mode interrupt | INT 10h (HAL); INT 10h, 16h, 33h (SDL) | INT 10h |
| 0301h real-mode far call | SDL VBE bank switching (banked modes) | — |
| 0501h/0502h memory blocks | DJGPP runtime | `sys_alloc` |
| 0600h page locking | ISRs, rings, thread stacks (SDL); `_CRT0_FLAG_LOCK_MEMORY` | — |
| 0800h/0801h physical mapping | Matrox MMIO and framebuffer (HAL `mga_map`); VBE LFB (SDL) | Matrox MMIO and framebuffer |
| Not used | 0303h real-mode callbacks, PCI BIOS (INT 1Ah), RDTSC | — |

Runtime start-up code and DOS/4GW itself use more than this, for example 0303h for mouse callbacks (§6.6). The M4 gate finds the rest.

### A.2 Hardware and BIOS

- **PCI:** configuration mechanism #1, 32-bit at CF8h/CFCh. Buses 0–255 are scanned; memory space and bus mastering are enabled.
- **Matrox:** MMIO for the CRTC, INSTS1, the DAC and X_DATAREG (no VGA port I/O in the HAL).
- **VBE:**
  - Functions 4F00h, 4F01h, 4F02h (with the LFB bit), 4F06h and 4F07h (each flip in DOS-GL, `vbe.c:145`).
  - SDL also uses 4F05h (bank switching), mode 13h, and ports 3C8h/3C9h.
- **Timer:** DJGPP's `uclock` programs PIT channel 0 to mode 2 with reload FFFFh, then latches and reads port 40h. The OW side reads the BIOS tick at 0040:006C. `delay()` uses INT 15h 86h.
- **Keyboard:** SDL's IRQ1 handler reads port 60h, sends its own EOI and chains to the BIOS handler, so the PIC gets two EOIs.
- **Mouse:** INT 33h polled (functions 0, 3, 4, 7, 8, 0Bh, 1Bh).
- **Sound Blaster:**
  - IRQ from `BLASTER`, auto-init DMA: 8-bit on ports 0Ah–0Ch with pages 81h–87h, 16-bit on ports D4h–D8h and C0h–DFh with pages 89h–8Bh.
  - The DMA buffer assumes linear = physical below 1 MB.
- **Gameport:** 201h, a polled timing loop with interrupts left on.
- **Privileged instructions:** CLI/STI (SDL mutexes, semaphores, PlayDevice), HLT (SDL scheduler idle).
- **Harness:** port 0E80h (86Box unit tester), COM1 3F8h–3FDh (polled, 115200 8N1), port 80h.

## Appendix B: Cross-repo changes

| Repo | Change | Milestone | Gate |
|---|---|---|---|
| MGA-Glide | Loop A H1–H7 (§15) | M0, M3 | Full MGA-Glide regression (conform 27 × 4, replays, `loopa-selftest`) |
| MGA-Glide | Loop B SSH transport (H8) | M5 | The same, plus a vbench dry run |
| MGA-Glide (HAL) | `hal/port/glos.c` (`sys_rm_int` becomes a nested V86 call under the DOS lock; physical mapping through GLOS) | M7 | The same; `make sync-hal` into DOS-GL and GLOS afterwards |
| MGA-Glide (HAL) | Screen-to-screen BITBLT, mono expansion, hardware cursor, a shared VRAM allocator, `engine_resync()`, display start through the CRTC | M7 | The same, plus new conformance tests for each primitive |
| DOS-GL | The GLOS path in `src/dgl/context.c`: 0A00h detection, VRAM from GLOS, swap as present, the GPU lock, no VBE calls | M8 | DOS-GL conformance on G200, G400 and G450 on plain DOS (unchanged), and windowed under GLOS |
| DOS-GL (SDL patches) | 0007 onward: GLOS video, audio and input in SDL's DOS port | M8 | `loopa-sdl` on plain DOS, and windowed under GLOS |
| Fifth Wheel | None (the one-EXE proof) | M8 | Runs windowed under GLOS, unmodified |
| DOSBench | Optional: run its suites with GLOS loaded (P1 numbers) | M5 | — |

## Appendix C: Reuse and licence table

Licences as researched on 2026-10-01; each is re-checked at import (§17.2).

### C.1 Candidates for reuse (MIT-compatible)

| Component | Licence | Use in GLOS | Source |
|---|---|---|---|
| TinySSH | CC0-1.0 OR 0BSD OR MIT-0 OR MIT | SSH protocol and crypto (curve25519, ed25519, chacha20-poly1305) | https://github.com/janmojzis/tinyssh |
| OpenSSH `sftp-server.c` | ISC-style | SFTP subsystem | https://github.com/openssh/openssh-portable |
| lwIP 2.2 | BSD-3-Clause | TCP/IP, DHCP, DNS, mDNS, SNTP | https://github.com/lwip-tcpip/lwip |
| BearSSL | MIT | TLS 1.2 client | https://bearssl.org/ |
| Mbed TLS (alternative) | Apache-2.0 OR GPL-2.0-or-later | TLS 1.3, if needed after v1 | https://github.com/Mbed-TLS/mbedtls |
| stb (`stb_image_write`, `stb_truetype`) | Public domain OR MIT | PNG screenshots, TrueType | https://github.com/nothings/stb |
| FreeType | FTL OR GPL-2.0 | Optional font rasteriser (ring 3) | https://freetype.org/ |
| microui / Nuklear | MIT / MIT OR public domain | Possible toolkit base (Q11) | https://github.com/rxi/microui , https://github.com/Immediate-Mode-UI/Nuklear |
| EtherDFS | MIT | Reference for a network drive (after v1) | https://sourceforge.net/projects/etherdfs/ |
| PMODE/W 1.33 | MIT since 2023 (check the repo's LICENSE) | Reference for protected-mode start-up | https://github.com/amindlost/pmodew |
| PC/GEOS | Apache-2.0 | Design reference (16-bit code, not reused) | https://github.com/bluewaysw/pcgeos |
| MS-DOS 1.25/2.0/4.0 | MIT | Reference | https://github.com/microsoft/MS-DOS |

### C.2 Reference and baselines only (D26)

| Component | Licence | Use | Source |
|---|---|---|---|
| Jemm / JemmEx | Artistic 1.0 (in part) | V86 monitor reference | https://github.com/Baron-von-Riedesel/Jemm |
| HX / HDPMI32 | Freeware, no licence to modify | DPMI behaviour baseline | https://github.com/Baron-von-Riedesel/HX |
| CWSDPMI r7 | GPL | The DJGPP baseline host | https://sandmann.dotster.com/cwsdpmi/ |
| DPMIONE, 386MAX | GPL-3.0 | Reference | https://github.com/sudleyplace/DPMIONE , https://github.com/sudleyplace/386MAX |
| FreeDOS kernel | GPL-2.0 | The system VM's DOS (shipped alongside, unmodified) | https://github.com/FDOS/kernel |
| Watt-32 | Waterloo licence (no modified redistribution) | Reference | https://github.com/gvanem/Watt-32 |
| mTCP | GPL-3.0 | Bench tooling today (BENCH.BAT) | http://www.brutman.com/mTCP/ |

### C.3 Specifications

| Document | Source |
|---|---|
| DPMI 1.0 | https://www.delorie.com/djgpp/doc/dpmi/ |
| DPMI 0.9 | https://www.phatcode.net/res/262/files/dpmi09.html |
| Windows 3.0 DDK, *Virtual Device Adaptation Guide* (instance data, start-up) | https://www.pcjs.org/documents/books/mspl13/win/w3ddkvxd/ |
| INT 2Fh 16xxh notes | https://geoffchappell.com/notes/dos/interrupts/2Fh/16h/00h.htm |
| Packet Driver Specification 1.11 | https://www.cs.vsb.cz/grygarek/PS/packet/pds111.txt |
| SDL3 DOS port (merged 2026-04-23) | https://github.com/libsdl-org/SDL/pull/15377 |
| Intel 486 and Pentium manuals; Ralf Brown's Interrupt List | Standard references |
