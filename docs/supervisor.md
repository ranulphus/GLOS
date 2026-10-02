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
- **Measured (M3):** the stub is 2,030 bytes. With the PSP and the environment, MEM /C shows GLOS at 2,480
  bytes, and the largest program is 2,480 bytes smaller than under plain DOS.
- **From a prompt or AUTOEXEC.BAT:** COMMAND.COM stays the shell; `glos exit` returns to it.
- **As the shell (`SHELL=C:\GLOS\GLOS.EXE` in CONFIG.SYS):**
  - `GLOS.EXE`'s PSP is the root of the PSP chain, and its environment is the master environment (1 KB by
    default, set in GLOS.CFG). GLOS sets `COMSPEC` to COMMAND.COM, found next to the kernel, in the boot
    drive's root or through `PATH`.
  - Once the kernel is running, the stub runs `COMMAND.COM /C AUTOEXEC.BAT`. When that COMMAND.COM ends
    (INT 21h 4Ch with its PSP), the kernel copies its environment into the master environment before DOS
    frees it.
  - .COM and .EXE programs are EXECed directly. COMMAND.COM runs batch files, internal commands and
    `%COMSPEC%` shell-outs.
  - Until the desktop exists (M6), the local console is a COMMAND.COM the stub runs and runs again whenever it
    exits. Agent commands run alongside it at safe points (§17.2).
  - GLOS never just ends: a refusal (§2 item 1), a missing kernel or a failure before the system VM exists
    runs COMMAND.COM in real mode instead, after a `GLOS-REFUSE` line and a message on screen. `glos exit`
    leaves protected mode and runs COMMAND.COM; its `EXIT` starts GLOS again.
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
| 38h, 40h | 16-bit code and data at the resident stub, used to leave protected mode |
| 4Bh | Ring-3 trampoline code: host-owned stubs (IRET trampoline, PM breakpoints, the RMCB return) |
| 53h | Ring-3 data alias of the trampoline page |
| 5Bh | BIOS data selector, base 400h, limit FFFFh, ring 3 (for Windows-era clients that expect 0040h-style access) |

The order above never changes. New selectors are appended.

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

