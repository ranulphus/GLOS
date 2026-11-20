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
