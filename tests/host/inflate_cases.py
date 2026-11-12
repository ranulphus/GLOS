"""inflate_cases.py DIR [FILE...]: NAME.raw and NAME.z (a raw deflate stream) pairs for tests/host/inflate_test.c:
made-up data of several kinds at several of zlib's settings, and each FILE given (the kernel's image, if built).
Prints the pairs' paths without their endings."""
import os
import random
import sys
import zlib

d = sys.argv[1]
os.makedirs(d, exist_ok=True)
rnd = random.Random(7)
text = (b"GLOS-BOOT step=kernel file=%d total=%d\r\n" * 4000) % tuple(range(8000))
cases = {
    "empty": b"",
    "one": b"x",
    "zeros": bytes(200000),
    "random": bytes(rnd.getrandbits(8) for _ in range(70000)),
    "text": text,
    "mixed": bytes(rnd.getrandbits(8) for _ in range(40000)) + text[:90000] + bytes(50000)
             + bytes(rnd.choice(b"abcdefgh") for _ in range(120000)),
    "far": (bytes(rnd.getrandbits(8) for _ in range(32768)) * 5)[:150000],     # matches at the window's whole width
}
for path in sys.argv[2:]:
    if os.path.exists(path):
        cases[os.path.basename(path).replace(".", "_")] = open(path, "rb").read()
for name, raw in cases.items():
    for tag, level, strategy in (("9", 9, zlib.Z_DEFAULT_STRATEGY), ("1", 1, zlib.Z_DEFAULT_STRATEGY),
                                 ("0", 0, zlib.Z_DEFAULT_STRATEGY), ("fixed", 9, zlib.Z_FIXED),
                                 ("huff", 6, zlib.Z_HUFFMAN_ONLY)):
        c = zlib.compressobj(level, zlib.DEFLATED, -15, 9, strategy)
        z = c.compress(raw) + c.flush()
        base = os.path.join(d, "%s-%s" % (name, tag))
        open(base + ".raw", "wb").write(raw)
        open(base + ".z", "wb").write(z)
        print(base)
