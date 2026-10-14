# GLOS M6: the GUI core on VBE

The task-level breakdown of PRD §19's M6, in the form of [milestones-m5.md](milestones-m5.md). PRD sections: §5.4
(processes), §6.5 (the virtual PIT), §6.8 (the DOS server), §9 (graphics and the window system), §10.1–10.2 (the
API).

**Exit (PRD §19):** two GLOS apps run at once, and a busy-looping one never stalls the other or the agent.
Desktop golden images match at 8, 16 and 32 bpp on the generic VBE card and on a Matrox card in VBE mode.

## Decisions taken here (proposals, PRD §21's open questions)

- **Q8, launching: DOS EXEC, and the program's own stub.** A GLOS app starts as any DOS program does: the
  resident stub EXECs it, its go32 stub loads it and switches to protected mode, its start-up code runs. It
  becomes an app when it first calls the GLOS API. GLOS loads no images itself: nothing of DJGPP's stub or
  start-up contract is re-implemented, and a program that never calls the API is an exclusive session as
  before (D12's one EXE).
- **Q6, a real-mode child of an app** (its `system()`, a spawn) runs in the system VM as part of that app's
  call into it, which is an exclusive session for as long as it lasts: the other apps wait.
- **Windows keep their pixels.** Each window has a backing store at the screen's depth, owned by the kernel
  (system memory in M6; VRAM on a Matrox card in M7). The screen is composed from them by dirty rectangles. An
  app never has to repaint because another window moved, a screenshot or a VNC frame is a copy, and the
  desktop comes back after an exclusive session without asking anyone. The cost is memory: a full-screen
  window is 300 KB at 640×480×8 and 1.5 MB at 1024×768×16, on machines that have it at those modes.
- **Q11, the toolkit** is M9's; M6 gives it the 2D library and events to build on. Proposed then: our own
  retained-mode toolkit (the look of D7 is its own, and microui and Nuklear are immediate-mode libraries that
  repaint everything on every event).
- **Q5 and Q16, the look and the fonts,** are the owner's: asked in step G, with pictures from the real
  renderer, and answered there (the status below).

## How an app runs (step A, as built: `kernel/app/app.c`)

- **An app is a DPMI context with a thread of its own.** Until now the one context ran on the VM thread. The
  scheduler's switch hook loads what a thread that runs user-mode code needs when it gets the CPU: its
  context (page directory, LDT, watchpoints), the FPU's state, and for an app an I/O map out of reach. The
  virtual interrupt flag is the thread's.
- **Becoming one.** The program is EXECed and starts as a session on the VM thread. Its first API call
  (`glos_init`, function 1 of the entry DPMI 0A00h "GLOS" gives) moves its context to a new thread, which goes
  on from the call's return; the VM thread goes to the stub's `detached` entry, and the stub asks for its
  next job as if the program had ended, though DOS still has it as a process, the stub's child. Refused, with
  a reason, for a program GLOS didn't start itself (COMMAND.COM's child, a program's child), from a handler
  or a callback, for a 16-bit client, and for a session started with `glos run --exclusive` or in direct
  mode: the program then runs on as the session it is.
- **Its protected-mode code runs on its own thread,** preempted like any other, and so does what the DPMI
  host can do for it without real mode: memory, descriptors, handlers, exceptions, its API calls.
- **What needs real mode needs the system VM.** Every real-mode call the host makes for a client goes through
  one function (`rm_call`: a reflected DOS or BIOS interrupt, 0300h to 0302h, a DOS memory block). On an
  app's thread that function hands the call to the VM thread and waits. The VM thread makes it when the stub
  asks for its next job: nothing else is in DOS then, by construction (as for SFTP's calls since M3). For
  the length of the call the VM thread is the app's: the app's context is loaded, so its real-mode
  callbacks, and whatever they call, run exactly as in a session. (The first design re-ran the app's whole
  trap on the VM thread; handing over only the call needs no guess at which traps need real mode.)
- **DOS state for each app** (PRD §6.8), put in place for the call and taken out after:
  - the current PSP and DTA, in DOS's swappable data area;
  - the current drive and directory, set through DOS only when DOS has another (so an app that stays where
    the system VM is costs nothing), and read back after a call that may have changed them (0Eh, 3Bh, 4Bh,
    71xxh);
  - the real-mode vectors the app changed: the table is the system's while the app is out of the VM,
    compared with it when the app leaves and its changes put back when it returns. 0200h and 0201h on the
    app's thread read and write its own copy;
  - a spare parent frame. DOS ends a process by going back into the last INT 21h frame of its parent, kept
    in the parent's PSP; the parent is the stub, which has made other calls since. For the call that pointer
    is a spare frame on the stub's kill stack, and DOS goes on at the program's terminate address, which the
    mode switch pointed at the host.
- **A program an app runs** (its `system()`, a spawn: INT 21h 4Bh) runs inside that call, and is an exclusive
  session for as long as it lasts (Q6). Its exit code is asked of DOS at once and kept for the app's own
  INT 21h 4Dh: DOS has one for everybody, and another app may end first.
- **Its end.** INT 21h 4Ch, a kill or a crash on its own thread frees the context there; the thread then has
  the VM thread make one last call, INT 21h 4Ch as the program. An end inside a call the VM thread is making
  (in a callback, in real mode, DOS's own abort) ends that call. Either way DOS has ended the process when
  the VM thread sees the terminate address; the exit code is INT 21h 4Dh's.
- **Paused by sessions.** While a session has the machine every app stops at its next trap (PRD §6.9), and
  a session's end wakes them. A program that says it is an app is no such session while it starts (step B):
  its file is marked ("GLOSAPP1" in its MZ header's padding: `tools/glosmark.py`, run by the build on
  programs made with libglos), or it was started with `glos run --app`; the others run on beside it (their
  calls into the VM wait for the stub either way).
- **Callbacks' addresses** are ARPLs in the stub, the same in every context, so each belongs to one context
  at a time (there are 32 now, for all of them together): a call that arrives while another context is the
  VM's returns at once.
- **The agent.** A job whose program becomes an app stays that app's: its output (while the VM thread makes
  its DOS calls), its exit code, its kill (`glos kill SEQ`, its client going away). The stub is free, and
  the next job runs. `glos ps` and `glos stat` list apps; `glos exit` ends them first.
- **The scheduler.** Apps share the normal class with the VM thread by running time: the ready thread that
  has run least goes next, and a woken one that has run less than the running one takes the CPU from it. An
  app back from a DOS call, or with a timer interrupt to take, so runs at once beside busy apps, and busy
  ones share what is left. The VM thread goes first whenever it is woken, for an interrupt DOS or the BIOS
  must not wait a round of apps for.
- **The FPU** is saved and loaded at the switch between threads that run user-mode code (not lazily: two
  instructions a switch, and no CR0.TS beside the clients' own MP and EM). An app begins with its program's
  state as it was at `glos_init`.
- **Devices (step B, `kernel/app/dev.c`).** An app reaches no port: every IN and OUT traps, and is answered
  from devices of its own.
  - **A PIC pair**, as the system VM has one. Line 0 is its timer's, line 13 the FPU's: an FPU error
    interrupts while the code that made it runs (CR0.NE stays clear), so one in an app's code is the app's.
  - **A timer** (PIT channel 0). While its count is what GLOS keeps the real timer at (FFFFh, DJGPP's
    `uclock()` setting; or 0), reads are the real counter's and its IRQ 0 comes with the real one: coherent
    with the BIOS tick at 0040:006C, which is the system's and which apps read from memory. Programmed
    otherwise, count and interrupts run off the kernel's tick: right on average at any rate up to 1024 Hz.
  - Its interrupts go to the handlers it hooked in protected mode, at a return to its code with its virtual
    interrupt flag on. One with no handler is ended as the BIOS would end it (the timer's goes on to its
    INT 1Ch handler); real mode's handlers are the system VM's, and never run for an app's interrupt.
  - A wait in the kernel (the API's sleep, INT 15h 86h, HLT) ends early for an interrupt of the app's: its
    handler runs, then the call is made again for the time left.
- **Without asking.** `glos run --app PROGRAM` makes an app of a 32-bit DPMI program that never calls the
  API, at its first protected-mode instruction: DJGPP 2.05's own TIMER, UCLOCK and SIGNALS tests run so,
  unchanged.
- **Not yet:** an app's raw mode switch (0306h) ends it with a crash report (DJGPP has no use for it); an app
  waiting for a key in DOS or the BIOS holds the system VM until step E gives it its own keyboard; a
  real-mode child of an app can't be killed on its own, as a session client's child can't; an app's handler
  for INT 8 that passes the interrupt on doesn't reach its own INT 1Ch handler; while the kernel writes a
  line of its log to COM1 (9 ms for a hundred characters) only urgent threads run, so an app's timer faster
  than about 100 Hz loses an interrupt for each line written meanwhile (lines come with events, a program's
  start or end, not with time).

## Plan (2026-10-11)

Each step is a commit with its tests. The gate (`make gate`) must still pass at B, at F and at the end.

- **A. App threads.**
  - Contexts on the heap; the scheduler's switch hook (page directory, LDT, watchpoints, FPU, I/O map); the
    virtual IF the thread's.
  - `glos_init` (the API's entry through 0A00h, function 1): the client leaves the VM thread; the stub's
    detached entry; `glos ps` and `glos stat` list apps, `glos kill SEQ` kills one.
  - An app's real-mode calls, made by the VM thread at the stub's idle point.
  - DOS state for each app; the end; apps paused while a session runs.
  - The agent: an app's job stays the app's (its output, its exit code) while other jobs run.
  - The API so far: `glos_version`, `glos_init`, `glos_yield`, `glos_sleep`, `glos_ticks` (an app ends as any
    program does). The client library (`sdk/libglos`, DJGPP) and `include/glos/api.h`.
  - T (`jobs.py apps`): two APPTESTs at once, each in its own directory with its own files, directory search
    and timing loop, their results as when run alone; a third that spins forever stalls neither them nor
    `glos tick` (the gate's 2 s rule); it is killed; a session beside them (a DOS command over ssh) pauses
    and resumes them; an app crashes and the others go on; an app runs a real-mode child; `glos exit` with
    an app running ends it first.
- **B. An app's timer.** The virtual PIT and PIC, IRQ 0 to a protected-mode handler, latched reads.
  T: DJGPP's `uclock` and a hooked timer at 100 Hz count right in two apps at once; djtst205's TIMER as an app.
- **C. The display layer and the 2D library.**
  - `kernel/gfx/`: surfaces at 8, 16 and 32 bpp; fills, lines, blits with clipping and a clip region;
    1-bit glyph text; the 8 bpp palette (a 6×6×6 cube, greys and the system colours) and dithered images.
  - Fonts: bitmaps made at build time from open TrueType fonts by a host tool (candidates in step G).
  - The display: a VBE mode with a linear framebuffer, set through the system VM (`[display]` in GLOS.CFG:
    mode and depth); N screens in the data model, one driven; text mode back at `glos exit` and for sessions.
  - T: host tests of every primitive against reference pictures at each depth; `glos shot` of a test card
    equals the host's rendering, on the S3 and the G200.
- **D. The window manager.** Windows with backing stores, z-order, focus, moving and resizing, the composer
  (dirty rectangles), decorations, the software pointer, the task bar (programs, clock, the remote-session
  indicator of D35).
  T: host tests of the composer (windows as rectangles of colour); golden desktops.
- **E. Input and events.** The keyboard and the PS/2 mouse for the desktop (the kernel reads the 8042 as it
  does already); focus, Alt-Tab, moving and resizing by keys (D34); an event queue for each app; INT 16h,
  port 60h with IRQ 1, and INT 33h served to an app from its queue (PRD §6.8).
  T: `glos key` and `glos mouse` drive windows over ssh; an app reading keys three ways gets them only while
  it has the focus.
- **F. API v1** (PRD §10.1): windows, events, the 2D calls on a window's surface, direct access to its pixels
  with `present`; headers with size-prefixed structures and an append-only function table; libglos.
  T: the exit's first half with windows: two apps drawing at once, one busy-looping.
- **G. The look** (D7, Q5, Q16): decorations, colours, the pointer, icons for the task bar, the fonts. The
  owner chooses from rendered candidates.
- **H. The exit.** Golden desktops at 8, 16 and 32 bpp on the S3 and on the G200 in VBE mode; the gate.

## Status

- **A. App threads: done** (2026-10-12). `make loopa-apps`: on the Pentium II with the RTL8029 and the 486DX2
  with the NE2000, 24 apps a run: alone; two at once beside one that loops for ever (`glos tick` answered
  within 0.11 s throughout; each got through 8.8 turns a second on the Pentium II against 11.7 alone, 4.5
  against 7.7 on the 486, the two sharing the system VM for their DOS calls); beside a session, a crash and
  each other; a real-mode child; kills of one looping, one asleep and one waiting in DOS for the keyboard;
  ten more that start and end, after which the kernel has the memory it had; `glos exit` with one running.
  `GLOS.EXE /RUN` with an app on the three machines: GLOS leaves with the app's exit code.
  Found on the way:
  - a new thread's first frame can't be built by code running on that thread's stack: it lies where the
    frame goes (`frame_start`);
  - beside a busy app, every DOS call of another waited out the busy one's turn (10 ms on average): turns
    are now kept across waits, and a woken thread with turn left goes ahead (the scheduler, supervisor.md
    §7);
  - a kill asked for while the VM thread made a call for an app outlived the app, and killed the next
    program started;
  - FreeDOS leaves InDOS at 1 when a program is killed inside DOS (it counts one call's end where MS-DOS
    zeroes the flag): INT 29h output was then never captured again, and every later kill waited its two
    seconds. The stub's NEXT, where nothing is in DOS, now clears it (`GLOS-VM indos=reset`).
  Limits, as built: four agent jobs at once (an app keeps its job's slot while it runs); one current
  directory an app (the current drive's).
- **B. An app's timer: done** (2026-10-12), with what it showed up in the kernel's own timekeeping.
  `make loopa-apps`, now 33 apps a run: `uclock()` never goes back and keeps time with GLOS's clock; a timer
  set to 100 Hz with a handler of the app's own counts 100 a second through the API's sleep, the BIOS's
  wait, HLT and a busy loop in turn, the BIOS's tick still the system's 18.2; two of each at once (100 and
  250 Hz); DJGPP 2.05's TIMER, UCLOCK and SIGNALS (FPU errors as signals, 70 to 230 of them a run) as apps
  without asking.
  Found on the way, none of it in the app code:
  - every session's end set the BIOS clock from the RTC, to the middle of its second: a step of up to half
    a second either way, at every program start once apps run beside. DJGPP's `uclock()` takes a step back
    for midnight. The clock is now left alone when it is right to the tick (supervisor.md §11.1), and the PIT
    is no longer set again at a session's end unless the program moved it (that cost the BIOS clock up to a
    tick each time);
  - apps were woken before the session's end had done its BIOS calls, and paused for the whole start of
    every program, half a second of nothing on the 486 each time another app started. A program marked as
    an app (`tools/glosmark.py`) no longer pauses the others while it starts;
  - the kernel's log cost its own clock: a line written to COM1 with interrupts off loses eight 1024 Hz
    ticks for every hundred characters, 110 ms in 4 s while four apps started on the 486. Lines now wait in
    a ring and are written with interrupts on at the way out of traps and in the idle loop
    (`kprintf_pump`), and the tick count is checked against the PC timer's interrupts while that is GLOS's
    own (`timer_pit_irq`, supervisor.md §8.1);
  - the normal class's round-robin lost a quarter of a 100 Hz timer's interrupts in an app beside two busy
    ones on the 486: it is shared by running time now (supervisor.md §7).
  Not done here: CR0.NE=1 with a synthesised IRQ 13 (PRD §6.10). With NE clear the error interrupt arrives
  while its maker runs, which is all that routing it needs; NE=1 would change what sessions see for no gain
  yet.
- **C. The display layer and the 2D library: done** (2026-10-12; design in [graphics.md](graphics.md)).
  - `kernel/gfx/`: surfaces at 8, 16 and 32 bits; fills, frames, lines, copies, 1-bit and true-colour
    pictures, UTF-8 text, regions; the palette every 8-bit surface has. Host test: 240,000 random drawing
    calls against a one-pixel-at-a-time model, and they agreed at the first run.
  - Fonts from TrueType files by `tools/mkfont.py` (`make fonts`; DejaVu at 11 pixels as stand-ins).
  - `kernel/gfx/display.c`: a VBE mode with a linear framebuffer, found by its description, set through the
    system VM; `[display] mode = WxHxBITS` in GLOS.CFG, `glos display on|card|off`; text mode for sessions and
    the desktop back after them.
  - `make loopa-display`: the test card at 640x480 in 8, 16 and 32 bits on the S3 Trio64V2 (Pentium II and
    486DX2) and the G200, each screenshot equal to the host's rendering pixel for pixel; a session and an
    app beside it; the desktop from GLOS.CFG (640x480x8, 800x600x16).
  - Found on the way: the Matrox BIOS's teletype writes into a VBE mode's framebuffer, so an app's console
    output drew on the desktop. While the desktop has the screen, programs' video BIOS calls are answered
    by the kernel and the VGA's memory at A0000h is out of reach (graphics.md §2.3).
- **D. The window manager: done** (2026-10-12; graphics.md §3).
  - `kernel/wm/wm.c`, the core: windows that keep their pixels, the order, the focus, frames drawn from the
    look, the task bar (a button a window, the remote-session note of D35, the clock), the pointer, and the
    composer, which draws each damaged part of the screen once, from the top window down.
  - `kernel/wm/desk.c`: the core in the kernel, with its own thread and windows' pixels in pages of their
    own; `glos desk`, `glos desk demo|clear`.
  - Host test: the composer against a model, with windows as rectangles of colour (30,000 random things at
    each depth), and with frames, bar and pointer against everything drawn afresh.
  - `make loopa-desk`: the demonstration desktop at 640x480 in 8, 16 and 32 bits on the S3 Trio64V2 (Pentium
    II and 486DX2) and the G200, each screenshot equal to the host's composition; the same after a session.
  - Found on the way: the composer's thread as an urgent one kept the system VM from its interrupts for the
    length of a whole screen's drawing (`vif-stuck` after a session): it is of the normal class.
  - Left for step E: moving and resizing by hand (the calls are there, and tested); for step F: windows that
    are apps'.
- **E. Input and events: done** (2026-10-12; graphics.md §4, §5.4).
  - `kernel/wm/deskin.c`: the keyboard and the mouse while the desktop has the screen, from the 8042's code
    (real bytes and injected ones alike); the focus by a press, a task-bar button and Alt-Tab; moving and
    sizing by the title bar and the corner, and by Alt-F7 and Alt-F8 with the arrows (an outline, then the
    window); the close box and Alt-F4; the kill hotkey for the app in front.
  - An event queue for each app (`GLOS_FN_EVENT`), and for an app with a window a keyboard and a mouse of
    its own as a DOS program reads them: INT 16h, port 60h with IRQ 1, INT 33h (`kernel/app/dev.c`).
  - Found on the way:
    - DJGPP's own IRQ 1 handler reads port 60h and passes the interrupt on to the BIOS's, which reads it
      again: the port must hold a byte until its interrupt has ended (the first model gave the next byte at
      once, and INT 16h lost every other key);
    - a mouse driver unloaded before GLOS starts leaves the AUX port and IRQ 12 on and the mouse quiet: the
      mouse is asked whether it reports (E9h).
- **F. API v1: done** (2026-10-12; graphics.md §5; `include/glos/api.h`, `include/glos/gfx.h`).
  - Functions 5 to 15: events; windows made, closed, shown, moved, sized, titled, focused; their pixels,
    mapped into the app; `present`; the screen. `kernel/wm/deskapp.c` is the desktop's side.
  - libglos: the wrappers, `glos_ptr`, `glos_surface`, and the 2D library with its fonts, the kernel's own
    sources built for the program.
  - `make loopa-win`: five cases (the S3 at 32 bits, the G200 at 16, the 486 at 8; 86Box's own keyboard and
    PS/2 mouse with CuteMouse and without). The exit's first half: an app drawing 50 times a second keeps
    47 a second beside one drawing in a busy loop on the Pentium II (49 alone) and 42 on the 486 (49);
    `glos tick` answered within 0.1 s; the busy one ended by the hotkey.
  - A window's pixels can change behind the app (the user's new size, another depth): the app is told and
    asks again, and memory it has mapped is never reused until it has let go.
- **G. The look: done** (2026-10-13; graphics.md §3.6).
  - The owner chose from rendered candidates (four looks, four font sets, each the demonstration desktop as
    the window manager draws it at 640x480 and 8 bits): the look "Slate", and DejaVu Sans at 11 pixels as
    it was (PRD Q5 and Q16 answered).
  - With it a new decision, PRD D42: a DOS program in a window is drawn in the VGA's own fonts, not the
    UI's. Nothing draws one yet (D5); the demonstration's third window, which looked like a DOS prompt in
    the UI's fixed-width font, shows GLOS's own text instead.
  - `kernel/wm/wm.c`: the look's colours, the focus's close box filled with the one accent, and the task
    bar's pictures (GLOS's mark, a window on each button, a sign beside the remote-session note).
- **H. The exit: done** (2026-10-14). M6's exit (PRD §19) is met: two GLOS apps run at once and a
  busy-looping one stalls neither the other nor the agent; the desktop's golden images match at 8, 16 and 32
  bits on the generic VBE card and on a Matrox card in VBE mode; the regression and the gate pass on the last
  commit, 763383d (the gate's two failures were the host's, below, and their cells pass again).
  - **Golden desktops:** `make loopa-desk`, the demonstration in the chosen look at 640x480 in 8, 16 and 32
    bits, each screenshot equal to the host's composition, on the S3 Trio64V2 (Pentium II and 486DX2) and on
    the G200 in VBE mode; the same after a session.
  - **Two apps at once, one busy:** `make loopa-apps` (two beside one that loops for ever: 7.4 turns a second
    each against 11.7 alone on the Pentium II, 3.5 against 8.0 on the 486; `glos tick` within 0.13 s) and
    `make loopa-win` (one drawing 50 times a second beside one drawing in a busy loop: 47 a second against
    49 on the Pentium II, 42 on the 486; `glos tick` within 0.1 s).
  - **The regression** on the last commit, alone on the host: the host tests and 24 Loop A suites (m1, m2,
    mem, shell, sess, ssh, gdbprog, input, djtst, dpmitools, hdpmireg, ecm, dpmi16, tpx, dpmi, hostile,
    sched, apps, net, shot, m5exit, display, desk, win), 161 cases, none failed.
  - **The gate** at step B, run beside other suites: 130 of 133 parts, the three one cell (Fifth Wheel
    under HIMEMX: its plain-DOS baseline timed out, and on a second try its baseline on the other boot
    underran its audio while GLOS's runs passed). That cell needs the host to itself; the gate runs alone
    from now on, once on the last commit for step F and the end together (step G changed the look only).
  - **The gate on the last commit, first try** (2026-10-13 evening): 126 of 133, and none of the seven
    GLOS's. The host ran out of memory in the middle of it (another project's build; the kernel's log has
    the out-of-memory killer called at 22:53:44, swap full, and nothing killed until 23:00:50, then again
    at 23:07:17). Three games that had started at different times all "hung" and ended within two seconds
    of each other at 23:00:44, two suites' results came right after, one ssh probe went unanswered. Away
    from that the same build hung in none of 28 ClassiCube runs (twelve at once among them) and failed
    none of 18 ecm runs. The gate's runner now reads the kernel's log for such kills during its run and
    says so in its summary (`host_oom`).
  - **The gate on the last commit, second try** (2026-10-14 night, four runs at a time): 131 of 133. The
    host ran out of memory again (the kernel's log: 04:17:15 to 04:20:15), and the summary said so. One
    failure was in it: the G450 conformance (m4c, forced direct) ran from 04:12:51 to 04:20:39, five of its
    tests hung together and the next passed. The other was not: Fifth Wheel with the ssh probe underran its
    audio (24,581 of 43,122 chunks) from 04:56 to 05:42, its game and its probes fine. That counter follows
    the host's load, not GLOS (it failed once in a baseline with no GLOS at all): the probe run again, twice
    on this build and twice on step B's side by side, passed all four times with no chunk underrun and
    every probe answered. The m4c cell was run again by itself and passed, both ways (MGA-Glide's
    conformance 27 of 27 on G100, G200, G400 and G450, the GTA and Screamer Rally replays, DOSBench),
    though the host ran out of memory once more meanwhile.
  - Found on the way: a program marked as an app paused the apps for a moment while its mark was read at
    the EXEC, and an app that looked just then waited out the whole start of the new program (a quarter to
    three quarters of a second on the 486). It showed as the apps suite's four timers at once failing now
    and then, a 100 Hz timer short of 30 interrupts in 400; it had been there since step B. With the order
    put right (supervisor.md §11.1), twenty rounds of the four lose 11 of 400 at worst.
  - The window suite's busy app is judged by its picture moving and its square being in one of the
    samples: on the 486 the composer copied its band between the app's clearing it and drawing the square
    (graphics.md §5.2).
  - `GLOS-WARN vif-stuck` once after a session beside a window on the G200: the system VM, preempted inside
    a stretch with its interrupts off and one waiting, sat out the composer's whole screen and the app's
    turn. The VM thread goes first while an interrupt waits behind its cleared flag, for the watchdog's 50
    ms (supervisor.md §7).
  - HDPMI's regression tests I3102101 and I3102102 printed a damaged line each. The tests' own doing: their
    printf writes a number's digits through DS at an address taken from EBP, which in an exception handler
    on the host's stack is low memory at the stack's offset (3F5xh under GLOS, F5xh under HDPMI). On a boot
    without HIMEM FreeDOS has its disk buffers there, and since step C's larger loader moved the files on
    the test disk, the buffer at that address was the one with the test's own output in it. Found by the
    file's bytes (the digits with their NUL) and the test's code; the suite lets those two differ in their
    text (`HR_SCRIBBLE`).
  - HDPMI's I310508A, looked into at the owner's word: under HIMEMX it differed from HDPMI in a page's
    accessed and dirty bits (79h for 19h) after 0507h made memory of mapped pages. It was HDPMI's answer
    that changed with the boot (79h without XMS, where the suite runs, and so "the same"); GLOS said 79h
    always, because the kernel zeroes a new page through the page's own address and the CPU marks it. A
    fresh page now reads as the client left it, not accessed and not written, and 0507h sets the two bits
    when asked (it ignored them). DPMICONF has the checks: cleared on request, then set by a read and by a
    write; CWSDPMI, HDPMI32i and GLOS all pass them.
  - When GLOS leaves, the mouse's answer to the desktop's "stop reporting" is taken before real mode gets
    the chip back (`vkbc_leave`); it could have come after.
  - Seen once and not explained: on the 486, with three machines running at once, the batch file's last
    line never ran after `glos exit` from the desktop. GLOS had left as it should (exit code 0, the
    vectors as they were, text mode) and two programs ran after it. The same case alone passed six times
    and a shorter one six more; its log is kept, and the next time decides whether it is ours.
