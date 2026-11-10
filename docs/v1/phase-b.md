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

**Step 1 as built (2026-11-09, GLOS main):**
- **Files:**
  - `include/glos/api2.h`, the API's header during phase B (`glos/api.h` once step 15 removes 1.x);
  - `kernel/app/api2.c`: the dispatcher, CORE, events, timers and properties;
  - `kernel/app/handle.c`: the handle tables;
  - libglos 2.0 in `sdk/libglos2` (`glos/glos2.h`);
  - APITEST in `tests/api`, run by `make loopa-api`;
  - `tests/host/handle_test.c`.
- **Where it differs from `api-2.md`** (folded into it 2026-11-10, with steps 2 to 4's):
  1. **Function numbers carry the major version:** 20000h + the area times 256 + the function. `api-2.md` had the
     area times 256 + the function. While 1.x is answered, 2.0's calls never meet 1.x's (0-33), and every call says
     which API it is.
  2. **ECX and EDX are 0 in a call.** GLOS uses them to finish a wait that an interrupt of the app's own cut short,
     as 1.x used ECX, so that the caller's structure is never changed. (Step 4 changed how: the call returns
     `GLOS_E_AGAIN` and the caller makes it again; see "Step 4 as built".)
  3. **INIT takes the app's name, Help page and Start menu group, not its icon:** an icon is a window's (WIN) and a
     Start entry's (SHELL).
  4. **TICKS gives its milliseconds in EAX** (no structure), and VERSION its version there when EBX is 0, so that
     a program can check cheaply.
  5. **Errors:** 1.x's numbers kept where the meaning is the same. 6 is now HANDLE (1.x's WINDOW); 16-19 are new
     (KIND, PROPERTY, ROOM, REFUSED).
  6. **TIME's offset is in seconds,** the clock's own unit.
- **Tests:**
  - **Host:** `handle_test`. The dispatcher's size rules and the property table are tested by APITEST in Loop A
    rather than on the host: they need too much of the kernel around them.
  - **Loop A:** APITEST's 40 cases on bf6 (raw) and the 486 (HIMEMX), all passing.
  - **The 1.x suites the change touches** (`app_init`, `app_sleep`, `app_event`), all green: m1, apps, win, gpu,
    audio, console, config, gui, native, netapp.
- **Left for later steps:**
  - the host shim's 2.0 side, with the toolkit's move (phase C);
  - POST from an app's other threads (step 4);
  - the END event's sender (step 9's kill, step 15's shutdown).

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

