#!/usr/bin/env python3
"""Fonts of the PostScript kinds for tests/host/psfont_test.c (apps/shared/psfont.c), written here byte by byte: a
CFF font and a Type 1 font of a few glyphs each, whose every point is known, and which between them use every
operator the reader knows. (No font of anyone's: these are made up.)

    psfont_cases.py DIR      writes DIR/SYNTH.CFF, DIR/SYNTH.PFA; prints DIR"""
import os, struct, sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)


# ---- a CFF font: .notdef, A (a triangle), B (a box with a round hole), blob (curves of every kind)
def num(v):
    """a number of a Type 2 program, or of a dictionary"""
    if -107 <= v <= 107:
        return bytes([v + 139])
    return bytes([28]) + struct.pack(">h", v)


def prog(*words):
    b = bytearray()
    for w in words:
        b += num(w) if isinstance(w, int) else w
    return bytes(b)


RMOVETO, HMOVETO, VMOVETO, RLINETO, HLINETO, VLINETO, RRCURVETO, ENDCHAR = (bytes([c]) for c in (21, 22, 4, 5, 6, 7, 8, 14))
CALLSUBR, CALLGSUBR, RETURN, HSTEM, VSTEM, HSTEMHM, HINTMASK = (bytes([c]) for c in (10, 29, 11, 1, 3, 18, 19))
RCURVELINE, RLINECURVE, VVCURVETO, HHCURVETO, VHCURVETO, HVCURVETO = (bytes([c]) for c in (24, 25, 26, 27, 30, 31))
HFLEX, FLEX, HFLEX1, FLEX1 = (bytes([12, c]) for c in (34, 35, 36, 37))
K = 55                                                          # (a quarter circle of radius 100 by one curve)
# a local subroutine (number 0: called as -107): a circle of radius 100 from its right, down and round to where it
# began: the other way round from the box it is drawn in, as a letter's hole is
circle = prog(0, -K, K - 100, K - 100, -K, 0, RRCURVETO, -K, 0, K - 100, 100 - K, 0, K, RRCURVETO,
              0, K, 100 - K, 100 - K, K, 0, RRCURVETO, K, 0, 100 - K, K - 100, 0, -K, RRCURVETO, RETURN)
# a global subroutine (number 0): a line up and to the right
gsub = prog(50, 50, RLINETO, RETURN)
glyphs = [
    prog(ENDCHAR),
    # A: its width (500 less the nominal 400) before the first move's two numbers
    prog(100, 100, 0, RMOVETO, 300, 700, 300, -700, RLINETO, ENDCHAR),
    # B: hints (two stems across, one up) and a mask of one byte; a box by hlineto's turns; a round hole (its right
    #    point at 450, 350) by the subroutine
    prog(0, 50, 600, 50, HSTEMHM, 50, 600, HINTMASK, bytes([0xE0]), 50, 50, RMOVETO, 600, 600, -600, HLINETO,
         400, -300, RMOVETO, -107, CALLSUBR, ENDCHAR),
]
# blob: from (0, 0): each kind of curve in turn; where it has come to after each is in the test
blob = prog(0, HMOVETO,
            10, 20, 30, 40, 50, HHCURVETO,                      # to (100, 50)
            10, 20, 30, 40, 50, VVCURVETO,                      # to (140, 160)
            10, 20, 30, 40, HVCURVETO,                          # to (170, 230)
            10, 20, 30, 40, VHCURVETO,                          # to (230, 270)
            1, 2, 3, 4, 5, 6, 10, 20, RCURVELINE,               # to (239, 282), then (249, 302)
            10, 20, 1, 2, 3, 4, 5, 6, RLINECURVE,               # to (259, 322), then (268, 334)
            10, 20, 30, 40, 50, 60, 70, HFLEX,                  # to (338, 364), then (518, 334)
            1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 50, FLEX,    # to (527, 346), then (554, 376)
            10, 20, 30, 40, 50, 60, 70, 80, 90, HFLEX1,         # to (644, 436), then (864, 376)
            10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, FLEX1,        # to (954, 496), then (864, 786): it goes up
            -107, CALLGSUBR,                                    # to (914, 836)
            -200, VLINETO, ENDCHAR)                             # to (914, 636)
glyphs.append(blob)


def index(items):
    if not items:
        return b"\0\0"
    b, at = bytearray(struct.pack(">HB", len(items), 2)), 1
    for it in items:
        b += struct.pack(">H", at)
        at += len(it)
    return bytes(b) + struct.pack(">H", at) + b"".join(items)


def big(v):
    return bytes([29]) + struct.pack(">i", v)


def cff():
    head = bytes([1, 0, 4, 1])
    names, strings, gsubrs = index([b"Synth"]), index([b"blob"]), index([gsub])
    charset = bytes([0]) + struct.pack(">HHH", 34, 35, 391)     # A, B, and the font's own first string
    encoding = bytes([0, 3, 65, 66, 0x80])
    chars = index(glyphs)
    subrs = index([circle])
    private = prog(400, bytes([20]), 400, bytes([21])) + big(0) + bytes([19])   # the two widths; the subroutines after it
    private = private[:-6] + big(len(private)) + bytes([19])
    top_len = len(big(0) + bytes([15]) + big(0) + bytes([16]) + big(0) + bytes([17]) + big(0) + big(0) + bytes([18]))
    at = len(head) + len(names) + len(index([bytes(top_len)])) + len(strings) + len(gsubrs)
    top = big(at) + bytes([15])
    at += len(charset)
    top += big(at) + bytes([16])
    at += len(encoding)
    top += big(at) + bytes([17])
    at += len(chars)
    top += big(len(private)) + big(at) + bytes([18])
    return head + names + index([top]) + strings + gsubrs + charset + encoding + chars + private + subrs


