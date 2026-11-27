# GLOS's command-line tools: all of DOS, the common Linux ones, an editor, the network

**Status: agreed by the owner (2026-11-27, the four answers under "Decided by the owner"); C1 next.** The owner, 2026-11-27: "We've not talked
about CLI utilities much and we need to make sure we have everything covered from DOS and some modern Linux-esque
equivalents too. We need some kind of text editor, like EDIT.COM or nano, and we need full network management
capability. So all of DOS, plus common Linux equivalents."

Why it matters now: a native GLOS (phase G, [native-dos.md](native-dos.md)) ships no FreeDOS, so every DOS
external command a user expects (FIND, SORT, FC, EDIT, TREE ...) is missing there unless GLOS has it. On a hosted
GLOS the DOS's own commands still run, but the shell's built-ins answer first for the ones it has.

## What there is today (main 0db53e6)

- **The shell's built-ins** (`sdk/shell/shell.c`, [shell.md](shell.md)): BREAK, CALL, CD/CHDIR, CHCP, CHOICE, CLS,
  COPY, CTTY, DATE, DEL/ERASE, DIR, ECHO, EXIT, FOR, GOTO, IF, LH/LOADHIGH, MD/MKDIR, PATH, PAUSE, PROMPT, RD/RMDIR,
  REM, REN/RENAME, SET, SHIFT, TIME, TRUENAME, TYPE, VER, VERIFY, VOL; and beyond COMMAND.COM: ATTRIB, XCOPY, MOVE,
  DELTREE, MORE, START, WHERE, HISTORY, HELP, DF, DU. Redirection and pipes are the shell's own.
- **GLOS's own commands** (the same names over SSH, in the Terminal and on the console): PS, KILL, LOG, STAT, RUN,
  CONFIG, DISPLAY, DESK, SHOT, REC, SECRETS, USB, JOY, CLIP, AUDIO, CACHE, IDLE, RESTART, REBOOT, SHUTDOWN, CLOCK,
  SYSMEM, NETWORK, DISK (partitions, FORMAT, INSTALL), DD, PCI, MTRR, SERVICES, DEVICES, NDOS, CDROM, SENDTEXT,
  SENDKEYS, SENDMOUSE.
- **Programs**: SSH, SFTP, SSHKEY, TELNET, SERIAL, FETCH (HTTP/HTTPS to a file), PKG; and the desktop's apps
  (Notepad, Files, Disk Tools, Monitor ...).
