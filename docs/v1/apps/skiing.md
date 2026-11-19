# The skiing game: plan (GLOS v1, phase F)

**New in v1** (the owner, 2026-11-19): one of the two games GLOS comes with, which need nobody's licensed files.
A downhill skiing game in 2D, small enough to ride with a release and to run on a 486.

**Its own game.** The genre is old and wide (a slope seen from above that scrolls under a skier); this one has
its own name, skier, pictures, sounds, mountains and rules, drawn and written for GLOS. Nothing of any other
skiing game is in it: no character, picture, sound or course of theirs, and no creature that hunts the skier.
Its name is the owner's to choose (below); "the skiing game" until then.

## What it does

- **A mountain, top to valley.** The slope is seen from above and scrolls up under the skier, who is a third of
  the way down the window. A run begins at the summit hut and ends under the banner at the valley station.
- **The skier:**
  - steers left and right through sixteen headings, from across the slope to straight down the fall line; the
    nearer the fall line, the faster;
  - **tucks** (faster, turns slower) and **snow-ploughs** (brakes);
  - leaves **tracks** in the snow behind, which stay for the run;
  - leaves the ground at a ramp or a mogul taken fast, and can turn in the air (a half turn, a whole one, two)
    for style; landed across the slope or half way round a turn is a fall;
  - falls at a tree, a rock, a fence or another skier: a second or two lost getting up, no lives, no end of
    the run.
- **The snow:** piste (fast), powder at the edges (slow, soft falls), ice (fast, hardly any grip), moguls.
- **What is on the mountain:** firs and pines, rocks, fences and nets, lift pylons with the chairs' shadows
  passing, ramps, gates, huts, snow cannons, a piste groomer that crosses slowly, other skiers (slow ones who
  zigzag, to be passed), and a mountain hare now and then, which is in nobody's way.
- **The weather:** clear, snowfall, fog (less is seen ahead), wind in gusts that push across the slope. A run
  at night is lit by the skier's lamp alone.
- **Three ways to play:**
  - **Slalom:** gates to pass on the right side, against the clock; a gate missed costs five seconds. The best
    run is kept and skis beside you as a **ghost**.
  - **Free run:** the whole mountain, for points: distance without a fall, air, turns landed, near misses.
  - **The mountain of the day:** the day's date makes the mountain, the same on every PC, with a table of the
    day's best times and scores.
- **The mountains:** made from a number (a seed) by rule: the piste's width and bends, where the trees close in,
  the ramps, the ice. Four grades as on a real mountain (green, blue, red, black: steeper, narrower, more in
  the way). A mountain's number can be typed in, to ski a friend's.
- **The tables:** the best ten times and scores of each grade and of the day, with names, kept in a file beside
  the program.
- **Controls:** the arrow keys (or A and D, S, W), Space for the turn in the air; the mouse (the skier turns
  towards the pointer, the button tucks); a pad (the stick, two buttons). Esc pauses.
- **The window:** a playing field of 480 by 360 with a strip beside it (the time, the speed, the gates, the
  score); the window doubled on a large desktop; full screen (F11).
- **Sound:** the skis' hiss, which rises with the speed and changes on ice; the wind; a gate's bell; the thump
  of a landing; a fall. Made by rule when the game is built (noise, sweeps, a struck tone): no recordings. No
  music in v1.

## From toolkit 2

- **A canvas** drawn every frame (the field), an app's idle function for the frame's clock, the menus, the
  dialogs for the tables and a mountain's number, the status strip.
- **Pictures** from the app's own sheet (the skier's sixteen headings, in the air, fallen; the trees, the rest),
  drawn with a transparent colour, at 8, 16 and 32 bits.

## From API 2.0

- **DRAW:** a window's pixels, presented each frame; the screen's size (for doubling).
- **INPUT:** the keys held (not typed), the mouse, the pads.
- **AUDIO:** two or three streams into the mixer.
- **FS, SETTINGS:** the tables' file; the last choices (grade, weather, controls).
- **TIME:** the date, for the mountain of the day; the frame's clock.

## Libraries

None but GLOS's own (libglos, the toolkit). The mountains' rule, the skier's motion and the sounds are the
game's own code; the pictures are drawn for it (a sheet in the source tree, as the icons are).

## CPU floor

A 486DX2-66 at 30 frames a second in the 480 by 360 field at 8 bits (the snow is a fill; a few dozen small
pictures over it; the tracks a few hundred short lines). To be measured on the 486 bench, not sized from the
emulator; if it falls short there, the field is 320 by 240, doubled.

## How it is built

- **The motion in fixed steps** (60 a second, whatever the frame rate), from the keys alone: a run is its
  mountain's number and the keys of each step. That is the ghost (a few kilobytes), the demonstration that plays
  when nobody does, and the tests.
- **The mountain** is made a screen ahead as the skier goes down, from the seed and the distance: nothing of it
  is kept but what is in sight, and the same number is always the same mountain.

## Tests

- **Host:**
  - the mountains' rule: for a thousand seeds of each grade there is a way down (a path of the piste's width
    from the top to the bottom that meets nothing), every gate can be passed, and the same seed gives the same
    mountain;
  - the motion: recorded runs (the keys of each step) end at the same time and score, to the step, on every
    build;
  - GUI scripts with the demonstration's run at a fixed seed: pictures at given steps, at 8, 16 and 32 bits.
- **Loop A:** the demonstration's run on the 486's profile and on the G200's desktop (the same pictures; the
  frame times in the log); a run by keys through `glos key`; the table written and read again.
- **The bench:** the frame rate on the 486DX4 with its ViRGE, and on the 486DX2 profile's nearest real PC; by
  eye and ear on the IBM (the mixer through its Crystal chip).

## Later, not in v1

Music; two skiers on one PC (the field split); the day's table shared between PCs on a network; a course
editor.

## For the owner

- **The name.** Suggestions, all skiing's own words: **Fall Line** (the line straight down a slope), **Schuss**
  (a straight fast run), **Black Run**, **Powder Day**. Another of the owner's is as good.
- **The skier:** one figure whose jacket's colour is chosen, or a few to choose from.
- **Whether a fall can end a run** (a number of falls allowed in slalom) or only costs time, as planned here.
