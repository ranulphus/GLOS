# Native GLOS: no DOS underneath

**Status (2026-11-22):** a design the owner reviewed (2026-11-20), being built. Built: N0 (the census, `tools/census`,
results in [native-dos-census.md](native-dos-census.md); the checklist of every DOS function from Ralf Brown's
Interrupt List, [native-dos-rbil.md](native-dos-rbil.md), `make rbil`); N1a-N1d (FAT read and write, GLOS's IDE
driver under the cache, many callers); N2's groundwork and GLOS's own INT 21h (`kernel/ndos/`), checked line by
line against MS-DOS 7.1 (DOSDIFF); native-DOS sessions NS1-NS3 (`glos ndos`, `loopa-ndos`); and native boot NB1-NB3
and NB4's first part (GLOS started by its own boot sector and GLOSLDR.SYS with no DOS on the disk, its own DOS in
the system VM under the agent, SSH, the console, Open Watcom and DJGPP programs and GLOS apps: `loopa-nboot`). Each
step's own section says what was built and how it was shown to work. Hosted GLOS (GLOS.EXE from DOS) is unchanged
but where a step says otherwise (N1c's `[cache] driver`).

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
- **In steps** (beside phase F, the owner's choice; none changes what GLOS does but behind a setting that is off by default (N1c), and nothing native runs before N3, but for N2's native-DOS
  sessions on disks GLOS alone mounts: the owner, 2026-11-22, [N2](#n2-the-dos-personality)):
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
    BIOS's CHS limit.) **Proven 2026-11-21/22:** the gate with `driver = glos` as the default (a throwaway
    branch, every suite's DOS disks on GLOS's driver): 33 of 33 after a fix (the Control Panel's letter for the
    new setting clashed with the cache's own); and bench-g400's real disk (a Fujitsu MPE3173AE on the PIIX4,
    LBA, the BIOS's 16-sector multiple; the ATAPI CD found on the secondary channel) with `driver = glos-read`
    and `enabled = verify`: DOOM's 17 MB copied by XCOPY and compared by FC /B (all 19 files the same), 40,027
    sectors read by GLOS's driver, 125,839 hit sectors read again by the BIOS and compared, no mismatch, no
    driver error. Still to come in N1c: `driver = glos` (its writes too) on a real disk, the cache keeping
    writes for 2 seconds, IRQ-driven transfers.
  - **N1d, many callers:** a lock per volume, handles that stay right while another caller changes the folder
    they are in. **Built 2026-11-21** (`kernel/fs/fsv.c`, plain C with the lock as callbacks; not yet linked
    into GLOSK.BIN: nothing uses it before N1e): every call under the volume's lock; a file's handles share one
    node (its entry's place, size and first cluster), each handle its own position and place in the chain,
    dropped when another handle changes the chain other than by growing it (the node's generation); an open
    file can't be made again or have its attributes changed (`FSV_ESHARE`, as DOS with SHARE answers), and
    (since NS2, as MS-DOS 7.1 does: DOSDIFF) it can be renamed (its handles follow it to the new entry) and
    deleted (its entry goes, its clusters stay its handles' until the last closes); access and denial per open (DOS's sharing modes, compatibility mode treated as deny-none until
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

- **Brought up early: native-DOS sessions** (the owner, 2026-11-22). Rather than wait for N3's boot, N2 runs
  under today's hosted GLOS as a kind of session: a machine whose INT 21h goes to GLOS's own DOS
  (`kernel/ndos/`), with drives only on volumes GLOS mounts itself through its own IDE driver and `fsv.c` (in
  Loop A, the scratch disk GLOS formats; on a PC, a disk or partition no DOS beside it uses), never a volume
  the hosted DOS has: one writer per volume, always. Real DOS programs test N2 end to end in Loop A long
  before native boot; the machine is the one native GLOS needs anyway (N6's several machines, pulled earlier).
- **Where it lives:** `kernel/ndos/` (the native DOS; `kernel/dos/` stays the hosted DOS server until hosted
  mode ends). Built so far, plain C and host-tested ahead of the rest: `share.c`, RBIL's table 01404 (DOS 7.x
  sharing) as data, every one of its 400 cells checked against RBIL's own text (`share_test`); `names.c`,
  FINDFIRST's template, matching and search attributes, and a byte's upper case in the code page
  (`names_test`); `exe.c`, what EXEC computes of a .COM or .EXE (the image from the header's pages and last
  page, memory needed and wanted, loading high when both allocations are 0, relocations by a factor for 4B03h's
  overlays too, the starting registers RBIL gives) (`exe_test`); `mcb.c`, the memory chain (48h, 49h, 4Ah,
  5801h, 5803h: first, best and last fit, upper memory only or first, the largest block told on failure, free
  blocks joined on allocating and resizing but not on freeing, a failed resize leaving the block as large as it
  can be (DOS 2.1-6.0's, to check against 7.1), upper memory linked through a DOS "SC" block from A000h)
  (`mcb_test`); `psp.c`, a PSP as RBIL's table 01378 lays it out (CALL 5's far call to F01D:FEF0, which wraps to
  0000:00C0, where N2's DOS area keeps a jump to its CP/M entry, and the HMA a copy while A20 is on) and a
  child's environment (the parent's strings, 0001h, the program's full name; 32 KB at most) (`psp_test`);
  `files.c`, files by handle (3Ch, 3Dh, 3Eh, 3Fh, 40h, 42h, 45h, 46h, 5Bh, EXEC's inheritance, a program's handles
  closed at its exit): the system file table in the machine's memory as RBIL's table 01642 lays it out (AUX,
  CON, PRN its first three, a file's fields brought up to date at each call), each program's handle table
  through its PSP, DOS 7's sharing table between every entry open on a file, the file itself `fsv.c`'s
  (`files_test`, mutation-checked); `path.c`, a program's path made a drive and a whole path (each drive's
  current folder, '.' and '..', both separators, the code page's upper case, parts cut to 8.3) and 60h's and
  47h's forms (`path_test`); `find.c`, 4Eh and 4Fh with their state in the DTA as RBIL's table 01626 has it
  (MS-DOS 5's layout, FAT32's cluster's high word in its reserved bytes), going on from where they were after
  other calls, several at once (`find_test`); `dtime.c`, 2Ah-2Dh: DOS's dates (1980-2099, leap years, the day of
  the week), a machine's own offset that its programs' 2Bh and 2Dh move, never GLOS's clock (`dtime_test`).
  To check against MS-DOS 7.1 (now in `MSDOS_DIR`): parts cut to 8.3 rather than refused, the longest whole
  path (127 here), 4Eh with nothing matching (12h here, or 02h), and FCB parsing (29h: RBIL doesn't give its
  separators and terminators).
- **Upper case in file names, answered 2026-11-22** from MS-DOS 7.1's own COUNTRY.SYS (the owner's Windows 98
  SE): its filename upper-case tables (65h's 04h) follow no rule. In 437 (US and UK alike) e acute is E,
  though 437 has E acute, and so is every accented letter but ü, ä, å, ç, æ, ö and ñ (Ü, Ä, Å, Ç, Æ, Ö, Ñ); in
  850 they are the accented capitals, but ÿ is "_". **The owner's choice (2026-11-22): GLOS keeps MS-DOS's own
  tables** (`kernel/ndos/fucase.c`, made by `tools/ndos/fucase.py` from the COUNTRY.SYS in `MSDOS_DIR`; each
  page's default country's: 437 and 850 001, 737 030, 852 Central Europe, 866 007). Some countries' tables
  differ (437 in Belgium, Switzerland, Sweden and Finland makes e acute E acute): a region setting may choose
  them later. File names GLOS makes on long-name volumes stay Windows 98's (`fat.c`'s short names, E acute).

### Native-DOS sessions: the design (2026-11-22)

Today's GLOS has one V86 machine, the system VM (`kernel/vm/v86.c`'s `vm`, one thread): its "sessions" are DOS
programs in it, its shell sessions kernel threads, its apps DPMI clients. A native-DOS session is GLOS's first
second V86 machine, the one N6 needs anyway. What it is:

- **Its memory:** a page table of its own for the first MB (fresh frames for 0-9FFFFh; nothing at A0000h-FFFFFh
  or in the HMA at first, so a program that touches them faults and is told), in an address space like an
  app's, freed with it. Today PDE 0 is the shared `kpt_low` everywhere; only the native machine's own thread runs
  with its private one, and switching from it to any thread without an address space of its own loads the
  kernel's (kernel threads read the system VM's BIOS data area through `vm_rd8`: they must keep seeing it).
- **Its thread and traps:** a thread kind of its own (beside `TU_VM` and `TU_APP`); `dispatch` and `back_to_user`
  route its V86 frames to the native machine's code, never to `vm_exception`/`vm_return` (the system VM's kill,
  posted work, PIC and HLT queue stay the system VM's). Its vector table points every vector at an ARPL in its
  DOS area, so every INT reaches GLOS (with VME the global bitmap's vectors trap as #GP, the rest through its own
  table as #UD; without VME all as #GP). No port is its: every IN and OUT is refused and logged.
- **No BIOS of its own:** the BIOS's hardware state is the system VM's, so a native machine never runs BIOS code;
  its INT 10h, 13h, 16h and 1Ah are GLOS's to answer (console through its terminal, later steps). No IRQ is
  delivered to it at first; its timer (INT 08h/1Ch) comes with the console.
- **Its drives:** volumes GLOS mounts itself (`fsv.c` over GLOS's IDE driver) on a disk no DOS has a letter for
  (in Loop A, the scratch disk). Every use of an IDE channel by GLOS (the cache's driver, a native volume) holds
  that channel's lock, and the cache holds it round its BIOS calls for drives on that channel: one user of the
  channel at a time, one writer of a volume always.
- **The steps:**
  - **NS1, the machine:** its memory, thread and traps; INT 21h 02h, 09h and 4Ch answered, the rest logged
    (`GLOS-NDOS unanswered`) and refused; a .COM loaded from a GLOS-mounted volume; the shell's `NDOS FORMAT`,
    `NDOS COPY` (from a hosted drive onto the native volume: two volumes, one writer each) and `NDOS RUN`,
    the program's output on the shell's terminal and its exit code back. Loop A: a test .COM run, its words
    and code checked. **Built 2026-11-22**, in three parts: (a) the system VM's low memory reached by
    the kernel through an alias of its page table (`VMLOW_BASE`: `vm_ptr`), so that it is the system VM's from
    every address space (dpmi, console, cache and sess suites unchanged); (b) the machine, `kernel/vm/ndm.c`:
    `TU_NDOS` threads, its first MB of fresh frames (`mm_space_new_low`), loaded by `user_switch` and left for
    the kernel's at any switch to a kernel thread, its traps routed by `dispatch` and `back_to_user`, every vector
    at an ARPL, INT 21h 02h, 09h, 30h (7.10) and 4Ch and INT 20h answered; (c) `NDOS` (`kernel/vm/ndcmd.c`):
    `DISKS`, `FORMAT n` (one partition of a hidden FAT type, so the DOS beside GLOS gives it no letter; only a
    blank disk or a native one), `DIR`, `COPY` (from a DOS drive through the DOS server), `RUN N:\path tail` and
    `RUN HELLO`; the native volume's disk read and written by GLOS's IDE driver only as work posted to the VM
    thread, never inside the BIOS's INT 13h (and so refused while the cache is off). `loopa-ndos` on the bf6 (VME)
    and the 486 (none): HELLO six times (its words, 42, the memory back each time), the scratch disk made native,
    a program copied there from C: and run with its tail (its words, its exit code 4).
  - **NS2, the groundwork wired:** files, paths, searches, PSPs, EXEC of .COM and .EXE, memory, date and time
    (`kernel/ndos/`); Loop A: test programs of GLOS's own (Open Watcom), the census's programs. **Part 1 built
    2026-11-22:** `kernel/ndos/dos21.c`, GLOS's own INT 21h over the groundwork, plain C over a machine's memory
    and registers (`tests/host/dos21_test.c` runs programs' calls simulated). A machine's low memory: the vector
    table, the List of Lists (0540h), DOS's own bytes and swappable area (05F0h), the ARPLs (0600h), the file
    table (0800h), a root PSP of GLOS's own (1180h, its own parent: where the first program's five handles come
    from), the memory chain from 1280h to A000h. EXEC and a program's end as DOS does them (the parent's SS:SP in its
    PSP, its return in the child's INT 22h, the child's INT 22h-24h given back, its handles closed and memory freed);
    4B01h and 4B03h; 31h; devices by name in any folder; 03h for a folder on the way that isn't there. Answered:
    00h, 02h, 06h (output), 09h, 0Bh, 0Dh, 0Eh, 19h, 1Ah, 25h, 29h, 2Ah-2Fh, 30h, 31h, 33h, 34h-36h, 38h, 39h-43h,
    44h (00h, 01h, 06h-09h, 0Eh; 10h, 11h refused), 45h-4Fh, 50h-52h, 54h, 56h, 57h 00h, 58h, 59h, 5Bh, 5D06h,
    60h, 62h, 63h, 66h 01h, 68h, 6Ah, 6Ch, 7303h; 71h refused as 7.1 refuses it with no Windows. The machine
    (`kernel/vm/ndm.c`) calls it with interrupts on, its memory reached through a kernel window of its own
    (`NDM_BASE`, `mm_ndm_map`: so that the VM thread's disk I/O lands in it); INT 10h 0Eh/0Fh/03h, INT 1Ah 00h and
    INT 2Fh 1680h answered there. `NDOS RUN N:\path` loads through it (an .EXE of any size the machine holds). Loop
    A: NDOSTEST (`tests/dos/ndostest.c`, Open Watcom's C library: files, folders, searches, memory, spawn of
    itself, date and time) 0 failures under the DOS beside GLOS and in a native machine, nothing unanswered, on
    the bf6 and the 486. **Against MS-DOS 7.1 itself:** `tools/ndos/msdos-image.sh` makes a boot disk of the
    owner's MS-DOS 7.1 (`MSDOS_DIR`: private, and so is the disk, kept outside the repository) for Loop A's `--hdd`;
    DOSDIFF (`tests/dos/dosdiff.c`: ~90 probes where the books leave DOS's answer open) ran there, its output kept
    as `tests/dos/dosdiff.msdos71.txt`, and `loopa-ndos` runs it in a native machine and compares every line (but
    `DOSDIFF_KNOWN`'s: the swappable area's size, GLOS's own). What it settled, and GLOS's DOS now does: 0Eh says
    26 drives; 59h's class, action and locus (02h/03h/12h 08h 03h 02h, 05h/10h 03h 03h 02h, 50h 0Ch 03h 02h, 09h
    07h 04h 05h, 01h/06h/0Ch 07h 04h 01h); a seek before a file's start succeeds (the 32 bits wrapped) and reads
    and writes nothing there; 56h renames an open file and 41h deletes one (its handles follow it, or keep its
    clusters until the last closes: `fsv.c`, safer than DOS's stale entries); 6Ch's bad action is 01h; 3Ah of
    the root is 05h; 60h of a bare device is `C:/NUL`, above the root is 03h (and so for every call: `path.c`);
    4Eh with nothing matching is 12h; 47h leaves AX 0100h; NUL's device word is 80C4h; 29h as built. Open: the
    census's programs themselves (they need NS3's console and NS4's DPMI first); console input (NS3); 36h on a
    large FAT32 volume (DOSDIFF's disk is 128 MB).
  - **NS3, the console and the clock:** INT 16h and 10h's teletype, cooked input, Ctrl-C, INT 23h and 24h, the
    timer IRQ. **Built 2026-11-22:** what the client of `glos ndos run` types (the SSH channel's
    data: `sshd.c` hands it to the command, which keeps what comes before the machine starts) goes through
    `dos_type` (VT100's arrows, Home/End, Insert/Delete, Page keys and F1-F12 as the PC's keys; CR, LF or CR LF as
    Enter; Backspace and DEL as Backspace) into a ring of 64 keys in GLOS's own DOS, which INT 16h (00h-02h, 05h,
    10h-12h) and INT 21h's input calls read: 01h, 06h, 07h, 08h, 0Ah (Backspace, Esc a new start, ^X for control
    characters, a bell when full), 0Bh, 0Ch, 3Fh from CON (cooked, a line with CR LF given out over the reads it
    takes, Ctrl-Z at its start the end; raw as 4401h sets it), 4406h on CON. A poll that finds no key gives the
    machine's time slice up. Ctrl-C (and with BREAK ON any call, and the output calls always, finding one waiting)
    says "^C" and calls the program's INT 23h with the call's registers, returning to GLOS by an ARPL at 005F:0006:
    IRET, or RETF with CF clear, makes the call again; RETF with CF set, or no handler of its own, ends the
    program (4Dh's AH 01h). The machine's input ending (the client's EOF), or its command going, ends a wait for a
    key, and the machine. The BIOS's data area as a program finds it (640 KB, an 80 x 25 colour screen, the ticks
    since midnight); the timer at 18.2 Hz from the kernel's 1024 Hz clock as INT 08h while the machine's IF is on
    (VIF with VME), one at a time, the next after GLOS's own INT 08h (the tick count, then the program's INT 1Ch)
    or the program's EOI to port 20h; INT 29h. Loop A: CONTEST (`tests/dos/contest.c`) with what the test types:
    a line by 0Ah, a key by INT 16h, 08h, Ctrl-C into its own INT 23h and the call made again, ticks counted by
    INT 1Ch and the tick count; and Ctrl-C ending it with no handler of its own.
  - **NS4, programs that need more:** DPMI on a native machine (DJGPP programs), EMS and XMS, a VGA of its own.
  - **NS5, a suite:** `loopa-ndos` in the gate.

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

### Native boot: the design (2026-11-22)

The owner's choice of 2026-11-22, after NS1-NS3: native boot next, before NS4 (DPMI in native machines, which
waits for N6). GLOS starts from its own boot sector and loader with no DOS at all; GLOS's own DOS
(`kernel/ndos/dos21.c`) answers INT 21h in the **system VM**, which keeps the BIOS; the DPMI host, apps, sessions,
the agent and the shell run on top as they do today.

**What leans on DOS today** (a survey of the kernel, 2026-11-22; file:line in the survey's notes, summarised):
the DOS server's users (GLOS.CFG, GLOS.DAT, the secrets, the clock's 2Bh/2Dh, SFTP, the shell's ~40 file calls,
DISK's DD, NDOS COPY); the stub's EXEC loop and its NEXT protocol (agent jobs, /RUN, the shell's COMSPEC), GLOS's
COMMAND.COM's GCMD protocol; apps' DOS calls (`serve()` through DOS's INT 21h vector, `adopt`/`unadopt` writing
the SDA's current PSP and DTA, PSP:2Eh and the vector table; api2fs.c's raw INT 21h); the DPMI host (INT 21h
reflected to real mode, dosx.c's buffer by 48h, PSP:0Ah and the terminate address, 31h and resident parents);
the V86 monitor's INT 21h trap (output capture, stdin, FS watches, EXEC snapshots, session ends); what the kernel
reads of DOS's own structures (the SDA's current PSP everywhere, InDOS, the List of Lists' SFT chain for console
handles, the MCB chain's owners and names, PSPs' parents, environments and tails, GLOS.EXE's PSP as every job's
parent and its environment as the master one); INT 2Fh chained to DOS's handler; drive letters and paths from
DOS (3305h, `cfg_path`, COMSPEC); conventional memory by 48h/49h (bdisk.c, display.c, crash.c, session.c,
dosx.c); the loader itself (a DOS program: `fopen`, `_dos_allocmem`, the refusals, XMS mode, the DOS pointers in
bootinfo); and the ways out (`glos exit` back to DOS, DOWN, /ROUNDTRIP).

**The decisions:**
- **GLOS's DOS keeps DOS's own structures, in DOS's layouts, in the system VM's memory**: the MCB chain, PSPs,
  the file table (it does already), the List of Lists, and an SDA laid out as DOS's (the critical-error flag,
  InDOS, the DTA at 0Ch, the current PSP at 10h), the current PSP and DTA kept *there* (read from there at each
  call, so a writer of them, as apps' `adopt` is, is obeyed). Then the kernel's readers of DOS's structures, the
  stub, the agent, sessions, apps and the DPMI host's ends work as they do, and the DOS server works unchanged
  (its INT 21h, made by the stub, is answered by GLOS's DOS). Moving the kernel's own files off the DOS server
  onto `fsv.c` is a later cleanup, not a condition of native boot.
- **Its places are a layout, not constants**: a native machine's DOS area is at 0500h-1280h; the system VM's
  first 64 KB hold the loader (GLOS's boot sector loads it at 0060:0000), so there GLOS's DOS puts its tables in
  a block of its own (owner 0008h, as DOS's) after the loader's.
- **No wait blocks the system VM's thread in a trap.** A native machine's thread is its own; the system VM's
  thread is also the apps' and the DOS server's. A call that must wait for a key returns to the program at a
  STI; HLT and an ARPL, the call made again when the ARPL traps (its registers kept): the VM idles in V86 as
  DOS's own console wait does today, and posted work, apps' calls and the DOS server run meanwhile.
- **The disks:** the boot volume and the others through `fsv.c` over GLOS's IDE driver, synchronous PIO in the
  trap as the cache's own INT 13h is (BIOS-only disks, SCSI and USB, later: N4); drive letters as DOS sees them
  (the owner's choice), the boot drive C:.
- **INT 2Fh's DOS functions** are answered where DOS's handler answered them (1000h SHARE there, 11xxh no
  redirector, 1600h not Windows, 4A10h no SMARTDRV, 150Bh no MSCDEX before N4's CD-ROM); `int2f_hook`'s chain
  ends in an IRET.
- **The loader** (`\GLOSLDR.SYS`, the owner's name): today's loader built a second way, as a raw binary with no
  DOS runtime (Open Watcom; GLOS's boot sector loads it whole at 0060:0000 with BL the disk, as it loads
  KERNEL.SYS): files by `fat.c` built 16-bit over INT 13h, memory by E820h (raw mode, there already), the
  console on the BIOS and COM1; its first 256 bytes a PSP's, so that the kernel adopts it as the first program
  and its stub goes resident as GLOS.EXE's does. Bootinfo says `BI_F_NATIVE`; the DOS pointers (InDOS, SDA, List
  of Lists) are GLOS's DOS's, filled by the kernel.
- **The way out** is a restart or power off (`glos exit` has no DOS to return to).

**The steps:**
- **NB1, the start:** `make native-image` (mtools on the host: GLOS's MBR, a FAT volume with GLOS's boot sector
  naming `GLOSLDR SYS`, `\GLOSLDR.SYS`, `\GLOS\` with GLOSK.BIN and GLOS.CFG); GLOSLDR.SYS reads the kernel by
  INT 13h and enters it; the kernel says it started native. Loop A (`--hdd`): its GLOS-BOOT and GLOS-RING0 lines. **Built
  2026-11-22:** `tools/ndos/native-image.sh` (in the dev container: GLOS's MBR by `tools/ndos/mkmbr.c`, a volume
  of GLOS's own formatter by `tools/ndos/mkvol.c`, its boot code naming GLOSLDR.SYS); GLOSLDR.SYS (`make
  native-loader`) = `loader/nprelude.S` (512 bytes: says `GLOS-BOOT step=prelude`, applies the .EXE's
  relocations in place, makes its PSP in the 256 bytes before its image, enters it with DL the disk) + `loader/main.c`
  built `-dNATIVE` with `loader/nstart.asm` (its entry in place of the C library's: SS made DGROUP, the BSS cleared)
  and `loader/nlib.c` (`fopen`, `fgets`, `fread`, `fseek`, `access` over `kernel/fs/fat.c` built 16-bit on the
  BIOS's INT 13h, LBA or CHS, the active FAT partition; `_dos_allocmem` up from the loader's end; no environment).
  The C library's DOS start-up is linked, dead (sscanf's multibyte tables ask for it), and never runs. Bootinfo:
  `BI_F_NATIVE` (1000h). NB1 is a round trip: the kernel says `GLOS-NATIVE step=kernel`, measures its clock and
  returns; the loader says `GLOS-EXIT code=0`. `loopa-nboot` (the bf6, the 486) passes; loopa-m1 (GLOS.EXE from DOS)
  unchanged.
- **NB2, the system VM's DOS:** with `BI_F_NATIVE`, GLOS's DOS over the system VM's memory (the loader adopted,
  its PSP the master environment's), INT 20h and 21h trapped to it, C: the boot volume; the stub goes resident,
  the agent listens. Loop A: SSH's `ver`, `dir C:\`, a file written and read back through the shell (the DOS server
  through GLOS's DOS). **Built 2026-11-22:** NB2a, GLOS's DOS with a layout (`struct dos_layout`) and DOS's SDA
  holding the current PSP and DTA (read from it at every call, so app.c's `adopt` and the kernel's
  `vm_current_psp` work as over a DOS); NB2b, `kernel/vm/vdos.c`: `dos_m_init_vm` adopts the loader (its PSP from
  bootinfo's `int2f_entry`, its memory to PSP:2) as the running program and its own parent (`root_runs`: its
  children's ends come back to it), DOS's tables in a block of DOS's own (owner 0008h, "SD") at the top of
  conventional memory, bootinfo's InDOS, SDA and List of Lists its; INT 20h's and 21h's vectors at ARPLs there
  (`vdos_arpl`: a far call through the vector, as rm_call and the DPMI host's reflection make); `soft_int` gives
  INT 20h and 21h to it after the agent's hooks (`vdos_int`); a call waiting for a key goes back to its INT, the VM
  idling meanwhile; C: mounted at the first call (`fsv.c` over GLOS's IDE driver, the active FAT partition of the
  ATA disk of BIOS 80h's size). The native loader goes resident as GLOS.EXE's agent does, with a master
  environment of PATH and COMSPEC (GLOS's own COMMAND.COM in `\GLOS`); GLOS.CFG's `[shell]` is read (its `options`
  carry flags: `/ROUNDTRIP` gives NB1's round trip). `loopa-nboot`'s NB2 cases (bf6, 486): SSH up, VER, `dir
  C:\GLOS`, a file written and read back on C:, GLOS's COMMAND.COM run as a job; GLOS.CFG read by the kernel
  through the DOS server; nothing unanswered. The C: volume's I/O goes past the disk cache; its writes tell the
  cache which sectors to forget (`cache_forget`), so a program's INT 13h through the cache never reads them stale
  (its reads are only not cached: the cache under `fsv.c` itself is later work). Open then: the console (NB3).
- **NB3, waits and the console:** the HLT-and-again wait; lcon and the shell's terminal typing into GLOS's DOS;
  INT 2Fh answered; Ctrl-C into sessions' programs. **Built 2026-11-22:** keys come as they come to DOS: the VM's
  keyboard (the PC's, lcon's and the shell's typing, `glos type` and `glos key`) into the BIOS's INT 09h and its
  buffer, from which GLOS's DOS takes one key at a time when its ring is empty and the call wants input (`fill`),
  only Ctrl-C for any other call, so that a program reading a line through DOS and then a key through the BIOS's
  INT 16h finds that key there. A line being read (0Ah, cooked 3Fh) whose call goes back to its INT to wait keeps
  what it has (`rl_n`, `cooked`). CON's output on the VM's text screen as the BIOS's teletype writes it
  (`vcon_tty`); INT 29h, and DOS's own INT 23h, at ARPLs; Ctrl-C into a program's own INT 23h and the call made
  again from its INT (or its end); INT 2Fh's chain ends at an IRET of DOS's. `loopa-nboot`: TYPEIN's line typed
  by `glos type`; CONTEST (a line by 0Ah, a key by the BIOS's INT 16h, 08h, Ctrl-C by `glos key` into its own
  INT 23h, ticks by INT 1Ch and the BIOS's count), its words on the screen as well.
- **NB4, programs:** agent jobs and sessions EXEC'd by GLOS's DOS (NDOSTEST, DOSDIFF against MS-DOS 7.1 again),
  GLOS's COMMAND.COM, apps (the DPMI host's reflection reaching GLOS's DOS), the census's programs; a `native`
  variant of the suites. **First part built 2026-11-22**, in `loopa-nboot` (bf6, 486) on a disk with no DOS:
  NDOSTEST as an agent job (Open Watcom's C library, spawning itself) 0 failures; DOSDIFF's ~90 probes
  answered in the system VM as MS-DOS 7.1 answers them (`tests/dos/dosdiff.msdos71.txt`); DOSCALLS (DJGPP: its
  INT 21h through the DPMI host's reflection, an ARPL of DOS's) as a program, and as a GLOS app (its calls by
  app.c's `serve`, the SDA's PSP switched for it). Unanswered there: 71A0h (as 7.1 refuses it with no Windows) and
  7302h. (A shell's `cd` is its session's: Loop A's hosted runs start in `\TEST` because RUN.BAT changes to it,
  which a native boot's don't.) Open: the census's programs and the suites in a native variant.
- **NB5, the way out and the benches:** restart and power off; the 486 and bench-g400 booting natively from a
  partition of their own (with the owner's OK for each bench).

### The BIOS's disks, and a USB stick (2026-11-23)

The owner asked for a bootable USB stick for the next bench PCs. GLOS's boot chain (its MBR, its boot sector,
GLOSLDR.SYS and its FAT code) reads by INT 13h alone, so it starts from whatever the BIOS can start from; what was
missing came after the kernel's entry, where C: was GLOS's IDE driver's or nothing.

- **Which disk.** The loader gives the kernel the BIOS's number of the disk it came from (`bootinfo.boot_dl`,
  version 12) and, where the BIOS says, how it reaches that disk (`boot_if`: INT 13h 48h's interface type, EDD
  3.0: ATA, USB, other; most BIOSes of these PCs' years don't say). At the first DOS call the kernel reads that
  disk's first sector through the BIOS and looks for it among the ATA disks its own driver found: the same 512
  bytes, and the disk's size (the size 48h gave, or up to a cylinder more; at least 08h's geometry's size on a
  BIOS without 48h). Exactly one such disk: C: is GLOS's IDE driver's, through the disk cache, as before. None,
  or two alike: **C: is read and written by the BIOS's calls** (`kernel/vm/vdos.c`, `bios_rw`). `/BIOSDISK` on
  the loader's command line makes it so for an IDE disk too (the tests' way: 86Box has no USB disk to start from).
- **The calls.** Nested real-mode INT 13h from the DOS call's trap, on the VM's thread (`rm_call`, as the disk
  cache's own BIOS calls and `DISK`'s are made), 42h and 43h where the BIOS has them, else 02h and 03h a track's
  rest at a time, three tries with a reset between. Their stack (2 KB) and their buffer (16 KB, 32 sectors, never
  across a 64 KB boundary) are a block of DOS's memory under its tables at the top of conventional memory,
  given back to DOS when C: turns out to be the IDE driver's. The disk cache sees the calls like a program's
  INT 13h and answers what it holds. InDOS is set for the length of every DOS call, so nothing of the kernel's
  enters GLOS's DOS a second time while the BIOS waits for a disk (the apps' calls are made only with it clear).
- **USB is left to the BIOS** when C: is the BIOS's disk and the BIOS didn't say the disk is something other than
  a USB one (`vdos_usb_bios`): taking a USB controller clears its legacy support, and the BIOS's calls for a stick
  on it would stop there. `usb_late` says `GLOS-USB left to the BIOS: drive C: is its disk` and takes nothing
  (the EHCI hand-off of phase H goes behind the same test); the Devices page says why. GLOS's own USB keyboards,
  mice, pads and hubs are off on such a start; a USB keyboard works as far as the BIOS makes a PS/2 one of it.
  GLOS's own USB storage driver (N4) ends this.
- **The stick.** `make usb-stick` (`tools/ndos/stick.py`): `build/usb/glos-usb.img`, a disk of 255 heads by 63
  (as a BIOS shows a stick) with GLOS's MBR, a disk signature of its own, one FAT volume, GLOSLDR.SYS and the whole
  system in `\GLOS` as `install.py` puts it on a hard disk, a host key of its own and the owner's AUTHKEYS;
  written with `dd`. The PC's BIOS must be able to start from a USB disk (the P4 bench can; the 440BX PCs and the
  486 can't).
- **Tested:** `loopa-nboot` has the NB2-NB4 case a second time with `/BIOSDISK` (bf6, 486): every file, program
  and console check through the BIOS's calls, USB left alone on the bf6. **Not yet proved on a real PC:** a BIOS
  whose USB disk code doesn't run in V86 mode (it would have to work under EMM386 too), a BIOS that reaches the
  stick through a controller whose interrupt line a driver of GLOS's shares, and the stick as 80h with the PC's
  own disk moved to 81h (GLOS's IDE driver doesn't mount the second disk yet: stage 3's drives).

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
3. **The MS-DOS 7.1 image** (N7): answered 2026-11-22: bench-g400's own Windows 98 SE, from the backup taken
   before it became a GLOS bench (/srv/bulk/backups/ibm300pl-20261026: its FAT32 partition, whole); its
   MS-DOS 7.1 files go to `MSDOS_DIR` (private, never in the repository).

Answered 2026-11-20: N1 beside phase F; no redirect of the root's files; the names as proposed; GLOS's own IDE
driver from the start; the active flag flipped for DOS on another partition; letters as DOS sees them; hosted
mode's end (above); CD-ROM, EMS and USB mass storage before it; the owner's MS-DOS 7.1 image for Loop A.
