# The hovercraft game: plan (GLOS v1, phase F)

**New in v1** (the owner, 2026-11-19): the second of the two games GLOS comes with, which need nobody's licensed
files. Hovercraft in walled arenas, in 3D, alone against the computer's craft or PC against PC over a network.

**Its own game.** Craft that slide and bump are a genre; this one has its own name, craft, arenas, pictures,
sounds and rules, made for GLOS. Nothing of any other hovercraft game is in it: no arena, craft, picture or
sound of theirs, and none of their particular rules or things to pick up. Its name is the owner's to choose
(below); "the hovercraft game" until then.

## What it does

- **A hovercraft** has almost no grip: it goes where it was going until its fans have pushed it somewhere else.
  It is driven from just behind and above (or from its seat: a key changes the view).
  - **Thrust** forwards and back, **turn**, and **side fans** (a slide left or right).
  - **Boost:** a tank that fills by itself and empties in a burst of speed.
  - **Set down:** the cushion dropped, the craft on its skirt: it stops in a length and cannot be pushed, and
    cannot move either. The two together are the skill of it: when to be fast, when to be a rock.
  - **A bump** passes speed from craft to craft and off the walls. Nothing is shot; nobody is destroyed. A craft
    pushed into the water of a pit is put back at its side's dock after three seconds.
- **The arenas** are yards and basins: quays, ramps, bridges, pits, narrow gates, open floors. Each is a text
  file: a grid of squares, a letter a square (floor, wall, a height, a ramp and its way, water, a dock, a place
  for a beacon or the lamp), and a few lines above it (its name, its light, its colours). Six come with GLOS; a
  new one is written in Notepad and put beside them.
- **Four games:**
  - **Flags:** two sides, a flag at each dock; the other side's carried home scores. A carrier is slower and
    cannot boost; bumped hard, it drops the flag where it is.
  - **Beacons:** five or more in the arena; one touched turns to the side's colour. The side with more of them
    lit scores as the seconds pass.
  - **The lamp:** everyone for themselves. One lamp; whoever carries it scores by the second, and a bump takes
    it.
  - **Laps:** round the arena's buoys against the clock, alone or together, with the best lap's ghost.
- **Players:** two to eight craft. The computer drives those that nobody does, at three strengths. Alone, the
  player has a side of the computer's craft with them and one against.
