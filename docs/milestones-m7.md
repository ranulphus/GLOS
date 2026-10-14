# GLOS M7: Matrox 2D

The task-level breakdown of PRD §19's M7, in the form of [milestones-m6.md](milestones-m6.md). PRD sections:
§9.1 (display drivers), §9.3 (the 2D library), §9.5 (VRAM), §9.6 (GPU ownership, for what M7 prepares),
Appendix B (the cross-repo changes), D38 (PCI Matrox cards in 86Box).

**Exit (PRD §19):** accelerated desktop frames are identical to VBE frames, on G200, G400 and G450.

## What M7 adds, in one paragraph

On a Matrox card the desktop stops being drawn by the CPU alone. The card is still put in its mode through the
VBE BIOS, as in M6, but the drawing engine then does the work it is good at: filling rectangles, copying
rectangles about the screen and from off-screen memory, writing text by expanding one-bit glyphs in colour,
and taking images from the CPU. The pointer is the card's own hardware cursor. The card's memory is shared out
by a VRAM manager (the front buffer, the cursor, windows kept off screen), which DOS-GL will ask for its buffers
in M8. The HAL (MGA-Glide's) gets the new primitives first, each with a conformance test of its own; GLOS takes
a copy of the HAL and drives it from the kernel through a port of its own. Every frame the engine draws must be
the frame the 2D library draws in software, pixel for pixel.

## Decisions taken here (proposals)

- **Mode setting stays VBE**, through the system VM, as in M6 (the HAL's VBE code, run through the kernel's
  nested real-mode calls). The HAL's mode planner chooses among the BIOS's modes. Programming the CRTC for
  modes the BIOS lacks is not M7's.
- **The HAL in GLOS is a vendored copy** (`third_party/mgahal`, `make sync-hal` and `make check-hal` as DOS-GL
  has them), taken from the pinned MGA-Glide. The kernel builds the part it needs: no FPU code (the kernel is
  built without it), so not the 3D set-up, the texture code or `engine_present`.
- **The kernel's port is MGA-Glide's** (`hal/port/glos.c`, Appendix B): port I/O, physical mappings through
  the kernel's own map, real-mode interrupts as nested calls in the system VM, a conventional-memory buffer
  from the resident stub, the kernel's clock.
- **One interface, two backends.** The 2D library keeps its software code; a surface that is the card's (the
  screen, a window kept in VRAM) carries a device whose fills, copies, glyphs and image loads the engine does.
  What the device can't do the software does, on the same pixels, after the engine has finished. The software
  path is the reference: host tests already check it against a model.
- **The hardware cursor in screenshots.** `glos shot` draws the cursor's image into the picture where the card
  shows it, so a screenshot is what is on the monitor; with that, accelerated and VBE desktops are compared as
  whole frames, pointer included.
- **PCI cards in 86Box** (D38): local patch 0116 adds PCI versions of the G200, G400 and G450, so the 486
  profiles can have a Matrox card (Loop A `--card g200pci`, `g400pci`, `g450pci`).

## The steps

Each step ends with its tests passing; the gate runs at the end.

- **A. PCI Matrox cards in 86Box** (MGA-Glide, H10). Patch 0116: the G200, G400 and G450 as PCI devices,
  besides the AGP ones (same BIOS images, PCI configuration space without the AGP capability). Loop A cards
  `g200pci`, `g400pci`, `g450pci`, allowed on the 486 profiles.
  T: MGA-Glide's HELLO and a conformance run on the 486DX2 with each PCI card; MGA-Glide's full regression.
- **B. The HAL's 2D primitives** (MGA-Glide). At 8, 16 and 32 bits a pixel, on a target with its own origin,
  pitch and clip: solid fills; screen-to-screen copies in either direction (overlapping ones too); colour
  expansion of one-bit glyph data, opaque and transparent, from the CPU and from VRAM; image loads from the CPU
  at every depth. `engine_resync()`. The display start through the CRTC. The hardware cursor (image, colours,
  position, on and off). A VRAM allocator.
  T: TWODTEST, a DOS program that draws with each primitive, draws the same in system memory with its own
  software, reads the card's memory back and compares, on the G100, G200, G400 and G450 at each depth; host
  unit tests for the allocator; MGA-Glide's full regression.
