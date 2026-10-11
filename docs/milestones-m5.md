# GLOS M5: the full agent and Loop B

The task-level breakdown of PRD §19's M5, in the form of [milestones-m0-m4.md](milestones-m0-m4.md): steps,
their tests, and what was found on the way. PRD sections: §7 (agent), §8.1 (NIC drivers), §8.2 (mDNS), §15 (H8).

**Exit (PRD §19):** a Loop A job driven entirely over SSH (keys, shots, logs) matches its `--keys`/`--shots`
equivalent. Bench: Loop B runs over SSH with BENCH.BAT as fallback, and DOSBench is within 3% of its numbers
without GLOS (P1).

## Plan (2026-10-11)

Each step is a commit with its tests.

- **A1 Work on the VM thread, and streams.**
  - `vm_post()`: a function the VM thread runs at its next return to user mode, as a kill does. A screenshot
    needs the program stopped between instructions, and a banked VBE mode needs BIOS calls, which only that
    thread can make. The poster waits, with a timeout: a direct-mode program with interrupts off never returns.
  - Streams in the ssh daemon: a built-in that produces output over time (a large screenshot, `--follow`) is
    serviced on every turn of the ssh thread while its channel has room.
- **A2 `glos shot` in VGA and VBE modes.**
  - The frame is copied on the VM thread into kernel frames (not the 4 MB heap: 1024×768×32 is 3 MB), then
    encoded and sent by the ssh thread.
  - Modes: text (as in M3); CGA 320×200×4 and 640×200×2; EGA/VGA planar 16-colour; 13h; unchained 256-colour
    ("mode X", told from the sequencer and CRTC); VBE with a linear framebuffer or banked.
  - A VBE mode's information comes from the BIOS when the mode is set: INT 10h 4F02h seen in V86 traps and in
    the DPMI host's real-mode calls, then a nested 4F01h.
  - The VGA state the reading changes is put back: the graphics controller's index and registers 4 to 6, the
    sequencer's index, the DAC's write index; the latches go through plane offset FFFFh.
  - The palette is read from the DAC at shot time, not from a shadow (PRD §7.5): trapping 3C8h/3C9h costs a
    trap per byte of every palette fade. A program stopped between the three bytes of one entry gets that
    entry wrong until it next writes the index.
  - PNG as a stream: 8-bit indexed or 24-bit RGB in deflate's stored blocks. `--raw`: a 16-byte header and the
    pixels.
  - T: SHOTTEST draws a known pattern in each mode; `glos shot` over ssh must equal the pattern computed on the
    host, on bf6 (the Matrox VBE BIOS), the S3 VBE card and the 486 profiles.
- **A3 `glos shot` in the modes MGA-Glide's HAL sets** (not through VBE): resolution, pitch, depth and start
  address from the card's CRTC, CRTCEXT and DAC registers; the frame from its framebuffer BAR.
  T: `glos shot` of MGA-Glide's HELLO and of a DOS-GL test equals the frame the program dumps itself.
- **A4 Input injection:** `glos key`, `glos type`, `glos mouse` in Loop A's `--keys` syntax, into the virtual
  8042's queue (in a direct-mode session into the chip, with D2h/D3h).
  T: KEYWAIT and MGA-Glide's MOUSETST driven over ssh give what `--keys` gives.
- **A5 Telemetry and the command table:** `glos log --follow`; `glos stat [--follow]` (JSON lines: CPU per
  thread, memory, IRQ and trap counts, network counters, the session and its DPMI context); `glos mem`,
  `glos net`, `glos kill <id>`, `glos reboot`. (`glos config` comes with the configuration schema, M9.)
  T: the ssh suite.
