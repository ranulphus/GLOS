#!/usr/bin/env python3
"""Scream Tracker 3 modules for tests/host/s3m_test.c (apps/shared/s3m.c), written here byte by byte: small songs
whose every note is known, so that the test can say what must be heard (which pitch when, how loud, for how long).

    s3m_cases.py DIR      writes DIR/*.s3m; prints DIR

The sample of most of them is one cycle of a sine in 32 bytes, looped, whose middle C (C-4, the note byte 40h) is
8363 samples a second: a tone of 261.34 Hz; C-5 twice that, C-3 half."""
import math, os, struct, sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
WAVE = [int(round(127 * math.sin(2 * math.pi * i / 32))) for i in range(32)]
SINE = bytes((v + 128) & 0xFF for v in WAVE)                    # unsigned, as Scream Tracker 3 saves them
SIGNED = bytes(v & 0xFF for v in WAVE)
SINE16 = b"".join(struct.pack("<H", v * 256 + 32768) for v in WAVE)
BLIP = SINE * 30 + bytes([128]) * 40                            # 1000 bytes, no loop: a note that ends by itself
C3, C4, C5, CUT = 0x30, 0x40, 0x50, 254


def sample(data=SINE, volume=64, c2spd=8363, loop=(0, 32), bits16=False, kind=1):
    return dict(data=data, volume=volume, c2spd=c2spd, loop=loop, bits16=bits16, kind=kind)


def cell(note=None, ins=0, vol=None, fx=None, info=0):
    return (note, ins, vol, fx, info)


def rows(*pairs):
    """{row: {channel: cell}} as a list of 64"""
    p = [dict() for _ in range(64)]
    for r, cells in pairs:
        p[r] = cells
    return p


def pack(pattern):
    b = bytearray()
    for r in range(64):
        for ch, (note, ins, vol, fx, info) in pattern[r].items():
            what = ch | (32 if note is not None or ins else 0) | (64 if vol is not None else 0) | (128 if fx else 0)
            b.append(what)
            if what & 32:
                b += bytes([255 if note is None else note, ins])
            if what & 64:
                b.append(vol)
            if what & 128:
                b += bytes([ord(fx) - 64, info])
        b.append(0)
    return bytes(b)


