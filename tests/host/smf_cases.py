#!/usr/bin/env python3
"""MIDI files for tests/host/smf_test.c (apps/shared/smf.c), written here byte by byte: small files whose every
message and its time are known.

    smf_cases.py DIR      writes DIR/*.mid; prints DIR"""
import os, struct, sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)


def num(v):
    """a number of the file's own kind: seven bits a byte"""
    b = [v & 0x7F]
    while v > 0x7F:
        v >>= 7
        b.insert(0, 0x80 | (v & 0x7F))
    return bytes(b)


def meta(kind, data):
    return bytes([0xFF, kind]) + num(len(data)) + data


def tempo(us):
    return meta(0x51, struct.pack(">I", us)[1:])


END = meta(0x2F, b"")


def track(*events):
    """events: (ticks since the last, bytes)"""
    body = b"".join(num(d) + e for d, e in events)
    return b"MTrk" + struct.pack(">I", len(body)) + body


def midi(kind, div, *tracks):
    return b"MThd" + struct.pack(">IHHH", 6, kind, len(tracks), div) + b"".join(tracks)


def write(name, data):
    open(os.path.join(out, name), "wb").write(data)


# one track, 96 ticks to the quarter note, the first tempo 120 to the minute (a tick 5.208 ms): a name, a
# program, a note on and (by a note on with no force, its first byte left out) off, then the tempo halved (a tick
# 10.417 ms) and another note, a system message between them, and a quarter note of nothing before the end
one = track((0, meta(0x03, b"A Tune  ")), (0, meta(0x01, b"words for the reader")), (0, bytes([0xC0, 73])),
            (0, bytes([0x90, 66, 100])), (96, bytes([66, 0])), (0, tempo(1000000)), (0, bytes([0xF0, 3, 0x7E, 0x7F, 0xF7])),
            (96, bytes([0x91, 60, 90])), (48, bytes([0x81, 60, 64])), (96, END))
write("one.mid", midi(0, 96, one))
# the same in a RIFF wrapper, with a chunk before it
riff = b"RMID" + b"LIST" + struct.pack("<I", 5) + b"abcde\0" + b"data" + struct.pack("<I", len(midi(0, 96, one))) + midi(0, 96, one)
write("one.rmi", b"RIFF" + struct.pack("<I", len(riff)) + riff)
# three tracks together: the tempo's (150 to the minute from the start: a tick of 480 is 0.8333 ms; 75 from tick 960)
# and two of notes, whose messages at one time come in the tracks' order; a chunk that is no track between them
write("three.mid", midi(1, 480,
                        track((0, meta(0x03, b"Three")), (0, tempo(400000)), (960, tempo(800000)), (960, END)),
                        b"XTRA" + struct.pack(">I", 3) + b"abc",
                        track((0, meta(0x03, b"not the song's")), (0, bytes([0xC1, 5])), (480, bytes([0x91, 64, 80])),
                              (480, bytes([0x81, 64, 0])), (480, bytes([0x91, 65, 80])), (240, bytes([0x81, 65, 0])), (0, END)),
                        track((0, bytes([0xB2, 7, 100])), (480, bytes([0x92, 40, 70])), (0, bytes([44, 70])),
                              (1440, bytes([0xE2, 0, 64])), (0, bytes([0x82, 40, 0])), (0, bytes([44, 0])), (0, END))))
# ticks by film time: 25 frames a second, 40 ticks a frame (a tick is a millisecond); its tempo says nothing
write("film.mid", midi(0, (256 - 25) << 8 | 40, track((0, tempo(250000)), (500, bytes([0x90, 60, 100])),
                                                       (1500, bytes([0x80, 60, 0])), (250, END))))
# cut in its second note: what there is of it; in its header: nothing
whole = midi(0, 96, one)
write("cut.mid", whole[:whole.index(bytes([0x91, 60, 90])) + 2])
write("cuthead.mid", whole[:12])
# songs side by side (type 2): not played; no MIDI file; a header with no tracks after it
write("two.mid", midi(2, 96, one, one))
write("nomidi.mid", bytes((i * 37 + 11) & 0xFF for i in range(3000)))
write("notrack.mid", midi(1, 96))
print(out)
