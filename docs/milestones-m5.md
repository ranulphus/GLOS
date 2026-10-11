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

- **A1 to A3 are done** (2026-10-11); as built in [supervisor.md](supervisor.md) §9.5 (posted work), §17.6
  (streams) and §17.7 (screenshots).
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

### Found on the way

- **The kernel heap and the frame bitmap had no lock.** The ssh and net threads are preemptible, and the VM
  thread allocates too (every DPMI block, every page backed on first touch): a switch in the middle of
  `kmalloc` could corrupt the block list. Found by reading, while deciding where a capture may allocate; no
  failure was ever traced to it. Each call now runs with interrupts off (`kernel/mm/vmm.c`).
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
- **A 486 needs one ssh connection for the suite** (OpenSSH's ControlMaster): with a key exchange for each of
  a hundred commands the run took longer than its 25 minutes.

### Limits (in supervisor.md §17.7)

- The four bytes at offset FFFFh of the VGA's planes are lost to the latches in an unchained mode.
- A font the program loaded is not seen (text is drawn with the ROM's); no cursor; pel panning not followed.
- A mode a program sets by writing the card's registers itself, outside INT 10h, is taken for what the VGA
  registers say. On a Matrox card that covers the HAL's zoom, not a mode timed from scratch.
- Direct-colour pictures have the framebuffer's values, not what a gamma ramp in the DAC makes of them.
