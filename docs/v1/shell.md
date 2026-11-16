# GLOS's shell: one command interpreter, reached two ways

**Status: agreed by the owner (2026-11-12, 2026-11-13) and built through S3 (S0 to S2d, and S3 but for `DD` from
the connection). Step S4 is designed below and agreed (2026-11-16); it is being built, S4a first.**

## What the owner asked for (2026-11-12)

- A shell that is reached **through the Terminal app or through SSH**, the same shell either way. The terminal
  is only the way in.
- **GLOS's own COMMAND.COM**: DOS's syntax with GLOS's benefits, so that GLOS doesn't rely on the command
  interpreter of the DOS underneath. One interpreter in both roles: what a person types into, and what runs batch
  files and answers `COMMAND /C`.
- **Tools of GLOS's own** where it leans on FreeDOS's today (FDISK, FORMAT, SYS) and where it has none: `dd`,
  `df`, `du`, disk images, partitions, formatting.
- **Eventually no DOS underneath.** Not part of this design, but nothing here may make it harder.

## What there is today

| | SSH | The Terminal app |
|---|---|---|
| Interactive | No: one command a connection (`ssh pc COMMAND`) | Yes |
| A line is run by | the kernel, if it begins `glos`; otherwise the DOS's `COMMAND.COM /C` | the app's own shell (`apps/term/shell.c`, decision D50): its built-ins, or a program; batch files and pipes go to `COMMAND.COM /C` |
| Commands | `glos ver, run, shot, key, type, log, stat, ps, mem, net, kill, config, display, secrets, exit, restart, reboot` ... and whatever DOS has | `CD, DIR, COPY, MOVE, REN, DEL, MD, RD, TYPE, ECHO, SET, PATH, PROMPT, CLS, VER, EXIT, HISTORY, HELP, WHERE, START, SSH, SFTP, SSH-KEYGEN`, and programs |
| Where it runs | the kernel (`kernel/ssh/sshd.c`, `kernel/dos/agent.c`) | a GLOS app (ring 3) |

Two half-shells: neither has the other's commands, SSH has no prompt, and both depend on the DOS's COMMAND.COM
for batch files. The Terminal has its own shell because there is one DOS under GLOS: a COMMAND.COM waiting at a
prompt holds it, and everything else that needs DOS waits behind it.

## The design

### Three things, kept apart

- **A terminal** is a screen of characters and a keyboard: the Terminal app's window, or an SSH connection with
  a terminal on the other end. It knows nothing of commands.
- **The shell** is GLOS's command interpreter: one body of code, one command set, one language (DOS's).
- **A session** is one shell attached to one terminal (or to none: `ssh pc COMMAND`, a batch file, `COMMAND /C`).

### Where the shell runs: in the kernel, as SSH and SFTP do (proposed)

- The interpreter is a library in portable C (the parser, the batch language, the built-in commands) that asks
  for everything it needs through a small interface (files, programs, a terminal). It is tested on the host
  against that interface, like the toolkit and the window manager.
- GLOS's kernel has it, a thread a session. Its file work goes through the kernel's DOS server, a call at a
  time, as SFTP's and the apps' does: so several sessions run side by side, and a session at its prompt holds
  nothing. Programs are run as jobs, as now (one DOS: they still take turns).
- **Why the kernel and not an app:**
  - it is there before anything is installed: on the setup floppy, GLOS's shell and its tools are in `GLOSK.BIN`,
    and FreeDOS's COMMAND.COM, FDISK and FORMAT (about 190 KB) can go;
  - an SSH login needs no file on the disk and no free DOS;
  - a GLOS app is 300-400 KB (it carries the C library): too big for the floppy, and one more program to load
    for every `COMMAND /C`.
- **What it costs:** more code in ring 0 (my estimate: 40-60 KB), which reads what people type. It is kept to
  the interpreter; the tools' heavy parts (FAT, partitions) are the same code the host tests run.
- **The alternative:** the shell as a GLOS app that the Terminal app and SSH both attach to through a console
  the kernel provides. Cleaner separation, but none of the three points above, and the setup floppy would keep
  FreeDOS's tools. I recommend the kernel.

### The ways in

- **The Terminal app** becomes a terminal only: its window, its xterm screen (`vt.c`), its keys. It opens a
  session (a new API call: a console, a handle with bytes each way and a size) and shows what comes. Its own
  shell (`shell.c`) goes, once the kernel's does everything it did (history, completion, colour, `&&`, `||`).
- **SSH** gets a login: `ssh pc` gives a prompt (the protocol's `shell` and `pty-req` requests, which GLOS
  refuses today). `ssh pc COMMAND` runs the line through the same shell, with no terminal. SFTP is as it is.
- **The PC's own screen** in text mode, when GLOS runs without its desktop: a session there too. Until then
  (2026-11-12, a temporary measure that S2 removes) the screen has a notice: GLOS is running, the PC's name and
  address, and Ctrl-Alt-Del restarts the PC, which it does while nothing runs (`kernel/vm/vcon.c`).

### GLOS's COMMAND.COM

- **The batch language:** `@`, `ECHO`, `REM`, `SET`, `IF [NOT] EXIST | ERRORLEVEL | ==`, `GOTO` and labels,
  `CALL`, `FOR`, `SHIFT`, `PAUSE`, `%0`-`%9`, `%NAME%`, redirection and pipes. The shell runs batch files
  itself. This replaces D50's "batch files go to COMMAND.COM /C".
