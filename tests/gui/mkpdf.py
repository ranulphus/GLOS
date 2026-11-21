#!/usr/bin/env python3
"""The PDF files of the GUI scripts' drive (tests/gui/drives/C/GLOS: pdf.txt), made once and kept in the tree; run
again only to change them.

    python3 tests/gui/mkpdf.py

GUIDE.PDF: two pages of the usual size (612 by 792 points): a title, lines of text in the standard fonts (which
the file doesn't carry: GLOS's stand-ins draw them), a few shapes; its words packed (Flate). LOCKED.PDF: the same
with an Encrypt entry (refused)."""
import os, zlib

here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "drives", "C", "GLOS")
os.makedirs(here, exist_ok=True)


def stream(d, data):
    return b"<< " + d + b" /Length %d >>\nstream\n" % len(data) + data + b"\nendstream"


def pdf(objs, extra=b""):
    b = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
    where = {}
    for n in sorted(objs):
        where[n] = len(b)
        b += b"%d 0 obj\n" % n + objs[n] + b"\nendobj\n"
    at, top = len(b), max(objs) + 1
    b += b"xref\n0 %d\n0000000000 65535 f \n" % top + b"".join(b"%010d 00000 n \n" % where[n] for n in range(1, top))
    b += b"trailer\n<< /Size %d /Root 1 0 R %s >>\nstartxref\n%d\n%%%%EOF\n" % (top, extra, at)
    return bytes(b)


lines = [b"GLOS reads PDF files with a reader and a renderer of its own.", b"Text is drawn in the fonts a file carries, or in stand-ins",
         b"for Helvetica, Times and Courier, as here.", b"", b"Paths are filled and stroked with smoothed edges;",
         b"pictures are drawn through any matrix."]
one = b"BT /F1 28 Tf 72 700 Td (A Guide to Nothing Much) Tj ET\n0.2 0.4 0.7 RG 2 w 72 690 m 540 690 l S\n"
one += b"BT /F2 12 Tf 15 TL 72 660 Td " + b" ".join(b"(%s) Tj T*" % l for l in lines) + b" ET\n"
one += b"0.9 0.3 0.2 rg 72 440 120 80 re f 0.2 0.6 0.3 rg 232 440 m 292 520 l 352 440 l h f\n"
one += b"0.2 0.3 0.8 rg 452 480 m 452 502 434 520 412 520 c 390 520 372 502 372 480 c 372 458 390 440 412 440 c 434 440 452 458 452 480 c f\n"
one += b"BT /F3 10 Tf 72 410 Td (A box, a triangle and a disc; this line is in the mono.) Tj ET\n"
one += b"BT /F4 11 Tf 72 80 Td (Page 1) Tj ET\n"
two = b"BT /F1 20 Tf 72 700 Td (The Second Page) Tj ET\n"
two += b"BT /F5 12 Tf 15 TL 72 660 Td (Italic serif, and below it bold sans:) Tj T* /F4 12 Tf (caf\\351, na\\357ve, \\200 5.00) Tj ET\n"
two += b"0 G 0.5 w " + b" ".join(b"%d 560 m %d 600 l S" % (72 + 12 * i, 72 + 12 * i + 20) for i in range(30)) + b"\n"
two += b"BT /F4 11 Tf 72 80 Td (Page 2) Tj ET\n"
fonts = b"<< /F1 6 0 R /F2 7 0 R /F3 8 0 R /F4 9 0 R /F5 10 0 R >>"
objs = {
    1: b"<< /Type /Catalog /Pages 2 0 R /Outlines 12 0 R >>",
    2: b"<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 /MediaBox [0 0 612 792] /Resources << /Font " + fonts + b" >> >>",
    3: b"<< /Type /Page /Parent 2 0 R /Contents 5 0 R >>",
    4: b"<< /Type /Page /Parent 2 0 R /Contents 11 0 R >>",
    5: stream(b"/Filter /FlateDecode", zlib.compress(one)),
    6: b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>",
    7: b"<< /Type /Font /Subtype /Type1 /BaseFont /Times-Roman /Encoding /WinAnsiEncoding >>",
    8: b"<< /Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding /WinAnsiEncoding >>",
    9: b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>",
    10: b"<< /Type /Font /Subtype /Type1 /BaseFont /Times-Italic /Encoding /WinAnsiEncoding >>",
    11: stream(b"/Filter /FlateDecode", zlib.compress(two)),
    # the outline: the first page's title with an entry under it (shown open), and the second page's
    12: b"<< /Type /Outlines /First 13 0 R /Last 15 0 R /Count 3 >>",
    13: b"<< /Title (A Guide to Nothing Much) /Parent 12 0 R /Next 15 0 R /First 14 0 R /Last 14 0 R /Count 1 /Dest [3 0 R /Fit] >>",
    14: b"<< /Title (The shapes) /Parent 13 0 R /Dest [3 0 R /XYZ 72 520 0] >>",
    15: b"<< /Title (The Second Page) /Parent 12 0 R /Prev 13 0 R /A << /S /GoTo /D [4 0 R /Fit] >> >>",
}
open(os.path.join(here, "GUIDE.PDF"), "wb").write(pdf(objs))
open(os.path.join(here, "LOCKED.PDF"), "wb").write(pdf(objs, b"/Encrypt << /Filter /Standard /V 1 /R 2 >>"))
print(here)