- **A6 NIC drivers:** RTL8139 (86Box's RTL8139C+) and Intel 8255x (82557, 82558), with their bus-master rings
  in kernel frames. T: `jobs.py net` and `ssh` on each card.
- **A7 `direct-tcpip` and `glos gdb`:** the SSH channel type behind `ssh -L`, to ports GLOS serves itself; a gdb
  remote stub for the session's program (registers, memory, INT3 breakpoints, single step, continue, detach).
  T: a gdb script through `ssh -L` breaks in a DJGPP program's `main`, reads a variable and lets it finish.
- **A8 mDNS:** lwIP's responder, the name from GLOS.CFG (`[net] hostname`, default `glos`). SLiRP carries no
  multicast to the host, so Loop A checks only that it starts; the bench checks the name resolves.
- **A9 The exit job, and the gate:** a Loop A job driven over SSH against its `--keys`/`--shots` twin; the
  gate's probe takes a real screenshot of every program.
- **H8 (in MGA-Glide, on a branch):** Loop B's SSH transport in `tools/bench/run.py`, BENCH.BAT kept as the
  fallback; a dry run on the virtual bench PC. Merged with the owner's approval. The bench half of the exit
  (DOSBench within 3%, P1) needs the bench.

## Status

- **A1 to A9 and H8 are done** (2026-10-11), and the gate passes; the bench half of the exit is left; as built in [supervisor.md](supervisor.md) §9.5 (posted work), §17.6
  (streams, forwarded channels and the command list), §17.7 (screenshots), §10 and §17.8 (injected input),
  §17.9 (telemetry), §10 (the network cards), §19.2 (the program debugger).
- **`make loopa-shot`** (`jobs.py shot`): SHOTTEST draws a picture the host can compute, in one mode a run, and
  waits; `glos shot` and `glos shot --raw` over ssh must both equal it; then the program is killed. Five cases,
  all passing: real mode on bf6 with the G200's and the S3's VBE BIOS and on the 486DX2 (no VME: every INT
  traps), and as a DPMI client (SHOTDJ) on both cards.

  | Modes | What they check |
  |---|---|
  | text, textb, text50 | the cell's height and the rows from the CRTC, blinking from the attribute controller, colours through its palette and the DAC |
  | cga4, cga2 | the CGA's two interleaved banks, 2 bits and 1 bit a pixel |
  | ega0d, ega0e, ega10, vga12 | four planes, read with the latches saved |
  | vga13, x, xpage, xsplit | 256 colours chained and unchained, the start address, the line compare |
  | vga13c | a program with its interrupts off for five seconds: the picture comes after a quarter of a second |
  | v101, v103, v105 | VBE 256 colours through the window, up to twelve banks |
  | v102 | VBE 800×600 in 16 colours: planar and banked |
  | v110, v111, v112 (and `l`) | 15, 16 and 32 bits a pixel, through the window and the linear framebuffer |
  | v118l | 1024×768 in 32 bits: a 3 MB frame |
  | v101s, v101d, v111p | the display start (4F07h), the 8-bit DAC (4F08h; the S3 BIOS has none: skipped), the scan line made longer (4F06h) |
  | v111z | a Matrox card zoomed by two the way MGA-Glide's HAL does it (SHOTDJ on the G200 only: the registers are in memory) |

- **`make loopa-input`** (`jobs.py input`), on bf6 and the 486DX2, both passing. In one run the same programs
  get their input first from Loop A's `--keys` (86Box's keyboard and mouse) and then from `glos key` with the
  same lists:
  - KEYWAIT reports the same key from both: Enter, and the grey Up arrow (an E0 code);
  - MGA-Glide's MOUSETST (CuteMouse on the PS/2 mouse, bf6 only: the 486 board has no mouse port) passes with
    both. The mickey counts are not compared: 86Box scales its monitor's by the window, and gave 270 by -110
    and 270 by -122 in two runs for 80 by -40; GLOS's came to 240 by -120, three times what was asked (the
    driver's doing), in proportion;
  - `glos type` of a line with shifted characters arrives through DOS as typed;
  - the kill hotkey typed with `glos key` kills (`reason=hotkey`);
  - in a direct-mode session DIRTEST's own IRQ 1 handler reads the injected key from the 8042 (`1c9c`);
  - a list or a character that makes no sense is refused, and so is a mouse move when no program has one on.

- **The ssh suite** (`make loopa-ssh`) checks A5 on its three machines, all passing: `glos stat`'s JSON with its
  counters moving and the session in it while a job runs; `--follow` giving a line a second, and the log's
  showing a command as it comes; `glos mem`, `glos net`; `glos kill SEQ` taking a waiting job out of the queue
  and killing the running one. Two more cases (bf6, 486DX2) for `glos reboot`: refused while a job runs, then
  the machine resets, boots and starts GLOS again, which answers.

