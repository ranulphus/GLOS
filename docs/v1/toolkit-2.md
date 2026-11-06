# Toolkit 2: design (GLOS v1, phase A)

**Status (2026-11-06): design v0, for the owner's review with the rest of phase A.**

Toolkit 2 is GLOS's toolkit (`sdk/gui`, `include/glos/gui.h`, `docs/gui.md`) grown for v1's apps, on API 2.0
(`api-2.md`). It is designed from:
- every app plan's "From toolkit 2";
- the shell's (`shell.md`);
- the IDE's (`ide.md`).

It is built in phase C, after API 2.0 (phase B). The apps move to it in phases E and F.

## What stays

- **Retained widgets:**
  - a window holds a tree of widgets, each an object of a class (draw, event, measure, layout, destroy) that keeps
    its own state;
  - damage, then drawing only what changed, into a back buffer, then PRESENT.
- **Layout by measuring:** boxes, forms, groups, tabs, the scrolling area. Nothing is placed by fixed pixels, so a
  bigger font or a translation lays out again by itself.
- **The keyboard first (D34):**
  - Tab's order; "&" for Alt and a letter; F10 for the menu bar; Enter and Esc for the default and cancel buttons.
  - Every new widget below is usable from the keyboard: the toolbar's commands through their menus and shortcuts;
    splitters and docking through the View menu; the canvas's tools by keys.
- **640x480 (D34):** every window and dialog fits it, docked panes and toolbars included (a narrow window folds
  the toolbar into its overflow and the panes into tabs).
- **Text:** UTF-8; DOS's code pages through `gui_dos_text` and `gui_text_dos`; smoothed text as an option (M10 step G, `docs/gui.md` §1.10).
- **The host shim and its scripts** (`docs/gui.md` §2): every widget and app built for the host and driven by
  scripts, with goldens at 8, 16 and 32 bits, and the same scripts played in Loop A.
- **A static library linked into each app** (until the MODULES question says otherwise).

## What changes in the shape

### 1. Commands (new)

- **A command** is an id with:
  - its text ("&Save"), its icon (16x16), its shortcut ("Ctrl+S"), its tooltip;
  - whether it can be used now, and whether it is checked (or which of a group is chosen).
- **Menus, toolbars and context menus show commands,** so a command is set once and every place follows: Save
  greyed in the menu and on the toolbar together (as Delphi's actions).
- **The app's handler** (the window's `on_command`, as today) is told the id, wherever it came from.
- **An update hook** asks the app, before a menu opens and when the app is idle, which commands can be used now.
  Apps don't have to keep every control's state by hand.

### 2. Models for lists and trees (new: the virtual list)

- **Today** `gui_list` keeps every row's text. A folder of 2,000 files is 2,000 rows made before anything shows.
- **Toolkit 2's list asks the app for what it shows:**
  - the number of rows, which may grow while the list shows ("more are coming");
  - a row's cells, icon and state, only for the rows on screen.
- **Files' folders load as they go:** FS's LIST in a background task adds rows as they arrive, and the list shows
  the first screen at once.
