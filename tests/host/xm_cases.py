#!/usr/bin/env python3
"""FastTracker 2 modules for tests/host/xm_test.c (apps/shared/xm.c), written here byte by byte: small songs whose
every note is known, so that the test can say what must be heard (which pitch when, how loud, for how long).

    xm_cases.py DIR      writes DIR/*.xm; prints DIR

The sample of most of them is one cycle of a sine in 32 bytes, looped, whose middle C (C-4, the note 49) is 8363
samples a second: a tone of 261.34 Hz; C-5 twice that, C-3 half."""
import math, os, struct, sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
WAVE = [int(round(127 * math.sin(2 * math.pi * i / 32))) for i in range(32)]
BLIP = WAVE * 30 + [0] * 40                                     # 1000 samples, no loop: a note that ends by itself
RAMP = [(i - 32) * 4 for i in range(64)]
C3, C4, C5, C6, OFF = 37, 49, 61, 73, 97
G, H, K, L, P, R, T, X = 16, 17, 20, 21, 25, 27, 29, 33         # the commands after F


def sample(wave=WAVE, volume=64, loop=1, bits16=False, fine=0, pan=128, rel=0):
    """loop: 0 none, 1 forwards, 2 back and forth (the whole of it)"""
    return dict(wave=wave, volume=volume, loop=loop, bits16=bits16, fine=fine, pan=pan, rel=rel)


def inst(*samples, keys=None, vol=None, panenv=None, fade=0, vib=(0, 0, 0, 0)):
    """keys: which sample each of the 96 notes plays; vol, panenv: (points, sustain, loop from, loop to, type)"""
    return dict(samples=list(samples), keys=keys or [0] * 96, vol=vol, pan=panenv, fade=fade, vib=vib)


def cell(note=0, ins=0, vol=0, fx=0, par=0):
    return (note, ins, vol, fx, par)


def rows(n, *pairs):
    p = [dict() for _ in range(n)]
    for r, cells in pairs:
        p[r] = cells
    return p


def pack(pattern, channels, plain):
    b = bytearray()
    for row in pattern:
        for ch in range(channels):
            c = row.get(ch, cell())
            if plain:                                           # all five bytes (a note must be there for it)
                b += bytes(c if c[0] else (0x80,))
            else:
                bits = sum(1 << i for i in range(5) if c[i])
                b += bytes([0x80 | bits] + [c[i] for i in range(5) if c[i]])
    return bytes(b)


def envelope(e):
    points, sustain, start, end, kind = e or ([], 0, 0, 0, 0)
    b = b"".join(struct.pack("<HH", x, y) for x, y in points).ljust(48, b"\0")
    return b, len(points), sustain, start, end, kind


def deltas(wave, bits16):
    b, last = bytearray(), 0
    for v in wave:
        v = v * 256 if bits16 else v
        b += struct.pack("<H", (v - last) & 0xFFFF) if bits16 else bytes([(v - last) & 0xFF])
        last = v
    return bytes(b)


def song(patterns, orders, insts=None, channels=2, title=b"test song", speed=6, tempo=125, linear=True, version=0x0104,
         plain=False):
    insts = insts if insts is not None else [inst(sample())]
    b = bytearray(b"Extended Module: " + title.ljust(20, b"\0") + b"\x1a" + b"GLOS's tests".ljust(20) + struct.pack("<H", version))
    b += struct.pack("<IHHHHHHHH", 276, len(orders), 0, channels, len(patterns), len(insts), 1 if linear else 0, speed, tempo)
    b += bytes(orders).ljust(256, b"\0")
    for p in patterns:
        packed = pack(p, channels, plain)
        b += struct.pack("<IBHH", 9, 0, len(p), len(packed)) + packed
    for i, ins in enumerate(insts):
        n = len(ins["samples"])
        name = (b"instrument %d" % (i + 1)).ljust(22, b"\0")
        if not n:
            b += struct.pack("<I", 33) + name + b"\0" + struct.pack("<HI", 0, 40)
            continue
        vp, vn, vs, vf, vt, vk = envelope(ins["vol"])
        pp, pn, ps, pf, pt, pk = envelope(ins["pan"])
        b += struct.pack("<I", 263) + name + b"\0" + struct.pack("<HI", n, 40) + bytes(ins["keys"]) + vp + pp
        b += bytes([vn, pn, vs, vf, vt, ps, pf, pt, vk, pk, *ins["vib"]]) + struct.pack("<H", ins["fade"]) + bytes(22)
        datas = [deltas(s["wave"], s["bits16"]) for s in ins["samples"]]
        for s, d in zip(ins["samples"], datas):
            b += struct.pack("<IIIBbBBbB", len(d), 0, len(d) if s["loop"] else 0, s["volume"], s["fine"],
                             s["loop"] | (16 if s["bits16"] else 0), s["pan"], s["rel"], 0) + b"a sample".ljust(22, b"\0")
        for d in datas:
            b += d
    return bytes(b)


def write(name, data):
    open(os.path.join(out, name), "wb").write(data)


END = cell(fx=13)                                               # a break to an order there is none of: the end
blip = [inst(sample(BLIP, loop=0))]

