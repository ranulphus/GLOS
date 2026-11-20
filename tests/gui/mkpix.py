#!/usr/bin/env python3
"""The pictures of the GUI scripts' drive (tests/gui/drives/C/PIX: files-thumbs.txt), made once and kept in the
tree; run again only to change them. A PNG (with clear pixels), a BMP, a PCX taller than it is wide, a BMP smaller
than a thumbnail, a file that is a picture by its name alone, a text file and a folder.

    python3 tests/gui/mkpix.py
"""
import os, struct, zlib

here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "drives", "C", "PIX")
os.makedirs(os.path.join(here, "SUB"), exist_ok=True)


def png(w, h, px):                      # px(x, y) -> (r, g, b, a)
    raw = b"".join(b"\0" + b"".join(bytes(px(x, y)) for x in range(w)) for y in range(h))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def bmp(w, h, px):                      # px(x, y) -> (r, g, b)
    pad = (4 - w * 3 % 4) % 4
    rows = b"".join(b"".join(bytes(px(x, y)[::-1]) for x in range(w)) + b"\0" * pad for y in range(h - 1, -1, -1))
    return (b"BM" + struct.pack("<IHHI", 54 + len(rows), 0, 0, 54)
            + struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(rows), 2835, 2835, 0, 0) + rows)


def pcx(w, h, px):                      # 24 bits: three planes a line, each run-length coded
    bpl = w + (w & 1)
    out = bytearray(struct.pack("<BBBBHHHHHH", 10, 5, 1, 8, 0, 0, w - 1, h - 1, 72, 72) + b"\0" * 48
                    + struct.pack("<BBHHHH", 0, 3, bpl, 1, 0, 0) + b"\0" * 54)
    for y in range(h):
        for plane in range(3):
            line = bytes(px(x, y)[plane] for x in range(w)) + b"\0" * (bpl - w)
            i = 0
            while i < bpl:
                n = 1
                while i + n < bpl and n < 63 and line[i + n] == line[i]:
                    n += 1
                if n > 1 or line[i] >= 0xC0:
                    out += bytes((0xC0 | n, line[i]))
                else:
                    out.append(line[i])
                i += n
    return bytes(out)


def grid(x, y):                         # squares of colours, a clear border
    if x < 12 or y < 12 or x >= 288 or y >= 188:
        return (0, 0, 0, 0)
    c = ((x - 12) // 46 + (y - 12) // 44) % 4
    return ((220, 60, 50), (50, 150, 70), (50, 90, 200), (235, 200, 60))[c] + (255,)


def sunset(x, y):                       # a sky going down to red, a sun, the sea
    if y >= 70:
        return (20, 40 + (y - 70) * 2, 110 + (y - 70) * 3)
    if (x - 110) ** 2 + (y - 58) ** 2 < 20 ** 2:
        return (255, 235, 120)
    return (250 - y, 90 + y, 70 + y * 2)


def tall(x, y):                         # stripes down a tower
    return ((200, 200, 210), (90, 90, 120), (240, 170, 60))[(y // 15) % 3] if 10 <= x < 50 else (30, 30, 40)


def tiny(x, y):
    return (255, 255, 255) if (x + y) % 2 else (200, 30, 30)


files = {
    "GRID.PNG": png(300, 200, grid),
    "SUNSET.BMP": bmp(160, 100, sunset),
    "TALL.PCX": pcx(60, 150, tall),
    "TINY.BMP": bmp(20, 12, tiny),
    "BROKEN.PNG": b"This file has a picture's name and no picture in it.\r\n",
    "NOTES.TXT": b"Pictures for the thumbnail view's test.\r\n",
    os.path.join("SUB", "INSIDE.TXT"): b"A folder beside the pictures.\r\n",
}
for name, data in files.items():
    with open(os.path.join(here, name), "wb") as f:
        f.write(data)
    print("%-16s %6d bytes" % (name, len(data)))
