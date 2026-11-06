# Sound Recorder: plan (GLOS v1, phase A)

**New in v1.** Recording from the sound card's input: the line in, the microphone, the CD's audio.

## What it does

- **Record:**
  - from the input chosen in the Mixer (line, microphone, CD), at 8 or 16 bits, mono or stereo;
  - rates 8000 to 44100 Hz (what the chip takes: the Crystal chip's codec, an SB16's ADC);
  - a level meter while waiting and recording, and a clip warning.
- **The take:**
  - a waveform to see, play, cut, trim, fade in and out, normalise and change volume;
  - undo;
  - mono to stereo and back; resample.
- **Save:** WAV (PCM), and IMA ADPCM WAV to save space; MP3 not planned (an encoder is heavy and its licences
  vary).
- **Timer:** start and stop at a time, or after a length (for recording from tape or radio).
- **Play back** through the mixer while editing.

## From toolkit 2

- **The waveform:** a canvas with scrolling, zoom, selection and a play position.
- **The meter:** a level-meter widget.
- **Basics:** the toolbar; the undo framework.

## From API 2.0

- **AUDIO:**
  - capture: a stream in from the chip's ADC, with its format and the input chosen, in buffers as they fill
    (the Crystal chip's capture DMA, `[sound] crystal`'s cap; the SB16's input);
  - the input's level and gain (the chip's mixer);
  - playing back as a stream out;
  - the monitor (hearing the input while recording) where the chip allows.
- **THREAD:** saving and the edits' work off the window's thread.
- **IDLE:** "keep the screen on" isn't needed, but a recording mustn't be stopped by the screensaver (savers never
  touch the sound).

## Libraries

None: WAV and IMA ADPCM are GLOS's own.

## CPU floor

A 486DX2: recording at 44.1 kHz 16-bit stereo is DMA and disk writes; editing a long take is slower there.

## Tests

- **Host:** the edits (cut, fade, normalise, resample) on generated tones, compared exactly; WAV and ADPCM written and
  read back.
- **Loop A smoke:** 86Box's sound cards record from a WAV fed to their input (86Box's line-in from a file, if its
  devices allow; else a loop from the output where the card mixes it); the take's tone is found in the file.
- **By hand:** recording a line-in source on bench-g400 with the owner.

## Open points

- **86Box's capture:** whether 86Box can feed a file to the Crystal chip's or the SB16's input decides the Loop A
  test. Otherwise recording is tested on the bench only.