def song(patterns, orders, samples=None, chans=(0, 8), title=b"test song", speed=6, tempo=125, gvol=64, stereo=True,
         pans=None, ffi=2, flags=0, cwt=0x1320):
    """chans: what each channel is (0 to 7 a left one, 8 to 15 a right one, 16 and up the AdLib's)"""
    samples = samples or [sample()]
    orders = list(orders) + [255] * (len(orders) % 2)
    h = bytearray(0x60)
    h[0:len(title)] = title
    h[0x1C], h[0x1D] = 0x1A, 16
    struct.pack_into("<HHHHHH", h, 0x20, len(orders), len(samples), len(patterns), flags, cwt, ffi)
    h[0x2C:0x30] = b"SCRM"
    h[0x30], h[0x31], h[0x32], h[0x33] = gvol, speed, tempo, 0x30 | (0x80 if stereo else 0)
    h[0x35] = 0xFC if pans else 0
    for i in range(32):
        h[0x40 + i] = chans[i] if i < len(chans) else 255
    b = bytearray(h) + bytes(orders)
    tables = len(b)
    b += bytes(2 * len(samples) + 2 * len(patterns))
    if pans:
        b += bytes(pans).ljust(32, b"\0")

    def align():
        while len(b) % 16:
            b.append(0)

    heads = []
    for s in samples:
        align()
        heads.append(len(b))
        b += bytes(0x50)
    for i, p in enumerate(patterns):
        align()
        struct.pack_into("<H", b, tables + 2 * len(samples) + 2 * i, len(b) // 16)
        packed = pack(p)
        b += struct.pack("<H", len(packed) + 2) + packed
    for i, s in enumerate(samples):
        align()
        where, at = len(b) // 16, heads[i]
        b += s["data"]
        struct.pack_into("<H", b, tables + 2 * i, at // 16)
        b[at] = s["kind"]
        b[at + 1:at + 9] = b"TEST.SMP"
        b[at + 0x0D] = where >> 16
        struct.pack_into("<H", b, at + 0x0E, where & 0xFFFF)
        loop = s["loop"] or (0, 0)
        struct.pack_into("<III", b, at + 0x10, len(s["data"]) // (2 if s["bits16"] else 1), loop[0], loop[1])
        b[at + 0x1C] = s["volume"]
        b[at + 0x1F] = (1 if s["loop"] else 0) | (4 if s["bits16"] else 0)
        struct.pack_into("<I", b, at + 0x20, s["c2spd"])
        name = b"sample %d" % (i + 1)
        b[at + 0x30:at + 0x30 + len(name)] = name
        b[at + 0x4C:at + 0x50] = b"SCRS"
    return bytes(b)


def write(name, data):
    open(os.path.join(out, name), "wb").write(data)


END = cell(fx="C", info=0)                                      # a break to an order there is none of: the end
blip = [sample(BLIP, loop=None)]

# three notes of sixteen rows each, then one softer (by its volume column)
notes = [rows((0, {0: cell(C4, 1)}), (16, {0: cell(C5, 1)}), (32, {0: cell(C3, 1)}), (48, {0: cell(C4, 1, 16)}),
              (63, {0: END}))]
write("notes.s3m", song(notes, [0]))
# the same from samples saved signed (the older way), and in a song that is not stereo
write("signed.s3m", song(notes, [0], samples=[sample(SIGNED)], ffi=1))
write("mono.s3m", song(notes, [0], stereo=False))
# on a channel of the right; and with the channels' places said by the file (the first hard left, the second hard right)
right = [rows((0, {1: cell(C4, 1)}), (63, {0: END}))]
write("right.s3m", song(right, [0]))
write("pans.s3m", song([rows((0, {0: cell(C4, 1)}), (8, {0: cell(CUT), 1: cell(C4, 1)}), (16, {0: END}))], [0],
                       pans=[0x20, 0x2F]))
# speeds: 32 rows of six ticks, then (A03) 32 of three; a faster tempo (T96: 150); and both from the header
write("speed.s3m", song([rows((0, {0: cell(C4, 1)}), (32, {0: cell(fx="A", info=3)}))], [0]))
write("tempo.s3m", song([rows((0, {0: cell(C4, 1, fx="T", info=150)}))], [0]))
write("header.s3m", song([rows((0, {0: cell(C4, 1)}))], [0], speed=3, tempo=150, gvol=32))
# a break into the second pattern's row 8, a jump back to the start from its row 15: the song goes round there
write("jump.s3m", song([rows((0, {0: cell(C4, 1)}), (7, {0: cell(fx="C", info=0x08)})),
                        rows((8, {0: cell(C5, 1)}), (15, {0: cell(fx="B", info=0)}))], [0, 1]))
# orders with markers among them (254), and an end (255) before the list's: a jump counts the markers
write("orders.s3m", song([rows((0, {0: cell(C4, 1)}), (3, {0: cell(fx="B", info=3)})),
                          rows((0, {0: cell(C5, 1)}), (3, {0: END}))], [254, 0, 254, 1, 255, 0]))
# rows 2 to 5 three times (SB0, SB2), then on to row 7 and out
write("loop.s3m", song([rows((0, {0: cell(C4, 1)}), (2, {0: cell(fx="S", info=0xB0)}), (5, {0: cell(fx="S", info=0xB2)}),
                             (7, {0: END}))], [0]))
# ... and a second loop with no start said: it begins after the first (rows 6 to 9 twice), as Scream Tracker has it
write("loop2.s3m", song([rows((0, {0: cell(C4, 1)}), (2, {0: cell(fx="S", info=0xB0)}), (5, {0: cell(fx="S", info=0xB2)}),
                              (9, {0: cell(fx="S", info=0xB1)}), (11, {0: END}))], [0]))
# a row three times as long (SE2), its note struck once; then out
write("delay.s3m", song([rows((0, {0: cell(C4, 1, fx="S", info=0xE2)}), (1, {0: END}))], [0], samples=blip))
# a sample with no loop: the note ends by itself (1000 bytes at 8363 a second)
write("blip.s3m", song([rows((0, {0: cell(C4, 1)}), (15, {0: END}))], [0], samples=blip))
# ... from 512 bytes in (O02), whole, and from there again (O00)
write("offset.s3m", song([rows((0, {0: cell(C4, 1, fx="O", info=2)}), (4, {0: cell(C4, 1)}), (8, {0: cell(C4, 1, fx="O")}),
                               (12, {0: END}))], [0], samples=blip))
# slides: up (F05: 20 a tick) for eleven rows, 1712 to 612; to a note (G08) and no further; down (E05) for five
# rows, 1712 to 2212; and a fine one up (FFF: 60, once)
write("slide.s3m", song([rows((0, {0: cell(C4, 1)}), *[(r, {0: cell(fx="F", info=5)}) for r in range(1, 12)],
                              (16, {0: cell(C4, 1)}), (17, {0: cell(C5, 0, fx="G", info=8)}),
                              *[(r, {0: cell(fx="G")}) for r in range(18, 32)],
                              (40, {0: cell(C4, 1)}), (41, {0: cell(fx="E", info=5)}),
                              *[(r, {0: cell(fx="E")}) for r in range(42, 46)],
                              (52, {0: cell(C4, 1, fx="F", info=0xFF)}), (60, {0: END}))], [0]))
# a chord by arpeggio (J47: the note, a major third and a fifth above, a tick each)
write("arp.s3m", song([rows(*[(r, {0: cell(C4 if r == 0 else None, 1 if r == 0 else 0, fx="J", info=0x47)})
                              for r in range(16)], (16, {0: END}))], [0]))
# volumes: slid down (D02, two rows: 64 to 44), finely down and up (DF4, D4F), up (D20: to 54); the song's (V20: half,
# V40); the channel's place (X40: the middle; S80: hard left; S8F: hard right)
vols = rows((0, {0: cell(C4, 1)}), (1, {0: cell(fx="D", info=0x02)}), (2, {0: cell(fx="D")}),
            (8, {0: cell(fx="D", info=0xF4)}), (9, {0: cell(fx="D", info=0x4F)}), (10, {0: cell(fx="D", info=0x20)}),
            (16, {0: cell(fx="V", info=0x20)}), (21, {0: cell(fx="V", info=0x40)}),
            (22, {0: cell(vol=64, fx="X", info=0x40)}), (27, {0: cell(fx="S", info=0x80)}),
            (32, {0: cell(fx="S", info=0x8F)}), (37, {0: END}))
write("vols.s3m", song([vols], [0]))
# the slides of the first Scream Tracker 3 (and of files that ask for them): on a row's first tick too
write("fast.s3m", song([rows((0, {0: cell(C4, 1)}), (1, {0: cell(fx="D", info=0x02)}), (2, {0: cell(fx="D")}),
                             (8, {0: END}))], [0], flags=0x40))
# a note cut (^^), cut at a tick (SC3), struck late (SD3), and never (SD7 in a row of six ticks)
write("cut.s3m", song([rows((0, {0: cell(C4, 1)}), (4, {0: cell(CUT)}), (8, {0: cell(C4, 1, fx="S", info=0xC3)}),
                            (12, {0: cell(C5, 1, fx="S", info=0xD3)}), (16, {0: cell(C4, 1, fx="S", info=0xD7)}),
                            (20, {0: END}))], [0]))
# instruments: 16 bits; a middle C of 16726 a second (an octave up); half as loud; a tuning by S28 (7895)
write("kinds.s3m", song([rows((0, {0: cell(C4, 2)}), (8, {0: cell(C4, 3)}), (16, {0: cell(C4, 4)}),
                              (24, {0: cell(C4, 1, fx="S", info=0x28)}), (32, {0: END}))], [0],
                        samples=[sample(), sample(SINE16, bits16=True), sample(c2spd=16726), sample(volume=32)]))
# struck again every three ticks (Q03: a short sample goes on); tremor (I21); vibrato (H88); tremolo (R88, at half)
write("fx.s3m", song([rows((0, {0: cell(C4, 2, fx="Q", info=0x03)}), *[(r, {0: cell(fx="Q")}) for r in range(1, 4)],
                           (8, {0: cell(C4, 1, fx="I", info=0x21)}), *[(r, {0: cell(fx="I")}) for r in range(9, 12)],
                           (16, {0: cell(C4, 1, fx="H", info=0x88)}), *[(r, {0: cell(fx="H")}) for r in range(17, 24)],
                           (24, {0: cell(C4, 1, 32, fx="R", info=0x88)}), *[(r, {0: cell(fx="R")}) for r in range(25, 32)],
                           (32, {0: END}))], [0], samples=[sample(), sample(BLIP, loop=None)]))
# eight channels, a note on a left one and an octave above on a right one
write("eight.s3m", song([rows((0, {0: cell(C4, 1), 5: cell(C5, 1)}), (15, {0: END}))], [0],
                        chans=(0, 1, 2, 3, 8, 9, 10, 11)))
# an AdLib channel and an AdLib instrument beside the sample's: they are passed over
write("adlib.s3m", song([rows((0, {0: cell(C4, 1), 1: cell(C4, 2)}), (4, {0: cell(C5, 2)}), (8, {0: END}))], [0],
                        chans=(0, 16), samples=[sample(), sample(bytes(0), kind=2)]))
# the file cut in its samples (it plays, with what there is), in its tables (refused); no channel that plays
# samples (refused); and no module at all
whole = song([rows((0, {0: cell(C4, 1)}), (15, {0: END}))], [0], samples=blip)
write("cutsample.s3m", whole[:len(whole) - 600])
write("cuttable.s3m", whole[:0x63])
write("nochannel.s3m", song(notes, [0], chans=(16, 17)))
write("nos3m.s3m", bytes((i * 37 + 11) & 0xFF for i in range(3000)))
print(out)
