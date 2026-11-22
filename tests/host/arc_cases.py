#!/usr/bin/env python3
"""Archives for tests/host/arc_test.c (apps/shared/arc.c), made here with Python's own zipfile, tarfile, gzip and
lzma, those in tests/host/arcfix that other tools made, and LZH, ARJ and 7z archives written by arcfix/pack.py,
and a list of what each must be read as.

    arc_cases.py DIR      writes the archives and DIR/MANIFEST; prints DIR

MANIFEST, a line a thing, tabs between its fields:
    A  FILE  KIND         an archive and its kind ("!": it must not open; "?": it may open or not)
    E  NAME  SIZE  CRC  d|f  HOW  GET     its next entry: HOW as arc_entry.how; GET: what unpacking it gives
                                          (0, a negative error, or "x": any error)
    D  DATE  TIME                         every entry's date and time, as DOS has them
    P  N  PATH                            entry N's path on a DOS disc (arc_dos_path)
    L  DIR  NAME|NAME/|...                a folder's contents in order ("/" after a folder's name)
    W  RESULT                             what a walk of all of it gives (0, or "x": any error)
"""
import gzip, io, os, random, struct, sys, tarfile, time, zipfile, zlib

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
rnd = random.Random(20261119)
lines = []
STAMP = (2026, 11, 19, 21, 30, 44)
UNIX = 1795123844                       # (a time in 2026: tarfile's and gzip's)