- **A6:** `jobs.py net` (DHCP and a TCP echo) passes on the RTL8139 (bf6, 486DX2) and the 8255x (82557 and
  82558 on bf6, 82557 on the 486DX4); the whole ssh suite passes over each (bf6 with all three, the 486DX2 with
  the RTL8139), with the NE2000 cases as before.
- **A7:** `make loopa-gdbprog` (`jobs.py gdb`), bf6 and 486DX2, both passing: the host's gdb through
  `ssh -W` on GDBME, a DJGPP program. It stops in its start-up code; gdb breaks at `main`, reads a variable
  (42), sets it (1000), steps an instruction and lets go, and the program prints what the new value makes.
  Then let go at once; then run to its end under gdb, which hears of it. A forward to another port, or to
  the debugger unarmed, is refused. `make ssh-test` (the ssh layer natively against OpenSSH) has
  `direct-tcpip` too: 300 KB echoed through `ssh -W`, and beside a command on one connection.
- **A8:** `jobs.py net` on all ten of its cases: the responder starts under the name GLOS.CFG gives (one case;
  `glos` elsewhere) and answers `/SELFTEST`'s query for it with the interface's address.
- **A9, the exit job:** `make loopa-m5exit` (`jobs.py m5exit`), bf6 with the G200 and with the S3, both
  passing. SHOTDJ's `v112k` mode shows three pictures (640×480 in 32 bits), each until a key.
  - The twin runs under plain DOS (CWSDPMI) with Loop A's `--keys`, anchored on the program's lines, and
    86Box's own screenshots (`--shots`).
  - The other runs under GLOS's agent with nothing but ssh: `glos log --follow` tells when each picture is
    up, `glos shot` takes it, `glos key` sends the key.
  - The program's lines are the same from both, and each `glos shot` equals 86Box's screenshot pixel for
    pixel, and the picture the host computes.
- **A9, the gate:** the probe now takes `glos shot` up to six times in every program's run (after its 5th,
  25th and each 100th answer) and each must be a whole PNG; they are kept as `probe-shot-N.png`.
- **The gate** (`make gate`) on a4616a6: 133 parts, none failed, no `vif-stuck` reported and no ACK made up.
  The probes' pictures are the games at work (GTA in the street, GLQuake's and Half-Life's demos, 640×480).
- **H8** is in MGA-Glide (3a164b0, merged 2026-10-11 with the owner's approval; `deps.mk` pins it):
  `tools/bench/run.py` drives a bench PC through
  GLOS over ssh when it answers and through BENCH.BAT otherwise; `vpc.py start --glos` gives the virtual
  bench PC a GLOS. The dry run on vbench-g200 is in MGA-Glide's `docs/bench.md`: jobs over ssh, the
  fallback to the poller after `glos exit`, `C:\OUT` fetched by SFTP, a timed-out job killed without a reset.
  The bench half of the exit (DOSBench within 3%, P1) needs the bench.

### Found on the way

- **The kernel heap and the frame bitmap had no lock.** The ssh and net threads are preemptible, and the VM
  thread allocates too (every DPMI block, every page backed on first touch): a switch in the middle of
  `kmalloc` could corrupt the block list. Found by reading, while deciding where a capture may allocate; no
  failure was ever traced to it. Each call now runs with interrupts off (`kernel/mm/vmm.c`).
- **SFTP uploads could be corrupted** (since M3). A WRITE's data is read from the session's input buffer by
  the DOS server, later, on the VM thread; more input arriving meanwhile could move the buffer's contents, and
  the write then took the last 512 bytes of its 32 KB from 64 KB further on in the file. It needed input to
  keep coming faster than DOS wrote it, which the NE2000s never managed: the Intel 8255x's first ssh run
  showed it, one to three bad sectors in each 1 MB upload. Fixed in `kernel/ssh/sftp.c`.
- **A screenshot taken during a mode set was no picture.** The gate's probe got 320×480 of black from
  DOS-GL's first conformance test: the Matrox BIOS was halfway through setting 640×480, which takes it more
  than the quarter second a capture waited for the video BIOS, and when the wait ran out the CPU was in a
  timer handler on top of it. Mode sets are now counted from their start to their return (GLOS makes
  every one itself), and a capture waits for the count, two seconds at most.
- **M4e's open stall after a hotkey kill** was looked into with a trace of the virtual 8042 (kept:
  supervisor.md §10). After the kill the BIOS updates the keyboard's LEDs (the kill put the lock bits
  back), a few milliseconds after the hotkey's Esc. The command reaches the chip, and 86Box's keyboard
  gives no ACK, nor the Esc's release; the BIOS waits seconds, interrupts off, then asks for a resend,
  which brings the Esc again and a second kill with nothing to kill. GLOS's own traffic with the chip is
  clean in every trace, and writing the command byte less often changed nothing; the rate goes with the
  host's load (1 run in 90, then 8 in 150 beside a gate). GLOS now gives the program the ACK itself after
  20 ms. Whether silicon ever needs it is for the bench to show (`GLOS-KBC ack=made-up` in the log).
