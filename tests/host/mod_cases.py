#!/usr/bin/env python3
"""Modules for tests/host/mod_test.c (apps/shared/mod.c), written here byte by byte: small songs whose every note
is known, so that the test can say what must be heard (which pitch when, how loud, for how long).

    mod_cases.py DIR      writes DIR/*.mod; prints DIR

The sample of them all is one cycle of a sine in 32 bytes, looped: at the period 428 (C-2) the Amiga plays
3546895 / 428 = 8287 of its bytes a second, a tone of 259.0 Hz; at 214 twice that, at 856 half."""
import math, os, struct, sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
SINE = bytes(int(round(127 * math.sin(2 * math.pi * i / 32))) & 0xFF for i in range(32))
BLIP = SINE * 30 + bytes(40)                             # 1000 bytes, no loop: a note that ends by itself


def cell(sample=0, period=0, effect=0, param=0):
    return bytes([(sample & 0xF0) | period >> 8, period & 0xFF, (sample & 15) << 4 | effect, param])


def module(patterns, orders, samples=None, sig=b"M.K.", channels=4, title=b"test song", cut=None):
    """patterns: lists of 64 rows, a row a dict {channel: cell}; samples: (bytes, volume, finetune, loop start, loop length in bytes)"""
    samples = samples or [(SINE, 64, 0, 0, 32)]
    b = bytearray(title.ljust(20, b"\0"))
    for i in range(31):
        if i < len(samples):
            data, vol, fine, ls, ll = samples[i]
            b += b"sample %d" % (i + 1) + bytes(22 - len(b"sample %d" % (i + 1)))
            b += struct.pack(">HBBHH", len(data) // 2, fine & 15, vol, ls // 2, max(ll // 2, 1))
        else:
            b += bytes(22) + struct.pack(">HBBHH", 0, 0, 0, 0, 1)
    b += bytes([len(orders), 127]) + bytes(orders).ljust(128, b"\0") + sig
    for p in patterns:
        for r in range(64):
            for c in range(channels):
                b += p[r].get(c, cell()) if r < len(p) else cell()
    for s in samples:
        b += s[0]
    return bytes(b[:cut] if cut else b)


def rows(*pairs):
    """{row: {channel: cell}} as a list of 64"""
    p = [dict() for _ in range(64)]
    for r, cells in pairs:
        p[r] = cells
    return p


def write(name, data):
    open(os.path.join(out, name), "wb").write(data)


# three notes of sixteen rows each, the last softer; ended by a break to an order there is none of
write("notes.mod", module([rows((0, {0: cell(1, 428)}), (16, {0: cell(1, 214)}), (32, {0: cell(1, 856)}),
                                (48, {0: cell(1, 428, 12, 16)}), (63, {0: cell(0, 0, 13, 0)}))], [0]))
# the same on a channel of the right
write("right.mod", module([rows((0, {1: cell(1, 428)}), (63, {0: cell(0, 0, 13, 0)}))], [0]))
# speeds: 32 rows of six ticks, then (F03) 32 of three; and a faster tempo (F96: 150) from the start
write("speed.mod", module([rows((0, {0: cell(1, 428)}), (32, {0: cell(0, 0, 15, 3)}))], [0]))
write("tempo.mod", module([rows((0, {0: cell(1, 428, 15, 150)}))], [0]))
# a break into the second pattern's row 8, a jump back to the start from its row 15: the song goes round there
write("jump.mod", module([rows((0, {0: cell(1, 428)}), (7, {0: cell(0, 0, 13, 8)})),
                          rows((8, {0: cell(1, 214)}), (15, {0: cell(0, 0, 11, 0)}))], [0, 1]))
# rows 2 to 5 three times (E60, E62), then on to row 7 and out
write("loop.mod", module([rows((0, {0: cell(1, 428)}), (2, {0: cell(0, 0, 14, 0x60)}), (5, {0: cell(0, 0, 14, 0x62)}),
                               (7, {0: cell(0, 0, 13, 0)}))], [0]))
# a row three times as long (EE2), then out
write("delay.mod", module([rows((0, {0: cell(1, 428, 14, 0xE2)}), (1, {0: cell(0, 0, 13, 0)}))], [0]))
# a sample with no loop: the note ends by itself (1000 bytes at 8287 a second)
write("blip.mod", module([rows((0, {0: cell(1, 428)}), (15, {0: cell(0, 0, 13, 0)}))], [0],
                         samples=[(BLIP, 64, 0, 0, 2)]))
# slides: up an octave by 1xx (428 to 214 needs 214 steps: 43 ticks of 5), and to a note by 3xx
write("slide.mod", module([rows((0, {0: cell(1, 428)}), *[(r, {0: cell(0, 0, 1, 5)}) for r in range(1, 12)],
                                (16, {0: cell(1, 428)}), (17, {0: cell(0, 214, 3, 8)}),
                                *[(r, {0: cell(0, 0, 3, 0)}) for r in range(18, 32)], (40, {0: cell(0, 0, 13, 0)}))], [0]))
# a chord by arpeggio (047: the note, a major third and a fifth above, a tick each)
write("arp.mod", module([rows(*[(r, {0: cell(1 if r == 0 else 0, 428 if r == 0 else 0, 0, 0x47)}) for r in range(16)],
                              (16, {0: cell(0, 0, 13, 0)}))], [0]))
# eight channels, a note on each side
write("eight.mod", module([rows((0, {0: cell(1, 428), 5: cell(1, 214)}), (15, {0: cell(0, 0, 13, 0)}))], [0], sig=b"8CHN",
                          channels=8))
# a fine tuning of +7 (an eighth of a semitone short of a semitone up... seven of them)
write("fine.mod", module([rows((0, {0: cell(1, 428)}), (15, {0: cell(0, 0, 13, 0)}))], [0], samples=[(SINE, 64, 7, 0, 32)]))
# the file cut in its samples (it plays, with what there is), in its patterns (refused), and no module at all
whole = module([rows((0, {0: cell(1, 428)}), (15, {0: cell(0, 0, 13, 0)}))], [0], samples=[(BLIP, 64, 0, 0, 2)])
write("cutsample.mod", whole[:len(whole) - 600])
write("cutpattern.mod", whole[:1084 + 500])
write("nomod.mod", bytes((i * 37 + 11) & 0xFF for i in range(3000)))
write("speed0.mod", module([rows((0, {0: cell(1, 428)}), (4, {0: cell(0, 0, 15, 0)}))], [0]))
print(out)
