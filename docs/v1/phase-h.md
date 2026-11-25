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
- **Bench B (2026-11-23, late evening):** its EHCI (00:0e.2, five ports) was handed over at start-up
  (`GLOS-USB ehci 00:0e.2 handed bios=0 forced=0 ports=5`: the BIOS did not hold it) with no trouble after.
- **Owed:** bench F (ICH6: 4 UHCI + EHCI) and bench G (VIA: 3 UHCI + EHCI) with a USB keyboard or mouse, under
  GLOS and after `glos exit`.

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
| `via686.c` (the same file) | VIA VT8233, VT8235, VT8237 AC'97 (1106:3059) | VIA's VT8235M data sheet | none (not modelled) | G (a VT8235) |

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
- **VIA VT8233 family (bench G, added the same evening):** the AC-link and the codec register are the 686's;
  the channels are not. It plays through DXS channel 0, whose own rate converter takes any rate to the codec's
  48 kHz (so no variable rates are needed) and whose volume, muted after a reset, is set to 0 dB. It records
  through write channel 0 at the codec's ADC rate. The codec layer sets the DAC's and the ADC's rates
  separately for it (`ac97_rates`). If bench G is silent, the first thing to try is configuration 41h's bit 2
  (slots 3 and 4 given to the multichannel channel), which is set as on the 686.

The downloaded specifications (C-Media's, Ensoniq's, VIA's) are reference only and are not in the repository.
Each driver's head names its source and its SHA-256.

- **Tests (Loop A, 2026-11-23 afternoon):**
  - **CMI8738:** passed the whole audio case once: tones, two streams, a session, the Mixer, the Sound
    Recorder, the Player, MIDI Studio, FM in a session, every tone in 86Box's recording, exit.
  - **ES1371:** plays in every stream.
  - **Its recording:** 86Box's model has the ES1371's ADC registers but never runs the channel or raises its
    interrupt, so recording is a bench's to prove.
  - **The stops (afternoon):** in some runs a card's playback stopped for good, about 18 seconds in (1,544 to
    1,548 interrupts), the step where the Mixer app opens an input. The first guesses were the handlers (they
    now go round until their card says nothing is pending, and a start clears what was left) and 86Box's
    CMI8738 restarting a channel's DMA at each write of its enable byte. Neither was the cause. Until a fix,
    the two cases were taken out of the gate's audio suite (`AUDIO_CASES_H`, run by
    `LOOPA_AUDIO_SOUND=cmi8738,es1371`).
  - **ES1371 codec:** its codec's vendor reads as FFFFFFFF in 86Box (its model's codec).
  - **The stops found (evening):** the mixer now logs both PICs and the card when the card stops. The CMI8738
    stopped after 79 interrupts, at start-up: its channel pending and its line high (`st=80000001`), and the
    PIC holding no request for it. The edge was lost. 86Box's keyboard controller lowers IRQ 1 and IRQ 12 at
    every byte it puts out (`kbc_at.c`, `kbc_do_irq`), and its BIOS gives IRQ 12 to PCI when there is no PS/2
    mouse. So a PCI card on IRQ 12 loses its request at a keystroke or a reply to the mouse probe, and with an
    edge-triggered PIC its line, already high, never makes another. That explains the stops at the same step,
    where the script types, and that they came in some runs only.
  - **The fix, for every card:** `mixer_tick()` (every kernel tick) runs a playing card's handler by hand
    (`card->kick`, the four PCI drivers) after 125 ms with no interrupt from it. Its status is answered, the line
    falls, and the next half's rise is an edge again. The first four kicks are logged with the PICs' registers,
    and all are counted in `glos audio` (`kicks=`). A real PC's ELCR makes a PCI line level-triggered, so a bench
    should rarely see one. The network thread already services its card at every turn, so a network card on
    IRQ 12 costs a delay at most.
  - **Results:** the CMI8738 passed the whole case twice running (two and three kicks, the first at about 1,550
    interrupts each time, where it used to stop for good). The ES1371 passed (one kick at 1,576). Both cases
    are back in the gate's audio suite (`AUDIO_CASES`).
  - **ES1371 in Loop A:** 86Box's model records nothing, so its case checks that the Recorder says "recorded 0"
    and ends. It goes on recording silence after the last sound, so a large recording now has its silent end
    cut off before its tones are checked (`wav_for_check`). It has no FM chip, so the Player's MIDI file is refused. The Player now passes over a file it can't
    play when the list goes on by itself (it used to stop the list with a message), so the MP3 and Ogg after it
    play.
