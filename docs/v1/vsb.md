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
| The ports in a session, the interrupt, the program's memory | `kernel/vm/` (the trapped ports) | next |
| Into GLOS's mixer; the card kept through a session | `kernel/drv/mixer.c` | next |
| `BLASTER` for programs; `[sound] blaster` in GLOS.CFG | the shell, sessions, the settings | next |
| FM music | passed to a real OPL where the card has one; a synthesiser | after |
| MIDI at 330h | passed to a real MPU-401, or GLOS's MIDI out | after |

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
the other that its moment is. The program's bytes are taken from its DMA channel a few at a time (64), so a
block's interrupt comes when its last bytes are all but played; the mixer's period (about 12 ms) is the grain
of it all.

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

## What is next

1. **In a session:** the card's ports and the DMA's trapped (`cpu_io_trap`, `kernel/vm/vdev.c`), the DMA's
   bytes read from the system VM's memory, the interrupt raised in the virtual interrupt controller.
2. **Into the mixer:** `vsb_mix` called from the mixer's period, and the card kept by the mixer through a
   session while the virtual card is on (as it is for a full-screen program's own stream since API 2.5).
3. **`BLASTER`** in the environment of the programs GLOS starts, where DOS's own has none; `[sound] blaster`
   to turn the virtual card off (the real one is the session's then, as before) or say its numbers.
4. **Tests on Loop A:** a DOS test program that finds the card and plays a block each way, with what GLOS's
   mixer played counted; then the ports' sound runs (Half-Life, PrBoom: RetroGPU's) and SDLBEEP.
5. **FM** passed through to a real OPL; a synthesiser for cards with none (ymfm is the first candidate: BSD,
   C++, to be ported or run outside the kernel). **MIDI** at 330h.
6. **The bench:** a game with sound on the Pentium III's CMI8738 once that has its driver.
