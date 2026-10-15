# GLOS M8: windowed DOS-GL and SDL3

The task-level breakdown of PRD §19's M8, in the form of [milestones-m7.md](milestones-m7.md). PRD sections:
§9.5 (VRAM), §9.6 (GPU ownership), §9.7 (windowed DOS-GL), §10.3 (DOS-GL), §10.4 (SDL3), §10.5 (the mixer),
Appendix B (the cross-repo changes); decisions D6, D12, D24, D43; questions Q12 and Q15.

**Exit (PRD §19):**
- DOS-GL conformance run windowed gives render surfaces identical to full-screen;
- the same EXEs still pass on plain DOS;
- Fifth Wheel and PrBoom-plus run windowed with zero underruns (SDL patch 0005's counter);
- a forced GL hang is recovered by the watchdog.

## What M8 adds, in one paragraph

A DOS-GL program started on a GLOS desktop opens a window instead of taking the screen. Its colour, depth and
texture memory are blocks of the card's memory that GLOS gives it; it draws with the card's engine itself, from its
own copy of the HAL, as it does on DOS; a swap tells GLOS which of its two colour buffers the window now shows,
and the desktop's engine copies that into the window's place on the screen. The engine has one owner at a time:
an app becomes the owner by asking (or by touching the card's registers, which are not mapped for it until then),
and stops being the owner when the desktop needs the engine and the app is at a point where its turn may end. An
app that keeps the engine too long while the desktop waits has the engine reset under it and is ended. GLOS
drives the Sound Blaster 16 itself and mixes the apps' sound into it. SDL3's DOS port gains GLOS drivers for
video, input and sound, so an SDL game such as Fifth Wheel or PrBoom-plus, rebuilt but unchanged, runs in a
window with its sound mixed by GLOS. On plain DOS, and on a desktop that can't show GL in a window, the same
programs do what they do today.

## Decisions taken here (proposals)

- **The GPU lock (D24, worked out: PRD D44).** The engine's owner is a kernel thread (the desktop's composer, the
  test card, `glos shot`) or an app. An app becomes the owner with `GLOS_FN_GPU_LOCK`, or by touching the card's
  registers: their pages are mapped into the app only while it owns the engine, so the first touch faults and
  the fault makes it the owner (waiting for the engine if need be). The app does not give the engine back at
  its swaps. Its turn ends when someone else wants the engine **and** the app is at a *safe point*: inside a
  GLOS API call or a DOS or BIOS call (while it waits for the system VM), *quiet* (it says so in the shared page,
  around long work without the engine: added in step D), or ended; a session waits for its next safe point. A safe
  point is never in the middle of the HAL's work, so the app's HAL never finds its register shadows wrong
  half-way through a sequence of writes: before each piece of engine work DOS-GL looks at a page GLOS shares
  with it (`held`, and `lost`, a count of the times someone else had the engine since it last did), takes the
  lock if it doesn't hold it, and resynchronises (`engine_resync`, and its own GL state marked unknown) if the
  count moved. *Why not release at each swap, as PRD §9.6 first said:* a game holds the engine from its first
  draw to its swap, which covers level loads and texture uploads that take seconds; the desktop would freeze for
  them. *Why not take the engine back at any time:* an app interrupted between two register writes would go on
  with the engine in someone else's state.
- **Handing the engine over.** Between two owners the engine is drained (a second at most; then the HAL resets
  it, and an app that left it hung is ended). The kernel's own HAL resynchronises when it gets the engine back
  from an app. The hardware cursor and the display start are not the engine's (the RAMDAC's position registers,
  the CRTC): they move whoever owns the engine, so the pointer keeps moving through a long GL frame (§9.6).
- **The watchdog.** When a kernel thread has waited for the engine longer than the limit (`[display]
  gpu_watchdog`, milliseconds, default 500) while an app holds it outside a safe point, GLOS logs
  `GLOS-GPU watchdog`, resets the engine and ends the app. Nobody waiting, nothing to recover: an app may hold
  the engine as long as no one else needs it.
