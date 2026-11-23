# Phase H: the hardware compatibility audit

**Status: APPROVED by the owner, 2026-11-23** (question tool), with these choices:

- **Tiers:** tier 1, then tier 2. Tier 3 only when the owner wants a particular PC covered.
- **Benches D and E:** GLOS is installed on them, as on B and F, so its Devices page names their hardware.
- **Spare cards:** fitted as planned. The ALS100 Plus+ goes in bench C; an ES1373 or CT5880 card goes in a free
  PCI slot (bench B or F); the OPTi 931 is for tier 2.
- **86Box models:** none. A driver only a bench can prove says so in docs/devices.md, as the Rhine does.

It was written the same day as a draft. The route's phase H (docs/v1-route.md) is
"settled after a hardware compatibility audit, and nothing is built before that". This is that audit:

1. the bench fleet;
2. what GLOS drives today;
3. what the PCs of GLOS's period typically had;
4. the gaps;
5. a proposed phase H in tiers, each item with the way it is tested.

Nothing here is built until the owner has chosen what H holds. The Rhine driver is the exception: the owner
lifted the hold for it on 2026-11-13.

Facts about GLOS cite its files. Facts about the period are general knowledge of the hardware of 1993-2005,
not measurements, and are marked so.

## 1. The bench fleet

