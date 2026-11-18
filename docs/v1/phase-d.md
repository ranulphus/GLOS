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
- **A tile dragged** onto another's place moves there (the toolkit's lists drag their chosen row; the tiles take
  it back as a move, in a format of Files' own); it moves by its page's rows too.
- **Files by their names:** the names of the files on the hard drives (C: and up) are walked a folder at a time
  in the shell's idle moments once the menu has first come up (no thread: one folder's read is short; 4000
  names at most, six folders deep; walked again when the menu comes up five minutes or more after the last walk
  ended), kept in memory, not in a file. Two letters or more typed list up to eight files whose names have them,
  each with its folder; Enter opens one as Files opens any file.
- **Not yet:** the tiles' groups and sizes. ("Recently added": further down.)
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

### D4 as built (2026-11-17)

- **Changed from the plan:** the shell draws the wallpaper, not the kernel. Files' desktop window is the whole
  work area under the icons already, so a picture drawn by the kernel would be covered by it; and Files can have
  the decoders the kernel is to do without. (With no shell running the desktop is its theme's colour, as before.)
- **`[desktop] wallpaper`** (a picture: PNG, JPEG, BMP, GIF or TGA, by stb_image as Images has it) and
  **`wallpaper_style`** (`fill`, the default: made as big as covers the desktop, what is over cut away;
  `stretch`: made its size; `centre`: in its middle at its own size, the theme's colour round it; `tile`: side
  by side). `apps/files/wall.c` reads the picture once into one the desktop's size (the mean of the pixels a
  place covers when made smaller, the nearest when larger) and draws it under the icons; the 2D library dithers
  it on an 8-bit desktop. Made again when either setting changes (the shell watches `[desktop]`:
  `GLOS_EV_SETTING`), when the theme changes, and when the desktop's size does. One that can't be read is no
  wallpaper. It costs the desktop's size in 32-bit pixels of the shell's memory (1.2 MB at 640 by 480).
- **The desktop window follows the work area** (a gap of D1's): when the task bar goes to the top or hides,
  the shell's desktop window is moved and sized to the work area, so its icons are never under the bar.
- **Not done here:** the Control Panel's Appearance page with a file chooser and a preview (D5: the generic
  Desktop page has the two settings meanwhile); a light and a Windows 98-like theme (not planned).
- **Tests:** host and Loop A `wall` (tiled) and `wall-fill` (the default), each the desktop with its icons over
  the picture, at 8, 16 and 32 bits.

### D5. The Control Panel grown

