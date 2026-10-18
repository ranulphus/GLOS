#!/usr/bin/env python3
"""A TrueType font at one size as a 1-bit bitmap font for the 2D library
(kernel/include/gfx.h struct gfx_font; PRD D7: UI fonts are bitmaps made at
build time from open TrueType fonts).

  mkfont.py TTF PIXELS NAME [--first N] [--last N] [--ranges A-B,C,...] [--smooth OUT] > kernel/gfx/fonts/NAME.c

FreeType renders each code point without anti-aliasing, with the font's own
hinting (Pillow's ImageFont in mode "1"). The glyph table has an entry for
every code point from --first to --last (default 20h to FFh), then for each
of --ranges' (M10: Greek, Cyrillic, the box-drawing characters ...: the
font's `more` table says where each starts), and one more, a box, for code
points the font or the table hasn't got. The output names
the file and its SHA-256, so a regenerated font can be told from this one.

--smooth (M10 step G) also writes the same glyphs, in the same order, as
FreeType draws them anti-aliased: 4-bit coverage for the toolkit's font
smoothing (sdk/gui/smooth.c; struct gfx_aa). The 1-bit font's advances stay
the pens', so smoothed text takes the same room. The chunk, little-endian:
the font's name (32 bytes, NUL-padded, as struct gfx_font has it), the
glyphs' number and the coverage's bytes (u32 each), then a glyph's offset
(u32), width (u8) and left edge (s8) and two zero bytes for each, and the
coverage: `height` rows of (width + 1) / 2 bytes, the high nibble leftmost.
`make fonts` joins the three after "GLOSAA1\n" as sdk/gui/fonts/SMOOTH.DAT.
Runs on the host: it needs python3-pil. `make fonts` runs it for the fonts
the kernel has."""
import argparse
import hashlib
import os
import struct
import sys

from PIL import Image, ImageDraw, ImageFont


