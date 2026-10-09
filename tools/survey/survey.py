#!/usr/bin/env python3
"""GLOS M0 survey: how do our programs behave under an IOPL-0 DPMI host?

GLOS will run DPMI clients at IOPL 0 (PRD D20), where CLI/STI trap and POPF
cannot restore IF; CWSDPMI and DOS/4GW give programs IOPL 3. Before GLOS
has a DPMI host of its own, this runs existing suites in Loop A three ways:

  base      as usual (CWSDPMI for DJGPP programs, DOS/4GW's own host)
  hdpmi32   HDPMI32 resident first (another IOPL-3 host)
  hdpmi32i  HDPMI32i resident first (the same host at IOPL 0)

and compares each variant with base: status, HX-TEST results, the end
markers, and the frames the programs dump. hdpmi32 against base shows host
differences; hdpmi32i against hdpmi32 isolates IOPL 0.

  survey.py run [--suite S ...] [--variant V ...]    results in out/survey/
  survey.py report                                  writes docs/survey-iopl0.md
  survey.py diff [--suite S ...] [--variant V ...]  each job's variants against base, printed
  survey.py list

Jobs run through MGA-Glide's Loop A harness ($MGA_GLIDE, default
~/MGA-Glide): directly with
its run.py, or through DOS-GL's own runners with MGAHAL_DIR pointing at
it and LOOPA_EXTRA_ARGS carrying the host. HDPMI is run as a behavioural
baseline only (PRD D26). Set BOX86_DIR to choose the 86Box build."""
import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
HOME = os.path.expanduser("~")
MGA = os.environ.get("MGA_GLIDE", os.path.join(HOME, "MGA-Glide"))
DOSGL = os.environ.get("DOSGL", os.path.join(HOME, "DOSGL"))
FW = os.environ.get("FIFTHWHEEL", os.path.join(HOME, "FifthWheel"))
CACHE = os.environ.get("MGA_CACHE", os.path.join(HOME, ".cache", "mga-glide"))
HX = os.path.join(CACHE, "glos", "hx")           # inside the cache the dev container mounts
OUT = os.path.join(ROOT, "out", "survey")
BLASTER = "SET BLASTER=A220 I5 D1 H5 T6"

VARIANTS = {
    "base": [],
    "hdpmi32": ["--file", HX + "/HDPMI32.EXE=/HX/HDPMI32.EXE", "--pre", "HDPMI32 -r"],
    "hdpmi32i": ["--file", HX + "/HDPMI32I.EXE=/HX/HDPMI32I.EXE", "--pre", "HDPMI32I -r"],
    # Diagnosis only (not in the default set): HDPMI reporting less free memory (-n).
    "hdpmi32i-n": ["--file", HX + "/HDPMI32I.EXE=/HX/HDPMI32I.EXE", "--pre", "HDPMI32I -r -n"],
    # GLOS as the shell (M4c): every program runs in its system VM, with GLOS as the DPMI host. `make
    # glos-cache` puts the build in the cache first.
    "glos": ["--boot-cfg", "glosshell", "--file", CACHE + "/glos/bin/GLOS.EXE=/TEST/GLOS.EXE",
             "--file", CACHE + "/glos/bin/GLOSK.BIN=/TEST/GLOSK.BIN"],
}
# The same with every program in direct mode (IOPL 3, M4e): GLOS.CFG's [shell] options = /DIRECT.
VARIANTS["glos-direct"] = VARIANTS["glos"] + ["--file", CACHE + "/glos/bin/direct/GLOS.CFG=/TEST/GLOS.CFG"]
DEFAULT_VARIANTS = ["base", "hdpmi32", "hdpmi32i"]

