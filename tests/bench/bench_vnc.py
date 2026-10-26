#!/usr/bin/env python3
"""bench_vnc.py: M10's VNC and capture checks on the bench PC's real desktop, as jobs.py's vnc case does them in
86Box. A stock client (vncdotool, the glos-vnc:m10 container) through ssh -L: its screen equal to glos shot's
(a second viewer held on, so that the task bar's VNC mark is in both), text typed into Notepad and saved, read
back, Notepad's close box pressed, the screen equal again; glos rec meanwhile, every shot one of its frames."""
import os
import socket
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench_gui  # noqa: E402  (ROOT, jobs, BenchAgent)

jobs, ROOT = bench_gui.jobs, bench_gui.ROOT
BITS = int(os.environ.get("GLOS_BENCH_BITS", "16"))


def main():
    agent = bench_gui.BenchAgent()
    out = os.path.join(ROOT, "out", "bench-g400-vnc")
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        if f.endswith(".png"):
            os.remove(os.path.join(out, f))
    checks, notes, procs = {}, [], []
    g = jobs.GuiDriver(agent, out, BITS, False, "C:\\GLOS\\")
    hold = "glos-vnc-hold-bench"

    def vncdo(*args, timeout=180):
        r = subprocess.run(["docker", "run", "--rm", "--network", "host", "-v", out + ":/out", "glos-vnc:m10",
                            "timeout", str(timeout), "vncdo", "-s", "127.0.0.1::%d" % port] + list(args),
                           capture_output=True, text=True, timeout=timeout + 60)
        if r.returncode:
            notes.append("vncdo %s: %s" % (" ".join(args), (r.stdout + r.stderr)[-200:]))
        return r.returncode == 0

    def shot():
        for _ in range(20):
            data = subprocess.run(agent.opts + ["glos@127.0.0.1", "glos shot"], capture_output=True, timeout=180).stdout
            if data[:8] == b"\x89PNG\r\n\x1a\n":
                return data
            time.sleep(0.5)
        return b""

    def same(name):
        from PIL import Image
        for _ in range(3):
            before = shot()
            if not vncdo("capture", "/out/%s.png" % name):
                return False
            data = shot()
            if data == before:
                break
        open(os.path.join(out, "%s-shot.png" % name), "wb").write(data)
        a = jobs.png_rgb(data)
        v = Image.open(os.path.join(out, "%s.png" % name)).convert("RGB")
        if not a or (a[0], a[1]) != v.size:
            notes.append("%s: sizes %s %s" % (name, a and (a[0], a[1]), v.size))
            return False
        vb = v.tobytes()
        diff = sum(1 for i in range(0, len(a[2]), 3) if a[2][i:i + 3] != vb[i:i + 3])
        if diff:
            notes.append("%s: %d pixels differ" % (name, diff))
        return diff == 0

    rec = None
    try:
        checks["vnc-on"] = agent.ssh("glos config set vnc enable on")[0] == 0
        checks["display"] = agent.ssh("glos display on 640x480x%d" % BITS)[0] == 0
        time.sleep(5)
        t = socket.socket()
        t.bind(("127.0.0.1", 0))
        port = t.getsockname()[1]
        t.close()
        tunnel = subprocess.Popen(agent.opts + ["-N", "-L", "%d:127.0.0.1:5900" % port, "glos@127.0.0.1"])
        procs.append(tunnel)
        t0 = time.time()
        while time.time() - t0 < 30:
            try:
                socket.create_connection(("127.0.0.1", port), timeout=2).close()
                break
            except OSError:
                time.sleep(0.5)
        subprocess.run(["docker", "rm", "-f", hold], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run(["docker", "run", "-d", "--rm", "--name", hold, "--network", "host", "glos-vnc:m10", "vncdo",
                        "-s", "127.0.0.1::%d" % port, "pause", "1200"], check=True, stdout=subprocess.DEVNULL)
        t0 = time.time()
        while time.time() - t0 < 60 and "GLOS-VNC init" not in agent.ssh("glos log")[1]:
            time.sleep(1)
        time.sleep(3)
        recout = os.path.join(out, "rec")
        subprocess.run(["rm", "-rf", recout])
        rec = subprocess.Popen("%s | %s %s %s" % (" ".join(agent.opts + ["glos@127.0.0.1", "'glos rec --fps 5 --seconds 120'"]),
                                                  sys.executable, os.path.join(ROOT, "tools", "rec.py"), recout),
                               shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        time.sleep(3)
        checks["desk-same"] = same("desk")
        agent.ssh("del C:\\GLOS\\VNC.TXT")
        procs.append(subprocess.Popen(agent.opts + ["glos@127.0.0.1", "C:\\GLOS\\NOTEPAD.EXE C:\\GLOS\\VNC.TXT"],
                                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT))
        g.until(lambda d: any("Notepad" in w["title"] and w["shown"] for w in d["win"]), 90)
        time.sleep(3)
        checks["notepad-same"] = same("notepad")
        vncdo("type", "hello from vnc")
        vncdo("key", "ctrl-s")
        got, t0 = None, time.time()
        while time.time() - t0 < 60 and got != b"hello from vnc":
            time.sleep(2)
            r = subprocess.run(["scp", "-o", "BatchMode=yes", "-o", "HostKeyAlias=bench-g400", "-o", "IdentitiesOnly=yes",
                                "-i", agent.key, "glos@%s:/C/GLOS/VNC.TXT" % bench_gui.HOST, os.path.join(out, "VNC.TXT")],
                               capture_output=True, timeout=120)
            got = open(os.path.join(out, "VNC.TXT"), "rb").read() if r.returncode == 0 else None
        checks["keys"] = got == b"hello from vnc"
        if not checks["keys"]:
            notes.append("VNC.TXT: %r" % got)
        time.sleep(2)
        checks["typed-same"] = same("typed")
        w = next((w for w in g.desk()["win"] if "Notepad" in w["title"] and w["shown"]), None)
        if w:
            vncdo("move", str(w["x"] + 9), str(w["y"] - 10), "click", "1")
        checks["button"] = bool(w) and bool(g.until(lambda d: not any("Notepad" in x["title"] and x["shown"]
                                                                       for x in d["win"]), 60))
        time.sleep(3)
        checks["after-same"] = same("after")
        o, _ = rec.communicate(timeout=300)
        frames = {}
        for f in sorted(os.listdir(recout)) if os.path.isdir(recout) else []:
            if f.endswith(".png"):
                frames[open(os.path.join(recout, f), "rb").read()] = f
        shots = [open(os.path.join(out, n + "-shot.png"), "rb").read() for n in ("desk", "notepad", "typed", "after")]
        found = [frames.get(x) for x in shots]
        checks["rec-frames"] = len(frames) >= 3 and all(found)
        notes.append("rec: %d frames; the shots were %s" % (len(frames), found))
    finally:
        subprocess.run(["docker", "rm", "-f", hold], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for p in procs:
            p.terminate()
        agent.ssh("glos config unset vnc enable")
        agent.ssh("del C:\\GLOS\\VNC.TXT")
    for n in notes:
        print("   ", n)
    bad = [k for k, v in checks.items() if not v]
    print("bench-vnc-%d %s%s" % (BITS, "PASS" if not bad else "FAIL", " failed: " + " ".join(bad) if bad else ""))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
