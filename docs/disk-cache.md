# GLOS disk cache: a plan

**Status: plan v2 (2026-10-29), checked against the code and by probes, with the owner's answers to its four
questions ([Decisions](#decisions)). Nothing is built yet.**

PRD §2.3 lists "GLOS's own 32-bit FAT driver and disk cache" after v1. This plan takes the cache alone, now, and
leaves the FAT driver where it is. PRD sections: §4.5 (storage), §6.2 (memory), §6.8 (the DOS server), §7.2
(commands), §10.7 (configuration), §13 (performance, P7); decisions D20, D28, D41, D57; supervisor.md §4, §9,
§10, §17.2.

## Why

The owner asked whether GLOS needs a cache like SMARTDRV. Measured on bench-g400 on 2026-10-29 (Pentium II,
MS-DOS 7.1 without BUFFERS, FAT32; scratch tool DISKRD):

| | Plain DOS | GLOS | DOS + SMARTDRV | GLOS on SMARTDRV |
|---|---|---|---|---|
| 32 KB read, first time | 6.6 ms | 6.7 ms | 7.2 ms | 7.7 ms |
| 32 KB read, again | 6.6 ms | 6.7 ms | 0.45–0.57 ms | 0.74–0.84 ms |
| 512-byte read call | 0.41 ms | 0.46 ms | 0.06–0.08 ms | 0.06–0.09 ms |

DOSBench's game BATs under GLOS, wall time: Q2D1 106 s → 38 s and HLD1 431 s → 87 s with SMARTDRV (32 MB, read
cache only, `/X`). The second HLD1 took 85 s, so the gain wasn't left over from an earlier run. SMARTDRV counted
about 630,000 hits against 15,000 misses over the three runs. DOS re-reads the same sectors all the time, most
likely the FAT, which it walks from a file's start on a backward seek in a big pak file. Frame rates didn't move,
apart from Half-Life's first frame (5.1 s → 0.5 s).

So a cache is worth having. GLOS itself isn't slow: it adds about 2% to large reads and about 50 µs to a DOS call.
SMARTDRV can't ship with GLOS (it is Microsoft's). Under GLOS it also costs more per hit than under DOS, because
its copies out of extended memory go through GLOS's XMS.

## What it does, in one paragraph

GLOS traps the BIOS disk interrupt for the PC's hard disks. A read of sectors it holds is copied in ring 0 straight
into the caller's buffer, and the BIOS never runs. A read of sectors it doesn't hold goes to the BIOS as before;
GLOS then keeps a copy and returns. A write goes to the disk first, then GLOS updates its copy. Nothing is ever
written late, so a reset or `glos reboot` can't lose data. The cache lives in memory nobody is using and gives it
back the moment anything else needs it. Programs still see all the memory they saw before.

## What this plan rests on (checked 2026-10-29)

1. **DOS's own reads pass through the INT 13h vector.** I13PROBE (scratch tool) hooks INT 13h in the IVT, counts
   calls by function, and reads a file through DOS:
   - FreeDOS 1.4 in 86Box, plain and under GLOS: 56 calls, all AH=02h (CHS), at most 32 sectors each.
   - MS-DOS 7.1 (bench-g400's own disk, booted in 86Box) under GLOS: all AH=42h (LBA), at most 32 sectors; 151
     calls for a 2.3 MB file and 69 for 1 MB.

   So trapping the `INT 13h` instruction sees what DOS reads; nothing needs to sit in DOS's own path (INT 2Fh
   AH=13h).
2. **GLOS already re-issues a trapped INT itself.** `vbe_set` (kernel/vm/v86.c) re-issues a mode set through
   `rm_call` on the program's own stack, copies the registers back and steps past the INT. A miss and a write
   follow that pattern.
