#!/usr/bin/env python3
"""The sound files of the GUI scripts' drive (tests/gui/drives/C/SND: player.txt), made once and kept in the tree;
run again only to change them.

    python3 tests/gui/mksnd.py

SONG.MOD: a module of one pattern played four times (its order table names it four times): three notes and a
soft one on a sine of 32 bytes, 30 seconds. TONE.WAV: two seconds of 440 Hz, 8 bits, one channel, 8000 Hz.
NOSOUND.WAV: a text. LIST.M3U: the two, by their names alone (they are beside it)."""
import math, os, struct

here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "drives", "C", "SND")
os.makedirs(here, exist_ok=True)
SINE = bytes(int(round(127 * math.sin(2 * math.pi * i / 32))) & 0xFF for i in range(32))


def cell(sample=0, period=0, effect=0, param=0):
    return bytes([(sample & 0xF0) | period >> 8, period & 0xFF, (sample & 15) << 4 | effect, param])


b = bytearray(b"Four Notes".ljust(20, b"\0"))
b += b"a sine".ljust(22, b"\0") + struct.pack(">HBBHH", 16, 0, 64, 0, 16)
for _ in range(30):
    b += bytes(22) + struct.pack(">HBBHH", 0, 0, 0, 0, 1)
b += bytes([4, 127]) + bytes(128) + b"M.K."
notes = {0: cell(1, 428), 16: cell(1, 214), 32: cell(1, 856), 48: cell(1, 428, 12, 16)}
for r in range(64):
    b += notes.get(r, cell()) + cell() * 3
b += SINE
open(os.path.join(here, "SONG.MOD"), "wb").write(bytes(b))

tone = bytes(128 + int(round(100 * math.sin(2 * math.pi * 440 * i / 8000))) for i in range(16000))
wav = b"WAVEfmt " + struct.pack("<IHHIIHH", 16, 1, 1, 8000, 8000, 1, 8) + b"data" + struct.pack("<I", len(tone)) + tone
open(os.path.join(here, "TONE.WAV"), "wb").write(b"RIFF" + struct.pack("<I", len(wav)) + wav)
open(os.path.join(here, "NOSOUND.WAV"), "wb").write(b"This file is named as a sound and is none.\r\n")
open(os.path.join(here, "LIST.M3U"), "wb").write(b"# a playlist: a file a line\r\nSONG.MOD\r\nTONE.WAV\r\n")
print(here)
