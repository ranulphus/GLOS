# GLOS API 2.0: design (GLOS v1, phase A)

**Status (2026-11-06): design v0, for the owner's review with the rest of phase A.**

API 2.0 replaces API 1.x outright: the owner's choice of a clean 2.0, with no 1.x compatibility. It is designed
from phase A's plans:
- the apps (`apps/*.md`);
- the shell (`shell.md`);
- the follow-up IDE (`ide.md`).

It is frozen as GLOS's v1 API when GLOS v1 ships. After that it only grows (D14): nothing changes meaning, nothing is
taken away.

This document fixes the API's shape and every area's calls, objects and events at the level needed to approve the
design. The exact structures and numbers are written in `include/glos/api.h` in phase B, from this.

## What stays from 1.x

- **Finding GLOS:** DPMI function 0A00h with the name "GLOS". The entry comes back in ES:EDI as a far procedure.
  Plain DOS and other DPMI hosts have no such name, so a program that gets no answer runs as a DOS program.
- **Becoming an app:** an app is a DOS program that calls INIT. It then runs on a thread of its own beside the
  other apps, and has the rest of the API.
- **Errors:** the carry flag set, the error's code in EAX.
- **Text:** UTF-8 everywhere.
- **Structures:** each starts with its own size.

## What 2.0 changes in the shape

1. **One structure a call.**
   - Every call takes EAX, the function's number, and EBX, the address of its structure (or 0 for calls with
     nothing to pass). The structure holds the call's arguments and receives its results.
   - New arguments are added at a structure's end. An older program's smaller structure still works: GLOS reads
     the size and treats what is missing as zero.
   - This is the main defence against "API v37". A call grows without a new function or a new version.
2. **Numbers by area.** A function's number is its area's number times 256, plus its number in the area. Each area
   grows at its own end, and none runs into another.
3. **Handles.** Everything an app makes is a handle, a 32-bit number whose top byte says its kind:
   - windows, threads, locks, timers, jobs, streams, connections, watches, tray icons, notifications, debug
     channels, clipboard transactions.

   One call closes any handle. When an app ends, GLOS closes all its handles.
4. **Properties.** Most of what a handle has is a property, read and set with two generic calls by the property's
   number. For example, a window's title, icon, cursor, minimum size and state, or a stream's volume.

   A small addition later is a new property number, not a new function.
5. **Features, not versions.**
   - FEATURES returns, for each area, a bit set of what this GLOS has.
   - Apps test features, not version numbers. The version is 2.0 at GLOS v1, and rises only with GLOS releases that
     add features (2.1, 2.2): one step a release, never one a feature.
6. **One event queue an app.**
   - Every event (input, window, timers, jobs, the network, the shell's icons, other threads' posts) comes through
     one queue, in one structure: a common header, then a part for its kind.
   - Any thread of the app may wait on the queue or post to it.
7. **Who started whom.** GLOS remembers which app started each job and app. Some calls are allowed only on programs
   the caller started: DEBUG, CAPTURE, a job's output and input.

## The areas and their calls

Each area below lists its calls, its handles' properties, and its events. "(new)" marks what 1.x didn't have.

### 00h CORE

