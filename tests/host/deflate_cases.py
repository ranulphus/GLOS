#!/usr/bin/env python3
"""Inputs for tests/host/deflate_test.c (apps/shared/deflate.c), and the check of what it packed by zlib, which
did not write it.

    deflate_cases.py make DIR       writes the inputs and DIR/LIST; prints DIR
    deflate_cases.py check DIR      each NAME.L1 and NAME.L2 the test wrote is unpacked by zlib and must be the
                                    input; and must not be much longer than zlib's own packing of it
"""
import os, random, sys, zlib

what, d = sys.argv[1], sys.argv[2]
os.makedirs(d, exist_ok=True)
rnd = random.Random(20261119)


def text(n):
    words = ["the", "desktop", "window", "folder", "archive", "file", "of", "a", "GLOS", "and", "is", "packed\r\n",
             "screen", "keyboard", "which", "its", "to", "from"]
    return (" ".join(rnd.choice(words) for _ in range(n // 4 + 8))).encode()[:n]


def noise(n):
    return bytes(rnd.getrandbits(8) for _ in range(n))


def binary(n):                          # something like a program: tables, small numbers, a few strings
    out = bytearray()
    while len(out) < n:
        k = rnd.randrange(4)
        if k == 0:
            out += bytes(rnd.choice(b"\x00\x00\x00\x01\x02\xff\x8b\x45\x89\xe8") for _ in range(rnd.randrange(8, 200)))
        elif k == 1:
            out += rnd.choice([b"GLOS-", b"error=", b"\x55\x89\xe5\x83\xec", b"\xc9\xc3\x90\x90"]) * rnd.randrange(1, 6)
        elif k == 2:
            v = rnd.randrange(1 << 20)
            out += b"".join((v + i * 4).to_bytes(4, "little") for i in range(rnd.randrange(4, 64)))
        else:
            out += noise(rnd.randrange(1, 24))
    return bytes(out[:n])


CASES = {
    "EMPTY": b"",
    "ONE": b"x",
    "TWO": b"ab",
    "THREE": b"abc",
    "SAME": b"a" * 100000,
    "ZEROS": bytes(300000),
    "TEXT": text(200000),
    "NOISE": noise(70000),
    "BINARY": binary(180000),
    "MIXED": text(60000) + noise(40000) + bytes(50000) + binary(90000) + text(30000) + noise(3000) + text(100000),
    "FARBACK": noise(1000) + bytes(31768) + noise(1000)[:0] + text(500) + noise(40000)[:0],
    "REPEAT32K": (lambda b: b + b + b)(noise(32768)),           # each time exactly as far back as a match may reach
    "REPEAT32K1": (lambda b: b + b + b)(noise(32769)),          # ... and a byte further: nothing to find
    "SHORTRUNS": b"".join(bytes([rnd.randrange(256)]) * rnd.randrange(1, 9) for _ in range(30000)),
    "RUN258": (b"q" * 258 + b"Z") * 300,
    "W32767": text(32767), "W32768": text(32768), "W32769": text(32769),
    "W65535": text(65535), "W65536": text(65536), "W65537": text(65537),
    "W65800": text(65800), "W98304": text(98304),
    "NOISY64K": noise(65536),
    "SKEW": bytes(rnd.choice(b"aaaaaaaaaaaaaaaabbbbbbbbccccdde") for _ in range(120000)),     # codes of very unlike lengths
    "ALLBYTES": bytes(range(256)) * 40,
    "BIG": text(700000) + binary(500000),
}

if what == "make":
    for name, data in CASES.items():
        open(os.path.join(d, name), "wb").write(data)
    open(os.path.join(d, "LIST"), "w").write("\n".join(CASES) + "\n")
    print(d)
    sys.exit(0)

bad = 0
total_ours = [0, 0]
total_zlib = [0, 0]
for name, data in CASES.items():
    for i, level in enumerate((1, 2)):
        path = os.path.join(d, "%s.L%d" % (name, level))
        try:
            packed = open(path, "rb").read()
            o = zlib.decompressobj(-15)
            out = o.decompress(packed) + o.flush()
            if out != data or not o.eof or o.unused_data:
                print("FAIL %s level %d: zlib unpacks %d bytes of %d (eof=%s, %d bytes left over)"
                      % (name, level, len(out), len(data), o.eof, len(o.unused_data)))
                bad += 1
                continue
        except Exception as e:
            print("FAIL %s level %d: %r" % (name, level, e))
            bad += 1
            continue
        c = zlib.compressobj(1 if level == 1 else 6, zlib.DEFLATED, -15)
        z = len(c.compress(data) + c.flush())
        total_ours[i] += len(packed)
        total_zlib[i] += z
        # No more than a sixth and a few bytes over zlib's (level 1 against its 1, level 2 against its 6), and
        # never much more than the bytes themselves.
        if len(packed) > z * 7 // 6 + 64 or len(packed) > len(data) + len(data) // 1000 + 16:
            print("FAIL %s level %d: %d bytes packed to %d; zlib's %d" % (name, level, len(data), len(packed), z))
            bad += 1
print("deflate_cases: %d failures (%d inputs; packed to %d and %d bytes; zlib's levels 1 and 6: %d and %d)"
      % (bad, len(CASES), total_ours[0], total_ours[1], total_zlib[0], total_zlib[1]))
sys.exit(1 if bad else 0)
