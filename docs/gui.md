# GLOS's toolkit and native apps

The design of M9's ring-3 side (PRD §10.6, D49; docs/milestones-m9.md): the toolkit every native app is built on
(`sdk/gui`, `include/glos/gui.h`), and how the apps and their tests are made. The kernel's side (windows, events,
the clipboard) is docs/graphics.md's.

## 1. The toolkit (`sdk/gui`)

### 1.1 Retained widgets

A window (`struct gui_window`) holds a tree of widgets (`struct gui_widget`). Each widget is an object of a class
(`struct gui_class`: draw, event, measure, layout, destroy) that keeps its own state: a button whether it is held
down, a list its rows and which are chosen, the editor its text. The app makes widgets once, changes them when it
wants, and is told of what the user did through each widget's `on` function (`GUI_ON_CLICK`, `GUI_ON_CHANGE`,
`GUI_ON_SELECT`, `GUI_ON_ACTIVATE`) and the window's `on_command` (menus and their shortcuts).

A change to a widget marks its rectangle damaged (`gui_damage`). Before the app next waits for an event
(`gui_step`), each window's damaged rectangles are drawn: every widget that meets one draws itself, from the root
down, clipped to the rectangle and to its parents. Nothing else is drawn. The window manager keeps a window's
pixels (M6), so nothing has to be drawn because another window moved or a menu closed over it.

**Drawing goes into a back buffer** of the window's size and format, then each damaged rectangle is copied into
the window's pixels and presented (`GLOS_FN_PRESENT`). The composer copies a window's pixels whenever it runs
(graphics.md §5.2), so an app that cleared a widget and then drew its text in the window's own pixels could be
caught between the two; from a back buffer it never is. A window's pixels are found again before each use
(`glos_ptr`: an app's memory may have moved since).

### 1.2 Layout

Containers place their children: boxes (`gui_vbox`, `gui_hbox`: children along the box, each its measured size,
what is left shared by their `grow` weights, all as wide or as tall as the box across), forms (rows of a label and
a box, the labels in a column as wide as the widest), groups (a frame with a title), tabs (one page shown),
the scrolling area (a box taller than the room it has, and a bar). Tabs whose titles don't fit in a row
(`gui_tabs_style`) go into more rows (`GUI_TABS_ROWS`, the default; the rows keep their order, the chosen title
bold), stay in one row with two arrows that scroll it (`GUI_TABS_SCROLL`), or become a list down the left with a
scroll bar when it is taller than its room (`GUI_TABS_LIST`). Each is laid out for the width the tabs have at the
time, so a window made wider takes fewer rows; with every title in one row the three look the same, but for the
list. The Control Panel uses rows (`GUITEST tabs` shows all three, `tests/gui/tabs.txt`). Every widget measures the size it wants from
the fonts; `gui_size` sets one instead. A dialog's window is the size its widgets want (`GUI_W_DIALOG`), so
every dialog fits 640×480 (PRD D34) by construction.

A window's layout is done again when something asks (a widget added, removed, shown, hidden, a new text that
changes a size; `gui_relayout`) and before it is drawn; then the whole window is damaged.

### 1.3 The keyboard and the mouse

