#!/usr/bin/env python3
"""GLOS Loop A jobs (milestones-m0-m4.md). Runs inside the dev container
through MGA-Glide's harness:

  jobs.py m1 [--profile P ...] [--boot B ...]
  jobs.py refuse
  jobs.py m2 [--profile P ...] [--boot B ...] [-j N]
  jobs.py hostile [--profile P ...] [--boot B ...] [-j N]

m1: on each machine profile (bf6, 486dx2, 486dx4) and boot (default: no
XMS driver, raw mode; himemx: XMS mode, DOS=HIGH), RUN.BAT does
    VECCHK save, GLOS /ROUNDTRIP, VECCHK check, KEYWAIT (Enter typed), VMODE
and the run passes when the kernel's RTC tick count over 988.6 ms (18
PIT channel-2 windows) is within 2% of 1012, GLOS exits with code 0, the
interrupt vectors and PIC masks are as they were, the BIOS keyboard works
and the display is in text mode.

m2: M2.BAT (the HX tools XMSINFO, VBEINFO, VMODE, XMSTEST, WAITSEC and
KEYWAIT) runs once from RUN.BAT and once under "GLOS /RUN COMMAND /C", on
each profile and boot; the HX lines must be the same. Expected differences:
XMSINFO's free= and largest= (GLOS's own memory comes out of the pool), and
on the raw boot every XMS line (there is no driver without GLOS; GLOS's
XMSTEST lines are compared with the HIMEMX boot's instead, but for the HMA
and A20). After GLOS: VECCHK, a key through the BIOS and text mode.

hostile: HOSRUN.BAT runs every tests/dos/hostile.c case under one "GLOS /RUN
COMMAND /C"; the harness types Ctrl-Alt-Shift-Esc after each one's "armed"
line (and Ctrl-Alt-Del first for CAD). Each must end in GLOS-KILL (PRIV by
GLOS itself) and be followed by a KEYWAIT that gets Enter, with the reset requests, the A20 wrap and the vif-stuck
warnings expected; then the batch, GLOS and the machine carry on: the
kernel's tick still advancing, VECCHK, WAITSEC (the BIOS tick), a key and
text mode. Exit status 1 if any job fails."""
import argparse
import concurrent.futures
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
    os.makedirs(out, exist_ok=True)
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


HX = os.path.join(os.environ.get("MGA_CACHE", os.path.expanduser("~/.cache/mga-glide")), "glos", "hx")


def refuse():
    """GLOS refuses to start over another DPMI host and leaves the machine as it was."""
    status, serial = run("m1-refuse-dpmi", [
        "--file", "build/ow/GLOS.EXE=/TEST/GLOS.EXE", "--file", "build/kernel/GLOSK.BIN=/TEST/GLOSK.BIN",
        "--file", HX + "/HDPMI32.EXE=/HX/HDPMI32.EXE",
        "--cmd", "SERSAY HX-START refuse", "--cmd", "HDPMI32 -r", "--cmd", "VECCHK save",
        "--cmd", "C:\\TEST\\GLOS.EXE /ROUNDTRIP", "--cmd", "VECCHK check", "--cmd", "SERSAY HX-DONE 0"])
    ok = status == "PASS" and "GLOS-REFUSE reason=dpmi" in serial and "HX-VECCHK ok" in serial \
        and "GLOS-RING0" not in serial
    print("  %-22s %s" % ("m1-refuse-dpmi", "PASS" if ok else "FAIL"), flush=True)
    return ok


GLOS_FILES = ["--file", "build/ow/GLOS.EXE=/TEST/GLOS.EXE", "--file", "build/kernel/GLOSK.BIN=/TEST/GLOSK.BIN"]
MGA_DOS = os.path.join(MGA, "build/ow/dos")
M2_FILES = ["--file", MGA_DOS + "/XMSINFO.COM=/HX/XMSINFO.COM", "--file", MGA_DOS + "/VBEINFO.COM=/HX/VBEINFO.COM",
            "--file", "build/ow/dos/XMSTEST.EXE=/HX/XMSTEST.EXE", "--file", "build/ow/dos/HOSTILE.EXE=/HX/HOSTILE.EXE"]
M2_BAT = ["@ECHO OFF", "SERSAY HX-M2 begin", "XMSINFO", "VBEINFO", "VMODE", "XMSTEST", "WAITSEC 2",
          "SERSAY HX-M2 waited", "KEYWAIT 20", "SERSAY HX-M2 end"]
