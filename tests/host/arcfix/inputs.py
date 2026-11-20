"""The files inside the LZH and ARJ archives beside this, which LHA's and ARJ's own tools packed (make.py), and
the same bytes made again by tests/host/arc_cases.py for what the reader must unpack them to. Only getrandbits is
used: its numbers are the same in every Python."""
import random

STAMP = (2026, 11, 19, 21, 30, 44)      # every file's and folder's time (UTC)


def files():
    """(name, bytes; None for a folder), in the order they are given to the tools"""
    r = random.Random(20261120)
    words = ["the", "desktop", "window", "folder", "archive", "file", "of", "a", "GLOS", "and", "is", "unpacked\r\n",
             "packed", "by", "LHA", "ARJ", "in", "1991", "or", "so"]

    def text(n):
        return " ".join(words[r.getrandbits(8) % len(words)] for _ in range(n // 4)).encode()[:n]

    def four(n):                        # of four letters, no order in them: two bits a byte, few matches
        return bytes(b"ACGT"[r.getrandbits(2)] for _ in range(n))

    def skew(n):                        # byte k half as often as byte k-1: codes of every length
        out = bytearray()
        for _ in range(n):
            k, x = 0, r.getrandbits(20)
            while x & 1 and k < 19:
                k, x = k + 1, x >> 1
            out.append(65 + k)
        return bytes(out)

    far = bytearray(four(34000))
    far += far[100:900] + four(300) + far[1000:1600] + far[33000:33400]     # matches 34 KB back, and near ones
    return [
        ("READ.ME", b"LZH and ARJ archives are read as folders too.\r\n"),
        ("TEXT.TXT", text(9000)),
        ("FAR.BIN", bytes(far)),
        ("SKEW.BIN", skew(24000)),
        ("ZEROS.BIN", bytes(70000)),
        ("NOISE.BIN", bytes(r.getrandbits(8) for _ in range(1000))),
        ("EMPTY", b""),
        ("DOCS", None),
        ("DOCS/NOTE.TXT", text(700)),
        ("DOCS/DEEP", None),
        ("DOCS/DEEP/A.TXT", b"a file two folders down\r\n"),
        ("EMPTYDIR", None),
    ]