Keyboard-first (PRD D34):
- **Tab** and **Shift+Tab** move the focus through the widgets that take it, in the order they were made
  (radio buttons: the chosen one of a group; arrows move within it); **Ctrl+Tab**, **Ctrl+PgUp** and
  **Ctrl+PgDn** change tabs (with the tabs' focus, Left and Right too; Up and Down in a list of them);
- **"&" before a letter** in a label, button, check box or menu marks **Alt** and that letter (it is drawn
  underlined; "&&" is an ampersand): a button is pressed, a label gives the focus to the widget after it;
- **F10** goes to the menu bar, Alt and a title's letter opens that menu; in a menu, Up, Down, Home, End, Enter,
  Esc, Left and Right (the next menu), an item's letter;
- **Enter** presses the window's default button (`GUI_DEFAULT`, drawn with a second line) and **Esc** its cancel
  button, unless the widget with the focus takes them (the editor takes Enter; a menu takes Esc);
- **shortcuts** shown in menus ("Ctrl+S", "F3", "Del"): those with Ctrl or Alt before the widget with the focus
  sees the key; those without (Del, F3) only if it leaves the key alone, so Del still deletes in a field.

The mouse: a press gives the focus to the widget under it (or the nearest parent that takes it), which then has
every move and the release until the buttons are up; a second press of the same button within 500 ms and 4
pixels is a double click. A widget under the pointer is told when the pointer leaves it.

Timers (`gui_timer`: a widget's; `gui_idle`: the app's) run from the event wait's time-out: a scroll arrow held
down repeats, the monitor (step G) refreshes. The caret doesn't blink: a blink would change the pictures the tests
compare, and costs a redraw twice a second for nothing.

### 1.4 Widgets

| Widget | What it is |
|---|---|
| label | text, "&" marking its letter; style: font, colour, alignment |
| button | pressed by the mouse, Space, Enter or its letter; default and cancel buttons |
| check box, radio button | a group of radio buttons are the siblings made with the same first |
| field | one line of UTF-8; selection, copy, cut, paste; stars for a password; Enter is `GUI_ON_ACTIVATE` |
| editor | a file's bytes (below) |
| combo box (toolkit 2) | a field with a drop button and a list: the app's items (`gui_combo_add`), a history of texts accepted by Enter (`gui_combo_history`), or items the app makes for what is typed (`gui_combo_source`); typing drops the items that begin so while the field keeps the keys (Up, Down, Enter, Esc steer the list), Alt+Down or F4 drops them all |
| list | rows of cells under a header that sorts; 16-pixel icons; several rows chosen with Ctrl and Shift; typing a name's first letters goes to it; or 32-pixel icons in a grid with their names (the file manager's folders) |
| tree | nodes that open and close, their children added by the app when one first opens |
| virtual list (toolkit 2) | a list that keeps no rows: the app's model says how many and gives the rows on the screen when they are drawn (`gui_vlist`, `struct gui_model`); chosen rows kept by id; sorting asked of the app (`GUI_ON_SORT`); columns resized and moved by their heads (`gui_vlist_columns` to keep the layout); four views: detail, big icons, small icons, thumbnails (`gui_vlist_view`; the rows' `thumb` pictures, `GUI_THUMB` square at most); rubber band; type-ahead by the model's `match` |
| virtual tree (toolkit 2) | a tree that keeps only the ids on show: the model gives a node's children's ids when it opens, and their texts and icons when drawn (`gui_vtree`, `struct gui_tmodel`); badges |
| choice | a button that drops its list of items down |
| spin box, slider | a number in a range; a slider may be upright, have marks and show its value in the app's format (`gui_slider_style`, toolkit 2) |
| level meter, graph (toolkit 2) | one or two channels' levels with their peaks held (`gui_meter`); a few series' recent samples as lines (`gui_graph`) |
| wizard (toolkit 2) | pages with Back, Next and Finish, Next held back until a page is filled in, pages skipped, the steps listed down the left (`gui_wizard`) |
| property grid (toolkit 2) | names and values under groups, each edited as its kind wants (text, number, yes-or-no, choice, colour, font), with the current one's help (`gui_propgrid`) |
| canvas view, image view (toolkit 2) | a document in units that the app draws, zoomed and scrolled both ways, with rulers and a pointer for each tool (`gui_canvas_view`); a picture fitted or zoomed and panned (`gui_image_view`) |
| colour button, palette (toolkit 2) | a button showing a colour that opens the colour dialog, with alpha if asked (`gui_colour_button`); Paint's swatches, the left button the primary colour and the right the secondary, a double click editing one (`gui_palette`) |
| calendar, date and time fields (toolkit 2) | a month with today framed and days chosen (`gui_calendar`); a date field with a calendar that drops down (`gui_date_field`) and a time field (`gui_time_field`), written as the region says; dates as yyyymmdd numbers (`gui_date_add`, `gui_weekday`, `gui_date_text`) |
| tabs, group, form, scrolling area, boxes, space, separator | containers and spacing; tabs may close (a box on each title, the middle button, Ctrl+F4: `GUI_ON_CLOSE`, the app removes the page with `gui_tab_remove`), move by dragging (`GUI_ON_MOVE`), add a page ("+", `GUI_ON_NEW`) and list every title (a button at the strip's end): `gui_tabs_flags` |
| dock (toolkit 2) | a window's main area: the app's content in the middle (`gui_dock_centre`), panes at the left, right and bottom (several at a side as tabs) or floating in tool windows (`gui_pane`); a pane's close box hides it, its title's context menu docks it elsewhere, floats or hides it; `gui_pane_show` for the View menu; the layout as a line of text (`gui_dock_save`, `gui_dock_load`) |
| splitter (toolkit 2) | two panes and a bar between them (`gui_split`): dragged, or moved by its keys between the panes in Tab's order; Home and End fold a pane; one pane keeps its size as the window changes (`at`, or minus the other's); `GUI_ON_CHANGE` for the app's settings |
| menu bar, menus | menus are pop-up windows (below); items with letters, shortcuts, checks; separators |
| progress bar, image, canvas, status bar | a canvas is the app's to draw in and has its events |
| dialogs | message (OK, OK and Cancel, Yes and No, Yes No and Cancel; with a "Don't ask again" box: `gui_message_check`), a line to type, open and save a file (and toolkit 2's `gui_file_dialog_ex`: places, the folder's combo box, the files in a virtual list, filters, a preview, formats), a folder (`gui_folder_dialog`), About (`gui_about`: the app's credits and the toolkit's fonts' and icons'), Find and Replace (`gui_find`, modeless; the matching is `gui_match`: plain or regular expressions, case, whole words, groups for replacing), Colour (`gui_colour_dialog`), Font (`gui_font_dialog`; `gui_font_get` for an app's own text) |

### 1.5 The editor

A file's bytes as they are, shown as Latin-1 (DOS's code pages, M10, are for names and DOS programs' output: a
file's text is left as Latin-1 so that every byte shows and saves unchanged), in DejaVu Sans Mono by default. The text is a gap buffer; the editor keeps the offset of each line's start and moves, adds or takes
them at each edit rather than counting lines again, so a megabyte edits as fast as a line. Lines end in LF or CR
LF, and the caret never stands between the two; Enter writes what the file's first line ends in (CR LF for a new
file). Tabs reach every eighth space. Wrapping shows a line in as many rows as it needs, broken after a space where
it can be; each line's count of rows is kept and counted again only for the lines an edit touched, or all of them
when the width changes. Undo goes back 200 steps; typing joins one step until a new line. Find (with or without
case), replace the chosen text, go to a line, the caret's line and column.

