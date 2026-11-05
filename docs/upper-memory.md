# GLOS upper memory: a plan

**Status (2026-11-05): U0–U5 done (branch `upper`): U4 is the whole Loop A regression with the page on and off,
U5 bench-g400 ([U5's results](#u5s-results-bench-g400-2026-11-05)). B1 done: Loop A (`make loopa-umb`, with
MGA-Glide's `umbpci` boot) and bench-g400 (below, under B1). Plan v1 (2026-10-31), with the
owner's answers ([Decisions](#decisions)), written for the disk cache's Q6 (docs/disk-cache.md, Decisions).**

Two parts:
- **Part A** is D41's opt-in setting, owed since M9. GLOS moves its resident stub into an upper-memory page it maps
  itself, so only the PSP and the environment stay below 640K (about 0.5 KB).
- **Part B** is a study of D25's after-v1 item: UMBs for DOS programs (LOADHIGH, DEVICEHIGH and XMS 10h after DOS
  has booted, and VDS for DMA into them). It ends in a recommendation, not in code.

PRD sections: §4.2 (memory), §6.2, §6.8 (the DOS server), §10.7 (configuration), §13 (P7); decisions D25, D39–D41,
D72; supervisor.md §2.2 (the stub), §4, §5, §12.1a (1687h), §16 (XMS).

## Why

GLOS keeps 3 KB of conventional memory today, or 7 KB with the disk cache on (P7):

| Piece | Size | Where it must be |
|---|---|---|
| GLOS.EXE's PSP | 256 bytes | Conventional memory (DOS owns it) |
| GLOS.EXE's environment | about 0.25 KB | Conventional memory (DOS owns it) |
| The stub (`loader/stub.asm`, `_stub_end` = 0A3Eh) | 2,622 bytes | Any real-mode segment while GLOS runs; real memory when the kernel leaves (below) |
| The disk cache's block buffer | 4,112 bytes | Real memory at its physical address: the BIOS may DMA into it (disk-cache.md, The block buffer) |
| The master environment (shell mode only) | 1–2 KB | Real memory: the shell keeps running after `glos exit` |
| The DOS server's transfer buffer | up to 32 KB, only during a write or an SFTP session | Real memory: DOS may DMA into it (`kernel/dos/dos.c:103-168`) |

Part A takes the stub out: about 2.6 KB back, leaving 0.5 KB (cache off) or 4.6 KB (cache on). D41 made it opt-in
because a page that looks free may belong to an adapter that isn't awake yet.

## What this plan rests on (checked 2026-10-31)

- **The first MB is one page table shared by every address space.** `kpt_low` maps physical 0–10FFFFh one to one
  and `kpd[0]` points at it (`kernel/mm/vmm.c:432-436`). `mm_space_new` copies the kernel's PDEs, PDE 0 included
  (157-169), so every DPMI context and app sees the same first MB.
- **GLOS already maps a RAM frame of its own below 1 MB.** `mm_vga_hide` puts one spare frame from above 1 MB over
  A0000h–BFFFFh, in every space at once (`vmm.c:338-352`). A UMA page is the same move: `take1()`, one PTE in
  `kpt_low`, `invlpg`.
- **The kernel holds the stub's segment in one place,** `vm.loader_cs` (`kernel/include/vm.h:40`), set when the
  stub goes resident (`kernel/vm/v86.c:546`). Everything else follows it:
  - the ARPL dispatch (v86.c:1075-1085) and IVT 2Fh, which follows a move (794-805);
  - what programs are handed: 4310h's ES (828), 1687h's ES:DI (`kernel/dpmi/host.c:409`), 0305h/0306h
    (`int31.c:673-680`), real-mode callbacks (`int31.c:325,343`), a DPMI client's PSP:0Ah (host.c:392);
  - `rm_call`'s return address (`rmcall.c:95`) and the real-mode stacks at `loader_cs:kill_sp`;
  - selector 38h's base (`kernel/arch/cpu.c:130`).
- **Three places assume the stub sits right after the PSP:**
  - `stub_resident` copies it to PSP+10h (`stub.asm:285-297`);
  - the AH=4Ah size counts PSP, stub and block buffer together (318);
  - `vm_resident` takes the PSP as the stub's segment minus 10h and the block buffer as the stub's segment plus
    its size (v86.c:534-535), and the loader checks that layout (`GLOS-VM error=stub-layout`, `loader/main.c:716`).
- **The stub runs in real mode again when the kernel leaves.** `_pm_ret` turns paging and protection off and
  carries on in the stub (`stub.asm:176-188`). It then restores A20 and the XMS blocks, reports and ends with
  4Ch (`stub_exit`, 465-530). In shell mode it runs COMSPEC for good (`shell_on`, 532), so the EXEC loop, its data
  and its stack run in real mode from then on. With paging off, a page GLOS mapped is gone: the CPU would fetch
  from the bus.
- **Nothing DMAs into the stub.** The kernel lends the stub's buffers only to calls that write them by CPU:
  - VBE 4F01h (`kernel/dos/shot.c:464-475`, `kernel/gfx/display.c:92-101`);
  - INT 21h 47h, the current directory (`kernel/app/app.c:118-130`);
  - an 8-byte INT 21h 3Fh, the `GLOSAPP1` check (`kernel/vm/session.c:402-420`), which DOS serves from its own
    buffers.
- **XMS 10h–12h answer "not implemented"** (AX=0, BL=80h: `kernel/vm/xms.c:329-336`). GLOS refuses to start under
  a memory manager by what it does, not by name: V86 mode (EMM386, QEMM, JemmEx) or a VCPI answer
  (`loader/main.c:514-527`).
- **No VDS.** INT 4Bh isn't trapped (v86.c:1111) and nobody sets the BDA's flag at 40:7Bh bit 5. GTA and Screamer
  Rally call INT 4Bh through 0300h (PRD Appendix A) and get whatever the IVT holds, as on plain DOS.
- **No record of which UMA pages are free** on any test machine. GLOS reads no ROM signatures and no PCI
  expansion-ROM BARs (`kernel/drv/pci.c:64` reads the BARs from 10h only).
- **The docs and the code disagreed on 1687h.** D41 and supervisor.md §2.2 said the DPMI host needs no
  real-mode memory per client (§12.1a had what was built). The code answers SI = 80h
  (`kernel/include/dpmi.h:23`): every DPMI client allocates 2 KB of its own conventional memory for GLOS's
  real-mode stack. MEM's largest-program figure can't see it, since it belongs to the program, but a program
  short of conventional memory loses it. The docs are corrected now (Q3).

## Part A: the stub in an upper-memory page

### The setting

`[memory] stub_page = off | auto | SEGMENT` (for example `D800`), off by default (D41: opt-in). `/STUBPAGE=` on
GLOS.EXE's command line says the same for one start. The Control Panel's Memory page gets the setting, with its
golden.

### Which page

A 4 KB page from C8000h to EFFFFh qualifies only if all of these hold:
- every byte reads FFh, twice (the bus with nothing on it);
- no option ROM (55AAh at a 2 KB boundary, with its length byte) covers it;
- no PCI memory BAR or enabled expansion-ROM BAR covers it.

`auto` takes the first qualifying page from C8000h up, skipping E0000h–EFFFFh, which many PCI BIOSes use and
don't always mark. A named segment is checked by the same rules. The owner can then pick E000h and take the risk.
A page that fails gives `GLOS-VM stub-page refused reason=rom|bar|data segment=…`, and the stub stays in
conventional memory. GLOS starts either way.

What the rules can't see: an ISA card whose memory appears only when its driver wakes it. That is why the
setting is opt-in, and why it accepts a named page.

### What U0 found (2026-11-01)

UMAPROBE on plain DOS and under GLOS gave the same pages on every Loop A machine (`make loopa-umaprobe`, 7 of 7).
86Box answers FFh for memory nothing maps, as silicon does. No machine has a PCI memory or expansion-ROM BAR
below 1 MB.

| Machine | Option ROMs | Not open from C800h | Free pages | `auto` |
|---|---|---|---|---|
| bf6, G200 / G400 / VBE | C000h, 32 KB | E000h–E7FFh BIOS data | 32: C800h–DFFFh, E800h–EFFFh | C800h |
| bf6, G450 | C000h, 34 KB | C800h (the ROM's end); C900h–CBFFh zero (the BIOS's shadow RAM, 16 KB steps); E000h–E7FFh data | 28 | CC00h |
| 486dx2, VBE / G200 PCI | C000h, 32 KB | E000h–EBFFh data or zero | 28: C800h–DFFFh, EC00h–EFFFh | C800h |
| 486dx2, G450 PCI | C000h, 34 KB | C800h (the ROM's end); E000h–EBFFh | 27 | C900h |
| bench-g400 (under GLOS) | C000h, 32 KB | E000h–EFFFh BIOS data (E820 reserves E0000h–FFFFFh) | 24: C800h–DFFFh | C800h |

The G450's shadow RAM is why a page reading 00h is never taken: it is RAM, but the BIOS write-protects it.

### Going resident

As today, the stub copies itself to PSP+10h and tells the kernel (RESIDENT). With a page, the kernel then:
1. checks the page (above), takes a frame (`take1()`), points `kpt_low`'s entry at it with PTE_U, and copies the
   stub there;
2. moves `vm.loader_cs` and everything that follows it, as RESIDENT does now;
3. answers RESIDENT with the new segment, so the stub shrinks its block to the PSP plus the block buffer (cache
   on), and goes on from the page.

RESIDENT carries the PSP's segment apart from the stub's, and the block buffer is placed after the PSP, not after
the stub. The stub-layout check follows. `GLOS-VM resident` gains `page=D800` (or `page=none`).

### The way out

The stub runs in real mode once the kernel leaves, so before `_pm_ret` the kernel copies it back down:
1. in the system VM, AH=4Ah grows GLOS's block back to hold the stub; if a later TSR stands in the way, AH=48h
   takes a block anywhere (owned by GLOS's PSP, so 4Ch frees it);
2. the stub is copied there, and its own segment words (`rm_cs_imm`, `new_seg`) are set as RESIDENT sets them
   now;
3. `vm.loader_cs` and IVT 2Fh move to the copy, the page's PTE goes back to the identity entry, and the frame is
   freed;
4. then the kernel leaves as today.

If DOS has 2.6 KB nowhere, `glos exit` is refused (`GLOS-EXIT refused reason=stub-memory`), and GLOS keeps
running, so the agent can say why. Every way back to real mode goes through this copy: `glos exit` and the shell
fallback after it. (A panic halts where it is: `kernel/arch/cpu.c:219-220`.)

### The DPMI host's real-mode stack (Q3)

With a page there is room for the host's real-mode stacks too: 2 KB per DPMI context, a second page if needed.
1687h then answers SI = 0 while a slot is free, and 80h (the client's own memory, as now) when none is. That
gives D41's "no real-mode memory per client" for real.

## Part B: UMBs for DOS programs (a study)

### What they would give

UMBs that GLOS creates exist only while GLOS runs. So they help only programs started under GLOS:
- TSRs and real-mode programs started from a GLOS session with LOADHIGH;
- programs that ask XMS for a UMB themselves.

Drivers in CONFIG.SYS and AUTOEXEC.BAT load before GLOS and gain nothing. DPMI programs, most of the census, need
extended memory, and P7 already gives them conventional memory within 4 KB of plain DOS. No program in the census
fails for lack of conventional memory under GLOS.

### What they would take

- **Pages:** as Part A, many of them (about 96–160 KB on a typical PCI machine), by the same rules.
- **DOS's arena:** DOS links UMBs while CONFIG.SYS runs (DOS=UMB, from the XMS UMB provider loaded there). GLOS
  starts later, so it would have to build the UMBs' memory control blocks itself and link them into DOS's chain
  through the List of Lists. FreeDOS, MS-DOS 6.22 and MS-DOS 7.1 keep that state differently, and whether
  5802h/5803h then work without DOS=UMB is to be found out by a probe on each.
- **XMS 10h–12h** for programs that ask directly.
- **VDS** (INT 4Bh 8102h–810Ch, and 40:7Bh bit 5), so that DMA into a UMB is translated. ISA DMA and bus-master
  DMA use physical addresses, and a page GLOS maps has nothing at its physical address. Programs that don't use
  VDS assume linear = physical below 1 MB (D25: SDL's Sound Blaster driver), so their DMA buffers must never land
  in a UMB. Only UMBs given when asked for (link off by default, low-first strategy) keep them out.
- **`glos exit`:** whatever lives in a UMB dies with the page. GLOS would refuse to exit while DOS's chain has an
  owned block in the UMA.
- **Kill:** DOS frees a killed program's UMB blocks at 4Ch only if it walks the linked chain; to check per DOS.

### Alternatives

- **UMBs from the chipset at boot** (shadow RAM in the UMA made writable before CONFIG.SYS, as UMBPCI does on
  Intel chipsets): real RAM at those physical addresses. DOS=UMB works as usual, drivers load high before GLOS,
  and with LOADHIGH GLOS.EXE itself (stub and block buffer included) would sit there. DMA into shadow RAM works
  only on some chipsets, so that needs checking too. GLOS needs no new code, only a check that it runs on such a
  DOS. UMBPCI is freeware without source, so it can be a test baseline only (D26).
- **GLOS as a CONFIG.SYS driver** providing UMBs from boot, as JemmEx does: rejected. GLOS's kernel would start
  before DOS has booted, against D39/D40's start modes.

### Recommendation

D25 stands: no GLOS-made UMBs in v1. Instead, the chipset check (step B1 below), and VDS only if a program in the
census needs it.

## The steps

Development in `~/wt-glos-m5`, long runs in `~/wt-glos-test`, as for the cache.

**Where they stand (2026-11-05):** U0–U4 done in Loop A; U5's first bench run and B1 in Loop A are in
[the status](#glos-upper-memory-a-plan) above and under B1. As of 2026-11-02:
- **U0 done:** Loop A 7 of 7 and bench-g400 ([What U0 found](#what-u0-found-2026-11-01)).
- **U1, U2 done in Loop A:** `jobs.py mem-page` 6 of 6 (bf6, 486dx2, 486dx4; raw and HIMEMX). The page is
  UMAPROBE's; GLOS keeps 448 bytes (4,560 with the cache's buffer); the copy-down through 4Ah, and through 48h
  when STUBTSR stays after GLOS; `glos exit` refused while STUBTSR ALL holds every free block, GLOS still
  answering, then leaving once the memory is back; DOS's memory as before. The shell-mode case U2 lists is moot:
  shell mode never leaves (`glos exit` is refused there), so its stub never comes down.
- **U3 done in Loop A:** the stacks' pages are the next two that qualify (C900h and CA00h, or CD00h and CE00h
  with the G450's longer ROM), a 4 KB stack each; NESTRM three levels deep runs the first two in them and the
  third on its own memory, and a new client then has a slot again.
- **U4's first run (2026-11-03) found a bug in the DPMI host, on main too:** it gave each nested real-mode call a
  512-byte slice of a 2 KB stack, and calls nest eight deep. A fifth one (an IRQ's INT 1Ch, four deep under
  MGA-Glide's DOS/4GW conformance tests) wrapped SP: on main into conventional memory 62 KB past the client's
  block, unnoticed; in a page of upper memory into nothing, and the PC hung. The owner chose 4 KB stacks (eight
  slices): 1687h asks for 4 KB without the page, and with it each slot is a page (GLOS main, branch `rmstack`,
  RMDEEP). HDPMI's EXC0E2 hung too (AH=09h through DS = the host's stack, in a page with no `$`): skipped.
- **U4 running:** the whole regression on a test build that defaults to `auto`, then on `upper` itself (off).

- **U0. Probe, changing nothing.** UMAPROBE (`tests/dos/umaprobe.c`, DJGPP, read-only), on plain DOS:
  - each 4 KB page from C0000h to EFFFFh: open bus, option ROM (its signature and length) or data;
  - PCI memory and expansion-ROM BARs below 1 MB (INT 1Ah B1xxh);
  - whether 86Box answers FFh for unmapped memory, as silicon does.

  Runs on Loop A's bf6 (G200, G400, G450, VBE) and 486dx2 (VBE, G200 PCI, G450 PCI), then on bench-g400
  (read-only, from C:\GAMES\DGL). *Exit:* each machine's free pages recorded here.
- **U1. The page.** The setting, the checks, the mapping, RESIDENT with the PSP apart, the shrink, the log line.
  - T (host): the page rules from a fake UMA image (tests/host/umapage_test.c).
  - T (Loop A): `jobs.py mem` gains `mem-page`: with `stub_page = auto`, MEM /C shows GLOS at 1 KB or less (cache
    off) and 5 KB or less (cache on), with the largest program within the same of plain DOS. The same under
    `/RUN`, in agent mode and in shell mode.
- **U2. The way out.** The copy-down, the refusal.
  - T: `glos exit` with the page: VECCHK clean, `GLOS-EXIT`, MEM as before GLOS started.
  - T: with a small TSR loaded after GLOS (the AH=48h path); and with no 2.6 KB anywhere (refused, GLOS still
    answering).
  - T: shell mode, `glos exit`, then a program run by the fallback shell.
- **U3. The host's real-mode stacks in the page** (Q3: yes). T: `loopa-dpmi`, `djtst`, `ecm`, `hdpmireg`
  and `m4c-games` with SI = 0, and with the slots used up.
- **U4. The gate.** The whole GLOS regression with `stub_page = auto` on bf6 and 486dx2, raw and HIMEMX boots:
  every DPMI suite goes through the moved ARPLs, callbacks and raw switch. Then with it off: unchanged.
- **U5. The bench.** U0's probe result first; then the setting on: MEM /C, DOSBench's game set, `glos exit`.

  ### U5's results (bench-g400, 2026-11-05)

  The release build (2026-10-27) off C:\GLOS, `upper` (4d8a4f7) on with `stub_page = auto`, the release build back
  after, every file checked by sha1. AUTOEXEC.BAT ran MEM /C before GLOS and, after `glos exit`, MEM /C again and
  GLOS once more, both from a CALLed batch.
  - **The page:** C800h, the real-mode stacks at C900h and CA00h, as U0 found. GLOS's own conventional memory:
    36,160 bytes for the release build, 7,680 for `upper` with the page off (the stub and the cache's block
    buffer), 4,864 with it on; the largest program 573K, 600K and 603K.
  - **DOSBench's games**, eleven runs of Quake, Quake 2 and Half-Life, all `ok` (Quake 25–40 fps, Half-Life
    31–40), every DPMI client on the page's stacks (`rm=page`).
  - **`glos exit`:** the first try hung the PC. DOSLFN, which the Quake 2 and Half-Life runs load, stayed resident
    on INT 2Fh above GLOS, and its chain led into the page once paging was off (the owner's photo of the screen:
    `int2f-hooked-over`, then nothing). Fixed on main (GLOS's INT 2Fh entry in its PSP, a pass-through when
    hooked over, `make loopa-int2f`). Again: GLOS left its pass-through, AFTER.BAT's MEM ran and GLOS answered
    again 5 s later; MEM after the exit shows GLOS at 272 bytes and DOSLFN at 11,520, as expected.
- **B1. The chipset check** (Q4: yes). bf6 in 86Box is a 440BX, which UMBPCI supports (whether 86Box's
  shadow RAM behaves as silicon's is part of the check):
  - GLOS started with drivers loaded high (DOS=UMB, DEVICEHIGH, LOADHIGH);
  - and `LOADHIGH GLOS.EXE`: the stub and block buffer in a UMB, and DSKTORT with the block buffer there (DMA into
    shadow RAM).

  On bench-g400 only with the owner's OK, since it changes CONFIG.SYS. Then the PRD says what works.

  **Loop A (2026-11-05, `make loopa-umb`; MGA-Glide's `--boot-cfg umbpci`: HIMEMX, then UMBPCI 3.90, which hands
  DOS its blocks through the XMS driver loaded before it, and `DOS=HIGH,UMB`):** UMBPCI finds the 440BX and gives
  C800h–DFFFh and E800h–EFFFh, five blocks, 128,944 bytes. A TSR loaded high (STUBTSR at C80Ch) stays as it was
  through four GLOS runs. `LH GLOS.EXE` puts all of GLOS in a UMB (PSP C828h; MEM /C: 7,312 bytes, none of them
  conventional); DSKTORT through the cache's block buffer there, on the scratch disk, gives the digest of plain DOS
  and of GLOS loaded low. `/STUBPAGE=AUTO` takes no page (the UMBs read as RAM) and says so (`stub-page refused
  reason=none`). VECCHK clean each time. In 86Box the BIOS's disk reads reach the buffer by the CPU; whether a
  bus master's DMA reaches shadow RAM on silicon is for the bench.

  **bench-g400 (2026-11-05, the owner's OK; CONFIG.SYS's [GLOS] block with HIMEM.SYS, then UMBPCI.SYS
  /I=C800-DFFF, the range U0 found free, and `DOS=HIGH,UMB`; everything put back and checked by sha1 after):**
  - UMBPCI finds the 440BX (8086:7190) and gives 98,320 bytes. MS-DOS 7.1 puts parts of itself, COMMAND.COM's
    resident part and IFSHLP there (17,712 bytes), and the largest block left is 72,400: too small for GLOS.EXE,
    which needs about 77 KB until it goes resident, so `LH GLOS.EXE` loads it low on this PC. It runs low among
    the UMBs, its cache verifying: Quake's PAK0.PAK (18.7 MB) fetched twice by SFTP gives its sha256 both times,
    688 hits, no mismatch, no warning.
  - **UMBRD** (`tests/dos/umbrd.c`, read-only): 256 reads of 8 KB by INT 13h into a UMB, each the same as into
    conventional memory. The BIOS's disk reads do reach shadow RAM here, so the cache's block buffer would be
    safe in a UMB on this PC, were there room.
  - **ISA DMA does not:** UMBPCI's DMACHK finds every block from C800h to EFFFh writable and none that ISA DMA
    reaches, though 86Box's 440BX passes them all (an 86Box deviation). A program whose 8237 DMA buffer lands in
    a UMB (a floppy cache, a Sound Blaster driver loaded high) fails on this PC. GLOS's own DMA buffers are in
    extended memory below 16 MB, never in a UMB.

Then the docs: PRD (D41 as built; P7 with the page; D25's note from Part B), supervisor.md §2.2 (the stub,
1687h), §4 (the first MB's one remapped page), §12.1a, the release notes.

## Decisions

The owner's answers (2026-10-31), each the recommendation:

- **Q1. The setting:** off by default, with `auto` and a named segment, as D41 says? **Yes.** A page
  that looks free can still belong to a sleeping ISA card, and a named page lets the owner decide.
- **Q2. The way out:** copy the stub back down at `glos exit` (2.6 KB of DOS memory needed at that moment, or the
  exit is refused)? Or keep a real-mode exit part below 640K for good? **The copy.** Most of the stub
  runs again in real mode at exit (the cleanup, the shell fallback, the EXEC loop), so a resident exit part
  would save little.
- **Q3. 1687h's 2 KB per DPMI client:** move the host's real-mode stacks into GLOS's pages with the setting on
  (U3)? Or only correct the docs? **Correct the docs now (done: D41, supervisor.md §2.2 and §9.5) and move the
  stacks with the setting.** It is one more page and a fallback, and it makes D41 true.
- **Q4. Part B:** leave GLOS-made UMBs after v1 (D25), and do the chipset check (B1) instead? **Yes.**
  GLOS-made UMBs help only programs started under GLOS, need VDS and a DOS-specific arena hack, and no census
  program is short of conventional memory. Chipset UMBs help everything, GLOS included, for the cost of a test.

## Size

- Kernel: about 200 lines (the page rules, mapping, RESIDENT, the copy-down, configuration); U3 about 60 more.
- Stub and loader: about 50 lines.
- UMAPROBE about 150 lines; tests about 200.
- B1: a Loop A boot variant with UMBPCI (MGA-Glide first, D27) and a jobs.py case.

U0–U5 are a day or two at the cache's pace.