# three notes of sixteen rows each, then one softer (by its volume column); with FastTracker's periods, and the Amiga's
notes = [rows(64, (0, {0: cell(C4, 1)}), (16, {0: cell(C5, 1)}), (32, {0: cell(C3, 1)}), (48, {0: cell(C4, 1, 0x20)}),
              (63, {0: END}))]
write("notes.xm", song(notes, [0]))
write("amiga.xm", song(notes, [0], linear=False))
# samples that are all on one side
write("sides.xm", song([rows(64, (0, {0: cell(C4, 1)}), (8, {0: cell(C4, 2)}), (16, {0: END}))], [0],
                       insts=[inst(sample(pan=0)), inst(sample(pan=255))]))
# speeds: 32 rows of six ticks, then (F03) 32 of three; a faster tempo (F96: 150); both from the header; and F00: the end
write("speed.xm", song([rows(64, (0, {0: cell(C4, 1)}), (32, {0: cell(fx=15, par=3)}))], [0]))
write("tempo.xm", song([rows(64, (0, {0: cell(C4, 1, fx=15, par=150)}))], [0]))
write("header.xm", song([rows(64, (0, {0: cell(C4, 1)}))], [0], speed=3, tempo=150))
write("stop.xm", song([rows(64, (0, {0: cell(C4, 1)}), (9, {0: cell(fx=15)}))], [0]))
# a break into the second pattern's row 8, a jump back to the start from its row 15: the song goes round there
write("jump.xm", song([rows(64, (0, {0: cell(C4, 1)}), (7, {0: cell(fx=13, par=0x08)})),
                       rows(64, (8, {0: cell(C5, 1)}), (15, {0: cell(fx=11)}))], [0, 1]))
# an order that names no pattern is 64 empty rows; then a pattern of 16 rows, eight channels, its cells not packed
write("rows.xm", song([rows(16, (0, {0: cell(C4, 1), 5: cell(C5, 1)}))], [3, 0], channels=8, plain=True))
# rows 2 to 5 three times (E60, E62), then on to row 7 and out
write("loop.xm", song([rows(64, (0, {0: cell(C4, 1)}), (2, {1: cell(fx=14, par=0x60)}), (5, {1: cell(fx=14, par=0x62)}),
                            (7, {0: END}))], [0]))
# a row three times as long (EE2), its note struck once; then out
write("delay.xm", song([rows(64, (0, {0: cell(C4, 1, fx=14, par=0xE2)}), (1, {0: END}))], [0], insts=blip))
# a sample with no loop: the note ends by itself (1000 samples at 8363 a second)
write("blip.xm", song([rows(64, (0, {0: cell(C4, 1)}), (15, {0: END}))], [0], insts=blip))
# ... from 512 samples in (902), whole, from there again (900), and from past its end (9FF: not at all)
write("offset.xm", song([rows(64, (0, {0: cell(C4, 1, fx=9, par=2)}), (4, {0: cell(C4, 1)}), (8, {0: cell(C4, 1, fx=9)}),
                              (12, {0: cell(C4, 1, fx=9, par=0xFF)}), (16, {0: END}))], [0], insts=blip))
# slides: up (105: 20 a tick) for eleven rows; to a note (308) and no further; down (205) for five rows; a fine one up
# (E1F: 60, once)
slide = [rows(64, (0, {0: cell(C4, 1)}), *[(r, {0: cell(fx=1, par=5)}) for r in range(1, 12)],
              (16, {0: cell(C4, 1)}), (17, {0: cell(C5, fx=3, par=8)}), *[(r, {0: cell(fx=3)}) for r in range(18, 32)],
              (40, {0: cell(C4, 1)}), (41, {0: cell(fx=2, par=5)}), *[(r, {0: cell(fx=2)}) for r in range(42, 46)],
              (52, {0: cell(C4, 1, fx=14, par=0x1F)}), (60, {0: END}))]
write("slide.xm", song(slide, [0]))
write("amislide.xm", song(slide, [0], linear=False))
# a chord by arpeggio (047: the note, a major third and a fifth above, a tick each)
arp = [rows(64, (0, {0: cell(C4, 1)}), *[(r, {0: cell(fx=0, par=0x47)}) for r in range(1, 17)], (17, {0: END}))]
write("arp.xm", song(arp, [0]))
write("amiarp.xm", song(arp, [0], linear=False))
# volumes: slid down (A02, two rows: 64 to 44), finely down and up (EB4, EA4), up (A20: to 54); the song's (G20: half,
# G40); hard left (800) and right (8FF); slid by the volume column (62, two rows: to 44); the place slid left (P0F);
# and set (C10)
write("vols.xm", song([rows(64, (0, {0: cell(C4, 1)}), (1, {0: cell(fx=10, par=0x02)}), (2, {0: cell(fx=10)}),
                            (8, {0: cell(fx=14, par=0xB4)}), (9, {0: cell(fx=14, par=0xA4)}), (10, {0: cell(fx=10, par=0x20)}),
                            (16, {0: cell(fx=G, par=0x20)}), (21, {0: cell(fx=G, par=0x40)}),
                            (22, {0: cell(vol=0x50, fx=8, par=0)}), (27, {0: cell(fx=8, par=0xFF)}),
                            (32, {0: cell(vol=0x62, fx=8, par=0x80)}), (33, {0: cell(vol=0x62)}),
                            (38, {0: cell(fx=P, par=0x0F)}), (39, {0: cell(fx=P)}), (44, {0: cell(fx=12, par=0x10)}),
                            (49, {0: END}))], [0]))
