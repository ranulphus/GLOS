#!/usr/bin/env python3
"""The folders of "games" on the GUI scripts' drive (tests/gui/drives/C/GAMES: games.txt, games-run.txt), made once
and kept in the tree; run again only to change them.

    python3 tests/gui/mkgames.py

Nothing of any game is here: files that only have well-known games' names, for the Games Library to know them by
(apps/shared/gamedb.c). Each "program" is two bytes that end it at once should it ever be run (INT 20h) and a
line of text; BLIP.COM is a real program of five bytes that ends with the code 7 (the scripts play it)."""
import os

here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "drives", "C", "GAMES")
text = b"A placeholder for GLOS's tests: only its name matters.\r\n"
program = b"\xCD\x20" + text                    # int 20h: back to DOS
files = {
    "DOOM/DOOM.EXE": program, "DOOM/DOOM1.WAD": text, "DOOM/SETUP.EXE": program,       # a known game, by its two files
    "KEEN/KEEN4E.EXE": program,                                                          # ... by its program alone
    "TYCOON/TYCOON.EXE": program, "TYCOON/TTDX.EXE": program,                            # ... the one of two its companion tells
    "ZAPPER/ZAPPER.EXE": program, "ZAPPER/INSTALL.EXE": program, "ZAPPER/README.TXT": text,   # no known game: by its folder
    "BLIP/BLIP.COM": bytes([0xB8, 0x07, 0x4C, 0xCD, 0x21]),                             # mov ax, 4C07h; int 21h
    "NOTES/README.TXT": text,                                                            # no game at all
}
for name, data in files.items():
    path = os.path.join(here, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "wb").write(data)
print(here)
