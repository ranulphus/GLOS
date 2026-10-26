#!/usr/bin/env python3
"""bench_sshc.py: M10's SSH client checks on the bench PC (the terminal's ssh, vi and top over a pty, sftp), as
jobs.py's sshc case does them in 86Box, against Debian's OpenSSH in the glos-sshd:m10 container published on this
host's LAN address for the run only: key logins only (no password service on the LAN), its client key put in
C:\\GLOS\\SSH\\ID_ED25 (TERM's own) and removed after, with KNOWNHST.TXT and the test files."""
import hashlib
import os
import socket
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench_gui  # noqa: E402

jobs, ROOT = bench_gui.jobs, bench_gui.ROOT
LAN = bench_gui.lan_address()
NAME = "glos-sshd-bench"


def main():
    agent = bench_gui.BenchAgent()
    out = os.path.join(ROOT, "out", "bench-g400-sshc")
    keys = os.path.join(out, "sshd")
    os.makedirs(keys, exist_ok=True)
    for f in os.listdir(keys):
        os.remove(os.path.join(keys, f))
    for k, c in (("hostkey", "host"), ("client", "glos@bench")):
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-C", c, "-f", os.path.join(keys, k)], check=True)
    open(os.path.join(keys, "authorized_keys"), "w").write(open(os.path.join(keys, "client.pub")).read())
    open(os.path.join(keys, "sshd_config"), "w").write(
        "HostKey /keys/hostkey\nAuthorizedKeysFile /keys/authorized_keys\nStrictModes no\nPasswordAuthentication no\n"
        "KbdInteractiveAuthentication no\nUsePAM yes\nPermitRootLogin no\nSubsystem sftp /usr/lib/openssh/sftp-server\n")
    t = socket.socket()
    t.bind((LAN, 0))
    port = t.getsockname()[1]
    t.close()
    checks, notes, lines = {}, [], []

    def run(cmd):
        return subprocess.run(["docker", "exec", "-u", "glos", "-w", "/home/glos", NAME, "sh", "-c", cmd],
                              capture_output=True, text=True).stdout

    def put(src, dst):
        return subprocess.run(["scp", "-q", "-o", "BatchMode=yes", "-o", "HostKeyAlias=bench-g400", "-o", "IdentitiesOnly=yes",
                               "-i", agent.key, src, "glos@%s:%s" % (bench_gui.HOST, dst)], capture_output=True).returncode == 0

    def fetch(src, dst):
        r = subprocess.run(["scp", "-q", "-o", "BatchMode=yes", "-o", "HostKeyAlias=bench-g400", "-o", "IdentitiesOnly=yes",
                            "-i", agent.key, "glos@%s:%s" % (bench_gui.HOST, src), dst], capture_output=True)
        return open(dst, "rb").read() if r.returncode == 0 else None

    def reader(p):
        for raw in iter(p.stdout.readline, b""):
            lines.append(raw.decode("latin-1").rstrip("\r\n"))

    def until(text, limit=120, start=0):
        t0 = time.time()
        while time.time() - t0 < limit:
            for i in range(start, len(lines)):
                if lines[i].startswith(text):
                    return i + 1
            time.sleep(0.3)
        notes.append("waited %ds for %r" % (limit, text))
        return 0

    def typed(text):
        agent.ssh("glos type " + text)

    term = None
    try:
        subprocess.run(["docker", "build", "-q", "-t", "glos-sshd:m10", os.path.join(ROOT, "tests", "sshd")], check=True,
                       stdout=subprocess.DEVNULL)
        subprocess.run(["docker", "rm", "-f", NAME], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run(["docker", "run", "-d", "--rm", "--name", NAME, "-p", "%s:%d:22" % (LAN, port), "-v",
                        "%s:/keys:ro" % keys, "glos-sshd:m10"], check=True, stdout=subprocess.DEVNULL)
        time.sleep(3)
        agent.ssh("md C:\\GLOS\\SSH")
        mb = os.urandom(1 << 20)
        open(os.path.join(out, "MB.BIN"), "wb").write(mb)
        checks["files-in"] = put(os.path.join(keys, "client"), "/C/GLOS/SSH/ID_ED25") and \
            put(os.path.join(keys, "client.pub"), "/C/GLOS/SSH/ID_ED25.PUB") and put(os.path.join(out, "MB.BIN"), "/C/GLOS/MB.BIN")
        checks["on"] = agent.ssh("glos display on 640x480x16")[0] == 0
        time.sleep(5)
        term = subprocess.Popen(agent.opts + ["glos@127.0.0.1", "C:\\GLOS\\TERM.EXE -log"], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        threading.Thread(target=reader, args=(term,), daemon=True).start()
        time.sleep(8)
        typed("ssh -p %d glos@%s echo hello-over-ssh\\n" % (port, LAN))
        k = until("SSH| ask-host")
        checks["ask-host"] = bool(k)
        typed("yes\\n")
        k = until("SSH| end", start=k)
        checks["command"] = bool(k) and lines[k - 1] == "SSH| end code=0"
        typed("ssh -p %d glos@%s exit 7 || echo status %%errorlevel%%\\n" % (port, LAN))
        k = until("SSH| end", start=k)
        checks["status"] = bool(k) and lines[k - 1] == "SSH| end code=7"
        typed("ssh -p %d glos@%s\\n" % (port, LAN))
        k = until("SSH| logged-in", start=k)
        time.sleep(4)
        typed("vi notes.txt\\n")
        time.sleep(4)
        typed("ihello from GLOS vi")
        agent.ssh("glos key " + jobs.gui_key_list(["esc"]))
        time.sleep(1)
        typed(":wq\\n")
        time.sleep(3)
        typed("top\\n")
        k3 = until("VT| top - ", 60, start=k)
        checks["top"] = bool(k3) and any(l.startswith("VT| Tasks:") for l in lines[k3:k3 + 6])
        typed("q")
        time.sleep(2)
        typed("exit\\n")
        k = until("SSH| end", start=k)
        checks["session"] = bool(k) and lines[k - 1] == "SSH| end code=0"
        got = run("cat notes.txt")
        checks["vi"] = got == "hello from GLOS vi\n"
        if not checks["vi"]:
            notes.append("notes.txt: %r" % got)
        typed("sftp -P %d glos@%s\\n" % (port, LAN))
        k = until("SSH| sftp-ready", start=k)
        t0 = time.time()
        typed("put C:\\\\GLOS\\\\MB.BIN mb.bin\\n")
        k = until("SSH| sftp-ready", 300, start=k)
        up = time.time() - t0
        t0 = time.time()
        typed("get mb.bin C:\\\\GLOS\\\\MB2.BIN\\n")
        k = until("SSH| sftp-ready", 300, start=k)
        down = time.time() - t0
        notes.append("sftp: 1 MB up %.1fs, down %.1fs (the real PII-300 and its PRO/100)" % (up, down))
        typed("exit\\n")
        k = until("SSH| end", start=k)
        checks["sftp-up"] = run("sha256sum mb.bin").split()[:1] == [hashlib.sha256(mb).hexdigest()]
        checks["sftp-down"] = fetch("/C/GLOS/MB2.BIN", os.path.join(out, "MB2.BIN")) == mb
        typed("exit\\n")
        term.wait(timeout=120)
        time.sleep(1)
        for want in ("TERM| hello-over-ssh", "TERM| status 7"):
            if want not in lines:
                checks["log"] = False
                notes.append("no line %r" % want)
        checks.setdefault("log", True)
    finally:
        if term and term.poll() is None:
            term.terminate()
        subprocess.run(["docker", "rm", "-f", NAME], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        agent.ssh("del C:\\GLOS\\MB.BIN")
        agent.ssh("del C:\\GLOS\\MB2.BIN")
        agent.ssh("C:\\WINDOWS\\COMMAND\\DELTREE.EXE /Y C:\\GLOS\\SSH")
        open(os.path.join(out, "term.out"), "w").write("\n".join(lines) + "\n")
    for n in notes:
        print("   ", n)
    bad = [k for k, v in checks.items() if not v]
    print("bench-sshc %s%s" % ("PASS" if not bad else "FAIL", " failed: " + " ".join(bad) if bad else ""))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
