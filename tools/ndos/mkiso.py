#!/usr/bin/env python3
"""mkiso: an ISO 9660 CD image of a folder, as GLOS makes its own (docs/v1/native-dos.md, N4's CD-ROM; phase G's
boot CD). Python alone, no mkisofs: for loopa's CD-ROM cases and `make cd-image`, on any PC that runs the gate.

    mkiso.py OUT.iso FOLDER [--label LABEL] [--boot-floppy IMG] [--cue OUT.cue --tone SECONDS ...]

Level 1 names (8.3, upper case, ";1"; a name that doesn't fit is refused, not cut), folders to eight deep, both
path tables, the dates the files have. --boot-floppy: El Torito's boot record and catalogue, the image (1.2,
1.44 or 2.88 MB) as BOOT.IMG in the root started by floppy emulation, as a PC's BIOS starts a setup floppy.
--cue: a CUE sheet of a mixed-mode disc, the image its first track (MODE1/2048), then an audio track for each
--tone (a sine of 220 Hz times the track's number, SECONDS long, 44.1 kHz 16-bit stereo, in OUT-NN.bin beside the
sheet, after the two seconds' pregap the Red Book wants): for N4's CD audio (MSCDEX's PLAY AUDIO, the drive's
Q channel), as 86Box plays a CUE sheet's audio."""
import math
import argparse
import os
import struct
import sys
import time

SECTOR = 2048


def both16(v):
    return struct.pack("<H", v) + struct.pack(">H", v)


def both32(v):
    return struct.pack("<I", v) + struct.pack(">I", v)


def dir_date(t):
    g = time.gmtime(t)
    return bytes([g.tm_year - 1900, g.tm_mon, g.tm_mday, g.tm_hour, g.tm_min, g.tm_sec, 0])


def vd_date(t):
    return time.strftime("%Y%m%d%H%M%S00", time.gmtime(t)).encode() + b"\0"


def padded(s, n):
    return s.encode("ascii")[:n].ljust(n, b" ")


D_CHARS = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")


def iso_name(name, is_dir):
    """The 8.3 name as ISO 9660 level 1 has it, or None if it doesn't fit."""
    up = name.upper()
    base, dot, ext = up.partition(".")
    if not base or len(base) > 8 or len(ext) > 3 or (is_dir and dot) or not set(base + ext) <= D_CHARS:
        return None
    return up if is_dir else "%s.%s;1" % (base, ext)


class Node:
    def __init__(self, path, name, is_dir, parent=None):
        self.path, self.name, self.is_dir, self.parent = path, name, is_dir, parent
        self.children, self.lba, self.size = [], 0, 0
        self.mtime = os.stat(path).st_mtime if path else time.time()
        self.number = 0


def record(name_bytes, lba, size, mtime, is_dir):
    n = len(name_bytes)
    r = bytes([0, 0]) + both32(lba) + both32(size) + dir_date(mtime) + bytes([2 if is_dir else 0, 0, 0]) \
        + both16(1) + bytes([n]) + name_bytes
    if n % 2 == 0:
        r += b"\0"
    return bytes([len(r)]) + r[1:]


