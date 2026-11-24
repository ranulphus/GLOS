#!/usr/bin/env python3
"""packkernel.py GLOSK.BIN OUT: the kernel's image packed (deflate), for where the disk is small (a 720 KB setup
floppy: docs/floppy.md). GLOS.EXE takes either form under the name GLOSK.BIN (loader/main.c, loader/inflate.c).

OUT: "GLOX", then the image's own entry, file size and total (its header's, which the loader needs before it
has memory to unpack into), the packed stream's length, the image's Adler-32, and the stream: the whole image,
its header too, as raw deflate.

Before it is packed the image's near calls and jumps (E8h, E9h and their 32-bit distance) are given their
targets' places in the image in the distance's stead, as Microsoft's LZX does for programs: the same target is
then the same four bytes wherever it is called from, and the stream is some 30 KB shorter (2026-11-24: the 720 KB
floppy had 2 KB to spare for the kernel, then none). The loader puts the distances back (pk_calls, the same
rule): each block of 32,768 bytes by itself (the unpacker's window, which the loader gets a block at a time),
an opcode with its four bytes inside the block and ten bytes or more before the image's end, the five bytes
stepped over whether changed or not. A distance d at place p in an image of n bytes changes if -p <= d < n: to
d + p if that is below n, else to d - n; so every value has one other and comes back. ("GLOZ": a stream of the
image as it is, which the loader takes too.)"""
import struct
import sys
import zlib

BLOCK = 32768

raw = open(sys.argv[1], "rb").read()
magic, entry, file_size, total = struct.unpack("<4I", raw[:16])
if magic != 0x4B4F4C47 or file_size != len(raw):
    sys.exit("packkernel: %s isn't a kernel image" % sys.argv[1])


def calls(image, encode):
    b, n = bytearray(image), len(image)
    for base in range(0, n, BLOCK):
        end, i = min(base + BLOCK, n), base
        while i + 5 <= end:
            if b[i] in (0xE8, 0xE9) and i + 10 < n:
                v = struct.unpack_from("<i", b, i + 1)[0]
                if -i <= v < n:
                    if encode:
                        v = v + i if v < n - i else v - n
                    else:
                        v = v - i if v >= 0 else v + n
                    struct.pack_into("<i", b, i + 1, v)
                i += 5
            else:
                i += 1
    return bytes(b)


turned = calls(raw, True)
if calls(turned, False) != raw:
    sys.exit("packkernel: the calls don't come back")
c = zlib.compressobj(9, zlib.DEFLATED, -15, 9)
z = c.compress(turned) + c.flush()
open(sys.argv[2], "wb").write(struct.pack("<6I", 0x584F4C47, entry, file_size, total, len(z), zlib.adler32(raw)) + z)
print("packkernel: %d -> %d bytes" % (len(raw), len(z) + 24))
