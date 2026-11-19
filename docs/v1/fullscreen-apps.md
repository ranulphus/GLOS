# Games full screen as GLOS apps

Decided by the owner on 2026-11-19, to follow the virtual Sound Blaster: our own games and ports, which already
find GLOS and run in a window, should also run **full screen as a GLOS app**, not only as a session that takes
the PC. This says what there is, what is to be built, and in what steps. The SDL half is the DOS-GL session's
(RetroGPU); the display half is GLOS's.

## What there is

- **A session** (a full-screen DOS program): the program's own DOS-GL drives the graphics card and sets the
  mode; GLOS's display driver lets go of the card and takes it back at the end. GLOS stays alive underneath
  (the network, SSH, pads, the mixer since API 2.5), but the desktop and its apps stand still, and there is no
  switching away from a program in a graphics mode.
- **A window** (an app): on a Matrox card with a 16-bit desktop a program becomes an app and DOS-GL renders
  into blocks of the card's memory that are the app's own (`GLOS_DRAW_VRAM`), with the engine under a lock GLOS
  arbitrates (`GLOS_DRAW_GPU_LOCK`). A window is told which block it shows (`GLOS_DRAW_WINVRAM`:
  `desk_win_vram`), and the compositor copies that block to the screen whenever the window is presented. SDL's
  `glos` video driver (DOS-GL's SDL patches 0008-0010) does this for Fifth Wheel, PrBoom-plus and Echoes; its
  "fullscreen" is a window of the mode's size on the desktop.
- **A full-screen window** (`GLOS_WS_FULL`: no frame, the whole screen, the task bar gone while it has the
  focus): the toolkit's apps and the savers use it. A DOS-GL saver in it is copied to the screen like any
  window.

So nothing of OpenGL has to move into GLOS. What is missing is a full-screen window that costs no more than a
session does, at the game's own resolution, with the desktop a key away.

**What a window costs today** (the DOS-GL session, bench-g400, a G400, 2026-11-19; PrBoom-plus's timedemo at
640x480, no sound, no wait for the vertical blank): 74.5 frames a second in a session, **36.0 in a window** on
an 800x600 desktop: 14 ms more a frame. Not the copy alone: the engine changes hands twice a frame. The
compositor wants it for the copy while the program draws the next frame with the lock held, and gets it at the
program's next GLOS call (the next swap); it drains the engine, copies, gives it back; the program finds it was
taken (`lost`) and writes its engine state again. F1 has none of that for a full-screen window.

## Steps

### F1. Shown as it is (GLOS): built

Built 2026-11-19 (`kernel/wm/desk.c` `scan_turn` and `desk_scan_swap`, `wm_scanout`, `mga_scanout`; API 2.7's
`shown`). On Loop A the five DOS-GL savers and the gpu suite's full-screen part run so on the G200, G400 and
G450, and DOS-GL's TEXCUBE from the DOS-GL session's branch for F2 fills the screen as an app. The bench's
frame rate is owed.

A window that is full screen, in front of everything, and showing a block of the app's video memory of the
screen's size, format and row length is shown **by pointing the display at that block** (the CRTC's start
address: the shared HAL's `crtc_set_start`), not by copying it. No API change.

- **When:** the topmost shown window that touches the screen is `WS_FULL` with such a block (`wm_scanout`),
  the pointer is the card's own (or hidden), no move outline is up, and no VNC viewer or recording wants the
  composed screen. Anything else (a notification over the game, the switcher, a menu, a viewer) and the
  compositor copies as before, with the game's frames under what is drawn over them.
- **A swap** (`WINVRAM` with the other block) of the window that is shown so takes effect in the call itself:
  the display is on the new block before the app draws into the old one. (Left to the compositor's thread, the
  app would be seen drawing.) Without waiting for the vertical retrace at first: a tear line as with vsync
  off; a wait is a later choice of the app's.
- **Out of it:** the screen's own picture is composed whole first, then the display goes back to it. At once,
  without composing, when the block is no longer the window's (the window closed, the app ended or died, its
  memory given back) or a session takes the card.
- **Screenshots** (`glos shot`) read what is shown: the app's block while it is.
- **The log:** `GLOS-DESK scanout on win=N off=...` and `... off why=...`.
- **Tests:** the host's `wm_test` for when a window qualifies; Loop A's saver cases on Matrox cards (a DOS-GL
  saver is a full-screen window with a block of its own: its picture by `glos shot` as before, the log's line,
  a notification over it and away again); a frame rate on the bench, full screen as an app against a window
  and against a session.

### F2. SDL's fullscreen is GLOS's (DOS-GL)

Agreed with the DOS-GL session (2026-11-19): a `dglGlosHints` flag and `DGL_GLOS_FULL=1` ask for a
`GLOS_WS_FULL` window at the context's size (its list of window sizes offers the screen's for that); SDL's
`glos` video driver uses the same flag when a program wants fullscreen; the ports that call DOS-GL themselves
(Quake, Quake II, Half-Life, ClassiCube) need only the setting. `DGL_FULLSCREEN=1` keeps meaning a session.

