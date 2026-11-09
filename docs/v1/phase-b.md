# Phase B: API 2.0 and the window system, the step plan

**Status (2026-11-09): plan v1, approved.** The owner's answers to its four questions are written in. Their other
changes (period profiles, cable networks, the hardware ceiling) went to phase H.

Phase B of the route (`docs/v1-route.md`) builds API 2.0 as `api-2.md` designs it, in the kernel and in libglos.
It also covers:
- the window system's new behaviour;
- the IDE's needs;
- the move of RetroGPU's GLOS paths (DOS-GL, the SDL3 bridge, the HAL's port) and of GLOS's own apps to 2.0.

Phase C (toolkit 2) runs beside it from step 6.

## Before it starts

- **RetroGPU published,** and GLOS's `retrogpu` branch (the switch, checked 2026-11-09) merged into GLOS main.
  Phase B's branches start from that main.
- **Phase A's documents** are the design: `api-2.md` for every call, `toolkit-2.md` for what the toolkit asks of
  the kernel, `shell.md` for the task bar's side.

## How it is built

- **1.x stays answered until B's last step** (the owner's answer).
  - INIT tells the two apart by the major version it is given (1.x's INIT already checks it). Each app keeps its own
    major, and the dispatcher chooses its table by it.
  - VERSION answers both kinds of caller.
  - Every existing suite stays green at every step, the siblings' too.
  - Step 15 removes 1.x. GLOS v1 still ships 2.0 only, as decided.
- **One step, one branch, one merge.** Each step is developed in a worktree. Its own tests run in the change budget
  (about 10 minutes); the merge gate (about 90 minutes) runs alone on the host before it merges.
- **Tests in three tiers** (the route's):
  - **Host first:** the dispatcher, handles, properties and each area's logic built for Linux, as `make host-test`
    builds kernel code today. The host shim (`tests/host/glosshim.c`) gains each area as it is built, so the
    toolkit and the apps' scripts can use it.
  - **An API conformance suite,** `tests/api/`:
    - a DJGPP program, APITEST, on libglos 2.0, calling every built call (good arguments, bad ones, small
      structures from "older" programs, handles of the wrong kind or another app's);
    - its host build against the shim;
    - in Loop A, `make loopa-api`: one boot, bf6 with a G450, a few minutes.

    Each step adds its area's cases.
  - **The P4,** once it is set up: APITEST and the step's own checks at real speed, before merges.
- **The reference** is `include/glos/api.h`, written as each area is built. `api-2.md` is corrected wherever the
  building teaches something; each correction is listed for the owner at the step's merge.

## The steps

| # | Step | Needs |
|---|---|---|
| 1 | The frame: CORE, the dispatcher, handles, properties, events, libglos 2.0, APITEST | — |
| 2 | WIN and DRAW: window kinds and states, decorations, cursors, the task bar's list, EMBED, SOFT | 1 |
| 3 | INPUT: key state, raw keys, text, mouse capture, double clicks, pads, the game port | 1 |
| 4 | THREAD: an app's own threads, locks, signals, WAIT; the C library's lock | 1 |
| 5 | SETTINGS and IDLE: the schema, change events, themes, the region; idle, AWAKE, the saver, power | 1 |
| 6 | CLIP and DND: formats, the kernel's store, drag and drop | 1, 2 |
| 7 | FS: lists in batches, information, drives, watched files, sectors; long names through DOSLFN | 1, 4 |
| 8 | SHELL: the tray, notifications, Start entries, associations and verbs, OPEN, HELP, POINT, SCHEDULE | 2, 5 |
| 9 | JOB and SYS: streamed output, input, signals, long command lines; statistics, programs, the log | 1, 4 |
| 10 | AUDIO: capture, the mixer, levels, MIDI out (OPL3, MPU-401) and in (MPU-401) | 1, 4 |
| 11 | NET: listening sockets, the network's state | 1 |
| 12 | DEBUG and CAPTURE: a debug channel, a child's pixels and input | 2, 9 |
| 13 | SECRETS, and PKG's "local" source | 1 |
| 14 | RetroGPU's GLOS paths on 2.0: DOS-GL's windowed path, the SDL3 bridge, the HAL's port | 2, 3, 10 |
| 15 | GLOS's apps and toolkit on 2.0; 1.x removed; B's exit | all |

**Order:** 1 to 6 first, in that order. These are what toolkit 2 needs, so phase C can start once 6 is merged.
After that:
- 7 and 8, for Files and Notepad (the route's first apps);
- then the rest in the table's order;
- 14 without waiting for DOS-GL's B1, which now comes after phase B (the owner's answer, 2026-11-09). The windowed
  path moves to 2.0 here, and into B0's back end later. SOFT is tested by APITEST alone until the software
  renderer's B3 uses it.

### 1. The frame (CORE)

- **What:**
  - `include/glos/api.h` 2.0: the calling shape, every area's number range reserved, CORE in full (VERSION with
    FEATURES, INIT, CLOSE, GET, SET, EVENT with its filter, POST, TIMER, TICKS, SLEEP, YIELD, TIME, RANDOM), the
    event header, the general errors;
  - **the dispatcher** (`kernel/app/api2.c`): a table per area; each call's structure read by its size, missing
    fields zero; the transition beside 1.x's `api.c`;
  - **handles** (`kernel/app/handle.c`): a table per app, the kind in the top byte, CLOSE for any kind, all closed
    when the app ends;
  - **properties:** a registry per handle kind (get, set, which are read-only), so later steps only add entries;
  - **events:** the queue holds 2.0's events (a header and a part), with 1.x apps' events made as before; POST
    from any of the app's threads (once 4 has them); TIMER;
  - **libglos 2.0** (`sdk/libglos`): a C function per call, the structures, a version of the shim;
  - **APITEST and `make loopa-api`.**
- **Tests:**
  - host: the dispatcher's size rules, handles' reuse and kinds, the property registry;
  - Loop A: APITEST's CORE cases;
  - every existing suite unchanged (1.x untouched).
- **Exit:** CORE's cases pass on the host and in Loop A, and the full gate is green.

### 2. WIN and DRAW

- **Window kinds:** normal, dialog (owned, optionally modal), tool window, popup, the desktop, full screen.
- **States:** hidden, normal, minimised, maximised, full screen.
- **Decorations** (`kernel/wm/wm.c`):
  - the title bar's minimise, maximise/restore and close;
  - resizing from every edge and corner, with the cursor showing which;
  - double-clicking the title;
  - snapping to half the screen (a setting);
  - a bare window asking GLOS to move or resize it.
- **Properties:**
  - the title and icon;
  - cursors: the standard set, or the app's own picture;
  - the minimum and maximum size, owner and modal, on top;
  - the task bar button (yes, no, grouped), its flash, progress and menu entries;
  - the help page.
- **ENUM, FOCUS's rules, "not answering"** (an app that takes no event for 5 seconds).
- **EMBED:** a window of a program the caller started, shown inside one of the caller's.
- **The task bar's side** (`kernel/wm/desk.c`): buttons from ENUM's list, grouped, flashing, with progress.
  - The tray and notifications are step 8; Alt+Tab is phase D.
- **DRAW:**
  - PIXELS, PRESENT, SCREEN and the modes;
  - the GPU path, unchanged in meaning;
  - SOFT, the window pixels DOS-GL's software renderer draws into (its format agreed with RetroGPU's B3).
- **Tests:**
  - host: `wm_test.c` grows (states, hit-testing every edge, snapping), with goldens of the decorations at 8, 16
    and 32 bits;
  - Loop A: APITEST's WIN cases; a script that minimises, maximises, restores and resizes a test app from each edge
    with `glos mouse`; `loopa-win` and `loopa-apps` unchanged.

### 3. INPUT

- **KEYS:** the keyboard's state now.
- **Key events:** a flag for raw keys (down and up, no repeat: MIDI Studio's piano), and TEXT events apart from keys.
- **The mouse:**
  - CAPTURE_MOUSE;
  - double clicks marked by GLOS from the settings' time and distance;
  - the wheel as 1.5 has it.
- **PADS:** the USB pads' and the game port's state, connected and gone events.
- **GAMEPORT:** the virtual game port's mapping (the gamepad plan's J6) read and set.
- **Tests:**
  - host: the double-click rules and the raw and text events from `vkbc`'s stream;
  - Loop A: APITEST with `--keys` and the mouse.
  - Pads are host-tested (`hid_test`, `xpad_test`), and checked on bench-g400 by hand at the step's merge.

### 4. THREAD

- **What:**
  - an app's own threads, each a kernel thread in the app's DPMI context (its LDT, its page tables), with its own
    ring-3 stack;
  - exceptions delivered to the thread that caused them;
  - DOS calls from any thread serialised through the system VM, as an app's calls are today;
  - ending the app ends them all;
  - LOCK, SIGNAL, WAIT (several handles), JOIN, EXIT.
- **The C library's lock** (the owner's choice): libglos 2.0 wraps DJGPP's malloc family, stdio and file I/O in one
  recursive lock (link-time `--wrap`), and documents what is safe.
