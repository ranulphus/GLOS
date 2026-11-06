# Help and the welcome tour: plan (GLOS v1, phase A)

**New in v1.**
- **Help:** GLOS's user guide in a viewer, with each app's help a key away (F1).
- **A first-start tour:** the desktop, the Start menu, the task bar, how to run DOS programs and games, where the
  settings are.

## What it does

- **The user guide:** chapters on the desktop and the shell, each app, DOS programs and games (sessions, profiles,
  direct mode), sound, USB devices and pads, the network and SSH, the settings, troubleshooting (the log, safe
  start).
- **The format:** Markdown files with pictures, in GLOS's folder (HELP\), shown by the toolkit's rich-text view (the
  Web Reader's). Links between pages, a contents tree, an index, search over every page.
- **F1 in any app** opens that app's page (SHELL: an app names its help page); a dialog's Help button opens its
  section.
- **The tour:**
  - on GLOS's first start (and from Help's menu);
  - a few full-window pages with pictures and a "show me" button that points at the real thing (the Start button
    highlighted, the task bar's icons named).
- **Tips:** a "tip of the day" at start (a setting), from the guide.
- **Written for the user,** not the developer: the PRD and the docs/ folder stay the developers'.

## From toolkit 2

- **The rich-text view:** Markdown's subset (headings, paragraphs, lists, emphasis, code, links, pictures, tables).
- **Basics:** the contents tree and search with results; a tour page widget (a picture, text, buttons, and an
  overlay pointing at a screen position).

## From API 2.0

- **SHELL:**
  - an app's help page (its name, given when it starts);
  - F1 handled by the toolkit, which asks the shell to open Help at that page;
  - pointing at a place on the screen for the tour (the task bar's parts, the Start button: the shell knows where
    they are).
- **FS:** long file names; search over HELP\.
- **SETTINGS:** the tour shown once, tips on or off.

## Libraries

None: GLOS's own Markdown reader (the subset).

## CPU floor

A 486DX2.

## Tests

- **Host:** the Markdown reader on the guide's pages (every link resolves, every picture exists); GUI scripts of
  pages, search and the tour.
- **Loop A smoke:** F1 in Notepad opens Notepad's page; the tour's first page on a first start.

## Writing the guide

The guide is written as the apps are finished (phase E and F), checked by the owner, and is part of G's release
docs.