- **Over a network** (GLOS PCs on an Ethernet, or joined by a cable: GLOS's direct link): one PC opens a game,
  which the others see in a list by themselves (a broadcast) and join; no addresses typed. The PC that opened it
  keeps the game's truth; a PC that leaves is taken over by the computer; if the opener leaves, the game ends.
- **Controls:** the keys (arrows or WASD, Q and E for the side fans, Shift to boost, Space to set down); a pad
  (the stick, the triggers, two buttons); the mouse turns the craft if it is taken. Esc for the menu, Tab for
  the scores, F11 for full screen.
- **What is seen besides the arena:** a small map (the walls, the beacons, one's own side), the score, the
  clock, the boost's tank, an arrow to the flag or the lamp.
- **The look:** plain coloured faces with light and haze, a few small patterns (a quay's edge, a dock's stripes,
  the water): bold, readable at 320 by 240, cheap enough for the software renderer. Each side's craft in its
  colour, with the player's name over it.
- **Sound:** the fans (their pitch by the thrust), the skirt's scrape when set down, bumps by their force, a
  beacon's chime, the flag's and the lamp's calls, the horn at the end. Made by rule when the game is built:
  no recordings. No music in v1.

## Where it runs

- **On DOS-GL** (RetroGPU's `gl`): a card's engine where DOS-GL has one for a window (Matrox today; others as
  their devices come), the software renderer elsewhere.
- **In a window, or full screen as a GLOS app** (`docs/v1/fullscreen-apps.md`): at its own size, the desktop a
  key away. Never a session of its own: it needs GLOS's network, mixer and pads, and has them as an app.
- **The lobby is a toolkit window** (the game, the arena, the craft's colour and name, the list of games found
  on the network, the settings); the arena is the DOS-GL window it opens.

## From API 2.0

- **DRAW** (through DOS-GL): the window's video memory or its pixels, the swap, full screen (`GLOS_WF_OWN_MODE`).
- **INPUT:** the keys held, the mouse taken (`GLOS_INPUT_CAPTURE`), the pads.
- **AUDIO:** a few streams into the mixer (the fans' as a loop whose rate changes).
- **NET:** UDP sockets (API 2.2): a broadcast to find games, datagrams to play them.
- **FS, SETTINGS:** the arenas' folder, the best laps, the last choices.
- **THREAD:** the network's and the sound's work beside the frames.

## Libraries

DOS-GL and GLOS's own (libglos, the toolkit). The craft's motion, the computer's driving, the network's
messages, the arenas' reader and the sounds are the game's own code.

## How it is built

- **The game's truth in fixed steps** (30 a second), apart from the frames: a slow PC draws fewer frames of the
  same game.
- **The network:** the opener's PC runs the steps for everyone. The others send their controls (a few bytes,
  30 times a second) and are sent every craft's place and speed (15 times a second, some 30 bytes a craft)
  and what happened (a flag taken, a beacon lit); each draws its own craft from its own controls at once and
  is put right gently, and the others' between what it was last told. A datagram lost is the next one's to
  mend. Every message has the game's version: PCs of different releases do not join each other.
- **The computer's craft** find their way by the arena's grid (the shortest way round the walls, made once for
  the arena), steer for the next square of it, and know the game: go for the flag, see the carrier home, hold a
  beacon, chase the lamp. The strengths differ in how well they aim a bump and use the boost, not in speed.
- **An arena's check:** the reader refuses a file with no dock for a side, a place nobody can reach, or a
  square it cannot read, and says the line.

## CPU floor

**To be measured, and set by DOS-GL.** With a card's engine: the least PC DOS-GL draws for (a Pentium with a
Matrox card, at 640 by 480). With the software renderer: a guess of a fast Pentium at 320 by 240 for an arena
of a few hundred faces in sight; the 486s get the skiing game. The steps themselves (eight craft, the
computer's driving) are a 486's work. Measured on the benches before the page is believed (the P4 with the
G450, the IBM with the G400, the P3 with its TNT2 when DOS-GL draws there in a window).

## Tests

- **Host:**
  - the motion: recorded games (every craft's controls at each step) end with the same places and scores, to
    the step, on every build;
  - the arenas: the six read, each place reachable, each dock there; files with each fault refused with its
    line;
  - the computer's craft: a side of them against a craft that stands still scores in every game and arena; two
    sides of the same strength over a hundred games score within a margin of each other;
  - the network's messages, with a model of a network between two copies of the game in one program: datagrams
    lost, doubled and out of order, a PC that goes quiet, different versions.
- **Loop A:** two emulated PCs on one network: one opens, the other finds it by the broadcast and joins; a
  minute's game of each kind with the computer at the controls on both; the scores agree on both; the pictures
  of a recorded game at given steps (the software renderer and the G200); a PC that leaves.
- **The bench:** frames a second in a window, full screen as an app, on each card; two real PCs across the
  switch and across a cable; by eye, ear and pad.

## Later, not in v1

Music; more than eight craft; a game across the Internet; an arena editor with a picture (Notepad and the
reader's messages do for v1); two players on one PC.

## For the owner

- **The name.** Suggestions: **Slipway** (where craft come ashore), **Cushion**, **Skirt and Fan**, **Basin**.
  Another of the owner's is as good.
- **The setting:** harbour yards and basins by day and by night, as planned here; or something else with walls
  and floors (a quarry, an ice works, a rooftop).
- **Nothing shot, nobody destroyed** is this plan's choice (bumps, the boost and setting down are the whole of
  the fight). A thing to pick up now and then (a moment's heavier craft, a beacon's lock) could be added if the
  games feel thin when played.