- **Apps' VRAM (D43's M8 half).** `GLOS_FN_VRAM` gives an app blocks of the card's memory from the top down;
  the screen, the cursor and the desktop's windows keep theirs from the bottom. An app sees only its own blocks,
  mapped at their own offsets in a view of the card's memory (so DOS-GL's offsets for the engine and its CPU
  addresses stay one number). Before a session or another mode the apps' blocks are copied into the system's
  memory, and back to the same offsets afterwards (the apps are paused meanwhile, and their `lost` counts move);
  an app whose blocks don't fit beside the new screen is ended. An app's end frees its blocks.
- **A window can show an app's VRAM.** `GLOS_FN_WINVRAM` makes a window's pixels a block of the app's VRAM, in
  the screen's format; the composer copies from there with the engine as from the desktop's own windows. A swap
  is `GLOS_FN_WINVRAM` with the other buffer: the window shows it, nothing is copied by the CPU, and the screen
  follows when the composer next runs.
- **Windowed GL needs a 16-bit desktop on a Matrox card** (capability bit `GLOS_CAP_GLWIN` of
  `GLOS_FN_VERSION`). DOS-GL draws in RGB565 and the engine's copies don't convert. On an 8-bit or 32-bit
  desktop, or the S3, DOS-GL and SDL programs run full-screen as sessions, exactly as on plain DOS. (PRD §9.2
  promised 32 bits too: DOS-GL could convert into a 32-bit window with `engine_present`; noted for later.)
- **Q15 answered: GL windows have a fixed size** (`GLOS_WIN_FIXED`). A program changes size as it does on DOS:
  a new context. Nothing is scaled.
- **Q12 answered: SDL keeps its cooperative threads** inside each app, as §10.4 has it for v1; GLOS preempts
  between apps. No SDL call may block the app's thread in GLOS for longer than a sleep it asked for.
- **The mixer.** GLOS drives an SB16 (DSP 4.x) itself while the desktop runs: 16-bit stereo, auto-initialised
  DMA on the high channel, at `[sound] rate` (default 22050 Hz), refilled in halves of 256 frames from the
  card's interrupt. Each app may open streams (`GLOS_FN_AUDIO`): a ring of 16-bit samples in pages shared with
  the app, its rate and channels, a volume. The mixer converts rates (linear, in integers), mixes with clipping,
  and counts a stream's *underruns*: periods in which a stream that had begun ran short. A session gets the
  card: the mixer stops it before and starts it again after. BLASTER (or `[sound] blaster`) says where it is.
- **SDL3 gets GLOS drivers** (DOS-GL's SDL patches 0008 on): video `glos` (windows: software ones through the
  window's pixels and `GLOS_FN_PRESENT`, OpenGL ones through DOS-GL's GLOS path), input from GLOS events, audio
  `glos` (a stream), the gameport read through GLOS (port 201h is passed through to apps). SDL chooses `glos`
  when GLOS answers with `GLOS_CAP_GLWIN`; otherwise its DOS drivers, which now leave a GLOS machine alone only
  in the way a session needs. The underrun line keeps patch 0005's form: `GLOS: U of N chunks underran`.
- **DOS-GL's GLOS path** (`src/dgl/glos.c`): at `dglInit`, GLOS with `GLOS_CAP_GLWIN` and no `DGL_FULLSCREEN`
  means a window: become an app (or already be one), the chip from GLOS (no PCI probing, no VBE), a window of the
  context's size titled with the program's name, VRAM blocks for the two colour buffers, the depth buffer and the
  texture heap (`DGL_TEXHEAP_KB`, default all GLOS gives up to 4 MB), the lock taken before every piece of engine
  work, swaps through `GLOS_FN_WINVRAM`. No mode set, no palette, no display start, no text mode at the end.

## The steps

Each step ends with its tests passing; the gate runs at the end. Work in other repositories is on branches in
worktrees (`~/wt-dosgl-m8`, branch `m8-glos`, from DOS-GL main; SDL patch work in `~/wt-sdl-glos`), never in the
shared checkouts; nothing is merged into their mains or pushed without the owner's approval.

- **A. The HAL in DOS-GL.** `make sync-hal` in the DOS-GL worktree to MGA-Glide main 1e1d08e (86Box patches
  0113-0117, `engine_resync`, the 2D primitives). T: DOS-GL conformance on the G200, G400 and G450 and `loopa-sdl`
  as before (D27: a HAL sync is checked like any change).
