# Phase D: the desktop shell and the system (step plan)

Plan v1, 2026-11-17 (the owner's one question answered the same day), written when phase C (toolkit 2) was merged (GLOS main 07dbd70) and the owner chose phase D
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

### D1 as built, first part (2026-11-17)

- **The desktop's pop-up menus** (`kernel/wm/popmenu.c`, pure and host-tested by `popmenu_test`;
  `kernel/wm/deskpop.c` draws them and acts): items with "&" letters, lines between, ticks and dim ones; Up, Down,
  Home, End, Enter, Esc and the letters (a letter only one item has chooses it, else moves to the next with it);
  the item under the pointer is the current one. Drawn by the kernel in the task bar's colours; one at a time; it
  has the keyboard while it is up and gives it back when it goes without a choice; a press outside closes it and
  goes on to what it was on.
- **A window's menu** (Restore, Minimise, Maximise, Close, each dim when it can't be done): a task bar button's
  right button, or Alt+Space for the window in front (under its title's left end, as Windows has it).
- **Grouping** (`[desktop] group = auto | always | never`, auto by default): a program's windows share one button
  when the buttons would be narrower than 72 pixels (auto), always, or never; the button shows what their titles
  end in after " - " ("Notepad") and how many, is in front when any of them is and flashes when any does, and a
  press (either button) drops a menu of its windows, the one in front ticked. Windows of the kernel's own are
  never grouped. `wm_bar_entry_at` gives a button's window, how many it stands for and its left edge.
- **Alt+Tab's strip:** while Alt is held, every window that can have the focus, the one in front first, then the
  others from the top down, the minimised ones last, each its icon (16 pixels drawn twice the size: no 32-pixel
  icon from apps yet) in a row over the screen's middle, the current one's title under them; Tab and Shift+Tab
  move along it, Esc gives up, letting Alt go switches. One Alt+Tab is still "the window before" (now the one most
  recently in front, not the next by when it was made).
- **Tests:** `popmenu_test`; `wm_test` (grouping: auto on a narrow and a wide bar, always, never, a minimised
  window in its group, the kernel's own windows); Loop A `win` (Alt+Space and its letters, a button's menu,
  the strip held, given up and switching).

### D1 as built, second part (2026-11-17)

- **The clock:** today's date in its tooltip, as the region writes a date; a press drops a month's calendar
  (kernel-drawn, the weeks from the region's first day, today marked; PgUp and PgDn, Left and Right, or a press
  on the header's arrows go a month back or on; Esc or a press elsewhere closes it, a press on the clock too);
  a double press opens the Control Panel at Date and Time (`CONTROL SECTION` now opens that section's page;
  `api2_shell_start` starts one of GLOS's own programs for the desktop).
- **The tray's overflow:** at most four icons on the bar, the first ones added (GLOS's own will be, from D2);
  the rest behind a ^, a press on which drops them in a row; a press on one there is that icon's press for its
  app, as on the bar.
- **Quiet** (`[desktop] quiet = off | on`): notifications wait, and the bar shows how many in the accent's
  colour; a press on that number shows them, one after another, until none wait.
- **The bar's place** (`[desktop] bar = bottom | top`, `bar_hide = off | on`): at the top the work area starts
  under it, and pop-ups, tooltips, notifications and the calendar open below it; hidden, it takes nothing from
  the work area and comes out when the pointer reaches the screen's edge it is at, going back once the pointer
  leaves it (not while one of its pop-ups is up). Maximised and snapped windows follow (`wm_bar_place`, then
  the apps are told their new sizes).
- **Not done:** the Start menu's own placement with the bar at the top (Files' launcher is told the bar's edge;
  D3's Start menu uses it); a WIN property for a 32-pixel icon (Alt+Tab draws the 16-pixel one twice the size).
- **Tests:** Loop A `win` grows: the calendar up and closed by Esc, the clock's double press bringing up the
  Control Panel, the bar at the top with a window maximised under it and back, hidden, out at the edge, in
  again. Host: `cfg_test` knows the new settings; the Control Panel's Desktop page has them (`control` goldens).

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

### D2 as built (2026-11-17)

- **GLOS's own tray icons** (`kernel/wm/desktray.c`): no app's; four times a second at most each is made to
  match what it stands for (there or not, its picture, its tooltip). Their pictures are one-bit shapes in the
  bar's text colour, made again when the theme changes, so they are right in every theme. A press on one is the
  kernel's (`deskshell.c` keeps whose each icon is), on the bar or behind the ^.
  - **Volume** (while a card plays): a speaker, quieter below 37%, crossed when muted or at nothing; "Volume:
    75%" in its tooltip. A press: an upright slider with its percentage and a Mute box (`deskpop.c`: a press or
    a drag on the track, Up and Down a step, PgUp and PgDn a big one, M, Esc); the wheel over the icon: up and
    down; the right button: mute; twice: the Control Panel's Sound page. It is the mixer's master, set at once,
    and kept in `[sound] volume` and `mute` when the slider goes (after the desktop's lock is let go).
  - **Network** (with a card, or a cable link): crossed out without an address; the address in its tooltip. A
    press: the card, address, gateway, name server and frames in and out, and "Network settings..."; twice: the
    Control Panel's Network page.
  - **Pads** (while one is plugged in): which joysticks in its tooltip. A press: each pad and "Game
    controllers..."; twice: the Joystick page.
  - **A remote session:** PRD D35's note (VNC) is an icon now, not words on the bar; a press says who looks.
- **Pop-ups of the desktop's own making** (`deskpop_begin`, `deskpop_show`): a menu filled by the kernel, with
  lines of words (`PMI_TEXT`) that are read, not chosen, and a function told the item chosen.
- **GLOS's notifications** (`deskshell_note`; `desk_notify_own` from other threads; their owner is nobody, so no
  app hears of them): a pad plugged in or taken out, the network connected (with its address) or down, a window
  whose app isn't answering (once, until it answers again). Not at the start for what was already so.
- **`glos desk`** lists the tray too: `tray ID app|own at X,Y "tooltip"` (-1,-1 behind the ^), for the tests.
- **Not done:** "a disk nearly full" (nothing in the kernel watches DOS's free space yet) and "a scheduled run
  failed" (the scheduler queues a run and doesn't hear how it ended); the link's state and the API's "device came
  or went" event are phase H's, as planned.
- **Tests:** Loop A `win` (its first PC has a Sound Blaster): the network's icon with the guest's address; a
  virtual pad plugged in (its icon and a notification) and taken out; the volume's slider, a step down, kept in
  GLOS.CFG, muted and not by the right button.

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

### D3 in detail (2026-11-17, before it is built)

- **The window:** Files' own, a pop-up (`GUI_W_POPUP`: bare, above the others, with the keyboard; `on_popdown`
  hides it when a press elsewhere takes the keyboard). Made once, shown at the bar's edge that
  `GLOS_SHELL_LAUNCH` gives: above it, or below it when the bar is at the screen's top. Esc hides it.
- **Left:** a search field, then one list (a virtual list): with nothing typed, "Most used" (the five started
  most from the menu, Files' own count), then every entry under its first letter; typing filters it to what
  matches, by kind: programs (their titles), the Control Panel's pages (their titles: opened as
  `CONTROL SECTION`), recent documents (their names). Enter opens the current row (the first, while typing);
  Down from the field goes into the list. The names of files in the user's folders (an index built in the
  background) come in a second part.
