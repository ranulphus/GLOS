#!/usr/bin/env python3
"""Archives for tests/host/arc_test.c (apps/shared/arc.c), made here with Python's own zipfile, tarfile and gzip,
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
