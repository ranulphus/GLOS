#!/usr/bin/env python3
"""PDF files for tests/host/pdf_test.c (apps/shared/pdf.c), written here byte by byte: small documents whose every
object is known.

    pdf_cases.py DIR      writes DIR/*.pdf; prints DIR"""
import os, struct, subprocess, sys, zlib

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
TEXT = (b"The quick brown fox jumps over the lazy dog. " * 20) + bytes(range(256))
ROWS = bytes((r * 7 + c * 13) & 255 for r in range(12) for c in range(16 * 3))     # 12 rows of 16 RGB pixels


def write(name, data):
    open(os.path.join(out, name), "wb").write(data)


def stream(d, data):
    """a dictionary's text (without << >>) and its bytes, as an object's body"""
    return b"<< " + d + b" /Length %d >>\nstream\n" % len(data) + data + b"\nendstream"


def classic(objs, root, extra=b"", version=b"1.4", prev=None, base=b"", size=None):
    """objs: {number: body}. A file (or, after `base`, an update of it) with a table of text."""
    b = bytearray(base or b"%PDF-" + version + b"\n%\xe2\xe3\xcf\xd3\n")
    where = {}
    for n in sorted(objs):
        where[n] = len(b)
        b += b"%d 0 obj\n" % n + objs[n] + b"\nendobj\n"
    at = len(b)
    top = size or max(objs) + 1
    b += b"xref\n"
    if prev is None:
        b += b"0 %d\n" % top + b"0000000000 65535 f \n"
        for n in range(1, top):
            b += b"%010d 00000 n \n" % where[n] if n in where else b"0000000000 00001 f \n"
    else:                                       # an update: a part of the table for each run of numbers
        for n in sorted(objs):
            b += b"%d 1\n%010d 00000 n \n" % (n, where[n])
    b += b"trailer\n<< /Size %d /Root %d 0 R %s" % (top, root, extra)
    if prev is not None:
        b += b" /Prev %d" % prev
    b += b" >>\nstartxref\n%d\n%%%%EOF\n" % at
    return bytes(b), at


