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
- **The built-in themes** (shell.md): Slate, a dark one and a high-contrast one.

### 6. Fonts (grown)

- **The UI's fonts as today:**
  - bitmaps made at build time from open fonts (DejaVu, `docs/gui.md` §1.10);
  - now in several sizes (small, normal, large: the theme's UI size), each with its smoothed version.
- **The font dialog:** the fonts there are and their sizes, for:
  - Term's and Notepad's font;
  - Paint's text tool, which also draws TrueType files at any size (stb_truetype, Paint's plan; the PDF Viewer
    has the same).
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
- **Text, so it diffs and merges well:** a small format of nested blocks, as Delphi's DFM text form:

  ```
  object Settings: Dialog
    Title = "Settings"
    object Ok: Button
      Text = "&OK"
      Default = true
      Command = cmd_ok
    end
  end
  ```
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

## Phase C in steps (2026-11-16: the order it is built in; the owner chose phase C as the next work that day)

Each step ends with the host tests and goldens green and a Loop A smoke; a step that changes pictures says
which and why. The apps keep working at every step (they move as the toolkit does).

| | What | Shows it works |
|---|---|---|
| C1a | Colours by role: the toolkit and the apps draw with the theme's colours (API 2.0's THEME), read at start and again at a theme change; the host shim has the three themes | Slate's goldens byte for byte as before; the gallery's in the dark and the high-contrast theme; a theme changed under a running app in Loop A |
| C1b | Metrics and fonts: the theme's sizes (scroll bar, icon, UI text small, normal and large), every widget measured with the font it has | The gallery at each text size; goldens that change by metrics, listed |
| C2 | Commands (text, icon, shortcut, tooltip, enabled, checked, set once), the update hook; toolbars with overflow; tooltips; context menus; the status bar's sections; menus with icons, submenus and radio items | A gallery page for each; keys only |
| C3 | Models: the virtual list (detail and icon views, columns sorted, resized and reordered, selection by ids, rubber band) and the virtual tree; the old `gui_list` on top of the model | 10,000 rows: only the visible rows' cells asked for, counted |
| C4 | Background work on API 2.0's THREAD and POST: tasks, the pool, progress and cancelling, the progress dialog; the shim's threads with an in-order mode | A list that fills as a task reads a folder; Loop A smoke |
| C5 | Splitters, tabs that close, move and overflow, docking panes and their saved layout | Gallery pages; a 640x480 window that folds |
| C6 | The combo box; dialogs: open and save (places, views, filter, preview), folder, find and replace, colour, font, about, message with "don't ask again" | Each dialog driven by keys only |
| C7 | Undo history and its list; the clipboard's formats and drag and drop (API 2.0's CLIP and DND), built into lists and trees | Host tests; two apps in Loop A |
| C8 | The remaining widgets, in the order the apps of phases E and F need them: slider and level meter, graph, calendar, colour picker, image and canvas views, property grid, wizard, rich-text and chat views, terminal, code editor, tour overlay, preview surface | A gallery page each |
| C9 | Resource files and the classes' property descriptions; `GUI_T` | Every dialog loaded from a file and compared with the same made in code |
| C10 | The apps moved (each still doing what it did), the gallery app whole, the toolkit's documentation (`docs/gui.md`) | Every app's goldens; the full gate |

### C1a as built (2026-11-16)

- `gui_theme` (`include/glos/gui.h`): the theme in force, read by `gui_init` and at `GLOS_EV_THEME`; the old
  names (`GUI_FACE` ...) are its roles now, with new ones for text (`GUI_TEXT`, `GUI_FIELD_TEXT`), for what is
  chosen (`GUI_CHOSEN`, `GUI_CHOSEN_TEXT`) and `GUI_ROLE(role)` for the rest. `docs/gui.md` §1.9 has the table.
- Every text, tick, arrow, caret and selection in the toolkit has its role; the apps' own fixed colours too
  (Files' desktop, the Terminal's selection, Calculator's display). Left for later steps: the Terminal's
  default colours (white paper in every theme, until the terminal widget of C8) and the Monitor's graph (its
  own black scope, until the graph widget).
- Kept colours follow the theme: a label's default text and a box's background are kept as "the theme's" and
  looked up when drawn.
