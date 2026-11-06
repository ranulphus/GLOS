# Games Library: plan (GLOS v1, phase A)

**New in v1.** The front end for what GLOS is for: DOS games, found, shown with their box art, set up, and played
with the right settings and controllers.

## What it does

- **Finding games:**
  - folders the user names (C:\GAMES by default) scanned in the background for programs, recognised by name and
    size against a small built-in list of well-known DOS games (title, year, publisher, which executable to run);
  - anything else added by hand: a program, its title, its picture;
  - the scan watches the folders for new games (FS's watching).
- **The library:**
  - a grid of box art (or a list), grouped and filtered (genre, year, publisher, played recently, favourites),
    searched as you type;
  - a game's page: its picture, its details, its notes, when it was last played and for how long.
- **Box art and details:** pictures the user adds (dropped onto a game, any format Images reads), the game's own
  icons, and screenshots GLOS takes while it runs (the owner's choice: nothing fetched online in v1). Kept beside
  the library's data, never in the game's folder.
- **Settings for each game:**
  - its profile (GLOS.CFG's `[program]` section: exclusive or windowed, direct mode, memory, environment, its
    BLASTER, the screen mode for windowed DOS-GL games), edited here as a form;
  - its controller mapping (the gamepad plan's J6: which pad, which buttons, the axis range);
  - the sound card it should see.
- **Playing:**
  - Play runs it as its profile says (JOB, sessions);
  - the time played counted;
  - a setup program (SETUP.EXE, SETSOUND) one click away.
- **DOSBench:** for games DOSBench knows, run its benchmark from the game's page and keep the results (frame times),
  to compare cards and settings.
- **Opened by the Start menu's Games group;** its pinned games are tiles there.

## From toolkit 2

- **Views:** an icon grid with big pictures (the icon view at a large size), loading pictures as they come; filters
  and search.
- **A game's page:** a form with pictures, text and buttons, and its profile's form (shared with the Control
  Panel's).

## From API 2.0

- **FS:** long file names; scanning folders in the background; watching them; file sizes and dates.
- **JOB:** running a game by its profile (as an exclusive session or a window) and knowing when it ends; the time it
  ran.
- **SETTINGS:** the profiles (`[program]` sections), read and written.
- **INPUT:** the pads and their mapping (the game port's configuration, J6), and a test.
- **SHELL:** the Start menu's Games group and its tiles; jump lists of recent games.
- **THREAD:** the scan and the pictures' loading.

## Libraries

- **Pictures:** stb_image (as Paint).
- **The list of known games:** GLOS's own (titles, years, publishers, executables), written by hand for a few hundred
  well-known games. No third-party databases with restrictive terms.

## CPU floor

A 486DX2 (the grid's pictures load in the background; on a 486 slowly).

## Tests

- **Host:** the scan on a fake tree of game folders against the known list; GUI scripts of the grid, a game's page
  and its profile's form.
- **Loop A smoke:** a game (one of the suite's: Fifth Wheel, PrBoom) found, its profile set, played as an exclusive
  session, and its time counted when it ends.
- **The P4:** C:\GAMES-like folders of real games (the owner's, read only), DOSBench from the page.

## Settled at the owner's review (2026-11-06)

- **Box art:** the user's own pictures, the games' icons and screenshots; no online source in v1. Fetching can come
  later, from a source whose terms allow it.
