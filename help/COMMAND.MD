# The command line

GLOS has one command line everywhere: in each tab of the [Terminal](TERM.MD), when you log in from another
computer, and on the PC's own screen when there is no desktop. It reads DOS's commands as DOS does.

## At the prompt

- Up and Down bring back lines you typed; Tab completes a name.
- `&&` and `||` run the next command if this one worked, or if it failed. `<`, `>`, `>>` and `|` send input and
  output to files and to other commands. `%NAME%` is a variable; `%ERRORLEVEL%` is the last command's result.
- A program's name runs it. A program that wants the whole screen takes it until it ends.
- `START program` runs a program beside the Terminal.

## Commands

| For | Commands |
|---|---|
| Files and folders | CD, DIR, COPY, XCOPY, MOVE, REN, DEL, DELTREE, ATTRIB, MD, RD, TYPE, MORE, VOL, DF, DU, WHERE, TRUENAME |
| The prompt | ECHO, SET, PATH, PROMPT, CLS, VER, HELP, HISTORY, DATE, TIME, CHCP, EXIT |
| Batch files | IF, GOTO, CALL, FOR, SHIFT, PAUSE, REM |
| GLOS itself | PS, KILL, LOG, STAT, CONFIG, DISPLAY, AUDIO, NETWORK, USB, JOY, CLIP, SHOT, SERVICES |
| Leaving | SHUTDOWN, RESTART, REBOOT |

`HELP` lists them all.

- **PS** lists what is running, **KILL** ends one of them.
- **LOG** shows GLOS's own log (`LOG --follow` keeps showing it).
- **CONFIG** reads and changes [settings](SETTINGS.MD): `CONFIG set desktop theme dark`.
- **DISPLAY** changes the screen mode: `DISPLAY on 800x600x16`.
- **XCOPY** copies files, and with `/S` the folders in them too (`/E`: empty ones as well, `/H`: hidden files
  too): `XCOPY C:\GAMES\DOOM D:\DOOM /E`. It doesn't ask before writing over a file.
- **DELTREE** deletes a folder and everything in it, after asking (`/Y`: without asking).
- **MORE** shows a file a screen at a time (`MORE file`, or `command | MORE`): any key for the next, Q to stop.

## Batch files

A BAT file is run by GLOS's command line itself: `IF`, `GOTO`, `CALL`, `FOR`, `SHIFT`, `PAUSE`, `%0` to `%9`
and `ECHO OFF` work as in DOS. Ctrl+C asks whether to stop it.

## Leaving

- **EXIT** ends this command line (a Terminal tab).
- **SHUTDOWN** ends GLOS and returns to DOS.
- **RESTART** ends GLOS so that a loop in AUTOEXEC.BAT can start it again.
- **REBOOT** restarts the PC.

## Starting GLOS

Type `C:\GLOS\GLOS.EXE` at DOS's prompt, or put that line in AUTOEXEC.BAT. GLOS can also be DOS's own command
line: `SHELL=C:\GLOS\GLOS.EXE /SHELL` in CONFIG.SYS.