- The pages of `apps/control-panel.md` that phase B's generic pages don't make well: Appearance (theme, text
  size, wallpaper with its preview), Screensaver (which, the wait, the power-off wait, its preview: D6),
  Display (the modes the card has, from DRAW's MODES; 15 bits as a choice of its own when the card has it; 24
  bits waits for a second vendor's card), Sound (devices, volumes), Network (status and settings), Date, time and
  region, Mouse and keyboard, Game controllers (the gamepad plan's J6: the pads, the mapping, a test), USB
  devices, Startup programs and default apps, Secrets (the store's master password: phase B noted it for D);
  search over every page.
- **Tests:** the `control` suite (every setting by keys) grows with the new pages; host scripts per page.

### D5 as built, first part (2026-11-18)

- **Decided here:** the pages stay the schema's sections, in its order, and a page grows by what is put beside
  or under its generic rows. The control suite walks every setting by the pages' order and each label's letter,
  and goes on doing so; a page of settings from several sections (the plan's Appearance) would have needed a
  second map of what is where, for the app and for the suite. So the wallpaper, the theme and the text size are
  on the Desktop page, with the desktop's look beside them.
- **The frame:** the pages' names are a list down the left (toolkit 2's `GUI_TABS_LIST`), which gives the pages
  the two rows the titles took. Ctrl+Tab goes from page to page as before.
- **Search:** a field over the list (Ctrl+F). Enter goes to the first setting whose label, description or page
  name has the word (whatever its capitals): its page, the keyboard to its widget, its description under the
  page as always, and the status line says what was found. F3, or Enter in the field again, goes to the next
  one, round and round; another word starts from the first setting. A word nothing has is said so.
- **The desktop's look** (`apps/control/look.c`): beside the Desktop page, a picture of the desktop 80 by 60 as
  the wallpaper and style in the page would make it (not yet applied), with a window and the task bar in the
  theme's colours; and **Choose...**, the file dialog with the kinds of picture, whose choice goes into the
  field. The picture is made as FILES makes the real one, by the same code (`apps/files/wallmake.h`, in a
  header because the two programs each have a copy), for the screen's work area, then made small; it follows
  the field (once the typing has stopped for a moment) and the list.
- **For its room at 640 by 480:** the Desktop page's fields are 20 characters wide, and "Snap windows at the
  screen's edges" is "Snap to the screen's edges".
- **Display's modes are the card's** (DRAW's MODES): the Screen list has "none" and the modes the card says it
  has, 640 by 480 and up (what the desktop needs), in place of twelve usual ones whatever the card; those only
  when the card doesn't answer. The file's own mode is in the list whatever it is, as before.
- **The Network page says how the network is now** (`apps/control/status.c`, NET's STATE, once a second): the
  card and its hardware address (or the cable link), the address and mask with what is left of DHCP's lease, the
  gateway and the name servers, GLOS's name, the frames in, out and bad. The scripts' shot of the page masks the
  lines, which are the machine's.
- **The Joystick page shows the pads as they are** (PADS, ten times a second): for each of the two joysticks
  a DOS program sees, the stick's place in a square (the right stick's too, if the pad has one) and its first
  ten buttons, lit while down; "no pad" for one with none. A pad's test before a game is started (seen on Loop
  A with an injected pad; the scripts' machines have none).
- **The Input page has places to try the keys and the mouse:** a field for the keys' repeat, and a place that
  counts the double clicks made on it (by the double-click time the Control Panel started with: that setting is
  an app's at its start).
- **The Sound page has Test sound:** two notes through the mixer at the volume that is set, and the page says
  so, or that there is no card GLOS can play through (seen on Loop A's SB16: its stream in `glos audio` while
  it plays, closed after).
- **The USB page lists the controllers and what is plugged in**, as `glos usb` says it (SYS's STAT, its text),
  read every two seconds; masked in the scripts' shot.
- **The Date and Time page has the clock:** local time now and how it was last set (the PC's own clock, the
  network, by hand), read every second; a date field and a time field with **Set the clock**, and **Ask the
  server now**: both the shell's CLOCK (`glos time set`, `glos time sync`), run for the page. Seen on Loop A:
  set by the button, `glos time` says by hand. Masked in the scripts' shot.
- **The Services page lists its programs:** `[services] run`'s command lines one a row, read again whenever
  the line changes; **Add...** chooses a program with the file dialog and puts it at the line's end, **Remove**
  takes the chosen row out. The line stays the setting, and its label's letter is O now (P is Apply's: on that
  page Alt+P went to the field).
- **The Display page says which card it is** (SYS's STAT): the card, its memory, whether its engine draws the
  desktop, and the mode now.
- **The time zone is chosen by place:** a list of 28 places on the Date and Time page; one chosen writes its
  POSIX string in the Time zone setting's field (the setting stays that string), and the list shows the place
  whose string the field has. Every string of the list is one `tz_parse` takes.
- **Startup...** (beside the Desktop page): the programs started with the desktop (`[desktop] start`) as a
  list in a dialog, with Add... (the file dialog; a program beside GLOS's own is written by its name alone, as
  the setting has them) and Remove; OK writes the list back in the setting's field. A path with a space can't
  be listed (the setting's programs have spaces between them).
- **Not done yet:** the other grown pages (the sound devices and each stream's volume (the Mixer's, phase F), the pads' mapping, USB's devices as a tree with their
  details, Startup and default apps, Secrets);
  the Screensaver page's preview (D6); a preview of a theme not yet applied; icons in the list.
- **Tests:** host and Loop A `control-find` (a setting found, the next, a word nothing has, the look following
  the field and the style) and `control-services` (two programs written, one removed, Add...'s dialog);
  `control` and the control suite as before, by the list.

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

### D6 in detail (2026-11-18, before any of it is written)

What there is to build on, read from both repositories:

- **DOS-GL in a window:** `dglInit` under GLOS opens a window of the context's size and shows it at once, framed
  and with the focus (RetroGPU `hal/src/rg/mga_glos.c`); `dglGetGlosWindow()` gives its GLOS handle. Only a
  Matrox card at 16 bits has it (`GLOS_MACHINE_GLWIN`). There is no frameless, full-screen, topmost or hidden
  start, no on-screen software renderer (DOS-GL's step B3), and DOS-GL reads no GLOS events: its program must.
- **GLOS:** a window can be full-screen (`GLOS_WS_FULL`) and topmost (`GLOS_PROP_WIN_TOPMOST`); the blank saver
  paints the screen black and stops composing while it is on (`kernel/wm/desk.c`), and the input that ends it is
  used up (`deskin.c`); `[saver] saver` is in the schema ("blank") and read by nobody; the toolkit's
  `gui_preview` embeds another program's window in a widget and resizes it to the widget.

The steps:

1. **The kernel runs a saver's program** (`kernel/wm/saver.c`). `[saver] saver` names it: `blank` (the built-in
   one) or a saver, whose program is `NAME.SCR` beside GLOS.EXE (an ordinary GLOS app's EXE by another
   extension, so that the Control Panel can list what is there). When the saver comes on with one named, the
   desktop goes on composing and the program is started as a job of the kernel's (as a service is); the pointer
   is hidden. Once the program has a full-screen window up, that is the saver. If its program isn't there, ends,
   or has no such window ten seconds on, the blank saver takes over: the screen is never left as it was. The
   first input ends the saver as today (and is used up): the program is ended by the kernel. The screen's power
   going off ends the program too (nothing sees its pictures).
2. **A saver's program:** with no arguments, a DOS-GL context of the screen's size, its window made full-screen
   and topmost, frames until it is ended (it reads its events: the end, its window's close). With `/P W H`, a
   context W by H and nothing else: the Control Panel's preview embeds that window.
3. **The savers** (`apps/savers`, built against RetroGPU's DOS-GL: `-I$(DOSGL_DIR)/include`, `libGL.a`): first
   the starfield; then pipes, the maze, flying objects, 3D text.
4. **The Control Panel's Saver page:** the savers there are, the preview (`gui_preview`), the waits.
5. **Asked of DOS-GL** (its session, 2026-11-18): a hidden start for the window, so that a saver is full-screen
   from its first frame and a preview doesn't flash up framed; and B3, the on-screen software renderer through
   `GLOS_DRAW_BLIT`, without which a saver draws only on a Matrox card at 16 bits. Until then the blank saver is
   what every other card and depth gets, by step 1's rule.

### D6 as built, first part (2026-11-18)

- **The kernel runs a saver's program** (`kernel/wm/saver.c`, asked each turn by the desktop's thread while the
  saver is on): `[saver] saver` is `blank` or a saver's name, whose program is `NAME.SCR` beside GLOS.EXE,
  started as a job of the kernel's own and made an app. The desktop goes on composing with the pointer away;
  once the program has a full-screen window up, that is the saver (`GLOS-IDLE saver program=NAME shown`). The
  blank saver takes over if the program isn't there (code 126), ends (its code says why), has no full-screen
  window ten seconds after its start, or the screen's power goes off. The saver's end (the first input, used
  up as ever, or `glos idle wake`) ends the program.
- **`.SCR` is a program to the agent** (`kernel/dos/agent.c`): run by EXEC as a `.COM` or `.EXE` is, not given
  to COMMAND.COM, whose child could not be an app.
- **STARS.SCR** (`apps/savers/stars.c`): a flight through 220 stars, each a small square larger and brighter
  the nearer it is, moved by the clock so that its speed is the same at any frame rate. It asks GLOS first
  whether DOS-GL has a window here (`GLOS_MACHINE_GLWIN`: a Matrox card at 16 bits) and ends with code 3 if not,
  since DOS-GL would otherwise take the screen as a DOS program does; then a context of the screen's size, its
  window made topmost and full-screen. `/P W H` is its preview: a context that size in an ordinary window.
  It uses DOS-GL's hidden start when the library has it (asked of DOS-GL: until then its window is framed for
  a moment before it is full-screen).
- **Built** against RetroGPU's DOS-GL as built there (`$(DOSGL_DIR)/build/lib/libGL.a`, no copy): `make apps`
  makes `build/apps/STARS.SCR` (680 KB, most of it the library).
- **Seen on Loop A** (bf6, G200, 640 by 480 at 16 bits): the program's window up 115 ms after the saver came
  on; about 1500 lit pixels on black, another picture a moment later; a key ends it and the desktop above the
  bar is pixel for pixel as it was.
- **The Control Panel's Saver page shows the saver chosen, running small** (`apps/control/status.c`, the
  toolkit's `gui_preview`): the saver's own program started with `/P 192 144`, its window taken into the page.
  Only while the Saver page is the one shown, and nothing for the blank saver; another saver chosen ends the
  one before. The picture's place is held at the saver's own size, since a DOS-GL window can't follow a
  resize. Where the program can't draw, or isn't there, the place says "Ended". (The program's path is written
  as DOS writes one: DJGPP's `argv[0]` has `/` for `\`, and the agent takes a `/` as where a command's
  switches begin.)
- **PIPES.SCR** (`apps/savers/pipes.c`): pipes that grow a cell at a time through a nine-cell box, with a joint
  where they turn, each in its colour, its sides lit by which way they face, with a depth buffer; seven pipes to
  a picture, which is looked at for a moment and begun again; the box turns slowly. What the two savers do the
  same (the start, the frames' time, GLOS's word to end) is `apps/savers/saver.h`.
- **TEXT.SCR** (`apps/savers/text.c`): a word (GLOS) in solid letters made of cubes, five by seven from a small
  table of capitals and digits; it swings from side to side about its upright, never so far that it can't be
  read, tips a little, drifts across the screen and back, and its colour goes slowly round. The lit box the
  pipes and the letters are made of is `saver_box` in `saver.h`.
- **`[saver] text`** is the word TEXT.SCR shows (twelve characters at most; GLOS unless set), read when the
  saver starts: the Control Panel's preview shows the word that is applied.
- **OBJECTS.SCR** (`apps/savers/objects.c`): fourteen blocks (cubes, slabs, bars) in seven colours come from
  far off towards the viewer, each turning about an axis of its own and drifting a little; one that has gone by
  starts again far off.
- **`[saver] speed`** (slow, normal, fast): the savers' clock at half, its own or twice the speed, for all of
  them (`saver_ms` in `saver.h`), read when a saver starts.
- **MAZE.SCR** (`apps/savers/maze.c`): a maze of ten cells by ten, carved from a corner so that every cell is
  reached; the viewer walks it with a hand on the right-hand wall, a cell at a time, turning where the wall
  turns and back out of the dead ends; after 420 cells the walls change colour and another maze is made. (Its
  near plane is 0.2 of a cell: at 0.07 the 16-bit depth buffer could not tell the walls apart, and the screen
  was one wall's colour.)
- **`[saver] detail`** (less, normal, more; the Control Panel's Detail): how much the savers draw: 110, 220 or
  440 stars; 4, 7 or 10 pipes to a picture; 8, 14 or 26 blocks; a maze of 7, 10 or 14 cells a side (`saver_amount`
  in saver.h). Read as the saver starts, like its speed.
- **Not done yet:** looks of a saver's own beyond that (the pipes' joints); STARS.SCR in the packages, the floppy and the
  benches' update; the software renderer's window (DOS-GL's B3), without which every other card and depth has
  the blank saver.
- **Tests:** Loop A `desk` suite's saver case: the G200 (the program shown, moving, ended by a key, the
  desktop as it was; in the Control Panel its window bare, 192 by 144, stars in it, ended when another page is
  shown), the S3 (code 3, the blank saver, black; no preview), and the program deleted (code 126, blank).

### D6 on every card (2026-11-18)

- **DOS-GL 1.7** (RetroGPU main 1c9f73b; its session's work, asked for here) has a window on any desktop: a
  Matrox card's engine draws it at 16 bits, and its software renderer everywhere else (another card, or an 8- or
  32-bit desktop), shown through `GLOS_DRAW_BLIT`. It also has `dglGlosHints`: the window made hidden and bare.
- **The savers use both** (`apps/savers/saver.h`): built against 1.7 or later they no longer ask GLOS whether
  DOS-GL has a window here, and their window is hidden and bare until it is shown full-screen (no framed moment
  first). Built against an older DOS-GL they are as before.
- **Seen on Loop A** (bf6): stars, pipes and text shown 125 to 177 ms after the saver came on, on the G200 at 16
  bits (the engine), the G200 at 32 bits and the S3 at 8, 16 and 32 bits (software); the Control Panel's preview
  embedded on each. The software renderer draws fewer frames a second than the engine (not yet measured).
- **Seen on bench-g400** (the IBM 300PL's G400, 640 by 480 at 16 bits, GLOS 82efd1c, 2026-11-18): the three
  savers, each asked for and ended over SSH: the pipes' window up 184 ms after the saver came on, the text's
  200 ms; the pictures right (two pipes grown in seven seconds); the bench as it was afterwards.
- **The blank saver** is still what shows where a saver's program isn't there or ends: the kernel's rule.
- **Tests:** Loop A's saver case expects the savers on the S3 as on the G200.

### D7. Background apps (`[services]`)

- A list of apps GLOS starts with itself (with or without a desktop), starts again if they end (with a growing
  pause if they keep ending), and lists, stops and starts by the shell's `services` command and the task
  manager. Supervision only: they wait while a DOS program has the whole PC, as every app does.
- **Tests:** Loop A `console` or `apps` (a service started at boot, killed and restarted, stopped by command).

### D7 as built (2026-11-17)

- **`[services] run`**: programs' command lines with `;` between them (190 characters in all, eight services).
  `kernel/core/services.c` is their supervisor, a kernel thread in agent mode: each is a job of the agent's,
  made an app without asking and owned by the supervisor, which is told when it moves on. Started as soon as
  GLOS.CFG is read, with a desktop or with none. A service's name is its program's file name (`CHATD.EXE`); one
  written with no folder is beside GLOS.EXE, as `[desktop] start`'s are. What a service writes is thrown away.
- **Started again when it ends:** after two seconds, then four, and so on up to five minutes while it keeps
  ending within a minute of its start; one that ran a minute or more starts again after two seconds. A program
  that isn't there ends at once with code 126 and is tried again by the same rule.
- **Only apps:** a service is a GLOS app, or a 32-bit DPMI program GLOS can make one. Anything else would have
  DOS to itself with the apps paused for as long as it liked: a service that is no app twenty seconds after its
  start is ended and left "refused" until `services start NAME` or another command line for it.
- **`glos services`** (the shell's `SERVICES`): a line each, `service NAME running|waiting|stopped|refused
  seq=N restarts=N code=N "command line"`. **`glos services stop NAME`** ends one and leaves it ended (until
  GLOS's next start); **`glos services start NAME`** starts it again. A change to the setting takes effect at
  once: what it lists no more is ended, what is new is started.
- **Found on the way:** `glos run --app` of a program that is marked as an app and asks to be one itself raced:
  made one first, its own init was refused ("already") and it ended with an error; a restarted service ended so
  every time. A marked program is no longer made an app before it asks (`kernel/vm/session.c`); the apps
  suite's `run-app` case, which had the refusal as its expectation, has the program's own exit code now.
- **GLOS leaving:** nothing is started while GLOS has been asked to leave (the exit ends the apps, and a
  service started again would hold it up).
- **Not done here:** the task manager's list of them (D8); a page of its own in the Control Panel (D5: the
  generic Services page has the one setting); per-service settings (a folder to start in, a user to tell).
- **Tests:** Loop A `apps`: started by the setting, started again after a kill (2 s, then 4 s), stopped and
  started by hand, ended when unset, a real-mode program refused with DOS and the apps as they were after it,
  one named with no folder, `glos exit` with one running, and one started with nobody asking at GLOS's next
  start.

### D8. The task manager (Monitor grown)

- `apps/task-manager.md`: programs, apps and their windows, sessions, services, memory, the network, the sound
  streams; end a task; the processor over time. SYS's STAT gains the structures it needs (NET, AUDIO, USB), so
  that Monitor reads numbers and not text.
- **Tests:** `monitor` script (masked figures), Loop A native, APITEST's SYS cases for the new structures.

### D8 as built, first part (2026-11-18)

- **Monitor's Services page:** the background apps (`[services] run`, D7), each one's state (Running, Starting
  again, Stopped, Refused: not an app), how often it was started again and its command line, read every second
  while the page is shown; **Stop** and **Start** for the one chosen, which run the shell's `SERVICES STOP NAME`
  and `SERVICES START NAME` for it. With none, the page says where they are set.
- **SYS's STAT has `GLOS_STAT_SERVICES`** (22): `glos services`' lines as text, as its other text kinds are.
- **`glos services stop|start`** takes its words in capitals too (the shell's command is typed as DOS's are;
  found by Monitor's Stop).
- **Switch To** (Programs page): the chosen program's window in front, with the keyboard (WIN's FOCUS by a
  window's number, which the API keeps for the shell and the task manager; libglos has `glos_focus_id`).
- **Not done yet:** Monitor as the plan's task manager: each app's windows listed, sessions, the network's and
  the sound's streams, the processor's time by program over time; SYS's STAT's structures for NET, AUDIO and USB
  (so that Monitor reads numbers and not text).
- **Tests:** host and Loop A `monitor` (the fifth page, none there); by hand on Loop A with a service set: its
  row, Stop (stopped), Start (running again); the apps suite's `glos services START NAME`.

### D2's leftover: a scheduled run that failed (2026-11-18)

- The schedule's runner (`kernel/core/schedule.c`) remembers the runs it starts by their job numbers; the agent
  tells of every job's end (`agent_on_done`), with its exit code, or 126 when its program was found nowhere
  (COMMAND.COM was left to say so, and its own code is 0). A run that ends with anything but 0 is a line in the
  log (`GLOS-SCHED failed name=NAME code=N`) and one of GLOS's notifications: "NAME" couldn't be run: its
  program wasn't found, or "NAME" ended with an error (code N). The notification waits for a desktop on the
  screen (a run at GLOS's start ends before the desktop is up).
- **Tests:** by hand on Loop A (a schedule entry whose program isn't there: the log's line, the notification
  18 s after the desktop came on); the api suite's schedule cases and the apps suite as before.

### D2's other leftover: a disk nearly full (2026-11-18)

- In the shell, which is there whenever the desktop is (`apps/files/diskwarn.c`), not the kernel: every half
  minute FS's DRIVES gives each hard disk's size and what is free of it (no floppy or CD is made to spin), and
  one with under a twentieth free is said with one of the shell's notifications ("Drive C: has 12 MB free of
  490 MB."): once, and again only after it has had twice that free. `GLOSDISKWARN` in the environment is another
  percentage (0: never said).
- **Tests:** by hand on Loop A with `GLOSDISKWARN=100` (the notification 35 s after the shell's start, the
  shell's `FILES disk-low C` line); the shell's scripts as before (their disk has room: nothing is said).

### D3's "Recently added", and D1's windows' own icons (2026-11-18)

- **"Recently added":** START.CFG keeps when each entry was first seen (`seen TIME NAME`, seconds since 1970;
  1: there from the first). The first START.CFG (written when the menu first comes up) takes everything there is
  for old; an entry met later is stamped with the time, and is listed first in the menu's list, under "Recently
  added" (three at most, the newest first), for a fortnight or until it is started. A START.CFG of before the
  stamps takes what it finds for old, as a first one does.
- **A window's own icons:** `gui_window_icon(win, "app-calc")` gives a window one of ICONS.DAT's colour icons:
  its 16 pixels as GLOS_PROP_WIN_ICON (the task bar's button, which had only the one-colour window sign: no app
  had set one), its 32 as the new **GLOS_PROP_WIN_ICON_BIG** (4096 bytes, kept with the window in memory of its
  own; a property's bytes over the usual 1039 go through the kernel's memory, not the app's kernel stack), which
  Alt+Tab's strip draws instead of the 16 twice the size. GLOS's own apps set theirs (Files' browser and its
  folders, the Terminal, Notepad, the Calculator, Images, the Monitor, the Control Panel, Packages).
- **Monitor's Windows page** (D8): the apps' ordinary windows that are shown or minimised, front to back, each
  its program, how it is shown (normal, minimised, maximised, full screen, a half) and its client area's size;
  **Go To** puts the chosen one in front with the keyboard (`glos_focus_id`).
- **The processor by program** (D8): the Programs page's Use column: each app's share of the processor since
  the last look (a second), from PROGRAMS' busy time; the time it has had in all stays beside it.
- **Tests:** Loop A `start-recent` (a program given a place in the menu while the shell runs: the menu's
  `FILES start recent` line); `api` (win-icon-big: set, read back, the wrong size refused, taken away);
  `monitor` (the page's shot, its rows masked) and Loop A `monitor-windows` (Notepad chosen by its title, Go To,
  Alt+F4 closes Notepad).
- **A race mended on the way** (kernel/core/schedule.c): a schedule entry "at GLOS's start" that an app made
  before the runner's first look (the runner waits for GLOS.CFG and GLOS.DAT at a low priority: a second or so,
  more on a loaded machine) ran at once, as if it had been there at the start. The API now tells the runner which
  names are new (`schedule_fresh`), and the first look leaves them for the next start. Seen as `apisched-bf6`
  failing (written, start, once) on a loaded host.
- **On silicon (2026-11-18):** bench-g400, whose card is a G100 since today, runs this desktop at 640x480x16 with
  acceleration; every DOS-GL saver ends there at once (a divide by zero in the HAL's `write_origin`, the same on
  Loop A's G100) and the blank one takes over, as it is meant to. With that mended the real card still drew
  nothing right: it has 4 MB, and the HAL's memory probe measured 8 (memory that isn't there read back what was
  just written), so the window's buffers lay where there is no memory. Both are mended on RetroGPU's branch
  g100win (the DOS-GL session's; not merged yet): a GLOS built against it says `vram=4096K` on that card and
  OBJECTS.SCR draws as it should there, in the hardware's window.

### D5's Secrets page (2026-11-18; API 2.4)

- **The page** is the Control Panel's last, and the one that is not a section of GLOS.CFG (`CONTROL secrets`
  opens it): what the store is for, how it is now (open, with or without a master password; locked; no store
  here; its file unreadable), and two buttons: **Unlock...** for a locked store, and **Set a master password...**
  (or **Master password...**, where it has one) to set, change or take away the master password.
- **No password passes through the program.** Both buttons call SECRETS' new **STORE** (`glos_secrets_store`:
  GLOS_STORE_STATE, _UNLOCK, _PASSWORD), and the kernel asks the user in the desktop's own dialog
  (kernel/wm/deskpass.c), as it already did when an app needed a locked store. The change is asked there a
  question at a time: the password it has now (where it has one), the new one (none: just Enter), the new one
  once more; a wrong one or two that differ are asked again, Esc leaves everything as it was. PASSWORD is for
  GLOS's own programs alone (another's is refused), and a locked store is opened first.
- **The change's arithmetic is done on a thread of the kernel's own** (deskpass.c `change`), the caller waiting
  for it ("One moment..." in the dialog). An app's call is a trap path, with interrupts off: the tick stands
  still in it, and secrets.c chooses the key's rounds by timing this PC against the tick. Called straight from
  the dialog it took the most rounds there are (four million: minutes, with the whole PC stopped; Loop A called
  it a hang).
- **Two older faults seen on the way, mended:** an app that waits in a call for GLOS's own dialog was said to be
  "not answering" after five seconds (its events wait with it): it isn't now (`app_asking`), and its five
  seconds begin again when it is back. And that notification's words were ".60s isn't answering": ksnprintf
  has no "%.60s".
- **API 2.4:** the STORE call, and a window's GLOS_PROP_WIN_ICON_BIG (above). `ver` in the shell says so.
- **Tests:** `api` (the functions' list; `apisec`: the store open without a password, another folder's program
  refused the change, the store locked after a restart with one); Loop A `control-secrets` (a master password
  set in the dialog, typed twice, then taken away with the old one: the page's lines say each state); the
  page's shot in `control`; the control suite (its walk counts the page).
- **Not done:** the shared secrets' grants (which program may use which name) as a list on the page; today
  `glos secrets allow|deny` over SSH alone.

## Where phase D stands (2026-11-18)

| Step | Done | Left |
|---|---|---|
| **D1** the task bar | Window and group menus, grouping, Alt+Tab's strip in the order of use, the calendar, the volume's slider, the bar at the top or hidden | (nothing: a group's windows are one app's, with one icon, so the group menu stays text) |
| **D2** the tray | GLOS's own icons (volume, network, pads, a remote session), its notifications (a pad, the network, an app not answering, a scheduled run that failed, a disk nearly full: the shell's), the overflow, the held count | (nothing) |
| **D3** the Start menu | Apps, pins and tiles, the most used, search (apps, settings, files by name), Power, tiles dragged | Tiles' groups and sizes |
| **D4** the wallpaper | A picture in four styles, drawn by the shell | (its chooser and preview are D5's, done) |
| **D5** the Control Panel | The page list and search; the desktop's look and Choose...; the card and its own modes; the network, USB and pads as they are; places to try keys, mouse and sound; the clock set by hand or by the server, the time zone by place; the services' list and the startup programs'; the Secrets page (the store's state, Unlock, the master password in GLOS's own dialog) | The pads' mapping; USB as a tree; the shared secrets' grants as a list; icons in the list |
| **D6** savers | The kernel runs a saver's program and falls back to the blank one; STARS, PIPES, TEXT, OBJECTS and MAZE (the five of the plan), on any card and at 8, 16 and 32 bits (DOS-GL 1.7); the Control Panel's preview; the text saver's word, the savers' speed and detail | (the pipes' joints and such: not planned); their speed measured where the software renderer draws them |
| **D7** background apps | `[services] run`, started again when they end, `glos services`, refused if no app | A folder or a user for each |
| **D8** the task manager | Monitor's Services page with Stop and Start; Switch To; the Windows page with Go To; each app's share of the processor | Sessions and streams; SYS's structures for NET, AUDIO and USB |

### What is left, and how each is meant to be done (2026-11-18)

- **Tiles' groups and sizes** (D3): the pins' lines of START.CFG gain a group's name and a size; the tiles' area
  lays out by group. Not begun.
- **The pads' mapping** (D5): `[joystick] map` is in the gamepad plan but not in the kernel (vjoy.c reads port
  and range alone): the kernel first, then a page with a row a button.
- **USB as a tree** (D5): SYS's STAT needs USB as structures (a controller, a hub, a device, its parent), which
  D8 wants too; the page then draws a tree with each device's details.
- **The task manager's other views** (D8): the sessions, the sound's streams and the network's connections, from
  structures of SYS's STAT (NET, AUDIO, USB) rather than the commands' text. (The apps' windows: done, below.)

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
