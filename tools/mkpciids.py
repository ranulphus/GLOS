#!/usr/bin/env python3
"""mkpciids.py PCI.IDS OUT: the Devices page's names for PCI devices (apps/shared/pciids.c reads them).

From the PCI ID Project's list (pci.ids; https://pci-ids.ucw.cz/, used under its 3-clause BSD licence option:
THIRD_PARTY.md): its header's lines (who keeps it, its version, its copyright), then the vendors and their
devices, nothing else (no subsystems, no classes), as text packed by deflate:

    "GLOSPCI1"  u32 the text's length (low byte first)  the deflate stream (RFC 1951, no wrapper)

The text, a line each: "# ..." the header's; "VVVV Name" a vendor; "\\tDDDD Name" a device of the vendor before
it. Lowercase hexadecimal, as the list has them. The list's input is untrusted data: only lines of those forms
are kept, each name at most 120 bytes."""
import re
import struct
import sys
import zlib


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    lines = open(sys.argv[1], encoding="utf-8", errors="replace").read().split("\n")
    out, head, vendors, devices = [], True, 0, 0
    for line in lines:
        if head:
            if line.startswith("#\t"):
                out.append("# " + line[2:].strip()[:120])
                continue
            if line == "#":
                continue
            head = False
        if line.startswith("C "):               # (the classes' part: the page has its own words for those)
            break
        m = re.match(r"([0-9a-f]{4})  (\S.*)$", line)
        if m:
            out.append("%s %s" % (m.group(1), m.group(2).strip()[:120]))
            vendors += 1
            continue
        m = re.match(r"\t([0-9a-f]{4})  (\S.*)$", line)
        if m and vendors:
            out.append("\t%s %s" % (m.group(1), m.group(2).strip()[:120]))
            devices += 1
    if vendors < 100:
        sys.exit("mkpciids: %s does not look like pci.ids (%d vendors)" % (sys.argv[1], vendors))
    text = ("\n".join(out) + "\n").encode("utf-8")
    z = zlib.compressobj(9, zlib.DEFLATED, -15)
    packed = z.compress(text) + z.flush()
    open(sys.argv[2], "wb").write(b"GLOSPCI1" + struct.pack("<I", len(text)) + packed)
    print("mkpciids: %d vendors, %d devices, %d bytes (%d packed)" % (vendors, devices, len(text), len(packed) + 12))


if __name__ == "__main__":
    main()
