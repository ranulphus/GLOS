# GLOS M9: native apps and the Control Panel

The task-level breakdown of PRD §19's M9, in the form of [milestones-m8.md](milestones-m8.md). PRD sections:
§6.11 (keys GLOS reserves), §10.2 (what the API covers), §10.6 (toolkit and native apps), §10.7 (Control Panel
and configuration), §14.5 (GUI tests), §18 (layout); decisions D7, D19, D33, D34, D36, D37, D40; questions Q11 and
Q18.

**Exit (PRD §19):** each app's golden images and its SSH-driven tests pass. Every setting in `GLOS.CFG` can be
changed from the Control Panel and over SSH.

## What M9 adds, in one paragraph

GLOS gets the programs a person uses on the desktop, all of them GLOS apps (DJGPP programs in ring 3, D19) built
on a toolkit of GLOS's own. The desktop gains icons and a launcher menu, drawn by the file manager, which is also a
browser of folders and the program that opens files with the right app. A terminal runs GLOS's own shell, a
modernised COMMAND.COM: DOS programs started there write into the terminal's window and read its keys, and only
those that want the screen take it, full-screen, for as long as they run. A notepad, a calculator, an image viewer
and a system monitor (which is also Ctrl-Alt-Del's task list) complete the set. Every setting GLOS has is
described once, in a schema that the loader, the kernel, `glos config` over SSH and the Control Panel all use; the
Control Panel is drawn from that schema, and a change made in either place is checked the same way, written to
`GLOS.CFG` with the file's comments kept, and applied at once where it can be.

## Decisions

### The owner's (2026-10-16)

- **Q11 answered: our own retained-mode toolkit** (`sdk/gui`, `include/glos/gui.h`; PRD D49). Widgets are objects
  that keep their state and redraw only what changed, on the 2D library, in the Slate look (D7) and DejaVu fonts
  (D37), keyboard-first (D34). Not microui or Nuklear: both are immediate-mode, repainting the window on every
  event, and their looks and input models would need bending to fit.
- **Q18 answered: GLOS's shell is DOS, modernised** (PRD D50). COMMAND.COM's command names and syntax (`DIR`,
  `COPY`, `CD`, `SET`, `%VAR%`, `>`, `>>`, `<`, `|`), with line editing, history, Tab completion, `&&` and `||`,
  and colour. Batch files are handed to `COMMAND.COM /C`, as the agent does (D40): no second batch language.
- **DOS programs run in the terminal until they want the screen** (PRD D51). A program started from the terminal
  that writes only through DOS or the BIOS's teletype runs with its output in the terminal's window and its input
  from the terminal's keys. Its first video mode set, or write to the text screen's or graphics memory, makes it a
  full-screen session for the rest of its run, as agent jobs are today; the terminal shows what it wrote before
  and after.
- **The SSH client moves to M10**, with the HTTPS client (PRD D52; §10.6 and §19 changed). M9's terminal is the
  shell only. The client will share M10's ring-3 crypto, entropy and host-key handling with TLS.

### Proposals (taken here; they go into the PRD as they are built)

- **Where things live.** The toolkit beside libglos: `sdk/gui` (the SDK's libraries stay in `sdk/`, as M6 began
  them, not the `lib/` of PRD §18, which is corrected). The apps in `apps/NAME/`, each one EXE with a short DOS
  name: `FILES.EXE` (file manager and desktop), `NOTEPAD.EXE`, `CALC.EXE`, `TERM.EXE`, `MONITOR.EXE`,
  `IMAGES.EXE` (image viewer), `CONTROL.EXE` (Control Panel). They are installed beside `GLOS.EXE` (in the image,
  `C:\GLOS\`), and GLOS finds them there.
- **The toolkit, in outline** (docs/gui.md, written with step A):
  - one `struct gui_window` per GLOS window: a tree of widgets, the focus, a damage region; drawing goes into the
    window's own pixels and is presented by the damaged rectangles; nothing is redrawn that didn't change;
  - widgets: label, button, check box, radio button, text field, text editor (a gap buffer with a line index,
    for files of a megabyte), list (columns, icons, sorting, type-to-find), tree, scroll bars, menu bar and pop-up
    menus, tabs, group box, spin box, slider, progress bar, image, canvas (the app draws), status bar; dialogs:
    message, question, input, and open/save of files;
  - layout by rows and columns that stretch, measured from the fonts, so that every dialog fits 640×480;
  - keyboard (D34): Tab and Shift-Tab through the focus order, Alt and an underlined letter, F10 to the menu bar,
    arrows in lists, menus and trees, Enter for the default button and Esc for cancel, Space to press; menus show
    their shortcuts (Ctrl-S);
  - timers through the event wait's time-out (the caret, the monitor's refresh);
  - the look: Slate's greys with one-pixel black lines, the dark title bar's grey for headers, orange only where
    D7 has it, a dotted focus line; every colour from the palette's cube, so a window is the same picture at 8, 16
    and 32 bits.
- **The kernel's part (API 1.2)**:
  - two kinds of window: `GLOS_WIN_POPUP` (bare, above every framed window, no task-bar button: menus, lists that
    drop down, tool tips; it has the keyboard while shown, and its app is told when the pointer is pressed outside
    it) and `GLOS_WIN_DESKTOP` (bare, under every other window, the screen less the task bar, no button, one at a
    time: the file manager's icons);
  - `GLOS_FN_CLIP`: the clipboard, text (UTF-8) up to 64 KB, the kernel's, shared by all apps;
  - `GLOS_FN_SHELL`: an app says it is the desktop's shell; it is sent `GLOS_EV_LAUNCH` when the task bar's mark
    is pressed or Ctrl-Esc or the Windows key typed, and `GLOS_EV_SYSKEY` for Ctrl-Alt-Del (PRD §6.11's task
    list: the shell starts the monitor at its Programs page). Without a shell, those do nothing new;
  - `GLOS_FN_RUN` and `GLOS_FN_JOB`: a program started by an app, as the agent starts jobs (the same queue and
    stub): beside it if it is a GLOS app, as a session if not; or as a *console job* (below), whose output the app
    reads and whose input it writes, with its exit code; and ended on request;
  - `GLOS_FN_STAT` and `GLOS_FN_KILL`: what `glos stat`, `glos ps`, `glos display` and `glos audio` say, in
    structures (the time each thread and app has had, memory, the network, the display, the mixer), and an app,
    job or session ended (the monitor);
  - `GLOS_FN_CONFIG`: the settings (below);
  - `GLOS_FN_WATCH`: a directory's changes as events (`GLOS_EV_FILES`): the DOS server sees every call that makes,
    removes, renames or closes a written file, from any program, the agent and SFTP included, and tells apps
    watching that directory (the file manager's windows stay right without polling).
- **Console jobs** (PRD D51, supervisor.md gains a section). While a console job runs in the system VM:
  - its console output (INT 29h, which DOS's console device calls for everything written to it, echo included;
    INT 10h 0Eh outside DOS) goes into the job's ring, not to the screen, and the screen stays the desktop's;
  - INT 10h is answered by the V86 monitor for what a line-by-line program needs: 0Eh, 03h (a cursor that follows
    the output), 0Fh and the BIOS data area's 80 columns and 25 rows, 08h, 01h; 06h, 07h and 02h only where they
    clear the screen and home the cursor (CLS). Any other INT 10h call, an access to the VGA's ports, or a touch of
    A0000h-BFFFFh (whose pages are not present meanwhile, for real-mode and protected-mode code alike) makes the
    job a full-screen session there and then: text mode, the pages mapped, the call or access done for real;
  - its keyboard is the terminal's: keys the app writes go into the BIOS's buffer, where INT 16h and DOS find
    them; Ctrl-C is 2E03h, which DOS turns into INT 23h; the terminal's Ctrl-Break kills the job (the agent's kill);
  - apps go on running beside it (it has neither the screen nor the keyboard), until it becomes a session, when
    they pause as for any session. A profile can say a program always runs full-screen (`[program X] screen =
    full`).
- **The settings' schema** (`include/glos/config.h`, PRD D53). One table: every setting's section, key, type
  (switch, number with a range, one of a list, text with a pattern, a mode, a path, a BLASTER line), default,
  one-line description, and when a change takes effect (*now*, *at GLOS's next start*, *when the program next
  starts*). Shared C, built into the loader (Open Watcom), the kernel, the Control Panel (DJGPP) and the host
  tests, with one checker (`cfg_check`). The loader reads `GLOS.CFG` through it (the keys it needs before the
  kernel) and gives the kernel the file's text; the kernel keeps it, answers `glos config` (`list`, `get`, `set`,
  `unset`, `describe`) and `GLOS_FN_CONFIG`, writes the file back through the DOS server with its comments and
  order kept (a changed line in place, a new key at its section's end, a new section at the file's end), and
  applies what can be applied at once: the desktop's mode (with the Control Panel's fifteen seconds to keep it),
  the engine on or off, the watchdog, the mixer and its rate, the host name, key repeat, the pointer's speed,
  profiles. Settings that M9 adds: `[input]` (key repeat delay and rate, pointer speed, double-click time),
  `[desktop]` (the programs started with the desktop: the shell, by default `FILES.EXE`; the icons' places), and
  in `[program X]` the title, icon, arguments, directory, `screen`, and whether it is in the launcher (`menu`) or
  on the desktop (`desktop`); `[open]` says which program opens a kind of file (`.TXT = NOTEPAD.EXE`). Nothing is
  added for features M9 doesn't build (SNTP, VNC, packages, time zones and font smoothing are M10's; they get
  their settings with their features).
- **The file manager is the desktop's shell.** The kernel keeps the task bar (it must outlive any app); the
  desktop's icons, the launcher menu and the folders' windows belong to `FILES.EXE`, started with the desktop
  (`[desktop] start`). Icons: the drives, the apps, the Control Panel, and programs whose profile says
  `desktop = 1`; arranged down the left, and kept where the user drags them. A folder opens in a window of its own
  (spatial, D33: one window per folder, reopened where it was left; places kept in `C:\GLOS\FOLDERS.CFG`), and the
  browser (a tree and a list with columns) is a window of the same program. Opening a file runs what `[open]`
  says, a program runs as itself, a batch file in a terminal. If `FILES.EXE` ends, the desktop is bare until it
  starts again (the launcher key starts it).
- **Image formats.** stb_image (public domain or MIT, PRD Appendix C's stb, ring 3 as D19 wants for parsers):
  PNG, JPEG, BMP, GIF (its first frame), TGA, PNM; and PCX, the DOS era's own, of ours. Dithered to the palette at
  8 bits.
- **How the apps are tested.** The toolkit and the apps build for the host too, against a host libglos (windows
  as surfaces in memory, events from a script). An app's golden image is its window's client area as the host
  draws it after a scripted sequence of keys and presses; in Loop A the same app on the desktop gets the same
  sequence over SSH (`glos key`, `glos type`, `glos mouse`) and its client area, cut from `glos shot` with the
  window's place from `glos desk`, must be the same bytes. What isn't a picture is checked by its effect: the
  file notepad saved (fetched by SFTP), the files the file manager copied, the terminal's output (the shell's log,
  and the screen), the settings file after the Control Panel's OK. Pictures that hold the time or live counters
  (the monitor) are compared outside those areas, which the host fills the same way.

## The steps

Each step ends with its tests passing; the regression and the gate run at the end. Development in
`~/wt-glos-m5` (branch `m9`), long runs in `~/wt-glos-test` (as the M8 rules have it).

- **A. The toolkit** (`sdk/gui`, `include/glos/gui.h`, docs/gui.md). The core (windows, widgets, focus, damage,
  events, timers, layout), the widgets and dialogs above, the host libglos (`tests/host/glosshim.c`), and
  GUITEST, a gallery of every widget in a few windows. Kernel: `GLOS_WIN_POPUP` and `GLOS_FN_CLIP`.
  T: host tests of each widget's behaviour (keys, presses, focus order, the editor's buffer, the list's
  sorting); the gallery's golden images at 8, 16 and 32 bits; `make loopa-gui`: GUITEST on the desktop (the S3 at
  8 bits, the G200 at 16, the 486 at 8), driven over SSH, its windows equal to the host's, menus dropping out of
  their windows, copy and paste between two GUITESTs.
- **B. The settings** (`include/glos/config.h`, `kernel/core/config.c`, the loader). The schema and checker; the
  loader on them; the kernel's copy of the file and its writer; `glos config`; `GLOS_FN_CONFIG`; applying at once
  what can be. T: host tests (each type's checks; a file with comments, blank lines, unknown keys and sections
  changed and written back with only the changed lines different; the loader's results the same as before for
  every `GLOS.CFG` the suites use); `make loopa-config`: each setting set over SSH, read back, in the file, and in
  effect (now, or after `glos exit` and a new start); a wrong value refused with the rule it broke.
- **C. Programs started by apps, and console jobs** (`kernel/dos/agent.c` generalised, `kernel/vm`). `GLOS_FN_RUN`,
  `GLOS_FN_JOB`, `GLOS_FN_SHELL` (with `GLOS_WIN_DESKTOP`, the launcher's keys, Ctrl-Alt-Del), `GLOS_FN_STAT`,
  `GLOS_FN_KILL`, `GLOS_FN_WATCH`. T: RUNTEST, an app that runs programs as console jobs: their output exactly
  as the agent captures it (and as they write it on plain DOS), keys typed into a program that reads them three
  ways (DOS line input, INT 16h, DOS character input), exit codes, Ctrl-C, Ctrl-Break, CLS kept in the window;
  a program that sets a mode, one that writes to B800h from real mode and one from protected mode each taken
  full-screen and back with the window's text kept; apps running beside a console job and pausing for a session;
  watchers told of changes made by an app, the agent and SFTP; the stat structures against `glos stat`.
- **D. Notepad, calculator, image viewer.** T: each app's goldens (a file opened, typed in, selected, found,
  saved; sums done by keys and presses, the memory, an error; images of each format at each depth, fitted,
  zoomed, the next in the folder) and their effects (the saved file's bytes; the clipboard; a 1 MB file).
- **E. The terminal and GLOS's shell** (`apps/term`). The terminal (a grid in DejaVu Sans Mono, a scroll-back
  of a thousand lines, selection and copy, paste as typing, sized by the user); the shell (Q18): the prompt
  (`PROMPT`, `$P$G`), editing, history, completion, `%VAR%`, `&&`, `||`, redirection and pipes, the built-in
  commands (`CD`, drive letters, `DIR`, `COPY`, `MOVE`, `REN`, `DEL`/`ERASE`, `MD`, `RD`, `TYPE`, `ECHO`, `SET`,
  `PATH`, `PROMPT`, `CLS`, `VER`, `EXIT`, `HISTORY`, `HELP`, `WHERE`, `START`), programs as console jobs, GLOS
  apps started beside, batch files and lines it can't do itself through `COMMAND.COM /C`. T: the shell's parser
  on the host (quoting, variables, `&&`, redirection); `make loopa-term`: a script of commands typed into the
  terminal, its screen against the host's golden, its output files; DJGPP's and Open Watcom's tools (`gcc
  --version`, a compile) and FreeDOS's (`MEM`, `CHKDSK`, `FIND`) in the window; `EDIT` full-screen and back;
  a batch file with `%1` and `IF ERRORLEVEL`.
- **F. The file manager and the desktop** (`apps/files`). The desktop's icons, the launcher menu, folders'
  windows, the browser, copying, moving, removing, renaming, new folders, opening by `[open]`, drag and drop
  between its windows, watching. T: goldens (the desktop at 640×480 at each depth with its icons; the launcher
  open; a folder; the browser at C:\); files copied, moved and removed as asked, checked over SFTP; a change made
  over SFTP shown without a refresh; a program started from an icon and from the launcher; Ctrl-Alt-Del's task
  list.
- **G. The system monitor and the Control Panel** (`apps/monitor`, `apps/control`). The monitor's pages
  (programs, with End; performance, with the processor's use over a minute, memory and the network; the system;
  GLOS's log). The Control Panel, drawn from the schema: a page per section, a program list and its profile form,
  Apply, OK and Cancel, when each change takes effect, the display mode tried for fifteen seconds. T: goldens of
  each page (the live areas masked); a program ended from the monitor; **every setting changed from the Control
  Panel** (the suite walks the schema: each key set to a value other than its default by keys alone, and the file
  and `glos config get` agree), and a mode change kept and one let lapse.
- **H. The exit.** `make loopa-m9` (the gui, config, console, apps, term, files and control suites), the
  regression (everything since M1, alone on the host), the gate, the docs (docs/gui.md, graphics.md,
  supervisor.md's console jobs, PRD v0.7).

## Status

- Planned 2026-10-16. The owner answered Q11, Q18, the terminal's DOS programs and the SSH client's milestone the
  same day.
- Steps A-G built 2026-10-16 (branch `m9`). What changed from the plan as it was built:
  - the apps' scripts are one suite, `native` (the terminal's and the file manager's included: `loopa-term` and a
    files suite of their own weren't needed), and the Control Panel's every-setting walk is the `control` suite;
  - goldens live in a directory per script (`build/host/gui/SCRIPT/`), so two scripts can't share a shot's name;
  - `GLOS_FN_CONFIG` gained EBX 4 (a section's lines, for `[open]`); an env value given with LFs keeps its
    semicolons (`PATH=C:\A;C:\B`);
  - console jobs: an app's calls into the system VM are made while the job waits for keys (INT 28h; INT 16h
    with the BIOS's buffer empty), or an app that types a job's keys while it writes to stdout deadlocks with it
    (supervisor.md §17.10); a console job's INT 29h output stops at the capture;
  - `[desktop] start` is empty by default, not `FILES.EXE` (PRD D53): the bootable image's GLOS.CFG will name it,
    and a file without it keeps the bare desktop every earlier suite expects;
  - a console job's unread keys are its app's again when it ends (type-ahead; `GLOS_FN_JOB` EBX 4); the BIOS's
    buffer is emptied as one starts;
  - the watches count a close only for a file written to (an app's printf told its own watch), and INT 21h 6Ch
    only when it may create or truncate (DJGPP opens every file with it: the file manager, reading the files
    of a folder it showed, told itself the folder had changed, a hundred times a script);
  - apps keep 256 events (32 lost a 486's typed keys), and `glos key` and `glos type` wait while the app with
    the keyboard is far behind (ten seconds at most, then `GLOS-WARN input-wait`);
  - the mouse's NAK to the desktop's own F5h, as a program took the screen, was put before the program, whose
    IRQ 12 was masked: nobody read it, and every key behind it waited (since M6, any program started from the
    desktop without a mouse driver of its own had a dead keyboard). It is dropped as the ACK is;
  - seen, not chased: with four machines on the host, the desktop's thread now and then left its input for
    a second or two (the guest's clock runs on while the host is busy); nothing is lost now, but it is slow;
  - `rm_call()`'s hooks run before it builds its nested frame below its stack pointer (supervisor.md invariant
    19): the watches' buffers had overwritten it, and the file manager's first Loop A run panicked the kernel;
  - the loader copies the kernel to extended memory in 32 KB pieces (it had grown past what low memory holds),
    with A20 on again after INT 15h 87h, and aligns its XMS block to a page.
