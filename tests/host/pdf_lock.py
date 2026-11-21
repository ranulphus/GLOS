"""Locked (encrypted) PDFs for the host's tests, made as a writer of such files makes them (ISO 32000-1 7.6, and
32000-2 for the newest kind): the format's standard security handler in all its kinds, in plain Python (hashlib's
hashes; RC4 and AES written out here: tests/host has no library to lean on). Used by pdf_cases.py.

    lock = Lock(v, r, bits, how, user=b"", owner=b"owner", ident=b"...")
    lock.string(num, b"text")      a string of object num, locked, as it is written: <hex>
    lock.data(num, bytes)          a stream's bytes of object num, locked
    lock.dictionary()              the Encrypt dictionary"""
import hashlib, struct

PAD = bytes([0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41, 0x64, 0x00, 0x4E, 0x56, 0xFF, 0xFA, 0x01, 0x08,
             0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68, 0x3E, 0x80, 0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A])


def rc4(key, data):
    s, j, out = list(range(256)), 0, bytearray()
    for i in range(256):
        j = (j + s[i] + key[i % len(key)]) & 255
        s[i], s[j] = s[j], s[i]
    i = j = 0
    for c in data:
        i = (i + 1) & 255
        j = (j + s[i]) & 255
        s[i], s[j] = s[j], s[i]
        out.append(c ^ s[(s[i] + s[j]) & 255])
    return bytes(out)


# ---- AES (FIPS 197), encrypting only: its S-box worked out, not copied
def _mul(a, b):
    r = 0
    while b:
        if b & 1:
            r ^= a
        a = ((a << 1) ^ 0x11B) if a & 0x80 else a << 1
        b >>= 1
    return r


def _sbox():
    box = []
    for x in range(256):
        inv = next((y for y in range(1, 256) if _mul(x, y) == 1), 0)
        s = inv
        for k in range(1, 5):
            s ^= ((inv << k) | (inv >> (8 - k))) & 0xFF
        box.append(s ^ 0x63)
    return box


SBOX = _sbox()


def _expand(key):
    nk, w, rcon = len(key) // 4, [list(key[i:i + 4]) for i in range(0, len(key), 4)], 1
    for i in range(nk, 4 * (nk + 7)):
        t = list(w[i - 1])
        if i % nk == 0:
            t = [SBOX[t[1]] ^ rcon, SBOX[t[2]], SBOX[t[3]], SBOX[t[0]]]
            rcon = _mul(rcon, 2)
        elif nk > 6 and i % nk == 4:
            t = [SBOX[b] for b in t]
        w.append([a ^ b for a, b in zip(w[i - nk], t)])
    return [sum(w[4 * r:4 * r + 4], []) for r in range(nk + 7)]


def _block(keys, block):
    s = [a ^ b for a, b in zip(block, keys[0])]
    for r in range(1, len(keys)):
        s = [SBOX[b] for b in s]
        s = [s[(i + 4 * (i % 4)) % 16] for i in range(16)]             # the rows shifted (the state is column by column)
        if r < len(keys) - 1:
            m = []
            for c in range(0, 16, 4):
                a = s[c:c + 4]
                m += [_mul(a[i], 2) ^ _mul(a[(i + 1) % 4], 3) ^ a[(i + 2) % 4] ^ a[(i + 3) % 4] for i in range(4)]
            s = m
        s = [a ^ b for a, b in zip(s, keys[r])]
    return bytes(s)


def aes_cbc(key, iv, data):
    """whole blocks encrypted in a chain (the caller pads)"""
    keys, out, last = _expand(key), bytearray(), iv
    for i in range(0, len(data), 16):
        last = _block(keys, bytes(a ^ b for a, b in zip(data[i:i + 16], last)))
        out += last
    return bytes(out)


assert _block(_expand(bytes(range(16))), bytes.fromhex("00112233445566778899aabbccddeeff")).hex() == "69c4e0d86a7b0430d8cdb78070b4c55a"
assert _block(_expand(bytes(range(32))), bytes.fromhex("00112233445566778899aabbccddeeff")).hex() == "8ea2b7ca516745bfeafc49904b496089"


def hash6(password, salt, udata=b""):
    """R 6's hash (ISO 32000-2, algorithm 2.B)"""
    k, i = hashlib.sha256(password + salt + udata).digest(), 0
    while True:
        e = aes_cbc(k[:16], k[16:32], (password + k + udata) * 64)
        k = (hashlib.sha256, hashlib.sha384, hashlib.sha512)[sum(e[:16]) % 3](e).digest()
        i += 1
        if i >= 64 and e[-1] <= i - 32:
            return k[:32]


