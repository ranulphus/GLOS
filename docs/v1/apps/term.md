# Term: plan (GLOS v1, phase A)

**Today:** the terminal and GLOS's shell (D50, D51):
- a text grid with a thousand lines of scroll-back;
- the shell's line editing, history and completion;
- DOS programs' output captured, keys typed into them;
- since M10, `ssh`, `sftp` and `ssh-keygen`, with an xterm screen (`vt.c`) for remote sessions.

**In v1:** tabs, saved connections, the toolkit's look throughout, and the IDE's output pane built from it.

## What it does

- **Tabs:**
  - a tab for each shell or SSH session, named by its folder or host;
  - reorder, close, and a menu of new ones (a shell, a saved connection).
- **Saved connections:**
  - host, user, port, key, and the terminal's settings;
  - kept in GLOS's folder, with keys and passwords in SECRETS, not in the file;
  - opened from a menu, the Start menu's jump list, or `ssh NAME`.
- **SFTP from the session:** a side pane to drag files between the PC and the host (Files' views over SFTP),
  beside the session's tab.
- **Scroll-back:** as long as memory allows (a setting), searchable (Ctrl+Shift+F), with matches highlighted.
- **The screen:** fonts and sizes (DejaVu Sans Mono at the toolkit's sizes), colour schemes (the theme's, a few
  classics), the cursor's shape, 80 or 132 columns, any size the window has.
- **xterm more fully:** 256 colours, the mouse reported to programs that ask (for `mc`, `htop` over SSH), bracketed
  paste, the alternate screen; the line drawing and code pages of DOS programs kept.
- **Splits:** two sessions side by side or one above the other in a tab (optional, if the toolkit's panes make it
  cheap).
- **The IDE's output pane:** Term's text grid and its VT as a toolkit widget (`toolkit-2.md`), so the IDE, and any
  app, can host a shell or show a build's output.

## From toolkit 2

- **Tabs and panes:** tabs that close and reorder; splitters.
- **The terminal widget:** Term's grid and VT, moved into the toolkit.
- **Dialogs:** connections (a list and a form); the font dialog; search with its history.

## From API 2.0

- **JOB:** the shell's programs run with their output streamed and their input given, cancelled with Ctrl+Break, an
  environment and a working folder each (today's GLOS_FN_RUN and JOB, grown).
- **NET:** connections (as today), DNS.
- **SECRETS:** saved passwords and key passphrases.
- **CLIP:** text, and (optional) rich text with colours.
- **FS:** long file names in the shell's commands where DOS has an LFN driver.
- **SHELL:** the Start menu's jump list of connections.

## Libraries

- **SSH:** GLOS's own SSH layer and TinySSH's crypto (as today).

## CPU floor

- **A 486DX2:** as today. An SSH handshake there takes about a second (PRD §1); sessions after that are fine.

## Tests

- **Host:** the VT against recorded xterm streams (vttest-like screens compared); the connections file; GUI scripts
  of tabs and search at 8, 16 and 32 bits.
- **Loop A smoke:** a shell tab and an SSH tab to the host side by side; a build's output streamed into the pane
  widget.
- **The P4:** SSH throughput at real speed.