- **First, a design note in `docs/supervisor.md`:** threads in a context, the virtual IF and interrupts (an app's
  virtual PIC delivers to its first thread), the FPU state per thread, the locked stacks. It is reviewed with the
  owner before code, as the supervisor's other designs were.
- **Tests:**
  - host: the scheduler with several threads per context; the lock's wrapping;
  - Loop A: THRTEST (DJGPP), with threads computing, allocating and doing file I/O at once, a lock contended, the
    app killed mid-run, an exception in a second thread; `loopa-apps` and `hostile` unchanged.
- **The phase's largest risk,** so it comes early.

### 5. SETTINGS and IDLE

- **SETTINGS:**
  - GET and SET (1.x's CONFIG);
  - SCHEMA from the kernel's schema (`sdk/config/cfg.c`, shared);
  - WATCH by section;
  - THEME: colours by role and metrics, read by the window manager too;
  - REGION: the date, time and number formats, and the code page and layout.
- **IDLE:** IDLE time; AWAKE handles; SAVER (start, preview through EMBED, its events); POWER (VBE/DPMS where the
  card has it).
- **Tests:** host: themes by role and the region's formats; Loop A: APITEST, and `loopa-config` and `loopa-control`
  unchanged.

### 6. CLIP and DND

- **CLIP:**
  - the kernel's store (outside any app, kept when the app ends);
  - transactions, FORMATS, private formats by name;
  - text, an image, a list of files, rich text;
  - the change event;
  - no limit but memory.
- **DND:**
  - DRAG with formats, effects and a picture;
  - TARGET and ANSWER;
  - enter, over, leave and drop events;
  - the data passed through the kernel at the drop;
  - the desktop as a target (files dropped on it go to the shell app).
- **Tests:** host: the store and the drag's states; Loop A: two APITESTs dragging and dropping between them by
  `glos mouse`, the clipboard across an app's end.
- **Phase C starts** once this is merged.

### 7. FS

- **LIST:**
  - a folder's entries in batches, from where the last call stopped, made by the VM thread in one go;
  - FindFirst/FindNext, or 714Eh/714Fh when DOSLFN is loaded.
- **INFO:** several files at once.
- **DRIVES:** kinds (floppy, hard disk, CD through MSCDEX, network, RAM), labels, sizes, a changed floppy.
- **WATCH:** files too (`kernel/dos/watch.c`).
- **SECTORS:** INT 13h through the VM thread, with the disk cache told.
- **Long names:**
  - passed through, with FEATURES saying whether DOSLFN is there;
  - Loop A's golden images load FreeDOS's DOSLFN, a boot variant in RetroGPU's harness (with its regression
    first, D27);
  - the distribution image (phase G) loads it by default.
