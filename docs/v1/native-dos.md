# Native GLOS: no DOS underneath

**Status: a design for the owner's review (2026-11-20). Nothing in the kernel has changed. N0 is built: the census
(`tools/census`, results in [native-dos-census.md](native-dos-census.md)) and the checklist of every DOS function
from Ralf Brown's Interrupt List ([native-dos-rbil.md](native-dos-rbil.md), `make rbil`).**

## What the owner asked for (2026-11-20)

- "What would we need to do to remove the dependency on FreeDOS and MS-DOS in GLOS? So that GLOS is everything
  that is needed to boot from a hard disk or floppy disk with no third party requirements?"
- Decided the same day, by the question tool:
  - **Design and census first**, as the shell was ([shell.md](shell.md)).
  - **Native only:** in the end GLOS starts itself and no longer starts from a DOS (`GLOS.EXE` on MS-DOS 7.1,
    FreeDOS or Windows 98's DOS goes, once native GLOS runs the acceptance set). **But** "we need a boot manager
    that can boot MS-DOS/Windows 9x from the same (if that's simple to do) or another partition so that they
    can co-reside on the same disk".
  - **GLOS's DOS runs in the 32-bit kernel:** the kernel answers INT 21h, with a small real-mode part in each
    machine that holds the tables programs read. Not a 16-bit DOS kernel of GLOS's own in the virtual machine.
- And (a plain message): with MS-DOS on the same disk, "ideally we wouldn't use any of the same filenames
  (COMMAND.COM, CONFIG.SYS, AUTOEXEC.BAT etc), otherwise there will be crossover" ([below](#names-nothing-dos-or-windows-owns)).
- **The scope** (the owner's choice, 2026-11-20, after asking whether the census would miss what common programs
  need): every INT 21h and INT 2Fh function in Ralf Brown's Interrupt List, those of features GLOS doesn't have
  answered as MS-DOS 7.1 answers them when the feature isn't loaded; a checklist made from RBIL; the census only
  sets the order; behaviour checked by a conformance program on MS-DOS 7.1, FreeDOS and GLOS.
- **Decided the same day after the design's first draft** (question tool): N1 is built **beside phase F**
  (host-tested only, in its own worktree, no change to GLOS's behaviour; native boot and the INT 21h work wait for
  phase F); programs' opens of the root's `AUTOEXEC.BAT` and `CONFIG.SYS` are **not redirected**; GLOS's names are
  **`\GLOSLDR.SYS`, `\GLOS\STARTUP.BAT` and `\GLOS\DOSBOOT.BIN`**.
- Also asked: how GLOS's DOS can improve on FreeDOS's and MS-DOS's kernels, and grow towards newer hardware
  (SMP, power management, shared resources): [Beyond DOS](#beyond-dos-what-a-dos-of-gloss-own-makes-possible).

"Third party" here means software that has to be on the disk beside GLOS. Code built into GLOS (lwIP, the LZMA
SDK, DJGPP's C library in the apps) stays, and so does the PC's BIOS, which is the machine's own.

## What DOS still does for GLOS (main 2ebabc9)

The shell (S0 to S4) took COMMAND.COM, FDISK, FORMAT and SYS away: the setup floppy has FreeDOS's `KERNEL.SYS`
and five of its programs (ATTRIB, XCOPY, MEM, MORE, DELTREE) left. The kernel is the part that matters: DOS is
still GLOS's file system and the loader of its programs.

| Job | Where, today | Native |
|---|---|---|
| Starting the PC: GLOS's boot sector loads `KERNEL.SYS` at 0060:0000 | `kernel/disk/fdboot.S` | the boot sector loads GLOS's loader ([N3](#n3-booting-and-the-boot-manager)) |
| `GLOSK.BIN` read; memory, PSP, environment for `GLOS.EXE` | `loader/main.c` (INT 21h) | the loader reads it through INT 13h; raw mode (E820h, A20) is there already |
| Programs run | the stub's EXEC loop, `loader/stub.asm` | the kernel's loader ([N2](#n2-the-dos-personality)) |
| The kernel's files: GLOS.CFG, keys, secrets, `kdata`, the clock's file, SFTP, the shell's sessions, `DISK`'s checks | the DOS server, `kernel/dos/dos.c` | the kernel's file system ([N1](#n1-the-file-system)) |
| Apps' FS calls (API 2), DJGPP's C library in apps | `kernel/app/api2fs.c`, `app.c`'s batches in the system VM's DOS | the same file system, each app a process of its own |
| Drive letters, CONFIG.SYS, the country, date and time | DOS | GLOS |
| Long names, the mouse for DOS programs, CD audio | DOSLFN, CuteMouse, MSCDEX (TSRs) | GLOS's own ([N4](#n4-what-dos-programs-had-from-drivers)) |

## The shape

- **The kernel's services:** a file system (volumes, drive letters, a cache), processes and their memory,
  devices.
- **Front doors onto them:** the DOS personality (INT 21h, INT 2Fh's DOS functions, INT 20h, 25h-27h) for DOS
  programs and DPMI clients; API 2 for GLOS's apps; the shell; SFTP. Later, maybe, a Win16 personality
  (the owner's Win16 study of 2026-11-19, discussion only: Windows 3.1's KRNL386 needs INT 21h under it, and would
  get GLOS's).
- DOS's limits come from DOS being the kernel. As one front door among several, most of them go: one call at a
  time in the whole machine (InDOS, one swappable data area), 640 KB shared with DOS itself, synchronous BIOS disks,
  no protection between programs.

What leaves GLOS with DOS (simpler code): the DOS server and its safe point (supervisor.md §17.1, §17.2), the
stub's EXEC loop, apps' state swapping in the system VM (`serve()`: PSP, DTA, the vector table copied whole and
compared), INT 2Fh's pass-through at the leave, the refusals of EMM386, QEMM and Windows, XMS mode in the loader,
and `glos exit` (there is nothing to return to: it becomes restart or power off).

## N0: the census (built)

What the acceptance programs ask of DOS, measured rather than guessed, so that N2 is built in the right order and
nothing is left out unknowingly.
- **`tools/census/census.c` (CENSUS.EXE, `make census`):** a TSR loaded first, on plain FreeDOS 1.4 in Loop A.
  It hooks INT 21h, 2Fh, 20h, 25h-27h (and 33h, 67h when something is there) and counts each call by program
  (DOS's current PSP, read from the swappable data area), function (AX where AL picks a subfunction) and where
  the call came from (the program's own memory, DOS's, or elsewhere: a DPMI host reflecting a protected-mode
  call). A call seen for the first time goes to COM1 at once; a program's counts when it ends.
- **The runs:** the survey's suites in a `census` variant (`tools/survey/survey.py run --variant census`: the
  MGA-Glide and DOS-GL test programs, ClassiCube, GLQuake, PrBoom-plus, Half-Life with DOSLFN under CENSUS
  (`census-lfn`), Fifth Wheel, GTA, Screamer Rally), and jobs.py's `tpx` (Turbo Pascal 7 on RTM), `djtst`
  (DJGPP's own tests) and `api` (GLOS apps) with CENSUS in `LOOPA_EXTRA_ARGS`. Their GLOS runs have CENSUS
  under GLOS: what GLOS itself asks of DOS today, beside the programs' calls it passes down.
- **The report:** `tools/census/report.py` writes [native-dos-census.md](native-dos-census.md).
- **CENSUS changes nothing a program sees**, but for 9 KB less conventional memory: the survey's comparison with
  `base` finds the same results (HELLO's frame differs only in the cursor's blink). One bug was found and fixed on
  the way: its lines must wait for the UART's transmitter to empty, or a program that sets the UART up next
  (SERSAY) reset the FIFO, the CR LF went, and Loop A missed the HX-DONE glued to the line (djtst and api failed
  that way, then passed).
- **The runs of 2026-11-20:** 31 jobs, all PASS but Fifth Wheel, whose lap ran into the run's 3,600 s limit at
  hash 572 of about 617 (86Box under the day's load; its census lines were all written by then).
- **Limits:**
  - it sees calls, not reads of DOS's memory (programs that walk the SFT or the MCB chain are found by running them);
  - a TSR started after it (DOSLFN in Half-Life's runner) sees calls first and may answer them itself: loaded
    before CENSUS instead;
  - counts only for programs that end inside their run (Loop A stops the PC when a program says HX-DONE);
  - not covered yet: DOSBench (its runner writes DOSBench's own `out/`), Quake 2, retail games beyond GTA and
    Screamer Rally, DOS tools people run (EDIT, archivers, installers);
  - one scripted path a program: GLQuake ran with `-nosound -nocdaudio -nolan` (no MSCDEX audio, no IPX),
    PrBoom-plus with `-nosound`; no disk full, no saved game, no setup menu, no sharing violation;
  - behaviour is not counted: error codes, wildcards, `.` and `..`, the DTA's reserved bytes, handle
    inheritance, Ctrl-C and critical-error paths;
  - DOS's other ways in are not hooked: INT 28h, INT 29h, CALL 5, far calls straight into DOS.
- **So the census sets N2's order, not its scope** (the owner asked whether it would miss what common programs
  need, 2026-11-20). How the gaps are closed:
  - **scope from RBIL** (the owner's choice): [the checklist](#the-checklist-from-rbil) below;
  - **behaviour by comparison:** N2's conformance program (16-bit and DJGPP) on MS-DOS 7.1, FreeDOS and native
    GLOS, transcripts compared, error paths included;
  - **not now** (the owner, 2026-11-20): a wider corpus under CENSUS (FreeDOS 1.4's packages, the owner's own
    programs) and a census of reads (a debug mode trapping programs' reads of DOS's data pages). RBIL documents
    the tables' layouts, so N2 fills them whole; either can come later if a program shows the need;
  - **gaps that say so:** native GLOS logs every function it doesn't answer, with the program's name (as
    `DPMI-UNIMPL` does for the DPMI host), and the gate fails on such a line;
  - **hosted mode is removed only after the wider corpus passes too**, not the acceptance set alone.

### What it found (2026-11-20)

The tables are in [native-dos-census.md](native-dos-census.md); what they mean for N2:
- **A common core** that nearly every program asks for: files by handle (3Ch-40h, 42h, 4400h, 6Ch), memory
  with the allocation strategy and the UMB link (48h-4Ah, 5800h-5803h), EXEC and the child's exit code (4B00h,
  4Dh), vectors (25h, 35h), the versions (30h, 3306h), drive, DTA and PSP (19h, 1Ah, 62h), free space (36h),
  TRUENAME (60h), attributes (43h), find first (4Eh), and whether long names are there (71A0h: every DJGPP
  program asks).
- **DOS's insides are used, by the programs that matter most:** the List of Lists (52h: DOS/4GW, and GTA's
  and Screamer Rally's), the swappable data area (5D06h: Borland's RTM, Half-Life), the InDOS flag's address
  (34h: RTM), get and set PSP (51h, 50h: RTM; DJGPP's C library around EXEC). The tables of N2 must be real from
  the first step, not added later.
- **EXEC's other forms:** load without running (4B01h: TPX, for the program it debugs) and overlays (4B03h: TPX,
  Screamer Rally).
- **Long names in earnest:** Half-Life uses thirteen of the 71xxh functions (open, find, rename, delete, make
  and change directory, names), and FAT32's free space (7303h).
- **INT 2Fh:** DPMI's entry (1687h) everywhere; XMS (4300h: DOS/4GW, RTM); the idle call (1680h) in 14 jobs
  (DJGPP's C library makes it while it waits: idle detection comes almost free for DJGPP programs); Windows
  checks (1600h, 160Ah, 1684h); MSCDEX probes (1500h, 150Bh: GTA, Screamer Rally, Half-Life); RTM's own (FB42h).
- **INT 33h:** reset, position and buttons, motion, CuteMouse's wheel (0011h: Half-Life), state size.
- **Hot calls:** ClassiCube gets the date and the time hundreds of times a run (2Ah, 2Ch, every frame): from
  protected mode these must not touch V86 at all.
- **Not seen:** the FCB file calls (only 29h, parsing a name, by TPX and the harness's programs), INT 25h/26h,
  EMS (no EMM was loaded: INT 67h untested), the console's input calls but 0Bh. DOS programs people use
  (editors, archivers, installers) are owed to the census before N2 is called complete.
- **FreeDOS's own calls,** which GLOS's DOS doesn't need to make: INT 2Fh 1000h, 111Dh, 1120h, 1122h, 1123h on
  every open, close and exit (SHARE and the network redirector asked).
- **What GLOS asks of DOS today** (its runs with CENSUS under GLOS: the stub, the DOS server, the clock): 34h,
  52h, 5D06h (DOS's insides, for the DOS lock and the safe point), 3305h (the boot drive), 1600h and 4A10h (Windows
  and SMARTDRV refused or noted), 6300h, 6601h; and 2Bh/2Dh when the clock has a network time to give DOS
  (`kernel/core/clock.c`: not in these runs, which had none). All of it goes with DOS. (The one 2B00h seen,
  setting the date, was RTM's own, under GLOS only.)

### The checklist from RBIL

[native-dos-rbil.md](native-dos-rbil.md), made by `tools/census/rbil.py` (`make rbil` fetches RBIL release 61,
pinned in `tools/setup/versions.mk`): every entry of RBIL's DOS category for INT 20h-2Fh, and INT 2Fh's functions
of DOS's own parts and of what GLOS stands in for (Windows' and DPMI's 16xxh, XMS, MSCDEX, the clipboard, KEYB,
DOSKEY, ANSI.SYS, APPEND, the task switcher), each with the census's count and a proposed status. As made
(2026-11-20): 463 entries; **implement 220**, **absent 90** (answered as MS-DOS 7.1 answers without the feature:
the network server and redirector, printer setup, SHARE's internals, APPEND, DOSKEY, NLSFUNC, Windows' own),
**internal 47** (INT 2Fh 12xxh, DOS's services for TSRs and redirectors: after the core), **later 20** (MSCDEX,
ANSI.SYS, with the CD-ROM and the console), **callout 17** (DOS calls them and programs answer: INT 23h, 24h, 28h,
2Ah; Windows' broadcasts, which GLOS doesn't make), **null 9**, **other 60** (European MS-DOS 4's multitasking,
DOS 2.11's OEM calls, DOS 4's extended attributes ...). The statuses are proposals: the script's `OVERRIDES` holds
the ones made by hand, for review.

N2 works through **implement** in its areas' order (processes, handles, directories, long names, FCBs,
information, INT 2Fh), each area's census functions first; **absent** and **null** come with their area (the
right error is part of the answer); **internal** after the core.

## N1: the file system

- **FAT12, FAT16 and FAT32 with long names (VFAT)**, read and write, in the kernel, over the sectors as they are
  reached today: `kernel/disk/bdisk.c` (runs of 32 KB through the BIOS's INT 13h) and the disk cache
  (`kernel/drv/cache.c`). Native disk drivers come later ([Beyond DOS](#beyond-dos-what-a-dos-of-gloss-own-makes-possible)).
- **What Windows 98 expects of a volume it shares:** long-name entries with their checksums, FSInfo's free count
  and next-free hint, the FAT's clean-shutdown and no-error bits (FAT16 and FAT32), FAT32's backup boot sector
  untouched, the last-access date. ScanDisk must find nothing after GLOS has written a volume.
- **Writes in a safe order:** data, then the FAT (both copies), then the directory entry; the volume marked
  dirty while anything is unwritten and clean again after the flush. Flushes when idle, at restart and power off.
- **Drive letters in MS-DOS's order:** the first primary partition of each hard disk, disk by disk; then the
  logical drives of each disk; then the other primaries. Paths like `C:\GAMES` mean the same under GLOS as under
  the MS-DOS beside it. A and B the floppies (B the phantom of A on a one-drive PC, as DOS has it).
- **One file system for every caller**, safe to enter from several threads at once: the shell, SFTP, the
  kernel's own files, apps, and DOS programs in several machines ([N6](#n6-several-dos-programs-at-once)).
- **The volume GLOS boots from may be DOS's too** (the owner's same-partition case). Only one writer may ever
  touch a FAT volume: under native GLOS no DOS runs, so GLOS is that writer.
- **While hosted mode still exists** (until native runs the acceptance set), the kernel's file system has a
  second back end: the DOS server as today. Two writers on one volume would corrupt it, so the native back end
  is never used under a DOS.
- **Tests:** host tests against `mtools` and `fsck.fat` (as S0's were), images made by Windows 98 and MS-DOS 7.1
  in 86Box; power cut in Loop A in the middle of writes, then `fsck.fat` and ScanDisk.

## N2: the DOS personality

### Where it runs

- **In the kernel** (the owner's choice). Each machine's INT 21h vector points at a stub in that machine's memory
  that enters the kernel with ARPL (#UD in V86 mode), as `GLOS.EXE`'s calls into the kernel do today
  (supervisor.md §2.1). **One trap per call**, where a DOS call under GLOS costs 16 on a 486 without VME today
  (474 µs against 55 under plain DOS on the 486DX2 profile, 1,303 µs from an app: tests/dos/doscalls.c).
- **Protected-mode callers skip real mode.** DJGPP's C library reaches DOS through INT 31h 0300h; DOS/4GW
  reflects INT 21h to real mode. Both come to the host as "simulate real-mode INT 21h": when that machine's
  INT 21h still points at GLOS's stub (no TSR has hooked it), the host calls the personality directly, the
  transfer buffer read where it lies. When a TSR has hooked INT 21h, the call goes down the chain as now, so the
  TSR sees it.
- **A call that waits (a disk read, a key) blocks its own machine's thread**, nobody else's.

### What it holds in each machine's memory

DOS programs read DOS's tables directly, so they are kept real and current, in a small block at the bottom of
conventional memory (a few KB, where DOS's kernel data is):
- the **List of Lists** (INT 21h 52h; DOS/4GW calls it, census), with the **device driver chain** (NUL, CON,
  AUX, PRN, CLOCK$, COM1-4, LPT1-3), the **system file table** (one entry per open file, FILES= as needed),
  the **current directory structures** (one per drive letter) and **drive parameter blocks**;
- the **swappable data area** (5D06h, 5D0Bh) with the **InDOS** flag and the critical-error flag (34h), set
  while that machine's call is under way, for its TSRs and interrupt handlers;
- the **memory chain** (MCBs): programs walk it (MEM, TSR checks); GLOS's personality keeps it and allocates
  from it (48h, 49h, 4Ah, 58h);
- each program's **PSP** with its handle table (20 handles, 67h for more), and its **environment**.

Everything else (the files, the directories, the cache) is the kernel's, above 1 MB.

### What it answers

Everything the [checklist](#the-checklist-from-rbil) marks implement, absent or null; the census
([native-dos-census.md](native-dos-census.md)) orders each area. The areas, in the order they are built:
1. **Processes:** EXEC of .COM and .EXE (4B00h, 4B01h, 4B03h overlays), the PSP, terminate (4Ch, 00h, INT 20h),
   stay resident (31h, INT 27h), exit codes (4Dh), Ctrl-C and Ctrl-Break (INT 23h, 33h), critical errors
   (INT 24h: Fail for a session's program, as now; a dialog for an app, as supervisor.md §17.5 plans).
2. **Files by handle:** 3Ch-46h, 5Ah, 5Bh, 67h, 68h, 6Ch; IOCTL (44h) for devices and drives; sharing modes and
   locks (SHARE built in: 5Ch; INT 2Fh 1000h says it is there).
3. **Directories and names:** 39h-3Bh, 47h, 4Eh/4Fh with the DTA (1Ah, 2Fh), 56h, 43h, 57h, 60h (TRUENAME),
   29h; disk space (36h, 7303h); drives (0Eh, 19h, 1Ch, 32h, 3305h).
4. **Long names:** 71xxh, natively (no DOSLFN): 71A0h says a drive has them, so DJGPP programs use them (census).
5. **Old programs:** FCBs (0Fh-17h, 21h-24h, 27h, 28h); the console calls (01h-0Ch); INT 25h/26h absolute
   sectors (allowed on a volume only through the file system's own rules: [Risks](#risks)).
6. **Information:** version (30h; 3306h true version 7.10, with FAT32 and long names, as FreeDOS reports), the
   country and code pages (38h, 63h, 65xxh, 66xxh: GLOS's names are UTF-8, `glos/cp.h` converts at the door),
   date and time (2Ah-2Dh, from GLOS's clock with its time zone), switch character, verify.
7. **INT 2Fh's DOS functions:** 1000h, 1100h (no redirector), 1600h/160Ah (not Windows), 1680h (idle: the
   machine gives up its turn), 1687h and 4300h/4310h as now, 4A01h/4A02h (the HMA), 15xxh (MSCDEX, when a CD is
   configured), AE00h (no installable commands).

### Processes and machines

- **First, one machine** (the system VM as today, with the personality instead of DOS in it), so that the
  acceptance set runs before anything else changes.
- GLOS's own COMMAND.COM and the kernel's shell work as now: the shell asks the kernel to run a program.
- **Apps become processes of their own**: an app's DOS context (PSP, drive, directory, handles) is its own, not
  DOS's swapped in and out around each call (`serve()`).

## N3: booting and the boot manager

### GLOS's own start

1. **The disk's first sector** (`kernel/disk/mbrboot.S`, GLOS's already): the boot manager's first stage.
2. **The volume's boot sector** (`kernel/disk/fdboot.S`, GLOS's already, for FAT12/16/32): loads GLOS's loader
   (`\GLOSLDR.SYS`, hidden and system: the owner's name) instead of `KERNEL.SYS`.
3. **The loader** (16-bit, Open Watcom, much of today's `loader/` reused): a FAT reader through INT 13h (and the
   LBA extensions); `\GLOS\GLOS.CFG`'s `[boot]` section; `GLOSK.BIN` read (packed or not, as now); memory by
   E820h/E801h/88h and A20 as raw mode does now; the menu ([below](#the-boot-manager)); bootinfo with a new
   `BI_F_NATIVE`; protected mode and paging as `loader/pm.asm` does.
4. **The kernel** builds the first machine from the BIOS's state (its vector table, its data area), mounts the
   volumes, puts the personality into the machine, and runs GLOS's startup ([names](#names-nothing-dos-or-windows-owns)).

### The boot manager

- **The same partition** as MS-DOS or Windows 9x (the owner's first wish): when GLOS's boot records are put on a
  volume that already starts a DOS, the DOS's boot sector is saved first (`\GLOS\DOSBOOT.BIN`, as Windows NT
  kept `BOOTSECT.DOS`). The loader's menu offers GLOS or that DOS; choosing DOS loads the saved sector at
  0000:7C00h and jumps to it with the boot drive in DL. DOS then starts from its own `IO.SYS` as if GLOS weren't
  there. Simple, and the way NT shared a volume with MS-DOS.
- **Another partition:** the first stage lists the partitions that start something and chains to the chosen
  one's boot sector (marking it active in memory only, so that DOS finds itself). MS-DOS must be on a primary
  partition, as it always must.
- **The menu:** `[boot] default = glos | dos | partition N`, `timeout = 5`, the names shown; a key held (Shift
  or F8) shows it when the timeout is 0. Text mode, through the BIOS.
- **Windows 9x and MS-DOS's SYS rewrite the boot sector** (a reinstall, `SYS C:`, some disk tools): GLOS's start
  is then lost and DOS starts as before. `DISK n BOOT` puts GLOS's back and keeps the new DOS sector as the saved
  one. From the setup floppy too.
- **Windows 98's own boot menu** (`BootMulti=1`, "Previous version of MS-DOS") renames `IO.SYS`, `MSDOS.SYS`,
  `COMMAND.COM`, `CONFIG.SYS` and `AUTOEXEC.BAT` to and from `.DOS` names at each start: nothing of GLOS's may be
  among them.

### Names: nothing DOS or Windows owns

The owner's rule (2026-11-20): no crossover with a DOS on the same volume.
- **Never written or owned by GLOS:** `IO.SYS`, `MSDOS.SYS`, `KERNEL.SYS`, `\COMMAND.COM`, `\CONFIG.SYS`,
  `\AUTOEXEC.BAT`, `FDCONFIG.SYS`, `FDAUTO.BAT`, the `.DOS` names above, `BOOTSECT.DOS`, `NTLDR`, `BOOT.INI`,
  `\WINDOWS`, `\DOS`, `\FREEDOS`.
- **GLOS's own:** `\GLOSLDR.SYS` (the one file in the root: a boot sector can reach only the root easily) and
  everything else in `\GLOS`: `GLOS.CFG` (what CONFIG.SYS was for), **`\GLOS\STARTUP.BAT`** (what AUTOEXEC.BAT
  was: run by GLOS's shell at start), `\GLOS\COMMAND.COM` (GLOS's own, as now, and
  `COMSPEC` points at it), `\GLOS\DOSBOOT.BIN`.
- **Today's default changes:** `[shell] autoexec` is the boot drive's `\AUTOEXEC.BAT` (supervisor.md §2.2): in
  native GLOS it is `\GLOS\STARTUP.BAT`.
- **Programs that edit the root's files** (a game's installer adding `SET BLASTER=...` to `C:\AUTOEXEC.BAT`, or a
  driver to `C:\CONFIG.SYS`): **not redirected** (the owner, 2026-11-20). Under GLOS they reach MS-DOS's real
  files, as they would under MS-DOS; GLOS's own startup (`\GLOS\STARTUP.BAT`) is changed by hand. GLOS gives its
  sessions `BLASTER` for its own Sound Blaster already, so a game's sound needs no such line under GLOS.
- **DOS's own system files are read-only to programs under GLOS** (`IO.SYS`, `MSDOS.SYS`, the root
  `COMMAND.COM`): a program that tries is told access denied, and the log says which.

## N4: what DOS programs had from drivers

- **The mouse (INT 33h):** GLOS emulates it for apps already (supervisor.md §12.5, graphics.md §5.4); DOS programs in a
  machine get the same, from the PC's PS/2 or serial mouse, or USB through GLOS. No CuteMouse.
- **Long names:** in the personality (N2, 71xxh). No DOSLFN.
- **The keyboard's layout:** `[region] layout` reaches the desktop and apps today; native, the machine's INT 16h
  and INT 09h see the layout's characters too. No KEYB.
- **Memory managers:** none needed. GLOS is the XMS server already; upper memory and EMS become a page-table
  matter per machine, which D25 can revisit (its reasons were DOS's arena).
- **CD-ROM (optional, later):** ATAPI, ISO 9660, and MSCDEX's INT 2Fh 15xxh for programs (GLQuake asks for it:
  CD audio); ISO and CUE/BIN images as drives, their audio played through GLOS's mixer.
- **Not supported at first:** DOS device drivers loaded from CONFIG.SYS (`DEVICE=`). GLOS has its own for what
  matters; a loader for character-device drivers could come later if a program needs one.

## N5: the setup floppy and installing with nothing of FreeDOS's

- `make floppy` makes a disk with GLOS's boot sector, `\GLOSLDR.SYS` and `\GLOS`, nothing of FreeDOS's. ATTRIB,
  XCOPY, MORE and DELTREE become built-ins of GLOS's shell (none is yet; COPY and MOVE are); MEM's job is
  `glos mem`'s.
- `install.py --new-disk` partitions and formats with `DISK` (as now) and puts GLOS's boot records and files on:
  no `KERNEL.SYS` copied.
- `install.py` onto a disk that has MS-DOS or Windows 9x: GLOS into `\GLOS`, its boot records with the DOS's
  sector saved (N3), the menu's default GLOS or DOS as asked.
- The 486 (bench C) is the floppy's test, as for S3.

## N6: several DOS programs at once

The owner's choice of 2026-11-13 ([shell.md](shell.md), "later"): with INT 21h answered by GLOS, each DOS program
can have a machine of its own (its own 640 KB, its own virtual devices), text programs in separate terminals.
After N2 works in the one machine; its own plan then.

## The order, and how each step shows it works

| Step | What | Shows it works |
|---|---|---|
| N0 | This design; the census of what the acceptance programs ask of DOS (`tools/census`) | The owner's review; [native-dos-census.md](native-dos-census.md) |
| N1 | The kernel's file system: FAT12/16/32 and long names over INT 13h and the cache; the kernel's users moved onto it; the DOS server kept as the second back end while hosted mode lives | Host tests against `mtools` and `fsck.fat`; Windows 98's ScanDisk on volumes GLOS wrote; Loop A power cuts |
| N2 | The DOS personality in one machine: the checklist's functions area by area, and the tables programs read | The conformance program ([below](#the-conformance-program)) matching MS-DOS 7.1's transcripts; the census run again under native GLOS finds nothing unanswered; no line saying a function went unanswered in the gate |
| N3 | GLOS's loader from its own boot sector; the boot manager (same partition and other partition) | Loop A from a native disk image; MS-DOS 7.1 and Windows 98 started from GLOS's menu on the same volume |
| N4 | INT 33h, the layout, (CD-ROM) for DOS programs | The survey's suites in a `native` variant against `base`; DJGPP's tests, HDPMI's regression, TPX; GLOS's gate on a native boot |
| N5 | The floppy and `install.py` with nothing of FreeDOS's | `loopa-floppy`; the 486 |
| N6 | A machine per DOS program | Its own plan |
| then | Hosted mode removed (`GLOS.EXE` from DOS) | The wider corpus (N0) as well as the acceptance set; the benches on native GLOS: bench-g400 and the P3 beside their MS-DOS 7.1 and Windows 98 |

The benches: bench C (the 486, no VME: where DOS calls cost most today, DOSCALLS before and after), the P3
(Windows 98 SE on the same disk: the boot manager and FAT32 with long names shared), bench-g400 (MS-DOS 7.1 on
the same volume).

### The conformance program

- **`tests/dos/dosconf`**, built twice: 16-bit (Open Watcom, real mode: INT 21h straight) and DJGPP (through
  the DPMI host's reflection, as programs reach DOS from protected mode). One case per function of the checklist,
  each printing what came back (registers that carry results, the carry flag, error codes, the buffers), on a
  scratch drive the run makes (files, folders, long names, attributes, sharing modes, disk full, a missing drive).
- **Transcripts compared:** MS-DOS 7.1 is the reference, FreeDOS beside it; GLOS's must match MS-DOS 7.1's, or the
  difference is listed with its reason (as the shell's known differences are, [shell.md](shell.md)). Where MS-DOS
  7.1 and FreeDOS differ, the transcript says so: programs may have been written against either.
- **Where MS-DOS 7.1 runs:** Loop A's boot images are FreeDOS's; an MS-DOS 7.1 image (the owner's, private like
  `BORLAND_DIR`) for Loop A, or bench-g400 and the P3, which boot MS-DOS 7.1 already.
- **The tables:** cases read the List of Lists, the SDA, the SFT, the CDS, the DPBs and the MCB chain as RBIL lays
  them out, after opening files and changing directories, and print the fields: the same transcript rule.

## Risks

- **Programs that read DOS's insides** past what the tables above give (undocumented fields, the SFT walked
  for names, INT 2Fh 12xxh). The census sees calls, not reads of memory: such programs are found by running
  them. FreeDOS's kernel is a behavioural reference here (clean room, PRD D26), and RBIL.
- **A FAT bug loses data**, on a disk that may be MS-DOS's and Windows 98's too. Images first, `fsck.fat` after
  every write test, ScanDisk in 86Box, the real disks last.
- **Raw sector writes by programs** (INT 25h/26h, INT 13h: DOSLFN did it; disk tools) under the file system's
  cache: refused on a mounted volume unless the program has the disk to itself (the cache flushed and dropped
  first), and logged.
- **Old BIOSes:** INT 13h without the LBA extensions reaches 8 GB at most (CHS); some BIOSes are slow or wrong.
  Native disk drivers remove the BIOS from the path ([Beyond DOS](#beyond-dos-what-a-dos-of-gloss-own-makes-possible)).
- **Size:** N2 is the largest piece of GLOS since the DPMI host (`kernel/dpmi` is 3,852 lines today) and likely
  larger; N1 a few thousand lines with its tests.

## Beyond DOS: what a DOS of GLOS's own makes possible

Ideas for the owner (2026-11-20), with a rough cost: **S** small, **M** a milestone's step, **L** a milestone.

### Better than DOS's kernel

- **Several callers at once** (N1, N2): kernel threads, apps and machines in DOS at the same time, where DOS
  allows one call in the whole PC. **Part of N1/N2.**
- **Conventional memory:** no DOS kernel, buffers, COMMAND.COM or drivers below 640 KB: about 630 KB free in each
  machine. **Part of N2.**
- **Per-machine views of the drives:** SFTP's remote folders, archives (Files reads ZIP, LZH, ARJ and 7z
  already), ISO images, a RAM disk, as drive letters for DOS programs, without TSRs. **M.**
- **Isolation:** a program that crashes takes its machine, not the PC. **Part of N6.**
- **Settings without a restart:** GLOS.CFG's changes apply at once. **S.**

### Shared resources

- **One resource manager** for PCI, ISA Plug and Play and (later) ACPI's tables: each device GLOS's, given to one
  machine directly, or virtual. GLOS does this already for the Sound Blaster, the keyboard controller, the game
  port and the NICs (docs/devices.md); native GLOS extends it to every device. **M.**
- **Networking for DOS programs:** a packet driver (INT 60h) or a virtual NE2000 over GLOS's network, so mTCP and
  the like share the card with GLOS; IPX over UDP so DOS games play over the LAN or further. **M.**
- **Printing:** LPT output spooled by GLOS (to a printer on the network, or a file). **S-M.**
- **The clipboard:** the WinOldAp functions (INT 2Fh 17xxh) between DOS programs and the desktop. **S.**

### SMP

- **The hard part is the kernel, not DOS:** it uses "interrupts off" as its lock (an app's call runs with
  interrupts off). Needed: the local and I/O APICs (the virtual PIC stays for machines), the MP table or ACPI's
  MADT, the other CPUs' start, per-CPU tables and run queues, the APIC timer, TLB shoot-downs.
- **In two steps:** one lock around the kernel first (code outside it runs in parallel: DOS programs, apps,
  DOS-GL's software tiles, the mixer); finer locks where measurements show contention.
- **What it gives:** a game on one CPU while the network, SSH, the desktop and sound run on another, with less
  stutter; several machines side by side; apps' own threads (phase B step 4) spread out.
- **Hardware:** bench F's Pentium 4 with Hyper-Threading is two logical CPUs today; a dual Slot 1 440BX board
  next. **L.**

### Power

- **Idle detection for DOS programs:** a machine that polls the keyboard (INT 16h 01h in a loop) or says it is
  idle (INT 28h, INT 2Fh 1680h) sleeps instead of spinning, and the CPU halts: cooler and quieter, and fair to the
  other machines. **S-M; the biggest everyday win.**
- **APM 1.2 on period PCs:** power off, standby, CPU idle; VBE's DPMS for the screen (the savers exist). **S-M.**
- **ACPI later:** power off and the power button need part of ACPI's AML (or ACPICA, BSD-licensed, built in as
  lwIP is); then SpeedStep and C-states (Pentium III-M, Pentium 4, Pentium M), and disks spun down once GLOS
  drives them. **L.**

### Speed control: a fast PC run as a 386SX or a 486DX2 (the owner's idea, 2026-11-20)

A game written for a 486 may run too fast on a Pentium III, and far too fast on a 4 GHz CPU of today. DOS's
slowdown tools (MOSLO and the like) burn a share of each timer tick: one speed for the whole PC, coarse, and the
program's own calibration loop often sees through them. GLOS owns the scheduler, the timers, the page tables and
the performance counters, so it can pace each machine.
- **An instruction budget** (the main way, from the Pentium Pro on): the performance counter of retired
  instructions interrupts on overflow; the machine gets N instructions a millisecond (a 486DX2-66 runs roughly
  25-30 MIPS on typical code) and is stopped until the next one when they are used. A program's delay calibration
  then measures the target, because that is what it gets. DOSBox's "cycles", on the real CPU.
- **Time slices** (any CPU without such counters): the machine runs a share of each short period and halts for
  the rest; rougher, the program running in bursts.
- **Slow memory** (optional): the machine's memory mapped uncached through the page tables (PAT), so that
  memory-bound code behaves as it did then, for that machine alone.
- **The CPU's clock modulation** (IA32_CLOCK_MODULATION, from the Pentium 4 and Pentium M): even its 12.5% is far
  faster than a 486 on a CPU of today; with the above it saves power rather than doing the pacing.
- **An older CPU's identity** (optional): Intel's CPUID faulting (Ivy Bridge on) makes CPUID trap at user level,
  so that GLOS answers it as the profile's CPU would, for games that choose code by CPU type.
- **The hardware's own time stays real:** the PIT, the RTC, the VGA's retrace and sound DMA run as on a real 486;
  only the program's speed is paced.
- **Profiles measured on silicon:** 386SX-16, 386DX-33, 486DX2-66, Pentium 90 ..., set by a small speed test run
  on bench C (the 486DX4) and the P3, then under GLOS in a loop until they agree (never from 86Box's speed).
- **The controls:** a setting per program, and a "turbo button" (a hotkey or the task bar) that changes the speed
  while it runs. Borland's "Runtime error 200" (Turbo Pascal's CRT unit dividing by its calibration on fast CPUs)
  goes by pacing a program's first ticks only.
- **What it needs:** not GLOS's own DOS (today's GLOS runs DOS in its machine already, so pacing that machine
  paces every DOS program in it); a speed per program needs N6; best with SMP (the paced game on one core, GLOS's
  network, desktop and sound at full speed on another). **M.**

### Newer hardware: the BIOS is the next thing under GLOS

- 32-bit protected mode and V86 mode are on every x86 CPU, even where the firmware is UEFI only. What such a PC
  lacks is the **BIOS**: INT 10h, 13h, 15h, 16h.
- **The path:** (1) GLOS's own DOS (this plan); (2) native disk drivers (IDE with bus-master DMA on the PIIX, then
  AHCI, USB mass storage), the BIOS only a fallback; (3) the display through UEFI's GOP framebuffer, input through
  GLOS's USB, and a **small virtual BIOS in each machine** for DOS programs; (4) GLOS started by UEFI on a current
  PC. Each step is useful on period PCs too: native disks are faster and reach past 8 GB.
- **GLOS stays 32-bit:** long mode has no V86 mode, and 4 GB is plenty for this software.

### Room to grow

- **Loadable drivers** (a module format and a stable interface), so storage, network and sound drivers ship as
  files. **M.**
- **Installable file systems** behind N1's interface: network, archives, ISO, and later others. **M.**
- **Personalities as parts:** DOS first, then Win16 (the Win16 study). **L.**

## Open questions

1. **The checklist's statuses** ([native-dos-rbil.md](native-dos-rbil.md)): proposals, to change where the owner
   sees otherwise (for example ANSI.SYS later rather than now, DOSKEY absent, the clipboard calls in).
2. **Which of "Beyond DOS" go on the route**, and when.

Answered 2026-11-20: N1 beside phase F; no redirect of the root's files; the names as proposed.
