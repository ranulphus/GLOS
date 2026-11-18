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
- **Not done yet:** files dropped on the window; bookmarks; Markdown's colours and a menu to choose a kind;
  whole words, regular expressions and the results pane, find in files; code pages; large files.

## What is next

The rest of the Terminal's and Notepad's plans (the margin and highlighting first); Files (toolbar, address bar,
views, thumbnails, search, archives as folders); Images and Paint. Each by its plan in `docs/v1/apps/`.
