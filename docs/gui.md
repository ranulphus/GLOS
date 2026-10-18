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
the scrolling area (a box taller than the room it has, and a bar). Every widget measures the size it wants from
the fonts; `gui_size` sets one instead. A dialog's window is the size its widgets want (`GUI_W_DIALOG`), so
every dialog fits 640×480 (PRD D34) by construction.

A window's layout is done again when something asks (a widget added, removed, shown, hidden, a new text that
changes a size; `gui_relayout`) and before it is drawn; then the whole window is damaged.

### 1.3 The keyboard and the mouse

Keyboard-first (PRD D34):
- **Tab** and **Shift+Tab** move the focus through the widgets that take it, in the order they were made
  (radio buttons: the chosen one of a group; arrows move within it); **Ctrl+Tab**, **Ctrl+PgUp** and
  **Ctrl+PgDn** change tabs;
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
| list | rows of cells under a header that sorts; 16-pixel icons; several rows chosen with Ctrl and Shift; typing a name's first letters goes to it; or 32-pixel icons in a grid with their names (the file manager's folders) |
| tree | nodes that open and close, their children added by the app when one first opens |
| choice | a button that drops its list of items down |
| spin box, slider | a number in a range |
| tabs, group, form, scrolling area, boxes, space, separator | containers and spacing |
| menu bar, menus | menus are pop-up windows (below); items with letters, shortcuts, checks; separators |
| progress bar, image, canvas, status bar | a canvas is the app's to draw in and has its events |
| dialogs | message (OK, OK and Cancel, Yes and No, Yes No and Cancel), a line to type, open and save a file |

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
made (`make fonts`, `tools/mkfont.py --smooth`): 4-bit coverage, 129 KB for the three fonts.

`gfx_smooth` hands them to the 2D library, which blends them at 16 and 32 bits (integer sums, the same on host and
DOS). At 8 bits the text stays 1-bit. Pens still move by the 1-bit font's advances, so smoothed text takes exactly
the room it took before and nothing moves. The kernel's own text (title bars, the task bar) never smooths. A
`SMOOTH.DAT` that doesn't match the program's fonts (another build's) is ignored.

The plan (docs/milestones-m10.md) had stb_truetype drawing the TrueType files in each program. Coverage made at
build time was chosen instead, because the plan's way would have meant:
- floating point a 486SX hasn't got;
- no hinting, so other advances and every layout moved;
- 1.8 MB of fonts and a rasteriser in every program.

## 2. How the toolkit and the apps are tested

The toolkit and every app build for the host too: the app's `main` renamed `app_main`, linked with
`tests/host/glosshim.c` in place of libglos. The shim is a desktop in memory: windows of pixels in the screen's
format (8, 16 or 32 bits), placed as the window manager places them, the focus moved as it moves it (a pop-up
loses it to any press outside), keys turned into characters by the kernel's own table (`kernel/include/keymap.h`,
shared), Alt+F4 a close request.

A **test script** (`tests/gui/NAME.txt`) says what to do: run the app with arguments, wait for a window, keys
(`ctrl+s`, `alt+f`, `shift+tab`), typed text, clicks and drags at a window's client coordinates, pauses, the
clipboard's text, and shots: a window's client area by its title. On the host the shim plays the script and
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
