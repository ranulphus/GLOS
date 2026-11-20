#!/usr/bin/env python3
"""The PDF Viewer's stand-in fonts (apps/pdf/fonts/*.TTF), made from the Liberation fonts (SIL OFL 1.1; metrics as
Helvetica's, Times's and Courier's, which is why PDFs that embed no font look right in them): each cut down to the
letters a PDF's standard encodings name (Latin-1, Windows' extras, a few ligatures and signs), its glyphs
renumbered, and only the tables a reader needs kept (cmap, glyf, head, hhea, hmtx, loca, maxp). The OFL reserves
the names Liberation, Arimo, Tinos and Cousine: what is made here is a changed font and carries none of them (no
name table at all; the files are PDFSANS.TTF and so on). Kept in the repository: GLOS's build needs no fonts.

    mkpdffonts.py SOURCE-DIR OUT-DIR      (/usr/share/fonts/truetype/liberation apps/pdf/fonts)
    mkpdffonts.py --pack FONTS-DIR FILE   the twelve as one file, PDFFONTS.DAT, as the program reads them (the build's:
                                          "GPDF", twelve, then each one's place and length, family * 4 + bold * 2 +
                                          italic; then the fonts)

No library is needed: a TrueType file's tables are read and written here."""
import hashlib, os, struct, sys

FACES = [("LiberationSans", "PDFSANS"), ("LiberationSerif", "PDFSERI"), ("LiberationMono", "PDFMONO")]
STYLES = [("Regular", ""), ("Italic", "I"), ("Bold", "B"), ("BoldItalic", "Z")]          # (in the packed file's order)
if sys.argv[1] == "--pack":
    fonts = [open(os.path.join(sys.argv[2], short + letter + ".TTF"), "rb").read() for _, short in FACES for _, letter in STYLES]
    at, table = 8 + 8 * len(fonts), b""
    for f in fonts:
        table += struct.pack("<II", at, len(f))
        at += len(f)
    open(sys.argv[3], "wb").write(b"GPDF" + struct.pack("<I", len(fonts)) + table + b"".join(fonts))
    sys.exit(0)
src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
WANT = (list(range(0x20, 0x7F)) + list(range(0xA0, 0x100))
        + [0x152, 0x153, 0x160, 0x161, 0x178, 0x17D, 0x17E, 0x192, 0x2C6, 0x2DC, 0x2013, 0x2014, 0x2018, 0x2019, 0x201A, 0x201C,
           0x201D, 0x201E, 0x2020, 0x2021, 0x2022, 0x2026, 0x2030, 0x2039, 0x203A, 0x20AC, 0x2122, 0x2212, 0xFB01, 0xFB02,
           0x131, 0x141, 0x142, 0x2D8, 0x2D9, 0x2DA, 0x2DB, 0x2DD, 0x2C7, 0x2044, 0x3A9, 0x3BC, 0x3C0, 0x2202, 0x2206, 0x220F,
           0x2211, 0x221A, 0x221E, 0x222B, 0x2248, 0x2260, 0x2264, 0x2265, 0x25CA])


def tables(data):
    n = struct.unpack(">H", data[4:6])[0]
    t = {}
    for i in range(n):
        tag, _, off, length = struct.unpack(">4sIII", data[12 + 16 * i:28 + 16 * i])
        t[tag.decode()] = data[off:off + length]
    return t


def cmap4(cmap):
    """code -> glyph, from the Unicode subtable of format 4"""
    n = struct.unpack(">H", cmap[2:4])[0]
    for i in range(n):
        pid, eid, off = struct.unpack(">HHI", cmap[4 + 8 * i:12 + 8 * i])
        if (pid, eid) in ((3, 1), (0, 3)) and struct.unpack(">H", cmap[off:off + 2])[0] == 4:
            sub = cmap[off:]
            segs = struct.unpack(">H", sub[6:8])[0] // 2
            ends = struct.unpack(">%dH" % segs, sub[14:14 + 2 * segs])
            starts = struct.unpack(">%dH" % segs, sub[16 + 2 * segs:16 + 4 * segs])
            deltas = struct.unpack(">%dH" % segs, sub[16 + 4 * segs:16 + 6 * segs])
            ranges_at = 16 + 6 * segs
            ranges = struct.unpack(">%dH" % segs, sub[ranges_at:ranges_at + 2 * segs])
            m = {}
            for s in range(segs):
                for c in range(starts[s], ends[s] + 1):
                    if c == 0xFFFF:
                        continue
                    if ranges[s]:
                        at = ranges_at + 2 * s + ranges[s] + 2 * (c - starts[s])
                        g = struct.unpack(">H", sub[at:at + 2])[0]
                        g = (g + deltas[s]) & 0xFFFF if g else 0
                    else:
                        g = (c + deltas[s]) & 0xFFFF
                    if g:
                        m[c] = g
            return m
    raise SystemExit("no Unicode cmap of format 4")


