# The shell: plan (GLOS v1, phase A)

**Today:**
- **The desktop:** Files is the shell (D33). The desktop has icons; folders open in windows.
- **The task bar:** the launcher's mark (a menu of apps), the time, and a note in a remote session (D35).
- **No:** window buttons, notification area, notifications or screensavers.

**In v1:** a shell after Windows 10 (the owner's choice over 11):
- a task bar with the windows and a notification area;
- a Start menu with search and tiles;
- notifications;
- screensavers on DOS-GL;
- themes and wallpapers.

The task bar and the desktop are the kernel's (`kernel/wm/desk.c`). The Start menu and the desktop's icons are Files'
(the shell app). The split stays: the kernel draws what must never wait for an app; Files draws what is an app's
business.

## The task bar

```
+--------------------------------------------------------------------------------------+
| [G] | [Files: C:\GAMES] [Notepad - GLOS.CFG] [Paint]         | ^ (()) [net] [pad]  14:32 |
+--------------------------------------------------------------------------------------+
```

- **The Start button:** the GLOS mark, at the left. A click, the Windows key or Ctrl+Esc opens the Start menu.
- **Window buttons:**
  - one for each window that asks for one (WIN), with its icon and title;
  - clicking restores, focuses or minimises it; a right click gives its menu (restore, minimise, maximise, close);
  - grouped by app when there are many (a setting), with a menu of the group's windows.
- **Alt+Tab:** a strip of the windows with their icons, Alt+Tab and Alt+Shift+Tab to move, release to switch.
- **The notification area:**
  - icons put there by GLOS and by apps (SHELL), each with a tooltip, a click, a double-click and a menu;
  - icons beyond a few go into an overflow (the ^);
  - GLOS's own:
    - **Volume:** a click gives a slider and mute; a double-click opens the Mixer.
    - **Network:** the link and address in its tooltip; a click gives status, a double-click opens the Network
      page.
    - **USB devices and pads:** what is plugged in; a click lists them; a pad's icon shows which joystick it is.
    - **A remote session:** today's note, as an icon with its peer in the tooltip.
- **The clock:** time to the minute; the date in its tooltip; a click gives a month's calendar; a double-click opens
  "Date, time and region".
- **Position and size:** the task bar at the bottom (top as a setting); auto-hide (a setting).

## The Start menu

```
+------------------------------+----------------------------------+
| [search: type to find      ] |  Pinned                          |
|                              |  [Files] [Notepad] [Paint] [Term]|
| Recently added               |  [Games] [Music]   [Web]  [Chat] |
|   MIDI Studio                |                                  |
| Most used                    |  Games                           |
|   Notepad, Files, Term       |  [DOOM] [Quake] [Fifth Wheel]    |
| A                            |                                  |
|   Audio Player               |                                  |
| C                            |                                  |
|   Calculator, Chat, ...      |                                  |
| [Settings] [Power]           |                                  |
+------------------------------+----------------------------------+
```

- **The left column:** recently added and most used apps, then every app A to Z (letters jump), from the Start
  menu's entries (SHELL): the apps beside GLOS.EXE, installed packages' apps, programs with profiles that ask to be
  listed.
- **Search as you type:**
  - apps, Control Panel pages (by their labels and the schema's descriptions), recent documents, files in the user's
    folders (a quick index of names, built in the background);
  - Enter opens the first.
- **Tiles on the right:**
  - pinned apps and programs, in groups the user names, dragged to reorder;
  - small or medium tiles;
  - live tiles not planned (an app's tile could show a count or a picture later).
- **Jump lists:** an app's recent documents (Notepad's files, Term's connections) by a right click on its entry or
  tile.
- **At the bottom:** Settings (the Control Panel), Power (exit GLOS, restart GLOS, reboot), the user's folders.
- **The keyboard:** the arrows, type to search, Enter, Esc (D7).

## Notifications

- **The pop-up:** a small window above the notification area, with an icon, a title, a line or two, and an action
  or two (buttons). Shown for a few seconds unless hovered, and queued if several come.
- **Who sends them:** apps (SHELL) and GLOS itself (a pad plugged in, the network up or down, a disk full, a
  download finished in Fetch or PKG, an app not answering).
- **Never over a full-screen program:** in an exclusive session they wait, and the task bar's icon shows a count.
  A "do not disturb" setting holds them.

## Screensavers

- **When:** after the Control Panel's wait without input (IDLE). Never while a full-screen program or exclusive
  session runs, or while an app asks the screen to stay on (a video, a game).
- **What:** savers on DOS-GL, after Windows 98's OpenGL ones:
  - **Starfield:** 3D stars flying past, speed and density.
  - **Pipes:** 3D pipes growing and turning, with joints of balls or elbows, and textured pipes.
  - **3D maze:** a textured maze walked through, with the rat and the OpenGL-logo flips of the original's style (our
    own art).
  - **Flying objects:** a waving flag or a ribbon, and GLOS's mark in 3D.
  - **Text:** a 3D line of text that spins.
  - **And:** a blank screen, and the screen's power off after a further wait (DPMS through VBE where the card has it).
- **How:**
  - each saver is a DOS-GL program run full-screen as an exclusive session, or drawn into the desktop's own surface
    (settled in B: whichever leaves the desktop's state safest);
  - a Matrox card draws them in hardware, any other card through DOS-GL's software renderer (DOS-GL
    `docs/backends.md`);
  - the first key or mouse movement ends it, and the desktop is as it was.
- **The preview:** the Control Panel's small screen, the saver drawn into a child window.

## Themes, wallpapers, the look

- **Themes:**
  - colours and metrics, as settings (SETTINGS);
  - the toolkit and the window manager redraw everything when the theme changes;
  - the built-in ones in the Control Panel's Appearance page.
- **The wallpaper:** a picture (Paint's decoders, scaled once when set), a colour, or a pattern; on 8-bit desktops
  dithered to the palette.
- **Window decorations** (the kernel's, `kernel/wm`):
  - a title bar with the window's icon, its title, and minimise, maximise/restore and close;
  - borders to resize from every edge and corner, with the cursor showing which;
  - a double-click on the title maximises or restores;
  - Aero-style snapping to half the screen (a setting).

## From toolkit 2

- **The Start menu (Files'):** a list grouped by letter, tiles that drag, a search field with results by kind.
- **Notifications:** the notification's look (drawn by the kernel? or by Files as an app? Settled in `api-2.md`: the
  kernel draws notifications, so they show even when Files is busy).
- **The Control Panel's pages:** above.

## From API 2.0

- **WIN:**
  - states and decorations: minimise, maximise, restore, full screen;
  - resizing from every edge; icons; snapping;
  - "not answering" (a window whose app hasn't taken an event for 5 seconds).
- **SHELL:**
  - the task bar's window list (which windows have buttons, their groups);
  - the notification area (an app's icon, its tooltip, its events);
  - notifications (send, act on);
  - the Start menu's entries, its pins and the jump lists;
  - recent documents; the wallpaper; Alt+Tab's strip.
- **IDLE:** idle time, the saver's start and end, "keep the screen on", the screen's power.
- **AUDIO:** the master volume and mute (the volume icon).
- **NET:** the network's state (the network icon).
- **SYS:** USB devices and the pads (the USB icon).
- **SETTINGS:** themes, the clock's formats, and their change event.
- **DRAW:** DOS-GL full screen for savers, and the preview's child window.

## CPU floor

- **A 486DX2:** the task bar, the Start menu (its search index built in the background) and notifications.
- **The savers:** on a Matrox card from a 486 up. Through the software renderer, at a modest size on a 486, at
  640x480 on a Pentium II.

## Tests

- **Host:** GUI scripts of the Start menu (search, tiles, jump lists), the task bar's buttons and notification area,
  Alt+Tab, a notification's pop-up; each saver's first frames against references (the software renderer on the host,
  as DOS-GL's tier 1).
- **Loop A smoke:** minimise, maximise and restore a window from its buttons and the task bar; the volume icon's
  slider changes the mixer's master; a saver starts after the wait and ends on a key; a notification while an
  exclusive session runs waits.
- **The P4 and bench-g400:** the savers on the G450 and the G400 in hardware and in software, timed.