def png_rows(data, row, how):
    """PNG's predictor done: how(r) says each row's kind"""
    o, bpp, prevrow = bytearray(), 3, bytes(row)
    for r in range(len(data) // row):
        cur, k = data[r * row:(r + 1) * row], how(r)
        o.append(k)
        for i in range(row):
            a = cur[i - bpp] if i >= bpp else 0
            u = prevrow[i]
            c = prevrow[i - bpp] if i >= bpp else 0
            if k == 0:
                p = 0
            elif k == 1:
                p = a
            elif k == 2:
                p = u
            elif k == 3:
                p = (a + u) // 2
            else:
                pp = a + u - c
                pa, pb, pc = abs(pp - a), abs(pp - u), abs(pp - c)
                p = a if pa <= pb and pa <= pc else u if pb <= pc else c
            o.append((cur[i] - p) & 255)
        prevrow = cur
    return bytes(o)


def lzw(data, early=1):
    table = {bytes([i]): i for i in range(256)}
    nxt, width, acc, bits, o, w = 258, 9, 0, 0, bytearray(), b""

    def put(code):
        nonlocal acc, bits
        acc = acc << width | code
        bits += width
        while bits >= 8:
            o.append(acc >> (bits - 8) & 255)
            bits -= 8

    put(256)
    for byte in data:
        wc = w + bytes([byte])
        if wc in table:
            w = wc
            continue
        put(table[w])
        table[wc] = nxt
        nxt += 1
        if nxt + early > (1 << width) and width < 12:
            width += 1
        if nxt == 4094:                         # (the table full: begun anew)
            put(256)
            table = {bytes([i]): i for i in range(256)}
            nxt, width = 258, 9
        w = bytes([byte])
    if w:
        put(table[w])
        nxt += 1
        if nxt + early > (1 << width) and width < 12:
            width += 1
    put(257)
    if bits:
        o.append(acc << (8 - bits) & 255)
    return bytes(o)


def a85(data):
    o = bytearray()
    for i in range(0, len(data), 4):
        chunk = data[i:i + 4]
        v = int.from_bytes(chunk.ljust(4, b"\0"), "big")
        if v == 0 and len(chunk) == 4:
            o += b"z"
            continue
        digits = bytearray()
        for _ in range(5):
            digits.insert(0, v % 85 + 33)
            v //= 85
        o += digits[:len(chunk) + 1]
        if i % 60 == 56:
            o += b"\n"
    return bytes(o) + b"~>"


def runlength(data):
    o, i = bytearray(), 0
    while i < len(data):
        j = i
        while j < len(data) and j - i < 128 and data[j] == data[i]:
            j += 1
        if j - i >= 3:
            o += bytes([257 - (j - i), data[i]])
            i = j
            continue
        j = i
        while j < len(data) and j - i < 128 and not (j + 2 < len(data) and data[j] == data[j + 1] == data[j + 2]):
            j += 1
        o += bytes([j - i - 1]) + data[i:j]
        i = j
    return bytes(o) + b"\x80"


# ---- a plain file: two pages, what the pages share said by their parent; words of every kind in its Info
page = lambda n: b"BT /F1 12 Tf 72 720 Td (Page %d) Tj ET" % n
plain = {
    1: b"<< /Type /Catalog /Pages 2 0 R >>",
    2: b"<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 /MediaBox [0 0 612 792] /Resources << /Font << /F1 7 0 R >> >> >>",
    3: b"<< /Type /Page /Parent 2 0 R /Contents 5 0 R >>",
    4: b"<< /Type /Page /Parent 2 0 R /Contents 6 0 R /Rotate 90 /MediaBox [0 0 200 100] >>",
    5: stream(b"", page(1)),
    6: b"<< /Length 8 0 R >>\nstream\n" + page(2) + b"\nendstream",        # (its length said by another object)
    7: b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
    8: b"%d" % len(page(2)),
    9: b"<< /Title (A \\(test\\) \\\\ \\101\\n(nested) end\\\nless) /Hex <48656C6C6F2> /Name#20With /A#42C % a comment\n"
       b"/Numbers [ -.5 +3. 1.25 0 -17 4294967296 ] /Flags [ true false null ] /Deep [[1 [2 [3]]] << /K /V >>] /Empty () >>",
}
doc, at = classic(plain, 1, b"/Info 9 0 R")
write("plain.pdf", doc)
# ... updated: page 1's words changed, by a part added at the file's end that names the table before it
update, _ = classic({5: stream(b"", page(11)), 10: b"(new)"}, 1, b"/Info 9 0 R", prev=at, base=doc, size=11)
write("update.pdf", update)
# ... its table lost (the number at its end points into its middle): made anew from the objects
write("broken.pdf", doc.replace(b"startxref\n%d" % at, b"startxref\n%d" % (at // 2)).replace(b"\nxref\n", b"\nxrex\n"))
# ... cut in its last object: what was before is read
write("cut.pdf", doc[:doc.index(b"9 0 obj") + 40])
# ... with an Encrypt entry: refused as locked
locked, _ = classic(plain, 1, b"/Encrypt << /Filter /Standard /V 1 /R 2 >>")
write("locked.pdf", locked)
write("nopdf.pdf", bytes((i * 37 + 11) & 0xFF for i in range(3000)))


# ---- a file of 1.5's kind: most objects packed in an object stream, the table a stream with a predictor
def packed_file(damage=False):
    inner = {1: b"<< /Type /Catalog /Pages 2 0 R >>",
             2: b"<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 /MediaBox [0 0 612 792] >>",
             3: b"<< /Type /Page /Parent 2 0 R /Contents 5 0 R >>",
             4: b"<< /Type /Page /Parent 2 0 R /Contents 6 0 R >>",
             9: b"<< /Title (Packed) >>"}
    index, body = b"", b""
    for n in sorted(inner):
        index += b"%d %d " % (n, len(body))
        body += inner[n] + b"\n"
    b = bytearray(b"%PDF-1.5\n%\xe2\xe3\xcf\xd3\n")
    where = {}
    for n, obj in ((5, stream(b"/Filter /FlateDecode", zlib.compress(page(1)))),
                   (6, stream(b"/Filter /FlateDecode", zlib.compress(page(2)))),
                   (7, stream(b"/Type /ObjStm /N %d /First %d /Filter /FlateDecode" % (len(inner), len(index)),
                              zlib.compress(index + body)))):
        where[n] = len(b)
        b += b"%d 0 obj\n" % n + obj + b"\nendobj\n"
    at = len(b)
    rows = bytearray()
    for n in range(10):                         # a row an object: its kind, two bytes of where, one of which
        if n in inner:
            rows += struct.pack(">BHB", 2, 7, sorted(inner).index(n))
        elif n in where:
            rows += struct.pack(">BHB", 1, where[n], 0)
        elif n == 8:
            rows += struct.pack(">BHB", 1, at, 0)
        else:
            rows += struct.pack(">BHB", 0, 0, 255)
    up, last = bytearray(), bytes(4)            # (PNG's "up": each row less the one above)
    for r in range(10):
        cur = rows[r * 4:r * 4 + 4]
        up += bytes([2]) + bytes((cur[i] - last[i]) & 255 for i in range(4))
        last = cur
    b += b"8 0 obj\n" + stream(b"/Type /XRef /Size 10 /W [1 2 1] /Root 1 0 R /Info 9 0 R /Filter /FlateDecode "
                               b"/DecodeParms << /Predictor 12 /Columns 4 >>", zlib.compress(bytes(up))) + b"\nendobj\n"
    b += b"startxref\n%d\n%%%%EOF\n" % (at + (7 if damage else 0))
    return bytes(b)


write("packed.pdf", packed_file())
write("brokenp.pdf", packed_file(damage=True))

# ---- streams packed every way: each is TEXT, or (the predictors') ROWS
flt = {1: b"<< /Type /Catalog /Pages 2 0 R >>", 2: b"<< /Type /Pages /Kids [] /Count 0 >>"}
kinds = [
    (b"/Is /Text /Filter /FlateDecode", zlib.compress(TEXT)),
    (b"/Is /Text /Filter [/ASCII85Decode /FlateDecode]", a85(zlib.compress(TEXT))),
    (b"/Is /Text /Filter /ASCIIHexDecode", TEXT.hex().encode() + b">"),
    (b"/Is /Text /Filter [/AHx /A85]", a85(TEXT).hex().upper().encode() + b" >"),
    (b"/Is /Text /Filter /LZWDecode", lzw(TEXT)),
    (b"/Is /Text /Filter /LZWDecode /DecodeParms << /EarlyChange 0 >>", lzw(TEXT, 0)),
    (b"/Is /Text /Filter /RunLengthDecode", runlength(TEXT)),
    (b"/Is /Text /Filter /A85", a85(TEXT)),
    (b"/Is /Rows /Filter /FlateDecode /DecodeParms << /Predictor 15 /Colors 3 /Columns 16 >>",
     zlib.compress(png_rows(ROWS, 48, lambda r: r % 5))),
    (b"/Is /Rows /Filter /FlateDecode /DecodeParms << /Predictor 2 /Colors 3 /Columns 16 >>",
     zlib.compress(bytes((ROWS[i] - (ROWS[i - 3] if i % 48 >= 3 else 0)) & 255 for i in range(len(ROWS))))),
    (b"/Is /Rows /Filter /LZWDecode /DecodeParms << /Predictor 12 /Colors 3 /Columns 16 >>", lzw(png_rows(ROWS, 48, lambda r: 2))),
    (b"/Is /Picture /Filter /DCTDecode", b"\xff\xd8 not really a picture \xff\xd9"),
    (b"/Is /Picture /Filter [/A85 /DCTDecode]", a85(b"\xff\xd8 not really a picture \xff\xd9")),
]
for i, (d, data) in enumerate(kinds):
    flt[3 + i] = stream(d, data)
n = 3 + len(kinds)
flt[n] = b"<< /Is /Text >>\nstream\n" + TEXT + b"\nendstream"                   # no length said
flt[n + 1] = b"<< /Is /Text /Length 17 >>\nstream\n" + TEXT + b"\nendstream"    # a wrong one
flt[n + 2] = b"<< /Is /Text /Filter /FlateDecode /Length 99999 >>\nstream\n" + zlib.compress(TEXT) + b"\nendstream"
write("filters.pdf", classic(flt, 1)[0])

# ---- a tree of pages three deep, seven pages; what a page has by its parents
tree = {1: b"<< /Type /Catalog /Pages 2 0 R >>",
        2: b"<< /Type /Pages /Kids [3 0 R 4 0 R 20 0 R] /Count 7 /MediaBox [0 0 612 792] /Resources << /A 1 >> >>",
        3: b"<< /Type /Pages /Parent 2 0 R /Kids [10 0 R 11 0 R 5 0 R] /Count 4 /CropBox [10 10 600 780] >>",
        4: b"<< /Type /Pages /Parent 2 0 R /Kids [14 0 R 15 0 R] /Count 2 >>",
        5: b"<< /Type /Pages /Parent 3 0 R /Kids [12 0 R 13 0 R] /Count 2 /Rotate 180 >>"}
for i, (num, parent) in enumerate(((10, 3), (11, 3), (12, 5), (13, 5), (14, 4), (15, 4), (20, 2))):
    tree[num] = b"<< /Type /Page /Parent %d 0 R /Number %d >>" % (parent, i)
write("tree.pdf", classic(tree, 1)[0])

# ---- drawing (tests/host/pdfdraw_test.c): a page 200 by 100 points with a shape of every kind at a known place
K = 0.5523                                                      # (a quarter circle by one curve)
circle = lambda x, y, r: (b"%g %g m " % (x + r, y) + b"%g %g %g %g %g %g c " % (x + r, y + K * r, x + K * r, y + r, x, y + r)
                          + b"%g %g %g %g %g %g c " % (x - K * r, y + r, x - r, y + K * r, x - r, y)
                          + b"%g %g %g %g %g %g c " % (x - r, y - K * r, x - K * r, y - r, x, y - r)
                          + b"%g %g %g %g %g %g c h " % (x + K * r, y - r, x + r, y - K * r, x + r, y))
words = b"\n".join([
    b"1 0 0 rg 10 10 50 30 re f",                                # a red box
    b"0 0 1 RG 4 w 100 20 m 180 20 l S",                         # a blue line, four wide
    b"0 1 0 rg 100 50 m 140 90 l 180 50 l h f",                  # a green triangle
    b"0 g 20 60 30 30 re 30 70 10 10 re f*",                     # a black box with a hole (even-odd)
    b"q 150 5 20 10 re W n 1 0 1 rg 140 0 60 40 re f Q",         # magenta, clipped to a small box
    b"q 2 0 0 2 0 0 cm 0.5 g 0 0 4 4 re f Q",                    # gray, the matrix doubled
    b"0 1 1 rg " + circle(80, 75, 12) + b"f",                    # a cyan disc
    b"q 20 0 0 20 150 60 cm /Im1 Do Q",                          # four colours, a pixel each
    b"q 16 0 0 16 180 80 cm /Im2 Do Q",                          # an indexed picture
    b"q 1 0.5 0 rg 8 0 0 8 60 2 cm /Im3 Do Q",                   # a stencil in orange
    b"q /G1 gs 0 0 1 rg 12 12 20 10 re f Q",                     # blue at half strength over the red
    b"q 0 0 0 0 k 190 2 8 8 re f 0 1 0 0 k 190 2 4 4 re f Q",    # CMYK: white, then magenta
    b"q 10 0 0 10 120 2 cm /Fm1 Do Q",                           # a form: a unit box of its own colour
])
draw = {
    1: b"<< /Type /Catalog /Pages 2 0 R >>",
    2: b"<< /Type /Pages /Kids [3 0 R 10 0 R 11 0 R] /Count 3 >>",
    3: b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] /Contents [4 0 R] /Resources << /XObject << /Im1 5 0 R /Im2 6 0 R "
       b"/Im3 7 0 R /Fm1 8 0 R >> /ExtGState << /G1 << /ca 0.5 >> >> >> >>",
    4: stream(b"/Filter /FlateDecode", zlib.compress(words)),
    5: stream(b"/Type /XObject /Subtype /Image /Width 2 /Height 2 /BitsPerComponent 8 /ColorSpace /DeviceRGB",
              bytes([255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0])),
    6: stream(b"/Type /XObject /Subtype /Image /Width 2 /Height 1 /BitsPerComponent 4 /ColorSpace [/Indexed /DeviceRGB 2 <000000 800000 008000>]",
              bytes([0x12])),
    7: stream(b"/Type /XObject /Subtype /Image /Width 8 /Height 2 /ImageMask true", bytes([0xF0, 0x0F])),
    8: stream(b"/Type /XObject /Subtype /Form /BBox [0 0 1 1] /Matrix [1 0 0 1 0 0]", b"0.25 0.5 0.75 rg 0 0 1 1 re f"),
    # the same page turned a quarter, and one with a box of its own to show (a part of the page)
    10: b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] /Rotate 90 /Contents 4 0 R /Resources << >> >>",
    11: b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 100] /CropBox [100 0 200 50] /Contents 4 0 R /Resources << >> >>",
}
write("draw.pdf", classic(draw, 1)[0])

# ---- text (tests/host/pdfdraw_test.c): a page 300 by 200 points
def glyph_of(font, code):
    """a character's glyph in a TrueType font, by its Unicode map of format 4 (the stand-in fonts have that alone)"""
    n = struct.unpack(">H", font[4:6])[0]
    for i in range(n):
        tag, _, off, length = struct.unpack(">4sIII", font[12 + 16 * i:28 + 16 * i])
        if tag == b"cmap":
            sub = font[off + 12:off + length]
            segs = struct.unpack(">H", sub[6:8])[0] // 2
            ends = struct.unpack(">%dH" % segs, sub[14:14 + 2 * segs])
            starts = struct.unpack(">%dH" % segs, sub[16 + 2 * segs:16 + 4 * segs])
            deltas = struct.unpack(">%dH" % segs, sub[16 + 4 * segs:16 + 6 * segs])
            for k in range(segs):
                if starts[k] <= code <= ends[k]:
                    return (code + deltas[k]) & 0xFFFF
    return 0


fonts_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "apps", "pdf", "fonts")
serif = open(os.path.join(fonts_dir, "PDFSERI.TTF"), "rb").read()
mono = open(os.path.join(fonts_dir, "PDFMONOB.TTF"), "rb").read()
glyphs = b"".join(struct.pack(">H", glyph_of(mono, ord(ch))) for ch in "GLOS 2.10")
words = b"\n".join([
    b"BT /F1 24 Tf 20 165 Td (Hello, GLOS!) Tj ET",                                  # Helvetica, not carried: a stand-in
    b"BT /F2 12 Tf 20 140 Td [(The quick) -250 (brown fox, kerned: A) 80 (V) 80 (A)] TJ ET",
    b"q BT /F3 10 Tf 20 122 Td 1 Tc 4 Tw (Courier, spaced out) Tj ET Q",              # (the spacings stay until Q)
    b"BT /F4 12 Tf 14 TL 20 104 Td (Bold: caf\\351 na\\357ve \\200 5) Tj T* /F5 12 Tf (Italic, on the next line) Tj ET",
    b"BT /F6 14 Tf 20 60 Td (A font the file carries) Tj ET",                         # TrueType, in the file
    b"BT /F7 14 Tf 20 40 Td <" + glyphs.hex().encode() + b"> Tj ET",                  # ... and one by glyph numbers
    b"BT /F1 12 Tf 0 1 -1 0 280 20 Tm (Turned) Tj ET",                                # a quarter turned
    b"q BT /F1 12 Tf 3 Tr 150 20 Td (Unseen) Tj ET Q",                                # not painted
    b"0 0 1 rg BT /F1 9 Tf 150 8 Td (small and blue) Tj ET",
])
text = {
    1: b"<< /Type /Catalog /Pages 2 0 R >>",
    2: b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
    3: b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 200] /Contents 4 0 R /Resources << /Font << /F1 5 0 R /F2 6 0 R /F3 7 0 R "
       b"/F4 8 0 R /F5 9 0 R /F6 10 0 R /F7 13 0 R >> >> >>",
    4: stream(b"", words),
    5: b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>",
    6: b"<< /Type /Font /Subtype /Type1 /BaseFont /Times-Roman /Encoding /WinAnsiEncoding >>",
    7: b"<< /Type /Font /Subtype /Type1 /BaseFont /Courier >>",
    8: b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>",
    9: b"<< /Type /Font /Subtype /Type1 /BaseFont /Times-Italic /Encoding /WinAnsiEncoding >>",
    10: b"<< /Type /Font /Subtype /TrueType /BaseFont /ABCDEF+Carried /Encoding /WinAnsiEncoding /FirstChar 32 /LastChar 126 /Widths ["
        + b" ".join(b"%d" % 500 for _ in range(95)) + b"] /FontDescriptor 11 0 R >>",
    11: b"<< /Type /FontDescriptor /FontName /ABCDEF+Carried /Flags 32 /FontFile2 12 0 R >>",
    12: stream(b"/Filter /FlateDecode /Length1 %d" % len(serif), zlib.compress(serif)),
    13: b"<< /Type /Font /Subtype /Type0 /BaseFont /GHIJKL+Numbered /Encoding /Identity-H /DescendantFonts [14 0 R] >>",
    14: b"<< /Type /Font /Subtype /CIDFontType2 /BaseFont /GHIJKL+Numbered /CIDToGIDMap /Identity /DW 600 /FontDescriptor 15 0 R >>",
    15: b"<< /Type /FontDescriptor /FontName /GHIJKL+Numbered /Flags 4 /FontFile2 16 0 R >>",
    16: stream(b"/Filter /FlateDecode /Length1 %d" % len(mono), zlib.compress(mono)),
}
write("text.pdf", classic(text, 1)[0])

