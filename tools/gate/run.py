#!/usr/bin/env python3
"""GLOS M4e's gate (docs/milestones-m0-m4.md, "The gate's baseline matrix"):
every cell of the matrix against its baseline, under GLOS, and with direct
mode forced (IOPL 3, supervisor.md §9.7); every program also killed with the
hotkey, and probed over ssh while it runs.

  run.py [--cell C ...] [--skip C ...] [-j N] [--fresh] [--no-direct]
  run.py list

Three kinds of cell (CELLS below):
  jobs     a tests/loopa/jobs.py suite, which runs its baselines and GLOS
           itself and judges each run; then again with GLOS_SET=/DIRECT
  program  a program in Loop A (a tools/survey/survey.py suite), with no host
           of its own or with its own (CWSDPMI, DOS/4GW): the baseline, then
           GLOS as the shell (the glosshell boots) and the same with direct
           mode forced, each compared with the baseline after
           normalize.toml (status, HX-TEST/HX-IMG/DGL-/MGL- lines as
           multisets, end markers, state hashes, dumped frames pixel for
           pixel); GLOS's own lines judged (GLOS-PANIC and the like fail, a
           DPMI-UNIMPL call must be listed, vif-stuck is reported; every
           session must end with its IRQ vectors and mask bits as it found
           them, irq=ok); then
             kill   the hotkey kill_at seconds after the program's DPMI start: GLOS-KILL
                    reason=hotkey, and text mode and the IRQ set-up back
             probe  GLOS with an RTL8029 (--net ne2kpci): `glos tick` over
                    ssh about once a second while the program runs must
                    always answer, never more than 2 s of guest time late
                    (the replies carry the kernel's tick: 86Box runs below
                    real time), one `glos shot` must answer, and the
                    program must still end as in the baseline
  script   a runner that judges itself (tools/m4c-games.sh: MGA-Glide's
           conform and replays, DOSBench; DOS-GL's loopa-sdl): as the shell,
           and with direct mode forced (loopa-sdl's baseline too)
Baselines are cached in ~/.cache/mga-glide/glos/gate/ by the program's
files, its repository's state, the boot, the harness and the 86Box build;
--fresh runs them again. `make glos-cache` must have put this build where
the runs take it from (make gate does; GLOS_BIN= another such directory,
inside the cache). Results: out/gate/, with
out/gate/summary.txt; exit status 1 if any part failed. Runs on the host:
the probes use OpenSSH."""
import argparse
import collections
import concurrent.futures
import hashlib
import json
import os
import re
import shlex
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

try:
    import tomllib
except ImportError:                             # Python < 3.11
    tomllib = None

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools", "survey"))
import survey  # noqa: E402

MGA = survey.MGA
DOSGL = survey.DOSGL
CACHE = survey.CACHE
BIN = os.environ.get("GLOS_BIN", os.path.join(CACHE, "glos", "bin"))      # make glos-cache's (GLOS_CACHE=)
GATE_CACHE = os.path.join(CACHE, "glos", "gate")
OUT = os.path.join(ROOT, "out", "gate")

# ---- the matrix

# jobs: tests/loopa/jobs.py SUITE ARGS, and whether to run it again with direct mode forced.
JOBS = [
    ("dpmiconf32", ["dpmi"], True,
     "DPMICONF-32 against CWSDPMI r7 and HDPMI32i, DPMIMINI, MGA-Glide's HELLOs; bf6/dx2/dx4, raw/HIMEMX"),
    ("ecm", ["ecm", "--boot", "default"], True, "ecm's dpmitest under lDebugX; bf6/dx2/dx4, raw"),
    ("hdpmireg", ["hdpmireg", "--profile", "bf6", "--profile", "486dx2", "--boot", "default"], True,
     "HDPMI's regression tests against HDPMI32i; bf6/dx2, raw"),
    ("djtst", ["djtst"], True, "djtst205 against CWSDPMI; bf6/dx2/dx4, raw/HIMEMX"),
    ("dpmiconf16", ["dpmi16", "--profile", "bf6", "--profile", "486dx2", "--boot", "default"], True,
     "DPMICONF-16 against HDPMI16 and HDPMI16i; bf6/dx2, raw"),
    ("tpx", ["tpx", "--profile", "bf6", "--profile", "486dx2"], True,
     "Turbo Pascal 7's TPX on Borland's RTM; bf6/dx2, raw/HIMEMX"),
    ("hxtools", ["m2"], True, "the HX tools without and with GLOS; all profiles and boots"),
    ("dpmitools", ["dpmitools"], True, "MGA-Glide's DJGPP tools: STACKPG, MOUSETST, JOYTEST, SBBEEP; bf6, raw/HIMEMX"),
    # At IOPL 3 a CLI loop can't be killed: direct mode gives that up (supervisor.md §9.7).
    ("hostile", ["hostile"], False, "the hostile programs, each killed; all profiles and boots"),
    ("sess", ["sess"], False, "sessions, profiles, direct mode (its own cases), glos run, kills"),
]

