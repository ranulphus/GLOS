# The virtual Sound Blaster

Decided by the owner on 2026-11-06 (`docs/v1-route.md`, phase H) and put before the rest of phase E on
2026-11-19 ("Audio now"). This says how it is built, what is built so far, and what is left.

## What it is for

A DOS game talks to a Sound Blaster's ports itself. Until now a game in a session had the PC's real card, so
it had sound only where that card is a Sound Blaster or imitates one in hardware. GLOS has drivers of its own
for cards that are neither (and will have more: the CMI8738's native side, ES1371, AC'97, HD Audio), which play
through GLOS's mixer. The virtual Sound Blaster is the piece between: the game finds a Sound Blaster 16 at
`BLASTER`'s ports, and what it plays there comes out of GLOS's mixer, on whatever card the mixer has.

## The pieces

| Piece | Where | State |
|---|---|---|
| The DSP and the mixer chip | `kernel/vm/vsb.c`, `kernel/include/vsb.h` | built, host-tested |
| The DMA channels (a virtual 8237) | `kernel/vm/vdma.c`, `kernel/include/vdma.h` | built, host-tested |
| The ports in a session, the interrupt, the program's memory | `kernel/vm/vsbdev.c`, `vdev.c` | built, on Loop A |
| Into GLOS's mixer; the card kept through a session | `kernel/drv/mixer.c` | built, on Loop A |
| `BLASTER` for programs; `[sound] virtual` in GLOS.CFG | `mixer_blaster`, the settings | built, on Loop A |
| FM music | passed to a real FM chip at 388h; a synthesiser | passed through; the synthesiser after |
| MIDI at 330h | a real MPU-401's, as before | GLOS's own MIDI out after |

The first two are pure state with no I/O in them, as the virtual interrupt controller is (`vpic.c`), so that
the host's tests drive them with the port sequences a game sends (`tests/host/vsb_test.c`, `vdma_test.c`).

## The DSP (`vsb.c`)

A Sound Blaster 16's DSP, version 4.05, with the older cards' commands under it:

- **Found as a card is found:** the reset (a 1 and a 0 at port 6, then AAh to read), the version (E1h), the
  identification and test bytes (E0h, E4h, E8h), the speaker's state (D1h, D3h, D8h), the copyright string
  (E3h), an interrupt asked for (F2h, F3h: how programs find which interrupt the card has), and the mixer
  chip's registers 80h and 81h, which say the interrupt and the DMA channels.
- **Played:** 8 bits by the older commands (14h a block, 1Ch block after block, 90h and 91h "high speed"),
  at the rate a time constant gives (40h) and in the Pro's stereo when the mixer chip's register 0Eh says so;
  8 and 16 bits by the SB16's commands (Cxh, Bxh: signed or not, one channel or two, a block or block after
  block), at the rate 41h gives. A block's length is 48h's, or the command's own.
- **A block's end** is the card's interrupt, the 8-bit one or the 16-bit one, acknowledged by reading port Eh
  or Fh; which are waiting is in the mixer chip's register 82h. Block after block goes on until the program
  asks for its end (DAh, D9h: with the block under way), pauses (D0h, D5h; on again D4h, D6h), or resets.
- **Silence** for so many samples and then the interrupt (80h).
- **The mixer chip:** the SB16's registers, with the older cards' two-volumes-in-a-byte registers (04h, 22h,
  26h, 28h, 2Eh) kept in them; the master's and the voice's volumes are applied to what the DSP plays, in
  steps of 2 dB.
- **Not done:** recording; ADPCM (such a block's interrupt comes at once and nothing sounds: a program is not
  kept waiting); the direct DAC (a sample written by hand, 10h); the DMA identification (E2h).

What the program plays is turned into 16-bit stereo at the rate GLOS's mixer plays at, once a period of the
mixer's (`vsb_mix`): between two of the program's frames, the mixer's frame is the part of the way from one to
the other that its moment is. The mixer's period (about 12 ms) is the grain of it all.

**The program's bytes are taken a period before they sound:** each period the card has in hand what that
period plays and as much again. So a block's interrupt comes about a period before its last frame is heard,
and a program that asks for its next block from that interrupt (single blocks one after another: how many
games play, auto-initialised blocks being the Sound Blaster 2.0's and later) is heard with no gap where one
block ends and the next begins. It holds while the program answers its interrupt within a period and its
blocks are no shorter than one; the DMA's count is that period ahead of the ear. A block asked for at another
rate, after a pause, or once the one before has played out starts afresh.

## The DMA channels (`vdma.c`)

The program sets a DMA channel up as it would the 8237's: masked, the flip-flop cleared, the mode, the address,
the page, the count, unmasked. The channels `BLASTER` names (D and H) are virtual: their registers are kept
here, no real transfer is made, and the virtual card takes the bytes from the program's memory itself, moving
the channel's address and count on as it does (a program that reads the count to know where the card is sees
it move). At the terminal count the channel starts again (auto-initialise) or is masked, and the status
register says so, once.