- **The same model drives:**
  - the detail view: columns sorted, resized and reordered;
  - the icon view: small, large, and big pictures for the Games Library and thumbnails;
  - the tree (nodes opened on demand, as today's `GUI_ON_CHANGE` opening).
- **Sorting:** the app's, since only the app has all the data. The list says which column and which way.
- **Selection:** by row ids the app gives, so it survives sorting and rows arriving. Rubber-band selection in the
  icon view.
- **The old `gui_list` stays** as a simple list on top of the model, for short lists (Calculator's history).

### 3. Background work (new, on API 2.0's THREAD)

- **A task:** `gui_task` runs a function on a thread of the app's, with:
  - progress reported (a fraction and a line of text);
  - results posted back to the window's thread as they come (rows for a list, a thumbnail, a finished page);
  - cancellation asked and checked for.
- **One thread for the widgets:** only the window's thread touches widgets. Tasks give it results through events
  (API 2.0's POST).
- **A pool of threads,** a few by default (one on a 486, set by the app), so many small jobs (thumbnails) don't
  each make a thread.
- **The progress dialog:**
  - shown by a task that runs longer than a second;
  - a bar, the line of text, a Cancel button, and optionally a log pane (Disk Tools' bad sectors).
  - It is the "copying" dialog of Files and Notepad's find in files.

### 4. Undo (new)

- **An undo history:** each change an object with "undo" and "redo", grouped (one typing burst is one step),
  limited by memory.
- **The Edit menu's commands** (Undo, Redo, with the step's name: "Undo Fill") come from it.
- **A history list widget** shows it (Paint's History panel).
- **Users:** Paint, the Sound Recorder, MIDI Studio, the editor component (its own, made on the same framework).

### 5. Themes and metrics (new: today's colours are constants)

- **Colours by role** (face, field, line, dim, shade, dark, mark; text on each; chosen; links; the editor's
  syntax roles), read from SETTINGS' THEME at start and again when the theme changes.

  Every widget draws by role, never by a constant. `GUI_FACE` and the others become the default (Slate) theme's
  values.
- **Metrics:** the UI font's size, a scroll bar's width, the toolbar's icon size; also from the theme.
- **On a theme change:** everything is measured again and redrawn (the window manager redraws its decorations at
  the same time).
- **8-bit desktops:** a theme's colours must be in the palette's cube there, as D7 has them today. A theme with
  others is shown with the nearest; the Control Panel warns.
- **The built-in themes** (shell.md): Slate, and others the Appearance page offers (a dark one; a high-contrast
  one).

### 6. Fonts (grown)

- **The UI's fonts as today:**
  - bitmaps made at build time from open fonts (DejaVu, `docs/gui.md` §1.10);
  - now in several sizes (small, normal, large: the theme's UI size), each with its smoothed version.
- **The font dialog:** the fonts there are and their sizes, for:
  - Term's and Notepad's font;
  - Paint's text tool (open question 1: whether that needs fonts at any size).
- **Measured, never assumed:** every widget measures with the font it has. Layout follows a bigger font.

### 7. Docking, splitters and panes (new)

- **Splitters:** two panes and a bar between them, dragged or moved by keys, with sizes kept in the app's settings.
- **Docking:** panes (Paint's tools, layers and history; MIDI Studio's piano roll and loop browser; the IDE's
  project, output and debugger panes):
  - docked at a side of the window, grouped as tabs, or floating in tool windows (WIN's tool window kind);
  - hidden and shown from the View menu;
  - the layout saved and restored.
- **The toolbar:**
  - commands as icon buttons with tooltips, drop-down buttons, toggles and separators;
  - fields and choices in it (Files' address bar, Paint's zoom);
  - what doesn't fit goes into an overflow menu ("»");
  - text under icons, or not (a setting).
- **The status bar's sections:** fixed or growing, each with text, an icon or a small progress bar.

### 8. Resource files and introspection (new: the IDE's)

- **A widget class describes its properties:** name, kind (text, number, yes/no, choice, colour, font, command,
  picture), default, how to get and set it.
- **A resource file describes a window or dialog:** its widgets in their tree, each with its class and the
  properties that aren't the defaults, and the commands. It is loaded by any app at run time
  (`gui_load("SETTINGS.GRS")`) and widgets are found by name.
- **Text, so it diffs and merges well:** a small format of nested blocks, as Delphi's DFM text form (open question
  2).
- **Who uses it:**
  - the IDE's form designer edits these;
  - the property grid uses the same description;
  - GLOS's own apps can use them for their dialogs, which then stay translatable without code changes.

### 9. Drag and drop, and the clipboard (grown, on API 2.0's DND and CLIP)

- **Clipboard formats:** text, rich text, images and lists of files (`gui_clip_set_text`, `_image`, `_files`, and
  any format by its name), and transactions.
- **Drag and drop:**
  - a widget offers data in formats (a list's chosen rows as files; a tree's node; a layer as an image);
  - a widget accepts formats and says where a drop would go (a line between rows, a folder highlighted);
  - feedback: the effect's cursor (copy, move, link, no);
  - scrolling when the pointer stays near an edge.
- **Lists and trees do it for the app:** reordering rows within a list (a playlist, tabs) is built in.

### 10. Translation later (new)

- **The owner's answer:** English in v1, translatable later.
- **What v1 does so a translation needs no code changes:**
  - every string a user sees goes through `GUI_T("...")`. In v1 that returns the string; later it looks the string
    up in a catalogue chosen by the region's language;
  - layout already measures, so longer text lays out;
  - dates, times and numbers are formatted by the region's formats (SETTINGS' REGION), never by hand.
- **Not done in v1:** right-to-left text, input methods for Chinese, Japanese and Korean (API 2.0 keeps text input
  apart from keys, so they can come).

## The widgets

New (N) and grown (G). The users named are the app plans that need each.

| Widget | What it is | N/G | Users |
|---|---|---|---|
| Toolbar | §7 | N | Files, Notepad, Paint, PDF, Audio, Recorder, MIDI, Term, Web, IDE |
| Splitter | §7 | N | Files, Notepad, PDF, Chat, Term, MIDI, Paint |
| Dock panes | §7 | N | Paint, MIDI, IDE |
| Status bar sections | §7 | G | Files, Notepad, Paint |
| Tabs | Closing, reordering by dragging, overflow into a list, a "+" | G | Notepad, Term, Web, Task Manager |
| Virtual list | §2: detail and icon views, columns, rubber band, big pictures | G | Files, Games, PDF thumbnails, Task Manager, Mixer's streams, playlists |
| Virtual tree | §2, with badges (unread counts) | G | Files, Help, PDF outline, Chat, Control Panel (USB), Web bookmarks |
| Combo box | An editable field with a drop-down and completion; history | N | Files' address bar, Web's address bar, find fields |
| Code editor | Notepad's editor grown: a piece table for big files, a margin (line numbers, marks, folding), highlighting by a lexer the app chooses, brace matching, column selection, zoom, completion and signature pop-ups, error marks | G | Notepad, IDE |
| Terminal | Term's text grid and VT, moved into the toolkit; clickable lines | N | Term, IDE (output), Task Manager (log) |
| Rich-text view | Styled runs, wrapped; headings, lists, quotes, code blocks, tables as simple grids, pictures and links inline, blocks that expand; selection and copy; built from a document tree that Markdown and the HTML subset make | N | Help, Web, Chat, IDE (conversation) |
| Chat view | Lines that grow at the bottom, coloured and wrapped; the rich-text view's lines, kept to a limit | N | Chat, IDE |
| Canvas view | A canvas with scrolling and zoom in both directions, rulers or headers, a cursor by tool, the mouse captured while dragging; the app draws what is visible | G | Paint, PDF, Recorder (waveform), MIDI (timeline, piano roll) |
| Graph | Lines over time, a few series, scaled | N | Task Manager, Control Panel (network) |
| Level meter | A bar with peak hold, mono or stereo, vertical or horizontal | N | Mixer, Recorder, Control Panel |
| Slider | Vertical too, with marks and a live value | G | Mixer, Audio (seeking with times) |
| Property grid | Names and values, editors by kind (text, number, choice, colour, font), groups | N | IDE, Control Panel (advanced) |
| Calendar | A month, days chosen; the date and time fields | N | Date & Time, the clock's calendar |
| Colour picker | Swatches, RGB, HSV, hex, alpha; the colour dialog | N | Paint, Control Panel (themes) |
| Tooltip | For any widget, after a pause | N | Everywhere (toolbars first) |
| Wizard | Pages with Back, Next, Finish | N | Disk Tools, first start |
| Tour overlay | A page that points at a place on the screen (SHELL's POINT) | N | Help |
| Image view | A picture scaled to fit, or at its size with scrolling | G | Games, Web, PDF, Control Panel previews |
| Preview surface | A child window of another program inside a widget (WIN's EMBED) | N | Control Panel (savers), IDE |

## Dialogs

| Dialog | What it is |
|---|---|
| Message | As today, with "do this for all" and "don't ask again" (kept in the app's settings) |
| Open and save | Long names; places (drives, the desktop, recent folders); the list's detail and icon views; a filter; an optional preview (pictures); an encoding or format choice (Notepad, Paint) |
| Folder | Choosing a folder |
| Progress | §3 |
| Find and replace | With history, case, whole words, regular expressions (the editor's) |
| Colour | The colour picker's dialog |
| Font | §6 |
| About | The app's name, version, licence, and its libraries' licences |

## Menus

- **Commands in menus** (§1); submenus; check and radio items; icons beside items.
- **Context menus:** on the right button or the Menu key (Shift+F10), at the widget's chosen item. Files adds other
  apps' verbs (SHELL's ASSOC).
- **Menus that grow:** recent files, open windows.

## Speed

- **The CPU floor is a 486DX2 for most apps:**
  - virtual lists and the canvas draw only what is on screen;
  - text is measured once and cached;
  - damage stays per widget, as today.
- **Long work is never on the window's thread** (§3). A window that takes no event for 5 seconds is "not answering"
  to the shell (API 2.0's WIN). The toolkit keeps its own work short so that only an app's own fault causes it.

## Tests

- **The host shim gets API 2.0's areas:**
  - threads (real host threads, with a mode that runs tasks in order for scripts that need the same pictures each
    time);
  - DND, CLIP's formats, SETTINGS' change events and themes.
- **Each widget:** a GUI script and goldens at 8, 16 and 32 bits, in the default theme and one other.
- **Each dialog:** a script driven only by keys (D34).
- **Resource files:** every app's dialogs loaded and compared with the same dialogs made in code; a file written,
  read back, and the same.
- **Speed:**
  - on the host: a virtual list of 10,000 rows asks for the cells of the visible rows only, and draws only them
    (counted, so the check is exact);
  - on the P4: the first screen's time, measured (86Box's speed never sets a budget).
- **Loop A smoke:** one app with a toolbar, a splitter, a virtual list loading as it goes and a background task.

## Moving the apps (phases C, E, F)

- **The toolkit's API is not compatible with today's where meaning changes** (colours by role, lists by models). The
  calls that keep their meaning keep their names (`gui_button`, `gui_vbox` and most others).
- **GLOS's apps move in phase C** with the toolkit, each still doing what it did (the goldens' pictures may change
  only by the theme's metrics), before phases E and F add their features.
- **The toolkit's API is frozen with API 2.0 at GLOS v1:** the SDK publishes it, and after v1 it only grows.

## Open questions, for the owner

1. **Fonts at any size:** Paint's text tool, the PDF Viewer's fonts and a large UI size all want TrueType drawn at
   any size. Should v1 have a scalable font rasteriser (integer arithmetic, no FPU: our own, or stb_truetype made
   fixed-point) shared by the toolkit, Paint and the PDF Viewer? Or the bitmap sizes only, with Paint's text tool
   limited to them?
2. **The resource format:** a DFM-like text of nested blocks (readable, diffs well), or INI sections (GLOS.CFG's
   reader already exists), or JSON (well known, more to type)?
3. **The built-in themes:** Slate, a dark one and a high-contrast one in v1. Any others?
