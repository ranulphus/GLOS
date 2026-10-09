# GLOS supervisor design

| | |
|---|---|
| Status | Draft 0.1 (before M1) |
| Last updated | 2026-10-01 |
| Covers | Milestones M1–M4 (PRD §6, §19) |
| Companion | [PRD.md](../PRD.md) v0.2, [milestones-m0-m4.md](milestones-m0-m4.md) |

This document fixes the supervisor's design before any kernel code is written. Choices that are expensive to
change later are marked **[fixed]**. Changing one needs a PRD decision-register entry.

Sources are cited inline:
- **[DPMI0.9 §n] / [DPMI1.0 ch]:** the DPMI specifications.
- **[SDM]:** the Intel manuals.
- **[RBIL]:** Ralf Brown's Interrupt List.
- **[census]:** the 2026-10-01 census of our programs and games (PRD Appendix A).
- **[86Box]:** the pinned emulator source, MGA-Glide `tools/setup/versions.mk`.

---

## 1. Scope and provenance

The supervisor is everything that runs at ring 0 in M1–M4:
- the loader's protected-mode half;
- memory and CPU management;
- the scheduler;
- the V86 monitor and virtual devices;
- the XMS server;
- the DPMI host;
- the DOS server;
- the agent's kernel side.

M3 adds network and SSH; they are covered in PRD §7–8.

**Clean room (PRD D26):**
- GLOS code is written from the DPMI 0.9/1.0 specifications, the Intel manuals, RBIL, the Windows 3.0 DDK
  documentation, and the behaviour observed in tests.
- CWSDPMI (GPL), HDPMI (freeware), Jemm (Artistic), DJGPP's libc sources and the FreeDOS kernel are
  behavioural references only. This document records *behaviour* learned from them and never code.
- Their binaries are used as baselines in tests (§13, milestones doc).

## 2. Boot contract

`GLOS.EXE` is a 16-bit Open Watcom program. It does the following, in order, writing a `GLOS-BOOT step=…` line
to COM1 at each step:

