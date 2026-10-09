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

**M3 status (2026-10-02):**
- **Item 0 done.** `kernel/core/sched.c`; the system VM is a thread; VME on the Pentium II and iDX4 profiles
  (`/NOVME` turns it off). `make loopa-sched` (`/SELFTEST`: bulk at 30% of contended ticks, the urgent sleeper
  never late), `loopa-m2`, `loopa-hostile`, `loopa-m1`, the refusal and `loopa-gdb` pass on all six
  combinations.
- **Item 0a done.** `loader/stub.asm`. `make loopa-mem` passes on all six: GLOS takes 2,832 bytes (was
  38,192), and the largest program is 2,832 bytes smaller than under plain DOS. `SHELL=` mode
  (supervisor.md §2.2): `make loopa-shell` on the glosshell boots (MGA-Glide 556bf7a) checks AUTOEXEC.BAT's
  environment copied back, the console from GLOS.CFG, and the fallback to `COMMAND.COM /P` without a kernel;
  all six pass, and so do mem, sched, m2, hostile, m1, the refusal and gdb.
1. **PCI and NE2000.** `kernel/drv/pci.c` (enumeration, claiming) and `drv/nic/ne2k.c` (ISA and PCI), with the
   refusal rules (shared IRQ, a packet-driver signature on the same base).
2. Wait queues, timers and mutexes.
3. **lwIP.** `third_party/lwip` (BSD-3), `NO_SYS=1`, DHCP, the port in `kernel/net/`, and `THIRD_PARTY.md`.
   T: `tests/host/net_loop.c`.

   **Items 1-3 status (2026-10-02): done.** `kernel/drv/pci.c`, `kernel/drv/ne2k.c` (the RTL8029, or ISA with its
   IRQ found from the PICs' request registers), timed waits in the scheduler (`thread_wait`), lwIP 2.2.0
   vendored with its signature checked, and the net thread (`kernel/net/net.c`): the card's IRQ wakes it, and
   it sleeps until lwIP's next timer. `make loopa-net` (bf6 and 486DX2 with each card, 486DX4 with the
   RTL8029): DHCP gives 10.0.2.15, a line from the host comes back from the `/SELFTEST` echo service, and
   the Crynwr packet driver keeps GLOS off the ISA card. Instead of `net_loop.c`, Loop A's SLiRP is the peer.
   Kernel lines now wait for a program's half-written COM1 line (supervisor.md §18).
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

   **Items 4-6 status (2026-10-03): done** (supervisor.md §17.6).
   - **Keys:** the loader reads `KEYS\SEED.BIN`, `HOSTKEY` and `AUTHKEYS`; `kernel/core/random.c` is the
     SHA-512 pool with ChaCha20 fast key erasure, fed by IRQ timing.
   - **Crypto:** TinySSH's crypto vendored. `make host-test` runs RFC 7748, 8032 and 8439 vectors (extracted
     from the RFC texts), and so does `/SELFTEST` in the kernel.
   - **SSH:** `kernel/ssh/` (protocol, buffers, keys, the kernel glue). `make ssh-test` passes 12 checks against
     OpenSSH 9.6 on the host. `make loopa-ssh` passes on bf6 with each card and on 486DX2 with the ISA card:
     `glos ver`, `glos echo`, exit code 127 for an unknown command, three sessions at once, a stranger's key
     refused, and strict KEX.
   - **Found on the way:** the trap entry left DF as the interrupted code had it, so kernel string instructions
     could run backwards after an IRQ from DOS code mid `STD`; it now clears it. Thread stacks went to 16 KB.
     The double-fault report gained the TSS state, ESP0's page and the threads.
   - **A false hang:** the bf6 SSH runs "hung" after the last connection. The kernel was fine. The guest's
     `WAITSEC 240` ran at under half speed on a loaded host and outlasted run.py's 300 s wall-clock idle
     limit. The suite now ends the guest when the host's last command shows on COM1 (supervisor.md §19).
     A second harness fault: a rerun polled the previous run's `serial.log` and connected before 86Box was
     up ("Connection refused"); `jobs.py` now removes it first.
