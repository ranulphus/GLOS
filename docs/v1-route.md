# GLOS v1: the route

**Status (2026-11-06): plan v0, for the owner's review. Nothing in it is built yet.**

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
- **Sibling projects move with the API.** DOS-GL's windowed path, the SDL3 bridge (Fifth Wheel, PrBoom-plus,
  Half-Life) and MGA-Glide's HAL port call the GLOS API. Two ways through, chosen in phase A:
  - they move to 2.0 in phase B, merged and pushed in those repositories with the owner's approval;
  - or the kernel keeps a 1.x compatibility table until they have moved.

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
- **Tests:**
  - every existing suite on 2.0;
  - an API conformance suite, which starts host-side and runs in Loop A;
  - DOS-GL, SDL3 and the HAL either moved or covered by the compatibility table.

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
- **Screensavers on DOS-GL,** after Windows 98's OpenGL ones: a starfield, pipes, a 3D maze, flying objects.
  Matrox cards draw them; a simpler software set serves the rest. An idle timeout, and a preview in the Control
  Panel.
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
  - archives opened as folders: ZIP read and written; TAR and GZ read; others (LZH, ARJ, 7z) to be settled in A.
- **Notepad,** after Notepad++:
  - tabs, sessions kept;
  - find and replace, line numbers;
  - syntax highlighting (C, assembly, batch, INI, Pascal, HTML);
  - code pages and UTF-8;
  - large files.
- **Paint (Images, grown),** after Paint.NET:
  - the viewer kept;
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
  - a MIDI keyboard as input where the hardware allows (to be settled in A).
- **PDF Viewer:** pages, zoom, search, text selection, a page cache. A Pentium at least. The library choice in A is
  the risk.
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

## Testing, throughout

- **Loop A:** each app's GUI scripts with goldens at 8, 16 and 32 bits, and its SSH-driven tests, at its CPU floor.
- **Host tests:** parsers, codecs, archive formats, the toolkit's widgets.
- **On the bench, by hand with the owner:** sound, MIDI, USB, screensavers on a real Matrox card.
- **The full regression before each merge into main,** as now; the gate before the release.

## Open questions, for phase A

1. **1.x compatibility:** do DOS-GL, the SDL3 bridge and the HAL move to 2.0 in phase B, or does the kernel keep a
   1.x table for a while?
2. **Archive formats** beyond ZIP, TAR and GZ: LZH, ARJ and 7z read? Written?
3. **The PDF library:** which permissive parser, and how fonts are drawn.
4. **MIDI input:** an MPU-401 (bench-g400's Crystal chip shows none), a USB MIDI keyboard (another USB class), or
   none in v1.
5. **Screensavers on non-Matrox cards:** software versions, or Matrox only.
6. **The UI's language:** English only in v1, with formats by region, or translations too?
7. **The IDE's needs** (above): which of them API 2.0 carries in full, and which only as a way in (shared modules
   especially).
8. **The order inside E and F:** which apps first. Proposal: Files and Notepad (they exercise the toolkit most),
   then Paint and the audio apps, then the rest.