class Lock:
    """how: "rc4", "aes" (128 bits: V 4), "aes256" (V 5)"""

    def __init__(self, v, r, bits, how, user=b"", owner=b"owner", ident=b"0123456789abcdef", p=-3904, meta=True, strings=True):
        self.v, self.r, self.how, self.p, self.meta, self.ident, self.strings = v, r, how, p, meta, ident, strings
        self.n = 5 if r == 2 else 32 if v == 5 else bits // 8
        if v == 5:                              # the file's key is its own; U and UE lock it by the user's password
            self.key = bytes((i * 7 + 3) & 255 for i in range(32))
            vs, ks = b"valid-8!", b"keysalt8"
            h = hash6 if r == 6 else (lambda pw, salt, udata=b"": hashlib.sha256(pw + salt + udata).digest())
            self.u = h(user, vs) + vs + ks
            self.ue = aes_cbc(h(user, ks), bytes(16), self.key)
            ovs, oks = b"ownerval", b"ownerkey"
            self.o = h(owner, ovs, self.u) + ovs + oks
            self.oe = aes_cbc(h(owner, oks, self.u), bytes(16), self.key)
            perms = struct.pack("<i", p) + b"\xff\xff\xff\xff" + (b"T" if meta else b"F") + b"adb" + b"abcd"
            self.perms = _block(_expand(self.key), perms)
            return
        pad = lambda pw: (pw + PAD)[:32]
        k = hashlib.md5(pad(owner)).digest()    # O: the user's password locked with the owner's
        for _ in range(50 if r >= 3 else 0):
            k = hashlib.md5(k).digest()
        self.o = rc4(k[:self.n], pad(user))
        for i in range(1, 20 if r >= 3 else 1):
            self.o = rc4(bytes(b ^ i for b in k[:self.n]), self.o)
        k = hashlib.md5(pad(user) + self.o + struct.pack("<i", p) + ident + (b"\xff\xff\xff\xff" if r >= 4 and not meta else b"")).digest()
        for _ in range(50 if r >= 3 else 0):
            k = hashlib.md5(k[:self.n]).digest()
        self.key = k[:self.n]
        if r == 2:
            self.u = rc4(self.key, PAD)
        else:
            u = rc4(self.key, hashlib.md5(PAD + ident).digest())
            for i in range(1, 20):
                u = rc4(bytes(b ^ i for b in self.key), u)
            self.u = u + bytes(16)

    def _lock(self, num, data, gen=0):
        if self.how == "aes256":
            key = self.key
        else:
            key = hashlib.md5(self.key + struct.pack("<I", num)[:3] + struct.pack("<H", gen) + (b"sAlT" if self.how == "aes" else b"")).digest()
            key = key[:min(self.n + 5, 16)]
        if self.how == "rc4":
            return rc4(key, data)
        iv = hashlib.md5(b"iv %d" % num + data[:8]).digest()            # (any 16 bytes; these are the same every time)
        fill = 16 - len(data) % 16
        return iv + aes_cbc(key, iv, data + bytes([fill]) * fill)

    def string(self, num, text):
        return b"<" + (self._lock(num, text) if self.strings else text).hex().encode() + b">"

    def data(self, num, data):
        return self._lock(num, data)

    def dictionary(self):
        d = b"<< /Filter /Standard /V %d /R %d /O <%s> /U <%s> /P %d" % (self.v, self.r, self.o.hex().encode(), self.u.hex().encode(), self.p)
        if self.v in (2, 4):
            d += b" /Length %d" % (self.n * 8)
        if self.v >= 4:
            name = {"rc4": b"V2", "aes": b"AESV2", "aes256": b"AESV3"}[self.how]
            d += b" /CF << /StdCF << /CFM /%s /AuthEvent /DocOpen /Length %d >> >> /StmF /StdCF /StrF %s" % (
                name, self.n, b"/StdCF" if self.strings else b"/Identity")
            if not self.meta:
                d += b" /EncryptMetadata false"
        if self.v == 5:
            d += b" /Length 256 /OE <%s> /UE <%s> /Perms <%s>" % (self.oe.hex().encode(), self.ue.hex().encode(), self.perms.hex().encode())
        return d + b" >>"