- **Tests:**
  - host: the batching;
  - Loop A: APITEST over a folder of 2,000 files (first batch time, all names), long names with and without
    DOSLFN, a watched file changed, sectors read on a scratch disk;
  - `loopa-console` unchanged.

### 8. SHELL

- **The tray and notifications:**
  - TRAY: icons, tooltips, click, double-click and menu events;
  - NOTIFY: drawn by the kernel (`kernel/wm/desk.c`), never over full-screen programs; actions and dismissal.
- **The Start menu's data:** ENTRY (entries and jump lists), RECENT.
- **Files:** ASSOC with verbs (`[open]` grown), OPEN (a file or a link, or "open with").
- **The rest:**
  - HELP;
  - POINT (where the task bar's parts are);
  - SCHEDULE, kept and run by the kernel, so scheduled commands run even when Files isn't running (the owner's
    answer);
  - DESKTOP (1.x's SHELL).
- **Tests:**
  - host: notifications' layout and lifetimes, ASSOC's rules;
  - Loop A: APITEST, and a scheduled command run at its time (the clock moved in 86Box).

### 9. JOB and SYS

- **RUN:**
  - the working folder and environment;
  - stdin, stdout and stderr as pipes to the caller, files or the console, through the DOS server's handles (the
    agent's output capture, grown);
  - how it runs; long command lines (`CMDLINE`, response files).
- **The job's own calls:** READ and WRITE as the data comes; SIGNAL (Ctrl+C, Ctrl+Break, end); STATUS.
- **SYS:** STAT's kinds as sized structures (devices added), PROGRAMS with who started each, LOG as a stream, KILL.
- **Tests:**
  - Loop A: a DJGPP compiler-like job streaming output and reading stdin, cancelled half way; DJGPP's own GCC
    building a small program as a job (the IDE's case);
  - `loopa-console` unchanged.

### 10. AUDIO

- **OUT** grown: position, pause, volume, balance and mute.
- **IN:** capture through the Crystal chip's and the SB16's DMA, the input chosen.
- **MIXER:** the master, every stream, and the chip's channels behind one description (the SB16's mixer, Crystal's
  codec).
- **LEVELS, DEVICES.**
- **MIDI_OUT:**
  - the OPL3 through GLOS's own FM driver, with Freedoom's GENMIDI bank as its General MIDI instruments (the
    owner's answer; Freedoom is BSD-3-Clause, and the bank's licence is checked at import);
  - an MPU-401;
  - messages with times, played on the kernel's tick.
- **MIDI_IN** from an MPU-401's UART.
  - USB MIDI is phase F's, with MIDI Studio and the owner's keyboard's descriptors (the owner's answer).