- **The entries:** SHELL's ENTRY list (the programs whose profiles ask to be listed) and GLOS's own apps beside
  FILES.EXE (Files' table, as the desktop's icons have them).
- **Right:** the pinned tiles, big icons with their titles in a grid (a list's icon view), in the order the user
  dragged them to; Enter or a double press starts one. A right press (or the Menu key) on a row or a tile: Pin
  to Start or Unpin, and the entry's jump list: its tasks (ENTRY) and its recent documents (RECENT).
- **Bottom:** Settings (the Control Panel), Files (the browser), Power: a menu of Exit GLOS, Restart GLOS and
  Restart the PC.
- **Kept** in START.CFG beside FILES.EXE: the pins in their order, and how often each entry was started.
- **The host:** the shim answers `glos_entry_list` and `glos_recent_list` with a few made-up entries and
  documents, so that the script `start` plays there: the menu up, a search, a page of the Control Panel found,
  a tile pinned and dragged, a jump list, Power's menu; `files` opens Run from the Start menu's search instead
  of the launcher's menu.

### D3 as built, first part (2026-11-17)

- **The Start menu** (`apps/files/start.c`): as designed above. One window, made the first time and hidden
  between times (made again only if the task bar's edge has moved): a new window each time could be told of the
  old one's lost keyboard. The launcher's event while it is up takes it away.
- **Its pages** are in the list itself, not menus over it (a context menu would take the keyboard, and the
  Start menu with it): More (the Menu key, Shift+F10, or its button) turns the list into the current row's or
  tile's own page: Open, Pin to Start or Unpin, Move its tile earlier or later, its tasks, its recent documents,
  Back; Power turns it into Exit GLOS, Restart GLOS, Restart the PC, Back. Esc goes back a page, then away.
- **Typing anywhere** in it goes into the search; Down from the search goes into the list. A row that finds
  nothing else is "Run: what was typed": a folder or a file there is opened, anything else run (the old
  launcher's Run).
- **Kept** in START.CFG beside FILES.EXE: `pin` and `use` lines. With none, the first four of GLOS's own apps
  are pinned.
- **The toolkit:** a pop-up window with `on_key` set is a panel of widgets and has a window's keys (Tab, Alt
  and a letter, its default and cancel buttons, `on_key`); menus and drop-downs, which set none, are as they
  were (`sdk/gui/core.c`).
- **The host shim:** `glos_entry_list` and `glos_recent_list` answer with a few made-up entries and documents;
  a program started detached is taken as started, so that Files goes on as it would and says what it ran.
- **Not yet:** the index of file names for the search; tiles dragged by the mouse (they move by their page's
  rows) and their groups and sizes; "Recently added".
- **Power's three rows** run `glos exit`, `glos restart` and `glos reboot` as lines of GLOS's own shell (a
  `GLOS_RUN_SHELL` job; a job's command would be run as a program). Exit GLOS was proven by hand on Loop A
  (2026-11-17: Files as the shell, Ctrl+Esc, Alt+P, Enter: Files is asked to end and says so, GLOS exits with
  code 0, DOS goes on); no suite ends GLOS from the menu.
- **A press on the task bar's mark while the menu is up** takes it away (the press takes the keyboard from it,
  and the launch that follows within 300 ms is ignored).
- **Loop A:** the native suite's `files` (the Start menu's picture is the host's) and `start-run` (the Calculator
  and the Control Panel's Region page found by typing and really started; Esc; the shell ends), on a Pentium II
  with the VBE card and a G200, and a 486.
- **Tests:** host `start` (found by typing: a program, a page of the Control Panel, a recent document, a
  folder's path; More: unpinned; a tile by Tab and Enter; a task from Doom's page; Power's page) and `files`
  (the folder opened by its path typed in the Start menu).

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
- How one runs: as an app (the owner's choice, below): a full-screen topmost window drawn through DOS-GL's
  windowed path. The saver programs are GLOS's (`apps/savers`), built
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

## Decided by the owner (2026-11-17)

- **A saver runs as an app:** a full-screen, topmost window of a GLOS app drawing through DOS-GL's windowed path
  (as `loopa-glwin` tests), so the desktop, its apps and the background services go on beneath it; the Control
  Panel's preview is the same program in a small window. Not chosen: a saver as an exclusive session (which
  would pause every app and service while it runs).