# ---- fonts of the PostScript kinds, carried in the file (tests/host/pdfdraw_test.c): psfont_cases.py's made-up
#      fonts, whose shapes are known, each way a PDF may carry and use one. A page 300 by 200 points; the fonts'
#      streams as they are, so that the test's changed bytes fall in them too.
subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)), "psfont_cases.py"), out], check=True,
               stdout=subprocess.DEVNULL)
cff = open(os.path.join(out, "SYNTH.CFF"), "rb").read()
pfa = open(os.path.join(out, "SYNTH.PFA"), "rb").read()
# an OpenType font of the PostScript kind: the CFF font as its one table
otto = b"OTTO" + struct.pack(">HHHH", 1, 16, 0, 0) + b"CFF " + struct.pack(">III", 0, 28, len(cff)) + cff + b"\0" * (-len(cff) % 4)
clear = pfa.index(b"eexec") + 6
words = b"\n".join([
    b"BT /F1 50 Tf 20 140 Td (AB\\301) Tj ET",          # Type 1, by its own encoding: A, B, A with an acute accent
    b"BT /F2 50 Tf 20 80 Td (Aab) Tj ET",               # CFF: A by its own encoding, then two the Differences name
    b"BT /F3 50 Tf 190 80 Td <00010002> Tj ET",         # CFF in a composite font: by the glyphs' numbers
    b"BT /F4 50 Tf 190 140 Td (A) Tj ET",               # CFF within an OpenType font, by a named table's character
    b"BT /F5 30 Tf 20 20 Td (A) Tj ET",                 # a Type 1 font nothing can be made of: a stand-in draws
])
fonts = {
    1: b"<< /Type /Catalog /Pages 2 0 R >>",
    2: b"<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
    3: b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 200] /Contents 4 0 R /Resources << /Font << /F1 5 0 R /F2 8 0 R /F3 11 0 R "
       b"/F4 15 0 R /F5 18 0 R >> >> >>",
    4: stream(b"", words),
    5: b"<< /Type /Font /Subtype /Type1 /BaseFont /ABCDEF+Synth /FirstChar 65 /LastChar 193 /Widths ["
       + b" ".join(b"800" for _ in range(129)) + b"] /FontDescriptor 6 0 R >>",
    6: b"<< /Type /FontDescriptor /FontName /ABCDEF+Synth /Flags 4 /FontFile 7 0 R >>",
    7: stream(b"/Length1 %d /Length2 %d /Length3 0" % (clear, len(pfa) - clear), pfa),
    8: b"<< /Type /Font /Subtype /Type1 /BaseFont /BCDEFG+Synth /FirstChar 65 /LastChar 98 /Widths ["
       + b" ".join(b"1000" for _ in range(34)) + b"] /Encoding << /Type /Encoding /Differences [97 /blob /B] >> /FontDescriptor 9 0 R >>",
    9: b"<< /Type /FontDescriptor /FontName /BCDEFG+Synth /Flags 4 /FontFile3 10 0 R >>",
    10: stream(b"/Subtype /Type1C", cff),
    11: b"<< /Type /Font /Subtype /Type0 /BaseFont /CDEFGH+Synth /Encoding /Identity-H /DescendantFonts [12 0 R] >>",
    12: b"<< /Type /Font /Subtype /CIDFontType0 /BaseFont /CDEFGH+Synth /DW 1000 /FontDescriptor 13 0 R >>",
    13: b"<< /Type /FontDescriptor /FontName /CDEFGH+Synth /Flags 4 /FontFile3 14 0 R >>",
    14: stream(b"/Subtype /CIDFontType0C", cff),
    15: b"<< /Type /Font /Subtype /Type1 /BaseFont /DEFGHI+Synth /Encoding /WinAnsiEncoding /FirstChar 65 /LastChar 66 "
        b"/Widths [1000 1000] /FontDescriptor 16 0 R >>",
    16: b"<< /Type /FontDescriptor /FontName /DEFGHI+Synth /Flags 32 /FontFile3 17 0 R >>",
    17: stream(b"/Subtype /OpenType", otto),
    18: b"<< /Type /Font /Subtype /Type1 /BaseFont /EFGHIJ+Helvetica /Encoding /WinAnsiEncoding /FirstChar 65 /LastChar 65 "
        b"/Widths [667] /FontDescriptor 19 0 R >>",
    19: b"<< /Type /FontDescriptor /FontName /EFGHIJ+Helvetica /Flags 32 /FontFile 20 0 R >>",
    20: stream(b"", b"%!PS-AdobeFont-1.0: nothing of use\n" + bytes(range(256))),
}
write("fonts.pdf", classic(fonts, 1)[0])
print(out)
