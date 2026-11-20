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
- **Bookmarks** (Edit's menu): Ctrl+F2 puts one on the line or takes it away, F2 goes to the next round the
  text's end, Shift+F2 to the one before. They are the editor's margin marks, which move with their lines as
  the text is edited (the editor is asked where they are: `gui_code_marks`, `gui_code_mark_next`); the first
  one turns Line Numbers on, so that they show. They are not kept with the file.
- **Tests:** `notepad-book` on the host and on Loop A.
- **Markdown, and Highlight As** (2026-11-19): Markdown has its colours (headings, lists' and quotes' marks,
  rules, code in spans and fenced over lines, emphasis, links; `.MD` files), and View's **Highlight As** chooses
  the shown document's kind by hand, or By the File's Name again: a file with no name, or one whose name says
  nothing, can be coloured.
- **Tests:** `lex_test` (Markdown's lines, the kinds' list); `notepad-kind` on the host and on Loop A.
- **Not done yet:** files dropped on the window; the results pane, find in files; code pages; large files;
  bookmarks and the chosen kind kept with the session.

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
- **Archives as folders** (the seventh part, 2026-11-19): an archive's file is gone into as a folder is. In
  the browser, Enter or a double click on `PACK.ZIP` shows what is in it, and the address bar says
  `C:\DOCS\PACK.ZIP\TEXT` for a folder inside (typed there too; Up and Back go out of it again). What is in it is
  listed with its sizes and dates; a file of it is opened from a copy unpacked beside FILES.EXE (`ARCTEMP`,
  emptied when Files starts); Properties says what a file comes to packed. Copy and a paste elsewhere unpack
  what was chosen, a folder with all in it, as a job in the background like any paste; **Extract Here** (File's
  menu, the context menu) unpacks a whole archive into a folder of its name.
  - **A ZIP is changed from here too:** files and folders are pasted into the folder of it that is shown (one
    of that name there already is asked about, as on a disc: Replace, Skip, Keep Both), a folder is made in it
    (New Folder), what is chosen is deleted from it, and **Add to ZIP** (File's menu, the context menu) makes a
    new ZIP of what is chosen, named after it. All of it is a job in the background. A paste is all or nothing:
    if it is stopped, or the disc fills, the ZIP is as it was (a new one: not there); what a paste replaces goes
    only once the new is in. Rename and Cut are grey inside an archive, and a TAR or a GZ is only read.
  - **Read:** ZIP (stored and deflated; one behind a program that unpacks it; sizes after the bytes), TAR
    (ustar, GNU's and pax's long names), GZ, and a TAR in a GZ. Told by the file's bytes; gone into by its
    name's end (`.ZIP .TAR .GZ .TGZ`). An archive that is damaged, cut short, encrypted or packed in a way not
    read here is said so, in a word (`arc_error`).
  - **Names:** an archive's own names are shown as they are. Unpacked, each part of a name becomes one DOS can
    have (eight characters, a dot and three; `a long file name.text` is `ALONGFIL.TEX`), and the later of two
    that would be alike in one folder is numbered (`ALONGF~1.TEX`), the same however much of the archive is
    unpacked. A name that would leave the folder it is unpacked into (`..\..\AUTOEXEC.BAT`) is not listed at
    all.
  - **The code** is `apps/shared/arc.c` (the reader; a ZIP's writer too, below) over the loader's inflate
    (`loader/inflate.c`, whose state is now a structure the caller may give: two archives are unpacked at once
    by an app's threads). In an app with threads only the C library's functions that libglos's lock is round
    touch a file there.
  - **ZIPs written** (`arcw_*`, `arc_zip_remove`; `apps/shared/deflate.c` packs): a new archive, entries
    added to one, entries taken out (the archive written again beside itself without them, their packed bytes
    as they are, and put in its place); anything that fails (the disc full) leaves the disc as it was. An
    archive's file is open only while it is read or written, so a window may show one that is being changed.
    The packer finds
    matches through a hash of three bytes in the last 32 KB and writes each block with codes of its own, the
    fixed ones, or as it is. The test's 27 inputs (3.5 MB of made-up text, noise, runs and program-like
    bytes) come to 1.15 MB at its quick level and 1.08 MB at its better one; zlib makes 1.24 MB of them at
    its level 1 and 1.11 MB at its level 6. Its speed on a 486 is not measured yet.
  - **Tests:** on the host, `arc_test` (39 archives made by Python's zipfile, tarfile and gzip against a list
    of what each must be read as: every entry's name, size, date and bytes, one by one and in one walk; the DOS
    names; then 1,360 archives with bytes changed, cut or moved, under the address and undefined-behaviour
    checkers while it was written), `deflate_test` (27 inputs packed at both levels, however they are given,
    unpacked again here and by zlib) and `arcw_test` (archives written, added to, given up, failed for want
    of room, entries taken out; read back here, by Python's zipfile and by `unzip -t`). `files-arc` on the host
    and on Loop A (a ZIP gone into, a folder and three long-named files pasted out and checked byte for byte,
    a TAR in a GZ extracted and gone into, a cut-short ZIP refused, the address bar into a folder of one);
    `files-arc-open` on Loop A alone (a file opened from an archive: Notepad has it); `files-zip` on the host
    and on Loop A (a folder packed by Add to ZIP, a file pasted in and pasted again over itself, a folder made,
    a file deleted, then all of it unpacked again and read back byte for byte). The script found that a
    paste's new entry was taken out again as "the old one" when there was no old one.
  - **A folder shown is not listed anew for every file a job writes** (found by these scripts on the 486's
    profile, where two small files took more than a second to pack, and plain copies as long): each file
    written changed the watched folder, and its listing, made again each time, held the job up. While a job is
    at work a changed folder is listed again once a second at most, and at the job's end.
  - **A file is asked after by DOS's own find, not the C library's `stat`** (the toolkit's `gui_stat`,
    `gui_is_dir` and `gui_dir_read` on DOS: `sdk/gui/files.c`). A job says how long it took (`FILES job
    start=... count=... work=... told=...`, milliseconds), and on the 486's profile that showed where a small
    job's second went: not the threads (a few milliseconds) but the files' own calls. DJGPP's `stat` makes a
    dozen DOS calls for a name and opens every file to see whether it is a program: 45 ms a name there, once
    for every name in a folder listed and two or three times for every file of a job. One find call says the
    same (attributes, size, date, time). Measured there, before and after: a paste's counting 0.49 s to 0.11;
    Add to ZIP 1.47 s to 0.39; three files pasted out of a ZIP 1.67 s to 0.47; a TAR in a GZ extracted 1.78 s
    to 0.82. No job of the scripts takes a second there any more. What is left is the library's `open` (it
    walks the path for symbolic links) and the cost of a DOS call from an app's thread, some 4 ms each on
    that profile: to be measured apart (an app's call goes to the system VM's thread and back).
  - **The scripts wait for a job's end by what Files says of it** (`said LINE`, new in both players: the
    host's reads its own output's file back, Loop A's reads the app's output as it comes), and then for its
    window's going (`gone "Packing"`), where the older scripts pause a second and a half: on the 486's profile
    that was not enough for these, and the keys typed while a job's window was going were nobody's.
  - **Not yet:** a thing of one archive pasted straight into another (it is unpacked to a folder first),
    renaming inside a ZIP, "Extract To...", LZH/LHA, ARJ and 7z, the old ZIPs' implode and shrink, big archives
    listed as they load (a TAR in a GZ is unpacked from end to end for its list, with the window waiting),
    names by long file names where DOS has them, the packer's speed on a 486 (unmeasured: its quick level is
    there for it).
- **The Recycle folder** (the eighth part, 2026-11-20; `apps/files/recycle.c`): what is deleted in Files is not
  gone at once. Del asks "Move ... to the Recycle folder?", and the thing goes to its drive's own
  (`X:\RECYCLE.GLS`, under a name of the folder's own, with a line in its `INDEX` saying where it was and when
  it went): renamed there, so it is at once, whatever its size. **Go's Recycle Folder** and the desktop's
  **Recycle** icon open its window: every drive's, with where each thing was; **Restore** puts one back where
  it was (its folders made again if they have gone; beside what has its name now, if something has),
  **Delete** takes one out for good, **Empty** all of them.
  - **For good at once:** Shift+Del (File's Delete for Good); a floppy's files (drives A and B have no such
    folder); what is deleted inside a ZIP; and whatever a DOS program deletes, as the plan has it.
  - **A tenth of the drive at most:** when more is put in, the oldest go for good.
  - **`[desktop] recycle = off`** (the Control Panel's Desktop page): nothing goes in.
  - The folder itself is not shown in its drive's listing, in the tree, to Find or to the Start menu's search.
  - **Tests:** `files-bin` on the host and on Loop A (a file and a folder deleted, the window's list, the
    file restored and read back byte for byte, the folder deleted from there, Shift+Del, the folder empty).
- **Not done yet:** the rest of the plan: views
  (small icons, a list, resized columns), folders that load as they go, long names, "open with" and apps'
  entries in the context menu, the rest of archives (above), a search that goes on in the background and looks
  inside files, a folder's size counted in the background, thumbnails, drag and drop.

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
- **Layers** (2026-11-19): a list of them beside the picture (the top one first; a click makes one the current
  one) and a Layers menu: New Layer (clear, over the current one), Duplicate, Delete, Move Up and Down, Merge
  Down, Shown, Opacity. The tools draw on the current layer; the eraser makes it clear there, so that what is
  under shows (on the bottom layer it paints the second colour, as before). Every change of the layers is
  undone as a resize is (the picture before kept whole). A picture is saved as it shows, what is clear in all
  its layers white.
- **Tests:** `paint_test` (layers added, drawn on, made clear, hidden, copied, moved, merged, undone, half
  shown); `paint` on the host and on Loop A (a layer drawn on and rubbed out in part, hidden, merged, the merge
  undone, the layer deleted: the picture as it was).
- **A selection** (2026-11-19): the Select tool (S) drags out a rectangle, which the view marks with dashes
  (`gui_image_view_mark`, new in the toolkit); Select All; Esc or a click takes it away. **Cut**, **Copy** and
  **Paste** (Edit's menu) are of the selection, or of the whole picture with none: what is copied is the
  current layer's pixels there, and a paste is a new layer with its corner at the selection's. **Crop to
  Selection** (Image's menu) cuts the picture down to it. What is copied is kept in Paint for now: the
  desktop's clipboard for pictures is to come.
- **Tests:** `paint_test` (a part copied, pasted as a layer, the picture cut down); `paint` (a part selected and
  copied, pasted elsewhere, the picture cropped, both undone: the picture as it was).
- **Adjustments** (2026-11-19; their own menu): Invert (Ctrl+I), Black and White, Sepia, Brightness and
  Contrast, on the current layer, in the selection or all of it; what is clear stays clear. A step as a tool's
  stroke is. `paint_test` has each; `paint` inverts the picture, makes a selected part black and white and
  brighter and flatter, and undoes all three.
- **Not done yet:** blend modes, layers renamed and dragged, the layered format written; selections of other
  shapes, a selection moved, the desktop's clipboard; text; gradients, curves, the clone stamp; resizing in
  proportion and bicubic; levels, curves, hue and saturation, the effects, their previews; tiles for pictures
  of any size.
- **PCX written** (2026-11-19): Save As has PCX, of 24 bits (three planes a row, each run-length coded by
  itself: `apps/shared/picfile.c`). `paint-pcx`: a picture saved so and opened again is the same picture.

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
- **A key typed just after a menu's item was chosen was dropped** (the other side of the first fault): it
  came for the menu's pop-up, which had the keyboard as it was pressed and was gone when the app looked at it.
  It is the pop-up's owner's now (`sdk/gui/core.c`: the pop-ups closed lately are remembered, and whose they
  were). Paint's suite on the 486 again: B after Layers' New Layer chose no brush.
- **Alt+F4 with a menu open, or typed just after its item,** closed the menu alone, or nothing (the third
  side of the same fault): GLOS asks the window that has the keyboard to close, which is the menu's pop-up. A
  close request for a pop-up is its owner's now, the pop-up open (it goes first) or closed since. The twentieth
  gate's `notepad-book` on the 486: Notepad was not closed after View's Line Numbers. The Slow page of GUITEST
  (`tests/gui/slow.txt`) has both cases.
- **A number typed into a spin box** went on the end of the value that was there (5, then "40" typed, was 0),
  and a minus sign only changed the sign of what was there ("-50" typed over 100 was -100). The first digit
  typed is the number anew now, the next ones go on its end, and a minus sign before them makes it negative
  (`sdk/gui/basic.c`). Found by Paint's Brightness and Contrast in its script.
- **An open menu is forgotten when its window goes** (`gui_menu_gone`): the menus' state pointed at freed
  windows when a menu's owner was closed with the menu open.
- **The suites:** a GUI script that fails now reports each app's exit code and what it wrote to stderr; a
  script waits for a full-screen program's end by `settle` (no DOS program running) where it paused a fixed
  time (a session is 8.6 seconds on the G400's machine: `term-run`, the twelfth gate); the batch case waits
  for the log's line that names FreeDOS's COMMAND.COM where it looked once (the 486, the twelfth gate).

## What is next

More of Paint (layers, selections, the image's size, adjustments); the rest of Files' plan (views, archives,
thumbnails); Images' filmstrip; what is left of the Terminal's and Notepad's plans. Each by its plan in
`docs/v1/apps/`.
