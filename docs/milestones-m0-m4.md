# GLOS milestones M0–M4: work breakdown

| | |
|---|---|
| Status | Draft 0.1 |
| Last updated | 2026-10-01 |
| Companion | [PRD.md](../PRD.md) §19, [supervisor.md](supervisor.md) |

- Each numbered task is one commit or a short series. "T:" is the test the task adds.
- Exits are checked in Loop A unless marked "bench".
- Harness and 86Box changes land in MGA-Glide first (PRD D27):
  - on branch `glos-harness` in the worktree `~/wt-mga-glos`;
  - with MGA-Glide's full regression run before the user approves a merge;
  - GLOS then bumps `deps.mk`.

---

## M0: harness (MGA-Glide) and the IOPL-0 survey (GLOS)

### MGA-Glide tasks

1. **Config check.**
   - `tools/loopa/run.py --emit-config` prints the generated `86box.cfg` and the golden-image key without
     starting 86Box.
   - `make loopa-cfgcheck` compares both against committed references for the default BF6 job, proving they
     stay byte-identical. Run after every later task.
2. **Machine profiles: `--machine bf6|486dx2|486dx4`.**
   - A profile table in `run.py`:
     - `bf6`: today's Pentium II 350.
     - `486dx2`: `hot433a` with BIOS `hot433a_v451pg` and `i486dx2` at 66 MHz (no CR4).
     - `486dx4`: the same board with `idx4` at 100 MHz (VME/PVI).
   - The NVR cache is named `nvr-<profile>`, replacing the hard-coded `nvr-bf6` in `run.py` and
     `tools/bench/vpc.py`.
   - `tools/86box/build.sh` adds `/machines/hot433/` and `/video/s3/` to the sparse ROM checkout and to the ROM
     cache key.
   - T: `selftest-486` runs the HX tools and DOS4GW HELLO on both 486 profiles.
3. **Generic VBE card: `--card vbe`** (`s3_trio64v2dx_pci`, VBE 2.0).
   - Asking for a Matrox card on a 486 profile gives SETUP-ERROR, because 86Box's Matrox devices are AGP-only.
   - New `tools/dos/vbeinfo.c` (16-bit) prints `HX-VBE ver=… lfb=… modes=…`.
   - T: `selftest-vbe` on bf6 and 486dx2.
4. **Network: `--net ne2k|ne2kpci|rtl8139c+|i82557|i82558[:PORT]`.**
   - Writes `[Network]` and `[SLiRP Port Forwarding #1]` (`0_protocol`, `0_external`, `0_internal`), picks a
     free host port when none is given, and records it in `result.json`.
   - `vpc.py` uses the same helper.
   - T: `selftest-net`: the Crynwr NE2000 packet driver and mTCP (both already pinned) listen in the guest, and
     the host connects to the forwarded port and exchanges a line.
