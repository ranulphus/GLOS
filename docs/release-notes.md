# GLOS release notes

What each milestone added. Everything here is built and tested in 86Box (Loop A, `make loopa-*`); the bench
(real PCs and Matrox cards) re-verifies before a release (PRD §19). Each milestone's plan, status and evidence is
in its `docs/milestones-*.md`.

## Themes in the apps, and three text sizes (toolkit 2, 2026-11-16)

- **The apps follow the desktop's theme** (`[desktop] theme = slate | dark | contrast`): windows' contents are
  drawn in the theme's colours, and change with it while they run. Until now only the window frames and the task
  bar did.
- **`[desktop] text = small | normal | large`** sets the size of the text in windows, menus and dialogs, at once.
  The small and large fonts are in a new file, `FONTS.DAT`, which goes beside the programs with `SMOOTH.DAT`
  (copy it when updating; without it the text stays at the normal size).
- The shell's `VER` says the API's version as it is (2.3; it said 2.1).

## The shell runs batch files itself (S4a, 2026-11-16)

- **GLOS's shell runs `.BAT` files**, at an SSH login, in the Terminal and at the PC's own prompt: `IF`, `GOTO`,
  `CALL`, `FOR`, `SHIFT`, `PAUSE`, `%0` to `%9`, `ECHO OFF`, as MS-DOS's COMMAND.COM has them. Ctrl+C asks
  `Terminate batch job (Y/N)?`.
- `VOL`, `TRUENAME`, `DATE`, `TIME`, `CHCP`, `BREAK`, `VERIFY` and `LH` are commands of the shell's own.
- **Pipes and redirection around programs are the shell's own** (S4b): `PROG < IN > OUT`, `>>`, `> NUL`,
  `A | B | C`, with DOS's temporary files in `TEMP`.
