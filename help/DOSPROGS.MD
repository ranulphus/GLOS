# DOS programs and games

GLOS runs DOS programs as DOS does, on the real PC: nothing is imitated but what a program must not have to
itself.

## Starting one

- Double-click its EXE or COM in [Files](FILES.MD).
- Type its path in the Start menu's search and choose "Run:".
- Type its name at a [Terminal](TERM.MD) prompt.
- Keep your games in [Games](GAMES.MD), which finds them and remembers what you played.
- Give it an icon on the desktop or a place in the Start menu: the Control Panel's Programs page.

## What happens then

A program that only prints text runs beside the desktop. When it wants the screen for itself (it sets a
screen mode, as games do) it gets the **whole screen until it ends**. GLOS's own programs wait meanwhile; the
network, sound and remote logins go on. When the program ends, the desktop comes back as it was.

- One such program runs at a time.
- There is no key that switches back to the desktop while it runs: leave the program as you would in DOS.
- **Ctrl+Alt+Shift+Esc** ends a program that will not leave.
- Ctrl+Alt+Del does not restart the PC while GLOS runs.

## A program's own settings

The Control Panel's **Programs** page keeps settings for a program by its file's name:

- its title, icon, arguments and starting folder;
- whether it is in the Start menu and on the desktop;
- *Screen:* "full" for a program that should always have the whole screen;
- *Memory:* the most it may take;
- *Direct mode:* for the few programs that must have the PC's hardware entirely to themselves. In direct mode
  the program has the real sound card, and GLOS may not answer the network while the program keeps the PC
  busy.

## Sound

DOS programs find a **Sound Blaster 16** at the usual place (port 220, IRQ 5, DMA 1 and 5, unless DOS's
BLASTER setting says otherwise), whatever card the PC has that GLOS can play through: choose Sound Blaster 16
in a game's set-up. GLOS mixes the game's sound
with everything else. FM music needs a card with a real FM chip. More in [Sound](SOUND.MD).

## Game pads, joysticks and the mouse

USB game pads and Xbox pads appear to DOS games as an ordinary joystick on the game port. A PS/2 or USB mouse
appears as a PS/2 mouse; a DOS program still needs its mouse driver, as in DOS. More in
[Keyboards, mice and game pads](PADS.MD).