`tests/host/gui_test.c` makes 20,000 random edits and undos (insertions, replacements, removals, wrapping on,
another width), checking after each the text, every line's start and every row's place against a plain copy kept
beside, and then a megabyte.

### 1.6 Menus and pop-up windows

An open menu is a window of its own (`GLOS_WIN_POPUP`, API 1.2): bare, above every other window (the task bar
too), no button on the task bar, with the keyboard while it is up. A press anywhere outside it takes the keyboard
from it (`GLOS_EV_FOCUS`, down 0), and the toolkit closes it: a press on its own title in the bar closes it
without opening it again. One menu is open at a time. Choosing an item closes the menu first (`gui_close_later`:
a window isn't freed in the middle of its own event) and the command runs after (`gui_post_command`), so a
dialog it opens is modal over the right window. The choice widget's list is a menu.

A pop-up's place is on the screen: `GLOS_FN_PIXELS` gives a window's client area's corner from API 1.2, and a
menu that would cross the screen's right edge or the task bar moves left or opens upward.

**Commands, toolbars, tooltips and context menus** (toolkit 2, step C2; `docs/v1/toolkit-2.md` §1, §7).

- A **command** is set once for its window (`gui_command(win, id, "&Save", "Ctrl+S", icon, tooltip)`) and
  changed by `gui_command_enable`, `_check` and `_radio`. A menu's item (`gui_menu_command`) and a toolbar's
  button (`gui_toolbar_command`) show it and follow it; the shortcut is the command's, menu bar or none.
  The window's `on_update` is asked which commands can be used now before a menu opens, before a shortcut is
  looked up and before a toolbar is drawn; `on_command` is told the id wherever it came from.
- **Menus** have icons where their commands do, a tick or (for one of a group) a dot, and submenus
  (`gui_menu_sub`): the open menus are a stack, a choice closes them all, Left or Esc goes back one.
- **The toolbar** (`gui_toolbar`) is under the menu bar: icons, texts beside them if asked, a command with no
  icon as its text; what doesn't fit the window is in a menu behind the button at its end. It never has the
  keyboard.
- **A tooltip** (`gui_tip`; a toolbar's buttons have their commands') is a pop-up by the pointer once it has
  rested 600 ms, shown without the keyboard (`glos_show_quiet`).
- **A context menu** (`gui_context`) opens by the right button, after the press has chosen what is under the
  pointer, and by the Menu key or Shift+F10 by the widget with the keyboard; the widget is told first
  (`GUI_ON_CONTEXT`).
- **The status bar** has sections at its right end (`gui_status_section`): a text, an icon, a small progress bar.

### 1.6b Undo and the clipboard's formats (toolkit 2)

An app keeps its changes in a `gui_undo` history (each step: a name, undo and redo functions and their data;
typing bursts merge by a key); `gui_undo_commands` names its Undo and Redo commands after the steps and
`gui_undo_list` shows the history. The clipboard holds text, pictures, lists of files and private formats, several
at once (`gui_clip_start` ... `gui_clip_done`); a copy whose put fails changes nothing. Widgets are drag sources
(`gui_drag_source`) and drop targets (`gui_drop_target`); the virtual list and tree drag what is chosen in them
and show where a drop would go.

### 1.7 Modal dialogs

`gui_modal` shows a window and runs the loop until `gui_end_modal` answers it; meanwhile the app's other windows
hear no keys, presses or close requests (a press on one brings the dialog to the front), though they are still
drawn. Closing a dialog with its box or Alt+F4 is its cancel. A dialog is placed in the middle of its owner, on
the screen.

### 1.8 Files and the clipboard

The toolkit's few calls on files (`gui_dir_read`, `gui_drives`, `gui_fopen`, `gui_mkdir`, `gui_rename`...) take DOS
paths. On DOS they are the C library's. On the host a path's drive is a directory (`$GLOS_HOST_DRIVES/C`), so an
app shows the same names in the host's tests as on DOS. A name or path is DOS's bytes, in its code page (§1.10):
`gui_dirent`'s `name` is those bytes, for paths, and its `text` is the name to show. A folder is read sorted: folders first, then names
without regard to case. The drives are those DOS has (INT 21h 4408h; floppies from the BIOS's equipment word, not
touched to see).

The clipboard is the system's (`GLOS_FN_CLIP`, `glos clip`): text, 64 KB at most.

### 1.9 The look

Slate (PRD D7) carried into the client area: a grey face (CCCCCCh), white where the user types or chooses, lines a
pixel wide in black, the dark grey of the title bars (333333h) for what is chosen (with white text) and headers,
grey (999999h) for what can't be used now and a button held down, a darker grey (666666h) for a scroll bar's
trough. A button has a shadow a pixel deep. The focus is a dotted line. Orange stays the window manager's. Every
colour is one of the palette's cube, so a window is the same picture at 8 bits as at 16 and 32 (to the screen's
own precision).