**No tearing:** a program that wants its swaps in the vertical blank waits for the blank itself, as in a
session, and then swaps. `GLOS_DRAW_WINVRAM` says when that matters (API 2.7, `shown`): 1 when the display
itself is on the window's pixels, 0 when the window is composed. GLOS's own calls stay short: no waiting in
the kernel.

### F3. A mode of its own (GLOS, an API addition)

A game that wants 640x480 on a desktop of 1024x768. Today a full-screen window's pixels are the screen's size,
whatever the program made (a smaller block of its own is refused, and the window shows its blank store: the
DOS-GL session saw a white screen, and its `DGL_GLOS_FULL` asks for full screen only at the screen's size
until this is built). The plan:

- **The window keeps its size: built (API 2.8).** A flag at its making (`GLOS_WF_OWN_MODE`) says that full
  screen does not make its client area the screen's: the frame covers the screen, black, and the client area
  keeps the size the program gave it (`kernel/wm/wm.c` `ownmode`).
- **Composed: built.** Its pixels in the middle of the black, whenever something is over it, a VNC viewer is
  there, or the card has no mode of its size.
- **Alone in front, with a block of its own: built** (`display_app_mode`, `mga_mode_begin` and `mga_mode_end`,
  `scan_turn`). GLOS sets the card to the mode of the client's size at the desktop's depth (the BIOS's mode set
  by the system VM's thread, as the desktop's own, with the card's memory kept: VBE 4F02h with bit 15), points
  the display at the block, and the row length is the mode's (the block's must be the same: it is, at the sizes
  the BIOS lists; if not, that size is not tried again). The desktop's own pixels stay as they are in the
  card's memory, unseen. Leaving: the desktop's mode again, the screen composed whole, the display back on it.
  Nothing is drawn on the screen while the card is in such a mode or changing (`display_app_busy`); the engine
  is the kernel's around the BIOS's call, and the app finds it was taken (`lost`) when it next draws.
- **The pointer and the mouse:** a game takes the mouse to itself (`GLOS_INPUT_CAPTURE`, movements); a
  pointer over such a window is the card's own cursor at the client's coordinates (`mga_cursor` from the
  window's corner).
- **Sizes:** those `GLOS_DRAW_MODES` lists at the desktop's depth; another size is composed in the middle
  (`GLOS-DISPLAY app error=no-mode`). Lower sizes through the HAL's own mode set are a later step.
- **The log and `glos display`:** `GLOS-DISPLAY app mode=N WxHxB pitch=P`, `GLOS-DISPLAY app off`, and a line
  `app mode WxH` in `glos display` while it is so.
- **Tests:** the gpu suite's mode part on Loop A (`GPUTEST full A 40 0 0 640 480` on an 800x600 desktop: the
  mode by `glos display`, the picture by `glos shot` at the game's size, the desktop's mode and picture after;
  320 by 240 composed), on the G200, G400 and G450. DOS-GL's TEXCUBE at 640x480 on an 800x600 desktop is owed
  (its `DGL_GLOS_FULL` asks for full screen only at the screen's size until it uses the flag).
- **Open:** a notification over such a window takes the card out of its mode and back (a flicker of a second
  or two): whether notifications wait while a game is in front is the owner's to say.

### F4. Other cards

Hardware OpenGL in a window is Matrox's alone today. On a VBE desktop DOS-GL's software renderer draws into
a window by `GLOS_DRAW_BLIT`; full screen it is the same, slowly. A card of another vendor gets a device on
DOS-GL's B1 interface first (the TNT2's is being written); until it can draw for a window, accelerated games
there stay sessions.

### F5. What a game needs besides pixels

Already the app's: keys and the mouse as events (the mouse held to a window: `GLOS_INPUT_CAPTURE`), pads, the
mixer. GLOS's own keys (the switcher, the volume) stay GLOS's. Ports with video, input or sound code of their
own (Half-Life's) need each routed through GLOS as SDL's are.

## Open

- What the switcher shows of a game that is behind (its last frame; a game that goes on rendering unseen).
- F3's choice between setting the game's mode and scaling its picture to the desktop's.
- A window that is not full screen: the copy made inside the program's swap call (it is at a safe point
  there), and the program giving the engine up for a moment after a swap, to take some of the 14 ms back.
- **The WARP for windows.** DOS-GL's window path sets its triangles up on the CPU, because GLOS could take the
  engine in any of the program's DOS or GLOS calls with a DMA list still to run, which then ran onto GLOS's
  target. PrBoom loses nothing by it (74.5 frames a second either way in a session); Quake would have 36 to 45%
  more with the WARP on that card. The rule DOS-GL needs: GLOS takes the engine from an app only when the
  app's primary DMA list has ended and the engine is idle. The DOS-GL session's own resync after a hand-over is
  a few dozen register writes: the 14 ms above are GLOS's.
