# Phase E, as built: the core apps, upgraded

The route (`docs/v1-route.md`, E) and each app's plan (`docs/v1/apps/`) say what is wanted; this says what was
built, in the order it was built, and what is left. The owner chose phase E on 2026-11-18, with phase D's large
leftovers left where `phase-d.md` lists them.

## The Calculator (2026-11-18)

Plan: `docs/v1/apps/calculator.md`. Three modes, chosen in View; a mode's change makes the window anew.

- **The arithmetic is apart from the window** (`apps/calc/calcmath.c`), so that the host tests it against tables
  (`tests/host/calc_test.c`, about a hundred rows): expressions with the usual order of operations, brackets,
  powers (right to left), signs, `pi` and `e`, and functions by name with a bracketed argument; why an
  expression has no value (not one, a division by nothing, out of a function's range); and the programmer's
  words of 8, 16, 32 and 64 bits.
- **Standard:** as it was, its window too (the tests press its keys by their places). One old fault mended: an
  operand that came without typing (a root's result, the memory, the clipboard) after an operation was not
  counted as one, so `9 + 16 sqrt * 2` lost its `+ 4` and gave 18; it gives 26.
- **Scientific:** the keys build an expression, shown over the display and worked out by `=`. A function key
  (sin, cos, tan, the hyperbolic ones, ln, log, exp, x², n!, cbrt, abs, the root, 1/x) works on the number in
  the display at once, or on the bracket just closed (its name goes before the bracket). Inv turns sin, cos and
  tan into their inverses, ln into exp, log into 10^x and x² into the root. Deg goes round degrees, radians and
  grads. pi, e, x^y, Mod, and Exp for a number's exponent. Doubles, shown to 15 digits (the plan's 80-bit
  floats would want long double functions of the C library's, which DJGPP's hasn't all of).
- **Programmer:** a word of 8, 16, 32 or 64 bits, signed or not, typed and shown in base 16, 10, 8 or 2; all four
  are under the display, and the word's bits, which a click turns over. AND, OR, XOR, NOT, shifts, rotates, the
  four operations and modulo, each as it comes, as the standard mode does them (no order of operations here).
  A digit the word has no room for is not taken.
- **History** (View): the calculations done, beside the keys, the standard mode's as they were typed and the
  scientific mode's expressions; a click on one takes its result again. Thirty are kept while the program runs.
- **Kept between runs:** the mode, the history's place, the angles' unit and the programmer's word, in
  `CALC.CFG` beside the program (GLOS.CFG's settings are GLOS's own: an app has no keys there). `CALC -n`, `-s`
  and `-p` start in a mode whatever is kept.
- **Keys:** as the program's first comment lists them: the standard ones as ever; `( ) ^ s c t l n p ! i m` in
  the scientific mode; `a`-`f`, `& | ^ ~ < > %`, F5-F8 (the base) and F12, F2, F3, F4 (the size) in the
  programmer's.
- **Tests:** `calc_test` in the host run; GUI scripts `calc` (the standard mode, as before), `calc-sci`,
  `calc-prog` and `calc-hist`, on the host and on Loop A (three machines).
- **Not done:** units (optional in the plan); the programmer mode has no memory keys and no history.

## The Terminal (2026-11-18): tabs

Plan: `docs/v1/apps/term.md`. The first of its parts.

- **Tabs:** each is a terminal (the toolkit's widget) with a shell's session of its own, named by the title its
  programs set ("Shell 2" until one does); the window's title is the shown tab's. The strip's "+", File's New
  Tab or Ctrl+Shift+T opens one; a tab's close box, Close Tab or Ctrl+Shift+W closes it and ends its session;
  EXIT in a shell closes its tab, and the last tab's end is the Terminal's, as it was. Eight at most. The
  window is as much larger as the strip takes: a terminal is 80 by 25 still.
- **-log** (the suites') writes the first tab's lines; its last words are written when the window closes, before
  its widgets go (Alt+F4 with -log once read a terminal already freed).
- **Tests:** Loop A `term-tabs` (a second tab, a command in it, closed by the key; a third, closed by its
  shell's EXIT; the first tab's shell and what it kept as they were), with `term`, `term-run` and `terminal` as
  before, on three machines. On the host the strip is in `term`'s pictures; the host's shim has one shell's
  session, so `term-tabs` is GLOS's alone.
- **Saved connections** (File's Connections..., Ctrl+Shift+O): a list of them and a form (a name, a host, a
  user, a port); Save keeps the form's (a new one, or the one of that name again), Remove takes the chosen one
  away, Connect opens a tab and types the SSH command into its shell (`ssh -p 2222 glos@host`): SSH.EXE does the
  rest, its keys and its questions included, so nothing secret is kept. They are in `TERM.CFG` beside the
  program (`connection NAME<tab>HOST<tab>USER<tab>PORT` a line). A word with a space, a quote or a shell's sign
  in it is refused (it goes into a command line).
- **Tests:** `term-conn` on the host and on Loop A (one made and saved, in the list; Connect's tab; the name is
  one no network has, `nohost.invalid`).
- **Not done yet:** a connection's key and its terminal settings, the Start menu's jump list and `ssh NAME`; the
  SFTP pane; the scroll-back's length and search; fonts and colour schemes; more of xterm (256 colours, the
  mouse, bracketed paste); splits; tabs dragged along the strip.

## Notepad (2026-11-18): tabs

Plan: `docs/v1/apps/notepad.md`. The first of its parts.

- **Tabs:** a tab for each open file (the toolkit's code editor in each), with its name and a `*` while it has
  changes not saved; the window's title is the shown one's, as it was with one file. New makes an empty tab;
  Open opens the file in a tab of its own (in the empty Untitled, if that is all there is; a file open already
  is only shown); Close (Ctrl+W, a tab's close box) asks about its changes and takes it away, and the last one's
  leaves an empty Untitled; Exit asks about each changed one in turn, showing it. `NOTEPAD FILE...` opens each
  in a tab. Ctrl+Tab goes round; the strip's "+" is New. Twelve at most. Word Wrap is every document's.
- **A fault of the toolkit's, mended:** the code editor took Ctrl+Tab for Tab, so a window's tabs could not be
  gone round from inside one (`sdk/gui/code.c`). Loop A showed it; on the host the script's `wait` passed with
  the title never come (a host `wait` does not fail for an app without threads), so the script now has a shot
  of the tab it expects.
- **Tests:** `notepad-tabs` on the host and on Loop A (two files as two tabs, New, round by Ctrl+Tab, a changed
  one closed and asked about, a file open already shown again, the last one's close); `notepad` as before; the
  scripts that start Notepad (`monitor`, `monitor-switch`, `monitor-windows`, `wrap`, `control-startup`).
- **View** (the second part): **Line Numbers** shows the code editor's margin (numbers, and fold boxes for what
  is indented under a line); **Highlighting** (on as a rule) colours a file by its kind, which its name's end
  says: C (the toolkit's lexer), batch, INI and GLOS.CFG, assembly, Pascal and HTML (`apps/notepad/lexers.c`:
  a lexer gives each byte of a line a class and the state the line ends in, a comment still open). The kind is
  in the status bar. A file saved by another name gets that name's kind. Both are every document's.
- **Tests:** `lex_test` in the host run (each lexer's lines against tables, comments over two lines among them);
  `notepad-view` on the host and on Loop A (C typed and saved as T.C, the margin, a batch file, plain again).
- **Find and Replace** (the third part) are dialogs, with **Match case** (kept): Find's Enter finds the next and
  F3 goes on; Replace's replaces every one and says how many. (A dialog's fields are read while it is there,
  in its button's handler: after `gui_modal` has come back their text is not to be had.)
- **Keep Session** (View; off as a rule): the open files' names in their order, and View's and Format's choices,
  are kept in `NOTEPAD.CFG` beside the program, and a Notepad started with no file opens them again. With it off
  a bare Notepad is an empty one, as the other programs' tests expect.
- **Tests:** `notepad-find` on the host and on Loop A (two replaced; "the" is not "The" with Match case);
  `notepad-session` on Loop A alone (two files kept and opened again; off again: an empty Notepad).
- **Find and Replace on the toolkit's dialog** (the fourth part, 2026-11-19), which stays up beside the text:
  Find next and previous, Replace, Replace all; Match case, **Whole words** and **Regular expressions** (the
  toolkit's matching, as every app's Find is to have). Notepad's own two dialogs, of the day before, are gone.
  The editor has a find by those rules for it (`gui_code_find_ex`, and `gui_code_sel` for what is chosen).
- **Tests:** `notepad-find` anew on the host and on Loop A (two replaced; "the" is not "The"; "start" is not
  "starts", said in the dialog; `st.rts` found); `notepad` closes the dialog by Esc before F3.
- **Not done yet:** files dropped on the window; bookmarks; Markdown's colours and a menu to choose a kind;
  the results pane, find in files; code pages; large files.

## Files (2026-11-18): the browser's toolbar and address bar

Plan: `docs/v1/apps/files.md`. The first of its parts, in the browser (`FILES -browse`; the folders' own windows
are as they were).

- **The toolbar** (the toolkit's, with its commands and one-colour glyphs): Back, Forward, Up, Refresh; New
  Folder; Cut, Copy, Paste, Delete; Large icons, Details. Each has its tip; Back, Forward and Up are grey when
  there is nowhere to go.
- **The address bar:** the folder's path, to type into (Ctrl+L goes to it with its text chosen); Enter goes to
  the folder typed, a path that is no folder is said and the bar says the folder shown again; the folders gone
  to are kept in its list.
- **Back and Forward** (Alt+Left, Alt+Right, Go's menu): along the way the browser came, sixteen folders long;
  a folder gone to from the middle of it forgets what lay ahead.
- **Two things of the toolkit's learnt:** `gui_editor_command` is the many-lined editor's alone (given a field
  it writes over the widget); and a combo dropped its list again at Enter when the app put the text as it
  wanted it (an address bar's path in capitals): Enter never drops it now (`sdk/gui/field.c`).
- **Tests:** `files-address` on the host and on Loop A (a path typed, Back, Forward, Up, a path that is none);
  `files-browse`, `files` and the desktop's scripts as before.
- **Properties** (the second part; Alt+Enter, File's menu, the context menu): the chosen file's or folder's
  name, where it is, its kind, its size (a folder's: its files' number and size in all, counted now, to five
  thousand files and eight folders deep), when it was last written and its attributes; with nothing chosen,
  the folder shown and its drive's room.
- **The list's context menu** (the right button, the Menu key, Shift+F10): Open; Cut, Copy, Paste; Delete,
  Rename; New Folder, Properties. In the folders' own windows too.
- **Tests:** `files-props` on the host and on Loop A (a folder's, a file's, and the same dialog from the context
  menu; the dates and attributes are the copy's, and masked).
- **Find Files** (the third part; Edit's Find..., Ctrl+F, the toolbar): files and folders by name under the
  folder shown: a name with `*` and `?`, or a piece of one. What is found is listed with its folder; Go To
  (Enter, a double click) shows the chosen one in its folder, chosen there. Looked for at once, to two hundred
  found and five thousand looked at ("and there may be more").
- **Tests:** `files-find` on the host and on Loop A (a wildcard's four, one gone to, a piece of a name).
- **A paste onto a name that is there already is asked about** (the fourth part): Replace (a file: written
  over; a folder: what is copied goes into it, over its files of the same names), Skip, Keep Both (the copy
  gets a free name of DOS's kind, `LETTER~1.TXT`), or Cancel (nothing of the paste is done); "Do this for all"
  answers for the rest of the paste. Until now a paste wrote over what was there without a word. A copy into
  its own folder gets a free name too (it took `~1` whatever was there); a move into its own folder does
  nothing; a folder is not put into itself.
- **Select All did nothing** in Files: it sent the list Ctrl+A, and the toolkit's list had none. A list that
  lets several rows be chosen takes Ctrl+A now (`sdk/gui/vlist.c`).
- **The tree follows** the folder shown, however it was gone to (the address bar, Back and Forward, Up, a
  folder opened in the list, Find's Go To); it only did when the tree itself was what was clicked.
- **Tests:** `files-clash` on the host and on Loop A (two files copied, pasted again: Keep Both for all; again:
  Skip one, Replace the other; then it deletes what it made: Loop A runs the scripts on one disk, in their
  names' order, and the ones after it count the folder's files).
- **A paste's work is a job in the background** (the fifth part): the copying and moving is done on a thread of
  the toolkit's (`gui_task`), so Files' windows, the desktop among them, go on answering. Every question (a
  name that is there already) is asked first; Cancel there now means nothing of the paste is done. What there
  is to copy is counted (kilobytes), for a bar and "About N seconds left". A job that has run a second has a
  window of its own ("Copying", "Moving": the item in hand, the bar, the time left, Cancel), which is no dialog
  over anything: the folder may be closed and the work goes on, and Files itself stays until its jobs are
  done. Cancel stops after the piece of a file in hand; a file cut short is taken away (so is one that could
  not be written whole: a piece of a file was left before).
- **Files is the first app with threads:** linked with the C library's lock (`@build/sdk/gloswrap.opt`; the
  Makefile's `APP_LINK_files`), its map checked by `tools/wrapcheck.py`. The toolkit's file helpers hold that
  lock over `localtime` in a program with tasks (`gui_libc_hook`): a folder read on a task's thread and one
  read on the window's would otherwise have each other's dates.
- **Tests:** every script that pastes goes through a job now (`files`, `files-clash`); `files-job` on Loop A
  (`FILES -browse DIR -slowcopy` waits ten seconds before each file: the job's window comes, Esc stops it,
  nothing was copied).
- **Delete is a job too** (the sixth part), with its window ("Deleting") when it takes more than a second:
  a folder of thousands of files no longer holds the desktop still. Cancel stops between two files; what has
  gone has gone.
- **A copy is as old as what it was copied from** (its date and time, as DOS's COPY leaves them); it was as
  new as the copying. (`gui_set_time` holds the C library's lock in a program with tasks: `utime` is not among
  the functions the lock is put round.)
- **Not done yet:** the rest of the plan: views
  (small icons, a list, resized columns), folders that load as they go, long names, "open with" and apps'
  entries in the context menu, archives as folders, a search that goes on in the background and looks inside
  files, a folder's size counted in the background, thumbnails, drag and drop, the Recycle folder.

## Images (2026-11-19): the viewer's toolbar, turning, details, slideshow

Plan: `docs/v1/apps/paint.md` (Images stays the viewer; Paint is the editor to come). The first of its parts.

- **A toolbar:** Open, Previous, Next; Zoom in, Zoom out, Fit; Turn left, Turn right; Slideshow, Details.
- **Turned and flipped** (Image's menu; L and R): a quarter turn either way, flipped across or over. It is the
  picture in memory that turns, not the file: saving is Paint's, and the plan's turn of the file itself
  (a JPEG's without loss) waits for the encoders that come with it.
- **Details** (File's menu, Alt+Enter): the picture's name and folder, its kind, its size in pixels, the file's
  size and date, how large it is shown.
- **Slideshow** (View's menu, S): the folder's next picture every four seconds, round and round; S again or
  Esc stops it, and so does Next or Previous by hand. The status bar says while it runs.
- **Full screen** (View's menu, F11): the picture alone on the whole screen, fitted, GLOS's task bar gone
  while it is there (a second window, bare, shown `GLOS_WS_FULL`). Esc, F11 or Enter comes back; Next,
  Previous, turning and the slideshow work there as in the window.
- **Tests:** `images-view` on the host and on Loop A; `images` as before (its pictures have the toolbar now).
- **Edit** (E; with Paint, below): the picture shown is opened in Paint.
- **Not done yet:** the filmstrip of the folder's pictures; the picture kept as tiles (any size
  memory allows).

## Paint (2026-11-19): the editor's first part

Plan: `docs/v1/apps/paint.md`. A new program, `PAINT.EXE`, in the launcher and the Start menu, and Images'
**Edit** (E, its File menu, its toolbar) opens the picture shown in it.

- **The picture and its tools are apart from the window** (`apps/paint/paintdoc.c`), so the host tests each
  tool against small pictures written out as text (`tests/host/paint_test.c`). A picture is layers (one, so
  far: the layers' panel is to come) of AARRGGBB pixels, shown one over another on a chequered ground.
- **Tools** (the toolbar, the Tools menu, a key each): pencil (P), brush (B), eraser (E), line (L), rectangle
  (R), ellipse (O), flood fill (F), colour picker (I). The left button draws in the first colour, the right in
  the second; the eraser always in the second, the paper's. The brush's, eraser's and shapes' width is the
  Tip box's (`[` and `]`); **Shapes Filled** fills rectangles and ellipses with the other colour. A shape is
  drawn anew as it is dragged out.
- **Colours:** the two in hand (each a button that opens the colour dialog) and a palette of 28: a click takes
  the first colour, a right click the second.
- **Undo and Redo** of every step (the toolkit's history: "Undo Fill"), a step being the pixels a tool changed,
  before and after; eight megabytes of them are kept.
- **Files:** New (its size asked), Open (every kind Images shows), Save and Save As: PNG, BMP or JPEG by the
  name's end. Changes not saved are asked about. Reading and writing are one code in Images and Paint
  (`apps/shared/picfile.c`; writing by stb_image_write, new in `third_party/stb`).
- **The view** is the toolkit's image view (zoom by + and -, the wheel with Ctrl; the middle button moves the
  picture). One thing of the toolkit's was in the way: an image view took the left button to move the picture
  before the app saw it; `GUI_CV_TOOLS` (`gui_cview_style`) leaves that button to the app.
- **Eight glyphs more** in the toolkit's set, for the tools (`sdk/gui/icons/glyphs.list`; `make icons`).
- **Tests:** `paint_test` in the host run; `paint` on the host and on Loop A (a drawing session with every
  tool, undo and redo, the picture saved as a PNG and opened again: the same picture); `images-edit` on Loop A.
- **The whole picture turned and flipped** (Image's menu: Turn Left, Turn Right, Flip Across, Flip Over), every
  layer; undone by the turn the other way, with no pixels kept for it. `paint` turns the opened picture and
  undoes it (the picture as it was), and answers Paint's question about changes not saved.
- **Resize and Canvas Size** (Image's menu): the picture scaled to a size (smooth: each new pixel from the four
  old ones round where it falls; or by the nearest), or kept as it is on a canvas of another size, at one of
  nine places, the new part in the second colour. The picture before is kept whole for undo.
- **Not done yet:** layers' panel and blend modes; selections and crop; text; gradients, curves, the clone
  stamp; resizing in proportion and bicubic; adjustments and effects; the clipboard; PCX and the layered format
  written; tiles for pictures of any size.

## Faults of the toolkit and the suites that phase E's gates found (2026-11-19)

- **Keys typed ahead of an opening menu** went to the window behind it: GLOS sends a key to the window that has
  the keyboard as it is pressed, and a menu's pop-up has it only once the app has made and shown it. Alt+T and
  then D at once, on a slow PC, chose nothing and left the menu open (Paint's suite on the 486). A key that
  comes for a window whose menu is open is the menu's now (`sdk/gui/core.c`, `gui_menu_popup_of`).
- **An app's idle functions ran after its last window was freed:** the loop's turn that took GLOS's close
  (with no `on_close` of the app's) freed every widget and then ran what was due. Monitor, which samples
  every second, died there now and then (exit code 255, no last line): the twelfth gate's `monitor-windows` on
  the 486, and likely others before it taken for lost keys. Idle functions and timers run only while the loop
  goes on.
- **`gui_modal` closes and frees its window**, which the header did not say; eight dialogs (seven written in
  this phase after the Control Panel's old one) closed theirs again, reading freed memory. No harm was seen
  from it; they no longer do, and `gui.h` says it.
- **The suites:** a GUI script that fails now reports each app's exit code and what it wrote to stderr; a
  script waits for a full-screen program's end by `settle` (no DOS program running) where it paused a fixed
  time (a session is 8.6 seconds on the G400's machine: `term-run`, the twelfth gate); the batch case waits
  for the log's line that names FreeDOS's COMMAND.COM where it looked once (the 486, the twelfth gate).

## What is next

More of Paint (layers, selections, the image's size, adjustments); the rest of Files' plan (views, archives,
thumbnails); Images' filmstrip; what is left of the Terminal's and Notepad's plans. Each by its plan in
`docs/v1/apps/`.