- **Pieces to reuse**: deflate (`apps/shared/deflate.h`, the PDF Viewer's), ZIP reading (`apps/pkg/zip.c`), the
  FAT checker (`kernel/fs/fat.c`, Disk Tools'), GLOS's resolver, lwIP's raw API (ICMP, UDP, TCP), the SHA-256 of
  TinySSH's crypto, the line editor (`sdk/shell/edit.c`).
- **The network from the command line**: only `NETWORK` (the card, its address, its counters) and
  `NETWORK resolve NAME`; settings through `CONFIG` ([net] keys, applied at the next start). No ping, route,
  connections list, DHCP renew, or live change.

## 1. All of DOS (MS-DOS 7.1's commands and DOS 6.22's that 7.1 dropped)

| Command | Today | Proposed |
|---|---|---|
| ATTRIB, XCOPY, MOVE, DELTREE, MORE, CHOICE, START | built-in | as is |
| FIND, SORT | built-in (C1a, 2026-11-27: [shell.md](shell.md)) | FIND's /V /C /N /I; SORT's /R /+n, pipe-friendly |
| FC, COMP | built-in (C1b, 2026-11-27: [shell.md](shell.md)) | FC (text: /C /N /W; binary /B), COMP as FC /B's other face |
| TREE | built-in (C1c, 2026-11-27: [shell.md](shell.md)) | built-in (/F, /A) |
| LABEL | built-in (C1c; setting a label needs the kernel's host's `setlabel`) | built-in (FAT volume label) |
| SUBST | missing | built-in, if GLOS's drive table can map a letter to a folder (native first) |
| MEM | built-in (C1c; its figures need the kernel's host's `mem`, `mem_blocks`), SYSMEM | MEM as a name for SYSMEM, MS-DOS's /C /F output where it makes sense |
| CHKDSK, SCANDISK | Disk Tools only | CHKDSK [d:] [/F] on the kernel's checker; SCANDISK an alias |
| FORMAT, FDISK, SYS | DISK | the DOS names lead to DISK's verbs (DISK n FORMAT, DISK n PARTITIONS, DISK n INSTALL), with the same "what it will destroy" questions |
| DISKCOPY, DISKCOMP | missing | via DD and FC /B on floppies, or small built-ins |
| MODE | built-in (C1c) | MODE CON (columns/lines), MODE COMn (baud), MODE LPT; the rest says so |
| KEYB | built-in (C1c), CONFIG region | KEYB xx (layout now and saved) |
| DOSKEY | HISTORY | macros (DOSKEY name=text), /HISTORY |
| EDIT | missing | the editor (section 3) |
| EXPAND, EXTRACT | missing | EXPAND for single .CAB/compressed files; ZIP/UNZIP (section 2) cover archives |
| REPLACE, UNDELETE, DEFRAG, PRINT | missing | REPLACE built-in; UNDELETE and DEFRAG: no (FAT risks, little use); PRINT: later with printing |
| DEBUG, QBASIC, MSD | missing | DEBUG: no (a program's, not a shell's); QBASIC: no; MSD: STAT and DEVICES already say it |
| HIMEM, EMM386, MSCDEX, SHARE, SETVER, NLSFUNC, SMARTDRV | drivers | GLOS is each of these itself (XMS, EMS, CD-ROM, cache, code pages): nothing to add |

## 2. The common Linux ones

Two ways to have them (owner's choice, question 1): names that lead to the DOS command (`ls` is `DIR`), or small
tools with the Linux flags and output people type from habit. Either way a Linux name never hides a DOS one
(`find` is Linux's, `FIND` DOS's: the shell is case-blind, so `find` stays DOS's FIND and the Linux one is reached as
`FINDF`, or only with Linux-style arguments: to be settled).

| Linux | Nearest today | Proposed |
|---|---|---|
| ls, cat, cp, mv, rm, mkdir, rmdir, touch, pwd | DIR, TYPE, COPY, MOVE, DEL, MD, RD, -, CD | ls (-l -a -R), cat, cp (-r), mv, rm (-r -f), touch, pwd |
| grep | - | grep (-i -n -v -r -c -l, fixed strings and simple regex) |
| head, tail, wc, uniq, cut, tr, tee | - | each, small, pipe-friendly; tail -f for a growing log |
| less | MORE | less: MORE with paging back and search |
| diff | - | diff -u (text), beside FC |
| find, which | WHERE | find PATH -name PAT [-type f/d], which = WHERE |
| ps, kill, top | PS, KILL, Monitor | top: a text Monitor (threads, sessions, memory) refreshed |
| df, du, free, uptime, uname, hostname | DF, DU, SYSMEM, STAT, VER | free, uptime, uname -a, hostname as names on those |
| env, export, alias, history, man | SET, -, -, HISTORY, HELP | env, alias (= DOSKEY macros), man = HELP's pages |
| date, sleep, watch, clear | DATE, -, -, CLS | sleep N, watch -n N cmd, clear |
| sha256sum, md5sum, hexdump/xxd | - | sha256sum, md5sum, xxd |
| tar, zip, unzip, gzip, gunzip | PKG reads ZIPs | ZIP/UNZIP (deflate already here), gzip/gunzip, tar (-c -x -t -z) |
| nano, vi | Notepad (desktop) | the editor (section 3); vi: no |
| curl, wget | FETCH | wget and curl -o as faces of FETCH (GET; curl -I, -X POST later) |
| ssh, scp, sftp, telnet, nc | SSH, SFTP, TELNET | scp (on SFTP's code), nc (TCP connect/listen) |

## 3. A text editor

One full-screen editor, `EDIT`, that runs everywhere the shell does: the PC's own console (text mode), the
Terminal app and SSH (VT100). Proposed look and keys (owner's choice, question 2):

- **EDIT.COM's look**: a menu bar (File, Edit, Search, Options, Help) by Alt or F10, mouse where there is one,
  shift+arrows to select, Ctrl+Ins/Shift+Ins/Shift+Del, a status line with the line and column;
- **nano's keys too**: Ctrl+O save, Ctrl+X exit, Ctrl+W find, Ctrl+K cut a line, Ctrl+U paste, Ctrl+G help, and
  the shortcut bar at the bottom when asked (`EDIT -n` or a setting).
- Files of any size the memory holds, CR LF or LF kept as found, tabs shown, code page 437/850 text, a file
  read-only when it is; Unicode not (DOS's code pages).
- Built on GLOS's terminal layer (the same cells and keys as the shell's line editor), so it is one program for
  the three screens. A program (EDIT.EXE beside GLOS) or a built-in: a program (it is big, and starts fast).

## 4. Network management

Everything the Control Panel's Network page does, and the usual tools, from the command line; changes apply at once
and are saved to GLOS.CFG (as CONFIG does), with the DOS/Windows names and the Linux ones on the same tools (owner's
choice, question 3):

| Windows / DOS | Linux | What |
|---|---|---|
| IPCONFIG [/ALL] [/RENEW] [/RELEASE] | ip addr, ifconfig | the cards, addresses, gateway, DNS, DHCP lease; renew/release now |
| NETSH-style: IPCONFIG /SET (or NETWORK SET) | ip addr add, ip route | a static address, mask, gateway, DNS, or DHCP again: live, and saved |
| PING host [-n N] [-l size] | ping -c N | ICMP echo through lwIP's raw API |
| TRACERT host | traceroute | TTL-stepped UDP/ICMP |
| NSLOOKUP name [server] | host, dig (short) | GLOS's resolver, and a server asked directly |
| NETSTAT [-a] [-n] [-r] | ss, netstat | TCP/UDP sockets (GLOS's and apps'), listeners, the route |
| ROUTE PRINT / ADD / DELETE | ip route | the gateway (lwIP has one route per card, plus default) |
| ARP -a [-d] | ip neigh | lwIP's ARP table |
| HOSTNAME [name] | hostname | the name now (mDNS) and saved |
| - | nc host port, nc -l port | TCP connect/listen for tests and transfers |
| CLOCK sync | ntpdate | SNTP now (CLOCK has it) |
| - | wget, curl | FETCH (section 2) |

Also: the cable link (SLIP/PLIP) and SSH's forwarding shown and changed the same way; a firewall is not proposed
(GLOS listens only where its services say: SERVICES and NETSTAT -a show them).

## Decided by the owner (2026-11-27, question tool)

1. **Linux names: "Real tools, Linux flags"**: small tools with the flags and output people type from habit (ls -la,
   grep -rin, tail -f, diff -u), sharing code with the DOS ones; where a name clashes with DOS, DOS keeps it.
2. **The editor: "EDIT's look + nano's keys"**: EDIT.COM's menus, mouse and selection, and nano's keys too, its
   shortcut bar on request.
3. **The network: "Both names, live + saved"**: the Windows/DOS names and the Linux ones on the same tools; changes
   apply at once and are saved to GLOS.CFG.
4. **When: "DOS+editor+network before UAT"**: C1, C2 and C3 join phase G's must-fix list before UAT; C4 (archives)
   and C5 (the Linux set) right after, before v1's release.

## In what order (proposed)

1. **C1, the DOS gaps a native GLOS feels first**: FIND, SORT, FC, TREE, LABEL, MEM, CHKDSK, MODE, KEYB, DOSKEY
   macros, FORMAT/FDISK/SYS names on DISK.
2. **C2, the editor** (EDIT).
3. **C3, the network tools** (IPCONFIG with live changes, PING, TRACERT, NSLOOKUP, NETSTAT, ROUTE, ARP, HOSTNAME,
   nc).
4. **C4, archives and checks**: ZIP/UNZIP, gzip, tar, sha256sum/md5sum, xxd, EXPAND.
5. **C5, the Linux set** (as question 1 decides): ls, cat, grep, head/tail, wc, less, diff, find, top, ... and
   wget/curl, scp.

Each step: built-ins where small (the shell's library, shared by the console, the Terminal and SSH), programs where
big (EDIT, the archivers); MS-DOS 7.1's output followed where the reference disk has the command (DOSDIFF, as
ATTRIB and XCOPY were); host tests for each tool in `tests/shell/`, and a Loop A case per step.
