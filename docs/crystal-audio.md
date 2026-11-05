# GLOS on Crystal audio: a plan

**Status (2026-11-05): plan v0, not started. The owner chose GLOS's own driver over UNISOUND, to start after upper
memory merges (docs/upper-memory.md). Written for bench-g400's onboard Crystal chip; the chip itself is still to
be confirmed on the PC (C0).**

GLOS's mixer plays only a Sound Blaster 16 (D47, `kernel/drv/sb16.c`: DSP version 4, 16-bit DMA). bench-g400, an
IBM PC 300PL, has onboard Crystal audio, which no DOS driver sets up, so it stays silent under GLOS and plain DOS
alike. This plan gives GLOS a driver of its own for the Crystal CS423x family:
- **it sets the chip up itself**, by the Crystal Key and the datasheet's SLAM method, with no DOS driver;
- **GLOS's mixer plays through the chip's Windows Sound System codec**, 16-bit stereo;
- **DOS programs in sessions get the chip's Sound Blaster Pro and FM sides**, as on any SB Pro.

PRD sections: §4.5 (sound), §10.5 (the mixer), D47. Docs to change at the end: PRD, supervisor.md's device
table, docs/devices.md, the release notes.

## Why not UNISOUND

The owner asked first for UNISOUND, the freeware DOS initialiser for ISA Plug and Play sound cards. It doesn't fit:
- it is closed freeware, so like UMBPCI (D26) it could be a test baseline at most, never committed or shipped;
- it only sets the card's DOS modes up; GLOS's mixer would still have no driver for the chip;
- users report it failing on the CS4236 and CS4237B, the likely chip here (Vogons).

## The chip

From IBM's technical manual and Linux's driver table: every PC 300PL (types 6862, 6892) has integrated Crystal
audio supporting Sound Blaster, AdLib and Windows Sound System; IBM names a 4237B, and Linux's `cs4236` driver
lists "IBM PC 300PL sound". Most likely a CS4236B or CS4237B, which are register-compatible. C0 confirms it.

From Crystal's CS4236B datasheet (DS212PP4, 1997), the clean-room source for everything below:
- **Six logical devices** behind ISA Plug and Play 1.0a: 0, the WSS codec with the FM synthesiser and the SB Pro
  interface, sharing an IRQ and the playback DMA channel; 1, the game port; 2, the control port; 3, MPU-401; 4, an
  IDE CD-ROM; 5, a modem. The usual motherboard resources: WSS 534h, FM 388h, SB 220h, IRQ 5, DMA 1 (and 0 or 3
  for capture), game 200h, control 120h, MPU 330h on IRQ 9 (Table 1).
- **Off at power-up:** the part is isolated from the bus until software configures and activates it. Its
  hardware-configuration and resource data come from an external EEPROM, or are hostloaded by the BIOS.
- **The Crystal Key and SLAM** ("Bypassing Plug and Play", p.19): the 32-byte Crystal Key to 279h puts the part in
  configuration mode; then a CSN, each logical device's registers written directly (15h device, 47h/48h/42h
  bases, 22h/27h IRQs, 2Ah/25h DMA, 33h activate), and 79h to activate the chip. One Crystal part per machine.
  Optionally the PnP and Crystal keys can be disabled until reset (55h/56h to CTRLbase+5); GLOS won't.
- **Mode switching is the chip's own:** SB Pro and WSS share the codec and switch "transparently to the host"
  (p.57): GLOS's mixer on the WSS side and a DOS program on the SB side don't need a mode change from GLOS.
- **The WSS codec** is CS4231A-class (MODE 2): 16-bit stereo, rates up to 48 kHz, playback by 8-bit DMA
  (count by byte), a playback count and an interrupt (DS139PP2, the CS4231A datasheet).

86Box emulates the family (`snd_cs423x.c`: CS4232, CS4235, CS4236, CS4236B, CS4237B, CS4238B, on-board and card
forms), including the Crystal Key, SLAM, ISA PnP and the EEPROM, so the driver can be built and tested in Loop A
(MGA-Glide `run.py --sound cs4236b`, an ISA card in the bf6) before the bench.

## The steps

- **C0. Look, changing nothing.**
  - **bench-g400, from Ubuntu** after the reset: `/sys/bus/pnp/devices/*/id` and `resources`, `/proc/asound/cards`,
    the kernel log's isapnp and pnp lines. Which IDs (CSC0000 and so on), whether the BIOS lists the chip as a
    PnP BIOS node or it is found by isolation, and with which resources.
  - **CRYSPROBE** (DOS, read-only), on plain DOS on the bench and in Loop A: the PnP BIOS's nodes, a WSS codec at
    the usual bases (its ID register), an SB DSP at 220h. Whether the BIOS has configured and activated the chip.
- **C1. Set the chip up** (`kernel/drv/crystal.c`, at boot, before the system VM runs DOS programs):
  - if it is already active (C0 shows the BIOS does it), use its resources as found;
  - else the Crystal Key and SLAM with GLOS's resources: the datasheet's defaults unless `[sound] crystal` says
    otherwise, and none that another device GLOS knows uses; refused, with the reason logged, when the part
    doesn't answer (no EEPROM and no hostload: the board's pin mapping is data GLOS doesn't have);
  - BLASTER (`A220 I5 D1 T4`) set for the system VM when the environment has none, so DOS programs find the SB Pro.
- **C2. The mixer on the WSS codec** (`kernel/drv/wss.c`, beside sb16.c): MODE 2, 16-bit stereo signed, at
  D47's rate (22050 Hz by default); auto-initialised DMA on the 8237's low controller (byte counts, a buffer within
  one 64 KB block below 16 MB); the codec's playback count and interrupt each half, as sb16.c does with the DSP.
  The card is a session's while one runs (the same hand-over as sb16.c), and stopped at `glos exit`, as the SB16
  is (the audio suite's SBCHK): no DMA into memory that is DOS's again.
- **C3. Loop A:** the audio suite gains Crystal cases (bf6 with `--sound cs4236b`, raw and HIMEMX): set-up by SLAM
  from power-up, the mixer's periods counted and every tone found in 86Box's recording (`--wav`, as the SB16
  cases do), a session playing through the SB Pro side (SDL's SB Pro path and an FM test), and DOS's state after
  `glos exit`. The same on the CS4232 and CS4237B for the family.
- **C4. bench-g400** (the owner's OK for each run): GLOS's apps through the mixer, DOSBench's games in sessions
  with sound, and plain DOS after `glos exit`.
- **C5. Docs:** PRD §4.5, §10.5, D47 (the mixer's cards), supervisor.md, devices.md, the release notes.

## Questions for the owner (before C1)

1. **Resources when GLOS sets the chip up itself:** the datasheet's defaults (WSS 534h, SB 220h, IRQ 5, DMA 1),
   or only what `[sound] crystal` names?
2. **After `glos exit`:** leave the chip set up and active, so plain DOS keeps its SB Pro, or deactivate it as
   found?
3. **Scope:** the CS4236B and CS4237B on the bench only, or the whole CS423x family 86Box models (CS4232 to
   CS4238B)?
4. **MPU-401 and the game port** (logical devices 1 and 3): set up for sessions too, or left alone?

## Provenance

Crystal's datasheets (DS212PP4 for the CS4236B, DS139PP2 for the CS4231A) and the ISA Plug and Play 1.0a
specification are the sources. Linux's `cs4236` and `wss` drivers are GPL: behavioural reference only, no code.
UNISOUND is not used.
