# Mixer: plan (GLOS v1, phase A)

**New in v1.** The volumes of everything that makes a sound, after Windows' Volume Control. Opened from the task
bar's volume icon (double-click) and from the Control Panel's Sound page.

## What it does

- **Master:** the overall volume and mute (the same as the volume icon's slider).
- **Each stream:** every app's stream by the app's name and icon, with its own volume and mute, live as apps start
  and stop playing (GLOS's mixer, D47, already mixes them).
- **The chip's mixer:** the hardware's own levels where the chip has them:
  - wave, FM (MIDI), CD audio, line in, microphone;
  - the input chosen for recording.
  
  These are the Crystal chip's codec registers and the SB16's mixer. DOS programs in sessions get the hardware's
  levels as left here.
- **Levels:** a meter for the output and each stream.
- **Balance:** left and right, for the master and each stream.

## From toolkit 2

- **Volumes:** vertical sliders with a mute check under each, in a row that grows as streams come and go.
- **A level meter.**

## From API 2.0

- **AUDIO:**
  - the streams: list, names, volumes, mutes, levels, and an event when one starts or stops;
  - the master volume and mute;
  - the chip's mixer channels and the recording input.

  The SB16's mixer and the Crystal chip's codec registers, behind one description of channels.
- **SHELL:** the volume icon's slider shares the master.

## Libraries

None.

## CPU floor

A 486DX2.

## Tests

- **Host:** GUI scripts with recorded AUDIO answers (streams appearing and going).
- **Loop A smoke:** a stream's volume changed here halves its samples in the recording (the audio suite's WAV); the
  master's mute silences all; the chip's FM level changes FMTEST's notes.
- **By hand:** on bench-g400 with the owner.
