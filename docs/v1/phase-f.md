# Phase F, as built: the new apps

The route (`docs/v1-route.md`, F) and each app's plan (`docs/v1/apps/`) say what is wanted; this says what was
built, in the order it was built, and what is left. Phase F began on 2026-11-20, when phase E's list was done
(`phase-e.md`); its order is the route's: the audio apps first.

## The Mixer (2026-11-20)

Plan: `docs/v1/apps/mixer.md`. `apps/mixer/mixer.c`, MIXER.EXE: one window of strips, each a name, a slider
upright, a balance and a mute.

- **Volume:** GLOS's master, the same number the task bar's volume icon has, with the output's level beside it
  (a meter of two channels).
- **Sound card:** the chip's own channels where it has them (its master, wave, FM, CD, line, microphone: the
  SB16's mixer or the Crystal chip's codec, as AUDIO's CHANNELS lists them), each with a balance (the chip's
  left and right levels, shown as one volume and how far it leans), and the input that is recorded.
- **Programs:** a strip for every stream that plays, under its program's name, with its level; strips come and
  go as programs begin and stop playing, and the window is as wide as they need.
- **Set at once, and shown when another sets it.** What is moved here is written to the mixer as it moves. Ten
  times a second the mixer is read: a slider is moved from outside only when GLOS's number is another than the
  one read last. (The chip takes its levels in steps: what is read back is not always what was written, and a
  slider that followed every reading would creep under the hand that moves it.)
- **The volume icon's double-click opens it** (`kernel/wm/desktray.c`; the Control Panel's Sound page until
  now). It is in the Start menu with GLOS's other programs, and not among the desktop's icons: those are for
  what is used every day (`apps/files/files.c`: the programs' list says which).
- **Tests.**
  - `tests/gui/mixer.txt`, host and Loop A, on the test's own numbers (`MIXER -demo`: a card that is always
    there, streams that come and go at F9; the pictures are the same on every PC): no stream, one, two, the
    master by the keyboard, a stream's and a channel's mute and the input by the mouse, the first stream gone.
  - The audio suite (`audio_case`), the real Mixer on a tone that plays: its window opens; the master to
    nothing (Home) silences the output while the stream's own level stays; the stream's mute silences it;
    its volume ten steps down gives an output of that share. `AUDTEST levels` (new) reads the mixer back; the
    Mixer's own lines say what it set; the tone ends with no underrun.
- **Not yet:** each program's icon on its strip; the level of what is being recorded; a button for it on the
  Control Panel's Sound page; the chip's FM level against FMTEST's notes in the suite; the bench (by hand,
  with the owner).

## The Sound Recorder (2026-11-20): the first part

Plan: `docs/v1/apps/sound-recorder.md`. `apps/recorder/recorder.c`, RECORDER.EXE, on `apps/shared/wave.c` (sound
as samples in memory, its edits, WAV files), which the Player will read WAV files with too.

- **A take is recorded** from AUDIO's stream in (the input the Mixer has chosen) onto the take's end, at the
  take's rate and channels (a new take's: Format's, 8000 to 44100 Hz, one channel or two; 16 bits always). The
  input's level is beside the waveform while it records; "Clipped" stays in the status bar once a sample was at
  the top.
- **The waveform** shows each channel's lowest and highest sample for every column of pixels; a press sets the
  cursor and a drag selects, as do Left and Right (with Shift), Home and End; View zooms in on the cursor and
  out. Play (F5) plays the selection, or from the cursor to the end, through a stream out, a line going along
  with what is heard; Stop is F6 or Esc.
- **Edits,** each undone by Undo (one step, and Undo again does it again): Cut, Copy, Paste (in place of the
  selection), Delete, Trim to Selection; Effects on the selection, or on all of the take when nothing is
  selected: louder (a quarter more), softer, as loud as fits, fade in, fade out, backwards, silence; Format
  makes the take over as one channel or two, or at another rate.
- **Files:** WAV opened (PCM of 8, 16, 24 and 32 bits and IMA ADPCM, one or two channels; what a file's sizes
  say beyond its end is ignored) and saved as 16 bits, 8 bits or IMA ADPCM (Format says which; a quarter of
  the size). A take that was changed is asked about before it goes.
- **The take is in memory:** two bytes a sample, 5 MB a minute at 44100 Hz in stereo. A recording stops with a
  message when 24 MB are full or memory runs out. (`wave.c` has a WAV writer that takes samples as they come:
  for recordings longer than memory, not used by the window yet.)
- **`wave.c`'s arithmetic is whole numbers throughout,** so that the host's test holds every edit to the
  sample (`tests/host/wave_test.c`: ramps and steady levels whose results are known in closed form) and the
  same test passes on any compiler. `tests/host/wave_cases.py` writes the WAV files it must read (each kind of
  PCM, extra chunks, the "extensible" header, sizes that lie, a file cut short, IMA ADPCM from another packer
  than `wave.c`'s) and the files that must be refused, and reads back what it wrote with Python's own `wave`
  module and its own ADPCM unpacker. ffmpeg plays the ADPCM files `wave.c` writes (looked at once: it rounds a
  step its own way, and its samples are within half a hundredth of full scale of ours).
- **Tests.** `tests/gui/recorder.txt`, host and Loop A, on the test's own sound (`RECORDER -demo`: each Record
  puts a second of it on the take; no card is asked for): recorded, made as loud as fits, a piece selected by
  the keyboard, faded, cut and pasted, undone, a second second recorded, one channel, half the rate, saved, a
  new take, the file opened again and played. The audio suite: the real Recorder records some seconds from
  the card while a tone plays, saves, and the WAV fetched from the PC has the frames it said (118,528 on the
  Pentium's profile, 120,320 on the 486's). 86Box's SB16 records silence (the take's peak is 0): the sound in
  the samples is the bench's to hear.
- **Not yet:** takes longer than memory (recorded straight to a file, edited in pieces); the level while
  waiting; the timer (start and stop at a time); more steps of undo; the monitor; a scroll bar under the
  waveform; the waveform's columns kept (a long take is looked through for every picture).