def dos(t):
    return "%d\t%d" % ((t[0] - 1980) << 9 | t[1] << 5 | t[2], t[3] << 11 | t[4] << 5 | t[5] // 2)


ZSTAMP, USTAMP = dos(STAMP), dos(time.gmtime(UNIX)[:6])


def text(n):
    words = ["the", "desktop", "window", "folder", "archive", "file", "of", "a", "GLOS", "and", "is", "unpacked\r\n"]
    s = " ".join(rnd.choice(words) for _ in range(n // 5))
    return s.encode()[:n]


FILES = [
    ("READ.ME", text(1200)),
    ("DOCS/GUIDE.TXT", text(70000)),
    ("DOCS/DEEP/DOWN/A.BIN", bytes(rnd.getrandbits(8) for _ in range(40000))),
    ("EMPTY.DAT", b""),
    ("big/zeros.bin", bytes(300000)),
    ("a long file name with spaces.text", b"long names are kept as they are\r\n"),
    ("DOCS/a", b"a file named a"),
    ("DOCS/a.txt", b"beside a folder's worth of names"),
]


def crc(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def manifest(fname, kind, entries, walk="0", stamp="", dos=None):
    """entries: (name, data or None for a folder, how, get)"""
    lines.append("A\t%s\t%s" % (fname, kind))
    if kind == "!":
        return
    if stamp != None:
        lines.append("D\t%s" % (stamp or (ZSTAMP if kind == "ZIP" else USTAMP)))
    for name, data, how, get in entries:
        lines.append("E\t%s\t%d\t%08x\t%s\t%d\t%s" % (name, len(data or b""), crc(data or b""), "f" if data is not None else "d",
                                                      how, get))
    for i, path in enumerate(dos or []):
        lines.append("P\t%d\t%s" % (i, path))
    dirs = {""}
    for name, data, _, _ in entries:
        parts = name.split("/")
        for i in range(1, len(parts) + (data is None)):
            dirs.add("/".join(parts[:i]))
    isdir = set(dirs) | {n for n, d, _, _ in entries if d is None}
    for d in sorted(dirs):
        kids = []
        for name, data, _, _ in entries:
            if d and not name.startswith(d + "/"):
                continue
            rest = name[len(d) + 1 if d else 0:]
            c = rest.split("/")[0]
            if c and c not in kids:
                kids.append(c)
        kids.sort(key=lambda s: s.encode("latin-1"))
        full = lambda c: (d + "/" + c) if d else c
        lines.append("L\t%s\t%s" % (d, "|".join(c + ("/" if full(c) in isdir else "") for c in kids)))
    lines.append("W\t%s" % walk)


def write(fname, data):
    open(os.path.join(out, fname), "wb").write(data)


def make_zip(names_data, method=zipfile.ZIP_DEFLATED, comment=b""):
    b = io.BytesIO()
    with zipfile.ZipFile(b, "w", method) as z:
        for name, data in names_data:
            zi = zipfile.ZipInfo(name, STAMP)
            zi.compress_type = method
            if data is None:
                zi.external_attr = 0x10
                z.writestr(zi, b"")
            else:
                z.writestr(zi, data)
        z.comment = comment
    return b.getvalue()


def ok(names_data):
    return [(n.rstrip("/"), d, 0, "0") for n, d in names_data]


# ---- ZIP
plain = FILES[:2] + [("EMPTY_DIR/", None), ("DOCS/", None)] + FILES[2:]
z = make_zip(plain)
write("plain.zip", z)
manifest("plain.zip", "ZIP", ok(plain))
write("stored.zip", make_zip(plain, zipfile.ZIP_STORED))
manifest("stored.zip", "ZIP", ok(plain))
write("nodirs.zip", make_zip(FILES))
manifest("nodirs.zip", "ZIP", ok(FILES))
write("comment.zip", make_zip(FILES, comment=b"A comment at the archive's end. " * 10))
manifest("comment.zip", "ZIP", ok(FILES))
write("sfx.zip", bytes(rnd.getrandbits(8) for _ in range(5000)) + z)   # (a program before it: its places are 5000 on)
manifest("sfx.zip", "ZIP", ok(plain))
write("empty.zip", make_zip([]))
manifest("empty.zip", "ZIP", [])
back = [("DOS\\SUB\\FILE.TXT", b"backslashes between the parts"), ("DOS\\TOP.TXT", b"top")]
write("back.zip", make_zip(back))
manifest("back.zip", "ZIP", [(n.replace("\\", "/"), d, 0, "0") for n, d in back])
evil = [("../up.txt", b"1"), ("ok.txt", b"2"), ("/abs/path.txt", b"3"), ("C:\\x\\y.txt", b"4"), ("a/./b/../c.txt", b"5"),
        ("./dot//file.txt", b"6"), ("..", b"7"), ("ctl\x01name.txt", b"8")]
write("evil.zip", make_zip(evil))
manifest("evil.zip", "ZIP", [("ok.txt", b"2", 0, "0"), ("abs/path.txt", b"3", 0, "0"), ("x/y.txt", b"4", 0, "0"),
                             ("dot/file.txt", b"6", 0, "0"), ("ctl_name.txt", b"8", 0, "0")])
many = [("D%02d/F%04d.TXT" % (i % 37, i), ("file %d\r\n" % i).encode() * (i % 7)) for i in range(3000)]
rnd.shuffle(many)
write("many.zip", make_zip(many))
manifest("many.zip", "ZIP", ok(many))


# Names for a DOS disc: each with the path it must get (the later of two alike, in the names' order, is numbered).
names = [
    ("ReadMe.txt", "README.TXT"),
    ("a long file name with spaces.text", "ALONGF~1.TEX"),
    ("a long file name with more.text", "ALONGFIL.TEX"),
    ("docs/", "DOCS~1"),
    ("docs/Chapter One.html", "DOCS~1\\CHAPTERO.HTM"),
    ("docs/Chapter Two.html", "DOCS~1\\CHAPTERT.HTM"),
    ("docs/chapter one.HTML", "DOCS~1\\CHAPTE~1.HTM"),
    ("Docs/other.txt", "DOCS\\OTHER.TXT"),
    (".profile", "PROFILE"),
    ("archive.tar.gz", "ARCHIVET.GZ"),
    ("con.txt", "CON_.TXT"),
    ("weird+name[1];x=y.c", "WEIRD_NA.C"),
    ("noext", "NOEXT"),
    ("trailingdot.", "TRAILING"),
    ("x/y/z/deep file name.data", "X\\Y\\Z\\DEEPFI~1.DAT"),
    ("x/y/z/deep file name 2.data", "X\\Y\\Z\\DEEPFILE.DAT"),
    ("12345678.123", "12345678.123"),
    ("123456789.1234", "123456~1.123"),
    ("lpt1", "LPT1_"),
    ("x/y/after.txt", "X\\Y\\AFTER.TXT"),
] + [("many names/longfilename%02d.txt" % i,
      "MANYNAME\\" + ("LONGFILE.TXT" if i == 0 else "LONGFI~%d.TXT" % i if i < 10 else "LONGF~%d.TXT" % i)) for i in range(12)]
nd = [(n, None if n.endswith("/") else ("named " + n).encode()) for n, _ in names]
write("names.zip", make_zip(nd))
manifest("names.zip", "ZIP", ok(nd), dos=[d for _, d in names])


def patch(z, name, what):
    """One entry's local and central headers changed: what(header bytes at the general flags) -> the six bytes
    of flags, method and... (flags at +0, method at +2)."""
    z = bytearray(z)
    nb = name.encode()
    at = 0
    while True:
        at = z.find(b"PK\x03\x04", at)
        if at < 0:
            break
        n = struct.unpack_from("<H", z, at + 26)[0]
        if z[at + 30:at + 30 + n] == nb:
            what(z, at + 6)
        at += 4
    at = 0
    while True:
        at = z.find(b"PK\x01\x02", at)
        if at < 0:
            break
        n = struct.unpack_from("<H", z, at + 28)[0]
        if z[at + 46:at + 46 + n] == nb:
            what(z, at + 8)
        at += 4
    return bytes(z)


def set_flag(z, at):
    z[at] |= 1


def set_method(z, at):
    z[at + 2] = 6


small = [("ONE.TXT", text(3000)), ("TWO.TXT", text(5000)), ("THREE.TXT", text(2000))]
zs = make_zip(small)
write("locked.zip", patch(zs, "TWO.TXT", set_flag))
manifest("locked.zip", "ZIP", [("ONE.TXT", small[0][1], 0, "0"), ("TWO.TXT", small[1][1], -6, "-6"),
                               ("THREE.TXT", small[2][1], 0, "0")])
write("method.zip", patch(zs, "TWO.TXT", set_method))
manifest("method.zip", "ZIP", [("ONE.TXT", small[0][1], 0, "0"), ("TWO.TXT", small[1][1], -5, "-5"),
                               ("THREE.TXT", small[2][1], 0, "0")])
zst = bytearray(make_zip(small, zipfile.ZIP_STORED))
at = zst.find(small[1][1][:40]) + 1000                 # (a byte of TWO.TXT's own, stored as it is)
zst[at] ^= 0x20
write("badcrc.zip", bytes(zst))
manifest("badcrc.zip", "ZIP", [("ONE.TXT", small[0][1], 0, "0"), ("TWO.TXT", small[1][1], 0, "-7"),
                               ("THREE.TXT", small[2][1], 0, "0")])


class NoSeek:                                           # (zipfile then writes each entry's sizes after its bytes)
    def __init__(self):
        self.b = io.BytesIO()

    def write(self, d):
        return self.b.write(d)

    def flush(self):
        pass


ns = NoSeek()
with zipfile.ZipFile(ns, "w", zipfile.ZIP_DEFLATED) as zf:
    for name, data in small:
        zf.writestr(zipfile.ZipInfo(name, STAMP), data, zipfile.ZIP_DEFLATED)
write("descr.zip", ns.b.getvalue())
manifest("descr.zip", "ZIP", ok(small))
zd = bytearray(zs)
at = zd.find(b"PK\x03\x04", 10) + 30 + 7 + 600          # (in TWO.TXT's packed bytes)
zd[at] ^= 0xFF
zd[at + 1] ^= 0xFF
write("badpack.zip", bytes(zd))
manifest("badpack.zip", "ZIP", [("ONE.TXT", small[0][1], 0, "0"), ("TWO.TXT", small[1][1], 0, "x"),
                                ("THREE.TXT", small[2][1], 0, "0")])
write("cut.zip", z[:len(z) // 2])
manifest("cut.zip", "!", [])
zc = bytearray(z)
at = zc.rfind(b"PK\x01\x02")
zc[at + 2] ^= 0x55                                      # (the directory's last entry is no entry)
write("baddir.zip", bytes(zc))
manifest("baddir.zip", "!", [])


# ---- TAR
def make_tar(names_data, fmt, extra=None, gz=False):
    b = io.BytesIO()
    with tarfile.open(fileobj=b, mode="w", format=fmt) as t:
        for name, data in names_data:
            ti = tarfile.TarInfo(name)
            ti.mtime = UNIX
            if data is None:
                ti.type = tarfile.DIRTYPE
                t.addfile(ti)
            else:
                ti.size = len(data)
                t.addfile(ti, io.BytesIO(data))
        if extra:
            extra(t)
    raw = b.getvalue()
    if not gz:
        return raw
    g = io.BytesIO()
    with gzip.GzipFile(filename="", mode="wb", fileobj=g, mtime=UNIX) as f:
        f.write(raw)
    return g.getvalue()


tplain = [("READ.ME", FILES[0][1]), ("DOCS", None), ("DOCS/GUIDE.TXT", FILES[1][1]), ("EMPTY_DIR", None)] + FILES[2:]
write("plain.tar", make_tar(tplain, tarfile.USTAR_FORMAT))
manifest("plain.tar", "TAR", ok(tplain))
longs = [("short.txt", b"short"),
         ("a/" + "very-long-folder-name-" * 6 + "/" + "and-a-long-file-name-" * 5 + ".txt", text(900)),
         ("p/" * 60 + "deep.txt", b"deep"), ("after.txt", b"after")]
write("gnu.tar", make_tar(longs, tarfile.GNU_FORMAT))
manifest("gnu.tar", "TAR", ok(longs))
write("pax.tar", make_tar(longs, tarfile.PAX_FORMAT))
manifest("pax.tar", "TAR", ok(longs))
pre = [("some/folders/deep/enough/to/need/the/prefix/field/of/a/ustar/header/because/the/name/alone/has/a/hundred/"
        "bytes/file.txt", b"prefix")]
write("prefix.tar", make_tar(pre, tarfile.USTAR_FORMAT))
manifest("prefix.tar", "TAR", ok(pre))


def links(t):
    ti = tarfile.TarInfo("link-to-ok")
    ti.type = tarfile.SYMTYPE
    ti.linkname = "ok.txt"
    t.addfile(ti)
    ti = tarfile.TarInfo("hard-to-ok")
    ti.type = tarfile.LNKTYPE
    ti.linkname = "ok.txt"
    t.addfile(ti)
    ti = tarfile.TarInfo("last.txt")
    ti.size = 4
    ti.mtime = UNIX
    t.addfile(ti, io.BytesIO(b"last"))


tevil = [("../up.txt", b"1"), ("ok.txt", b"2" * 3000), ("/abs/path.txt", b"3"), ("a/../../c.txt", b"5" * 700)]
write("evil.tar", make_tar(tevil, tarfile.GNU_FORMAT, links))
manifest("evil.tar", "TAR", [("ok.txt", b"2" * 3000, 0, "0"), ("abs/path.txt", b"3", 0, "0"), ("last.txt", b"last", 0, "0")])
tmany = [("T%02d/F%03d.TXT" % (i % 11, i), ("tar file %d\r\n" % i).encode() * (i % 90)) for i in range(500)]
write("many.tar", make_tar(tmany, tarfile.USTAR_FORMAT))
manifest("many.tar", "TAR", ok(tmany))
t = make_tar(tplain, tarfile.USTAR_FORMAT)
write("cut.tar", t[:len(t) // 2 + 100])
manifest("cut.tar", "!", [])
write("noend.tar", t.rstrip(b"\0") + bytes(-len(t.rstrip(b"\0")) % 512))   # (no empty blocks at its end: read all the same)
manifest("noend.tar", "TAR", ok(tplain))

# ---- GZ, and a TAR in one
tg = make_tar(tplain, tarfile.USTAR_FORMAT, gz=True)
write("plain.tgz", tg)
manifest("plain.tgz", "TAR.GZ", ok(tplain))
write("many.tgz", make_tar(tmany, tarfile.USTAR_FORMAT, gz=True))
manifest("many.tgz", "TAR.GZ", ok(tmany))
write("long.tgz", make_tar(longs, tarfile.PAX_FORMAT, gz=True))
manifest("long.tgz", "TAR.GZ", ok(longs))
g = io.BytesIO()
with gzip.GzipFile(filename="GUIDE.TXT", mode="wb", fileobj=g, mtime=UNIX) as f:
    f.write(FILES[1][1])
write("single.gz", g.getvalue())
manifest("single.gz", "GZ", [("GUIDE.TXT", FILES[1][1], 0, "0")])
g2 = io.BytesIO()
with gzip.GzipFile(filename="", mode="wb", fileobj=g2, mtime=0) as f:
    f.write(FILES[2][1])
write("NONAME.BIN.gz", g2.getvalue())
manifest("NONAME.BIN.gz", "GZ", [("NONAME.BIN", FILES[2][1], 0, "0")], stamp="0\t0")
gb = bytearray(g.getvalue())
gb[len(gb) // 2] ^= 0xFF
gb[len(gb) // 2 + 1] ^= 0xFF
write("bad.gz", bytes(gb))
manifest("bad.gz", "GZ", [("GUIDE.TXT", FILES[1][1], 0, "x")], walk="0")
gs = bytearray(g.getvalue())
gs[-6] ^= 1                                              # (the check at its end is of other bytes)
write("badsum.gz", bytes(gs))
manifest("badsum.gz", "GZ", [("GUIDE.TXT", FILES[1][1], 0, "-7")])
write("cut.tgz", tg[:len(tg) // 2])
manifest("cut.tgz", "!", [])
tgs = bytearray(tg)
tgs[-7] ^= 1
write("badsum.tgz", bytes(tgs))                          # (every file comes out; the walk says it at the end)
manifest("badsum.tgz", "TAR.GZ", ok(tplain), walk="-7")

write("names.tgz", make_tar([(n.rstrip("/"), d) for n, d in nd], tarfile.PAX_FORMAT, gz=True))
manifest("names.tgz", "TAR.GZ", ok(nd), dos=[d for _, d in names])

# ---- LZH and ARJ. First the archives that LHA's and ARJ's own tools made (tests/host/arcfix: make.py says how;
# LIST is 7-Zip's account of what is in each, SUMS the sums of the files packed), held to what inputs.py makes
# again here.
fix = os.path.join(os.path.dirname(os.path.abspath(__file__)), "arcfix")
sys.path.insert(0, fix)
import inputs, pack

assert inputs.STAMP == STAMP
FX = dict(inputs.files())
for ln in open(os.path.join(fix, "SUMS")):
    n, size, c = ln.rstrip("\n").split("\t")
    assert len(FX[n]) == int(size) and crc(FX[n]) == int(c, 16), "inputs.py does not make %s as it was packed" % n
made = {}
for ln in open(os.path.join(fix, "LIST")):
    f = ln.rstrip("\n").split("\t")
    if f[0] == "A":
        cur = made.setdefault(f[1], [])
    else:
        cur.append((f[1], f[2] == "d"))


def fixture(fname, special={}):
    """a made archive's bytes, and its entries for the manifest (special: name -> (data, how, get))"""
    ents = []
    for name, isdir in made[fname]:
        ents.append((name,) + special.get(name, (None if isdir else FX[name], 0, "0")))
    return open(os.path.join(fix, fname), "rb").read(), ents


for fname in sorted(made):
    kind = "LZH" if fname.endswith(".LZH") else "7Z" if fname.endswith(".7Z") else "ARJ"
    special = {}
    if fname == "S5.7Z":                                # (its header has a password: no list to be had)
        write(fname, open(os.path.join(fix, fname), "rb").read())
        manifest(fname, "!", [])
        continue
    if fname in ("S4.7Z", "S6.7Z"):                     # files with a password; PPMd: listed, not unpacked
        why = -6 if fname == "S4.7Z" else -5
        special = {n: (FX[n], why, str(why)) for n, isdir in made[fname] if not isdir}
    elif fname == "LOCKED.ARJ":
        special = {"READ.ME": (FX["READ.ME"], -6, "-6")}
    elif fname == "VOL.ARJ":                            # (a file that goes on in the next volume: not unpacked)
        cutat = os.path.getsize(os.path.join(fix, "VOL.ARJ"))
        piece = len(FX["FAR.BIN"]) - 13346
        special = {"FAR.BIN": (FX["FAR.BIN"][:piece], -9, "-9")}
    elif fname == "VOL.A01":
        special = {"FAR.BIN": (FX["FAR.BIN"][-13346:], -9, "-9")}
    b, ents = fixture(fname, special)
    write(fname, b)
    manifest(fname, kind, ents, stamp=ZSTAMP)


def le16(b, at):
    return struct.unpack_from("<H", b, at)[0]


def lzh_data(b, name):
    """where a level 0 archive's entry has its header and its packed bytes"""
    at = b.find(name.encode()) - 22
    return at, at + b[at] + 2


def arj_data(b, name):
    """where an ARJ's entry has its header and its packed bytes"""
    at, first = 0, True
    while le16(b, at + 2):
        size = le16(b, at + 2)
        head = at + 4 + b[at + 4]
        data = at + 4 + size + 4 + 2
        if not first and b[head:b.find(b"\0", head)] == name.encode():
            return at, data
        at = data + (0 if first else struct.unpack_from("<I", b, at + 4 + 12)[0])
        first = False
    raise KeyError(name)


def changed(ents, name, get):
    return [(n, d, h, get if n == name else g) for n, d, h, g in ents]


l0, l0e = fixture("L0.LZH")
write("cut.lzh", l0[:len(l0) // 2])                     # (in the middle of a file's packed bytes)
manifest("cut.lzh", "!", [])
lb = bytearray(l0)
_, at = lzh_data(l0, "TEXT.TXT")
lb[at + 600] ^= 0xFF
lb[at + 601] ^= 0xFF
write("badpack.lzh", bytes(lb))
manifest("badpack.lzh", "LZH", changed(l0e, "TEXT.TXT", "x"), stamp=ZSTAMP)
lb = bytearray(l0)
at = l0.find(b"SKEW.BIN") + 8                           # (the sum of the file's bytes, after its name)
lb[at] ^= 1
write("badsum.lzh", bytes(lb))
manifest("badsum.lzh", "LZH", changed(l0e, "SKEW.BIN", "-7"), stamp=ZSTAMP)
write("padded.lzh", l0.rstrip(b"\0") + b"\x1a" * 100)   # (no end written, and a file transfer's padding after)
manifest("padded.lzh", "LZH", l0e, stamp=ZSTAMP)
write("sfx.lzh", b"MZ" + bytes(rnd.getrandbits(8) for _ in range(700)) + l0)     # (a program before it: not looked for)
manifest("sfx.lzh", "!", [])

m1, m1e = fixture("M1.ARJ")
write("cut.arj", m1[:len(m1) // 2])
manifest("cut.arj", "!", [])
mb = bytearray(m1)
_, at = arj_data(m1, "SKEW.BIN")
mb[at + 500] ^= 0xFF
mb[at + 501] ^= 0xFF
write("badpack.arj", bytes(mb))
manifest("badpack.arj", "ARJ", changed(m1e, "SKEW.BIN", "x"), stamp=ZSTAMP)
m4, m4e = fixture("M4.ARJ")
mb = bytearray(m4)
_, at = arj_data(m4, "TEXT.TXT")
mb[at + 500] ^= 0xFF
write("badpack4.arj", bytes(mb))
manifest("badpack4.arj", "ARJ", changed(m4e, "TEXT.TXT", "x"), stamp=ZSTAMP)
m0, m0e = fixture("M0.ARJ")
mb = bytearray(m0)
_, at = arj_data(m0, "NOISE.BIN")
mb[at + 10] ^= 4                                        # (stored as it is: one byte of it another)
write("badsum.arj", bytes(mb))
manifest("badsum.arj", "ARJ", changed(m0e, "NOISE.BIN", "-7"), stamp=ZSTAMP)
mb = bytearray(m1)
at, _ = arj_data(m1, "READ.ME")
mb[at + 40] ^= 1                                        # (a header in its middle whose own sum is not its bytes')
write("badhead.arj", bytes(mb))
manifest("badhead.arj", "!", [])
assert m1.endswith(b"\x60\xEA\0\0")
write("noend.arj", m1[:-4])                             # (no end written: read all the same)
manifest("noend.arj", "ARJ", m1e, stamp=ZSTAMP)
write("junk.arj", m1[:-4] + b"\x1a" * 64)               # (and what follows the last file is no header)
manifest("junk.arj", "!", [])

# Then archives written here bit by bit (arcfix/pack.py): the method's corners, which no packer goes into when
# asked. (7-Zip and lhasa unpack every one of the good ones to the same bytes: looked at when these were written.)
ZD, ZT = [int(x) for x in ZSTAMP.split("\t")]
SP = b" " * 65536                                       # LHA's window before the first byte: spaces
lrnd = random.Random(1991)


def four(n):
    return bytes(b"ACGT"[lrnd.getrandbits(2)] for _ in range(n))


def lh(tokens, kind, before=b"", **how):
    """(packed, the bytes it is of)"""
    return pack.pack(tokens, kind, **how), pack.unparse(tokens, before)


deep_syms = [0, 2, 5, 9, 28, 48, 69, 91, 120, 121, 122, 200, 255, 256, 300, 400, 509]   # (C's: zeros between them in
deep_len = [0] * pack.NC                                                                # runs of every size)
for k, sym in enumerate(deep_syms):
    deep_len[sym] = min(k + 1, 16)                      # code lengths 1 to 16 (two of 16)
deep_tok = [0, 2, 5, 9, 28, 48, 69, 91, 120, 121, 122, 200, 255, (3, 1), (47, 13), (147, 5), (256, 60)]
for _ in range(3000):                                   # (the short codes often, the long ones now and then)
    k, x = 0, lrnd.getrandbits(20)
    while x & 1 and k < 16:
        k, x = k + 1, x >> 1
    t = deep_syms[k]
    deep_tok.append(t if t < 256 else (t - 253, 1 + lrnd.getrandbits(4)))
t_len = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 15, 16, 16, 16, 16]             # T's 19 codes, as long as may be
p_len = list(range(1, 16)) + [16, 16]                   # P's 17 (lh7), of every length
p_tok = [97] + [(3 + j, 1 if j == 0 else (1 << (j - 1)) + 1) for j in range(17)] * 3 + [(256, 65536), (3, 65535)]
txt = text(30000)
big = four(66000)
big_tok = list(big) + [(3, 65536), (256, 65536), (100, 32769), (5, 32768), (40, 8193), (256, 1), (3, 65000)]
six = four(34000)
six_tok = list(six) + [(256, 32768), (3, 32768), (77, 16385), (9, 8192)]
spaces = [(10, 5), 120, (20, 8192), (5, 3), (256, 8000), 121, (3, 1)]

corners5 = [
    ("ONE.BIN", b"-lh5-") + lh([65], pack.LH5),                                         # C of one code
    ("RUN.BIN", b"-lh5-") + lh([66, (255, 1), (3, 1)], pack.LH5),                       # P of one code
    ("SPACES.TXT", b"-lh5-") + lh(spaces, pack.LH5, SP),                                # from before the first byte
    ("DEEP.BIN", b"-lh5-") + lh(deep_tok, pack.LH5, clen=deep_len),                     # C's codes to 16 bits
    ("DEEP2.BIN", b"-lh5-") + lh(deep_tok, pack.LH5, clen=deep_len, runs=(999, 3)),     # zeros without code 2
    ("DEEP3.BIN", b"-lh5-") + lh(deep_tok, pack.LH5, clen=deep_len, runs=(999, 999)),   # and one by one
    ("TDEEP.BIN", b"-lh5-") + lh(deep_tok, pack.LH5, clen=deep_len, tlen=t_len),        # T's codes to 16 bits
    ("BLOCKS.TXT", b"-lh5-") + lh(pack.parse(txt, 8192), pack.LH5, per=300),            # many small blocks
    ("STORED.TXT", b"-lh0-", txt[:500], txt[:500]),
    ("OLD.TXT", b"-lz4-", txt[:300], txt[:300]),                                        # (LArc's "not packed")
]
write("corners5.lzh", pack.lzh(corners5, (ZD, ZT)))
manifest("corners5.lzh", "LZH", [(n, d, 0, "0") for n, _, _, d in corners5], stamp=ZSTAMP)
corners6 = [("FAR6.BIN", b"-lh6-") + lh(six_tok, pack.LH6, per=20000),
            ("BLOCKS6.TXT", b"-lh6-") + lh(pack.parse(txt, 32768), pack.LH6, per=1000)]
write("corners6.lzh", pack.lzh(corners6, (ZD, ZT), level=1))
manifest("corners6.lzh", "LZH", [(n, d, 0, "0") for n, _, _, d in corners6], stamp=ZSTAMP)
corners7 = [("PDEEP.BIN", b"-lh7-") + lh(p_tok, pack.LH7, SP, plen=p_len),             # P's codes to 16 bits
            ("FAR7.BIN", b"-lh7-") + lh(big_tok, pack.LH7, per=65535)]                  # the largest block there is
write("corners7.lzh", pack.lzh(corners7, (ZD, ZT)))
manifest("corners7.lzh", "LZH", [(n, d, 0, "0") for n, _, _, d in corners7], stamp=ZSTAMP)
other = [("ONE.TXT", b"-lh5-") + lh([65], pack.LH5), ("OLD1.TXT", b"-lh1-", b"\0" * 20, txt[:100]),
         ("OLD5.TXT", b"-lz5-", b"\0" * 20, txt[:100]), ("LAST.TXT", b"-lh0-", b"last", b"last")]
write("methods.lzh", pack.lzh(other, (ZD, ZT)))         # (LHarc 1's and LArc's ways of packing: listed, not unpacked)
manifest("methods.lzh", "LZH", [(n, d, -5 if m in (b"-lh1-", b"-lz5-") else 0, "-5" if m in (b"-lh1-", b"-lz5-") else "0")
                                for n, m, _, d in other], stamp=ZSTAMP)


def raw(*fields):
    w = pack.Bits()
    for value, nbits in fields:
        w.put(value, nbits)
    return w.bytes() + bytes(8)


wrong = [
    ("GOOD.TXT", b"-lh5-") + lh([65, 66, 67], pack.LH5),
    ("OVER.BIN", b"-lh5-", raw((5, 16), (3, 5), (1, 3), (1, 3), (1, 3), (0, 2)), b"12345"),     # T: three codes of 1 bit
    ("UNDER.BIN", b"-lh5-", raw((5, 16), (1, 5), (1, 3)), b"12345"),                    # T: one code of 1 bit
    ("NOSUCH.BIN", b"-lh5-", raw((5, 16), (0, 5), (0, 5), (0, 9), (511, 9), (0, 4), (0, 4)), b"12345"),     # C's one code: 511
    ("NOBLOCK.BIN", b"-lh5-", raw((0, 16)), b"12345"),                                  # a block of no codes
    ("SHORT.BIN", b"-lh5-", lh(pack.parse(txt[:4000], 8192), pack.LH5)[0][:700], txt[:4000]),   # its packed bytes end early
    ("LONG.BIN", b"-lh5-", lh([65, 66, 67], pack.LH5)[0], b"ABCDEFGH"),                 # said to be of more bytes
    ("STORED.BIN", b"-lh0-", b"abc", b"abcdef"),                                        # stored, of two sizes
    ("GOOD2.TXT", b"-lh5-") + lh([97, (20, 1)], pack.LH5),
]
write("wrong.lzh", pack.lzh(wrong, (ZD, ZT)))
manifest("wrong.lzh", "LZH", [(n, d, 0, "0" if n.startswith("GOOD") else "x") for n, _, _, d in wrong], stamp=ZSTAMP)

a_txt = pack.parse(txt, 26624)
a_far = list(six[:27000]) + [(256, 26624), (3, 26624), (50, 16385), (256, 1)]
f_txt = pack.parse(txt, 15872)
f_far = list(six[:16000]) + [(256, 15872), (3, 15872), (2 + 127, 513), (3, 512), (256, 1), (130, 7000), (66, 1025)]
arjs = [
    ("ONE.BIN", 1) + lh([65], pack.ARJ),
    ("RUN.BIN", 1) + lh([66, (255, 1), (3, 1)], pack.ARJ),
    ("DEEP.BIN", 2) + lh(deep_tok, pack.ARJ, clen=deep_len),
    ("TDEEP.BIN", 3) + lh(deep_tok, pack.ARJ, clen=deep_len, tlen=t_len),
    ("BLOCKS.TXT", 1) + lh(a_txt, pack.ARJ, per=300),
    ("FAR.BIN", 1) + lh(a_far, pack.ARJ, per=65535),
    ("ONE4.BIN", 4, pack.arj4([65]), b"A"),
    ("RUN4.BIN", 4, pack.arj4([66, (255, 1), (256, 1), (3, 1)]), b"B" * 515),
    ("TEXT4.TXT", 4, pack.arj4(f_txt), txt),
    ("FAR4.BIN", 4, pack.arj4(f_far), pack.unparse(f_far)),
    ("STORED.TXT", 0, txt[:500], txt[:500]),
]
write("corners.arj", pack.arj(arjs, (ZD, ZT)))
manifest("corners.arj", "ARJ", [(e[0], e[3], 0, "0") for e in arjs], stamp=ZSTAMP)
awrong = [
    ("GOOD.TXT", 1) + lh([65, 66, 67], pack.ARJ),
    ("BEFORE.BIN", 1) + lh([65, (5, 3)], pack.ARJ, SP),                                 # from before the first byte
    ("BEYOND.BIN", 1) + lh(list(six[:30000]) + [(3, 30000)], pack.ARJ, per=65535),      # from farther than its window
    ("BEFORE4.BIN", 4, pack.arj4([65, (5, 3)]), b"A    "),
    ("SHORT4.BIN", 4, pack.arj4(f_txt)[:900], txt),
    ("METHOD9.BIN", 9, b"what", b"what"),                                               # (no such method)
    ("GOOD2.TXT", 4, pack.arj4([97, (20, 1)]), b"a" * 21),
]
write("wrong.arj", pack.arj(awrong, (ZD, ZT)))
manifest("wrong.arj", "ARJ", [(e[0], e[3], -5 if e[1] == 9 else 0, "0" if e[0].startswith("GOOD") else "-5" if e[1] == 9 else "x")
                              for e in awrong], stamp=ZSTAMP)

# ---- 7z. 7-Zip's own archives are among those above (S1 to S6.7Z); here, archives whose format is written by
# arcfix/pack.py around streams that liblzma packs (another packer than the SDK's, whose reader ours is).
exe = bytearray(lrnd.getrandbits(8) for _ in range(20000))
for k in range(0, len(exe) - 5, 7):                     # (calls, as a program has them: the x86 filter's work)
    exe[k] = 0xE8
    exe[k + 4] = 0 if k % 3 else 0xFF
s_files = [("A.TXT", text(13000)), ("SUB/B.EXE", bytes(exe)), ("SUB/C.BIN", bytes(range(256)) * 20)]
s_second = [("SECOND/D.TXT", text(1000))]
s_others = [("SUB", None), ("EMPTYDIR", None), ("NOTHING.DAT", b"")]
s_all = [(n, d, 0, "0") for n, d in s_others + s_files + s_second]
for how in ("copy", "lzma2", "lzma2big", "lzma", "bcj", "delta"):
    write("sz-%s.7z" % how, pack.sevenz([(how, s_files), ("lzma2", s_second)], s_others, UNIX))
    manifest("sz-%s.7z" % how, "7Z", s_all)
write("sz-empty.7z", pack.sevenz([], [], UNIX))
manifest("sz-empty.7z", "?", [])                        # (no files at all: opened or not, no harm)
wide = [("café/naïve 日本.txt", b"names are UTF-16 in a 7z: UTF-8 in our list"),
        ("far \U0001F600 plane.txt", b"a character of two UTF-16 units")]
write("sz-names.7z", pack.sevenz([("lzma2", wide)], [], UNIX))
manifest("sz-names.7z", "7Z", [(n.encode("utf-8").decode("latin-1"), d, 0, "0") for n, d in wide])
write("sz-evil.7z", pack.sevenz([("lzma2", evil)], [], UNIX))
manifest("sz-evil.7z", "7Z", [("ok.txt", b"2", 0, "0"), ("abs/path.txt", b"3", 0, "0"), ("x/y.txt", b"4", 0, "0"),
                              ("dot/file.txt", b"6", 0, "0"), ("ctl_name.txt", b"8", 0, "0")])
write("sz-many.7z", pack.sevenz([("lzma2big", many[:1500]), ("lzma2", many[1500:])], [], UNIX))
manifest("sz-many.7z", "7Z", ok(many))


def spoil_pack(k, packed, sums):
    if k == 0:
        packed = bytearray(packed)
        packed[len(packed) // 2] ^= 0xFF
        packed[len(packed) // 2 + 1] ^= 0xFF
    return bytes(packed)


def spoil_sum(k, packed, sums):
    if k == 0:
        sums[1] ^= 1
    return packed


write("sz-badpack.7z", pack.sevenz([("lzma2", s_files), ("lzma2", s_second)], s_others, UNIX, spoil_pack))
manifest("sz-badpack.7z", "7Z", [(n, d, 0, "x" if (n, d) in s_files else "0") for n, d, _, _ in s_all])
write("sz-badsum.7z", pack.sevenz([("lzma2", s_files), ("lzma2", s_second)], s_others, UNIX, spoil_sum))
manifest("sz-badsum.7z", "7Z", changed(s_all, "SUB/B.EXE", "-7"))      # (the block's other files are good)
write("sz-method.7z", pack.sevenz([("unknown", s_files), ("lzma2", s_second)], s_others, UNIX))
manifest("sz-method.7z", "7Z", [(n, d, -5 if (n, d) in s_files else 0, "-5" if (n, d) in s_files else "0")
                                for n, d, _, _ in s_all])
sz = pack.sevenz([("lzma2", s_files), ("lzma2", s_second)], s_others, UNIX)
write("sz-cut.7z", sz[:len(sz) // 2])
manifest("sz-cut.7z", "!", [])
zb = bytearray(sz)
zb[-20] ^= 1                                            # (its header's sum is of other bytes)
write("sz-badhead.7z", bytes(zb))
manifest("sz-badhead.7z", "!", [])
write("sz-sfx.7z", b"MZ" + bytes(lrnd.getrandbits(8) for _ in range(700)) + sz)    # (a program before it: not looked for)
manifest("sz-sfx.7z", "!", [])

# ---- (phase F) a disk's image: a 1.44 MB FAT12 floppy that mtools makes, read by Disk Tools' reader (fatimg.c):
# its files in folders' order, folders before what is in them; a long name as the volume has it
import shutil, subprocess
if shutil.which("mformat") and shutil.which("mcopy") and shutil.which("mmd"):
    src = os.path.join(out, "imgsrc")
    os.makedirs(src, exist_ok=True)
    i_a, i_b, i_c, i_l = text(300), text(5000), bytes(rnd.getrandbits(8) for _ in range(20000)), text(100)
    for n, d in (("a", i_a), ("b", i_b), ("c", i_c), ("l", i_l)):
        open(os.path.join(src, n), "wb").write(d)
    img = os.path.join(out, "floppy.img")
    if os.path.exists(img):
        os.remove(img)
    env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
    run = lambda *a: subprocess.run(list(a), check=True, env=env, capture_output=True)
    run("mformat", "-i", img, "-C", "-f", "1440", "-v", "GLOSTEST", "::")
    run("mcopy", "-i", img, os.path.join(src, "a"), "::A.TXT")
    run("mmd", "-i", img, "::SUB")
    run("mcopy", "-i", img, os.path.join(src, "b"), "::SUB/B.TXT")
    run("mmd", "-i", img, "::SUB/DEEP")
    run("mcopy", "-i", img, os.path.join(src, "c"), "::SUB/DEEP/C.BIN")
    run("mcopy", "-i", img, os.path.join(src, "l"), "::A long name.txt")
    manifest("floppy.img", "IMG", [("A.TXT", i_a, 0, "0"), ("SUB", None, 0, "0"), ("SUB/B.TXT", i_b, 0, "0"),
                                   ("SUB/DEEP", None, 0, "0"), ("SUB/DEEP/C.BIN", i_c, 0, "0"),
                                   ("A long name.txt", i_l, 0, "0")], stamp=None)
    bad = bytearray(open(img, "rb").read())
    bad[13] = 0                                     # (no sectors a cluster: no volume, and no other archive)
    write("floppy-bad.img", bytes(bad))
    manifest("floppy-bad.img", "!", [])
else:
    sys.stderr.write("arc_cases: no mtools: no disk image case\n")

# ---- no archives
write("random.bin", bytes(rnd.getrandbits(8) for _ in range(9000)))
manifest("random.bin", "!", [])
write("text.txt", text(4000))
manifest("text.txt", "!", [])
write("tiny.zip", b"PK")
manifest("tiny.zip", "!", [])
write("none.dat", b"")
manifest("none.dat", "!", [])

open(os.path.join(out, "MANIFEST"), "w", encoding="latin-1", newline="\n").write("\n".join(lines) + "\n")
print(out)
