#!/usr/bin/env python3
"""Marks a DOS program as a GLOS app: "GLOSAPP1" at offset 1Ch of the file,
in the padding of its MZ header (a DJGPP program has a comment of its stub's
there; DOS reads nothing of it).

  glosmark.py PROGRAM.EXE ...     mark
  glosmark.py --check PROGRAM.EXE ...   say which are marked (status 1 if any isn't)

GLOS reads the mark when the program is started (kernel/vm/session.c). A
marked program is expected to call the GLOS API's init at once: until it
does, the apps already running are not paused for it, and it is not given
the screen. An unmarked program can still become an app by asking; it starts
as the exclusive session any DOS program is. Plain DOS ignores the mark."""
import sys

MARK, AT = b"GLOSAPP1", 0x1C


def main():
    args = sys.argv[1:]
    check = args[:1] == ["--check"]
    if check:
        args = args[1:]
    if not args:
        sys.exit(__doc__)
    bad = 0
    for path in args:
        with open(path, "r+b") as f:
            head = f.read(0x40)
            if head[:2] not in (b"MZ", b"ZM"):
                sys.exit("%s: not an MZ executable" % path)
            first = int.from_bytes(head[0x18:0x1A], "little")    # the relocation table, the header's first user
            size = int.from_bytes(head[0x08:0x0A], "little") * 16
            relocs = int.from_bytes(head[0x06:0x08], "little")
            if size < AT + len(MARK) or (relocs and first < AT + len(MARK)):
                sys.exit("%s: no room in its header at %02Xh" % (path, AT))
            if check:
                marked = head[AT:AT + len(MARK)] == MARK
                print("%s: %s" % (path, "marked" if marked else "not marked"))
                bad += not marked
            else:
                f.seek(AT)
                f.write(MARK)
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
