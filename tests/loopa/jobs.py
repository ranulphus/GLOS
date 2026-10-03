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

ssh: GLOS with the test keys (tests/keys) on bf6 + RTL8029 and 486DX2 + ISA
NE2000; from the host, through SLiRP's forward of port 22, OpenSSH's ssh runs
built-in commands (output and exit codes exact), three at once, and a
stranger's key is refused. The guest waits in KEYWAIT until the host's last
command ("glos echo ssh-done") shows on COM1 and the harness types Enter: a
fixed wait in guest time lost to run.py's wall-clock idle limit whenever
86Box ran slower than real time. The first connection's time is logged, not
judged (PRD D28).

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
        "--cmd", "SERSAY HX-START ssh", "--cmd", "C:\\TEST\\GLOS.EXE /RUN KEYWAIT 900", "--cmd", "SERSAY HX-DONE 0",
        "--keys", "@ssh-done,1:0x1c"],
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
        rc, out, err = ssh("nosuch")
        checks["unknown-127"] = rc == 127 and "no such command" in err
        ps = [subprocess.Popen(opts + ["glos@127.0.0.1", "glos echo par%d" % i], stdout=subprocess.PIPE)
              for i in range(3)]
        outs = [p.communicate(timeout=300)[0].decode() for p in ps]
        checks["three-at-once"] = sorted(outs) == ["par0\n", "par1\n", "par2\n"]
        stranger = os.path.join(tmp, "stranger")
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", stranger], check=True)
        rc, _, _ = ssh("glos ver", keyfile=stranger)
        checks["stranger-refused"] = rc == 255
        ssh("glos echo ssh-done")               # its exec line lets KEYWAIT (and GLOS) end
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    proc.wait()
    text = open(serial, "rb").read().decode("latin-1").replace("\r", "") if os.path.exists(serial) else ""
    checks["strict-kex"] = "GLOS-SSH kex done strict=1" in text
    checks["clean"] = "GLOS-PANIC" not in text and "GLOS-WARN" not in text and "GLOS-EXIT code=0" in text
    checks["key-ended"] = "HX-KEY scan=1c" in text
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
    ap.add_argument("suite", choices=["m1", "refuse", "m2", "hostile", "sched", "mem", "shell", "net", "ssh"])
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
