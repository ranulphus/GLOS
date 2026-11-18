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
- **Not done yet:** saved connections, the SFTP pane, the scroll-back's length and search, fonts and colour
  schemes, more of xterm (256 colours, the mouse, bracketed paste), splits, tabs dragged along the strip.

## What is next

The Terminal's saved connections and the rest of its plan; Notepad (tabs, find and replace, highlighting); Files (toolbar, address bar,
views, thumbnails, search, archives as folders); Images and Paint. Each by its plan in `docs/v1/apps/`.
