# Phase D: the desktop shell and the system (step plan)

Plan v1, 2026-11-17, written when phase C (toolkit 2) was merged (GLOS main 07dbd70) and the owner chose phase D
next. What D is for: `docs/v1-route.md`, "D. The shell and the system". Its design is phase A's `desktop.md` (the
task bar, the Start menu, notifications, savers, the look), the Control Panel's and the task manager's plans
(`apps/control-panel.md`, `apps/task-manager.md`), and what phases B and C built under it. This plan says what is
built in which step, where, how each is tested, and what waits for other phases.

## What there is already

- **The kernel's desktop** (`kernel/wm`):
  - the task bar is a window of the window manager's own: the GLOS mark, a button for each framed window (its
    icon, flash and progress; a click activates or minimises it), the clock (the region's format) and a note
    (VNC's remote session);
  - the notification area: up to 12 apps' 16-pixel icons, their click, double-click and menu, a tooltip after
    0.6 s (`deskshell.c`);
  - notifications: one shown, 8 queued, 6 s, held back under a full-screen program (`notes.c`);
  - Alt+Tab moves the focus only; the screen saver is a black fill drawn by the desktop (`desk.c`, `idle.c`).
- **API 2.0's SHELL area** (phase B step 8): DESKTOP, TRAY, NOTIFY, POINT, HELP, OPEN, ASSOC, ENTRY (the Start
  menu's entries from the programs' profiles: menu, title, icon, args, group, path, tasks), RECENT (32 per app),
  SCHEDULE; `GLOS_EV_SHELL` (entries and recents changed). IDLE: the idle time, AWAKE, the saver's start and end,
  the screen's power (VBE/PM). SETTINGS: THEME (three themes), REGION, WATCH.
- **Files is the shell app** (`apps/files`): the desktop's icons (DESKTOP.CFG, profiles with `desktop=1`) and a
  launcher that is a plain menu (a fixed list, profiles with `menu=1`, Run...), opened by the mark, the Windows
  key and Ctrl+Esc (`GLOS_SHELL_LAUNCH`).
- **Toolkit 2** has what the Start menu, the Control Panel and the task manager need: the virtual list and tree,
  search fields (combo), tiles (a list's icon view; drag and drop), a property grid, the preview surface (EMBED),
  a graph, tasks for background work.

## The split, as phase A decided it

GLOS (the kernel) draws what must never wait for an app: the task bar, the notification area, notifications,
Alt+Tab, and, new in this plan, the small pop-ups of GLOS's own tray icons and the clock (a volume slider, a
month's calendar, a list of devices: fixed layouts, drawn as the bar is). Files draws what is an app's business:
the Start menu and the desktop's icons. Everything a user changes is a setting in GLOS.CFG (SETTINGS), changed by
the Control Panel.

## The steps

Each step is a branch, its tests, the gate, and a merge; the order is the one below.

### D1. The task bar whole (kernel)

- A button's right click: its window's menu (restore, minimise, maximise, close).
- Buttons grouped by app when they no longer fit (`[desktop] group = auto | always | never`), a group's button
  dropping a menu of its windows.
- Alt+Tab's strip: the windows' 32-pixel icons and titles in a row over the screen's middle, Alt+Tab and
  Alt+Shift+Tab to move, the release to switch (the window's 32-pixel icon: a WIN property, 16 scaled up until an
  app gives one).
- The clock: the date in its tooltip; a click drops a month's calendar (kernel-drawn); a double-click opens the
  Control Panel's Date, time and region page.
- The notification area's overflow (^) past four icons; the bar at the top or the bottom
  (`[desktop] bar = bottom | top`) and hidden until the pointer reaches its edge (`[desktop] bar_hide`).
- Notifications: "do not disturb" (`[desktop] quiet`), and the bar's count of those held back while an exclusive
  session runs.
- **Tests:** `wm_test` (grouping, the strip's order, overflow); Loop A `desk` grows (right-click menu, grouping,
  Alt+Tab switching, the calendar's month, top and hidden bars), `win` and `api` (a 32-pixel icon).

### D2. GLOS's own tray icons and notifications (kernel)

- **Volume:** the mixer's master (AUDIO); a click drops a slider and mute (kernel-drawn), the wheel over it
  changes it; a double-click opens the Control Panel's Sound page until phase F's Mixer is there.
- **Network:** the address in its tooltip (the link's state is phase H's: until then "connected" means an
  address); a click shows status (address, gateway, frames in and out); a double-click opens the Network page.
- **Pads and USB devices:** what is plugged in (GLOS's own USB stack knows; the API's "device came or went" event
  stays phase H's), a pad's icon showing which joystick it is; a click lists them.
- **A remote session:** today's text note becomes an icon, its peer in the tooltip.
- **GLOS's notifications:** a pad plugged in or out, the network up or down, a disk nearly full, an app not
  answering (the window manager knows: `WF_HUNG`), a scheduled run that failed.
- **Tests:** Loop A `desk`: the slider changes the master (`glos stat`), the network icon's tooltip has the
  guest's address, a virtual pad (`glos joy pad=`) plugs in and its notification shows, a hung test app's
  notification; host `notes_test` for the queue with quiet and held back.

### D3. The Start menu (Files)

- Files' launcher becomes the Start menu of `desktop.md`: on the left recently added and most used, then every
  app A to Z (letters jump), from SHELL's ENTRY list (it no longer has its own list); search as you type over
  apps, the Control Panel's pages (their labels and the schema's descriptions), recent documents (RECENT) and the
  names in the user's folders (an index built by a task in the background, kept in Files' folder); Enter opens
  the first; tiles on the right: pinned apps and programs in groups the user names, small or medium, dragged to
  reorder (kept in Files' settings); jump lists (an entry's recent documents and tasks) by a right click; Settings,
  Power (exit, restart GLOS, reboot) and the user's folders at the bottom. All by keys.
- Folders get a kind of their own for OPEN, Files registering itself for it (phase B's note).
- **Tests:** host script `start` (search, letters, tiles dragged, a jump list, Power's menu) and its goldens in
  the three themes; Loop A native (Ctrl+Esc opens it, typing finds Notepad, Enter starts it).

### D4. Wallpapers and the look (kernel and the Control Panel)

- `[desktop] wallpaper` (a picture), `wallpaper_style` (centre, stretch, fill, tile) and `colour`: the Control
  Panel decodes the picture it is given (stb_image) and writes it at the screen's size beside GLOS.CFG as a
  plain picture the kernel reads (no decoders in the kernel); the desktop draws it under the icons, dithered on
  8-bit desktops; made again when the mode changes.
- Themes: the three as they are; the Control Panel's spec names a light and a Windows 98-like theme too (to be
  added as theme tables if the owner wants them: not in this plan).
- **Tests:** host `wallpaper_test` (scaling, styles, dithering); Loop A `desk` (a picture set, then the mode
  changed: the wallpaper follows).

### D5. The Control Panel grown

- The pages of `apps/control-panel.md` that phase B's generic pages don't make well: Appearance (theme, text
  size, wallpaper with its preview), Screensaver (which, the wait, the power-off wait, its preview: D6),
  Display (the modes the card has, from DRAW's MODES; 15 bits as a choice of its own when the card has it; 24
  bits waits for a second vendor's card), Sound (devices, volumes), Network (status and settings), Date, time and
  region, Mouse and keyboard, Game controllers (the gamepad plan's J6: the pads, the mapping, a test), USB
  devices, Startup programs and default apps, Secrets (the store's master password: phase B noted it for D);
  search over every page.
- **Tests:** the `control` suite (every setting by keys) grows with the new pages; host scripts per page.

### D6. Screensavers

- The savers of `desktop.md` on DOS-GL: starfield, pipes, a 3D maze, flying objects (a flag, GLOS's mark), a line
  of 3D text; and blank (today's) with the screen's power off after a further wait.
- How one runs: **for the owner** (below). Either way the saver programs are GLOS's (`apps/savers`), built
  against RetroGPU's DOS-GL as the games are (no copy of it), drawn by a Matrox card's engine or by DOS-GL's
  software renderer on any other card, at a modest size on a 486.
- The preview: the saver drawn small inside the Control Panel's Screensaver page (`gui_preview`, EMBED).
- **Tests:** each saver's first frames on the host against references (the software renderer, as DOS-GL's own
  tier 1); Loop A: after the wait the saver starts, a key ends it and the desktop is as it was; the preview;
  bench-g400 and bench F: each saver timed in hardware and in software.

### D7. Background apps (`[services]`)

- A list of apps GLOS starts with itself (with or without a desktop), starts again if they end (with a growing
  pause if they keep ending), and lists, stops and starts by the shell's `services` command and the task
  manager. Supervision only: they wait while a DOS program has the whole PC, as every app does.
- **Tests:** Loop A `console` or `apps` (a service started at boot, killed and restarted, stopped by command).

### D8. The task manager (Monitor grown)

- `apps/task-manager.md`: programs, apps and their windows, sessions, services, memory, the network, the sound
  streams; end a task; the processor over time. SYS's STAT gains the structures it needs (NET, AUDIO, USB), so
  that Monitor reads numbers and not text.
- **Tests:** `monitor` script (masked figures), Loop A native, APITEST's SYS cases for the new structures.

## What waits for other phases

- The volume icon's double-click opens the Mixer when phase F has made it (the Control Panel's Sound page until
  then).
- The network icon's link state, and the API's "device came or went" event, are phase H's.
- 24-bit desktops wait for a second vendor's card (S3 ViRGE on bench C is the first).
- The savers on non-Matrox cards use DOS-GL's software renderer, which is merged in RetroGPU (B1, B2, V3); its
  speed on a 486 decides each saver's size there.

## Decided in this plan (mine; the owner may change them)

- GLOS's own tray icons' pop-ups and the clock's calendar are drawn by the kernel, like the bar (they must answer
  even when Files is busy).
- The wallpaper is decoded and scaled by the Control Panel, not the kernel (no image decoders in ring 0 beyond the
  PNG reader it has for its own use).
- Files keeps the Start menu's tiles and its search index in its own folder, not in GLOS.CFG.
- The bar's new settings default to today's behaviour: at the bottom, not hidden, grouping `auto`, quiet off.

## For the owner

- **How a saver runs.** (a) As an app: a full-screen, topmost window of a GLOS app drawing through DOS-GL's
  windowed path (as `loopa-glwin` tests), so the desktop, its apps and the background services go on beneath it;
  the preview is the same program in a small window. (b) As an exclusive session: the saver takes the whole PC as
  a DOS program does, which pauses every app and service while it runs (for hours, if the PC is left). My
  recommendation: (a).