- **C. The HAL in the kernel.** `hal/port/glos.c`; the vendored copy; the card found on the PCI bus and its
  registers mapped; the engine brought up on the desktop's mode after the VBE mode set, and let go of before a
  session takes the screen and again at `glos exit`. `glos display` says whether the desktop is accelerated;
  `[display] accel = off` turns it off.
  T: the display and desk suites on the G200, G400 and G450 with the engine up and still drawing nothing.
- **D. The accelerated 2D backend.** The device hooks in the 2D library; the composer's fills, frames, copies
  of window contents and text through the engine.
  T: host tests that run the composer on a device that records the calls and replays them in software (same
  pixels); in Loop A, the same scripted desktop (windows made, moved, sized, drawn in, text, a session in
  between) with acceleration on and off gives the same screenshots, on the three cards at each depth.
- **E. The VRAM manager and windows off screen.** The front buffer, the cursor and the desktop's own windows'
  stores in VRAM while there is room (the system's memory after that), moved out before a session or another
  mode and back after; copies between them by the engine. `glos desk` and `glos display` show the use.
  T: as D, with stores in VRAM, in system memory, and some of each.
  *Changed while building it (2026-10-14):* apps' windows stay in the system's memory, and there is no glyph
  cache. A session takes the card's memory whole, so stores there must leave it first; an app's window mapped
  into the app can't be moved from under it while it may be drawing (it would draw into the session's memory),
  and the CPU reads VRAM slowly. Apps' VRAM waits for M8's GPU lock. Text is expanded from the CPU
  (`engine_mono`), which needs no cache.
- **F. The hardware cursor and the display start.** The pointer as the card's cursor; screenshots with it drawn
  in. The display start through the CRTC (the HAL's; GLOS uses it to put the desktop at the start of VRAM).
  T: the goldens with the cursor; TWODTEST's cursor and display-start cases.
- **G. The 486 with a PCI Matrox card.** The suites of steps C to F on the 486DX2 with the G200 and G450 PCI.
- **H. The exit.** Golden desktops at 8, 16 and 32 bpp, accelerated, on the G200, G400 and G450 (the Pentium II)
  and the PCI G200 and G450 (the 486), each equal to the VBE desktop; the regression; the gate.

## Status

- **A done** (2026-10-14, MGA-Glide 4d0d5de). Patch 0116; Loop A `--card g200pci|g400pci|g450pci` on any profile
  (the G200 boots Matrox's PCI BIOS, 899-33: with the AGP one it found 2 MB and no linear framebuffer);
  `loopa-selftest-ext`'s new `pci` check (the HAL's PROBE on the 486DX2 with each card). Conformance 27/27 on the
  486DX2 with the PCI G200 and G450; MGA-Glide's full regression green (conformance 108/108, 27 self-tests,
  hreplay, the six game replays).
- **B done** (2026-10-14, MGA-Glide d5a5cac; `docs/matrox-2d.md` there gives each rule's source). Copies (on the
  G200 and G400 through SRCORG, a band of rows at a time, keeping AR0 within its 18 bits), colour expansion from the
  CPU and from VRAM, image loads, 8-bpp targets, `engine_resync`, the cursor (AND/XOR masks in XGA mode) and
  `crtc_set_start`, and `vram.c`. Patch 0117 brings the model to the specifications (SRCORG, display start bit
  20, the cursor's size, colours and address). TWODTEST passes on the G100, G200, G400 and G450 and on the 486DX2
  with the PCI G200 and G450 (and fails on the model without 0117); `test_vram`; the full regression again. Both
  steps merged into MGA-Glide main with the user's approval (not pushed). Questions for real cards: §7 there.