- The host shim has the kernel's three themes (`kernel/wm/theme.c` itself), by `[desktop] theme` (a script's
  `config desktop theme NAME`) and changed under the app by a script's `theme NAME`; `same A B` checks two shots
  of a run are one picture.
- Tests: Slate's 357 goldens byte for byte what they were (the Terminal's `ver` line apart, which says API 2.3
  now); `theme-dark`, `theme-contrast` (the gallery's pages, a menu, typed and chosen text, a chosen row, the
  About box) and `theme-change` (Slate, dark, high contrast with a dialog open, and Slate again: the first
  picture), on the host and played on the desktop in Loop A.

### C1b as built (2026-11-16)

- **Sizes:** small 9, normal 11, large 14 pixels (DejaVu Sans, Bold and Mono each). The normal ones stay built
  into every program; the others are data, `FONTS.DAT` beside the program (112 KB), so a program pays for them
  only when they are in force. `SMOOTH.DAT` has all nine's coverage (400 KB); a program reads its three.
- **`[desktop] text = small | normal | large`** (the Control Panel's Desktop page), in force at once: the
  kernel's THEME answers with it and tells the apps as for a theme change. The toolkit swaps the fonts' contents
  (`sdk/gui/fonts.c`, which took `smooth.c`'s place), lays every window out again and draws it.
- **Metrics by the font:** button, field, row and tab heights and a button's least width are the font's height
  and a margin; a scroll bar's width is the theme's (14 in the built-in themes: the kernel's table said 16, which
  nothing had read). A tab is as wide as its title in bold needs.
- **Pictures that changed at the normal size:** the Control Panel's (its Desktop page has the new setting; its
  tabs are a pixel or two wider for their bold titles) and the tabs page's list style, for the same reason.
  Everything else is byte for byte as before.
- **Not yet by the font:** check boxes and radio buttons (13 pixels), the tree's indent, big icons' cells, the
  windows' own sizes where an app gives them in pixels (the gallery's 440x330 holds the large size). Those go
  with the widgets' own steps.
- **Tests:** `text-small`, `text-large` (the gallery's pages, a menu, the About box) and `text-change` (normal,
  large, small and normal again: the first picture), on the host and on the desktop in Loop A.

### C2 as built, first part: commands and the toolbar (2026-11-16)

- **Commands** (`sdk/gui/command.c`): `gui_command(win, id, text, shortcut, icon, tip)` sets one, once;
  `gui_command_enable`, `_check` and `_radio` change its state, and every place that shows it follows. The
  window's `on_update` is asked which commands can be used now before a menu opens, before a shortcut is looked
  up and before a toolbar is drawn; `on_command` is told the id wherever it came from. A shortcut belongs to
  the command, so a window with a toolbar and no menu bar has its shortcuts too.
- **Menus show commands** (`gui_menu_command(menu, id)`): the text, shortcut and state are the command's, looked
  up when the menu opens; with icons, where the commands have them (the texts then start past the icons); a
  dot for the chosen one of a group; a checked command with an icon is framed. Items given by `gui_menu_item`
  are as they were.
- **The toolbar** (`sdk/gui/toolbar.c`; `gui_toolbar`, `gui_toolbar_command`, `_sep`, `_text`): under the menu
  bar; a button a command, its icon (its text beside it if asked, its text alone where it has no icon), greyed
  while it can't be used, held in while it is checked or pressed, framed under the pointer. What doesn't fit
  the window's width is behind a button at the bar's end, in a menu of the same commands. It never has the
  keyboard (D34).