- **GLOS has its own COMMAND.COM** (S4c): a 1 KB program beside `GLOS.EXE` that programs are given as `COMSPEC`.
  A program that shells out (`COMMAND /C line`, the C library's `system()`, a "DOS shell" in its menu) gets
  GLOS's shell: the same commands and batch language as at GLOS's prompt, with the programs it starts as the
  calling program's children and its output where that program's goes. `[shell] command = dos` keeps the DOS's.
  Copy `COMMAND.COM` into GLOS's folder with the other files when updating (never over the DOS's own in `C:\`).
- **GLOS can be DOS's shell with no COMMAND.COM of the DOS's** (S4d): with `SHELL=C:\GLOS\GLOS.EXE /SHELL` in
  CONFIG.SYS and GLOS's own COMMAND.COM beside it, GLOS runs `AUTOEXEC.BAT` itself at the PC's screen, what the
  file `SET` is every program's environment from then on, and the prompt after it is GLOS's. The setup floppy
  and a disk made by `install.py --new-disk` start this way, with FreeDOS's kernel as the only file of
  FreeDOS's they need. `glos restart` restarts the PC there. A PC that has a DOS keeps its COMMAND.COM and
  starts GLOS from `AUTOEXEC.BAT` as before.

## The network for full-screen programs (API 2.3, 2026-11-15)

- **A full-screen program can use sockets** (TCP and UDP), where only an app in a window could: a game plays over
  the network at full-screen speed. Nothing changes in the program: `socket()` succeeds there now.
- `socket()` also works in a program that DOS-GL or SDL made an app (it answered "not supported").
- Such a program has no events, and a call that waits holds up the program's own interrupt handlers until it
  returns: poll, or use short waits. What it leaves open is closed when it ends.

## UDP for apps: datagram sockets (API 2.2, 2026-11-15)

- **`socket(AF_INET, SOCK_DGRAM, 0)`** in the SDK's BSD sockets, with `bind`, `getsockname`, `sendto`, `recvfrom`,
  `connect`, non-blocking calls, `select`, `SO_BROADCAST`, `SO_REUSEADDR` and `MSG_TRUNC`; under it the API's
  new NET calls DATAGRAM, SENDTO and RECVFROM. A datagram is 1472 bytes at most; 64 wait to be read.
- **Broadcasts reach across a cable link:** a program on a PC with a network card and a program on the PC
  cabled to it find each other by broadcast, either way round.
- Programs check `glos_version`: minor 2, or NET's function bits 7 to 9.
- **The network's state lists the cable link too** (NET's STATE): a PC that is on the network through its cable
  has an interface and an address to show, where it had none.

## The network over a parallel cable; a PC on a cable as a member of the network (2026-11-15)

- **A parallel (LapLink) cable is a second kind of cable link** (N2): `cable = client LPT1` in GLOS.CFG, or
  `NETWORK SERVER LPT1` / `NETWORK CLIENT LPT1`. It speaks PLIP, as Crynwr's DOS packet driver and Linux do,
  several times faster than a serial cable. The parallel port is GLOS's while the link is up.
- **A PC at the far end of a cable can be a member of the near PC's network** (N1; over the parallel cable
  too: N3): a PC
  with a network card and a server link passes the far PC's request for an address to the network's DHCP
  server and then its traffic both ways, so the far PC has an address of its own there and is reached by it.
  `[net] cable_share = off` on the near PC, or `cable_join = off` on the far one, keeps the cable to the two.
- **A PC with no network card is installed over a parallel cable** (N3): `make floppy FLOPPY_CABLE="client
  LPT1"`, the cable to a GLOS PC that has a card, `tools/floppy/install.py ADDRESS --cable "client LPT1"`.
- `[net] card = off`: GLOS looks for no network card (the PC is on the network by its cable link alone).
  `cable_share` and `cable_join` set in GLOS.CFG drew a warning from the loader at start: no more.
- With a cable link up on a PC that has no network card, `glos exit` left the PS/2 mouse's interrupt line
  unmasked for DOS. It leaves the interrupt controller as it found it now.

## Write-combining for the video card's framebuffer: a setting, and the VGA window's trial (2026-11-14)

- **`[display] write_combining = auto | on | off`** in GLOS.CFG (the owner's choice after the trial below; `auto` is
  the default). `auto` switches write-combining on as GLOS starts for the video cards it has been checked on with
  real hardware, whose registers are in an address range of their own: Matrox's G-series (measured on a G400)
  and NVIDIA's TNT and TNT2 (measured on a TNT2 M64). Other cards are left alone and the log says so
  (`GLOS-MTRR wc skip ... reason=unchecked-card`); `on` forces it for any card, and `MTRR LFB ON` tries it by hand.
  ATI's Rage family and S3's cards keep registers inside the framebuffer's range and are not `auto`'s until
  looked at.
- **A second trial, `MTRR VGA ON` / `MTRR VGA OFF`:** the window at A0000h that mode 13h and banked VESA modes are
  written through, made write-combining (the CPU's fixed range for it; FASTVID's other half). It is a command
  only, off at every start. On the Pentium III bench (TNT2): mode 13h frames at 13.8 MB/s without and 48.9 with;
  640x480 through the window a bank at a time at 13.6 and 47.6 (`LFBSPEED frames vga`, `LFBSPEED frames bank`).

## Write-combining for the video card's framebuffer: a trial (2026-11-14)

- **`MTRR LFB ON`** (`glos mtrr lfb on`) makes the video card's linear framebuffer write-combining, as FASTVID does
  from plain DOS (under GLOS such a tool cannot run: only the kernel may write the CPU's MTRRs). Programs
  started afterwards that map the framebuffer (a game's software renderer, through DPMI) write it several
  times faster. `MTRR LFB OFF` (or `MTRR OFF`) takes it away, and GLOS takes it away when it leaves. `MTRR` alone
  lists the CPU's ranges; `MTRR WC BASE MB` names a range by hand.
- Measured (2026-11-14). Whole 640x480 frames written to the framebuffer (`LFBSPEED.EXE`, tests/dos/lfbspeed.c):
  13.9 MB/s without and 54.5 MB/s with on the Pentium III bench (TNT2); 40.5 and 243.4 MB/s on bench-g400
  (Pentium II, G400). Quake's demo1 on bench-g400 (the DOS-GL session's runs, `-mem 32 -nojoy`), frames a second
  without / with:

  | | 320x200 | 640x480 |
  |---|---|---|
  | id's QUAKE.EXE 1.06 | 90.7 / 91.1 | 19.2 / 38.8 |
  | the port's software renderer (QDOS.EXE) | 106.9 / 106.9 | 23.2 / 43.75 |
  | DOS-GL on the G400 | | 42.9 / 43.2 |

  320x200 is mode 13h, through the window at A0000h, which the trial doesn't touch; the G400's own path
  neither gains nor breaks.
- It is a trial the owner asked for: off unless the command is given, nothing in GLOS.CFG yet. Pentium Pro, II
  and III class CPUs (MTRRs with write-combining); a 486 or a Pentium has none and the command says so. A range
  the BIOS set is never changed. The framebuffer is found by PCI (the display card's prefetchable memory, 32 MB
  of it at most; 16 MB on S3 cards, whose registers share the range).

## A prompt on the PC's own screen (2026-11-14)

- **With no desktop, the PC's screen and keyboard have GLOS's shell**: the same shell an SSH login and the
  Terminal app have, in a session of its own (step S2d of docs/v1/shell.md). Before, the screen said only that
  GLOS was running, and the keyboard did nothing but Ctrl-Alt-Del.
- **A DOS program started there has the screen and the keyboard itself, as under DOS**, and the prompt goes on
  below whatever it left on the screen. A program another session runs (an SSH command) writes on the screen as
  before; the prompt and what was being typed come back below it.
- **Start-up lines, then notices** (the owner's choice): how the PC is reached and anything GLOS refused as it
  started, then the prompt. Later only what the person at the PC should know is shown, a line above the prompt:
  the network up or gone, a crash report written. The log itself is `LOG`'s.
- `EXIT` there begins a new session; `SHUTDOWN` leaves GLOS; Ctrl-Alt-Del at the prompt with nothing running
  restarts the PC. The keyboard's layout is `[region] layout`'s.
- **`[shell] prompt = off`** in GLOS.CFG keeps the old line and leaves the keyboard to DOS.
- **`glos shot --text`**: the PC's text screen as text, a line a row and the cursor's place.
- A terminal is told that its program took the PC's screen even when the program ended at once (one run in three
  of Loop A's shell test missed it).

## British keyboards (2026-11-14)

- **`[region] layout = uk`** (the Control Panel's Region page, or `glos config set region layout uk`): the desktop
  and GLOS's apps type what a British keyboard's keys say: `"` on 2, `£` on 3, `@`, `#`, `~`, `\` and `|` where
  they are, `¬`; and with AltGr `€`, `¦` and `á é í ó ú`. The right Alt is AltGr on this layout and no longer an
  Alt. `us` stays the default.
- **`glos type` and the remote desktop follow the layout**: they press the keys that type each character on the
  keyboard set, so text typed from another computer comes out right in a DOS that has `KEYB UK` loaded.
  `glos type` takes UTF-8 (`glos type £5`).
- DOS programs are not touched: they read the keys themselves, by DOS's own keyboard setting.

## VIA Rhine network cards; PCI (2026-11-14)

- **GLOS drives VIA's Rhine network chip** (the VT86C100A of D-Link's DFE-530TX and its like), found and started
  with the other cards when GLOS starts. Tested on a real card alone, in the Pentium III bench: 86Box has none.
  The later Rhine II and III (VT6102, VT6105) are taken by the same driver, untried.
- **A network card that shares its interrupt line with an OHCI USB controller nobody uses is taken now** (GLOS
  switches that controller's interrupts off, as it did for UHCI ones). It was refused before (`reason=shared-irq`).
- **`PCI`** (`glos pci`) lists the PC's PCI devices as GLOS found them: address, maker and device numbers, class,
  interrupt line, I/O and memory ranges, and who keeps each (DOS, GLOS, or made quiet). `PCI 00:10.0` prints
  one device's configuration space.
- A PC started from Windows 98's "Restart in MS-DOS mode" can have its PCI cards left without addresses or
  interrupt lines: GLOS says so (`reason=no-io`) and leaves the card. Started straight from the boot menu's
  "Command prompt only", the BIOS's settings stand.

## No drive is C: by name (2026-11-14)

- GLOS no longer assumes it, or DOS, is on C:. **GLOS's own files follow the folder GLOS.EXE runs from:** crash
  reports go to `CRASH` beside it, `FETCH` finds `CERTS.DAT` there. **Where a user starts is the drive DOS
  started from:** the Terminal, file dialogs and the Files browser open at its root (`A:\` on a PC started from
  a floppy). **Packages** install into `\APPS` on that drive too, or on GLOS's own drive when DOS started from a
  floppy.
- Programs can ask where things are: `glos_home()` and `glos_boot_root()` (the system figures' `home` and
  `boot`, API 2.1).
- The setup floppy's installer still puts a new system on C:, the first hard disk's first partition.

## API 2.1: GLOS's shell as a job (2026-11-13)

- An app can open a session of GLOS's shell as it runs a program: `GLOS_JOB_RUN` with `how = GLOS_RUN_SHELL` and
  a terminal's size (`glos_job_shell()`), the terminal's bytes by READ and WRITE, its size by the new
  `GLOS_JOB_SIZE` (`glos_job_size()`). The first step of making the Terminal app a terminal of the kernel's
  shell (docs/v1/shell.md, S2c). `VER` says API 2.1; programs built for 2.0 run unchanged.
- A program can be run at a terminal as a GLOS app (`GLOS_INIT_TERMINAL`, `GLOS_SYS_TERMINAL`): the shell waits
  for it, and the terminal's bytes are its, each way, as they are. What the SSH client needs to be a program
  of its own.
- `SSH.EXE`, `SFTP.EXE` and `SSHKEY.EXE` in `C:\GLOS`: the SSH client, its file transfer and its key maker as
  programs, run from any shell: at an SSH login to the PC they hop on to another computer (`ssh pc`, then
  `ssh other`). `SSH-KEYGEN` in the Terminal is `SSHKEY` now.
- **The Terminal app is a terminal alone:** the shell in its window is the kernel's, the same one an SSH login
  has, with GLOS's own commands (`PS`, `LOG`, `DISK`, `NETWORK` ...) and everything else the shell gains from
  here on. Full-screen programs over SSH (`vi`, `top`) draw in the same screen as everything else, and what
  scrolls off is kept (a thousand lines) to scroll back to, choose and copy. Its look changed a little: the
  prompt is drawn as the rest of the text, the cursor is a block.
- GLOS's own commands (`PS`, `LOG`, `DISK` ...) no longer need a network card: the thread that runs them starts
  on every PC.

## The cable link: the network over a serial cable (2026-11-13)

- `[net] cable = client COM1` in GLOS.CFG (or `NETWORK CLIENT COM1` while GLOS runs) makes a serial port a
  network link to the computer at the other end of a null-modem cable: SLIP, 115,200 baud unless said, the
  server 192.168.250.1 and the client 192.168.250.2. SSH, SFTP, the installer and the apps' sockets work over
  it. A PC with no network card GLOS drives is reached this way: the network starts for the cable alone
  (docs/cable.md; `tools/cable.sh` is a Linux computer's end).
- Fixed: the SSH server lost output when TCP refused a write with room still in its buffer (its queue of
  segments full). Seen with the cable's small packets, where a download stopped after 20 KB.

## The DISK command; the setup floppy without FDISK, FORMAT and SYS (2026-11-13)

- `DISK` (`glos disk` over SSH) lists the BIOS's hard disks and their partitions, and with `/Y` lays a disk out
  anew (`DISK n NEW`), formats a partition as FAT12, FAT16 or FAT32 (`DISK n FORMAT`) and writes boot records
  that start FreeDOS (`DISK n BOOT`). Without `/Y` it says what would be lost and does nothing. The disk GLOS or
  DOS runs from is never written (docs/floppy.md).
- `DF [drive: ...]` lists drives with their size, what is used and free (a FAT32 volume's real figures, past
  DOS's old 2 GB); `DU [folder] [/S]` says what a folder's files take, with everything under it.
- `DD IF=source OF=destination [SKIP= SEEK= COUNT=] [/Y]` copies sectors between hard disks (`DISKn`), partitions
  (`DISKn:p`), floppies (`A:`), files and, as a destination, the connection: `ssh pc "glos dd if=disk1 of=-" >
  disk.img`. `DISK`'s rules for writing a disk hold.
- The setup floppy has no FDISK, FORMAT or SYS any more: `install.py --new-disk` uses `DISK`, and copies
  FreeDOS's kernel and COMMAND.COM itself. The 720 KB floppy has about 90 KB to spare.

## `ssh pc` logs in to GLOS's shell (2026-11-13)

- `ssh pc` with no command logs in: GLOS's own shell (DOS's commands and syntax, a history, completion with Tab),
  the same one as the Terminal app's, on a session in the kernel (docs/v1/shell.md, steps S2a and S2b).
  `ssh pc COMMAND` goes through that shell too. GLOS's own commands are the shell's as well (`PS`, `LOG`, `STAT`
  ...; `GLOS name` as before). A GLOS app started there goes on beside the shell and still writes to that
  terminal.
- A program started at a login writes to its terminal on a PC without a desktop too, whichever way DOS writes
  to the screen. (MS-DOS 7.1's console driver calls the video BIOS without an INT, and in a text mode writes
  most characters straight into the screen's memory; at a login on such a PC what a program wrote went to the
  PC's own screen instead. Seen on bench-g400. While such a program runs, the BIOS's mode byte says a graphics
  mode, as it does under the desktop, so the driver has only the BIOS to write with.) A program that takes the
  screen (a mode set, the VGA's ports) has the PC's own from there, and the terminal says so.

## The idle screen, and CR4 asked for only where CPUID names it (2026-11-12)

- A PC that runs GLOS with no desktop and nothing running showed nothing of GLOS's and ignored its keyboard: it
  looked as if it had stopped. Its screen now says that GLOS is running, the PC's name and address (once the
  network has one) and whether SSH answers; and Ctrl-Alt-Del there restarts the PC (`GLOS-REBOOT source=cad`).
  While a program or a job runs, Ctrl-Alt-Del is as before (`GLOS-RESET-REQ source=cad`). A temporary measure:
  GLOS's shell takes that screen and keyboard (docs/v1/shell.md, S2).
- CR4 is read only on a processor whose CPUID names a feature that lives there: a 486 that is not Intel's is
  no longer asked for a register it hasn't got. A precaution, not the fix of a fault seen: the 486 bench's one
  boot that reported CR4 with VME was probably with an IntelDX4 in the socket, which has both.

## The API is 2.0 alone (2026-11-12)

- **GLOS answers API 2.0 only** (`include/glos/api.h`, `docs/v1/api-2.md`; phase B of `docs/v1-route.md`, its
  step 15). API 1.x, the API of M6 to M10, is gone: its calls are refused (`GLOS_E_VERSION`).
- **GLOS's own programs are 2.0's:** the toolkit, Files, the Terminal, Notepad, the Calculator, Images, the
  Monitor, the Control Panel, PKG and FETCH. They look and behave as before.
- **A program built for 1.x still runs, as a DOS program:** it finds no API it knows, so it isn't an app: it has
  the screen to itself, as on plain DOS. To be an app again it is built with `libglos.a` and `<glos/glos.h>`
  (one library now: the API's functions, the 2D library, the settings' schema, sockets). DOS-GL and the SDL
  bridge in RetroGPU moved on 2026-11-12: programs linked with an older build of either are rebuilt.
- **GLOS's kernel and its programs go together:** `GLOS.EXE`, `GLOSK.BIN` and the programs in `C:\GLOS` are
  updated at the same time.

## USB keyboards, mice, hubs and gamepads (2026-11-05)

- **GLOS drives the PC's UHCI USB controllers itself** (Intel's PIIX and ICH, VIA's), and `glos usb` lists the
  controllers and what is on them (docs/gamepad.md, PRD D74):
  - **USB keyboards** type as a PS/2 keyboard does, at a DOS prompt, in programs and in the desktop, with GLOS's
    own key repeat (`[input]`'s delay and rate), and their lock lights follow the PC's.
  - **USB mice and touchpads** move the desktop's pointer and a DOS program's mouse as a PS/2 mouse does, read by
    their own report descriptors (the wheel isn't passed on yet).
  - **Combined keyboards and mice** on one receiver: both halves.
  - **USB gamepads drive the game port** (the virtual one below): HID pads, and Xbox pads (Xbox 360; Xbox One and
    Series, woken by GLOS). One pad is the whole port (its left stick joystick A, its right stick B, four
    buttons); a second pad becomes joystick B.
  - **Hubs**, with low-speed devices behind them. Plugging and unplugging is followed.
- `[usb] enabled = off` (the Control Panel's "USB" page) leaves the controllers to the BIOS and DOS. At `glos exit`
  GLOS gives each controller back stopped; a BIOS that emulated a USB keyboard may not resume it until a restart.

## Fixed: INT 15h from DPMI programs (2026-11-05)

- **A DPMI program's INT 15h now gets GLOS's answers**, as a real-mode program's always did: block moves (87h) made
  by GLOS and kept out of its memory, no free extended memory reported (88h, E801h, E820h), the virtual A20. Before,
  such calls went to the BIOS: a DPMI program asking for a block move was killed (the BIOS's own switches to
  protected mode), and the memory sizes were the PC's, which GLOS owns.

## A virtual game port (2026-11-05)

- **`glos joy a=X,Y b=X,Y buttons=NNNN`** (axes 0-255, 128 the centre; `none` for a stick that isn't there;
  buttons A1 A2 B1 B2, 1 pressed) sets a joystick that DOS programs read at the game port, 201h, and through INT
  15h AH=84h, in real mode, as DPMI programs and as GLOS apps alike. `glos joy` alone shows it, `glos joy off`
  gives the PC's own game port back. This is the first step of USB gamepads under GLOS (docs/gamepad.md): the pads
  will drive the same port.
- **Timing as a real stick's:** each axis's one-shot runs 24 µs plus up to 1,100 µs (`[joystick] range`), timed from
  the program's write by the processor's time-stamp counter, or PIT channel 0 on a 486. A program that counts its
  reads sees fewer of them than on a real port, because each read is answered by GLOS; its calibration screen
  scales them as usual.
- **`[joystick] port = auto | virtual | off`** (the Control Panel's new "Joystick" page): auto, the default,
  answers 201h only while a source is on. A direct-mode program's protected-mode code still reads the PC's own port.

## Sound on Crystal chips (2026-11-05)

- **GLOS plays sound on a Crystal CS423x** (CS4232 to CS4238B), as on bench-g400's onboard audio, which no DOS
  driver set up before. GLOS sets the chip up itself when it starts (docs/crystal-audio.md): its apps play
  through GLOS's mixer, and DOS programs in sessions find a Sound Blaster Pro at 220h, with BLASTER set for
  them (Quake's demo played its sound on bench-g400). The chip stays set up after `glos exit`.
- `[sound] crystal = off` leaves the chip alone; `[sound] crystal = wss=534 sb=220 fm=388 irq=5 dma=1 cap=0 game=200
  ctrl=120` (in hex) picks other resources. `glos audio` names the card and counts the codec's sample errors.

## glos restart (2026-11-05)

- **`glos restart`** ends GLOS as `glos exit` does, but with ERRORLEVEL 200, so that a line in AUTOEXEC.BAT
  can start it again: `:glosrun`, then `C:\GLOS\GLOS.EXE`, then `IF ERRORLEVEL 200 GOTO glosrun` (not `:glos`,
  where CONFIG.SYS's menu has a GLOS entry: DOS labels ignore case). A new GLOS copied
  over SFTP then runs within seconds, with no reboot.

## Upper memory (2026-11-05)

- **`[memory] stub_page = off | auto | SEGMENT`** (or `/STUBPAGE=`; the Control Panel's "Memory" page; off by
  default): GLOS's resident stub moves into a free 4 KB page above 640 KB that GLOS maps itself, and DOS keeps only
  its PSP and environment. MEM /C shows GLOS at 448 bytes, or 4,560 with the disk cache's buffer, against 7,312.
- **Which page:** one that reads as nothing there (FFh throughout, twice), that no adapter ROM reaches and no PCI
  card claims; `auto` takes the first from C8000h, below E0000h. A refused page is logged and GLOS starts anyway.
  A card whose memory appears only when its driver wakes it can't be seen, hence opt-in and a page by name.
- **DPMI programs give up their 4 KB too:** with the page, the DPMI host's real-mode stacks live in pages of GLOS's
  (two at once, a page each; a third program gives its own, as before).
- **`glos exit`** copies the stub back into DOS's memory first; if a program has taken every byte, the exit is
  refused (in the log) and GLOS carries on until memory is free.
- **UMAPROBE** (`tests/dos/umaprobe.c`) lists what is in C0000h-EFFFFh, read-only, on plain DOS or GLOS.
- **Chipset UMBs** (UMBPCI's, made before GLOS starts) are DOS's own memory, and GLOS runs among them:
  `LOADHIGH GLOS.EXE` puts all of GLOS in one, 7,312 bytes with the cache's buffer, none of them conventional.
  `stub_page = auto` takes no page there (they read as RAM). Checked in 86Box (`make loopa-umb`) and on bench-g400,
  where MS-DOS 7.1 leaves too small a block for GLOS to load high (it runs low among them).
- **On bench-g400** (an IBM PC 300PL, a G400): the page at C800h; GLOS's conventional memory 4,864 bytes with the
  disk cache, against 36,160 for the build before the cache and the page; DOSBench's games run as before.

## glos exit after a resident program hooked INT 2Fh (2026-11-05)

- **`glos exit` works when a program loaded under GLOS stays resident on INT 2Fh** (DOSLFN, which DOSBench's
  Quake 2 and Half-Life runs load). GLOS's INT 2Fh handler was at the bottom of the chain, and the program
  still called it after GLOS had gone: the first INT 2Fh in DOS afterwards ran into memory GLOS had given
  back, and bench-g400 hung. GLOS now leaves 256 bytes resident in that case only, which pass the program's
  calls on to DOS's own handler: MEM shows them as GLOS. The program keeps working, and GLOS can start again.

## The network card at glos exit (2026-11-05)

- **GLOS stops its network card when it leaves.** It had left the card running, so a PCI card went on
  writing the frames it received into memory GLOS had given back to DOS, for whatever ran next to find.
  The card is reset now and its PCI settings are put back as GLOS found them. An Intel 8255x gets a
  selective reset before its software reset, at GLOS's start too, as Linux's driver does for a card that
  may be at work.

## The DPMI host's real-mode stack (2026-11-03)

- **A fix for DOS/4GW programs and others that nest real-mode calls.** The DPMI host ran each nested real-mode
  call (a program's 0300h, a reflected interrupt) on a 512-byte slice of a 2 KB stack, so a fifth one in a row
  wrapped round and wrote 62 KB past the stack, into whatever conventional memory lay there. Seen with MGA-Glide's
  DOS/4GW tests (an INT 1Ch reflected four deep). The stack is 4 KB now, eight slices, and a call with none left is
  refused: each DPMI program gives 2 KB more of its own conventional memory for it (1687h).

## Tabs that don't fit in a row (2026-11-02)

- **Every page's title is in view now.** The Control Panel's titles had run off the window's right edge (File
  Types and Programs were never seen); they now wrap into two rows, and into fewer as the window grows. The
  window is 440 pixels tall, as 640x480 allows, so the pages keep their room.
- **For any program** (`gui_tabs_style`, docs/gui.md): titles that don't fit go into more rows (the default), stay
  in one row with arrows that scroll it, or become a list down the left with a scroll bar. Tabs whose titles fit
  in a row look as before.

## The disk cache (2026-10-31)

- **GLOS keeps what DOS reads from the hard disks in spare memory** (D72, docs/disk-cache.md), on by default. On
  bench-g400, DOSBench's Half-Life run (HLD1) took 431 s under GLOS without a cache and 87 s with SMARTDRV; with
  GLOS's own cache it takes 71 s, or 65 s when the level was read before. Quake 2's (Q2D1) takes 37 s, down from
  106 s. Frame rates are as before, apart from Half-Life's 5-second first frame, which is gone.
- **Write-through:** a write goes to the disk before GLOS keeps it, so a reset or `glos reboot` never loses data.
- **Its memory is given back** the moment a program, XMS or the kernel needs it: programs see all the memory
  they saw before.
- **Safe against writes it can't see:** the IDE command registers are watched outside its own calls, a
  direct-mode session bypasses it and empties it when it ends, and with SMARTDRV loaded first it stays off.
- **`[cache] enabled`** (on, off, verify, count) and **`size`**, in the Control Panel's "Disk Cache" page;
  `glos cache`, `glos cache drop` and `glos cache lbas` over SSH.
- **Conventional memory:** 7 KB with the cache on (its 4 KB block buffer), 3 KB with it off.
- Tested by `make loopa-cache`: DSKTORT, a torture test of random reads and writes by CHS and LBA, from V86 code
  and protected mode, with sectors written behind the BIOS's back, gives the same answers with the cache on, off
  and verifying, and on plain DOS. On bench-g400, verify mode compared 2.9 million hits with the disk over the
  DOSBench game set and found no difference.

## P7 in agent mode (2026-10-29)

- **GLOS took 35 KB of conventional memory in agent mode,** not P7's 4 KB: since M9 the settings writer held the
  DOS server's 32 KB transfer buffer for good. It now holds it only while it reads or writes GLOS.CFG (3 KB), and
  `make loopa-mem` measures agent mode too.

## API 1.5: the mouse wheel (2026-11-06)

- **Scrolling with the wheel.** A USB or PS/2 mouse's wheel scrolls the window under the pointer, focused or not:
  three rows a notch in the toolkit's lists, the editor (Notepad) and Term. Apps hear of it as `GLOS_EV_WHEEL`
  (key: the notches, positive away from the user), sent only to apps built for API 1.5 or later (D75).
- **DOS programs** that turn their PS/2 mouse's wheel on get a USB mouse's wheel in the fourth byte, as from a PS/2
  wheel mouse.
- **A PS/2 wheel mouse no DOS driver has set up** is put in its wheel mode when GLOS starts (`GLOS-KBC mouse id=3
  wheel=1`), for the desktop; a DOS program's reset of the mouse puts it back.

## API 1.4 (2026-10-27)

- **Write-combining memory.** `GLOS_FN_CACHE` (for any program) makes whole 64 KB units of the program's own
  conventional memory write-combining, by the fixed MTRRs, and GLOS gives them back when the program ends (D71).
  MGA-Glide's HAL uses it for the G400's DMA rings, which the card then fetches by AGP cycles. GLOS logs the
  MTRRs at boot (`GLOS-MTRR`). Tested by CACHEWC in `make loopa-dpmi`.

## M10: community features (2026-10-18)

Each feature ships when its own gate passes (PRD D13). All of them have.

| Feature | Gate | Passed |
|---|---|---|
| Network time (SNTP), local time by `[time] zone` | `make loopa-time` | yes |
| HTTPS (`FETCH.EXE`, BearSSL, Mozilla's roots) | fetches from GitHub and a FreeDOS mirror with certificate checks (`make loopa-https`) | yes |
| The terminal's SSH and SFTP client | `make loopa-sshc` against OpenSSH | yes |
| Packages (`PKG.EXE`) | a signed package installs and uninstalls cleanly (`make loopa-pkg`) | yes, with the suite's test key |
| VNC through SSH | a stock VNC client views and drives the desktop through an SSH tunnel (`make loopa-vnc`) | yes (vncdotool, 8, 16 and 32 bits) |
| Capture (`glos rec`) | a capture's frames are identical to `glos shot` (`make loopa-vnc`) | yes |
| UTF-8 fonts, DOS's code pages, smoothed text | goldens with Greek and Cyrillic, names through 850 and 866, smoothing at 16 and 32 bits (`make loopa-gui`) | yes |

- **Time.** The RTC and DOS keep local time. `[time] zone` is a POSIX TZ string, which DJGPP programs get as
  `TZ`. SNTP asks `[time] server` when the network comes up, and `glos time` sets the time by hand. The Control
  Panel has a "Date and Time" page.
- **API 1.3.** Apps get TCP clients (`GLOS_FN_NET`, with BSD sockets in the SDK), random bytes and the time.
- **HTTPS.** `FETCH.EXE URL [FILE]` checks certificates against `CERTS.DAT` (121 of Mozilla's roots). It
  refuses, with the reason, until the clock has been set.
- **SSH and SFTP.** `ssh`, `sftp` and `ssh-keygen` in the terminal. The screen is xterm-compatible (vi and top
  work). Login is by Ed25519 key or password, and host keys are kept in `SSH\KNOWNHST.TXT`.
- **Packages.** `PKG` installs, upgrades and removes packages, as a command or a window. A GLOS source is signed
  with Ed25519. FreeDOS's repositories work too, unsigned and said so.
- **VNC.** Turn it on with `[vnc] enable` (the Control Panel's "Remote Desktop"), then use `ssh -L
  5900:127.0.0.1:5900`. Up to four viewers; the task bar says VNC while one is connected. `glos rec` streams PNG
  frames, and `tools/rec.py` writes them out for ffmpeg.
- **Text.** The fonts now cover Latin Extended-A, Greek, Cyrillic and common signs. DOS's names and DOS
  programs' output go through code page 437, 850, 852, 866 or 737 (GLOSCP, or what DOS says). Smoothed text is
  `[display] font_smoothing`, at 16 and 32 bits.

The GLOS packages repository is [ranulphus/glos-packages](https://github.com/ranulphus/glos-packages) (MIT). It is
signed with the owner's offline Ed25519 key, made 2026-10-20; GLOS ships its public half as
`C:\GLOS\KEYS\PKG\GLOS.PUB` (`tools/pkg/GLOS.PUB`). Its first signed index, with no packages yet, was published
on 2026-10-20, and PKG reads it as signed (`make loopa-pkg`). FreeDOS's base and util repositories are PKG's other
default sources.

The regression (the host tests and 42 Loop A suites) and the gate (133 parts) pass on M10's last commit. They
found two older races, now fixed:
- `glos rec` or a VNC viewer could keep a half-drawn frame;
- an app's `GLOS_FN_WINVRAM` was refused while another program's session had the screen.

On real hardware (2026-10-26; bench-g400, an IBM PC 300PL with a G400 under MS-DOS 7.1), every M10 check has
passed except the FreeDOS mirror, which was down: time, HTTPS from GitHub, the SSH client and SFTP, packages,
VNC and capture at 16 and 32 bits, and the fonts, smoothing and code pages byte for byte against the host's
pictures. The bench found three bugs, fixed the same day: the kernel's entry flags under MS-DOS 7.1, no log
without a serial cable (`/VERBOSE`, `GLOSLOG`), and a network card refused for sharing its IRQ (devices'
keepers, docs/devices.md). See docs/milestones-m10.md, "On the bench".

Needs MGA-Glide's 86Box patch 0118 (the PIIX4 RTC alias). Without it, the bf6 machine's clock stops after
SNTP's first change of century.

## M9: native apps and the Control Panel (2026-10-18)

The retained-mode toolkit (`sdk/gui`) and the apps, each tested by its pictures on the host and in Loop A:
- the file manager, which is also the desktop's shell;
- Notepad, the calculator and the image viewer;
- the terminal, with GLOS's shell (DOS, modernised) and console jobs;
- the monitor;
- the Control Panel, which can change every setting in GLOS.CFG (one schema for the loader, the kernel and the
  apps; `glos config`).

## M8: windowed DOS-GL, SDL3 and the mixer (2026-10-15)

- The GPU lock: an app's turn with the engine ends only at a safe point, with a watchdog.
- Apps' VRAM, and DOS-GL and SDL3 programs in windows on a 16-bit Matrox desktop.
- The SB16 driver and the desktop's mixer.
- Apps' COM1 lines in GLOS's log.

## M7: Matrox 2D (2026-10-15)

The desktop on Matrox cards' drawing engine (the shared HAL): copies, fills, colour expansion, the hardware
cursor and a VRAM manager. `[display] accel = off` turns it off.

## M6: the GUI core (2026-10-14)

- App threads and the GLOS API.
- The 2D library, the VBE display and the window manager.
- The desktop in the "Slate" look, with DejaVu fonts.

## M5: the agent completed (2026-10-11)

- `glos shot` in every mode, and input through `glos key`, `type` and `mouse`.
- `glos stat`, `log --follow`, `kill` and `reboot`.
- RTL8139 and Intel 8255x drivers, gdb through SSH, and mDNS.

## M4: the DPMI host (2026-10-04 to 2026-10-09)

- 32- and 16-bit clients, exceptions, IRQs, callbacks and DOS/4GW.
- Sessions and per-program profiles, with direct mode (IOPL 3).
- `glos run`, and the gate's baseline matrix: CWSDPMI, HDPMI, DOS/4GW, RTM and the games.

## M1 to M3: the supervisor and the agent (2026-10-02 to 2026-10-03)

- The loader and the ring-0 kernel.
- DOS in a V86 system VM, with virtual devices, XMS and INT 15h.
- Threads, NE2000, lwIP, the SSH server, SFTP, and the agent's shell and capture.

## M0: the harness (MGA-Glide)

Loop A's machines (bf6, the 486s), cards, network, COM2, boot variants and V86TEST. Plus 86Box patches for the V86
faults it found.
