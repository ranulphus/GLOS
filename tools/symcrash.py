#!/usr/bin/env python3
"""symcrash.py REPORT (--exe PROG.EXE | --map PROG.MAP [--base ADDR]): name
the code in a GLOS crash report (supervisor.md §19).

REPORT is a serial log with GLOS-CRASH lines, or a C:\\GLOS\\CRASH\\CRASHnnn.TXT
file. The report's CS:EIP, and the stack words that fall inside the
program's code (likely return addresses), are looked up in:

  --exe  a DJGPP program (stub + COFF): its own symbol table. DJGPP code
         runs in a segment whose base is the block it was loaded into, so
         EIP is the image's own address.
  --map  an Open Watcom map: flat programs (DOS/4GW) run with CS base 0, so
         EIP is linear; --base gives the linear address object 1 was loaded
         at (DOS/4GW reports it), and map addresses are taken from there.

Prints, for each report, its why= and vec=, then one line per address:
"eip  ADDRESS  name+offset" (or "?"), then "stack ADDRESS name+offset"."""
import argparse
import re
import struct
import sys


def coff_symbols(path):
    """The text symbols of a DJGPP image: [(address, name)], and the text range."""
    data = open(path, "rb").read()
    off = 0
    if data[:2] == b"MZ":                                   # the go32 stub in front
        last, pages = struct.unpack_from("<HH", data, 2)
        off = pages * 512 - (512 - last if last else 0)
    magic, nscns, _, symptr, nsyms, opthdr, _ = struct.unpack_from("<HHIIIHH", data, off)
    if magic != 0x14C:
        raise ValueError("%s: no COFF image at %#x" % (path, off))
    sec = off + 20 + opthdr
    text = None
    for i in range(nscns):
        name, _, vaddr, size = struct.unpack_from("<8sIII", data, sec + 40 * i)
        if name.rstrip(b"\0") == b".text":
            text = (i + 1, vaddr, vaddr + size)
    if not text:
        raise ValueError("%s: no .text section" % path)
    strtab = off + symptr + 18 * nsyms
    syms, i = [], 0
    while i < nsyms:
        e = off + symptr + 18 * i
        raw, value, scnum, _, sclass, naux = struct.unpack_from("<8sIhHBB", data, e)
        if raw[:4] == b"\0\0\0\0":
            so = strtab + struct.unpack_from("<I", raw, 4)[0]
            name = data[so:data.index(b"\0", so)].decode("latin-1")
        else:
            name = raw.rstrip(b"\0").decode("latin-1")
        if scnum == text[0] and sclass in (2, 3) and not name.startswith("."):
            syms.append((value, name))
        i += 1 + naux
    syms.sort()
    return syms, (text[1], text[2])


def map_symbols(path, base):
    """An Open Watcom map's symbols in object 1, at base + their offset."""
    syms, lo, hi = [], None, None
    for line in open(path, encoding="latin-1"):
        m = re.match(r"\s*0*1:([0-9a-fA-F]{8})[+*]?\s+(\S+)", line)
        if m:
            a = base + int(m.group(1), 16)
            syms.append((a, m.group(2)))
            lo = a if lo is None else min(lo, a)
            hi = a if hi is None else max(hi, a)
    syms.sort()
    return syms, (lo or 0, (hi or 0) + 0x10000)


def name_of(syms, addr):
    best = None
    for a, n in syms:
        if a > addr:
            break
        best = (a, n)
    if not best:
        return "?"
    return "%s+%#x" % (best[1], addr - best[0]) if addr != best[0] else best[1]


def parse(path):
    """The reports in a log or file: [{why, vec, eip, stack}], in order."""
    lines = open(path, "rb").read().decode("latin-1").replace("\r", "").splitlines()
    tagged = [l[len("GLOS-CRASH "):] for l in lines if l.startswith("GLOS-CRASH ")]
    reps = []
    for line in tagged or lines:                # a log's own lines, or a CRASHnnn.TXT
        m = re.match(r"why=(\S+) vec=(\S+)", line)
        if m:
            reps.append({"why": m.group(1), "vec": m.group(2)})
            continue
        if not reps:
            continue
        m = re.search(r"cs:eip=([0-9a-f]{4}):([0-9a-f]{8})", line)
        if m:
            reps[-1]["eip"] = int(m.group(2), 16)
        m = re.match(r"stack=(.*)", line)
        if m and m.group(1) != "?":
            reps[-1]["stack"] = [int(w, 16) for w in m.group(1).split()]
    reps = [r for r in reps if "eip" in r]
    if not reps:
        raise ValueError("%s: no GLOS-CRASH report" % path)
    return reps


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("report")
    ap.add_argument("--exe")
    ap.add_argument("--map")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=0)
    a = ap.parse_args()
    if bool(a.exe) == bool(a.map):
        ap.error("one of --exe and --map")
    syms, (lo, hi) = coff_symbols(a.exe) if a.exe else map_symbols(a.map, a.base)
    for rep in parse(a.report):
        print("why=%s vec=%s" % (rep["why"], rep["vec"]))
        print("eip   %08x  %s" % (rep["eip"], name_of(syms, rep["eip"])))
        for w in rep.get("stack", []):
            if lo <= w < hi:
                print("stack %08x  %s" % (w, name_of(syms, w)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