7. **Agent shell** in the resident stub (supervisor.md §17.3).
8. **Output capture** `kernel/dos/capture.c`.

   **Items 7-8 status (2026-10-03): done** (supervisor.md §17.3, §17.4).
   - **Headless mode:** GLOS.EXE without a mode option (or `/AGENT`). The stub halts between commands
     (NEXT's new answer 2), so DOS keeps its clock.
   - **Jobs:** `kernel/dos/agent.c` (capture lives there too, not in a `capture.c`). Programs are EXECed
     directly (current directory, then PATH, `.COM` before `.EXE`) for an exact exit code; anything else goes
     through `COMSPEC /C`. Four may queue.
   - **Capture:** INT 21h 02h/06h/09h/40h when the handle's SFT entry is the console (the loader now passes the
     List of Lists), INT 29h outside DOS; handle 2 is stderr.
   - **`glos exit`** leaves from headless mode.
   - **Cost:** the stub's wait loop adds 18 bytes: MEM /C now shows GLOS at 2,848 bytes (4,736 as the shell).
   - **T:** `make loopa-ssh` runs GLOS headless: `tests/dos/echoargs.c`'s stdout through every captured path,
     its stderr and exit code 7 exactly, a program found in the current directory, `dir` through COMMAND.COM,
     three DOS commands queued at once, and `glos exit` to end the run.
9. **SFTP.** `kernel/dos/idle.c` (safe points) and a port of OpenSSH `sftp-server.c` (ISC). 8.3 names only.

   **Item 9 status (2026-10-03): done** (supervisor.md §17.2, §17.6).
   - **The DOS server** (`kernel/dos/dos.c`) makes DOS calls from headless mode's idle stub (NEXT's answer 3),
     with a 32 KB transfer buffer that exists only while an SFTP session does.
   - **SFTP is GLOS's own** (`kernel/ssh/sftp.c`, protocol version 3) rather than a port of `sftp-server.c`:
     the port's Unix half (permissions, owners, links, `stat`) would all have been replaced, and the
     protocol half is small.
   - **SSH channels can pace their input** (`ssh_chan_hold`/`ssh_chan_consumed`), so uploads can't outrun DOS.
   - **Cost:** the stub's DOS call adds 124 bytes: MEM /C shows GLOS at 2,976 bytes (4,864 as the shell).
   - **T:** `make loopa-ssh`: 1 MB up and back by `sftp` and by `scp` with the same sha256, `ls -l`, rename,
     remove, mkdir and rmdir.
