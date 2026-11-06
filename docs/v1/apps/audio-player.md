# Audio Player: plan (GLOS v1, phase A)

**New in v1.** A music player for these PCs: their own era's formats (tracker modules, MIDI through the OPL3 chip)
and today's (MP3, Ogg Vorbis) where the CPU allows.

## What it does

- **Formats:**
  - **WAV:** PCM 8 and 16 bits, mono and stereo, any rate, resampled to the mixer's.
  - **IMA ADPCM WAV and VOC:** Creative's, from DOS games.
  - **Tracker modules:** MOD (Protracker and its variants), S3M, XM, IT (without its newer filters at first).
  - **MIDI (.MID, Type 0 and 1):** through the OPL3 FM chip (the Crystal chip's or an SB16's: GLOS's FM driver,
    AUDIO's MIDI out), through an MPU-401 to an external synth, or through the General MIDI software synth with a
    SoundFont on a fast Pentium.
  - **MP3:** on a Pentium (75 MHz and up for 44.1 kHz stereo, by estimate; measured on the P4 and 86Box's Pentium).
  - **Ogg Vorbis:** on a Pentium.
  - **CD audio:** from a CD-ROM drive through MSCDEX (play, track list), where the drive's audio is cabled to the
    sound card.
- **Playing:** play, pause, stop, previous, next, seek, shuffle, repeat, volume (its stream's). Gapless where the
  format allows.
- **Playlists:** a list to drag files onto, saved as M3U; folders added whole; sorted by any column (title, artist,
  album, length, file) from tags (ID3v1 and v2 for MP3, Vorbis comments, module titles).
- **A small view:** the controls and the title only, kept on top (a setting); the task bar's button with play and
  pause on its menu.
- **A visualisation:** a spectrum and a scope, drawn in the window (and a tracker's pattern view for modules: the
  rows scrolling as they play).
- **In the background:** music keeps playing while the window is minimised; notifications for the next track
  (optional).
- **Opened by Files** for the formats it plays; "play" and "add to playlist" in Files' context menu.

## From toolkit 2

- **Lists:** the playlist (the detail view with columns, drag to reorder, files dropped on it).
- **Controls:** a slider for seeking (with time marks); transport buttons on the toolbar.
- **Display:** a canvas for the visualisation.
- **The small view:** a compact window mode kept on top (WIN's "on top").

## From API 2.0

- **AUDIO:**
  - a stream out with its own volume;
  - the mixer's rate and format;
  - MIDI out to the OPL3 (FM registers through GLOS, which owns the chip in the desktop) and to an MPU-401;
  - the stream's position (for the visualisation and seeking);
  - CD audio through MSCDEX's calls.
- **THREAD:** decoding off the window's thread, feeding the stream; the module and MIDI players' timing on a thread
  of their own.
- **WIN:** "on top" for the small view; the task bar's button menu (SHELL).
- **SHELL:** notifications (optional), the playing state on the task bar.
- **FS:** long file names, folders listed as they load.
- **IDLE:** "keep the screen on" isn't needed (audio only), but the screensaver mustn't stop the music.

## Libraries

- **MP3:** minimp3 (CC0).
- **Ogg Vorbis:** stb_vorbis (public domain / MIT).
- **Modules:** GLOS's own MOD/S3M/XM/IT player (libxmp is LGPL, so it is behaviour reference only), written from the
  formats' public descriptions.
- **MIDI:**
  - **FM:** GLOS's own MIDI-to-OPL3 driver (General MIDI patches for the OPL3 from a public-domain bank, to be
    chosen in C).
  - **Software synth:** TinySoundFont (MIT) with a small permissively-licensed SoundFont (to be chosen in C; size
    matters).
- **Tags:** GLOS's own ID3 and Vorbis comment readers.

## CPU floor

- **A 486DX2:** WAV, VOC, modules (at the mixer's rate), MIDI through the OPL3 or an MPU-401, CD audio.
- **A Pentium:** MP3 and Ogg Vorbis. The player says so on a slower CPU rather than stuttering.
- **A fast Pentium II:** MIDI through the software synth.

## Tests

- **Host:**
  - each decoder against reference decodes (the libraries' own test vectors, and files decoded by reference tools on
    the host, compared within a tolerance);
  - the module player against a reference renderer's output, with a tolerance;
  - the MIDI-to-OPL3 driver's register writes against recorded ones;
  - GUI scripts of the playlist and controls.
- **Loop A smoke:**
  - a WAV, a MOD and a MIDI (OPL3) played through the SB16 and a Crystal chip in 86Box;
  - the recording (as the audio suite's) shows the tones;
  - an MP3 on bf6.
- **The P4:** MP3 and Ogg at real speed (decoding time per second of audio), the software synth's load.
- **By hand:** listening with the owner on bench-g400.
