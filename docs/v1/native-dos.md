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
- **Decided after the review of the plan's gaps** (question tool, 2026-11-20):
  - **Disks through GLOS's own IDE driver from native GLOS's first version** (not the BIOS first); the BIOS for
    floppies and other controllers ([N1](#n1-decided-in-detail-2026-11-20)).
  - **DOS on another partition:** the boot manager **flips the active flag** in the MBR
    ([N3](#the-boot-manager)).
  - **Drive letters under GLOS as the DOS beside it sees them**, so a path is the same in both systems.
  - **Hosted mode goes** when native GLOS passes the acceptance set, the conformance program and the gate on
    native boots, and runs on the benches beside their DOS. The wider corpus is not a condition.
  - **Before hosted mode goes, native GLOS has its own CD-ROM, EMS and USB mass storage**
    ([N4](#n4-what-dos-programs-had-from-drivers)), beside the mouse, long names and the keyboard's layout.
  - **MS-DOS 7.1 for Loop A:** the owner provides an image (private, like `BORLAND_DIR`), for the conformance
    program's reference and the boot manager's tests ([N7](#n7-loop-a-and-the-gate-on-native-boots)).
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
  - **when hosted mode goes** (the owner, 2026-11-20): the acceptance set, the conformance program and the gate
    on native boots, and the benches running native GLOS beside their DOS; the wider corpus is not a condition.

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
parts A-D, pinned in `tools/setup/versions.mk`): every entry of RBIL's DOS category for INT 20h-2Fh; INT 2Fh's
functions of DOS's own parts and of what GLOS stands in for (Windows' and DPMI's 16xxh, XMS, MSCDEX, the clipboard,
KEYB, DOSKEY, ANSI.SYS, APPEND, the task switcher); and, since the owner's answers of 2026-11-20, the disk BIOS's
standard INT 13h functions (GLOS's IDE driver answers them), Microsoft's mouse driver's INT 33h and LIM EMS's and
VCPI's INT 67h. Each has the census's count and a proposed status. As made (2026-11-20): 604 entries;
**implement 346**; **absent 124** (answered as MS-DOS 7.1 answers without the feature: the network server and
redirector, printer setup, SHARE's internals, APPEND, DOSKEY, NLSFUNC, Windows' own, VCPI, the XT's and PS/2's
disk functions on IDE disks); **internal 47** (INT 2Fh 12xxh, DOS's services for TSRs and redirectors: after the
core); **callout 17** (DOS calls them and programs answer: INT 23h, 24h, 28h, 2Ah; Windows' broadcasts, which
GLOS doesn't make); **null 9**; **later 1** (ANSI.SYS, with the console); **other 60** (European MS-DOS 4's
multitasking, DOS 2.11's OEM calls, DOS 4's extended attributes ...). The statuses are proposals: the script's
`OVERRIDES` holds the ones made by hand, for review.

N2 works through **implement** in its areas' order (processes, handles, directories, long names, FCBs,
information, INT 2Fh), each area's census functions first; **absent** and **null** come with their area (the
right error is part of the answer); **internal** after the core.

## N1: the file system

- **FAT12, FAT16 and FAT32 with long names (VFAT)**, read and write, in the kernel, over GLOS's own block layer:
  GLOS's IDE driver for IDE disks (the owner's choice: from native GLOS's first version), the BIOS's INT 13h
  (`kernel/disk/bdisk.c`, through the system VM's thread) for floppies and other controllers, and one block cache
  under both (today's `kernel/drv/cache.c` and `cstore.c`, which cache the BIOS's INT 13h, moved under the block
  layer).
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
- **While hosted mode still exists** (until the owner's conditions for its end are met: [the order](#the-order-and-how-each-step-shows-it-works)), the kernel's file system has a
  second back end: the DOS server as today. Two writers on one volume would corrupt it, so the native back end
  is never used under a DOS.
- **Tests:** host tests against `mtools` and `fsck.fat` (as S0's were), images made by Windows 98 and MS-DOS 7.1
  in 86Box; power cut in Loop A in the middle of writes, then `fsck.fat` and ScanDisk.
- **In steps** (beside phase F, the owner's choice; none changes what GLOS does but behind a setting that is off by default (N1c), and nothing native runs before N3):
  - **N1a, reading:** `kernel/fs/fat.c`, plain C over a sector callback as `kernel/disk` is: a volume mounted
    from its boot sector (FAT12, FAT16, FAT32), cluster chains, folders with their long names (UTF-8, the short
    names in the code page), paths looked up by either name without regard to case, files read and sought, free
    space. Host test: trees of files with long and short names, deep folders, empty and fragmented files, copied
    by mtools into volumes mkfs.fat and GLOS's own `fat_format` made, then read back through the library and
    compared byte for byte. **Done 2026-11-20** (`kernel/fs/fat.c`, `make fat-test`, in `host-test` too): eight
    volumes pass, FAT12/16/32 from mkfs.fat (clusters of 512 bytes to 32 KB) and from `fat_format`, 154-334 files
    each, the free space and the label as mtools has them; built with the kernel's flags (5.8 KB of code, not yet
    linked into GLOSK.BIN: nothing uses it).
  - **N1b, writing:** clusters allocated and freed (FSInfo's hints kept), files made, written, cut short,
    deleted and renamed, folders made and removed, long names with their short names made as Windows makes
    them (`~1`), attributes and times; the writes in order and the dirty bits. Host test: random sequences of
    those against a model of the tree, `fsck.fat` after each, mtools reading the result. **Built 2026-11-21**
    (`fat_mount_rw`, `fat_create`, `fat_write`, `fat_truncate`, `fat_unlink`, `fat_mkdir`, `fat_rmdir`,
    `fat_rename`, `fat_setattr`, `fat_settime`, `fat_flush`; `FAT_READ_ONLY` leaves them out of the loader's
    16-bit build). Write-through until N1c's cache, in an order that leaves the volume repairable wherever it
    stops: a cluster's FAT entries (end mark, then the link to it) before the data, the data before the size
    that covers it, a smaller size before the chain is cut and freed, a new folder's cluster zeroed and its "."
    and ".." written before the entry that points at it, a renamed entry's new entries before its old ones go, a
    deleted name's short entry before its long-name entries and those from the nearest back to the first (a cut
    the other way round leaves fragments without their start in front of a live entry, which `fsck.fat` won't
    repair: the cuts found it). Every loop over the disk's structures is bounded (a folder at 65,536 entries, a
    chain at the cluster count), so a damaged volume gives an error, never a hang (the cuts found one too: a
    folder grown by a link that never reached the disk). A FAT12 entry across two sectors can't be written at once: the half
    written first is the one whose half-way value is the old, the new, or past the volume's clusters, which fsck
    and ScanDisk end the chain at (the low byte first for a link, the high first for a new end; the cuts found a
    torn link).
    The host test is one script (rather than random sequences) done twice, through `fat.c` on a volume and
    through the host's own calls in a mirror folder, then: mtools' copy of the volume compared with the mirror,
    `fsck.fat -n` clean, the reader's own check of every file by both names, mtools' free space; and the same
    script cut short at sampled writes (a power cut: what follows is dropped), where `fsck.fat` may find only
    lost clusters, chains longer than their files, the FAT copies apart, or long-name entries left alone,
    never a file longer than its chain or a cluster in two chains. The short names Windows 9x gives are checked
    by name (`LONGFI~1.TXT` to `~6`, `X_Y_Z~1.TXT`, `HIDDEN~1`, `CAFÉ.TXT` in code page 850). Left for later:
    the creation time's 10 ms (the clock callback gives DOS's 2 seconds), the volume label written (DOS's create
    with attribute 08h, N2), FSInfo's free count worked out when it says "unknown" (N1c, at mount, by the
    cache's scan).
  - **N1c, GLOS's IDE driver and the block layer:** the driver (PIO), the cache under it, INT 13h for its disks
    answered by GLOS; proven under hosted GLOS behind `[cache] driver = bios | glos` (bios by default; first
    named `[disk] driver`, put with the cache because the cache is what makes the calls), with DOS, ScanDisk and
    the gate on top of it, before native GLOS relies on it. **Under way 2026-11-21:** part 1, the ATA protocol
    (`kernel/drv/ata.c`, plain C over port callbacks, host-tested against a model of a channel); part 2, the
    kernel's side (`kernel/drv/ide.c`: the channels, a clock that breathes) and the cache's reads, block fills
    and writes going to it for each BIOS drive matched by sector 0. Hosted, the driver never resets a channel or
    changes a drive's multiple (the BIOS's settings stay), holds the channel's interrupt off for each command
    and leaves none pending, and answers CHS calls with the AL the BIOS was seen to give. The other INT 13h
    calls for those drives are answered without the BIOS: 08h, 15h, 41h and 48h as the BIOS answered them once
    at the switch (its geometry is what the partition tables were made with), resets, seeks, ready and
    recalibrate as done, 01h the last status, verifies (04h, 44h) by the drive's own READ VERIFY; formatting,
    long reads and writes and calls the cache can't decode still go to the BIOS (their errors stay its own).
    A CHS call's AL is answered as the BIOS was seen to answer it, learnt apart for reads, writes and verifies
    (86Box's BIOS gives 0 for all three; DSKTORT's digest and a trace of its calls showed each). Loop A's
    disks are under 1,024 cylinders, where 86Box reports no LBA and its BIOS sets no multiple: the driver's
    CHS single-sector path is what Loop A proves; LBA, 48-bit and READ/WRITE MULTIPLE are the host test's and
    the benches'. (A Loop A scratch disk past 504 MB needs RetroGPU's run.py to allow one: it keeps them to the
    BIOS's CHS limit.) Still to come in N1c: the gate run with `driver = glos` as the default (every suite on
    it), the benches' real disks, the cache keeping writes for 2 seconds, IRQ-driven transfers.
  - **N1d, many callers:** a lock per volume, handles that stay right while another caller changes the folder
    they are in. **Built 2026-11-21** (`kernel/fs/fsv.c`, plain C with the lock as callbacks; not yet linked
    into GLOSK.BIN: nothing uses it before N1e): every call under the volume's lock; a file's handles share one
    node (its entry's place, size and first cluster), each handle its own position and place in the chain,
    dropped when another handle changes the chain other than by growing it (the node's generation); an open
    file can't be deleted, renamed, made again or have its attributes changed (`FSV_ESHARE`, as DOS with SHARE
    answers); access and denial per open (DOS's sharing modes, compatibility mode treated as deny-none until
    N2 gives it DOS's rules and whose process opened what); 64 files open on a volume. Entries never move when
    others are made, deleted or renamed, nor when their folder is renamed or moved, so an open file's entry
    stays its own. A folder listed between calls holds no lock: what others do meanwhile may or may not be
    seen (entries there throughout are seen once). Host test `fsv_test` (in host-test), checked by mutation:
    without the generation, a handle's stale place reads another file's clusters and the test fails.
  - **N1e, the kernel's users onto it** behind the file system's interface, the DOS server staying the back end
    while GLOS starts from a DOS (hosted mode: never two writers); used only by native GLOS, from N3.
  - **N1f, bus-master DMA** on the PCI IDE controllers (PIIX, ICH, VIA, SiS, ALi), measured on the benches;
    before hosted mode goes.

### N1 decided in detail (2026-11-20)

- **Long names written as Windows 9x writes them:** long-name entries whenever the name isn't a valid 8.3 name in
  upper case in the code page (so `readme.txt` gets one, as on Windows 98); NT's case bits read, never written.
- **Short names made as Windows 9x makes them:** upper case in the code page; what the page hasn't got, and what
  8.3 doesn't allow, becomes `_`; spaces and leading dots dropped; the base cut to six and `~1` added, the number
  raised until the name is free in that folder (`~1` to `~999999`, the base shorter as the tail grows).
- **Times:** local time (GLOS's clock with `[region]`'s time zone), as DOS and Windows keep them; creation time
  to 10 ms; the last-access date set when a file is made or written, not on reads (Windows 98 sets it on reads
  too: GLOS spares those writes).
- **Write-back:** a written sector stays in the cache 2 seconds at most; sooner at idle, by a commit (68h, 6Ah),
  before a restart or power off, and before a removable disk can change. The order: the data, each FAT copy
  (FAT32 with mirroring off: the active one), the directory entry, FSInfo. The volume's dirty bit (FAT16, FAT32)
  is set before the first write and cleared by the flush that leaves nothing unwritten (Windows 98 runs ScanDisk
  at its start when it finds it set).
- **Removable disks:** the change line (INT 13h 16h; ATAPI's media status for CD drives) asked before a floppy's
  first access after 2 seconds idle, and the volume's serial number compared; a change drops that volume from
  the cache; a change with writes pending is DOS's "Invalid disk change" (INT 24h).
- **Partitions:** the primary table and the extended chain (`mbr.c` gains the EBR walk). Types 01h, 04h, 06h,
  0Bh, 0Ch and 0Eh are FAT volumes; hidden types (11h, 14h, 16h, 1Bh, 1Ch, 1Eh) and other systems' get no letter,
  as under DOS; `DISK` lists them all.
- **Drive letters as the DOS beside GLOS sees them** (the owner, 2026-11-20): MS-DOS's order worked out as if the
  DOS's partition were the active one (the boot manager's DOS entry), so `C:\GAMES` is the same path in both
  systems; GLOS's own partition gets the letter DOS gives it, or the next free one where DOS gives none. With no
  DOS on the disk, MS-DOS's order with GLOS's partition active. The rules are checked against MS-DOS 7.1 and
  Windows 98 in 86Box (N7): a primary partition per disk, disk by disk, then the logical drives, then the other
  primaries (Windows 9x's; MS-DOS 6.22 gives a second primary no letter). CD drives after the disks, as MSCDEX
  letters them; USB drives as they are plugged in, the next free letters.
- **The IDE driver** (N1c): the channels at 1F0h and 170h (ISA, VLB, and PCI controllers in compatibility mode)
  and a PCI IDE function's own ports in native mode; IDENTIFY; LBA28, LBA48 past 128 GB, CHS for drives without
  LBA; PIO with READ/WRITE MULTIPLE first, by IRQ 14 and 15 (polled where the interrupt isn't free); FLUSH CACHE
  before a restart and power off; the drive's write cache left as the BIOS set it; ATAPI's packets for CD drives
  (N4). SATA controllers in IDE (legacy) mode are IDE to it; AHCI later ([Beyond DOS](#newer-hardware-the-bios-is-the-next-thing-under-glos)).
- **Once GLOS drives a controller, the BIOS never touches its disks again:** GLOS answers INT 13h for them itself
  (the checklist's INT 13h section: what an AT BIOS answers), for DOS programs and direct-mode sessions alike,
  and their ports are never given to a program (direct mode excludes them). Floppies and the disks of other
  controllers (SCSI cards with their own BIOS, the BIOS's USB emulation until N4) stay the BIOS's.
- **Stacks:** the file system runs on the caller's kernel stack; its frames stay under 3 KB (N1a's deepest, a
  lookup, is about 2.5 KB).

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

### N2 decided in detail (2026-11-20)

- **A machine's memory:** the vector table and the BIOS's data area as the BIOS left them; 0500h-05FFh as DOS has
  it; GLOS's DOS area from 0060:0000h (the entry stub for INT 20h-2Fh, the List of Lists with the device chain,
  the SFT with `[dos] files` entries, 40 by default, 26 CDS entries (LASTDRIVE Z), the DPBs, the SDA); then the
  first MCB. Target: at least 625 KB free for programs (MS-DOS 7.1 with DOS=HIGH,UMB gives about 620 KB). The HMA
  stays free for XMS's clients: GLOS's DOS is "low". UMBs (58xxh's link, LOADHIGH) from upper-memory pages GLOS
  maps (no ROM, no BAR, as `kernel/mm/umapage.c` finds them).
- **The root process:** a PSP of GLOS's own, its own parent, as COMMAND.COM's is under DOS; it owns the master
  environment (`[shell] envsize`); programs the kernel's shell starts are its children; GLOS's COMMAND.COM runs
  batch files as now.
- **Identity:** MS-DOS 7.10 (30h: 7.10, OEM FFh; 3306h: 7.10 revision 0, DOS neither in the HMA nor in ROM), long
  names and FAT32 there. A version per program can be set (`[dos] version PROGRAM.EXE = 6.22`: SETVER's job).
- **Date and time:** 2Ah and 2Ch from GLOS's clock in local time; a program's 2Bh or 2Dh moves only its machine's
  date and time (an offset), not GLOS's clock, which GLOS's DATE and TIME and SNTP set.
- **Calls back into the machine:** INT 23h, 24h, 28h and 2Ah are made as DOS makes them, as nested real-mode calls
  on the machine's thread (supervisor.md §9.5), with InDOS as DOS has it around each; an INT 24h handler's answer
  (retry, ignore, fail, abort) is acted on as DOS acts on it.
- **CON:** input through the machine's own INT 16h (its BIOS, fed by GLOS's virtual keyboard), cooked or raw by
  IOCTL 4401h; Ctrl-C, Ctrl-S and Ctrl-P as DOS has them; output through the machine's INT 29h and INT 10h's
  teletype, and into a job's capture (supervisor.md §17.4) with no SFT walk. AUX through the BIOS's INT 14h, PRN
  through its INT 17h; COM1 stays GLOS's log port.
- **The DPMI host:** bootinfo's `indos` and `sda` point at GLOS's own SDA; the DOS lock and its safe point
  (§17.1, §17.2) become the personality's own locks; a protected-mode call reaches the personality without V86
  when the vector is GLOS's (above).
- **A function GLOS doesn't answer** logs `GLOS-DOS unanswered int= ax= prog=` and is answered as MS-DOS 7.1
  answers an unknown function (AL 0; carry set with 01h where the function reports by carry); the gate fails on
  that line.
- **Targets** (the 486DX2 profile without VME, `tests/dos/doscalls.c`): INT 21h 30h from a program 70 µs at most
  (55 under plain DOS, 474 under GLOS today); from a DJGPP program 150 µs; findfirst and a cached 512-byte read no
  slower than under plain DOS (653 and 527 µs).

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
   (`\GLOSLDR.SYS`, hidden and system: the owner's name) whole at 0060:0000h, as it loads `KERNEL.SYS` today (BL
   and DL the disk; about 150 KB at most, below where the boot code moves itself).
3. **The loader** (16-bit Open Watcom, a raw binary with no DOS runtime; much of today's `loader/` reused):
   `kernel/fs/fat.c` built 16-bit (clean since 2026-11-20) over INT 13h and its LBA extensions; `\GLOS\GLOS.CFG`
   read whole and handed to the kernel in bootinfo, with the boot disk and partition, the BIOS's disks and the
   volumes' letters; `GLOSK.BIN` read (packed or not, as now); memory by E820h/E801h/88h and A20 as raw mode does
   now; the menu ([below](#the-boot-manager)); bootinfo with a new `BI_F_NATIVE`; protected mode and paging as
   `loader/pm.asm` does. What stays below 1 MB is the first machine's resident code: the entry stub, the kill
   stub and the host's real-mode stacks (what `loader/stub.asm` holds today, without its EXEC loop).
5. **A PC started from a USB stick** (the BIOS's USB emulation): the loader reads through the BIOS; once GLOS
   takes the USB controllers (as it does for HID today), its own mass-storage driver (N4) carries the boot volume
   on, found again by its serial number.
4. **The kernel** builds the first machine from the BIOS's state (its vector table, its data area), mounts the
   volumes, puts the personality into the machine, and runs GLOS's startup ([names](#names-nothing-dos-or-windows-owns)).

### The boot manager

- **The same partition** as MS-DOS or Windows 9x (the owner's first wish): when GLOS's boot records are put on a
  volume that already starts a DOS, the DOS's boot sector is saved first (`\GLOS\DOSBOOT.BIN`, as Windows NT
  kept `BOOTSECT.DOS`). The loader's menu offers GLOS or that DOS; choosing DOS loads the saved sector at
  0000:7C00h and jumps to it with the boot drive in DL and DS:SI at the partition's table entry, as MBR code
  leaves them. DOS then starts from its own `IO.SYS` as if GLOS weren't there. Simple, and the way NT shared a
  volume with MS-DOS. Only sector 0 is GLOS's: a FAT32 volume's second boot code sector (2, MS-DOS 7.1's) stays,
  and so does the backup at sector 6 (a DOS tool that restores from it restores DOS's start; `DISK n BOOT` puts
  GLOS's back).
- **Another partition** (the owner, 2026-11-20: flip the active flag): DOS letters its drives from the table on
  the disk, so the DOS's partition must be the active one when it starts. GLOS's first stage (`mbrboot.S`, with
  the number of GLOS's partition kept in it) always starts GLOS's loader, whichever partition is active, so the
  menu is always there. Choosing DOS writes the active flag to DOS's partition if it isn't active already (two
  bytes of the MBR, the rest of the table as it was), then chains to its boot sector as above. Choosing GLOS
  changes nothing: GLOS gives letters as DOS sees them, active flag or not. MS-DOS must be on a primary
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

Before hosted mode goes (the owner, 2026-11-20), native GLOS has all of this section but `DEVICE=`.

- **The mouse (INT 33h):** GLOS emulates it for apps already (supervisor.md §12.5, graphics.md §5.4); DOS programs in a
  machine get the same, from the PC's PS/2 or serial mouse, or USB through GLOS. No CuteMouse.
- **Long names:** in the personality (N2, 71xxh). No DOSLFN.
- **The keyboard's layout:** `[region] layout` reaches the desktop and apps today; native, the machine's INT 16h
  and INT 09h see the layout's characters too. No KEYB.
- **Memory managers:** none needed. GLOS is the XMS server already; UMBs per machine (N2).
- **EMS** (the owner, 2026-11-20; revisits PRD D25, which is amended when it lands): LIM EMS 4.0 per machine (the
  checklist's INT 67h section): a 64 KB page frame in a free 64 KB of upper memory, pages from GLOS's memory,
  INT 67h's vector at a device header named `EMMXXXX0` (how programs find an EMM); `[dos] ems = KB`, 8 MB by
  default (as DOSBox has it), 0 for none, per program too; no VCPI (GLOS is the DPMI host, which DOS extenders ask
  for first).
- **CD-ROM** (the owner, 2026-11-20): ATAPI drives on GLOS's IDE driver; ISO 9660 (Joliet's long names later);
  MSCDEX's INT 2Fh 15xxh, 1510h's device requests too (programs ask for audio play, the Q channel and the
  drive's state that way); CD audio read digitally (READ CD) and played through GLOS's mixer, the drive's
  analog output where it can't; ISO and CUE/BIN images as drives too. GLQuake, GTA and Half-Life ask for MSCDEX
  (census).
- **USB mass storage** (the owner, 2026-11-20): GLOS's USB (UHCI today) gains OHCI and EHCI (non-Intel chipsets'
  USB 1.1, and USB 2.0) and the mass-storage class (Bulk-Only Transport, SCSI's commands): sticks, USB floppy
  and CD drives as removable drives; plugged in, the next free letter; pulled with writes pending, GLOS says so
  (the log, the desktop) and the program gets INT 24h.
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

## N7: Loop A and the gate on native boots

Every suite today boots FreeDOS and starts `GLOS.EXE`; before hosted mode goes, every suite boots native GLOS.
- **`make native-image`** (mtools on the host) makes disk images: GLOS alone; GLOS with FreeDOS on the same volume;
  GLOS with MS-DOS 7.1 on the same volume and on another partition, from the owner's MS-DOS 7.1 image (`MSDOS_DIR`
  in `config.local.mk`, private like `BORLAND_DIR`).
- **RetroGPU's `run.py`** boots them with `--hdd` (there today); the harness's programs (SERSAY, VECCHK, UTEXIT and
  the rest of `C:\HX`) run on GLOS's DOS; jobs.py's suites gain native boots; the gate runs on them.
- **MS-DOS 7.1 in Loop A** also gives the conformance program its reference and the boot manager its tests.

## The order, and how each step shows it works

| Step | What | Shows it works |
|---|---|---|
| N0 | This design; the census of what the acceptance programs ask of DOS (`tools/census`) | The owner's review; [native-dos-census.md](native-dos-census.md) |
| N1 | The kernel's file system: FAT12/16/32 and long names over GLOS's IDE driver and the BIOS, the cache under both, INT 13h for IDE disks answered by GLOS; the kernel's users moved onto it; the DOS server kept as the second back end while hosted mode lives | Host tests against `mtools` and `fsck.fat`; the IDE driver under hosted GLOS behind its setting (DOS, ScanDisk and the gate on top); Windows 98's ScanDisk on volumes GLOS wrote; Loop A power cuts |
| N2 | The DOS personality in one machine: the checklist's functions area by area, and the tables programs read | The conformance program ([below](#the-conformance-program)) matching MS-DOS 7.1's transcripts; the census run again under native GLOS finds nothing unanswered; no line saying a function went unanswered in the gate |
| N3 | GLOS's loader from its own boot sector; the boot manager (same partition and other partition) | Loop A from a native disk image; MS-DOS 7.1 and Windows 98 started from GLOS's menu on the same volume |
| N4 | INT 33h, the layout, CD-ROM (ATAPI, ISO 9660, MSCDEX, CD audio), EMS, USB mass storage | The survey's suites in a `native` variant against `base` (GLQuake with its CD audio); an EMS game; a USB stick in 86Box and on the benches; DJGPP's tests, HDPMI's regression, TPX |
| N5 | The floppy and `install.py` with nothing of FreeDOS's | `loopa-floppy`; the 486 |
| N6 | A machine per DOS program | Its own plan (not a condition for hosted mode's end) |
| N7 | Loop A's suites and the gate on native boots | The gate green on native images |
| then | Hosted mode removed (`GLOS.EXE` from DOS) | The owner's condition: the acceptance set on native boots, the conformance program matching MS-DOS 7.1, the gate on native boots (N7), the benches (bench-g400, the P3, the 486) running native GLOS beside their DOS |

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
  GLOS's IDE driver takes the BIOS out of the path for IDE disks; floppies and other controllers keep it.
- **GLOS's own IDE driver** (the owner's choice): controllers' quirks (the CMD640's and RZ1000's bugs, PIIX, VIA,
  SiS, ALi, Promise), drives without LBA or with odd geometries, a slave without a master, 32-bit PIO, a drive's
  write cache at power off. Tested in 86Box and on every bench (the 300PL's PIIX4, the P3, the P4's ICH5, the
  486's ISA IDE) under hosted GLOS first, behind its setting.
- **New USB host controllers** (OHCI, EHCI) and mass storage are sizeable, boards differ, and the BIOS's legacy
  emulation must be handed over cleanly (GLOS does it for HID already).
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

### A 386 floor (the owner asked, 2026-11-20; a probe made the same day)

- **The question:** could GLOS run on a 386SX? The 486 is the floor today (the owner's decision of 2026-10-01).
  Everything GLOS's design rests on is the 386's (paging, V86 mode, the I/O bitmap, the debug registers); the
  386 has no VME, as bench C's 486 hasn't.
- **The probe** (local branches `probe-386` in GLOS, `~/wt-glos-386`, and RetroGPU, `~/wt-rgpu-386`; nothing on
  main): a Loop A profile `386sx` (an AAEON SBC-350A, ALi M1217, AMIBIOS 1993, an Intel 386SX-33, no FPU, 8 MB,
  IBM's VGA; its CMOS set once by a key script and cached, `--from-start`), and GLOS built with `KARCH=i386`
  and `LOADER_DEFS=-dGLOS_ALLOW_386`: `invlpg()` reloads CR3 on a 386 (EFLAGS.AC found fixed at boot), and
  `GLOS.EXE` lets a 386 through, saying so. Everything else newer than the 386 was guarded already (CR4,
  CPUID, RDTSC), and the DPMI host already copes with no FPU (`kernel/dpmi/host.c`).
- **Found:** GLOS boots on it (`GLOS-RING0 cpu=386 tlb=cr3`, raw mode, the disk cache on its ISA IDE disk),
  runs a DOS program in a session, and is the DPMI host of a DJGPP program (DOSCALLS) with no FPU; both leave
  cleanly. With 8 MB, 4.5 MB is free once the kernel is in.
- **What it would take:** for headless GLOS (SSH, SFTP, the shell, DOS programs) the probe's changes made
  proper (the floor a setting or a build, the TLB fallback kept), a slimmer kernel (below), FPU emulation for
  apps that use floating point (DJGPP's emulator through the host's 0E01h), and a real 386 for speed (86Box's
  speed is no measure). The desktop is more: ISA cards have no linear framebuffer (banked VESA, or VGA's
  planar 16 colours), 16 MB is the 386SX's ceiling, and it is 5-10 times slower than a 486DX2.
- **The kernel's size** (the owner asked what uses that much memory, 2026-11-20): the probe's kernel is 786 KB
  of code, 147 KB of constants and 1.73 MB of static data: 16 job slots with their rings (644 KB,
  `kernel/dos/agent.c`), a frame bitmap for 4 GB (128 KB), lwIP's heap and pool (204 KB), static page tables
  (about 100 KB). At run time a DPMI context is about 92 KB (72 KB of it a full LDT), an SSH connection about
  90 KB, the desktop's back buffer 0.94 MB at 800×600×16 and VNC's shadow about 1 MB. About 1 MB of the static
  data could be allocated as needed, on every PC; loadable drivers (Room to grow) would take the code down
  further.
- **Decided (the owner, 2026-11-20):** the memory diet now, on a branch of its own (the job slots on demand, the
  frame bitmap and LDTs sized to need, lwIP's pools to the RAM); a **headless 386 tier later**, after native GLOS,
  with the diet and FPU emulation for apps; no desktop on 386s. The probe branches stay local until then.

### Newer hardware: the BIOS is the next thing under GLOS

- 32-bit protected mode and V86 mode are on every x86 CPU, even where the firmware is UEFI only. What such a PC
  lacks is the **BIOS**: INT 10h, 13h, 15h, 16h.
- **The path:** (1) GLOS's own DOS (this plan); (2) native disk drivers (IDE and USB mass storage are in this
  plan; AHCI next), the BIOS only a fallback; (3) the display through UEFI's GOP framebuffer, input through
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
   sees otherwise (for example ANSI.SYS later rather than now, DOSKEY absent, the clipboard calls in, EMS 8 MB by
   default).
2. **Which of "Beyond DOS" go on the route**, and when.
3. **The MS-DOS 7.1 image** (N7): where it is (`MSDOS_DIR`), and what it is (an installed hard disk image, a
   Windows 98 SE startup floppy, or both).

Answered 2026-11-20: N1 beside phase F; no redirect of the root's files; the names as proposed; GLOS's own IDE
driver from the start; the active flag flipped for DOS on another partition; letters as DOS sees them; hosted
mode's end (above); CD-ROM, EMS and USB mass storage before it; the owner's MS-DOS 7.1 image for Loop A.
