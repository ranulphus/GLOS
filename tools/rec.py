#!/usr/bin/env python3
"""glos rec's stream into files (M10 step F, PRD D60): NNNNN.png for each frame and index.txt, which is the time
of each in ffmpeg's concat format, so that

  ssh -p PORT glos@HOST glos rec --fps 10 --seconds 60 | tools/rec.py OUT
  ffmpeg -f concat -i OUT/index.txt -vsync vfr OUT.mp4

makes a video. glos rec writes "FRAME n ms bytes", the PNG (glos shot's bytes for that moment), and "END frames"."""
import os
import sys


def main(argv):
    if len(argv) != 1:
        print(__doc__)
        return 2
    out = argv[0]
    os.makedirs(out, exist_ok=True)
    src = sys.stdin.buffer
    frames = []                                  # (file, ms)
    while True:
        line = src.readline()
        if not line:
            break
        words = line.decode("latin-1").split()
        if words[:1] == ["END"]:
            break
        if words[:1] != ["FRAME"] or len(words) != 4:
            print("rec: not glos rec's: %r" % line[:60], file=sys.stderr)
            return 1
        n, ms, size = int(words[1]), int(words[2]), int(words[3])
        data = src.read(size)
        if len(data) != size:
            print("rec: frame %d cut short" % n, file=sys.stderr)
            break
        name = "%05d.png" % n
        open(os.path.join(out, name), "wb").write(data)
        frames.append((name, ms))
    with open(os.path.join(out, "index.txt"), "w") as f:
        for i, (name, ms) in enumerate(frames):
            f.write("file '%s'\n" % name)
            f.write("duration %.3f\n" % ((frames[i + 1][1] - ms) / 1000.0 if i + 1 < len(frames) else 0.1))
        if frames:
            f.write("file '%s'\n" % frames[-1][0])  # (the concat format's last frame, given its duration)
    print("rec: %d frames in %s" % (len(frames), out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