- **Bench B's real CMI8738 (2026-11-23, late evening; GLOS main d235e81 put on the P3):** the driver's first
  silicon. Found (I/O DE00h, IRQ 10, PCI revision 10h) and playing: 86 interrupts a second at 22050 Hz with
  256-frame halves, as it should be, its count level with the mixer's periods, no watchdog kick. Not yet
  listened to by anyone.
  - **Its FM and MPU-401 were not found at first.** This chip is older than the one 86Box models: its ports
    40h and 50h read 0, and its FM chip and MPU-401 are at the legacy addresses alone. The driver now sets the
    legacy control register's first choices (388h and 330h; put back at exit), and `midi.c` tries those when the
    chip's own ports answer nothing: an OPL3 at 388h and the MPU-401 at 330h are found.
  - **The FM test was too fast for a PCI chip** (found on the way): it waited for the chip's 80-microsecond
    timer by 200 reads of the status port, which on a PCI port take well under that. It now polls for the
    timer's flags, paced by port 80h, for up to 4 ms; the MPU-401's waits are paced the same way.
- **Bench G's VIA VT8235 (2026-11-24 morning; the native-dos session ran it):** the driver's first silicon.
  - **First run, refused:** the codec was found (its ID read as 434D434Dh) and `via_start` then refused
    ("error=rate-or-size"). The controller's valid flag, left set by the codec read before, answered the next read
    at once with old data: the rate read back after it was set was wrong, and so was that first ID. No emulator
    models this controller.
  - **Fixed:** every codec command carries the valid flag, which clears it, and a write is waited out before the
    next access (as FreeBSD's VIA drivers do; the 686's path has the same change).
  - **Second run:** the codec is a C-Media CMI9761 (434D4983h) with no variable rates, the case the DXS channel's
    own rate converter is for. Playback starts at 22050 Hz: 86 interrupts a second, level with the mixer's
    periods, no watchdog kick, on IRQ 12 shared with a UHCI the BIOS still holds. Not yet listened to.
  - **Recording there** wants `[sound] rate = 48000` (the codec records at 48 kHz only).
  - A refused "crystal" chip was listed in Devices on this PC, which has none: with `crystal = auto` a conflict
    of the chip's default resources is now only logged.
- **Bench A (the Rage 128 Pro since that evening), same build:** `write_combining = auto` turned its aperture
  on by itself (MTRR range 3, 32 MB at F0000000h).
- **Owed:**
  - each chip on its bench;
  - the CMI8738's FM on bench B's chip (an OPL3 on most);
  - the ES1371's codec accesses on silicon. They now wait for the converter's safe window, as FreeBSD's es137x
    does (its source read as a BSD-licensed reference). In 86Box the window's bits always read 0, so the wait runs
    out its 4,096 reads each time; a real chip is the judge.

## Tier 2: write-combining for the Rage 128 Pro (2026-11-23)

`[display] write_combining = auto` covers 1002:5046 (the Rage 128 Pro). Its registers are in a BAR of their
own, unlike the Rage Pro's. DOS-GL checked it on bench B with MTRR LFB ON by hand: GTA's Glide replay frames
were byte-identical, Glide conformance and DOS-GL's 35/35 were as before.

The Matrox G550 (102B:2527) was in the G-series list from the start, unchecked. DOS-GL checked it on bench G the
same night: its 32 MB frame buffer write-combining by GLOS's choice, DOS-GL's conformance 35/35 through the WARPs
with it on.

The Radeon 7000 VE (1002:5159) joined the list on 2026-11-24. DOS-GL measured it on bench B with MTRR LFB ON
by hand: 16-bit stores 6 to 75 MB/s, a 640x480x16 frame copied in 12.2 ms where it took 47.8; DOS-GL's
conformance 35 of 35 and Glide's as without it. Its range is 64 MB (its frame buffer's size; the others' stays
32 MB at most). The Radeon 7500 LE (1002:5157) followed the same day, the owner having swapped it into bench B:
the same figures, 64 MB again.

## Tier 2: AMD PCnet-PCI (2026-11-23)

`kernel/drv/pcnet.c` drives the Am79C970A (1022:2000), from AMD's data sheet (Rev D):