- **Threads** are kernel objects with their own kernel stack (8 KB) and a saved trapframe. The **system VM**
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
- PUSHF(D), POPF(D) (respecting TF and the virtual IF);
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
  PUSHF, POPF, IRET and software INTs run in hardware against EFLAGS.VIF. Only INT 15h, 21h and 2Fh trap,
  through the redirection bitmap; INT 10h and 13h join them with the DOS server's busy tracking (§17.2). The
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
| GLOS-owned NIC | Trapped; reads return FFh | Trapped | From M3 |
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
  ([DPMI deep dive]; DOSBench's DBMENU (DJGPP) starts BENCHG (DOS/4GW) and BENCHGL [census]).
- A child's mode switch pushes a client level. On the child's 4Ch, GLOS frees the blocks, selectors, RMCBs and
  handlers allocated at that level, and restores PSP:2Ch.

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
| 0000h | Allocate LDT descriptors | M4a | DJGPP stub, DOS/4GW | CWSDPMI, HDPMI32i |
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
| 0100h–0102h | DOS memory | M4a | Under the DOS lock. **On failure AX=0008h and BX=largest block**: the DJGPP stub relies on it. | CWSDPMI |
| 0200h/0201h | Get/set real-mode vector | M4a | DJGPP installs its INT 1Bh RMCB here | |
| 0202h/0203h | Get/set exception handler | M4b | DJGPP: 0–11h; MGA-Glide OW: 00h, 06h, 0Dh, 0Eh | CWSDPMI |
| 0204h/0205h | Get/set PM vector | M4a (vectors), M4b (IRQ delivery) | INT 8/9/1Bh/23h/24h/75h, IRQ3/4, INT 21h (DOS/4GW, MGA-Glide exit hook) | CWSDPMI, DOS/4GW |
| 0210h–0213h | Extended exception handlers (1.0) | M4b | | HDPMI32i |
| 0300h | Simulate real-mode interrupt | M4a | SS:SP=0 means the host stack; CX words copied; IF/TF clear | CWSDPMI |
| 0301h/0302h | Call real-mode far / IRET procedure | M4a | GLQuake IPX entry; SDL VBE bank switch | |
| 0303h/0304h | Allocate/free real-mode callback | M4b | **Every DJGPP program** (INT 1Bh); DOS/4GW. At least 16 per context. ES:EDI returned unchanged. | CWSDPMI |
| 0305h/0306h | State save / raw switch addresses | **M4a** | **DOS/4GW needs it to start.** The "real-mode" side is a V86 trap stub. | DOS/4GW, HDPMI32i |
| 0400h | Version | M4a | 0.90, BX bit 0=32-bit, bit 1=0 (reflection in V86); **DH=08h, DL=70h** (or the virtual PIC's ICW2) | CWSDPMI |
| 0401h | Capabilities (1.0) | M4c | | HDPMI32i |
| 0500h | Free memory info | M4a | Accurate; programs allocate all of it | CWSDPMI |
| 0501h–0503h | Linear blocks | M4a | Ascending (§12.3) | CWSDPMI |
| 0504h–0506h | 1.0 linear memory, page attributes | log | | |
| 0507h | Set page attributes | M4b | DJGPP crt0 uncommits page 0 (null trap) unless NULLOK | CWSDPMI |
| 0508h/0509h | Map device / conventional memory (1.0) | M4c | HDPMI suite | HDPMI32i |
| 0600h–0603h | Lock/unlock | nop (recorded) | SDL, Quake, LOCK_MEMORY | |
| 0604h | Page size | M4a | 4096 | |
| 0702h/0703h | Discardable | nop | DOS/4GW | |
| 0800h/0801h | Physical mapping | M4a | Matrox BARs, VBE LFB; linear = physical (§12.4) | DOS/4GW |
| 0900h–0902h | Virtual IF | M4a | Return the old state in AL | |
| 0A00h | Vendor API | M4a | "GLOS" → entry point and API version (PRD D23). **Any other string → CF=1, AX=8001h** (DOS/4GW probes "RSI CLIENT 0.9"/"RATIONAL DOS/4G"). | |
| 0B00h–0B03h | Watchpoints | M4c | DR0–DR3, shared with the gdb stub | HDPMI32i |
| 0C00h/0C01h | TSR services (1.0) | log | | |
| 0D00h–0D03h | Shared memory (1.0) | fail (8001h) | DOS/4GW probes 0D00h | |
| 0E00h/0E01h | Coprocessor status / emulation | M4b | DJGPP: 0E01h BX=1; without an FPU, BX=3 and EMU387 | CWSDPMI (ignores 0E00h) |

**INT 2Fh** (handled by the host in both modes):

| Call | Behaviour |
|---|---|
| 1687h | The DPMI host, with SI = private paragraphs |
| 1686h | AX=0 in protected mode |
| 168Ah | 1.0 vendor API, same rules as 0A00h |
| 1680h | **A real yield** (`uclock`'s start-up spin and `usleep` call it [census]) |
| 1600h, 160Ah | **Never report Windows.** GLQuake aborts if Windows is reported [census]. |
| 1681h/1682h | Reflected (no handler, as on the baselines) |
| 4300h/4310h | GLOS's XMS server (§16) |

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
  - 0–5 and 7 reflect to real-mode interrupts;
  - 6 and 8–1Fh terminate the client with a crash report (§19).
  - After 5 nested exceptions the client is terminated (CWSDPMI's rule).

### 14.7 FPU

- Lazy switching (CR0.TS, #NM).
- NE=1, so errors arrive as #MF (vector 10h) for 32-bit clients. IRQ13 is synthesised for V86 code and for
  clients that hooked INT 75h (DJGPP writes port F0h and EOIs both PICs).
- Per-client FPU state, including the control word: GLQuake changes FLDCW constantly [census].

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

### 17.3 The agent shell (M3)

- After start-up, the system VM's foreground program is `GLOS.EXE`'s resident stub. In headless mode it waits
  at a breakpoint for the next command.
- For each command it EXECs `COMMAND.COM /C …` and returns the exit code from INT 21h 4Dh. DOS is never
  re-entered behind a running program's back.
- In desktop mode (M6) the same stub hosts the launcher.

### 17.4 Output capture

- Program output is captured from INT 29h and INT 21h 02h, 06h, 09h and 40h.
- For 40h, only handles whose open-file table entry is a character device count. 8.3 file names only (no LFN
  API in v1).

### 17.5 Critical errors and Ctrl-Break

- In exclusive sessions these follow the baseline: INT 24h fails (AL=3), and Ctrl-C at the DOS level is
  swallowed (CWSDPMI hooks real-mode INT 23h with IRET).
- For GLOS apps (M6), INT 24h becomes a dialog.

## 18. Logging

- Lines on COM1 have the form `GLOS-<TAG> key=value …`, one line per event, written synchronously so the last
  line survives a hang.
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
| KILL | A kill (§9.6) |
| RESET-REQ | A reset request: `source=kbc`, `port92`, `cf9` or `cad` |
| DPMI-UNIMPL | An unimplemented call |
| PANIC | A panic: registers, stack, last log lines |

## 19. Debugging

- **Kernel:** a gdb remote stub on COM2 (`/GDB`) supports `g G m M c s Z0` and hardware breakpoints in
  DR0–DR3. Loop A routes COM2 through a FIFO pair to TCP (M0's `--com2`).
- **Clients (M5):** a stub per process, reached through an SSH `direct-tcpip` forward.
- **Crash reports:**
  - registers, the faulting instruction bytes, the stack and the context's module map (EXE and base
    addresses);
  - written to `C:\GLOS\CRASH\` and the log;
  - `tools/symcrash.py` symbolises them against DJGPP COFF or Watcom maps.

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
| Matrox G-series cards are AGP only | 486 profiles can't have a Matrox card | S3 Trio64V2/DX until a PCI-variant patch at M7 |

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
