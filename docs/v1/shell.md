# GLOS's shell: one command interpreter, reached two ways

**Status: a design for the owner's review (2026-11-12). Nothing here is built, and nothing in the Terminal app or
in SSH changes until it is agreed.** The disk-level code under the new tools (S0 below) is being built meanwhile:
every version of this design needs it.

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
| S3 | `DF`, `DU`, `DD`, `DISK`; the setup floppy without FreeDOS's FDISK and FORMAT | `loopa-floppy`; the 486 |
| S4 | The batch language; GLOS's `COMMAND.COM`; `AUTOEXEC.BAT` run by GLOS; the floppy with FreeDOS's kernel alone | Batch files against FreeDOS's COMMAND.COM; `loopa-floppy` |
| later | GLOS's own DOS services (files, FAT), then no DOS underneath | its own plan |

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

### In what order, inside S2

| | What | Shows it works |
|---|---|---|
| S2a | Sessions, the kernel's host, the line editor; `ssh pc` logs in | Loop A: a script typed through `ssh -tt` (commands, a program, history, Tab, Ctrl+C), its transcript; host tests of the editor |
| S2b | `ssh pc COMMAND` through the shell; GLOS's commands as the host's table, with their new names and `GLOS NAME` | Every suite as it is (they all speak through `ssh pc COMMAND`) |
| S2c | The CONSOLE call; the Terminal app a terminal only; SSH, SFTP, SSH-KEYGEN programs | The Terminal's scripts in Loop A, the same lines; `loopa-sshc` |
| S2d | The session on the PC's own screen | Loop A: keys typed at the emulated keyboard, the screen read back |

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
