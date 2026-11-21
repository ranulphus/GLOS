# Hovercraft

A game of hovercraft in walled harbour yards, in 3D. (Its name is still a working one, and this is its first
part: six arenas, and the computer driving every craft but yours.)

A hovercraft has almost no grip. Its fans change its speed, not its place: it goes on the way it was going
until you have pushed it somewhere else. Turn early, and use the side fans to stop a slide.

## Driving

| Keys | What they do |
|---|---|
| Up, Down (or W, S) | The fans forwards and back |
| Left, Right (or A, D) | Turn |
| Q, E | The side fans: slide left and right |
| Shift | The boost, while its tank lasts; the tank fills again by itself |
| Space | Set down on the skirt: the craft stops in a length and cannot be pushed, and cannot move either |
| 1, 2, 3, 4 | Begin a game of flags, beacons, the lamp, laps |
| Tab (or N) | The next of the six arenas |
| Esc | Leave |

Your craft is orange with a white cabin, and you see it from behind. Nothing is shot and nobody is destroyed:
craft bump each other and the walls. A craft pushed into the water is back at its dock three seconds later.
Quays are reached by their ramps; you can drop off a quay's edge, not climb it.

## The four games

The score is at the top left, in the sides' colours; the bar at the bottom left is the boost's tank; the
small map at the top right shows the arena from above, with every craft (yours is white).

- **Flags:** two sides, a flag at each dock. Touch the other side's flag to take it, and bring it to your own
  dock while your own flag is there. Carrying it you are slower and cannot boost. If the flag's own side
  bumps you hard, the flag goes home; set down, you keep it.
- **Beacons:** touch a beacon to turn it to your side's colour. Each second, the side with more beacons
  scores.
- **The lamp:** everyone for themselves. Whoever carries the lamp scores by the second; any bump takes it.
- **Laps:** round the buoys in their order; the tall yellow one is next.

## An arena of your own

An arena is a text file: write one in [Notepad](NOTEPAD.MD) and save it, with a name ending .TXT, in the
folder ARENAS beside the game (C:\GLOS\ARENAS). Tab reaches it after the game's own six. A line for its
name, an empty line, then the arena, a letter a square:

```
name: A Yard of My Own

#########
#A.a.b.B#
#A.*.*.B#
#..d.c..#
#########
```

| Letter | What it is |
|---|---|
| # | A wall |
| . | Floor |
| 1 and 2 | Floor one and two levels up: a quay, a deck |
| ^ v < > | A ramp, rising the way it points to a floor one level up |
| ~ | Water |
| A and B | Each side's dock, where its craft start and its flag stands |
| * | A beacon |
| ! | The lamp |
| a b c ... | The buoys of a lap, in their order |

Every row is the same length, each side needs a dock, and every place must be one a craft can reach and
leave. A file that breaks a rule is left out; start the game from a [Terminal](TERM.MD) (HVCRAFT) and it
says which line is wrong.

## It needs

A graphics card that GLOS's DOS-GL draws on in a window; on other cards it is drawn by the processor, which
wants a fast Pentium.