def parts(glyph):
    """a composite glyph's parts: (where its glyph number is written, the number)"""
    if len(glyph) < 10 or struct.unpack(">h", glyph[:2])[0] >= 0:
        return []
    at, found = 10, []
    while True:
        flags, g = struct.unpack(">HH", glyph[at:at + 4])
        found.append((at + 2, g))
        at += 4 + (4 if flags & 1 else 2) + (2 if flags & 8 else 4 if flags & 0x40 else 8 if flags & 0x80 else 0)
        if not flags & 0x20:
            return found


def cut(path):
    data = open(path, "rb").read()
    t = tables(data)
    long_loca = struct.unpack(">h", t["head"][50:52])[0]
    nglyphs = struct.unpack(">H", t["maxp"][4:6])[0]
    loca = struct.unpack(">%dI" % (nglyphs + 1), t["loca"]) if long_loca else \
        [2 * v for v in struct.unpack(">%dH" % (nglyphs + 1), t["loca"])]
    glyph = lambda g: t["glyf"][loca[g]:loca[g + 1]]
    code_to = cmap4(t["cmap"])
    keep, todo = {0}, [code_to[c] for c in WANT if c in code_to]
    while todo:                                 # ... and the glyphs those are put together from
        g = todo.pop()
        if g not in keep:
            keep.add(g)
            todo += [p for _, p in parts(glyph(g))]
    order = sorted(keep)
    new = {g: i for i, g in enumerate(order)}
    glyf, where = bytearray(), []
    for g in order:
        where.append(len(glyf))
        body = bytearray(glyph(g))
        for at, p in parts(body):
            struct.pack_into(">H", body, at, new[p])
        glyf += body + bytes(-len(body) % 4)
    where.append(len(glyf))
    nlong = struct.unpack(">H", t["hhea"][34:36])[0]
    hmtx = bytearray()
    for g in order:                             # every glyph with its own advance
        adv, lsb = struct.unpack(">Hh", t["hmtx"][4 * g:4 * g + 4]) if g < nlong else \
            (struct.unpack(">H", t["hmtx"][4 * nlong - 4:4 * nlong - 2])[0],
             struct.unpack(">h", t["hmtx"][4 * nlong + 2 * (g - nlong):4 * nlong + 2 * (g - nlong) + 2])[0])
        hmtx += struct.pack(">Hh", adv, lsb)
    head = bytearray(t["head"])
    struct.pack_into(">I", head, 8, 0)          # (no checksum: nothing here reads it)
    struct.pack_into(">h", head, 50, 1)
    hhea = bytearray(t["hhea"])
    struct.pack_into(">H", hhea, 34, len(order))
    maxp = bytearray(t["maxp"])
    struct.pack_into(">H", maxp, 4, len(order))
    codes = sorted(c for c in WANT if c in code_to)     # a cmap of format 4: a segment a code (few enough)
    segs = len(codes) + 1
    log = segs.bit_length() - 1                 # (the numbers a reader's search of the segments goes by)
    sub = struct.pack(">HHHHHHH", 4, 16 + 8 * segs, 0, 2 * segs, 2 << log, log, 2 * segs - (2 << log))
    sub += struct.pack(">%dH" % segs, *codes, 0xFFFF) + b"\0\0" + struct.pack(">%dH" % segs, *codes, 0xFFFF)
    sub += struct.pack(">%dH" % segs, *[(new[code_to[c]] - c) & 0xFFFF for c in codes], 1) + bytes(2 * segs)
    cmap = struct.pack(">HHHHI", 0, 1, 3, 1, 12) + sub
    made = {"cmap": cmap, "glyf": bytes(glyf), "head": bytes(head), "hhea": bytes(hhea), "hmtx": bytes(hmtx),
            "loca": struct.pack(">%dI" % len(where), *where), "maxp": bytes(maxp)}
    b = bytearray(struct.pack(">IHHHH", 0x00010000, len(made), 64, 2, len(made) * 16 - 64))
    at = 12 + 16 * len(made)
    body = bytearray()
    for tag in sorted(made):
        b += struct.pack(">4sIII", tag.encode(), 0, at + len(body), len(made[tag]))
        body += made[tag] + bytes(-len(made[tag]) % 4)
    return bytes(b + body), len(order), hashlib.sha256(data).hexdigest()


for face, short in FACES:
    for style, letter in STYLES:
        name = "%s-%s.ttf" % (face, style)
        data, n, digest = cut(os.path.join(src, name))
        open(os.path.join(out, short + letter + ".TTF"), "wb").write(data)
        print("%s%s.TTF %d glyphs %d bytes from %s sha256 %s" % (short, letter, n, len(data), name, digest))
