# Sound

GLOS plays every program's sound at once through the PC's sound card. It knows these:

- a Sound Blaster 16, or a card that is one to DOS (an ALS100, say);
- a Crystal CS423x chip, as many PCs of the late 1990s have on the main board;
- an ESS AudioDrive (ES688 to ES1888);
- the PCI cards and main-board sound of 1998 to 2003: C-Media CMI8738; Ensoniq and Creative ES1371, ES1373
  and CT5880 (the Sound Blaster PCI 64 and 128); the AC'97 sound of Intel and VIA main boards.

An ISA card is found by the BLASTER setting, or by itself if it is a Plug and Play card. The Control Panel's
**Devices** page says which card GLOS found.

- The **loudspeaker on the task bar** and the [Mixer](MIXER.MD) set the volume.
- The Control Panel's **Sound** page chooses the card and the mixing rate, and plays a test sound. Its **MIDI
  out** says where the Player and MIDI Studio play music: the card's FM synthesiser (fm), or a synthesiser
  plugged into its MIDI port (port).
- With no card that GLOS knows, GLOS's programs are silent; a DOS program can still use the card directly.

## DOS programs and games

A DOS program finds a **Sound Blaster 16** and plays through GLOS's mixer, even on a PC whose card is not a
Sound Blaster: choose Sound Blaster 16 in the game's set-up, at port 220, IRQ 5, DMA 1 and high DMA 5 (or as
DOS's BLASTER setting has it).

- Music made by the card's FM chip (most games' music) needs a card that has one.
- Recording from a DOS program does not work through it.
- If a program does not get on with it, "Virtual Sound Blaster" on the Sound page can be turned off (GLOS
  must be started again); DOS programs then have the real card to themselves.

## Music files and MIDI

The [Player](PLAYER.MD) plays WAV, MP3, Ogg, module and MIDI files. MIDI goes to the card's FM chip or to a
MIDI device on its MPU-401 port. [MIDI Studio](STUDIO.MD) writes MIDI files; the
[Sound Recorder](RECORDER.MD) records.
