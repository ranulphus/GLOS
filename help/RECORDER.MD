# Sound Recorder

Records sound from the input the [Mixer](MIXER.MD) has chosen, shows it as a wave, and lets you trim it and
change it. It opens and saves WAV files.

| Keys | What they do |
|---|---|
| Ctrl+R | Record, onto the end of what is there |
| F5 | Play what is selected, or from the cursor on |
| F6 or Esc | Stop |
| Left, Right, Home, End | Move the cursor (with Shift: select) |
| Ctrl+A | Select everything |
| Ctrl+X, Ctrl+C, Ctrl+V, Del | Cut, copy, paste, delete |
| Ctrl+T | Keep only what is selected |
| Ctrl+Z | Undo the last change |
| Ctrl++, Ctrl+-, Ctrl+0 | Look closer, less close, at all of it |

Press in the wave to put the cursor there; drag to select.

- **Effects** work on what is selected, or on everything: Louder, Softer, As Loud as Fits, Fade In, Fade Out,
  Backwards, Silence.
- **Format** chooses one channel or two, the rate (8000 to 44100 Hz), and how the file is saved: 16 bits,
  8 bits, or ADPCM, which is small.
- The sound is held in memory, 24 MB at most. "Clipped" in the bar at the bottom means the input was too
  loud: turn it down in the Mixer.