# program: a survey suite (its jobs), the boots, when to kill it (seconds of wall time after it enters protected
# mode; 86Box runs at about 0.6 of real time on Nulphlix with a G450).
PROGRAMS = [
    ("gta", "gta", ["default"], 40, "GTA (3dfx, DOS/4GW) through its menus into the game; bf6 + G450"),
    ("screamer-rally", "screamer-rally", ["default"], 40, "Screamer Rally (DOS/4GW 1.97) attract mode; bf6 + G450"),
    ("dosgl-conform", "dosgl-conform", ["default", "himemx"], 2, "DOS-GL's conformance tests (CWSDPMI); G450"),
    ("dosgl-texcube", "dosgl-texcube", ["default", "himemx"], 3, "DOS-GL's TEXCUBE, 150 frames; G450"),
    ("classicube", "classicube", ["default", "himemx"], 20, "ClassiCube, 300 frames; G450"),
    ("quake", "quake", ["default", "himemx"], 20, "GLQuake's timedemo; G450"),
    ("quake2", "quake2", ["default", "himemx"], 20, "Quake 2 on map base1, 600 frames; G450"),
    ("halflife", "halflife", ["default", "himemx"], 20, "Half-Life's timedemo; G450"),
    ("prboom", "prboom", ["default", "himemx"], 10, "PrBoom-plus's timedemo; G450"),
    ("fifthwheel", "fifthwheel", ["default", "himemx"], 20, "Fifth Wheel's fixed autopilot lap; G450"),
]

# script: a runner that judges itself: (cwd, command), its base run too (or GLOS only), extra environment.
SCRIPTS = [
    ("m4c", ROOT, ["tools/m4c-games.sh", "conform", "replay", "dosbench"], False,
     "MGA-Glide's conform 27x4 and replays, DOSBench, under GLOS as the shell (tools/m4c-games.sh)"),
    ("sdl", DOSGL, ["tools/sdl/loopa.sh", "g450"], True, "DOS-GL's loopa-sdl checks; G450"),
]

CELLS = [c[0] for c in JOBS] + [p[0] for p in PROGRAMS] + [s[0] for s in SCRIPTS]

# Quake 2 isn't one of the survey's suites: its map run, as DOS-GL's runner does it.
survey.SUITES.setdefault("quake2", dict(kind="script", cwd=DOSGL, cmd=["tools/quake/run.sh", "quake2", "g450"]))

TSRS = {"CTMOUSE.EXE", "CTMOUSE.COM"}           # sessions that go resident change vectors on purpose

# ---- normalisation

def load_norm():
    path = os.path.join(HERE, "normalize.toml")
    if tomllib:
        return tomllib.load(open(path, "rb"))
    norm, section = {}, None                    # (a small reader for this file's own subset of TOML)
    for line in open(path):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("["):
            section = norm.setdefault(line.strip("[]"), {})
            continue
        k, _, v = line.partition("=")
        section[k.strip()] = v.strip()
    raise SystemExit("normalize.toml needs Python 3.11's tomllib")


NORM = load_norm()
STRIP = [re.compile(p) for p in NORM["lines"]["strip"]]
DROP = [re.compile(p) for p in NORM["lines"]["drop"]]
ENDS = re.compile(r"^(HX-DONE \S+|DGL-EXIT frames=\d+|MGL-EXIT frames=\d+|HX-GAME-EXIT|MGL-EXC|DGL-FAULT|"
                  r"DGL-GLERR \S+|DGL-TEXOOM|HX-SDLCRASH)", re.M)


