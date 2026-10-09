#!/usr/bin/env python3
"""GLOS Loop A jobs (milestones-m0-m4.md). Runs inside the dev container
through MGA-Glide's harness:

  jobs.py m1 [--profile P ...] [--boot B ...]
  jobs.py refuse
  jobs.py m2 [--profile P ...] [--boot B ...] [-j N]
  jobs.py hostile [--profile P ...] [--boot B ...] [-j N]
  jobs.py sched [--profile P ...] [--boot B ...] [-j N]
  jobs.py mem [--profile P ...] [--boot B ...] [-j N]
  jobs.py shell [--profile P ...] [-j N]      (MGA_GLIDE must have the glosshell boots)
  jobs.py net [-j N]
  jobs.py ssh [-j N]                          (on the host: it needs ssh, and runs Loop A through tools/dev)

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

sched: m2 with "GLOS /SELFTEST": a bulk thread that never blocks and an
urgent one sleeping 10 ticks at a time run beside the VM. The HX lines must
still match; the sleeper must wake every 10 ticks, at most 2 late; and bulk
must get 20-40% of the ticks when it and the VM were both ready (the 30%
budget).

mem: MEM /C natively and under "GLOS /RUN MEM /C" (PRD P7): GLOS's own
memory (its PSP, the resident stub and its environment) is 4 KB or less,
and MEM's largest program size is within 4 KB of plain DOS's.

shell: GLOS as the DOS shell (SHELL= on the glosshell and glosshell-himemx
boots). RUN.BAT, run from AUTOEXEC.BAT, sets a variable and EXITs; GLOS
copies that environment back and runs the console from GLOS.CFG
(SHCON.BAT), which must see the variable, PATH and COMSPEC, and reports
MEM /C. Then without GLOSK.BIN: GLOS refuses and hands over to
COMMAND.COM /P, which runs AUTOEXEC.BAT and so the job.

net: GLOS /SELFTEST with each Loop A NE2000 (M3 items 1-3): the card is
found and claimed, DHCP gives lwIP SLiRP's 10.0.2.15, a line sent from the
host to the echo service (port 7, forwarded) comes back, and DOS is clean
afterwards; with the Crynwr packet driver loaded, GLOS leaves the card alone
(GLOS-NET refuse reason=packet-driver).

ssh: GLOS headless (the agent) with the test keys (tests/keys) on bf6 +
RTL8029 and 486DX2 + ISA NE2000; from the host, through SLiRP's forward of
port 22, OpenSSH's ssh runs built-in commands (output and exit codes exact),
three at once, then DOS commands: ECHOARGS's stdout (through INT 21h 40h,
09h, 02h, 06h and INT 29h), stderr and exit code 7 exactly, a program found
in the current directory, an internal command (COMMAND.COM), three queued at
once; 1 MB up and back by sftp and by scp (same sha256), with a listing, a
rename, a removal and a directory made and removed; then glos shot of a
known screen against tests/loopa/golden.txt
(GLOS_GOLDEN=update records it), glos log, glos ps, and glos kill ending a
WAITSEC; a stranger's key is refused, and "glos exit" ends the run with
VECCHK clean. With the two cases on 486DX2 and bf6 + RTL8029, this is M3's
exit (make loopa-m3). (The run
ends on the host's word: a fixed wait in guest time lost to run.py's
wall-clock idle limit whenever 86Box ran slower than real time.) The first
connection's time is logged, not judged (PRD D28).

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


IN_CONTAINER = os.path.exists("/.dockerenv")


def runpy_cmd():
    """run.py, directly in the dev container, else through it (on the host network)."""
    if IN_CONTAINER:
        return [sys.executable, os.path.join(MGA, "tools/loopa/run.py")]
    return [os.path.join(MGA, "tools/dev"), "python3", os.path.join(MGA, "tools/loopa/run.py")]


def run(name, args, background=False):
    out = os.path.join(ROOT, "out", name)
    os.makedirs(out, exist_ok=True)
    for stale in ("serial.log", "status", "result.json"):   # a background run's caller polls these
        if os.path.exists(os.path.join(out, stale)):
            os.remove(os.path.join(out, stale))
    env = dict(os.environ, MGA_DOCKER_NETWORK="host") if not IN_CONTAINER else None
    cmd = runpy_cmd() + ["--name", name, "--out", out, "--idle", "60", "--boot-grace", "60", "--timeout", "300"] + args
    if background:
        return subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=ROOT, env=env)
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, cwd=ROOT, env=env)
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


def m2(profile, boot, selftest=False):
    """One profile and boot: M2.BAT natively, then under GLOS."""
    tag = "%s-%s" % (profile, boot)
    suite = "sched" if selftest else "m2"
    bat = batfile("m2-%s/M2.BAT" % tag, M2_BAT)
    common = ["--machine", profile, "--boot-cfg", boot, "--file", bat + "=/TEST/M2.BAT"] + M2_FILES + GLOS_FILES
    st0, base = run("m2-base-" + tag, common + [
        "--cmd", "SERSAY HX-START m2", "--cmd", "CALL C:\\TEST\\M2.BAT", "--cmd", "SERSAY HX-DONE 0",
        "--keys", KEY_ENTER])
    st1, glos = run(suite + "-glos-" + tag, common + [
        "--cmd", "SERSAY HX-START m2", "--cmd", "VECCHK save",
        "--cmd", "C:\\TEST\\GLOS.EXE %s/RUN %%COMSPEC%% /C C:\\TEST\\M2.BAT" % ("/SELFTEST " if selftest else ""), "--cmd", "VECCHK check",
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
    info = ""
    if selftest:
        sc = re.search(r"GLOS-SCHED switches=\d+ contended_bulk=(\d+) contended_normal=(\d+)", glos)
        st = re.search(r"GLOS-SCHEDTEST wakes=(\d+) late_max=(\d+) burn=(\d+)", glos)
        lv = re.search(r"GLOS-VM leave code=\d+ ticks=(\d+)", glos)
        share = int(sc.group(1)) / max(1, int(sc.group(1)) + int(sc.group(2))) if sc else -1
        checks["sleeper"] = bool(st and lv) and int(st.group(1)) >= int(lv.group(1)) // 10 - 50 \
            and int(st.group(2)) <= 2
        checks["budget"] = 0.20 <= share <= 0.40
        info = " bulk=%.0f%%%s" % (share * 100, " late_max=" + st.group(2) if st else "")
    bad = [k for k, v in checks.items() if not v]
    if not same and lb is not None and lg is not None:
        nb, ng = normalise(lb, raw), normalise(lg, raw)
        diff = ["-" + l for l in nb if l not in ng] + ["+" + l for l in ng if l not in nb]
        bad.append("diff: " + " | ".join(diff[:8]))
    xms = [l for l in (lg or []) if l.startswith("HX-XMSTEST ")]
    return "  %-22s %s%s%s" % (suite + "-" + tag, "PASS" if not bad else "FAIL", info,
                               "" if not bad else " failed: " + " ".join(bad)), not bad, xms


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


def mem(profile, boot):
    tag = "%s-%s" % (profile, boot)
    st, serial = run("mem-" + tag, ["--machine", profile, "--boot-cfg", boot] + GLOS_FILES + [
        "--cmd", "SERSAY HX-START mem", "--cmd", "MEM /C > C:\\OUT\\BASE.TXT",
        "--cmd", "C:\\TEST\\GLOS.EXE /RUN MEM /C > C:\\OUT\\GLOS.TXT", "--cmd", "SERSAY HX-DONE 0"])
    files = os.path.join(ROOT, "out", "mem-" + tag, "files")

    def read(n):
        p = os.path.join(files, n)
        return open(p, "rb").read().decode("latin-1") if os.path.exists(p) else ""

    def largest(t):
        m = re.search(r"Largest executable program size\s+\d+K \(([\d,]+) bytes\)", t)
        return int(m.group(1).replace(",", "")) if m else -1
    base, glos = read("BASE.TXT"), read("GLOS.TXT")
    own = re.search(r"^\s*GLOS\s+([\d,]+)", glos, re.M)
    own = int(own.group(1).replace(",", "")) if own else -1
    lb, lg = largest(base), largest(glos)
    checks = {
        "status": st == "PASS" and "GLOS-EXIT code=0" in serial,
        "resident": "GLOS-VM resident" in serial and "GLOS-WARN" not in serial,
        "own<=4K": 0 < own <= 4096,
        "largest": lb > 0 and lg > 0 and lb - lg <= 4096,
    }
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s glos=%d largest=%d/%d%s" % ("mem-" + tag, "PASS" if not bad else "FAIL", own, lg, lb,
                                                   "" if not bad else " failed: " + " ".join(bad)), not bad


SHCON = ["@ECHO OFF", "SERSAY HX-SHELL console var=%GLOSVAR% comspec=%COMSPEC%", "MEM /C > C:\\OUT\\SHELL.TXT",
         "SERSAY HX-DONE 0", "UTEXIT 0"]


def shell(profile, boot):
    """boot is the RAM configuration: default or himemx; the boot floppy is glosshell(-himemx)."""
    tag = "%s-%s" % (profile, boot)
    cfg_boot = "glosshell" + ("-himemx" if boot == "himemx" else "")
    con = batfile("shell-%s/SHCON.BAT" % tag, SHCON)
    cfg = batfile("shell-%s/GLOS.CFG" % tag, ["; GLOS as the shell, for jobs.py shell", "[shell]",
                                               "console = C:\\TEST\\SHCON.BAT"])
    st, serial = run("shell-" + tag, ["--machine", profile, "--boot-cfg", cfg_boot] + GLOS_FILES + [
        "--file", con + "=/TEST/SHCON.BAT", "--file", cfg + "=/TEST/GLOS.CFG",
        "--cmd", "SERSAY HX-START shell", "--cmd", "SET GLOSVAR=from-autoexec",
        "--cmd", "SERSAY HX-SHELL autoexec comspec=%COMSPEC%", "--cmd", "EXIT"])
    text = ""
    p = os.path.join(ROOT, "out", "shell-" + tag, "files", "SHELL.TXT")
    if os.path.exists(p):
        text = open(p, "rb").read().decode("latin-1")
    own = re.search(r"^\s*GLOS\s+([\d,]+)", text, re.M)
    largest = re.search(r"Largest executable program size\s+\d+K \(([\d,]+) bytes\)", text)
    comspec = "comspec=A:\\FREEDOS\\BIN\\COMMAND.COM"
    checks = {
        "status": st == "PASS",
        "shell": "GLOS-BOOT step=shell" in serial and "GLOS-VM resident" in serial,
        "autoexec": "HX-SHELL autoexec " + comspec in serial and "GLOS-VM autoexec code=" in serial,
        "env-back": "GLOS-VM env bytes=" in serial,
        "console": "HX-SHELL console var=from-autoexec " + comspec in serial,
        "clean": "GLOS-WARN" not in serial and "GLOS-PANIC" not in serial,
        "mem": bool(own and largest),
    }
    # The fallback: no kernel to start.
    st2, fb = run("shellfb-" + tag, ["--machine", profile, "--boot-cfg", cfg_boot,
                                     "--file", "build/ow/GLOS.EXE=/TEST/GLOS.EXE",
                                     "--cmd", "SERSAY HX-START shellfb", "--cmd", "SERSAY HX-FB comspec=%COMSPEC%",
                                     "--cmd", "SERSAY HX-DONE 0"])
    checks["fallback"] = st2 == "PASS" and "GLOS-REFUSE reason=no-kernel" in fb \
        and "GLOS-SHELL fallback=A:\\FREEDOS\\BIN\\COMMAND.COM /P=A:\\AUTOEXEC.BAT /E:2048" in fb \
        and "HX-FB " + comspec in fb
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s glos=%s largest=%s%s" % ("shell-" + tag, "PASS" if not bad else "FAIL",
                                               own.group(1) if own else "?", largest.group(1) if largest else "?",
                                               "" if not bad else " failed: " + " ".join(bad)), not bad


NET_CASES = [("bf6", "ne2kpci"), ("bf6", "ne2k"), ("486dx2", "ne2k"), ("486dx2", "ne2kpci"), ("486dx4", "ne2kpci")]


def net(profile, card):
    tag = "%s-%s" % (profile, card)
    line = "hello-glos-" + tag
    st, serial = run("net-" + tag, ["--machine", profile, "--net", card, "--net-fwd", "7",
                                    "--tcp-send", "GLOS-NET dhcp|net:7|" + line] + GLOS_FILES + [
        "--cmd", "SERSAY HX-START net", "--cmd", "VECCHK save", "--cmd", "C:\\TEST\\GLOS.EXE /SELFTEST /RUN WAITSEC 12",
        "--cmd", "VECCHK check", "--cmd", "SERSAY HX-DONE 0"])
    want = "rtl8029" if card == "ne2kpci" else "ne2000"
    echo = os.path.join(ROOT, "out", "net-" + tag, "tcp-0.txt")
    echo = open(echo, "rb").read().decode("latin-1") if os.path.exists(echo) else ""
    checks = {
        "status": st == "PASS" and "GLOS-EXIT code=0" in serial,
        "card": "GLOS-NET card=%s base=" % want in serial,
        "dhcp": "GLOS-NET dhcp ip=10.0.2.15 gw=10.0.2.2 mask=255.255.255.0" in serial,
        "echo": line in echo,
        "clean": "GLOS-WARN" not in serial and "GLOS-PANIC" not in serial and "HX-VECCHK ok" in serial,
    }
    if card == "ne2k":                          # the Crynwr driver owns the ISA card: GLOS refuses it
        st2, s2 = run("netpd-" + tag, ["--machine", profile, "--net", card, "--net-dos"] + GLOS_FILES + [
            "--cmd", "SERSAY HX-START netpd", "--cmd", "NE2000 0x60 10 0x300",
            "--cmd", "C:\\TEST\\GLOS.EXE /SELFTEST /RUN WAITSEC 1", "--cmd", "SERSAY HX-DONE 0"])
        checks["pktdrv"] = st2 == "PASS" and "GLOS-NET refuse reason=packet-driver int=60" in s2 \
            and "GLOS-EXIT code=0" in s2 and "GLOS-NET card=" not in s2
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s" % ("net-" + tag, "PASS" if not bad else "FAIL",
                             "" if not bad else " failed: " + " ".join(bad)), not bad


# SSH_ONE=1 runs one case (SSH_PROFILE, SSH_CARD); SSH_TAG suffixes its name; SSH_EXTRA adds run.py options.
SSH_CASES = ([(os.environ.get("SSH_PROFILE", "bf6"), os.environ.get("SSH_CARD", "ne2kpci"))] if os.environ.get("SSH_ONE")
             else [("bf6", "ne2kpci"), ("486dx2", "ne2k"), ("bf6", "ne2k")])
KEYS = ["--file", "tests/keys/hostkey=/TEST/KEYS/HOSTKEY", "--file", "tests/keys/AUTHKEYS=/TEST/KEYS/AUTHKEYS"]

# HDPMI32i (the HX runtime, a behavioural baseline only: PRD D26), fetched by tools/survey/survey.py.
HX = os.path.join(os.environ.get("MGA_CACHE", os.path.join(os.path.expanduser("~"), ".cache", "mga-glide")),
                  "glos", "hx")


def dpmiconf(profile, boot):
    """DPMICONF-32 (tests/dos/dpmiconf.c) under CWSDPMI (the DJGPP stub
    loads it from C:\\HX), HDPMI32i and GLOS: the baselines prove the
    checks, GLOS must pass them all."""
    tag = "%s-%s" % (profile, boot)
    common = ["--machine", profile, "--boot-cfg", boot, "--file", "build/dj/DPMICONF.EXE=/TEST/DPMICONF.EXE",
              "--file", "build/ow/dos/DPMI16.EXE=/TEST/DPMI16.EXE",       # its 16-bit child (M4d)
              "--timeout", "400", "--idle", "150", "--cmd", "SERSAY HX-START dpmiconf"]
    end = ["--cmd", "SERSAY HX-DONE 0"]
    out = {
        "cwsdpmi": run("dpmiconf-cws-" + tag, common + ["--cmd", "C:\\TEST\\DPMICONF.EXE"] + end),
        "hdpmi32i": run("dpmiconf-hdpmi-" + tag, common + ["--file", HX + "/HDPMI32I.EXE=/HX/HDPMI32I.EXE",
                        "--cmd", "HDPMI32I -r", "--cmd", "C:\\TEST\\DPMICONF.EXE"] + end),
        "glos": run("dpmiconf-glos-" + tag, common + GLOS_FILES + [
            "--cmd", "VECCHK save", "--cmd", "C:\\TEST\\GLOS.EXE /RUN C:\\TEST\\DPMICONF.EXE", "--cmd", "VECCHK check"]
            + end),
    }
    checks, info = {}, ""
    for host, (st, text) in out.items():
        checks[host] = "HX-TEST dpmi-end fails=0" in text
        if not checks[host]:
            info += " %s:[%s]" % (host, " ".join(l.split()[1] for l in text.splitlines()
                                                 if l.startswith("HX-TEST") and " FAIL" in l))
    gl = out["glos"][1]
    unimpl = re.findall(r"GLOS-DPMI-UNIMPL \S+ ax=(\w+)", gl)
    checks["unimpl-none"] = not unimpl
    checks["clean"] = "GLOS-PANIC" not in gl and "GLOS-WARN" not in gl and "GLOS-CRASH" not in gl
    checks["vecchk"] = "HX-VECCHK ok" in gl
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s%s" % ("dpmiconf-" + tag, "PASS" if not bad else "FAIL",
                               "" if not bad else " failed: " + " ".join(bad), info), not bad


def dpmiconf16(profile, boot):
    """DPMICONF-16 (tests/dos/dpmi16/) under HDPMI16 and HDPMI16i, its
    baselines, and GLOS (M4d): the baselines prove the checks, GLOS must pass
    them all, its glos-* ones too (a 32-bit child of the 16-bit client)."""
    tag = "%s-%s" % (profile, boot)
    common = ["--machine", profile, "--boot-cfg", boot, "--file", "build/ow/dos/DPMI16.EXE=/TEST/DPMI16.EXE",
              "--file", "build/dj/DPMICONF.EXE=/TEST/DPMICONF.EXE", "--timeout", "400", "--idle", "150",
              "--cmd", "SERSAY HX-START dpmi16"]
    end = ["--cmd", "SERSAY HX-DONE 0"]
    hosts = {
        "hdpmi16": common + ["--file", HX + "/HDPMI16.EXE=/HX/HDPMI16.EXE", "--cmd", "HDPMI16 -r",
                             "--cmd", "C:\\TEST\\DPMI16.EXE"] + end,
        "hdpmi16i": common + ["--file", HX + "/HDPMI16I.EXE=/HX/HDPMI16I.EXE", "--cmd", "HDPMI16I -r",
                              "--cmd", "C:\\TEST\\DPMI16.EXE"] + end,
        "glos": common + GLOS_FILES + ["--cmd", "VECCHK save", "--cmd", "C:\\TEST\\GLOS.EXE /RUN C:\\TEST\\DPMI16.EXE",
                                       "--cmd", "VECCHK check"] + end,
    }
    procs = {h: run("dpmi16-%s-%s" % (h, tag), a, background=True) for h, a in hosts.items()}
    text = {}
    for h, pr in procs.items():
        pr.wait()
        log = os.path.join(ROOT, "out", "dpmi16-%s-%s" % (h, tag), "serial.log")
        text[h] = open(log, "rb").read().decode("latin-1").replace("\r", "") if os.path.exists(log) else ""
    checks, info = {}, ""
    for host, t in text.items():
        checks[host] = "HX-TEST dpmi16-end fails=0" in t
        if not checks[host]:
            info += " %s:[%s]" % (host, " ".join(l.split()[1] for l in t.splitlines()
                                                 if l.startswith("HX-TEST") and " FAIL" in l) or "no end")
    gl = text["glos"]
    checks["unimpl-none"] = not re.findall(r"GLOS-DPMI-UNIMPL \S+ ax=(\w+)", gl)
    checks["clean"] = "GLOS-PANIC" not in gl and "GLOS-WARN" not in gl
    checks["one-crash"] = gl.count("GLOS-CRASH why=") == 1            # the child that faults on purpose
    checks["vecchk"] = "HX-VECCHK ok" in gl
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s%s" % ("dpmi16-" + tag, "PASS" if not bad else "FAIL",
                               "" if not bad else " failed: " + " ".join(bad), info), not bad


BORLAND = os.path.join(os.environ.get("MGA_CACHE", os.path.expanduser("~/.cache/mga-glide")), "glos", "borland")
# Ctrl-F9 (compile and run) once the IDE is up; Alt-X when the program has reported.
TPX_KEYS = "@HX-TPX start,15:0x1d:down,15.2:0x43,15.4:0x1d:up,@HX-TEST tpx-run,4:0x38:down,4.2:0x2d,4.4:0x38:up"


def tpx(profile, boot):
    """TPX.EXE, Turbo Pascal 7's IDE, a 16-bit DPMI client on Borland's RTM
    (M4d): it opens tests/borland/hello.pas, compiles and runs it (Ctrl-F9),
    the program reports over COM1, and Alt-X leaves. Without GLOS RTM loads
    DPMI16BI.OVL as the host; under GLOS, GLOS is the host. Both must report,
    and TPX end with code 0."""
    tag = "%s-%s" % (profile, boot)
    files = []
    for f in ("TPX.EXE", "RTM.EXE", "DPMI16BI.OVL", "TURBO.TPL"):
        files += ["--file", "%s/%s=/TP/%s" % (BORLAND, f, f)]
    files += ["--file", "tests/borland/hello.pas=/TP/HELLO.PAS", "--file", "build/ow/dos/RUNOUT.EXE=/TEST/RUNOUT.EXE"]
    # RUNOUT: it empties the BIOS key buffer first (the boot's F1 would open TPX's help) and reports the exit code.
    common = ["--machine", profile, "--boot-cfg", boot, "--timeout", "400", "--idle", "150", "--keys", TPX_KEYS,
              "--cmd", "C:", "--cmd", "CD \\TP", "--cmd", "SERSAY HX-TPX start"] + files
    line = "C:\\TEST\\RUNOUT.EXE TPX C:\\TP\\TPX.EXE HELLO.PAS"
    end = ["--cmd", "SERSAY HX-DONE 0"]
    hosts = {"base": common + ["--cmd", line] + end,
             "glos": common + GLOS_FILES + ["--cmd", "VECCHK save", "--cmd", "C:\\TEST\\GLOS.EXE /RUN " + line,
                                            "--cmd", "VECCHK check"] + end}
    if not os.path.exists(os.path.join(BORLAND, "TPX.EXE")):
        return "  %-22s FAIL no %s/TPX.EXE (make m4d-inputs; BORLAND_DIR)" % ("tpx-" + tag, BORLAND), False
    procs = {h: run("tpx-%s-%s" % (h, tag), a, background=True) for h, a in hosts.items()}
    text = {}
    for h, pr in procs.items():
        pr.wait()
        log = os.path.join(ROOT, "out", "tpx-%s-%s" % (h, tag), "serial.log")
        text[h] = open(log, "rb").read().decode("latin-1").replace("\r", "") if os.path.exists(log) else ""
    checks = {}
    for h, t in text.items():
        checks[h + "-ran"] = "HX-TEST tpx-run ok sum=5050" in t
        checks[h + "-exit0"] = "HX-RUN TPX code=0" in t
    gl = text["glos"]
    checks["glos-16bit"] = "GLOS-DPMI start bits=16" in gl and "GLOS-DPMI resident" in gl     # RTM's TSR
    checks["clean"] = "GLOS-PANIC" not in gl and "GLOS-WARN" not in gl and "GLOS-CRASH" not in gl and "DPMI-UNIMPL" not in gl
    checks["vecchk"] = "HX-VECCHK ok" in gl
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s" % ("tpx-" + tag, "PASS" if not bad else "FAIL", "" if not bad else " failed: " + " ".join(bad)), \
        not bad


def same_screen(a, b):
    """Two screenshots alike but for a text cursor's blink: whatever differs
    lies inside one 9x16 character cell (MGA-Glide's PNG reader)."""
    sys.path.insert(0, os.path.join(MGA, "tools", "loopa"))
    import png as mga_png
    try:
        (w, h, p), (w2, h2, q) = mga_png.read_png(a), mga_png.read_png(b)
    except (OSError, ValueError, AssertionError):
        return False
    if (w, h) != (w2, h2):
        return False
    cells, row = set(), w * 3
    for y in range(h):
        if p[y * row:(y + 1) * row] == q[y * row:(y + 1) * row]:
            continue
        for x in range(w):
            if p[y * row + x * 3:y * row + x * 3 + 3] != q[y * row + x * 3:y * row + x * 3 + 3]:
                cells.add((x // 9, y // 16))
        if len(cells) > 1:
            return False
    return True


def dpmi_hello(boot):
    """MGA-Glide's HELLO, DOS/4GW (Open Watcom) and DJGPP, on bf6 (Matrox
    cards are AGP-only in 86Box): the same HX-TEST and HX-IMG lines with and
    without GLOS, the pictures alike but for the text cursor's blink."""
    res, info = {}, ""
    for kind, exe in (("dos4gw", "build/ow/dos/HELLO.EXE"), ("djgpp", "build/djgpp/HELLO.EXE")):
        path = os.path.join(MGA, exe)
        if not os.path.exists(path):
            res[kind] = False
            info += " %s: no %s (make it in MGA-Glide)" % (kind, exe)
            continue
        common = ["--machine", "bf6", "--boot-cfg", boot, "--exe", path]
        st0, base = run("hello-%s-base-%s" % (kind, boot), common)
        st1, gl = run("hello-%s-glos-%s" % (kind, boot), common + GLOS_FILES + ["--wrap", "C:\\TEST\\GLOS.EXE /RUN"])

        def lines(t):                           # HX-IMG without its CRC: the pictures are compared below
            return sorted(re.sub(r" crc=\w+", "", l.strip()) for l in t.splitlines()
                          if l.startswith("HX-TEST") or l.startswith("HX-IMG"))
        imgs = re.findall(r"^HX-IMG (\S+)", base, re.M)
        same = all(same_screen(os.path.join(ROOT, "out", "hello-%s-base-%s" % (kind, boot), i + ".png"),
                               os.path.join(ROOT, "out", "hello-%s-glos-%s" % (kind, boot), i + ".png")) for i in imgs)
        unimpl = set(re.findall(r"GLOS-DPMI-UNIMPL \S+ ax=(\w+)", gl))
        res[kind] = (st0 == "PASS" and st1 == "PASS" and lines(gl) == lines(base) and lines(base) != [] and same
                     and "GLOS-DPMI start" in gl and "GLOS-PANIC" not in gl and "GLOS-CRASH" not in gl
                     and not unimpl)
        if not res[kind]:
            info += " %s: %s/%s" % (kind, st0, st1)
    bad = [k for k, v in res.items() if not v]
    return "  %-22s %s%s" % ("hello-bf6-" + boot, "PASS" if not bad else "FAIL",
                             "" if not bad else " failed: " + " ".join(bad) + info), not bad


def dpmi(profile, boot):
    """DPMIMINI (tests/dos/dpmimini.asm): with no host; as a 32-bit client
    under HDPMI32i, which proves the test; and under GLOS as a 16-bit and a
    32-bit client."""
    tag = "%s-%s" % (profile, boot)
    common = ["--machine", profile, "--boot-cfg", boot, "--file", "build/ow/dos/DPMIMINI.COM=/TEST/DPMIMINI.COM"]
    st0, none = run("dpmi-none-" + tag, common + [
        "--cmd", "SERSAY HX-START dpmi", "--cmd", "C:\\TEST\\DPMIMINI", "--cmd", "SERSAY HX-DONE 0"])
    st1, hd = run("dpmi-hdpmi-" + tag, common + ["--file", HX + "/HDPMI32I.EXE=/HX/HDPMI32I.EXE",
        "--cmd", "SERSAY HX-START dpmi", "--cmd", "HDPMI32I -r", "--cmd", "C:\\TEST\\DPMIMINI /32",
        "--cmd", "SERSAY HX-DONE 0"])
    runs = {}
    for bits in ("16", "32"):
        runs[bits] = run("dpmi-glos%s-%s" % (bits, tag), common + GLOS_FILES + [
            "--cmd", "SERSAY HX-START dpmi", "--cmd", "VECCHK save",
            "--cmd", "C:\\TEST\\GLOS.EXE /RUN C:\\TEST\\DPMIMINI.COM%s" % (" /32" if bits == "32" else ""),
            "--cmd", "VECCHK check", "--cmd", "SERSAY HX-DONE 0"])

    def line(text, key):
        m = re.search(r"HX-DPMI %s[^\r\n]*" % key, text)
        return m.group(0) if m else None
    checks = {"none": "HX-DPMI none" in none, "hdpmi": "HX-DPMI done" in hd}
    for bits, (st, gl) in runs.items():
        checks.update({
            "glos" + bits: st == "PASS" and "HX-DPMI done" in gl and "GLOS-DPMI start bits=" + bits in gl,
            "exit7-" + bits: "GLOS-DPMI exit code=7" in gl and "GLOS-EXIT code=7" in gl,
            "same-dos-" + bits: line(gl, "rm") is not None and line(gl, "rm") == line(hd, "rm")
                                and line(gl, "int21") == line(hd, "int21"),
            "vendor" + bits: "HX-DPMI vendor glos=0 dos4g=1" in gl,
            "espfix" + bits: bits == "32" or "HX-DPMI espfix hi=1234" in gl,
            "clean" + bits: "GLOS-PANIC" not in gl and "GLOS-WARN" not in gl and "DPMI-UNIMPL" not in gl,
            "vecchk" + bits: "HX-VECCHK ok" in gl,
        })
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s" % ("dpmi-" + tag, "PASS" if not bad else "FAIL",
                             "" if not bad else " failed: " + " ".join(bad)), not bad


# ---- M4b: DJGPP 2.05's own tests (make djtst), CRASHME, MGA-Glide's DJGPP tools

CTRL_C = ["3:0x1d:down", "3.3:0x2e:down", "3.6:0x2e:up", "3.9:0x1d:up"]
# NAME, keys timed from RUNOUT's "HX-RUN-START NAME" (scan codes: 39h space, 10h q)
DJTST = [("FAULT", []), ("NULL", []), ("FPU", []), ("RAISE", []), ("INFOBLK", []), ("BRK", []), ("MULTISPN", []),
         ("MULTISPN3", []), ("NEAR", []), ("NEAR2", []), ("NEAR3", []), ("ENABLE", []), ("GETOCW", []), ("STAT", []),
         ("TIMER", ["4:0x39"]), ("UCLOCK", ["3:0x39"]), ("HANG", CTRL_C), ("CTRLC", CTRL_C),
         ("SIGNALS", ["2:0x39", "10:0x10"]), ("CRASHME", []), ("CRASHGP", [])]
# DJGPP turns a key or the timer into a signal by cutting DS's limit to 4 KB
# in the IRQ handler, so the next data access faults: HANG, CTRLC and
# SIGNALS need MGA-Glide's 86Box patch 0112 (pinned in deps.mk), without
# which the dynarec checked no limit on loads.
EXC_LINE = re.compile(r"^(.*?) at eip=([0-9a-f]+)")


def dj_runs(text):
    """{NAME: (output lines, exit code)} from RUNOUT's HX-OUT and HX-RUN lines."""
    out = {}
    for l in text.splitlines():
        m = re.match(r"HX-OUT (\S+) ?(.*)", l)
        if m:
            out.setdefault(m.group(1), [[], None])[0].append(m.group(2))     # as sent: a cut keeps its length
        m = re.match(r"HX-RUN (\S+) code=(-?\d+)", l)
        if m:
            out.setdefault(m.group(1), [[], None])[1] = int(m.group(2))
    return out


def dj_key(name, lines):
    """What must be the same on two hosts: the lines that don't hold their
    addresses, selectors or timings, and the fault's kind and EIP."""
    keep = []
    for l in lines:
        m = EXC_LINE.match(l)
        if m and not l.startswith("SIGFPE handler"):
            keep.append("%s at eip=%s" % (m.group(1), m.group(2)) if name not in ("HANG", "CTRLC", "SIGNALS")
                        else m.group(1))                # where a key or a timer stopped it: anywhere
        elif "xiting due to" in l or l.startswith("gnal SIG"):
            continue                            # below, from the joined text
        elif l.startswith("Call frame traceback"):
            keep.append(l)
        elif name in ("FPU", "RAISE", "GETOCW", "STAT", "BRK", "ENABLE"):
            keep.append(l)
        elif name == "INFOBLK" and not re.search(r"linear_address|pid|selector|run mode", l):
            keep.append(l)
        elif name == "TIMER" and not l.startswith("iter"):
            keep.append(l)
        elif name == "MULTISPN3":               # itself 3 times by system(), each a client level (M4c)
            keep.append(re.sub(r"\b[0-9a-f]{4}\b", "#", l))
    # DJGPP's signal line may follow a line the program hadn't ended, and
    # RUNOUT cuts lines at 160 bytes: look for it in the lines as written.
    keep += ["Exiting due to signal " + m for m in re.findall(r"Exiting due to signal (SIG[A-Z]+)", "\n".join(dj_unwrap(lines)))]
    return keep


def dj_unwrap(lines):
    """RUNOUT's lines with its 160-byte cuts joined up again."""
    out, cont = [], False
    for l in lines:
        if cont:
            out[-1] += l
        else:
            out.append(l)
        cont = len(l) == 160
    return out


def dj_own(name, lines):
    """Checks a run must pass on its own: None, or why not."""
    if name == "TIMER":
        tics = [int(m.group(1)) for m in (re.search(r"tics = (\d+)", l) for l in lines) if m]
        return None if tics and max(tics) >= 5 else "tics=%s" % (max(tics) if tics else 0)
    if name == "UCLOCK":
        if sum(1 for l in lines if l.startswith("uclock ->")) < 40:
            return "only %d readings" % sum(1 for l in lines if l.startswith("uclock ->"))
        for l in lines[1:]:
            m = re.match(r"uclock -> \w{8} \w{8}\s+(\w{8}) (\w{8})", l)
            if m and (m.group(1) != "00000000" or int(m.group(2), 16) >= 0x80000000):
                return "went back: " + l
        return None
    if name in ("HANG", "CTRLC"):
        return None if any(l.startswith("INTR key Pressed") for l in lines) else "no SIGINT"
    if name == "SIGNALS":
        ticks = [int(m.group(1)) for m in (re.match(r"Tick (\d+)", l) for l in lines) if m]
        fpe = sum(1 for l in lines if l.endswith("SIGFPE") and l.startswith("."))
        return None if ticks and max(ticks) >= 5 and fpe else "ticks=%s fpe=%d" % (max(ticks) if ticks else 0, fpe)
    return None


# ENABLE reads IF with PUSHF after DJGPP's disable(). CWSDPMI (IOPL 3) clears
# the real IF; HDPMI32i clears it too, at IOPL 0, by returning to the client
# with IF off, which a `cli; jmp $` then turns into a dead machine. GLOS keeps
# the virtual IF (PRD D20), so PUSHF shows IF set and ENABLE stops at its
# first check: expected until direct-mode profiles (IOPL 3, M4e) run it.
DJ_KNOWN = {"ENABLE": (1, "disable -> incorrect; expected 0")}
# SIGNALS fires SIGALRM and SIGFPE as fast as it can, then ends on purpose with
# an FP exception. CWSDPMI doesn't always get there (one bf6 run died with a
# GPF in the signal storm), so GLOS is held to the test's own ending.
DJ_EXPECT = {"SIGNALS": (255, ["Floating Point exception", "Exiting due to signal SIGFPE"])}


def djtst(profile, boot):
    """DJGPP 2.05's tests (djtst205) by RUNOUT, which sends what each prints
    to COM1: under CWSDPMI, the baseline, and GLOS. Then CRASHME's crash
    report, its file, and symcrash naming the code."""
    tag = "%s-%s" % (profile, boot)
    bat = os.path.join(ROOT, "out", "djtst-" + tag + ".bat")
    with open(bat, "w", newline="\r\n") as f:
        for name, _ in DJTST:
            prog = {"CRASHME": "C:\\TEST\\DJ\\CRASHME.EXE", "CRASHGP": "C:\\TEST\\DJ\\CRASHME.EXE gp",
                    "MULTISPN3": "C:\\TEST\\DJ\\MULTISPN.EXE 3"}.get(
                name, "C:\\TEST\\DJ\\%s.EXE" % name)
            f.write("C:\\TEST\\RUNOUT.EXE %s %s\n" % (name, prog))
        f.write("C:\\TEST\\RUNOUT.EXE CRASHFILE %COMSPEC% /C TYPE C:\\GLOS\\CRASH\\CRASH000.TXT\n")
    files = ["--file", bat + "=/TEST/DJ.BAT", "--file", "build/ow/dos/RUNOUT.EXE=/TEST/RUNOUT.EXE",
             "--file", "build/dj/CRASHME.EXE=/TEST/DJ/CRASHME.EXE"]
    for name, _ in DJTST:
        if name not in ("CRASHME", "CRASHGP", "MULTISPN3"):
            files += ["--file", "build/dj/djtst/%s.EXE=/TEST/DJ/%s.EXE" % (name, name)]
    keys = ",".join("@HX-RUN-START %s,%s" % (n, ",".join(k)) for n, k in DJTST if k)
    common = ["--machine", profile, "--boot-cfg", boot, "--timeout", "1500", "--idle", "300", "--keys", keys,
              "--cmd", "SERSAY HX-START djtst"] + files
    end = ["--cmd", "SERSAY HX-DONE 0"]
    hosts = {"cws": common + ["--cmd", "CALL C:\\TEST\\DJ.BAT"] + end,     # CALL: back to RUN.BAT
             "glos": common + GLOS_FILES + ["--cmd", "VECCHK save", "--cmd", "C:\\TEST\\GLOS.EXE /RUN C:\\TEST\\DJ.BAT",
                                            "--cmd", "VECCHK check"] + end}
    procs = {h: run("djtst-%s-%s" % (h, tag), a, background=True) for h, a in hosts.items()}
    text, status = {}, {}
    for h, pr in procs.items():
        pr.wait()
        out = os.path.join(ROOT, "out", "djtst-%s-%s" % (h, tag))
        log = os.path.join(out, "serial.log")
        text[h] = open(log, "rb").read().decode("latin-1").replace("\r", "") if os.path.exists(log) else ""
        status[h] = open(os.path.join(out, "status")).read().strip() if os.path.exists(os.path.join(out, "status")) else "?"
    runs = {h: dj_runs(t) for h, t in text.items()}
    checks, info = {"runs-pass": all(v == "PASS" for v in status.values())}, ""
    if not checks["runs-pass"]:
        info += " runs: %s;" % " ".join("%s=%s" % kv for kv in sorted(status.items()))
    for name, _ in DJTST:
        if name in ("CRASHME", "CRASHGP"):
            continue
        g, c = runs["glos"].get(name), runs["cws"].get(name)
        why = None
        if name in DJ_KNOWN:
            code, text_ = DJ_KNOWN[name]
            checks[name.lower() + "-known"] = bool(g) and g[1] == code and any(text_ in l for l in g[0])
            info += " %s: known (M4e);" % name
            continue
        if name in DJ_EXPECT and g and g[1] is not None:
            code, want = DJ_EXPECT[name]
            key = dj_key(name, g[0])
            why = None if g[1] == code and all(w in key for w in want) else "ending: code %s %s" % (g[1], key[-2:])
            why = why or dj_own(name, g[0])
            if c and dj_key(name, c[0]) != key:
                info += " %s: baseline ended %s;" % (name, dj_key(name, c[0])[-1:])
        elif not g or g[1] is None:
            why = "no run"
        elif not c or c[1] is None:
            why = "no baseline"
        elif g[1] != c[1]:
            why = "code %s, baseline %s" % (g[1], c[1])
        elif dj_key(name, g[0]) != dj_key(name, c[0]):
            diff = [l for l in dj_key(name, g[0]) if l not in dj_key(name, c[0])][:2]
            why = "output: " + " | ".join(diff or ["(lines missing)"])
        else:
            why = dj_own(name, g[0]) or dj_own(name, c[0])
        checks[name.lower()] = why is None
        if why:
            info += " %s: %s;" % (name, why)
    gl = text["glos"]
    reports = re.findall(r"^GLOS-CRASH why=(\S+) vec=(\w+)", gl, re.M)
    checks["crash-reports"] = reports == [("exception", "0e"), ("exception", "0d")]
    checks["crash-codes"] = (runs["glos"].get("CRASHME", [[], None])[1] == 255
                             and runs["glos"].get("CRASHGP", [[], None])[1] == 255)
    checks["crash-file"] = any(l.startswith("why=exception vec=0e") for l in runs["glos"].get("CRASHFILE", [[]])[0])
    log = os.path.join(ROOT, "out", "djtst-glos-" + tag, "serial.log")
    sym = subprocess.run([sys.executable, os.path.join(ROOT, "tools/symcrash.py"), log, "--exe",
                          os.path.join(ROOT, "build/dj/CRASHME.EXE")], capture_output=True, text=True).stdout
    checks["symcrash"] = re.search(r"^eip\s+\w+\s+_crash_here", sym, re.M) is not None and \
        re.search(r"^eip\s+\w+\s+_crash_gp", sym, re.M) is not None
    checks["vecchk"] = "HX-VECCHK ok" in gl
    checks["clean"] = "GLOS-PANIC" not in gl and "GLOS-WARN" not in gl and "DPMI-UNIMPL" not in gl
    # MULTISPN3's children ran as levels of its context (M4c).
    ms = gl[gl.find("HX-RUN-START MULTISPN3"):gl.find("HX-RUN MULTISPN3")]
    checks["multispn-levels"] = (len(re.findall(r"GLOS-DPMI start [^\n]* level=2 ", ms)) == 3      # each child's
                                 and len(re.findall(r"GLOS-DPMI exit code=\d+ level=2", ms)) == 3)  # start and exit
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s%s" % ("djtst-" + tag, "PASS" if not bad else "FAIL",
                               "" if not bad else " failed: " + " ".join(bad), info), not bad


def dpmi_tools(boot):
    """MGA-Glide's DJGPP tools on bf6, without GLOS and under it (--wrap):
    STACKPG (INT 31h's stack), MOUSETST (CuteMouse's PS/2 IRQ and its INT
    33h), JOYTEST (the game port, timed with uclock), SBBEEP (the Sound
    Blaster DAC at uclock pace, 440 Hz in the recording)."""
    res, info = {}, ""
    joy = ("@HX-TEST centre,1:joy:axis:0:-32767,2:joy:axis:0:32767,3:joy:axis:0:0,3:joy:axis:1:-32767,"
           "4:joy:axis:1:32767,5:joy:axis:1:0,5:joy:axis:2:-32767,6:joy:axis:2:32767,7:joy:axis:2:0,"
           "7:joy:axis:3:-32767,8:joy:axis:3:32767,9:joy:axis:3:0,10:joy:button:0:1,11:joy:button:1:1,"
           "12:joy:button:2:1,13:joy:button:3:1")
    tools = [("stackpg", "STACKPG", []),
             ("mouse", "MOUSETST", ["--mouse", "ps2", "--keys", "@HX-TEST driver,1:mouse:40:-20:1,2:mouse:40:-20:0"]),
             ("joy", "JOYTEST", ["--keys", joy]),
             ("wav", "SBBEEP", ["--sound", "sb16", "--wav", "--pre", "SET BLASTER=A220 I5 D1 H5 T6"])]
    for key, exe, extra in tools:
        path = os.path.join(MGA, "build/djgpp/%s.EXE" % exe)
        if not os.path.exists(path):
            res[key] = False
            info += " %s: no %s (make it in MGA-Glide)" % (key, path)
            continue
        common = ["--machine", "bf6", "--boot-cfg", boot, "--exe", path, "--timeout", "200", "--idle", "80"] + extra
        st0, base = run("tools-%s-base-%s" % (key, boot), common)
        st1, gl = run("tools-%s-glos-%s" % (key, boot), common + GLOS_FILES + ["--wrap", "C:\\TEST\\GLOS.EXE /RUN"])

        def lines(t):
            return sorted(re.sub(r"\b(ms|t|us|ticks|spin)=\S+", "", l.strip()) for l in t.splitlines()
                          if l.startswith("HX-TEST"))
        ok = st0 == "PASS" and st1 == "PASS" and "GLOS-DPMI start" in gl and "GLOS-PANIC" not in gl \
            and "GLOS-CRASH" not in gl and "DPMI-UNIMPL" not in gl
        if key == "wav":
            wav = os.path.join(ROOT, "out", "tools-wav-glos-%s" % boot, "audio.wav")
            ok = ok and subprocess.run([sys.executable, os.path.join(MGA, "tools/loopa/wavcheck.py"), wav, "--tone",
                                        "440"], capture_output=True).returncode == 0
        res[key] = ok
        if not ok:
            info += " %s: %s/%s" % (key, st0, st1)
    bad = [k for k, v in res.items() if not v]
    return "  %-22s %s%s" % ("tools-bf6-" + boot, "PASS" if not bad else "FAIL",
                             "" if not bad else " failed: " + " ".join(bad) + info), not bad


# ---- M4c: HDPMI's regression tests (make m4c-inputs), HDPMI32i against GLOS

# Left out: interactive (WAITKEY, MOUEVNT*, NEWCLMZ starts a shell), HDPMI's
# own internals or privileges (DISPGDT, DISPIDT, PRVILEG0, SETCR0, SETCR4,
# SETMSR, EXC0ER0, I3102103 fault inside HDPMI, I4B8105 its VDS), a speed
# test (INTSPEED), EMUHLT, IRQ1EXC and IRQ12EXC (they wait for keys or the mouse under HDPMI32i too), and
# GETCLMZ (EXEC2D's helper).
HR_SKIP = {"EMUHLT", "IRQ1EXC", "IRQ12EXC", "WAITKEY", "MOUEVNT1", "MOUEVNT2", "MOUEVNT3", "NEWCLMZ", "DISPGDT", "DISPIDT", "PRVILEG0", "SETCR0",
           "SETCR4", "SETMSR", "EXC0ER0", "I3102103", "I4B8105", "INTSPEED", "GETCLMZ"}


# Differences from HDPMI32i that are expected, and why (supervisor.md §13,
# §14, §20). A test that differs only in selector numbers (GLOS's client
# code selector is 97h where HDPMI's is 9Fh, its callbacks' stack another)
# needs no entry.
HR_KNOWN = {
    # HDPMI's own INT 21h translation for 32-bit clients (EXEC, 55h, AH=09h
    # from a 32-bit DS:EDX), which a plain DPMI host doesn't do.
    "EXEC2C": "hx-api", "EXEC2D": "hx-api", "INT2155": "hx-api", "EXC0E2": "hx-api",
    # An unhandled exception: the same exit code, but HDPMI prints its dump on
    # the program's output and GLOS writes GLOS-CRASH to the log (§19).
    "EXC00": "dump", "EXC05": "dump", "EXC06": "dump", "EXC07": "dump", "EXC0B": "dump", "EXC0C": "dump",
    "EXC0D": "dump", "EXC0E": "dump", "I3105032": "dump",
    "EXC11": "privileged",      # HDPMI sets CR0.AM for the client; a ring-3 client can't
    "EXAMPLE": "timing",        # the exit code counts IRQs
    # (EXC01MZ, single-step routing, differed only through 86Box's stale single-step trap: patch 0113, M4d.)
    "I3100001": "error-code",   # 0000h with CX=0: 8021h ([DPMI1.0]); HDPMI leaves AX=0
    # HDPMI refuses a second client started by the first; GLOS runs it as a level (M4c).
    "I3100002": "nested", "NEWCL": "nested", "NEWCL2": "nested",
    "I3100003": "layout",       # the host's own LDT layout
    "I3103002": "xms",          # the HDPMI run has no XMS; GLOS's server has no 4309h handle table
    "I31090X": "iopl0",         # PUSHF shows the real IF at IOPL 0 (D20)
    # 0305h: GLOS has no state to save (size 0), HDPMI 1Ch; RAWJMP5 then loops
    # once more inside RUNOUT's window.
    "RAWJMP1": "state-size", "RAWJMP2": "state-size", "RAWJMP3": "state-size", "RAWJMP5": "state-size",
    "RAWJMP6": "iopl0",
    "RMCB7": "rmcb-fs",         # FS in a callback's register structure: HDPMI passes a selector, GLOS 0
    "RMCB8": "nest-limit",      # GLOS fails the 17th nested real-mode call; HDPMI exits fatally at 39
}


def hr_tests():
    d = os.path.join(ROOT, "build", "hdpmireg")
    return sorted((f[:-4].upper(), f) for f in os.listdir(d) if f.lower().endswith(".exe")) if os.path.isdir(d) else []


def hdpmireg(profile, boot):
    """HDPMI's regression tests by RUNOUT, under HDPMI32i (their baseline)
    and GLOS: each test's exit code and output, compared."""
    tag = "%s-%s" % (profile, boot)
    tests = [(n, f) for n, f in hr_tests() if n not in HR_SKIP]
    bat = os.path.join(ROOT, "out", "hdpmireg-" + tag + ".bat")
    with open(bat, "w", newline="\r\n") as f:
        for n, _ in tests:
            f.write("C:\\TEST\\RUNOUT.EXE %s C:\\TEST\\HR\\%s.EXE\n" % (n, n))
    files = ["--file", bat + "=/TEST/HR.BAT", "--file", "build/ow/dos/RUNOUT.EXE=/TEST/RUNOUT.EXE"]
    for n, fn in hr_tests():                    # GETCLMZ too: EXEC2D starts it
        files += ["--file", "build/hdpmireg/%s=/TEST/HR/%s.EXE" % (fn, n)]
    # Some wait for a key at the end (EXAMPLE): Enter, then Esc, a few seconds
    # into each; RUNOUT drops what a quicker test left in the buffer.
    keys = ",".join("@HX-RUN-START %s,6:0x1c,8:0x01" % n for n, _ in tests)
    common = ["--machine", profile, "--boot-cfg", boot, "--timeout", "2400", "--idle", "240", "--keys", keys,
              "--cmd", "SERSAY HX-START hdpmireg", "--cmd", "CD \\TEST\\HR"] + files
    end = ["--cmd", "SERSAY HX-DONE 0"]
    hosts = {"hdpmi": common + ["--file", HX + "/HDPMI32I.EXE=/HX/HDPMI32I.EXE", "--cmd", "HDPMI32I -r",
                                "--cmd", "CALL C:\\TEST\\HR.BAT"] + end,
             "glos": common + GLOS_FILES + ["--cmd", "VECCHK save", "--cmd", "C:\\TEST\\GLOS.EXE /RUN C:\\TEST\\HR.BAT",
                                            "--cmd", "VECCHK check"] + end}
    procs = {h: run("hdpmireg-%s-%s" % (h, tag), a, background=True) for h, a in hosts.items()}
    text, status = {}, {}
    for h, pr in procs.items():
        pr.wait()
        out = os.path.join(ROOT, "out", "hdpmireg-%s-%s" % (h, tag))
        log = os.path.join(out, "serial.log")
        text[h] = open(log, "rb").read().decode("latin-1").replace("\r", "") if os.path.exists(log) else ""
        status[h] = open(os.path.join(out, "status")).read().strip() if os.path.exists(os.path.join(out, "status")) else "?"
    runs = {h: dj_runs(t) for h, t in text.items()}
    same, sel, known, new, gone = [], [], [], [], []
    for n, _ in tests:
        g, h = runs["glos"].get(n), runs["hdpmi"].get(n)
        if g and h and g[1] == h[1] and hr_norm(g[0]) == hr_norm(h[0]):
            (gone if n in HR_KNOWN else same).append(n)
        elif g and h and g[1] == h[1] and hr_sel(g[0]) == hr_sel(h[0]):
            (gone if n in HR_KNOWN else sel).append(n)
        elif n in HR_KNOWN:
            known.append(n)
        else:
            new.append(n)
    ok = not new and all(v == "PASS" for v in status.values())
    return "  %-22s %s same=%d selectors=%d known=%d%s%s%s" % (
        "hdpmireg-" + tag, "PASS" if ok else "FAIL", len(same), len(sel), len(known),
        " differ: " + " ".join(new) if new else "", " (known, now alike: %s)" % " ".join(gone) if gone else "",
        "" if all(v == "PASS" for v in status.values()) else " runs=%s" % status), ok


def hr_norm(lines):
    """A test's lines with what is the host's own (addresses, HDPMI banners)
    made comparable: hex numbers of 3 or more digits become #."""
    return [re.sub(r"\b[0-9A-Fa-f]{3,8}h?\b", "#", l).rstrip() for l in lines]


def hr_sel(lines):
    """hr_norm, and two-digit hex after = or : too (selectors)."""
    return [re.sub(r"(?<=[=:])[0-9A-Fa-f]{2}\b", "#", l) for l in hr_norm(lines)]



# ---- M4c: ecm's lDebugX stepping his dpmimini.com (make m4c-inputs)

def ecm(profile, boot):
    """lDebugX, a debugger that follows its program into protected mode,
    runs ecm's dpmimini.com from tests/ecm/script.txt (go to each of its
    breakpoints, registers, a trace step) under HDPMI32i and GLOS: the same
    output but for selectors and addresses."""
    tag = "%s-%s" % (profile, boot)
    files = ["--file", "build/ow/dos/RUNOUT.EXE=/TEST/RUNOUT.EXE", "--file", "build/ecm/ldebugx.com=/TEST/ECM/LDEBUGX.COM",
             "--file", "build/ecm/dpmimini.com=/TEST/ECM/DPMIMINI.COM", "--file", "tests/ecm/script.txt=/TEST/ECM/SCRIPT.TXT"]
    line = "C:\\TEST\\RUNOUT.EXE ECM C:\\TEST\\ECM\\LDEBUGX.COM C:\\TEST\\ECM\\DPMIMINI.COM < C:\\TEST\\ECM\\SCRIPT.TXT"
    common = ["--machine", profile, "--boot-cfg", boot, "--timeout", "600", "--idle", "120",
              "--cmd", "SERSAY HX-START ecm", "--cmd", "CD \\TEST\\ECM"] + files
    end = ["--cmd", "SERSAY HX-DONE 0"]
    bat = os.path.join(ROOT, "out", "ecm-" + tag + ".bat")
    with open(bat, "w", newline="\r\n") as f:
        f.write(line + "\n")
    hosts = {"hdpmi": common + ["--file", HX + "/HDPMI32I.EXE=/HX/HDPMI32I.EXE", "--cmd", "HDPMI32I -r", "--cmd", line] + end,
             "glos": common + GLOS_FILES + ["--file", bat + "=/TEST/ECM.BAT", "--cmd", "VECCHK save",
                                            "--cmd", "C:\\TEST\\GLOS.EXE /RUN C:\\TEST\\ECM.BAT", "--cmd", "VECCHK check"] + end}
    procs = {h: run("ecm-%s-%s" % (h, tag), a, background=True) for h, a in hosts.items()}
    text = {}
    for h, pr in procs.items():
        pr.wait()
        log = os.path.join(ROOT, "out", "ecm-%s-%s" % (h, tag), "serial.log")
        text[h] = open(log, "rb").read().decode("latin-1").replace("\r", "") if os.path.exists(log) else ""
    runs = {h: dj_runs(t).get("ECM") for h, t in text.items()}
    g, hd = runs["glos"], runs["hdpmi"]
    ok = bool(g and hd and g[1] == hd[1] and hr_norm(g[0]) == hr_norm(hd[0]))
    ok = ok and "HX-VECCHK ok" in text["glos"] and "GLOS-PANIC" not in text["glos"]
    info = ""
    if g and hd and not ok:
        a, b = hr_norm(g[0]), hr_norm(hd[0])
        info = " first difference: %r / %r" % next(((x, y) for x, y in zip(a, b) if x != y), (len(a), len(b)))
    return "  %-22s %s%s" % ("ecm-" + tag, "PASS" if ok else "FAIL", info), ok


GOLDEN = os.path.join(ROOT, "tests/loopa/golden.txt")      # "NAME SHA256" lines


def png_pixels(data):
    """(width, height, sha256 of the decoded scanlines) of a PNG, or None."""
    import hashlib
    import struct
    import zlib
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    p, idat, w, h = 8, b"", 0, 0
    while p + 12 <= len(data):
        n = struct.unpack(">I", data[p:p + 4])[0]
        kind, body = data[p + 4:p + 8], data[p + 8:p + 8 + n]
        if zlib.crc32(kind + body) != struct.unpack(">I", data[p + 8 + n:p + 12 + n])[0]:
            return None
        if kind == b"IHDR":
            w, h = struct.unpack(">II", body[:8])
        elif kind == b"IDAT":
            idat += body
        p += 12 + n
    return w, h, hashlib.sha256(zlib.decompress(idat)).hexdigest()


def golden(name, value):
    """value against tests/loopa/golden.txt; GLOS_GOLDEN=update records it instead."""
    lines = dict(l.split() for l in open(GOLDEN) if l.strip()) if os.path.exists(GOLDEN) else {}
    if os.environ.get("GLOS_GOLDEN") == "update":
        lines[name] = value
        with open(GOLDEN, "w") as f:
            f.writelines("%s %s\n" % kv for kv in sorted(lines.items()))
        return True
    return lines.get(name) == value


def ssh_case(profile, card):
    import socket
    import tempfile
    import time
    tag = "%s-%s%s" % (profile, card, os.environ.get("SSH_TAG", ""))
    s = socket.socket()
    s.bind(("0.0.0.0", 0))
    port = s.getsockname()[1]
    s.close()
    proc = run("ssh-" + tag, os.environ.get("SSH_EXTRA", "").split() + ["--machine", profile, "--net", card, "--net-fwd", "%d:22" % port, "--timeout", "600",
                              "--idle", "300"] + GLOS_FILES + KEYS + [
        "--file", "build/ow/dos/ECHOARGS.EXE=/TEST/ECHOARGS.EXE",
        "--cmd", "SERSAY HX-START ssh", "--cmd", "VECCHK save", "--cmd", "C:\\TEST\\GLOS.EXE",
        "--cmd", "VECCHK check", "--cmd", "SERSAY HX-DONE 0"],
        background=True)
    serial = os.path.join(ROOT, "out", "ssh-" + tag, "serial.log")
    tmp = tempfile.mkdtemp()
    key, known = os.path.join(tmp, "client"), os.path.join(tmp, "known")
    open(key, "w").write(open(os.path.join(ROOT, "tests/keys/client")).read())
    os.chmod(key, 0o600)
    open(known, "w").write("[127.0.0.1]:%d %s" % (port, open(os.path.join(ROOT, "tests/keys/hostkey.pub")).read()))
    base = ["ssh", "-p", str(port), "-o", "IdentitiesOnly=yes", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=yes",
            "-o", "UserKnownHostsFile=" + known, "-o", "ConnectTimeout=60", "-o", "LogLevel=ERROR"]
    opts = base + ["-i", key]

    def ssh(cmd, keyfile=None):
        p = subprocess.run((base + ["-i", keyfile] if keyfile else opts) + ["glos@127.0.0.1", cmd],
                           capture_output=True, timeout=180)
        return p.returncode, p.stdout.decode("latin-1"), p.stderr.decode("latin-1")
    checks, info = {}, ""
    t0 = time.time()
    while time.time() - t0 < 240:
        text = open(serial, "rb").read().decode("latin-1") if os.path.exists(serial) else ""
        if "GLOS-NET dhcp" in text and "GLOS-SSH listen" in text:
            break
        time.sleep(1)
    try:
        t1 = time.time()
        rc, out, _ = ssh("glos ver")
        info = " first=%.1fs" % (time.time() - t1)
        checks["ver"] = rc == 0 and out.startswith("GLOS M3")
        rc, out, _ = ssh("glos echo hello from " + tag)
        checks["echo"] = rc == 0 and out == "hello from %s\n" % tag
        rc, out, err = ssh("glos nosuch")
        checks["unknown-127"] = rc == 127 and "no such command" in err
        ps = [subprocess.Popen(opts + ["glos@127.0.0.1", "glos echo par%d" % i], stdout=subprocess.PIPE)
              for i in range(3)]
        outs = [p.communicate(timeout=300)[0].decode() for p in ps]
        checks["three-at-once"] = sorted(outs) == ["par0\n", "par1\n", "par2\n"]
        # DOS commands through the agent: every capture path, exactly, and the exit code.
        rc, out, err = ssh("C:\\TEST\\ECHOARGS.EXE a b c")
        checks["dos-capture"] = (rc == 7 and out == "a b c\r\nnine two six int29\r\n"
                                 and err == "echoargs: 3 arguments\r\n")
        if not checks["dos-capture"]:
            info += " capture=%r/%r/%d" % (out, err, rc)
        rc, out, _ = ssh("cd \\TEST")
        rc2, out2, _ = ssh("echoargs found")       # the current directory, no extension
        checks["dos-lookup"] = rc == 0 and rc2 == 7 and out2.startswith("found\r\n")
        rc, out, _ = ssh("dir C:\\TEST")           # an internal command: COMMAND.COM
        checks["dos-internal"] = rc == 0 and "ECHOARGS" in out
        ps = [subprocess.Popen(opts + ["glos@127.0.0.1", "echoargs q%d" % i], stdout=subprocess.PIPE,
                               stderr=subprocess.DEVNULL) for i in range(3)]
        outs = [(p.communicate(timeout=300)[0].decode("latin-1"), p.returncode) for p in ps]
        checks["dos-queued"] = sorted(outs) == [("q%d\r\nnine two six int29\r\n" % i, 7) for i in range(3)]
        # SFTP on the DOS server: 1 MB there and back by sftp and by scp, intact; a listing,
        # a rename, a removal, a directory made and removed.
        import hashlib
        big = os.path.join(tmp, "big.bin")
        open(big, "wb").write(os.urandom(1 << 20))
        want = hashlib.sha256(open(big, "rb").read()).hexdigest()
        fx = ["-P", str(port)] + base[3:] + ["-i", key]
        batch = os.path.join(tmp, "batch")
        open(batch, "w").write("cd /C/TEST\nput %s\nls -l\nget BIG.BIN %s\nrename BIG.BIN OLD.BIN\nrm OLD.BIN\n"
                               "mkdir NEWDIR\nrmdir NEWDIR\nls\n" % (big, os.path.join(tmp, "back.bin")))
        t3 = time.time()
        p = subprocess.run(["sftp"] + fx + ["-b", batch, "glos@127.0.0.1"], capture_output=True, timeout=600)
        info += " sftp=%.1fs" % (time.time() - t3)
        lsout = p.stdout.decode("latin-1")
        back = os.path.join(tmp, "back.bin")
        checks["sftp"] = (p.returncode == 0 and os.path.exists(back)
                          and hashlib.sha256(open(back, "rb").read()).hexdigest() == want
                          and "BIG.BIN" in lsout and "1048576" in lsout)
        if not checks["sftp"]:
            info += " sftp-rc=%d err=%r" % (p.returncode, p.stderr.decode("latin-1")[-200:])
        sback = os.path.join(tmp, "sback.bin")
        p1 = subprocess.run(["scp"] + fx + [big, "glos@127.0.0.1:/C/TEST/S.BIN"], capture_output=True, timeout=600)
        p2 = subprocess.run(["scp"] + fx + ["glos@127.0.0.1:/C/TEST/S.BIN", sback], capture_output=True, timeout=600)
        checks["scp"] = (p1.returncode == 0 and p2.returncode == 0 and os.path.exists(sback)
                         and hashlib.sha256(open(sback, "rb").read()).hexdigest() == want)
        rc, _, _ = ssh("del C:\\TEST\\S.BIN")
        # Built-ins: a known screen in a PNG, the COM1 mirror, the process list, and a kill.
        ssh("cls")
        ssh("echoargs GOLDEN")
        p = subprocess.run(opts + ["glos@127.0.0.1", "glos shot"], capture_output=True, timeout=180)
        open(os.path.join(ROOT, "out", "ssh-" + tag, "shot.png"), "wb").write(p.stdout)
        px = png_pixels(p.stdout)
        checks["shot"] = p.returncode == 0 and px is not None and px[:2] == (640, 400) \
            and golden("shot-" + profile, px[2])
        rc, out, _ = ssh("glos log")
        checks["log"] = rc == 0 and "GLOS-SSH exec=\"glos shot\"" in out and "GLOS-VM agent comspec=" in out
        rc, out, _ = ssh("glos ps")
        checks["ps"] = rc == 0 and "thread vm normal" in out and "dos psp=" in out
        slow = subprocess.Popen(opts + ["glos@127.0.0.1", "WAITSEC 120"], stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        t2 = time.time()
        while time.time() - t2 < 120 and 'cmd="WAITSEC 120"' not in open(serial, "rb").read().decode("latin-1"):
            time.sleep(0.5)
        time.sleep(2)
        rc, _, _ = ssh("glos kill")
        checks["kill"] = rc == 0 and slow.wait(timeout=120) == 255
        stranger = os.path.join(tmp, "stranger")
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", stranger], check=True)
        rc, _, _ = ssh("glos ver", keyfile=stranger)
        checks["stranger-refused"] = rc == 255
        rc, _, _ = ssh("glos exit")
        checks["exit"] = rc == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    proc.wait()
    text = open(serial, "rb").read().decode("latin-1").replace("\r", "") if os.path.exists(serial) else ""
    checks["strict-kex"] = "GLOS-SSH kex done strict=1" in text
    checks["clean"] = "GLOS-PANIC" not in text and "GLOS-WARN" not in text and "GLOS-EXIT code=0" in text
    checks["done"] = "HX-DONE 0" in text and "HX-VECCHK ok" in text
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s%s" % ("ssh-" + tag, "PASS" if not bad else "FAIL", info,
                               "" if not bad else " failed: " + " ".join(bad)), not bad


def matrix(fn, combos, jobs):
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        results = list(ex.map(lambda pb: fn(*pb), combos))
    for r in results:
        print(r[0], flush=True)
    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("suite", choices=["m1", "refuse", "m2", "hostile", "sched", "mem", "shell", "net", "ssh", "dpmi",
                                      "djtst", "dpmitools", "hdpmireg", "ecm", "dpmi16", "tpx"])
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
    if a.suite == "ssh":
        return 0 if all(r[1] for r in matrix(ssh_case, SSH_CASES, a.jobs)) else 1
    if a.suite == "dpmi":
        res = matrix(dpmi, combos, a.jobs) + matrix(dpmiconf, combos, a.jobs)
        res += matrix(dpmi_hello, [(b,) for b in a.boot or BOOTS], a.jobs)
        return 0 if all(r[1] for r in res) else 1
    if a.suite == "tpx":
        return 0 if all(r[1] for r in matrix(tpx, combos, max(1, a.jobs // 2))) else 1
    if a.suite == "dpmi16":
        return 0 if all(r[1] for r in matrix(dpmiconf16, combos, max(1, a.jobs // 2))) else 1
    if a.suite == "djtst":
        return 0 if all(r[1] for r in matrix(djtst, combos, max(1, a.jobs // 2))) else 1
    if a.suite == "ecm":
        return 0 if all(r[1] for r in matrix(ecm, combos, max(1, a.jobs // 2))) else 1
    if a.suite == "hdpmireg":
        return 0 if all(r[1] for r in matrix(hdpmireg, combos, max(1, a.jobs // 2))) else 1
    if a.suite == "dpmitools":
        return 0 if all(r[1] for r in matrix(dpmi_tools, [(b,) for b in a.boot or BOOTS], a.jobs)) else 1
    if a.suite == "net":
        return 0 if all(r[1] for r in matrix(net, NET_CASES, a.jobs)) else 1
    if a.suite == "shell":
        return 0 if all(r[1] for r in matrix(shell, combos, a.jobs)) else 1
    if a.suite == "mem":
        return 0 if all(r[1] for r in matrix(mem, combos, a.jobs)) else 1
    if a.suite == "sched":
        return 0 if all(r[1] for r in matrix(lambda p, b: m2(p, b, True), combos, a.jobs)) else 1
    if a.suite == "hostile":
        return 0 if all(r[1] for r in matrix(hostile, combos, a.jobs)) else 1
    ok = True
    for p in a.profile or PROFILES:
        for b in a.boot or BOOTS:
            ok &= m1(p, b)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
