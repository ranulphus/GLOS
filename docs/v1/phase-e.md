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
- **Find** (2026-11-20; Edit's Find..., Ctrl+Shift+F): text looked for in what the tab has kept and shows,
  with the toolkit's Find dialog (it stays up beside the terminal: Find next and previous, Match case, Whole
  words, Regular expressions, its history). Each one found is chosen in the grid (Ctrl+Shift+C copies it) and
  scrolled to; the search goes round the end. Without the dialog: Ctrl+Shift+G the next, Ctrl+Shift+H the one
  before (F3 is the shell's own key). The finding is the terminal widget's (`gui_term_find`, by `gui_match`
  as every app's), so the IDE's output and the Task Manager's log will have it too. `term-find` on the host
  and on Loop A.
- **Not done yet:** a connection's key and its terminal settings, the Start menu's jump list and `ssh NAME`; the
  SFTP pane; the scroll-back's length (a setting); every match marked at once; fonts and colour schemes; more of
  xterm (256 colours, the mouse, bracketed paste); splits; tabs dragged along the strip.

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
- **Find in Files** (2026-11-20; Edit's menu, Ctrl+Shift+F): the text, the folder (the shown file's at first;
  Browse has the toolkit's folder dialog) and the files' names (`*.TXT;*.MD`). The files of the folder and of
  those under it are read on a worker's thread (Notepad is linked with the C library's lock now), and each
  line that has the text is listed as it is found in a pane under the tabs: the file, the line's number, the
  line. Enter or a double click opens the file (or shows its tab) at the line. Letters' case apart unless
  Match case is ticked; a file with a zero byte in its first 512 is no text and is left out, as is one of
  more than 4 MB; a thousand lines at most. View's item, or the pane's Close, puts the pane away and ends a
  search at work. `notepad-infiles` on the host and on Loop A (`C:\INF`).
- **Code pages and UTF-8** (2026-11-20): the editor's text is UTF-8, as all the toolkit's is; a file's bytes
  are UTF-8 or one of DOS's code pages (437, 850, 852, 866, 737: `include/glos/cp.h`). A file that begins with
  UTF-8's mark, or has bytes above 7Fh that are all well-formed UTF-8, is UTF-8 (the mark is written back if
  it was there); any other is in the code page DOS has now, plain ASCII too, so that what is typed into
  AUTOEXEC.BAT is written as DOS's programs read it. The status bar says which. **Format's Encoding** changes
  it: a file as it is on the disc is read again as the one chosen (bytes taken for the wrong page are put
  right so); a changed or new text will be written as it. Saving in a code page a text with characters the
  page hasn't got asks first: UTF-8 instead, or a question mark for each.
  - Until now a file's bytes were shown as UTF-8 whatever they were: a DOS text with accents was strays.
  - **Tests:** `notepad-enc` on the host and on Loop A (a code page file changed and read back byte for byte;
    a UTF-8 file with its mark; a UTF-8 text saved in code page 437 with a euro sign in it).
- **Large files** (2026-11-20): a file of a megabyte or more is read on a worker's thread, and made the
  editor's text there (its code page taken for UTF-8 too): its tab has its name at once, the window and the
  other tabs answer meanwhile, and after a second of reading the toolkit's progress dialog says how far it is,
  with Cancel (the tab is an empty, nameless text again). Every file was read by the window's own thread
  until now: twenty megabytes kept it deaf for as long as the disc took. A text being read is not saved
  over its file. `notepad-big` on the host and on Loop A (`-big 30` makes a file of 38 bytes a big one).
- **Not done yet:** files dropped on the window; the pane resized by a bar, regular expressions and whole
  words in Find in Files; the line ends in the status bar and chosen; a big file's first screen before the
  rest of it is read, and files bigger than memory; bookmarks and the chosen kind kept with the session.

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
  - **Extract To...** (2026-11-20; File's menu, the context menu): the archive unpacked into a folder of its
    name in a folder chosen for it (the toolkit's folder dialog: its tree, or a path typed). `files-arc` has it.
  - **Renaming inside a ZIP** (2026-11-20): Rename (F2) on a file or a folder of the ZIP shown; a folder's
    entries all get its new name. The name may be any a ZIP can hold (longer than DOS's, with small letters);
    the archive is written again beside itself, the entries' packed bytes as they are (`arc_zip_rename`;
    `arcw_test` has a file, a folder to a deeper and longer name, the archive behind a program and the one
    with sizes after the bytes, read back by Python's zipfile; `files-zip` renames a file and its folder and
    unpacks them).
  - **Not yet:** a thing of one archive pasted straight into another (it is unpacked to a folder first),
    LZH/LHA, ARJ and 7z, the old ZIPs' implode and shrink, big archives
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
- **Views** (the ninth part, 2026-11-20): View has **Large icons, Small icons, Thumbnails** and **Details**,
  in the browser and in a folder's own window. The toolkit's list had the four already (`sdk/gui/vlist.c`);
  its plain list (`gui_list`) now gives them to its apps: `gui_list_view`, a row's big icon beside its small
  one (`gui_list_big`: a view changes without the list's being filled again), a row's thumbnail
  (`gui_list_thumb`: the app's pixels), and the rows on the screen now (`gui_list_shown`).
  - **Thumbnails** (`apps/files/thumbs.c`) are made of pictures by the viewer's and the editor's own decoders
    (`apps/shared/picfile.c`: PNG, JPEG, BMP, GIF, TGA, PNM, PCX), 96 pixels at most each way, each pixel the
    mean of those it stands for. One is made at a time on a worker's thread, those on the screen first and a
    screenful after them; none while a copy or another job is at work. Files' windows go on answering.
  - **Kept:** each thumbnail is written beside FILES.EXE (`THUMBS\XXXXXXXX.THM`, the name from the picture's
    path; its head has the file's size and date, so a changed picture's is made again). The next time it is
    one small file read, which is what makes the view usable on a 486 (a big JPEG is many seconds there). The
    folder is kept to 400 files and 6 MB: the oldest go.
  - **Memory:** all the windows' thumbnails together are kept to 3 MB; when more are wanted, those farthest
    from what is shown go, and come back (from the kept files) when they are scrolled to. A picture of more
    than eight million pixels has no thumbnail (the decoders want eight bytes a pixel while they work).
  - **A folder listed again** (something in it changed) keeps the thumbnails of the files that did not.
  - **Kept from one run to the next** (`FOLDERS.CFG` beside FILES.EXE): the browser's view, its columns'
    widths and their order (they were dragged already, and forgotten); each folder window's view, with its
    place.
  - **Tests:** `files-thumbs` on the host and on Loop A (the four views of a folder of pictures, `tests/gui/
    mkpix.py`'s; each thumbnail made, then kept; a file with a picture's name and no picture); `files-view-kept`
    on Loop A (Files run twice: the second browser opens in the first's view).
  - **Not yet:** a list view (names in columns, downwards); a thumbnail's size chosen; thumbnails of what is in
    an archive; the toolbar's view button as a list of the four.
- **A folder's size is counted in the background** (2026-11-20): Properties is up at once, its Holds line
  saying how far the count has got ("1,234 files, 56,789 KB so far") and then the whole; closing the dialog
  ends the count. The whole of a drive can be asked for now: the count had a limit of 5,000 files while the
  window waited for it (`files-props` waits for the count's word).
- **The search goes on in the background, and looks inside files** (2026-11-20): Find Files lists what it
  finds as it comes, with a line saying where it has got to; the window answers meanwhile (Go To one already
  found, Find again for another search, Close to end it). **Containing:** takes text, and then only the files
  that have it in them are found (letters' case apart; with no name given, any file). 2,000 found at most, with
  no limit on what is gone through (it was 200 of 5,000 while the window waited). `files-find` has both.
- **Open With** (2026-11-20; File's menu and the context menu): the chosen file opened by a program chosen
  for it. The list has GLOS's own that take a file (Notepad, Images, Paint) and the program the file's kind
  has already, if it is another; **Other** finds any program on the discs. **Always open .EXT files with it**
  writes `[open] .EXT = PROGRAM` (the setting Files has gone by since M9; the Control Panel shows it too).
  `files-openwith` on the host and on Loop A.
- **A big folder is listed as it loads** (2026-11-20): a folder's first 256 entries are read and shown at
  once, which is all of nearly every folder, and nothing is different for those. A bigger one's rest is read
  on a worker's thread, 64 at a time, each piece put into its place in the list (the order the list shows:
  folders first and by name, or as its header says), with "so far: reading more..." in the status bar until
  the end. The window answers meanwhile: what is listed already is opened, chosen and copied as ever. A
  listing of 2,000 files kept the window waiting two to seven seconds on the 486's profile (a find call an
  entry, each a DOS call from an app).
  - The toolkit has the reading (`gui_dir_open`, `gui_dir_next`, `gui_dir_close`, `gui_dir_sort`) and
    `gui_list_resort` for rows added to a sorted list.
  - **Tests:** `files-big` on the host and on Loop A (`-listfirst 2` makes `C:\FM` a big folder: two entries
    at once, four after; the list as any listing of it once Files says the last has come).
  - **Not yet:** a thing gone to by name (Find's Go To, a new folder) is chosen only if it is listed already;
    an archive's listing still waits for all of it.
- **Not done yet:** the rest of the plan: long names, apps' own entries in the context menu, the rest of
  archives (above), drag and drop.

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
- **The filmstrip** (2026-11-20): View's **Filmstrip** (T) shows the folder's pictures in a row along the
  bottom of the window, small, the one shown framed; a press on another shows it, the wheel moves the row,
  PgDn and PgUp take the frame along. Images remembers whether it was shown (`IMAGES.CFG` beside the program).
  - The pictures are Files' thumbnails (`apps/shared/thumbs.c`, moved there from Files; the kept ones' folder
    is one for both programs), shown at half their size: a folder Files has shown as thumbnails is in the
    strip at once, and the other way. They are made one at a time on a worker's thread (Images is linked
    with the C library's lock now, as Files is), those in the row first and then a row either side; 160 are
    kept in memory, the farthest from the one wanted going first.
  - **Tests:** `images-strip` on the host and on Loop A (the strip of `C:\PIX`, each one said as it is made;
    the next picture; a press; the strip hidden again).
  - **Not yet:** the strip down the side; a bigger size on a big screen; names under the pictures or as
    tooltips; the row dragged.
- **Not done yet:** the picture kept as tiles (any size memory allows).

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
- **The desktop's clipboard** (2026-11-20): what Copy and Cut take goes on GLOS's clipboard as a picture
  (`GLOS_CLIP_IMAGE`), and Paste takes the picture that is there, whoever put it: pictures go between Paint
  and any other program that takes them. One the clipboard has no room for stays Paint's own until the next
  copy. `paint-clip` on the host and on Loop A (a copy, the clipboard given words instead, a paste that finds
  no picture, a copy pasted).
- **The Text tool** (2026-11-20; T): a press on the picture asks for a line of text and, by its Font button,
  one of GLOS's faces and sizes (the toolkit's font dialog); the letters go on the current layer in the
  button's colour, their top left corner where the press was, as one step to undo. The 2D library draws them
  into a picture of their own and the pixels they took are stamped on the layer (`pd_stamp`). `paint-text`.
- **Effects** (2026-11-20; a menu of their own): **Blur** (how far is asked, 1 to 12 pixels), **Sharpen**,
  **Emboss**, **Find Edges** and **Pixelate** (a block's side is asked), on the selection or the whole of
  the current layer, each one step to undo. Every pixel is made from its neighbours as they were (a copy of
  the layer is worked from); what is clear stays clear, and a neighbour that is clear or outside the picture
  counts as the pixel itself, so edges are not darkened by what is not there (`pd_effect`; `paint_test` has
  each on pictures small enough to work out by hand; `paint-effects`).
- **Not done yet:** blend modes, layers renamed and dragged, the layered format written; selections of other
  shapes, a selection moved; text of more than a line, turned or with fonts from files; effects' previews; gradients, curves, the clone stamp; resizing in
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
- **A program shown inside a window took the keyboard from it** (the preview surface; the twenty-fourth gate's
  `preview` on the 486, 2026-11-20, and the "embedded program still there two seconds after Stop" of the day
  before, which was taken for slowness): a toolkit program makes its window, draws it, and only then shows
  it; a host that looked in between took the window in and took the keyboard back, and the program's showing
  it then gave the keyboard to the window inside. The host's keys were the program's from there: Stop did
  nothing, Alt+F4 closed the program and left the host. Being shown no longer gives a window inside another
  the keyboard (`kernel/wm/deskapp.c`, `desk_win_state`): its host has it until the user presses inside.
  `tests/gui/preview-late.txt` makes the order certain (GUITEST's program inside waits two and a half seconds
  before it shows its window): it failed as the gate's did on every machine before the change.
- **Page Up and Page Down did nothing in a grid one line high** (the toolkit's list: they move by a screen
  less a line, which was no line when one line of big pictures is all that fits; a line at least now). Found
  by the thumbnail view in the browser's window at its first size.
- **A key typed just after the one that opened a submenu was dropped** (the fourth side of the menus' fault):
  it came for the menu's own pop-up, which still had the keyboard as it was pressed, and a menu under a
  submenu took no keys. Format, Encoding and 4, a tenth of a second apart, left the submenu open on the 486
  (`notepad-enc`: two runs of four under load before the change, none of eight after). Such a key is the
  submenu's now (`sdk/gui/menu.c`, `gui_menu_popup_of`). The Slow page has a submenu too, typed ahead as its
  other cases are, but that order did not lose the key: `notepad-enc` on the 486's profile is what shows it.
- **The host's script player waited for the first 32 different `said` lines only,** and went straight past
  any after them: a script with more passed without its lines being said. 256 now, and a script with more
  fails.
- **The suites:** a GUI script that fails now reports each app's exit code and what it wrote to stderr; a
  script waits for a full-screen program's end by `settle` (no DOS program running) where it paused a fixed
  time (a session is 8.6 seconds on the G400's machine: `term-run`, the twelfth gate); the batch case waits
  for the log's line that names FreeDOS's COMMAND.COM where it looked once (the 486, the twelfth gate).

## What is next

More of Paint (layers, selections, the image's size, adjustments); the rest of Files' plan (views, archives,
thumbnails); Images' filmstrip; what is left of the Terminal's and Notepad's plans. Each by its plan in
`docs/v1/apps/`.