KEY_ENTER = "@HX-KEYWAIT ready,1:0x1c"
KILL_KEYS = "1:0x1d:down,1:0x38:down,1:0x2a:down,1.3:0x01,1.6:0x2a:up,1.6:0x38:up,1.6:0x1d:up"
CAD_KEYS = "1:0x1d:down,1:0x38:down,1.2:0x53,1.5:0x38:up,1.5:0x1d:up"
HOSTILE = ["CLIJMP", "POPFIF", "HLTCLI", "A20OFF", "PICREMAP", "RTCWRITE", "PITPROG", "KBCRESET", "CF9RESET", "CAD",
           "PRIV"]


def batfile(name, lines):
    """A DOS batch file in out/, for --file."""
    path = os.path.join(ROOT, "out", "bat", name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "w", newline="").write("\r\n".join(lines) + "\r\n")
    return path


def hx_lines(serial, begin, end):
    """The HX- lines between two markers, as a sorted list."""
    a, b = serial.find(begin), serial.find(end)
    if a < 0 or b < 0:
        return None
    return sorted(l for l in serial[a:b].split("\n") if l.startswith("HX-") and not l.startswith("HX-M2 "))


def normalise(lines, raw):
    out = []
    for l in lines:
        if l.startswith("HX-XMS ") or l.startswith("HX-XMSTEST "):
            if raw:
                continue
            l = re.sub(r" (free|largest)=\d+", "", l)
        out.append(l)
    return out


def m2(profile, boot):
    """One profile and boot: M2.BAT natively, then under GLOS."""
    tag = "%s-%s" % (profile, boot)
    bat = batfile("m2-%s/M2.BAT" % tag, M2_BAT)
    common = ["--machine", profile, "--boot-cfg", boot, "--file", bat + "=/TEST/M2.BAT"] + M2_FILES + GLOS_FILES
    st0, base = run("m2-base-" + tag, common + [
        "--cmd", "SERSAY HX-START m2", "--cmd", "CALL C:\\TEST\\M2.BAT", "--cmd", "SERSAY HX-DONE 0",
        "--keys", KEY_ENTER])
    st1, glos = run("m2-glos-" + tag, common + [
        "--cmd", "SERSAY HX-START m2", "--cmd", "VECCHK save",
        "--cmd", "C:\\TEST\\GLOS.EXE /RUN %COMSPEC% /C C:\\TEST\\M2.BAT", "--cmd", "VECCHK check",
        "--cmd", "KEYWAIT 20", "--cmd", "SERSAY HX-DONE 0", "--keys", KEY_ENTER + "," + KEY_ENTER])
    lb, lg = hx_lines(base, "HX-M2 begin", "HX-M2 end"), hx_lines(glos, "HX-M2 begin", "HX-M2 end")
    raw = boot == "default"
    same = lb is not None and lg is not None and normalise(lb, raw) == normalise(lg, raw)
    checks = {
        "base": st0 == "PASS" and lb is not None,
        "glos": st1 == "PASS" and lg is not None,
        "same-lines": same,
        "exit0": "GLOS-EXIT code=0" in glos,
        "vm": "GLOS-RING0 step=vm" in glos and "GLOS-VM leave code=0" in glos,
        "clean": "GLOS-PANIC" not in glos and "GLOS-WARN" not in glos,
        "vecchk": "HX-VECCHK ok" in glos,
        "keyboard": glos.count("HX-KEY scan=1c") == 2,
        "textmode": "HX-VMODE bios=03" in glos.split("GLOS-EXIT")[-1],
    }
    bad = [k for k, v in checks.items() if not v]
    if not same and lb is not None and lg is not None:
        nb, ng = normalise(lb, raw), normalise(lg, raw)
        diff = ["-" + l for l in nb if l not in ng] + ["+" + l for l in ng if l not in nb]
        bad.append("diff: " + " | ".join(diff[:8]))
    xms = [l for l in (lg or []) if l.startswith("HX-XMSTEST ")]
    return "  %-22s %s%s" % ("m2-" + tag, "PASS" if not bad else "FAIL", "" if not bad else " failed: " + " ".join(bad)), \
        not bad, xms


