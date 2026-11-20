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

## The Player (2026-11-20): the first part

Plan: `docs/v1/apps/audio-player.md`. `apps/player` (PLAYER.EXE): `player.c` the window, `engine.c` the playing;
`apps/shared/source.c` a sound file as samples had as they are wanted, `wave.c` and `mod.c` under it.

- **A list of files,** each with its title (the song's own name where the file has one), length and kind; Enter
  or a double click plays a row. The one playing has a mark; its title, kind and rate are above the list, with
  the time, a slider that shows where it is and goes there when moved, and the volume of the Player's own
  stream (the Mixer shows the same). When a file ends the next begins; Repeat begins the list again, Shuffle
  takes any other. Files are added one at a time or a folder's at once; a list is saved and opened as an M3U
  playlist (a path a line; a path with no drive is beside the playlist). Files opens WAV, MOD and M3U files
  with it, and gives sound files their icon.
- **WAV** (PCM of 8 to 32 bits, IMA ADPCM) is read piece by piece from its file (`wave_in_*`: the same reader
  `wave_load` is made of), so a file may be longer than memory.
- **MOD modules** (`apps/shared/mod.c`): ProTracker's format and its descendants of 1 to 32 channels ("M.K.",
  "xCHN", "xxCH"), played in ticks as the format's descriptions have it: notes by the Amiga's periods with fine
  tuning, the volume and its slides, slides of pitch (up, down, to a note), vibrato and tremolo with their
  waves, arpeggio, sample offset, position jump and pattern break, pattern loop and delay, note cut, delay and
  retrigger, speed and tempo. A side's two channels of four at their loudest are full scale, a quarter of each
  side in the other. Each sample is taken on the line between the two it falls between (`mod_smooth(0)`: the
  nearer, for a slow PC). A song that goes round for ever ends where it comes to a row it has played; its
  length is found by going through it without sound, and seeking does the same to the place asked for (the
  samples after a seek are those of playing to there: tested). Whole numbers throughout.
  - `tests/host/mod_test.c` on songs `mod_cases.py` writes: each note's pitch by its cycles counted, loudness
    and sides, lengths to the frame for speeds, tempos, jumps, breaks, loops and delays, slides' ends, the
    arpeggio's mean, fine tuning, eight channels, a sample that ends by itself, files cut short, and 400
    modules with bytes changed at random (no harm, under the sanitizers too).
  - ffmpeg's libopenmpt plays the same songs to the same pitches, the same loudness of a soft note against a
    full one (0.250) and the same share of a channel in the other side (3.00), looked at once on the host.
- **The playing is a thread's** (`engine.c`): every 15 ms it fills the stream's ring from the file, under a lock
  it shares with the window's calls, so that the music goes on while a menu is open or a dialog is up. A
  module is played at the mixer's own rate. A seek while it plays closes the stream and opens another (what was
  queued is of the old place). FILES, IMAGES and NOTEPAD already link with the C library's lock for threads;
  the Player is the fourth.
- **Tests.** `tests/gui/player.txt`, host and Loop A, with no card and no clock (`PLAYER -demo`: F9 is ten
  seconds heard): a module and a WAV added, a text named .WAV refused, the module played, paused and on, the
  WAV begun by itself at its end, the list through, a playlist saved (its bytes checked), cleared, a playlist
  opened. The audio suite: the real Player given a WAV of 1175 Hz and a module of 259 Hz plays one after the
  other, says so, and both tones are in 86Box's recording.
- **Not yet:** S3M, XM and IT modules; VOC; MP3 and Ogg Vorbis (minimp3 and stb_vorbis, a Pentium's); MIDI
  through the OPL3; CD audio; tags; drag and drop and sorting in the list; the small view; a visualisation and
  the pattern view; gapless playing; a folder's files added in the background.