- 16-bit I/O and SWSTYLE 2 (32-bit structures);
- rings of 16 receive and 8 transmit descriptors, and the initialisation block, in one page;
- the station's address from its PROM; every multicast taken;
- short frames padded in software; received frames handed on without their CRC.

It is probed after the Rhine and before the NE2000.

- **Tests:** Loop A's NET_CASES gain bf6 and 486dx2 with `pcnetpci` (one line in RetroGPU's
  `tools/loopa/hostio.py` NICS: main 89b60a1, merged with the owner's OK); gate 47 passed them.

## Tier 2: DEC's Tulip (2026-11-23)

`kernel/drv/tulip.c` drives the 21140 (1011:0009) and the 21142/21143 (1011:0019), from Intel's 21143 hardware
reference manual (278074-001):

- the CSRs in I/O space; descriptors in ring mode, 16 receive and 8 transmit, in one page;
- the station's address from the serial ROM (bytes 20 to 25 of the 21X4 SROM format), read bit by bit through
  CSR9, as a 93C46 and then as a 93C66;
- a setup frame first, for the perfect filter: its own address and the broadcast; every multicast taken;
- the MII/SYM port at 100 Mbit, half duplex. The SIA's 10 Mbit ports and autosensing are not done; a bench
  Tulip would want them.

The Tulip's clones (Macronix, Lite-On, ADMtek, Davicom) have other IDs, ROMs and PHYs, and are not driven. It
is probed after the PCnet and before the NE2000.

- **Tests:** Loop A NET_CASES `bf6` with 86Box's `dec_21140_tulip_migrate` (its 21140) and `dec_21143_tulip`:
  both passed (DHCP, echo, mDNS) on the first runs. run.py takes the cards from RetroGPU main 7127504 (two
  lines in hostio.py's NICS, merged with the owner's OK the same evening); with an older RetroGPU the cases are
  left out.

## Tier 2: ESS AudioDrive (2026-11-23)

`kernel/drv/ess.c` plays the ESS AudioDrives (ES688, ES1688, ES1868/1869, ES1878/1879, ES1887/1888): ISA chips
on many cards and boards of 1995-1999, bench D's among them (an ES1887). As a Sound Blaster they are a Pro (DSP
3.01), which `sb16.c` refuses. This driver plays them in ESS's Extended Mode, from ESS's ES1868 data sheet:

- found at BLASTER's port by their answer to E7h (68h, 8xh); their interrupt and 8-bit DMA channel are BLASTER's
  I and D (they have no H);
- 16-bit signed stereo by auto-initialised DMA (demand mode, four bytes a request), interrupting at each half;
  the rate from the 397.7 or 795.5 kHz clock (22050 plays at 22097 Hz), the filter at 80% of half the rate;
