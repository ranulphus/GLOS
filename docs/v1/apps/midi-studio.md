# MIDI Studio: plan (GLOS v1, phase A)

**New in v1.** GarageBand for MIDI files: compose and arrange songs from tracks of notes, played through the OPL3 FM
chip, an external synth, or a General MIDI software synth, and saved as standard MIDI files that DOS games and other
programs can play.

## What it does

- **Songs:**
  - tracks, each with an instrument (General MIDI's 128, and drums on channel 10), its volume, pan, mute and solo;
  - tempo and time signature, with changes along the song;
  - markers.
- **The arrangement view:** tracks as rows of regions (clips) along bars, to drag, copy, split, loop and join. A
  region's notes are shown in miniature.
- **The piano roll:**
  - a region's notes on a keyboard grid, drawn with the mouse, moved, stretched;
  - their velocity in a lane underneath;
  - quantise (to a grid, with strength and swing), transpose, select by pitch or time;
  - controllers in lanes: the sustain pedal, modulation, pitch bend, volume.
- **Step entry:** notes entered with the PC's keyboard as a piano (two rows of keys, octaves moved), or from a MIDI
  keyboard.
- **Recording from a MIDI keyboard** (the owner's choice: an MPU-401 port where a card has one, and USB MIDI
  keyboards):
  - played in, with a count-in and the metronome;
  - overdubbed or replacing;
  - quantised after.
- **Playing it:**
  - **The OPL3 FM chip:** GLOS's FM driver with General MIDI patches, as the Audio Player's.
  - **An MPU-401:** to an external synth or a wavetable card (a Roland MT-32 or SC-55 on the port, a Yamaha DB50XG
    daughterboard).
  - **The software synth with a SoundFont:** on a fast Pentium.
  - **Choice:** per track, or the whole song.
- **Loops and patterns:** a library of drum patterns and loops (our own, made for GLOS) to drag into tracks, after
  GarageBand's loop browser.
- **Files:**
  - Standard MIDI Files Type 0 and 1, in and out; its own project format (tracks, regions, the loops used) as a MIDI
    file with GLOS's metadata in a meta event, so any player still plays it;
  - export to WAV (the software synth's render, or the OPL3's by recording its output where the chip allows).
- **The mixer:** each track's level and pan, and the master; the software synth's reverb and chorus.

## How it looks

```
+--------------------------------------------------------------------+
| File Edit Track Region View Transport Help                         |
| [|<][<<][>][o][>>][>|]  120 BPM  4/4   001.01.000  [metronome][loop]|
+------------+-------------------------------------------------------+
| Drums   M S| [====]    [====]    [====]    [====]                   |
| Bass    M S|      [==========]       [==========]                   |
| Piano   M S|  [======================]                             |
| Strings M S|                  [=================]                  |
+------------+-------------------------------------------------------+
| piano roll of the selected region (keys at the left, notes as bars) |
+--------------------------------------------------------------------+
```

## From toolkit 2

- **Canvases:** a timeline with bars and beats, zoom and scroll in both directions, and regions to drag; the piano
  roll (a canvas with a keyboard header and lanes).
- **Track headers:** a list with buttons and sliders in each row.
- **Panels:** docking panes (the piano roll under the arrangement, the loop browser at the side), splitters.
- **Helpers:** the undo framework; background work (rendering to WAV).

## From API 2.0

- **AUDIO:**
  - MIDI out to the OPL3 and an MPU-401, timed to the millisecond (events with timestamps queued to the kernel, which
    plays them on its tick, so a busy window doesn't make the music stumble);
  - MIDI in from an MPU-401 and USB MIDI (USB's audio class's MIDI streaming: a driver on GLOS's USB stack), with
    timestamps;
  - a stream out for the software synth;
  - the metronome's click through the mixer.
- **THREAD:** the sequencer's player and the software synth's rendering on threads of their own.
- **INPUT:** the PC's keys as a piano: key up and down events without repeat (the toolkit's raw key events).
- **FS:** long file names; the loop library's folder.

## Libraries

- **The software synth:** TinySoundFont (MIT), with a permissively-licensed General MIDI SoundFont (to be chosen,
  as the Audio Player's).
- **The FM patches:** a public-domain OPL3 General MIDI bank.
- **MIDI files:** GLOS's own reader and writer.

## CPU floor

- **A 486DX2:** editing, and playing through the OPL3 or an MPU-401.
- **A fast Pentium II:** the software synth live; rendering to WAV works on slower CPUs, slower than the song.

## Tests

- **Host:**
  - MIDI files read and written, round-tripped against a corpus;
  - the sequencer's timing (events out at their times, with a fake clock);
  - quantise and the edits;
  - the OPL3 driver's register writes;
  - the software synth's render against a reference with a tolerance;
  - GUI scripts of the arrangement and the piano roll.
- **Loop A smoke:** a song played through 86Box's OPL3 (the recording shows its notes); MIDI out to 86Box's MPU-401
  (if its device logs or loops it back); a song saved and read back.
- **The P4:** the software synth's load; MIDI in from a USB keyboard if the owner has one there.
- **By hand:** listening on bench-g400; an external synth on an MPU-401 if the owner has one.

## Open points

- **USB MIDI:** the owner's MIDI keyboard(s), to capture their descriptors (as the pads' J0) before the USB MIDI
  driver is written.
- **An MPU-401:** which card would provide one at the bench (bench-g400's Crystal chip shows none; an SB16 or a
  wavetable card in the P4?).
