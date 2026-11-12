#!/usr/bin/env python3
"""packkernel.py GLOSK.BIN OUT: the kernel's image packed (deflate), for where the disk is small (a 720 KB setup
floppy: docs/floppy.md). GLOS.EXE takes either form under the name GLOSK.BIN (loader/main.c, loader/inflate.c).

OUT: "GLOZ", then the image's own entry, file size and total (its header's, which the loader needs before it
has memory to unpack into), the packed stream's length, the image's Adler-32, and the stream: the whole image,
its header too, as raw deflate."""
import struct
import sys
import zlib

raw = open(sys.argv[1], "rb").read()
magic, entry, file_size, total = struct.unpack("<4I", raw[:16])
if magic != 0x4B4F4C47 or file_size != len(raw):
    sys.exit("packkernel: %s isn't a kernel image" % sys.argv[1])
c = zlib.compressobj(9, zlib.DEFLATED, -15, 9)
z = c.compress(raw) + c.flush()
open(sys.argv[2], "wb").write(struct.pack("<6I", 0x5A4F4C47, entry, file_size, total, len(z), zlib.adler32(raw)) + z)
print("packkernel: %d -> %d bytes" % (len(raw), len(z) + 24))
