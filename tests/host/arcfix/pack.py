"""LZH and ARJ archives written bit by bit, for tests/host/arc_cases.py: the corners of the packing method that
LHA's and ARJ's own tools (make.py) cannot be made to go into. No packer for use: what is given here is the
codes themselves (a byte, or a match's length and distance), and for each of a block's three tables either the
code lengths Huffman's way from the counts, or lengths said outright.

The method (as apps/shared/lzh.c reads it): blocks, each the number of its codes (16 bits), the table T (code
lengths of 19 codes: 3 bits each, 7 and more as 1 bits to a 0; after the third, 2 bits: that many zeros), the
table C (510 lengths, written in T's codes: 0 one zero, 1 and 4 bits 3 to 18 zeros, 2 and 9 bits 20 to 531, n+2
the length n), the table P (as T, without the skip), then the codes: C's, and after one of 256 and more (a match
of code - 253 bytes) one of P's: 0, or the number of bits of the distance - 1, whose bits under the top one
follow. A table of one code is written as no lengths and the code: its codes take no bits."""
import heapq, struct, zlib

NC, NT = 510, 19
LH5, LH6, LH7, ARJ = (14, 4, 1 << 13), (16, 5, 1 << 15), (17, 5, 1 << 16), (17, 5, 26624)     # P's codes, bits, window


class Bits:
    def __init__(self):
        self.out, self.acc, self.n = bytearray(), 0, 0

    def put(self, value, nbits):
        for i in range(nbits - 1, -1, -1):
            self.acc = self.acc << 1 | (value >> i & 1)
            self.n += 1
            if self.n == 8:
                self.out.append(self.acc)
                self.acc = self.n = 0

    def bytes(self):
        return bytes(self.out) + (bytes([self.acc << (8 - self.n)]) if self.n else b"")