**By the theme's roles** (toolkit 2, step C1a; `docs/v1/toolkit-2.md` §5). The colours above are Slate's, one of
three built-in themes (`[desktop] theme = slate | dark | contrast`). The toolkit reads the theme in force at
`gui_init` (`gui_theme`, API 2.0's THEME) and again when it changes: every window is then laid out and drawn
again, and an app that draws for itself is told (`GLOS_EV_THEME` through `gui_on_event`). Nothing is drawn with a
colour of its own:

| Name | Role | For |
|---|---|---|
| `GUI_FACE`, `GUI_TEXT` | FACE, FACE_TEXT | a window's background and buttons; text and glyphs on them |
| `GUI_FIELD`, `GUI_FIELD_TEXT` | FIELD, FIELD_TEXT | fields, lists, menus; text, ticks and the caret there |
| `GUI_LINE` | LINE | outlines and rules (never text) |
| `GUI_DIM` | DIM | what can't be used now; a button held down |
| `GUI_SHADE` | SHADE | a scroll bar's trough, a slider's track |
| `GUI_DARK` | DARK | strong fills (a progress bar's); text on it is `GUI_CHOSEN_TEXT` |
| `GUI_CHOSEN`, `GUI_CHOSEN_TEXT` | CHOSEN, CHOSEN_TEXT | what is chosen in a list, a tree, a menu, typed text |
| `GUI_MARK`, `GUI_MARK_TEXT`, `GUI_LINK` | MARK, MARK_TEXT, LINK | the accent; a link |

`GUI_ROLE(role)` gives any of API 2.0's roles (the desktop's, the editor's syntax colours). A colour kept in a
widget is kept as "the theme's" (`GFX_NONE` to `gui_label_style`), not as the value it had when the widget was
made. In Slate, text on a face, text in a field and lines are all black, and what is chosen is the title bars'
dark grey, so Slate's pictures are what they were before the roles.

### 1.10 Other scripts, DOS's code pages and smoothed text (M10 step G)

**The fonts** have Latin-1 and Latin Extended-A (U+0020-U+017F), then Greek and Cyrillic, the general
punctuation (dashes, quotes, the ellipsis, daggers, the per-mille sign), the euro, № and ™, and arrows. The
monospaced font also has what code page 437 draws: box drawing, blocks and shades, and the mathematical signs. The
glyph table is sparse (`struct gfx_range`, `tools/mkfont.py --ranges`), so the kernel's three fonts stay about 80 KB
each. A code point a font hasn't got is its box.

**DOS's code pages** (`glos/cp.h`, `sdk/config/cp.c`): DOS's names and a DOS program's output are bytes in the
code page DOS has. That is GLOSCP in the environment (a profile's `env` sets it for one program), else what
INT 21h 6601h says, else 437. GLOS has tables for 437, 850, 852, 866 and 737 (`tools/mkcp.py`, from Python's
codecs), and treats another page as 437. The toolkit shows a name through `gui_dos_text` and takes a typed one
back through `gui_text_dos`. A typed name with a letter the page hasn't got is refused, and not saved with a `?`
in it. The file manager, the file dialog, Notepad's and Images' titles and the terminal (its input line, its
programs' output and its title) all go through these. A file's text in Notepad stays Latin-1 (§1.5).

**Smoothed text** (`[display] font_smoothing`, off by default; when a program next starts). With it on,
`gui_init` reads `SMOOTH.DAT` from the program's folder (on the host, drive C's GLOS folder). Its glyphs are the
same ones in the same order, made anti-aliased by FreeType from the same DejaVu files when the 1-bit fonts are
made (`make fonts`, `tools/mkfont.py --smooth`): 4-bit coverage, 400 KB for the nine fonts (three sizes), of which
a program reads the three in force.

**Three sizes** (toolkit 2, step C1b; `[desktop] text = small | normal | large`, at once: GLOS tells every app as
for a theme). The normal size (DejaVu at 11 pixels, 14 rows) is built into each program. The small and the
large one (9 and 14 pixels; 12 and 17 rows) are in `FONTS.DAT` beside the program (112 KB; `tools/mkfont.py
--chunk`), read when the theme's text size asks for them; without the file the normal size stays. `gui_font`,
`gui_bold` and `gui_mono` are the same three objects from `gui_init` on, whose contents are the size in force,
so a widget that keeps one keeps "the UI's font". The toolkit's heights follow the font (a button its height
and 8, a field 6, a row 2), a tab has room for its title in bold, and a scroll bar's width is the theme's
(`GUI_SB`); with the normal size every widget is the size it always was.

`gfx_smooth` hands them to the 2D library, which blends them at 16 and 32 bits (integer sums, the same on host and
DOS). At 8 bits the text stays 1-bit. Pens still move by the 1-bit font's advances, so smoothed text takes exactly
the room it took before and nothing moves. The kernel's own text (title bars, the task bar) never smooths. A
`SMOOTH.DAT` that doesn't match the program's fonts (another build's) is ignored.

The plan (docs/milestones-m10.md) had stb_truetype drawing the TrueType files in each program. Coverage made at
build time was chosen instead, because the plan's way would have meant:
- floating point a 486SX hasn't got;
- no hinting, so other advances and every layout moved;
- 1.8 MB of fonts and a rasteriser in every program.

### 1.11 Icons by name (toolkit 2; the owner's choice of sets, 2026-11-16)

GLOS draws no icon set of its own: it takes two open ones, converted at build time (`make icons`,
`tools/mkicons.py`; the sets pinned in `tools/setup/versions.mk`, what is made from them kept in the repository).

- **Colour icons** (`gui_icon(name, big)`): FatCow's Farm-Fresh icons (CC BY 3.0), for files, folders, drives,
  programs and the desktop, at 16 and at 32 pixels. `sdk/gui/icons/colour.list` says which Farm-Fresh icon each
  GLOS name is (`folder`, `file-text`, `drive-floppy`, `app-files` ...); a name is added there when a program
  needs one (the set has 3,926; icons that carry other companies' logos are not to be listed). They are in
  `ICONS.DAT` beside the programs (1.3 KB an icon, read when first asked for), reduced to the palette's cube
  with hard edges, so an icon is the same picture at every depth and in every theme.
- **One-colour glyphs** (`gui_glyph(name)`): Font Awesome Free's solid icons (CC BY 4.0), for toolbars and menus:
  16-pixel masks in the toolkit itself (`sdk/gui/glyphs.c`; `sdk/gui/icons/glyphs.list`: `save`, `cut`, `undo`
  ...). A glyph is given wherever a 16-pixel icon is taken (a command's, a row's, a node's, a status section's)
  and is drawn in the colour of the text there: the theme's, greyed while the command can't be used, the chosen
  text's on a chosen row. So a toolbar is right in the dark and the high-contrast theme, which a colour picture
  can't be.
- **Credit** is the licences' condition: `THIRD_PARTY.md` and `README.md` name both sets, with their links and
  what was changed; the About box of GLOS's shell is to say the same (phase D).

## 2. How the toolkit and the apps are tested

The toolkit and every app build for the host too: the app's `main` renamed `app_main`, linked with
`tests/host/glosshim.c` in place of libglos. The shim is a desktop in memory: windows of pixels in the screen's
format (8, 16 or 32 bits), placed as the window manager places them, the focus moved as it moves it (a pop-up
loses it to any press outside), keys turned into characters by the kernel's own table (`kernel/include/keymap.h`,
shared), Alt+F4 a close request.

A **test script** (`tests/gui/NAME.txt`) says what to do: run the app with arguments, wait for a window, keys
(`ctrl+s`, `alt+f`, `shift+tab`), typed text, clicks and drags at a window's client coordinates, pauses, the
clipboard's text, the pointer moved with no button (`move`, for a tooltip), the desktop's theme or text size
changed under the app (`theme NAME`, `text SIZE`), two shots that must be one picture (`same A B`), and shots: a
window's client area by its title. On the host the shim plays the script and
writes each shot as a PNG (`make gui-goldens`: build/host/gui/SCRIPT/NAME-BITS.png, at 8, 16 and 32 bits). In Loop A
(`tests/loopa/jobs.py`'s `GuiDriver`) the same script is played over ssh on a GLOS desktop: the app started as an
agent job, keys through `glos key`, text through `glos type`, the pointer moved by `glos mouse` to the window
`glos desk` says is there, and each shot cut from `glos shot` and compared with the host's, byte for byte, a few
times while the app catches up. Lines the app writes are checked when it has ended (`expect`).

The pictures are the same on both because they are drawn by the same code from the same fonts into the same
formats; what differs between a host and a GLOS desktop (where a window is put, the time) is kept out of them.

What a picture can't hold the same on both is masked: a shot may name rectangles of the client area (`shot NAME
"TITLE" X Y W H ...`) that both sides paint black before comparing, as the monitor's figures and the file
manager's dates are. `quick` sends keys that open no menu in one `glos key` (the Loop A driver otherwise waits
after each Alt and letter for the menu it may open). `make gui-goldens` also checks each script's `expect` lines
against what the host run printed, except in a script marked `#loopa-only` (the terminal's programs, which only
Loop A runs).

The shim stands in for the kernel where an app asks it about the machine: the settings are a GLOS.CFG in memory,
read from `$GLOS_HOST_DRIVES/C/GLOS/GLOS.CFG` and checked by the same schema (`sdk/config/cfg.c`) as the kernel
checks them. A script's `config SECTION KEY VALUE` lines before its `run` change them for that script, and Loop A's
driver sets them with `glos config set` and unsets them afterwards. A `[program NAME]` section's `env` reaches the
program's environment on the host as GLOS gives it at the EXEC; `GLOS_FN_STAT` gives made-up figures that follow the script's clock (two apps, a job, a log), and
one of the apps can be ended. Neither runs programs.

The Control Panel's every setting is changed by keys in the `control` suite: it reads the schema from
`sdk/config/cfg.c`, writes a script that changes each setting from its default (Alt and the label's letter, the
value typed or chosen), plays it, and checks `glos config get` and the file the kernel wrote.

## 3. The apps (`apps/`)

Each is one DJGPP EXE beside GLOS.EXE, on the toolkit, with a script in `tests/gui` (its goldens and effects):

| App | What it does | Script |
|---|---|---|
| `FILES.EXE` | The desktop's shell (PRD D55): drive, app and program icons, the launcher, a window for each folder (spatial; places in FOLDERS.CFG), the browser (`-browse`, a tree and a list), copy, move, delete, rename, new folders, opening by `[open]`, watching | `files`, `files-browse` |
| `NOTEPAD.EXE` | Text files of a megabyte: find, go to, wrap, undo, the clipboard, DOS's CR LF kept | `notepad` |
| `CALC.EXE` | A calculator: keys and presses, memory, square root, errors | `calc` |
| `IMAGES.EXE` | PNG, JPEG, BMP, GIF, TGA, PNM (stb_image) and PCX; fitted, zoomed, the next in the folder; dithered at 8 bits | `images` |
| `TERM.EXE` | GLOS's shell (D50) in a terminal: history, completion, `&&`, `\|\|`, redirection; DOS programs as console jobs (D51, supervisor.md §17.10), type-ahead kept; redraws only the rows that changed | `term`, `term-run` |
| `MONITOR.EXE` | Programs (End), performance (the processor over a minute, memory, the network), the system, GLOS's log; `-tasks` for Ctrl-Alt-Del | `monitor` (its figures masked) |
| `CONTROL.EXE` | The Control Panel, drawn from the settings' schema (D53): a page per section, programs' profiles, kinds of file; everything checked before anything is set; a new screen mode tried for fifteen seconds | `control`; every setting by keys: the `control` suite |
