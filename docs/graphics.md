# GLOS graphics and window system: design

What [supervisor.md](supervisor.md) is for the supervisor, for PRD §9 and §10: the 2D library, the display, the
window manager, input, and apps' windows through the API. Each part says what is built and where. Plan and
status: [milestones-m6.md](milestones-m6.md).

## 1. The 2D library (`kernel/gfx/`, `include/glos/gfx.h`)

What the window system draws with, and what an app draws into its window with. Pure code on memory: it knows
nothing of the kernel, and `tests/host/gfx_test.c` runs it on the host. Its header is a public one, in plain C
types; `sdk/libglos` builds the same sources for the program (§5.3).

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

## 3. The window manager (`kernel/wm/`)

### 3.1 The core (`wm.c`, `kernel/include/wm.h`)

Pure code on surfaces, like the 2D library, and tested on the host the same way.

- **A window keeps its client area's pixels** in a surface of its own, in the screen's format. Whoever owns the
  window draws there, with the 2D library or straight into the memory, and says where it drew (`wm_damage`).
  Nobody is ever asked to draw again because something else moved: a window uncovered is copied from its own
  pixels, and after a session the whole desktop is.
- **The manager draws the frame**: a framed window (`WF_FRAME`) has a border, a title bar with a close box and
  its title, and a line under the bar, drawn straight onto the screen from the look (`struct wm_look`:
  widths, colours, fonts; §3.6). The frame's pixels are nobody's to keep.
- **The order** is one list from the bottom up. Topmost windows (`WF_TOPMOST`: the task bar) stay above the
  rest; `wm_raise` puts a window at the top of its kind. **The focus** is one shown window or none; its title
  bar and its task-bar button show it; `wm_next` gives the next in the order windows were made (Alt-Tab).
- **Where a point is** (`wm_hit`): the topmost shown window there, and the part: client, title bar, close
  box, the bottom right corner (12 pixels, where a window that may be resized is dragged to do it), border.
- **The outline** (`wm_ghost`): one rectangle's outline above every window, two pixels wide, black outside and
  white inside so that it shows on anything. It is where a window being moved or sized will be (§4.3).

### 3.2 The composer

`dirty` is what of the screen is out of date: a region (§1.5), added to by everything that changes what is
seen (a window shown, hidden, moved, resized, raised, its title, the focus, its owner's drawing, the pointer,
the task bar's text). `wm_compose()` brings the screen up to date:

1. From the top window down: the part of the region the window's frame covers is drawn from that window
   (frame, then its pixels copied), rectangle by rectangle with the screen's clip set to each, and the frame
   is taken out of the region.
2. What is left when the windows are done is desktop.
3. The pointer is drawn on every rectangle as it is finished, clipped to it.

Nothing is drawn and then drawn over, so the manager can draw on the screen itself without a second buffer
and without flicker. If the region runs out of room for its rectangles it can only cover too much (§1.5):
that one time everything in its bounds is drawn from the bottom up.

### 3.3 The task bar

A window of the manager's own along the bottom, topmost and never the focus: GLOS's mark and name, a button
for every framed window that is shown (in the order they were made, the focus's filled in), and at the right
a note and the clock. The note is "ssh" while someone is connected, with a sign beside it (PRD D35): the
remote-session sign is there and nowhere else, never drawn over a program's own picture. Its pictures are
one-bit bitmaps of 11 pixels: the mark (two windows), a window on each button, a line in use.

### 3.4 In the kernel (`desk.c`)

- One manager for the one screen, its state one thread's at a time (`desk_lock`).
- **The composer's thread** is of the normal class, with the system VM and the apps (not urgent, as PRD §5.4
  has the supervisor's tasks: a whole screen drawn takes a 486 tens of milliseconds, and the system VM would
  wait that long for its interrupts; the first desktop after a session did exactly that, `vif-stuck`). It
  mostly waits, so it runs at once when woken (supervisor.md §7), and the VM thread still goes first. It
  wakes when something changed (or every five seconds, for the clock, which comes from the RTC), and
  composes onto the screen when the screen is the desktop's. While a session has it nothing is drawn; when the display's mode is set again the
  display calls the desktop's painter, which says "everything" and wakes the thread.
- **Memory:** a window's pixels are whole pages of their own at `WIN_BASE` (C2000000h, 32 MB of addresses
  whose page tables are in every address space from the start), a frame each, so that an app's window can be
  mapped into the app as well (§5.2). The core says which of its memory is pixels (`wm_host`); the rest
  comes from the kernel's heap. The last 64 free frames are never given to windows.