- **Tests:**
  - host: the mixer's levels, MIDI timing against the tick;
  - Loop A: recorded output with `wavcheck` (as `loopa-audio`), capture from 86Box's input where it allows, the
    OPL3's notes recorded;
  - `loopa-audio` and `loopa-sdlwin`'s sound unchanged.

### 11. NET

- **LISTEN and ACCEPT.**
- **STATE:** every interface (a network card, and phase H's serial and parallel cables), each with its link, its
  address, the DHCP lease, DNS, the gateway and its counters, and a change event.
- **Tests:** Loop A: an APITEST server reached from the host through SLiRP's forwarding; `loopa-netapp` unchanged.

### 12. DEBUG and CAPTURE

- **DEBUG:** ATTACH gives a handle carrying gdb's remote protocol to a program the caller started, served by
  `kernel/dbg`'s stub (COM2's and `glos gdb`'s today), one program at a time.
- **CAPTURE:** a child's window as RGBA or PNG; keys and the mouse into it.
- **Who may use them:** only on programs the caller started (the job's owner).
- **Tests:**
  - Loop A: a test debugger app starts a child, breaks at `main`, steps, reads registers, and resumes;
  - CAPTURE's PNG compared with `glos shot`'s;
  - a refusal for another app's program.

### 13. SECRETS, and PKG's "local" source

- **SECRETS:**
  - the store's file, scrambled;
  - the optional master password: ChaCha20-Poly1305 (TinySSH's) under a key derived from it, asked for in a
    kernel dialog at the first GET after each start;
  - the Control Panel's page is phase D's.
- **PKG:** a "local" source for packages built on this PC, installed from a file and said so (D66 grown).
- **Tests:**
  - host: the store's format, the derivation's known answers;
  - Loop A: secrets kept across a restart; the password dialog by keys; a local package installed and removed;
    `loopa-pkg` unchanged.

### 14. RetroGPU's GLOS paths on 2.0

- **DOS-GL's windowed path** (`gl/src/dgl/glos.c`): DRAW's calls, the lock, and SOFT for the software renderer
  (RetroGPU's B3).
- **The SDL3 bridge** (`gl/tools/sdl`'s patches 0008 and 0009): WIN, EVENT, AUDIO's OUT, CLIP.
- **The HAL's port** (`hal/port/glos.c`): the lock and the cache call.
- **Tests and approval:**
  - RetroGPU's regression, and GLOS's `loopa-glwin`, `loopa-sdlwin` and `loopa-games` on 2.0;
  - merged in RetroGPU with the owner's approval.

### 15. GLOS's apps on 2.0, 1.x removed, B's exit

- **GLOS's apps move to 2.0:** the toolkit (its 1.x calls; toolkit 2 itself is phase C's) and the apps' own calls.
  The goldens' pictures are unchanged.
- **1.x is removed:** `api.c`'s 1.x table goes, INIT refuses major 1, and VERSION answers 2.0 only.
- **B's exit:**
  - every suite green on 2.0: the full gate, `loopa-m8`, `loopa-m9`, `loopa-m10`, `loopa-api` with every area;
  - RetroGPU's suites on 2.0;
  - APITEST on bench-g400 and the P4.

## Risks

| Risk | What is done about it |
|---|---|
| Threads inside a DPMI context (step 4) break assumptions in the DPMI host | The design note reviewed first; step 4 early; THRTEST and the hostile suite |
| The transition dispatcher lets a 1.x bug into 2.0, or the reverse | Separate tables; APITEST covers 2.0, the existing suites 1.x; step 15 deletes 1.x whole |
| The C library's lock is missed somewhere (a stdio call without it) | Link-time wrapping, checked by a map-file test that every wrapped symbol goes through libglos |
| Phase B is long, and phase C waits on it | Steps 1-6 first; phase C starts after 6 |
| 86Box can't test capture or USB MIDI | Capture checked on bench-g400 by hand; USB MIDI in phase F with the owner's keyboard |

## The owner's answers (2026-11-09)

1. **The transition:** 1.x answered beside 2.0 until step 15, which removes it. v1 ships 2.0 only.
2. **USB MIDI input:** phase F's, with MIDI Studio.
3. **The OPL3's General MIDI bank:** Freedoom's GENMIDI, its licence checked at import.
4. **SCHEDULE:** kept and run by the kernel.