| Call | What it does |
|---|---|
| VERSION | The API's version, GLOS's build, and FEATURES' table (for any program, app or not) |
| INIT | Become an app. The program says the version it was built for (major 2), its name, its help page, its icon (a resource) and its Start menu category. Refused for major 1 |
| CLOSE | Close any handle |
| GET, SET | A handle's property, by number (new) |
| EVENT | Wait for the next event: a timeout, and optionally only some kinds (new: the filter) |
| POST | Put an event of the app's own on its queue, from any of its threads (new) |
| TIMER | A timer: one-shot or repeating, its events on the queue (new: today the toolkit times by EVENT's timeout) |
| TICKS, SLEEP, YIELD | As in 1.x |
| TIME | UTC and how it was set, as in 1.x; and the region's offset (new) |
| RANDOM | As in 1.x |

### 01h WIN (windows)

| Call | What it does |
|---|---|
| CREATE | A window. Its kind: normal, dialog (owned by another, optionally modal), tool window (no task bar button), popup (menus, drop-downs: as 1.x's GLOS_WIN_POPUP), the shell's desktop, full screen. Its client area's size and place (or "anywhere") |
| SHOW | Its state: hidden, normal, minimised, maximised, full screen (new: minimise, maximise, full screen) |
| PLACE | Its rectangle, for the normal state (move and resize together) |
| FOCUS | The focus, and in front. Another app's window only for the shell app and Task Manager |
| ENUM | The windows of an app, or all (for the shell and Task Manager): handles, titles, icons, states (new) |
| EMBED | A window of a program the caller started placed inside one of the caller's, as a child (new: the screensaver preview, the IDE's form preview) |

- **Properties:**
  - title; icon (a picture in the app's memory, 16x16 and 32x32);
  - cursor (one of the standard ones: arrow, busy, text, hand, crosshair, the resize arrows; or the app's own
    picture);
  - minimum and maximum client size; resizable or not;
  - owner, modal;
  - on top; task bar button (yes, no, or grouped under another);
  - the task bar button's flash (asking for attention: Chat), progress (a bar across it: copies, downloads) and
    menu entries (Play and Pause for the Audio Player, sent back as events);
  - help page;
  - state (read: normal, minimised, maximised, full screen; "not answering");
  - snapped (read).
- **Events:**
  - a key, the mouse and the wheel (INPUT's);
  - focus gained and lost;
  - resized (new pixels: DRAW);
  - state changed;
  - close asked;
  - not answering (to the shell);
  - help asked (F1);
  - the theme changed (from SETTINGS).
- **The decorations are GLOS's:**
  - the title bar with minimise, maximise/restore and close;
  - borders to resize from every edge and corner;
  - snapping;
  - double-clicking the title.

  An app with a bare window (BARE) draws its own and asks GLOS to move or resize it (a call for "the user is
  dragging my title" and "the user is dragging my edge", as Windows' hit-test does).

### 02h DRAW (pixels, the screen, the GPU)

| Call | What it does |
|---|---|
| PIXELS | Where a window's pixels are, their format and pitch, as in 1.x |
| PRESENT | The rectangles drawn, as in 1.x |
| SCREEN | The screen's size and format; the modes (for the Control Panel) |
| GPU, GPU_LOCK, GPU_UNLOCK, VRAM, WINVRAM | DOS-GL's windowed path on a Matrox card, as in 1.x, unchanged in meaning |
| SOFT | DOS-GL's software renderer's path: a window's pixels in a format DOS-GL draws into, and PRESENT (new: replaces GPU on other cards) |

- **Events:** the pixels moved (after a resize or a mode change, as 1.x's RESIZE).

### 03h INPUT

| Call | What it does |
|---|---|
| KEYS | The keyboard's state now (which keys are down, the lock keys) |
| CAPTURE_MOUSE | The mouse's events to this window while a button is down, wherever the pointer goes (new) |
| PADS | The pads and joysticks: each one's kind, buttons, axes, and its state now (new) |
| GAMEPORT | The virtual game port's sources and mapping (the gamepad plan's J6), read and set, and the axis range (new) |

- **Events:**
  - **Keys:** the set 1 code, the character, the modifiers. With a flag for raw keys: down and up with no repeat
    (new: MIDI Studio's PC piano).
  - **Text:** a character typed, apart from the key, for later input methods (new).
  - **The mouse:** moved, a button, the wheel's notches (1.5's), double-click marked by GLOS from the settings'
    time.
  - **Pads:** a pad connected or gone; a pad's state changed (new).

### 04h SHELL

| Call | What it does |
|---|---|
| TRAY | An icon in the notification area: its picture, its tooltip; changed or removed. Its events: click, double-click, the menu asked for (new) |
| NOTIFY | A notification: icon, title, text, up to two actions; its events: an action chosen, dismissed (new) |
| ENTRY | The app's Start menu entry: its name, icon, category, command line; and its jump list (recent items, tasks) (new) |
| RECENT | A document opened, for the jump list and the Start menu's search (new) |
| OPEN | Open a file or a link with its default app, or a named one ("open with"), as the shell would (new) |
| ASSOC | The kinds of file and their apps, read and set; and verbs, other apps' entries for a kind of file in Files' context menu ("Add to ZIP", "Play") (new: today `[open]` through CONFIG) |
| HELP | Open Help at a page (new) |
| POINT | Where a part of the shell is on the screen (the Start button, the task bar, the clock), for the tour (new) |
| SCHEDULE | A command run at times (daily, weekly, at GLOS's start), kept by the shell (new: Disk Tools' backups) |
| DESKTOP | The shell app's desktop window and its launcher events, as 1.x's GLOS_FN_SHELL |

- **Drawing:** the task bar, the notification area, notifications and Alt+Tab are drawn by GLOS (`kernel/wm/desk.c`),
  so they never wait on an app.
- **The Start menu:** drawn by the shell app (Files) from the entries it reads here.
- **Events:**
  - the tray icons';
  - the notifications';
  - for the shell app: launcher asked, task list asked, an entry added.

### 05h CLIP (the clipboard)

| Call | What it does |
|---|---|
| OPEN | A clipboard transaction (to read or write consistently) |
| PUT | Data in a format, replacing what was there when the first PUT of the transaction comes |
| GET | Data in a format |
| FORMATS | The formats there now |
| FORMAT | A private format's number by its name ("glos.gui.widgets", the IDE's) |

- **Standard formats:**
  - text (UTF-8);
  - an image (32-bit RGBA with its size);
  - a list of files;
  - rich text (the toolkit's styled runs, for Notepad's highlighted copies).
- **No limit but memory.** The data is held by GLOS, so it stays when the app that put it ends.
- **Events:** the clipboard changed.

### 06h DND (drag and drop)

| Call | What it does |
|---|---|
| DRAG | Start dragging: the formats offered, the data (or a promise to give it on drop), the effects allowed (copy, move, link), a picture under the pointer |
| TARGET | A window's (or a part's) formats and effects it accepts |
| ANSWER | The effect for the place the pointer is now, during a drag over the app's window |

- **Events:**
  - for the target: enter, over (place), leave, drop (with the data);
  - for the source: the effect done.
- **Who takes part:** apps, and GLOS's own desktop (files dropped on the desktop go to the shell app).

### 07h THREAD

| Call | What it does |
|---|---|
| CREATE | A thread of the app's own: an entry, an argument, a stack size (new) |
| EXIT, JOIN | End this thread; wait for another to end |
| LOCK | A lock (a mutex), and its take and give with a timeout |
| SIGNAL | An event object, set and waited for, with a timeout |
| WAIT | Wait for any of several handles (threads, signals, jobs, streams) or a timeout |

- **The C library:** DJGPP's isn't safe for threads. libglos 2.0 gives a lock around the C library's calls (malloc,
  stdio, file I/O) and documents which are safe. The toolkit is used from one thread (the window's), with work
  handed to others and results posted back as events.

### 08h JOB (running programs)

| Call | What it does |
|---|---|
| RUN | A program or a command line, with the details below |
| READ | Output from its stdout or stderr pipe, as it comes (new: streamed) |
| WRITE | Input to its stdin pipe (new) |
| SIGNAL | Ctrl+C, Ctrl+Break, or end it (new: as a call, beside the kill call) |
| STATUS | Running or ended, its exit code, its time |

- **RUN's details:**
  - its working folder and environment;
  - its stdin, stdout and stderr: pipes to the caller, files, or the console;
  - how it runs: a session, exclusive, windowed (a text window), as an app, with a profile; direct mode; or hidden;
  - long command lines: past DOS's 126 characters, passed in DJGPP's way (the `CMDLINE` variable) and as a response
    file for programs that take one (the IDE's compilers).
- **Events:** output available; ended.
- **Ownership:** a job belongs to the app that ran it. READ, WRITE and SIGNAL are for its owner. Task Manager's
  "end" uses SYS's kill, which is for any program.

### 09h FS (files and disks)

| Call | What it does |
|---|---|
| LIST | A folder's entries, many at a time (names, sizes, dates, attributes), from where the last call stopped (new: Files' folders that load as they go, without a DOS call for each file) |
| INFO | Several files' details at once (new) |
| DRIVES | The drives: kind (floppy, hard disk, CD, network, RAM), label, size and free space; a floppy changed (new) |
| WATCH | A folder or a file watched for changes, as 1.x's GLOS_FN_WATCH (new: a file, for Notepad's "changed on disk") |
| SECTORS | A disk's sectors read or written through INT 13h, the disk cache told (new: Disk Tools) |

- **Long file names:** through DOS's LFN API (INT 21h 71xxh), which GLOS passes through. GLOS's boot image loads
  FreeDOS's DOSLFN by default, so every GLOS install has it. GLOS's calls (LIST, INFO, WATCH) and DOS's own take
  long names. Where no driver is loaded (a user's own boot), names are 8.3 and FEATURES says so.
- **Ordinary file I/O:** stays the C library's, through DOS (GLOS's DOS server), as in 1.x.

### 0Ah AUDIO

| Call | What it does |
|---|---|
| OUT | A stream out: its rate, format and channels, written in blocks, its position, paused or not (1.x's GLOS_FN_AUDIO, grown) |
| IN | A stream in: the input chosen, rate, format, read in blocks as they fill (new: recording) |
| MIXER | The master volume and mute; the streams (every app's) with their volumes; the chip's channels (wave, FM, CD, line, microphone) and the recording input (new) |
| LEVELS | Each stream's and the output's level now, for meters (new) |
| MIDI_OUT | A MIDI port out: the OPL3 (GLOS's FM driver with General MIDI patches) or an MPU-401. Messages with times, played by GLOS on its tick (new) |
| MIDI_IN | A MIDI port in: an MPU-401, or a USB MIDI device. Messages with their times, as events (new) |
| DEVICES | The sound devices and ports (new) |

- **Properties:** a stream's volume, balance and mute.
- **CD audio:** MSCDEX's calls are DOS's, made through DPMI as today; the CD's line is one of MIXER's channels.
- **Screensavers never touch sound,** so music and recordings go on under them.
- **Events:**
  - a stream started or ended (for the Mixer);
  - a block wanted (out) or filled (in);
  - MIDI in;
  - a device came or went.

### 0Bh NET

| Call | What it does |
|---|---|
| CONNECT, SEND, RECEIVE | TCP, as 1.x's GLOS_FN_NET |
| LISTEN, ACCEPT | A port to accept connections on (new) |
| RESOLVE | A name's address (DNS) |
| STATE | The network: the card, the link, the address, DHCP's lease, DNS, the gateway, the counters (new: the tray's icon and the Network page) |

- **Events:**
  - a connection's state, bytes waiting, room to send (as 1.3's GLOS_EV_NET);
  - the network's state changed (new).

### 0Ch SETTINGS

| Call | What it does |
|---|---|
| GET, SET | A setting, as 1.x's GLOS_FN_CONFIG |
| SCHEMA | The schema's entries: sections, keys, kinds, ranges, labels, descriptions (new: the Control Panel reads it from GLOS rather than its own copy) |
| WATCH | Settings to be told of when they change, by section (new) |
| THEME | The theme now: colours by role, metrics, the UI font's size (new) |
| REGION | The region's formats: dates, times, numbers, the first day of the week, the code page and the keyboard layout (new) |

- **Events:** a setting changed; the theme changed.

### 0Dh IDLE

| Call | What it does |
|---|---|
| IDLE | How long since the last input (new) |
| AWAKE | Keep the screen on while a handle is open (a video, a game) (new) |
| SAVER | The screensaver: start it now, preview it in a window (EMBED), its state (new) |
| POWER | The screen's power (DPMS through VBE where the card has it) (new) |

- **Events:** the saver started or ended.

### 0Eh SYS

| Call | What it does |
|---|---|
| STAT | As 1.x's GLOS_FN_STAT, each kind a structure with its size: programs, memory, the disk cache, the GPU lock, sessions; and devices (PCI functions with keepers, USB's tree, sound, network, the game port) (new kinds) |
| KILL | End a program, a job or the session in front, as 1.x |
| LOG | The log's lines as a stream: from a point, and as they come (new) |
| PROGRAMS | The programs, apps and jobs, with who started each (new) |

- **Events:** a log line (for those following), a device came or went.

### 0Fh DEBUG

| Call | What it does |
|---|---|
| ATTACH | A debug channel to a job or app the caller started: a handle that carries gdb's remote serial protocol, packets written and read (new) |
| READ, WRITE | The channel's bytes |

- **Served by GLOS's gdb stub** (`kernel/dbg`, today on COM2 and `glos gdb`), for one program at a time: stopped
  at its start or attached while running; breakpoints, stepping, registers, memory, the stop's reason; the program's
  modules (DXE3s) and their addresses.
- **Events:** the program stopped (a breakpoint, a step, an exception).

### 10h CAPTURE

| Call | What it does |
|---|---|
| PIXELS | A window of a program the caller started: its pixels now, as raw RGBA or PNG bytes (new) |
| KEYS, MOUSE | Keys and mouse events into that program's windows, as if typed and clicked (new) |

- **Only for programs the caller started,** as DEBUG. Not a way to watch or drive any app.

### 11h SECRETS

| Call | What it does |
|---|---|
| PUT, GET, REMOVE | A secret by name, for this app (by its program's name), or shared under a name the user allowed (new) |

- **Where:** in their own file, apart from GLOS.CFG and any project.
- **By default, scrambled, and said so:** DOS has no users and no protected storage. The file is scrambled with a
  key on the same disk. That keeps secrets out of files people share and back up, but it doesn't stop someone with
  the disk. The Control Panel says so.
- **With a master password** (set in the Control Panel): secrets are encrypted with ChaCha20-Poly1305 (TinySSH's),
  under a key derived from the password. GLOS asks for it at the first GET after each start, in a dialog of the
  kernel's, so no app sees the password. Forgetting it loses the secrets, and the Control Panel says that too.

### 12h MODULES

| Call | What it does |
|---|---|
| (none in 2.0) | DXE3 modules are loaded by the program itself (DJGPP's `dlopen`): plug-ins for the IDE, codecs for the Audio Player. GLOS needs no call for that. Each app loads its own; GLOS doesn't share modules' code between apps in v1 (the owner's choice). The area's number is kept for that later |

## Events

- **The header:** size, kind, handle (the window, stream, connection and so on), time.
- **Then a part for the kind:**
  - the key's code and character;
  - the mouse's place, buttons and wheel;
  - the job's bytes waiting;
  - the tray icon's click;
  - and so on.

  Each part has its size, so later fields come at its end.
- **Kinds are numbered by area,** as functions are.

## Errors

- **The general ones as 1.x's:** no such function, not an app, a bad argument, the wrong version, refused, no such
  handle, no memory.
- **Each area's own** in its range (the network's as 1.x's NET errors).
- **A call that fails with a reason** (a setting refused by the schema) puts the reason's text in its structure.

## libglos 2.0 and the SDK

- **The C side:** libglos 2.0 gives a C function for each call, with the structures, in `include/glos/`.
- **The toolkit** (`toolkit-2.md`) builds on libglos.
- **Libraries, not calls:** what apps share as code rather than as GLOS's calls, in the SDK beside libglos:
  - TLS (BearSSL);
  - SSH and SFTP (Term's client, made a library for Disk Tools' backups);
  - archives (miniz and the readers of `files.md`);
  - pictures (stb_image), fonts (the toolkit's).
- **TLS's place:** `sdk/tls/` already (BearSSL, Fetch's and PKG's); it moves into the SDK's libraries unchanged.
- **Packages:** not an area. PKG's sources today are the owner's signed index and FreeDOS's unsigned ones (D66). A
  third, "local" (a package built on this PC, installed from a file, and said so), covers the IDE's "try it here"
  without a call.
- **The SDK on GLOS** (the IDE's, D14) packages the headers, libglos, the libraries, the toolkit and the reference.

## Moving to 2.0 (phase B)

- **GLOS's own apps:** they move with the kernel.
- **DOS-GL's windowed path:** RetroGPU's `gl/src/dgl/glos.c`; GPU, VRAM, WINVRAM, the lock and PIXELS move to DRAW. Its software
  renderer uses SOFT.
- **The SDL3 bridge** (Fifth Wheel, PrBoom-plus, Half-Life): windows, events, sound streams and the clipboard move to
  WIN, CORE's EVENT, AUDIO's OUT and CLIP.
- **The HAL's GLOS port:** RetroGPU's `hal/port/glos.c`, the GPU lock and the cache call (1.4's GLOS_FN_CACHE, kept in DRAW or
  CORE).
- **Approvals:** GLOS's side and RetroGPU's side (DOS-GL, the SDL3 bridge, the HAL's port) each merged with the
  owner's approval, after each repository's regression. Fifth Wheel and the ports rebuild against RetroGPU.

## Settled at the owner's review (2026-11-06)

1. **Long file names:** GLOS passes DOS's LFN API through, and GLOS's boot image loads FreeDOS's DOSLFN by default,
   so every GLOS install has long names. GLOS doesn't answer the LFN API itself. Without a driver, programs fall
   back to 8.3.
2. **Shared modules:** per app in v1. Each app loads its own DXE3s; the toolkit stays a static library.
3. **The secrets store:** scrambled by default, and said so, with an optional master password. With a password set,
   secrets are encrypted, and GLOS asks for the password at the first use after each start.
4. **Threads and the C library:** a lock in libglos around DJGPP's C library.
5. **Notifications, the task bar, the tray and Alt+Tab:** drawn by the kernel, as today's task bar.