| Bench | CPU and chipset | Video | Network | Sound | USB | GLOS on it |
|---|---|---|---|---|---|---|
| bench-g400 (IBM 300PL) | Pentium II (Deschutes), 440BX/PIIX4 | Matrox G400 AGP (a G100 was in it for a day) | Intel PRO/100 onboard, 8086:1229 | Crystal CS4235 (CSCe825), ISA PnP | PIIX4 UHCI | yes, GLOS main |
| B (Pentium III) | ALi (10b9:1621, M1533 bridge, 5229 IDE) | ATI Rage 128 Pro AGP (a TNT2 M64 and a G400 before) | VIA Rhine VT86C100A (D-Link DFE-530TX) | C-Media CMI8738 onboard, 13f6:0111 | ALi OHCI (5237) | yes, main 823a3ce (DOS-GL's soaks) |
| C (486DX4-100) | a 486 board with PCI slots, 32 MB | S3 ViRGE/DX PCI, 4 MB | NE2000 ISA clone (AT/LANTIC) at 280h, IRQ 10 | the plan was an ALS100 Plus+ (ISA); not confirmed fitted | none | yes, main 000176e |
| D (Pentium MMX) | not yet known | not yet known | not yet known | ESS AudioDrive ES1887 | not yet known | no |
| E (Tualatin Pentium III-S) | not yet known | not yet known | not yet known | Realtek ALC201A (an AC'97 codec) onboard | not yet known | no |
| F (Pentium 4 3.06 HT) | Gigabyte GA-8I915PMD: i915P/ICH6 | Matrox G450 PCI (102b:0525, behind a HiNT bridge) | Realtek RTL8139 (10ec:8139 rev 10) | Realtek ALC880, Intel HD Audio | ICH6: 4 UHCI + EHCI | yes, its own partition |
| G (Pentium 4; added 2026-11-23) | PCChips M925G: VIA VT8235 | Matrox G550 (102b:2527) | VIA Rhine onboard | VIA AC'97, 1106:3059 | 3 UHCI (1106:3038) + EHCI (1106:3104) | yes: native, beside Windows 98, the native-dos session's branch |

The owner's spare cards (2026-11-11):

- **PCI sound:** CMI8738/PCI-6ch-MX; ForteMedia FM801-AU; Creative CT5803 or CT5807 (ES1373); Creative CT4740
  (CT5880).
- **ISA sound:** Asound AS004 (ALS100 Plus+); Crystal CX4235-XQ3; OPTi 82C931.
- **Video:** the Riva TNT2 M64 and the G100, out of the benches.

The owner owns no Sound Blaster.

## 2. What GLOS drives today

| Kind | GLOS's own driver | Tested in Loop A (86Box) | Proven on a bench |
|---|---|---|---|
| Network | RTL8139 (10EC:8139) | rtl8139c+ | **not yet**: bench F has one |
| | Intel 8255x (8086:1229 and 7 more) | i82557, i82558 | bench-g400 |
| | VIA Rhine (1106:3043, 3065, 3106) | no model in 86Box | bench B (Rhine I only) |
| | NE2000: PCI RTL8029, ISA by probe | ne2k, ne2kpci | bench C |
| | Serial cable (SLIP, 8250 to 16550A) | cable case on COM2 | bench B |
| | Parallel cable (PLIP, LapLink) | plip case | **not yet** |
| Sound | Sound Blaster 16 (from BLASTER, DSP 4 or later) | sb16 | **not yet**: no SB16 owned |
| | Crystal CS4232 to CS4238B, ISA PnP | cs4232, cs4235, cs4236b | bench-g400 |
| | OPL2 or OPL3 FM, MPU-401 UART | yes | bench-g400 (FM) |
| | Virtual SB16 for DOS programs, over the mixer's card | yes | bench-g400 (SBTEST 6/6) |
| Video | Any card by VBE: linear modes, software 2D | vbe (S3 Trio64V2) | bench C (ViRGE/DX), B |
| | Matrox G100, G200, G400, G450: 2D engine, cursor, VRAM, the GPU lock for DOS-GL | g100 to g450 (AGP and PCI) | bench-g400 (G400, G100); bench F's G450 runs GLOS (2026-11-16), the route's G450 re-verification is owed |
| | Write-combining: Matrox, TNT/TNT2 by default | (not measurable) | bench-g400, B |
| USB | UHCI host, polled | listed only (86Box has no USB devices) | bench-g400 |
| | Hubs, HID keyboards, mice and pads, Xbox One pads | host tests | bench-g400 |
| | Xbox 360 pads | host tests | **not yet** (the pad is owed) |
| | OHCI: its interrupts quieted, nothing driven | host test | bench B |
| Disks | INT 13h with GLOS's cache (IDE ports watched) | loopa-cache | bench-g400, F |
| Input | 8042 keyboard, PS/2 mouse with wheel, virtual game port | yes | bench-g400 |

Some hardware falls back to DOS or the BIOS:

- **Video:** every mode is set by the card's VBE. The Matrox engine draws for GLOS's 2D; other cards are drawn
  by the CPU.
- **Disks and floppies:** INT 13h only.
- **Network:** a loaded packet driver makes GLOS leave the card alone.
- **USB:** with `[usb] off` the BIOS's keyboard emulation stays.
- **Any device:** `[devices] off` leaves it to DOS.

DOS-GL and Glide (in ~/RetroGPU) drive their own 3D on more cards: Matrox, ViRGE, TNT2, and the Rage 128 on a
branch. That is RetroGPU's work, not phase H's.

## 3. What the PCs of the period had (general knowledge, not measured)

**486 (1992-1995: ISA, VLB, early PCI)**

| Kind | Typical |
|---|---|
| Video | Cirrus Logic GD542x/543x, Tseng ET4000, S3 801/805/928, Trident |
| Network | NE2000 clones, 3Com 3C509, WD/SMC 80x3 |
| Sound | Sound Blaster Pro and 16 and their clones (Aztech, ESS, OPTi), Gravis UltraSound |
| Disks | IDE through the BIOS |

**Pentium (1995-1998: PCI; Intel 430FX/HX/VX/TX, VIA VP, SiS)**

| Kind | Typical |
|---|---|
| Video | S3 Trio64 and ViRGE, Cirrus 5446, ATI Mach64 and Rage II, Matrox Millennium and Mystique, plus a Voodoo add-on |
| Network | RTL8029 (NE2000 PCI), 3Com 3C905, DEC 21140 Tulip and its clones, Intel 82557 |
| Sound | ISA PnP chips (SB16/AWE, ESS 1868/1869, Yamaha OPL3-SAx, Crystal, OPTi 93x); later Ensoniq AudioPCI (ES1370/1371) |
| USB | UHCI on Intel's PIIX3 and PIIX4 and VIA; OHCI on SiS and ALi |

**Pentium II and III (1998-2001: AGP; 440BX, i810/815, VIA Apollo Pro and KT133, SiS, ALi)**

| Kind | Typical |
|---|---|
| Video | Matrox G200 and G400, nVidia TNT/TNT2/GeForce, ATI Rage Pro, Rage 128 and Radeon, 3dfx Voodoo3, S3 Savage, Intel i740 and i810/815 |
| Network | 3C905B/C, Intel 82559, RTL8139, VIA Rhine (onboard with the 686B and VT8233), Tulip clones, SiS 900 |
| Sound | Creative's AudioPCI line (ES1371, ES1373, CT5880), SB Live! (EMU10K1), Aureal Vortex, CMI8738, ESS Maestro and Solo-1, Yamaha YMF724/744, ForteMedia FM801 |
| Onboard sound | AC'97: Intel ICH and ICH2, VIA 686, SiS 7012 controllers, with codecs from Realtek, Analog Devices, SigmaTel |

**Pentium 4 and Athlon XP (2001-2005: i845 to i915, VIA KT400/KT600, nForce2)**

| Kind | Typical |
|---|---|
| Video | GeForce and Radeon (VBE only for GLOS) |
| Network | Intel 8254x (e1000), RTL8139/8100, RTL8110/8169, VIA Rhine II and III, Broadcom, nForce |
| Sound | AC'97 everywhere, then Intel HD Audio from ICH6 (2004) |
| USB | EHCI beside UHCI or OHCI companions |
| Disks | SATA in IDE mode |

What every period has in common:

- Sound in DOS games means **Sound Blaster compatibility**. Cards that lack it in hardware (PCI and onboard
  sound) are exactly the ones GLOS's virtual SB16 makes useful under GLOS, once GLOS has a driver for the card.
- A network card GLOS drives replaces a packet driver and gives the desktop SSH, the agent and the apps' network.

## 4. The gaps

**Sound: the largest gap, and the one the fleet already shows.**

GLOS has an SB16 (which no bench has) and the Crystal family. None of the cards in bench B, D, E or F, nor any
of the owner's spare PCI cards, has a driver. On those PCs the desktop and GLOS's apps are silent, and DOS
games get no virtual SB.

| Candidate | Where | Loop A (86Box) | Notes |
|---|---|---|---|
| CMI8738 | bench B onboard; a spare card | cmi8738 | Its legacy SB Pro side needs PCI-to-ISA DMA (DDMA), so a native driver is the useful path. MPU-401, game port, FM (OPL3 on most chips; to be checked) |
| AC'97 controllers: Intel ICH to ICH5, VIA 686 and VT8233 | bench E (codec ALC201A; its controller is not yet known) | VIA 686's (ac97_via) with codecs ad1881, alc100, cs4297 and more, on a VIA-chipset machine Loop A would gain; Intel's ICH AC'97 is not modelled | One driver for the controller family, one for the AC'97 codec register set. The commonest onboard sound of 1999-2004 |
| Intel HD Audio | bench F (ALC880) | none | Codec graphs are more work than AC'97; only the late end of the period |
| Ensoniq ES1371/ES1373, Creative CT5880 | spare cards | es1371, es1373, ct5880 | One driver for the family |
| ESS ES1887 (AudioDrive, ISA) | bench D | ess_1868/1869 (no 1887) | SB Pro compatible in hardware: the existing SB path may need only a non-SB16 mode |
| ALS100 Plus+ | spare card (planned for C) | none | SB16 class: sb16.c may drive it as it is. First silicon for sb16.c |
| OPTi 82C931 | spare card | opti_82c931 | ISA PnP; SB Pro and WSS |
| FM801 | spare card | none | Bench only |

**Network: little missing for the fleet, more for the period.**

- **The fleet:** bench F's RTL8139 is driven but never tried on silicon (a test, not a driver). Bench D's and
  E's cards are unknown.
- **The period:**
  - the 3C905 family (86Box: EISA 3C59x only, no 3C905);
  - DEC 21140/21143 Tulip and its clones (86Box: dec_21140_tulip, dec_21143_tulip);
  - AMD PCnet-PCI (86Box: pcnetpci; also what VirtualBox and VMware offer);
  - Intel e1000 and the RTL8169 (86Box has neither);
  - the 3C509 (86Box: 3c509b).

**USB**

- **EHCI:** bench F's keyboard and mouse sit behind EHCI. GLOS has to take EHCI from the BIOS (the hand-off)
  and leave every port to its UHCI companions. That is the cheap step. A real EHCI driver (USB 2.0 speeds) is
  out of v1's scope (PRD).
- **OHCI:** bench B's ALi and the SiS and ALi boards of the period have only OHCI. Today GLOS only keeps it
  quiet. A real OHCI driver gives those PCs USB keyboards, mice and pads under GLOS.
- **Loop A:** 86Box models the controllers but no USB devices, so either driver is proven on benches.

**Video**

- Every card works through VBE. Only Matrox has an accelerated desktop.
- The non-Matrox cards the fleet has (ViRGE/DX, Rage 128, TNT2) could get the kernel's 2D accelerated: fills,
  copies and the cursor, as gfx/mga.c does.
- Write-combining by default for more cards is a smaller step. ATI and S3 are excluded today because their
  registers sit inside the framebuffer's range.
- The G450 runs GLOS on bench F; the route's G450 re-verification (phase G) is owed.

**Storage and input**

- INT 13h covers IDE and floppies on every period PC.
- Serial mice, USB storage and USB MIDI are after v1 by the PRD.
- Native IDE or SATA is out of scope.

## 5. A proposed phase H

**Tier 1: the fleet's own hardware.** Every item has a bench to prove it, and most have an 86Box model.

1. **Bench checks, no new code.** The RTL8139 on F, the ALS100 with sb16.c on C (if the owner fits it), PLIP
   between two benches, the Xbox 360 pad.
2. **CMI8738 native driver.** Playback, recording, MPU-401, game port, and FM if the chip has one, under GLOS's
   mixer, so the virtual SB16 plays through it. 86Box cmi8738 in Loop A; bench B.
3. **AC'97.** The Intel ICH family and VIA 686/8233 controllers with the AC'97 codec. In Loop A on a VIA-chipset
   machine (86Box's ac97_via and its codecs; Intel's not modelled); bench E (once its controller is known).
4. **EHCI hand-off,** every port to the UHCI companions. Bench F's USB keyboard and mouse under GLOS.
5. **ES1371/ES1373/CT5880.** 86Box es1371/es1373/ct5880; the spare cards in a bench with a free PCI slot.

**Tier 2: common hardware of the period that 86Box models.** Testable in Loop A without silicon.

6. DEC Tulip (21140/21143) and its clones.
7. AMD PCnet-PCI.
8. OPTi 82C93x and ESS 1868/1869 by ISA PnP. The spare OPTi 931 card is a bench check.
9. Write-combining by default for more cards, where the registers are out of the way.

**Tier 3: bench-only or larger.**

10. Intel HD Audio (bench F).
11. OHCI driver (bench B).
12. The 3C905 family.
13. Accelerated 2D for ViRGE, TNT2 and Rage 128 in the kernel.
14. Intel e1000 and RTL8169: P4-era onboard network, neither in 86Box.

**Out of H (the PRD's after-v1 list):** serial mice, USB storage, USB MIDI, native IDE/SATA, SCSI, EHCI at
USB 2.0 speeds.

**How H is tested:**

- **Loop A** gains every 86Box card a driver has. A new `loopa-hw` suite would play sound (wavcheck), move data
  over the network (the NET cases) and so on, per card.
- **The benches** prove each driver on silicon.
- A driver whose only proof is a bench says so in docs/devices.md, as the Rhine does now.

## 6. Questions for the owner

1. **Which tiers go into H.** My recommendation: tier 1 whole, then tier 2, and tier 3 only for what the owner
   wants for a particular PC.
2. **Benches D and E:** their chipsets, video and network cards. Or may GLOS be installed on them (as on B and
   F), so that its Devices page lists them?
3. **Bench C's sound card:** is the ALS100 Plus+ fitted? Which spare cards go into which bench (the ES1373 or
   CT5880 card for the family driver, the OPTi 931)?
4. **86Box models for silicon-only hardware** (Rhine, ALS100, FM801, HDA) as RetroGPU patches, or the benches
   alone. Each patch is RetroGPU's to merge, with the owner's OK.