- `glos desk` lists the windows; `glos desk demo` puts up the demonstration (`demo.c`: three windows, the
  bar, the pointer) and `glos desk clear` takes it down.

### 3.5 Tests

- `tests/host/wm_test.c` (`make host-test`). With windows as rectangles of colour, 30,000 random things done
  to up to twelve windows at each depth (made, shown, hidden, moved, resized, raised, focused, drawn in,
  removed), and after every few the screen as the composer has brought it up to date must be the picture a
  model paints from nothing: for each pixel, the topmost shown window over it, or the desktop. With frames,
  the bar and the pointer, against the composer's own picture of everything drawn afresh: nothing that
  changed may have gone undamaged. Then hit-testing, the focus's order, the bar's buttons, and the memory
  given back.
- `make loopa-desk` (`jobs.py desk`): the demonstration composed on the real screen at 640×480 in 8, 16 and
  32 bits on the S3 (both machines) and the G200, each screenshot equal to the host's composition
  (`build/host/wm-desk-N.png`); the same again after a DOS program had the screen.

### 3.6 The look and the fonts (PRD D7, D37, D42)

The owner chose both from candidates the window manager itself drew (four looks, four font sets, 640×480 at
8 bits a pixel).

- **"Slate"** (`wm_look_default`): title bars and the task bar in dark grey with white writing, on a grey
  desktop; a window that isn't the focus has a lighter bar and dark writing; borders and lines are one pixel
  of black. One colour is not a grey, an orange: the close box and the task-bar button of the window in
  front. Every colour is one of the palette's cube, so the desktop is the same picture at 8, 16 and 32 bits.
- **The fonts** are DejaVu Sans and DejaVu Sans Bold for the desktop and DejaVu Sans Mono for GLOS's own
  fixed-width text, at 11 pixels, one bit a pixel (§1.4).
- **A DOS program's text is never in those.** One shown in a window (after v1, PRD D5) is drawn in the VGA's
  own fonts at its text mode's cell size, so that it looks as it does full-screen: the card's ROM fonts,
  which the loader finds with INT 10h 1130h and `glos shot` draws text screens with already
  (supervisor.md §17.7).
- **The pointer** is an arrow of 12 by 19, black with a white inside, which shows on anything.

## 4. Input (`kernel/wm/deskin.c`)

### 4.1 Whose the keyboard and the mouse are

While the desktop has the screen they are the desktop's (`vkbc_desk()`, called by the display when its mode is
set and when a session takes the screen). The 8042's code (`kernel/vm/vkbc.c`, supervisor.md §10) reads the
chip as it always does, and hands every byte to the desktop instead of the system VM's queue: DOS and the BIOS
see no keys meanwhile. Injected input (`glos key`, `glos type`, `glos mouse`) goes the same way, so everything
here can be driven over ssh.

- The bytes go into a ring from the interrupt; the composer's thread takes them in its turn, with the manager
  locked. Only **the kill hotkey** (Ctrl-Alt-Shift-Esc) is carried out at once, from the interrupt: it ends
  the app whose window has the focus, and has to work when nothing else does.
