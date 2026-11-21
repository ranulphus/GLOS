# When something goes wrong

## A program has stopped answering

Its title says "(not answering)". Give it a moment; if it stays so:

- **Ctrl+Alt+Del** opens the list of programs: choose it and press End Program;
- or **Ctrl+Alt+Shift+Esc** ends the program in front at once.

What the program had not saved is lost. GLOS and the other programs go on.

## A DOS program or game will not leave

**Ctrl+Alt+Shift+Esc** ends it and brings the desktop back.

## A program crashed

GLOS ends the program and goes on. It writes what it knows to a file in the folder CRASH in GLOS's folder
(CRASH001.TXT and so on), which is worth keeping if you report the fault.

## The PC beeps two short notes, over and over

GLOS itself has failed and stopped. Restart the PC. If it happens again in the same place, the last lines of
GLOS's log before it say most.

## GLOS will not start

At DOS's prompt GLOS says why in a line that begins GLOS-REFUSE:

| It says | What it means |
|---|---|
| reason=cpu | The processor is older than a 486 |
| reason=v86 | A memory manager such as EMM386 is running: start DOS without it |
| reason=windows | Windows is running: GLOS starts from plain DOS |
| reason=dpmi or vcpi | Another program has the PC's protected mode already |

From Windows 98, start with "Command prompt only" from the start-up menu, not "Restart in MS-DOS mode".

## There is no desktop, only a prompt

GLOS shows its desktop when GLOS.CFG has a screen mode and starts FILES.EXE with it:

`CONFIG set display mode 640x480x16`

`CONFIG set desktop start FILES.EXE`

and start GLOS again. See [Settings](SETTINGS.MD).

## The screen is black or garbled after a change

A new screen mode chosen in the Control Panel goes back by itself after fifteen seconds if you do not keep
it. If GLOS.CFG itself has a mode the monitor cannot show, edit the file from DOS and set
`mode = 640x480x8`.

## No sound

- Is the loudspeaker on the task bar muted, or the volume down? Open the [Mixer](MIXER.MD).
- The Control Panel's Sound page names the card GLOS found, and plays a test sound. If it found none, GLOS
  does not know the card.
- In a DOS game, choose Sound Blaster 16 in its set-up. See [Sound](SOUND.MD).

## No network

The task bar's network icon and the Control Panel's Network page show whether GLOS found the card and got an
address. No address usually means no cable, or no router answering. A DOS packet driver loaded before GLOS
keeps the card for DOS. See [The network](NETWORK.MD).

## GLOS's log

GLOS notes what it does in a log: the [Monitor](MONITOR.MD)'s Log page shows its last lines, and `LOG` at the
command line all of it. The log is in memory, not in a file: copy it out before restarting if you want to
keep it.
