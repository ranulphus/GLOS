#!/usr/bin/env python3
"""bench_pkg.py: M10's packages gate on the bench PC, as jobs.py's pkg case does it in 86Box: a source on this host
over HTTPS (the suite's test CA), signed with the suite's test key; NOTES installed (its files, its profile in
GLOS.CFG), upgraded to 1.1, removed (its files gone, GLOS.CFG byte for byte as before); a zip changed after signing
and an index changed after signing refused. PKG runs from C:\\PKGTEST, its own base (CERTS.DAT with the test CA,
KEYS\\PKG\\TEST.PUB, PKG\\SOURCES.TXT): C:\\GLOS's trust and key are untouched. C:\\PKGTEST and C:\\APPS (neither
there before) are removed after."""
import http.server
import os
import socket
import ssl
import subprocess
import sys
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bench_gui  # noqa: E402

jobs, ROOT = bench_gui.jobs, bench_gui.ROOT
LAN = bench_gui.lan_address()


class LanRepo:
    """jobs.RepoServer, on this host's LAN address."""
    def __init__(self, pki, root):
        server = self
        self.root = root

        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *a):
                pass

            def do_GET(self):
                f = os.path.join(server.root, os.path.basename(self.path))
                data = open(f, "rb").read() if os.path.isfile(f) else None
                self.send_response(200 if data is not None else 404)
                self.send_header("Content-Length", str(len(data) if data is not None else 0))
                self.end_headers()
                if data is not None:
                    self.wfile.write(data)
                self.close_connection = True

        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.minimum_version = ssl.TLSVersion.TLSv1_2
        ctx.load_cert_chain(os.path.join(pki, "good.pem"), os.path.join(pki, "good.key"))
        self.httpd = http.server.ThreadingHTTPServer((LAN, 0), Handler)
        self.httpd.socket = ctx.wrap_socket(self.httpd.socket, server_side=True)
        self.port = self.httpd.server_address[1]
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()


def main():
    agent = bench_gui.BenchAgent()
    out = os.path.join(ROOT, "out", "bench-g400-pkg")
    pki = os.path.join(out, "pki")
    os.makedirs(pki, exist_ok=True)
    jobs.test_pki(pki)
    certs = os.path.join(out, "CERTS.DAT")
    subprocess.run([os.path.join(ROOT, "build", "host", "mkcerts"), certs,
                    os.path.expanduser("~/.cache/mga-glide/dl/cacert-2026-09-25.pem"), os.path.join(pki, "ca.pem")],
                   check=True, stdout=subprocess.DEVNULL)
    pub, repos = jobs.pkg_repos(out)
    srv = LanRepo(pki, repos["one"])
    open(os.path.join(out, "SOURCES.TXT"), "w").write("glos https://test.glos:%d/\r\n" % srv.port)
    checks, notes = {}, []

    def put(src, dst):
        return subprocess.run(["scp", "-q", "-o", "BatchMode=yes", "-o", "HostKeyAlias=bench-g400", "-o", "IdentitiesOnly=yes",
                               "-i", agent.key, src, "glos@%s:%s" % (bench_gui.HOST, dst)], capture_output=True).returncode == 0

    def fetch(src):
        dst = os.path.join(out, "got")
        if os.path.exists(dst):
            os.remove(dst)
        r = subprocess.run(["scp", "-q", "-o", "BatchMode=yes", "-o", "HostKeyAlias=bench-g400", "-o", "IdentitiesOnly=yes",
                            "-i", agent.key, "glos@%s:%s" % (bench_gui.HOST, src), dst], capture_output=True)
        return open(dst, "rb").read() if r.returncode == 0 else None

    def exists(path):
        return "THERE" in agent.ssh("IF EXIST %s ECHO THERE" % path)[1]

    def pkg(args):
        rc, o, e = agent.ssh("C:\\PKGTEST\\PKG.EXE --resolve test.glos:%s %s" % (LAN, args), timeout=600)
        o = (o + e).replace("\r", "")
        open(os.path.join(out, "pkg.log"), "a").write("> PKG %s (rc %d)\n%s\n" % (args, rc, o))
        return rc, o

    try:
        for d in ("C:\\PKGTEST", "C:\\PKGTEST\\KEYS", "C:\\PKGTEST\\KEYS\\PKG", "C:\\PKGTEST\\PKG"):
            agent.ssh("md " + d)
        checks["files-in"] = put(os.path.join(ROOT, "build", "apps", "PKG.EXE"), "/C/PKGTEST/PKG.EXE") and \
            put(certs, "/C/PKGTEST/CERTS.DAT") and put(pub, "/C/PKGTEST/KEYS/PKG/TEST.PUB") and \
            put(os.path.join(out, "SOURCES.TXT"), "/C/PKGTEST/PKG/SOURCES.TXT")
        before = fetch("/C/GLOS/GLOS.CFG")
        rc, o = pkg("SOURCES")
        checks["signed"] = "signed by glos-loopa-test" in o
        rc, o = pkg("LIST")
        checks["list"] = "NOTES" in o and "1.0" in o
        rc, o = pkg("INSTALL NOTES")
        checks["install"] = rc == 0 and fetch("/C/APPS/NOTES/README.TXT") == b"version one\r\n"
        cfg = fetch("/C/GLOS/GLOS.CFG") or b""
        checks["profile-in"] = b"[program NOTES.EXE]" in cfg and b"title = Notes" in cfg
        srv.root = repos["two"]
        rc, o = pkg("UPGRADE")
        checks["upgrade"] = rc == 0 and fetch("/C/APPS/NOTES/README.TXT") == b"version one point one\r\n" and \
            exists("C:\\APPS\\NOTES\\NEW.TXT")
        rc, o = pkg("REMOVE NOTES")
        checks["remove"] = rc == 0 and not exists("C:\\APPS\\NOTES\\NUL")
        import time
        after, t0 = None, time.time()
        while time.time() - t0 < 30 and after != before:
            time.sleep(1)
            after = fetch("/C/GLOS/GLOS.CFG")
        checks["cfg-as-before"] = before is not None and after == before
        srv.root = repos["tampered"]
        rc, o = pkg("INSTALL NOTES")
        checks["tampered"] = rc == 1 and "isn't the signed index's: refused" in o and not exists("C:\\APPS\\NOTES\\NUL")
        srv.root = repos["badsig"]
        rc, o = pkg("INSTALL NOTES")
        checks["bad-signature"] = rc == 1 and "isn't signed by a key" in o
    finally:
        srv.httpd.shutdown()
        agent.ssh("C:\\WINDOWS\\COMMAND\\DELTREE.EXE /Y C:\\PKGTEST")
        agent.ssh("C:\\WINDOWS\\COMMAND\\DELTREE.EXE /Y C:\\APPS")
    for n in notes:
        print("   ", n)
    bad = [k for k, v in checks.items() if not v]
    print("bench-pkg %s%s" % ("PASS" if not bad else "FAIL", " failed: " + " ".join(bad) if bad else ""))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