10. **Built-in commands.** Text-mode `glos shot` (B800h + BDA → a CP437 bitmap font rendered at build time →
    PNG via stb_image_write), plus `glos exit|ps|log|kill`.

    **Item 10 status (2026-10-03): done** (supervisor.md §17.6).
    - **`glos shot`** uses the video BIOS's own font (INT 10h 1130h, found by GLOS.EXE) instead of a font built
      in, and its own small PNG writer (stored deflate blocks) instead of stb_image_write. Both are simpler, and
      the picture matches the card. `tests/host/png_test.c` checks the writer.
    - **`glos log`** (a 16 KB COM1 mirror), **`glos ps`** (jobs, the DOS program in front, threads), **`glos
      kill`** (the running job, through M2's kill), **`glos exit`**.
    - **T:** `make loopa-ssh`: after `cls` and ECHOARGS, `glos shot`'s pixels match `tests/loopa/golden.txt`
      (`GLOS_GOLDEN=update` records them); `glos log` holds the kernel's lines and GLOS.EXE's from V86;
      `glos ps` lists the VM thread and the DOS program; `glos kill` ends a `WAITSEC 120` with status 255.
11. **MGA-Glide H7:** `run.py --ssh-steps FILE --ssh-key K` (exec/expect, put/get, shot). **Done** (MGA-Glide
    416fcda).

**M3 exit:** `make loopa-m3` on bf6 + `ne2kpci` and 486dx2 + `ne2k`:
- `tests/dos/echoargs.c`'s stdout, stderr and exit code 7 are captured exactly;
- a 1 MB file survives an SFTP round trip with the same sha256;
- the text-mode screenshot matches its golden PNG;
- `glos exit` leaves VECCHK OK.

Handshake time is logged, not judged (PRD D28).

**M3 exit status (2026-10-03): done.** `make loopa-m3` passes on bf6 with each card and on 486dx2 with the ISA
NE2000 (the SFTP round trip takes about 5 s on bf6 and 27 s on the 486DX2).
- **Item 11 (H7) is in MGA-Glide 416fcda:** `run.py --ssh-steps FILE --ssh-key KEY` runs `wait`, `exec`,
  `expect rc|out|err`, `put`, `get` and `shot` steps against GLOS. Checked with GLOS on bf6 and the 486DX2.
- **86Box patch 0111** came with it: the emulator ignores SIGPIPE. Before it, a client closing its connection
  while SLiRP wrote could kill 86Box (status CRASH, exit -13).
- `deps.mk` pins MGA-Glide 416fcda.

---

## M4: DPMI host, in five sub-milestones

| Sub | Scope | Tests | Exit |
|---|---|---|---|
| **M4a** | 32-bit basics: `kernel/dpmi/{host,int31,ldt,mem,rmcall}.c`; 1687h and the mode switch; contexts (supervisor.md §12); 0000h–000Dh, 0100h–0102h, 0200h/0201h, 0204h/0205h (vectors), 0300h–0302h, 0305h/0306h, 0400h, 0500h–0503h, 0600h–0604h, 0800h/0801h, 0900h–0902h, 0A00h; PSP and environment selectors; INT 21h 4Ch teardown; espfix | DPMICONF-32 (`tests/dos/dpmi/`, DJGPP; first validated against CWSDPMI and HDPMI32i — a test that fails on both is wrong); MGA-Glide DJGPP HELLO and DOS4GW HELLO | `make loopa-m4a` |
| **M4b** | Exceptions (0202h/0203h, 0210h–0213h), the locked stack, frame edits; protected-mode-first IRQ delivery and the IRET trampoline; 0303h/0304h with the pass-up loop guard; INT 31h reentrant from IRQ handlers; 0507h; 0E00h/0E01h, IRQ13/INT 75h; crash reports (`kernel/dbg/crash.c`, `tools/symcrash.py`) | djtst205 (ctrlc, fault, fpu, hang, infoblk, raise, signals, timer, null, brk, multispn, nearptr, hwint); DPMICONF exception, IRQ and RMCB tests; MGA-Glide STACKPG, MOUSETST, JOYTEST, SBBEEP | `make loopa-m4b` |
| **M4c** | DOS/4GW and retail: 000Bh/000Ch Big-bit changes; INT 2Fh 1600h/160Ah/1680h–1682h/1686h/4310h; VCPI and EMS absent; 0D00h failing cleanly; nested contexts; 0401h, 0508h/0509h, 0B00h–0B03h | ecm dpmitest; the HDPMI regression suite (against HDPMI32i); MGA-Glide conform 27×4 and replays; GTA; Screamer Rally; DOSBench (BENCHGL, BENCHG, DBMENU); nesting: DPMICONF `nest`, djtst205 MULTISPN 3 | `make loopa-m4c` |
| **M4d** | 16-bit clients: 16-bit frames, stacks, RMCBs, raw switch; espfix on every return | DPMICONF-16 (`tests/dos/dpmi16/`, OW 16-bit, reporting over COM1) against HDPMI16; TPX.EXE from `$(BORLAND_DIR)` on RTM/DPMI16BI, driven by `--keys` (open a file, compile, exit) | `make loopa-m4d` |
| **M4e** | Exclusive sessions (`kernel/vm/session.c`): save and restore, kill; per-program profiles (`direct=1` for IOPL 3); `glos run`; `tools/gate/` | The full gate | `make gate` |

**M4a status (2026-10-03): done** (supervisor.md §12.1a, §13).
- **The host:** `kernel/dpmi/{host,ldt,mem,rmcall,int31}.c`.
  - The mode switch through 1687h's ARPL, for 16- and 32-bit clients.
  - Contexts with their own page directory and an 8192-entry LDT.
  - The virtual IDT with host defaults in a trampoline page.
  - Nested real-mode calls on the VM thread; IRQs reflected to real mode while a client runs; espfix.
  - INT 21h 4Ch teardown, and a kill that works from protected mode.
- **Functions:** every one listed for M4a. 0202h/0203h are kept and 0303h/0304h allocated for M4b to deliver,
  and 0E00h/0E01h are done. Unknown ones log `GLOS-DPMI-UNIMPL` and fail with 8001h; 0506h and 0507h (page
  attributes) are the ones DOS/4GW and DJGPP ask for, and both carry on.
- **T: `make loopa-m4a`** (`jobs.py dpmi`):
  - DPMIMINI (`tests/dos/dpmimini.asm`) as a 16- and 32-bit client: mode switch, 0400h, LDT and 0501h memory,
    0300h, a reflected INT 21h, 0A00h, espfix, and exit code 7 from protected mode. HDPMI32i proves it, and it
    passes on all six profile and boot combinations.
  - DPMICONF-32 (`tests/dos/dpmiconf.c`, 37 checks): 0 failures on CWSDPMI r7, HDPMI32i and GLOS, on all six.
  - MGA-Glide's DOS/4GW and DJGPP HELLO on bf6: the same HX-TEST and HX-IMG lines (CRCs) with and without GLOS.
- **Cost:** the stub's DPMI ARPLs add 42 bytes; MEM /C shows GLOS at 3,008 bytes (4,896 as the shell).

**M4b status (2026-10-04): done** (supervisor.md §12.1b, §13, §14, §19).
- **The host:** `kernel/dpmi/deliver.c`:
  - entries that keep what a handler interrupted and return through TR_RET+n;
  - the locked stack;
  - IRQs to the client's handler first, from either mode; INT 1Ch/23h/24h passed up (an unhooked INT 23h is
    ignored, as CWSDPMI does);
  - exceptions with the 0.9 and 1.0 frames and frame edits; real-mode callbacks and the pass-up guard.

  Also: `kernel/dbg/crash.c` and `tools/symcrash.py`; 0506h/0507h and 0210h/0212h; a stub ARPL at the
  client's terminate address, so a DOS abort frees the context and restores the vectors as a kill does;
  CR0.NE kept clear.