def huffman(counts, limit=16):
    """code lengths for the counts (0: no code), none over limit; one code alone has length 0"""
    counts = list(counts)
    while True:
        heap = [(c, i, (i,)) for i, c in enumerate(counts) if c]
        lens = [0] * len(counts)
        if len(heap) < 2:
            return lens
        heapq.heapify(heap)
        tie = len(counts)
        while len(heap) > 1:
            a, b = heapq.heappop(heap), heapq.heappop(heap)
            for s in a[2] + b[2]:
                lens[s] += 1
            heapq.heappush(heap, (a[0] + b[0], tie, a[2] + b[2]))
            tie += 1
        if max(lens) <= limit:
            return lens
        counts = [(c + 1) // 2 if c else 0 for c in counts]     # (flatter, and again)


def codes(lens):
    """each coded thing's (code, bits): the shorter first, then by number, as the unpackers count them"""
    out, code = {}, 0
    for n in range(1, 17):
        for s, l in enumerate(lens):
            if l == n:
                out[s] = (code, n)
                code += 1
        code <<= 1
    return out


def table(lens, counts):
    """(lens, codes, the one code of a table of one or None)"""
    used = [s for s, c in enumerate(counts) if c]
    if lens is None:
        lens = huffman(counts)
    if not any(lens):
        one = used[0] if used else 0
        return lens, {one: (0, 0)}, one
    assert sum(2.0 ** -l for l in lens if l) == 1.0, "no whole code: %r" % lens
    assert all(lens[s] for s in used), "a thing used has no code"
    return lens, codes(lens), None


def put_lens(w, lens, one, nbit, special):
    if one is not None:
        w.put(0, nbit)
        w.put(one, nbit)
        return
    n = max(i for i, l in enumerate(lens) if l) + 1
    w.put(n, nbit)
    i = 0
    while i < n:
        k = lens[i]
        i += 1
        if k <= 6:
            w.put(k, 3)
        else:
            w.put((1 << (k - 3)) - 2, k - 3)
        if i == special:
            z = 0
            while i < 6 and (i >= len(lens) or lens[i] == 0):
                i, z = i + 1, z + 1
            w.put(z, 2)


def block(w, tokens, kind, clen=None, plen=None, tlen=None, runs=(20, 3)):
    """One block of tokens (a byte's number, or (length, distance): 3 to 256 bytes from `distance` bytes back,
    1 the byte before). runs: zeros in C's lengths are written as code 2 from runs[0] of them on and as code 1
    from runs[1] on (LHA's packers: 20 and 3), else one by one."""
    np, pbit, _ = kind
    cs, ps = [], []
    for t in tokens:
        if isinstance(t, int):
            cs.append(t)
        else:
            assert 3 <= t[0] <= 256 and t[1] >= 1
            cs.append(253 + t[0])
            ps.append(t[1] - 1)
    ccount, pcount = [0] * NC, [0] * np
    for c in cs:
        ccount[c] += 1
    for d in ps:
        pcount[d.bit_length()] += 1
    clen, ccode, cone = table(clen, ccount)
    plen, pcode, pone = table(plen, pcount)
    w.put(len(cs), 16)
    if cone is not None:                # C of one code: T is of one too
        w.put(0, 5), w.put(0, 5)
        w.put(0, 9), w.put(cone, 9)
    else:
        n = max(i for i, l in enumerate(clen) if l) + 1
        seq, i = [], 0                  # (T's code, bits after it, their value)
        while i < n:
            if clen[i]:
                seq.append((clen[i] + 2, 0, 0))
                i += 1
                continue
            k = 0
            while clen[i + k] == 0:
                k += 1
            i += k
            while k:
                if k >= max(runs[0], 20):
                    take = min(k, 531)
                    seq.append((2, 9, take - 20))
                elif k >= max(runs[1], 3):
                    take = min(k, 18)
                    seq.append((1, 4, take - 3))
                else:
                    take = 1
                    seq.append((0, 0, 0))
                k -= take
        tcount = [0] * NT
        for s, _, _ in seq:
            tcount[s] += 1
        tlen, tcode, tone = table(tlen, tcount)
        put_lens(w, tlen, tone, 5, 3)
        w.put(n, 9)
        for s, nb, v in seq:
            w.put(*tcode[s])
            w.put(v, nb)
    put_lens(w, plen, pone, pbit, -1)
    ps.reverse()
    for c in cs:
        w.put(*ccode[c])
        if c >= 256:
            d = ps.pop()
            j = d.bit_length()
            w.put(*pcode[j])
            if j > 1:
                w.put(d - (1 << (j - 1)), j - 1)


def parse(data, window, longest=256, chain=8):
    """the bytes as tokens: at each place the longest match among the last few places its three bytes were at"""
    out, seen, i, n = [], {}, 0, len(data)
    while i < n:
        best, where = 0, 0
        key = data[i:i + 3]
        if len(key) == 3:
            for j in reversed(seen.get(key, ())):
                if i - j > window:
                    break
                k = 3
                top = min(longest, n - i)
                while k < top and data[j + k] == data[i + k]:
                    k += 1
                if k > best:
                    best, where = k, i - j
        step = best if best >= 3 else 1
        out.append((best, where) if best >= 3 else data[i])
        for k in range(i, i + step):
            key = data[k:k + 3]
            if len(key) == 3:
                lst = seen.setdefault(key, [])
                lst.append(k)
                if len(lst) > chain:
                    del lst[0]
        i += step
    return out


def unparse(tokens, before=b""):
    """what the tokens unpack to (`before`: what the window holds before the first byte)"""
    out = bytearray(before)
    for t in tokens:
        if isinstance(t, int):
            out.append(t)
        else:
            for _ in range(t[0]):
                out.append(out[-t[1]])
    return bytes(out[len(before):])


def pack(tokens, kind, per=4000, **how):
    """tokens as blocks of `per` codes each"""
    w = Bits()
    for i in range(0, len(tokens), per):
        block(w, tokens[i:i + per], kind, **how)
    return w.bytes()


def arj4(tokens):
    """ARJ's method 4: a number is 1 bits (one for each bit it has over its least count of them) to a 0, then its
    bits; a 0 is a byte, anything else a match of that + 2 bytes, whose distance - 1 follows (9 to 13 bits)."""
    w = Bits()

    def number(x, first, most):
        width, plus = first, 0
        while width < most and x >= plus + (1 << width):
            plus += 1 << width
            width += 1
            w.put(1, 1)
        if width < most:
            w.put(0, 1)
        assert x - plus < 1 << width
        w.put(x - plus, width)

    for t in tokens:
        if isinstance(t, int):
            number(0, 0, 7)
            w.put(t, 8)
        else:
            number(t[0] - 2, 0, 7)
            number(t[1] - 1, 9, 13)
    return w.bytes()


# ---- the archives around the packed bytes

def crc16(data):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = crc >> 1 ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def lzh(entries, stamp, level=0):
    """entries: (name, method b"-lh5-", packed bytes, the file's bytes); a level 0 header each (or level 1: the
    name in an extended header, as LHA 2 has it)"""
    out = bytearray()
    for name, method, packed, data in entries:
        nb = name.encode("latin-1")
        if level == 0:
            body = (method + struct.pack("<IIHH", len(packed), len(data), stamp[1], stamp[0]) + b"\x20\x00"
                    + bytes([len(nb)]) + nb + struct.pack("<H", crc16(data)))
            ext = b""
        else:
            ext = struct.pack("<B", 1) + nb + struct.pack("<H", 0)      # (its size is in the header before it)
            body = (method + struct.pack("<IIHH", len(packed) + len(ext), len(data), stamp[1], stamp[0]) + b"\x20\x01"
                    + bytes([0]) + struct.pack("<H", crc16(data)) + b"M" + struct.pack("<H", len(ext)))
        out += bytes([len(body), sum(body) & 0xFF]) + body + ext + packed
    return bytes(out) + b"\0"


def arj(entries, stamp, name="TEST.ARJ"):
    """entries: (name, method 0 to 4, packed bytes, the file's bytes, flags)"""
    def header(fname, method, kind, packed, size, crc, flags):
        first = struct.pack("<BBBBBBBBHHIIIHHH", 30, 11, 1, 0, flags, method, kind, 0, stamp[1], stamp[0], packed, size, crc,
                            0, 0x20, 0)
        body = first + fname.encode("latin-1") + b"\0" + b"\0"
        return b"\x60\xEA" + struct.pack("<H", len(body)) + body + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF) + b"\0\0"

    out = bytearray(header(name, 0, 2, 0, 0, 0, 0))
    for e in entries:
        fname, method, packed, data = e[:4]
        out += header(fname, method, 0, len(packed), len(data), zlib.crc32(data) & 0xFFFFFFFF, e[4] if len(e) > 4 else 0)
        out += packed
    return bytes(out) + b"\x60\xEA\0\0"