# name -> how to run it. "run": MGA-Glide's run.py with these arguments,
# started from `cwd` (the repo whose files it uses, so the dev container
# mounts it). "script": a DOS-GL runner, output in its out/<NAME>.
SUITES = {
    # How the host treats a client's CLI, PUSHF/CLI/POPF and STI (tests/dos/iftest.c).
    "iftest": dict(kind="run", cwd=ROOT, args=["--exe", "build/dj/IFTEST.EXE"]),
    # MGA-Glide's own test programs: DOS/4GW and DJGPP.
    "mga-hello": dict(kind="run", cwd=MGA, args=["--exe", "build/ow/dos/HELLO.EXE"]),
    "mga-stackpg": dict(kind="run", cwd=MGA, args=["--exe", "build/djgpp/STACKPG.EXE"]),
    "mga-mouse": dict(kind="run", cwd=MGA, args=["--exe", "build/djgpp/MOUSETST.EXE", "--mouse", "ps2", "--keys",
                                                 "@HX-TEST driver,1:mouse:40:-20:1,2:mouse:40:-20:0"]),
    "mga-sbbeep": dict(kind="run", cwd=MGA, args=["--exe", "build/djgpp/SBBEEP.EXE", "--sound", "sb16",
                                                  "--pre", BLASTER]),
    "mga-conform": dict(kind="run", cwd=MGA, multi=["t04", "t13", "t15", "t26", "t27"],
                        args=["--exe", "build/ow/dos/CONFORM.EXE", "--args", "{t}", "--ovl", "build/ow/GLIDE2X.OVL",
                              "--card", "g450"]),
    # DOS-GL (DJGPP, CWSDPMI) and its programs.
    "dosgl-conform": dict(kind="run", cwd=DOSGL, multi=["t01", "t05", "t09", "t13", "t15", "t20", "t25"],
                          args=["--exe", "build/exe/conform/{T}.EXE", "--card", "g450"]),
    "dosgl-texcube": dict(kind="run", cwd=DOSGL, args=["--exe", "build/exe/TEXCUBE.EXE", "--args=--frames 150",
                                                       "--card", "g450"]),
    "classicube": dict(kind="run", cwd=DOSGL, args=["--exe", "build/cc/CCDOS.EXE", "--card", "g450",
                                                    "--args=--singleplayer",
                                                    "--file", "build/cc/default.zip=/TEST/TEXPACKS/DEFAULT.ZIP",
                                                    "--pre", "SET DGL_EXIT_AFTER=300", "--pre", "SET DGL_STATS=1",
                                                    "--timeout", "1500", "--idle", "300"]),
    "fifthwheel": dict(kind="run", cwd=FW, args=["--exe", "build/dos/FWHEEL.EXE", "--file", "build/data/WORLD.PAK",
                                                 "--args=-test -fixed -autopilot -laps 1 -hash", "--card", "g450",
                                                 "--sound", "sb16", "--pre", BLASTER, "--idle", "300",
                                                 "--timeout", "2400"]),
    "quake": dict(kind="script", cwd=DOSGL, cmd=["tools/quake/run.sh", "quake", "g450"]),
    "prboom": dict(kind="script", cwd=DOSGL, cmd=["tools/doom/run.sh", "timedemo", "g450"]),
    "halflife": dict(kind="script", cwd=DOSGL, cmd=["tools/halflife/run.sh", "timedemo", "g450"]),
    # Retail: GTA (3dfx build, DOS/4GW) through its menus into the game. Enter until well after the menu
    # appears (about 34 s in; a few seconds later under GLOS, which the 35 s press missed, M4c).
    "gta": dict(kind="run", cwd=MGA, args=["--game", "gta", "--ovl", "build/ow/GLIDE2X.OVL", "--card", "g450",
                                           "--keys", "25:0x1c,30:0x1c,35:0x1c,45:0x1c,55:0x1c,65:0x1c", "--pre", "SET MGAGLIDE=exit_after=600",
                                           "--timeout", "1500", "--idle", "600"]),
    # A retail game with DOS/4GW 1.97 and MGA-Glide in its attract mode.
    "screamer-rally": dict(kind="run", cwd=MGA, args=["--game", "sr", "--ovl", "build/ow/GLIDE2X.OVL", "--card",
                                                      "g450", "--pre", "SET MGAGLIDE=exit_after=400",
                                                      "--timeout", "2400", "--idle", "1500"]),
}


def pins():
    text = open(os.path.join(ROOT, "tools/setup/versions.mk")).read()
    return dict(re.findall(r"^(\w+)\s*:=\s*(\S+)", text, re.M))


