# MIDI Studio

Writes music as notes on a piano roll and saves it as a MIDI file (MID), which the [Player](PLAYER.MD) and
other programs play.

## Tracks

The list at the top has a track for each instrument. **Track, New Track** (Ctrl+T) adds one; **New Drum
Track** adds one whose keys are drums. **Instrument** (Ctrl+I) chooses among General MIDI's 128. **Mute**
(Ctrl+M) silences a track, **Solo** (Ctrl+L) plays it alone.

## Notes

On the roll, under the track chosen:

- press on an empty place to put a note there;
- press a note to choose it (with Ctrl: one more, or one fewer); drag it to move it;
- drag a note's right end to make it longer or shorter;
- the right mouse button removes a note. Del removes the chosen ones; Ctrl+A chooses all.

Undo is Ctrl+Z, Redo Ctrl+Y. Ctrl++ and Ctrl+- show more or less of the roll.

## Hearing it

**View, Arrangement** (F4) shows every track as a row of bars, with each bar's notes small in it: press a bar
to choose it (drag along to choose several), then **Edit, Copy Bars** (Ctrl+C), choose where they should go and
**Paste Bars** (Ctrl+V), or **Delete** them. F4 again shows the notes of one track.

**F5** plays from the start, **F6** stops. **Transport, Tempo** sets the speed in quarter notes a minute, from
the start or from a bar on (a change of tempo there). **Transport, Time Signature** sets the beats in a bar and
the note a beat is.

**F7** (Record) records into the chosen track while the song plays from its start: play a **MIDI keyboard**
plugged into the sound card's MIDI port (an MPU-401), or the **PC's keys** as a piano: the bottom row
Z S X D C V G B H N J M is the octave from middle C, the row Q 2 W 3 E R 5 T 6 Y 7 U the one above. Each note
lands where you played it, on the nearest sixteenth; hold **Shift** with a key for an accent (a louder note). **F7** or **F6** stops; **Ctrl+Z** takes the whole take
away again.

While recording, a **click** sounds each beat (a wood block, higher on a bar's first beat). **Transport, Click
While Recording** turns it off; **Transport, Count In a Bar** gives a bar of clicks before the take begins (the
song, if it has notes, starts after it).

**Edit, Step Entry** (F8) puts notes in without playing in time: each key you press (on the PC's piano rows or a
MIDI keyboard) puts its note at the blue cursor, as long as the step, and moves the cursor on. **Space** leaves
a rest, **Backspace** goes back a step and takes away what begins there, **Ctrl+1** to **Ctrl+4** make the step a
sixteenth, an eighth, a quarter or a half, and a click on the roll moves the cursor.

Press keys together for a chord: a key pressed while another you just put in is still held goes at the
same step, and the cursor moves on once.

**Edit, Quantise** (Ctrl+Q) puts the chosen notes' starts (or all the track's, with none chosen) on the
nearest sixteenth. **Transpose Up** and **Down** (Ctrl+Up, Ctrl+Down) move them a semitone, **An Octave Up**
and **Down** (Ctrl+Shift+Up, Ctrl+Shift+Down) twelve; nothing moves if a note would go past the keyboard's end. **Edit,
Velocity** (Ctrl+E) sets how hard the chosen notes (or all) are played, 1 to 127; softer notes are paler on the
roll.
