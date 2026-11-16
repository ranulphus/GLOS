#!/usr/bin/env python3
"""GLOS's icons from the open sets they are taken from (docs/gui.md 1.11; THIRD_PARTY.md).

  mkicons.py FARMFRESH.zip FONTAWESOME.zip sdk/gui/icons sdk/gui/fonts/ICONS.DAT sdk/gui/glyphs.c

Colour icons (sdk/gui/icons/colour.list: a GLOS name and a Farm-Fresh icon's): each icon's 16 and 32 pixel
pictures as GLOS has icons, a byte a pixel: 0 clear, else the 8-bit palette's index of the cube's colour nearest
to the pixel's (16 + 36 r + 6 g + b: kernel/gfx/pal.c). A pixel less than half opaque is clear: the pictures
have hard edges, so they are the same at every depth. ICONS.DAT, little-endian: "GLOSIC1\\n", the number of
icons (u32), then for each, in the order of their names (bytes), its name (24 bytes, NUL-padded), 256 bytes (16
by 16) and 1024 bytes (32 by 32). The toolkit reads an icon from it by name (sdk/gui/icons.c).

One-colour glyphs (sdk/gui/icons/glyphs.list: a GLOS name and a Font Awesome Free icon's, solid style): each
drawn at 14 pixels by FreeType without anti-aliasing, centred in a 16 by 16 mask of two bytes a row, the high bit
leftmost; written as C (glyphs.c), which the toolkit draws in the theme's text colour.

Both are kept in the repository: the build needs no download. `make icons` fetches the pinned archives
(tools/setup/versions.mk) and runs this, on the host: it needs python3-pil."""
import io
import json
import struct
import sys
import zipfile

from PIL import Image, ImageDraw, ImageFont


def listed(path):
    out = []
    for line in open(path, encoding="ascii"):
        line = line.split("#")[0].split()
        if len(line) == 2:
            out.append((line[0], line[1]))
        elif line:
            sys.exit("%s: a name and an icon a line: %r" % (path, line))
    names = [n for n, _ in out]
    if len(set(names)) != len(names) or any(len(n) > 23 for n in names):
        sys.exit("%s: a name twice, or one longer than 23 characters" % path)
    return out


def cube(r, g, b):
    return 16 + 36 * ((r + 25) // 51) + 6 * ((g + 25) // 51) + (b + 25) // 51


def picture(z, member, side):
    im = Image.open(io.BytesIO(z.read(member))).convert("RGBA")
    if im.size != (side, side):
        sys.exit("%s is %dx%d" % (member, im.width, im.height))
    out = bytearray()
    for y in range(side):
        for x in range(side):
            r, g, b, a = im.getpixel((x, y))
            out.append(cube(r, g, b) if a >= 128 else 0)
    return bytes(out)


def main():
    farm, awesome, lists, dat, glyphs_c = sys.argv[1:6]
    z = zipfile.ZipFile(farm)
    members = {}
    for n in z.namelist():
        for side in (16, 32):
            mark = "/%dx%d/" % (side, side)
            if mark in n and n.endswith(".png"):
                members[(n.split(mark)[1][:-4], side)] = n
    colour = sorted(listed(lists + "/colour.list"), key=lambda p: p[0].encode())
    blob = b"GLOSIC1\n" + struct.pack("<I", len(colour))
    for name, src in colour:
        if (src, 16) not in members or (src, 32) not in members:
            sys.exit("colour.list: Farm-Fresh has no %s at both sizes" % src)
        blob += name.encode().ljust(24, b"\0") + picture(z, members[(src, 16)], 16) + picture(z, members[(src, 32)], 32)
    open(dat, "wb").write(blob)

    fz = zipfile.ZipFile(awesome)
    root = [n for n in fz.namelist() if n.endswith("metadata/icons.json")][0].rsplit("metadata/", 1)[0]
    meta = json.loads(fz.read(root + "metadata/icons.json"))
    font = ImageFont.truetype(io.BytesIO(fz.read(root + "webfonts/fa-solid-900.ttf")), 14)
    rows = []
    for name, src in listed(lists + "/glyphs.list"):
        if src not in meta or "solid" not in meta[src].get("free", []):
            sys.exit("glyphs.list: Font Awesome Free has no solid %s" % src)
        ch = chr(int(meta[src]["unicode"], 16))
        box = font.getbbox(ch, mode="1", anchor="la")
        w, h = box[2] - box[0], box[3] - box[1]
        im = Image.new("1", (16, 16), 0)
        d = ImageDraw.Draw(im)
        d.fontmode = "1"
        d.text(((16 - w) // 2 - box[0], (16 - h) // 2 - box[1]), ch, fill=1, font=font, anchor="la")
        bits = []
        for y in range(16):
            v = 0
            for x in range(16):
                if im.getpixel((x, y)):
                    v |= 0x8000 >> x
            bits += [v >> 8, v & 0xFF]
        rows.append((name, src, bits))
    with open(glyphs_c, "w", encoding="ascii") as f:
        f.write("/* GLOS's one-colour glyphs (docs/gui.md 1.11): Font Awesome Free's solid icons at 14 pixels, each a 16 by\n"
                " * 16 mask after the four bytes that say so (GUI_MASK_MAGIC). Made by tools/mkicons.py from\n"
                " * sdk/gui/icons/glyphs.list; not to be edited: `make icons` makes it again. The icons' licence (CC BY 4.0)\n"
                " * and the font's (SIL OFL 1.1): THIRD_PARTY.md. */\n"
                "#include \"guiint.h\"\n\n"
                "const struct gui_glyph_def gui_glyphs[] = {\n")
        for name, src, bits in rows:
            f.write("    { \"%s\", { 0xFF, 'G', 'M', 0xFF,\n        %s } },    /* %s */\n"
                    % (name, ", ".join("0x%02x" % b for b in bits), src))
        f.write("};\nconst int gui_glyphs_n = %d;\n" % len(rows))
    print("mkicons: %d colour icons (%d bytes), %d glyphs" % (len(colour), len(blob), len(rows)))


if __name__ == "__main__":
    main()