- **T: `make loopa-m4b`:**
  - **DPMICONF-32** gains 22 checks (exceptions, frame edits, page attributes, PM IRQs from both modes, INT 31h
    inside an IRQ handler, the IRET's virtual IF, INT 1Ch passed up, the FPU error, callbacks, the GLOS-only
    pass-up guard). 0 failures on CWSDPMI r7, HDPMI32i and GLOS, on all six profile and boot combinations.
  - **djtst205** (`jobs.py djtst`): 17 of DJGPP 2.05's own tests run by RUNOUT (`tests/dos/runout.c`), which
    sends their output to COM1. On all six combinations GLOS matches CWSDPMI's exit codes and output: the fault
    messages with their EIPs, Ctrl-C (HANG, CTRLC), SIGALRM and SIGFPE (SIGNALS), the PIT (TIMER, UCLOCK).
  - **ENABLE** reads IF with PUSHF, which shows the real IF at IOPL 0, so it stops at its first check. That is
    expected under PRD D20 until direct mode (M4e).
  - **CRASHME** faults with no handler (a page fault on the null page, a #GP): GLOS writes both crash reports
    (the log and `C:\GLOS\CRASH\`), exits 255 with VECCHK OK, and `symcrash.py` names `crash_here` and
    `crash_gp`.
  - **MGA-Glide's STACKPG, MOUSETST, JOYTEST and SBBEEP** (`jobs.py dpmitools`): the same HX-TEST results with
    and without GLOS, and SBBEEP's 440 Hz recorded under GLOS.
- **86Box:** the dynarec checks no segment limit on loads (supervisor.md §20), so DJGPP's Ctrl-C and SIGALRM
  trick never faults in a read-only loop, on any host. Local patch 0112 fixes it (MGA-Glide 81c0686, merged and
  pushed with the user's approval; deps.mk pins it), with V86TEST case V, which fails without it on all three
  profiles. djtst runs every test with the dynarec on again.
- **Test notes:** SIGNALS is held to its own ending (the deliberate SIGFPE), since one CWSDPMI run died of a GPF
  in its signal storm; the baseline batch is CALLed, so RUN.BAT goes on to HX-DONE.
- **Found on the way:**
  - M4a cleared the virtual IF for software INTs to a client's handler, which the handler's IRET could never
    restore.
  - HDPMI32i lets DOS abort a DJGPP program on Ctrl-C (and then crashes the next one); CWSDPMI doesn't, and GLOS
    follows CWSDPMI.
- **Deferred:** per-client FPU state and NE=1 (the FPU's second user); multispn's nested runs (M4c);
  0211h/0213h; the 1.0 frame for 16-bit clients (M4d).

**M4c status (2026-10-04): done** (supervisor.md §9.2, §12.1c, §13, §20).
- **The host:**
  - client levels (`kernel/dpmi/level.c`): a client's child that switches to protected mode shares its
    context, and its end frees what its level made and puts back the parent's handlers;
  - each level's terminate address;
  - INT 2Fh as a link in the V86 IVT chain, so lDebugX can hook 1687h;
  - INT 41h ignored; 1680h a real yield;
  - DPMI 1.0's 0401h, 0504h/0505h, 0508h/0509h, 050Bh and 0B00h–0B03h;
  - 0E01h's MP/EM per client;
  - the 1.0 exception frame's own return path and its PTE field;
  - for debugging, an `options` key in GLOS.CFG's `[shell]` section (a SHELL= line has no room for
    `/DPMITRACE`), and the exception trace now gives the faulting address.
- **Found by the new suites:**
  - 0301h ran the procedure with IF from the structure's FLAGS (zero), and nested V86 calls never set VIF.
  - The emulated FLAGS image let ID toggle on a 486DX2, because 86Box's IRETD to V86 mode doesn't mask it, so
    lDebugX ran CPUID into #UD. That image now shows IOPL 3, as VME's does.
  - Answering 1680h with AL=0 made DJGPP's `uclock()` wait for a tick with interrupts off (JOYTEST hung). AL
    now stays 80h, as on plain DOS.
  - A pending single-step trap reached the kernel through an ARPL #UD (an 86Box deviation; the stray #DB is
    dropped).
  - GTA's DOS/4GW code loads selector 0040h, which was GLOS's ring-0 16-bit data selector, so the game died
    with a #GP (exit code 209). 0040h is now the BIOS data selector (supervisor.md §3.1).
  - GTA's menus come up a few seconds later under GLOS than the 35 s the job's last Enter allowed; the job
    now presses Enter until 65 s.
- **T: `make loopa-m4c`:**
  - **loopa-m4b**, with DPMICONF's `nest` (a child that keeps a vector hooked, one that faults) and 1.0 checks,
    and djtst205's MULTISPN running itself 3 times through `system()` (each child a level), on all six
    combinations.
  - **HDPMI's regression suite** (`loopa-hdpmireg`), 69 tests under HDPMI32i and GLOS. On bf6, 30 are the same,
    9 differ only in selectors and 30 differ for listed reasons (`HR_KNOWN`: HDPMI's crash dump and INT 21h
    translation API, IOPL 0's PUSHF, 0305h's empty state, HDPMI refusing nested clients). The 486DX2 gives
    29/10/30. Any unlisted difference fails.
  - **lDebugX** stepping ecm's dpmimini into protected mode (`loopa-ecm`): the same output as under HDPMI32i on
    bf6, 486DX2 and 486DX4.
  - **Under GLOS as the shell** (`loopa-m4c-games`, `tools/m4c-games.sh`):
    - MGA-Glide's conform, 27/27 on G100, G200, G400 and G450;
    - the GTA and Screamer Rally replays on G200, G400 and G450;
    - GTA and Screamer Rally themselves, against runs without GLOS;
    - DOSBench's Loop A job (BENCHGL on DJGPP, BENCHG on DOS/4GW, DBMENU).
- **Deferred:**
  - an 86Box patch and V86TEST case for the stale single-step trap (MGA-Glide, with the user's approval);
  - watchpoints checked on silicon (86Box fires no DR breakpoints);
  - mixed-bitness levels (a 16-bit child of a 32-bit client) with M4d;
  - XMS 4309h.

**M4d status (2026-10-08): done** (supervisor.md §12.1d, §13, §20).
- **The host:**
  - every handler keeps its own client's bitness: frames, returns and a locked-stack selector of each bitness;
  - levels of either bitness (a 16-bit child of a 32-bit client and the reverse);
  - 0210h/0212h for 16-bit clients (the 1.0 frame as HDPMI16 lays it out);
  - INT 2Fh 168Ah's "MS-DOS" entry (0100h: a read-only selector for the LDT, whose pages now hold only it);
  - clients that go resident with INT 21h 31h keep their context until the program they returned to ends;
  - DOS API translation for 16-bit clients (`kernel/dpmi/dosx.c`): selectors for segments, buffers through an
    8 KB block of DOS memory.
- **Why the extras:** Borland's RTM, under TPX, needs them. It asks for "MS-DOS"'s LDT selector and won't start
  without one. It goes resident from inside its own EXEC and comes back through raw switches. It calls DOS from
  protected mode with selectors (50h with its PSP's selector, 34h and 5D06h for the InDOS flag).
- **T: `make loopa-m4d`:**
  - loopa-m4c;
  - **DPMICONF-16** (`tests/dos/dpmi16/`, Open Watcom C and WASM, 82 lines, 77 judged under GLOS: descriptors, DOS
    memory tiled for a 16-bit client, real-mode calls, vectors, memory, exceptions with both frames, IRQs and INT
    1Ch passed up, the FPU error, callbacks, a raw round trip, espfix, nested clients, the DOS translation): 0
    failures on HDPMI16, HDPMI16i and GLOS on all six profile and boot combinations, with GLOS's mixed-bitness
    nests both ways (DPMICONF-32 gains `glos-nest-16in32`);
  - **TPX** on RTM, without GLOS and under it on bf6 and the 486DX2: it opens `tests/borland/hello.pas`, compiles
    and runs it (Ctrl-F9), and the program reports over COM1. Alt-X ends TPX with code 0.
- **86Box:** patches 0113–0115 (a faulting instruction takes no single-step trap; IRETD loads only the model's
  EFLAGS bits; the debug registers built in), found by M4c and prepared on a branch with V86TEST cases W–Y, merged
  into MGA-Glide main as a8684e5 with the user's approval. deps.mk pins it. With them, HDPMI's EXC01MZ matches
  (its "single-step routing" difference was 86Box's stale trap, not HDPMI's; HR_KNOWN loses it).
- **DR breakpoints in Loop A:** with 0115, DPMICONF's `watch` is a check again (CWSDPMI, HDPMI32i, GLOS). It
  found GLOS dropping the RF owed to an execute watchpoint's return: `db_rf` was spent entering the client's
  exception handler, so the breakpoint fired forever. It also found two test bugs: a non-volatile `exc_or_flags`,
  and the shared handler's EBX from an earlier check.
- **Also:** GLOS.EXE's `/RUN` stack smash (`_searchenv()` given 80 bytes for `_MAX_PATH`), fixed separately
  (d697999); the DPMI trace gained raw switches, translated DOS calls, a client's real-mode DOS calls, and the code
  and registers at a fault.
- **Deferred:**
  - translation of EXEC (4Bh) and the other pointer functions dosx.c logs;
  - a resident client at a level above the first;
  - writes through "MS-DOS"'s LDT selector (none seen).

**M4e plan (2026-10-09).** Each step is a commit with its tests.
- **E1 Sessions** (`kernel/vm/session.c`, supervisor.md §11):
  - A session is a top-level program: an agent job, a `/RUN` program, or a program the shell-mode COMMAND.COM
    EXECs. Nested EXECs belong to it.
  - It begins at that EXEC (`GLOS-SESSION begin`) and ends when the program does (`GLOS-SESSION end`).
  - At the end GLOS restores:
    - the video mode it began in (INT 10h);
    - the virtual PIC's masks, PIT channel 0 (mode 2, FFFFh), the virtual RTC A/B, the 8042 command byte and
      the keyboard LEDs;
    - Sound Blaster DMA stopped (8237 masks; a DSP reset at BLASTER's port).
  - It also restores interrupt vectors the session changed that now point into free DOS memory (a TSR's stay),
    and sets DOS's time from the RTC.
  - T: hostile programs and a new SESSTEST (changes each of these and exits) leave VECCHK, VMODE and the time
    clean.
- **E2 Profiles:**
  - `[program NAME.EXE]` sections in GLOS.CFG (the PRD's one settings file), read by GLOS.EXE into bootinfo.
  - Keys: `direct = 0|1`, `env = NAME=VALUE` (repeatable: added to the EXEC's environment block), `memory = KB`
    (caps 0500h/0501h).
  - A session takes the profile of the program that begins it.
  - T: env and memory seen by a DJGPP probe; a missing profile changes nothing.
- **E3 Direct mode** (supervisor.md §9.7, D20): a session with `direct = 1` runs at IOPL 3, in V86 mode and
  protected mode.
  - CLI/STI/PUSHF/POPF/IRET act on the real IF.
  - V86 INT n arrives through the IDT and is reflected as before.
  - Protected-mode code bypasses the I/O bitmap (it owns the PIC, PIT and 8042 as under CWSDPMI). GLOS puts its
    own lines' masks and the RTC's periodic interrupt back at every trap. A program that re-initialises the
    PIC isn't supported.
  - T: djtst205's ENABLE passes under a direct profile; DPMICONF-32/16 and the games pass with direct forced.
- **E4 `glos run [--exclusive|--app] [--direct] [--profile NAME] COMMAND`:** a job with session options;
  `--app` waits for M6. T: the ssh suite.
- **E5 Kill** reports `reason=` (hotkey, agent, crash) and runs the session-end restoration.
- **E6 The gate** (`tools/gate/`, the matrix below): each cell's baseline and GLOS runs, baselines cached by
  binary sha, profile, boot, card and 86Box key, the comparisons below, forced-direct runs, a kill job and a
  screenshot job per suite, and VECCHK/VMODE after each. T: `make gate`.

**M4e status (2026-10-09): E1-E3 done.**
- **E1 sessions** (02020d5; supervisor.md §11.1). Each top-level program is a session: the stub's EXECs (agent
  jobs, `/RUN`) and, as the shell, the console COMMAND.COM's (FreeCOM is its own parent). An EXEC of a file that
  isn't there starts none (the agent tries each place along PATH). The end puts back the video mode, PIT channel
  0, the virtual PIC, RTC A/B, the 8042 command byte and the lock bits; it stops Sound Blaster DMA and resets
  the DSP at BLASTER's port; it puts back vectors left in freed memory; and it sets the BIOS clock from the RTC.
  T: `make loopa-sess` (on the host): SESSTEST changes each of these natively, and leaves none changed under
  `/RUN`, as the shell, or as an agent job over ssh. 20 runs pass across bf6/486DX2/iDX4, raw and HIMEMX. The
  hostile suite also checks the clock afterwards.
- **E2 profiles** (supervisor.md §11.2): GLOS.CFG's `[program NAME.EXT]` sections (env, memory, direct), read
  in every mode. A session takes the profile of the program that begins it. T: PROFCHK sees its profile's
  variable and PATH and gets 3,328 KB under a 4 MB cap; as NOPROF.EXE it gets neither (about 62 MB).
- **E3 direct mode** (supervisor.md §9.7): sessions at IOPL 3 by profile or `/DIRECT` (also `SET
  GLOS=/DIRECT`).
  - The real IF stands for the virtual IF: it is read at each entry and written back in `trap_exit`.
  - V86 INT n arrives through the DPL-3 gates.
  - The virtual PIC ends what it delivers at once.
  - GLOS takes back its own IMR lines and the RTC rate at entries.
  - T: djtst205's ENABLE passes under a direct profile (and still stops without one). DIRTEST sees IOPL 3, keeps
    an IRQ 0 handler that EOIs the PIC itself, and loses the keyboard line and the RTC rate back to GLOS.
    SESSTEST at IOPL 3 leaves nothing behind. All three profiles.

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