5. **COM2: `--com2`.**
   - `serial2_device = pipe` plus `tools/loopa/com2bridge.py` (FIFO pair ↔ TCP, for gdb's `target remote`).
   - T: `selftest-com2` with `tools/dos/com2echo.c`.
6. **HIMEMX boot: `--boot-cfg himemx`.**
   - `mkgolden.sh` takes a variant argument; the variant's files go in `tools/loopa/dos-himemx/`
     (`DEVICE=HIMEMX.EXE`, `DOS=HIGH`). The default image's key hashes `dos/*` only, so it is untouched.
   - HIMEMX is pinned in `versions.mk` if the FreeDOS 1.4 floppy lacks it.
   - T: `selftest-himemx`: `tools/dos/xmsinfo.c`, DJGPP HELLO (CWSDPMI over XMS) and DOS4GW HELLO.
7. **`--wrap PREFIX`** (puts e.g. `C:\GLOS\GLOS.EXE /RUN` in front of each test command) and
   **`--dynarec 0|1`**.
8. **V86TEST (CPU self-test).**
   - `tests/cpu/v86test.c` (Open Watcom 16-bit) builds page tables and a TSS in conventional memory, enters
     protected mode with paging, runs the cases from a GNU as `--32` blob (`tests/cpu/v86pm.S`) and returns to
     real mode.
   - `tests/cpu/known-86box.txt` lists known emulator deviations with their patch numbers. A case passes if it
     matches silicon or is listed.
   - Cases:

| Case | Checks |
|---|---|
| A | CR4 bits per profile; MOV CR4 gives #UD on the dx2 |
| B | Entering V86 with IRETD and leaving it: segment registers pushed, then set to null |
| C | IOPL-sensitive instructions at IOPL 0 without VME: #GP(0) at the instruction |
| D | INT3, INTO and BOUND in V86 reach IDT vectors 3, 4 and 5 (patch 0107) |
| E | HLT in V86 and in ring 3: #GP |
| F | I/O bitmap: always checked in V86; checked in ring 3 when CPL>IOPL; byte, word and dword straddles; INS/OUTS; access to the last byte |
| G | VME: CLI/STI change VIF; STI with VIP set gives #GP; the PUSHF image has IF=VIF and IOPL=3; POPF/IRET with TF, or with IF while VIP is set, give #GP; redirection bit 0 goes to the IVT with IF=VIF pushed (patch 0106), bit 1 to the IDT; IOPL 3 still uses the bitmap (patch 0108) |
| H | PVI: ring-3 CLI/STI change VIF; POPF leaves it alone |
| I | The redirection bitmap sits at the IOPB base minus 32 |
| J | INT n to a DPL-0 gate gives #GP(n·8+2), not #DF; a not-present gate gives #NP |
| K | A change to ESP0 takes effect |
| L | A V86 INT or IRQ frame across a not-present page can be restarted (the patch 0103 class) |
| M | IRET to a 16-bit SS keeps ESP[31:16] from ring 0 (the reason for espfix) |
| N | IRET with a bad SS or CS gives a ring-0 #GP that a fixup can handle, not #DF |
| O | #PF error codes: ring 3 touching a supervisor page, V86, a ring-0 write with WP=1; the U bit on implicit accesses is recorded |
| P | PGE behaviour (recorded) |
| Q | RTC at 1024 Hz counted against the PIT; IRQ8 still raised while register C goes unread (recorded) |
| R | Whether the BIOS's INT 15h 86h/83h turns the RTC periodic interrupt on: hook INT 70h, read CMOS register B |
| S | A20 through port 92h, 8042 D1h and INT 15h 2401h/2400h |
| T | FPU: NE=1 gives #MF (vector 10h); NE=0 gives IRQ13; OUT F0h clears it |
| U | With the dynarec on, the same V86 code block under IOPL 0, then 3, then 0 |

   - T: `selftest-cpu` on all three profiles.
9. **Local 86Box patches** (as V86TEST found them):
   - `0106-cpu-vme-int-redirect-flags`: a redirected INT pushes FLAGS with IF=VIF and IOPL=3.
   - `0107-cpu-v86-int3-into`: INT3/INTO in V86 mode go through the IDT as on silicon.
   - `0108-cpu-vme-iopl3-redirect`: VME at IOPL 3 consults the redirection bitmap.
   - `0109-cpu-popfd-vif-vip`: POPFD never loads VIF or VIP (found by case H; the dynarec patch planned
     for this number wasn't needed, as case U passed).
   - `0110-cpu-iopb-two-bytes`: a byte port's two bitmap bytes must both be inside the TSS limit.

   Each patch removes its line from `known-86box.txt`, which is now empty.
10. **`docs/loops.md`:** the new options and self-tests.

**M0 exit (MGA-Glide worktree):**
- `make 86box loopa-cfgcheck loopa-selftest conform-ci hreplay-check` and the six replays pass unchanged.
- `selftest-{486,vbe,net,com2,himemx,cpu}` pass.
- `known-86box.txt` lists no bug a local patch fixes.

### GLOS tasks: the IOPL-0 survey

11. **`tools/setup/fetch.sh` and `versions.mk`** in GLOS: sha256-pinned downloads into `~/.cache/glos/dl`:
    - HX runtime with HDPMI32i (IOPL-0 variant) and HDPMI16;
    - ecm dpmitest binaries;
    - DJGPP `djtst205.zip`.
12. **`tools/survey/run.py`.** For each existing suite, a baseline Loop A run and a run with HDPMI32i resident
    (`--pre "HDPMI32I -r"`), compared on status, HX-TEST/DGL-/MGL- line sets (timing fields removed) and
    dumped frames. The suites:
    - DOS-GL conform;
    - `loopa-sdl`;
    - ClassiCube, GLQuake, Quake 2, Half-Life, PrBoom-plus, Fifth Wheel;
    - MGA-Glide HELLO, conform and replays;
    - DOSBench;
    - djtst205.
13. **`docs/survey-iopl0.md`:** results per suite, programs that need a direct-mode profile, and hazards to
    design for.

---

## M1: ring-0 round trip

1. Review `docs/supervisor.md` (done before any code).
2. **Kernel build.**
   - `mk/kernel.mk` and `kernel/kernel.ld`, linked at C010_0000.
   - Flags: `-m32 -march=i486 -ffreestanding -fno-pic -mgeneral-regs-only`. No FPU in the kernel through M5.
   - `GLOSK.BIN`: a flat image with a header.
   - A native `-m32` harness for host tests in `tests/host/`.
3. **Loader refusals** in `loader/cpu.c` (supervisor.md §2). T: refusal jobs with HDPMI32i and CWSDPMI
   resident.
4. **Loader memory.**
   - `loader/mem.c` (E820/E801/88h), `loader/a20.c`, `loader/xms.c` (09h/89h, 0Ch lock).
   - T: `tests/host/e820_test.c`.
5. **Kernel load and bootinfo.** INT 15h 87h or XMS 0Bh; `include/glos/bootinfo.h` v1.
6. **Mode switch.** `loader/pm.asm` (Open Watcom wasm) for entry and the return path (16-bit selectors, IVT
   reload, A20 restore).
7. **`kernel/arch/`.**
   - GDT/IDT/TSS as in supervisor.md §3; 256 entry stubs; the trapframe; the fixup table; the #DF task
     (`GLOS-PANIC`).
   - `kernel/drv/serial.c` for COM1 logging.
8. **`kernel/mm/`.** Frame bitmap, `kmalloc`, recursive page directory, kmap. T: `tests/host/pmm_test.c`,
   `vmm_test.c`.
9. **`kernel/core/timer.c`.**
   - PIC remapped to 50h/58h.
   - RTC tick, always reading register C, with a spurious-IRQ8 counter.
   - TSC calibration where present.
10. **`GLOS.EXE /ROUNDTRIP`:** ticks for 1 s, writes `GLOS-RING0 ticks=N`, restores the PIC, RTC and A20, and
    returns to DOS.
11. **gdb stub** `kernel/dbg/gdbstub.c` on COM2 (`/GDB`). `tools/gdb-loopa.sh`. T:
    `tests/loopa/gdb-smoke.gdb` (break, step, read memory, continue).
12. **`tests/loopa/jobs.py`:** the profile × boot matrix over `run.py --wrap`, with expected line sets.

**M1 status (2026-10-02): done.** `make loopa-m1` passes on all six machine and boot combinations (ticks
1011–1012 of an expected 1012). `make loopa-gdb` passes. `tests/loopa/jobs.py refuse` checks the refusal over
a resident HDPMI. `make host-test` runs the frame bitmap and heap tests. Differences from the plan:
- the loader copies the kernel itself (supervisor.md §2.4), so no INT 15h 87h;
- an E820 parser host test was dropped: the parser lives in the 16-bit loader, and both raw-mode boots
  exercise it;
- MGA-Glide gained `OUT/com2.port` for the gdb test.

**M1 exit:** `make host-test kernel loopa-m1 loopa-gdb`.
- On bf6, 486dx2 and 486dx4, each with raw and HIMEMX boots,
  `VECCHK save; GLOS /ROUNDTRIP; VECCHK check; KEYWAIT; VMODE` gives:
  - `GLOS-RING0` with a tick count within ±2% of 1024;
  - `HX-VECCHK ok`;
  - `HX-KEY scan=1c` (key sent with `--keys`);
  - `HX-VMODE bios=03`.
- gdb breaks in the kernel.

---

## M2: the system VM

1. **Threads and scheduler.** `kernel/core/{thread,sched}.c`: preempt count, preemptible kernel threads, the
   system VM thread, ring-0 HLT idle.
2. **`kernel/vm/v86.c`:** the system VM from the loader's interrupted state; PDE 0 shared; the A20 wrap through
   page-table entries.
3. **`kernel/vm/v86dec.c`** (supervisor.md §9.2). T: `tests/host/v86dec_test.c` (table-driven, every prefix
   combination).
4. **Virtual IF and pending IRQs.** The VME/PVI path on CPUs that have them; the redirection bitmap per
   session; the `vif-stuck` watchdog.
5. **`vm/vpic.c`.** T: `tests/host/vpic_test.c` with scripts for BIOS POST, DOS, SDL's double EOI and DJGPP's
   INT 75h.
6. **IRQ routing** (supervisor.md §10).
7. **Virtual devices:**
   - `vm/vrtc.c`, `vm/vkbc.c` (full 8042, hotkeys), `vm/a20.c`, `vm/pci.c`;
   - ELCR, CF9h (`GLOS-RESET-REQ`);
   - the COM1 mirror ring.
8. **XMS server and INT 15h.**
   - `kernel/dpmi/xms.c` (XMS 3.0, HMA, takeover) and `dpmi/int15.c`.
   - T: `tests/dos/xmstest.c` (OW) against HIMEMX as the baseline.
9. **`kernel/dos/proc.c`:** PSP tracking via 4Bh/4Ch/31h, IVT snapshots, kill (`GLOS-KILL`).
10. **Hostile suite** `tests/dos/hostile/*.c` (OW .COM): CLIJMP, POPFIF, HLTCLI, A20OFF, PICREMAP, RTCWRITE,
    PITPROG, KBCRESET, CF9RESET, CAD.

**M2 exit:**
- `make loopa-m2`: the HX tools' line sets under GLOS match those without GLOS, on 3 profiles × 2 boots.
- `make loopa-hostile`: each hostile program is killed through the hotkey (via `--keys`), giving `GLOS-KILL`.
  Afterwards the tick still advances, VECCHK is OK and VMODE reports 3.
- Borland: `$(BORLAND_DIR)` is checked for TPX.EXE, RTM.EXE and DPMI16BI.OVL (needed by M4d).

**M2 status (2026-10-02): done, with two items moved to M3.** On all six machine and boot combinations:
- `make loopa-m2`: M2.BAT (XMSINFO, VBEINFO, VMODE, XMSTEST, WAITSEC, KEYWAIT) gives the same HX lines under
  `GLOS /RUN COMMAND /C` as without GLOS. The only differences allowed are XMS free/largest; on the raw boots
  the XMS lines aren't compared, because there's no driver without GLOS. On the HIMEMX boots, GLOS's XMS
  server matches HIMEMX line for line, error codes included.
- `make loopa-hostile`: all 11 hostile cases end in `GLOS-KILL` under one `GLOS /RUN`, each followed by a
  KEYWAIT that gets its key. The reset requests (kbc, cf9, cad), the A20 wrap and three `vif-stuck` warnings
  appear as expected. After GLOS: the tick advanced, VECCHK, the BIOS tick, a key and text mode.
- `make loopa-m1`, `loopa-gdb` and the refusal still pass. `make host-test` adds the decoder and virtual PIC
  tests.
- Borland: TPX.EXE, RTM.EXE and DPMI16BI.OVL are in `~/BORLAND/TP7`.

Differences from the plan:
- **Moved to M3: threads and the scheduler (item 1) and the VME/PVI path (part of item 4).** The single
  system VM runs from trap context, and every software INT traps (supervisor.md §9.4). The network stack in M3
  is the first thing that needs threads.
- **Not yet:** ELCR (passed through) and the COM1 mirror ring (M3, with the agent's log).
- **Kill snapshots** are taken at INT 21h 4B00h/4B01h and keyed by the parent's PSP. A program that can't be
  emulated (a system instruction) is killed by GLOS itself. supervisor.md §9.6 has the details.
- The hostile programs are one OW program with a case argument (`tests/dos/hostile.c`, plus PRIV), not one
  .COM each. XMSTEST is `tests/dos/xmstest.c`.
- **Bug found by the per-kill KEYWAIT:** a program whose keyboard handler takes IRQ 1 without reading port 60h
  (PICREMAP) left a byte in the virtual 8042, and nothing raised IRQ 1 for it again after the kill. The kill
  now raises it again, and leaving GLOS empties the chip.

---

## M3: network and minimal agent

0. **From M2: threads and the scheduler** (supervisor.md §7; M2's item 1), with the system VM as a thread,
   **and the VME/PVI path** with the redirection bitmap and the `vif-stuck` watchdog on it (M2's item 4).
   T: `make loopa-m2 loopa-hostile` unchanged.
0a. **The resident stub and start modes** (supervisor.md §2.2; PRD D39–D41, P7). `loader/stub.asm` linked
   first; GLOS.EXE shrinks to it after start-up. `SHELL=` mode: COMSPEC, AUTOEXEC.BAT through `COMMAND.COM
   /C` with its environment kept, the local COMMAND.COM console, and the fallback to COMMAND.COM.
   MGA-Glide harness item: a `--boot-cfg` variant whose CONFIG.SYS has `SHELL=C:\TEST\GLOS.EXE` (D27).
   T: `jobs.py mem` (MEM /C with and without GLOS, P7, on both boots); `jobs.py shell` (the `SHELL=` boot:
   AUTOEXEC.BAT's `SET` and `PATH` visible to the next program, RUN.BAT runs, and a refusal (no GLOSK.BIN)
   falls back to a working prompt).
1. **PCI and NE2000.** `kernel/drv/pci.c` (enumeration, claiming) and `drv/nic/ne2k.c` (ISA and PCI), with the
   refusal rules (shared IRQ, a packet-driver signature on the same base).
2. Wait queues, timers and mutexes.
3. **lwIP.** `third_party/lwip` (BSD-3), `NO_SYS=1`, DHCP, the port in `kernel/net/`, and `THIRD_PARTY.md`.
   T: `tests/host/net_loop.c`.
4. **Keys.** Entropy pool and seed file; the fixed Loop A test host key and client key in `tests/keys/`.
5. **Crypto.** TinySSH crypto (CC0) in `third_party/tinyssh/`, sntrup761 left out. T: known-answer tests from
   RFC 7748, RFC 8032 and RFC 8439.
6. **SSH server `kernel/ssh/`.**
   - Event-driven, several connections, several channels: `session` (exec, the `sftp` subsystem), later
     `direct-tcpip`.
   - curve25519-sha256, ssh-ed25519, chacha20-poly1305.
   - About 90 KB per connection.
   - T: `tests/host/sshd` built natively on sockets and driven by OpenSSH `ssh`/`sftp` with concurrent
     sessions, before any Loop A run.
7. **Agent shell** in the resident stub (supervisor.md §17.3).
8. **Output capture** `kernel/dos/capture.c`.
9. **SFTP.** `kernel/dos/idle.c` (safe points) and a port of OpenSSH `sftp-server.c` (ISC). 8.3 names only.
10. **Built-in commands.** Text-mode `glos shot` (B800h + BDA → a CP437 bitmap font rendered at build time →
    PNG via stb_image_write), plus `glos exit|ps|log|kill`.
11. **MGA-Glide H7:** `run.py --ssh-steps FILE --ssh-key K` (exec/expect, put/get, shot).

**M3 exit:** `make loopa-m3` on bf6 + `ne2kpci` and 486dx2 + `ne2k`:
- `tests/dos/echoargs.c`'s stdout, stderr and exit code 7 are captured exactly;
- a 1 MB file survives an SFTP round trip with the same sha256;
- the text-mode screenshot matches its golden PNG;
- `glos exit` leaves VECCHK OK.

Handshake time is logged, not judged (PRD D28).

---

## M4: DPMI host, in five sub-milestones

| Sub | Scope | Tests | Exit |
|---|---|---|---|
| **M4a** | 32-bit basics: `kernel/dpmi/{host,int31,ldt,mem,rmcall}.c`; 1687h and the mode switch; contexts (supervisor.md §12); 0000h–000Dh, 0100h–0102h, 0200h/0201h, 0204h/0205h (vectors), 0300h–0302h, 0305h/0306h, 0400h, 0500h–0503h, 0600h–0604h, 0800h/0801h, 0900h–0902h, 0A00h; PSP and environment selectors; INT 21h 4Ch teardown; espfix | DPMICONF-32 (`tests/dos/dpmi/`, DJGPP; first validated against CWSDPMI and HDPMI32i — a test that fails on both is wrong); MGA-Glide DJGPP HELLO and DOS4GW HELLO | `make loopa-m4a` |
| **M4b** | Exceptions (0202h/0203h, 0210h–0213h), the locked stack, frame edits; protected-mode-first IRQ delivery and the IRET trampoline; 0303h/0304h with the pass-up loop guard; INT 31h reentrant from IRQ handlers; 0507h; 0E00h/0E01h, IRQ13/INT 75h; crash reports (`kernel/dbg/crash.c`, `tools/symcrash.py`) | djtst205 (ctrlc, fault, fpu, hang, infoblk, raise, signals, timer, null, brk, multispn, nearptr, hwint); DPMICONF exception, IRQ and RMCB tests; MGA-Glide STACKPG, MOUSETST, JOYTEST, SBBEEP | `make loopa-m4b` |
| **M4c** | DOS/4GW and retail: 000Bh/000Ch Big-bit changes; INT 2Fh 1600h/160Ah/1680h–1682h/1686h/4310h; VCPI and EMS absent; 0D00h failing cleanly; nested contexts; 0401h, 0508h/0509h, 0B00h–0B03h | ecm dpmitest; the HDPMI regression suite (against HDPMI32i); MGA-Glide conform 27×4 and replays; GTA; Screamer Rally; DOSBench (DBMENU → BENCHG/BENCHGL) | `make loopa-m4c` |
| **M4d** | 16-bit clients: 16-bit frames, stacks, RMCBs, raw switch; espfix on every return | DPMICONF-16 (`tests/dos/dpmi16/`, OW 16-bit, reporting over COM1) against HDPMI16; TPX.EXE from `$(BORLAND_DIR)` on RTM/DPMI16BI, driven by `--keys` (open a file, compile, exit) | `make loopa-m4d` |
| **M4e** | Exclusive sessions (`kernel/vm/session.c`): save and restore, kill; per-program profiles (`direct=1` for IOPL 3); `glos run`; `tools/gate/` | The full gate | `make gate` |

### The gate's baseline matrix (M4e)

| Suite | Baseline | Profiles | Boots |
|---|---|---|---|
| DPMICONF-32 | CWSDPMI r7, HDPMI32i | bf6, dx2, dx4 | raw, HIMEMX |
| ecm dpmitest | CWSDPMI, HDPMI32i | bf6, dx2, dx4 | raw |
| HDPMI suite | HDPMI32i | bf6, dx2 | raw |
| djtst205 | CWSDPMI | bf6, dx2, dx4 | raw, HIMEMX |
| DPMICONF-16 | HDPMI16 | bf6, dx2 | raw |
| TPX.EXE | RTM + DPMI16BI | bf6, dx2 | raw, HIMEMX |
| MGA-Glide HELLO, conform 27×4, replays | DOS/4GW without a host | bf6 (4 cards) | raw, HIMEMX |
| GTA, Screamer Rally | Their own DOS/4GW | bf6, G450 | raw |
| DOS-GL conform, loopa-sdl, ClassiCube, GLQuake, Q2, Half-Life, PrBoom-plus, Fifth Wheel | CWSDPMI | bf6, G450 (G200 for conform) | raw, HIMEMX |
| DOSBench | CWSDPMI + DOS/4GW | bf6, G450 | raw |
| HX tools, hostile suite, loopa-selftest | Without GLOS | all | raw, HIMEMX |

- dx4 exercises VME/PVI; dx2 is the pure trap path. Every suite also runs once with direct mode forced, to
  find programs that need a profile.
- `tools/gate/run.py` runs each cell's baseline and GLOS jobs. Baselines are cached by (binary sha, profile,
  boot, card, 86Box build key).
- It compares:
  - the status;
  - HX-TEST, HX-IMG, DGL- and MGL- line multisets, after `normalize.toml` strips timing fields (HX-STAT,
    `ms=`, `fps=`, `t=`, `mem_free_kb=`);
  - dumped frames through `samepix`;
  - GLOS- lines: `GLOS-PANIC` fails, `DPMI-UNIMPL` must be on a whitelist, `vif-stuck` is reported.
- An SSH status probe must answer every 2 s of **guest time** (each reply carries the tick count, because
  86Box runs below real time). Each suite also gets one screenshot job and one kill job, and VECCHK/VMODE run
  afterwards.

---

## Risks for M0–M4, in mitigation order

1. **86Box CPU fidelity:** V86TEST and patches 0106–0108 come first (M0).
2. **IOPL 0 breaking DJGPP/SDL programs:** the HDPMI32i survey (M0); the `vif-stuck` watchdog; direct-mode
   profiles (M4e).
3. **Programs treating physical addresses as linear ones:** the reserved identity region (supervisor.md §4,
   §12.4); log #PF at BAR addresses.
4. **Borland binaries:** TPX.EXE confirmed by M2; the OW 16-bit suite covers the spec meanwhile.
5. **DOS/4GW's raw switch, 16-bit stacks and nesting:** espfix and 0305h/0306h in M4a; nesting in M4c.
6. **Our own SSH layer:** host tests against OpenSSH before Loop A.
7. **Breaking the shared harness:** `loopa-cfgcheck`; work only on the worktree branch.
8. **XMS takeover with `DOS=HIGH` and MS-DOS paths:** the HIMEMX boot uses `DOS=HIGH`; MS-DOS on the bench.
9. **Kill/restore leaving the machine dirty:** the hostile suite plus VECCHK after every job.
10. **486 cost of the tick and traps:** a configurable RTC rate; budgets from CPU models (PRD D28).
