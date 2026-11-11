#!/usr/bin/env python3
"""kernel/drv/fmbank.c: what GLOS's FM driver (kernel/drv/fm.c) plays General MIDI with.

  mkfmbank.py FREEDOOM.ZIP OUT.c

The instruments are Freedoom's GENMIDI lump (175 two-operator OPL patches: 128 melodic, 47 percussion; the Freedoom
project's own, BSD-3-Clause: THIRD_PARTY.md), read from a release's freedoom1.wad as it is. The two tables beside
them are computed here:

  fm_fnum[384]  an octave of F-numbers from F sharp in steps of a 32nd of a semitone, for A = 440 Hz at the
                chip's 49716 Hz: 440 * 2^((i - 96) / 384) * 2^16 / 49716 (block 4: F sharp 4 is its first).
  fm_atten[128] a MIDI level (0 to 127) as attenuation in the chip's 0.75 dB steps: 40 log10(127 / v) dB, as
                General MIDI recommends for volume and expression (63: silent).

Run by `make fmbank`; its output is kept in the repository, so the kernel's build needs no download.
"""
import hashlib
import math
import struct
import sys
import zipfile

LUMP_SHA256 = "0bea035cca1ef715c8bbbc00a7eb207084f0f8c47f8900dc053e9b1923a7d770"


def lump(zip_path):
    z = zipfile.ZipFile(zip_path)
    name = [n for n in z.namelist() if n.endswith("/freedoom1.wad")][0]
    d = z.read(name)
    magic, n, off = struct.unpack("<4sII", d[:12])
    if magic != b"IWAD":
        raise SystemExit("mkfmbank: %s: not a WAD" % name)
    for i in range(n):
        pos, size, lname = struct.unpack("<II8s", d[off + 16 * i: off + 16 * i + 16])
        if lname.rstrip(b"\0") == b"GENMIDI":
            return d[pos:pos + size]
    raise SystemExit("mkfmbank: no GENMIDI lump")


def main():
    g = lump(sys.argv[1])
    if len(g) != 8 + 175 * 36 + 175 * 32 or g[:8] != b"#OPL_II#":
        raise SystemExit("mkfmbank: GENMIDI isn't 175 instruments")
    if hashlib.sha256(g).hexdigest() != LUMP_SHA256:
        raise SystemExit("mkfmbank: GENMIDI's sha256 is %s, not the one checked" % hashlib.sha256(g).hexdigest())
    out = ["/* Made by tools/mkfmbank.py (make fmbank): don't edit.",
           " *",
           " * fm_bank: Freedoom's GENMIDI lump (freedoom 0.13.0, sha256 %s...)," % LUMP_SHA256[:16],
           " * Copyright (c) 2001-2024 Contributors to the Freedoom project, BSD-3-Clause (THIRD_PARTY.md).",
           " * fm_fnum, fm_atten: computed (tools/mkfmbank.py says how). */",
           '#include "fm.h"',
           "",
           "const u8 fm_bank[FM_PATCHES][FM_PATCH_BYTES] = {"]
    for i in range(175):
        r = g[8 + 36 * i: 8 + 36 * i + 36]
        name = g[8 + 175 * 36 + 32 * i:][:32].split(b"\0")[0].decode("ascii")
        out.append("    { %s },  /* %d %s */" % (",".join("%d" % b for b in r), i, name.replace("*/", "")))
    out.append("};")
    out.append("")
    out.append("const u16 fm_fnum[384] = {")
    f = [round(440.0 * 2 ** ((i - 96) / 384.0) * 65536 / 49716) for i in range(384)]
    assert f[96] == 580 and max(f) < 1024
    for i in range(0, 384, 16):
        out.append("    " + ",".join("%d" % v for v in f[i:i + 16]) + ",")
    out.append("};")
    out.append("")
    out.append("const u8 fm_atten[128] = {")
    a = [63] + [min(63, round(40 * math.log10(127.0 / v) / 0.75)) for v in range(1, 128)]
    for i in range(0, 128, 16):
        out.append("    " + ",".join("%d" % v for v in a[i:i + 16]) + ",")
    out.append("};")
    open(sys.argv[2], "w").write("\n".join(out) + "\n")


main()