**Step 2 as built (2026-11-10):**
- **Files:**
  - the window manager's core, `kernel/wm/wm.c` (states, boxes, edges, families, cursors, icons);
  - the desktop's input, `kernel/wm/deskin.c`, and apps' windows, `kernel/wm/deskapp.c`;
  - `kernel/app/api2win.c` (WIN, DRAW, the desktop's events) and `kernel/app/api2int.h`;
  - `kernel/gfx/convert.c` (BLIT's formats);
  - libglos 2.0's window calls; APITEST's window cases and `APITEST win`.
- **For every app, 1.x's too:** the states, the title bar's boxes, sizing by any edge, snapping, the pointer's
  shapes and the task bar's new buttons are the window manager's, so today's apps have them at once.
- **Where it differs from `api-2.md` and from this plan:**
  1. **The title bar's boxes are at its right** (minimise, maximise or restore, close), and the title at its left
     with the window's icon. Slate's close box was at the left. The focus's close box keeps the orange.
  2. **An edge is caught from 3 pixels outside the frame,** since Slate's border is a pixel wide.
  3. **EMBED moves to step 12,** with CAPTURE: both are for programs the caller started, which 2.0's JOB (step 9)
     says.
  4. **The display's modes (MODES) move to step 5,** with the display's settings.
  5. **F1 is a key like any other.** The kernel sends no "help asked" event: the toolkit opens Help (SHELL's HELP,
     step 8) at the window's HELP property.
  6. **A task-bar button is there or not** (TASKBAR 1 or 0). Grouping one window's button under another's isn't
     built.
  7. **Snapping is on;** its setting (`[desktop] snap`) comes with step 5's settings. The title bar's double click
     follows `[input] double_click`, which exists.
  8. **A window's icon is 16 by 16.** The 32 by 32 one comes with Alt+Tab (phase D).
  9. **"Not answering"** is told by ENUM (`GLOS_WF_HUNG`), the window's title and the pointer. The event to the shell
     comes with SHELL (step 8). It is said only of an app that reads its events at all.
  10. **SOFT is BLIT:** pixels in any format of 8, 16 or 32 bits, with a palette of the app's own for 8, into the
      window. A renderer whose format is the screen's draws straight into PIXELS instead.
  11. **FOCUS by a window's number** (another app's window) is anyone's until step 8, which keeps it to the shell
      and Task Manager.
  12. **Key, mouse and wheel events come already** (they are INPUT's): a window without them can't be tried. Step 3
      adds the rest of INPUT.
- **Tests:**
  - **Host:** `wm_test` (270,000 random operations with states and cursors against a full repaint; places, boxes,
    edges, states, families, modal, on top, every cursor's masks); `gfx_test` (every format into every other).
  - **Loop A:** `loopa-desk` (the demonstration with an icon, a flash and progress, on every card); `loopa-win`
    (1.x's WINTEST through the boxes, a double click, snapping, edges, minimise and the task bar); `loopa-api`
    (APITEST's cases, and `APITEST win` under the mouse and keys: events, states, not answering).

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

**Step 3 as built (2026-11-10):**
- **Files:** `kernel/app/api2in.c` (INPUT's calls, the pads' news); the desktop's input (`kernel/wm/deskin.c`:
  repeats told, presses counted, the capture); the game port (`kernel/vm/vjoy.c`: pads readable and told of).
- **The calls:** KEYS, CAPTURE, PADS, GAMEPORT, and MODE (new: how the app takes its keys).
- **The events:** KEY has `repeat`; MOUSE has `clicks`; TEXT and PAD are new.
- **Where it differs from `api-2.md` and from this plan:**
  1. **Raw keys are a mode of the app** (MODE's `GLOS_INPUT_RAW`), not a flag on each event: no repeats and no
     TEXT while it is on.
  2. **TEXT comes for every character typed** (20h and above) unless the app is in raw mode, after the key's KEY
     event, which still carries the character.
  3. **CAPTURE is the mouse without a button held:** a window already has the mouse from a press in it until every
     button is up. CAPTURE gives a window every movement and press wherever the pointer is, until it lets go, for
     menus and drop-down lists. A press elsewhere then changes no focus.
  4. **Presses are counted** (`clicks`: 1, 2, 3) by `[input] double_click`'s time and 4 pixels.
  5. **Pads are as the kernel has them today:** two, each with a left stick (the direction buttons folded into
     it), a right stick where it has one, and up to 16 buttons; axes -127 to 127. Triggers as axes, the direction
     buttons apart and more pads come with the drivers' work (phase H). A pad's coming and going is told to every
     app that asked; its changes to the app with the focus.
  6. **GAMEPORT only reads.** Which pad feeds which joystick is fixed (the gamepad plan's J6); `[joystick] port` and
     `range` are settings, set through SETTINGS (step 5).
  7. **The locks told are Caps and Num.**
- **For tests:** `glos joy pad=N,LX,LY,RX,RY,BUTTONS` (or `pad=N,off`) gives the kernel a pad's state as USB would,
  since Loop A's PCs have no USB pads.
- **Tests:**
  - **Loop A:** APITEST's INPUT cases (the keyboard's state, the mode, pads, the game port, capture and its
    refusals); `APITEST win` under the keys and mouse: a double click, raw keys with no text, the mouse captured
    (a press far outside is the window's and moves no focus), a pad plugged in, changed and taken out.
  - **The suites the change touches,** all green: joy, win, desk, apps, gui, native, console, vnc.
  - **By hand, at the bench:** a real pad's events, with the owner (the kernel's part is as the gamepad plan
    tested it).

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

**Step 4 as built** (2026-11-10; the design and the owner's four answers are `docs/supervisor.md` §12.7):

- **THREAD's calls** (`include/glos/api2.h`): CREATE, EXIT, JOIN, LOCK, TAKE, GIVE, SIGNAL, SET, WAIT, SELF.
  Handles `GLOS_HK_THREAD`, `GLOS_HK_LOCK`, `GLOS_HK_SIGNAL`, with read-only properties (a thread's number,
  whether it has ended, its code, its class, the time it has run; a lock's holder; whether a signal is set).
- **Different from the plan's text:**
  - **An exception in a thread the app made ends the app** with a crash report naming the thread (the owner's
    answer), where the plan said "delivered to the thread that caused them".
  - **TAKE and GIVE, and SET, are calls of their own,** not properties.
  - **CREATE takes the stack's top, not a size:** the stack is the app's own memory (libglos allocates it).
  - **CREATE takes a data selector** for the thread's DS, ES and SS. DJGPP's signals shrink the program's data
    segment for a moment, which a thread sharing the selector feels; libglos passes DJGPP's second selector.
  - **A thread's class can't be changed after CREATE** (the property is read-only).
  - **At most 16 threads beside the first,** counted until each has ended and its handle is closed.
- **A wait cut short returns to the app** (a change to step 1's calling shape, found here). When an interrupt
  of the app's own is due during a wait, the call now returns `GLOS_E_AGAIN` with ECX and EDX set, the handler
  runs on the app's own code, and libglos makes the call again for the rest. Before, the kernel re-entered the
  call straight from the handler. DJGPP's handlers shrink the data segment to raise a signal, and only a touch
  of memory in the program's own code puts it right: re-entered without one, the call could not read the
  stack to return, and repeated for ever. 1.x's calls still restart the old way (they go with 1.x in step 15).
- **A GLOS call whose return address can't be read** is a crash report (`api-return`), not a silent loop.
- **`glos ps` lists an app's threads** when it has made any: where each is in its code, or which GLOS call it
  is in and from where; which holds the DOS lock.
- **The kernel** (`kernel/app/app.c`, `api2thr.c`, `tsync.c`):
  - a record a thread in the app; each a kernel thread in the app's context with its own FPU state;
  - the DOS lock round the one call the VM thread serves an app; the API lock round calls that may wait midway;
  - handlers and another thread's DOS call kept apart; the first thread let in ahead of the next call;
  - the ends: a thread's `exit()` or crash, the first thread's end, a kill, DOS ending the program inside a
    call (the VM thread waits for the other threads to be out of the context).
  - An app that makes no thread takes none of the new paths.
- **libglos 2.0** (`sdk/libglos2/glos2thr.c`, `glos2wrap.c`, `wrap.list`):
  - `glos_thread`, `glos_thread_exit`, `glos_join`, `glos_detach`, `glos_self`, `glos_lock`, `glos_take`,
    `glos_give`, `glos_signal`, `glos_signal_set`, `glos_wait`, `glos_libc_lock`, `glos_libc_unlock`;
  - **the C library's lock:** 68 functions wrapped at link time (`@build/sdk/glos2wrap.opt`); the lock costs no
    call to GLOS unless two threads want it at once; nothing is locked until the program makes a thread;
  - a program that uses `glos_thread` without the linker's options fails to link;
  - **a threaded program's exit ends through GLOS:** DJGPP's last step of exit frees the selector the other
    threads run on, so libglos takes that step over (`__exit`, wrapped too) and ends the program by DOS's
    function at once; GLOS stops the threads and drops the context;
  - what stays unsafe is listed in `include/glos/glos2.h` (the `getc` and `putc` macros, functions with
    memory of their own such as `strtok` and `localtime`, `errno`, the program's own use of `__tb`).
- **Tests:**
  - host: `tsync_test` (locks and signals); `tools/wrapcheck.py` on THRTEST's link map (every wrapped function
    reached only through the lock, from the C library's own files too);
  - Loop A: `make loopa-thr` (THRTEST through the agent, on the bf6 and the 486).

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

**Step 5 as built** (2026-11-10):

- **SETTINGS' calls** (`kernel/app/api2set.c`): GET, SET, SECTIONS, KEYS, SCHEMA, WATCH, THEME, REGION. Events:
  `GLOS_EV_SETTING` (a watched section's setting changed, whoever changed it) and `GLOS_EV_THEME` (to every app).
- **IDLE's calls:** TIME (the idle time), AWAKE (a handle), SAVER (its state, start, end), POWER. Event:
  `GLOS_EV_SAVER`, to every app.
- **DRAW's MODES:** the card's modes with a linear framebuffer at 8, 16 or 32 bits, listed once by the VM thread.
- **New settings** (`sdk/config/cfg.c`; the 1.x Control Panel shows them, since it is built from the schema):
  - `[desktop] snap` (on) and `theme` (slate, dark, contrast);
  - `[region]`: `date`, `clock`, `decimal`, `thousands`, `first_day`, `codepage`, `layout`, `language`;
  - `[saver]`: `wait`, `saver`, `power`.
- **Themes** (`kernel/wm/theme.c`): Slate, dark and high contrast, 32 colours by role each, all in the 8-bit
  palette. The window manager's frames, task bar and desktop follow the theme at once. The sizes are the same
  in all three for now.
- **The region's formats** (`sdk/config/region.c`, shared with the kernel): the task bar's clock follows
  `[region] clock`. Dates and numbers are for toolkit 2 to use.
- **Where it differs from the plan and from `api-2.md`:**
  1. **The saver is GLOS's own black screen for now,** drawn in the desktop's surface in place of the windows:
     no window, focus or app is touched. The DOS-GL savers and how they are run are phase D's (the plan said
     "settled in B": what B settles is that the built-in one is drawn by the desktop itself).
  2. **The saver is off unless `[saver] wait` is set.** A default wait would use up the first key of every
     test that waits, and GLOS has no saver worth looking at until phase D. The default is for phase D.
  3. **The saver's preview comes with EMBED** (step 12).
  4. **The key or movement that ends the saver is used up:** the app with the focus never sees it, nor a
     button's release that follows.
  5. **What keeps the screen on:** an AWAKE handle of any app's, a full-screen window with the focus, and a
     session having the screen. An app holding the GPU does not, by itself: DOS-GL takes an AWAKE handle when
     it moves to 2.0 (step 14).
  6. **POWER is VBE/PM** (INT 10h 4F10h), asked of the card the first time. A card without it answers with no
     states, and a set is refused.
  7. **The code page setting is `dos` by default** (what DOS says). A named page reaches GLOS's programs as
     GLOSCP in their environment.
  8. **One keyboard layout (`us`) and one language (`en`)** are in the schema. More layouts need a keymap each
     (`kernel/include/keymap.h`); they come with the Control Panel's keyboard page (phase E).
  9. **The region's defaults** are ISO dates (2026-11-10), a 24-hour clock, 1,234.5 and Monday first: for the
     owner to change if GLOS's defaults should be another region's.
  10. **Texts are passed by address** (a section, a key, a value), not in fixed fields.
  11. **A theme's sizes can't differ yet:** a theme with a wider border needs every window laid out again,
      which comes with toolkit 2.
- **For tests:** `glos idle` (the idle time, the saver, the screen), `glos idle +SECONDS` (as if nobody had
  touched anything for so long more), `glos idle saver` and `glos idle wake`.
- **Tests:**
  - **Host:** `region_test` (every date pattern, both clocks, the marks, room), `theme_test` (every colour in
    the palette; text readable on its ground: 3 to 1 at least, 7 to 1 for high contrast), `cfg_test`.
  - **Loop A:** APITEST's settings, modes and idle cases; `APITEST set` under the agent (the app's own sets;
    a watched change told and an unwatched one not; the theme to dark and back; the region; the saver after
    its wait, ended by a key nobody sees, held off by AWAKE, by hand; the screen's power); `loopa-config` and
    `loopa-control` with the new settings.

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

**Step 6 as built** (2026-11-10):

- **CLIP's calls** (`kernel/app/api2clip.c`, over `clip.c` and `clipstore.c`): OPEN, PUT, GET, FORMATS, FORMAT.
  A transaction is a handle (`GLOS_HK_CLIP`); CLOSE of a writing one makes what was put the clipboard, all at
  once. Event: `GLOS_EV_CLIP`, to every app.
- **DND's calls:** DRAG, TARGET, ANSWER. Events: `GLOS_EV_DND_ENTER`, `OVER`, `LEAVE` and `DROP` for the target,
  `DONE` for the source.
- **The store:** a content is data in up to 8 formats, counted by who holds it. The clipboard, every reading
  transaction and a drag each hold one, so a reader sees the clipboard whole as it was at its OPEN. Small data
  is the kernel heap's, data over a page the windows' region's.
- **1.x and the agent** (`GLOS_FN_CLIP`, `glos clip`) read and write the same clipboard's text.
- **Where it differs from the plan and from `api-2.md`:**
  1. **PUT replaces at CLOSE,** not at the transaction's first PUT: the clipboard is never seen half written.
     A transaction with no PUT, or with its COMMIT property set to 0, changes nothing.
  2. **A drag's data is a writing transaction** given to DRAG, which takes it: no second way to say what is
     offered. Its data is given when the drag starts. A promise to give it at the drop isn't built.
  3. **GET reads from an offset,** so large data can be read a piece at a time.
  4. **One format's data is 16 MB at most,** and all of it together what the windows' region has (32 MB of
     addresses, shared with the windows' pixels). "No limit but memory" otherwise.
  5. **A target is a whole window,** not a part of one: the app answers by where the pointer is (ANSWER).
  6. **The picture is opaque,** on a plain ground with a line round it, beside the pointer rather than under it,
     and 64 by 64 at most. The pointer itself says what a drop would do (copy, move, link, or nothing).
  7. **The source gets no mouse events during its drag,** nor the release that ends it: DONE says it is over.
  8. **The bare desktop's drops** go to whichever app asked for them (TARGET with window 0). Until SHELL (step
     8) keeps that to the shell, any app may ask.
  9. **Esc gives a drag up;** so does a session taking the keyboard.
  10. **1.x apps take no part in drags.**
- **Tests:**
  - **Host:** `clip_test` (contents, readers, formats, memory given back), `dnd_test` (every state: targets
    entered and left, answers, drops taken and refused, give-ups, apps and windows going).
  - **Loop A:** APITEST's clipboard cases and DND's refusals; `APITEST dnd` twice under the agent
    (`api_dnd_case`): a drag from one to the other dropped as a move, one dropped on the bare desktop, one given
    up with Esc, the clipboard across an app's end, and the agent's `glos clip` on the same clipboard.
- **Phase C may start** once this is merged.

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

**Step 7 as built** (2026-11-10):

- **FS's calls** (`kernel/app/api2fs.c`): LIST, INFO, DRIVES, WATCH, SECTORS. Handles: a listing
  (`GLOS_HK_LIST`, new) and a watch (`GLOS_HK_WATCH`). Event: `GLOS_EV_FS`.
- **A batch** (`app_batch`, `kernel/app/app.c`): the VM thread makes all of a call's DOS and BIOS calls in one
  turn, in the app's own DOS context. A batch borrows 1 KB of DOS's memory for as long.
- **LIST** gives entries one after another in the app's buffer (a record each: attributes, size, date and time,
  the 8.3 name, the name), 512 a batch at most. A listing is gone at the folder's last batch.
- **Names are UTF-8** through the region's code page (`sdk/config/cp.c`, now the kernel's too).
- **Long names** where INT 21h 71A0h answers for the drive: LIST and INFO use DOS's long-name finds and give
  both names. GLOS answers none of those calls itself.
- **WATCH** takes a file too; changes made through DOS's long-name calls are seen (they mark every watch on
  the drive: the path may name the folder another way than the watcher did).
- **SECTORS** goes through INT 13h on the VM thread, where the disk cache sees every call. One sector a call
  to the BIOS, 32 a call to GLOS.
- **Where it differs from the plan and from `api-2.md`:**
  1. **DOSLFN is loaded by the test itself** (`C:\TEST\DOSLFN.COM` before GLOS, as a user's own boot would
     have it), from FreeDOS 1.3's package, pinned in `tools/setup/versions.mk`. No boot variant in RetroGPU's
     harness was needed, so nothing changes there.
  2. **FEATURES doesn't say whether long names are there:** a drive does (`GLOS_DRIVE_LONG`), since a driver
     answers a drive at a time, and LIST says of each listing.
  3. **A missing folder** is `GLOS_E_ARGUMENT`; an empty one is no entries.
  4. **Dates are DOS's own** (a packed date and time, local), not seconds.
  5. **DRIVES asks a floppy or a CD for nothing** unless that drive is asked for by its letter: listing the
     drives never spins one. A changed floppy is told by its serial number, which the app compares.
  6. **A RAM disk is known by its label** (MS-RAMDRIVE, RAMDISK, RDV, XMSDSK, VDISK, SRDISK): DOS has no
     better way to say.
  7. **Writes to GLOS's own disk are refused:** the first hard disk, and the floppy GLOS was started from.
     Which BIOS disk a DOS drive is on is not worked out more finely than that.
  8. **INFO takes 64 paths at most.**
- **Tests:**
  - **Host:** `fslist_test` (both of DOS's find records, the packing into batches, names through the code
    pages).
  - **Loop A:** APITEST's FS cases (a folder of 2,000 files made and listed in small and in large batches;
    a listing closed early; several files' details; the drives; a folder's and a file's watch; sectors read
    from the first disk, refused for writing there, written and read back on a scratch disk); `APITEST lfn`
    with DOSLFN loaded (`api_lfn_case`).

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

**Step 8, first part, as built** (2026-11-10; ENTRY, RECENT and SCHEDULE are the second part):

- **SHELL's calls** (`kernel/app/api2shell.c`): DESKTOP, TRAY, NOTIFY, POINT, HELP, OPEN, ASSOC. Handles: a tray
  icon (`GLOS_HK_TRAY`) and a notification (`GLOS_HK_NOTIFY`). Events: `GLOS_EV_TRAY`, `GLOS_EV_NOTIFY`,
  `GLOS_EV_SHELL`. A new error, `GLOS_E_NOT_FOUND` (21).
- **GLOS's own programs:** a program whose file is in the folder GLOS.EXE is in (`app_own`, found at INIT from
  the program's path). Only such a program may:
  - be the shell (DESKTOP; one at a time; it gets `GLOS_EV_SHELL`: the launcher, the task list, another app's
    window not answering and answering again);
  - give the keyboard to another app's window (WIN's FOCUS by a window's number).

  Only the shell may take the bare desktop's drops (DND's TARGET with window 0). This default is to be put to
  the owner.
- **The tray** (`kernel/wm/wm.c`, `kernel/wm/deskshell.c`): up to 12 icons of 16 by 16 pixels left of the clock,
  drawn by the desktop. A press of the left button is CLICK, a second within the double-click time
  (`[input] double_click`) DOUBLE, the right button MENU; each event has the icon's place on the screen. The
  tooltip is a small window of the desktop's own above the icon, shown once the pointer has rested there 0.6 s.
- **Notifications** (`kernel/wm/notes.c`, drawn by `deskshell.c` from the theme): one shown at a time above the
  task bar's right end, up to 8 shown and waiting. A title, up to three lines of text, an icon, up to two
  actions. Shown 6 s unless it says (2 to 60); it stays while the pointer is on it; it is hidden, its time
  stopped, while a full-screen program has the screen. Its app is told ACTION1, ACTION2, CLOSED or TIMEOUT.
  CLOSE takes it away, shown or waiting.
- **POINT:** the Start button, the task bar, the tray, the clock, the work area.
- **ASSOC:** GET and SET are `[open]`; verbs are kept in each program's profile (`[program NAME] verbs`, a new
  setting: `.KIND:Label=ARGUMENTS;...`). VERBS lists every program's verbs for a kind; VERB sets or takes away
  one of the calling program's own. The rules are `sdk/config/assoc.c`'s.
- **OPEN:** a program is run itself; a file by its kind's program, by a program named ("open with"), or by a
  verb of a program's; a link (`http://`, `https://`) is the kind `.URL`. A file must be there (one batch asks
  DOS). The program is started as a job of nobody's: it runs on when the app that opened it ends. The job's
  number is given back; JOB's calls for it come with step 9.
- **HELP** runs HELP.EXE in GLOS's folder with the page, and is refused (`GLOS_E_NOT_FOUND`) until Help is
  there (phase E).
- **Not done here:** a folder has no kind, so OPEN of a folder needs a program named (Files registers itself in
  phase D). With no desktop a notification waits for one.
- **Fixed on the way:** FS's INFO kept its paths in one buffer for every app, across a wait for the VM thread;
  two apps asking at once could read each other's. Each call has its own now.
- **Tests:** host `notes_test` (lifetimes, the queue, the layout) and `assoc_test`; APITEST's SHELL cases (20)
  in `loopa-api`; `api_shell_case` under the agent (the tray's presses and tooltip, four notifications, the
  shell's events, a program from another folder refused).

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
