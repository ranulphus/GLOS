#!/usr/bin/env python3
"""The two modules beside the packed sound files here, written byte by byte and kept in the repository: three
seconds each (25 rows) of a tone of its own, which the host's test plays through source.c (tests/host/packed_test.c)
and Loop A's audio suite in the Player (their tones are looked for in the recording).

    PS3M.S3M    Scream Tracker 3: a sine of 32 samples whose middle C is 42234 a second: 1319.8 Hz
    PXM.XM      FastTracker 2: a sine of 8 samples at A#4: 1862.6 Hz

    python3 modules.py      (from this folder: makes them again)"""
import math, struct


def sine(n):
    return [int(round(127 * math.sin(2 * math.pi * i / n))) for i in range(n)]


def s3m():
    wave = bytes((v + 128) & 0xFF for v in sine(32))
    h = bytearray(0x60)
    h[0:6] = b"a tone"
    h[0x1C], h[0x1D] = 0x1A, 16
    struct.pack_into("<HHHHHH", h, 0x20, 2, 1, 1, 0, 0x1320, 2)         # two orders, an instrument, a pattern
    h[0x2C:0x30] = b"SCRM"
    h[0x30], h[0x31], h[0x32], h[0x33] = 64, 6, 125, 0xB0
    for i in range(32):
        h[0x40 + i] = (0, 8)[i] if i < 2 else 255
    b = bytearray(h) + bytes([0, 255]) + struct.pack("<HH", 0x70 // 16, 0xC0 // 16)
    b = b.ljust(0x70, b"\0")
    ins = bytearray(0x50)                                               # at 70h: the sample's header
    ins[0] = 1
    struct.pack_into("<H", ins, 0x0E, 0x110 // 16)
    struct.pack_into("<III", ins, 0x10, 32, 0, 32)
    ins[0x1C], ins[0x1F] = 64, 1
    struct.pack_into("<I", ins, 0x20, 42234)
    ins[0x30:0x36], ins[0x4C:0x50] = b"a sine", b"SCRS"
    b += ins
    # at C0h: the pattern: C-4 with instrument 1 on both channels, and at row 24 a break (C00)
    rows = bytes([0x20, 0x40, 1, 0x21, 0x40, 1, 0]) + bytes(23) + bytes([0x80, 3, 0, 0]) + bytes(39)
    b += struct.pack("<H", len(rows) + 2) + rows
    return bytes(b.ljust(0x110, b"\0")) + wave


def xm():
    wave, last, deltas = sine(8), 0, bytearray()
    for v in wave:
        deltas.append((v - last) & 0xFF)
        last = v
    b = bytearray(b"Extended Module: " + b"a tone".ljust(20, b"\0") + b"\x1a" + b"GLOS's tests".ljust(20) + struct.pack("<H", 0x0104))
    b += struct.pack("<IHHHHHHHH", 276, 1, 0, 2, 1, 1, 1, 6, 125) + bytes(256)
    # the pattern, 25 rows: A#4 (59) with instrument 1 on the first channel; nothing else
    rows = bytes([0x83, 59, 1, 0x80]) + bytes([0x80, 0x80]) * 24
    b += struct.pack("<IBHH", 9, 0, 25, len(rows)) + rows
    b += struct.pack("<I", 263) + b"a sine".ljust(22, b"\0") + b"\0" + struct.pack("<HI", 1, 40) + bytes(96 + 48 + 48 + 14 + 2 + 22)
    b += struct.pack("<IIIBbBBbB", 8, 0, 8, 64, 0, 1, 128, 0, 0) + b"a sine".ljust(22, b"\0") + deltas
    return bytes(b)


open("PS3M.S3M", "wb").write(s3m())
open("PXM.XM", "wb").write(xm())
