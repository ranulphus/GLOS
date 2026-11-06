# GLOS v1: the route

**Status (2026-11-06): plan v1, for the owner's review: the open questions answered (below). Nothing in it is
built yet.**

GLOS's milestones M0–M10 are done in Loop A, and much has been added since: the disk cache, upper memory, Crystal
audio, USB input and the virtual game port, the mouse wheel. The native side is still thin, though. The apps are
small (Notepad is 285 lines, Images 403). The window manager has a close box and a resize grip, and nothing else:
no minimize, maximize or restore, and no resizing from the borders. The task bar shows only the launcher and the
time.

The owner wants a v1 that is genuinely useful and powerful: a native side with today's standard capabilities, a
richer shell, and modern apps for old PCs. This plan is the route to that v1.

## The owner's decisions (2026-11-06)

- **A clean API 2.0, now:** the API redesigned while GLOS's own programs are almost its only users, then frozen as
  GLOS's v1 API. Not an append-only 1.6.
- **Everything in v1:** every app below ships before the release is called v1.
- **A minimum CPU for each app:**
  - the shell and the core apps stay fine on a 486;
  - a heavier app or feature states its own minimum, and degrades where it can: MIDI through the OPL3 chip on a slow
    PC, MP3 saying it needs a Pentium.
- **Extra apps:** a games library, chat and a web reader, disk and backup tools.
- **The ISA DMA limit** (docs/upper-memory.md B1) stays documented, with nothing more done about it.
- **An AI-powered IDE, later and separately:** a GLOS IDE in the spirit of Visual C++ or Delphi, with AI agents like
  Claude Code built in, for writing GLOS apps on GLOS itself. It is a follow-up project with its own repository,
  not part of this work. **API 2.0 and toolkit 2 must already cover what it needs**
  ([Designed for the IDE](#designed-for-the-ide-a-follow-up)).

**The answers to plan v0's open questions (2026-11-06):**
- **1.x compatibility: none.** DOS-GL (in MGA-Glide once merged), the SDL3 bridge and the HAL move to 2.0 in phase B
  with the kernel, each merged with the owner's approval. No compatibility table.
- **Archives:** ZIP read and written. TAR, GZ, LZH/LHA, ARJ and 7z read. (RAR is out: its decoder's licence isn't
  permissive.)
- **PDF:** GLOS's own renderer, scoped:
  - a permissive parser (PDFio, Apache-2.0, a candidate), stb_truetype for fonts, stb_image for images;
  - manuals and papers drawn properly;
  - exotic PDFs (Type 3 fonts, transparency groups, forms) best effort.
- **MIDI input:** MPU-401 ports where a card has one, and USB MIDI keyboards (a USB MIDI class driver on GLOS's
  stack).
- **Screensavers, and DOS-GL, on any card:** the owner's answer is a software renderer in DOS-GL. Every PC then
  gets the DOS-GL screensavers, and windowed DOS-GL apps run on any VBE card. It ties into the work on other
  vendors' cards (the Riva TNT2, ATI's Rage 128 and Radeon). It is built in DOS-GL's code, which is to merge into
  MGA-Glide ([The DOS-GL software renderer](#the-dos-gl-software-renderer)).
- **Language:** English in v1, with every string in resources so translations can come later without code changes.
  Regional formats for dates, times and numbers.
- **The IDE's needs: all of them in API 2.0,** built in phase B and used by GLOS's own tools: Term streams a job's
  output, and Files uses long names.
- **Order:** Files and Notepad first, as they exercise the toolkit most. Then Paint and the audio apps, then the rest.

**Phase A's review (2026-11-06):** the owner's answers, each written into its document:
- **API 2.0** (`v1/api-2.md`):
  - long file names through DOS's LFN API, with FreeDOS's DOSLFN loaded by GLOS's boot image;
  - a lock in libglos around DJGPP's C library for threads;
  - modules per app in v1;
  - notifications, the tray and Alt+Tab drawn by the kernel;
  - secrets scrambled, with an optional master password.
- **Toolkit 2** (`v1/toolkit-2.md`):
  - the UI's bitmap fonts in three sizes (stb_truetype for Paint's text and the PDF Viewer);
  - resource files in a DFM-like text;
  - Slate, dark and high-contrast themes.
- **The apps:**
  - Files' Recycle folder on, capped at 10% of a drive;
  - Disk Tools writes images to hard disks and CF cards, never to GLOS's own disk;
  - the Games Library's pictures are the user's own and screenshots (nothing fetched);
  - Notepad prints text to LPT and has no spelling checker in v1;
  - Images stays the viewer beside a new Paint;
  - the Web Reader searches DuckDuckGo Lite by default.
- **DOS-GL's B0** (DOS-GL `docs/backend-interface.md`):
  - the back-end interface in clip space (today's seam);
  - stencil in the software renderer from B2's start.

## Principles

- **The API is designed once, from the app plans.** Phase A writes every app's and the shell's plan first. API 2.0
  is designed from what they need, built, and frozen. After v1, additions are append-only (D14 again): batched into
  one version per release, behind capability bits, with structures that carry their size. A generic
  window-property call absorbs small additions (a window's icon, its minimum size, its cursor) without new
  functions.
- **Most features never touch the API.** These live in the toolkit (`sdk/gui`), which is linked into each program,
  or in the apps themselves:
  - toolbars, context menus, splitters, tooltips, tabs;
  - lists that fill as they load;
  - editor, paint and archive features, codecs.

  The kernel API covers only what needs the kernel: windows, the desktop, devices, threads.
- **Each app states its CPU floor**, in its plan and its About box. Loop A tests each app on the 486 profile or on
  bf6, as its floor says.
- **Licences:**
  - GLOS stays MIT. Reused code must be permissive (MIT, BSD, zlib, public domain, CC0; Apache-2.0 case by case),
    imported into `third_party/` with its notice and re-checked then (§17).
  - Candidates, each to be confirmed in phase A:
    - miniz (ZIP);
    - minimp3 (MP3);
    - stb_vorbis (Ogg Vorbis);
    - stb_image (JPEG, PNG and GIF decoding);
    - stb_truetype (font files);
    - TinySoundFont (a General MIDI software synth);
    - a PDF parser.
  - Copyleft code (MuPDF, libxmp, Linux's drivers) stays behaviour reference at most.
- **Sibling projects move with the API, in phase B.** DOS-GL's windowed path, the SDL3 bridge (Fifth Wheel,
  PrBoom-plus, Half-Life) and MGA-Glide's HAL port call the GLOS API, and move to 2.0 with the kernel. Each is merged
  and pushed in its own repository with the owner's approval. There is no 1.x compatibility table.

## Phases

A comes first; B and C run side by side; D builds on B and C; E and F come in waves; G is the release.

### A. Design (documents only)

- **A plan for each app:** one page each: what it does, what it needs from the toolkit and from the API, its CPU
  floor, its libraries, and how it is tested.
- **The shell's plan:**
  - the task bar, its notification area, notifications;
  - the Start menu;
  - screensavers;
  - themes and wallpapers;
  - the Control Panel's new pages.
- **The IDE's needs, as if it were one more app:** a page like the apps', from [Designed for the
  IDE](#designed-for-the-ide-a-follow-up), so that API 2.0 needs nothing new when that project starts.
- **docs/api-2.md:** API 2.0 in full, from those plans and the IDE's page. Its tables, structures, events and errors;
  how programs find it; and the decision on 1.x compatibility.
- **docs/toolkit-2.md:** the toolkit's new widgets and services, the IDE's included.
- **Exit:** the owner approves api-2.md and the plans. Nothing in B–F starts before then.

### B. API 2.0 and the window system (the kernel)

- **The new interface:** API 2.0's tables, and libglos 2.0 for programs.
- **Window states:** minimize, maximize and restore, with their title-bar buttons. Resizing from every edge and
  corner. Cursor shapes (busy, text, resize arrows, hand). A window's icon, owned and modal windows, a minimum
  size.
- **The task bar's side:** the list of windows to show buttons for.
- **The notification area:** an app puts an icon there, with a tooltip, and is told of clicks and its menu.
  Notifications that pop up and go.
- **A richer clipboard:** several formats (text, images, a list of files), well beyond 64 KB, with the owner's
  data held by the kernel.
- **Drag and drop:** files and text between windows and apps.
- **Threads for apps:** an app's own background threads, so that it can load or decode without stopping its window.
- **Sound:** recording (the Crystal chip's and the SB16's capture DMA), a stream's volume, the mixer's levels, and
  MIDI out to the OPL3 FM chip (and an MPU-401 where there is one).
- **The desktop:** idle time for screensavers, and an event when a setting changes (theme, colours, region,
  mouse).
- **Windowed and full-screen DOS-GL:** for screensavers.
- **The IDE's list, in full:**
  - builds as jobs with their output streamed and cancel;
  - a debug channel to programs the caller started (gdb's protocol);
  - window capture and input for programs the caller started;
  - long file names (LFN);
  - a secrets store;
  - local-trust package installs;
  - shared modules (DXE3), if phase A's design keeps them.
- **The siblings moved to 2.0:** DOS-GL (in MGA-Glide once merged), the SDL3 bridge, the HAL's GLOS port.
- **Tests:**
  - every existing suite on 2.0;
  - an API conformance suite, which starts host-side and runs in Loop A;
  - the siblings' own suites on 2.0.

### C. Toolkit 2 (`sdk/gui`)

- **New widgets:**
  - toolbars (icons, tooltips, overflow), context menus (right click, the Menu key), splitters, tooltips;
  - icon and detail views with sorting;
  - lists and trees that fill a part at a time as their content arrives;
  - tabs that close;
  - status bar sections;
  - an undo framework.
- **Common dialogs:**
  - open and save, with places and a preview;
  - fonts, colours, find and replace, about.
- **Themes and fonts:**
  - themes: colours and metrics, changing live;
  - DejaVu at more sizes and weights, and fonts from files (stb_truetype) where the CPU allows.
- **Helpers:** keyboard accelerators; drag-and-drop helpers; background-work helpers on B's threads.
- **Tests:** host tests, and goldens of a gallery app at 8, 16 and 32 bits.

### D. The shell and the system

- **The task bar:**
  - a button for each window;
  - the notification area:
    - volume: click for a slider, double-click for the mixer;
    - network: its state; click for the network page and monitor;
    - USB devices and pads;
  - the clock: double-click opens Date & Time.
- **The Start menu, after Windows 10 rather than 11:**
  - left: recent and pinned programs, then every app, with search as you type;
  - right: tiles to pin;
  - power and settings at the bottom.
- **Screensavers on DOS-GL,** after Windows 98's OpenGL ones: a starfield, pipes, a 3D maze, flying objects. A
  Matrox card draws them, and DOS-GL's software renderer draws them on any other card. An idle timeout, and a
  preview in the Control Panel.
- **Appearance:** wallpapers and themes, and notifications for the system's own news (a pad plugged in, the network
  up).
- **The Control Panel, grown:**
  - Appearance and themes; Screensaver; Display;
  - Sound (devices, the mixer, volumes); Network (status, configuration, monitor);
  - Date, time and region (zone, formats, network time); Mouse and keyboard (speed, wheel, repeat);
  - Game controllers (the pads, the mapping, a test and calibration: the gamepad plan's J6);
  - USB devices; startup programs and default apps.
- **The system monitor** becomes a task manager: programs, apps, sessions, memory, the network, end a task.

### E. The core apps, upgraded

- **Files (the shell):**
  - a toolbar and an address bar;
  - icon, list and detail views;
  - folders listed as they load, not all at once;
  - thumbnails, search, properties;
  - archives opened as folders: ZIP read and written; TAR, GZ, LZH/LHA, ARJ and 7z read.
- **Notepad,** after Notepad++:
  - tabs, sessions kept;
  - find and replace, line numbers;
  - syntax highlighting (C, assembly, batch, INI, Pascal, HTML);
  - code pages and UTF-8;
  - large files.
- **Images and Paint** (two programs, the owner's choice at phase A's review), Paint after Paint.NET:
  - Images stays the viewer, and opens a picture in Paint with Edit;
  - tools: brush, line, shapes, fill, text, selection, crop, resize, rotate;
  - layers, adjustments and filters, undo;
  - BMP, PCX, PNG, GIF and JPEG in (stb_image); BMP, PCX and PNG out;
  - images on the clipboard.
  - Its filters' CPU floor is set in A.
- **Term:** tabs, and saved SSH sessions (its SSH client exists, M10).
- **The Calculator:** scientific and programmer modes.

### F. The new apps

- **Audio:**
  - **Player:** WAV, MOD, S3M and XM (on a 486), MP3 and Ogg Vorbis (on a Pentium), and MIDI, through the OPL3 chip
    or a software synth on a fast Pentium. Playlists.
  - **Sound Recorder:** WAV from the Crystal chip's or the SB16's input.
  - **Mixer:** each stream's volume and the master's; also opened from the volume icon.
- **MIDI Studio** (GarageBand for MIDI files):
  - tracks and a piano roll; instruments as OPL3 patches or General MIDI (the synth);
  - .MID in and out;
  - a MIDI keyboard as input: an MPU-401 port where a card has one, and USB MIDI keyboards (the USB MIDI class on
    GLOS's stack).
- **PDF Viewer:** pages, zoom, search, text selection, a page cache. A Pentium at least. GLOS's own renderer:
  - a permissive parser;
  - stb_truetype for fonts, stb_image for images;
  - manuals and papers properly, exotic PDFs best effort.
- **Games Library:**
  - DOS games found (C:\GAMES and wherever the owner says), with box art and details;
  - each game's profile (GLOS.CFG's `[program]` sections, like Windows' PIFs) and its pad mapping;
  - launched in exclusive sessions; DOSBench runs from it.
- **Chat:** IRC over TLS (BearSSL, M10).
- **Web Reader:**
  - Gemini, and a subset of HTML and CSS with images, over HTTPS (M10); no JavaScript;
  - bookmarks, a reader view.
- **Disk Tools:** floppy images read and written (IMG/IMA), disks formatted, a disk's image to a file, backup to and
  restore from an SFTP server.
- **Help and a welcome tour:** GLOS's user guide in a viewer, and a first-start tour.

### G. The release

- **The distribution (D32):** the bootable FreeDOS + GLOS image, and the installer onto an existing DOS.
- **Bench re-verification:**
  - G400: bench-g400 again;
  - G450: bench #2, the Pentium 4, once its details are in;
  - P1: DOSBench within 3% under GLOS.
- **The open problems:**
  - shell mode on MS-DOS 7.1;
  - `glos ver`;
  - the intermittent direct-mode session failure on the 486;
  - T19 windowed on the G200;
  - the rare SSH channel message;
  - the Xbox 360 pad.
- **The docs:** the user guide (the Help app's), the PRD, release notes.
- **API 2.0 frozen as GLOS's v1 API:** v1.0.

## Designed for the IDE (a follow-up)

The IDE is built after v1, in its own repository. Most of it is an app's own business:
- **The model:** an AI model's API reached over HTTPS from the app, with GLOS's network and the BearSSL client the M10
  apps already use. No model runs on these PCs.
- **The rest:** the code editor, project files, the agents' loop.

What it needs from GLOS, though, has to be in API 2.0 and toolkit 2, or it would force the piecemeal additions this
plan is meant to avoid.

**From API 2.0:**
- **Builds with live output:** a compiler (DJGPP's, Open Watcom's) run as a job with its output streamed as it
  comes, input given, its own environment and working folder, and a way to cancel it. GLOS_FN_RUN and GLOS_FN_JOB
  are the start.
- **Debugging the app being built:** attach to an app or job the IDE started; breakpoints, stepping, registers,
  memory, the exception that stopped it. GLOS already speaks gdb's remote protocol (`kernel/dbg`), so a local
  channel to that, for programs the caller started, may be all it takes.
- **An agent that sees and drives the app under test:**
  - a window's pixels, and keys and mouse into it, as `glos shot` and `glos key` do over SSH;
  - only for programs the caller started, so it doesn't become a general way to watch or drive other apps.
- **Long file names** (the LFN follow-up, PRD §7.3): modern projects and toolchains expect them.
- **A store for secrets** (an AI service's API key), kept apart from GLOS.CFG.
- **Apps built locally, installed:** the package manager trusts only the owner's signing key today. A local trust
  level is needed, for the IDE's builds and for others' apps.
- **Background threads** (B), **file watching** (GLOS_FN_WATCH, kept), and **the clipboard with formats** (B).
- **Possibly shared modules** (DJGPP's DXE3): the IDE's plug-ins, and the toolkit shared between apps. Phase A says
  whether 2.0 has them.

**From toolkit 2:**
- **Panes:** dockable panes and splitters, with tabbed documents.
- **A code editor component:**
  - syntax highlighting and folding;
  - completion and signature pop-ups;
  - error markers from the compiler's output.
- **Other widgets:** a property grid (an inspector for the form designer), and an output pane (Term's VT as a
  widget).
- **Windows and dialogs from a file:** windows and dialogs described in a resource file and loaded at run time,
  and widgets that can be listed with their properties. This is what lets a Delphi-like form designer make an
  app's windows, and it is the hardest to retrofit.
- **The SDK on GLOS itself:** headers, libglos and the toolkit as packages (D14's SDK), so that apps can be built
  without a modern PC.

**For the IDE's own plan, not this one:**
- which models and services it uses, and whether through a relay on a modern PC;
- the agents' permissions (what they may run, write and send, and what needs the user's yes);
- its languages (C first; Pascal?);
- its repository's name.

## The DOS-GL software renderer

The owner's answer for screensavers on non-Matrox cards. It reaches further than screensavers:
- **What it gives:** a software rasteriser behind DOS-GL's existing interface, so that DOS-GL programs run on any
  VBE card: the screensavers, windowed DOS-GL apps under GLOS, and DOS-GL games full-screen, slower than on a
  Matrox card.
- **What it shapes:** a DOS-GL with more than one back end. That is the shape the other vendors' cards need: the
  Riva TNT2 next, then ATI's Rage 128 and Radeon (the owner's vendor study).
- **Where it is built:** in DOS-GL's code, which the owner has chosen to merge into MGA-Glide. So it is planned
  there, with that repository's regressions and the owner's approval for merges, and coordinated with the merge.
  GLOS's phase D waits on it for screensavers on other cards.

## Testing, throughout: three tiers

The owner's choices (2026-11-06): 86Box's speed must not make suites hours long, so tests go host first, with a
short Loop A smoke, and the Pentium 4 bench as the silicon tier.

| Tier | What runs there | When | Time |
|---|---|---|---|
| **1. The host** | The toolkit's widgets and the apps' GUI scripts on the host shim (as `gui-goldens` already runs); parsers, codecs, archive formats, the PDF renderer, the HID, MIDI and audio code; DOS-GL's GL layer and software renderer against OSMesa | On every change | Seconds to a few minutes |
| **2. 86Box (Loop A)** | A short smoke for each area: the DOS and GLOS integration (an app's window, a job, a device), the goldens compared with the host's, the kernel's suites | Unattended, on each change that touches the kernel or a device; the full matrix only before merges | About 10 minutes for a change's loop; about 90 for a merge's regression |
| **3. The Pentium 4 bench (G450)** | Real speed and real hardware: DOS-GL's full conformance (the software renderer with MMX, and the G450), the apps' suites over SSH, sound and USB by hand, the CPU floors' timings | Before merges and in batches, through the Loop B tools; power-cycled by a network-switched plug, so unattended | Minutes |

- **The budgets are tracked:** each suite's time is logged, and a suite that outgrows its budget is split or moved
  down a tier, not left to grow.
- **bench-g400** stays the G400's bench and the place for by-hand checks with the owner.
- **The gate before the release** runs every tier.

## Open questions

Plan v0's eight are answered (above). One remains, about the DOS-GL software renderer: who builds it and when,
against GLOS v1's schedule.