def prepare_hosts():
    """HDPMI32.EXE and HDPMI32I.EXE from the pinned HX runtime, in the cache."""
    if all(os.path.exists(os.path.join(HX, f)) for f in ("HDPMI32.EXE", "HDPMI32I.EXE")):
        return
    os.makedirs(HX, exist_ok=True)
    p = pins()
    z = os.path.join(HX, "HXRT.zip")
    subprocess.run([os.path.join(ROOT, "tools/setup/fetch.sh"), p["HXRT_URL"], p["HXRT_SHA256"], z], check=True)
    with zipfile.ZipFile(z) as zf:
        for src, dst in (("BIN/HDPMI32.EXE", "HDPMI32.EXE"), ("BIN/HDPMI32i.EXE", "HDPMI32I.EXE")):
            open(os.path.join(HX, dst), "wb").write(zf.read(src))


def jobs(suite):
    """[(job name, suite spec with {t} filled)] for one suite."""
    s = SUITES[suite]
    if "multi" not in s:
        return [(suite, s)]
    out = []
    for t in s["multi"]:
        spec = dict(s, args=[a.replace("{t}", t).replace("{T}", t.upper()) for a in s["args"]])
        out.append(("%s-%s" % (suite, t), spec))
    return out


def run_job(name, spec, variant):
    """Run one job in one variant; copies its results to out/survey/NAME/VARIANT/."""
    dest = os.path.join(OUT, name, variant)
    shutil.rmtree(dest, ignore_errors=True)
    env = dict(os.environ)
    env.pop("MGA_CARD", None)
    jobname = "survey-%s-%s" % (name, variant)
    if spec["kind"] == "run":
        out = os.path.join(MGA, "out", "survey", name, variant)
        cmd = [os.path.join(MGA, "tools/dev"), "python3", os.path.join(MGA, "tools/loopa/run.py"),
               "--name", jobname, "--out", out] + spec["args"] + VARIANTS[variant]
    else:
        out = os.path.join(spec["cwd"], "out", jobname)
        env.update(MGAHAL_DIR=MGA, NAME=jobname, LOOPA_EXTRA_ARGS=" ".join(shlex.quote(a) for a in VARIANTS[variant]))
        cmd = spec["cmd"]
    subprocess.run(cmd, cwd=spec["cwd"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    os.makedirs(dest, exist_ok=True)
    for fn in ("status", "serial.log", "result.json"):
        if os.path.exists(os.path.join(out, fn)):
            shutil.copyfile(os.path.join(out, fn), os.path.join(dest, fn))
    for fn in os.listdir(out) if os.path.isdir(out) else []:
        if fn.endswith(".png") and not fn.startswith("screen-"):   # frames the program dumped
            shutil.copyfile(os.path.join(out, fn), os.path.join(dest, fn))
    if spec["kind"] == "script":
        shutil.rmtree(out, ignore_errors=True)      # not ours to leave in DOS-GL's out/
    st = open(os.path.join(dest, "status")).read().strip() if os.path.exists(os.path.join(dest, "status")) else "?"
    print("  %-26s %-9s %s" % (name, variant, st), flush=True)
    return st


TIMING = re.compile(r"\b(ms|fps|t|mem_free_kb|elapsed|us|cycles|ticks)=\S+")


def summary(name, variant):
    """What a run is compared on: status, test results, end markers, frames."""
    d = os.path.join(OUT, name, variant)
    if not os.path.exists(os.path.join(d, "status")):
        return None
    serial = open(os.path.join(d, "serial.log"), "rb").read().decode("latin-1").replace("\r", "") \
        if os.path.exists(os.path.join(d, "serial.log")) else ""
    tests = sorted(l.split(" ", 3)[1] + " " + l.split(" ", 3)[2] for l in serial.split("\n")
                   if l.startswith("HX-TEST ") and len(l.split(" ")) > 2)
    ends = sorted(set(re.findall(r"^(HX-DONE \S+|DGL-EXIT frames=\d+|MGL-EXIT frames=\d+|HX-GAME-EXIT|MGL-EXC|"
                                 r"DGL-FAULT|DGL-GLERR \S+|DGL-TEXOOM|HX-SDLCRASH)", serial, re.M)))
    # Deterministic program output (Fifth Wheel's -fixed run hashes its state every 60 ticks).
    hashes = [l for l in serial.split("\n") if l.startswith("FW-HASH")]
    frames = sorted(f for f in os.listdir(d) if f.endswith(".png"))
    return {"status": open(os.path.join(d, "status")).read().strip(), "tests": tests, "ends": ends, "frames": frames,
            "hashes": hashes}


def same_frames(name, a, b, frames):
    """Frames whose RGB pixels differ between variants a and b (or are missing)."""
    sys.path.insert(0, os.path.join(MGA, "tools/loopa"))
    import png
    bad = []
    for f in frames:
        pa, pb = os.path.join(OUT, name, a, f), os.path.join(OUT, name, b, f)
        if not os.path.exists(pb) or png.read_png(pa) != png.read_png(pb):
            bad.append(f)
    return bad


def compare(name, variant):
    base, other = summary(name, "base"), summary(name, variant)
    if not base or not other:
        return "not run"
    diffs = []
    if base["status"] != other["status"]:
        diffs.append("status %s" % other["status"])
    if base["tests"] != other["tests"]:
        lost = sorted(set(base["tests"]) - set(other["tests"]))
        diffs.append("tests differ (%s)" % ", ".join(lost[:4]) if lost else "tests differ")
    if base["ends"] != other["ends"]:
        diffs.append("end %s" % " ".join(other["ends"]) if other["ends"] else "no end marker")
    n = min(len(base["hashes"]), len(other["hashes"]))
    if base["hashes"][:n] != other["hashes"][:n]:
        first = next(i for i in range(n) if base["hashes"][i] != other["hashes"][i])
        diffs.append("state hash differs from %s" % base["hashes"][first].split()[1])
    bad = same_frames(name, "base", variant, base["frames"])
    if bad:
        diffs.append("%d of %d frames differ" % (len(bad), len(base["frames"])))
    return "same" if not diffs else "; ".join(diffs)


def all_jobs(suites):
    return [j for s in suites for j in jobs(s)]


def report():
    rows = []
    for name, _ in all_jobs(SUITES):
        base = summary(name, "base")
        if not base:
            continue
        rows.append((name, base["status"], compare(name, "hdpmi32"), compare(name, "hdpmi32i")))
    lines = ["# IOPL-0 survey (M0)", "",
             "Generated by `tools/survey/survey.py report` from `out/survey/`. Each job ran in Loop A on its usual",
             "host (base), then with HDPMI32 (IOPL 3) and HDPMI32i (IOPL 0) resident first; each variant is",
             "compared with base on status, HX-TEST results, end markers and dumped frames.", "",
             "| Job | Base | HDPMI32 (IOPL 3) | HDPMI32i (IOPL 0) |", "|---|---|---|---|"]
    lines += ["| %s | %s | %s | %s |" % r for r in rows]
    path = os.path.join(ROOT, "docs", "survey-iopl0-results.md")
    open(path, "w").write("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("action", choices=["run", "report", "list", "diff"])
    ap.add_argument("--suite", action="append", choices=sorted(SUITES))
    ap.add_argument("--variant", action="append", choices=sorted(VARIANTS))
    a = ap.parse_args()
    if a.action == "list":
        for s in SUITES:
            print(s, " ".join(n for n, _ in jobs(s)))
        return 0
    if a.action == "report":
        return report()
    if a.action == "diff":
        bad = 0
        for name, _ in all_jobs(a.suite or list(SUITES)):
            for v in a.variant or ["glos"]:
                c = compare(name, v)
                bad += c != "same"
                print("  %-26s %-9s %s" % (name, v, c))
        return 1 if bad else 0
    prepare_hosts()
    for name, spec in all_jobs(a.suite or list(SUITES)):
        for v in a.variant or DEFAULT_VARIANTS:
            run_job(name, spec, v)
    return 0


if __name__ == "__main__":
    sys.exit(main())