- the chip's mixer through its four-bit Extended Access registers; the master, the voice and FM set full when it
  is found (88h after a reset, some 30 dB down in all: the first run's tones were 37 dB below the CMI8738's, and
  MIDI Studio's FM note too faint to find);
- reset to a Sound Blaster Pro when the mixer stops it and at glos exit.

The mixer tries it after a Crystal chip and before the PCI cards. The virtual Sound Blaster then offers sessions
"A220 I5 D1 T6": an SB16 doing its 16-bit transfers on the 8-bit channel, as an SB16 does when BLASTER has no H.

- **Its FM** answers as an OPL3 at 388h, where `midi.c` looks at every start: Loop A's ES1688 case finds it
  (`GLOS-MIDI fm port=388 chip=opl3`) and MIDI Studio plays through it (2026-11-25).
- **Not yet:** recording (the first DMA channel plays or records, not both; full duplex needs the second, which
  the ES1868 and later have and 86Box's ES1688 hasn't); its MPU-401; both wait for bench D's ES1887.
- **Tests:** Loop A's audio suite gains bf6 + G200 + 86Box's `ess_es1688` (BLASTER "A220 I5 D1 T4"). The Sound
  Recorder must say it can't record. Bench D's ES1887 is owed.

## Shared interrupt lines (2026-11-24)

The PCI sound drivers took their line without asking, which on a PC whose sound chip shares the network card's
line would have cost the network its interrupts. They now follow the devices rule, and the rule's step D2 was
built for GLOS's own drivers: handlers on one line are chained, and an idle sound chip no longer gets a
network card refused. `docs/devices.md` has the detail; the audio suite's `es1371-share` case proves both
drivers at work on one line in Loop A.

## Tier 2: ISA Plug and Play (2026-11-23, night)

`kernel/drv/isapnp.c` is the protocol, from Intel's and Microsoft's Plug and Play ISA Specification 1.0a: the
initiation key, each card isolated by its 72-bit identifier and given a number, its resource data read, a
logical device set up and turned on. It is plain C over port callbacks. `kernel/drv/pnpbus.c` is the kernel's
side.

- **When it runs:** at `glos pnp`, and at start only on a PC with no BLASTER on which neither a Crystal chip
  nor a PCI sound card was found: the mixer then looks for a PnP AudioDrive, and for a PnP Sound Blaster 16 or
  ALS100, last of all. A PC whose card is found by other means is not touched.
- **What a scan changes:** the cards' numbers, nothing else. A logical device a BIOS or a DOS program has turned
  on is reported and used as it is.
- **Setting one up:** its preferred I/O ranges (its first dependent function's), an interrupt from 5, 7, 10, 11,
  9, 3, 4 that no PCI device, serial port or device of the PC's own has, DMA channels from 1, 3, 0, 5, 6, 7.
- **READ_DATA port:** 20Bh, then 213h, 23Bh and 3C3h if no card answers (something else answering on the first
  looks like no card).
- **First user, the ESS driver:** with no BLASTER, `ess.c` asks for ESS's audio device (the one with ports, an
  interrupt and a DMA channel), has it turned on if it isn't, and probes there. That is for the ES1868 and
  ES1869 class; bench D's ES1887 is not a PnP chip and wants BLASTER.
- **Second, the SB16 driver:** with no BLASTER, `sb16.c` asks for Creative's (CTL) or Avance Logic's (ALS) audio
  device, the one with two DMA channels, the second a 16-bit one. That is for the ALS100 Plus+ meant for bench
  C, whose 486 has no PnP BIOS to turn it on. Unproven until the card is in a bench.
- **`glos pnp`:** scans again and lists each logical device: its card, its ID, on or off, its resources.
- **Tests:**
  - host: `isapnp_test`, against a model of three cards on one bus (two alike but for one serial bit; logical
    devices with dependent functions, fixed I/O, a three-byte interrupt item; one set up beforehand), an empty
    bus, and a READ_DATA port that something else answers on;
  - Loop A: 86Box's Crystal cards are ISA PnP cards. `glos pnp` finds the CS4236B (card CSCA736, four logical
    devices) with the resources `crystal.c` gave it (WSS 534h, FM 388h, SB 220h, IRQ 5, DMA 1), twice running
    while it plays; the Crystal audio cases check it.