- **The gate's first run with the new probe** (main at 790636d, stopped at 114 parts passed) failed `sess`
  on that stall, and its probes' pictures showed the two things fixed above: 320×480 from a mode set under
  way, and black from games still loading 25 probes in (the probe now also asks later).
- **A3 turned out small.** MGA-Glide's HAL sets its modes through VBE (4F02h with the linear framebuffer),
  then changes the scan line's length (4F06h), flips pages (4F07h) and, for a small frame, sets the card's
  zoom. So A2's VBE path covers them once it asks the BIOS for the scan line's length and reads the zoom. The
  plan's test (a picture of MGA-Glide's HELLO against its own dump) is left to A9, where every gate program
  gets a screenshot: SHOTTEST's `p` and `z` modes check the same registers with a picture the host computes.
- **The Matrox BIOS clears the frame when a mode is set again with "don't clear"** (4F02h, bit 15) and the
  linear framebuffer bit; the S3's keeps it. SHOTTEST first drew through the window and then set the linear
  mode, and every such picture came back blank on the G200. It now draws straight into the framebuffer (INT
  15h 87h in real mode, a mapping as a DPMI client).
- **Mode 110h on the G200 says 16 bits a pixel** with 5:5:5 masks; the capture goes by the masks.
- **MGA-Glide's test programs end the emulator when they finish** (the shim's `hx_done` writes to 86Box's
  unit-tester device): one that runs as an agent job in the middle of a run takes `--noexit`.
- **The host's gdb is built for Linux**: it reads no DJGPP executable (nor does the host's objcopy, and
  DJGPP's makes no ELF), and with `set architecture i386` it writes Linux's `orig_eax` before it resumes a
  program. The test goes by `nm`'s addresses, and the stub lets writes to registers it hasn't pass.
- **A 486 needs one ssh connection for the suite** (OpenSSH's ControlMaster): with a key exchange for each of
  a hundred commands the run took longer than its 25 minutes.

### Open

- **The 8255x is slower than the others in 86Box:** the ssh suite's two 1 MB transfers take 6 to 8 s on
  it, 3.5 s on the NE2000s and 4.7 s on the RTL8139, with no errors or overruns counted. Not looked into:
  transfer rates are P5's, measured on silicon. The driver sends one frame at a time (a single transmit
  block) and takes an interrupt for each; a ring of blocks is the first thing to try if the bench agrees.
- **The bench half of the exit:** Loop B over ssh on a real PC, and DOSBench within 3% of its numbers
  without GLOS (P1).

### Left for later milestones

- **The remote-session indicator** (PRD D35, listed under M5) lives in the task bar, which M6 brings.
- `glos config get/set` comes with the configuration schema (M9); `glos shot --screen N` with a second
  screen (after v1); `glos gdb <pid>` with GLOS apps (M6).
- The DOS and GPU locks' times in `glos stat` come with those locks (M6).

### Limits (in supervisor.md §17.7)

- The four bytes at offset FFFFh of the VGA's planes are lost to the latches in an unchained mode.
- A font the program loaded is not seen (text is drawn with the ROM's); no cursor; pel panning not followed.
- A mode a program sets by writing the card's registers itself, outside INT 10h, is taken for what the VGA
  registers say. On a Matrox card that covers the HAL's zoom, not a mode timed from scratch.
- Direct-colour pictures have the framebuffer's values, not what a gamma ramp in the DAC makes of them.