The real chip's channels of the same numbers are never touched by a program: GLOS's own sound driver may be
playing through one. The other channels (the floppy's) are the real chip's, and a program's writes for them go
through; registers that all of a controller's channels share go through with the virtual channels' part taken
out (a master clear becomes a cleared flip-flop and the other channels' masks).

## In the system VM (`vsbdev.c`)

- **On when** GLOS's mixer has a card and `[sound] virtual` is not `off` (it is `on` unless said; a restart
  takes a change). The log says `GLOS-VSB on blaster="A220 I5 D1 H5 T6"`.
- **Its numbers** are `BLASTER`'s where DOS's environment has one: with a real Sound Blaster 16 under GLOS's
  mixer, a program finds the virtual card exactly where it would have found the real one. Otherwise A220 I5 D1
  H5, and GLOS puts `BLASTER` into the environment of the programs it starts (as it did for a Crystal chip's
  own Sound Blaster Pro side, which the virtual card now stands before).
- **The ports** (the card's sixteen, the two DMA controllers', the page registers) are trapped for the VM,
  real-mode and protected-mode programs alike. The DSP's and the mixer chip's are the virtual card's; the
  card's FM ports go to the real FM chip at 388h; the DMA's are `vdma.c`'s, which says what goes on to the
  real chip.
- **What plays** is asked for by GLOS's mixer once a period, in the sound card's interrupt: the DSP takes the
  program's bytes from the VM's first megabyte by its DMA channel and adds 16-bit stereo to the mixer's sum. A
  DMA buffer above the first megabyte is not memory GLOS can say is the program's: silence.
- **The interrupt** is raised in the virtual interrupt controller, on `BLASTER`'s line, whatever line the real
  card under the mixer has (GLOS's own driver has that one).
- **The mixer keeps the card all the time** while the virtual card is on: it was given to a session and taken
  back after. A program that wants the real card itself runs with `[sound] virtual = off`.
- **A session in direct mode** (`[program NAME] direct = 1`: its protected-mode code reaches the PC's ports
  untrapped) has the real card, as it always did: the mixer hands that over for the session and takes it back,
  the ports its real-mode code uses go to the real card too, and `BLASTER` is the real card's.
- **A program that has become an app does not find the card** (its ports read FFh there): an app plays through
  the mixer, and the card's interrupt goes to the system VM's programs alone.
- **A session's end** makes the card as after a reset, and the log says what the session played:
  `GLOS-VSB end starts=3 irqs=15 played=36959 peak=28672` (sounds begun, interrupts, frames given to the
  mixer, the loudest sample).
- **The mixer chip's volumes are at their loudest after a reset,** where a real card's are a little under: what
  a program plays goes into GLOS's mixer as it is, as loud as an app's own sound.

## Tests

- **Host:** `vdma_test` and `vsb_test` (the port sequences a game sends; samples out checked to the number).
- **Loop A, the audio suite's `vsb` cases:** `SBTEST` (`tests/dos/sbtest.c`, real mode) finds the card by
  `BLASTER` and plays it as a game does: one 8-bit block by a time constant, auto-initialised blocks and their
  end, eight single blocks one after another from the interrupt's handler, the SB16's 16 bits by the high DMA
  channel, each in its time. On an SB16, on a Crystal chip's codec
  (where `BLASTER` is GLOS's own), on the 486; and with `[sound] virtual = off` against 86Box's own SB16, where
  the same cases pass with the same timings: the test itself is right; and in direct mode, where it has the
  real card too. The suite's older cases (`audio_case`) run both ways: the virtual card on (the mixer plays from
  GLOS's start, keeps the card through a session, `BLASTER` is the virtual card's) and off (as before).
- **Loop A, the sdlwin suite's full-screen cases** (`sdlsess_case`): DOS-GL's SDLBEEP with SDL's own Sound
  Blaster driver (`SDL_AUDIO_DRIVER=soundblaster`) plays through the virtual card, 16 bits at 44100 a second
  in auto-initialised blocks, with none of SDL's chunks short; beside it the same program through GLOS's
  mixer itself (API 2.5), which is what SDL chooses when nothing is said.
- **On silicon (2026-11-19):** bench-g400's Crystal codec under GLOS's mixer, `SBTEST` at the virtual card: six
  cases of six, with the numbers 86Box gave (`GLOS-VSB end starts=3 irqs=15 played=36959 peak=28672`).
- **Echoes of the Void, by its session (2026-11-19, Loop A, full screen):** through SDL's Sound Blaster driver at
  the virtual card, none of 757 chunks short and its six test tones in the recording; through GLOS's mixer
  itself, two minutes of play, 10386 periods and none short.
- **Real programs, by the DOS-GL session (2026-11-19, Loop A):** PrBoom-plus's timedemo full-screen on SDL's
  Sound Blaster driver has its sound throughout; 12% of SDL's chunks are short with the virtual card and with
  86Box's own alike (the timedemo keeps SDL's mixing waiting, not the card). Half-Life's own Sound Blaster code
  finds the card (16 bits, 44100 a second, blocks of 32768 bytes) and plays for minutes.

## What is next

1. **Real programs:** the owner's games on a bench (each sound library drives a Sound Blaster its own way:
   Miles, HMI, DMX, id's), and a game with FM music.
2. **What they will want:** ADPCM, perhaps; the DMA's count moving between the mixer's periods (it moves a
   period at a time now, a period ahead); blocks shorter than a period; a rate above the mixer's brought down
   with a filter (it is taken between samples now: what lies above half the mixer's rate folds back).
3. **FM** for cards with no FM chip: a synthesiser (ymfm is the first candidate: BSD, C++, to be ported or
   run outside the kernel). **MIDI** at 330h into GLOS's own MIDI out.
4. **The bench:** the owner's games on bench-g400 (its Crystal codec is no Sound Blaster 16, and the virtual
   card is what they find there now); a card with no Sound Blaster side at all (the Pentium III's CMI8738, once
   it has its driver); what the virtual card costs a game beside the mixer itself (PrBoom's timedemo three
   ways, asked of the DOS-GL session).
