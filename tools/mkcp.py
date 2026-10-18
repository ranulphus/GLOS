#!/usr/bin/env python3
"""DOS code pages' upper halves as Unicode (M10 step G): sdk/config/cp_tables.c, from Python's own codecs (cp437, cp850,
cp852, cp866, cp737), so that the tables can't be typed wrong. `make codepages` runs it.

  mkcp.py > sdk/config/cp_tables.c"""
import sys

PAGES = [437, 850, 852, 866, 737]


def main():
    out = sys.stdout
    out.write("/* DOS code pages 80h-FFh as Unicode (include/glos/cp.h; M10 step G). Made by tools/mkcp.py from\n")
    out.write(" * Python's codecs: not to be edited. */\n#include \"glos/cp.h\"\n\n")
    for cp in PAGES:
        out.write("static const unsigned short cp%d[128] = {" % cp)
        for i in range(128):
            u = ord(bytes([0x80 + i]).decode("cp%d" % cp))
            out.write("%s0x%04X," % ("\n    " if i % 12 == 0 else " ", u))
        out.write("\n};\n\n")
    out.write("const unsigned short *cp_table(int page)\n{\n    switch (page) {\n")
    for cp in PAGES:
        out.write("    case %d:\n        return cp%d;\n" % (cp, cp))
    out.write("    }\n    return 0;\n}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
