#!/usr/bin/env python3
"""The ZIPs tests/host/arcw_test.c wrote (apps/shared/arc.c's writer), read by Python's zipfile, which wrote none
of them: each archive in DIR/WLIST opens, every entry's bytes pass their check (testzip), and its entries are the
ones the test listed, with those sizes and those CRCs of the unpacked bytes.

    arcw_check.py DIR
"""
import os, sys, zipfile, zlib

d = sys.argv[1]
want, cur = {}, None
for line in open(os.path.join(d, "WLIST"), encoding="latin-1"):
    f = line.rstrip("\n").split("\t")
    if f[0] == "A":
        cur = want[f[1]] = []
    elif f[0] == "E":
        cur.append((f[1], int(f[2]), int(f[3], 16), f[4] == "d"))
bad = 0
for name, entries in want.items():          # (an archive listed twice: as it was last)
    path = os.path.join(d, name)
    try:
        z = zipfile.ZipFile(path)
        t = z.testzip()
        if t is not None:
            print("FAIL %s: %s does not pass its check" % (name, t))
            bad += 1
        got = []
        for i in z.infolist():
            n = i.filename
            if n.startswith("/") or ".." in n.split("/") or "\\" in n or n.startswith("./") or "\x01" in n or ":" in n:
                continue                    # (names the reader lists otherwise or not at all: its own test's matter)
            data = b"" if i.is_dir() else z.read(i)
            got.append((n.rstrip("/"), len(data), zlib.crc32(data) & 0xFFFFFFFF, i.is_dir()))
        ours = [e for e in entries if e[0] in {g[0] for g in got}] if name == "w-evil.zip" else entries
        if got != ours:
            print("FAIL %s: zipfile has %d entries, the test listed %d; first difference: %r" % (
                name, len(got), len(ours), next(((a, b) for a, b in zip(got, ours) if a != b), "one is longer")))
            bad += 1
    except Exception as e:
        print("FAIL %s: %r" % (name, e))
        bad += 1
print("arcw_check: %d failures (%d archives)" % (bad, len(want)))
sys.exit(1 if bad else 0)