def hostile(profile, boot):
    tag = "%s-%s" % (profile, boot)
    lines = ["@ECHO OFF", "SERSAY HX-HOS begin"]
    keys = []
    for c in HOSTILE:
        # After each kill, the next program must get the keyboard through the BIOS.
        lines += ["HOSTILE " + c, "SERSAY HX-HOS after " + c, "KEYWAIT 10"]
        if c == "CAD":
            keys += ["@HX-HOSTILE CAD armed", CAD_KEYS, "@GLOS-RESET-REQ source=cad", KILL_KEYS]
        elif c != "PRIV":
            keys += ["@HX-HOSTILE %s armed" % c, KILL_KEYS]
        keys += [KEY_ENTER]
    lines += ["WAITSEC 1", "SERSAY HX-HOS end"]
    # Not HOSTILE.BAT: DOS looks in the current directory before PATH, so
    # "HOSTILE CLIJMP" would run the batch file again.
    bat = batfile("hostile-%s/HOSRUN.BAT" % tag, lines)
    st, serial = run("hostile-" + tag, ["--machine", profile, "--boot-cfg", boot, "--file",
                                        bat + "=/TEST/HOSRUN.BAT"] + M2_FILES + GLOS_FILES + [
        "--cmd", "SERSAY HX-START hostile", "--cmd", "VECCHK save",
        "--cmd", "C:\\TEST\\GLOS.EXE /RUN %COMSPEC% /C C:\\TEST\\HOSRUN.BAT", "--cmd", "VECCHK check",
        "--cmd", "WAITSEC 1", "--cmd", "SERSAY HX-HOS waited", "--cmd", "KEYWAIT 20", "--cmd", "SERSAY HX-DONE 0",
        "--keys", ",".join(keys + [KEY_ENTER]), "--timeout", "900"])
    kills = [int(t) for t in re.findall(r"GLOS-KILL psp=\w+ at=\S+ ticks=(\d+)", serial)]
    leave = re.search(r"GLOS-VM leave code=\d+ ticks=(\d+)", serial)
    checks = {
        "status": st == "PASS",
        "kills": len(kills) == len(HOSTILE),
        "each": all("HX-HOS after " + c in serial for c in HOSTILE),
        "resets": all("GLOS-RESET-REQ source=" + s in serial for s in ("kbc", "cf9", "cad")),
        "a20-wrap": "HX-HOSTILE a20 wrap=1" in serial,
        "vif-stuck": serial.count("GLOS-WARN vif-stuck") >= 3,
        "priv": "GLOS-WARN v86-priv" in serial and "HX-HOSTILE survived" not in serial,
        "tick-alive": bool(leave and kills and int(leave.group(1)) > kills[-1] + 800),
        "exit0": "GLOS-EXIT code=0" in serial,
        "no-panic": "GLOS-PANIC" not in serial,
        "vecchk": "HX-VECCHK ok" in serial,
        "bios-tick": "HX-HOS waited" in serial,
        "keyboard": serial.count("HX-KEY scan=1c") == len(HOSTILE) + 1,
        "textmode": "HX-VMODE bios=03" in serial,
    }
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s kills=%d%s" % ("hostile-" + tag, "PASS" if not bad else "FAIL", len(kills),
                                      "" if not bad else " failed: " + " ".join(bad)), not bad


def matrix(fn, combos, jobs):
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        results = list(ex.map(lambda pb: fn(*pb), combos))
    for r in results:
        print(r[0], flush=True)
    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("suite", choices=["m1", "refuse", "m2", "hostile"])
    ap.add_argument("--profile", action="append", choices=PROFILES)
    ap.add_argument("--boot", action="append", choices=BOOTS)
    ap.add_argument("-j", "--jobs", type=int, default=3, help="runs at once (m2, hostile)")
    a = ap.parse_args()
    if a.suite == "refuse":
        return 0 if refuse() else 1
    combos = [(p, b) for p in a.profile or PROFILES for b in a.boot or BOOTS]
    if a.suite == "m2":
        res = matrix(m2, combos, a.jobs)
        for (p, b), r in zip(combos, res):
            if r[2]:
                print("    %s-%s XMSTEST: %s" % (p, b, "; ".join(l[11:] for l in r[2])))
        return 0 if all(r[1] for r in res) else 1
    if a.suite == "hostile":
        return 0 if all(r[1] for r in matrix(hostile, combos, a.jobs)) else 1
    ok = True
    for p in a.profile or PROFILES:
        for b in a.boot or BOOTS:
            ok &= m1(p, b)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
