#!/usr/bin/env python3
"""GLOS Loop A jobs (milestones-m0-m4.md). Runs inside the dev container
through MGA-Glide's harness:

  jobs.py m1 [--profile P ...] [--boot B ...]

m1: on each machine profile (bf6, 486dx2, 486dx4) and boot (default: no
XMS driver, raw mode; himemx: XMS mode, DOS=HIGH), RUN.BAT does
    VECCHK save, GLOS /ROUNDTRIP, VECCHK check, KEYWAIT (Enter typed), VMODE
and the run passes when the kernel's RTC tick count over 988.6 ms (18
PIT channel-2 windows) is within 2% of 1012, GLOS exits with code 0, the
interrupt vectors and PIC masks are as they were, the BIOS keyboard works
and the display is in text mode. Exit status 1 if any job fails."""
import argparse
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MGA = os.environ.get("MGA_GLIDE", os.path.expanduser("~/MGA-Glide"))
PROFILES = ["bf6", "486dx2", "486dx4"]
BOOTS = ["default", "himemx"]
EXPECT, TOL = 1012, 0.02


def run(name, args):
    out = os.path.join(ROOT, "out", name)
    subprocess.run([sys.executable, os.path.join(MGA, "tools/loopa/run.py"), "--name", name, "--out", out,
                    "--idle", "60", "--boot-grace", "60", "--timeout", "300"] + args,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=ROOT)
    status = open(os.path.join(out, "status")).read().strip() if os.path.exists(os.path.join(out, "status")) else "?"
    serial = open(os.path.join(out, "serial.log"), "rb").read().decode("latin-1").replace("\r", "") \
        if os.path.exists(os.path.join(out, "serial.log")) else ""
    return status, serial


def m1(profile, boot):
    name = "m1-%s-%s" % (profile, boot)
    status, serial = run(name, [
        "--machine", profile, "--boot-cfg", boot,
        "--file", "build/ow/GLOS.EXE=/TEST/GLOS.EXE", "--file", "build/kernel/GLOSK.BIN=/TEST/GLOSK.BIN",
        "--cmd", "SERSAY HX-START m1", "--cmd", "VECCHK save", "--cmd", "C:\\TEST\\GLOS.EXE /ROUNDTRIP",
        "--cmd", "VECCHK check", "--cmd", "KEYWAIT 20", "--cmd", "SERSAY HX-DONE 0",
        "--keys", "@HX-KEYWAIT ready,1:0x1c"])
    m = re.search(r"GLOS-RING0 ticks=(\d+)", serial)
    ticks = int(m.group(1)) if m else -1
    mode = re.search(r"GLOS-RING0 step=entry mode=(\w+)", serial)
    checks = {
        "ticks": abs(ticks - EXPECT) <= EXPECT * TOL,
        "exit0": "GLOS-EXIT code=0" in serial,
        "vecchk": "HX-VECCHK ok" in serial,
        "keyboard": "HX-KEY scan=1c" in serial,
        "textmode": "HX-VMODE bios=03" in serial,
        "mode": bool(mode) and mode.group(1) == ("xms" if boot == "himemx" else "raw"),
        "status": status == "PASS",
    }
    bad = [k for k, v in checks.items() if not v]
    print("  %-22s %s ticks=%d mode=%s%s" % (name, "PASS" if not bad else "FAIL", ticks,
                                              mode.group(1) if mode else "?",
                                              "" if not bad else " failed: " + " ".join(bad)), flush=True)
    return not bad


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("suite", choices=["m1"])
    ap.add_argument("--profile", action="append", choices=PROFILES)
    ap.add_argument("--boot", action="append", choices=BOOTS)
    a = ap.parse_args()
    ok = True
    for p in a.profile or PROFILES:
        for b in a.boot or BOOTS:
            ok &= m1(p, b)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
