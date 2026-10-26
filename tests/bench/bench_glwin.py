#!/usr/bin/env python3
"""bench_glwin.py [--hang] [TEST...]: M8's windowed DOS-GL on the bench PC, as jobs.py's glwin case does it in 86Box.
DOS-GL's conformance programs (DOSGL_DIR's build/exe/conform, default ~/DOSGL) run full-screen with the desktop off,
then each in a window on the real desktop (GLOS_BENCH_DESK, default 800x600x16), and every frame each writes
(glReadPixels of its own buffer, C:\\OUT) must be the same bytes both ways. Under the desktop each is an app with
its turns at the engine (glos display's turns). --hang adds the forced GL hang: TEXCUBE --hang keeps the engine in
a frame until GLOS's watchdog ends it and resets the engine, and the first test draws right after. Files go to
C:\\GLWIN, removed after with the frames."""
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench_gui  # noqa: E402

ROOT = bench_gui.ROOT
DOSGL = os.environ.get("DOSGL_DIR", os.path.expanduser("~/DOSGL"))
DESK = os.environ.get("GLOS_BENCH_DESK", "800x600x16")


def main():
    args = sys.argv[1:]
    hang = "--hang" in args
    want = [a.upper() for a in args if not a.startswith("--")]
    agent = bench_gui.BenchAgent()
    out = os.path.join(ROOT, "out", "bench-g400-glwin")
    src = os.path.join(DOSGL, "build", "exe", "conform")
    tests = sorted(f for f in os.listdir(src) if re.match(r"T\d\d\.EXE$", f))
    if want:
        tests = [t for t in tests if t[:-4] in want]
    checks, notes = {"tests": bool(tests)}, []
    scp = ["scp", "-q", "-o", "BatchMode=yes", "-o", "HostKeyAlias=bench-g400", "-o", "IdentitiesOnly=yes",
           "-i", agent.key]

    def put(path, dst):
        return subprocess.run(scp + [path, "glos@%s:%s" % (bench_gui.HOST, dst)], capture_output=True).returncode == 0

    def frames(into):
        """C:\\OUT's frames into `into`, then deleted on the PC."""
        os.makedirs(into, exist_ok=True)
        for f in os.listdir(into):
            os.remove(os.path.join(into, f))
        names = [n.strip() for n in agent.ssh("dir /b C:\\OUT\\*.PPM")[1].splitlines() if n.strip().upper().endswith(".PPM")]
        for n in names:
            subprocess.run(scp + ["glos@%s:/C/OUT/%s" % (bench_gui.HOST, n), os.path.join(into, n.upper())],
                           capture_output=True)
        agent.ssh("del C:\\OUT\\*.PPM")
        return sorted(os.listdir(into))

    def run_all(label):
        bad = []
        for t in tests:
            rc, o, e = agent.ssh("C:\\GLWIN\\%s --noexit" % t, timeout=900)
            if rc != 0:
                bad.append("%s:%d" % (t[:-4], rc))
        if bad:
            notes.append("%s: %s" % (label, " ".join(bad)))
        return not bad

    follow = subprocess.Popen(agent.opts + ["glos@127.0.0.1", "glos log --follow"], stdout=subprocess.PIPE,
                              stderr=subprocess.DEVNULL)
    lines = []

    def reader():
        for raw in iter(follow.stdout.readline, b""):
            lines.append(raw.decode("latin-1").rstrip("\r\n"))
    import threading
    threading.Thread(target=reader, daemon=True).start()
    try:
        agent.ssh("md C:\\GLWIN")
        agent.ssh("md C:\\OUT")
        agent.ssh("del C:\\OUT\\*.PPM")
        checks["files-in"] = all(put(os.path.join(src, t), "/C/GLWIN/" + t) for t in tests)
        if hang:
            checks["files-in"] &= put(os.path.join(DOSGL, "build", "exe", "TEXCUBE.EXE"), "/C/GLWIN/TEXCUBE.EXE")
        # Full-screen: the desktop off.
        agent.ssh("glos display off")
        time.sleep(3)
        checks["full-ran"] = run_all("full-screen")
        full = frames(os.path.join(out, "full"))
        # Windowed.
        checks["on"] = agent.ssh("glos display on " + DESK)[0] == 0
        t0 = time.time()
        while time.time() - t0 < 60 and not agent.ssh("glos display")[1].startswith("display on mode="):
            time.sleep(0.5)
        time.sleep(3)
        mark = len(lines)
        checks["windowed-ran"] = run_all("windowed")
        disp = agent.ssh("glos display")[1]
        m = re.search(r"gpu owner=\S+ turns=(\d+)", disp)
        checks["turns"] = bool(m) and int(m.group(1)) > len(tests)
        notes.append("display: " + " ".join(disp.split())[:200])
        checks["apps"] = sum(1 for l in lines[mark:] if "GLOS-APP start id=" in l) >= len(tests)
        win = frames(os.path.join(out, "win"))
        differ = [f for f in full if f not in win or open(os.path.join(out, "full", f), "rb").read()
                  != open(os.path.join(out, "win", f), "rb").read()]
        checks["frames"] = bool(full) and not differ
        notes.append("frames: %d full-screen, %d windowed%s" % (len(full), len(win),
                                                                  ", differ: " + " ".join(differ) if differ else ""))
        if hang:
            mark = len(lines)
            h = subprocess.Popen(agent.opts + ["glos@127.0.0.1", "C:\\GLWIN\\TEXCUBE.EXE --frames 300 --hang --noexit"],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            t0 = time.time()
            while time.time() - t0 < 300 and not any("HX-TEXCUBE hang" in l for l in lines[mark:]):
                time.sleep(1)
            time.sleep(2)
            subprocess.run(agent.opts + ["glos@127.0.0.1", "glos shot"], capture_output=True, timeout=300)
            try:
                hrc = h.wait(timeout=300)
            except subprocess.TimeoutExpired:
                h.kill()
                hrc = None
            time.sleep(2)
            t = "\n".join(lines[mark:])
            checks["hang-ended"] = hrc == 255 and "GLOS-GPU watchdog" in t and "reason=gpu-watchdog" in t
            notes.append("hang: rc=%s" % hrc)
            again = agent.ssh("C:\\GLWIN\\%s --noexit" % tests[0], timeout=900)[0] == 0
            after = frames(os.path.join(out, "after"))
            checks["after-hang"] = again and bool(after) and all(
                open(os.path.join(out, "after", f), "rb").read() == open(os.path.join(out, "full", f), "rb").read()
                for f in after)
        time.sleep(2)
        checks["no-panic"] = not any(w in l for l in lines for w in ("GLOS-PANIC", "GLOS-WARN", "GLOS-CRASH"))
    finally:
        follow.terminate()
        os.makedirs(out, exist_ok=True)
        open(os.path.join(out, "glos.log"), "w").write("\n".join(lines) + "\n")
        agent.ssh("del C:\\OUT\\*.PPM")
        agent.ssh("C:\\WINDOWS\\COMMAND\\DELTREE.EXE /Y C:\\GLWIN")
    for n in notes:
        print("   ", n)
    bad = [k for k, v in checks.items() if not v]
    print("bench-glwin %s%s" % ("PASS" if not bad else "FAIL", " failed: " + " ".join(bad) if bad else ""))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
