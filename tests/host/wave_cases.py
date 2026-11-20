#!/usr/bin/env python3
"""WAV files for tests/host/wave_test.c (apps/shared/wave.c), written here byte by byte (and by Python's own wave
module), each with the samples it must be read as; and, with `check`, what wave_test wrote read back here.

    wave_cases.py DIR           writes DIR/in-*.wav, their samples as DIR/in-*.pcm (16 bits, as the take has them)
                                and DIR/MANIFEST; prints DIR
    wave_cases.py check DIR     DIR/out-*.wav, which wave_test wrote, are what they must be (exit status)

MANIFEST, a line a file, tabs between: NAME RATE CHANNELS FRAMES, or NAME and the error it must give (-2 ...).

The sound of the tests is sig(i, c): whole numbers only, the same here and in wave_test.c."""
import os, struct, sys, wave

STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
        130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060,
        1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484,
        7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767]
INDEX = [-1, -1, -1, -1, 2, 4, 6, 8]


def sig(i, c):
    """a triangle of 200 frames (one channel) or 316 (the other), with a slower swell over it (which falls back
    from its loudest to its softest at frame 6208: the ADPCM files are shorter, that packing follows no such leap)"""
    period = 200 if c == 0 else 316
    k = i % period
    tri = k if k < period // 2 else period - k           # 0 .. period/2
    swell = 2000 + (i // 64) % 97 * 250                  # 2000 .. 26000
    return tri * 2 * swell // (period // 2) - swell   # (nothing negative divided: C's and Python's ways differ there)


def frames_of(n, ch):
    return [sig(i, c) for i in range(n) for c in range(ch)]


def chunk(name, data):
    return name + struct.pack("<I", len(data)) + data + (b"\0" if len(data) & 1 else b"")


def riff(fmt, data, extra=b"", fact=None, data_says=None):
    body = b"WAVE" + chunk(b"fmt ", fmt) + extra
    if fact is not None:
        body += chunk(b"fact", struct.pack("<I", fact))
    d = chunk(b"data", data)
    if data_says is not None:
        d = b"data" + struct.pack("<I", data_says) + data
    body += d
    return b"RIFF" + struct.pack("<I", len(body)) + body


def pcm_fmt(rate, ch, bits, tag=1):
    return struct.pack("<HHIIHH", tag, ch, rate, rate * ch * ((bits + 7) // 8), ch * ((bits + 7) // 8), bits)


def ima_decode_nibble(state, code):
    pred, index = state
    step = STEP[index]
    d = step >> 3
    if code & 4:
        d += step
    if code & 2:
        d += step >> 1
    if code & 1:
        d += step >> 2
    pred = max(-32768, min(32767, pred - d if code & 8 else pred + d))
    index = max(0, min(88, index + INDEX[code & 7]))
    return pred, index


def ima_encode(samples, ch, align):
    """Microsoft's IMA ADPCM blocks of the samples (interleaved), another packer than wave.c's: it looks for the
    code whose result is nearest, by trying all sixteen"""
    per = (align // ch - 4) * 2 + 1
    n = len(samples) // ch
    out = bytearray()
    index = [0] * ch
    for start in range(0, n, per):
        block = bytearray(align)
        state = []
        for c in range(ch):
            first = samples[start * ch + c]
            struct.pack_into("<hBB", block, c * 4, first, index[c], 0)
            state.append((first, index[c]))
        for k in range(per - 1):
            for c in range(ch):
                at = min(start + 1 + k, n - 1)
                want = samples[at * ch + c]
                best = min(range(16), key=lambda code: (abs(ima_decode_nibble(state[c], code)[0] - want), code))
                state[c] = ima_decode_nibble(state[c], best)
                byte = 4 * ch + (k // 8) * 4 * ch + c * 4 + (k % 8) // 2
                block[byte] |= best << 4 if k & 1 else best
        for c in range(ch):
            index[c] = state[c][1]
        out += block
    return bytes(out)


def ima_decode(data, ch, align, frames):
    per = (align // ch - 4) * 2 + 1
    out = []
    for start in range(0, len(data) - align + 1, align):
        block = data[start:start + align]
        state = []
        for c in range(ch):
            first, index, _ = struct.unpack_from("<hBB", block, c * 4)
            state.append((first, min(index, 88)))
            out.append(first)
        for k in range(per - 1):
            for c in range(ch):
                byte = 4 * ch + (k // 8) * 4 * ch + c * 4 + (k % 8) // 2
                code = block[byte] >> 4 if k & 1 else block[byte] & 15
                state[c] = ima_decode_nibble(state[c], code)
                out.append(state[c][0])
    return out[:frames * ch]


def read_wav(path):
    """(tag, channels, rate, align, bits, per_block, fact, data) of a WAV, by its chunks"""
    b = open(path, "rb").read()
    assert b[:4] == b"RIFF" and b[8:12] == b"WAVE", path
    assert struct.unpack_from("<I", b, 4)[0] == len(b) - 8, "%s: RIFF says %d of %d" % (path, struct.unpack_from("<I", b, 4)[0], len(b) - 8)
    at, fmt, fact, data = 12, None, None, None
    while at + 8 <= len(b):
        name, size = b[at:at + 4], struct.unpack_from("<I", b, at + 4)[0]
        body = b[at + 8:at + 8 + size]
        assert len(body) == size, "%s: chunk %r cut" % (path, name)
        if name == b"fmt ":
            fmt = body
        elif name == b"fact":
            fact = struct.unpack("<I", body[:4])[0]
        elif name == b"data":
            data = body
        at += 8 + size + (size & 1)
    assert at == len(b), "%s: %d bytes after its chunks" % (path, len(b) - at)
    tag, ch, rate, avg, align, bits = struct.unpack_from("<HHIIHH", fmt)
    per = struct.unpack_from("<H", fmt, 18)[0] if tag == 0x11 else 0
    return tag, ch, rate, avg, align, bits, per, fact, data


def check(d):
    bad = []

    def want(cond, text):
        if not cond:
            bad.append(text)

    # PCM of 16 bits, two channels: Python's own reader has every sample
    with wave.open(os.path.join(d, "out-pcm16.wav"), "rb") as w:
        got = list(struct.unpack("<%dh" % (w.getnframes() * 2), w.readframes(w.getnframes())))
        want((w.getnchannels(), w.getframerate(), w.getsampwidth(), w.getnframes()) == (2, 22050, 2, 5000), "pcm16: its shape")
        want(got == frames_of(5000, 2), "pcm16: its samples")
    # ... of 8 bits, one channel, an odd count (the chunk filled to an even size)
    with wave.open(os.path.join(d, "out-pcm8.wav"), "rb") as w:
        got = list(w.readframes(w.getnframes()))
        want((w.getnchannels(), w.getframerate(), w.getsampwidth(), w.getnframes()) == (1, 11025, 1, 3001), "pcm8: its shape")
        want(got == [(s >> 8) + 128 for s in frames_of(3001, 1)], "pcm8: its samples")
    want(os.path.getsize(os.path.join(d, "out-pcm8.wav")) % 2 == 0, "pcm8: an odd file")
    # the stream writer's
    with wave.open(os.path.join(d, "out-stream.wav"), "rb") as w:
        got = list(struct.unpack("<%dh" % (w.getnframes() * 2), w.readframes(w.getnframes())))
        want((w.getnchannels(), w.getframerate(), w.getnframes()) == (2, 44100, 7000) and got == frames_of(7000, 2), "stream")
    # IMA ADPCM: its header's numbers, and unpacked by the unpacker here it is near the sound
    for name, ch, rate, n, align in (("out-adpcm1.wav", 1, 8000, 5000, 256), ("out-adpcm2.wav", 2, 44100, 6000, 2048),
                                     ("out-adpcm3.wav", 2, 22050, 4041, 1024)):
        tag, c, r, avg, al, bits, per, fact, data = read_wav(os.path.join(d, name))
        per_want = (align // ch - 4) * 2 + 1
        want((tag, c, r, al, bits, per, fact) == (0x11, ch, rate, align, 4, per_want, n), "%s: its header %r" % (
            name, (tag, c, r, al, bits, per, fact)))
        want(len(data) == -(-n // per_want) * align, "%s: %d bytes of blocks" % (name, len(data)))
        want(avg == rate * align // per_want, "%s: bytes a second" % name)
        got, src = ima_decode(data, ch, align, n), frames_of(n, ch)
        want(len(got) == n * ch, "%s: %d samples" % (name, len(got)))
        worst = max(abs(a - b) for a, b in zip(got, src)) if len(got) == n * ch else 99999
        mean = sum(abs(a - b) for a, b in zip(got, src)) / (n * ch) if len(got) == n * ch else 99999
        want(worst < 1500 and mean < 60, "%s: unpacked, it is %d off at worst, %.0f in the mean" % (name, worst, mean))
        print("wave_cases check: %s is %d off at worst, %.1f in the mean" % (name, worst, mean))
        raw = open(os.path.join(d, name[:-4] + ".pcm"), "rb").read()    # (what wave.c's own unpacker made of it)
        want(list(struct.unpack("<%dh" % (len(raw) // 2), raw)) == got, "%s: wave.c unpacks it otherwise" % name)
    for b in bad:
        print("FAIL wave_cases check: " + b)
    print("wave_cases check: %d failures" % len(bad))
    return 1 if bad else 0


def main():
    if sys.argv[1] == "check":
        sys.exit(check(sys.argv[2]))
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    lines = []

    def case(name, blob, rate=0, ch=0, samples=None, err=None):
        open(os.path.join(out, name), "wb").write(blob)
        if err is not None:
            lines.append("%s\t%d" % (name, err))
            return
        open(os.path.join(out, name[:-4] + ".pcm"), "wb").write(struct.pack("<%dh" % len(samples), *samples))
        lines.append("%s\t%d\t%d\t%d" % (name, rate, ch, len(samples) // ch))

    s2, s1 = frames_of(4000, 2), frames_of(3001, 1)
    p16 = struct.pack("<%dh" % len(s2), *s2)
    case("in-pcm16.wav", riff(pcm_fmt(22050, 2, 16), p16), 22050, 2, s2)
    case("in-pcm8.wav", riff(pcm_fmt(11025, 1, 8), bytes((s >> 8) + 128 for s in s1)), 11025, 1, [(s >> 8) * 256 for s in s1])
    case("in-pcm24.wav", riff(pcm_fmt(48000, 2, 24), b"".join(struct.pack("<i", s * 256 + 77)[:3] for s in s2)), 48000, 2, s2)
    case("in-pcm32.wav", riff(pcm_fmt(8000, 1, 32), b"".join(struct.pack("<i", s * 65536 + 1234) for s in s1)), 8000, 1, s1)
    # other chunks before the samples (one of an odd size), and after them
    case("in-chunks.wav", riff(pcm_fmt(22050, 2, 16), p16, extra=chunk(b"LIST", b"INFOabc") + chunk(b"junk", bytes(101)))
         + chunk(b"tail", b"after the samples"), 22050, 2, s2)
    # "extensible": the kind is in the longer header
    ext = pcm_fmt(22050, 2, 16, 0xFFFE) + struct.pack("<HHI", 22, 16, 3) + struct.pack("<H", 1) + bytes(14)
    case("in-ext.wav", riff(ext, p16), 22050, 2, s2)
    # sizes that say more than there is (a recorder stopped hard); a file cut in the middle of its samples
    case("in-toolong.wav", riff(pcm_fmt(22050, 2, 16), p16, data_says=0xFFFFFFFF), 22050, 2, s2)
    whole = riff(pcm_fmt(22050, 2, 16), p16)
    case("in-cut.wav", whole[:44 + 4 * 1000 + 2], 22050, 2, s2[:2000])
    case("in-empty.wav", riff(pcm_fmt(22050, 2, 16), b""), 22050, 2, [])
    # IMA ADPCM packed here (by another packer than wave.c's), one and two channels, with and without "fact"
    for name, ch, rate, n, align, fact in (("in-adpcm1.wav", 1, 11025, 3001, 256, True), ("in-adpcm2.wav", 2, 22050, 4000, 1024, True),
                                           ("in-adpcm3.wav", 2, 44100, 2500, 2048, False)):
        src = frames_of(n, ch)
        data = ima_encode(src, ch, align)
        per = (align // ch - 4) * 2 + 1
        fmt = struct.pack("<HHIIHHHH", 0x11, ch, rate, rate * align // per, align, 4, 2, per)
        whole_n = len(data) // align * per               # (without "fact" the last block's filling is sound too)
        case(name, riff(fmt, data, fact=n if fact else None), rate, ch, ima_decode(data, ch, align, n if fact else whole_n))
    # not read: no WAV, no header, a-law, six channels, a rate of nothing, ADPCM of blocks that are none
    case("in-text.wav", b"This is no sound at all, but it is long enough to be looked at.\n" * 4, err=-2)
    case("in-riff.wav", b"RIFF" + struct.pack("<I", 100) + b"AVI " + bytes(100), err=-2)
    case("in-nofmt.wav", b"RIFF" + struct.pack("<I", 4 + 8 + len(p16)) + b"WAVE" + chunk(b"data", p16), err=-4)
    case("in-alaw.wav", riff(pcm_fmt(8000, 1, 8, 6), bytes(1000)), err=-3)
    case("in-six.wav", riff(pcm_fmt(48000, 6, 16), bytes(1200)), err=-3)
    case("in-rate0.wav", riff(pcm_fmt(0, 1, 16), bytes(1200)), err=-3)
    case("in-badblock.wav", riff(struct.pack("<HHIIHHHH", 0x11, 2, 22050, 11000, 7, 4, 2, 9), bytes(700)), err=-3)
    case("in-short.wav", b"RIFF\x04\0\0\0WAVE", err=-4)
    open(os.path.join(out, "MANIFEST"), "w", newline="\n").write("\n".join(lines) + "\n")
    print(out)


main()