- **Not yet:** the PnP ESS cards in Loop A (86Box wants their PnP ROM images, which RetroGPU's ROM set lacks);
  the Devices page listing PnP cards; the OPTi 82C931, the PnP Sound Blasters and PnP NE2000s through it.

## Tier 2: OPTi 82C93x (begun 2026-11-23)

Not built yet. What was found:

- **The chip:** OPTi's 82C931 data book (rev 2.1, August 1997; sha256 1c73a813...4f1f, from bitsavers' mirror).
  The configuration is in indirect "MC" registers (MCIR1-23) behind a password: E4h written to 0F8Dh, its only
  fixed port. Bits 4-0 there place the MC index and data ports (0E0Eh-0FFFh). The Sound Blaster Pro is at
  220/240, the WSS codec (AD1848/CS4231 class) at 530/640/E80/F40 (the codec's index at WSBase+4), and an OPL3
  at 388. In PnP mode the resources come from ISA PnP.
- **The plan:** split `crystal.c` into its codec half (the CS4231-class codec at a base, shared) and its chip
  set-up, and write an OPTi set-up beside it: read or set the WSS base, IRQ and DMA through the MC registers
  (or PnP), then play through the shared codec code.
- **Loop A:** 86Box's `opti_82c931` needs its PnP ROM image (`roms/sound/opti931/adsrom.bin`) in RetroGPU's
  sparse ROM set (the owner's OK first). The ROM-free `opti_82c930` sets itself up in another way (its own MC
  scheme). The owner's spare card is an 82C931.

## Still to come in tier 1

- **Bench checks:** the RTL8139 on bench F, the ALS100 with sb16.c on bench C (once fitted), PLIP between two
  benches, the Xbox 360 pad.
- **GLOS on benches D and E:** setup floppies `glos1440-bench-d.img` and `glos1440-bench-e.img` were made on GLOS
  main 30ce832 (`make floppy FLOPPY_HOST=bench-d`, from the host). The owner writes them to floppies and starts
  each PC from its floppy with its network cable in; `tools/floppy/install.py bench-d.local` does the rest.

## ATI cards under GLOS with DOS-GL's devices (2026-11-24)

RetroGPU main 8148677 gave DOS-GL devices for the Rage 128 Pro and the Radeon 7000/7500. GLOS's own display
stays VBE on them; what changes is GLOS's GL programs and full-screen DOS-GL programs:

| Bench | Card | Checked under GLOS |
|---|---|---|
| A (IBM 300PL) | Rage 128 Pro | The OBJECTS saver (a full-screen GLOS window) drew right, and the desktop came back whole after the wake. Cushion in its desktop window drew right at 7 fps (the software renderer, as for any window). Echoes' ECHB, a full-screen DOS-GL program, ran nine runs on the card's CCE ring under GLOS. |
| B (P3) | Radeon 7500 LE | write_combining = auto took the 64 MB range by itself. TEXCUBE (linked from 8148677) ran full screen on the card's command processor for a minute, then GLOS killed it (no close of DOS-GL's ran); DOS-GL's DPROBE (it only reads registers) then showed the case is real: surface 0 still set over the tiled depth buffer (`info=00030050 lower=0012c000 upper=001c1fff`). After GLOS's desktop mode set (`glos display on`, whose `radeon_surfaces_off` runs first) it read `info=00000000 lower=00000000`, as the untouched surfaces do. The desktop at 1024x768x16, over that range, was clean. The CMI8738 plays: the NEC EHCI on its line is quiet (a quiet driver for EHCI, after main a39561b refused the sound chip for it). |

## Bench F's new cards: the CT5880 on a real chip (2026-11-24)

The owner had fitted a CT5880 (1274:5880 rev 2) in bench F, whose G450 has given way to a Radeon X550 (PCIe;
RV370, 1002:5b63; GeCube's HM550L, subsystem 18bc:1070, as its BIOS names it). Its GLOS (16 November's) refused the network card for a shared line; today's build took it (RTL8139
on IRQ 11) and showed what was left:

- **The sound card refused for the ICH6's SMBus controller** (8086:266a, on IRQ 5 with it and a UHCI), which had
  no quiet driver. One now (`kernel/drv/dev.c`, `smb_quiet`, from Intel's ICH data sheets): quiet when neither
  HST_CNT's INTREN nor SLV_CMD's host-notify enable is set, or when HOSTC routes it to SMI; a transfer under way
  with INTREN set is somebody's. Bench F's: `how=no-intr`.
- **The CT5880 found but silent**, not one interrupt, its DAC2 never moving. Three faults of the driver's that
  86Box's ES1371 model hides:
  1. *The codec was never answering.* Its "ready" read all ones (vendor `ffffffff`): STATUS bit 29 is the
     CT5880's AC'97 reset line, 0 at power-on. Now pulsed (0, then 1, 20 ms) at every start, for revisions 2-4;
     `ac97_start` takes a codec reading all ones or all zeros as none (`GLOS-AC97 none`).
  2. *The converter's disables were read back*, and the chip reads `00470000` there whatever was written: every
     write that kept them as read (the codec window's among them) turned the converter off. They are the
     driver's own copy now.
  3. *The converter was turned on before every rate was set* (DAC1's never was), which FreeBSD's es137x warns can
     lock the chip until its power is cut: it did (a constant `00470000`, no progress after GLOS's restarts). Now
     every rate (ADC, DAC1, DAC2) is set with the converter off, then it is turned on.
- After a power cycle the TriTech codec answered (`vendor=54524123`) and the card played. Eight GLOS restarts in
  a row since: all played, six at once and two after one more codec reset (about one start in three finds the
  codec's ready bits still clear after half a second: it is reset again with a longer wake, up to three tries).
  One of the eight read the codec's first vendor word as `ffff`: a codec read can still be lost now and then.
- The watchdog's kick of an ES1371 now says the chip's registers every 64th time (`GLOS-ES kick n=...`).
- Not yet: listening to it; recording on it; MIDI.

## A native boot's Sound Blaster 16, with no BLASTER (2026-11-25)

A PC started with no DOS has no AUTOEXEC.BAT to set BLASTER, and the native-dos session found that a jumpered
SB16 (86Box's, and most real ones) was never found. Now, natively and with no `[sound] blaster` in GLOS.CFG, after
the PCI cards and the Plug and Play ones, `sb16.c` looks where an SB16 can be, 220h, 240h, 260h, 280h: a port
range a found network card or a PnP device that is on holds is passed over (`pnpbus_io_busy`); a DSP that answers
4.xx gives its interrupt and DMA channels from its mixer's 80h and 81h (Creative's hardware guide); an older DSP or
an ESS is said and passed over. `midi.c` then looks for its MPU-401 at 330h, then 300h unless a network card is
there. Loop A, natively on the bf6: `GLOS-SB legacy port=220 irq=5 dma=1 dma16=5 mixer=02,22`, `GLOS-MIDI mpu
port=330`, played; the hosted case unchanged (BLASTER says, the probe never runs). A card at another place, or an
ESS, still wants `[sound] blaster` (the native installer carrying the old DOS's BLASTER over is the native-dos
session's script).

## The Devices page's ISA Plug and Play cards (2026-11-25)

The device list (`kernel/drv/devlist.c`: the Control Panel's Devices page, `glos devices`, the shell's DEVICES) now
has a line for each logical device the Plug and Play bus's scan found, `pnpCSN.LDN`, with its kind by its IDs
(an SB-compatible or a known maker's audio device sound, a game port input, IDE storage, serial and parallel
ports port), its IRQ, DMA and ports as set, and its state: on, with the driver, when its ports are among those of
a device one of GLOS's drivers has (the Crystal driver sets its card up with its own key, not through the bus) or
it is the MPU-401 GLOS's MIDI uses; dos when it is set up and GLOS doesn't drive it; none when nothing has set it
up. The list never scans the bus itself (`pnpbus_scanned`): the cards appear on a PC without BLASTER, or after
`glos pnp`. Loop A's three Crystal cases check it (`pnp-list`: the WSS device GLOS's); the CS4232's second DMA
channel is 3 where the others' is 0.

## Tier 2: OPTi 82C929 and 82C930 (2026-11-25; the owner's choice: H finished first, OPTi then line sharing)

`kernel/drv/opti.c` plays OPTi's ISA sound chips through their Windows Sound System side: a CS4231-class codec
that `crystal.c`'s codec code plays (`crystal_codec_at`: the Crystal driver split, its codec half used for a
chip another driver has set up).

- **82C929 (MAD16 Pro):** MC1-MC6 at F8Dh-F92h, each access after the password E3h at F8Fh. Found by MC1 reading
  otherwise without the password and a bit written with it reading back changed. GLOS puts MC1 in WSS mode at
  530h and sets the WSS configuration register (its interrupt and channel the Sound Blaster side had, from MC3
  or BLASTER; full duplex on the other of channels 0 and 1).
- **82C930:** the password E4h, its registers behind an index and data pair at MCBase (E0Eh after a reset), in
  the order its own drivers use (index, password, data). MC3 one interrupt and channel for both sides, MC4 bit
  2 the Sound Blaster side's output, MC5 bit 5 the codec's MODE 2, and its master volume the codec's I22/I23,
  muted after a reset: set to 0 dB.
- When the mixer stops, and at glos exit, the chip is as found: its Sound Blaster Pro for DOS programs.
- **First in the mixer's order:** its password protocol is its own, and its codec at 534h answered Crystal's
  automatic look, unset (an AcerMagic S20 was taken for a Crystal part and played silence).
- **Tests:** Loop A's audio suite on 86Box's AcerMagic S20 (82C929 + CS4231) and 82C930, BLASTER as each chip's
  Sound Blaster side starts (220h, IRQ 7 or 5, DMA 1): the whole case, every tone in 86Box's recording, the SB
  side's DMA stopped after GLOS (SBCHK). The Crystal and ESS cases unchanged.
- **Not done:** the 82C931 (Plug and Play, its password at F8Dh: 86Box's needs its PnP ROM, which RetroGPU's
  sparse ROM set hasn't); no bench has an OPTi chip.
- **Still open:** Crystal's automatic look takes any CS4231 answering at 534h for a Crystal part (another
  maker's Windows Sound System card at 530h would play silence the same way). A Crystal-only check (its control
  port, or I25's version) would close it.