def read(path):
    return open(path, "rb").read().decode("latin-1").replace("\r", "") if os.path.exists(path) else ""


def norm_lines(serial):
    out = collections.Counter()
    for l in serial.split("\n"):
        if not l.startswith(tuple(NORM["lines"]["prefixes"])) or any(d.search(l) for d in DROP):
            continue
        for s in STRIP:
            l = s.sub("", l)
        out[l.strip()] += 1
    return out


def frames(d):
    return sorted(f for f in os.listdir(d) if f.lower().endswith((".png", ".ppm"))) if os.path.isdir(d) else []


def compare(base, other):
    """What differs between two runs' directories: [] when nothing."""
    sb, so = read(os.path.join(base, "serial.log")), read(os.path.join(other, "serial.log"))
    st = lambda d: read(os.path.join(d, "status")).strip() or "?"     # noqa: E731
    diffs = []
    if st(base) != st(other):
        diffs.append("status %s (baseline %s)" % (st(other), st(base)))
    lb, lo = norm_lines(sb), norm_lines(so)
    if lb != lo:
        lost, extra = list((lb - lo).elements())[:3], list((lo - lb).elements())[:3]
        diffs.append("lines: %s" % "; ".join(["-" + l for l in lost] + ["+" + l for l in extra]))
    eb, eo = sorted(set(ENDS.findall(sb))), sorted(set(ENDS.findall(so)))
    if eb != eo:
        diffs.append("ends %s (baseline %s)" % (" ".join(eo) or "none", " ".join(eb) or "none"))
    hb, ho = [l for l in sb.split("\n") if l.startswith("FW-HASH")], [l for l in so.split("\n") if l.startswith("FW-HASH")]
    n = min(len(hb), len(ho))
    if hb[:n] != ho[:n]:
        diffs.append("state hash from %s" % next(hb[i] for i in range(n) if hb[i] != ho[i]).split()[1])
    fb = frames(base)
    if fb:
        p = subprocess.run([sys.executable, os.path.join(MGA, "tools/loopa/samepix.py"), base, other],
                           capture_output=True, text=True)
        if p.returncode:
            diffs.append(p.stdout.strip().splitlines()[-1] if p.stdout.strip() else "frames differ")
    return diffs


def glos_trouble(serial):
    """GLOS's own account of a run: (failures, reports)."""
    bad = [f for f in NORM["glos"]["fail"] if f in serial]
    unimpl = set(re.findall(r"GLOS-DPMI-UNIMPL \S+ ax=(\w+)", serial)) - set(NORM["glos"]["unimpl_ok"])
    if unimpl:
        bad.append("DPMI-UNIMPL " + ",".join(sorted(unimpl)))
    for m in re.finditer(r"GLOS-SESSION end n=\d+ prog=(\S+) .*? irq=(\S+)", serial):
        if m.group(2) != "ok" and m.group(1) not in TSRS:
            bad.append("%s left irq=%s" % (m.group(1), m.group(2)))
    if "GLOS-SESSION begin" not in serial:
        bad.append("no session")
    reports = ["%s x%d" % (r, serial.count(r)) for r in NORM["glos"]["report"] if r in serial]
    return bad, reports

# ---- running

def box86_build():
    d = os.environ.get("BOX86_DIR", os.path.join(CACHE, "86box", "bin"))
    p = os.path.join(d, "86Box")
    return os.path.basename(os.path.realpath(p)) if os.path.exists(p) else "?"


