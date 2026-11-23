# Phase H: hardware compatibility, as built

The plan is `docs/v1/hardware-audit.md`, approved by the owner on 2026-11-23: tier 1 (the fleet's own hardware),
then tier 2. This file says what was built, how it is tested and what is still owed, a part at a time. Work is
on branch `phase-h` (from phase-f's tip).

## The EHCI hand-off (2026-11-23)

`kernel/drv/ehci.c`. Before UHCI takes its controllers, each EHCI (class 0C/03/20) is:

- taken from the BIOS by its legacy-support capability: the OS's semaphore set, the BIOS's awaited for a second
  then cleared by force, its SMIs turned off;
- stopped, with its interrupts off;
- set to CONFIGFLAG 0, so that every port goes back to its UHCI companion and the devices plugged in are found
  again by uhci.c.

At glos exit CONFIGFLAG, the interrupts and the SMI enables are put back and the semaphore is given up. USB 2.0
speeds stay out of v1.

- **Tests:** builds, host-test. 86Box has no EHCI, so Loop A cannot test it.
- **Owed:** bench F (ICH6: 4 UHCI + EHCI) with a USB keyboard or mouse, under GLOS and after `glos exit`.

## Sound cards for the mixer (2026-11-23)

Four drivers, each a `struct sound_card` for `mixer.c`. Each plays 16-bit stereo in two halves by bus master,
records the same way, and is stopped at glos exit, because a bus master that went on recording would write into
memory DOS has back. The mixer tries them in this order, after the SB16 and a Crystal chip:

| Driver | Chips | Source | Loop A | Bench |
|---|---|---|---|---|
| `cmi8738.c` | C-Media CMI8738 (13F6:0111) | C-Media's CMI-8738/PCI-6CH specification | `audio` suite: bf6 + G200 + `cmi8738` | B (onboard) |
| `es1371.c` | Ensoniq ES1371/ES1373, Creative CT5880 (1274:1371, 5880) | Ensoniq's AudioPCI 97 specification; the converter's arithmetic as FreeBSD's BSD-licensed es137x has it | `audio` suite: bf6 + G200 + `es1371` | a spare card, in B or F |
| `ich97.c` | Intel ICH to ICH7's AC'97 (8086:2415 ... 27DE) | Intel's ICH datasheets | none (not modelled) | E, if its controller is an ICH |
| `via686.c` | VIA VT82C686A/B AC'97 (1106:3058) | VIA's VT82C686A data sheet | none (no VIA machine in Loop A) | E, if its controller is a 686 |

`ac97.c` is the codec's half for the last three. It resets the codec and waits until it is ready. It sets the
master, PCM, CD, line and mic levels, the record source and gain, and the variable rate. It maps GLOS's 0 to 255
levels onto the codec's attenuation and gain.

Per chip:

- **CMI8738:** eight fixed rates only; another is refused rather than played at the wrong pitch. The chip's
  legacy SB on channel 0 is off while GLOS plays and put back when GLOS stops. FM at 50h and the MPU-401 at 40h
  go to midi.c.
- **ES1371:** its sample rate converter takes any rate from 4000 to 48000. It has no FM synthesiser and its own
  MIDI UART is not used yet.
- **ICH and VIA 686:** they play at the codec's variable rates. A codec without them plays only at 48 kHz, so
  `[sound] rate = 48000` is then needed.

The downloaded specifications (C-Media's, Ensoniq's, VIA's) are reference only and are not in the repository.
Each driver's head names its source and its SHA-256.

- **Tests (Loop A, 2026-11-23 afternoon):**
  - **CMI8738:** passed the whole audio case once: tones, two streams, a session, the Mixer, the Sound
    Recorder, the Player, MIDI Studio, FM in a session, every tone in 86Box's recording, exit.
  - **ES1371:** plays in every stream.
  - **Its recording:** 86Box's model has the ES1371's ADC registers but never runs the channel or raises its
    interrupt, so recording is a bench's to prove.
  - **Open:** in two runs out of five, a card's playback stopped for good about 18 seconds in (1,544 and 1,546
    interrupts), at the step where the Mixer app opens an input and the mixer starts recording on the card. The
    handlers now go round until their card says nothing is pending, and a start clears what was left; the ES1371
    has not stopped since. Until neither stops, the two cases are out of the gate's audio suite
    (`AUDIO_CASES_H`, run by `LOOPA_AUDIO_SOUND=cmi8738,es1371`).
  - **ES1371 codec:** its codec's vendor reads as FFFFFFFF in 86Box (its model's codec).
- **Owed:**
  - each chip on its bench;
  - the CMI8738's FM on bench B's chip (an OPL3 on most);
  - the ES1371's codec writes on silicon: FreeBSD waits for a window of the converter's before each write,
    which is not done here yet.

## Tier 2: write-combining for the Rage 128 Pro (2026-11-23)

`[display] write_combining = auto` covers 1002:5046 (the Rage 128 Pro). Its registers are in a BAR of their
own, unlike the Rage Pro's. DOS-GL checked it on bench B with MTRR LFB ON by hand: GTA's Glide replay frames
were byte-identical, Glide conformance and DOS-GL's 35/35 were as before.

## Tier 2: AMD PCnet-PCI (2026-11-23)

`kernel/drv/pcnet.c` drives the Am79C970A (1022:2000), from AMD's data sheet (Rev D):

- 16-bit I/O and SWSTYLE 2 (32-bit structures);
- rings of 16 receive and 8 transmit descriptors, and the initialisation block, in one page;
- the station's address from its PROM; every multicast taken;
- short frames padded in software; received frames handed on without their CRC.

It is probed after the Rhine and before the NE2000.

- **Tests:** builds. Loop A's NET_CASES gain bf6 and 486dx2 with `pcnetpci`. Loop A's run.py takes the card
  only with RetroGPU branch `glos-pcnet` (one line in `tools/loopa/hostio.py`'s NICS), whose merge waits for
  the owner's OK; until then the cases run with `RETROGPU=~/wt-rgpu-pcnet`.

## Still to come in tier 1

- **Bench checks:** the RTL8139 on bench F, the ALS100 with sb16.c on bench C (once fitted), PLIP between two
  benches, the Xbox 360 pad.
- **GLOS on benches D and E:** setup floppies `glos1440-bench-d.img` and `glos1440-bench-e.img` were made on GLOS
  main 30ce832 (`make floppy FLOPPY_HOST=bench-d`, from the host). The owner writes them to floppies and starts
  each PC from its floppy with its network cable in; `tools/floppy/install.py bench-d.local` does the rest.