- **Tooltips** (`sdk/gui/tip.c`; `gui_tip(widget, text)`): a line in a pop-up of its own by the pointer, 600 ms
  after the pointer came to rest there; gone when the pointer goes on to something else, at a press, a key or
  the wheel, and when the window loses the keyboard. The pop-up is shown without the keyboard (libglos's
  `glos_show_quiet`: API 2.0's SHOW with NOFOCUS). A toolbar's button has its command's tooltip, or its name
  where the bar shows no texts, with the shortcut ("Open a file (Ctrl+O)"); a widget of an app's own names its
  parts' tooltips from its pointer handling (`gui_tip_at`).
- **Context menus** (`gui_context(widget, menu)`): opened by the right button over the widget or what is in it,
  after the press has chosen what is under the pointer (lists did; trees do now), and by the Menu key or
  Shift+F10 by the widget that has the keyboard. The widget's `on` is told first (`GUI_ON_CONTEXT`), so the app
  can change the menu for what is chosen; the menu's commands are asked for as for any menu.
- **The status bar's sections** (`gui_status_section`, `gui_status_set`, `gui_status_progress`): at its right
  end, each as wide as it is made, with a text, an icon before it, or a small progress bar; the bar's own text
  has what they leave.
- **Submenus** (`gui_menu_sub(menu, text)`): an item with an arrow that opens a menu of its own beside it (to
  the left at the screen's edge), by Right, Enter or its letter, or the mouse; Left or Esc closes it and the
  menu it came from has the keyboard again; three deep at most. The open menus are a stack in `menu.c`; a
  choice closes them all.
- **Still to come in C2:** drop-down buttons and fields in the toolbar (with the combo box, C6).
- **Tests:** `commands` (GUITEST's commands page: the menus with icons, a tick and a dot; the update hook
  greying Paste until Copy; a button pressed, a toggle held in, shortcuts; texts on the toolbar; the window
  made narrow and the overflow's menu; a list's context menu by the mouse and by Shift+F10; the status bar's
  sections; a button's and a label's tooltip), on the host and on the desktop in Loop A; the scripts' `move`.

### C3 as built, first part: the virtual list's detail view (2026-11-16)

- **The model** (`struct gui_model`): `count`, `row` (a row's cells, icon and id) and, if the app can say,
  `find` (where the row with an id is now). `gui_vlist(parent, columns, heads, widths, &model)` keeps no rows:
  it asks for the rows on the screen when it draws, and only for those among what is drawn again.
- **Rows that arrive, go or are sorted:** the app says `gui_vlist_changed`; the current row is where its id is
  now (the model's `find`, else a screen either side of where it was).
- **Chosen by id:** the current row and the rows chosen are ids, so they survive sorting and arrivals
  (`gui_vlist_id`, `gui_vlist_chosen`). Shift chooses a range (the rows between are asked for their ids), Ctrl
  with the arrows moves without choosing and Ctrl+Space or a Ctrl press turns a row's choice over.
- **Sorting is the app's:** a press on a column's head tells the widget's `on` (`GUI_ON_SORT`;
  `gui_vlist_sort` says the column and the way, a second press on the same head turning it), the app sorts and
  says the list changed. The head shows the way.
- **Columns:** an edge in the header is dragged; the last column has what is left.
- **The icon view** (second part, 2026-11-16; `gui_vlist_view(w, GUI_VIEW_ICONS)`): the same rows as big icons
  in a grid (the row's `big` icon, 32 pixels, or its small one in the middle), the first cell's text under
  each; the arrows move four ways; only the lines of icons on the screen are asked for. With more than one row
  to be chosen, a press on nothing held and moved is a rubber band that chooses the icons it touches.
- **The virtual tree** (third part, 2026-11-16; `sdk/gui/vtree.c`, `struct gui_tmodel`: `children`, `node`):
  the widget keeps the ids of the nodes on show, in their order; a node's children's ids are asked for when it
  is opened (in pieces of 64), and its text, icon, badge and whether it has children when it is drawn, so a
  folder of five thousand costs their ids when opened and a screen of texts. `gui_vtree_changed(w, id)` asks
  for a node's children again; what was open under it and the current node are found by their ids. A badge is
  a short text at the row's right end. Keys and the mouse as `gui_tree`'s.
- **Type-ahead** (fourth part, 2026-11-16): letters typed within a second of each other go to the row whose
  first cell begins so; one letter again and again goes on to the next such row. The search is the model's
  (`match`), since only the app has all the rows.
- **Columns moved, small icons, thumbnails** (fifth part, 2026-11-16): a column's head dragged sideways (more
  than 5 pixels) moves the column, a bar showing where it will go; a press that stays put sorts, at its
  release. The order is the widget's (the model's cells keep their numbers); `gui_vlist_columns` and
  `gui_vlist_set_columns` read and set the order and the widths, for an app to keep them. `GUI_VIEW_SMALL`:
  small icons in a grid, the name beside each; `GUI_VIEW_THUMBS`: the row's `thumb` (a picture of its own size,
  `GUI_THUMB` = 96 pixels square at most, framed; made by the app, best by a task) or its big icon, the name
  under. Both have the icon view's arrows and rubber band.
- **Moved to C10:** `gui_list` and `gui_tree` on top of the models. Every app's list and tree would be drawn
  anew, which changes every app's pictures; C10 moves the apps and makes their goldens again in any case, so the
  old widgets go onto the models there, once (decided 2026-11-16, at the end of C3).
- **Tests:** `vlist` (GUITEST's page: twenty rows, then ten thousand; End and Page Up; the app counts the
  distinct rows it was asked for, a few screens of ten thousand; sorted downward with the current row kept by
  its id; a range and a row more chosen; a column dragged wider; Enter; the icon view, the arrows in it, a
  rubber band over nine icons, and back), on the host and on the desktop in Loop A. `vtree` (GUITEST's page: nodes
  opened by the arrows; one with five thousand children and its end; the nodes asked for counted; badges; a
  node's children changed with a folder open under it; Left out and Enter). `views` (GUITEST's page: the Name
  column dragged past Size, the layout read back and reset; a head pressed still sorts; small icons, their
  arrows and a band; thumbnails of three shapes and a folder's icon).

### C4 in detail (2026-11-16, before it is built)

- **What the API has** (phase B): `glos_thread`, `glos_join`, locks (`glos_lock`, `glos_take`, `glos_give`),
  signals and `glos_wait`, and `glos_post`, an event of the app's own into its queue from any thread. A program
  with threads is linked with `@gloswrap.opt` (one lock round DJGPP's C library): GUITEST and every app that
  uses tasks.
- **A task:** `gui_task_start(owner, title, work, arg, done, flags)` queues `work(task, arg)` for a worker
  thread. From the worker: `gui_task_progress(task, done, total, text)`, `gui_task_cancelled(task)` and
  `gui_task_post(task, fn, data)`, which has `fn(data)` run on the window's thread (rows for a list, a
  thumbnail). On the window's thread: `gui_task_cancel(task)`, and `done(owner, arg, cancelled)` when the work
  has returned. Only the window's thread touches widgets.
- **How results cross:** a locked queue in the toolkit and one `glos_post` (a code of the toolkit's own) that
  wakes the loop, which drains the queue in order; progress is the latest value, not a queue.
- **The pool:** workers made when first needed, two by default and one where the CPU is a 486
  (`gui_task_threads(n)` says otherwise); tasks wait in a queue for a free worker.
- **The progress dialog** (`GUI_TASK_DIALOG`): if the task is still running a second after it started, a
  modal dialog with its title, a bar, the text and Cancel; gone when the task ends.
- **The host shim** gets threads two ways: real ones (pthreads), and for the goldens an in-order mode in which
  one thread runs at a time: a worker runs while the window's thread waits for an event with none queued,
  until it sleeps (the shim's clock moves on), blocks or ends. The same script then gives the same pictures.
- **Tests:** a list that fills as a task "reads" ten thousand rows in pieces (posted), its progress dialog and
  Cancel; two tasks and one worker (the second waits); the host's lock checks; Loop A smoke on a 486 and a
  Pentium II.

### C4 as built (2026-11-16)

As designed above, with these particulars:
- `sdk/gui/task.c`; the event loop reaches it through a hook set at the first task (`gui_post_hook`), so a
  program that starts no task links no thread code and needs no `@gloswrap.opt`. GUITEST does, and is linked
  with it (and `tools/wrapcheck.py`).
- The progress dialog is modal without a loop of its own (`gui_modal_on`/`gui_modal_off`): the window's thread
  goes on in `gui_run`, taking the tasks' posts.
- The host shim's threads run in order on pthreads (one at a time, by a baton): the made threads have turns when
  the first thread waits with no event queued; `glos_sleep` moves the shim's clock. A script's `wait` and `gone`
  let the shim's time pass (a tenth of a second a go, a minute at most) when the app has made threads.
- Tests: `tasks` (GUITEST's page: ten thousand rows read in a hundred pieces over ten seconds and posted, the
  list growing; the dialog after a second; Cancel by Esc), on the host and on the desktop in Loop A (a 486 and
  a Pentium II). The work lasts ten seconds because Loop A's driver takes seconds a step on a 486: with two, the
  dialog came and went unseen.

### C5 in detail (2026-11-16, before it is built)

In three parts, each with its GUITEST page and script, on the host and in Loop A:
- **C5a, splitters** (`sdk/gui/split.c`): `gui_split(parent, vertical, at)`; the app makes the two panes in it.
  `at` is the first pane's size, kept as the window changes, or (less than 0) minus the second's, kept instead.
  The bar is the split's own child, placed between the panes so that Tab reaches it between them; dragged, or
  moved by the arrows (8 pixels, Ctrl 1); Home and End fold a pane away and back. Least sizes (32 pixels unless
  `gui_split_min` says). `GUI_ON_CHANGE` when the user moved it, for the app's settings (`gui_split_get`).
- **C5b, tabs that close, move and overflow** (the tabs widget grows; old uses unchanged unless they ask):
  `gui_tabs_flags(tabs, GUI_TABS_CLOSE | GUI_TABS_MOVE | GUI_TABS_NEW | GUI_TABS_MENU)`. A close box on each
  title (and the middle button, and Ctrl+F4 while the tabs or their page have the keys) asks the app
  (`GUI_ON_CLOSE`, `gui_tabs_closing` says which; the app removes the page with `gui_tab_remove` when it agrees:
  a document with changes asks first). A title dragged along the strip moves its page (`GUI_ON_MOVE`; the order
  is the pages' order, so the app reads it from them). A "+" after the last title asks for a page (`GUI_ON_NEW`).
  With more titles than fit, a "v" button at the strip's end drops a menu of every title (GUI_TABS_SCROLL keeps
  its arrows as well). Ctrl+Tab and Ctrl+Page Up/Down as now.
- **C5c, docking panes and their layout** (`sdk/gui/dock.c`): a window's dock (`gui_dock(win, centre)`) holds the
  app's main content in the middle and panes at its four sides; `gui_pane(dock, id, title, side)` makes a pane
  (a vbox for its content) docked at a side, several at one side shown as tabs, the sides separated from the
  middle by splitters. A pane is hidden and shown again (`gui_pane_show`, the View menu's checks), moved to
  another side or floated in a tool window of its own (WIN's tool window kind) by its title's context menu or by
  dragging its title onto a side's edge. `gui_dock_save(dock, buf, room)` and `gui_dock_load` give the layout as
  one line of text (sides, sizes, tabs' order, which shown, floating places) for the app's settings. Narrower than
  the panes and the middle's least sizes (640x480), the side panes fold into tabs at one side.

### C5 as built, first two parts (2026-11-16)

- **C5a, splitters** as designed. The bar has the keys once pressed (a drag leaves it focused). A folded pane is
  hidden (no keys go into it); dragging the bar out unfolds it. `gui_split_get` is the place when unfolded,
  `gui_split_folded` says which pane is folded.
- **C5b, tabs:** as designed, in the strip's styles (not GUI_TABS_LIST). A press on a title's close box or the
  middle button on a title asks (`GUI_ON_CLOSE`), and so does Ctrl+F4 with the keys on the tabs or anywhere in
  their pages (passed up by the window when the page's widget doesn't take it). A title moves once dragged more
  than 5 pixels, a bar showing where; the moved page stays chosen. The "+" and the list button take the strip's
  right end, the scrolling arrows before them; the list is a pop-up menu whose choice goes to the tabs themselves
  (`gui_menu_to`, the toolkit's own), the chosen title checked. Old tabs (no flags) draw as before.
- **C5c, docking panes** (`sdk/gui/dock.c`): made of the toolkit's parts: three splitters (left | the rest;
  the middle | right; the centre over the bottom) and at each side a row of tabs whose pages are the panes
  (scrolling, with close boxes and dragging). A side with no pane is folded away with its bar. Hidden panes keep
  their widgets on a shelf of the dock's; a pane moves between sides and windows whole (`gui_adopt`, the toolkit's
  own). A side's context menu acts on its chosen pane: dock at another side, float, hide. A floating pane is a
  tool window (`GUI_W_TOOL`, GLOS_WK_TOOL, owned by the app's window), the first time beside the app's window
  where the screen has room (else where GLOS puts it); closing it hides the pane; its place is kept when it
  docks again or the layout is saved. The layout line: `dock1 L R B;id side shown[ x y w h];...`
  (the splitters' places; the docked panes in their sides' order, then the floating and hidden ones).
  `GUI_ON_CHANGE` on the dock for every change the user made (a bar, a close, a move, the menu).
- **Narrow (640x480):** a dock narrower than its two sides and the middle's least (140 pixels) need lends the
  right side's panes to the left side's tabs; they go back once it is 32 pixels wider than that (so a size near
  the line doesn't swing). They stay the right side's: the menu, `gui_pane_side` and the saved layout say so.
- **Dragging a pane** (`GUI_TABS_DRAG`, the dock's tabs have it): a title dragged more than 16 pixels out of its
  strip is dropped where it is let go (`GUI_ON_DROP`, `gui_tabs_dropped`), not moved along: in the dock's left
  or right fifth it docks at that side, in its bottom quarter at the bottom, anywhere else it floats there. No
  outline shows the place while it is dragged (the title stays shaded): a later refinement if it is missed.
- C5 is complete with that.
- **Tests:** `split` (GUITEST's page: a tree beside a list, an output pane under the list; the bar dragged, its
  arrows, a pane folded and back, the bar across moved up; the window made wider: the tree keeps its width, the
  output pane its height) and `doctabs` (six documents in a scrolling row: a page with changes kept at its close
  box, then closed by the middle button; a title dragged onto the next; "+"; the list; Ctrl+F4 in a page; the
  order read back). Both scripts' players learnt a middle button (`click ... middle`). `dock` (GUITEST's page:
  Tools and Layers at the left, Output at the bottom, Properties at the right; Properties closed (the right side
  goes), Output hidden and shown by a button, Layers docked at the bottom and Tools floated by their context
  menus, the layout saved, reset (the tool window goes) and loaded (it comes back), each change as the API
  reads it; then the window made narrow (Properties joins the left side's tabs, the layout still says right)
  and wide again; Properties' title dragged out and let go low in the middle: docked at the bottom). All three
  in Loop A on a 486 and a Pentium II too; there a shot is of the screen, so a page's
  shot is never taken with a pop-up or a tool window over it (the page's window is placed, and narrow enough
  for its floating pane to go beside it).

### C6 in detail (2026-11-16, before it is built)

Each part with GUITEST pages and scripts that drive the dialogs by keys alone, on the host and in Loop A:
- **C6a, the combo box** (`gui_combo`): a field with a drop button. Its list is the app's items
  (`gui_combo_add`) and its history: with `gui_combo_history(combo, n)`, a text accepted by Enter goes to the top
  of the list (once). Typing drops a list of the items that begin with what is typed, without taking the keys
  from the field; Up and Down walk it, Enter or a click takes one, Esc closes it; Alt+Down or F4 drops the whole
  list. An app may complete from its own source (`gui_combo_source`: Files' address bar, the folders there are).
  The field's text is the value (`gui_get_text`); `GUI_ON_ACTIVATE` at Enter, `GUI_ON_CHANGE` as it is typed.
  The toolbar takes a combo box as an item (C2's owed fields).
- **C6b, the small dialogs:** a message with a check box of "Don't ask again" or "Do this for all"
  (`gui_message_check`: the button, and the box's state, which the app keeps); About (`gui_about`: the app's
  icon, name, version, copyright, its licence, and a list of the libraries and icon sets it uses with theirs,
  the toolkit's own credits added for every app); Folder (`gui_folder_dialog`: a virtual tree of the drives and
  their folders, the path typed in a combo box, a New folder button).
- **C6c, Open and Save** (`gui_file_dialog_ex` with `struct gui_file_opts`; `gui_file_dialog` keeps its form):
  places down the left (the drives, the app's recent folders, the desktop's folder), the address as a combo box
  with the folders typed completed, the files in a virtual list (detail and icon views, sorted by column), a
  filter choice ("Pictures (*.BMP;*.PNG)"), the name field; an optional preview pane the app draws for the file
  chosen (Images', Paint's); for saving an optional format or encoding choice. Long names where DOS has them
  (INT 21h 71h); 8.3 names otherwise, as today.
- **C6d, Find and replace** (`gui_find`): modeless, over its window; what to find and what to put in its place
  as combo boxes with history; Match case, Whole words, Regular expressions; Find next, Replace, Replace all.
  The app is told what to do (`on_find`) and searches its own text; the toolkit gives the matching
  (`gui_match`, `sdk/gui/regex.c`: . * + ? [ ] ^ $ \ and groups for replacing, host-tested) so every app
  searches alike.
- **C6e, Colour** (`gui_colour_dialog`): swatches (sixteen basic colours, the theme's colours, the app's recent
  ones), a saturation-and-value square with a hue bar, red, green and blue, hue, saturation and value, and hex,
  each typed or moved by keys; the old colour beside the new.
- **C6f, Font** (`gui_font_dialog`): GLOS's fonts (the interface's at small, normal and large, bold or not, the
  fixed-width one, the smoothed scripts' where present), a size where a font has several, and a line of preview.

### C6 as built, first part (2026-11-16)

- **C6a, the combo box** as designed, in `sdk/gui/field.c` (a field with a drop button, the field's own code
  given the button's width): its list is a pop-up window that never takes the keys (`gui_show_nofocus`), drawn
  by the combo box's own row widget, a click on a row taking it. Esc closes the list and keeps the text as
  typed; Enter with no row chosen takes the text as typed. Down drops the whole list when it is closed. Up to 64
  rows match, eight show (the toolbar's combo item and a scrolling list are owed with the address bars that
  need them).
- **C6b, the small dialogs:** `gui_message_check` (the check box tells the app's int as it changes, the dialog
  being gone when gui_modal returns); `gui_about` (the toolkit's three credits after the app's, each a block);
  `gui_folder_dialog` (a virtual tree whose ids are places in the dialog's table of paths, opened down to the
  starting folder; the path in a combo box, Enter on it or the tree takes it; New folder asks for a name and
  makes it with `gui_mkdir`, then shows and chooses it).
- **C6c, Open and Save** (`sdk/gui/filedlg.c`, `gui_file_dialog_ex` and `struct gui_file_opts`): as designed. The
  places are a list (the drives, then the app's recent folders: a choice goes there); the folder's combo box keeps
  the folders visited and completes the ones typed; the files are a virtual list (Name, Size, Modified; folders
  first; a column's head sorts; details or icons by the View button; type-ahead). A name typed with * or ? is a
  filter; a folder's name goes there; saving, a name without a type takes the filter's first and a file there
  already asks before it is replaced. Names typed are DOS's capitals. Long names are not offered yet (DOS's
  INT 21h 71h isn't used by the toolkit's files). `gui_file_dialog` keeps its old form until C10.
- **C6d, Find and Replace** (`sdk/gui/find.c`, `sdk/gui/regex.c`): `gui_find` over its window, modeless; the two
  combo boxes' histories last the app's run; the app's `on_find` is told Next, Previous, Replace or All and
  answers with `gui_find_say`. `gui_match`: plain text (case, whole words) or a regular expression (. [] [^] * + ?
  and their lazy forms, ^ $, \d \w \s and their opposites, groups, |), searching back too; a backtracking
  matcher bounded at 200,000 steps (a pattern that would run on is "not found"). `gui_match_replace`: \0-\9 and
  &. Host test `regex_test`.
- **C6e, Colour** (`sdk/gui/colour.c`): as designed; the swatches' arrows take the colour they land on (Enter is
  OK); the recent colours are the app's run's eight last.
- **C6f, Font** (`sdk/gui/fontdlg.c`): the faces (sans, bold, fixed width) and the sizes there are (small and
  large only with FONTS.DAT), a sample in the font chosen; `gui_font_get` gives an app the font. The smoothed
  fonts' other scripts are not offered yet.
- **Owed from C6:** the toolbar's combo item (C2's owed fields), long names in Open and Save.
- **Test:** `smalldlg` (GUITEST's three buttons by keys alone: the message's box ticked and No; About's shot;
  Folder from C:\GUI, New folder NEWDIR, OK; the page removes NEWDIR after, as Loop A plays the other scripts on
  the same disk), on the host and in Loop A with `dialogs`. `filedlg` (Open: a folder's name typed, the Pictures
  filter, type-ahead to a picture the preview describes; Save as: a type added, a file there replaced after No
  then Yes, the other format; the Modified column blanked in the shots, as the dates are the copy's), `find`
  (next, previous, not found, replace, replace all, a regular expression swapping two words), `colour` (a
  swatch, hex, the square's arrows; the recent swatch next time), `font` (bold, large; the page's line drawn
  so), all on the host and in Loop A on a 486 and a Pentium II. `combo` (GUITEST's folder and name boxes, by keys alone: typed and chosen from the list, a text
  no item begins with, the history read back, F4's whole list, Esc, the name box's items made for what is
  typed), on the host and in Loop A on a 486 and a Pentium II. Its shots are taken with no list dropped (Loop
  A's shots are of the screen).

### C7 as built, first two parts (2026-11-16)

- **C7a, undo** (`sdk/gui/undo.c`): `gui_undo_new(limit)`, `gui_undo_push(u, name, undo, redo, data, bytes, drop,
  merge, key)`; a step pushed with the same key within a second is folded into the one before by the app's merge
  (a burst of typing undoes at once); a push after undone steps drops them; the oldest steps go past the limit.
  `gui_undo_commands` keeps the window's Undo and Redo commands named and enabled (Ctrl+Z, Ctrl+Y);
  `gui_undo_list` is the history list ("(the start)" and a row a step, the undone marked; a row chosen undoes or
  redoes to it); `gui_undo_watch` tells the app of every change. Test `undo` (host; Loop A on a 486 and a
  Pentium II).
- **C7b, the clipboard's formats** (`sdk/gui/clip.c`): pictures and lists of files besides text, any format by its
  name, several at once in a transaction (`gui_clip_start`, the puts, `gui_clip_done`), `gui_clip_has` for Paste,
  and readers for each. On the way GLOS itself changed: a put that fails spoils its transaction (closing it
  changes nothing: a paste never sees part of a copy), and a private format's name refused (a space, or not
  printable ASCII) is a wrong argument, no longer "no memory". The host shim's clipboard is GLOS's: formats,
  transactions, private names by the same rule. Test `clipfmt` (all four kinds and a private one in one
  transaction, read back; a failing copy changes nothing; plain text alone after), host and Loop A.
- **C7c, drag and drop** (`sdk/gui/dnd.c`, on API 2.0's DND and CLIP): `gui_drag_source` (the app's offer fills
  a writing transaction with what is chosen; done hears the effect: a move takes the things away) and
  `gui_drop_target` (formats, effects, the app's drop with the place and a reading transaction:
  `gui_drop_read`). A window's GLOS target is all its widgets' formats; the core routes ENTER/OVER/LEAVE/DROP to
  the widget under the pointer (`GUI_EV_DRAG`, `GUI_EV_DRAG_LEAVE`, `GUI_EV_DROP`) and answers GLOS with the
  widget's effect (a move within the widget a drag came from: `gui_drag_from`). The virtual list drags its chosen
  rows (a press on a chosen row keeps the choice until the release, so several go together) and shows a line where
  a drop would go (a frame round the cell in its grids); the virtual tree drags its current node and frames the
  node a drop would go into (`gui_vtree_id_at` for the drop's row). The host shim drives a drag on the kernel's own
  state machine (`kernel/wm/dnd.c`): a held button keeps the moves with its window, and the drop is decided once
  the move's events are answered, as on GLOS.
- **Owed from C7:** a picture with the pointer while dragging, scrolling when a drag stays near a list's edge, the
  old gui_list and gui_tree (on the models in C10).
- **Test** `dnd` (GUITEST's fruit, basket and places: a copy between lists at the place shown, a reorder within a
  list, a drop on nothing, a drop into a tree's node, two chosen rows dragged together), host and Loop A.

### Icons (2026-11-16: the owner's choice, after sample sheets of eight sets in GLOS's format)

- **Farm-Fresh** (FatCow; CC BY 3.0) for colour icons and **Font Awesome Free, solid** (CC BY 4.0) for the
  one-colour glyphs of toolbars and menus. Considered and not taken: Tango (public domain, 215 icons), Fugue,
  Silk, Fluent Emoji; Fluent, Bootstrap, Material, Codicons, Open Iconic, Phosphor.
- As built: `docs/gui.md` §1.11 (`gui_icon`, `gui_glyph`, `ICONS.DAT`, `make icons`). 55 of each to start with;
  the lists grow as the apps need them. Test: `icons` (GUITEST's page, in the three themes).

## Settled at the owner's review (2026-11-06)

1. **Fonts:** the UI's fonts stay bitmaps made at build time, in three sizes (small, normal, large), each with its
   smoothed version. Paint's text tool and the PDF Viewer draw TrueType at any size with stb_truetype, as their
   plans say.
2. **Resource files:** a text of nested blocks, like Delphi's DFM text form.
3. **Built-in themes:** Slate, a dark theme and a high-contrast one, all within the 8-bit palette's cube.