- **B. The GPU lock in the kernel** (`kernel/gfx/gpu.c`; `mga.c` uses it). Owners, safe points, the fault on the
  registers' pages, the shared page, the drain between owners, the watchdog; the cursor and display start outside
  it. API: `GLOS_FN_GPU` (the chip, the shared page, the registers' address in the app), `GLOS_FN_GPU_LOCK`,
  `GLOS_FN_GPU_UNLOCK`. `glos display` shows the owner and the hand-overs.
  T: host tests of the lock's rules; GPUTEST, a DJGPP app with its own copy of the HAL: fills and copies in its own
  VRAM while the desktop moves windows, its pixels read back and right; two at once; a touch of the registers
  without the lock; the desktop's goldens unchanged with them running; `GPUTEST --hang` ended by the watchdog,
  the desktop drawn again after.
- **C. Apps' VRAM and windows that show it.** `GLOS_FN_VRAM` (alloc, free), the view, `GLOS_FN_WINVRAM`; out to
  memory and back for sessions and modes; freed at the end. T: GPUTEST's windows in VRAM, a session between, the
  frames before and after equal; ten GPUTESTs ending with blocks held, the VRAM free again.
- **D. DOS-GL's GLOS path** (DOS-GL). As above, with `dglEnumModes` giving windowed sizes that fit the screen.
  T: the conformance tests on plain DOS (unchanged, three cards); the same EXEs windowed on GLOS desktops at
  16 bits on the G200, G400 and G450, their frames identical to the full-screen ones (GLOS's `glwin` suite); on
  a 32-bit desktop they run full-screen and pass.
- **E. The mixer** (`kernel/drv/sb16.c`, `kernel/snd/mixer.c`). The SB16 driver, streams, rate conversion, volume,
  underrun counts, `glos audio`; the card given to sessions and taken back. T: AUDTEST, a DJGPP app that plays
  tones through a stream: the tones in 86Box's recording, no underruns; two at once; a session that plays through
  the card itself in between.
- **F. SDL's GLOS drivers** (DOS-GL's SDL patches 0008 on; port 201h for apps in GLOS). T: `loopa-sdl` on plain
  DOS unchanged; its checks windowed under GLOS (info, keys, beep and busy through the mixer with no underruns,
  gl, modes, crash, exit).
- **G. The games.** Fifth Wheel (its source unchanged, built against the M8 DOS-GL) and PrBoom-plus (GL) windowed
  on G200 and G450 16-bit desktops: Fifth Wheel's checked run (its hash as on DOS), PrBoom's GL timedemo (its
  gametics and checksum as on DOS), both with `GLOS: 0 of N chunks underran`; on plain DOS their suites as
  before.
- **H. The exit.** The four exit checks in one place (`make loopa-m8`), the regression, the gate, the docs
  (graphics.md, PRD v0.6).

## Status

- Planned 2026-10-15.
- **A done** (2026-10-15, DOS-GL branch `m8-glos` e76d1b7): `make sync-hal` to MGA-Glide 1e1d08e. DOS-GL
  conformance 27/27 and `loopa-sdl` 10/10 on the G200, G400 and G450. No change to MGA-Glide was needed: the HAL
  already had `engine_resync` and the 2D primitives (M7).
- **B and C done** (2026-10-15, GLOS fd89eca): `kernel/gfx/gpu.c`, apps' VRAM, `GLOS_FN_WINVRAM`. `make loopa-gpu`
  passes on the G200, G400, G450 and the 486DX2 with a PCI G450 (two GPUTESTs at about 48 frames a second each,
  every frame right, every frame's turn taken from them by the composer; a session between frames; the watchdog
  ending a hung app after 500 ms; VRAM back; refused at 8 bits). Found on the way: an ending app kept the engine
  until the watchdog (it is released at the end now); a struct grown under objects built against the old one
  (the Makefile tracks headers now).
- **E done** (2026-10-15, GLOS fd89eca): `make loopa-audio` passes on the Pentium II with a G200 and on the 486DX2
  with the S3 (tones at 11025, 22050 and 44100 Hz, mono and stereo, two at once and beside a session, all
  recorded; no underrun; the card the session's and back; SBCHK after GLOS).
