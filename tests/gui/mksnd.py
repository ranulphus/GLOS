#!/usr/bin/env python3
"""The sound files of the GUI scripts' drive (tests/gui/drives/C/SND: player.txt), made once and kept in the tree;
run again only to change them.

    python3 tests/gui/mksnd.py

SONG.MOD: a module of one pattern played four times (its order table names it four times): three notes and a
soft one on a sine of 32 bytes, 30 seconds. TONE.WAV: two seconds of 440 Hz, 8 bits, one channel, 8000 Hz.
NOSOUND.WAV: a text. LIST.M3U: the two, by their names alone (they are beside it).

For player-kinds.txt: TUNE.MID, a MIDI file of one track (a flute's two notes, three seconds); SONGS.MID, one of
the kind that holds songs side by side (type 2: not played); SCREAM.S3M and FAST.XM, the two modules of
tests/host/sndfix (modules.py there), three seconds each."""
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


def num(v):
    b = [v & 0x7F]
    while v > 0x7F:
        v >>= 7
        b.insert(0, 0x80 | (v & 0x7F))
    return bytes(b)


def track(*events):
    body = b"".join(num(d) + e for d, e in events)
    return b"MTrk" + struct.pack(">I", len(body)) + body


# 96 ticks to the quarter note, 120 quarter notes a minute: a tick is 5.208 ms
tune = track((0, bytes([0xFF, 0x03, 6]) + b"A Tune"), (0, bytes([0xC0, 73])), (0, bytes([0x90, 66, 100])), (192, bytes([0x80, 66, 0])),
             (0, bytes([0x90, 73, 100])), (288, bytes([0x80, 73, 0])), (96, bytes([0xFF, 0x2F, 0])))
open(os.path.join(here, "TUNE.MID"), "wb").write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, 96) + tune)
open(os.path.join(here, "SONGS.MID"), "wb").write(b"MThd" + struct.pack(">IHHH", 6, 2, 2, 96) + tune + tune)
fix = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "host", "sndfix")
open(os.path.join(here, "SCREAM.S3M"), "wb").write(open(os.path.join(fix, "PS3M.S3M"), "rb").read())
open(os.path.join(here, "FAST.XM"), "wb").write(open(os.path.join(fix, "PXM.XM"), "rb").read())
print(here)
