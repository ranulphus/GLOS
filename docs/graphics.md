# GLOS graphics and window system: design

What [supervisor.md](supervisor.md) is for the supervisor, for PRD §9: the 2D library, the display, and (as M6's
steps D to F land) the window manager, events and the app's side of them. Each part says what is built and
where. Plan and status: [milestones-m6.md](milestones-m6.md).

## 1. The 2D library (`kernel/gfx/`, `kernel/include/gfx.h`)

What the window system draws with, and what an app draws into its window with. Pure code on memory: it knows
nothing of the kernel, and `tests/host/gfx_test.c` runs it on the host.

### 1.1 Surfaces

A surface is pixels in memory: a pointer, a width and height, the bytes from one row to the next, a pixel
format and a clip rectangle. Three depths:

| Bits | A pixel is | Formats |
|---|---|---|
| 8 | an index into the GLOS palette (§1.2) | one: every 8-bit surface has the same palette |
| 16 | red, green and blue fields | 5-6-5; 5-5-5 on cards whose "16-bit" mode is that |
| 32 | 8 bits each, the fourth byte unused | as the card lays them out (blue lowest on the S3 and the Matrox) |

A colour is `00RRGGBBh` to every function. `gfx_px()` gives a format's own value for it and `gfx_rgb()` the
colour back; narrow fields are widened by repeating their top bits, so white stays white.

Everything is clipped to the surface's clip rectangle (the whole surface to begin with). Coordinates are
signed, and any of them may lie outside.

### 1.2 The palette

Fixed, and the same for every 8-bit screen and window, so that pixels copy between them unchanged and a
window's pixels mean the same whoever draws them:

| Indices | Colours |
|---|---|
| 0–15 | the sixteen colours of text mode, in their order |
| 16–231 | a cube, six levels each of red, green and blue (00h, 33h, 66h, 99h, CCh, FFh): 16 + 36r + 6g + b |
| 232–255 | 24 greys between the cube's own: 08h, 12h, … EEh |

- **Nearest colour** (`gfx_pal_nearest`): the cube's entry, or for a colour that is nearly a grey (its
  channels within 24 of each other) the nearer of the cube's and the ramp's. No channel lands further than 26
  from what was asked.
- **Pictures** (`gfx_image`) are dithered into the cube with a 4×4 ordered pattern fixed to the surface's own
  coordinates, so a picture drawn in pieces, or twice side by side, shows no seam. A colour that is one of
  the cube's stays itself everywhere.

### 1.3 Drawing

| Function | Draws |
|---|---|
| `gfx_fill` | a rectangle of one colour |
| `gfx_frame` | a rectangle's outline, one pixel wide |
| `gfx_line` | a line, both ends included (Bresenham's; each pixel tested against the clip rectangle, so which pixels a line has never depends on what is clipped away) |
| `gfx_blit` | pixels from a surface of the same format; from the same surface too, any overlap |
| `gfx_mono` | a 1-bit picture in one colour, its clear bits left alone or in a second colour (glyphs, the pointer, icons) |
| `gfx_image` | a true-colour picture, converted (dithered at 8 bits) |
| `gfx_text` | UTF-8 text in a font (§1.4) |

### 1.4 Text and fonts

- A font (`struct gfx_font`) is 1-bit glyphs for a run of code points, each with its bitmap's width, its
  left edge from the pen and the pen's advance; all glyphs are the font's height in rows. A code point the
  font hasn't got is drawn as its box. Text is UTF-8; a byte that isn't is taken for the Latin-1 character.
- **Fonts are made at build time from TrueType fonts** (PRD D7) by `tools/mkfont.py`: FreeType renders each
  code point without anti-aliasing, with the font's own hinting. It runs on the host (it needs python3-pil),
  so the generated C (`kernel/gfx/fonts/`) is kept in the repository and the kernel's build needs neither;
  `make fonts` makes them again, and each file names its source and that file's SHA-256.
- **For now:** DejaVu Sans, Sans Bold and Sans Mono at 11 pixels (14 rows), 20h to FFh. They are stand-ins:
  which fonts GLOS has is part of the look (step G, the owner's choice).

### 1.5 Regions

`struct gfx_region`: an area as up to 48 rectangles that don't overlap, with add, subtract and intersect.
The window manager's visible and damaged areas are these. A region that would need more rectangles becomes
its own bounding rectangle and says so (`rough`): it then covers too much, never too little, which is right
for "draw this again" and must not be trusted for "this is hidden".

### 1.6 Tests

- `tests/host/gfx_test.c` (`make host-test`): every drawing function at 8, 15, 16 and 32 bits against a model
  that writes one pixel at a time, 60,000 random calls each (most of them partly or wholly outside the
  surface), the two pictures compared after every call and the bytes around the library's checked; the
  palette's and the formats' conversions; UTF-8; regions against a bitmap, 20,000 random operations.
- **The test card** (`kernel/gfx/testcard.c`): one picture with every kind of drawing in it. The host test
  writes it as `build/host/gfx-card-N.png`; the kernel draws the same on the real screen (`glos display
  card`), and `make loopa-display` compares a screenshot with the host's file, pixel for pixel (§2.5).

## 2. The display (`kernel/gfx/display.c`)

### 2.1 The mode

- One screen is driven, through the video BIOS's VBE: a mode with a linear framebuffer, at the size and depth
  GLOS.CFG asks for (`[display]`, `mode = 640x480x8`; bootinfo `display_w/h/bpp`) or `glos display on
  WxHxBITS` does. With neither there is no desktop, and GLOS is as it was: text mode, headless.
- The mode is found by its description, not its number: the BIOS's own list (4F00h, whose 512 bytes are a DOS
  block for the moment), or VESA's numbers 100h–1FFh when that can't be had, each described by 4F01h.
  16 bits means 5-6-5, or 5-5-5 where the card has only that. The layout is the mode's own (VBE 3's
  linear fields when it gives them).