def glyph(font, ch, height):
    """(left, width, advance, rows): the ink's left edge from the pen, its width, and `height` rows of bits."""
    x0, y0, x1, y1 = font.getbbox(ch, mode="1", anchor="la")
    adv = int(round(font.getlength(ch, mode="1")))
    w = max(x1 - x0, 0)
    if w == 0:
        return 0, 0, adv, [b""] * height
    img = Image.new("1", (w, height), 0)
    d = ImageDraw.Draw(img)
    d.fontmode = "1"
    d.text((-x0, 0), ch, fill=1, font=font, anchor="la")
    px = img.load()
    rows = []
    for y in range(height):
        row = bytearray((w + 7) // 8)
        for x in range(w):
            if px[x, y]:
                row[x >> 3] |= 0x80 >> (x & 7)
        rows.append(bytes(row))
    return x0, w, adv, rows


def coverage(font, ch, height):
    """(left, width, rows) anti-aliased: rows of 4-bit coverage, two pixels a byte, the high nibble leftmost."""
    x0, y0, x1, y1 = font.getbbox(ch, mode="L", anchor="la")
    w = max(x1 - x0, 0)
    if w == 0:
        return 0, 0, [b""] * height
    img = Image.new("L", (w, height), 0)
    d = ImageDraw.Draw(img)
    d.fontmode = "L"
    d.text((-x0, 0), ch, fill=255, font=font, anchor="la")
    px = img.load()
    rows = []
    for y in range(height):
        row = bytearray((w + 1) // 2)
        for x in range(w):
            v = (px[x, y] * 15 + 127) // 255
            row[x >> 1] |= v << (4 if x % 2 == 0 else 0)
        rows.append(bytes(row))
    return x0, w, rows


def mono_coverage(left, w, rows):
    """A 1-bit glyph (the box) as coverage: its set bits full."""
    out = []
    for r in rows:
        row = bytearray((w + 1) // 2)
        for x in range(w):
            if r[x >> 3] & (0x80 >> (x & 7)):
                row[x >> 1] |= 15 << (4 if x % 2 == 0 else 0)
        out.append(bytes(row))
    return left, w, out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ttf")
    ap.add_argument("pixels", type=int)
    ap.add_argument("name")
    ap.add_argument("--first", type=lambda s: int(s, 0), default=0x20)
    ap.add_argument("--last", type=lambda s: int(s, 0), default=0xFF)
    ap.add_argument("--ranges", default="", help="more code points: 0x370-0x3FF,0x20AC,...")
    ap.add_argument("--smooth", help="the glyphs anti-aliased too, as a chunk of sdk/gui/fonts/SMOOTH.DAT, into this file")
    a = ap.parse_args()
    more = []
    for part in filter(None, a.ranges.split(",")):
        lo, _, hi = part.partition("-")
        more.append((int(lo, 0), int(hi or lo, 0)))
    font = ImageFont.truetype(a.ttf, a.pixels)
    ascent, descent = font.getmetrics()
    height = ascent + descent
    family, style = font.getname()
    digest = hashlib.sha256(open(a.ttf, "rb").read()).hexdigest()
    glyphs, smooth = [], []
    for cp in list(range(a.first, a.last + 1)) + [c for lo, hi in more for c in range(lo, hi + 1)]:
        ch = chr(cp)
        blank = (0x7F <= cp < 0xA0) or cp == 0xAD      # controls, and the soft hyphen: no ink, no width
        glyphs.append((0, 0, 0, [b""] * height) if blank else glyph(font, ch, height))
        if a.smooth:
            smooth.append((0, 0, [b""] * height) if blank else coverage(font, ch, height))
    bw = max(a.pixels // 2, 4)                          # the box: a rectangle on the baseline, a cap high
    top = max(ascent - (a.pixels * 7 // 10), 0)
    rows = []
    for y in range(height):
        row = bytearray((bw + 7) // 8)
        if top <= y < ascent:
            edge = y in (top, ascent - 1)
            for x in range(bw):
                if edge or x in (0, bw - 1):
                    row[x >> 3] |= 0x80 >> (x & 7)
        rows.append(bytes(row))
    glyphs.append((1, bw, bw + 2, rows))
    if a.smooth:
        smooth.append(mono_coverage(1, bw, rows))
        cbits, ctable = bytearray(), bytearray()
        for left, w, crow in smooth:
            ctable += struct.pack("<IBbH", len(cbits), w, left, 0)
            for r in crow:
                cbits += r
        name = ("%s %s %d" % (family, style, a.pixels)).encode()
        with open(a.smooth, "wb") as f:
            f.write(name.ljust(32, b"\0") + struct.pack("<II", len(smooth), len(cbits)) + ctable + cbits)
    bits, table = bytearray(), []
    for left, w, adv, rows in glyphs:
        table.append((len(bits), w, adv, left))
        for r in rows:
            bits += r
    out = sys.stdout
    out.write("/* %s %s at %d pixels: a 1-bit font for the 2D library (kernel/include/gfx.h).\n" % (family, style, a.pixels))
    out.write(" * Made by tools/mkfont.py from %s\n * (SHA-256 %s);\n" % (os.path.basename(a.ttf), digest))
    out.write(" * not to be edited: `make fonts` makes it again. The font's licence: THIRD_PARTY.md. */\n")
    out.write('#include "gfx.h"\n\n')
    out.write("static const u8 bits[] = {")
    for i, b in enumerate(bits):
        out.write("%s0x%02x," % ("\n    " if i % 20 == 0 else " ", b))
    out.write("\n};\n\nstatic const struct gfx_glyph glyphs[] = {")
    for i, (at, w, adv, left) in enumerate(table):
        out.write("%s{ %d, %d, %d, %d }," % ("\n    " if i % 6 == 0 else " ", at, w, adv, left))
    out.write("\n};\n\n")
    if more:
        at = a.last - a.first + 1
        out.write("static const struct gfx_range more[] = {")
        for i, (lo, hi) in enumerate(more):
            out.write("%s{ 0x%X, %d, %d }," % ("\n    " if i % 4 == 0 else " ", lo, hi - lo + 1, at))
            at += hi - lo + 1
        out.write("\n};\n\n")
    out.write('const struct gfx_font gfx_font_%s = {\n    "%s %s %d", %d, %d, 0x%X, %d, glyphs, bits, %d%s\n};\n'
              % (a.name, family, style, a.pixels, height, ascent, a.first, a.last - a.first + 1, len(table) - 1,
                 ", more, %d" % len(more) if more else ""))


if __name__ == "__main__":
    main()
