#!/usr/bin/env python3
"""bench_gui.py SCRIPT... : GLOS's GUI test scripts (tests/gui) played on a bench PC's real desktop through
jobs.py's own GuiDriver, each shot compared byte for byte with the host's golden (make gui-goldens), as the gui
suite does in 86Box. The desktop is put at 640x480xBITS first if it isn't; BITS from GLOS_BENCH_BITS (16).

The bench PC (tests/bench/README.md): GLOS in agent mode at GLOS_BENCH_HOST (192.168.1.33), its host key in
known_hosts under the name bench-g400, logins by GLOS_BENCH_KEY (~/.ssh/glos_bench)."""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tests", "loopa"))
os.chdir(ROOT)
import jobs  # noqa: E402

HOST = os.environ.get("GLOS_BENCH_HOST", "192.168.1.33")
KEY = os.path.expanduser(os.environ.get("GLOS_BENCH_KEY", "~/.ssh/glos_bench"))


def lan_address():
    """This host's address on the bench PC's network (for servers the PC reaches): GLOS_BENCH_HOST_LAN, or the
    source address of a route to the PC."""
    if os.environ.get("GLOS_BENCH_HOST_LAN"):
        return os.environ["GLOS_BENCH_HOST_LAN"]
    import socket
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.connect((HOST, 22))
    return s.getsockname()[0]


class BenchAgent:
    """What GuiDriver needs of jobs.Agent, for a real PC: ssh options (the destination glos@127.0.0.1 that
    GuiDriver adds is only a name: HostName says where), ssh(), and port/key for scp."""
    def __init__(self):
        self.key, self.port = KEY, 22
        self.opts = ["ssh", "-o", "BatchMode=yes", "-o", "HostName=" + HOST, "-o", "HostKeyAlias=bench-g400",
                     "-o", "IdentitiesOnly=yes", "-i", KEY, "-o", "StrictHostKeyChecking=yes", "-o", "LogLevel=ERROR"]

    def ssh(self, cmd, timeout=300):
        p = subprocess.run(self.opts + ["glos@127.0.0.1", cmd], capture_output=True, timeout=timeout)
        return p.returncode, p.stdout.decode("latin-1"), p.stderr.decode("latin-1")


def main():
    bits = int(os.environ.get("GLOS_BENCH_BITS", "16"))
    agent = BenchAgent()
    if (" 640x480x%d " % bits) not in agent.ssh("glos display")[1]:  # (the goldens' depth: the desktop's)
        import time
        agent.ssh("glos display on 640x480x%d" % bits)
        time.sleep(5)
    out = os.path.join(ROOT, "out", "bench-g400-gui")
    os.makedirs(out, exist_ok=True)
    bad = 0
    for script in sys.argv[1:]:
        g = jobs.GuiDriver(agent, out, bits, False, "C:\\GLOS\\")
        name = os.path.splitext(os.path.basename(script))[0]
        g.play(os.path.join("tests", "gui", os.path.basename(script)))
        text = g.finish()
        open(os.path.join(out, name + ".out"), "w").write(text)
        print("%-12s %s %s" % (name, "PASS" if not g.bad else "FAIL " + " ".join(g.bad), "; ".join(g.notes)[:400]))
        bad += bool(g.bad)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