- The BIOS calls are nested real-mode calls on the VM thread, made when the stub asks for its next job
  (nothing else is in the BIOS then) or from a session's start and end. A request from another thread
  (`display_ask`) waits for that, and ends the stub's halt.
- The framebuffer is mapped into the kernel at `FB_BASE` (C0C00000h, 8 MB of addresses; their page tables are
  in the kernel's directory from the start, so every address space has them), cached as the machine has that
  memory.
- **At 8 bits** the DAC is loaded with the GLOS palette: 8 bits a colour if the BIOS can switch the DAC to
  that (4F08h), 6 otherwise (the S3 Trio64V2 and the G200 in 86Box: 6).

### 2.2 Sessions

A session has the screen to itself (PRD §6.9): text mode 3 is set before it starts, and when it has ended
the desktop's mode is set again and the painter draws everything (the window manager's composer, from the
windows' own pixels, once step D has one; until then whatever was shown last). A program marked as a GLOS app
(supervisor.md §11.1) starts with the desktop on. If such a program sets a video mode itself, it is a session
after all, and gets the screen from there.

### 2.3 Nothing else draws on the desktop

- **A program's video BIOS calls** (INT 10h from V86 code, reflected from protected mode, or through 0300h)
  are answered by `display_quiet()` while the desktop has the screen: the mode is "3", the cursor is at the
  top, and writing characters, scrolling, the cursor and the palette do nothing. Only the BIOS's descriptions
  of the card pass. An app's console output so reaches its agent job and nothing else. (Found on the Matrox
  BIOS, whose teletype writes into a VBE mode's framebuffer: 32 pixels of an app's first line at the top of
  the test card. The S3's draws nothing there.)
- **The VGA's memory** (A0000h–BFFFFh) is a spare frame's while the desktop has the screen (`mm_vga_hide`):
  a program that writes text straight to B8000h hits nothing. The card's own window onto its memory may be
  anywhere in the picture. It is the VGA's again for the display's own BIOS calls and for a session.

### 2.4 Screenshots

`glos shot` needs nothing new: the display's mode sets go through `rm_call()`, which tells the screenshot
code the mode, and the capture reads the linear framebuffer as for any VBE mode.

### 2.5 Tests

`make loopa-display` (`jobs.py display`), on the S3 Trio64V2 with the Pentium II and the 486DX2 and on the
G200: the test card at 640×480 in 8, 16 and 32 bits, each screenshot equal to the host's rendering (at 8 bits
through the DAC's 6 bits, as the screenshot code widens them); a DOS program gets text mode and the card is
back after it; an app starts, writes to the console and files, and ends with the card untouched; `glos
display off`; a mode no card has; GLOS leaves in text mode.
