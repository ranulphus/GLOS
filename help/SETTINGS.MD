# Settings

The [Control Panel](CONTROL.MD) is the easy way to change a setting. All of them are lines in one text file,
**GLOS.CFG**, in GLOS's own folder (C:\GLOS if you put it there), and can be changed there too: with
[Notepad](NOTEPAD.MD), with DOS's EDIT when GLOS is not running, or from the command line:

`CONFIG set desktop theme dark`

`CONFIG get display mode`

## How the file is written

A name in square brackets begins a section; under it, lines of `name = value`. A line that begins with `;` is
a note.

```
[display]
mode = 800x600x16

[desktop]
start = FILES.EXE
theme = slate
```

## The ones most often wanted

| Section and name | Values | What it is |
|---|---|---|
| display mode | 640x480x8, 800x600x16, 1024x768x32 ... or none | The screen. "none" is no desktop: the command line alone |
| display refresh | auto, 60, 70, 75, 85 | The screen's refresh rate (Hz), where the card can set one and the monitor takes it. auto is the card's own |
| desktop start | FILES.EXE | What starts with the desktop. Without FILES.EXE there are no icons and no Start menu |
| desktop theme | slate, dark, contrast | The colours |
| desktop text | small, normal, large | The size of text |
| desktop wallpaper | a picture's path | The picture behind the icons |
| desktop colour | theme, or RRGGBB (as 204060) | The desktop's colour where no picture covers it: the quickest on slow PCs |
| desktop bar | bottom, top | Where the task bar is |
| usb ehci | off, on | A USB 2.0 disk at high speed (on); off: USB 1.1's speed for every device |
| region layout | us, uk | The keyboard |
| region codepage | dos, 437, 850, 852, 866, 737 | DOS's letters beyond English |
| region date | yyyy-mm-dd, dd/mm/yyyy, dd.mm.yyyy, mm/dd/yyyy | How dates are written |
| time zone | UTC0, GMT0BST,M3.5.0/1,M10.5.0 ... | The time zone, with its summer time |
| sound volume | 0 to 100 | The main volume |
| sound virtual | on, off | The Sound Blaster that DOS programs find |
| net hostname | a name | The PC's name on the network |
| saver wait | minutes, or off | When the screen saver starts |
| saver saver | blank, stars, pipes, text, objects, maze | Which one |
| boot default | glos, dos, partition 1 to 8 | What the start-up menu of a PC that starts GLOS by itself starts when nobody chooses |
| boot timeout | seconds (0 to 60), or off | How long that menu waits |

Some changes take effect at once, some when GLOS next starts. The Control Panel says which, for each.

## A program's settings

A DOS program's own settings are a section named after its file:

```
[program DOOM.EXE]
title = Doom
menu = 1
screen = full
```

The Control Panel's Programs page writes these for you. See [DOS programs and games](DOSPROGS.MD).