1. **Refusals** (`GLOS-REFUSE reason=…`, exit code 2):
   - the CPU is older than a 486 (the AC flag won't toggle);
   - the CPU is already in V86 mode (SMSW shows PE);
   - a DPMI host answers INT 2Fh 1687h;
   - VCPI answers INT 67h DE00h;
   - Windows answers INT 2Fh 1600h.
2. **Memory:**
   - **Raw mode** (no XMS driver): INT 15h E820h, then E801h, then 88h.
   - **XMS mode:** allocate the largest block(s) (XMS 09h/89h) and lock them (0Ch) to learn their physical
     addresses.
   - In either mode, physical 0–10FFFFh is reserved for the system VM.
3. **A20:** port 92h, then 8042 command D1h, checked with a wrap test. In XMS mode, through XMS 03h.
4. **Kernel load:**
   - `GLOSK.BIN` is a flat image with a 16-byte header: magic `GLOK`, entry offset, file size, size including
     bss.
   - The loader reads it into conventional memory. Its mode-switch code copies it to the kernel's high mapping
     (and so into extended memory) with 32-bit addressing, then zeroes the bss. That works the same in raw and
     XMS modes, so neither INT 15h 87h nor XMS 0Bh is needed.
   - Physical home: 110000h in raw mode (above the HMA); a locked XMS block in XMS mode. XMS mode also locks a
     second block for GLOS's memory, leaving 64 KB of XMS for others.
   - The loader leaves a **resident stub** in conventional memory. It holds the breakpoint stubs, the host
     real-mode stacks and the agent shell (§17). Until M3, the stub is the whole of `GLOS.EXE`, which stays
     loaded while it runs the `/RUN` program (§2.1).
5. **Bootinfo** (`include/glos/bootinfo.h`, version 1; M1 and M2 implement the fields below except the IVT
   and BDA copies, which DOS keeps owning in the system VM (§2.1); M2 adds the fields of §2.1):
   - the memory map and XMS handles;
   - copies of the IVT and the BIOS data area;
   - PIC masks and ICW state as found;
   - the video mode;
   - CPU features (CPUID when present; CR4 presence probed safely);
   - the resident stub's segment;
   - the real-mode return point.
6. **Entering the kernel:** `loader/pm.asm` (OW wasm) disables interrupts, loads a temporary GDT and enters
   protected mode with paging. The kernel takes over from there.
7. **Exit** (`glos exit`, the end of the `/RUN` program, or a fatal error before the system VM exists):
   - the kernel restores the PIC (to the virtual PIC's ICW2 and mask, which are 08h/70h and the original
     masks unless a program changed them), RTC registers A/B (as the program last set them), PIT channel 0
     (mode 3 count 0, as the BIOS sets it at POST), the 8042's command byte, and A20 as found;
   - it switches back through the 16-bit CS/DS selectors (§3). The IVT isn't reloaded: DOS owned it
     throughout, and a kill puts back each killed program's vectors (§9.6);
   - the resident stub returns to DOS with the requested exit code.
   - **Acceptance:** VECCHK OK and VMODE 3 afterwards.

### 2.1 The system VM's start and GLOS.EXE's calls

- `GLOS /RUN program [args]` sets `BI_F_VM`. The kernel then doesn't return to real mode: it resumes
  `GLOS.EXE` in V86 mode at `_vm_resume`, with SS, SP and DS as `pm_enter` saved them, so `pm_enter`
  appears to return 10000h. `GLOS.EXE` frees the kernel image's buffer and the first page tables, finds the
  program as COMMAND.COM would (.COM, .EXE, .BAT; here, then `PATH`; a batch file under `%COMSPEC% /C`) and
  hands over to its resident stub (§2.2), which runs it. When it ends, the kernel leaves through `_pm_ret`
  as in M1, and the stub exits with the program's code.
- **The kernel copies bootinfo** at entry: `GLOS.EXE`'s data doesn't outlive the stub. The loader fills in
  the DOS pointers (`indos`, `sda`) before entering the kernel.
- **Calls into the kernel** are ARPL instructions, which raise #UD in V86 mode (§9.2), at offsets in the
  stub's segment that bootinfo gives: `bp_call_off` for `glos_call(fn, arg)` (AX, EBX; the result in EAX) and
  `bp_xms_off` for the XMS entry point (§16).
- **Functions:** LEAVE (1, the exit code); NEXT (4, the stub asks what to run, passing how the last EXEC
  ended: INT 21h 4Dh's AX, 10000h plus the DOS error if EXEC failed, FFFFFFFFh at first; EAX = 1 means EXEC
  the stub's path and tail); RESIDENT (5, the stub has moved to `arg`:0). DOSPTR (2) and EXEC (3) were M2's
  and are no longer used.
- **Bootinfo fields:** `vm_resume_off`, `vm_state_off`, `bp_call_off`, `bp_xms_off`, `indos`, `sda`,
  `kill_off` and `kill_sp` (the kill stub and its 256-byte stack, §9.6), `stub_paras`, and in XMS mode the
  driver's version (`xms_ver`, `xms_rev`, `xms_hma`), whether the HMA was already taken (`hma_used`, so
  DOS=HIGH) and INT 2Fh 4309h's handle table (`xms_table`).

### 2.2 Start modes and the resident stub (M3; PRD D39–D41)

- **The stub** (`loader/stub.asm`, done in M3). Once the kernel is running, `GLOS.EXE` keeps only a resident
  stub of 4 KB or less (P7) and frees the rest of its memory block (INT 21h 4Ah), like a TSR. The stub holds
  the mode switch, the kernel-call and XMS entry points, the kill stub, its stacks (the real-mode stacks of
  §9.5 join them in M4) and the EXEC loop. Everything else (finding programs, the agent, later the desktop)
  runs in the kernel, which tells the stub what to EXEC (NEXT, §2.1). INT 2Fh 1687h will report no real-mode
  memory needed per DPMI client.
- **Going resident.** Open Watcom puts its runtime's code at the start of the image, so the stub is written to
  run from any segment (data through CS, no segment fixups) and is copied down to the paragraph after the
  PSP. The copy tells the kernel (RESIDENT), which moves selector 38h and the call sites to it and checks
  that no vector a C runtime hooks (00h–07h, 1Bh, 23h, 24h) points into the memory being freed
  (`GLOS-WARN ivt-into-loader`). The stub then shrinks the block and runs the EXEC loop. After the kernel
  leaves, the stub puts A20 and the XMS blocks back as found, writes `GLOS-EXIT` and ends with INT 21h 4Ch.
- **Measured (M4a, with the agent's wait, the DOS server's call and the DPMI host's ARPLs):** the stub is 2,556
  bytes. With the PSP and the environment, MEM /C shows GLOS at 3,008 bytes, and the largest program is 3,008
  bytes smaller than under plain DOS.
- **From a prompt or AUTOEXEC.BAT:** COMMAND.COM stays the shell; `glos exit` returns to it.
- **As the shell (`SHELL=C:\GLOS\GLOS.EXE /SHELL` in CONFIG.SYS; done in M3):**
  - `/SHELL` says so; so does a PSP that is its own parent (MS-DOS's shell). FreeDOS doesn't make its shell its
    own parent, so `/SHELL` is needed there. FreeDOS also ignores a `SHELL=` line much longer than 64
    characters, so the settings belong in GLOS.CFG:

    ```
    [shell]
    comspec  = A:\FREEDOS\BIN\COMMAND.COM   ; else COMSPEC, the boot drive's \, \FREEDOS\BIN or \DOS,
                                              ; GLOS.EXE's directory, or PATH
    autoexec = C:\AUTOEXEC.BAT                ; else the boot drive's \AUTOEXEC.BAT
    console  = C:\KIOSK.BAT                   ; run (through COMSPEC /C) instead of the prompt
    envsize  = 2048                           ; the master environment, in bytes (default 1024)
    options  = /DPMITRACE                     ; GLOS.EXE's flags: /DPMITRACE, /NOVME, /GDB (M4c)
    ```

    `/COMSPEC=`, `/P=`, `/E:` and `/CON=` on the command line override them, like COMMAND.COM's own options.
  - `GLOS.EXE`'s environment becomes the master environment: what DOS gave it, with `COMSPEC` set, in a
    block of `envsize` bytes that the stub allocates right above itself once resident.
  - Once the kernel is running, the stub runs `COMSPEC /C AUTOEXEC.BAT`. When that COMMAND.COM ends (INT 21h
    4Ch or 00h from a child of `GLOS.EXE`), the kernel copies its environment into the master environment
    before DOS frees it: whole strings, as far as the block holds (`GLOS-VM env bytes=… of …`).
  - .COM and .EXE programs are EXECed directly. COMMAND.COM runs batch files, internal commands and
    `%COMSPEC%` shell-outs.
  - Until the desktop exists (M6), the local console is `COMSPEC` (or `COMSPEC /C console`), which the stub
    runs and runs again whenever it exits. Agent commands run alongside it at safe points (§17.2).
  - GLOS never just ends. A refusal (§2 item 1), a missing kernel or a failure before the system VM exists
    leads to `GLOS-SHELL fallback=COMSPEC /P…`: the stub goes resident without the kernel and runs
    `COMSPEC /P` (with `=autoexec` and `/E:` when given) as the permanent shell. That fallback costs only the
    stub. If even COMSPEC can't run, `GLOS-SHELL error=cannot-run`, and the machine halts. When the kernel
    leaves (`glos exit`, M3), the stub does the same after its cleanup; `EXIT` returning to GLOS comes with
    `glos exit`.
  - Measured on the glosshell boots: GLOS takes 4,896 bytes (the stub and PSP, and the 2 KB master
    environment). While a batch file runs, the largest program is 577,600 bytes on the raw boot (plain DOS:
    511,152, because COMMAND.COM swaps itself into GLOS's XMS) and 628,272 on the HIMEMX boot (plain DOS:
    631,488).
- **Later (opt-in, M9):** the stub moves into an unused upper-memory page that GLOS maps for the system VM,
  leaving the PSP and environment (about 0.5 KB) below 640K. A page qualifies only when it reads back as
  open bus (FFh), holds no option ROM signature and isn't claimed by any PCI BAR.

## 3. CPU tables [fixed]

### 3.1 GDT

| Selector | Use |
|---|---|
| 08h | Kernel code, ring 0, flat |
| 10h | Kernel data, ring 0, flat |
| 18h | TSS (the one 32-bit TSS) |
| 20h | #DF task TSS |
| 28h | LDT of the current DPMI context (the descriptor is rewritten on context switch) |
| 30h | espfix stack segment (§6.3) |
| 38h | 16-bit code at the resident stub, used to leave protected mode |
| 40h | BIOS data selector, base 400h, limit FFFFh, ring 3: the 0040h that Windows-era code loads directly (GTA's DOS/4GW code does). Until M4c this was the 16-bit data selector below, and the game faulted |
| 4Bh | Ring-3 trampoline code: host-owned stubs (IRET trampoline, PM breakpoints, the RMCB return) |
| 53h | Ring-3 data alias of the trampoline page |
| 5Bh | The same as 40h (M1–M4b's BIOS selector) |
| 60h | 16-bit data at the resident stub, used to leave protected mode (40h until M4c) |

The order above never changes, and new selectors are appended. M4c's move of the 16-bit data selector
from 40h to 60h was the one exception: the deep dive's "0040h selector" had been built at 5Bh, where no
program looks for it.

### 3.2 IDT

| Vectors | Gate | Purpose |
|---|---|---|
| 00h–1Fh | DPL 0 (03h and 04h DPL 3) | CPU exceptions. Software INT 0–1Fh from a client therefore raises #GP and is decoded. |
| 20h–4Fh, 60h–FFh | DPL 3 "fast gates" | Software interrupts from 32-bit and 16-bit clients, dispatched through the current context's **virtual IDT** (§14). INT 21h and INT 31h never pass through #GP decoding, and a client that hooks INT 31h (an extender answering 0A00h, [DPMI0.9 §21.4]) is honoured. |
| 50h–57h, 58h–5Fh | DPL 0 | Physical PIC IRQ0–7 and IRQ8–15. Clients never see these vectors (§14.1). |

### 3.3 TSS and I/O bitmap

- One 32-bit TSS, page-aligned: the 104-byte fixed part, the 32-byte VME **interrupt redirection bitmap**,
  the 8 KB **I/O permission bitmap** and the mandatory **trailing FFh byte** (8329 bytes) fill three pages.
  The I/O map base is 136.
- A session switch (M4e) swaps those three page-table entries and runs three INVLPGs: no 8 KB copy, no
  hardware task switch. M2 has the one area, with every port passed through except those of §10.
- ESP0 is written per thread on every switch.
- A second TSS, with its own stack, handles #DF through a task gate and dumps `GLOS-PANIC`. It never returns.
- **The trailing FFh byte is mandatory on silicon.** 86Box doesn't enforce it [86Box], so it is a static
  assertion plus a host test.

## 4. Linear map [fixed]

| Range | Use |
|---|---|
| 0000_0000–0010_FFFF | Identity map of physical 0–10FFFFh (conventional memory, UMA, HMA). Shared page table for PDE 0 in every address space. |
| 0011_0000–003F_FFFF | Unused, so no client data shares PDE 0's page table |
| 0040_0000–BFFF_FFFF | User region of the current DPMI context. DPMI blocks are handed out **ascending from 4 MB** (§12.3). |
| C000_0000–DFFF_FFFF | Kernel: image at C010_0000, heap, per-thread stacks, kmap window, recursive page-directory slot |
| E000_0000–FFBF_FFFF | Reserved, so 0800h can return **linear = physical** for PCI BARs (§12.4) |
| FFC0_0000–FFFF_FFFF | Recursive mapping of the current page directory |

- Kernel pages are supervisor-only and global (PGE where present).
- The kernel has no full direct map of RAM; physical frames are reached through the kmap window.
- **With A20 virtually off,** the HMA page-table entries (100000h–10FFFFh) are remapped onto 0–FFFFh, so the
  1 MB wrap is emulated without touching the real A20 gate (§10).

## 5. Physical memory

- **Free frames:** a bitmap allocator over the bootinfo map, minus the kernel image, the 0–10FFFFh identity
  region and any ROM or ACPI ranges.
- **Raw mode:** all extended memory reported by E820 belongs to GLOS.
- **XMS mode:** only the locked blocks are GLOS's. Handles that other programs allocated before GLOS started
  stay theirs. The XMS server (§16) takes over HIMEM's handle table semantics for new requests.
- **No paging to disk** in v1. 0600h locks are recorded but change nothing.

## 6. Trapframes, fixups and espfix

### 6.1 Trapframe

One trapframe layout for every entry:
- general registers;
- the vector and error code;
- EIP, CS, EFLAGS;
- ESP and SS (when the privilege level changes);
- ES, DS, FS, GS (V86 entries push these; for protected-mode entries the stub saves them).

A `mode` field (V86 / PM16 / PM32 / ring 0) is derived from EFLAGS.VM and CS's descriptor.

The common entry clears DF before any C code runs: a gate leaves DF as the interrupted code had it, and DOS
may be part-way through an `STD; REP MOVSB`. The kernel's string instructions (GCC's inlined copies, the hash
functions' state copies) would otherwise run backwards, and so would any thread the scheduler resumes from that
path. The IRET restores the interrupted code's DF.

### 6.2 Fixup table

- Kernel instructions that may fault on client-supplied data have entries in an exception fixup table: copies
  from client memory, segment loads during IRET, descriptor reads.
- A fault at such an instruction continues at its fixup label, and the client gets the matching DPMI error or
  exception.
- Any other ring-0 fault is a panic.

### 6.3 espfix32

- IRET to a 16-bit SS loads only SP, leaving ESP[31:16] holding the kernel's value. DOS/4GW runs its kernel on
  16-bit stacks after clearing the Big bit [census], and 86Box reproduces the silicon behaviour [86Box].
- Every return to ring 3 or V86 with a 16-bit SS therefore goes through the espfix path. The final IRET runs on
  selector 30h, whose base is chosen so that ESP[31:16] already equals the client's value.

## 7. Threads and scheduling

- **Threads** are kernel objects with their own kernel stack (16 KB since M3 item 6: the SSH thread's crypto reached 5.4 KB, and IRQs nest on the current stack) and a saved trapframe. The **system VM**
  is one thread. Each DPMI client thread runs inside a context (§12). Supervisor tasks are kernel threads.
- **Preemption:** kernel code runs with a preempt count. Interrupt top halves queue deferred work, which runs
  on the way out of the outermost interrupt. Trap paths (V86 decoding, INT 31h) are never preempted midway.
- **Classes:**

| Class | Gets |
|---|---|
| Urgent supervisor work (NIC receive, input, short replies) | Unbounded |
| Bulk supervisor work (crypto, SFTP, PNG encoding) | Preemptible threads with a CPU budget: 30% while an exclusive session is in front, 100% when idle |
| System VM and app threads | Round-robin, 20 ms quantum |

- **Idle:** HLT in ring 0 with interrupts on.

**As built (M3, `kernel/core/sched.c`):**
- Run queues per class. Turns are 20 ticks (about 20 ms at 1024 Hz). Bulk threads take turns with normal
  threads until they have used 30 ticks of the current 100-tick window, then run only when no normal thread
  is ready. The exclusive-session distinction arrives with sessions (M4e).
- A switch happens only in `schedule()`, with interrupts off: when a thread blocks, sleeps or yields, and on
  the way out of an IRQ or a trap from V86 mode when a reschedule is due. An interrupted kernel thread is
  preempted only if it had interrupts on and isn't in a `preempt_disable()` section. The system VM's trap
  paths run with interrupts off, so they are never preempted midway.
- A woken thread that outranks the running one gets the CPU at the next safe point. An IRQ for the system VM
  wakes it and takes the CPU from bulk work (`vm_kick`).
- The system VM's V86 frame always sits at the top of its stack (TSS ESP0). Its waits (HLT, INT 15h 86h)
  block the thread.
- Each stack has a canary word at its base, checked at every switch (`GLOS-PANIC why=stack-overflow`).
- `/SELFTEST` adds a bulk thread that never blocks and an urgent thread sleeping 10 ticks at a time. Over the
  M2 batch on all six Loop A combinations, bulk got 30% of the contended ticks and the sleeper was never late.

## 8. Time and PIT ownership [fixed]

### 8.1 The clock

- The **RTC periodic interrupt** (IRQ8) is the only kernel clock. The default is 1024 Hz; `GLOS.CFG` can set
  256–8192 Hz.
- The handler **always reads register C**. 86Box keeps raising IRQ8 even when C isn't read, but a real
  MC146818 stops [86Box nvr_at.c]. A counter of spurious IRQ8s is kept.
- On CPUs with a TSC (Pentium and later), time between ticks is interpolated from it. A 486 uses the tick count
  alone.

### 8.2 The PIT and the BIOS tick

- **The system VM:** GLOS keeps PIT channel 0 in **mode 2, count FFFFh**, which is exactly what DJGPP's `uclock`
  programs [census]. IRQ0 goes to the system VM's virtual PIC, so the BIOS updates 0x46C as usual and latched
  reads of port 40h stay coherent with it.
- **GLOS never latches or reads PIT channel 0 itself.** That would break a program's lo/hi read sequence.
- **Exclusive sessions:** ports 40h–43h and 61h are passed through untrapped. Programs reprogram channel 0
  freely (Screamer Rally: mode 3 at a run-time divisor; GLQuake: channel 2 for the crash beep). On exit GLOS
  restores mode 2, count FFFFh.
- **GLOS apps (M6):** each gets a virtual PIT. Latched reads pass through while the app's programming matches
  the physical PIT; virtual IRQ0 rates are rounded to the tick.

### 8.3 BIOS waits

- INT 15h 86h and 83h are emulated as sleeps on the kernel clock.
- V86TEST case R (M0) confirmed that both Loop A BIOSes implement INT 15h 86h with the RTC periodic
  interrupt:
  - the BF6's Award BIOS turns PIE on for the wait and **leaves it on** afterwards (register B 02h → 42h);
  - the HOT-433A's Award BIOS turns it off again.

  GLOS never lets a BIOS touch the real RTC (§10). These BIOSes don't support INT 15h 2401h (A20) either,
  so the loader uses port 92h, then the keyboard controller.

## 9. V86 monitor

### 9.1 Mode

- The system VM runs at **IOPL 0** [fixed, D20].
- IOPL-sensitive instructions (CLI, STI, PUSHF, POPF, INT n, IRET) trap with #GP, or run against the virtual
  interrupt flag in hardware when the CPU has **VME** (iDX4, SL-enhanced 486s, Pentium and later).
- I/O instructions are checked against the I/O permission bitmap at any IOPL [SDM].

### 9.2 The decoder

`kernel/vm/v86dec.c` handles:
- the 16- and 32-bit operand and address size prefixes, segment overrides and REP;
- CLI, STI;
- PUSHF(D), POPF(D) (respecting TF and the virtual IF). The image the program sees (M4c): the real arithmetic
  flags, TF and AC; ID only on a CPU with CPUID (86Box's IRET to V86 mode would keep an ID a 486DX2 can't
  hold, and lDebugX's probe then ran CPUID into #UD); the virtual IF; NT as last written; IOPL always 3, as
  VME's PUSHF shows it, so V86 code sees the same FLAGS on every profile;
- INT n; INT3 and INTO only arrive as #BP/#OF through the IDT (they aren't IOPL-sensitive in V86 mode;
  86Box raised #GP until local patch 0107);
- IRET(D);
- HLT ("wait for the next virtual interrupt");
- IN, OUT, INS, OUTS on trapped ports, dispatched to the virtual devices.

ARPL (#UD in V86) is the breakpoint instruction for host stubs in the resident stub.

### 9.3 Virtual IF and pending interrupts

- **Per-VM state:** the virtual IF, a pending-IRQ mask from the virtual PIC, and a "stuck" timestamp.
- **Delivery:** when the virtual IF goes from 0 to 1 with an IRQ pending, the IRQ is delivered immediately.
- **With VME:** VIP is set while an IRQ is pending, and the next STI or POPF faults so it can be delivered.
- **Redirected software INTs:** with VME, the redirection bitmap sends most INTs straight to the IVT. Vectors
  GLOS handles (13h, 10h busy tracking; 15h; 21h for capture and EXEC tracking; 2Fh; 67h) stay clear.
- **PVI covers CLI and STI only.** With PVI, ring-3 CLI/STI change VIF without trapping, but **POPF never
  changes VIF** (or IF at IOPL < CPL) and doesn't trap. A client's `pushf; cli; ...; popf` therefore leaves
  VIF clear on PVI CPUs, and IF unchanged without PVI. This is the POPF hazard of IOPL 0 that the M0 survey
  measures. 86Box loaded VIF from the image until local patch 0109; V86TEST case H checks it.
- **Watchdog:** if the virtual IF stays off for more than 50 ms with an IRQ pending, GLOS writes
  `GLOS-WARN vif-stuck cs:ip`. It doesn't force the flag in v1.

### 9.4 The system VM as built (M2, M3)

- **A thread** (§7, M3). In M2 it ran from trap context alone.
- **VME** (M3) where CR4.VME exists (the Pentium II and iDX4 profiles; `/NOVME` turns it off): CLI, STI,
  PUSHF, POPF, IRET and software INTs run in hardware against EFLAGS.VIF. Only INT 15h and 21h trap
  through the redirection bitmap (with 1Ch, 23h and 24h for a client's handlers, M4b; INT 2Fh's handler is
  GLOS's at the bottom of the IVT chain from M4c, §12.1c); INT 10h and 13h join them with the DOS server's busy tracking (§17.2). The
  monitor reads VIF at every trap from V86 mode and writes it back, with VIP set while an IRQ waits. The same
  MEM run traps 1,237 times with VME and 33,968 times without. PUSHFD, POPFD and IRETD still trap. On the
  486DX2 every one traps, as in M2.
- **Exceptions** a real-mode CPU would raise (00h, 01h, 03h–07h, 0Ch) are reflected through the IVT. #GP for
  a segment limit becomes INT 0Dh. A system instruction (0Fh 20h-23h, LMSW, LGDT and the like) can't be
  emulated: the program is killed (`GLOS-WARN v86-priv`, then the kill of §9.6).
- **HLT** with the virtual IF off waits for a kill. `STI; HLT` with an IRQ pending skips the HLT, as on silicon.
- **INT 15h 86h** is executed again until its deadline (keyed by CS:IP and SS:SP), so the IRQs that arrive
  meanwhile are delivered in front of it.

### 9.5 Nested execution

- Only the system VM thread runs V86 code. DPMI 0300h–0302h calls, interrupt reflections and the agent's DOS
  calls nest on that thread, up to 16 levels. Each level saves the V86 state and uses its own host real-mode
  stack in the resident stub.
- Requests from other threads are **posted** and run at the next safe point (§17.2). That serialises DOS and
  the BIOS by construction.

### 9.6 Kill

- **Ctrl-Alt-Shift-Esc** ends the current program. The 8042 code (§10) sees the keys as bytes arrive from the
  chip, so the kill works whatever the program does to interrupts, the PIC or its own keyboard handler. The
  Esc and its break code never reach the program.
- **Snapshot:** on each INT 21h 4B00h/4B01h, the kernel saves the IVT, the virtual PIC, the virtual A20 and
  RTC registers A/B, and the 8042 command byte, keyed by the parent's PSP (8 levels).
- **Kill:** at the next return to the VM, unless InDOS is set (then up to 2 s later). The current PSP comes from
  the SDA, and its parent's snapshot is put back: the IVT, the virtual PIC (keeping requests, nothing in
  service), A20, RTC A/B, the 8042 command byte and PIT channel 0 at mode 2, count FFFFh (§8.2). A keyboard
  byte the program left unread is raised again. The VM then runs `GLOS.EXE`'s kill stub (INT 21h 4CFFh) as
  that program, so DOS ends it normally and its parent sees exit code FFh.
- `GLOS-KILL psp=… at=cs:ip ticks=…`, or `GLOS-KILL none` when only `GLOS.EXE` (or nothing that GLOS saw
  start) is running.

### 9.7 Direct mode

- A program profile with `direct=1` runs its session at IOPL 3. V86 code still sees the I/O bitmap;
  protected-mode code at CPL 3 ≤ IOPL bypasses it.
- IOPL is constant for a session's lifetime, because 86Box's dynarec compiles PUSHF for one IOPL [86Box].
- In direct mode, agent liveness isn't guaranteed while the program has interrupts off. The documentation says
  so.

## 10. Virtual devices

| Ports | System VM / GLOS apps | Exclusive session | Notes |
|---|---|---|---|
| 20h/21h, A0h/A1h (PIC) | Trapped: virtual 8259 pair | Trapped | Full ICW/OCW, OCW3 reads, poll, specific EOI, auto-EOI. Fast path for non-specific EOI. Double EOI tolerated (SDL, DJGPP's INT 75h handler, Quake's keyboard ISR [census]). |
| 4D0h/4D1h (ELCR) | Trapped (shadow) | Trapped | |
| 40h–43h (PIT) | System VM: passed through (§8.2). Apps: virtual (M6) | Passed through | |
| 61h | Passed through | Passed through | Speaker and gate for PIT channel 2 |
| 70h/71h (RTC/CMOS) | Trapped: virtual index; virtual registers A/B/C; NMI bit; time and CMOS bytes read from the chip | Trapped | GLOS owns the periodic interrupt; programs see their own virtual A/B/C. Screamer Rally and GTA write 70h/read 71h [census]. |
| 60h/64h (8042) | Trapped: full virtual 8042 | Trapped | Keyboard and AUX streams, command/ACK, LEDs, D1h (A20), FEh (reset → `GLOS-RESET-REQ`). Ctrl-Alt-Del and the kill hotkey are intercepted. The PS/2 mouse BIOS (INT 15h C2xxh, used by Quake/Q2) works through the AUX stream. |
| 92h (A20/fast reset) | Trapped | Trapped | A20 stays physically on; the wrap is emulated with page-table entries (§4) |
| CF8h–CFFh (PCI) | Trapped: address latch shadow; data passed through except to devices GLOS owns | Trapped | |
| CF9h | Trapped: reset request to the agent | Trapped | |
| 80h, E80h | Passed through | Passed through | POST code and the 86Box unit tester (the harness) |
| 3F8h–3FFh (COM1) | Passed through; output mirrored to the log ring | Passed through | Programs reprogram the UART (Xash, PrBoom, Fifth Wheel [census]) |
| 2F8h–2FFh (COM2) | Owned by the gdb stub when `/GDB` is given, otherwise passed through | Same | |
| GLOS-owned NIC | Trapped; reads return FFh, writes dropped; its PCI function reads as absent | Trapped | M3: an NE2000 (the RTL8029 on PCI, or ISA at 300h, 280h, 320h, 340h, 360h). Its IRQ is the kernel's: the VM never sees it. Refused (`GLOS-NET refuse`) under a packet driver or on a PCI IRQ line another function shares. |
| 3F8h (COM1 data) | Trapped for output, passed through | Same | M3: tells the kernel when a program is part-way through a line, so the kernel's lines wait for it (§18) |
| VGA 3B0h–3DFh, SB, GUS, DMA 00h–0Fh/80h–8Fh/C0h–DFh, 201h, F0h | Passed through | Passed through | Live 8237 count reads, GUS GF1/CODEC, UniVBE chipset probes and the IRQ13 acknowledge at F0h all need real hardware [census] |

**M2 status:**
- **Implemented:** the PIC, RTC, 8042, 92h, CF8h–CFFh and CF9h rows (`kernel/vm/vdev.c`, `vkbc.c`, `vpic.c`).
  The PIT is passed through and set to mode 2, count FFFFh when the VM starts.
- **Not yet:** ELCR (passed through), the COM1 mirror ring (M3, with the agent's log).
- **The 8042:** the kernel reads every byte the chip receives (IRQ 1, IRQ 12 and each tick) into one queue
  and hands them to the program one at a time; the next byte comes a tick after the last was read. The
  command byte's IRQ 1 enable and keyboard clock stay on in the chip whatever the program writes. A20 (D1h,
  DDh/DFh), the reset pulse and the self-tests are answered virtually. Other commands go to the chip.
- **The RTC:** registers A, B and C are virtual. The periodic flag follows the program's rate (at most once
  per kernel tick); AF and UF come from the chip. IRQ 8 is raised for the VM when IRQF rises. Writes to
  register B reach the chip except PIE, AIE and UIE.

**IRQ routing:**
- IRQ0 → the system VM's (or the exclusive session's) virtual IRQ0. Every IRQ passed to the VM masks its
  physical line until the program's EOI on the virtual PIC, so a level-triggered device can't storm.
- IRQ1/IRQ12 → the virtual 8042, which raises virtual IRQ1/IRQ12.
- IRQ8 → the kernel clock (§8), plus a virtual IRQ8 only if the program enabled a virtual periodic, alarm or
  update interrupt.
- The NIC IRQ → GLOS.
- Every other IRQ → the current session's virtual PIC.

## 11. Sessions

| Session | What runs | Ports |
|---|---|---|
| **System VM** | DOS, the BIOS, TSRs and COMMAND.COM | Table above |
| **Exclusive session** | A program launched full-screen (`glos run`, or any DOS command over SSH), with the DPMI context(s) it creates | Most hardware passed through |

- **Starting an exclusive session:**
  - GLOS apps are paused, and the display state is saved (VGA registers, DAC shadow, text-mode contents).
  - The program's profile applies: environment, `direct`, memory limit.
- **Ending it:**
  - restore the display mode and palette, PIC masks, PIT mode 2/FFFFh, the RTC's virtual registers and the
    keyboard LEDs;
  - stop any Sound Blaster DMA (8237 masks; DSP reset) and unhook vectors the program left behind (IVT
    snapshot);
  - re-sync DOS time from the RTC.
- **Kill:**
  - the same restoration, then the program's PSP chain is terminated through a forced `INT 21h 4C7Fh`
    executed from a resident-stub trampoline at a safe point;
  - reported as `GLOS-KILL psp=… reason=…`.
- **Switching away** is supported only for text-mode programs (save B800h/B000h, cursor and mode). A
  graphics-mode session runs until it exits or is killed.

### 11.1 As built (M4e E1; kernel/vm/session.c)

- **What a session is.** One top-level DOS program, with everything it EXECs. GLOS sees the EXEC as an
  INT 21h 4B00h from V86 code by a *session parent*:
  - the resident stub, for agent jobs (each job's `COMMAND /C`) and `/RUN`;
  - with GLOS as the shell, the console COMMAND.COM (or AUTOEXEC.BAT's): a PSP other than the stub's whose
    parent is the stub, or itself (FreeCOM makes itself its own parent).

  Only one session is open at a time; EXECs inside it belong to it. Load-only EXECs (4B01h/03h) start none,
  and nor does an EXEC of a file that isn't there (a nested INT 21h 4300h says so first): the agent looks for
  a program by trying each place along PATH.
- **The snapshot at the EXEC:** the BIOS video mode (0449h), the lock bits of the keyboard flags (0417h bits
  4-6), the virtual PIC's two masks, the virtual RTC's A and B, the 8042's command byte, and the whole IVT.
- **The end** comes at the first of:
  - the parent's INT 21h 4Dh (the stub and COMMAND.COM both ask for the exit code);
  - the parent's next EXEC (the last one failed to load);
  - the stub's NEXT call.
- **What the end puts back** (`end()`):
  - the video mode, by INT 10h 00h, when it differs (the palette comes with it);
  - PIT channel 0 at mode 2, count FFFFh (§8);
  - the virtual PIC masks, the virtual RTC's A and B (and so the periodic rate), the 8042 command byte and the
    lock bits;
  - Sound Blaster DMA stopped: 8237 channels 1, 3, 5, 6 and 7 masked, and the DSP reset at BLASTER's `A`
    address (the loader puts it in bootinfo's `sb_port`);
  - every vector the session changed that now points into a free DOS block, or that still holds the value it
    had when a program in the session ended pointing into that program's own memory (INT 20h, INT 21h 4Ch/00h
    from V86 code). The second rule matters with GLOS as the shell: COMMAND.COM may have put something in the
    freed memory before it asks for the exit code. A TSR's hooks stay: its memory is still allocated;
  - the BIOS tick count from the RTC (INT 1Ah 02h, then 01h). A program that sped the PIT up has run the DOS
    clock fast; it ends within a second of the RTC.

  The BIOS calls run nested (§15) on the stub's spare stack (the kill stub's, `kill_sp`), with no DPMI client
  needed.
- **Not yet:** pausing GLOS apps (M6), the display state beyond the mode, profiles (E2), direct mode (E3),
  kills ending a session (E5) and switching away (M6).
- **Tests.** `jobs.py sess` (`make loopa-sess`). SESSTEST leaves each of these changed natively; under
  `GLOS /RUN`, as the shell and as an agent job, none is (VECCHK, VMODE, TIMECHK).

## 12. DPMI context model [fixed]

### 12.1 Contexts

- A **context** is created when a program in the system VM calls the mode-switch entry and no context exists
  for its program tree.
- A context has:
  - a page directory, sharing PDE 0 and the kernel PDEs;
  - an LDT (8192 entries available, growing on demand; only the host writes it);
  - a virtual IDT;
  - a memory-block list, an RMCB table, exception handlers, and a 16 KB locked host stack;
  - the **client bitness** (16 or 32), a field from day one.
- **Child programs** EXECed by a client share their parent's context, as in CWSDPMI and HDPMI's default
  ([DPMI deep dive]; DOSBench's DBMENU (DJGPP) started BENCHG (DOS/4GW) and BENCHGL [census]; since DOSBench's
  D9 redesign it writes a batch file and exits instead, and DJGPP's `system()` is the case in our programs).
- A child's mode switch pushes a client level. On the child's 4Ch, GLOS frees the blocks, selectors, RMCBs and
  handlers allocated at that level, and restores PSP:2Ch.

### 12.1a As built in M4a (`kernel/dpmi/`)

- **One context at a time.** A child program's own mode switch fails with 8011h until M4c.
- **The VM thread runs the client.** It alternates between V86 code and the client at ring 3, on one kernel
  stack. A ring-3 entry pushes 16 bytes fewer than a V86 entry, so ESP0 sits 16 bytes lower while the client
  runs. The trapframe then lies at the same address in either mode, and the mode switch, raw switches and the end
  are edits of that one frame (`kernel/arch/cpu.c`).
- **Nested real-mode calls** (0300h–0302h, reflected INTs, IRQs) build a V86 frame below the caller's kernel
  stack and enter it. The stub's nest ARPL returns longjmp-style (`kernel/dpmi/rmcall.c`). A program that ends
  inside one leaves its nested levels behind: the V86 frame moves to the top and carries on.
- **The host's real-mode stack** is the block of 1687h's SI paragraphs (80h: 2 KB) that the client allocates
  and passes in ES at the mode switch, 512 bytes per nesting level.
- **The trampoline page** (SEL_TRAMP, at 3FF000h in PDE 0's shared table) holds HLTs: the default handler for
  each vector (offset = vector), 0306h's protected-to-real switch, 0305h's save/restore (nothing to save), and
  the "GLOS" vendor entry. A HLT at ring 3 is a #GP, so the host sees which one.
- **Software interrupts:**
  - 20h–4Fh and 60h–FFh arrive through DPL-3 gates; 0–1Fh and 50h–5Fh arrive as #GP, decoded from the error
    code and the INT instruction.
  - A vector the client hooked (0205h) gets an interrupt frame on the client's stack. The virtual IF stays as
    it was: the handler's IRET couldn't give it back at IOPL 0 (M4b found M4a clearing it).
  - Otherwise the host's default handles it: INT 31h is the API; INT 21h 4Ch ends the client; INT 2Fh 1686h
    returns AX=0; the rest are reflected to real mode with the general registers and arithmetic flags (DS and ES
    there are the host's segment).
- **IRQs while the client runs** go through the virtual PIC as in V86 mode. On the way back to ring 3, with the
  virtual IF on, each runs its real-mode handler, nested (M4b: the client's protected-mode handler first, §12.1b).
- **Ring-3 emulation:** CLI, STI, HLT and IN/OUT to trapped ports are emulated as in V86 mode (32-bit code
  decoded with 32-bit defaults). PUSHF/POPF can't see or change IF at IOPL 0 (0900h–0902h can). Other
  exceptions go to the client's handlers (M4b, §12.1b).
- **Before IRET to ring 3,** the frame's segment registers are checked: an unloadable DS, ES, FS or GS becomes
  0, and a bad CS or SS ends the client. So the IRET never faults in ring 0.
- **espfix** (§6.3) on every return to a 16-bit stack: trap_dispatch moves selector 30h's base so the IRET frame
  is addressed with the client's ESP[31:16]. DPMIMINI checks it.
- **The end:** INT 21h 4Ch in protected mode frees the context and carries on in V86 mode at the stub's INT 21h,
  as the program, with AX as it was. A program that ends from real mode (after a raw switch, or inside a nested
  call) frees the context when its 4Ch reaches DOS.
- **`/DPMITRACE`** logs every INT 31h call and its result (`GLOS-DPMI call`).

### 12.1b As built in M4b (`kernel/dpmi/deliver.c`, `kernel/dbg/crash.c`)

- **Entries.** Every handler the host calls in the client (an IRQ, an INT passed up, an exception, a real-mode
  callback) is an entry: the context keeps what it interrupted, V86 or protected mode, and the handler starts
  with SEL_TRAMP:TR_RET+n as its return address (n: the entry's index, up to 32). Its IRET, or an exception
  handler's RETF, comes back to the host there, and the host carries on from the entry, the virtual IF
  included (§14.3). Returning to an older entry drops the newer ones, whose handlers never returned.
- **One frame, both modes.** A handler for something that happened in V86 mode turns the VM thread's V86 frame
  into a protected-mode one; the entry's return turns it back. Real-mode calls the handler makes nest below as
  in M4a, and remember the protected-mode frame that called (`rm_pm_caller`).
- **The locked stack** (§14.2) is 16 KB at BFFF0000h, committed at the mode switch, with a selector of the
  client's bitness. A handler starts at its top when no entry holds it; otherwise on the stack the interrupted
  protected-mode code was using, or, from V86 mode, the one the real-mode call came from (HDPMI's rule). A
  handler that has moved to a stack of its own (DJGPP's IRET wrappers) so never has the locked stack's frames
  overwritten.
- **IRQs** go to the client's handler when it hooked the vector (0205h), from protected or V86 mode; the end
  of its chain, the host's TR_VEC handler, reflects to real mode. Unhooked ones run their real-mode handlers as
  in M4a. A chain that ends at the host's handler returns through TR_RET without another trip to ring 3.
- **INT 1Ch, 23h and 24h** in V86 mode trap (also with VME) and go to the client's handler when it hooked them;
  its general registers and arithmetic flags come back down (INT 24h's AL). **An INT 23h no handler takes is
  ignored** while a client lives, as CWSDPMI does: otherwise DOS's Ctrl-C abort ends a DJGPP program in the
  middle of its own SIGINT path, which restores what it changed (djtst205's HANG and CTRLC; HDPMI32i lets DOS
  abort, and its next client crashes).
- **Every end is seen.** The mode switch points the client's terminate address (PSP:0Ah) at the stub's term
  ARPL. A DOS abort that bypassed INT 21h 4Ch (a critical-error "Abort") lands there: the context goes, the
  vectors and devices are restored from the EXEC snapshot as on a kill (`GLOS-DPMI exit terminated`), nested
  levels are dropped, and DOS carries on at the old address.
- **Exceptions** go to the client's 0203h or 0212h handler with the 0.9 frame, and for a 32-bit client the 1.0
  frame above it at +20h (laid out as HDPMI32i lays it out: DPMICONF's `exc-frame10` passes on both). The RETF
  resumes from the frame as the handler left it (CS:EIP, EFLAGS, SS:ESP; the 1.0 frame's segment registers
  for a 0212h handler), with the general registers as the handler left them. Five nested ones end the client.
- **No handler** (or a chain to 0202h's TR_EXC default): 1–4 go on as the interrupt, through the client's PM
  handler or to real mode; 0, 5 and 7 likewise if the client hooked that interrupt; the rest end the client
  with a crash report. A fault reflected to real mode would only come back to the same instruction.
- **Real-mode callbacks** (0303h): the stub's ARPL enters the client's handler with DS:(E)SI the real-mode
  SS:SP (a selector per callback) and ES:(E)DI its register structure, filled in; the IRET resumes V86 code
  from the structure.
- **The pass-up guard** (§14.5): 0201h and 0205h remember the real-mode vector an RMCB or a PM hook took over;
  a reflection from the host's handler that would reach the client's own RMCB goes there instead.
- **0506h/0507h:** page attributes on 0501h blocks. Uncommitted pages free their frames; recommitted ones come
  back zeroed (the baselines keep the old contents; DJGPP's null page doesn't care). DJGPP's crt0 uncommits
  its null page, so a NULL write is a page fault.
- **The FPU** keeps CR0.NE clear (§14.7): errors arrive as IRQ13, through the virtual PIC to the client's INT
  75h handler first, as on CWSDPMI. DJGPP's handler writes port F0h, which is passed through.
- **Crash reports** (§19): `GLOS-CRASH` lines and `C:\GLOS\CRASH\CRASHnnn.TXT`, then the kill path (exit code
  FFh, vectors and devices restored). While the report is written the client gets no more handlers.
- **Software INTs to the client's handlers keep the virtual IF** (above), and **PUSHF still shows IF set**:
  DJGPP's `disable()` returns 1 even when the virtual IF is off. HDPMI32i returns to the client with the real IF
  clear after CLI, which PUSHF then shows; GLOS doesn't, so that `cli; jmp $` can't stop the agent (PRD D20).
  djtst205's ENABLE is the one test that notices; direct mode (M4e) runs it at IOPL 3.

### 12.1c As built in M4c (`kernel/dpmi/level.c`, `kernel/vm/v86.c`)

- **Client levels.** A program a client EXECs that switches to protected mode itself pushes a level in the
  parent's context (up to 6), as under CWSDPMI and HDPMI32i: one address space, one LDT, one virtual IDT.
  - Everything a level makes is tagged with its number: LDT entries, 0501h/0504h blocks, DOS blocks' selectors,
    RMCBs and watchpoints. Its end frees exactly those, puts back the parent's interrupt and exception vectors
    (a level starts with copies of them), drops the entries its handlers left, and restores PSP:2Ch.
  - Per level: the PSP and its selector, the environment selector, the host's real-mode stack, the first-block
    floor (§12.3) and the terminate address.
  - The parent waits inside its EXEC, a nested real-mode call, so the child runs at that depth and ends there:
    what nested deeper is unwound, and the parent's EXEC returns as on a plain DOS.
  - In M4c a child of the other bitness (a 16-bit child of a 32-bit client, or the reverse) failed with 8011h;
    M4d takes it as a level like any other (§12.1d). Only the first client gets the locked stack; the levels
    share it.
  - DPMICONF's `nest` (a child that leaves its vector hooked, and one that faults) passes on GLOS, HDPMI32i and
    CWSDPMI. CWSDPMI keeps a child's hook after the child ends; GLOS and HDPMI32i put the parent's back.
- **The terminate address** (PSP:0Ah) of each level points at the stub's term ARPL, and a pending-end stack
  matches each DOS end to its level, so an abort that bypassed 4Ch (§12.1b) ends the right level.
- **INT 2Fh in V86 mode is a chain.** GLOS hooks INT 2Fh at the bottom of the IVT chain (the stub's int2f ARPL)
  instead of trapping the vector, so a TSR or debugger loaded under GLOS can hook 1687h itself: ecm's lDebugX
  does, to follow its program into protected mode ("DPMI entry cannot be hooked!" otherwise). GLOS answers
  1687h, 4300h/4310h (§16), 1680h, 1600h and 160Ah, and passes the rest to the previous vector. At the end it
  unhooks, with `GLOS-WARN int2f-hooked-over` if something hooked above it and stayed.
- **INT 2Fh 1680h** in either mode yields the VM thread to the kernel's (`uclock`, `usleep`), and leaves AL at
  80h as plain DOS does (§13).
- **INT 41h** from protected mode does nothing: it is the Windows debugger interface, and HDPMI's I310508A calls
  it. Reflected, it would jump through the BIOS's disk-parameter pointer in the IVT (the test hung).
- **DPMI 1.0 functions** (§13): 0401h, 0504h/0505h, 0508h/0509h, 050Bh, 0B00h–0B03h; 0E01h honoured per client.
  - The 1.0 exception frame's handlers return through their own trampoline offsets (TR_RET10), so a RETF from
    a 0212h handler takes the 1.0 frame, from a 0203h handler the 0.9 one. Each is read where that handler's
    RETF leaves ESP.
  - The 1.0 frame's PTE field for a page fault is the faulting page's PTE attributes. An uncommitted page in a
    client block reads as user and writable, though not present (HDPMI32i's suite checks it).
- **The FPU per client** (0E01h): its MP and EM bits are in CR0 while it runs, and DOS's while V86 code runs.
  EM set means the client emulates: ESC instructions raise exception 7, to its handler. 0E00h reports them,
  with the FPU present and the CPU type.
- **Selector 0040h** is ring-3 BIOS data (§3.1), and a data register may hold it with RPL 0, which the
  check before IRET (§12.1a) now allows; CS and SS still need RPL 3, and SS writable data.
- **0301h keeps the caller's IF:** [DPMI0.9] gives the register structure's FLAGS no part in a far call
  (0300h/0302h push them and clear IF/TF), and clients leave them zero. M4a loaded IF from them, so the
  procedure ran with interrupts off (HDPMI's RAWJMP6). With VME the nested V86 frame carries the virtual IF in
  EFLAGS.VIF itself: its entry (`nest_enter`) skips `vm_return()`, which sets VIF on every other way into V86
  mode, and the first trap from the procedure would otherwise read IF back as clear.
- **A stray #DB in ring 0 is dropped** (§20): 86Box keeps a single-step trap pending across an instruction
  that faults into the kernel and delivers it in the handler. DR6 is 0 then; GLOS logs `GLOS-CPU stray-db`
  once and carries on. A #DB that hits a client's watchpoint in ring 0 is recorded for 0B02h.
- **Test inputs** (THIRD_PARTY.md): ecm's dpmimini and lDebugX (`make loopa-ecm`) give the same output under
  GLOS and HDPMI32i but for selectors and addresses. HDPMI's own regression suite (`make loopa-hdpmireg`) runs
  each test under HDPMI32i and GLOS. A test passes when the exit code and output agree, or differ only in
  selector numbers. Each remaining difference is listed with its reason in `HR_KNOWN`
  (`tests/loopa/jobs.py`): HDPMI's crash dump on the program's output, its INT 21h translation API,
  IOPL 0's PUSHF (D20), 0305h's empty state, and HDPMI refusing a nested client. Any
  other difference fails the job.

### 12.1d As built in M4d (`kernel/dpmi/dosx.c`; 16-bit clients)

- **Every handler keeps its own client's bitness** (`struct farptr.b32`, set by 0203h/0212h, 0205h and 0303h).
  Its frames follow it: the IRET frame of an IRQ or INT passed up, an exception's 0.9 frame, a callback's, and
  the frame of a software INT to it. So do the reads when it returns (an exception handler's RETF, a callback's
  structure pointer, the chain's end at the host's own handler).
  - The locked stack has a selector of each bitness, the second made when a handler of the other bitness first
    needs it. A handler that interrupts code of the other bitness on the locked stack carries on there through
    its own alias. From a stack of the client's own of the other bitness there is nowhere safe to go, and the
    client ends (`no-stack`).
- **Levels of either bitness:** a 16-bit child of a 32-bit client, or the reverse, is a level like any other
  (M4c refused it). Its handlers and frames are its own; the parent's, inherited, keep theirs. DPMICONF-16's
  `glos-nest-32in16` (DPMICONF-32 as its child) and DPMICONF-32's `glos-nest-16in32` check both ways.
- **The 1.0 exception frame for 16-bit handlers** (0210h/0212h), laid out as HDPMI16 lays it out: the 0.9 part
  in words at 0 (IP:CS of the host, error code, IP, CS, FLAGS, SP, SS), padded to 20h; the 1.0 part in dwords
  at 20h as for 32-bit handlers, except that its return address is IP:CS followed by a zero dword. The handler
  returns with a 16-bit RETF through either return address.
- **INT 2Fh 168Ah** in protected mode ([DPMI1.0]) gives "GLOS" (as 0A00h) and **"MS-DOS"**, Windows'
  extension. Its function 0100h returns a selector for the LDT. Borland's RTM won't start without it, and
  HDPMI16 has it. GLOS's selector shows the LDT's own frames **read-only**, at BFFD0000h: a client that could
  write its LDT could build a call gate into ring 0. The LDT is page-aligned so that its frames hold nothing
  else. RTM only reads it; a write through it is a page fault for the client, logged once as
  `GLOS-WARN ldt-alias-write`.
- **A client that goes resident** (INT 21h 31h from its program, in either mode, at the first level) keeps its
  context: the terminate address carries on without ending it or restoring vectors (`GLOS-DPMI resident`).
  The context ends when the program it returned to ends (`GLOS-DPMI exit resident`), or when a client of it
  ends with 4Ch from protected mode. Borland's RTM works this way: TPX.EXE runs RTM.EXE, which switches to
  protected mode, sets itself up and goes resident; TPX then reaches it through its INT 2Fh, and RTM
  raw-switches back into its context.
- **DOS API translation for 16-bit clients** (`dosx.c`), as Windows' DOSX gives its 16-bit clients and HDPMI16
  copies; RTM relies on it. 32-bit clients bring their own extender (DJGPP, DOS/4GW) and keep plain
  reflection, as under CWSDPMI. For a 16-bit client, GLOS's own INT 21h handler:
  - turns returned segments into selectors (0002h's): 34h, 52h, 1Bh/1Ch/1Fh/32h, 5D06h;
  - gives and takes the PSP as a selector: 50h, 51h, 62h (the client's own PSP selector for its PSP);
  - keeps the client's DTA (1Ah, 2Fh) and copies 4Eh/4Fh's results into it;
  - reads and sets protected-mode vectors for 25h/35h (as 0205h/0204h), and allocates DOS memory as
    0100h–0102h for 48h/49h/4Ah;
  - copies through an 8 KB block of DOS memory (taken the first time) the paths of 39h–3Dh, 41h, 43h, 4Eh, 56h,
    5Ah, 5Bh and 6Ch, the strings and buffers of 09h, 3Fh and 40h (in 7.5 KB pieces), 47h's directory, 38h's
    country data, and 29h's name and FCB.
  - Other functions with pointers are logged once (`GLOS-DPMI-UNIMPL dosx-21-NN`) and reflected as they are:
    EXEC (4Bh) among them, which RTM does itself.
- **Test inputs:** DPMICONF-16 (`tests/dos/dpmi16/`, Open Watcom C and WASM; the same check names as DPMICONF-32,
  prefixed `dpmi16-`) passes on HDPMI16, HDPMI16i and GLOS on all six profile and boot combinations; its
  `dosx-*` checks cover the translation. HDPMI16 returns ESP[31:16] as 0 after a trap on a 16-bit stack, GLOS
  the client's own (espfix); either is accepted. TPX.EXE (Turbo Pascal 7's IDE, `$(BORLAND_DIR)`, copied into
  the cache by `make m4d-inputs`) opens `tests/borland/hello.pas`, compiles and runs it (Ctrl-F9), and leaves
  (Alt-X), without GLOS (RTM loads DPMI16BI.OVL as the host) and under it.
- **Found on the way:** GLOS.EXE's `/RUN` gave `_searchenv()` an 80-byte buffer where it fills `_MAX_PATH`
  bytes, and a bare program name in the current directory smashed the loader's stack (fixed before M4d).

### 12.2 Initial state after the mode switch ([DPMI0.9 §4])

| Register | Value |
|---|---|
| CS | 16-bit, base = real-mode CS, limit FFFFh |
| DS, SS | 64 KB, Big bit set for 32-bit clients. DS==SS on entry gives one shared selector. |
| ES | PSP selector (limit FFh per spec; CWSDPMI uses FFFFh, and GLOS matches CWSDPMI). PSP:2Ch is replaced by an environment selector. |
| FS, GS | 0 |
| ESP | High word zeroed for 32-bit clients |
| EFLAGS | IF virtual (IOPL 0, or 3 in direct mode) |

### 12.3 Memory blocks

- 0501h blocks are allocated **ascending**, starting at 4 MB and always above the context's first block.
  DJGPP's sbrk raises its segment limits to cover new blocks and falls back to a near-4 GB wrap if a block
  appears below the first [census].
- 0503h resizes in place when it can, otherwise moves the block to the top of the allocated range (DJGPP's
  UNIX_SBRK path brackets it with 0900h/0901h).
- 0500h reports accurate figures. GLQuake and Q2 allocate everything 0500h reports and need at least 16 MB
  [census].
- The M0 survey showed why this matters. Under HDPMI (either IOPL), GLQuake's UNIX_SBRK heap couldn't grow,
  and its first texture failed with GL_OUT_OF_MEMORY; under CWSDPMI it runs. GLOS follows CWSDPMI here, and
  DPMICONF tests growing a UNIX_SBRK heap past 32 MB.

### 12.4 Physical mappings

- 0800h returns **linear = physical** when the physical range is inside E000_0000–FFBF_FFFF (PCI BARs). The
  mapping is per context.
- Retail DOS/4GW games were only ever tested under DOS/4GW with no external DPMI host, where linear equals
  physical.
- Other ranges are mapped into the user region.

## 13. INT 31h function table

Statuses:

| Status | Meaning |
|---|---|
| M4a…M4e | Implemented in that sub-milestone |
| fail | Returns CF=1 with the documented error |
| nop | Succeeds and does nothing |
| log | Logged as `GLOS-DPMI-UNIMPL` and fails with 8001h until a program needs it |

"Who" names the programs that use the function [census].

| Fn | Function | Status | Who / notes | Baseline |
|---|---|---|---|---|
| 0000h | Allocate LDT descriptors | M4a | DJGPP stub, DOS/4GW. CX=0 fails with 8021h ([DPMI1.0]; HDPMI32i leaves AX=0). | CWSDPMI, HDPMI32i |
| 0001h | Free descriptor | M4a | DJGPP exit frees the loaded DS. GLOS zeroes any segment register holding it ([DPMI1.0]; CWSDPMI does the same). | CWSDPMI |
| 0002h | Segment to descriptor | M4a | GLQuake | CWSDPMI |
| 0003h | Selector increment | M4a | 8 | |
| 0004h/0005h | Lock/unlock selector (undocumented) | nop | | |
| 0006h/0007h | Get/set base | M4a | GTA calls 0007h 13 times | DOS/4GW |
| 0008h | Set limit | M4a | Fat DS (FFFFFFFFh, checked with LSL). **Callable from IRQ handlers** (DJGPP's Ctrl-C trick sets DS limit 0FFFh inside IRQ1 and restores it in the exception handler). Segment caches reload on return. | CWSDPMI |
| 0009h | Set access rights | M4a | C09Bh/C093h \| DPL<<5; validated | CWSDPMI |
| 000Ah | Create alias | M4a | `__djgpp_ds_alias` | CWSDPMI |
| 000Bh/000Ch | Get/set descriptor | M4a, M4c | DOS/4GW clears the Big bit on SS/DS (espfix, §6.3); validated | DOS/4GW |
| 000Dh | Allocate specific descriptor | M4a | Selectors 04h–7Ch reserved | |
| 000Eh/000Fh | Get/set multiple (1.0) | log | | |
| 0100h–0102h | DOS memory | M4a | Under the DOS lock. **On failure AX=0008h and BX=largest block**: the DJGPP stub relies on it. A 32-bit client gets one selector whose limit is the whole block, so 0102h can grow it in place (M4c, HDPMI's I310102); a 16-bit client one per 64 KB ([DPMI0.9]), and 0102h fails with 8011h before DOS resizes if it would need more. | CWSDPMI |
| 0200h/0201h | Get/set real-mode vector | M4a | DJGPP installs its INT 1Bh RMCB here | |
| 0202h/0203h | Get/set exception handler | M4b | DJGPP: 0–11h; MGA-Glide OW: 00h, 06h, 0Dh, 0Eh. The default is SEL_TRAMP:TR_EXC+n; setting it back restores the host's own. | CWSDPMI |
| 0204h/0205h | Get/set PM vector | M4a (vectors), M4b (IRQ delivery) | INT 8/9/1Bh/23h/24h/75h, IRQ3/4, INT 21h (DOS/4GW, MGA-Glide exit hook) | CWSDPMI, DOS/4GW |
| 0210h/0212h | Get/set extended PM exception handler (1.0) | M4b, M4d | The 1.0 frame at +20h, as HDPMI32i's; for a 16-bit handler as HDPMI16's (§12.1d). | HDPMI32i, HDPMI16 |
| 0211h/0213h | Get/set extended real-mode exception handler (1.0) | log | Real-mode exceptions go to the IVT, as on a real-mode CPU | |
| 0300h | Simulate real-mode interrupt | M4a | SS:SP=0 means the host stack; CX words copied; IF/TF clear | CWSDPMI |
| 0301h/0302h | Call real-mode far / IRET procedure | M4a | GLQuake IPX entry; SDL VBE bank switch. 0301h keeps the caller's IF (M4c, §12.1c); 0302h clears IF/TF. | HDPMI32i |
| 0303h/0304h | Allocate/free real-mode callback | M4b | **Every DJGPP program** (INT 1Bh); DOS/4GW. 16 per context (ARPLs in the stub), each with a selector for the real-mode stack. ES:EDI returned unchanged. A freed one returns at once. | CWSDPMI |
| 0305h/0306h | State save / raw switch addresses | **M4a** | **DOS/4GW needs it to start.** The "real-mode" side is a V86 trap stub. The state size is 0: nothing to save (HDPMI32i: 1Ch). | DOS/4GW, HDPMI32i |
| 0400h | Version | M4a | 0.90, BX bit 0=32-bit, bit 1=0 (reflection in V86); **DH=08h, DL=70h** (or the virtual PIC's ICW2) | CWSDPMI |
| 0401h | Capabilities (1.0) | M4c | AX=002Fh, as HDPMI32i: page accessed/dirty, exceptions restartable, device and conventional-memory mapping, write-protect for clients. Not demand zero-fill: an uncommitted page faults. The buffer: version 0.4, "GLOS". | HDPMI32i |
| 0500h | Free memory info | M4a | Accurate; programs allocate all of it | CWSDPMI |
| 0501h–0503h | Linear blocks | M4a | Ascending (§12.3) | CWSDPMI |
| 0504h/0505h | 1.0 linear memory | M4c | 0504h at a given address (EBX, page aligned) or anywhere; committed or not (EDX bit 0). 0505h resizes; EDX bit 1 rebases the selectors listed at ES:EBX when the block moves. | HDPMI32i |
| 0506h/0507h | Get/set page attributes | M4b | DJGPP crt0 uncommits page 0 (null trap) unless NULLOK; DOS/4GW reads them | CWSDPMI, HDPMI32i |
| 0508h/0509h | Map device / conventional memory (1.0) | M4c | Pages of a 0504h block onto physical memory (0508h) or the first megabyte (0509h). A mapped page is page type 2 for 0506h; 0507h's commit replaces the mapping (HDPMI's I310508A). Mapped frames are never freed. | HDPMI32i |
| 050Bh | Memory information (1.0) | M4c | The 0500h figures in 1.0's layout, laid out as HDPMI32i's (nothing pages, so physical = virtual) | HDPMI32i |
| 0600h–0603h | Lock/unlock | nop (recorded) | SDL, Quake, LOCK_MEMORY | |
| 0604h | Page size | M4a | 4096 | |
| 0702h/0703h | Discardable | nop | DOS/4GW | |
| 0800h/0801h | Physical mapping | M4a | Matrox BARs, VBE LFB; linear = physical (§12.4) | DOS/4GW |
| 0900h–0902h | Virtual IF | M4a | Return the old state in AL | |
| 0A00h | Vendor API | M4a | "GLOS" → entry point and API version (PRD D23). **Any other string → CF=1, AX=8001h** (DOS/4GW probes "RSI CLIENT 0.9"/"RATIONAL DOS/4G"). | |
| 0B00h–0B03h | Watchpoints | M4c | DR0–DR3 (the gdb stub uses software breakpoints). A hit is exception 1 to the client and sets 0B02h's bit; returning to an execute watchpoint that fired sets RF once (kept across the client's exception handler). Freed with the level that set it. DPMICONF's `watch` passes on CWSDPMI, HDPMI32i and GLOS since 86Box patch 0115 (§20). | HDPMI32i |
| 0C00h/0C01h | TSR services (1.0) | log | | |
| 0D00h–0D03h | Shared memory (1.0) | fail (8001h) | DOS/4GW probes 0D00h | |
| 0E00h/0E01h | Coprocessor status / emulation | M4a, M4c | DJGPP: 0E01h BX=1; without an FPU, BX=3 and EMU387. 0E01h sets the client's MP and EM, in CR0 while it runs (M4c): EM means the client emulates, and gets exception 7. 0E00h reports them, the FPU present and the CPU type. | CWSDPMI (ignores 0E00h), HDPMI32i |

**INT 2Fh** (handled by the host in both modes):

| Call | Behaviour |
|---|---|
| 1687h | The DPMI host, with SI = private paragraphs |
| 1686h | AX=0 in protected mode |
| 168Ah | 1.0 vendor API in protected mode: "GLOS" as 0A00h, and Windows' "MS-DOS" (0100h: a read-only selector for the LDT; §12.1d). Others: AL as it was |
| 1680h | **A real yield** in either mode (`uclock`'s start-up spin and `usleep` call it [census]). AL stays 80h ("not supported"), as on plain DOS: DJGPP's `uclock()` takes AL=0 for Windows 9x and waits for a BIOS tick, which never comes when it is first called with interrupts off (JOYTEST hung, M4c) |
| 1600h, 160Ah | **Never report Windows.** GLQuake aborts if Windows is reported [census]. 1600h returns AL=0; 160Ah leaves AX as it was. |
| 1681h/1682h | Reflected (no handler, as on the baselines) |
| 4300h/4310h | GLOS's XMS server (§16). 4309h (HIMEM's handle table) isn't provided. |
| Others | In V86 mode, GLOS's handler is at the bottom of the INT 2Fh chain and passes them on (§12.1c) |

**INT 21h from 16-bit clients** is translated (selectors for segments, buffers copied: §12.1d); from 32-bit
clients it is reflected as it is.

**INT 67h:** VCPI DE00h and EMS are absent, so VCPI probes fail as they do with no EMM loaded (PRD D25).

## 14. Interrupt and exception delivery

### 14.1 Hardware IRQs ([DPMI0.9 §2.4])

- IRQs go to the **protected-mode handler first**, even when they arrive while the system VM is in V86. The
  end of the protected-mode chain reflects the IRQ to the real-mode vector.
- Clients see IRQs at the vectors 0400h reported (08h–0Fh, 70h–77h), never at the physical 50h/58h.

### 14.2 The locked host stack

Handlers for IRQs, INT 1Ch/23h/24h passed up, exceptions and RMCBs run on the context's locked host stack, with
the virtual IF and TF clear. The stack isn't switched again when handlers nest. It is 16-bit for 16-bit clients.

### 14.3 The IRET trampoline

- At IOPL < CPL, POPF and IRET don't restore IF [DPMI0.9]. Handler frames therefore return through a ring-3
  trampoline (selector 4Bh) that traps and restores the virtual IF.
- DJGPP's timer and keyboard paths IRET without STI [DPMI deep dive].

### 14.4 Real-mode interrupts passed up

Only INT 1Ch, 23h and 24h raised in real mode are passed up to protected-mode handlers [DPMI0.9].

### 14.5 Automatic pass-up loops

If the IVT points at the client's own RMCB (DOS/4GW's automatic pass-up), reflection uses the real-mode vector
saved when the protected-mode handler was hooked, so the IRQ doesn't loop.

### 14.6 Exceptions

- The frame follows [DPMI0.9]: return CS:EIP, error code, CS:EIP, EFLAGS, SS:ESP. The 1.0 extended frame sits
  at +20h for 0210h handlers.
- **Edits to the frame are honoured** on RETF. DJGPP rewrites CS:EIP and SS:ESP.
- Unhandled exceptions:
  - 1–4 (traps) go on as the interrupt: the client's PM handler, else real mode;
  - 0, 5 and 7 (faults) likewise only if the client hooked that interrupt, since real mode would return to the
    same instruction;
  - the rest terminate the client with a crash report (§19).
  - After 5 nested exceptions the client is terminated (CWSDPMI's rule).

### 14.7 FPU

- **As built (M4b): CR0.NE clear**, the PC's way. FPU errors raise IRQ13 through FERR#; the line goes to the
  virtual PIC like any VM line, so a client's INT 75h handler gets it first (§14.1) and V86 code gets the BIOS's.
  Port F0h (the acknowledge, which asserts IGNNE#) is passed through. This is what DOS programs, DJGPP's INT 75h
  handler and CWSDPMI expect, and while the system VM is the FPU's only user nothing else is needed.
- **With the FPU's second user** (GLOS apps, M6; sessions, M4e): lazy switching (CR0.TS, #NM) and per-client
  state, including the control word (GLQuake changes FLDCW constantly [census]). NE=1 then reports an error as
  #MF in the context that caused it, and IRQ13 is synthesised for V86 code and for clients that hooked INT 75h
  (DJGPP writes port F0h and EOIs both PICs). The deep dive chose NE=1 from the start; M4b deferred it because,
  with one user, NE=0 gives the same behaviour without emulating IGNNE#.

## 15. Real-mode calls, callbacks and raw switch

- **0300h–0302h** run as nested V86 execution on the system VM thread (§9.5), under the DOS lock (§17).
- **0303h:** a callback entry is a breakpoint stub in the resident stub. It enters the client on the locked
  stack with DS:(E)SI = real-mode SS:SP and ES:(E)DI = the register structure ([DPMI0.9]).
- **0305h/0306h:**
  - The "real-mode" raw-switch entry is a V86 breakpoint stub. The protected-mode entry is a trampoline
    selector.
  - Register conventions follow [DPMI0.9]. After a switch, general registers are undefined and EBP is
    preserved.
  - Save and restore buffers hold the nested V86 level's state.

## 16. XMS and INT 15h

- **XMS 3.0 server** for the system VM through INT 2Fh 4310h, including 88h/89h/8Eh/8Fh and the HMA. In XMS
  mode GLOS takes over the driver's role for new requests and keeps the existing handles. `DOS=HIGH` (HMA in
  use by DOS) is respected.
- **M2:** blocks are contiguous runs of GLOS's frames, so a lock (0Ch) returns a physical address. GLOS keeps
  1 MB of its memory for itself. In XMS mode the driver's version and HMA state carry over, and its handles
  from before GLOS stay its own: a program that kept the driver's entry point still reaches it, and the
  driver's moves (INT 15h 87h in V86 mode) can't reach GLOS's memory. UMBs (10h–12h) aren't provided (80h).
  The function results and error codes match HIMEMX's (XMSTEST, M2 exit).
- **INT 15h:**
  - 87h (block move) is emulated, and refused (AH=02h) for any part of GLOS's memory.
  - 88h, E801h and E820h report no free extended memory (E820h: CF set).
  - 86h and 83h are sleeps.
  - 24xxh drives the virtual A20.
  - C2xxh is the PS/2 mouse BIOS, passed to the BIOS against the virtual 8042.

## 17. DOS server and agent shell

### 17.1 The DOS lock

- All V86 execution happens on the system VM thread (§9.5), so DOS and the BIOS are serialised.
- The lock is held across every reflected INT 21h, every 0300h–0302h call and every BIOS call.
- ClassiCube reflects INT 21h 2Ch every frame, so the lock path must be cheap [census].

### 17.2 The safe point for posted calls

A posted call (an agent file operation; later, DOS calls from GLOS apps) runs only when all of these hold:
- InDOS is 0 and the critical-error flag is clear;
- no INT 13h or INT 10h is in progress (GLOS hooks both and keeps a busy flag);
- the virtual PIC has no IRQ in service;
- the VM is at an instruction boundary with the virtual IF on.

DOS is entered through the INT 21h entry captured at load.

**As built in M3 (the DOS server, `kernel/dos/dos.c`):** the safe point is headless mode's idle stub, the one
place where nothing else is in DOS by construction.
- A request is up to four INT 21h calls, made in order until one returns with CF set, with bytes copied into
  a transfer buffer before the first call and out of it after the last.
- The kernel hands the stub the registers through NEXT's answer 3 (`stub_data.dregs`); the stub makes the call
  and asks again.
- The transfer buffer is a DOS block of up to 32 KB that the stub allocates on the first request (so it belongs
  to GLOS.EXE) and frees when the last user (an SFTP session) lets go: conventional memory only while in use.
- A running job holds requests back until it ends. The conditions above (InDOS, INT 13h/10h, IRQs in service)
  come with posted calls during programs, which `/RUN` and `/SHELL` need.

### 17.3 The agent shell (M3)

- **Headless** is GLOS.EXE without a mode option (or with `/AGENT`); `/RUN` and `/SHELL` are the other modes.
  The system VM's foreground program is then the resident stub, which asks the kernel what to run (NEXT).
  While there is nothing, NEXT says so and the stub halts (`STI; HLT`), so DOS keeps taking its interrupts and
  its clock runs. A posted command or `glos exit` ends the halt.
- An SSH exec that isn't a built-in `glos …` command becomes a **job** (`kernel/dos/agent.c`). Jobs run one at
  a time, in the order they came; four can wait.
  - A program (`NAME`, `NAME.COM` or `NAME.EXE`: the current directory, then each PATH directory, `.COM` before
    `.EXE`, as COMMAND.COM looks) is EXECed directly. Its exit code arrives exactly, which MS-DOS's
    `COMMAND /C` wouldn't pass on.
  - Anything else runs as `COMSPEC /C <command>`: internal commands, batch files, redirection and pipes, and
    names found nowhere (COMMAND.COM then reports them).
  - The exit status is INT 21h 4Dh's code; 126 when not even COMSPEC could be run.
- DOS is never re-entered behind a running program's back: the stub only EXECs from its own NEXT loop.
- `glos exit` (headless only, for now) leaves at the next idle NEXT, half a second after it replies.
- **Ending a job:** `glos kill`, or its client going away, kills it: every half second the program in front
  gets the kill of §9.6 (innermost first) until the job has ended, with exit status 255; its output is dropped.
- Not yet: stdin for jobs (the client's input is dropped), and DOS commands while `/RUN` or `/SHELL` keeps DOS
  busy (§17.2's safe points, item 9).
- In desktop mode (M6) the same stub hosts the launcher.

### 17.4 Output capture

- While a job runs, what its programs write to the console through DOS is copied into the job's two 16 KB
  rings, which the ssh thread empties into the channel:
  - INT 21h 02h, 06h and 09h (DOS writes them to handle 1), and 40h on any handle, when the handle's file is
    the console: its SFT entry (through the PSP's handle table and the List of Lists' SFT chain) has device
    information bits 7 (a character device) and 1 (console output). Handle 2 goes to SSH's stderr, the rest to
    stdout. Output redirected to a file isn't captured.
  - INT 29h only outside DOS (InDOS clear): DOS's console driver calls it for output already counted. With VME,
    INT 29h traps only while a job runs.
- A full ring holds the program until the ssh thread makes room (the client's window); once the client has
  gone, output is dropped.
- Text written straight to the screen isn't captured (`glos shot` shows it). 8.3 file names only (no LFN API
  in v1).

### 17.5 Critical errors and Ctrl-Break

- In exclusive sessions these follow the baseline: INT 24h fails (AL=3), and Ctrl-C at the DOS level is
  swallowed (CWSDPMI hooks real-mode INT 23h with IRET).
- For GLOS apps (M6), INT 24h becomes a dialog.

### 17.6 Keys and the SSH server (M3)

- **Files:** `KEYS\` beside `GLOS.EXE` holds `SEED.BIN` (entropy carried across boots), `HOSTKEY` (an OpenSSH
  ed25519 private key without a passphrase) and `AUTHKEYS` (`authorized_keys` lines; ed25519 only). The loader
  reads them into bootinfo; the kernel keeps its own copy and GLOS.EXE's is freed with the rest of its data.
  Without `HOSTKEY` there is no SSH server (`GLOS-SSH off`). Loop A uses the public test keys in `tests/keys/`.
- **Randomness** (`kernel/core/random.c`): every IRQ adds its timing (the TSC where there is one) to a small
  buffer; requests fold it and a counter into a SHA-512 pool, which keys ChaCha20 with fast key erasure. The
  seed file is mixed in at start; writing a fresh one back waits for the DOS server (item 9).
- **Crypto:** TinySSH's (CC0) X25519, Ed25519, SHA-2, ChaCha20 and Poly1305 in `third_party/tinyssh/`.
  Ed25519 signatures are hedged (randomised), so known-answer tests check RFC 8032's signatures by
  verification. `/SELFTEST` runs RFC 7748, 8032 and 8439 vectors in a bulk thread (`GLOS-CRYPTO`).
- **Protocol** (`kernel/ssh/ssh.c`, also built natively for `tests/host/sshd`): curve25519-sha256 with strict
  KEX, ssh-ed25519, chacha20-poly1305@openssh.com, re-keying, public-key authentication for user `glos`,
  `session` channels with `exec` and window flow control. Commands are built-in `glos …` ones until the agent
  shell (§17.3) runs DOS commands.
- **Threads:** lwIP's TCP runs in the net thread (urgent); the protocol runs in the ssh thread (bulk: a key
  exchange takes hundreds of milliseconds on a 486). Each connection has two single-writer rings between them,
  so neither waits on a lock. Four connections at once; a fifth is refused (`GLOS-SSH refuse reason=busy`).
- **Limits:** 60 s to log in; 20 authentication attempts per connection.
- **Built-in commands (M3):** `glos ver`, `glos echo …`, `glos shot`, `glos log`, `glos ps`, `glos kill`,
  `glos exit`; anything else starting `glos` gives status 127.
  - `glos shot`: the text-mode screen as a PNG on stdout (4-bit indexed, deflate's stored blocks;
    `kernel/lib/png.c`). It reads the visible page and the BIOS data area (mode, columns, rows, page start,
    character height) and draws each cell with the video BIOS's own 8x16, 8x14 or 8x8 font, which GLOS.EXE
    finds with INT 10h 1130h: the glyphs the card shows, and no font in GLOS. Cells are 8 pixels wide (VGA's
    9th column is left out); blinking text shows steadily. Graphics modes later (status 1 until then).
  - `glos log`: the last 16 KB of COM1: the kernel's lines and what programs wrote there (no CRs).
  - `glos ps`: the jobs, the DOS program in front (PSP and its arena name), and each thread.
- **SFTP (M3, `kernel/ssh/sftp.c`):** protocol version 3, which OpenSSH's `sftp` and `scp` speak, written for
  DOS (no permissions, links or owners, and 8.3 names) on the DOS server; headless only for now.
  - Paths: `/C/TEST/FILE.TXT` is `C:\TEST\FILE.TXT`; relative paths start at DOS's current directory when the
    session began; `.` and `..` are resolved before DOS sees them. A name that isn't 8.3 is refused, since DOS
    would silently truncate it.
  - Attributes: size, permissions (directories 0755, files 0644, read-only 0444) and times (DOS's local time,
    given as UTC). SETSTAT is accepted and ignored; links and extensions are unsupported.
  - Each session serves one request at a time. The window reopens as requests finish (`ssh_chan_hold`), and no
    new one starts while more than 128 KB of output waits, so a session holds at most a window of input.
  - Files a client leaves open are closed when its channel goes.

## 18. Logging

- Lines on COM1 have the form `GLOS-<TAG> key=value …`, one line per event, written synchronously so the last
  line survives a hang.
- **Whole lines only.** Programs in the system VM write to COM1 too (Loop A's HX tools, GLOS.EXE itself). While
  one is part-way through a line, the kernel holds its own lines and writes them when that line ends, or after
  100 ticks at the latest (M3). A panic is written at once.
- **No periodic output, ever:** Loop A detects hangs by serial silence.
- Tags:

| Tag | Emitted for |
|---|---|
| BOOT | Each loader step |
| REFUSE | A refusal |
| RING0 | Ring-0 entry |
| EXIT | Exit |
| WARN | e.g. `vif-stuck` |
| VM | The system VM: `run=`, `dos indos= sda= psp=`, `resident psp= stub= freed=`, `leave code= ticks= gp= int= irq= spurious=` |
| SCHED | At leave: context switches, contended ticks per class, and each thread's ticks |
| PCI | The number of functions found; each one GLOS claims |
| NET | The card GLOS took (or why not), DHCP's address, and counters at leave |
| LWIP | lwIP's own diagnostics and assertions |
| RANDOM | At start: the seed's length and whether there is a TSC |
| CRYPTO | `/SELFTEST`: the known-answer tests and two timings |
| SSH | `listen`, `off`, `connect`, `client version=`, `kex done strict=`, `auth ok`, `exec=`, `close why=`, `refuse` |
| AGENT | A job: `run seq= cmd=`, `done seq= code= via=` (the program, or `comspec`) |
| KILL | A kill (§9.6) |
| SESSION | A session (§11.1): `begin n= prog= parent= mode=`, `end n= prog= why=exit\|next-exec\|stub mode= [remode=1] vectors= ticks= t=` (`mode=` the mode it ended in, `vectors=` those put back, `ticks=` the BIOS tick count set from the RTC, `t=` its length in kernel ticks) |
| RESET-REQ | A reset request: `source=kbc`, `port92`, `cf9` or `cad` |
| DPMI | The DPMI host: `start bits= psp= cs= ds= ss=`, `exit code=` (`real-mode`, `killed`), `exit terminated restored=`, `bad-frame`, `rmcb-failed`, and with `/DPMITRACE` `call fn= ... if= -> cf= ax=` (`if=` the virtual IF at the call), `raw to=rm|pm`, `dosx ax= ... -> cf=`, `vendor name=`, `v86-int21` (a client's real-mode DOS calls), `exception-code` (the code bytes and registers at a fault),, `deliver irq=|passup=|exception=|rmcb= to= from= at= err= entries= lstack=` (the first four of each) and `int23`/`int24 from= hooked= ivt=` for those from real mode |
| CRASH | A client the host ends (§19): `why= vec= err= prog= psp= bits= mode=`, `cs:eip= ss:esp= eflags= cr2=`, the general registers, `code=` (16 bytes at CS:EIP), `stack=`, a `seg` line per segment register, `handlers= lstack= nesting=`, `file=` |
| DPMI-UNIMPL | An unimplemented call |
| PANIC | A panic: registers and CR2. A double fault adds the interrupted context from the TSS, ESP0 and its page-table entry, any IDT gate or GDT descriptor that changed since start-up, and a `thread=` line per thread (stack, saved ESP, canary) |

## 19. Debugging

- **Kernel:** a gdb remote stub on COM2 (`/GDB`) supports `g G m M c s Z0` (software breakpoints). DR0–DR3
  are the clients' 0B00h watchpoints. Loop A routes COM2 through a FIFO pair to TCP (M0's `--com2`).
- **Clients (M5):** a stub per process, reached through an SSH `direct-tcpip` forward.
- **Loop A hangs:** `run.py`'s `--idle` and `--timeout` count host seconds, but guest time slows when the host is
  busy (2026-10-03: a Pentium II profile at under half speed). Before debugging a HANG, compare the guest's time
  (the BIOS tick at 46Ch, `GLOS-VM leave ticks=`) with the run's `elapsed_s`. Tests end guest waits on a host
  signal (a serial marker that makes the harness type a key), not on a fixed `WAITSEC`.
- **86Box monitor peeks** of kernel memory must read physical addresses: a linear read goes through the MMU at
  the guest's CPL and, from V86 code, leaves a page fault for the guest to take.
- **Crash reports** (M4b, `kernel/dbg/crash.c`), for a client the host ends: an exception it has no handler
  for, five nested ones, a frame it can't return to:
  - the program's path, the registers, the 16 bytes at CS:EIP, 12 stack words, each segment's base and limit,
    the host's entry count;
  - written to the log as `GLOS-CRASH` lines, and to `C:\GLOS\CRASH\CRASHnnn.TXT` by the program's own DOS
    calls, nested (not when DOS was busy: `file=none why=indos`);
  - `tools/symcrash.py REPORT --exe PROG.EXE` names EIP and the stack's return addresses from a DJGPP image's
    COFF symbols (its EIPs are image addresses); `--map PROG.MAP --base ADDR` reads an Open Watcom map for a
    flat program (EIP is linear, ADDR where object 1 was loaded).

## 20. 86Box deviations [86Box]

| Deviation | Effect on GLOS | Handling |
|---|---|---|
| A VME-redirected INT pushed the real FLAGS (IF, IOPL) | VIF became the real IF after the handler's IRET | **Fixed:** local patch 0106 |
| INT3/INTO raised #GP in V86 at IOPL<3 | The #BP/#OF paths went untested | **Fixed:** patch 0107 (BOUND was right) |
| VME at IOPL 3 skipped the redirection bitmap | Direct mode on VME CPUs behaved unlike silicon | **Fixed:** patch 0108 |
| POPFD at CPL > 0 loaded VIF and VIP from the image | Hid the PVI POPF hazard (§9.3) | **Fixed:** patch 0109 |
| Any fault during interrupt delivery becomes #DF | Real hardware would give #PF/#GP | Invariant: ring-0 stacks, GDT, IDT, TSS and LDT always present |
| Inter-privilege delivery leaves state inconsistent if a push faults | | Same invariant |
| #PF error code U bit reflects CPL during supervisor pushes | | Recorded by V86TEST; the kernel doesn't depend on U for implicit accesses |
| A byte port's second bitmap byte wasn't checked against the TSS limit | A missing trailing FFh byte worked in 86Box | **Fixed:** patch 0110; static assertion and host test too |
| IRQ8 keeps firing without a read of register C | A tick that forgets C works in 86Box, freezes on silicon | Always read C (§8.1) |
| PGE is stored but every flush is global | None | |
| The dynarec compiles PUSHF per IOPL | IOPL changes inside a session could be ignored | V86TEST case U found no problem; IOPL stays constant per session anyway |
| DR0–DR3 breakpoints never fire (86Box's debug-register support is behind its `DEBUGREGS486` build option, off by default) | 0B00h–0B03h couldn't be seen working in Loop A, on any host | **Fixed:** patch 0115 builds it (case Y); the emulated Pentium II's CPUID now reports DE, as the chip does. DPMICONF's `watch` is a check again; it found GLOS dropping the RF it owed the client's handler's return (M4d) |
| A single-step trap stays pending across an instruction that faults into ring 0, and is taken in the kernel's handler (silicon discards it, SDM 17.3.1.4) | A #DB in ring 0 with DR6=0, which the kernel never causes (M4c: an lDebugX trace step onto one of the stub's ARPLs, a #UD, panicked GLOS) | Such a #DB is dropped, `GLOS-CPU stray-db` logged once (§12.1c). **Fixed:** patch 0113 (MGA-Glide a8684e5; V86TEST case W). GLOS keeps the drop: harmless on silicon |
| IRETD to V86 mode loads EFLAGS[31:16] unmasked (POPFD masks by CPU model), so a 486DX2 keeps ID | An emulated POPFD that set ID made CPUID look present (lDebugX ran it into #UD, M4c) | GLOS's FLAGS image has ID only where the loader found CPUID (§9.2). **Fixed** too: patch 0114 (case X) |
| Matrox G-series cards are AGP only | 486 profiles can't have a Matrox card | S3 Trio64V2/DX until a PCI-variant patch at M7 |
| The dynarec (the old one MGA-Glide builds) checks segment limits on stores, never on loads (`MEM_LOAD_ADDR_EA_*`) | DJGPP's Ctrl-C and SIGALRM cut DS's limit to 4 KB in the IRQ handler; a loop that only reads never faults, on any host (found by djtst205's HANG, M4b) | **Fixed:** patch 0112 (MGA-Glide 81c0686; V86TEST case V) |

## 21. Invariants checklist (for code review)

1. Ring-0 stacks, the GDT, IDT, TSS (with all three bitmap pages) and the current LDT are present and pinned
   whenever interrupts can occur.
2. The IOPB is followed by an FFh byte inside the TSS limit.
3. The RTC handler reads register C on every IRQ8.
4. GLOS never latches or reads PIT channel 0.
5. Every return to a 16-bit SS goes through espfix.
6. Only the system VM thread executes V86 code; everything else posts.
7. A posted call runs only at a safe point (§17.2).
8. Client-supplied pointers are only dereferenced through fixup-protected copy routines.
9. 0501h blocks never move below a context's first block.
10. 0A00h answers only "GLOS".
11. 1600h/160Ah never report Windows.
12. No periodic COM1 output.
13. IOPL never changes within a session.
14. Every client-visible vector number matches what 0400h reported.
15. Trap entry clears DF; no kernel C code runs with DF set.