- **The mouse:** one a DOS driver has turned on is left as the driver set it (its packets have a fourth byte
  if the driver turned a wheel on). One nobody has on is turned on for the desktop (the AUX port, IRQ 12,
  reporting) and off again when a session takes over, on a controller that has an AUX port at all. Whether
  a mouse reports is asked of the mouse itself at GLOS's start (its status, E9h): a driver that was unloaded
  again leaves the port and IRQ 12 on and the mouse quiet. A packet's first byte has bit 3 set; anything
  else is dropped until one comes, and half a packet is forgotten after a tenth of a second.
- **When they change hands** everything held is forgotten: the app that saw a key or a button go down sees
  it let go.

### 4.2 Keys

A key is its set 1 scan code, 100h added for those with an E0 prefix, and whether it went down or up. Its
character comes from the US layout (Shift, Caps Lock, Num Lock for the number pad, Ctrl for the control
characters; none with Alt). The locks start as the BIOS has them (40:17h). Other layouts are for later.

**The manager's own**, never passed on:

| Keys | |
|---|---|
| Alt-Tab | the focus to the next window, in the order they were made |
| Alt-F4 | the window is asked to close |
| Alt-F7 | the window is moved with the arrow keys |
| Alt-F8 | ... or given another size |
| Ctrl-Alt-Shift-Esc | the app with the focus is ended |

In the last two an outline follows the arrows, eight pixels a step, one with Shift; Enter makes it so, Esc
leaves the window alone (PRD D34: everything the mouse does here, the keys do).

**Everything else** goes to the app whose window has the focus (§5.4). An app gets the keys that went down
while it had the focus, and the up of each whatever happens between; when the focus leaves, the keys it still
has down are let go for it first. The up of a key it never saw go down is not passed on.

### 4.3 The mouse

- A press on a window gives it the focus (and raises it); on a task-bar button, that button's window.
- **The title bar** moves the window, **the bottom right corner** sizes it. An outline follows the pointer
  and the window is moved or sized when the button is let go: nothing is copied meanwhile, which a 486
  would be slow at. The outline stays where a window may be: a client area of at least 80 by 40, no larger
  than the screen, the title bar always in reach.
- **The close box** closes when the button is let go on it.
- **The client area** of the focus's window is the app's: movements over it, and from a button's press there
  everything until all buttons are up again, wherever the pointer goes (so a drag can leave the window). The
  press that gives a window the focus is passed on too.
- The pointer is hidden until the mouse first moves.

A window of an app is sized by the manager, at once; the app is told afterwards (§5.2). A window asked to
close is the app's to close: it gets the event, and Alt-F4 on an app that never looks does nothing (the kill
hotkey is for that).

## 5. Apps' windows (`kernel/wm/deskapp.c`, `kernel/app/api.c`, `sdk/libglos`)

### 5.1 The API

`include/glos/api.h` is the reference: functions 5 to 15 of the entry behind DPMI 0A00h "GLOS" (the event
queue; a window made, closed, shown, moved, sized, titled, given the focus; its pixels; `present`; the
screen). Structures start with their size, and the kernel reads and writes as much of one as it knows.
They run on the app's own thread, each with the manager locked for its length.

### 5.2 A window's pixels

- They are the manager's memory, **mapped into the app as well** (`win_map`, at 80000000h and above, clear
  of the blocks a program asks for). The app draws there itself, in the screen's own format, and says where
  (`present`, which is `wm_damage`): the composer copies from the same memory. There is no second copy and
  no call per drawing operation.
- **The composer copies whenever it runs**, not only at a `present`: when something else changes over the
  window too, and a moment after a `present` when it was busy. An app that draws a frame by clearing first
  can so be caught between the two (seen on the 486: a square redrawn on a cleared band, the band on the
  screen without its square now and then). Nothing is locked against it, by design: the cure is the app's,
  which draws what changes and no more, or makes the frame elsewhere and copies it in whole.
