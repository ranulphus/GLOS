#!/usr/bin/env python3
"""A TrueType font at one size as a 1-bit bitmap font for the 2D library
(kernel/include/gfx.h struct gfx_font; PRD D7: UI fonts are bitmaps made at
build time from open TrueType fonts).

  mkfont.py TTF PIXELS NAME [--first N] [--last N] > kernel/gfx/fonts/NAME.c

FreeType renders each code point without anti-aliasing, with the font's own
hinting (Pillow's ImageFont in mode "1"). The glyph table has an entry for
every code point from --first to --last (default 20h to FFh) and one more,
a box, for code points the font or the table hasn't got. The output names
the file and its SHA-256, so a regenerated font can be told from this one.
Runs on the host: it needs python3-pil. `make fonts` runs it for the fonts
the kernel has."""
import argparse
import hashlib
import os
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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ttf")
    ap.add_argument("pixels", type=int)
    ap.add_argument("name")
    ap.add_argument("--first", type=lambda s: int(s, 0), default=0x20)
    ap.add_argument("--last", type=lambda s: int(s, 0), default=0xFF)
    a = ap.parse_args()
    font = ImageFont.truetype(a.ttf, a.pixels)
    ascent, descent = font.getmetrics()
    height = ascent + descent
    family, style = font.getname()
    digest = hashlib.sha256(open(a.ttf, "rb").read()).hexdigest()
    glyphs = []
    for cp in range(a.first, a.last + 1):
        ch = chr(cp)
        blank = (0x7F <= cp < 0xA0) or cp == 0xAD      # controls, and the soft hyphen: no ink, no width
        glyphs.append((0, 0, 0, [b""] * height) if blank else glyph(font, ch, height))
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
    out.write('const struct gfx_font gfx_font_%s = {\n    "%s %s %d", %d, %d, 0x%X, %d, glyphs, bits, %d\n};\n'
              % (a.name, family, style, a.pixels, height, ascent, a.first, a.last - a.first + 1, len(table) - 1))


if __name__ == "__main__":
    main()
