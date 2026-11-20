#!/usr/bin/env python3
"""The archives of the GUI scripts' drive (tests/gui/drives/C/ARC: files-arc.txt) and the files that
files-zip.txt packs (C/ZIP) and files-bin.txt deletes (C/RCY), made once and kept in the tree; run again only to
change them.

    python3 tests/gui/mkarc.py
"""
import gzip, io, os, tarfile, zipfile

here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "drives", "C", "ARC")
os.makedirs(os.path.join(here, "OUT"), exist_ok=True)
STAMP = (2026, 11, 19, 12, 30, 0)
UNIX = 1795091400                       # 2026-11-19 12:30:00 UTC

FILES = [
    ("READ.ME", b"An archive for Files' tests.\r\nIts files are small.\r\n"),
    ("DOCS/GUIDE.TXT", b"The guide.\r\n" * 40),
    ("DOCS/DEEP/NOTE.TXT", b"A note, two folders down.\r\n"),
    ("DOCS/a long file name.text", b"Its name is longer than DOS has room for.\r\n"),
    ("DOCS/a long file name too.text", b"So is this one's: the two get numbered names on a DOS disc.\r\n"),
    ("NUMBERS.DAT", bytes(range(256)) * 8),
]

b = io.BytesIO()
with zipfile.ZipFile(b, "w", zipfile.ZIP_DEFLATED) as z:
    for name, data in FILES:
        zi = zipfile.ZipInfo(name, STAMP)
        zi.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(zi, data)
open(os.path.join(here, "PACK.ZIP"), "wb").write(b.getvalue())
open(os.path.join(here, "BROKEN.ZIP"), "wb").write(b.getvalue()[:300])

t = io.BytesIO()
with tarfile.open(fileobj=t, mode="w", format=tarfile.USTAR_FORMAT) as tar:
    for name, data in [("LETTERS/ONE.TXT", b"The first letter.\r\n"), ("LETTERS/TWO.TXT", b"The second letter.\r\n" * 30),
                       ("TOP.TXT", b"At the top of the bundle.\r\n")]:
        ti = tarfile.TarInfo(name)
        ti.size = len(data)
        ti.mtime = UNIX
        tar.addfile(ti, io.BytesIO(data))
g = io.BytesIO()
with gzip.GzipFile(filename="", mode="wb", fileobj=g, mtime=UNIX) as f:
    f.write(t.getvalue())
open(os.path.join(here, "BUNDLE.TGZ"), "wb").write(g.getvalue())
open(os.path.join(here, "OUT", "KEEP.TXT"), "wb").write(b"What is unpacked by the tests is put beside this.\r\n")
# ... and the files of files-zip.txt (C:\ZIP), which makes its own archives of them.
zipdir = os.path.join(os.path.dirname(here), "ZIP")
os.makedirs(os.path.join(zipdir, "PAPERS"), exist_ok=True)       # (names the Start menu's script does not search for)
for name, data in [("PAPERS/ONE.TXT", b"The first note.\r\n"), ("PAPERS/TWO.TXT", b"The second note.\r\n" * 50),
                   ("MEMO.TXT", b"A memo, to go into a ZIP.\r\n"), ("MORE.TXT", b"More, " * 300 + b"and no more.\r\n")]:
    open(os.path.join(zipdir, *name.split("/")), "wb").write(data)
# ... and what files-bin.txt deletes and has back (C:\RCY).
rcy = os.path.join(os.path.dirname(here), "RCY")
os.makedirs(os.path.join(rcy, "OLD"), exist_ok=True)
for name, data in [("OLD/ONE.TXT", b"In a folder that is deleted whole.\r\n"), ("GONE.TXT", b"Deleted, and had back.\r\n"),
                   ("KEEP.TXT", b"Never deleted.\r\n")]:
    open(os.path.join(rcy, *name.split("/")), "wb").write(data)
for n in sorted(os.listdir(here)):
    p = os.path.join(here, n)
    print("%-12s %s" % (n, "folder" if os.path.isdir(p) else "%d bytes" % os.path.getsize(p)))