def build(folder, label, boot_floppy=None):
    root = Node(folder, "", True)
    root.parent = root
    bad = []

    def walk(node, depth):
        for e in sorted(os.listdir(node.path)):
            p = os.path.join(node.path, e)
            isd = os.path.isdir(p)
            n = iso_name(e, isd)
            if n is None:
                bad.append(os.path.relpath(p, folder))
                continue
            c = Node(p, n, isd, node)
            node.children.append(c)
            if isd:
                if depth >= 7:
                    bad.append(os.path.relpath(p, folder) + " (more than eight deep)")
                    continue
                walk(c, depth + 1)
    walk(root, 0)
    boot_img = None
    if boot_floppy:
        boot_img = open(boot_floppy, "rb").read()
        media = {1228800: 1, 1474560: 2, 2949120: 3}.get(len(boot_img))
        if not media:
            sys.exit("mkiso: --boot-floppy %s: not a 1.2, 1.44 or 2.88 MB image" % boot_floppy)
        if any(c.name == "BOOT.IMG;1" for c in root.children):
            sys.exit("mkiso: the folder has a BOOT.IMG of its own")
        b = Node(None, "BOOT.IMG;1", False, root)
        b.data, b.mtime = boot_img, time.time()
        root.children.append(b)
        root.children.sort(key=lambda c: c.name)
    if bad:
        sys.exit("mkiso: no ISO 9660 level 1 name for: %s" % ", ".join(bad))

    # The folders in the path tables' order (by level, then parent's number), each numbered.
    dirs, level = [root], [root]
    while level:
        nxt = []
        for d in level:
            nxt += [c for c in d.children if c.is_dir]
        dirs += nxt
        level = nxt
    for i, d in enumerate(dirs):
        d.number = i + 1

    def ptable(big):
        out = b""
        for d in dirs:
            ident = b"\0" if d is root else d.name.encode()
            e = bytes([len(ident), 0]) + struct.pack(">I" if big else "<I", d.lba) \
                + struct.pack(">H" if big else "<H", d.parent.number) + ident
            if len(ident) % 2:
                e += b"\0"
            out += e
        return out

    def dir_bytes(d):
        recs = [record(b"\0", d.lba, d.size, d.mtime, True), record(b"\1", d.parent.lba, d.parent.size, d.parent.mtime, True)]
        recs += [record(c.name.encode(), c.lba, c.size, c.mtime, c.is_dir) for c in d.children]
        out = b""
        for r in recs:                          # (a record never crosses a sector's end)
            if len(out) // SECTOR != (len(out) + len(r) - 1) // SECTOR:
                out += b"\0" * (SECTOR - len(out) % SECTOR)
            out += r
        return out + b"\0" * (-len(out) % SECTOR)

    # Where everything goes: the system area, the descriptors, the path tables, the folders, the catalogue, the files.
    pt_size = len(ptable(False))
    lba = 16 + 1 + (1 if boot_img else 0) + 1
    l_pt = lba
    lba += (pt_size + SECTOR - 1) // SECTOR
    m_pt = lba
    lba += (pt_size + SECTOR - 1) // SECTOR
    for d in dirs:                              # (sizes first: a folder's records depend on its children's places
        d.size = len(dir_bytes(d))              # only by value, not by length)
    for d in dirs:
        d.lba = lba
        lba += d.size // SECTOR
    catalogue = None
    if boot_img:
        catalogue = lba
        lba += 1
    files = []

    def place(d):
        for c in d.children:
            if c.is_dir:
                continue
            c.size = len(c.data) if c.path is None else os.path.getsize(c.path)
            c.lba = lba_box[0] if c.size else 0
            lba_box[0] += (c.size + SECTOR - 1) // SECTOR
            files.append(c)
        for c in d.children:
            if c.is_dir:
                place(c)
    lba_box = [lba]
    place(root)
    total = lba_box[0]

    img = bytearray(total * SECTOR)
    now = time.time()
    rootrec = record(b"\0", root.lba, root.size, root.mtime, True)
    pvd = bytearray(SECTOR)
    pvd[0:8] = b"\x01CD001\x01\0"
    pvd[8:40] = padded("GLOS", 32)
    pvd[40:72] = padded(label.upper(), 32)
    pvd[80:88] = both32(total)
    pvd[120:124] = both16(1)
    pvd[124:128] = both16(1)
    pvd[128:132] = both16(SECTOR)
    pvd[132:140] = both32(pt_size)
    pvd[140:144] = struct.pack("<I", l_pt)
    pvd[148:152] = struct.pack(">I", m_pt)
    pvd[156:190] = rootrec
    for a, n in ((190, 128), (318, 128), (446, 128), (574, 128), (702, 37), (739, 37), (776, 37)):
        pvd[a:a + n] = b" " * n
    pvd[574:574 + 128] = padded("GLOS MKISO", 128)
    pvd[813:830] = vd_date(now)
    pvd[830:847] = vd_date(now)
    pvd[847:864] = b"0000000000000000\0"
    pvd[864:881] = b"0000000000000000\0"
    pvd[881] = 1
    img[16 * SECTOR:17 * SECTOR] = pvd
    at = 17
    if boot_img:
        br = bytearray(SECTOR)
        br[0:7] = b"\0CD001\x01"
        br[7:7 + 23] = b"EL TORITO SPECIFICATION"
        br[71:75] = struct.pack("<I", catalogue)
        img[at * SECTOR:(at + 1) * SECTOR] = br
        at += 1
    img[at * SECTOR:at * SECTOR + 7] = b"\xffCD001\x01"
    pt = ptable(False)
    img[l_pt * SECTOR:l_pt * SECTOR + len(pt)] = pt
    pt = ptable(True)
    img[m_pt * SECTOR:m_pt * SECTOR + len(pt)] = pt
    for d in dirs:
        b = dir_bytes(d)
        img[d.lba * SECTOR:d.lba * SECTOR + len(b)] = b
    if boot_img:
        val = bytearray(32)
        val[0], val[1] = 1, 0                   # (the validation entry: x86)
        val[4:4 + 4] = b"GLOS"
        val[30], val[31] = 0x55, 0xAA
        s = sum(struct.unpack("<16H", bytes(val))) & 0xFFFF
        val[28:30] = struct.pack("<H", (-s) & 0xFFFF)
        boot = next(c for c in root.children if c.name == "BOOT.IMG;1")
        media = {1228800: 1, 1474560: 2, 2949120: 3}[len(boot_img)]
        ent = struct.pack("<BBHBBHI", 0x88, media, 0, 0, 0, 1, boot.lba) + b"\0" * 20
        img[catalogue * SECTOR:catalogue * SECTOR + 64] = bytes(val) + ent
    for f in files:
        data = f.data if f.path is None else open(f.path, "rb").read()
        img[f.lba * SECTOR:f.lba * SECTOR + len(data)] = data
    return bytes(img), len(files), len(dirs)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("folder")
    ap.add_argument("--label", default="GLOS")
    ap.add_argument("--boot-floppy", metavar="IMG")
    ap.add_argument("--cue", metavar="OUT.cue")
    ap.add_argument("--tone", type=float, action="append", default=[], metavar="SECONDS")
    a = ap.parse_args()
    if not os.path.isdir(a.folder):
        sys.exit("mkiso: %s: no such folder" % a.folder)
    if len(a.label) > 32 or not set(a.label.upper()) <= D_CHARS:
        sys.exit("mkiso: --label: up to 32 of A-Z, 0-9 and _")
    img, nf, nd = build(a.folder, a.label, a.boot_floppy)
    open(a.out, "wb").write(img)
    print("mkiso: %s: %d files, %d folders, %d KB%s" % (a.out, nf, nd, len(img) // 1024,
                                                        ", El Torito floppy" if a.boot_floppy else ""))
    if a.cue:
        d = os.path.dirname(os.path.abspath(a.cue))
        base = os.path.splitext(os.path.basename(a.cue))[0]
        lines = ['FILE "%s" BINARY' % os.path.relpath(os.path.abspath(a.out), d), "  TRACK 01 MODE1/2048",
                 "    INDEX 01 00:00:00"]
        for i, secs in enumerate(a.tone):
            frames = int(secs * 75)             # (2352-byte frames: 588 stereo samples each)
            n = frames * 588
            f = 220.0 * (i + 2)
            pcm = bytearray(n * 4)
            for k in range(n):
                v = int(12000 * math.sin(2 * math.pi * f * k / 44100.0))
                struct.pack_into("<hh", pcm, k * 4, v, v)
            name = "%s-%02d.bin" % (base, i + 2)
            open(os.path.join(d, name), "wb").write(pcm)
            lines += ['FILE "%s" BINARY' % name, "  TRACK %02d AUDIO" % (i + 2), "    PREGAP 00:02:00",
                      "    INDEX 01 00:00:00"]
        open(a.cue, "w").write("\r\n".join(lines) + "\r\n")
        print("mkiso: %s: the data track and %d audio track(s)" % (a.cue, len(a.tone)))


if __name__ == "__main__":
    main()