- The mapping is made and undone **on the app's thread only**, in its own address space.
- **The manager may give a window new pixels behind the app's back:** the user gave it another size, or the
  screen has another format (then every window's are new, and cleared). The app gets `GLOS_EV_RESIZE` and
  asks where its pixels are now (`GLOS_FN_PIXELS`). Until it has, it draws in the old ones, which nothing
  shows any more but which stay its own to write on: **memory an app has mapped is never given to anyone
  else** (`pins`). It lets go of them at its next window call, and at its end. So a window can be sized
  while its app is busy or stuck, and the worst an app that never asks can do is keep one window's worth of
  memory.
- An app's end takes its windows away, wherever it happens.

### 5.3 libglos

`sdk/libglos` is the C side for DJGPP: a wrapper for each function, `glos_ptr()` (a linear address as the
program's own pointer, through DJGPP's near pointers) and `glos_surface()` (a window's pixels as a surface of
the 2D library). The 2D library and the three fonts are in it: the kernel's sources, built for the program.

### 5.4 The keyboard and the mouse a DOS program reads

An app that has made a window has a keyboard and a mouse of its own (`kernel/app/dev.c`), fed with what the
desktop gives it, so code written for DOS reads the keys of its own window and nobody else's:

- **INT 16h** (00h/10h, 01h/11h, 02h/12h, 05h) from a buffer of its own, with the BIOS's codes (the function
  keys' with Shift, Ctrl and Alt; the grey keys' E0h, which the old functions give as 00h). The wait for a
  key is a sleep on the app's thread, not the BIOS's loop in the system VM.
- **Port 60h and IRQ 1** for an app with a handler: a scan code at a time, the next when the last has been
  read and its interrupt has ended. A handler that passes the interrupt on (DJGPP's own does, having looked
  for Ctrl-C) reaches the host's default, which keeps the key for INT 16h as the BIOS's would.
- **INT 33h**: there is a mouse, with two buttons; where it is in the window's client area and its buttons
  (03h), how far it has moved (0Bh). The rest is accepted and does nothing.

An app with no window reads the PC's keyboard through DOS and the BIOS as before; while the desktop has the
keyboard there are no keys for it (a terminal is M9's; DOS programs in windows come after v1, PRD D5).

### 5.5 Tests

- `make loopa-win` (`jobs.py win`; `tests/dos/wintest.c`), at 640×480 in 32 bits on the S3, 16 on the G200
  and 8 on the 486, everything driven with `glos key` and `glos mouse`:
  - two apps' windows where they asked to be, each with its own picture on the real screen;
  - keys to the one with the focus only; Alt-Tab;
  - a window moved by its title bar (the outline first), sized by its corner (the app draws again in its new
    pixels), the focus by a task-bar button, a press in the client area, the close box;
  - the same by keys: Alt-F7 and Alt-F8 with arrows and Enter, Esc, Alt-F4;
  - three apps reading the keyboard three ways (events, INT 16h, port 60h with their own IRQ 1 handler):
    each gets the keys typed while it had the focus, and none of the others';
  - INT 33h: the position in the client area, the buttons;
  - an app that draws 50 times a second, alone and beside one that draws in a busy loop: at least a third of
    its pace kept (measured: 47 against 49 on the Pentium II, 42 against 49 on the 486), `glos tick`
    answered within 0.1 s throughout, the busy one's picture moving; the kill hotkey ends the busy one;
  - a session beside a window (the window is there again after); another depth (the app is told, and draws
    again); ten apps that end with a window up, after which the kernel has the memory it had; `glos exit`
    with a window up.
- The same suite's two `winphys` cases use 86Box's own keyboard and PS/2 mouse, through the 8042: the pointer
  into a window, a press, a key, Alt-F4; once with CuteMouse loaded and once with it unloaded before GLOS
  starts, when the desktop turns the mouse on for itself.

### 5.6 Limits

- One layout (US). No key repeat of the manager's own: the keyboard's.
- Ctrl-C in a window is a key (INT 16h gives 2E03h), not DJGPP's SIGINT: its handler looks at the BIOS's
  shift flags at 40:17h, which are the system VM's.
- INT 33h has no handler calls (0Ch, 14h), no ranges and no cursor of its own.
- The pointer has one shape and no acceleration.
