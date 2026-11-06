# Calculator: plan (GLOS v1, phase A)

**Today:** a pocket calculator: the four operations as they come, square root, per cent, 1/x, the sign, memory, the
clipboard, by mouse or keyboard.

**In v1:** three modes, as Windows' calculator has had since 98: standard, scientific, programmer.

## What it does

- **Standard:** as today, plus a history of the calculations (click one to use its result again).
- **Scientific:** the order of operations and brackets, trigonometry (degrees, radians, grads), hyperbolic
  functions, powers and roots, logarithms, factorial, pi and e, exponent entry, the inverse key; precision of the
  FPU's 80-bit floats, shown to as many digits as the window takes.
- **Programmer:**
  - hexadecimal, decimal, octal and binary, side by side;
  - 8, 16, 32 and 64-bit words, signed or not;
  - AND, OR, XOR, NOT, shifts, rotates, modulo;
  - the binary's bits to click.

  Useful to anyone writing DOS programs (and the IDE's users).
- **Units (optional):** length, weight, temperature, data sizes (KB, MB, KiB, MiB).
- **The clipboard:** copy the display, paste a number or an expression.
- **The keyboard:** every key has a key, as today.

## From toolkit 2

- **Choosing a mode:** a menu or tabs.
- **A list for the history.**
- **A grid of buttons** (a layout the toolkit already has).

## From API 2.0

- **CLIP:** text.
- **SETTINGS:** the mode, kept.

## Libraries

None.

## CPU floor

A 486DX2 (it has an FPU; a 486SX isn't supported, PRD Q1).

## Tests

- **Host:**
  - each mode's arithmetic against a table, the expression parser included;
  - the programmer mode's words and their overflow;
  - GUI scripts of each mode.
- **Loop A smoke:** a calculation in each mode, the result on stdout as today.