- **For DOS programs that shell out** (`COMMAND /C`, the C library's `system()`): a small real-mode
  `COMMAND.COM` of GLOS's (a few KB) that hands the line to the kernel's shell and runs, as the caller's child,
  what the shell tells it to. `COMSPEC` points at it.
- **Starting the PC:** GLOS as DOS's shell (`SHELL=GLOS.EXE /SHELL`, which exists) runs `AUTOEXEC.BAT` itself.
  A GLOS disk then needs the DOS's kernel and nothing else of it.
- **Checked against** FreeDOS's and MS-DOS's COMMAND.COM: the same batch files, the same results (host tests on
  the interpreter; Loop A for the real ones).

### The tools: commands of the shell

- `DF` (drives, sizes, free space), `DU` (a folder's size), `DD` (a raw copy between a disk, a file and, over
  SSH, the connection), `DISK` (the disks; a disk's partitions; make and format one; write its boot records;
  an image to or from a disk).
- GLOS's own commands (`ps`, `kill`, `log`, `config`, `display` ...) are the shell's too, under `GLOS` as now,
  so they work in the Terminal app as over SSH.
- Whatever writes a disk says what it will destroy and wants a plain yes (a flag over SSH), and never the disk
  GLOS runs from.

## In what order

| Step | What | Shows it works |
|---|---|---|
| S0 | The disk code: sectors copied in runs, the partition table read and written, FAT12/16/32 made, boot records (done, 2026-11-12: `kernel/disk`. Sectors in runs of 32 KB through the BIOS when the stub is idle, the disk cache kept right, `bdisk.c`; the table, `mbr.c`; FAT volumes made, `fatfmt.c`; GLOS's own boot code for a disk's first sector and for a FAT12, FAT16 or FAT32 volume that starts FreeDOS's kernel, `mbrboot.S`, `fdboot.S`. Until S3's commands, `glos disktest` reaches the sectors for the tests) | Host tests against `mtools` and `fsck.fat`; Loop A on a blank disk |
| S1 | The interpreter as a library, from the Terminal app's shell; the Terminal app uses it, unchanged to the eye (done, 2026-11-13: `sdk/shell`, its host's interface in `shell.h`; `apps/term/shell.c` is the Terminal's side of it) | The Terminal's goldens (the same pictures, to the byte); host tests of the parser and built-ins (`tests/shell`, transcripts) |
| S2 | Sessions in the kernel; the console call; the Terminal app a terminal only; SSH logins; `ssh pc COMMAND` through the shell | Loop A: the same script through both ways in gives the same output |
| S3 | `DF`, `DU`, `DD`, `DISK`; the setup floppy without FreeDOS's FDISK and FORMAT (`DISK` done, 2026-11-13: `kernel/disk/diskcmd.c`, docs/floppy.md; the floppy has no FDISK, FORMAT or SYS, and `install.py --new-disk` uses `DISK`. `DF` and `DU` done the same day, in the library: `DF [drive: ...]`, a FAT32 volume's real size by DOS's newer call; `DU [folder] [/S]`; `DD IF= OF=` between disks, partitions, floppies, files and, as a destination, the connection: kernel/disk/diskcmd.c. S3 is done but for `DD` from the connection) | `loopa-floppy`; the 486 |
| S4 | The batch language; GLOS's `COMMAND.COM`; `AUTOEXEC.BAT` run by GLOS; the floppy with FreeDOS's kernel alone | Batch files against FreeDOS's COMMAND.COM; `loopa-floppy` |
| later | GLOS's own DOS services (files, FAT), then no DOS underneath. With INT 21h answered by GLOS, several DOS programs run at once, each in a machine of its own (text programs in separate terminals): the owner's choice of how that comes (2026-11-13), over giving each program a copy of the DOS's own state | its own plan |

## Step S2 as designed (2026-11-13; agreed by the owner the same day: GLOS's shell answers `ssh pc COMMAND` from S2)

S1 left the interpreter a library that asks a host for a terminal, files, programs and memory, and never waits.
S2 gives it a host in the kernel and takes the Terminal app's away.

### A session

- **A kernel thread** with a `struct sh` and a **console**: two rings of bytes (to the terminal, from it), a
  size in characters, and whether there is a terminal at all. `kernel/shell/session.c`.
- **Its host's side** (`kernel/shell/host.c`):
  - *Files:* through the DOS server, a call at a time (`kernel/dos/dos.c`), as SFTP's are; the server's one
    wake-up becomes a wait of the request's own, so that sessions and SFTP use it side by side. Folders are read
    with DOS's find-first and find-next. 8.3 names at first; long names when SFTP has them (PRD §7.3).
  - *Programs:* agent jobs, as an app's console jobs are (`agent_post_job`): the session sends the job's output
    to its terminal and its keys to the job, and tells the shell when it has ended. One DOS: they take turns,
    as now. A line with a pipe goes to the DOS's COMMAND.COM whole, until S4.
  - *Memory:* the kernel's heap.
- **The line editor** is the session's (`kernel/shell/edit.c`), since a terminal is only bytes: the keys an
  xterm sends (arrows, Home, End, Delete, Backspace, Tab, Ctrl+C), the history and completion asked of the
  shell, what is typed drawn again with a few ANSI sequences. The prompt and errors are coloured the same way.
  With no terminal there is no editor: the line is run and the session ends with its exit code.
- **Text** inside the shell is the PC's code page, as DOS's files and names are; a terminal speaks UTF-8. The
  console turns one into the other, with the tables GLOS has (`kernel/lib/cp*.c`).
- **GLOS's own commands** (`ps`, `kill`, `log`, `stat`, `config`, `display` ...) become the kernel host's
  commands: what `kernel/ssh/sshd.c` does for `glos NAME` today, moved behind one table with a place to write
  to, and named as decided below. `GLOS NAME` goes on working: it is that table's alias.

### The ways in

| Way in | What changes |
|---|---|
| `ssh pc` | New: `pty-req`, `shell` and `window-change` are answered. A session with a terminal: a prompt, editing, history, programs. Ends with EXIT or the connection. |
| `ssh pc COMMAND` | The line runs through the shell, with no terminal: its exit code is the command's. Today's behaviour is kept where scripts lean on it: see the question below. |
| The Terminal app | A terminal only. A new call (CONSOLE, API 2.1: open a session, read and write its bytes, its size, an event when there is something to read) and `vt.c`'s screen for everything. `apps/term/shell.c` and the app's own SSH client's screen switch go; SSH, SFTP and SSH-KEYGEN become programs of their own (`build/apps/SSH.EXE` ...), which the shell runs like any other. |
| The PC's own screen | With no desktop, a session on the text screen and the keyboard. The idle screen's notice and its Ctrl-Alt-Del rule (2026-11-12) go: the prompt is there. |

**The session on the PC's own screen (S2d), as the owner settled it (2026-11-14):**

- It is a session of its own, like each SSH login and each Terminal window: its own directory, history and
  environment. Sessions are not mirrored; what is typed at the PC is not seen in an SSH login.
- **Kernel messages: start-up lines, then notices.** At GLOS's start the screen shows its start-up lines
  (version, the PC's name and address, anything refused, such as a network card), then the prompt. After that
  the whole log stays in `LOG`; only what the person at the PC should know (a crash report written, the network
  lost or found) is shown, as one line before the next prompt and never in the middle of a program's output.
  Not chosen: the log printed on the console as it comes (Linux's way), several local screens on Alt-F1 and
  the like, and nothing but a banner.
- A full-screen program takes the PC's screen and keyboard whichever session started it, and the local session
  has its prompt back when the program ends.

**As built (`kernel/shell/lcon.c`, 2026-11-14):**

- The session is `shsess.c`'s, opened by a thread of its own (`console`) once DOS is at GLOS's idle point and
  GLOS.CFG is read. Its terminal is `apps/term/vt.c`, the Terminal app's xterm screen, built into the kernel at
  a text screen's sizes (132 by 60 at most; about 127 KB, taken when the prompt starts) and drawn into the
  VGA's text memory: a cell's character in DOS's code page, its colours the nearest of the sixteen, the cursor
  the BIOS's and the card's.
- Keys come from `vkbc.c`, the real keyboard's and `glos key`'s and `glos type`'s alike, while the terminal has
  the screen; they are made characters by `[region] layout` and sent as a terminal sends keys. Caps Lock and Num
  Lock are kept in the BIOS's flags, so DOS programs find them as they were left.
- **A DOS program started at this prompt has the screen and the keyboard itself, from its start**, as under DOS
  (the job is `KRUN_FULL`): it writes on the screen its own way, text memory included, and nothing of it goes
  through the session. A GLOS app started there writes through the session, as at an SSH login.
- **The screen is the terminal's only while no DOS program has it** (one of this session's, or an SSH command's,
  which has always had the PC's screen). Nothing is drawn meanwhile and the keys are the program's. When it has
  gone the screen is **taken over as it stands**: the program's last output stays, the cursor is where the BIOS
  has it, and the session goes on below. After another session's program the prompt and whatever was being
  typed are drawn again on a fresh row.
- **EXIT** there begins a new session (there is nowhere to go back to). `SHUTDOWN` leaves GLOS, `REBOOT` restarts
  the PC, and Ctrl-Alt-Del at the prompt with nothing running restarts the PC, as it did at the idle screen.
- **Start-up lines:** `GLOS is running on this PC: NAME.local, ADDRESS.`, a `Not used: ...` line for each thing GLOS
  refused as it started (the log's `refuse` lines), then the shell's own first line and the prompt, in the
  root of the drive DOS started from. The prompt waits up to four seconds for a network card's address so
  that it is in those lines; later it comes as a notice.
- **Notices** (`lcon_note`): the network up with its address, the network gone, a crash report written. Shown
  above the line being typed, at the prompt alone; while a program runs they wait for its end.
- **`[shell] prompt = off`** keeps the screen as it was before S2d: the line that says GLOS runs and how it is
  reached (`kernel/vm/vcon.c`), the keys DOS's.
- **`glos shot --text`** gives the PC's text screen as text (a line a row, then `cursor=COL,ROW`): what the tests
  read, and a quick look at a bench's screen from afar.
- **The keyboard is left to a GLOS app that has no terminal** and was started by someone else (`glos run --app`
  over SSH): with no desktop such an app reads the PC's keyboard through DOS, as it always has.
- **The session holds nothing of DOS's at its prompt**: the DOS server's 32 KB buffer is taken while a line of
  its runs and given back at the prompt and before a program it starts, so GLOS at rest still takes 8 KB of
  conventional memory at most and a program started at the PC finds what plain DOS would give. (A login over
  SSH holds the buffer for its whole life, as before: to do the same for it is a follow-up.)
- Not done: a program that writes text memory without the BIOS while another session's hidden program runs is
  not seen; 50-line and 132-column modes are taken as found but not set by the shell; the screen has no
  scroll-back.

### In what order, inside S2

| | What | Shows it works |
|---|---|---|
| S2a | Sessions, the kernel's host, the line editor; `ssh pc` logs in | Loop A: a script typed through `ssh -tt` (commands, a program, history, Tab, Ctrl+C), its transcript; host tests of the editor |
| S2b | `ssh pc COMMAND` through the shell; GLOS's commands as the host's table, with their new names and `GLOS NAME` | Every suite as it is (they all speak through `ssh pc COMMAND`) |
| S2c | The CONSOLE call; the Terminal app a terminal only; SSH, SFTP, SSH-KEYGEN programs | The Terminal's scripts in Loop A, the same lines; `loopa-sshc` |
| S2d | The session on the PC's own screen (built 2026-11-14: `kernel/shell/lcon.c`) | Loop A: keys typed at the emulated keyboard, the screen read back (`ssh-idle-*`) |

### What it costs, and what could go wrong

- **Ring 0 grows** by the interpreter, the editor and the sessions (my estimate: 25-35 KB of code; a session
  about 12 KB of memory, mostly its history and folders).
- **`ssh pc COMMAND` is what every test suite speaks.** If the shell's own DIR, COPY, TYPE and ECHO answered
  there in place of the DOS's, some suites and scripts would see other text (the shell's DIR is not FreeDOS's).
  The owner's answer (2026-11-13): GLOS's shell from S2, as at the prompt; batch files and piped lines to the
  DOS's COMMAND.COM until S4; the suites that leaned on FreeDOS's wording are changed, and a session begins
  with the environment a DOS job gets (TZ and the rest).
- **A session's files and a program's** go through the one DOS in turns: a long COPY in one session holds
  nothing, but each of its calls waits its turn behind a running program. As SFTP today.

## Step S2c as designed (2026-11-13; agreed by the owner the same day: the shell as a kind of job, and SSH, SFTP and SSH-KEYGEN as programs with `GLOS_SYS_TERMINAL`; not built yet)

S2a and S2b are built: `ssh pc` logs in, `ssh pc COMMAND` runs through the shell, GLOS's own commands are the
shell's. S2c makes the Terminal app a terminal of that same shell. It adds to the public API, which
the owner has agreed (above).

### The API: the shell as a kind of job (2.1)

A session is reached with the calls an app already has for a program it runs, not with an area of its own:

| | Today (2.0) | Added (2.1) |
|---|---|---|
| `GLOS_JOB_RUN` | a program, in a window of the app's, full-screen or hidden | `how = GLOS_RUN_SHELL`: GLOS's shell, with a terminal of `cols` by `rows` (two fields at the structure's end; `command` empty for a login, or a line to run) |
| `GLOS_JOB_READ`, `GLOS_JOB_WRITE` | the program's output; its input | the bytes for the terminal (UTF-8, an xterm's sequences); the bytes typed |
| `GLOS_JOB_STATUS`, `GLOS_EV_JOB`, `GLOS_CORE_CLOSE` | its state, its end | the session's, the same |
| `GLOS_JOB_SIZE` (new, the one new call) | | the terminal's size has changed |
| `GLOS_SYS_TERMINAL` (new) | | for a program that is run at a terminal (below): its bytes are the terminal's as they are |

One flag, two fields, two calls. `GLOS_MINOR` becomes 1; a 2.0 program sees nothing change.

### The Terminal app

- **A terminal only:** its window, `vt.c`'s screen for everything, keys turned into the bytes an xterm sends
  (as its SSH client does today), a session opened with `GLOS_RUN_SHELL`. `apps/term/shell.c` goes (the shell
  is the kernel's), and so does `term.c`'s own text view.
- **`vt.c` gains what the text view had:** lines kept above the screen to scroll back to, and a selection to
  copy. Its pictures change (the goldens are made again); the scripts' lines stay.
- **On the host** (the toolkit's tests have no kernel): the shim runs the same shell and editor on a thread,
  with the test's drives for files and no programs, as now.

### SSH, SFTP and SSH-KEYGEN

They are the Terminal app's own commands today. With the shell in the kernel they become programs
(`C:\GLOS\SSH.EXE`, `SFTP.EXE`, `SSHKEY.EXE`: GLOS apps without a window, on `sdk/sshc`), run by the shell like
any other, at any terminal: the Terminal app's, or an SSH login's (a hop from one PC to the next).

A program like that speaks to its terminal in bytes, not as a DOS program does (keys from the BIOS, text in
the code page). `GLOS_SYS_TERMINAL` says so: from then on what the program writes goes to the terminal as it
is, and what is typed comes to its standard input as the terminal sent it; it is told the terminal's size
and its changes. This is also the first piece of "text-mode programs over SSH" (decided: later), and nothing
else of that is built here.

### In what order, inside S2c

| | What | Shows it works |
|---|---|---|
| 1 | `GLOS_RUN_SHELL`, `GLOS_JOB_SIZE`; APITEST's cases; the shim's session (the kernel's side and the library's done, 2026-11-13: `kernel/app/api2job.c`, `glos_job_shell()`, `glos_job_size()`, API 2.1, five cases in APITEST's job mode. The kernel's command thread now runs on every PC, with or without a network, since every session asks GLOS's own commands of it. The shim's session comes with step 2) | Loop A's api suite; host tests |
| 2 | `vt.c`'s scroll-back and selection; the Terminal on a session (done, 2026-11-13: `apps/term/term.c` is a terminal alone, on one xterm screen with a thousand lines kept above it, the session opened with `glos_job_shell_in()` at the root of the drive DOS started from (`dir` = `\`; the owner, 2026-11-13: not `C:\` by name); a shell job may name its starting folder, `dir`. `apps/term/shell.c` became the host shim's session, `tests/host/shimshell.c`; the SSH client's source is `apps/ssh/ssh.c`. The Terminal's pictures were made again: its prompt is drawn as the rest of its text, an error in xterm's red, the cursor a block) | The Terminal's scripts, host and Loop A |
| 3 | `GLOS_SYS_TERMINAL`; SSH.EXE, SFTP.EXE, SSHKEY.EXE (the call done, 2026-11-13, before step 2 so that the Terminal keeps its SSH commands until the programs exist. As built: a program says `GLOS_INIT_TERMINAL` at INIT; a session that starts it at a terminal waits for its end; the program reads what is typed and writes its bytes with SYS's TERMINAL call itself (READ, WRITE, INFO for the size) and is told by `GLOS_EV_TERMINAL`, not through DOS's standard input and output, which would cost a trap a character. APPTEST `term` and the ssh suite's login case test it. The three programs, 2026-11-13: `apps/ssh/cli.c` is their terminal layer, and the client itself is still the Terminal's `apps/term/ssh.c`, built for both with a few `SSH_PROGRAM` conditions until step 2 takes it out of the Terminal; `loopa-sshc` has a case that runs them from an SSH login, and through a hop with no terminal) | `loopa-sshc`, from the Terminal and from an SSH login |

## Step S4 as designed (2026-11-16; agreed by the owner the same day, with the three answers below; S4a to S4c built, S4d to come)

S2 and S3 left three things to the DOS's COMMAND.COM: batch files, any line with a pipe or a redirection around
a program, and every program that shells out (`COMMAND /C`, the C library's `system()`). S4 takes all three, and
with them the last file of FreeDOS's that a GLOS disk needs beside its kernel.

### 1. The batch language (in the library, `sdk/shell`)

- **The shell runs a `.BAT` file itself**, at a prompt, from `ssh pc COMMAND`, and from another batch file.
- **A batch file being run is a frame**: its path, how far it has got (a byte offset: the file is opened, a
  line read and the file closed again for every line, as COMMAND.COM does, so a batch file may change or
  delete itself and a floppy may be swapped), its arguments (`%0` to `%9`, `SHIFT`), and whether lines are
  echoed. `CALL` puts a frame on top (sixteen deep at most); a batch file named without `CALL` takes the place
  of the one that named it.
- **It still never waits.** A line that starts a program returns to the host; when the program has ended the
  shell goes on with the rest of the line and then with the frame's next line. Ctrl+C in a batch file asks
  `Terminate batch job (Y/N)?` at a terminal, and ends it where there is none.
- **Commands:** `@`, `ECHO ON | OFF | text` and `ECHO.`, `REM`, `IF [NOT] EXIST name | ERRORLEVEL n | a==b`, `GOTO`
  and `:labels` (eight characters count, as DOS has it), `CALL`, `FOR %%v IN (set) DO` (and `%v` at the
  prompt; names with `*` and `?` in the set), `SHIFT`, `PAUSE`, `%0` to `%9`, `%NAME%`, `%%`.
- **The commands the shell hands to COMMAND.COM today**, become its own: `VOL`, `DATE`, `TIME` (GLOS's clock,
  set as well as shown), `TRUENAME`, `BREAK` and `VERIFY` (taken, and nothing done: GLOS has neither switch),
  `LH` and `LOADHIGH` (the program is run where it fits: upper memory is GLOS's to give), `CHCP` (shown; set
  through `[region]`), `CTTY` (refused, in words).
- **Whose behaviour:** MS-DOS 6.22's and 7.1's COMMAND.COM, which the period's batch files were written for.
  FreeDOS's COMMAND.COM is the one the tests can run beside (Loop A has it): the same batch files through
  both, the same output and exit codes; where FreeDOS's differs from MS-DOS's, MS-DOS's wins and the test's
  list of differences says so.
- **Not in it:** Windows NT's additions (`%~dp0`, `CALL :label`, `SETLOCAL`, `FOR /F`, `IF /I`). `&&` and `||`,
  which the shell has had since the Terminal's (D50), stay.

### 2. Redirection and pipes (the shell's own)

- **`>`, `>>` and `<` around a program:** the job's standard input and output are those files, opened by the
  resident stub before it starts the program (the agent already starts jobs with files for their streams, for
  the API's JOB calls). A built-in's output to a file works today.
- **`A | B`:** as DOS does it, since DOS runs one program at a time: A's output goes to a temporary file, B
  reads it, and the file is deleted. The file is in `%TEMP%`, else in the root of the drive DOS started from.
  Either side may be a built-in (`DIR | SORT`, `TYPE X | FIND "Y"`).
- From here no line goes to the DOS's COMMAND.COM whole.

### 3. GLOS's `COMMAND.COM`

For DOS programs that shell out: `C:\GLOS\COMMAND.COM`, a real-mode program of a few KB, and `COMSPEC` points at
it for everything GLOS starts.

- **It is the resident stub's loop in a program of its own.** The stub asks the kernel what to do next and is
  told "start this program", "make these DOS calls for me" or "leave": that is how GLOS's shell, which lives
  in the kernel, runs programs and reaches files through the one DOS there is. `COMMAND.COM` asks the same
  question, as the program that was started by whoever shelled out:
  - `COMMAND /C line`: the kernel opens a shell with no terminal for the line. Its variables are the
    environment `COMMAND.COM` was given, its folder DOS's current one, and its output goes where the calling
    program's does. Built-ins are done with DOS calls that this `COMMAND.COM` makes; programs are started by
    it, as its children, so they are the caller's grandchildren as under DOS. It ends with the line's exit
    code. What the line `SET` is gone with it, as under DOS.
  - `COMMAND` alone (a program's "DOS shell" item): an interactive session on what the program has, the PC's
    own screen and keyboard or the terminal its job was started at. `EXIT` goes back to the program.
  - `/K line` likewise, then the prompt. `/E:n`, `/P`, `/Y` and the rest are taken and nothing is done.
- **Not provided:** INT 2Eh (COMMAND.COM's back door for running a line, which a few DOS utilities use).
- **`[shell] comspec = glos | dos`** (default `glos`): `dos` leaves `COMSPEC` at the DOS's own COMMAND.COM for
  the programs GLOS starts, for one that needs the real thing. GLOS's own shell does not change with it.

### 4. `AUTOEXEC.BAT`, and GLOS as DOS's shell

- With `SHELL=C:\GLOS\GLOS.EXE /SHELL` in CONFIG.SYS, GLOS is what DOS starts instead of a COMMAND.COM (this
  exists: today GLOS then starts the DOS's COMMAND.COM to run `AUTOEXEC.BAT` and to be the console).
- **New:** GLOS runs `AUTOEXEC.BAT` itself, in the root of the drive DOS started from, as the first thing the
  session on the PC's own screen does: its output is on the screen, its resident programs (a mouse driver,
  a CD-ROM's) load once before anything else, and when it has ended the prompt is there. **What it `SET`
  (`PATH`, `BLASTER`, `TEMP` ...) becomes the system's environment**: every session and every job from then on
  starts with it.
- Started from an `AUTOEXEC.BAT` by the DOS's own COMMAND.COM, as an installed GLOS is today, nothing changes:
  GLOS does not run the file again.
- If GLOS cannot start (no 386, too little memory), the loader says why and, as today, starts the DOS's
  COMMAND.COM if there is one. On a disk with FreeDOS's kernel alone there is none: the message stays on the
  screen and the PC stops there.

### 5. The setup floppy, and new disks

- The floppy: `SHELL=A:\GLOS\GLOS.EXE /SHELL`, and FreeDOS's COMMAND.COM goes (about 90 KB, which doubles what
  the 720 KB disk has to spare). FreeDOS's kernel is the one file of FreeDOS's left.
- `install.py --new-disk` (a disk GLOS partitions and formats itself) installs the same way: FreeDOS's kernel,
  GLOS as its shell, an `AUTOEXEC.BAT` of GLOS's own. A PC that has a DOS already keeps it and its COMMAND.COM,
  with GLOS started from `AUTOEXEC.BAT` as now.

### In what order, inside S4

| | What | Shows it works |
|---|---|---|
| S4a | The batch language in the library; the shell runs `.BAT` files itself in every session | Host transcripts; the same batch files through FreeDOS's COMMAND.COM in Loop A, compared |
| S4b | Redirection and pipes around programs | Host tests; Loop A: every line that went to COMMAND.COM whole |
| S4c | GLOS's `COMMAND.COM` (`/C`, `/K`, alone); `COMSPEC`; `[shell] comspec` | Loop A: programs that shell out (`system()`, a batch file run by a program, a "DOS shell"), from an SSH login and at the PC |
| S4d | GLOS as DOS's shell runs `AUTOEXEC.BAT`; the system's environment; the floppy and new disks with FreeDOS's kernel alone | `loopa-floppy`; the 486 bench from the floppy |

### S4a as built (2026-11-16)

- **In the library** (`sdk/shell/shell.c`): a batch file is a frame (`struct sh_batch`: its path, the place of its
  next line, its words); `CALL` stacks them sixteen deep, a batch file named without `CALL` takes the place of
  the one that named it. Every line is read by opening the file anew. `IF`, `GOTO`, `CALL`, `FOR`, `SHIFT`,
  `PAUSE`, `REM`, `@`, labels, `%0` to `%9`, `ECHO ON | OFF` and the lines shown as they are run; `VOL`,
  `TRUENAME`, `BREAK`, `VERIFY`, `LH`, `LOADHIGH`, `CTTY`. A batch file's lines have their variables put in as
  the line is read, as DOS does; an unknown `%NAME%` is nothing there and stays as typed at the prompt.
- **Two values where MS-DOS has one and GLOS's `&&` needs another:** `IF ERRORLEVEL` looks at the last
  *program's* exit code, which a command of the shell's own leaves alone, as under MS-DOS; `&&` and `||` go by
  the last command's result, as they did. A batch file's own result, for what follows it and for the line's
  exit code, is its last program's exit code, as `COMMAND /C`'s is.
- **The host's new parts** (each optional): `seek` (a batch file's next line without reading up to it), `key`
  (`PAUSE`, and `Terminate batch job (Y/N)?`, which waits for Y or N) and `stop` (Ctrl+C since last asked: the
  shell asks between lines, so a batch file that loops without running a program can be stopped).
- **In the kernel** (`kernel/shell/shsess.c`): those three; and `DATE`, `TIME` and `CHCP` as commands of its
  own (GLOS's clock in the region's format, set with `DATE YYYY-MM-DD` and `TIME HH:MM[:SS]`; they print what
  DOS prints with Enter pressed at its question, since nothing waits). With that no command of the batch
  language goes to the DOS's COMMAND.COM.
- **Who runs a batch file today:** at a terminal (an SSH login, the Terminal app, the PC's own screen) GLOS's
  shell, always. In a line with no terminal (`ssh pc NAME.BAT`) a program is the agent's to find, so that the
  line is in its queue at once, and a batch file found there still goes to the DOS's COMMAND.COM until S4c
  makes `COMSPEC` GLOS's; `CALL NAME.BAT` there is the shell's.
- **Tests:** `tests/shell/batch.txt` on the host (a transcript: every command, Ctrl+C with a keyboard and
  without). Loop A, `ssh-batch-*` in `make loopa-ssh`: eight batch files through FreeDOS's COMMAND.COM and
  through GLOS's shell, the same output and exit code each; the label of more than eight characters, which
  FreeDOS's takes whole and MS-DOS's (and GLOS's) by its first eight; `PAUSE` with nobody at a keyboard;
  `DATE`, `TIME`, `CHCP`.
- **Known differences from MS-DOS, to be met on the benches:** a batch file's words keep no quotation marks
  (`%1` of `"a b"` is `a b`); `DATE` and `TIME` with nothing after them do not wait for a new one.

### S4b as built (2026-11-16)

- **`<`, `>` and `>>` around a program** are the shell's: it takes them out of the line and tells its host the
  files (`io`), and the kernel's host gives them to the agent as the job's standard input and output (the files
  a job could always have, `agent_opts.file`). `> NUL` and a name in quotation marks work. A built-in's output
  goes to the file as before; a built-in reads no input.
- **A pipe** is made the steps DOS would take with one program at a time (`pipe_steps`): each step's output is
  added to a temporary file made empty beforehand (so a step that writes nothing still leaves the next one a
  file), the next step reads it, and it is deleted when it has been read, whatever came of the step. The files
  are `GPnnnmmm.TMP` (the session's number, the pipe's) in `TEMP`'s folder, or `TMP`'s, else the root of the
  drive the shell is on. `&&` and `||` after a pipe go by its last step.
- **A batch file is not redirected by the line that names it** (`X.BAT > FILE`), as under MS-DOS.
- No line goes to the DOS's COMMAND.COM whole any more at a terminal. With no terminal a batch file named as a
  program still does (S4a's note), until S4c.
- **Tests:** `tests/shell/pipes.txt` on the host; in Loop A's `ssh-batch-*`: output to a file, added to a file,
  input from a file, a built-in piped to a program and a program to a program, each against FreeDOS's
  COMMAND.COM running the same line: the same output, exit code and file; no temporary file left.

### S4c in detail (2026-11-16, before it is built)

- **The program** (`apps/command/command.asm`, a `.COM` of about 1 KB): it tells the kernel it has begun, then
  asks what to do next, again and again, exactly as the resident stub does: start this program (EXEC, as its
  child), make this DOS call, wait (HLT), or leave with this exit code. It keeps a block for those (the
  registers of a DOS call, a program's path and command tail) and a block of memory for its children's
  environment.
- **How it reaches the kernel:** the instruction the stub uses (`ARPL`, an invalid opcode in V86 mode) followed
  by the letters `GCMD`: the kernel takes that from any real-mode program. Two calls: BEGIN (its block, its
  environment block; the kernel reads its command tail and its environment from its PSP) and NEXT (how the
  last thing ended).
- **In the kernel** (`kernel/shell/cmdcom.c`): each such COMMAND.COM that runs has a shell session of the
  ordinary kind (`shsess.c`) with three differences:
  - **its DOS calls are made by its COMMAND.COM**, not by the resident stub, which cannot run while the
    program that shelled out does. The DOS server's requests say who is to make them (`dos.c`); a file a
    session has open is a handle of the process that opened it, so one executor must make all of a session's
    calls. Other sessions' requests wait meanwhile, as they do behind any running program;
  - **its programs are started by its COMMAND.COM** (EXEC), not as jobs of the agent's: they are children of
    the program that shelled out, as under DOS, with the session's variables as their environment;
  - **what it writes goes through DOS's handles 1 and 2** of its COMMAND.COM, so it lands where the calling
    program's output does: the screen, a file it was redirected to, a job's captured output.
- COMMAND.COMs nest (a program run by one shells out again): a stack, eight deep.
- The DOS server's 32 KB buffer, if it has to be allocated while a COMMAND.COM is the one making DOS calls, is
  made GLOS.EXE's block (its owner written into DOS's memory control block): DOS would free it with the
  COMMAND.COM otherwise, while other sessions go on using it.
- **`/C line`** first: a session with no terminal, the line, the exit code. Then `COMMAND` alone and `/K`: the
  session reads lines with DOS's own line input (INT 21h 0Ah) through its COMMAND.COM, a plain prompt, no
  history: what a program's "DOS shell" needs.
- **`COMSPEC`** for the agent's jobs and in the master environment becomes `X:\...\GLOS\COMMAND.COM` (the
  folder GLOS runs from); `[shell] comspec = dos` leaves the DOS's.

### S4c as built (2026-11-16)

As designed above, with these particulars:

- **Where it is:** `COMMAND.COM` beside `GLOS.EXE` (1.2 KB; `make all` builds `build/glos/COMMAND.COM`; the
  installer, the setup floppy and `~/bench-update.sh` carry it). The loader looks for it there and knows it by
  its mark (`BI_F_OWNCMD`). Without the file GLOS behaves as before: the DOS's COMMAND.COM everywhere.
- **Who is given it:** every program the agent starts has `COMSPEC` set to it (unless its profile or the job
  sets `COMSPEC`); the agent's own "through COMSPEC" jobs (a line that is not a program's name, a batch file
  named with no terminal) go through it; and GLOS's shell's own variables have it as `COMSPEC`. The DOS's own
  COMMAND.COM stays where it was and still works when named by its path.
- **The setting is `[shell] command = glos | dos`** (not `comspec = dos`, as the design said: `[shell] comspec`
  is already the DOS's COMMAND.COM's path). `dos`: programs keep the DOS's.
- **Its exit code** is the last program's (what `IF ERRORLEVEL` would see), as COMMAND /C's is under DOS.
- **Its folder:** it starts in DOS's current drive and folder, runs programs there, and leaves DOS where the
  shell ended (`COMMAND /C CD X` changes the folder for whoever asked).
- **Redirection and pipes** inside a line it runs are done with DOS's handles in that process (the file opened,
  the standard handle put aside with 45h, the file's put in its place with 46h, both put back after), so a
  program it starts inherits them; a pipe's temporary files as in S4b.
- **`COMMAND` alone and `/K line`:** lines are read through the program from its standard input, with the
  prompt before each, until `EXIT`. From the keyboard, by DOS's own line input (INT 21h 0Ah). From a file
  (`COMMAND < FILE`), by plain reads: a piece is read, the line taken from it and shown, and the file set back
  to the line's end, so a program the line starts reads on from there. `COMMAND` alone says the shell's version
  first. `PAUSE` and a batch file's question read their key by DOS (08h).
- **Its memory:** about 11 KB of the calling program's: itself (1.2 KB and a stack), 4 KB for the environment of
  the programs it starts, and 4 KB (1.5 KB where memory is short) of its own for the DOS calls' data. It does
  not use the DOS server's 32 KB buffer: a program that shells out may have little memory left, and its child
  needs what there is. (So the design's note on that buffer's owner fell away.)
- **Ctrl+C:** it has its own INT 23h handler. While a program it started runs and has no handler of its own,
  Ctrl+C ends that program (and a batch file goes no further: INT 21h 4Dh says how the program ended). While
  the DOS call is its own, the batch file is stopped and the call goes on.
- **Input that runs dry** (`COMMAND < FILE` with no `EXIT` in the file): the file's end, or a Ctrl+Z in it,
  ends GLOS's COMMAND.COM as `EXIT` does. FreeDOS's own waits for ever there (seen in Loop A).
- **`DISK` and `DD` are refused** from a program's COMMAND.COM: they wait for the DOS server, which cannot run
  until the program that shelled out ends. The other commands of GLOS's own work there.
- **One that goes without leaving** (ended by DOS, or killed with the program that started it): the kernel
  finds it gone when the COMMAND.COM below it, or the resident stub, next asks (`GLOS-COMMAND gone`); its
  session is closed and its DOS requests end with an error (`dos_vm_gone`).
- **Log lines:** `GLOS-COMMAND begin psp= depth= mode=`, `GLOS-COMMAND end psp= code=`, `GLOS-COMMAND gone psp=`.
- **Tested** (`ssh-batch-*`): `COMSPEC` as a program sees it; the C library's `system()` with a built-in, a
  program and its exit code, a batch file that sets a variable for the program it starts and shells out again
  (two deep); `COMMAND < FILE`, `/K`, an input that runs dry; and the batch files of S4a through FreeDOS's
  COMMAND.COM (by its path) and through GLOS's, with the same output.
- **Known differences from the DOS's COMMAND.COM:**
  - `COMMAND /P`, `/E:` and `/Y` are accepted and ignored. No `/F`, no `CTTY`.
  - A program that looks for COMMAND.COM along `PATH` rather than by `COMSPEC` finds the DOS's (which works).
  - GLOS started as DOS's shell (`[shell]` mode) still runs `AUTOEXEC.BAT` and its console through the DOS's
    COMMAND.COM: that is S4d.

### Decided by the owner (2026-11-16)

1. **GLOS's `COMMAND.COM` by default** for the programs GLOS starts; `[shell] comspec = dos` puts the DOS's own
   back for a program that needs it.
2. **MS-DOS 6.22's and 7.1's behaviour** where the COMMAND.COMs differ; FreeDOS's is the tests' comparison where
   they agree, and MS-DOS's side is checked on the benches (bench-g400 and the P3 have MS-DOS 7.1).
3. **GLOS as DOS's shell on the setup floppy and on disks GLOS makes** (`install.py --new-disk`). A PC that has a
   DOS keeps it and its COMMAND.COM.

### What it costs, and what could go wrong

- **Ring 0 grows** by the batch language (my estimate: 8 to 12 KB of code).
- **Batch files are where COMMAND.COMs differ in small ways** (spaces round `==`, `ECHO` with nothing after it,
  `IF ERRORLEVEL` in a `FOR`, a label at the file's end). The comparison in Loop A is what finds them; an
  installer's batch file that trips over one is fixed as it is met.
- **A program that looks inside COMMAND.COM** (its memory, INT 2Eh) finds GLOS's is not it: `comspec = dos`.
- **A PC whose only shell is GLOS** has nothing to fall back on if GLOS cannot start. That is the floppy and a
  disk GLOS made, never a PC that had a DOS.

## Decided by the owner (2026-11-12)

1. **The shell runs in the kernel**, as SSH and SFTP do.
2. **Its own phase in the route, before phase C** (`docs/v1-route.md`, phase B2).
3. **DOS-style names, in either case, and GLOS's system commands are built-ins too.** Those whose names clash
   with a DOS command or program of another meaning are renamed; `GLOS <command>` stays as an alias for the old
   names, so scripts and the test suites go on working. Proposed names (the owner's to change):

   | Today | Built-in | Why renamed |
   |---|---|---|
   | `glos type TEXT`, `glos key CODES`, `glos mouse ...` | `SENDTEXT`, `SENDKEYS`, `SENDMOUSE` | DOS's `TYPE` shows a file; `MOUSE.COM` is the mouse driver |
   | `glos exit` | `SHUTDOWN` | `EXIT` leaves the shell |
   | `glos time` | `CLOCK` | DOS's `TIME` and `DATE` (which the shell has too, setting GLOS's clock) |
   | `glos mem` | `SYSMEM` | `MEM.EXE` shows DOS's memory |
   | `glos net` | `NETWORK` | `NET.EXE` is a network client's |
   | `glos ver`, `glos echo` | `VER` shows GLOS's version and the DOS's; `ECHO` | the same meaning: merged |
   | `glos ps, kill, log, stat, run, config, display, desk, shot, rec, secrets, usb, joy, clip, restart, reboot` ... | the same word | no clash |

4. **Text-mode programs over SSH: later**, as a follow-up. The first version gives a prompt, line commands and
   programs; a program that takes the screen takes the PC's.