3. **A kill or another thread's posted work can start at any return to V86 code,** nested calls included
   (`vm_return`: only posted work's own call is protected). A cache inside the BIOS must hold both off, or a
   kill could cut a disk write in two, and posted work could run a DOS call inside the BIOS.
4. **supervisor.md §17.2 and PRD §6.8 say GLOS keeps an INT 13h busy flag; it doesn't.** INT 13h isn't trapped
   today (`trapped_ints` has 10h, 15h, 1Ch, 21h, 23h, 24h). The cache provides the flag while it is on.
5. **`rm_call` can't tell a finished call from one `rm_nest_end` ended:** it returns 0 for both. The cache must
   know (point 3's forced case).
6. **The kernel has no map of all physical memory:** `kmap` gives one-page windows (1,024 of them), and the heap
   stops at 4 MB. The cache's frames are reached through `kmap`, and its tables need their own region.
7. **Free frames are handed out next-fit; XMS takes contiguous runs** (`pmm_alloc_run`). Eight places read
   `pmm_free_frames()` to decide what can be had: kernel/vm/xms.c:107, kernel/dpmi/mem.c:88, 114, 150 (which
   refuses a lazily backed page when free frames run low: with a full cache, that would be a page fault in the
   program) and 337, kernel/dos/shot.c:144, kernel/app/stat.c:37 and kernel/ssh/sshd.c (`glos stat`, `glos mem`).
8. **Direct mode still reaches the cache:** V86 code runs at IOPL 3 there, and `INT 13h` meets a DPL-0 gate,
   which faults to `vm_gp` and `soft_int` as INT 10h does. Port I/O from protected-mode code isn't seen there.
9. **bootinfo is version 4** (include/glos/bootinfo.h).
10. **Loop A's C: is 63×16×1000 CHS (492 MB) on IDE 0:0,** read by FreeDOS with CHS calls; a game disk goes on
    0:1. The bench is read by LBA (point 1). Both kinds get tested.
11. **GLOS takes 35 KB of conventional memory on the bench, not P7's 4 KB.** MEM /C in agent mode shows GLOS at
    36,160 bytes: the stub (2,880) and the DOS server's 32 KB transfer buffer. That buffer was meant to exist only
    while an SFTP session holds the server, but since M9 the settings writer holds it for good (kernel/core/config.c:
    "the transfer buffer stays while GLOS runs: a small price"). P7's test measures under `/RUN`, where the stub is
    never idle long enough to allocate it, so it never saw this. Step R fixes it before anything else.

## Design

### Trap points

- **V86 code:** 13h joins `trapped_ints`, so an `INT 13h` from DOS or a program traps to `soft_int`.
- **Protected-mode code:** `rm_call` with vector 13h goes through the cache, both `RM_INT` (DPMI 0300h,
  `__dpmi_int`) and `RM_IRET` (a client's own INT 13h reflected by `rm_reflect`).
- **Every trapped INT 13h is made through `rm_call`, whatever the drive and function,** in `vbe_set`'s pattern, on
  the caller's SS:SP (the same stack depth the INT itself would use). GLOS therefore always knows when the BIOS is
  busy.
- **Not seen by the trap:** far calls straight into the BIOS's INT 13h code, and a program programming the IDE
  controller itself. The port watch covers both.
- **With the cache off** (`enabled = off`), INT 13h isn't trapped at all: exactly today's behaviour.

### The busy flag

`vm.int13` counts trapped INT 13h calls in progress. While it is non-zero, `vm_work_due()` is false and
`vm_return` makes no unforced kill, as it already does for posted work's own call. This is supervisor.md §17.2's
rule, made real. `rm_call` gains a result for an ended call (nest_enter's value 2). For a read, nothing from that
call is kept; for a write, its range is dropped. Existing callers keep their behaviour.

### Which drives

GLOS.EXE asks the BIOS about each hard disk before it goes resident, for drives 80h up to the BDA's count
(0040:0075), at most four:
- AH=08h for the geometry;
- AH=41h for the extensions;
- AH=48h for the sector count, the sector size and the removable flag.

The answers go in bootinfo version 5 (`disks[4]`), with a flag if SMARTDRV answered and the block buffer's
address. A drive is cached when it has 512-byte sectors, isn't removable, and the machine's disk controllers can
be watched: no PCI mass-storage controller other than IDE (class 01h, subclass 01h). The IDE ports are the legacy ones unless the controller's
programming interface says native mode; then they come from BAR0 and BAR2. Otherwise the cache is off, with
`GLOS-CACHE off reason=controller`. Floppies and other drives still go through `rm_call` (for the busy flag) but
are never cached.

### Requests

A request is decoded exactly or not at all. Anything the cache can't decode goes to the BIOS unchanged; if it
might write, the drive's blocks are dropped first.

| INT 13h | What the cache does |
|---|---|
| 02h read (CHS), 42h read (LBA) | Hits copied; misses read by the BIOS, then kept |
| 03h write (CHS), 43h write (LBA, any verify flag) | Written by the BIOS; on success the sectors are kept from the caller's buffer (write-allocate) |
| 05h, 07h format; 0Fh write sector buffer | The drive's blocks dropped, then passed on |
| 0Bh write long | The range dropped, then passed on |
| 42h/43h with a 64-bit flat buffer (DAP size 18h, buffer FFFF:FFFF) | Passed on; a write drops its range |
| 00h, 0Dh resets; 04h, 44h verify; 0Ch, 47h seek; 08h, 15h, 41h, 48h; the rest | Passed on unchanged |

A decodable read or write has:
- a count of 1–127;
- a DAP of size 10h or 18h;
- CHS within the drive's geometry;
- LBA plus count within the drive's sectors.

CHS becomes LBA through the drive's own AH=08h geometry, so a sector read either way is one entry; DSKTORT checks
that.

### Hits, misses and writes

- **Hit** (every sector asked for is cached): the sectors are copied into the caller's buffer through the VM's view
  of memory. That is `seg*16+off`, wrapped when A20 is off as the BIOS would see it, since DOS may keep buffers in
  the HMA. The cache sets AH=0, CF clear, AL (CHS) to the count, and the BDA's last-status byte (0040:0074) to 0;
  the DAP is left as it was.
- **A small read with a miss** (fewer than 8 sectors): each aligned 4 KB block it touches that isn't wholly cached
  is read whole, one BIOS call per block, into the block buffer (below), clipped at the drive's end. The block is
  kept, and the requested sectors are copied out. DOS reads the FAT a sector at a time, so a cold walk of a chain
  takes an eighth of the calls. If a block read fails, nothing from it is kept, and the caller's own request goes
  to the BIOS unchanged, into its own buffer. Its result is the answer, exactly as without the cache.
- **A big read with a miss** (8 sectors or more): the missing runs are read one BIOS call each, straight into their
  place in the caller's buffer, and the cached sectors between them are copied in. For AH=42h, the cache rewrites
  the caller's DAP (count, buffer, LBA) for each run and puts it back afterwards. For AH=02h it uses registers, with
  ES:BX normalised and CHS from the run's LBA. The sectors read are kept, in blocks that may be partly valid.
  Partitions start at sector 63, so DOS's cluster reads never line up with 4 KB blocks; filling their ends as well
  would triple the calls.
- **An error in a big read's run:** the cache stops and returns AH and CF from that call. The count done (the DAP's
  count, or AL) is the sectors before the run plus what the BIOS did of it. Nothing from that run is kept.
- **Write:** the caller's registers go to the BIOS unchanged. On success, the sectors are copied in from the
  caller's buffer (write-allocate). On failure, or if the call was ended, the range is dropped.

### The block buffer

- **4 KB of conventional memory** in GLOS.EXE's resident block, beside the stub, plus a 16-byte DAP in the stub.
  It is kept only when `[cache] enabled` isn't `off` as GLOS starts; GLOS.EXE reads that through the schema, as it
  reads its other settings. With the cache on, the resident part is at most 8 KB (P7, amended by D72).
- **Placed so it doesn't cross a 64 KB boundary:** before the stub's code or after it, whichever fits. In the rare
  case neither does, it crosses. If a BIOS ever answers a block read with AH=09h (DMA boundary), that drive's small
  reads go straight to the caller's buffer from then on, with no block fills.
- **Its address goes in bootinfo v5.** A read into it goes through `rm_call` like any other, with AH=42h and the
  stub's DAP, or AH=02h with registers when the drive has no extensions.
- **No buffer, no block fills:** when the cache is turned on after GLOS started, small reads also go straight to the
  caller's buffer.

### The store

- **Blocks** of 4 KB, one frame each, keyed by (drive 0–3, LBA ÷ 8), with a valid bit per sector.
- **Descriptors** of 16 bytes (key, frame, hash link, valid bits, segment, referenced bit, pin count) live in a new
  kernel region, `CACHE_BASE` 0xC6800000 (after `DEV_BASE`'s 40 MB). It has up to 16 MB of addresses, its pages
  mapped as the cache grows. The hash table is sized at start to a power of two of at least a quarter of all
  frames. On the bench, 448 MB gives about 1.8 MB of descriptors at most.
- **Segmented LRU,** two clocks:
  - a new block starts in *probation*, and a hit there promotes it to *protected*;
  - protected holds at most ¾ of the blocks, and its oldest goes back to probation;
  - eviction takes probation's oldest first.

  Blocks read twice (the FAT) survive a long streaming read, which plain LRU, like SMARTDRV's, lets push them out.
- **Copies** `kmap` the block's frame, copy, and `kunmap` it, with the block pinned. The tables change only with
  interrupts off.

### Memory

- **The cache's frames** come from a new `pmm_take_high()`, highest free frame first, and it never takes one when
  that would leave fewer than the reserve (the 256 frames dpmi/mem.c keeps back). Its own allocations never
  reclaim. `size = auto` lets it grow into all memory above the reserve; `size = N` caps it at N MB.
- **`mm_avail_frames()`** is free frames plus the cache's unpinned ones. It replaces `pmm_free_frames()` in the
  decisions of point 7: xms.c `free_frames()`, dpmi/mem.c 88, 114, 150 and 337, and shot.c.
- **Single frames:** when `pmm_alloc()` finds none free, it calls `cache_reclaim(1)` (probation's oldest, then
  protected's) and tries again. This is safe with interrupts off, which is how `pmm_alloc` runs.
- **Runs** (`pmm_alloc_run`, `dma_alloc`, XMS): when no run is free, `cache_reclaim_run(n)` finds the lowest
  window of n frames made only of free and unpinned cache frames, evicts the blocks in it, and the allocation is
  tried again. `pmm_largest()`, which is XMS's largest block, counts unpinned cache frames as free.
- **Reporting:** `glos stat`'s `mem.free_kb` and the API's `mem_free_kb` (the System Monitor) mean *available*,
  free plus cache; `glos stat` adds `mem.cache_kb`, and `glos mem` adds a line `cache kb=.. blocks=..
  protected=..`. The existing tests' memory checks keep their meaning (what can be had), and the API struct
  doesn't change.

### The port watch

- **Which ports:** each watched channel's command register (1F7h, 177h; native mode: BAR+7). It is trapped in the
  IOPB except while the cache's own `rm_call` is in the BIOS, so the BIOS's status polling costs nothing.
- **What is caught:** word and dword accesses that cover the register trap too (vdev splits them into bytes).
  Reads pass through. A written command not on the read-and-control list below drops every cached drive's blocks,
  with `GLOS-CACHE drop reason=port port=1f7 cmd=30`.
- **The read-and-control list:**
  - reads: 20h, 21h, 24h, 25h, 29h, C4h, C8h, C9h;
  - verify: 40h, 41h, 42h;
  - seek and recalibrate: 10h–1Fh, 70h;
  - diagnostics and set parameters: 90h, 91h;
  - set multiple mode: C6h;
  - power: E0h–E5h;
  - flush cache: E7h, EAh;
  - identify: ECh, A1h;
  - set features: EFh;
  - read native max: F8h, 27h;
  - packet and device reset (ATAPI only, never a hard disk's): A0h, 08h.
- **Not seen:** protected-mode code in a direct-mode session (IOPL 3). The cache is bypassed for that session
  (calls pass through), and every block is dropped when it ends (`reason=direct`).

### Another cache

If SMARTDRV answers INT 2Fh AX=4A10h BX=0 with AX=BABEh when GLOS.EXE starts, GLOS's cache stays off
(`reason=smartdrv`). A cache loaded later, under GLOS, sits below GLOS's trap: GLOS's writes go through it, so the
two stay coherent.

### Interface

- **GLOS.CFG `[cache]`,** a new section in the schema (sdk/config/cfg.c), so the Control Panel gets a tab for it.
  The Control Panel's goldens (D57) are made again.
  - `enabled = on|off|verify|count`, default `on` (D72; `off` until C4 passes). `verify` reads every hit from the
    disk as well and compares; `count` caches nothing and counts. It changes at once, applied when no INT 13h is in
    progress; the block buffer comes only with a start.
  - `size = auto|N` (MB, 1–2048), default `auto`, shrinking at once.
- **Commands** (kernel/ssh/sshd.c):
  - `glos cache`: one JSON line, in the form of `glos stat`: `{"mode":"on","kb":..,"blocks":..,"protected":..,
    "hits":..,"misses":..,"hit_sectors":..,"miss_sectors":..,"write_sectors":..,"drops":..,"reclaims":..,
    "mismatches":..,"drives":[{"dl":128,"sectors":..,"lba":1}]}`;
  - `glos cache drop`, for cold timings without a reboot;
  - `glos cache lbas`: in count mode, `dl lba reads` lines, most-read first, up to 100,000.
- **Log grammar:**
  - `GLOS-CACHE start mode= drives=80:lba:SECTORS kb_max= ports=1f7,177`;
  - `GLOS-CACHE off reason=config|smartdrv|controller`;
  - `GLOS-CACHE drop reason=port|direct|format|write-error|ended|command blocks=`;
  - `GLOS-CACHE mismatch dl= lba=`;
  - `GLOS-CACHE end` with the totals, at exit.

### Not in this plan

- Delayed writes.
- Read-ahead beyond a block. C4 reports cold loads; anything more is a later plan.
- A file layer of GLOS's own, or GLOS's own IDE driver (D70's "later").
- Caching floppies, CDs, SCSI or USB disks.
- Keeping the cache across boots.
- The INT 13h busy flag while the cache is off: today's gap stays as it is then.

## The steps

Each step ends with its tests passing. Development in `~/wt-glos-m5` (branch `cache`), long runs in
`~/wt-glos-test`. Nothing reaches the bench before C3 is green, except C0's counting.

- **R. P7 back** (the owner's answer, Q5), before the cache:
  - the settings writer holds the DOS server only while it writes, so the 32 KB transfer buffer exists only during
    a write or an SFTP session;
  - the P7 test also measures in agent mode, with MEM /C as an SSH job after the settings writer has run: the
    largest program size within 4 KB of plain DOS from the same boot;
  - GLOS's regression, then GLOS main.
- **H. The harness, in MGA-Glide first** (the owner's yes, Q3), with MGA-Glide's full regression, then merged:
  - branch bench-hdd (`run.py --hdd`: bench-g400's own disk booted in 86Box), brought up to main;
  - `run.py --scratch-hdd MB`: a blank sparse disk on IDE 1:0 that DOS leaves alone, for DSKTORT.
- **C0. Measure, caching nothing.**
  - The trap points, `rm_call`'s ended result, the busy flag, bootinfo v5 from the loader (the block buffer
    included), and `enabled = count`.
  - The port watch logs commands that arrive outside the cache's calls but drops nothing.
  - Runs: Loop A (FreeDOS); bench-g400's MS-DOS 7.1 copy in 86Box;
    bench-g400 itself, each with DISKRD, Q2D1 and HLD1. (bench-hdd: step H.) On the bench, the counting build goes in C:\GLOS with the
    release build kept beside it to put back.
  - *Exit:* the counts give a perfect cache's hit rate on the games, and the port log is empty on the bench (or
    each entry is explained).
- **C1. Store, hits, misses, writes, verify.**
  - T (host, tests/host/cache_test.c): blocks, valid bits, runs split around hits, segmented LRU, pins, decoding of
    DAPs and CHS, error counts. tests/host/pmm_test.c gets `pmm_take_high` and the run window.
  - T (Loop A): DISKRD's second reads are hits by `glos cache`'s counters (not 86Box's speed: D28).
- **C2. Memory and safety.**
  - `mm_avail_frames`, both reclaims and the reporting.
  - The port watch dropping; direct mode; SMARTDRV; the configuration, commands, Control Panel and goldens.
- **C3. The Loop A gate.**
  - **DSKTORT** (tests/dos/dsktort.c, DJGPP), deterministic from a seed, 200,000 operations:
    - raw reads and writes on the blank scratch disk (step H), by AH=02h/03h and
      42h/43h from V86 and by 0300h from protected mode, every read checked against a model;
    - programmed-I/O writes by the test itself to that disk, which must drop the blocks (the port watch);
    - DOS file writes and reads on C: with checksums;
    - raw LBA reads of a C: file's sectors (found by reading the FAT16 itself) compared with DOS's CHS reads of
      the same file.

    Zero mismatches.
  - **Memory:** with the cache full, a DJGPP program allocates all that 0500h offers and touches every page, and
    XMSTEST takes the largest XMS block; both succeed and the cache shrinks.
  - **Errors:** reads past a disk's end return the BIOS's own error, the same with the cache on and off.
  - A direct-mode session leaves the cache dropped.
  - `glos exit`: VECCHK clean.
  - The whole GLOS regression with the cache on and with it off, on bf6 and 486dx2, raw and HIMEMX boots.
  - The MS-DOS 7.1 copy: DISKRD and the game set in verify mode.
  - P7 with the cache on and off, under `/RUN` and in agent mode: MEM's largest program size within 8 KB and 4 KB
    of plain DOS.
- **C4. The bench.**
  - First: confirm the 2026-10-26 backup is there, and record `fsck.vfat -n` from Ubuntu. Its baseline is the 2
    lost clusters and the free count left by the hang of 2026-10-28.
  - Verify mode over DISKRD and the DOSBench game set, twice: zero mismatches, and `fsck.vfat -n` unchanged.
  - Then on, measured against 2026-10-29: DISKRD; Q2D1 and HLD1 cold (after `glos cache drop`) and warm; frame
    rates.
  - At the end, the schema's default becomes `on` (Q1).

Then the docs:
- supervisor.md: §4 (`CACHE_BASE`), §9 (INT 13h), §10 (the port watch), §17.2 (the flag, as built);
- PRD v0.9: D72, §2.3 (the cache comes out of "after v1"; the FAT driver stays), §4.5, §6.2, §7.2, §10.7, P7
  (8 KB with the cache on), P8;
- the release notes.

DOSBench needs no change: it measures frame rates, not loads. The bench's results note whether the cache was on.

## P8 (proposed)

On bench-g400:
- DISKRD's repeated 32 KB reads take 0.4 ms or less (SMARTDRV under GLOS: 0.74–0.84 ms);
- HLD1's second run is faster than with SMARTDRV (85 s);
- DOSBench's game frame rates are unchanged within run-to-run noise.

- cold HLD1 (after `glos cache drop`) is no slower than with SMARTDRV (87 s).

In 86Box, correctness only (D28).

## Risks

- **Corruption** from a path nobody foresaw. Answered by the port watch, verify mode first on the bench, `fsck`
  before and after, and the backup.
- **A BIOS that writes past what it was asked, or reports counts its own way.** DSKTORT compares registers and DAPs
  with the cache on and off.
- **Memory pressure:** a driver wanting a large run late. Answered by the run window, and if need be by dropping
  everything.
- **Every INT 13h now nests through `rm_call`:** microseconds against milliseconds of disk. C0 measures it.

## Size

- Kernel, about 1,250 lines of C: the store and request path about 750; trap points, busy flag and `rm_call` about
  150; the port watch about 120; memory about 150; configuration and commands about 80.
- Loader: about 100 lines (the disks, SMARTDRV, the block buffer's placement).
- Tests: about 700 lines.
- MGA-Glide harness: about 40 lines (the scratch disk).

H and C0–C4 are a few days' work at M10's pace.

## Decisions

The owner's answers (2026-10-29):
- **Q1: on by default** once C4 passes. `[cache] enabled = off` turns it off.
- **Q2: all spare memory, given back on demand** (`size = auto`), with a cap for whoever wants one.
- **Q3: yes to both harness changes:** bench-hdd merged, and the scratch disk (step H).
- **Q4: 4 KB more conventional memory is acceptable if it is the better solution, else extra BIOS calls.** It is
  the better one:
  - the cost of a read here is per BIOS call (the drive's own look-ahead makes the next sectors cheap), so filling
    a block through the caller's 512-byte buffer takes the same eight calls as not filling it, and gains nothing;
  - the buffer makes a cold FAT walk an eighth of the calls, as SMARTDRV's 8 KB elements do;
  - it works the same whether the BIOS uses programmed I/O or bus-master DMA, since it is real conventional
    memory.

  So block fills are in the design from C1, instead of a conditional C5. P7 becomes 8 KB with the cache on (D72).
- **Q5: the 32 KB transfer buffer held since M9 is fixed first** (step R).
- **Q6: upper memory gets a plan of its own,** docs/upper-memory.md, written next:
  - D41's opt-in stub in a UMA page GLOS maps itself (owed since M9), which leaves about 0.5 KB;
  - a study for D25's after-v1 item, UMBs for DOS programs (LOADHIGH and XMS 10h after DOS has booted, VDS for
    DMA into them).

  The cache's block buffer and the DOS server's buffer stay in conventional memory either way, since a BIOS that
  uses DMA (bus master or ISA) writes to the physical address, which a page GLOS remaps doesn't have.