open(os.path.join(out, "SYNTH.CFF"), "wb").write(cff())


# ---- a Type 1 font: .notdef, A, acute, Aacute (A and acute put together), B (curves, a subroutine, a flex)
def crypt(data, r):
    o = bytearray()
    for c in data:
        e = c ^ (r >> 8)
        o.append(e)
        r = ((e + r) * 52845 + 22719) & 0xFFFF
    return bytes(o)


def n1(v):
    if -107 <= v <= 107:
        return bytes([v + 139])
    return bytes([255]) + struct.pack(">i", v)


def prog1(*words):
    b = bytearray()
    for w in words:
        b += n1(w) if isinstance(w, int) else w
    return bytes(b)


HSBW, CLOSEPATH, ENDCHAR1, RMOVETO1, HMOVETO1, VMOVETO1, RLINETO1, HLINETO1, VLINETO1, RRCURVETO1 = (
    bytes([c]) for c in (13, 9, 14, 21, 22, 4, 5, 6, 7, 8))
VHCURVETO1, HVCURVETO1, CALLSUBR1, RETURN1, HSTEM1 = (bytes([c]) for c in (30, 31, 10, 11, 1))
SEAC, DIV, CALLOTHER, POP, SETCURRENT = (bytes([12, c]) for c in (6, 12, 16, 17, 33))
subrs1 = [
    prog1(100, 0, RLINETO1, RETURN1),                           # 0: a line to the right
    prog1(0, 1, CALLOTHER, RETURN1),                            # 1: a flex begins
    prog1(0, 2, CALLOTHER, RETURN1),                            # 2: a point of it
]
flex = (prog1(1, CALLSUBR1, 50, 20, RMOVETO1, 2, CALLSUBR1)     # the middle (noted, not used), then six points
        + b"".join(prog1(dx, dy, RMOVETO1, 2, CALLSUBR1) for dx, dy in ((-40, 0), (20, 20), (20, 0), (20, 0), (20, -20), (10, 0)))
        + prog1(50, 300, 100, 3, 0, CALLOTHER, POP, POP, SETCURRENT))
chars1 = [
    (b".notdef", prog1(0, 500, HSBW, ENDCHAR1)),
    (b"A", prog1(100, 500, HSBW, 300, 700, RLINETO1, 300, -700, RLINETO1, CLOSEPATH, ENDCHAR1)),
    (b"acute", prog1(50, 300, HSBW, 0, 600, RMOVETO1, 100, 150, RLINETO1, 60, 0, RLINETO1, CLOSEPATH, ENDCHAR1)),
    (b"Aacute", prog1(100, 500, HSBW, 50, 250, 200, 65, 194, SEAC)),
    # B: from (200, 100): the subroutine's line (to 300, 100); a curve up, then across (to 400, 200); 600 300 div's 2
    #    as a line up (to 400, 202); a general curve (to 460, 262); a flex (to 510, 302, then 560, 282); a line down
    (b"B", prog1(200, 600, HSBW, 0, 100, HSTEM1, 100, VMOVETO1, 0, CALLSUBR1, 0 + 50, 50, 50, 50, VHCURVETO1,
                 600, 300, DIV, VLINETO1, 10, 20, 20, 20, 30, 20, RRCURVETO1)
     + flex + prog1(-200, VLINETO1, CLOSEPATH, ENDCHAR1)),
]


def type1(hexed=False):
    clear = (b"%!PS-AdobeFont-1.0: Synth 001.000\n11 dict begin\n/FontName /Synth def\n/FontMatrix [0.001 0 0 0.001 0 0] readonly def\n"
             b"/Encoding 256 array\n0 1 255 {1 index exch /.notdef put} for\ndup 65 /A put\ndup 66 /B put\ndup 193 /Aacute put\n"
             b"readonly def\ncurrentdict end\ncurrentfile eexec\n")
    plain = b"dup /Private 8 dict dup begin\n/RD {string currentfile exch readstring pop} executeonly def\n/lenIV 4 def\n"
    plain += b"/Subrs %d array\n" % len(subrs1)
    for i, s in enumerate(subrs1):
        e = crypt(b"\0\0\0\0" + s, 4330)
        plain += b"dup %d %d RD " % (i, len(e)) + e + b" NP\n"
    plain += b"2 index /CharStrings %d dict dup begin\n" % len(chars1)
    for name, s in chars1:
        e = crypt(b"\0\0\0\0" + s, 4330)
        plain += b"/" + name + b" %d RD " % len(e) + e + b" ND\n"
    plain += b"end\nend\nreadonly put\nnoaccess put\ndup /FontName get exch definefont pop\nmark currentfile closefile\n"
    secret = crypt(b"\x01\x02\x03\x80" + plain, 55665)
    if hexed:
        secret = secret.hex().encode()
    return clear + secret + b"\n" + b"0" * 64 + b"\ncleartomark\n"


open(os.path.join(out, "SYNTH.PFA"), "wb").write(type1())
open(os.path.join(out, "SYNTHX.PFA"), "wb").write(type1(hexed=True))
print(out)
