# Notepad: plan (GLOS v1, phase A)

**Today:**
- **The editor:** one file in the toolkit's editor (DejaVu Sans Mono), Latin-1, with open, save, find, replace, go
  to a line, word wrap and the clipboard.
- **Its limits:** one document a window, and no highlighting or line numbers.

**In v1:** an editor after Notepad++: tabs, highlighting, real find and replace, big files, sessions. Still small
enough to open at once on a 486.

## What it does

- **Tabs:**
  - a tab for each open file, with its name and a mark when changed;
  - drag to reorder, middle-click or a box to close, a list when there are too many;
  - Ctrl+Tab to go round.
- **Sessions:** the open files, their places and their tabs' order kept when Notepad closes, and back when it opens
  (a setting).
- **Files dropped on the window open in tabs:** from Files, from the desktop.
- **Find and replace:**
  - in the file, the selection, or all open files;
  - match case, whole word, regular expressions (a small engine: classes, groups, anchors, repetition), wrap;
  - results listed in a pane to click through.
- **Find in files:** a folder and a pattern, the hits as they're found, in a pane, cancellable.
- **The margin:** line numbers, bookmarks (Ctrl+F2, F2), the changed lines.
- **Syntax highlighting** for C and its headers, assembly (NASM, MASM, Watcom), batch, INI and GLOS.CFG, Pascal,
  HTML, Markdown and plain text. Chosen by extension or from a menu. Colours from the theme.
- **Editing:**
  - indentation kept and Tab/Shift+Tab on a selection;
  - brace matching;
  - column (block) selection with Alt and the mouse;
  - duplicate, move or delete a line;
  - upper and lower case;
  - trim trailing spaces;
  - convert tabs and spaces;
  - undo and redo without a limit but memory.
- **Text and line ends:**
  - code pages (437, 850, 866 and the rest GLOS has) and UTF-8, detected where it can be and chosen from a menu;
  - line ends CR LF, LF or CR, kept as found and shown in the status bar.
- **Big files:** a file of tens of megabytes opens without reading it all first. The editor keeps its pieces and
  reads the rest as needed (the toolkit's editor already keeps line starts; it gains a piece table).
- **Printing:** to a printer on LPT1 as plain text (a setting), or to a file.
- **The status bar:** line and column, the selection's size, the code page, the line ends, insert or overwrite.
- **Zoom:** Ctrl+wheel, among the font's sizes.
- **Recent files:** in the File menu and the Start menu's jump list.

## How it looks

```
+--------------------------------------------------------------+
| File Edit Search View Encoding Language Help                 |
| [New][Open][Save] | [Cut][Copy][Paste] | [Undo][Redo] | [Find]|
| [ GLOS.CFG * ][ README.TXT ][ main.c ]                    [v] |
|   1 | ; GLOS.CFG                                             |
|   2 | [display]                                              |
| > 3 | mode = 800x600x16                                      |
+--------------------------------------------------------------+
| Ln 3, Col 18 | 437 | CR LF | INS | INI                        |
+--------------------------------------------------------------+
```

## From toolkit 2

- **Tabs:** tabs that close, reorder and overflow into a list.
- **The editor component** (the IDE's too):
  - a margin (line numbers, marks), highlighting by a lexer the app chooses, brace matching;
  - column selection, a piece table for big files, zoom.
- **Panes and bars:** a results pane under the editor (a splitter), the toolbar, the status bar's sections.
- **Dialogs:** find and replace (with history), a code-page chooser, open and save with an encoding.
- **Interaction:** drag and drop of files onto the window; background work (find in files) on the app's threads.

## From API 2.0

- **FS:** long file names; reading a file a part at a time; watching an open file for changes from elsewhere ("it
  changed on disk: reload?").
- **THREAD:** find in files, opening big files.
- **DND:** files dropped onto the window.
- **CLIP:** text, and rich text for highlighted copies (optional).
- **SHELL:** recent documents for the Start menu's jump list; "open with".
- **SETTINGS:** the theme's colours, the font's size, the session setting.

## Libraries

- **None:** GLOS's own lexers and regular expressions (a small backtracking engine, written here).

## CPU floor

- **A 486DX2** for files up to a few megabytes.
- **Find in files** over a big tree, and highlighting of very long files, are slow there and run in the background.

## Tests

- **Host:**
  - the piece table against a plain model (the toolkit's editor test grows: random edits on a 10 MB file);
  - the regular expressions against a table of cases;
  - each lexer on sample files (tokens compared);
  - GUI scripts: tabs, find and replace, column selection, highlighting at 8, 16 and 32 bits.
- **Loop A smoke:** open a 20 MB file and show its first screen within a second; find in files over C:\GLOS; save
  as UTF-8 and as code page 850, read back by bytes.
- **The P4:** the same at real speed; the 486 floor timed on 86Box's 486.

## Open points

Settled at the owner's review (2026-11-06):
- **Printing:** plain text (page breaks, margins, a header) to LPT1-3 or a file. No PCL or PostScript in v1.
- **Spell checking:** not in v1.