# an envelope: from full to half in ten ticks, held there while the key is down; let go, on for ten ticks and to
# nothing in ten more
write("env.xm", song([rows(64, (0, {0: cell(C4, 1)}), (8, {0: cell(OFF)}), (16, {0: END}))], [0],
                     insts=[inst(sample(), vol=([(0, 64), (10, 32), (20, 32), (30, 0)], 1, 0, 0, 3))]))
# a key let go fades the note: by an eighth a tick
write("fade.xm", song([rows(64, (0, {0: cell(C4, 1)}), (2, {0: cell(OFF)}), (8, {0: END}))], [0],
                      insts=[inst(sample(), vol=([(0, 64), (100, 64)], 0, 0, 0, 3), fade=4096)]))
# ... and with no envelope it stops at once; by K03 at the third tick
write("keyoff.xm", song([rows(64, (0, {0: cell(C4, 1)}), (4, {0: cell(OFF)}), (8, {0: cell(C4, 1)}),
                              (12, {0: cell(fx=K, par=3)}), (16, {0: END}))], [0]))
# a note with no instrument keeps the volume; an instrument with no note gives the sample's own back
write("bare.xm", song([rows(64, (0, {0: cell(C4, 1, 0x20)}), (4, {0: cell(C5)}), (8, {0: cell(0, 1)}), (12, {0: END}))], [0]))
# an instrument of two samples, the second (16 bits, half as loud, an octave up) from C-5 on; one tuned half a
# semitone up and set an octave down; and that one tuned by E58 (to no tuning)
write("multi.xm", song([rows(64, (0, {0: cell(C4, 1)}), (8, {0: cell(C5, 1)}), (16, {0: cell(C4, 2)}),
                             (24, {0: cell(C4, 2, fx=14, par=0x58)}), (32, {0: END}))], [0],
                       insts=[inst(sample(), sample(volume=32, bits16=True, rel=12), keys=[0] * 60 + [1] * 36),
                              inst(sample(fine=64, rel=-12))]))
# a ramp of 64 samples looped back and forth (128 samples a cycle), then forwards (64)
write("ping.xm", song([rows(64, (0, {0: cell(C6, 1)}), (8, {0: cell(C6, 2)}), (16, {0: END}))], [0],
                      insts=[inst(sample(RAMP, loop=2)), inst(sample(RAMP, loop=1))]))
# struck again every three ticks (R03: a short sample goes on); tremor (T21); vibrato (488); tremolo (788, at half);
# an instrument's own vibrato; cut at a tick (EC3), struck late (ED3) and never (ED7); struck again by E93; F00
write("fx.xm", song([rows(64, (0, {0: cell(C4, 2, fx=R, par=0x03)}), *[(r, {0: cell(fx=R)}) for r in range(1, 4)],
                          (8, {0: cell(C4, 1, fx=T, par=0x21)}), *[(r, {0: cell(fx=T)}) for r in range(9, 12)],
                          (16, {0: cell(C4, 1, fx=4, par=0x88)}), *[(r, {0: cell(fx=4)}) for r in range(17, 24)],
                          (24, {0: cell(C4, 1, 0x30, fx=7, par=0x88)}), *[(r, {0: cell(fx=7)}) for r in range(25, 32)],
                          (32, {0: cell(C4, 3)}), (40, {0: cell(C4, 1, fx=14, par=0xC3)}),
                          (44, {0: cell(C5, 1, fx=14, par=0xD3)}), (48, {0: cell(C4, 1, fx=14, par=0xD7)}),
                          (52, {0: cell(C4, 2, fx=14, par=0x93)}), *[(r, {0: cell(fx=14, par=0x93)}) for r in range(53, 56)],
                          (60, {0: cell(fx=15)}))], [0],
                    insts=[inst(sample()), inst(sample(BLIP, loop=0)), inst(sample(), vib=(0, 0, 15, 20))]))
# an instrument with no samples: a note of it is silence
write("empty.xm", song([rows(64, (0, {0: cell(C4, 1)}), (4, {0: cell(C5, 2)}), (8, {0: END}))], [0],
                       insts=[inst(sample()), inst()]))
# the file cut in its samples (it plays, with what there is), in its patterns (refused); one of an older form
# (refused); and no module at all
whole = song([rows(64, (0, {0: cell(C4, 1)}), (15, {0: END}))], [0], insts=blip)
write("cutsample.xm", whole[:len(whole) - 600])
write("cutpattern.xm", whole[:60 + 276 + 5])
write("old.xm", song(notes, [0], version=0x0102))
write("noxm.xm", bytes((i * 37 + 11) & 0xFF for i in range(3000)))
print(out)