def repo_state(path):
    head = subprocess.run(["git", "-C", path, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    diff = subprocess.run(["git", "-C", path, "diff", "HEAD"], capture_output=True).stdout
    return head + ":" + hashlib.sha256(diff).hexdigest()[:16]


def file_shas(spec):
    """The sha256 of the files a run.py job names (--exe, --file, --ovl), as they are now."""
    out = {}
    args = spec.get("args", [])
    for i, a in enumerate(args):
        v = None
        if a in ("--exe", "--file", "--ovl") and i + 1 < len(args):
            v = args[i + 1]
        if v:
            src = os.path.join(spec["cwd"], v.split("=")[0])
            if os.path.isfile(src):
                out[v] = hashlib.sha256(open(src, "rb").read()).hexdigest()
    return out


def base_key(job, spec, boot):
    k = {"job": job, "spec": {k: v for k, v in spec.items() if k != "multi"}, "files": file_shas(spec),
         "repo": repo_state(spec["cwd"]), "harness": repo_state(MGA), "boot": boot, "86box": box86_build()}
    return hashlib.sha256(json.dumps(k, sort_keys=True).encode()).hexdigest()[:24]


def glos_args(boot, direct=False):
    a = ["--boot-cfg", "glosshell" + ("-himemx" if boot == "himemx" else ""),
         "--file", BIN + "/GLOS.EXE=/TEST/GLOS.EXE", "--file", BIN + "/GLOSK.BIN=/TEST/GLOSK.BIN"]
    if direct:
        a += ["--file", BIN + "/direct/GLOS.CFG=/TEST/GLOS.CFG"]
    return a


def kill_keys(t):
    """Ctrl-Alt-Shift-Esc t seconds after the program enters protected mode (every program here is a DPMI
    client)."""
    return ",".join(["@GLOS-DPMI start", "%g:0x1d:down" % t, "%g:0x38:down" % t, "%g:0x2a:down" % t,
                     "%g:0x01" % (t + .3), "%g:0x2a:up" % (t + .6), "%g:0x38:up" % (t + .6), "%g:0x1d:up" % (t + .6)])


def with_keys(spec, keys):
    """(spec, run.py arguments) typing keys too: added to the job's own --keys (run.py takes one), or
    --keys of their own."""
    a = spec.get("args", [])
    if spec["kind"] == "run" and "--keys" in a:
        i = a.index("--keys")
        return dict(spec, args=a[:i + 1] + [a[i + 1] + "," + keys] + a[i + 2:]), []
    return spec, ["--keys", keys]


def run_program(job, spec, args, dest, during=None):
    """One Loop A run of a survey job with run.py arguments added; its results in dest. during(out
    directory, status path) runs beside it (the probe) and its result is returned."""
    shutil.rmtree(dest, ignore_errors=True)
    os.makedirs(dest)
    env = dict(os.environ, MGA_DOCKER_NETWORK="host")
    env.pop("MGA_CARD", None)
    name = "gate-" + os.path.relpath(dest, OUT).replace(os.sep, "-")      # (unique: runs go in parallel)
    if spec["kind"] == "run":
        out = os.path.join(MGA, "out", "gate", name)
        cmd = [os.path.join(MGA, "tools/dev"), "python3", os.path.join(MGA, "tools/loopa/run.py"),
               "--name", name, "--out", out] + spec["args"] + args
    else:
        out = os.path.join(spec["cwd"], "out", name)
        env.update(MGAHAL_DIR=MGA, NAME=name, LOOPA_EXTRA_ARGS=" ".join(shlex.quote(a) for a in args))
        cmd = spec["cmd"]
    shutil.rmtree(out, ignore_errors=True)
    p = subprocess.Popen(cmd, cwd=spec["cwd"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    extra = during(out) if during else None
    p.wait()
    for fn in ("status", "serial.log", "result.json"):
        if os.path.exists(os.path.join(out, fn)):
            shutil.copyfile(os.path.join(out, fn), os.path.join(dest, fn))
    for sub in ("", "files"):                   # frames the program dumped (not run.py's screenshots)
        d = os.path.join(out, sub)
        for fn in os.listdir(d) if os.path.isdir(d) else []:
            if fn.lower().endswith((".png", ".ppm")) and not fn.startswith("screen-") and not fn.endswith(".diff.png"):
                shutil.copyfile(os.path.join(d, fn), os.path.join(dest, fn))
    if spec["kind"] == "script":
        shutil.rmtree(out, ignore_errors=True)  # not ours to leave in DOS-GL's out/
    return extra


def baseline(job, spec, boot, dest, fresh):
    key = base_key(job, spec, boot)
    cached = os.path.join(GATE_CACHE, key)
    if not fresh and os.path.exists(os.path.join(cached, "status")):
        shutil.rmtree(dest, ignore_errors=True)
        shutil.copytree(cached, dest)
        return "cached"
    run_program(job, spec, ["--boot-cfg", boot] if boot != "default" else [], dest)
    if read(os.path.join(dest, "status")).strip() == "PASS":
        shutil.rmtree(cached, ignore_errors=True)
        shutil.copytree(dest, cached)
    return "ran"

# ---- the probe

KEY = os.path.join(ROOT, "tests", "keys", "client")


class Probe:
    """`glos tick` over ssh about once a second (wall time) while a run goes, and one `glos shot`."""

    def __init__(self, port):
        self.port, self.replies, self.fails, self.shot = port, [], [], None
        self.tmp = tempfile.mkdtemp()
        self.key = os.path.join(self.tmp, "key")
        shutil.copyfile(KEY, self.key)
        os.chmod(self.key, 0o600)
        self.ssh = ["ssh", "-p", str(port), "-o", "IdentitiesOnly=yes", "-o", "BatchMode=yes",
                    "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null", "-o", "LogLevel=ERROR",
                    "-o", "ConnectTimeout=20", "-i", self.key, "glos@127.0.0.1"]

    def call(self, cmd, timeout=30):
        try:
            p = subprocess.run(self.ssh + [cmd], capture_output=True, timeout=timeout)
            return p.returncode, p.stdout, p.stderr
        except subprocess.TimeoutExpired:
            return -1, b"", b"timeout"

    def __call__(self, out):
        serial, status = os.path.join(out, "serial.log"), os.path.join(out, "status")
        t0 = time.time()
        while time.time() - t0 < 600 and not os.path.exists(status):
            text = read(serial)
            if "GLOS-SSH listen" in text and "GLOS-NET dhcp" in text:
                break
            time.sleep(1)
        end = re.compile(r"HX-GAME-EXIT|HX-EXIT|DGL-EXIT|MGL-EXIT|HX-DONE")
        while not os.path.exists(status) and not end.search(read(serial)):
            w = time.time()
            rc, o, e = self.call("glos tick")
            m = re.match(rb"ticks=(\d+)", o)
            if rc == 0 and m:
                self.replies.append((w, time.time(), int(m.group(1))))
                if self.shot is None and len(self.replies) == 5:
                    rc2, o2, e2 = self.call("glos shot", timeout=60)
                    self.shot = (rc2, o2[:8] == b"\x89PNG\r\n\x1a\n", e2.decode("latin-1").strip())
            elif not os.path.exists(status) and not end.search(read(serial)):     # (not the run ending)
                self.fails.append((w, rc, e.decode("latin-1").strip()[:80]))
            time.sleep(1)
        shutil.rmtree(self.tmp, ignore_errors=True)
        return self

    def judge(self):
        """(ok, what): every probe answered, none later than 2 s of guest time, and the shot answered."""
        if len(self.replies) < 3:
            return False, "%d replies" % len(self.replies)
        (w0, _, k0), (w1, _, k1) = self.replies[0], self.replies[-1]
        rate = (k1 - k0) / 1024.0 / max(w1 - w0, 1e-3)      # guest seconds per wall second
        worst = 0.0
        for (wa, ra, ka), (wb, rb, kb) in zip(self.replies, self.replies[1:]):
            worst = max(worst, (kb - ka) / 1024.0 - rate * max(wb - ra, 0))     # less the time between probes
        ok = not self.fails and worst <= 2.0 and self.shot is not None and \
            (self.shot[1] or "graphics modes come later" in self.shot[2])
        what = "replies=%d worst=%.2fs rate=%.2f fails=%d shot=%s" % (
            len(self.replies), worst, rate, len(self.fails),
            "png" if self.shot and self.shot[1] else (self.shot[2] if self.shot else "none"))
        return ok, what


def free_port():
    s = socket.socket()
    s.bind(("0.0.0.0", 0))
    port = s.getsockname()[1]
    s.close()
    return port

# ---- the cells

def jobs_cell(name, args, direct, jobs_n):
    res = []
    for forced in ([False, True] if direct else [False]):
        env = dict(os.environ)
        env.pop("GLOS_SET", None)
        if forced:
            env["GLOS_SET"] = "/DIRECT"
        p = subprocess.run([sys.executable, os.path.join(ROOT, "tests/loopa/jobs.py")] + args + ["-j", str(jobs_n)],
                           cwd=ROOT, env=env, capture_output=True, text=True)
        os.makedirs(os.path.join(OUT, name), exist_ok=True)
        open(os.path.join(OUT, name, "direct.txt" if forced else "glos.txt"), "w").write(p.stdout + p.stderr)
        lines = [l for l in p.stdout.splitlines() if re.match(r"\s+\S+\s+(PASS|FAIL)", l)]
        bad = [l.strip() for l in lines if " FAIL" in l]
        res.append(("%s%s" % (name, "/direct" if forced else ""), p.returncode == 0 and lines and not bad,
                    "%d runs%s" % (len(lines), "; " + " | ".join(bad[:3]) if bad else "")))
    return res


def program_job(cell, job, spec, boot, kill_at, fresh, direct):
    d = os.path.join(OUT, cell, job, boot)
    res = []
    how = baseline(job, spec, boot, os.path.join(d, "base"), fresh)
    base_st = read(os.path.join(d, "base", "status")).strip()
    res.append(("%s/%s/%s/base" % (cell, job, boot), base_st == "PASS", "%s (%s)" % (base_st, how)))
    for variant in ["glos"] + (["direct"] if direct else []):
        run_program(job, spec, glos_args(boot, variant == "direct"), os.path.join(d, variant))
        diffs = compare(os.path.join(d, "base"), os.path.join(d, variant))
        bad, reports = glos_trouble(read(os.path.join(d, variant, "serial.log")))
        res.append(("%s/%s/%s/%s" % (cell, job, boot, variant), not diffs and not bad,
                    "; ".join(diffs + bad + reports) or "same"))
    if boot != "default" or kill_at is None:   # (a kill and a probe for one job of a suite, on the raw boot)
        return res
    kspec, kargs = with_keys(spec, kill_keys(kill_at))
    run_program(job, kspec, glos_args(boot) + kargs, os.path.join(d, "kill"))
    s = read(os.path.join(d, "kill", "serial.log"))
    k = s.find("reason=hotkey")
    bad, reports = glos_trouble(s)
    ok = k >= 0 and "GLOS-KILL none" not in s and "HX-VMODE bios=03" in s[k:] and not bad
    res.append(("%s/%s/kill" % (cell, job), ok, "; ".join(
        (["no kill %gs into it (did it end first?)" % kill_at] if k < 0 else []) + bad + reports) or "killed, clean"))
    port = free_port()
    probe = run_program(job, spec, glos_args(boot) + [
        "--net", "ne2kpci", "--net-fwd", "%d:22" % port, "--file", BIN + "/keys/HOSTKEY=/TEST/KEYS/HOSTKEY",
        "--file", BIN + "/keys/AUTHKEYS=/TEST/KEYS/AUTHKEYS"], os.path.join(d, "probe"), during=Probe(port))
    ok, what = probe.judge()
    json.dump({"replies": probe.replies, "fails": probe.fails, "shot": probe.shot and list(probe.shot[:1]) +
               [probe.shot[1], probe.shot[2]]}, open(os.path.join(d, "probe", "probe.json"), "w"), indent=1)
    s = read(os.path.join(d, "probe", "serial.log"))
    eb, ep = sorted(set(ENDS.findall(read(os.path.join(d, "base", "serial.log"))))), sorted(set(ENDS.findall(s)))
    bad, reports = glos_trouble(s)
    res.append(("%s/%s/probe" % (cell, job), ok and eb == ep and not bad,
                "; ".join([what] + (["ends %s" % " ".join(ep)] if eb != ep else []) + bad + reports)))
    return res


def script_cell(name, cwd, cmd, with_base, direct):
    res = []
    variants = (["base"] if with_base else []) + ["glos"] + (["direct"] if direct else [])
    outs = {}
    for v in variants:
        env = dict(os.environ, MGAHAL_DIR=MGA)
        env.pop("M4C_DIRECT", None)
        if name == "m4c":
            if v == "direct":
                env["M4C_DIRECT"] = "1"
        elif v != "base":
            env["LOOPA_EXTRA_ARGS"] = " ".join(shlex.quote(a) for a in glos_args("default", v == "direct"))
        p = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True)
        os.makedirs(os.path.join(OUT, name), exist_ok=True)
        open(os.path.join(OUT, name, v + ".txt"), "w").write(p.stdout + p.stderr)
        outs[v] = p.stdout
        fails = [l.strip() for l in p.stdout.splitlines() if "FAIL" in l]
        res.append(("%s/%s" % (name, v), p.returncode == 0, "exit %d%s" % (p.returncode, "; " + " | ".join(fails[:3]) if fails else "")))
    return res


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("action", nargs="?", default="run", choices=["run", "list"])
    ap.add_argument("--cell", action="append", choices=CELLS)
    ap.add_argument("--skip", action="append", default=[], choices=CELLS)
    ap.add_argument("-j", "--jobs", type=int, default=3, help="Loop A runs at once")
    ap.add_argument("--fresh", action="store_true", help="run the baselines again, cached or not")
    ap.add_argument("--no-direct", action="store_true", help="leave out the forced-direct runs")
    a = ap.parse_args()
    if a.action == "list":
        for c in JOBS:
            print("  %-16s jobs     %s" % (c[0], c[3]))
        for c in PROGRAMS:
            print("  %-16s program  %s (%s)" % (c[0], c[4], "/".join(c[2])))
        for c in SCRIPTS:
            print("  %-16s script   %s" % (c[0], c[4]))
        return 0
    if not shutil.which("ssh"):
        raise SystemExit("gate: the probes need OpenSSH's ssh (run on the host)")
    for f in ("GLOS.EXE", "GLOSK.BIN", "direct/GLOS.CFG", "keys/HOSTKEY", "keys/AUTHKEYS"):
        if not os.path.exists(os.path.join(BIN, f)):
            raise SystemExit("gate: no %s in %s (make glos-cache)" % (f, BIN))
    want = [c for c in (a.cell or CELLS) if c not in a.skip]
    os.makedirs(OUT, exist_ok=True)
    results = []
    summary = open(os.path.join(OUT, "summary.txt"), "w")
    lock = threading.Lock()

    def report(rs):
        with lock:
            for name, ok, what in rs:
                line = "  %-44s %s  %s" % (name, "PASS" if ok else "FAIL", what)
                print(line, flush=True)
                summary.write(line + "\n")
                summary.flush()
            results.extend(rs)

    def guarded(fn, *args):
        try:
            report(fn(*args))
        except Exception as e:                  # a run that broke the gate itself
            report([(args[0] if args and isinstance(args[0], str) else "?", False, "gate error: %r" % e)])

    print("gate: GLOS %s, 86Box %s" % (repo_state(ROOT), box86_build()), flush=True)
    # The three kinds side by side: the scripts (one Loop A run at a time each), the jobs.py suites (-j at a
    # time), the programs (-j at a time).
    def scripts():
        for name, cwd, cmd, with_base, _ in SCRIPTS:
            if name in want:
                guarded(script_cell, name, cwd, cmd, with_base, not a.no_direct)

    def jobs_cells():
        for name, args, direct, _ in JOBS:
            if name in want:
                guarded(jobs_cell, name, args, direct and not a.no_direct, a.jobs)
    side = [threading.Thread(target=scripts), threading.Thread(target=jobs_cells)]
    for t in side:
        t.start()
    work = []
    for cell, suite, boots, kill_at, _ in PROGRAMS:
        if cell in want:
            for i, (job, spec) in enumerate(survey.jobs(suite)):
                for boot in boots:
                    work.append((cell, job, spec, boot, kill_at if i == 0 else None))
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, a.jobs)) as ex:
        futs = [ex.submit(guarded, program_job, c, j, s, b, k, a.fresh, not a.no_direct) for c, j, s, b, k in work]
        for f in futs:
            f.result()
    for t in side:
        t.join()
    bad = [r for r in results if not r[1]]
    line = "gate: %d parts, %d failed" % (len(results), len(bad))
    print(line)
    summary.write(line + "\n")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
