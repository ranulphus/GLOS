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
  jobs.py sess [--profile P ...] [--boot B ...] [-j N]   (on the host, as ssh)

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

sess: exclusive sessions (M4e E1). SESSTEST leaves behind video mode 13h,
IRQ5's PIC mask bit flipped, a DOS clock 30 s fast (the PIT sped up), Caps
Lock and INT 60h hooked into its own memory. Natively, VECCHK, VMODE and
TIMECHK afterwards must see every one of those (or the test proves nothing);
under GLOS /RUN, with GLOS as the shell (glosshell boots) and as an agent
job by ssh (bf6 + RTL8029, 486DX2 + NE2000), none, and GLOS's log must show
the session beginning and ending with the mode put back and the vector
taken out. Profiles (E2): with GLOS as the shell and a GLOS.CFG giving
PROFCHK.EXE a variable, a PATH of its own and a 4 MB cap, PROFCHK (DJGPP)
sees them and gets at most 4 MB from DPMI; the same program as NOPROF.EXE
sees the shell's environment and gets more than 8 MB. Direct mode (E3):
under direct profiles djtst205's ENABLE passes, DIRTEST runs at IOPL 3
with an IRQ 0 handler that EOIs the PIC itself and GLOS takes back its
keyboard line and RTC rate from it, and SESSTEST leaves nothing behind;
without a profile ENABLE still stops at its first check. Kills (E5): a
SESSTEST that hangs once it has made its changes, killed with the hotkey
inside a batch file's session, leaves nothing behind when the batch goes
on (the kill alone puts it back), and glos kill of one as an agent job is
reason=agent.

hostile: HOSRUN.BAT runs every tests/dos/hostile.c case under one "GLOS /RUN
COMMAND /C"; the harness types Ctrl-Alt-Shift-Esc after each one's "armed"
line (and Ctrl-Alt-Del first for CAD). Each must end in GLOS-KILL (PRIV by
GLOS itself) and be followed by a KEYWAIT that gets Enter, with the reset requests, the A20 wrap and the vif-stuck
warnings expected; then the batch, GLOS and the machine carry on: the
kernel's tick still advancing, VECCHK, WAITSEC (the BIOS tick), a key and
text mode, and DOS's clock within 2 s of the RTC (the session's end, M4e).
Exit status 1 if any job fails."""
import argparse
import concurrent.futures
import os
import re
import subprocess
import sys
import time

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
    if os.environ.get("GLOS_SET"):              # GLOS.EXE's flags for the whole run (GLOS_SET=/DIRECT: forced direct mode;
        args = ["--pre", "SET GLOS=" + os.environ["GLOS_SET"]] + args      # not as the shell, started before RUN.BAT)
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
# The same as a person types it, a third of a second apart: in direct mode the keys wait in the keyboard,
# which drops them when the BIOS sends it an LED update (as on silicon; supervisor.md §9.7).
KILL_KEYS_SPACED = "1:0x1d:down,1.3:0x38:down,1.6:0x2a:down,1.9:0x01,2.2:0x2a:up,2.5:0x38:up,2.8:0x1d:up"
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
                                        bat + "=/TEST/HOSRUN.BAT"] + M2_FILES + GLOS_FILES + SESS_FILES + [
        "--cmd", "SERSAY HX-START hostile", "--cmd", "VECCHK save",
        "--cmd", "C:\\TEST\\GLOS.EXE /RUN %COMSPEC% /C C:\\TEST\\HOSRUN.BAT", "--cmd", "VECCHK check",
        "--cmd", "WAITSEC 1", "--cmd", "SERSAY HX-HOS waited", "--cmd", "C:\\TEST\\TIMECHK.EXE",
        "--cmd", "KEYWAIT 20", "--cmd", "SERSAY HX-DONE 0",
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
        "reasons": serial.count(" reason=hotkey") == len(HOSTILE) - 1 and " reason=priv" in serial,     # (M4e)
        "tick-alive": bool(leave and kills and int(leave.group(1)) > kills[-1] + 800),
        "exit0": "GLOS-EXIT code=0" in serial,
        "no-panic": "GLOS-PANIC" not in serial,
        "vecchk": "HX-VECCHK ok" in serial,
        "bios-tick": "HX-HOS waited" in serial,
        "time": bool(re.search(r"HX-TIME rtc=\S+ dos=\S+ diff=[012] ", serial)),     # the session's end (M4e)
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


NET_CASES = [("bf6", "ne2kpci"), ("bf6", "ne2k"), ("486dx2", "ne2k"), ("486dx2", "ne2kpci"), ("486dx4", "ne2kpci"),
             ("bf6", "rtl8139c+"), ("bf6", "i82557"), ("bf6", "i82558"), ("486dx2", "rtl8139c+"), ("486dx4", "i82557")]
if os.environ.get("NET_ONLY"):                  # e.g. NET_ONLY=rtl8139c+,i82557
    NET_CASES = [c for c in NET_CASES if c[1] in os.environ["NET_ONLY"].split(",")]


def net(profile, card):
    """A card found and claimed, DHCP, a TCP echo from the host, and the mDNS responder answering for its name
    (GLOS asks it itself under /SELFTEST: SLiRP carries no multicast). On bf6 with the RTL8029 the name
    comes from GLOS.CFG's [net] section; elsewhere it is "glos"."""
    tag = "%s-%s" % (profile, card)
    line = "hello-glos-" + tag
    name = "bench-g450" if (profile, card) == ("bf6", "ne2kpci") else "glos"
    cfg = ["--file", batfile("net-%s/GLOS.CFG" % tag, ["[net]", "hostname = " + name]) + "=/TEST/GLOS.CFG"] \
        if name != "glos" else []
    st, serial = run("net-" + tag, ["--machine", profile, "--net", card, "--net-fwd", "7",
                                    "--tcp-send", "GLOS-NET dhcp|net:7|" + line] + GLOS_FILES + cfg + [
        "--cmd", "SERSAY HX-START net", "--cmd", "VECCHK save", "--cmd", "C:\\TEST\\GLOS.EXE /SELFTEST /RUN WAITSEC 12",
        "--cmd", "VECCHK check", "--cmd", "SERSAY HX-DONE 0"])
    want = {"ne2kpci": "rtl8029", "rtl8139c+": "rtl8139", "i82557": "i8255x", "i82558": "i8255x"}.get(card, "ne2000")
    echo = os.path.join(ROOT, "out", "net-" + tag, "tcp-0.txt")
    echo = open(echo, "rb").read().decode("latin-1") if os.path.exists(echo) else ""
    checks = {
        "status": st == "PASS" and "GLOS-EXIT code=0" in serial,
        "card": "GLOS-NET card=%s base=" % want in serial,
        "dhcp": "GLOS-NET dhcp ip=10.0.2.15 gw=10.0.2.2 mask=255.255.255.0" in serial,
        "echo": line in echo,
        "mdns": "GLOS-NET mdns name=%s.local\n" % name in serial
                and "GLOS-NET mdns answer=ok name=%s.local\n" % name in serial,
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
             else [("bf6", "ne2kpci"), ("486dx2", "ne2k"), ("bf6", "ne2k"), ("bf6", "rtl8139c+"), ("bf6", "i82557"),
                   ("bf6", "i82558"), ("486dx2", "rtl8139c+")])    # (M5 A6: the bus-master cards)
if os.environ.get("SSH_ONLY"):                  # e.g. SSH_ONLY=rtl8139c+,i82557: those cards' cases, no reboot ones
    SSH_CASES = [c for c in SSH_CASES if c[1] in os.environ["SSH_ONLY"].split(",")]
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


def lazy(profile, boot):
    """LAZYCHK (M4e): DPMI memory backed when first touched. Under GLOS 0501h hands over all of 0500h's largest
    block and then 8 MB more, pages read as zero when first touched and keep what is written (GLQuake's UNIX_SBRK
    heap), but not four times that block besides. CWSDPMI's line is shown, not judged."""
    tag = "%s-%s" % (profile, boot)
    common = ["--machine", profile, "--boot-cfg", boot, "--file", "build/dj/LAZYCHK.EXE=/TEST/LAZYCHK.EXE",
              "--timeout", "300", "--cmd", "SERSAY HX-START lazy"]
    end = ["--cmd", "SERSAY HX-DONE 0"]
    st0, cws = run("lazy-cws-" + tag, common + ["--cmd", "C:\\TEST\\LAZYCHK.EXE"] + end)
    st1, gl = run("lazy-glos-" + tag, common + GLOS_FILES + [
        "--cmd", "C:\\TEST\\GLOS.EXE /RUN C:\\TEST\\LAZYCHK.EXE"] + end)
    m = re.search(r"HX-LAZY largest=(\d+) first=(\d) second=(\d) zero=(\d) kept=(\d) huge=(\d)", gl)
    c = re.search(r"HX-LAZY .*", cws)
    checks = {"status": st1 == "PASS", "lazy": bool(m) and m.groups()[1:] == ("1", "1", "1", "1", "0")
              and int(m.group(1)) > 32768,
              "clean": "GLOS-PANIC" not in gl and "GLOS-CRASH" not in gl and "GLOS-WARN" not in gl}
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s%s" % ("lazy-" + tag, "PASS" if not bad else "FAIL",
                               " cwsdpmi: %s" % (c.group(0)[8:] if c else st0),
                               "" if not bad else " failed: " + " ".join(bad)), not bad


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
# first check, unless the run is in direct mode (IOPL 3, M4e: GLOS_SET=/DIRECT).
DJ_KNOWN = {"ENABLE": (1, "disable -> incorrect; expected 0")} if "/DIRECT" not in os.environ.get("GLOS_SET", "").upper() \
    else {}
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
    # Pages 0507h has just made memory of (they were a device's, 0508h), which nobody has touched since: GLOS says
    # not accessed, not written (19h). HDPMI says so with XMS, and accessed and written (79h) without, which is how
    # this suite's cases boot.
    "I310508A": "fresh-page-bits",
    "I3103002": "xms",          # the HDPMI run has no XMS; GLOS's server has no 4309h handle table
    "I31090X": "iopl0",         # PUSHF shows the real IF at IOPL 0 (D20)
    # 0305h: GLOS has no state to save (size 0), HDPMI 1Ch; RAWJMP5 then loops
    # once more inside RUNOUT's window.
    "RAWJMP1": "state-size", "RAWJMP2": "state-size", "RAWJMP3": "state-size", "RAWJMP5": "state-size",
    "RAWJMP6": "iopl0",
    "RMCB7": "rmcb-fs",         # FS in a callback's register structure: HDPMI passes a selector, GLOS 0
    "RMCB8": "nest-limit",      # GLOS fails the 17th nested real-mode call; HDPMI exits fatally at 39
}


# Tests whose own output may be damaged, on a boot without HIMEM. Their printf writes a number's digits through DS
# at an address it took from EBP: in an exception handler, on the host's locked stack, that is low memory at the
# stack's offset (3F5xh with GLOS's 16 KB stack; F5xh under HDPMI), and there FreeDOS has its disk buffers when it
# isn't loaded high. Which buffer holds the test's own output file then depends on what is where on the disk: when
# it is the one at that address, a few bytes of a line are the digits printed after them (M6 step H: the loader
# grew, the files moved, and both tests' lines changed). Nothing of GLOS's is wrong in either case.
HR_SCRIBBLE = {"I3102101", "I3102102"}


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
        elif n in HR_KNOWN or (n in HR_SCRIBBLE and g and h and g[1] == h[1] and len(g[0]) == len(h[0])):
            known.append(n)                     # (a scribbled one: the same exit code and as many lines)
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


def api_version():
    """GLOS_API_VERSION as include/glos/api.h has it now ((major << 16) | minor)."""
    src = open(os.path.join(ROOT, "include", "glos", "api.h")).read()
    major = int(re.search(r"#define GLOS_API_MAJOR\s+(\d+)", src).group(1))
    minor = int(re.search(r"#define GLOS_API_MINOR\s+(\d+)", src).group(1))
    return major << 16 | minor


def golden(name, value):
    """value against tests/loopa/golden.txt; GLOS_GOLDEN=update records it instead."""
    lines = dict(l.split() for l in open(GOLDEN) if l.strip()) if os.path.exists(GOLDEN) else {}
    if os.environ.get("GLOS_GOLDEN") == "update":
        lines[name] = value
        with open(GOLDEN, "w") as f:
            f.writelines("%s %s\n" % kv for kv in sorted(lines.items()))
        return True
    return lines.get(name) == value


class Agent:
    """GLOS headless (the agent) in a background Loop A run with the test keys,
    and OpenSSH's ssh to it through SLiRP's forward of port 22 (on the host)."""

    def __init__(self, name, profile, card, args, timeout=600):
        import socket
        import tempfile
        s = socket.socket()
        s.bind(("0.0.0.0", 0))
        self.port = s.getsockname()[1]
        s.close()
        self.proc = run(name, os.environ.get("SSH_EXTRA", "").split() + [
            "--machine", profile, "--net", card, "--net-fwd", "%d:22" % self.port, "--timeout", str(timeout),
            "--idle", "300"] + GLOS_FILES + KEYS + args, background=True)
        self.serial = os.path.join(ROOT, "out", name, "serial.log")
        self.tmp = tempfile.mkdtemp()
        self.key, known = os.path.join(self.tmp, "client"), os.path.join(self.tmp, "known")
        open(self.key, "w").write(open(os.path.join(ROOT, "tests/keys/client")).read())
        os.chmod(self.key, 0o600)
        open(known, "w").write("[127.0.0.1]:%d %s" % (self.port,
                                                      open(os.path.join(ROOT, "tests/keys/hostkey.pub")).read()))
        self.base = ["ssh", "-p", str(self.port), "-o", "IdentitiesOnly=yes", "-o", "BatchMode=yes",
                     "-o", "StrictHostKeyChecking=yes", "-o", "UserKnownHostsFile=" + known,
                     "-o", "ConnectTimeout=60", "-o", "LogLevel=ERROR"]
        self.opts = self.base + ["-i", self.key]

    def log(self):
        return open(self.serial, "rb").read().decode("latin-1").replace("\r", "") \
            if os.path.exists(self.serial) else ""

    def listening(self, limit=240, after=None):
        """GLOS up with its network and listening (after the line `after`, for a second start)."""
        import time
        t0 = time.time()
        while time.time() - t0 < limit:
            text = self.log()
            if after:
                text = text[text.index(after):] if after in text else ""
            if "GLOS-NET dhcp" in text and "GLOS-SSH listen" in text:
                return True
            time.sleep(1)
        return False

    def ssh(self, cmd, keyfile=None, timeout=180):
        p = subprocess.run((self.base + ["-i", keyfile] if keyfile else self.opts) + ["glos@127.0.0.1", cmd],
                           capture_output=True, timeout=timeout)
        return p.returncode, p.stdout.decode("latin-1"), p.stderr.decode("latin-1")

    def wait(self):
        self.proc.wait()
        return self.log()


def reboot_case(profile, card):
    """glos reboot (M5 A5): refused while a job runs, then the machine resets, comes up and starts GLOS again
    (Loop A's boot runs the same RUN.BAT), which answers and leaves."""
    import time
    tag = "reboot-%s-%s" % (profile, card)
    agent = Agent("ssh-" + tag, profile, card, ["--cmd", "SERSAY HX-START reboot", "--cmd", "C:\\TEST\\GLOS.EXE",
                                                "--cmd", "SERSAY HX-DONE 0"], timeout=900)
    checks = {"listen": agent.listening()}
    try:
        slow = subprocess.Popen(agent.opts + ["glos@127.0.0.1", "WAITSEC 100"], stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        t0 = time.time()
        while time.time() - t0 < 120 and 'cmd="WAITSEC 100"' not in agent.log():
            time.sleep(0.5)
        rc, _, err = agent.ssh("glos reboot")
        checks["refused"] = rc == 1 and "a job is running" in err
        checks["kill"] = agent.ssh("glos kill")[0] == 0 and slow.wait(timeout=120) == 255
        checks["reboot"] = agent.ssh("glos reboot")[0] == 0
        t0 = time.time()
        while time.time() - t0 < 400 and agent.log().count("GLOS-SSH listen") < 2:
            time.sleep(1)
        while time.time() - t0 < 400 and agent.log().count("GLOS-NET dhcp") < 2:
            time.sleep(1)
        rc, out, _ = agent.ssh("glos ver")
        checks["again"] = rc == 0 and out.startswith("GLOS ")
        checks["exit"] = agent.ssh("glos exit")[0] == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    text = agent.wait()
    checks["line"] = "GLOS-REBOOT\n" in text and text.count("HX-START reboot") == 2
    checks["clean"] = "GLOS-PANIC" not in text and "GLOS-WARN" not in text
    checks["done"] = "GLOS-EXIT code=0" in text and "HX-DONE 0" in text
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s" % ("ssh-" + tag, "PASS" if not bad else "FAIL", "" if not bad else " failed: " + " ".join(bad)), \
        not bad


def ssh_case(profile, card):
    import time
    tag = "%s-%s%s" % (profile, card, os.environ.get("SSH_TAG", ""))
    agent = Agent("ssh-" + tag, profile, card, [
        "--file", "build/ow/dos/ECHOARGS.EXE=/TEST/ECHOARGS.EXE",
        "--cmd", "SERSAY HX-START ssh", "--cmd", "VECCHK save", "--cmd", "C:\\TEST\\GLOS.EXE",
        "--cmd", "VECCHK check", "--cmd", "SERSAY HX-DONE 0"], timeout=1500)
    serial, tmp, key, port, base, opts, ssh = (agent.serial, agent.tmp, agent.key, agent.port, agent.base,
                                               agent.opts, agent.ssh)
    checks, info = {}, ""
    agent.listening()
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
        # Telemetry (M5 A5). glos stat: a line of JSON, its counters moving; with --follow a line a second,
        # and the log's --follow shows a command as it comes; glos mem and glos net.
        import json

        def stat():
            rc, out, _ = ssh("glos stat")
            try:
                return json.loads(out) if rc == 0 else None
            except ValueError:
                return None
        s1, s2 = stat(), stat()
        checks["stat"] = bool(s1 and s2 and s2["ticks"] > s1["ticks"] and s1["hz"] == 1024
                              and {"vm", "ssh", "net"} <= set(s1["threads"]) and s1["mem"]["free_kb"] > 0
                              and len(s1["irq"]) == 16 and s2["irq"][8] > s1["irq"][8] and s2["net"]["rx"] > s1["net"]["rx"]
                              and s1["session"] is None and s1["dpmi"] is None)
        if not checks["stat"] or os.environ.get("SSH_SHOW"):
            info += " stat=%r" % (s1,)
        fstat = subprocess.Popen(opts + ["glos@127.0.0.1", "glos stat --follow"], stdout=subprocess.PIPE,
                                 stderr=subprocess.DEVNULL)
        flog = subprocess.Popen(opts + ["glos@127.0.0.1", "glos log --follow"], stdout=subprocess.PIPE,
                                stderr=subprocess.DEVNULL)
        t2 = time.time()                        # (both are in once the log shows them)
        while time.time() - t2 < 120 and 'exec="glos log --follow"' not in agent.log():
            time.sleep(0.5)
        ssh("glos echo marker-" + tag)
        ssh("glos echo marker2-" + tag)
        time.sleep(3)
        fstat.terminate()
        flog.terminate()
        lines = fstat.communicate(timeout=60)[0].decode("latin-1").splitlines()
        followed = flog.communicate(timeout=60)[0].decode("latin-1")
        try:
            ticks = [json.loads(l)["ticks"] for l in lines]
        except ValueError:
            ticks = []
        # A second apart, or more: the ssh thread is a bulk one, and on a 486 a key exchange beside it (the
        # commands below) has held a line back by half a second.
        checks["stat-follow"] = len(ticks) >= 2 and all(900 <= b - a <= 3000 for a, b in zip(ticks, ticks[1:]))
        checks["log-follow"] = ("GLOS-VM agent comspec=" in followed          # the log so far, then as it grows
                                and 'GLOS-SSH exec="glos echo marker2-%s"' % tag in followed)
        if not (checks["stat-follow"] and checks["log-follow"]):
            info += " follow=%r/%d" % (ticks, len(followed))
        rc, out, _ = ssh("glos mem")
        checks["mem"] = rc == 0 and bool(re.search(r"memory total_kb=\d+ free_kb=\d+\nkernel image_kb=\d+ ", out)) \
            and "dpmi null" in out
        rc, out, _ = ssh("glos net")
        checks["net"] = rc == 0 and "ip=10.0.2.15 mask=255.255.255.0 gw=10.0.2.2" in out and "card=" in out
        # glos kill SEQ: a job that waits its turn is taken out, then the one that runs is killed; and the
        # session shows in glos stat while it does.
        slow = subprocess.Popen(opts + ["glos@127.0.0.1", "WAITSEC 100"], stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        t2 = time.time()
        while time.time() - t2 < 120 and 'cmd="WAITSEC 100"' not in agent.log():
            time.sleep(0.5)
        waiting = subprocess.Popen(opts + ["glos@127.0.0.1", "echoargs waiting"], stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL)
        t2, seqs = time.time(), {}
        while time.time() - t2 < 120 and len(seqs) < 2:
            seqs = {m[1]: int(m[0]) for m in re.findall(r"^job (\d+) (queued|running) ", ssh("glos ps")[1], re.M)}
        s3 = stat()
        checks["stat-session"] = bool(s3 and s3["session"] and s3["session"]["program"].startswith("WAITSEC"))
        checks["kill-queued"] = len(seqs) == 2 and ssh("glos kill %d" % seqs["queued"])[0] == 0 \
            and waiting.wait(timeout=120) == 255
        checks["kill-seq"] = len(seqs) == 2 and ssh("glos kill %d" % seqs["running"])[0] == 0 \
            and slow.wait(timeout=120) == 255
        rc, _, err = ssh("glos kill 9999")
        checks["kill-none"] = rc == 1 and "no such job" in err
        stranger = os.path.join(tmp, "stranger")
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", stranger], check=True)
        rc, _, _ = ssh("glos ver", keyfile=stranger)
        checks["stranger-refused"] = rc == 255
        rc, _, _ = ssh("glos exit")
        checks["exit"] = rc == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    text = agent.wait()
    checks["strict-kex"] = "GLOS-SSH kex done strict=1" in text
    checks["clean"] = "GLOS-PANIC" not in text and "GLOS-WARN" not in text and "GLOS-EXIT code=0" in text
    checks["done"] = "HX-DONE 0" in text and "HX-VECCHK ok" in text
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s%s" % ("ssh-" + tag, "PASS" if not bad else "FAIL", info,
                               "" if not bad else " failed: " + " ".join(bad)), not bad


SESS_FILES = ["--file", "build/ow/dos/SESSTEST.EXE=/TEST/SESSTEST.EXE",
              "--file", "build/ow/dos/TIMECHK.EXE=/TEST/TIMECHK.EXE"]
SESS_STEPS = ["C:\\TEST\\TIMECHK.EXE", "VECCHK save", "C:\\TEST\\SESSTEST.EXE", "VECCHK check", "VMODE",
              "C:\\TEST\\TIMECHK.EXE"]
SESS_AGENT = [("bf6", "ne2kpci"), ("486dx2", "ne2k")]


def sess_left(text):
    """What SESSTEST changed and left changed, from the HX lines around it: each of
    the PIC mask (VECCHK), the video mode, the DOS clock, INT 60h and the lock
    bits, True when it is as before."""
    a = text.find("HX-SESS done")
    before = re.search(r"HX-TIME rtc=\S+ dos=\S+ diff=(\d+) vec60=(\w+) kbd=(\w+)", text[:a] if a >= 0 else "")
    after = re.search(r"HX-TIME rtc=\S+ dos=\S+ diff=(\d+) vec60=(\w+) kbd=(\w+)", text[a:] if a >= 0 else "")
    if not before or not after:
        return None
    return {"pic": "HX-VECCHK ok" in text[a:], "mode": "HX-VMODE bios=03" in text[a:],
            "clock": int(after.group(1)) <= 2, "int60": after.group(2) == before.group(2),
            "locks": after.group(3) == before.group(3)}


def sess_checks(text, prog="SESSTEST.EXE"):
    """GLOS's own account of the session: it began and ended, with the mode put back and INT 60h's hook
    taken out."""
    end = re.search(r"GLOS-SESSION end n=\d+ prog=%s why=(\w+) mode=13 remode=1 vectors=(\d+)" % re.escape(prog),
                    text)
    return {"begin": "GLOS-SESSION begin n=" in text and " prog=%s " % prog in text,
            "end": bool(end and int(end.group(2)) >= 1),
            "no-panic": "GLOS-PANIC" not in text and "GLOS-WARN" not in text}


def sess(profile, boot, how):
    """SESSTEST natively (base: every change must show), under GLOS /RUN and
    with GLOS as the shell (none may)."""
    tag = "%s-%s-%s" % (how, profile, boot)
    cfg = boot if how != "shell" else "glosshell" + ("-himemx" if boot == "himemx" else "")
    steps = list(SESS_STEPS)
    if how == "run":
        steps[2] = "C:\\TEST\\GLOS.EXE /RUN " + steps[2]
    args = ["--machine", profile, "--boot-cfg", cfg] + SESS_FILES + GLOS_FILES
    for c in ["SERSAY HX-START sess"] + steps + ["SERSAY HX-DONE 0"]:
        args += ["--cmd", c]
    st, serial = run("sess-" + tag, args)
    left = sess_left(serial)
    checks = {"status": st == "PASS", "ran": "HX-SESS done" in serial and left is not None}
    if how == "base":                           # the test changes each of these
        checks.update({"changed-" + k: not v for k, v in (left or {}).items()})
    else:
        checks.update({"kept-" + k: v for k, v in (left or {}).items()})
        checks.update(sess_checks(serial))
        if how == "run":
            checks["exit0"] = "GLOS-EXIT code=0" in serial
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s" % ("sess-" + tag, "PASS" if not bad else "FAIL",
                             "" if not bad else " failed: " + " ".join(bad)), not bad


PROF_CFG = ["; profiles, for jobs.py sess", "[program PROFCHK.EXE]", "env = GLOSPROF=yes", "env = PATH=C:\\PROF",
            "memory = 4096"]


def sess_prof(profile, boot):
    """Profiles (E2), with GLOS as the shell: PROFCHK gets its profile's environment (a variable added and PATH
    replaced) and a DPMI context capped at 4 MB; the same program as NOPROF.EXE gets neither."""
    tag = "prof-%s-%s" % (profile, boot)
    cfg = batfile("sess-%s/GLOS.CFG" % tag, PROF_CFG)
    args = ["--machine", profile, "--boot-cfg", "glosshell" + ("-himemx" if boot == "himemx" else "")] + GLOS_FILES + [
        "--file", cfg + "=/TEST/GLOS.CFG", "--file", "build/dj/PROFCHK.EXE=/TEST/PROFCHK.EXE",
        "--file", "build/dj/PROFCHK.EXE=/TEST/NOPROF.EXE"]
    for c in ["SERSAY HX-START sess-prof", "C:\\TEST\\PROFCHK.EXE", "C:\\TEST\\NOPROF.EXE", "SERSAY HX-DONE 0"]:
        args += ["--cmd", c]
    st, serial = run("sess-" + tag, args)
    lines = re.findall(r"HX-PROF var=(\S+) path=(\S*) largest=(\d+) got=(\d+)", serial)
    checks = {"status": st == "PASS", "ran": len(lines) == 2,
              "profile": bool(re.search(r"GLOS-SESSION begin n=\d+ prog=PROFCHK.EXE .* profile=PROFCHK.EXE", serial)),
              "clean": "GLOS-PANIC" not in serial and "GLOS-WARN" not in serial}
    info = ""
    if len(lines) == 2:
        (v1, p1, l1, g1), (v2, p2, l2, g2) = lines
        checks["env"] = v1 == "yes" and p1 == "C:\\PROF"
        checks["memory"] = int(l1) <= 4096 and 2048 <= int(g1) <= 4096
        checks["none"] = v2 == "-" and p2 != "C:\\PROF" and int(g2) > 8192
        info = " got=%s/%s" % (g1, g2)
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s%s" % ("sess-" + tag, "PASS" if not bad else "FAIL", info,
                               "" if not bad else " failed: " + " ".join(bad)), not bad


DIRECT_CFG = ["; direct mode, for jobs.py sess", "[program ENABLE.EXE]", "direct = 1", "[program DIRTEST.EXE]",
              "direct = 1", "[program SESSTEST.EXE]", "direct = 1"]
ENABLE_OK = ["1(=1?) -> enable  -> 1(=1?)", "1(=1?) -> disable -> 0(=0?)", "0(=0?) -> disable -> 0(=0?)",
             "0(=0?) -> enable  -> 1(=1?)", "1(=1?) -> enable  -> 1(=1?)", "1(=1?) -> disable -> 0(=0?)",
             "0(=0?) -> enable  -> 1(=1?)"]


def sess_direct(profile, boot):
    """Direct mode (E3), with GLOS as the shell: djtst205's ENABLE (PUSHF after
    CLI) passes under a direct profile and, as ENABLE0.EXE, still stops at its
    first check without one; DIRTEST sees IOPL 3, keeps its IRQ 0 handler
    that EOIs the PIC itself, and loses its writes to GLOS's keyboard line
    and RTC rate (DIRTEST0.EXE, without a profile, at IOPL 0); its own IRQ 1
    handler, reading the 8042 itself, gets a tapped key's bytes (both ways);
    SESSTEST at IOPL 3 leaves nothing behind, and is killed with the hotkey
    there too (typed as a person would: supervisor.md §9.7)."""
    tag = "direct-%s-%s" % (profile, boot)
    cfg = batfile("sess-%s/GLOS.CFG" % tag, DIRECT_CFG)
    args = ["--machine", profile, "--boot-cfg", "glosshell" + ("-himemx" if boot == "himemx" else "")] + GLOS_FILES + \
        SESS_FILES + ["--file", cfg + "=/TEST/GLOS.CFG"]
    for f in ("ENABLE", "ENABLE0"):
        args += ["--file", "build/dj/djtst/ENABLE.EXE=/TEST/%s.EXE" % f]
    for f in ("DIRTEST", "DIRTEST0"):
        args += ["--file", "build/dj/DIRTEST.EXE=/TEST/%s.EXE" % f]
    cmds = ["SERSAY HX-START sess-direct", "C:\\TEST\\ENABLE.EXE > C:\\OUT\\ENABLE.TXT",
            "IF NOT ERRORLEVEL 1 SERSAY HX-ENABLE code=0", "C:\\TEST\\ENABLE0.EXE > C:\\OUT\\ENABLE0.TXT",
            "IF ERRORLEVEL 1 SERSAY HX-ENABLE0 code=1", "SERSAY HX-DIRTEST", "C:\\TEST\\DIRTEST.EXE",
            "SERSAY HX-DIRTEST0", "C:\\TEST\\DIRTEST0.EXE", "SERSAY HX-SESSTEST"] + SESS_STEPS + [
            "SERSAY HX-KILL", "C:\\TEST\\SESSTEST.EXE hang", "VMODE", "SERSAY HX-DONE 0"]
    for c in cmds:
        args += ["--cmd", c]
    st, serial = run("sess-" + tag, args + ["--keys", "@HX-DIRECT armed-key,1:0x1c,@HX-DIRECT armed-key,1:0x1c,"
                                            "@HX-SESS armed," + KILL_KEYS_SPACED])
    files = os.path.join(ROOT, "out", "sess-" + tag, "files")

    def read(n):
        p = os.path.join(files, n)
        return open(p, "rb").read().decode("latin-1").replace("\r", "").splitlines() if os.path.exists(p) else []

    def dirtest(part):
        return dict(kv for l in part.splitlines() if l.startswith("HX-DIRECT ") for kv in re.findall(r"(\w+)=(\w+)", l))
    d = serial.split("HX-DIRTEST0")
    d1, d0 = (dirtest(d[0].split("HX-DIRTEST")[-1]), dirtest(d[1].split("HX-SESSTEST")[0])) if len(d) == 2 else ({}, {})
    sess_part = serial[serial.find("HX-SESSTEST"):serial.find("HX-KILL")]
    kill_part = serial[serial.find("HX-KILL"):]
    left = sess_left(sess_part)
    checks = {
        "status": st == "PASS",
        "enable": read("ENABLE.TXT") == ENABLE_OK and "HX-ENABLE code=0" in serial,
        "enable0": "HX-ENABLE0 code=1" in serial and any("incorrect" in l for l in read("ENABLE0.TXT")),
        "direct": d1.get("iopl") == "3" and int(d1.get("n", 0)) >= 10 and d1.get("irq5") == "1"
                  and d1.get("irq1") == "0" and d1.get("a") == "26",
        "direct-keys": d1.get("keys") == "1c9c",
        "control": d0.get("iopl") == "0" and int(d0.get("n", 0)) >= 10 and d0.get("keys") == "1c9c",
        "sesstest": left is not None and all(left.values()) and "direct=1" in sess_part
                    and all(sess_checks(sess_part).values()),
        "direct-kill": bool(re.search(r"GLOS-KILL psp=\w+ at=\S+ ticks=\d+ reason=hotkey", kill_part))
                       and "GLOS-SESSION kill-restore mode=03 remode=1" in kill_part and "HX-VMODE bios=03" in kill_part,
        "direct-ends": serial.count("GLOS-SESSION direct-end") == 4,
        "clean": "GLOS-PANIC" not in serial and "GLOS-WARN" not in serial,
    }
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s" % ("sess-" + tag, "PASS" if not bad else "FAIL",
                             "" if not bad else " failed: " + " ".join(bad) + " direct=%s control=%s" % (d1, d0)), not bad


def sess_kill(profile, boot):
    """Kills (E5): SESSTEST hang under GLOS /RUN COMMAND /C, killed with the hotkey once armed; the batch goes on
    in the same session, and VECCHK, VMODE and TIMECHK must find everything put back by the kill alone."""
    tag = "kill-%s-%s" % (profile, boot)
    steps = list(SESS_STEPS)
    steps[2] = "C:\\TEST\\SESSTEST.EXE hang"
    bat = batfile("sess-%s/KILL.BAT" % tag, ["@ECHO OFF"] + steps + ["SERSAY HX-KILLBAT end"])
    args = ["--machine", profile, "--boot-cfg", boot] + SESS_FILES + GLOS_FILES + ["--file", bat + "=/TEST/KILL.BAT"]
    for c in ["SERSAY HX-START sess-kill", "C:\\TEST\\GLOS.EXE /RUN %COMSPEC% /C C:\\TEST\\KILL.BAT",
              "SERSAY HX-DONE 0"]:
        args += ["--cmd", c]
    st, serial = run("sess-" + tag, args + ["--keys", "@HX-SESS armed," + KILL_KEYS])
    a = serial.find("HX-SESS armed")
    left = sess_left(serial[:serial.find("HX-KILLBAT end")] if "HX-KILLBAT end" in serial else "")
    checks = {"status": st == "PASS", "ran": a >= 0 and left is not None,
              "kill": bool(re.search(r"GLOS-KILL psp=\w+ at=\S+ ticks=\d+ reason=hotkey", serial[a:])),
              "restore": "GLOS-SESSION kill-restore mode=03 remode=1" in serial,
              "batch-on": "HX-KILLBAT end" in serial and "GLOS-EXIT code=0" in serial,
              "clean": "GLOS-PANIC" not in serial and "GLOS-WARN" not in serial}
    checks.update({"kept-" + k: v for k, v in (left or {}).items()})
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s" % ("sess-" + tag, "PASS" if not bad else "FAIL",
                             "" if not bad else " failed: " + " ".join(bad)), not bad


def sess_agent(profile, card):
    """SESSTEST and the checks as agent jobs, through ssh: each job is a session (the agent EXECs a program
    directly, trying each place along PATH; only the one found begins a session). Then glos run (E4):
    --direct runs ENABLE at IOPL 3, --profile gives NOPROF.EXE PROFCHK's profile, --exclusive and --app
    run it as plain does, and an unknown profile and no command are refused with status 2."""
    tag = "agent-%s-%s" % (profile, card)
    cfg = batfile("sess-%s/GLOS.CFG" % tag, PROF_CFG)
    agent = Agent("sess-" + tag, profile, card, SESS_FILES + [
        "--file", cfg + "=/TEST/GLOS.CFG", "--file", "build/dj/djtst/ENABLE.EXE=/TEST/ENABLE.EXE",
        "--file", "build/dj/PROFCHK.EXE=/TEST/NOPROF.EXE",
        "--cmd", "SERSAY HX-START sess", "--cmd", "VECCHK save", "--cmd", "C:\\TEST\\GLOS.EXE",
        "--cmd", "VECCHK check", "--cmd", "SERSAY HX-DONE 0"])
    checks = {"listen": agent.listening()}
    try:
        rcs = [agent.ssh(c)[0] for c in SESS_STEPS]
        checks["jobs"] = rcs == [0] * len(SESS_STEPS)
        rc, out, _ = agent.ssh("glos run --direct C:\\TEST\\ENABLE.EXE")
        checks["run-direct"] = rc == 0 and out.replace("\r", "").splitlines() == ENABLE_OK
        if not checks["run-direct"]:
            print("    run --direct: rc=%d out=%r" % (rc, out[:300]))
        rc, out, _ = agent.ssh("glos run --exclusive C:\\TEST\\ENABLE.EXE")
        checks["run-plain"] = rc == 1 and "incorrect" in out
        checks["run-profile"] = agent.ssh("glos run --profile profchk C:\\TEST\\NOPROF.EXE")[0] == 0
        rc, out, _ = agent.ssh("glos run --app C:\\TEST\\ENABLE.EXE")        # (M6: as without it)
        checks["run-app"] = rc == 1 and "incorrect" in out
        rc, _, err = agent.ssh("glos run --profile NOSUCH.EXE C:\\TEST\\ENABLE.EXE")
        checks["run-noprofile"] = rc == 2 and "NOSUCH.EXE" in err
        checks["run-usage"] = agent.ssh("glos run --direct")[0] == 2
        import time
        hang = subprocess.Popen(agent.opts + ["glos@127.0.0.1", "C:\\TEST\\SESSTEST.EXE hang"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        t0 = time.time()
        while time.time() - t0 < 120 and "HX-SESS armed" not in agent.log():
            time.sleep(0.5)
        checks["kill"] = agent.ssh("glos kill")[0] == 0 and hang.wait(timeout=120) == 255
        checks["exit"] = agent.ssh("glos exit")[0] == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    text = agent.wait()
    left = sess_left(text)
    checks["ran"] = "HX-SESS done" in text and left is not None
    checks.update({"kept-" + k: v for k, v in (left or {}).items()})
    checks.update(sess_checks(text))
    checks["one-each"] = len(re.findall(r"GLOS-SESSION begin ", text)) == len(SESS_STEPS) + 5   # no failed EXECs
    checks["kill-reason"] = bool(re.search(r"GLOS-KILL psp=\w+ at=\S+ ticks=\d+ reason=agent", text))
    checks["run-sessions"] = bool(re.search(r"GLOS-SESSION begin n=\d+ prog=ENABLE.EXE \S+ mode=03 direct=1", text)) \
        and bool(re.search(r"GLOS-SESSION begin n=\d+ prog=NOPROF.EXE \S+ mode=03 profile=PROFCHK.EXE\n", text)) \
        and "HX-PROF var=yes path=C:\\PROF" in text and text.count("GLOS-SESSION direct-end") == 1
    checks["done"] = "GLOS-EXIT code=0" in text and "HX-DONE 0" in text and text.count("HX-VECCHK ok") == 2
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s" % ("sess-" + tag, "PASS" if not bad else "FAIL",
                             "" if not bad else " failed: " + " ".join(bad)), not bad


# ---- glos shot in every video mode (M5 A2)

SHOT_MODES = ["text", "textb", "text50", "cga4", "cga2", "ega0d", "ega0e", "ega10", "vga12", "vga13", "vga13c", "x",
              "xpage",
              "xsplit", "v101", "v101l", "v101s", "v101d", "v103", "v102", "v105", "v110", "v110l", "v111", "v111l",
              "v112", "v112l", "v118l", "v111p", "v111z"]
# (machine, video card, network card, the program: SHOTTEST in real mode, SHOTDJ a DPMI client)
SHOT_CASES = [("bf6", "g200", "ne2kpci", "SHOTTEST"), ("bf6", "vbe", "ne2kpci", "SHOTTEST"),
              ("486dx2", "vbe", "ne2k", "SHOTTEST"), ("bf6", "g200", "ne2kpci", "SHOTDJ"),
              ("bf6", "vbe", "ne2kpci", "SHOTDJ")]


def caps_of(text):
    """The capability bits an app's "caps=X" line says, in hex (0 if none): tests check their own bits, as API 1.3
    added GLOS_CAP_NET to every run with a network."""
    m = re.search(r"caps=([0-9a-fA-F]+)\b", text)
    return int(m.group(1), 16) if m else 0


def png_rgb(data):
    """(width, height, RGB bytes) of one of glos shot's PNGs (indexed or RGB, no scanline filter), or None."""
    import struct
    import zlib
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    p, idat, pal, head = 8, b"", b"", None
    while p + 12 <= len(data):
        n = struct.unpack(">I", data[p:p + 4])[0]
        kind, body = data[p + 4:p + 8], data[p + 8:p + 8 + n]
        if zlib.crc32(kind + body) != struct.unpack(">I", data[p + 8 + n:p + 12 + n])[0]:
            return None
        if kind == b"IHDR":
            head = struct.unpack(">IIBB", body[:10])
        elif kind == b"PLTE":
            pal = body
        elif kind == b"IDAT":
            idat += body
        p += 12 + n
    if not head or p != len(data):
        return None
    w, h, depth, ctype = head
    try:
        raw = zlib.decompress(idat)
    except zlib.error:
        return None
    row = 1 + (w * 3 if ctype == 2 else w if depth == 8 else (w + 1) // 2)
    if len(raw) != row * h or any(raw[y * row] for y in range(h)):
        return None
    colours = [pal[i:i + 3] for i in range(0, len(pal), 3)]
    out = bytearray()
    for y in range(h):
        line = raw[y * row + 1:(y + 1) * row]
        if ctype == 2:
            out += line
        elif depth == 8:
            out += b"".join(colours[i] for i in line)
        else:
            out += b"".join(colours[b >> 4] + colours[b & 15] for b in line)[:w * 3]
    return w, h, bytes(out)


def shot_want(f):
    """The picture SHOTTEST drew, from its HX-SHOT line's fields (tests/dos/shottest.c): RGB bytes."""
    w, h, kind = int(f["w"]), int(f["h"]), f["kind"]

    def six(v):
        return ((v & 63) << 2) | ((v & 63) >> 4)

    def dac(i, wide=False):
        return bytes((i, (5 * i + 9) & 255, 255 - i)) if wide else bytes((six(i), six(5 * i + 9), six((255 - i) >> 2)))

    def pat(x, y):
        return (x + 3 * y + 5 * ((x >> 4) ^ (y >> 3))) & 255

    out = bytearray()
    if kind == "pal256":
        table, split = [dac(i, f.get("wide") == "1") for i in range(256)], int(f.get("split", h))
        for y in range(h):
            if y < split:
                out += b"".join(table[pat(x, y)] for x in range(w))
            else:
                out += b"".join(table[pat(x, y - split) ^ 255] for x in range(w))
    elif kind == "pal16":
        table, n = [dac(0x50 + 3 * v) for v in range(16)], int(f["colours"])
        for y in range(h):
            if n == 2:
                out += b"".join(table[(pat(x, y) >> 1) & 1] for x in range(w))
            else:
                out += b"".join(table[pat(x, y) & (n - 1)] for x in range(w))
    elif kind == "text":
        table, rows, blink = [dac(0x50 + 3 * v) for v in range(16)], int(f["rows"]), f["blink"] == "1"
        for cy in range(rows):
            line = b""
            for cx in range(80):
                a = (7 * cx + 13 * cy) & 255
                line += table[a & 15 if (cx + cy) & 1 else (a >> 4) & 7 if blink else a >> 4] * 8
            out += line * (h // rows)
    elif kind == "rgb":
        def cut(v, size):
            c, bits = (v >> (8 - size)) << (8 - size), size
            while bits < 8:
                c |= c >> bits
                bits *= 2
            return c & 255
        rs, gs, bs = (int(f[k].split("/")[0]) for k in "rgb")
        red, green, blue = ([cut(v, s) for v in range(256)] for s in (rs, gs, bs))
        sx, sy = 37 * int(f.get("shift", 0)), 11 * int(f.get("shift", 0))      # (the exit job's later pictures)
        for y in range(h):
            v = (y + sy) & 255
            out += b"".join(bytes((red[(x + sx) & 255], green[v], blue[((x + sx) ^ (y + sy)) & 255]))
                            for x in range(w))
    return bytes(out)


def shot_case(profile, vcard, card, prog):
    """SHOTTEST's picture in each video mode against glos shot's, as a PNG and raw, over ssh while the program
    waits; then the program is killed, and the session's end puts text mode back. A mode the card's BIOS hasn't
    is skipped, but each kind of picture must have been taken. Real mode (SHOTTEST) goes through the V86
    monitor's INT 10h, a DPMI client (SHOTDJ) through the host's real-mode calls. vga13c keeps its
    interrupts off for five seconds and calls no one: its picture must come while it does (GLOS waits a
    quarter of a second for a program to take interrupts, then stops it where it is)."""
    import time
    tag = "%s-%s-%s" % (prog.lower(), profile, vcard)
    exe = "build/ow/dos/SHOTTEST.EXE" if prog == "SHOTTEST" else "build/dj/SHOTDJ.EXE"
    files = ["--file", "%s=/TEST/%s.EXE" % (exe, prog)]
    agent = Agent("shot-" + tag, profile, card, ["--card", vcard] + files + [
        "--cmd", "SERSAY HX-START shot", "--cmd", "VECCHK save", "--cmd", "C:\\TEST\\GLOS.EXE",
        "--cmd", "VECCHK check", "--cmd", "SERSAY HX-DONE 0"], timeout=2400 if profile == "bf6" else 4800)
    out = os.path.join(ROOT, "out", "shot-" + tag)
    checks, info, taken, skipped = {"listen": agent.listening()}, "", set(), []
    # One connection for every command (OpenSSH's ControlMaster): a key exchange takes a 486 seconds.
    agent.opts += ["-o", "ControlMaster=auto", "-o", "ControlPath=" + os.path.join(agent.tmp, "cm"),
                   "-o", "ControlPersist=900"]
    try:
        checks["ver"] = agent.ssh("glos ver")[0] == 0
        for mode in SHOT_MODES:
            job = subprocess.Popen(agent.opts + ["glos@127.0.0.1", "C:\\TEST\\%s.EXE %s" % (prog, mode)],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            t0, line = time.time(), None
            while time.time() - t0 < 180 and not line:
                m = re.findall(r"^HX-SHOT mode=%s (.*)\n" % mode, agent.log(), re.M)    # (a whole line)
                line = m[0] if m else None
                if not line:
                    time.sleep(0.3)
            if not line or " skip" in " " + line:
                skipped.append(mode + ("" if line else "?"))
                checks[mode] = bool(line)
                job.wait(timeout=120)
                continue
            f = dict(kv.split("=", 1) for kv in line.split() if "=" in kv)
            want = shot_want(f)
            p = subprocess.run(agent.opts + ["glos@127.0.0.1", "glos shot"], capture_output=True, timeout=180)
            if mode == "vga13c":
                checks["deaf"] = "HX-SHOT mode=vga13c resumed" not in agent.log()
            open(os.path.join(out, mode + ".png"), "wb").write(p.stdout)
            got = png_rgb(p.stdout)
            r = subprocess.run(agent.opts + ["glos@127.0.0.1", "glos shot --raw"], capture_output=True, timeout=180)
            head = b"GLOSRAW1" + int(f["w"]).to_bytes(4, "little") + int(f["h"]).to_bytes(4, "little")
            checks[mode] = (p.returncode == 0 and got is not None and got[:2] == (int(f["w"]), int(f["h"]))
                            and got[2] == want)
            if mode != "vga13c":                # (its five seconds may be over by now)
                checks[mode + "-raw"] = r.returncode == 0 and r.stdout == head + want
            if not checks[mode]:
                open(os.path.join(out, mode + ".want"), "wb").write(want)
                bad = sum(1 for i in range(0, min(len(want), len(got[2])), 3) if want[i:i + 3] != got[2][i:i + 3]) \
                    if got else -1
                info += " %s:%s" % (mode, "%s differ=%d" % (got[:2], bad) if got else p.stderr.decode("latin-1")[:80])
            taken.add(f["kind"] + (f.get("bpp", "")))
            if mode == "vga13c":
                checks[mode + "-end"] = job.wait(timeout=120) == 0
            else:
                checks[mode + "-kill"] = agent.ssh("glos kill")[0] == 0 and job.wait(timeout=120) == 255
        rc, _, _ = agent.ssh("glos shot")       # text mode is back: a picture of the DOS screen
        checks["after"] = rc == 0
        checks["exit"] = agent.ssh("glos exit")[0] == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    subprocess.run(agent.opts + ["-O", "exit", "glos@127.0.0.1"], capture_output=True, timeout=30)
    text = agent.wait()
    checks["kinds"] = {"text", "pal16", "pal256"} <= taken and any(k.startswith("rgb") for k in taken)
    checks["clean"] = not re.search(r"GLOS-PANIC|GLOS-CRASH|GLOS-WARN (?!vif-stuck)|DPMI-UNIMPL", text)   # (vga13c)
    checks["done"] = "GLOS-EXIT code=0" in text and "HX-DONE 0" in text and "HX-VECCHK ok" in text
    bad = [k for k, v in checks.items() if not v]
    return "  %-26s %s modes=%d skipped=%s%s%s" % (
        "shot-" + tag, "PASS" if not bad else "FAIL", len(SHOT_MODES) - len(skipped), ",".join(skipped) or "none",
        "" if not bad else " failed: " + " ".join(bad), info), not bad


# ---- glos key, type and mouse (M5 A4)

INPUT_CASES = [("bf6", "ne2kpci", True), ("486dx2", "ne2k", False)]      # (the 486 board has no PS/2 mouse port)
MOUSE_KEYS = "1:mouse:40:-20:1,2:mouse:40:-20:0"        # MGA-Glide's selftest-mouse: right and up, the left button held


def input_case(profile, card, mouse_too):
    """Input injected over ssh against the same input from Loop A's --keys (86Box's keyboard and mouse), in one
    run: KEYWAIT's key (Enter; the grey Up arrow, an E0 code) must be the same from both, and MGA-Glide's
    MOUSETST (CuteMouse on the PS/2 mouse) must pass with both. Then glos type's line through DOS, a list that makes no sense
    refused, the kill hotkey typed with glos key (the list jobs.py gives --keys elsewhere), and DIRTEST's own
    IRQ 1 handler in a direct-mode session, where an injected byte goes into the 8042 itself."""
    import time
    tag = "%s-%s" % (profile, card)
    agent = Agent("input-" + tag, profile, card, SESS_FILES + [
        "--file", "build/ow/dos/TYPEIN.EXE=/TEST/TYPEIN.EXE", "--file", "build/dj/DIRTEST.EXE=/TEST/DIRTEST.EXE",
        "--file", os.path.join(MGA, "build/djgpp/MOUSETST.EXE") + "=/TEST/MOUSETST.EXE",
        "--mouse", "ps2" if mouse_too else "none",
        "--keys", "@HX-KEYWAIT ready,0.5:0x1c,@HX-KEYWAIT ready,0.5:0x148,@HX-TEST driver," + MOUSE_KEYS,
        "--cmd", "SERSAY HX-START input", "--cmd", "VECCHK save", "--cmd", "C:\\TEST\\GLOS.EXE",
        "--cmd", "VECCHK check", "--cmd", "SERSAY HX-DONE 0"], timeout=900 if profile == "bf6" else 1800)
    checks, info = {"listen": agent.listening()}, ""
    agent.opts += ["-o", "ControlMaster=auto", "-o", "ControlPath=" + os.path.join(agent.tmp, "cm"),
                   "-o", "ControlPersist=900"]

    def job(cmd, mark, n, then=None, limit=120):
        """cmd as an agent job; when its n-th `mark` is on COM1, the ssh command `then`; the job's exit code."""
        p = subprocess.Popen(agent.opts + ["glos@127.0.0.1", cmd], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        t0 = time.time()
        while time.time() - t0 < limit and agent.log().count(mark) < n and p.poll() is None:
            time.sleep(0.3)
        rc = agent.ssh(then)[0] if then else 0
        return p.wait(timeout=limit), rc

    try:
        checks["ver"] = agent.ssh("glos ver")[0] == 0
        # From 86Box (--keys), then from glos key: the same programs, the same lists.
        job("KEYWAIT 20", "HX-KEYWAIT ready", 1)
        job("KEYWAIT 20", "HX-KEYWAIT ready", 2)
        if mouse_too:
            job("C:\\TEST\\MOUSETST.EXE --noexit", "HX-TEST driver", 99, limit=60)    # (or its end stops 86Box)
        checks["key-rc"] = job("KEYWAIT 20", "HX-KEYWAIT ready", 3, "glos key 0.5:0x1c") == (0, 0)
        checks["ext-rc"] = job("KEYWAIT 20", "HX-KEYWAIT ready", 4, "glos key 0.5:0x148") == (0, 0)
        if mouse_too:
            checks["mouse-rc"] = job("C:\\TEST\\MOUSETST.EXE --noexit", "HX-TEST driver", 2,
                                     "glos key " + MOUSE_KEYS)[1] == 0
        else:                                   # no mouse, so no program has it on
            rc, _, err = agent.ssh("glos mouse 5 5")
            checks["mouse-off"] = rc == 1 and "mouse" in err
        text = "Hello, World! 1+2=3 (ok) <a_b> ~x^y \"q\" z|w"
        checks["type-rc"] = job("C:\\TEST\\TYPEIN.EXE", "HX-TYPE ready", 1, "glos type " + text + "\\n") == (0, 0)
        rc, _, err = agent.ssh("glos key 1:0x1c,bogus")
        checks["refuse"] = rc == 2 and "bogus" in err
        rc, _, err = agent.ssh("glos type caf\xe9")
        checks["refuse-char"] = rc == 2
        checks["hotkey"] = job("C:\\TEST\\SESSTEST.EXE hang", "HX-SESS armed", 1, "glos key " + KILL_KEYS) == (255, 0)
        checks["direct-rc"] = job("glos run --direct C:\\TEST\\DIRTEST.EXE", "HX-DIRECT armed-key", 1,
                                  "glos key 1:0x1c")[1] == 0
        checks["exit"] = agent.ssh("glos exit")[0] == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    subprocess.run(agent.opts + ["-O", "exit", "glos@127.0.0.1"], capture_output=True, timeout=30)
    log = agent.wait()
    keys = re.findall(r"^HX-KEY (.*)$", log, re.M)
    checks["key"] = len(keys) == 4 and keys[0] == keys[2] and keys[0].startswith("scan=1c")
    checks["ext"] = len(keys) == 4 and keys[1] == keys[3] and keys[1].startswith("scan=48")
    mouse = re.findall(r"^HX-STAT mouse (.*)$", log, re.M)
    tests = re.findall(r"^HX-TEST (motion|buttons) (\S+)", log, re.M)
    if mouse_too:
        checks["mouse"] = len(mouse) == 2 and tests == [("motion", "PASS"), ("buttons", "PASS")] * 2
        # 86Box scales its monitor's mickeys by the window (270 by -110 and 270 by -122 seen for 80 by -40), so the
        # counts are not compared: GLOS's must keep the proportion they were given in (CuteMouse triples them).
        ours = re.match(r"dx=(-?\d+) dy=(-?\d+) ", mouse[1]) if len(mouse) == 2 else None
        checks["mouse-exact"] = bool(ours) and int(ours.group(1)) > 0 and int(ours.group(1)) == -2 * int(ours.group(2))
    checks["type"] = "HX-TYPE got=" + text + "\n" in log
    checks["hotkey-reason"] = bool(re.search(r"GLOS-KILL psp=\w+ at=\S+ ticks=\d+ reason=hotkey", log))
    checks["direct"] = "HX-DIRECT keys=1c9c" in log
    checks["clean"] = not re.search(r"GLOS-PANIC|GLOS-CRASH|GLOS-WARN|DPMI-UNIMPL", log)
    checks["done"] = "GLOS-EXIT code=0" in log and "HX-DONE 0" in log and "HX-VECCHK ok" in log
    if not (checks["key"] and checks["ext"] and checks.get("mouse", True) and checks.get("mouse-exact", True)):
        info = " keys=%r mouse=%r tests=%r" % (keys, mouse, tests)
    bad = [k for k, v in checks.items() if not v]
    return "  %-26s %s%s%s" % ("input-" + tag, "PASS" if not bad else "FAIL",
                               "" if not bad else " failed: " + " ".join(bad), info), not bad


# ---- glos gdb (M5 A7)

def gdb_case(profile, card):
    """The host's gdb on GDBME through an ssh direct-tcpip channel (ssh -W). glos gdb arms the debugger and
    the program stops in its start-up code; gdb sets a breakpoint at main, runs to it, reads counter (42),
    sets it to 1000, steps an instruction and lets go, and the program then says 2001. Twice more: let go
    at once, and run to its end under gdb, which hears that it has gone. The host's gdb reads no DJGPP
    executable, so the addresses are nm's (build/dj/GDBME.sym). A forward to anything else is refused, and
    so is one to the debugger when nothing is armed."""
    import shlex
    import time
    tag = "%s-%s" % (profile, card)
    sym = dict((l.split()[2], int(l.split()[0], 16)) for l in open(os.path.join(ROOT, "build/dj/GDBME.sym"))
               if len(l.split()) == 3)
    agent = Agent("gdb-" + tag, profile, card, ["--file", "build/dj/GDBME.EXE=/TEST/GDBME.EXE",
                                                "--cmd", "SERSAY HX-START gdb", "--cmd", "VECCHK save",
                                                "--cmd", "C:\\TEST\\GLOS.EXE", "--cmd", "VECCHK check",
                                                "--cmd", "SERSAY HX-DONE 0"], timeout=900)
    out = os.path.join(ROOT, "out", "gdb-" + tag)
    checks, info = {"listen": agent.listening()}, ""
    try:
        fwd = agent.opts + ["-W", "localhost:2345", "glos@127.0.0.1"]
        p = subprocess.run(fwd, stdin=subprocess.DEVNULL, capture_output=True, timeout=120)
        checks["unarmed-refused"] = p.returncode != 0
        rc, text, _ = agent.ssh("glos gdb")
        checks["arm"] = rc == 0 and "2345" in text
        p = subprocess.run(agent.opts + ["-W", "localhost:80", "glos@127.0.0.1"], stdin=subprocess.DEVNULL,
                           capture_output=True, timeout=120)
        checks["other-refused"] = p.returncode != 0
        job = subprocess.Popen(agent.opts + ["glos@127.0.0.1", "C:\\TEST\\GDBME.EXE"], stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
        t0 = time.time()
        while time.time() - t0 < 120 and "GLOS-GDB stop" not in agent.log():
            time.sleep(0.5)
        checks["stopped"] = "GLOS-GDB stop" in agent.log() and "HX-GDBME start" not in agent.log()
        script = ["set architecture i386", "set pagination off", "set confirm off",
                  "target remote | " + " ".join(shlex.quote(a) for a in fwd),
                  "info registers eip", "break *0x%x" % sym["_main"], "continue", "info registers eip",
                  "x/dw 0x%x" % sym["_counter"], "set {int}0x%x = 1000" % sym["_counter"],
                  "x/dw 0x%x" % sym["_counter"], "stepi", "info registers eip", "delete", "detach"]
        g = subprocess.run(["gdb", "-q", "-batch"] + [x for s in script for x in ("-ex", s)], capture_output=True,
                           timeout=300, stdin=subprocess.DEVNULL)
        text = g.stdout.decode("latin-1") + g.stderr.decode("latin-1")
        open(os.path.join(out, "gdb.log"), "w").write(text)
        eips = [int(x, 16) for x in re.findall(r"^eip\s+0x([0-9a-f]+)", text, re.M)]
        checks["breakpoint"] = bool(re.search(r"Breakpoint 1, 0x0*%x in" % sym["_main"], text))
        checks["eip"] = len(eips) == 3 and eips[1] == sym["_main"] and eips[2] != eips[1]
        vals = re.findall(r"^0x[0-9a-f]+:\s+(-?\d+)", text, re.M)
        checks["read"] = vals[:1] == ["42"]
        checks["write"] = vals[1:2] == ["1000"]
        checks["end"] = job.wait(timeout=120) == (2001 & 0x7F)
        if not all(checks[k] for k in ("breakpoint", "eip", "read", "write", "end")):
            info = " eips=%r vals=%r" % (eips, vals)
        # Again, untouched: armed, attached, let go at once.
        checks["arm2"] = agent.ssh("glos gdb")[0] == 0
        job = subprocess.Popen(agent.opts + ["glos@127.0.0.1", "C:\\TEST\\GDBME.EXE"], stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
        t0 = time.time()
        while time.time() - t0 < 120 and agent.log().count("GLOS-GDB stop") < 2:
            time.sleep(0.5)
        g = subprocess.run(["gdb", "-q", "-batch", "-ex", "set architecture i386", "-ex", script[3], "-ex", "detach"],
                           capture_output=True, timeout=300, stdin=subprocess.DEVNULL)
        checks["detach"] = job.wait(timeout=120) == 85
        # And run to its end under gdb, which is told that it has gone.
        checks["arm3"] = agent.ssh("glos gdb")[0] == 0
        job = subprocess.Popen(agent.opts + ["glos@127.0.0.1", "C:\\TEST\\GDBME.EXE"], stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
        t0 = time.time()
        while time.time() - t0 < 120 and agent.log().count("GLOS-GDB stop") < 3:
            time.sleep(0.5)
        g = subprocess.run(["gdb", "-q", "-batch", "-ex", "set architecture i386", "-ex", script[3], "-ex", "continue"],
                           capture_output=True, timeout=300, stdin=subprocess.DEVNULL)
        text3 = g.stdout.decode("latin-1") + g.stderr.decode("latin-1")
        open(os.path.join(out, "gdb3.log"), "w").write(text3)
        checks["ran-out"] = job.wait(timeout=120) == 85 and "exited normally" in text3
        checks["exit"] = agent.ssh("glos exit")[0] == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    log = agent.wait()
    checks["said"] = "HX-GDBME counter=2001" in log and log.count("HX-GDBME counter=85") == 2
    checks["clean"] = not re.search(r"GLOS-PANIC|GLOS-CRASH|GLOS-WARN|DPMI-UNIMPL", log)
    checks["done"] = "GLOS-EXIT code=0" in log and "HX-DONE 0" in log and "HX-VECCHK ok" in log
    bad = [k for k, v in checks.items() if not v]
    return "  %-26s %s%s%s" % ("gdb-" + tag, "PASS" if not bad else "FAIL",
                               "" if not bad else " failed: " + " ".join(bad), info), not bad


# ---- M5's exit: a job driven over SSH against its --keys/--shots twin

EXIT_KEYS = [0x1E, 0x30, 0x2E]                  # a, b, c
EXIT_CASES = [("bf6", "g200"), ("bf6", "vbe")]


# ---- the display and the 2D library (M6 step C)

# (machine, video card, network card, GLOS.CFG's [display] mode or none)
DISPLAY_CASES = [("bf6", "vbe", "ne2kpci", ""), ("bf6", "g200", "ne2kpci", "800x600x16"), ("486dx2", "vbe", "ne2k", "640x480x8"),
                 ("bf6", "g400", "ne2kpci", ""), ("bf6", "g450", "ne2kpci", "1024x768x32"),
                 ("bf6", "g200", "ne2kpci", "640x480x8", "off"),
                 ("486dx2", "g200pci", "ne2k", ""), ("486dx2", "g450pci", "ne2k", "640x480x16")]   # (M7 step G)
MATROX_CARDS = ("g100", "g200", "g400", "g450", "g200pci", "g400pci", "g450pci")


def display_case(profile, vcard, card, cfg_mode, accel="on"):
    """The desktop's screen (kernel/gfx/display.c): at 640x480 in 8, 16 and 32 bits a pixel, the kernel draws the
    2D library's test card on the real screen, and glos shot's picture of it must be the host's own rendering
    (tests/host/gfx_test.c wrote it: build/host/gfx-card-N.png), pixel for pixel; at 8 bits through the DAC's
    6 bits a colour where the BIOS can't give 8. Then: a DOS program (a session) gets text mode and the card
    is back after it; a program marked as a GLOS app starts, writes to the console and ends with the card
    untouched; glos display off is text mode; a mode no card has is refused; GLOS leaves in text mode. With a
    [display] mode in GLOS.CFG the desktop is there from the start, one colour above its task bar.
    On a Matrox card the drawing engine is up on every desktop mode (M7 step C: glos display says "accel on",
    and the card is let go of for a session and with the desktop off), unless GLOS.CFG's accel = off."""
    import time
    tag = "%s-%s%s" % (profile, vcard, "" if accel == "on" else "-accel-" + accel)
    cfg = ["--file", batfile("display-%s/GLOS.CFG" % tag, ["[display]", "mode = " + cfg_mode, "accel = " + accel]) +
           "=/TEST/GLOS.CFG"] if cfg_mode else []
    engine = vcard in MATROX_CARDS and accel == "on"
    want_accel = "accel on " if engine else "accel off" if accel != "on" else "accel none"
    agent = Agent("display-" + tag, profile, card, ["--card", vcard] + cfg + [
        "--file", "build/dj/APPTEST.EXE=/TEST/APPTEST.EXE", "--file", "build/ow/dos/ECHOARGS.EXE=/TEST/ECHOARGS.EXE",
        "--cmd", "SERSAY HX-START display", "--cmd", "VECCHK save", "--cmd", "C:\\TEST\\GLOS.EXE",
        "--cmd", "VECCHK check", "--cmd", "VMODE", "--cmd", "SERSAY HX-DONE 0"], timeout=1800)
    agent.opts += ["-o", "ControlMaster=auto", "-o", "ControlPath=" + os.path.join(agent.tmp, "cm"),
                   "-o", "ControlPersist=600"]
    out = os.path.join(ROOT, "out", "display-" + tag)
    checks, info = {"listen": agent.listening()}, ""

    def state(want, limit=60):                  # glos display's line once it starts with `want`
        t0 = time.time()
        while time.time() - t0 < limit:
            line = agent.ssh("glos display")[1]
            if line.startswith(want):
                return line
            time.sleep(0.5)
        return ""

    def shot(name):
        p = subprocess.run(agent.opts + ["glos@127.0.0.1", "glos shot"], capture_output=True, timeout=300)
        open(os.path.join(out, name + ".png"), "wb").write(p.stdout)
        return png_rgb(p.stdout) if p.returncode == 0 else None

    def card(bits, line):                       # the host's picture, as this screen shows it
        w, h, rgb = png_rgb(open(os.path.join(ROOT, "build/host/gfx-card-%d.png" % bits), "rb").read())
        if bits == 8 and " dac=6" in line:      # (what glos shot makes of a 6-bit DAC's colours)
            rgb = bytes((((v >> 2) << 2) | (v >> 6)) for v in rgb)
        return w, h, rgb

    def same(got, want, what):
        if got == want:
            return True
        if not got or got[:2] != want[:2]:
            print("    %s %s: %s" % (tag, what, "no picture" if not got else "%dx%d" % got[:2]))
            return False
        bad = [i // 3 for i in range(0, len(want[2]), 3) if got[2][i:i + 3] != want[2][i:i + 3]]
        print("    %s %s: %d pixels differ, the first at %d,%d: %s for %s" % (
            tag, what, len(bad), bad[0] % want[0], bad[0] // want[0], got[2][bad[0] * 3:bad[0] * 3 + 3].hex(),
            want[2][bad[0] * 3:bad[0] * 3 + 3].hex()))
        return False

    try:
        checks["ver"] = agent.ssh("glos ver")[0] == 0
        if cfg_mode:                            # the desktop from the start, as GLOS.CFG asks
            line = state("display on mode=")
            first = shot("first")
            w, h = [int(v) for v in cfg_mode.split("x")[:2]]
            checks["from-cfg"] = (" %s " % cfg_mode) in line and bool(first) and first[:2] == (w, h) \
                and first[2][:w * (h - 20) * 3] == first[2][:3] * (w * (h - 20))   # (above the task bar)
        else:
            checks["off-first"] = agent.ssh("glos display")[1].startswith("display off")
        line = ""
        for bits in (8, 16, 32):
            checks["ask-%d" % bits] = agent.ssh("glos display card 640x480x%d" % bits)[0] == 0
            line = state("display on mode=")
            checks["on-%d" % bits] = " 640x480x%d " % bits in line
            checks["accel-%d" % bits] = want_accel in line and (not engine or int((re.findall(r"draws=(\d+)", line)
                                                                                    or ["0"])[0]) > 0)
            if checks["on-%d" % bits]:
                checks["card-%d" % bits] = same(shot("card-%d" % bits), card(bits, line), "card at %d bits" % bits)
        info = " " + " ".join(w for w in line.split() if w.startswith(("mode=", "rgb=", "lfb=")))
        # A session takes the screen and gives it back; an app doesn't take it.
        rc, o, _ = agent.ssh("C:\\TEST\\ECHOARGS.EXE on the screen")
        checks["session"] = rc == 7 and "on the screen" in o
        line = state("display on mode=")
        checks["back"] = bool(line) and same(shot("after-session"), card(32, line), "the card after a session")
        checks["accel-back"] = want_accel in line
        if engine:                              # (let go of for the session, up again after it)
            checks["accel-session"] = agent.log().count("GLOS-MGA down") >= 1 and agent.log().count("GLOS-MGA up") >= 4
        n = agent.log().count("GLOS-DISPLAY on ")
        rc, o, _ = agent.ssh("C:\\TEST\\APPTEST.EXE run A 2", timeout=600)     # (it writes to the console, and files)
        checks["app"] = rc == 40 and "APP-OK" in o and agent.log().count("GLOS-DISPLAY on ") == n \
            and same(shot("after-app"), card(32, line), "the card after an app")
        rc, _, err = agent.ssh("glos display on 640x480x24")
        checks["usage"] = rc == 2 and "usage" in err
        agent.ssh("glos display on 1234x567x8")         # (no such mode on any card)
        time.sleep(3)
        checks["no-mode"] = "GLOS-DISPLAY error=no-mode want=1234x567x8" in agent.log()
        agent.ssh("glos display card 640x480x8")
        checks["again"] = bool(state("display on mode="))
        checks["off"] = agent.ssh("glos display off")[0] == 0 and bool(state("display off"))
        checks["accel-off"] = ("accel idle" if engine else want_accel) in agent.ssh("glos display")[1]
        t = shot("text")
        checks["text"] = bool(t) and t[:2] == (640, 400)
        agent.ssh("glos display card 640x480x16")
        checks["on-again"] = bool(state("display on mode="))
        checks["exit"] = agent.ssh("glos exit")[0] == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    text = agent.wait()
    checks["text-at-exit"] = "HX-VMODE bios=03" in text
    checks["no-panic"] = "GLOS-PANIC" not in text and "GLOS-WARN" not in text
    checks["done"] = "GLOS-EXIT code=0" in text and "HX-DONE 0" in text and "HX-VECCHK ok" in text
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s%s" % ("display-" + tag, "PASS" if not bad else "FAIL", info,
                               "" if not bad else " failed: " + " ".join(bad)), not bad


DESK_CASES = [("bf6", "vbe", "ne2kpci"), ("bf6", "g200", "ne2kpci"), ("486dx2", "vbe", "ne2k"), ("bf6", "g400", "ne2kpci"),
              ("bf6", "g450", "ne2kpci"), ("486dx2", "g200pci", "ne2k"), ("486dx2", "g450pci", "ne2k")]


def desk_case(profile, vcard, card):
    """The desktop (kernel/wm): the demonstration's windows, task bar and pointer composed on the real screen at
    640x480 in 8, 16 and 32 bits, each screenshot equal to the host's composition (tests/host/wm_test.c wrote
    build/host/wm-desk-N.png). A DOS program (a session) takes the screen and the desktop is back after it as
    it was, from the windows' own pixels; the windows taken down leave the bare desktop and its bar."""
    import time
    tag = "%s-%s" % (profile, vcard)
    agent = Agent("desk-" + tag, profile, card, ["--card", vcard,
        "--file", "build/ow/dos/ECHOARGS.EXE=/TEST/ECHOARGS.EXE",
        "--cmd", "SERSAY HX-START desk", "--cmd", "VECCHK save", "--cmd", "C:\\TEST\\GLOS.EXE",
        "--cmd", "VECCHK check", "--cmd", "VMODE", "--cmd", "SERSAY HX-DONE 0"], timeout=1800)
    agent.opts += ["-o", "ControlMaster=auto", "-o", "ControlPath=" + os.path.join(agent.tmp, "cm"),
                   "-o", "ControlPersist=600"]
    out = os.path.join(ROOT, "out", "desk-" + tag)
    checks, info = {"listen": agent.listening()}, ""

    def state(want, limit=60):
        t0 = time.time()
        while time.time() - t0 < limit:
            line = agent.ssh("glos display")[1]
            if line.startswith(want):
                return line
            time.sleep(0.5)
        return ""

    def shot(name):
        p = subprocess.run(agent.opts + ["glos@127.0.0.1", "glos shot"], capture_output=True, timeout=300)
        open(os.path.join(out, name + ".png"), "wb").write(p.stdout)
        return png_rgb(p.stdout) if p.returncode == 0 else None

    def golden(bits, line, ptr=""):
        w, h, rgb = png_rgb(open(os.path.join(ROOT, "build/host/wm-desk-%s%d.png" % (ptr, bits)), "rb").read())
        if bits == 8 and " dac=6" in line:
            rgb = bytes((((v >> 2) << 2) | (v >> 6)) for v in rgb)
        return w, h, rgb

    def same(name, want, what, tries=6):         # (the composer has its own thread: a moment)
        got = None
        for _ in range(tries):
            got = shot(name)
            if got == want:
                return True
            time.sleep(1)
        if not got or got[:2] != want[:2]:
            print("    %s %s: %s" % (tag, what, "no picture" if not got else "%dx%d" % got[:2]))
            return False
        bad = [i // 3 for i in range(0, len(want[2]), 3) if got[2][i:i + 3] != want[2][i:i + 3]]
        print("    %s %s: %d pixels differ, the first at %d,%d: %s for %s" % (
            tag, what, len(bad), bad[0] % want[0], bad[0] // want[0], got[2][bad[0] * 3:bad[0] * 3 + 3].hex(),
            want[2][bad[0] * 3:bad[0] * 3 + 3].hex()))
        return False

    try:
        checks["ver"] = agent.ssh("glos ver")[0] == 0
        line = ""
        for bits in (8, 16, 32):
            agent.ssh("glos desk clear")
            checks["ask-%d" % bits] = agent.ssh("glos display on 640x480x%d" % bits)[0] == 0
            line = state("display on mode=")
            checks["on-%d" % bits] = " 640x480x%d " % bits in line
            checks["demo-%d" % bits] = agent.ssh("glos desk demo")[0] == 0
            if checks["on-%d" % bits]:
                checks["desk-%d" % bits] = same("desk-%d" % bits, golden(bits, line), "the desktop at %d bits" % bits)
        st = agent.ssh("glos desk")[1]
        checks["stat"] = st.startswith("desk 640x480x32 windows=4 ") and len(re.findall(r"^window \d+ shown", st, re.M)) == 4
        rc, o, _ = agent.ssh("C:\\TEST\\ECHOARGS.EXE over the desktop")
        checks["session"] = rc == 7 and "over the desktop" in o
        line = state("display on mode=")
        checks["back"] = bool(line) and same("after-session", golden(32, line), "the desktop after a session")
        # The pointer at 200,150: drawn by the composer, or the card's cursor (M7 step F, a Matrox card), which
        # glos shot draws into its picture where the monitor shows it: the same picture either way.
        m = re.search(r"pointer=(-?\d+),(-?\d+)", agent.ssh("glos desk")[1])
        if m:
            agent.ssh("glos mouse %d %d" % (200 - int(m.group(1)), 150 - int(m.group(2))))
        checks["pointer"] = bool(m) and same("pointer", golden(32, line, "ptr-"), "the desktop with the pointer")
        if vcard in MATROX_CARDS:
            checks["cursor"] = "GLOS-MGA cursor at=" in agent.log()
            # The desktop's windows' pixels in the card's memory (M7 step E): out of it for the session (vram-out),
            # back after it; then with room for some only, the same picture from some of each.
            v = re.search(r" vram=(\d+)/", agent.ssh("glos desk")[1])
            checks["vram-stores"] = bool(v) and int(v.group(1)) == 4 and "GLOS-DESK vram-out windows=4" in agent.log()
            agent.ssh("glos display vram 300")
            agent.ssh("glos desk clear")
            agent.ssh("glos desk demo")
            v = re.search(r" vram=(\d+)/", agent.ssh("glos desk")[1])
            checks["vram-some"] = bool(v) and int(v.group(1)) == 2 \
                and same("vram-some", golden(32, line), "the desktop from VRAM and memory")
            agent.ssh("glos display vram 0")
        checks["clear"] = agent.ssh("glos desk clear")[0] == 0
        time.sleep(2)
        bare = shot("bare")
        checks["bare"] = bool(bare) and bare[:2] == (640, 480) and bare[2][:3] * 640 == bare[2][:640 * 3] \
            and bare[2][:3] == bytes((0x66, 0x66, 0x66))
        checks["exit"] = agent.ssh("glos exit")[0] == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    text = agent.wait()
    checks["text-at-exit"] = "HX-VMODE bios=03" in text
    checks["no-panic"] = "GLOS-PANIC" not in text and "GLOS-WARN" not in text
    checks["done"] = "GLOS-EXIT code=0" in text and "HX-DONE 0" in text and "HX-VECCHK ok" in text
    if vcard in MATROX_CARDS:                   # (M7 step C: the engine up on each mode, drawing nothing yet)
        checks["accel"] = text.count("GLOS-MGA up ") >= 4 and "GLOS-MGA down" in text and "GLOS-MGA error" not in text
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s%s" % ("desk-" + tag, "PASS" if not bad else "FAIL", info,
                               "" if not bad else " failed: " + " ".join(bad)), not bad


# ---- windows, input and the API (M6 steps E and F)

# (machine, video card, network card, bits a pixel, the bits to change to)
WIN_CASES = [("bf6", "vbe", "ne2kpci", 32, 16), ("bf6", "g200", "ne2kpci", 16, 8), ("486dx2", "vbe", "ne2k", 8, 16),
             ("bf6", "g400", "ne2kpci", 32, 8), ("bf6", "g450", "ne2kpci", 8, 32),
             ("486dx2", "g200pci", "ne2k", 8, 16), ("486dx2", "g450pci", "ne2k", 8, 32)]
# (On the 486 the busy phase is at 8 bits: a screenshot at 16 or 32 takes seconds there, too few probes fit in
# its ten, on the S3 as on a Matrox card.)
WIN_COLOUR = {"A": (0xCC, 0x33, 0x33), "B": (0x33, 0x33, 0xCC), "C": (0x33, 0xCC, 0x33)}


def win_case(profile, vcard, card, bits, bits2):
    """Apps with windows (tests/dos/wintest.c; kernel/wm/deskin.c, deskapp.c; the API's events and windows):
    two apps' windows on the real screen, each with its own picture; keys go to the one with the focus, Alt-Tab
    moves it; the mouse moves a window by its title bar and sizes it by its corner (the app draws again in its
    new pixels), a task-bar button gives the focus, the close box closes; the keys do the same (Alt-F7, Alt-F8,
    Alt-F4). Three apps read the keyboard three ways (the API's events, INT 16h, port 60h from their own IRQ 1
    handler) and each gets the keys typed while it had the focus, and no others; one reads the mouse with
    INT 33h. The exit's first half: an app that draws at a steady rate beside one that draws in a busy loop
    makes at least a third of the steps it makes alone, the agent answering throughout, and the kill hotkey
    ends the busy one, which had the focus. A session beside a window; another depth, which the app is told
    of; ten apps that make a window and end, after which the kernel has the memory it had; glos exit with a
    window up."""
    import time
    tag = "%s-%s" % (profile, vcard)
    agent = Agent("win-" + tag, profile, card, ["--card", vcard,
        "--file", "build/dj/WINTEST.EXE=/TEST/WINTEST.EXE", "--file", "build/ow/dos/ECHOARGS.EXE=/TEST/ECHOARGS.EXE",
        "--cmd", "SERSAY HX-START win", "--cmd", "VECCHK save", "--cmd", "C:\\TEST\\GLOS.EXE",
        "--cmd", "VECCHK check", "--cmd", "VMODE", "--cmd", "SERSAY HX-DONE 0"], timeout=2400)
    agent.opts += ["-o", "ControlMaster=auto", "-o", "ControlPath=" + os.path.join(agent.tmp, "cm"),
                   "-o", "ControlPersist=600"]
    out = os.path.join(ROOT, "out", "win-" + tag)
    app = "C:\\TEST\\WINTEST.EXE "
    checks, info = {"listen": agent.listening()}, ""
    BORDER, TITLE = 1, 18                       # the look's sizes (kernel/wm/wm.c)
    ALT_TAB, ALT_F4 = "0x38:down,0x0F,0x38:up", "0x38:down,0x3E,0x38:up"

    def start(cmd):
        return subprocess.Popen(agent.opts + ["glos@127.0.0.1", cmd], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def finish(p, limit=600):
        o, e = p.communicate(timeout=limit)
        return p.returncode, o.decode("latin-1").replace("\r", ""), e.decode("latin-1")

    def state(want, limit=60):
        t0 = time.time()
        while time.time() - t0 < limit:
            line = agent.ssh("glos display")[1]
            if line.startswith(want):
                return line
            time.sleep(0.5)
        return ""

    def desk():                                 # the focus, the pointer, and each window by its title
        text = agent.ssh("glos desk")[1].replace("\r", "")
        m = re.search(r"focus=(\d+) pointer=(-?\d+),(-?\d+)", text)
        d = {"focus": int(m.group(1)) if m else -1, "ptr": (int(m.group(2)), int(m.group(3))) if m else (0, 0), "win": {}}
        for w in re.finditer(r'^window (\d+) (\w+)(?: \w+)? at (-?\d+),(-?\d+) (\d+)x(\d+) "(.*)"$', text, re.M):
            d["win"][w.group(7)] = {"id": int(w.group(1)), "x": int(w.group(3)), "y": int(w.group(4)),
                                    "w": int(w.group(5)), "h": int(w.group(6))}
        return d

    def until(test, limit=30):                  # the desktop once `test` holds for it (the composer and the apps
        t0 = time.time()                        # have their own threads: a moment)
        d = desk()
        while not test(d) and time.time() - t0 < limit:
            time.sleep(0.3)
            d = desk()
        return d if test(d) else None

    def has(name):
        return lambda d: "WINTEST " + name in d["win"]

    def focus(name):
        return lambda d: "WINTEST " + name in d["win"] and d["focus"] == d["win"]["WINTEST " + name]["id"]

    def point(x, y, buttons=None):              # the pointer to a place on the screen
        px, py = desk()["ptr"]
        agent.ssh("glos mouse %d %d%s" % (x - px, y - py, "" if buttons is None else " %d" % buttons))

    def click(x, y, button=1):
        point(x, y)
        agent.ssh("glos mouse 0 0 %d" % button)
        agent.ssh("glos mouse 0 0 0")

    def shot(name):
        p = subprocess.run(agent.opts + ["glos@127.0.0.1", "glos shot"], capture_output=True, timeout=300)
        open(os.path.join(out, name + ".png"), "wb").write(p.stdout)
        return png_rgb(p.stdout) if p.returncode == 0 else None

    def pixel(pic, x, y):
        return tuple(pic[2][(y * pic[0] + x) * 3:(y * pic[0] + x) * 3 + 3]) if pic else None

    def near(a, b):                             # (5 or 6 bits a colour, on the screen or in the DAC)
        return bool(a) and all(abs(a[i] - b[i]) <= 8 for i in range(3))

    def picture(name, tries=8):                 # the app's colour in the middle of its client area
        got = None
        for _ in range(tries):
            w = desk()["win"].get("WINTEST " + name)
            pic = shot("%s-%d" % (name, _))
            got = w and pixel(pic, w["x"] + BORDER + w["w"] // 2, w["y"] + BORDER + TITLE + BORDER + w["h"] // 2)
            if near(got, WIN_COLOUR[name]):
                return True
            time.sleep(1)
        print("    %s window %s: its middle is %r, should be %r" % (tag, name, got, WIN_COLOUR[name]))
        return False

    def mem():
        m = re.search(r"free_kb=(\d+)\n.*heap_kb=(\d+)", agent.ssh("glos mem")[1])
        return (int(m.group(1)), int(m.group(2))) if m else None

    def said(res, rc, *lines):
        ok = res[0] == rc and all(l in res[1].split("\n") for l in lines)
        if not ok:
            print("    %s: rc=%d (want %d), wanted %r in %r" % (tag, res[0], rc, lines, res[1][-900:]))
        return ok

    try:
        checks["ver"] = agent.ssh("glos ver")[0] == 0
        checks["on"] = agent.ssh("glos display on 640x480x%d" % bits)[0] == 0 and (" 640x480x%d " % bits) in state("display on mode=")

        # Two windows; keys to the focus; Alt-Tab.
        a = start(app + "show A 900 40 40 200 120")
        checks["a-up"] = bool(until(focus("A")))
        b = start(app + "show B 900 300 60 200 120")
        d = until(focus("B"))
        checks["b-up"] = bool(d)
        wa, wb = (d or desk())["win"].get("WINTEST A"), (d or desk())["win"].get("WINTEST B")
        checks["placed"] = bool(wa and wb) and (wa["x"], wa["y"], wa["w"], wa["h"]) == (40, 40, 200, 120) \
            and (wb["x"], wb["y"]) == (300, 60)
        checks["pictures"] = picture("A") and picture("B")
        agent.ssh("glos key 0x1E")              # a: B's
        agent.ssh("glos key " + ALT_TAB)
        checks["alt-tab"] = bool(until(focus("A")))
        agent.ssh("glos key 0x2A:down,0x30,0x2A:up")    # B: A's
        # The mouse: A by its title bar, then by its corner; B by its task-bar button; a press in B; B's close box.
        point(wa["x"] + 100, wa["y"] + BORDER + 8)
        agent.ssh("glos mouse 0 0 1")
        agent.ssh("glos mouse 100 150")
        ghost = shot("drag")                    # (the outline, not the window: its corner is black, the window is where it was)
        checks["outline"] = pixel(ghost, wa["x"] + 100, wa["y"] + 150) == (0, 0, 0) and near(
            pixel(ghost, wa["x"] + 100, wa["y"] + BORDER + TITLE + BORDER + 60), WIN_COLOUR["A"])
        agent.ssh("glos mouse 0 0 0")
        d = until(lambda d: has("A")(d) and (d["win"]["WINTEST A"]["x"], d["win"]["WINTEST A"]["y"]) == (140, 190))
        checks["moved"] = bool(d)
        point(140 + 202 - 5, 190 + 141 - 5)
        agent.ssh("glos mouse 0 0 1")
        agent.ssh("glos mouse 30 20")
        agent.ssh("glos mouse 0 0 0")
        d = until(lambda d: has("A")(d) and (d["win"]["WINTEST A"]["w"], d["win"]["WINTEST A"]["h"]) == (230, 140))
        checks["sized"] = bool(d) and picture("A")
        ids = sorted(w["id"] for t, w in desk()["win"].items() if t.startswith("WINTEST"))
        click(62 + 120 * ids.index(wb["id"]) + 10, 470)        # (the bar's buttons: from 62, 120 wide)
        checks["bar-button"] = bool(until(focus("B")))
        click(wb["x"] + BORDER + 50, wb["y"] + BORDER + TITLE + BORDER + 30)
        click(wb["x"] + BORDER + 4 + 5, wb["y"] + BORDER + 4 + 5)
        rb = finish(b)
        checks["close-box"] = said(rb, 40, "EV B key 01E ch=97 down=1 mods=0", "EV B key 01E ch=0 down=0 mods=0",
                                   "EV B mouse 50,30 buttons=1 button=1 down=1", "EV B mouse 50,30 buttons=0 button=1 down=0",
                                   "EV B close", "WIN-OK B closed") and "key 030" not in rb[1]
        # The keys: A moved and sized by the arrows, left alone with Esc, closed with Alt-F4.
        checks["focus-back"] = bool(until(focus("A")))
        agent.ssh("glos key 0x38:down,0x41,0x38:up,0x14B,0x14B,0x148,0x1C")
        d = until(lambda d: has("A")(d) and (d["win"]["WINTEST A"]["x"], d["win"]["WINTEST A"]["y"]) == (124, 182))
        checks["key-move"] = bool(d)
        agent.ssh("glos key 0x38:down,0x42,0x38:up,0x14D,0x150,0x150,0x1C")
        d = until(lambda d: has("A")(d) and (d["win"]["WINTEST A"]["w"], d["win"]["WINTEST A"]["h"]) == (238, 156))
        checks["key-size"] = bool(d)
        agent.ssh("glos key 0x38:down,0x41,0x38:up,0x14D,0x14D,0x01")
        time.sleep(1)
        d = desk()
        checks["key-esc"] = has("A")(d) and (d["win"]["WINTEST A"]["x"], d["win"]["WINTEST A"]["y"]) == (124, 182)
        agent.ssh("glos key " + ALT_F4)
        ra = finish(a)
        checks["alt-f4"] = said(ra, 40, "EV A key 030 ch=66 down=1 mods=1", "EV A resize 230x140 now=230x140 bpp=%d" % bits,
                                "EV A resize 238x156 now=238x156 bpp=%d" % bits, "EV A close", "WIN-OK A closed") \
            and "key 01E" not in ra[1]
        checks["none-up"] = bool(until(lambda d: not any(t.startswith("WINTEST") for t in d["win"])))

        # The keyboard three ways: q for the first to have the focus, w for the second, e for the third.
        ways = [("A", "ev"), ("B", "bios"), ("C", "port")]
        progs = {}
        for name, how in ways:
            progs[name] = start(app + "keys %s %s 900" % (name, how))
            checks["keys-%s-up" % how] = bool(until(focus(name)))
        order = []
        for k, code in enumerate((0x10, 0x11, 0x12)):
            d = desk()
            order.append([t[8:] for t, w in d["win"].items() if w["id"] == d["focus"]][0])
            agent.ssh("glos key 0x%02X,0x148" % code)   # (and the grey up arrow: a key with a prefix)
            time.sleep(1)
            agent.ssh("glos key " + ALT_TAB)
            time.sleep(1)
        checks["each-had-it"] = sorted(order) == ["A", "B", "C"]
        for name, how in ways:
            for _ in range(3):
                if focus(name)(desk()):
                    break
                agent.ssh("glos key " + ALT_TAB)
                time.sleep(1)
            agent.ssh("glos key 0x01")
            res = finish(progs[name])
            k = order.index(name) if name in order else 0
            code, ch = (0x10, 0x11, 0x12)[k], "qwe"[k]
            mine = {"ev": ["KEY A ev %03X %d 1" % (code, ord(ch)), "KEY A ev 148 0 1", "KEY A ev 001 27 1"],
                    "bios": ["KEY B bios %02X%02X shift=00" % (code, ord(ch)), "KEY B bios 48E0 shift=00",
                             "KEY B bios 011B shift=00"],
                    "port": ["KEY C port %03X 1" % code, "KEY C port %03X 0" % code, "KEY C port 148 1",
                             "KEY C port 001 1"]}[how]
            others = [c for c in (0x10, 0x11, 0x12) if c != code]
            theirs = [l for l in res[1].split("\n") if l.startswith("KEY") and any(
                re.search(r" (0%02X|%02X[0-9A-F][0-9A-F])( |$)" % (c, c), l) for c in others)]
            checks["keys-" + how] = said(res, 42, *mine) and not theirs
            if theirs:
                print("    %s keys %s: another's keys %r" % (tag, how, theirs))

        # INT 33h: the mouse in the app's own window.
        c = start(app + "mouse C 900")
        d = until(focus("C"))
        checks["mouse-up"] = bool(d)
        wc = d["win"]["WINTEST C"] if d else {"x": 0, "y": 0}
        cx, cy = wc["x"] + BORDER, wc["y"] + BORDER + TITLE + BORDER
        point(cx + 20, cy + 30)
        time.sleep(1)
        agent.ssh("glos mouse 15 10 1")
        time.sleep(1)
        agent.ssh("glos mouse 0 0 0")
        time.sleep(1)
        agent.ssh("glos mouse 0 0 2")
        rc_ = finish(c)
        checks["int33"] = said(rc_, 42, "MOUSE C driver=FFFF buttons=2", "WIN-OK C right") \
            and bool(re.search(r"^MOUSE C at=20,30 b=0 ", rc_[1], re.M)) \
            and bool(re.search(r"^MOUSE C at=35,40 b=1 ", rc_[1], re.M)) \
            and bool(re.search(r"^MOUSE C at=35,40 b=2 moved=-?\d+,-?\d+$", rc_[1], re.M))
        agent.ssh("glos mouse 0 0 0")

        # The exit's first half: one draws at its own pace, alone and then beside one that never stops.
        alone = finish(start(app + "draw A 6"))
        m = re.search(r"^WIN-OK A frames=(\d+) ms=(\d+)$", alone[1], re.M)
        f_alone = int(m.group(1)) * 1000.0 / int(m.group(2)) if m and alone[0] == 41 else 0
        spin = start(app + "spin C")
        checks["spin-up"] = bool(until(focus("C")))
        beside = start(app + "draw B 10")
        checks["draw-up"] = bool(until(focus("B")))
        agent.ssh("glos key " + ALT_TAB)        # (the busy one in front: the hotkey is for it)
        worst, probes, moving = 0.0, 0, []
        while beside.poll() is None:
            t = time.time()
            rc, o, _ = agent.ssh("glos tick")
            worst = max(worst, time.time() - t)
            probes += rc == 0 and o.startswith("ticks=")
            if len(moving) < 6 and len(set(moving)) < 2:    # (a slow host gives the guest little time between two)
                pic, w = shot("spin-%d" % len(moving)), desk()["win"].get("WINTEST C")
                if pic and w:
                    row = (w["y"] + BORDER + TITLE + BORDER + 30) * pic[0] + w["x"] + BORDER
                    moving.append(pic[2][row * 3:(row + w["w"]) * 3])
            time.sleep(0.2)
        res = finish(beside)
        m = re.search(r"^WIN-OK B frames=(\d+) ms=(\d+)$", res[1], re.M)
        f_beside = int(m.group(1)) * 1000.0 / int(m.group(2)) if m and res[0] == 41 else 0
        checks["two-at-once"] = f_alone > 5 and f_beside >= f_alone / 3
        checks["agent-answers"] = probes >= 5 and worst < 2.0
        # (its picture moves, and its square is in it; not in every one: the composer may copy the band between
        # the app's clearing it and drawing the square, graphics.md 5.2)
        checks["busy-draws"] = len(set(moving)) >= 2 and any(bytes((255, 255, 255)) in row for row in moving)
        info += " %dbpp steps/s alone=%.1f beside=%.1f probes=%d worst=%.2fs" % (bits, f_alone, f_beside, probes, worst)
        checks["busy-in-front"] = bool(until(focus("C")))
        agent.ssh("glos key 0x1D:down,0x38:down,0x2A:down,0x01,0x2A:up,0x38:up,0x1D:up")
        checks["hotkey"] = spin.wait(timeout=300) == 255 and "GLOS-APP kill id=" in agent.log() \
            and " reason=hotkey" in agent.log()

        # A session beside a window: the screen is the program's, then the desktop's again with the window as it was.
        a = start(app + "show A 900 60 50 240 160")
        checks["a-again"] = bool(until(focus("A"))) and picture("A")
        rc, o, _ = agent.ssh("C:\\TEST\\ECHOARGS.EXE beside a window")
        checks["session"] = rc == 7 and "beside a window" in o and bool(state("display on mode=")) and picture("A")
        # Another depth: the app is told its pixels are elsewhere, and draws again.
        checks["depth"] = agent.ssh("glos display on 640x480x%d" % bits2)[0] == 0 \
            and (" 640x480x%d " % bits2) in state("display on mode=") and picture("A")
        agent.ssh("glos key " + ALT_F4)
        ra = finish(a)
        checks["told"] = said(ra, 40, "EV A resize 240x160 now=240x160 bpp=%d" % bits2, "WIN-OK A closed")
        # Apps that end with a window up: the window goes, and the memory comes back.
        finish(start(app + "draw B 1"))
        m0 = mem()
        for i in range(10):
            checks["again-%d" % i] = finish(start(app + "draw %s 1" % "ABC"[i % 3]))[0] == 41
        m1 = mem()
        checks["memory-back"] = bool(m0) and m0 == m1
        if m0 != m1:
            print("    %s memory: %r then %r" % (tag, m0, m1))
        checks["windows-gone"] = not any(t.startswith("WINTEST") for t in desk()["win"])
        last = start(app + "show B 900")
        checks["last-up"] = bool(until(focus("B")))
        checks["exit"] = agent.ssh("glos exit")[0] == 0
        last.wait(timeout=120)
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    text = agent.wait()
    checks["text-at-exit"] = "HX-VMODE bios=03" in text
    checks["no-panic"] = "GLOS-PANIC" not in text and "GLOS-WARN" not in text and "GLOS-CRASH" not in text
    checks["done"] = "GLOS-EXIT code=0" in text and "HX-DONE 0" in text and "HX-VECCHK ok" in text
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s%s" % ("win-" + tag, "PASS" if not bad else "FAIL", info,
                               "" if not bad else " failed: " + " ".join(bad)), not bad


# (machine, video card, network card): the GPU lock and apps' VRAM (M8 steps B and C)
GPU_CASES = [("bf6", "g200", "ne2kpci"), ("bf6", "g400", "ne2kpci"), ("bf6", "g450", "ne2kpci"),
             ("486dx2", "g450pci", "ne2k")]


def gpu_case(profile, vcard, card):
    """The Matrox engine shared with apps (M8 steps B and C; kernel/gfx/gpu.c, tests/dos/gputest.c): GPUTEST draws
    with the engine from its own copy of the HAL into two buffers of its own VRAM and checks every frame's pixels
    through its view of VRAM; its window shows the buffers in turn. Two at once, while the desktop moves their
    windows (the composer takes the engine between their frames): no frame wrong, each had its turn taken from it
    and resynchronised. A session between frames: the apps' VRAM goes out to memory and comes back, and the frames
    go on right. A touch of the registers without the lock takes it. An app that keeps the lock in a loop is
    ended by the watchdog when the desktop needs the engine, and the next app draws right. Blocks of VRAM come
    back when their apps end. On an 8-bit desktop GLOS says there is no windowed GL, and a window can't show
    VRAM."""
    import time
    tag = "%s-%s" % (profile, vcard)
    agent = Agent("gpu-" + tag, profile, card, ["--card", vcard,
        "--file", "build/dj/GPUTEST.EXE=/TEST/GPUTEST.EXE", "--file", "build/ow/dos/ECHOARGS.EXE=/TEST/ECHOARGS.EXE",
        "--cmd", "SERSAY HX-START gpu", "--cmd", "VECCHK save", "--cmd", "C:\\TEST\\GLOS.EXE",
        "--cmd", "VECCHK check", "--cmd", "VMODE", "--cmd", "SERSAY HX-DONE 0"], timeout=3000)
    agent.opts += ["-o", "ControlMaster=auto", "-o", "ControlPath=" + os.path.join(agent.tmp, "cm"),
                   "-o", "ControlPersist=600"]
    out = os.path.join(ROOT, "out", "gpu-" + tag)
    app = "C:\\TEST\\GPUTEST.EXE "
    checks, info = {"listen": agent.listening()}, ""
    BORDER, TITLE = 1, 18

    def start(cmd):
        return subprocess.Popen(agent.opts + ["glos@127.0.0.1", cmd], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def finish(p, limit=900):
        o, e = p.communicate(timeout=limit)
        return p.returncode, o.decode("latin-1").replace("\r", ""), e.decode("latin-1")

    def display():
        return agent.ssh("glos display")[1].replace("\r", "")

    def state(want, limit=60):
        t0 = time.time()
        while time.time() - t0 < limit:
            line = display()
            if line.startswith(want):
                return line
            time.sleep(0.5)
        return ""

    def desk():
        text = agent.ssh("glos desk")[1].replace("\r", "")
        d = {}
        for w in re.finditer(r'^window (\d+) (\w+)(?: \w+)? at (-?\d+),(-?\d+) (\d+)x(\d+) "(.*)"$', text, re.M):
            d[w.group(7)] = {"id": int(w.group(1)), "x": int(w.group(3)), "y": int(w.group(4)), "w": int(w.group(5)),
                             "h": int(w.group(6))}
        return d

    def until(name, limit=60):
        t0 = time.time()
        while time.time() - t0 < limit:
            if "GPUTEST " + name in desk():
                return True
            time.sleep(0.5)
        return False

    def shot(name):
        p = subprocess.run(agent.opts + ["glos@127.0.0.1", "glos shot"], capture_output=True, timeout=300)
        open(os.path.join(out, name + ".png"), "wb").write(p.stdout)
        return png_rgb(p.stdout) if p.returncode == 0 else None

    def picture(name, tries=6):                 # the app's colour near its client area's bottom (no square there)
        got = None
        for i in range(tries):
            w = desk().get("GPUTEST " + name)
            pic = shot("%s-%d" % (name, i))
            if w and pic:
                x, y = w["x"] + BORDER + w["w"] // 2, w["y"] + BORDER + TITLE + BORDER + w["h"] - 10
                got = tuple(pic[2][(y * pic[0] + x) * 3:(y * pic[0] + x) * 3 + 3])
                if all(abs(got[k] - WIN_COLOUR[name][k]) <= 8 for k in range(3)):
                    return True
            time.sleep(1)
        print("    %s window %s: %r, should be %r" % (tag, name, got, WIN_COLOUR[name]))
        return False

    def ok_line(res, name, closed=False):
        m = re.search(r"^GPU-OK %s frames=(\d+) bad=(\d+) worst=(\d+) lost=(\d+) resyncs=(\d+)" % name, res[1], re.M)
        good = bool(m) and res[0] == (40 if closed else 41) and m.group(2) == "0" and int(m.group(1)) >= 10
        if not good:
            print("    %s GPUTEST %s: rc=%d %r" % (tag, name, res[0], res[1][-600:]))
        return (good, int(m.group(4)) if m else 0, int(m.group(1)) if m else 0)

    def free_kb():
        m = re.search(r" free=(\d+)K", display())
        return int(m.group(1)) if m else -1

    try:
        checks["on"] = agent.ssh("glos display on 640x480x16")[0] == 0 and " 640x480x16 " in state("display on mode=")
        rc, o, _ = agent.ssh(app + "info")
        checks["info"] = rc == 0 and caps_of(o) & 3 == 3 and re.search(r"GPU-INFO device=05(20|21|25) ", o) is not None
        checks["stat"] = "gpu owner=" in display()
        free0 = free_kb()

        # Two at once while the desktop moves their windows: every frame right, turns taken from both.
        a = start(app + "draw A 40 40 40 200 120")
        b = start(app + "draw B 40 300 60 200 120")
        checks["up"] = until("A") and until("B")
        checks["pictures"] = picture("A") and picture("B")
        for i in range(6):                      # (Alt-F7 and arrows: the composer redraws as each window moves)
            agent.ssh("glos key 0x38:down,0x41,0x38:up")
            agent.ssh("glos key %s,0x1C" % ",".join(["0x14D" if i % 2 else "0x14B"] * 4))
            agent.ssh("glos key 0x38:down,0x0F,0x38:up")
        ra, rb = finish(a), finish(b)
        (oka, lost_a, fa), (okb, lost_b, fb) = ok_line(ra, "A"), ok_line(rb, "B")
        checks["two"] = oka and okb
        checks["turns-taken"] = lost_a > 0 and lost_b > 0
        info += " frames=%d,%d lost=%d,%d" % (fa, fb, lost_a, lost_b)

        # A session between frames: their VRAM out and back, the frames still right.
        a = start(app + "draw A 40 40 40 200 120")
        checks["a-again"] = until("A")
        rc, o, _ = agent.ssh("C:\\TEST\\ECHOARGS.EXE beside a GL window")
        checks["session"] = rc == 7 and bool(state("display on mode="))
        checks["after-session"] = picture("A")
        checks["vram-out-in"] = "GLOS-MGA vram-out app=" in agent.log() and "GLOS-MGA vram-in app=" in agent.log()
        checks["a-right"] = ok_line(finish(a), "A")[0]

        # The registers touched without the lock.
        rc, o, _ = agent.ssh(app + "raw")
        checks["raw"] = rc == 0 and re.search(r"^GPU-RAW status=\w+ held=1", o, re.M) is not None
        checks["faults"] = re.search(r"gpu owner=\S+ turns=\d+ faults=[1-9]", display()) is not None

        # Kept in a loop: the watchdog, when the desktop needs the engine.
        h = start(app + "hang H")
        t0 = time.time()
        while time.time() - t0 < 60 and "owner=app" not in display():
            time.sleep(0.5)
        checks["hang-holds"] = "owner=app" in display()
        shot("hang")                            # (glos shot syncs the engine: the desktop wants it)
        checks["watchdog"] = h.wait(timeout=300) == 255 and "GLOS-GPU watchdog app=" in agent.log() \
            and "reason=gpu-watchdog" in agent.log()
        checks["after-watchdog"] = ok_line(finish(start(app + "draw C 6 300 200 160 100")), "C")[0]

        # Blocks of VRAM come back when their apps end.
        for i in range(10):
            checks["vram-%d" % i] = agent.ssh(app + "vram 512")[0] == 0
        free1 = free_kb()
        checks["vram-back"] = free0 > 0 and free1 == free0
        info += " free=%d,%d" % (free0, free1)

        # 8 bits: no windowed GL, and no window shows VRAM.
        checks["on-8"] = agent.ssh("glos display on 640x480x8")[0] == 0 and " 640x480x8 " in state("display on mode=")
        rc, o, _ = agent.ssh(app + "info")
        checks["info-8"] = rc == 0 and caps_of(o) & 3 == 1
        rc, o, _ = agent.ssh(app + "draw A 3")
        checks["refused-8"] = rc == 45 and "GPU-ERR A winvram error=8" in o
        checks["exit"] = agent.ssh("glos exit")[0] == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    text = agent.wait()
    checks["text-at-exit"] = "HX-VMODE bios=03" in text
    checks["no-panic"] = "GLOS-PANIC" not in text and "GLOS-WARN" not in text and "GLOS-CRASH" not in text
    checks["done"] = "GLOS-EXIT code=0" in text and "HX-DONE 0" in text and "HX-VECCHK ok" in text
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s%s" % ("gpu-" + tag, "PASS" if not bad else "FAIL", info,
                               "" if not bad else " failed: " + " ".join(bad)), not bad


# (machine, video card, network card): GLOS's mixer (M8 step E); it is the desktop's, on any card
AUDIO_CASES = [("bf6", "g200", "ne2kpci"), ("486dx2", "vbe", "ne2k")]


def audio_case(profile, vcard, card):
    """GLOS's mixer (M8 step E; kernel/drv/mixer.c, sb16.c, tests/dos/audtest.c): with the desktop up GLOS plays
    the SB16 itself; AUDTEST's tone through a stream, then two streams of other rates and channels at once, then
    one while a session (a DOS program) has the card: no underrun in any while it was fed, the card given to the
    session and taken back, every tone in 86Box's recording, the card stopped when GLOS leaves (SBCHK)."""
    import time
    tag = "%s-%s" % (profile, vcard)
    agent = Agent("audio-" + tag, profile, card, ["--card", vcard, "--sound", "sb16", "--wav",
        "--pre", "SET BLASTER=A220 I5 D1 H5 T6",
        "--file", "build/dj/AUDTEST.EXE=/TEST/AUDTEST.EXE", "--file", "build/ow/dos/ECHOARGS.EXE=/TEST/ECHOARGS.EXE",
        "--cmd", "SERSAY HX-START audio", "--cmd", "VECCHK save", "--cmd", "C:\\TEST\\GLOS.EXE",
        "--cmd", "VECCHK check", "--cmd", "SBCHK", "--cmd", "VMODE", "--cmd", "SERSAY HX-DONE 0"], timeout=2400)
    agent.opts += ["-o", "ControlMaster=auto", "-o", "ControlPath=" + os.path.join(agent.tmp, "cm"),
                   "-o", "ControlPersist=600"]
    app = "C:\\TEST\\AUDTEST.EXE "
    checks, info = {"listen": agent.listening()}, ""

    def start(cmd):
        return subprocess.Popen(agent.opts + ["glos@127.0.0.1", cmd], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def finish(p, limit=600):
        o, e = p.communicate(timeout=limit)
        return p.returncode, o.decode("latin-1").replace("\r", "")

    def played(res, name):
        m = re.search(r"^AUD-OK %s periods=(\d+) underruns=(\d+)" % name, res[1], re.M)
        good = res[0] == 41 and bool(m) and int(m.group(1)) >= 100 and m.group(2) == "0"
        if not good:
            print("    %s AUDTEST %s: rc=%d %r" % (tag, name, res[0], res[1][-400:]))
        return good, (int(m.group(1)) if m else 0)

    try:
        checks["idle"] = agent.ssh("glos audio")[1].startswith("audio idle ")
        checks["on"] = agent.ssh("glos display on 640x480x16")[0] == 0
        t0 = time.time()
        while time.time() - t0 < 60 and not agent.ssh("glos audio")[1].startswith("audio on "):
            time.sleep(0.5)
        checks["mixer"] = agent.ssh("glos audio")[1].startswith("audio on rate=22050 ")
        rc, o, _ = agent.ssh(app + "info")
        checks["caps"] = rc == 0 and caps_of(o) & 4 == 4
        ok, n = played(finish(start(app + "tone A 440 6")), "A")
        checks["one"] = ok
        b, c = start(app + "tone B 1000 6 44100 2 32768"), start(app + "tone C 784 6 11025 1 8192")
        checks["two"] = played(finish(b), "B")[0] and played(finish(c), "C")[0]
        d = start(app + "tone D 523 20")
        time.sleep(4)
        rc, o, _ = agent.ssh("C:\\TEST\\ECHOARGS.EXE beside the mixer")
        checks["session"] = rc == 7
        checks["after-session"] = played(finish(d), "D")[0]
        log = agent.log()
        checks["handed-over"] = log.count("GLOS-SB start") >= 2 and "GLOS-SB stop" in log
        info += " periods=%d" % n
        checks["exit"] = agent.ssh("glos exit")[0] == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    text = agent.wait()
    wav = os.path.join(ROOT, "out", "audio-" + tag, "audio.wav")
    checks["recorded"] = os.path.exists(wav) and subprocess.run(
        [sys.executable, os.path.join(MGA, "tools/loopa/wavcheck.py"), wav, "--tone", "440", "--tone", "1000",
         "--tone", "784", "--tone", "523"], capture_output=True).returncode == 0
    checks["sb-stopped"] = re.search(r"HX-SBCHK dma=\d+ stopped", text) is not None
    checks["no-panic"] = "GLOS-PANIC" not in text and "GLOS-WARN" not in text and "GLOS-CRASH" not in text
    checks["done"] = "GLOS-EXIT code=0" in text and "HX-DONE 0" in text and "HX-VECCHK ok" in text
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s%s" % ("audio-" + tag, "PASS" if not bad else "FAIL", info,
                               "" if not bad else " failed: " + " ".join(bad)), not bad


# (machine, video card, network card, desktop): windowed DOS-GL (M8 step D)
GLWIN_CASES = [("bf6", "g200", "ne2kpci", "800x600x16"), ("bf6", "g400", "ne2kpci", "1024x768x16"),
               ("bf6", "g450", "ne2kpci", "1024x768x16")]


def glwin_case(profile, vcard, card, desk):
    """Windowed DOS-GL (M8 step D; DOS-GL's src/dgl/glos.c, kernel/gfx/gpu.c): DOS-GL's conformance programs
    (build/dosgl/conform, from DOSGL_DIR: make dosgl-inputs), the same EXEs, run full-screen on plain DOS and then
    each in a window on a GLOS desktop, which it opens itself; every frame each writes (glReadPixels of its own
    buffer) identical between the two. Under GLOS each is an app with a window and its turns with the engine. Then
    a forced GL hang: TEXCUBE --hang keeps the engine in the middle of a frame until the watchdog ends it, and the
    next program draws right."""
    import time
    tag = "%s-%s" % (profile, vcard)
    src = os.path.join(ROOT, "build/dosgl/conform")
    tests = sorted(f for f in os.listdir(src) if re.match(r"T\d\d\.EXE$", f)) if os.path.isdir(src) else []
    if os.environ.get("GLWIN_TESTS"):            # e.g. GLWIN_TESTS=T01,T05
        tests = [t for t in tests if t[:-4] in os.environ["GLWIN_TESTS"].split(",")]
    files = []
    for t in tests:
        files += ["--file", "%s=/TEST/%s" % (os.path.join(src, t), t)]
    checks, info = {"tests": bool(tests)}, " tests=%d" % len(tests)
    texcube = os.path.join(ROOT, "build/dosgl/sdl/TEXCUBE.EXE")
    # Full-screen, on plain DOS.
    dos = []
    for t in tests:
        dos += ["--cmd", "C:\\TEST\\%s --noexit" % t]
    name = "glwin-dos-" + tag
    run(name, ["--machine", profile, "--card", vcard, "--timeout", "2400", "--idle", "240"] + files
        + ["--cmd", "SERSAY HX-START glwin"] + dos + ["--cmd", "SERSAY HX-DONE 0"])
    dos_log = open(os.path.join(ROOT, "out", name, "serial.log"), "rb").read().decode("latin-1") \
        if os.path.exists(os.path.join(ROOT, "out", name, "serial.log")) else ""
    checks["dos-ran"] = dos_log.count("HX-DONE 0") == len(tests) + 1
    # Windowed, on GLOS's desktop.
    agent = Agent("glwin-" + tag, profile, card, ["--card", vcard] + files + [
        "--file", texcube + "=/TEST/TEXCUBE.EXE", "--cmd", "SERSAY HX-START glwin", "--cmd", "VECCHK save", "--cmd", "C:\\TEST\\GLOS.EXE",
        "--cmd", "VECCHK check", "--cmd", "VMODE", "--cmd", "SERSAY HX-DONE 0"], timeout=3600)
    checks["listen"] = agent.listening()
    try:
        checks["on"] = agent.ssh("glos display on " + desk)[0] == 0
        t0 = time.time()
        while time.time() - t0 < 60 and not agent.ssh("glos display")[1].startswith("display on mode="):
            time.sleep(0.5)
        bad_rc = []
        for t in tests:
            rc, o, e = agent.ssh("C:\\TEST\\%s --noexit" % t, timeout=900)
            if rc != 0:
                bad_rc.append("%s:%d" % (t[:-4], rc))
        checks["windowed-ran"] = not bad_rc
        if bad_rc:
            print("    %s windowed: %s" % (tag, " ".join(bad_rc)))
        gpu = agent.ssh("glos display")[1]
        m = re.search(r"gpu owner=\S+ turns=(\d+)", gpu)
        checks["turns"] = bool(m) and int(m.group(1)) > len(tests)
        # A forced GL hang (the exit's fourth check): TEXCUBE begins a frame and never finishes it; the desktop
        # wants the engine (glos shot), the watchdog ends the program and resets the engine, and GL draws right
        # again after.
        mark = len(agent.log())
        h = subprocess.Popen(agent.opts + ["glos@127.0.0.1", "C:\\TEST\\TEXCUBE.EXE --frames 300 --hang --noexit"],
                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        t0 = time.time()
        while time.time() - t0 < 300 and "HX-TEXCUBE hang" not in agent.log()[mark:]:
            time.sleep(1)
        time.sleep(2)
        subprocess.run(agent.opts + ["glos@127.0.0.1", "glos shot"], capture_output=True, timeout=300)
        try:
            hrc = h.wait(timeout=300)
        except subprocess.TimeoutExpired:
            h.kill()
            hrc = None
        t = agent.log()[mark:]
        checks["hang-ended"] = hrc == 255 and "GLOS-GPU watchdog" in t and "reason=gpu-watchdog" in t
        if tests:
            again = agent.ssh("C:\\TEST\\%s --noexit" % tests[0], timeout=900)[0] == 0
            checks["after-hang"] = again
        checks["exit"] = agent.ssh("glos exit")[0] == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    text = agent.wait()
    checks["apps"] = text.count("GLOS-APP start id=") >= len(tests)
    checks["windowed-done"] = text.count("HX-DONE 0") >= len(tests) + 1
    # The frames: the same bytes.
    a_dir, b_dir = os.path.join(ROOT, "out", name, "files"), os.path.join(ROOT, "out", "glwin-" + tag, "files")
    frames = sorted(f for f in os.listdir(a_dir) if f.upper().endswith(".PPM")) if os.path.isdir(a_dir) else []
    differ = [f for f in frames if not os.path.exists(os.path.join(b_dir, f))
              or open(os.path.join(a_dir, f), "rb").read() != open(os.path.join(b_dir, f), "rb").read()]
    checks["frames"] = bool(frames) and not differ
    info += " frames=%d" % len(frames)
    if differ:
        print("    %s frames that differ: %s" % (tag, " ".join(differ)))
    checks["no-panic"] = "GLOS-PANIC" not in text and "GLOS-WARN" not in text and "GLOS-CRASH" not in text
    checks["done"] = "GLOS-EXIT code=0" in text and "HX-VECCHK ok" in text
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s%s" % ("glwin-" + tag, "PASS" if not bad else "FAIL", info,
                               "" if not bad else " failed: " + " ".join(bad)), not bad


# (machine, video card, network card, desktop): SDL3 windowed (M8 step F)
SDLWIN_CASES = [("bf6", "g450", "ne2kpci", "800x600x16"), ("bf6", "g200", "ne2kpci", "800x600x16")]


def sdlwin_case(profile, vcard, card, desk):
    """SDL3 windowed (M8 step F; DOS-GL's SDL patches 0008 and 0009, build/dosgl/sdl from DOSGL_DIR): DOS-GL's SDL
    examples on a GLOS desktop with the SB16 the mixer's. SDLINFO finds SDL's glos video and audio drivers; SDLKEYS
    gets the keys typed into its window; SDLBEEP plays through the mixer with no underrun, also with a main thread
    that yields once in 37 ms; SDLGL's 150th frame, drawn through SDL in a window, equals TEXCUBE's in a window and
    TEXCUBE's full-screen on plain DOS (DOS-GL's snapshots, DGL_SNAP in GLOS.CFG profiles); SDLGL makes a window in
    each size SDL lists; SDLCRASH dies and GLOS ends it, its stream and window gone, and the next program plays;
    DOS-GL's exit() inside a swap ends the app cleanly."""
    import time
    tag = "%s-%s" % (profile, vcard)
    src = os.path.join(ROOT, "build/dosgl/sdl")
    exes = ["SDLINFO", "SDLKEYS", "SDLBEEP", "SDLGL", "SDLCRASH", "TEXCUBE"]
    out = os.path.join(ROOT, "out", "sdlwin-" + tag)
    os.makedirs(out, exist_ok=True)
    files = []
    for e in exes:
        files += ["--file", "%s=/TEST/%s.EXE" % (os.path.join(src, e + ".EXE"), e)]
    files += ["--file", "%s=/TEST/SDLGLX.EXE" % os.path.join(src, "SDLGL.EXE")]
    cfg = os.path.join(ROOT, "out", "sdlwin-%s.cfg" % tag)        # (not in the run's own directory: run.py empties it)
    with open(cfg, "w") as f:
        f.write("[program SDLGL.EXE]\nenv = DGL_SNAP=150\nenv = DGL_STATS=1\n"
                "[program TEXCUBE.EXE]\nenv = DGL_SNAP=150\n"
                "[program SDLGLX.EXE]\nenv = DGL_EXIT_AFTER=30\n")
    checks, info = {"inputs": all(os.path.exists(os.path.join(src, e + ".EXE")) for e in exes)}, ""
    blaster = "SET BLASTER=A220 I5 D1 H5 T6"
    # TEXCUBE full-screen on plain DOS: the reference frame.
    name = "sdlwin-dos-" + tag
    run(name, ["--machine", profile, "--card", vcard, "--timeout", "900", "--idle", "120",
               "--file", "%s=/TEST/TEXCUBE.EXE" % os.path.join(src, "TEXCUBE.EXE"), "--pre", "SET DGL_SNAP=150",
               "--cmd", "SERSAY HX-START sdlwin", "--cmd", "C:\\TEST\\TEXCUBE.EXE --frames 150 --noexit",
               "--cmd", "SERSAY HX-DONE 0"])
    ref = os.path.join(ROOT, "out", name, "files", "F00150.PPM")
    checks["reference"] = os.path.exists(ref)
    agent = Agent("sdlwin-" + tag, profile, card, ["--card", vcard, "--sound", "sb16", "--wav", "--pre", blaster,
        "--file", cfg + "=/TEST/GLOS.CFG"] + files + [
        "--cmd", "SERSAY HX-START sdlwin", "--cmd", "VECCHK save", "--cmd", "C:\\TEST\\GLOS.EXE",
        "--cmd", "VECCHK check", "--cmd", "SBCHK", "--cmd", "VMODE", "--cmd", "SERSAY HX-DONE 0"], timeout=3600)
    agent.opts += ["-o", "ControlMaster=auto", "-o", "ControlPath=" + os.path.join(agent.tmp, "cm"),
                   "-o", "ControlPersist=600"]
    checks["listen"] = agent.listening()

    def start(cmd):
        return subprocess.Popen(agent.opts + ["glos@127.0.0.1", cmd], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def since(mark):                            # the log from a point on
        return agent.log()[mark:]

    def fetch(remote, local):
        p = subprocess.run(["scp", "-q", "-P", str(agent.port)] + agent.opts[agent.opts.index("-o"):] +
                           ["glos@127.0.0.1:" + remote, local], capture_output=True, timeout=300)
        return p.returncode == 0 and os.path.exists(local)

    try:
        checks["on"] = agent.ssh("glos display on " + desk)[0] == 0
        t0 = time.time()
        while time.time() - t0 < 60 and not agent.ssh("glos audio")[1].startswith("audio on "):
            time.sleep(0.5)
        # info
        m = len(agent.log())
        rc, o, _ = agent.ssh("C:\\TEST\\SDLINFO.EXE --noexit")
        t = since(m)
        checks["info"] = rc == 0 and "drivers video=glos audio=glos" in t and "HX-TEST modes PASS" in t
        # keys
        m = len(agent.log())
        k = start("C:\\TEST\\SDLKEYS.EXE --noexit")
        t0 = time.time()
        while time.time() - t0 < 120 and "HX-TEST ready" not in since(m):
            time.sleep(0.5)
        time.sleep(1)
        for key in ("0x1C", "0x1E", "0x01"):
            agent.ssh("glos key " + key)
        k.wait(timeout=120)
        checks["keys"] = "HX-TEST escape PASS" in since(m)
        # beep, busy
        for what, args in (("beep", ""), ("busy", " --busy 37")):
            m = len(agent.log())
            rc, o, _ = agent.ssh("C:\\TEST\\SDLBEEP.EXE%s --noexit" % args)
            u = re.search(r"HX-TEST underruns (\w+) (\d+) of (\d+)", since(m))
            checks[what] = rc == 0 and bool(u) and u.group(1) == "PASS" and int(u.group(3)) > 0
            info += " %s=%s" % (what, "%s/%s" % (u.group(2), u.group(3)) if u else "?")
        # gl: TEXCUBE and SDLGL windowed, their 150th frames
        a, b = os.path.join(out, "texcube-150.ppm"), os.path.join(out, "sdlgl-150.ppm")
        rc1 = agent.ssh("C:\\TEST\\TEXCUBE.EXE --frames 150 --noexit", timeout=900)[0]
        got_a = fetch("/C/OUT/F00150.PPM", a)
        agent.ssh("del C:\\OUT\\F00150.PPM")
        m = len(agent.log())
        rc2 = agent.ssh("C:\\TEST\\SDLGL.EXE --frames 150 --noexit", timeout=900)[0]
        got_b = fetch("/C/OUT/F00150.PPM", b)
        hooks = re.findall(r"hooks=(\d+)", since(m))
        same = got_a and got_b and open(a, "rb").read() == open(b, "rb").read()
        checks["gl"] = rc1 == 0 and rc2 == 0 and same and bool(hooks) and int(hooks[-1]) > 0
        checks["gl-as-dos"] = same and os.path.exists(ref) and open(ref, "rb").read() == open(a, "rb").read()
        # modes
        m = len(agent.log())
        rc, o, _ = agent.ssh("C:\\TEST\\SDLGL.EXE --modes --noexit", timeout=900)
        mm = re.search(r"HX-TEST modes (\w+) (.*)", since(m))
        checks["modes"] = rc == 0 and bool(mm) and mm.group(1) == "PASS"
        info += " modes=%s" % (mm.group(2).strip() if mm else "?")
        # crash, then a program that plays
        m = len(agent.log())
        rc, o, _ = agent.ssh("C:\\TEST\\SDLCRASH.EXE --noexit", timeout=600)
        checks["crash-ended"] = "GLOS-APP" in since(m) and ("GLOS-CRASH" in since(m) or "reason=crash" in since(m)
                                                             or rc != 0)
        checks["crash-stream-gone"] = "stream " not in agent.ssh("glos audio")[1]
        m = len(agent.log())
        rc, o, _ = agent.ssh("C:\\TEST\\SDLBEEP.EXE --noexit")
        checks["after-crash"] = rc == 0 and "HX-TEST underruns PASS" in since(m)
        # exit() inside a swap
        m = len(agent.log())
        rc, o, _ = agent.ssh("C:\\TEST\\SDLGLX.EXE --frames 300 --noexit", timeout=600)
        checks["exit"] = "DGL-EXIT frames=30" in since(m) and "stream " not in agent.ssh("glos audio")[1]
        checks["desk-left"] = not re.search(r'"(sdlgl|sdlcrash|sdlkeys)"', agent.ssh("glos desk")[1])
        checks["glos-exit"] = agent.ssh("glos exit")[0] == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    text = agent.wait()
    wav = os.path.join(ROOT, "out", "sdlwin-" + tag, "audio.wav")
    checks["recorded"] = os.path.exists(wav) and subprocess.run(
        [sys.executable, os.path.join(MGA, "tools/loopa/wavcheck.py"), wav, "--tone", "440"],
        capture_output=True).returncode == 0
    checks["sb-stopped"] = re.search(r"HX-SBCHK dma=\d+ stopped", text) is not None
    checks["no-panic"] = "GLOS-PANIC" not in text and "GLOS-WARN" not in text
    checks["done"] = "GLOS-EXIT code=0" in text and "HX-VECCHK ok" in text
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s%s" % ("sdlwin-" + tag, "PASS" if not bad else "FAIL", info,
                               "" if not bad else " failed: " + " ".join(bad)), not bad


# (machine, video card, network card, desktop): the games windowed (M8 step G)
GAMES_CASES = [("bf6", "g450", "ne2kpci", "800x600x16"), ("bf6", "g200", "ne2kpci", "800x600x16")]


def games_case(profile, vcard, card, desk):
    """The games windowed (M8 step G; build/games from FIFTHWHEEL_DIR and DOSGL_DIR: make games-inputs): Fifth Wheel
    (its source unchanged, built against DOS-GL with SDL's glos drivers) driving itself for 1800 fixed-step frames,
    and PrBoom-plus's GL timedemo of DOOM's demo 1, each first full-screen on plain DOS (the same EXEs) and then in a
    window on a GLOS desktop with the SB16 the mixer's: Fifth Wheel's frame hashes the same, PrBoom's gametics, game-
    state checksum and frame 200 the same, no GL call refused, and on GLOS no chunk underran (SDL patch 0005's count:
    GLOS: U of N chunks underran)."""
    import time
    tag = "%s-%s" % (profile, vcard)
    g = os.path.join(ROOT, "build/games")
    out = os.path.join(ROOT, "out", "games-" + tag)
    os.makedirs(out, exist_ok=True)
    blaster = "SET BLASTER=A220 I5 D1 H5 T6"
    fw_args = "-test -fixed -autopilot -frames 1800 -hash -noexit"
    doom = "D:\\DOOM\\PRBOOMP.EXE -iwad D:\\DOOM\\DOOM.WAD -vidmode gl -timedemo demo1 -dosbench C:\\OUT D1GL1 " \
           "-dbshot 200 -checksum C:\\OUT\\D1GL1.SUM"
    doom_files = ["--games-file", os.path.join(g, "games.json"), "--game", "doom",
                  "--file", os.path.join(g, "PRBOOMP.EXE") + "=D:/DOOM/PRBOOMP.EXE",
                  "--file", os.path.join(g, "PRBOOM.WAD") + "=D:/DOOM/PRBOOM.WAD"]
    checks, info = {"inputs": all(os.path.exists(os.path.join(g, f)) for f in
                                  ("FWHEEL.EXE", "WORLD.PAK", "PRBOOMP.EXE", "PRBOOM.WAD", "games.json"))}, ""

    def hashes(text):
        return [l for l in text.split("\n") if l.startswith("FW-HASH")]

    def underran(text, what):
        m = re.findall(r"%s: (\d+) of (\d+) chunks underran" % what, text)
        return (int(m[-1][0]), int(m[-1][1])) if m else None

    def final(path):                            # the checksum file's last digest
        return [l.split()[-1] for l in open(path, errors="replace") if l.strip()][-1] if os.path.exists(path) else None

    only = os.environ.get("GAMES_GAME", "")            # fw or doom: that one alone (investigations)
    fw_on, doom_on = only in ("", "fw"), only in ("", "doom")
    # Full-screen, on plain DOS.
    name = "games-dos-" + tag
    run(name, ["--machine", profile, "--card", vcard, "--sound", "sb16", "--pre", blaster, "--mem", "64",
               "--timeout", "3600", "--idle", "300", "--file", os.path.join(g, "FWHEEL.EXE") + "=/TEST/FWHEEL.EXE",
               "--file", os.path.join(g, "WORLD.PAK") + "=/TEST/WORLD.PAK", "--pre", "SET DOOM_SERIAL=1",
               "--pre", "SET LFN=n"] + doom_files + ["--cmd", "SERSAY HX-START games"]
        + (["--cmd", "C:\\TEST\\FWHEEL.EXE " + fw_args] if fw_on else []) + (["--cmd", doom] if doom_on else [])
        + ["--cmd", "SERSAY HX-DONE 0"])
    dos = open(os.path.join(ROOT, "out", name, "serial.log"), "rb").read().decode("latin-1").replace("\r", "") \
        if os.path.exists(os.path.join(ROOT, "out", name, "serial.log")) else ""
    dos_hashes = hashes(dos)
    if fw_on:
        checks["dos-fw"] = len(dos_hashes) >= 30
    if doom_on:
        checks["dos-doom"] = "DOOM-TIMED gametics=1710" in dos
    dos_sum = final(os.path.join(ROOT, "out", name, "files", "D1GL1.SUM"))
    dos_shot = os.path.join(ROOT, "out", name, "files", "LD1GL1.PPM")
    # Windowed, on GLOS.
    cfg = os.path.join(ROOT, "out", "games-%s.cfg" % tag)         # (not in the run's own directory: run.py empties it)
    with open(cfg, "w") as f:
        f.write("[program PRBOOMP.EXE]\nenv = DOOM_SERIAL=1\nenv = LFN=n\n")
    agent = Agent("games-" + tag, profile, card, ["--card", vcard, "--sound", "sb16", "--pre", blaster, "--mem", "64",
        "--file", cfg + "=/TEST/GLOS.CFG", "--file", os.path.join(g, "FWHEEL.EXE") + "=/TEST/FWHEEL.EXE",
        "--file", os.path.join(g, "WORLD.PAK") + "=/TEST/WORLD.PAK"] + doom_files + [
        "--cmd", "SERSAY HX-START games", "--cmd", "VECCHK save", "--cmd", "C:\\TEST\\GLOS.EXE",
        "--cmd", "VECCHK check", "--cmd", "SBCHK", "--cmd", "VMODE", "--cmd", "SERSAY HX-DONE 0"], timeout=7200)
    checks["listen"] = agent.listening()

    def fetch(remote, local):
        p = subprocess.run(["scp", "-q", "-P", str(agent.port)] + agent.opts[agent.opts.index("-o"):] +
                           ["glos@127.0.0.1:" + remote, local], capture_output=True, timeout=300)
        return p.returncode == 0 and os.path.exists(local)

    try:
        checks["on"] = agent.ssh("glos display on " + desk)[0] == 0
        t0 = time.time()
        while time.time() - t0 < 60 and not agent.ssh("glos audio")[1].startswith("audio on "):
            time.sleep(0.5)
        m = len(agent.log())
        if fw_on:
            rc, o, _ = agent.ssh("C:\\TEST\\FWHEEL.EXE " + fw_args, timeout=3600)
            t = agent.log()[m:]
            u = underran(t, "GLOS")
            checks["fw-ran"] = rc == 0 and "GLOS-APP start" in t
            checks["fw-hashes"] = bool(dos_hashes) and hashes(t) == dos_hashes
            checks["fw-underruns"] = bool(u) and u[0] == 0 and u[1] > 0
            info += " fw-hashes=%d fw-under=%s" % (len(hashes(t)), "%d/%d" % u if u else "?")
        m = len(agent.log())
        rc, o, _ = agent.ssh(doom, timeout=3600) if doom_on else (0, "", "")
        t = agent.log()[m:]
        u = underran(t, "GLOS")
        if not doom_on:
            t = "DOOM-TIMED gametics=1710 GLOS-APP start"
            u = (0, 1)
        checks["doom-ran"] = "DOOM-TIMED gametics=1710" in t and "GLOS-APP start" in t
        checks["doom-underruns"] = bool(u) and u[0] == 0 and u[1] > 0
        checks["doom-gl"] = "DGL-STUB" not in t and "DGL-GLERR" not in t
        info += " doom-under=%s" % ("%d/%d" % u if u else "?")
        s = os.path.join(out, "D1GL1.SUM")
        checks["doom-checksum"] = not doom_on or fetch("/C/OUT/D1GL1.SUM", s) and dos_sum is not None and final(s) == dos_sum
        shot = os.path.join(out, "LD1GL1.PPM")
        checks["doom-frame"] = not doom_on or fetch("/C/OUT/LD1GL1.PPM", shot) and os.path.exists(dos_shot) \
            and open(shot, "rb").read() == open(dos_shot, "rb").read()
        checks["exit"] = agent.ssh("glos exit")[0] == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    text = agent.wait()
    checks["sb-stopped"] = re.search(r"HX-SBCHK dma=\d+ stopped", text) is not None
    checks["no-panic"] = "GLOS-PANIC" not in text and "GLOS-WARN" not in text
    checks["done"] = "GLOS-EXIT code=0" in text and "HX-VECCHK ok" in text
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s%s" % ("games-" + tag, "PASS" if not bad else "FAIL", info,
                               "" if not bad else " failed: " + " ".join(bad)), not bad


# (machine, video card, network card, the DOS mouse driver unloaded before GLOS starts)
WIN_PHYS_CASES = [("bf6", "vbe", "ne2kpci", False), ("486dx2", "vbe", "ne2k", True)]


def win_phys_case(profile, vcard, card, unload):
    """The desktop's keyboard and mouse as the PC has them: 86Box's own keyboard and PS/2 mouse (Loop A's --keys),
    through the 8042. The desktop is there from the start (GLOS.CFG); an app with a window starts; the mouse
    moves the pointer into the window and its button is pressed there, a key is typed, and Alt-F4 closes it.
    With CuteMouse loaded the mouse is on as DOS's driver set it; with CuteMouse unloaded before GLOS starts
    nobody has it on, and the desktop turns it on for itself."""
    import time
    tag = "%s-%s" % (profile, "nodriver" if unload else "ctmouse")
    script = ("@GLOS-APP start id=1,5:mouse:-100:-70,6:mouse:-100:-70,8:mouse:0:0:1,9:mouse:0:0:0,10:0x1E,"
              "12:0x38:down,12.5:0x3E,13:0x38:up")
    agent = Agent("winphys-" + tag, profile, card, ["--card", vcard, "--mouse", "ps2", "--keys", script,
        "--file", batfile("winphys-%s/GLOS.CFG" % tag, ["[display]", "mode = 640x480x8"]) + "=/TEST/GLOS.CFG",
        "--file", "build/dj/WINTEST.EXE=/TEST/WINTEST.EXE",
        "--cmd", "SERSAY HX-START winphys"] + (["--cmd", "CTMOUSE /U"] if unload else []) + [
        "--cmd", "VECCHK save", "--cmd", "C:\\TEST\\GLOS.EXE",
        "--cmd", "VECCHK check", "--cmd", "VMODE", "--cmd", "SERSAY HX-DONE 0"], timeout=1200)
    agent.opts += ["-o", "ControlMaster=auto", "-o", "ControlPath=" + os.path.join(agent.tmp, "cm"),
                   "-o", "ControlPersist=600"]
    checks, info = {"listen": agent.listening()}, ""
    try:
        t0 = time.time()
        while time.time() - t0 < 60 and not agent.ssh("glos display")[1].startswith("display on mode="):
            time.sleep(0.5)
        checks["desktop"] = agent.ssh("glos display")[1].startswith("display on mode=")
        p = subprocess.Popen(agent.opts + ["glos@127.0.0.1", "C:\\TEST\\WINTEST.EXE show A 300 40 40 200 120"],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        o, _ = p.communicate(timeout=600)
        o = o.decode("latin-1").replace("\r", "")
        m = re.search(r"^EV A mouse (-?\d+),(-?\d+) buttons=1 button=1 down=1$", o, re.M)
        checks["closed"] = p.returncode == 40 and "WIN-OK A closed" in o
        # (from the middle of the screen by -200, -140: 79, 40 in the client area, if a count is a pixel)
        checks["mouse"] = bool(m) and abs(int(m.group(1)) - 79) <= 40 and abs(int(m.group(2)) - 40) <= 40 \
            and bool(re.search(r"^EV A mouse -?\d+,-?\d+ buttons=0 button=1 down=0$", o, re.M))
        checks["key"] = "EV A key 01E ch=97 down=1 mods=0" in o and "EV A key 01E ch=0 down=0 mods=0" in o
        if m:
            info = " pressed at %s,%s" % m.groups()
        if not all(checks.values()):
            print("    %s: rc=%s out=%r" % (tag, p.returncode, o[-700:]))
        checks["exit"] = agent.ssh("glos exit")[0] == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    text = agent.wait()
    checks["started"] = "GLOS-APP start id=1" in text
    checks["no-panic"] = "GLOS-PANIC" not in text and "GLOS-WARN" not in text and "GLOS-CRASH" not in text
    checks["done"] = "GLOS-EXIT code=0" in text and "HX-DONE 0" in text and "HX-VECCHK ok" in text
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s%s" % ("winphys-" + tag, "PASS" if not bad else "FAIL", info,
                               "" if not bad else " failed: " + " ".join(bad)), not bad


APPS_CASES = [("bf6", "ne2kpci"), ("486dx2", "ne2k")]


def apps_run(profile, boot):
    """GLOS.EXE /RUN with a program that becomes an app: GLOS stays until the app ends, and leaves with its
    exit code."""
    tag = "%s-%s" % (profile, boot)
    args = ["--machine", profile, "--boot-cfg", boot, "--file", "build/dj/APPTEST.EXE=/TEST/APPTEST.EXE"] + GLOS_FILES
    for c in ["SERSAY HX-START apps", "VECCHK save", "C:\\TEST\\GLOS.EXE /RUN C:\\TEST\\APPTEST.EXE run R 2",
              "VECCHK check", "SERSAY HX-DONE 0"]:
        args += ["--cmd", c]
    st, serial = run("apps-run-" + tag, args)
    checks = {"status": st == "PASS", "app": "GLOS-APP start id=1 prog=APPTEST.EXE" in serial,
              "end": bool(re.search(r"GLOS-APP end id=1 prog=APPTEST.EXE code=57 how=0", serial)),
              "exit": "GLOS-EXIT code=57" in serial, "vectors": "HX-VECCHK ok" in serial,
              "no-panic": "GLOS-PANIC" not in serial}
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s" % ("apps-run-" + tag, "PASS" if not bad else "FAIL",
                             "" if not bad else " failed: " + " ".join(bad)), not bad


def apps_case(profile, card):
    """GLOS apps (M6 step A): APPTEST becomes an app (its own thread) and runs for some seconds in its own
    directory, checking between sleeps that its directory, its directory search (its DTA), its file's bytes,
    its FPU sum and its clock are its own. Alone; then two at once beside a third that loops for ever,
    while glos tick is asked again and again (the gate's rule: an answer within 2 s), each getting through
    at least a third of the turns a second it does alone; beside a session (a DOS command, which pauses
    them); beside one that crashes; one that runs a real-mode child; kills, by the job's number: of the
    spinner, of one asleep, of one waiting for the keyboard in DOS; ten that start and end, after which
    the kernel has the memory it had; glos exit with an app running. Refusals: glos run --exclusive, a
    second init, a program COMMAND.COM started.
    Step B, an app's own timer: uclock() never goes back and keeps time; a timer set to 100 Hz with a handler of
    the app's own counts 100 a second through sleeps, halts and busy loops, the BIOS's tick still the system's;
    four of those at once; and DJGPP 2.05's own TIMER, UCLOCK and SIGNALS tests (the last with FPU errors as
    signals), made apps without asking (glos run --app), keys typed with glos key."""
    import json
    import time
    tag = "%s-%s" % (profile, card)
    agent = Agent("apps-" + tag, profile, card, [
        "--file", "build/dj/APPTEST.EXE=/TEST/APPTEST.EXE", "--file", "build/ow/dos/ECHOARGS.EXE=/TEST/ECHOARGS.EXE",
        "--file", "build/dj/djtst/TIMER.EXE=/TEST/TIMER.EXE", "--file", "build/dj/djtst/UCLOCK.EXE=/TEST/UCLOCK.EXE",
        "--file", "build/dj/djtst/SIGNALS.EXE=/TEST/SIGNALS.EXE",
        "--cmd", "SERSAY HX-START apps", "--cmd", "VECCHK save", "--cmd", "C:\\TEST\\GLOS.EXE",
        "--cmd", "VECCHK check", "--cmd", "SERSAY HX-DONE 0"], timeout=2400)
    agent.opts += ["-o", "ControlMaster=auto", "-o", "ControlPath=" + os.path.join(agent.tmp, "cm"),
                   "-o", "ControlPersist=600"]
    app = "C:\\TEST\\APPTEST.EXE "
    checks, info = {"listen": agent.listening()}, ""

    def start(cmd):
        return subprocess.Popen(agent.opts + ["glos@127.0.0.1", cmd], stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def finish(p, limit=600):
        out, err = p.communicate(timeout=limit)
        return p.returncode, out.decode("latin-1").replace("\r", ""), err.decode("latin-1")

    def started(n, limit=240):                  # n apps have started since GLOS did
        t0 = time.time()
        while time.time() - t0 < limit and agent.log().count("GLOS-APP start ") < n:
            time.sleep(0.3)
        return agent.log().count("GLOS-APP start ") >= n

    rate = {}                                   # turns a second, by name

    def ran(res, name, seconds):
        rc, out, _ = res
        m = re.search(r"^APP-OK name=%s loops=(\d+) slept=\d+ found=12 " % name, out, re.M)
        if rc != 40 + ord(name) - 65 or not m:
            print("    %s run %s: rc=%d out=%r" % (tag, name, rc, out[-400:]))
            return False
        rate[name] = int(m.group(1)) / seconds
        return True

    def kill(pattern, p):                       # the job whose command ends so, by its number
        m = re.search(r"^job (\d+) running app \S+ %s$" % pattern, agent.ssh("glos ps")[1].replace("\r", ""), re.M)
        return bool(m) and agent.ssh("glos kill " + m.group(1))[0] == 0 and p.wait(timeout=300) == 255

    def mem():
        m = re.search(r"free_kb=(\d+)\n.*heap_kb=(\d+)", agent.ssh("glos mem")[1])
        return (int(m.group(1)), int(m.group(2))) if m else None

    try:
        checks["ver"] = agent.ssh("glos ver")[0] == 0
        checks["alone"] = ran(agent.ssh(app + "run A 3", timeout=600), "A", 3)
        rc, out, _ = agent.ssh(app + "plain")
        checks["plain"] = rc == 7 and "APP-PLAIN version=%x" % api_version() in out
        rc, out, _ = agent.ssh("glos run --exclusive " + app + "exit 5")
        checks["exclusive"] = rc == 3 and "APP-REFUSED error=5 reason=5" in out
        rc, out, _ = agent.ssh("glos run --app " + app + "exit 6")     # (an app before it asks: step B)
        checks["run-app"] = rc == 3 and "APP-REFUSED error=5 reason=1" in out
        rc, out, _ = agent.ssh(app + "twice")
        checks["twice"] = rc == 0 and "APP-TWICE r=-1 error=5 reason=1" in out
        checks["exit-code"] = agent.ssh(app + "exit 23")[0] == 23
        agent.ssh(app + "exit 9 > NUL")         # (through COMMAND.COM: not GLOS's own child; refused, in the log)
        n0 = agent.log().count("GLOS-APP start ")

        spin = start(app + "spin")
        checks["spin-started"] = started(n0 + 1)
        b, c = start(app + "run B 8"), start(app + "run C 8")
        checks["both-started"] = started(n0 + 3)
        worst, probes, ps, stat = 0.0, 0, "", None
        while b.poll() is None or c.poll() is None:
            t = time.time()
            rc, out, _ = agent.ssh("glos tick")
            worst = max(worst, time.time() - t)
            probes += rc == 0 and out.startswith("ticks=")
            # (a job is "running app" a moment after its app has started: asked again until all three are)
            if len(re.findall(r"^job \d+ running app ", ps, re.M)) < 3 and b.poll() is None and c.poll() is None:
                ps = agent.ssh("glos ps")[1]
                stat = json.loads(agent.ssh("glos stat")[1])
            time.sleep(0.2)
        checks["two-at-once"] = ran(finish(b), "B", 8) and ran(finish(c), "C", 8)
        checks["agent-answers"] = probes >= 5 and worst < 2.0
        checks["not-stalled"] = min(rate.get("B", 0), rate.get("C", 0)) >= rate.get("A", 1e9) / 3
        info += " probes=%d worst=%.2fs turns/s alone=%.1f beside=%.1f,%.1f" % (
            probes, worst, rate.get("A", 0), rate.get("B", 0), rate.get("C", 0))
        checks["ps"] = len(re.findall(r"^app \d+ APPTEST.EXE \S+ psp=\w+ thread=app\d+ ", ps, re.M)) == 3 \
            and len(re.findall(r"^job \d+ running app ", ps, re.M)) == 3
        checks["stat"] = bool(stat) and len(stat.get("apps", [])) == 3 \
            and all(x["program"] == "APPTEST.EXE" and x["held_kb"] > 0 for x in stat["apps"])
        if not checks["ps"] or not checks["stat"]:
            print("    %s ps=%r stat=%r" % (tag, ps, stat and stat.get("apps")))

        d = start(app + "run D 10")             # a session beside an app: the app waits, then runs on
        checks["d-started"] = started(n0 + 4)
        rc, out, err = agent.ssh("C:\\TEST\\ECHOARGS.EXE x y")
        checks["session"] = rc == 7 and out == "x y\r\nnine two six int29\r\n" and err == "echoargs: 2 arguments\r\n"
        checks["beside-session"] = ran(finish(d), "D", 10)

        e = start(app + "run E 10")             # a crash beside an app
        checks["e-started"] = started(n0 + 5)
        rc, out, _ = agent.ssh(app + "crash", timeout=600)
        checks["crash"] = rc == 255 and "APP-CRASH id=" in out
        checks["beside-crash"] = ran(finish(e), "E", 10)

        rc, out, err = agent.ssh(app + "child C:\\TEST\\ECHOARGS.EXE", timeout=600)
        checks["child"] = rc == 7 and "from an app\r\nnine two six int29\r\n" in out and "APP-CHILD-BACK r=7 ticks=1" in out \
            and err == "echoargs: 3 arguments\r\n"
        if not checks["child"]:
            print("    %s child: rc=%d out=%r err=%r" % (tag, rc, out, err))

        # (step B) an app's own timer
        rc, out, _ = agent.ssh(app + "uclock 3", timeout=600)
        checks["uclock"] = rc == 0 and "APP-UCLOCK-OK" in out
        rc, out2, _ = agent.ssh(app + "timer 100 3", timeout=600)
        checks["timer"] = rc == 0 and "APP-TIMER-OK" in out2
        if not checks["uclock"] or not checks["timer"]:
            print("    %s uclock=%r timer=%r" % (tag, out, out2))
        kill_spin = kill("spin", spin)          # (four jobs at once at most: the spinner makes room)
        # (other programs start meanwhile: the kernel writes its log a line at a time, 9 ms for a hundred
        # characters, and only urgent threads run until a line is out, which costs a 250 Hz timer an interrupt)
        four = [start(app + c) for c in ("uclock 4", "uclock 4", "timer 100 4", "timer 250 4 7")]
        res = [finish(p) for p in four]
        checks["timers-at-once"] = all(r[0] == 0 and "-OK " in r[1] for r in res)
        if not checks["timers-at-once"]:
            print("    %s at once: %r" % (tag, [r[1] for r in res]))
        agent.ssh(app + "drain")                # (a key from the boot waits in the BIOS's buffer)
        for name, keys in (("TIMER", "4:0x39"), ("UCLOCK", "3:0x39"), ("SIGNALS", "2:0x39,10:0x10")):
            n2 = agent.log().count("GLOS-APP start ")
            p = start("glos run --app C:\\TEST\\%s.EXE" % name)
            forced = started(n2 + 1)
            agent.ssh("glos key " + keys, timeout=120)
            rc, out, err = finish(p)
            lines = [l.replace("\t", ".") for l in (out + err).splitlines()]     # (as RUNOUT shows a tab: dj_own)
            want = DJ_EXPECT.get(name)
            why = dj_own(name, lines) if forced else "never an app"
            if not why and want and (rc != want[0] or not all(any(w in l for l in lines) for w in want[1])):
                why = "rc=%d" % rc
            if not why and not want and rc != 0:
                why = "rc=%d" % rc
            checks["dj-" + name.lower()] = not why
            if why:
                print("    %s %s as an app: %s; last lines %r" % (tag, name, why, lines[-4:]))
        checks["kill"] = kill_spin
        n1 = agent.log().count("GLOS-APP start ")
        nap = start(app + "sleep 600")
        checks["kill-asleep"] = started(n1 + 1) and kill("sleep 600", nap)
        key = start(app + "key")                # (in DOS for good: the system VM's kill, after its two seconds)
        checks["kill-in-dos"] = started(n1 + 2) and kill("key", key)
        checks["none-left"] = not re.search(r"^app ", agent.ssh("glos ps")[1], re.M)
        rc, out, err = agent.ssh("C:\\TEST\\ECHOARGS.EXE after kills")     # (DOS as it was: INT 29h counts outside DOS)
        checks["dos-after-kill"] = rc == 7 and out == "after kills\r\nnine two six int29\r\n"
        m0 = mem()
        for i in range(10):
            checks["again-%d" % i] = agent.ssh(app + "exit %d" % (30 + i))[0] == 30 + i
        m1 = mem()
        checks["memory-back"] = bool(m0) and m0 == m1
        if not checks["memory-back"]:
            info += " mem=%r->%r" % (m0, m1)

        last = start(app + "spin")              # glos exit ends it first
        t0 = time.time()
        while time.time() - t0 < 240 and not re.search(r"^app ", agent.ssh("glos ps")[1], re.M):
            time.sleep(0.5)
        checks["exit"] = agent.ssh("glos exit")[0] == 0 and last.wait(timeout=300) == 255
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    text = agent.wait()
    checks["balanced"] = text.count("GLOS-APP start ") == text.count("GLOS-APP end ") == 33
    checks["killed-in-dos"] = bool(re.search(r"GLOS-KILL psp=\w+ at=\S+ ticks=\d+ reason=agent", text))
    checks["refused"] = len(re.findall(r"GLOS-APP refuse psp=\w+ why=(exclusive|already|parent)", text)) == 4
    checks["crash-report"] = bool(re.search(r"GLOS-CRASH why=exception vec=0e .*prog=C:\\TEST\\APPTEST.EXE", text)) \
        and bool(re.search(r"GLOS-CRASH file=C:\\GLOS\\CRASH\\CRASH\d+\.TXT", text))
    checks["no-panic"] = "GLOS-PANIC" not in text and "GLOS-APP error" not in text and "GLOS-APP warn" not in text
    checks["done"] = "GLOS-EXIT code=0" in text and "HX-DONE 0" in text and "HX-VECCHK ok" in text
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s%s" % ("apps-" + tag, "PASS" if not bad else "FAIL", info,
                               "" if not bad else " failed: " + " ".join(bad)), not bad


def exit_case(profile, vcard):
    """PRD §19's exit for M5. SHOTDJ v112k shows three pictures (640x480 in 32 bits), each until a key.
    The twin: plain DOS (CWSDPMI), Loop A's --keys anchored on the program's lines, 86Box's own screenshots
    (--shots). Then the same under GLOS's agent with nothing but ssh: glos log --follow tells when each
    picture is up, glos shot takes it, glos key sends the key. The program's lines must be the same from both,
    and each picture the same as 86Box's and as the one the host computes."""
    import threading
    import time
    sys.path.insert(0, os.path.join(MGA, "tools", "loopa"))
    import png as mga_png
    tag = "%s-%s" % (profile, vcard)
    files = ["--file", "build/dj/SHOTDJ.EXE=/TEST/SHOTDJ.EXE"]
    keys = ",".join("@step=%d w=,45:0x%x" % (k, c) for k, c in enumerate(EXIT_KEYS))
    host = run("m5exit-host-" + tag, ["--machine", profile, "--card", vcard, "--timeout", "500", "--idle", "150"] + files + [
        "--cmd", "SERSAY HX-START exit", "--cmd", "C:\\TEST\\SHOTDJ.EXE v112k", "--cmd", "SERSAY HX-DONE 0",
        "--keys", keys, "--shots", "30,80,130"], background=True)
    agent = Agent("m5exit-ssh-" + tag, profile, "ne2kpci", ["--card", vcard] + files + [
        "--cmd", "SERSAY HX-START exit", "--cmd", "VECCHK save", "--cmd", "C:\\TEST\\GLOS.EXE",
        "--cmd", "VECCHK check", "--cmd", "SERSAY HX-DONE 0"], timeout=900)
    out = os.path.join(ROOT, "out", "m5exit-ssh-" + tag)
    checks, info, shots, seen = {"listen": agent.listening()}, "", [], []
    try:
        follow = subprocess.Popen(agent.opts + ["glos@127.0.0.1", "glos log --follow"], stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL)

        def reader():
            for line in iter(follow.stdout.readline, b""):
                seen.append(line.decode("latin-1"))
        threading.Thread(target=reader, daemon=True).start()
        job = subprocess.Popen(agent.opts + ["glos@127.0.0.1", "C:\\TEST\\SHOTDJ.EXE v112k"], stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
        for k, code in enumerate(EXIT_KEYS):
            t0 = time.time()
            while time.time() - t0 < 180 and not any("step=%d w=" % k in l for l in seen):
                time.sleep(0.3)
            p = subprocess.run(agent.opts + ["glos@127.0.0.1", "glos shot"], capture_output=True, timeout=180)
            open(os.path.join(out, "step-%d.png" % k), "wb").write(p.stdout)
            shots.append(png_rgb(p.stdout))
            checks["key-%d" % k] = agent.ssh("glos key 0x%x" % code)[0] == 0
        checks["end"] = job.wait(timeout=180) == 0
        time.sleep(2)                           # (the last key's line, into the followed log)
        follow.terminate()
        checks["exit"] = agent.ssh("glos exit")[0] == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    agent.wait()
    host.wait()
    hdir = os.path.join(ROOT, "out", "m5exit-host-" + tag)
    serial = open(os.path.join(hdir, "serial.log"), "rb").read().decode("latin-1").replace("\r", "") \
        if os.path.exists(os.path.join(hdir, "serial.log")) else ""
    lines_host = re.findall(r"^HX-SHOT .*$", serial, re.M)
    lines_ssh = [l.strip() for l in seen if l.startswith("HX-SHOT ")]
    checks["lines"] = len(lines_host) == 6 and lines_host == lines_ssh
    if not checks["lines"]:
        info += " host=%r ssh=%r" % (lines_host, lines_ssh)
    steps = [l for l in lines_host if " step=" in l]
    for k in range(3):
        f = dict(kv.split("=", 1) for kv in steps[k].split() if "=" in kv) if k < len(steps) else None
        want = shot_want(f) if f else None
        ours = shots[k] if k < len(shots) else None
        try:
            box = mga_png.read_png(os.path.join(hdir, "screen-%d.png" % k))
        except (OSError, ValueError, AssertionError):
            box = None
        checks["shot-%d" % k] = bool(f and ours and ours[:2] == (int(f["w"]), int(f["h"])) and ours[2] == want)
        checks["86box-%d" % k] = bool(ours and box and tuple(box[:2]) == ours[:2] and bytes(box[2]) == ours[2])
        if not checks["86box-%d" % k]:
            info += " 86box-%d=%s" % (k, tuple(box[:2]) if box else None)
    bad = [k for k, v in checks.items() if not v]
    return "  %-26s %s%s%s" % ("m5exit-" + tag, "PASS" if not bad else "FAIL",
                               "" if not bad else " failed: " + " ".join(bad), info), not bad


# ---- the toolkit and the native apps (M9): test scripts played over ssh against the host's goldens

GUI_KEYS = {"esc": 0x01, "backspace": 0x0E, "tab": 0x0F, "enter": 0x1C, "space": 0x39, "f1": 0x3B, "f2": 0x3C,
            "f3": 0x3D, "f4": 0x3E, "f5": 0x3F, "f6": 0x40, "f7": 0x41, "f8": 0x42, "f9": 0x43, "f10": 0x44,
            "f11": 0x57, "f12": 0x58, "home": 0x147, "up": 0x148, "pgup": 0x149, "left": 0x14B, "right": 0x14D,
            "end": 0x14F, "down": 0x150, "pgdn": 0x151, "ins": 0x152, "del": 0x153, "win": 0x15B}
GUI_ROW = "\0\0" "1234567890-=" "\0\0" "qwertyuiop[]" "\0\0" "asdfghjkl;'`" "\0\\" "zxcvbnm,./"
GUI_MODS = {"ctrl": 0x1D, "alt": 0x38, "shift": 0x2A}


def gui_key_list(specs):
    """glos key's list for the scripts' key specs (ctrl+s, alt+f, shift+tab, down...), as the host's shim types them."""
    items, t = [], 0.0
    for spec in specs:
        parts = spec.split("+")
        name, mods = parts[-1], parts[:-1]
        code = GUI_KEYS.get(name) or (GUI_ROW.index(name) if len(name) == 1 and name in GUI_ROW else 0)
        if not code or any(m not in GUI_MODS for m in mods):
            raise ValueError("key " + spec)
        for m in mods:
            items.append("%.2f:0x%x:down" % (t, GUI_MODS[m]))
            t += 0.03
        items.append("%.2f:0x%x" % (t, code))
        t += 0.09
        for m in reversed(mods):
            items.append("%.2f:0x%x:up" % (t, GUI_MODS[m]))
            t += 0.03
        t += 0.03
    return ",".join(items)


def gui_words(line):
    """A script line's words, a title in quotes one word."""
    out, i = [], 0
    while i < len(line):
        if line[i] in " \t":
            i += 1
        elif line[i] == '"':
            j = line.index('"', i + 1)
            out.append(line[i + 1:j])
            i = j + 1
        else:
            j = i
            while j < len(line) and line[j] not in " \t":
                j += 1
            out.append(line[i:j])
            i = j
    return out


class GuiDriver:
    """A test script (tests/gui/NAME.txt; tests/host/glosshim.c has the commands) played on a GLOS desktop over
    ssh: the app started, keys and the mouse put in, each shot's window cut from glos shot and compared with the
    host's golden (build/host/gui/NAME-BITS.png). Failures as words in .bad, what went wrong in .notes."""

    def __init__(self, agent, out, bits, dac6, appdir="C:\\TEST\\"):
        self.agent, self.out, self.bits, self.dac6, self.appdir = agent, out, bits, dac6, appdir
        self.bad, self.notes, self.procs, self.expects, self.unset = [], [], [], [], []

    def desk(self):
        text = self.agent.ssh("glos desk")[1].replace("\r", "")
        m = re.search(r"focus=(\d+) pointer=(-?\d+),(-?\d+)", text)
        d = {"focus": int(m.group(1)) if m else -1, "ptr": (int(m.group(2)), int(m.group(3))) if m else (0, 0),
             "win": []}
        for w in re.finditer(r'^window (\d+) (\w+)(?: (\w+))? at (-?\d+),(-?\d+) (\d+)x(\d+) "(.*)"$', text, re.M):
            kind = w.group(3) or ""
            fx, fy = int(w.group(4)), int(w.group(5))
            framed = kind not in ("popup", "bare", "bar", "desktop")
            d["win"].append({"id": int(w.group(1)), "shown": w.group(2) == "shown", "kind": kind, "title": w.group(8),
                             "x": fx + (1 if framed else 0), "y": fy + (20 if framed else 0),
                             "w": int(w.group(6)), "h": int(w.group(7))})
        return d

    def window(self, title, d=None):            # the topmost shown window by that title (glos desk lists the top first)
        for w in (d or self.desk())["win"]:
            if w["shown"] and w["title"] == title and w["kind"] != "bar":
                return w
        return None

    def until(self, test, limit=30):
        t0 = time.time()
        while time.time() - t0 < limit:
            d = self.desk()
            if test(d):
                return d
            time.sleep(0.3)
        return None

    def golden(self, name):
        path = os.path.join(ROOT, "build", "host", "gui", self.script, "%s-%d.png" % (name, self.bits))
        if not os.path.exists(path):
            return None
        w, h, rgb = png_rgb(open(path, "rb").read())
        if self.bits == 8 and self.dac6:
            rgb = bytes((((v >> 2) << 2) | (v >> 6)) for v in rgb)
        return w, h, rgb

    def shot(self, name, title, mask=None, tries=10):
        want = self.golden(name)
        if not want:
            self.bad.append("golden-" + name)
            return
        got = None
        self.park()
        for i in range(tries):
            w = self.window(title)
            p = subprocess.run(self.agent.opts + ["glos@127.0.0.1", "glos shot"], capture_output=True, timeout=300)
            pic = png_rgb(p.stdout) if p.returncode == 0 else None
            if w and pic:
                sw = pic[0]
                rows = [bytearray(pic[2][((w["y"] + y) * sw + w["x"]) * 3:((w["y"] + y) * sw + w["x"] + w["w"]) * 3])
                        for y in range(w["h"])]
                for mx, my, mw, mh in mask or []:  # (what differs from run to run: black, as the host's golden has it)
                    for y in range(max(0, my), min(w["h"], my + mh)):
                        x0, x1 = max(0, mx), min(w["w"], mx + mw)
                        if x1 > x0:
                            rows[y][x0 * 3:x1 * 3] = bytes(3 * (x1 - x0))
                got = (w["w"], w["h"], b"".join(bytes(r) for r in rows))
                if got == want:
                    return
                if i == tries - 1:
                    open(os.path.join(self.out, "%s-%d.png" % (name, self.bits)), "wb").write(p.stdout)
            time.sleep(0.6)
        if not got:
            self.notes.append("%s: no window \"%s\"" % (name, title))
        elif got[:2] != want[:2]:
            self.notes.append("%s: %dx%d, the golden %dx%d" % (name, got[0], got[1], want[0], want[1]))
        else:
            bad = [i // 3 for i in range(0, len(want[2]), 3) if got[2][i:i + 3] != want[2][i:i + 3]]
            self.notes.append("%s: %d pixels differ, the first at %d,%d: %s for %s" % (
                name, len(bad), bad[0] % want[0], bad[0] // want[0], got[2][bad[0] * 3:bad[0] * 3 + 3].hex(),
                want[2][bad[0] * 3:bad[0] * 3 + 3].hex()))
        self.bad.append("shot-" + name)

    def park(self):                             # the pointer in the screen's corner, on the task bar: out of the shots
        text = self.agent.ssh("glos desk")[1]
        m = re.search(r"^desk (\d+)x(\d+)x\d+ .*pointer=(-?\d+),(-?\d+)", text, re.M)
        if m and (int(m.group(3)), int(m.group(4))) != (int(m.group(1)) - 1, int(m.group(2)) - 1):
            self.agent.ssh("glos mouse %d %d" % (int(m.group(1)) - 1 - int(m.group(3)), int(m.group(2)) - 1 - int(m.group(4))))

    def point(self, x, y):
        px, py = self.desk()["ptr"]
        if (x, y) != (px, py):
            self.agent.ssh("glos mouse %d %d" % (x - px, y - py))

    def play(self, script):
        self.script = os.path.splitext(os.path.basename(script))[0]     # (its goldens: build/host/gui/SCRIPT/)
        for n, line in enumerate(open(os.path.join(ROOT, script)), 1):
            words = gui_words(line.rstrip("\n"))
            if not words or words[0].startswith("#"):
                continue
            cmd, args = words[0], words[1:]
            if cmd == "config":                 # (before run) a setting for this script, unset when it is done
                self.agent.ssh("glos config set %s %s %s" % (args[0], args[1], " ".join(args[2:])))
                self.unset.append((args[0], args[1]))
            elif cmd == "run":
                prog = self.appdir + args[0] + "".join(" " + a for a in args[1:])
                self.procs.append(subprocess.Popen(self.agent.opts + ["glos@127.0.0.1", prog],
                                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE))
            elif cmd == "wait":
                if not self.until(lambda d: self.window(args[0], d) is not None, 120):
                    self.bad.append("wait-%d" % n)
            elif cmd == "gone":
                if not self.until(lambda d: self.window(args[0], d) is None, 60):
                    self.bad.append("gone-%d" % n)
            elif cmd == "quick":                # keys that open no menu, in one go
                self.agent.ssh("glos key " + gui_key_list(args))
            elif cmd == "key":                  # a spec at a time; after Alt and a letter, the menu it opens
                for spec in args:
                    self.agent.ssh("glos key " + gui_key_list([spec]))
                    if re.match(r"^alt\+[a-z0-9]$", spec):
                        self.until(lambda d: any(w["shown"] and w["kind"] == "popup" for w in d["win"]), 5)
            elif cmd == "type":
                text = line.split(None, 1)[1].rstrip("\n") if len(line.split(None, 1)) > 1 else ""
                self.agent.ssh("glos type " + text)
            elif cmd in ("click", "drag"):
                w = self.until(lambda d: self.window(args[0], d) is not None, 30)
                w = w and self.window(args[0], w)
                if not w:
                    self.bad.append("%s-%d" % (cmd, n))
                    continue
                button = 2 if "right" in args else 1
                self.point(w["x"] + int(args[1]), w["y"] + int(args[2]))
                if cmd == "drag":
                    dx, dy = int(args[3]) - int(args[1]), int(args[4]) - int(args[2])
                    self.agent.ssh("glos key 0:mouse:0:0:%d,0.1:mouse:%d:%d:%d,0.2:mouse:0:0:0" % (button, dx, dy, button))
                elif "double" in args:
                    self.agent.ssh("glos key 0:mouse:0:0:%d,0.05:mouse:0:0:0,0.15:mouse:0:0:%d,0.2:mouse:0:0:0"
                                   % (button, button))
                else:
                    self.agent.ssh("glos key 0:mouse:0:0:%d,0.05:mouse:0:0:0" % button)
            elif cmd == "pause":
                time.sleep(int(args[0]) / 1000.0)
            elif cmd == "launch":               # Ctrl+Esc: the launcher, the shell's; its menu then
                self.agent.ssh("glos key 0x1D:down,0.05:0x01,0.2:0x1D:up")
                self.until(lambda d: any(w["shown"] and w["kind"] == "popup" for w in d["win"]), 5)
            elif cmd == "syskey":
                self.agent.ssh("glos key 0x1D:down,0.05:0x38:down,0.1:0x153,0.25:0x38:up,0.3:0x1D:up")
            elif cmd == "clip":
                self.agent.ssh("glos clip " + line.split(None, 1)[1].rstrip("\n"))
            elif cmd == "shot":
                v = [int(x) for x in args[2:]]
                self.shot(args[0], args[1], [tuple(v[i:i + 4]) for i in range(0, len(v) - 3, 4)])
            elif cmd == "expect":
                self.expects.append(line.split(None, 1)[1].rstrip("\n"))
            elif cmd == "file":                 # a file's bytes, fetched over SFTP once the app has written it
                rest = line.split(None, 2)[2].rstrip("\n") if len(line.split(None, 2)) > 2 else ""
                want = re.sub(r"\\(x[0-9a-fA-F]{2}|.)", lambda m: chr(int(m.group(1)[1:], 16)) if m.group(1)[0] == "x"
                              else {"r": "\r", "n": "\n", "t": "\t"}.get(m.group(1), m.group(1)), rest).encode("latin-1")
                path = args[0]
                dst = os.path.join(self.out, "file-%d" % n)
                got = None
                for _ in range(10):             # (the app writes it when it gets to it)
                    f = subprocess.run(["scp", "-P", str(self.agent.port), "-i", self.agent.key, "-o", "IdentitiesOnly=yes",
                                        "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=no", "-o",
                                        "UserKnownHostsFile=/dev/null", "-o", "LogLevel=ERROR",
                                        "glos@127.0.0.1:/" + path[0] + path[2:].replace("\\", "/"), dst],
                                       capture_output=True, timeout=120)
                    got = open(dst, "rb").read() if f.returncode == 0 else None
                    if got == want:
                        break
                    time.sleep(1)
                if got != want:
                    self.bad.append("file-%d" % n)
                    self.notes.append("%s: %r, not %r" % (path, got[:80] if got else got, want[:80]))
            else:
                self.bad.append("script-%d" % n)

    def finish(self, limit=120):
        """The apps' ends and their output: each expected line there; the script's settings unset."""
        text = ""
        for p in self.procs:
            try:
                o, _ = p.communicate(timeout=limit)
            except subprocess.TimeoutExpired:
                p.kill()
                o, _ = p.communicate()
                self.bad.append("app-end")
            text += o.decode("latin-1").replace("\r", "")
        for sec, key in self.unset:
            self.agent.ssh("glos config unset %s %s" % (sec, key))
        self.unset = []
        lines = text.split("\n")
        for e in self.expects:
            if e not in lines:
                self.bad.append("expect")
                self.notes.append("no line %r" % e)
        return text


# ---- the settings (M9 step B): GLOS.CFG kept and written by the kernel, glos config

CONFIG_FILE = """; GLOS.CFG for the config suite: comments, blank lines and an unknown section must all stay.
[display]
; no desktop at first
accel = on

[unknown]
colour = blue

[program COMMAND.COM]
env = GLOSCFG=one
"""

# (section, key, a value other than the default, how to see it in effect now: None, or a test of (agent, log))
CONFIG_SETS = [
    ("display", "mode", "640x480x16", "display"), ("display", "accel", "off", None),
    ("display", "gpu_watchdog", "750", "GLOS-GPU watchdog-limit ms=750"), ("display", "font_smoothing", "on", None),
    ("input", "repeat_delay", "750", "GLOS-KBC typematic=4"), ("input", "repeat_rate", "20", "GLOS-KBC typematic=44"),
    ("input", "pointer_speed", "2", "pointer"), ("input", "double_click", "400", None),
    ("sound", "rate", "44100", "GLOS-AUDIO rate=44100"), ("sound", "mixer", "off", "GLOS-AUDIO mixer off"),
    ("sound", "blaster", "A220 I5 D1 H5 T6", None), ("net", "hostname", "cfgtest", None),
    ("time", "sntp", "off", None), ("time", "server", "10.0.2.2:1123", None),
    ("time", "zone", "GMT0BST,M3.5.0/1,M10.5.0", None), ("time", "interval", "12", None), ("vnc", "enable", "on", None),
    ("desktop", "start", "NOTEPAD.EXE", None), ("shell", "comspec", "C:\\COMMAND.COM", None),
    ("shell", "autoexec", "C:\\AUTOEXEC.BAT", None), ("shell", "console", "C:\\KIOSK.BAT", None),
    ("shell", "envsize", "2048", None), ("shell", "options", "/NOVME", None),
    ("open", ".TXT", "NOTEPAD.EXE", None),
    ("program:FWHEEL.EXE", "title", "Fifth Wheel", None), ("program:FWHEEL.EXE", "args", "-fixed", None),
    ("program:FWHEEL.EXE", "dir", "C:\\FW", None), ("program:FWHEEL.EXE", "icon", "game", None),
    ("program:FWHEEL.EXE", "menu", "1", None), ("program:FWHEEL.EXE", "desktop", "1", None),
    ("program:FWHEEL.EXE", "screen", "full", None), ("program:FWHEEL.EXE", "direct", "1", None),
    ("program:FWHEEL.EXE", "memory", "16384", None), ("program:FWHEEL.EXE", "env", "A=1;B=2", None),
]
CONFIG_CASES = [("bf6", "g200", "ne2kpci")]


# ---- the clock (M10 step A): SNTP, the zone, the RTC and DOS

class NtpServer:
    """An SNTP server on the host, for the time suite: UDP on 127.0.0.1, which the guest reaches as 10.0.2.2
    through SLiRP, answering with the host's time plus `offset` seconds, the request's transmit time as the
    originate time (lwIP checks it)."""

    def __init__(self):
        import socket
        import threading
        self.offset, self.asked, self.stop = 0.0, 0, False
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.bind(("127.0.0.1", 0))
        self.s.settimeout(0.3)
        self.port = self.s.getsockname()[1]
        self.t = threading.Thread(target=self.serve, daemon=True)
        self.t.start()

    def now(self):
        return time.time() + self.offset

    def serve(self):
        import struct
        while not self.stop:
            try:
                data, addr = self.s.recvfrom(512)
            except OSError:
                continue
            if len(data) < 48:
                continue
            self.asked += 1
            t = self.now() + 2208988800             # (NTP counts from 1900)
            ts = struct.pack("!II", int(t), int((t - int(t)) * 2 ** 32))
            pkt = bytes([0x24, 1, 6, 0xEC]) + bytes(8) + b"GPS\0" + ts + data[40:48] + ts + ts
            self.s.sendto(pkt, addr)

    def close(self):
        self.stop = True
        self.t.join(1)
        self.s.close()


TIME_CASES = [("bf6", "ne2kpci"), ("486dx2", "ne2k")]


def time_case(profile, card):
    """The clock (M10 step A; kernel/core/clock.c, PRD §11.3): SNTP sets it from an NTP server on the host, in
    GLOS.CFG's zone (the United Kingdom's): glos time, the RTC and DOS (CLOCKCHK) agree, in summer time; a step
    of ten days followed; the end of summer time seen by itself, the RTC and DOS set back an hour; another zone;
    the time set by hand; a zone refused; TZ in a program's environment; names looked up; and, when the host has
    the internet, pool.ntp.org through SLiRP agreeing with the host's clock."""
    import calendar
    tag = "%s-%s" % (profile, card)
    ntp = NtpServer()
    ntp.offset = calendar.timegm((2026, 7, 1, 11, 59, 0, 0, 0, 0)) - time.time()   # (summer: local 12:59 BST)
    cfg = os.path.join(ROOT, "out", "time-%s.cfg" % tag)
    open(cfg, "w").write("; GLOS.CFG for the time suite\r\n[time]\r\nserver = 10.0.2.2:%d\r\n"
                         "zone = GMT0BST,M3.5.0/1,M10.5.0\r\ninterval = 1\r\n" % ntp.port)
    agent = Agent("time-" + tag, profile, card, ["--file", "%s=/TEST/GLOS.CFG" % os.path.relpath(cfg, ROOT),
        "--file", "build/ow/dos/CLOCKCHK.EXE=/TEST/CLOCKCHK.EXE",
        "--cmd", "SERSAY HX-START time", "--cmd", "C:\\TEST\\GLOS.EXE", "--cmd", "SERSAY HX-DONE 0"], timeout=2400)
    checks, notes = {"listen": agent.listening()}, []

    def at(s):
        return calendar.timegm(time.strptime(s, "%Y-%m-%dT%H:%M:%S"))

    def clock():
        o = agent.ssh("glos time")[1]
        m = re.search(r"utc=(\S+)Z local=(\S+) (\S+) zone=(\S+)", o)
        r = re.search(r"rtc=(\S+)", o)
        c = re.search(r"CLOCK dos=(\S+) rtc=(\S+)", agent.ssh("C:\\TEST\\CLOCKCHK.EXE")[1])
        if not (m and r and c):
            notes.append("glos time: %r" % o)
            return None
        return {"utc": at(m.group(1)), "local": at(m.group(2)), "name": m.group(3), "rtc": at(r.group(1)),
                "dos": at(c.group(1)), "bios": at(c.group(2)), "text": o}

    def agree(name, want_utc, off, zone_name, slack=4):
        c = clock()
        ok = bool(c) and abs(c["utc"] - want_utc) <= slack and c["local"] - c["utc"] == off and c["name"] == zone_name \
            and abs(c["rtc"] - c["local"]) <= slack and abs(c["dos"] - c["local"]) <= slack \
            and abs(c["bios"] - c["local"]) <= slack
        checks[name] = ok
        if not ok:
            notes.append("%s: want utc %d offset %d %s; %r" % (name, want_utc, off, zone_name, c))
        return ok

    def until_log(text, limit, mark=0):
        t0 = time.time()
        while time.time() - t0 < limit:
            if text in agent.log()[mark:]:
                return True
            time.sleep(1)
        return False

    try:
        checks["synced"] = until_log("GLOS-TIME set by=sntp", 120)
        agree("summer", ntp.now(), 3600, "BST")
        checks["tz"] = "GMT0BST,M3.5.0/1,M10.5.0" in agent.ssh("ECHO %TZ%")[1]
        ntp.offset += 10 * 86400                    # (ten days on)
        rc, o, _ = agent.ssh("glos time sync", timeout=90)
        checks["step"] = rc == 0 and re.search(r"step=8640\d{5}ms", o) is not None
        agree("stepped", ntp.now(), 3600, "BST")
        ntp.offset = calendar.timegm((2026, 10, 25, 0, 59, 30, 0, 0, 0)) - time.time()   # (BST ends at 01:00 UTC)
        mark = len(agent.log())
        agent.ssh("glos time sync", timeout=90)
        checks["autumn"] = until_log("GLOS-TIME offset=0", 150, mark)
        agree("winter", ntp.now(), 0, "GMT")
        mark = len(agent.log())
        rc, o, _ = agent.ssh("glos config set time zone CET-1CEST,M3.5.0,M10.5.0/3")
        checks["zone-set"] = rc == 0 and until_log("GLOS-TIME zone=CET-1CEST", 30, mark)
        agree("paris", ntp.now(), 3600, "CET")
        rc, o, e = agent.ssh("glos config set time zone NOPE")
        checks["zone-refused"] = rc == 1 and "POSIX" in e
        agent.ssh("glos config set time sntp off")
        rc, o, _ = agent.ssh("glos time set 2030-01-02T03:04:05Z")
        checks["by-hand"] = rc == 0 and "by=hand" in o
        agree("hand", at("2030-01-02T03:04:05"), 3600, "CET", slack=8)
        rc, o, _ = agent.ssh("glos net resolve 10.0.2.2")
        checks["resolve-dotted"] = rc == 0 and o.strip() == "10.0.2.2"
        rc, o, _ = agent.ssh("glos net resolve pool.ntp.org", timeout=30)   # (the internet: the name used below)
        internet = rc == 0 and re.match(r"\d+\.\d+\.\d+\.\d+$", o.strip()) is not None
        checks["resolve-unknown"] = agent.ssh("glos net resolve no-such-name.invalid", timeout=30)[0] == 1
        if internet:                                 # (pool.ntp.org through SLiRP: the host's own time)
            agent.ssh("glos config set time server pool.ntp.org")
            agent.ssh("glos config set time sntp on")
            rc, o, _ = agent.ssh("glos time sync", timeout=90)
            m = re.search(r"utc=(\S+)Z", o)
            checks["pool"] = rc == 0 and bool(m) and abs(at(m.group(1)) - time.time()) <= 5
            if not checks["pool"]:
                notes.append("pool.ntp.org: %r" % o)
        else:
            notes.append("no internet from the guest: pool.ntp.org not asked")
        checks["exit"] = agent.ssh("glos exit")[0] == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    text = agent.wait()
    ntp.close()
    checks["no-panic"] = "GLOS-PANIC" not in text and "GLOS-WARN" not in text
    checks["done"] = "GLOS-EXIT code=0" in text and "HX-DONE 0" in text
    for n in notes:
        print("    time-%s %s" % (tag, n))
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s" % ("time-" + tag, "PASS" if not bad else "FAIL",
                             "" if not bad else " failed: " + " ".join(bad)), not bad


class EchoServer:
    """TCP echo on 127.0.0.1, which the guest reaches as 10.0.2.2 through SLiRP: a thread a connection, each
    giving back what it is sent; one whose first bytes are "bye\n" is closed after they are echoed. `open` the
    connections open now, `total` all there have been."""

    def __init__(self):
        import socket
        import threading
        self.open = self.total = 0
        self.lock = threading.Lock()
        self.s = socket.socket()
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.s.bind(("127.0.0.1", 0))
        self.s.listen(32)
        self.port = self.s.getsockname()[1]
        threading.Thread(target=self.accept, daemon=True).start()

    def accept(self):
        import threading
        while True:
            try:
                c, _ = self.s.accept()
            except OSError:
                return
            threading.Thread(target=self.serve, args=(c,), daemon=True).start()

    def serve(self, c):
        with self.lock:
            self.open += 1
            self.total += 1
        first = b""
        try:
            while True:
                d = c.recv(65536)
                if not d:
                    break
                c.sendall(d)
                if len(first) < 4:
                    first += d[:4 - len(first)]
                    if first == b"bye\n":
                        break
        except OSError:
            pass
        c.close()
        with self.lock:
            self.open -= 1

    def close(self):
        self.s.close()


def free_port():
    """A port on 127.0.0.1 with nothing listening (for a refusal)."""
    import socket
    s = socket.socket()
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


NETAPP_CASES = [("bf6", "ne2kpci"), ("486dx2", "ne2k")]


def netapp_case(profile, card):
    """An app's TCP connections (M10 step B; GLOS_FN_NET, glos/net.h): NETTEST against an echo server on the
    host: names looked up and refused, a connection refused and one timed out, 1 MB echoed byte for byte, eight at
    once and a ninth refused, the far end's close, events, random bytes, and the BSD sockets on it; then an app
    holding four connections killed: the host sees them close and the kernel's table is empty."""
    tag = "%s-%s" % (profile, card)
    echo = EchoServer()
    closed = free_port()
    agent = Agent("netapp-" + tag, profile, card, ["--file", "build/dj/NETTEST.EXE=/TEST/NETTEST.EXE",
        "--cmd", "SERSAY HX-START netapp", "--cmd", "C:\\TEST\\GLOS.EXE", "--cmd", "SERSAY HX-DONE 0"], timeout=2400)
    checks, notes = {"listen": agent.listening()}, []
    try:
        internet = agent.ssh("glos net resolve pool.ntp.org", timeout=30)[0] == 0
        t0 = time.time()
        while internet and time.time() - t0 < 60 and "GLOS-TIME set by=sntp" not in agent.log():
            time.sleep(1)                       # (the clock set before GLOS_FN_TIME is asked)
        rc, o, e = agent.ssh("C:\\TEST\\NETTEST.EXE all 10.0.2.2 %d %d%s" % (echo.port, closed,
                             " pool.ntp.org" if internet else ""), timeout=900)
        open(os.path.join(ROOT, "out", "netapp-%s.out" % tag), "w").write(o + e)
        for line in o.replace("\r", "").split("\n"):
            m = re.match(r"NETTEST (\S+) (ok|FAIL) ?(.*)", line)
            if m:
                checks["nt-" + m.group(1)] = m.group(2) == "ok"
                if m.group(2) != "ok" or m.group(1) == "echo-1m":
                    notes.append("%s: %s" % (m.group(1), m.group(3)))
        checks["nt-done"] = rc == 0 and "NETTEST-DONE failures=0" in o
        m = re.search(r"NETTEST-TIME how=(\d+) utc=(\d+) offset=(-?\d+)", o)
        # (GLOS_FN_TIME: with the internet, SNTP from pool.ntp.org has set it to the host's time; GLOS.CFG is the
        # default one: UTC0)
        checks["fn-time"] = bool(m) and m.group(3) == "0" and (not internet or (m.group(1) == "1"
                                                                                 and abs(int(m.group(2)) - time.time()) < 60))
        if not internet:
            notes.append("no internet from the guest: no name looked up there")
        checks["table-empty"] = "sockets=0" in agent.ssh("glos net")[1]
        # an app killed with connections open
        before = echo.open
        hold = subprocess.Popen(agent.opts + ["glos@127.0.0.1", "C:\\TEST\\NETTEST.EXE hold 10.0.2.2 %d 4"
                                              % echo.port], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        t0 = time.time()
        while time.time() - t0 < 60 and echo.open < before + 4:
            time.sleep(0.5)
        checks["held"] = echo.open == before + 4 and "sockets=4" in agent.ssh("glos net")[1]
        seq = re.findall(r'GLOS-AGENT run seq=(\d+) cmd="C:\\TEST\\NETTEST.EXE hold', agent.log())
        rc, o, e = agent.ssh("glos kill %s" % (seq[-1] if seq else "?"))   # (an app: by its job's number)
        checks["kill"] = rc == 0
        try:
            hold.wait(timeout=60)
        except subprocess.TimeoutExpired:
            hold.kill()
            checks["kill"] = False
        if not checks["kill"]:
            notes.append("glos kill: rc=%d %r %r; the held job's ssh: rc=%r %r" % (rc, o, e, hold.returncode,
                                                                                hold.stdout.read()[-200:]))
        t0 = time.time()
        while time.time() - t0 < 30 and echo.open > before:
            time.sleep(0.5)
        checks["closed-on-kill"] = echo.open == before
        t0 = time.time()
        while time.time() - t0 < 30 and "sockets=0" not in agent.ssh("glos net")[1]:
            time.sleep(0.5)
        checks["table-clean"] = "sockets=0" in agent.ssh("glos net")[1]
        checks["exit"] = agent.ssh("glos exit")[0] == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    text = agent.wait()
    echo.close()
    checks["no-panic"] = "GLOS-PANIC" not in text and "GLOS-WARN" not in text
    checks["done"] = "GLOS-EXIT code=0" in text and "HX-DONE 0" in text
    for n in notes:
        print("    netapp-%s %s" % (tag, n))
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s" % ("netapp-" + tag, "PASS" if not bad else "FAIL",
                             "" if not bad else " failed: " + " ".join(bad)), not bad


def test_pki(out):
    """A test CA (RSA 2048) and another nobody trusts, and EC P-256 certificates for HTTPS servers on the host:
    test.glos (good), other.glos (the wrong name), test.glos expired, test.glos from the untrusted CA. Files in
    out: ca.pem, and NAME.pem with NAME.key for each server."""
    import datetime
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec, rsa
    from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID
    os.makedirs(out, exist_ok=True)
    now = datetime.datetime.now(datetime.timezone.utc)
    day = datetime.timedelta(days=1)

    def name(cn):
        return x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, cn)])

    def ca(cn):
        k = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        c = (x509.CertificateBuilder().subject_name(name(cn)).issuer_name(name(cn)).public_key(k.public_key())
             .serial_number(x509.random_serial_number()).not_valid_before(now - day).not_valid_after(now + 3650 * day)
             .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
             .add_extension(x509.KeyUsage(digital_signature=True, content_commitment=False, key_encipherment=False,
                                          data_encipherment=False, key_agreement=False, key_cert_sign=True,
                                          crl_sign=True, encipher_only=False, decipher_only=False), critical=True)
             .sign(k, hashes.SHA256()))
        return k, c

    def leaf(file, cak, cac, dns, before, after):
        k = ec.generate_private_key(ec.SECP256R1())
        c = (x509.CertificateBuilder().subject_name(name(dns)).issuer_name(cac.subject).public_key(k.public_key())
             .serial_number(x509.random_serial_number()).not_valid_before(before).not_valid_after(after)
             .add_extension(x509.SubjectAlternativeName([x509.DNSName(dns)]), critical=False)
             .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
             .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False)
             .sign(cak, hashes.SHA256()))
        open(os.path.join(out, file + ".pem"), "wb").write(c.public_bytes(serialization.Encoding.PEM)
                                                          + cac.public_bytes(serialization.Encoding.PEM))
        open(os.path.join(out, file + ".key"), "wb").write(k.private_bytes(
            serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))

    cak, cac = ca("GLOS Loop A test CA")
    otherk, otherc = ca("Nobody's CA")
    open(os.path.join(out, "ca.pem"), "wb").write(cac.public_bytes(serialization.Encoding.PEM))
    leaf("good", cak, cac, "test.glos", now - day, now + 30 * day)
    leaf("name", cak, cac, "other.glos", now - day, now + 30 * day)
    leaf("expired", cak, cac, "test.glos", now - 20 * day, now - 10 * day)
    leaf("untrusted", otherk, otherc, "test.glos", now - day, now + 30 * day)


HTTPS_BODY = bytes((i * 7919 + (i >> 8) * 31) & 255 for i in range(262144))     # (/file's 256 KB)


class HttpsServer:
    """An HTTPS server on 127.0.0.1 (the guest's 10.0.2.2) with one certificate: /file (HTTPS_BODY, with its
    length), /redirect (302 to /file), /relative (302 to "file"), /chunked (the same bytes in chunks), anything
    else 404. TLS 1.2 (BearSSL's only version)."""

    def __init__(self, pki, cert):
        import http.server
        import ssl
        import threading

        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *a):
                pass

            def do_GET(self):
                if self.path == "/file":
                    self.send_response(200)
                    self.send_header("Content-Length", str(len(HTTPS_BODY)))
                    self.send_header("Connection", "close")
                    self.end_headers()
                    self.wfile.write(HTTPS_BODY)
                elif self.path in ("/redirect", "/relative"):
                    self.send_response(302)
                    self.send_header("Location", "/file" if self.path == "/redirect" else "file")
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                elif self.path == "/chunked":
                    self.send_response(200)
                    self.send_header("Transfer-Encoding", "chunked")
                    self.send_header("Connection", "close")
                    self.end_headers()
                    at, n = 0, 1
                    while at < len(HTTPS_BODY):
                        part = HTTPS_BODY[at:at + n]
                        self.wfile.write(b"%x\r\n" % len(part) + part + b"\r\n")
                        at, n = at + len(part), n * 3 + 1
                    self.wfile.write(b"0\r\n\r\n")
                else:
                    self.send_response(404)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                self.close_connection = True

        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.minimum_version = ssl.TLSVersion.TLSv1_2
        ctx.load_cert_chain(os.path.join(pki, cert + ".pem"), os.path.join(pki, cert + ".key"))
        self.httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.httpd.socket = ctx.wrap_socket(self.httpd.socket, server_side=True)
        self.port = self.httpd.server_address[1]
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def close(self):
        self.httpd.shutdown()


HTTPS_CASES = [("bf6", "ne2kpci"), ("486dx2", "ne2k")]
HTTPS_INTERNET = [("github", "https://raw.githubusercontent.com/curl/curl/curl-8_10_1/COPYING"),
                  ("freedos", "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/base/mem.zip")]


def https_case(profile, card):
    """HTTPS (M10 step C; sdk/tls, sdk/http, FETCH): with the clock not set, refused; set by hand, FETCH from HTTPS
    servers on the host with a test CA (added to Mozilla's roots in the guest's CERTS.DAT): a file with its length,
    redirects (absolute and relative), a chunked body, each the host's bytes (SHA-256); refusals for a name that
    isn't the certificate's, an expired certificate, one from a CA nobody trusts, a 404. With the internet: a file
    from GitHub and one from a FreeDOS mirror, Mozilla's roots alone, their SHA-256 the host's."""
    import hashlib
    import urllib.request
    tag = "%s-%s" % (profile, card)
    out = os.path.join(ROOT, "out", "https-%s-files" % tag)  # (beside the run's own out/https-TAG)
    pki = os.path.join(out, "pki")
    test_pki(pki)
    certs = os.path.join(out, "CERTS.DAT")
    subprocess.run([os.path.join(ROOT, "build", "host", "mkcerts"), certs,
                    os.path.join(os.environ.get("MGA_CACHE", os.path.expanduser("~/.cache/mga-glide")), "dl",
                                 "cacert-2026-09-25.pem"), os.path.join(pki, "ca.pem")], check=True,
                   stdout=subprocess.DEVNULL)
    servers = {n: HttpsServer(pki, n) for n in ("good", "name", "expired", "untrusted")}
    cfg = os.path.join(out, "GLOS.CFG")
    open(cfg, "w").write("; GLOS.CFG for the https suite: the clock set by hand\r\n[time]\r\nsntp = off\r\n")
    agent = Agent("https-" + tag, profile, card, ["--file", "%s=/TEST/GLOS.CFG" % os.path.relpath(cfg, ROOT),
        "--file", "build/apps/FETCH.EXE=/TEST/FETCH.EXE", "--file", "%s=/TEST/CERTS.DAT" % os.path.relpath(certs, ROOT),
        "--cmd", "SERSAY HX-START https", "--cmd", "C:\\TEST\\GLOS.EXE", "--cmd", "SERSAY HX-DONE 0"], timeout=3600)
    checks, notes = {"listen": agent.listening()}, []
    want = hashlib.sha256(HTTPS_BODY).hexdigest()

    def fetch(name, url, ok_sha=None, why=None, resolve=True, timeout=600):
        t0 = time.time()
        rc, o, e = agent.ssh("C:\\TEST\\FETCH.EXE --quiet --certs C:\\TEST\\CERTS.DAT %s%s C:\\TEST\\GOT.BIN"
                             % ("--resolve test.glos:10.0.2.2 " if resolve else "", url), timeout=timeout)
        o = (o + e).replace("\r", "").strip()
        if ok_sha:
            good = rc == 0 and ("sha256=%s " % ok_sha) in o + " "
        else:
            good = rc == 1 and ("FETCH failed: " in o) and why in o
        checks[name] = good
        if not good or name in ("file", "github"):
            notes.append("%s (%.1fs): %s" % (name, time.time() - t0, o[-200:]))

    try:
        # GLOS_FN_TIME says RTC: certificates can't be checked
        fetch("clock-not-set", "https://test.glos:%d/file" % servers["good"].port, why="clock not set")
        checks["set"] = agent.ssh("glos time set " + time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()))[0] == 0
        good = servers["good"].port
        fetch("file", "https://test.glos:%d/file" % good, ok_sha=want)
        fetch("redirect", "https://test.glos:%d/redirect" % good, ok_sha=want)
        fetch("relative", "https://test.glos:%d/relative" % good, ok_sha=want)
        fetch("chunked", "https://test.glos:%d/chunked" % good, ok_sha=want)
        fetch("not-found", "https://test.glos:%d/nothing" % good, why="HTTP 404")
        fetch("wrong-name", "https://test.glos:%d/file" % servers["name"].port, why="wrong name")
        fetch("expired", "https://test.glos:%d/file" % servers["expired"].port, why="expired")
        fetch("untrusted", "https://test.glos:%d/file" % servers["untrusted"].port, why="not trusted")
        fetch("no-such-name", "https://no-such-name.invalid/file", why="no such name", resolve=False)
        internet = agent.ssh("glos net resolve raw.githubusercontent.com", timeout=30)[0] == 0
        for name, url in HTTPS_INTERNET if internet else []:
            try:
                host_sha = hashlib.sha256(urllib.request.urlopen(url, timeout=60).read()).hexdigest()
            except OSError as ex:
                notes.append("%s: the host couldn't fetch it (%s)" % (name, ex))
                continue
            fetch(name, url, ok_sha=host_sha, resolve=False, timeout=900)
        if not internet:
            notes.append("no internet from the guest: GitHub and the FreeDOS mirror not asked")
        checks["sockets"] = "sockets=0" in agent.ssh("glos net")[1]
        checks["exit"] = agent.ssh("glos exit")[0] == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    text = agent.wait()
    for srv in servers.values():
        srv.close()
    checks["no-panic"] = "GLOS-PANIC" not in text and "GLOS-WARN" not in text
    checks["done"] = "GLOS-EXIT code=0" in text and "HX-DONE 0" in text
    for n in notes:
        print("    https-%s %s" % (tag, n))
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s" % ("https-" + tag, "PASS" if not bad else "FAIL",
                             "" if not bad else " failed: " + " ".join(bad)), not bad


class SshdContainer:
    """OpenSSH's sshd in a container of the suite's own (tests/sshd/Dockerfile: Debian's openssh-server, vi, top),
    published on two ports of 127.0.0.1 (the guest's 10.0.2.2) with a host key and a client key made for the run:
    logins by that key or by the user glos's password."""

    PASSWORD = "glos-test-pw"

    def __init__(self, name, out):
        import socket
        self.name = "glos-sshd-" + name
        self.keys = os.path.join(out, "sshd")
        os.makedirs(self.keys, exist_ok=True)
        for f in ("hostkey", "hostkey.pub", "client", "client.pub", "other", "other.pub"):
            if os.path.exists(os.path.join(self.keys, f)):
                os.remove(os.path.join(self.keys, f))
        for k, c in (("hostkey", "host"), ("client", "glos@loopa"), ("other", "other")):
            subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-C", c, "-f", os.path.join(self.keys, k)],
                           check=True)
        open(os.path.join(self.keys, "authorized_keys"), "w").write(open(os.path.join(self.keys, "client.pub")).read())
        open(os.path.join(self.keys, "sshd_config"), "w").write(
            "HostKey /keys/hostkey\nAuthorizedKeysFile /keys/authorized_keys\nStrictModes no\n"
            "PasswordAuthentication yes\nKbdInteractiveAuthentication no\nUsePAM yes\nPermitRootLogin no\n"
            "Subsystem sftp /usr/lib/openssh/sftp-server\nLogLevel INFO\n")
        self.ports = []
        for _ in range(2):
            t = socket.socket()
            t.bind(("127.0.0.1", 0))
            self.ports.append(t.getsockname()[1])
            t.close()
        subprocess.run(["docker", "build", "-q", "-t", "glos-sshd:m10", os.path.join(ROOT, "tests", "sshd")],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run(["docker", "rm", "-f", self.name], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run(["docker", "run", "-d", "--rm", "--name", self.name, "-p", "127.0.0.1:%d:22" % self.ports[0],
                        "-p", "127.0.0.1:%d:22" % self.ports[1], "-v", "%s:/keys:ro" % self.keys, "glos-sshd:m10"],
                       check=True, stdout=subprocess.DEVNULL)
        t0 = time.time()
        while time.time() - t0 < 30:
            try:
                with socket.create_connection(("127.0.0.1", self.ports[0]), timeout=2) as c:
                    if c.recv(64).startswith(b"SSH-"):
                        break
            except OSError:
                time.sleep(0.5)

    def pub(self, which):
        return open(os.path.join(self.keys, which + ".pub")).read().split()[1]

    def run(self, cmd):
        return subprocess.run(["docker", "exec", "-u", "glos", "-w", "/home/glos", self.name, "sh", "-c", cmd],
                              capture_output=True, text=True).stdout       # (in the user's home, as the session was)

    def close(self):
        subprocess.run(["docker", "rm", "-f", self.name], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


SSHC_CASES = [("bf6", "g200", "ne2kpci", 16), ("486dx2", "vbe", "ne2k", 8)]


def sshc_case(profile, vcard, card, bits):
    """The terminal's SSH and SFTP client (M10 step D, PRD D61; sdk/sshc, apps/term/ssh.c) against OpenSSH in a
    container: a new host key asked about and kept; a command's output and its exit status as %ERRORLEVEL%; a login
    by password after a wrong one; a session with a pty: vi editing a file (read back in the container) and top's
    screen; a changed host key refused; SFTP put and get of 1 MB (SHA-256 on both sides) and ls; ssh-keygen's key
    read by OpenSSH's ssh-keygen."""
    import hashlib
    import threading
    tag = "%s-%s" % (profile, vcard)
    out = os.path.join(ROOT, "out", "sshc-%s-files" % tag)
    os.makedirs(out, exist_ok=True)
    srv = SshdContainer(tag, out)
    p1, p2 = srv.ports
    open(os.path.join(out, "KNOWNHST.TXT"), "w").write("[10.0.2.2]:%d ssh-ed25519 %s\n" % (p2, srv.pub("other")))
    mb = bytes((i * 2654435761 >> 13) & 255 for i in range(1 << 20))
    open(os.path.join(out, "MB.BIN"), "wb").write(mb)
    rel = lambda f: os.path.relpath(os.path.join(out, f), ROOT)
    agent = Agent("sshc-" + tag, profile, card, ["--card", vcard, "--file", "build/apps/TERM.EXE=/TEST/TERM.EXE",
        "--file", "%s=/TEST/SSH/ID_ED25" % rel("sshd/client"), "--file", "%s=/TEST/SSH/KNOWNHST.TXT" % rel("KNOWNHST.TXT"),
        "--file", "%s=/TEST/MB.BIN" % rel("MB.BIN"),
        "--cmd", "SERSAY HX-START sshc", "--cmd", "C:\\TEST\\GLOS.EXE", "--cmd", "VMODE", "--cmd", "SERSAY HX-DONE 0"],
        timeout=3600)
    checks, notes, lines = {"listen": agent.listening()}, [], []

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

    def fetch(path, dst):
        f = subprocess.run(["scp", "-P", str(agent.port), "-i", agent.key, "-o", "IdentitiesOnly=yes", "-o",
                            "BatchMode=yes", "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
                            "-o", "LogLevel=ERROR", "glos@127.0.0.1:/" + path[0] + path[2:].replace("\\", "/"), dst],
                           capture_output=True, timeout=120)
        return open(dst, "rb").read() if f.returncode == 0 else None

    term = None
    try:
        checks["on"] = agent.ssh("glos display on 640x480x%d" % bits)[0] == 0
        t0 = time.time()
        while time.time() - t0 < 60 and (" 640x480x%d " % bits) not in agent.ssh("glos display")[1]:
            time.sleep(0.5)
        term = subprocess.Popen(agent.opts + ["glos@127.0.0.1", "C:\\TEST\\TERM.EXE -log"], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        threading.Thread(target=reader, args=(term,), daemon=True).start()
        time.sleep(8)
        # a new host key: asked about, kept; a command's output
        typed("ssh -p %d glos@10.0.2.2 echo hello-over-ssh\\n" % p1)
        k = until("SSH| ask-host")
        checks["ask-host"] = bool(k) and ("SHA256:" in lines[k - 1])
        typed("yes\\n")
        k = until("SSH| end", start=k)
        checks["command"] = bool(k) and lines[k - 1] == "SSH| end code=0"
        # its exit status
        typed("ssh -p %d glos@10.0.2.2 exit 7 || echo status %%errorlevel%%\\n" % p1)
        k = until("SSH| end", start=k)
        checks["status"] = bool(k) and lines[k - 1] == "SSH| end code=7"
        # by password: a wrong one, then the right one (no key: -i a file that isn't there)
        typed("ssh -p %d -i C:\\\\TEST\\\\NOKEY glos@10.0.2.2 echo by-password\\n" % p1)
        k = until("SSH| ask-password", start=k)
        typed("wrong-password\\n")
        k2 = until("SSH| ask-password", start=k)
        checks["wrong-password"] = bool(k2)
        typed(SshdContainer.PASSWORD + "\\n")
        k = until("SSH| end", start=k2 or k)
        checks["password"] = bool(k) and lines[k - 1] == "SSH| end code=0"
        # a session with a pty: vi, then top
        typed("ssh -p %d glos@10.0.2.2\\n" % p1)
        k = until("SSH| logged-in", start=k)
        time.sleep(4)
        typed("vi notes.txt\\n")
        time.sleep(4)
        typed("ihello from GLOS vi")
        agent.ssh("glos key " + gui_key_list(["esc"]))
        time.sleep(1)
        typed(":wq\\n")
        time.sleep(3)
        typed("top -d 3\\n")
        k3 = until("VT| top - ", 60, start=k)
        checks["top"] = bool(k3) and any(l.startswith("VT| Tasks:") for l in lines[k3:k3 + 6])
        typed("q")
        time.sleep(2)
        typed("exit\\n")
        k = until("SSH| end", start=k)
        checks["session"] = bool(k) and lines[k - 1] == "SSH| end code=0"
        got = srv.run("cat notes.txt")
        checks["vi"] = got == "hello from GLOS vi\n"
        if not checks["vi"]:
            notes.append("notes.txt: %r" % got)
        # a changed host key: refused
        typed("ssh -p %d glos@10.0.2.2 echo nope\\n" % p2)
        k = until("SSH| end", start=k)
        checks["changed-key"] = bool(k) and lines[k - 1] == "SSH| end code=255" and \
            any(l == "SSH| host-key-changed" for l in lines)
        # SFTP: 1 MB up and down, ls
        typed("sftp -P %d glos@10.0.2.2\\n" % p1)
        k = until("SSH| sftp-ready", start=k)
        t0 = time.time()
        typed("put C:\\\\TEST\\\\MB.BIN mb.bin\\n")
        k = until("SSH| sftp-ready", 300, start=k)
        up = time.time() - t0
        t0 = time.time()
        typed("get mb.bin C:\\\\TEST\\\\MB2.BIN\\n")
        k = until("SSH| sftp-ready", 300, start=k)
        down = time.time() - t0
        notes.append("sftp: 1 MB up %.1fs, down %.1fs" % (up, down))
        typed("ls -l\\n")
        k = until("SSH| sftp-ready", start=k)
        typed("exit\\n")
        k = until("SSH| end", start=k)
        checks["sftp-up"] = srv.run("sha256sum mb.bin").split()[:1] == [hashlib.sha256(mb).hexdigest()]
        got = fetch("C:\\TEST\\MB2.BIN", os.path.join(out, "MB2.BIN"))
        checks["sftp-down"] = got == mb
        # ssh-keygen: a key OpenSSH reads
        typed("ssh-keygen -f C:\\\\TEST\\\\NEWKEY -C made-in-glos\\n")
        time.sleep(15)
        sk = fetch("C:\\TEST\\NEWKEY", os.path.join(out, "NEWKEY"))
        pk = fetch("C:\\TEST\\NEWKEY.PUB", os.path.join(out, "NEWKEY.PUB"))
        if sk and pk:
            os.chmod(os.path.join(out, "NEWKEY"), 0o600)
            y = subprocess.run(["ssh-keygen", "-y", "-f", os.path.join(out, "NEWKEY")], capture_output=True,
                               text=True).stdout.split()[:2]
            checks["keygen"] = y == pk.decode("latin-1").split()[:2] and y[0] == "ssh-ed25519"
        else:
            checks["keygen"] = False
        typed("exit\\n")
        term.wait(timeout=120)
        time.sleep(1)
        text = "\n".join(lines)
        for want in ("TERM| hello-over-ssh", "TERM| status 7", "TERM| by-password", "TERM| sftp> ls -l"):
            if want not in lines:
                checks["log"] = False
                notes.append("no line %r" % want)
        checks.setdefault("log", True)
        checks["refused-said"] = "REMOTE HOST IDENTIFICATION HAS CHANGED" in text
        checks["sockets"] = "sockets=0" in agent.ssh("glos net")[1]
        checks["exit"] = agent.ssh("glos exit")[0] == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
        if term:
            term.kill()
    log = agent.wait()
    srv.close()
    open(os.path.join(out, "term.out"), "w").write("\n".join(lines) + "\n")
    checks["no-panic"] = "GLOS-PANIC" not in log and "GLOS-WARN" not in log
    for n in notes:
        print("    sshc-%s %s" % (tag, n))
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s" % ("sshc-" + tag, "PASS" if not bad else "FAIL",
                             "" if not bad else " failed: " + " ".join(bad)), not bad


class RepoServer:
    """HTTPS on 127.0.0.1 (the guest's 10.0.2.2) with one certificate, serving the files of `root` (which the
    suite changes between requests: one repository, then another)."""

    def __init__(self, pki, cert, root):
        import http.server
        import ssl
        import threading
        server = self
        self.root = root

        class Handler(http.server.BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.1"

            def log_message(self, *a):
                pass

            def do_GET(self):
                f = os.path.join(server.root, os.path.basename(self.path))
                if os.path.isfile(f):
                    data = open(f, "rb").read()
                    self.send_response(200)
                    self.send_header("Content-Length", str(len(data)))
                    self.end_headers()
                    self.wfile.write(data)
                else:
                    self.send_response(404)
                    self.send_header("Content-Length", "0")
                    self.end_headers()
                self.close_connection = True

        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.minimum_version = ssl.TLSVersion.TLSv1_2
        ctx.load_cert_chain(os.path.join(pki, cert + ".pem"), os.path.join(pki, cert + ".key"))
        self.httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.httpd.socket = ctx.wrap_socket(self.httpd.socket, server_side=True)
        self.port = self.httpd.server_address[1]
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def close(self):
        self.httpd.shutdown()


PKG_CASES = [("bf6", "g200", "ne2kpci"), ("486dx2", "vbe", "ne2k")]
PKG_CFG = ("; GLOS.CFG for the pkg suite: a package's profile goes in and comes out again\r\n[time]\r\nsntp = off\r\n\r\n"
           "[program PKG.EXE]\r\nenv = DOSDIR=C:\\FDTEST\r\n")


def pkg_repos(out):
    """Four repositories made with tools/pkg and the suite's test key: NOTES 1.0; NOTES 1.0 and 1.1; 1.0 with its
    zip changed after signing; 1.0 with its index changed after signing."""
    import shutil
    tool = [sys.executable, os.path.join(ROOT, "tools", "pkg", "pkg.py")]
    key = os.path.join(out, "TEST")
    if not os.path.exists(key):
        subprocess.run(tool + ["keygen", key, "glos-loopa-test"], check=True, stdout=subprocess.DEVNULL)
    def make(version, readme, extra=None, title="Notes"):
        src = os.path.join(out, "src-" + version)
        shutil.rmtree(src, ignore_errors=True)
        os.makedirs(os.path.join(src, "DOC"))
        open(os.path.join(src, "GLOSPKG.INI"), "w").write(
            "[package]\nname = NOTES\nversion = %s\nsummary = A test package\n\n[program NOTES.EXE]\ntitle = %s\n"
            "menu = 1\n" % (version, title))
        shutil.copy(os.path.join(ROOT, "build", "ow", "dos", "CLOCKCHK.EXE"), os.path.join(src, "NOTES.EXE"))
        open(os.path.join(src, "README.TXT"), "wb").write(readme)
        open(os.path.join(src, "DOC", "NOTES.TXT"), "wb").write(b"the notes' notes\r\n" * 50)
        if extra:
            open(os.path.join(src, extra), "wb").write(b"new in 1.1\r\n")
        zf = os.path.join(out, "NOTES-%s.ZIP" % version.replace(".", ""))
        subprocess.run(tool + ["make", src, zf], check=True, stdout=subprocess.DEVNULL)
        return zf
    z10 = make("1.0", b"version one\r\n")
    z11 = make("1.1", b"version one point one\r\n", "NEW.TXT", "Notes 1.1")
    repos = {}
    for name, zips in (("one", [z10]), ("two", [z10, z11]), ("tampered", [z10]), ("badsig", [z10])):
        d = os.path.join(out, "repo-" + name)
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d)
        for z in zips:
            shutil.copy(z, d)
        subprocess.run(tool + ["index", d, key], check=True, stdout=subprocess.DEVNULL)
        repos[name] = d
    z = os.path.join(repos["tampered"], os.path.basename(z10))
    data = bytearray(open(z, "rb").read())
    data[len(data) // 2] ^= 0x55
    open(z, "wb").write(bytes(data))
    t = os.path.join(repos["badsig"], "INDEX.TXT")
    open(t, "ab").write(b"EVIL\t9.9\t1\t00\tEVIL.ZIP\tnot signed\n")
    return key + ".PUB", repos


def pkg_case(profile, vcard, card):
    """Packages (M10 step E, PRD D58; apps/pkg, tools/pkg). The gate: a signed package installs and uninstalls
    cleanly. A source on the host over HTTPS (the test CA), signed with the suite's test key: NOTES listed and
    installed (its files byte for byte, its profile in GLOS.CFG with a launcher entry, its list); upgraded to 1.1
    (files changed, added; the profile's title); removed (the files and folders gone, GLOS.CFG byte for byte as it
    was, the list gone). Refused: a zip changed after signing, an index changed after signing. With the internet,
    a FreeDOS source listed and one of its packages installed and removed (DOSDIR from PKG's profile). Then PKG's
    window: NOTES chosen, installed with Alt+I, removed with Alt+R."""
    tag = "%s-%s" % (profile, card)
    out = os.path.join(ROOT, "out", "pkg-%s-files" % tag)
    os.makedirs(out, exist_ok=True)
    pki = os.path.join(out, "pki")
    test_pki(pki)
    certs = os.path.join(out, "CERTS.DAT")
    subprocess.run([os.path.join(ROOT, "build", "host", "mkcerts"), certs,
                    os.path.join(os.environ.get("MGA_CACHE", os.path.expanduser("~/.cache/mga-glide")), "dl",
                                 "cacert-2026-09-25.pem"), os.path.join(pki, "ca.pem")], check=True,
                   stdout=subprocess.DEVNULL)
    pub, repos = pkg_repos(out)
    srv = RepoServer(pki, "good", repos["one"])
    internet = subprocess.run(["getent", "hosts", "www.ibiblio.org"], capture_output=True).returncode == 0
    open(os.path.join(out, "SOURCES.TXT"), "w").write(
        "glos https://test.glos:%d/\r\n" % srv.port
        + ("freedos https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/base/\r\n"
           if internet else ""))
    open(os.path.join(out, "GLOS.CFG"), "w").write(PKG_CFG)
    rel = lambda f: os.path.relpath(f, ROOT)
    agent = Agent("pkg-" + tag, profile, card, ["--card", vcard,
        "--file", "%s=/TEST/GLOS.CFG" % rel(os.path.join(out, "GLOS.CFG")),
        "--file", "build/apps/PKG.EXE=/TEST/PKG.EXE", "--file", "%s=/TEST/CERTS.DAT" % rel(certs),
        "--file", "%s=/TEST/KEYS/PKG/TEST.PUB" % rel(pub),
        "--file", "%s=/TEST/PKG/SOURCES.TXT" % rel(os.path.join(out, "SOURCES.TXT")),
        "--cmd", "SERSAY HX-START pkg", "--cmd", "C:\\TEST\\GLOS.EXE", "--cmd", "SERSAY HX-DONE 0"], timeout=3600)
    checks, notes = {"listen": agent.listening()}, []

    def pkg(args, timeout=600):
        rc, o, e = agent.ssh("C:\\TEST\\PKG.EXE --resolve test.glos:10.0.2.2 " + args, timeout=timeout)
        o = (o + e).replace("\r", "")
        open(os.path.join(out, "pkg.log"), "a").write("> PKG %s (rc %d)\n%s\n" % (args, rc, o))
        return rc, o

    def fetch(path):
        dst = os.path.join(out, "got")
        if os.path.exists(dst):
            os.remove(dst)
        f = subprocess.run(["scp", "-P", str(agent.port), "-i", agent.key, "-o", "IdentitiesOnly=yes", "-o",
                            "BatchMode=yes", "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
                            "-o", "LogLevel=ERROR", "glos@127.0.0.1:/" + path[0] + path[2:].replace("\\", "/"), dst],
                           capture_output=True, timeout=120)
        return open(dst, "rb").read() if f.returncode == 0 else None

    def exists(path):
        return "THERE" in agent.ssh("IF EXIST %s ECHO THERE" % path)[1]

    def src(version, f):
        return open(os.path.join(out, "src-" + version, f), "rb").read()

    try:
        t0 = time.time()                        # (GLOS.CFG read: its first copy, to compare at the end)
        while time.time() - t0 < 60 and not agent.ssh("glos config")[1].startswith("config loaded"):
            time.sleep(0.5)
        agent.ssh("glos time set " + time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()))
        before = fetch("C:\\TEST\\GLOS.CFG")
        rc, o = pkg("LIST")
        checks["list"] = rc == 0 and re.search(r"^PKG NOTES +1\.0 ", o, re.M) is not None
        rc, o = pkg("INSTALL NOTES")
        checks["install"] = rc == 0 and "PKG installed NOTES 1.0 in C:\\APPS\\NOTES: 3 files (signed)" in o
        checks["files"] = fetch("C:\\APPS\\NOTES\\README.TXT") == src("1.0", "README.TXT") and \
            fetch("C:\\APPS\\NOTES\\NOTES.EXE") == src("1.0", "NOTES.EXE") and \
            fetch("C:\\APPS\\NOTES\\DOC\\NOTES.TXT") == src("1.0", os.path.join("DOC", "NOTES.TXT"))
        checks["profile"] = agent.ssh("glos config get program:NOTES.EXE title")[1].strip() == "Notes" and \
            agent.ssh("glos config get program:NOTES.EXE menu")[1].strip() == "1"
        checks["runs"] = "CLOCK dos=" in agent.ssh("C:\\APPS\\NOTES\\NOTES.EXE")[1]
        rc, o = pkg("LIST -I")
        checks["installed"] = "PKG installed NOTES    1.0" in o
        rc, o = pkg("INSTALL NOTES")
        checks["again-refused"] = rc == 1 and "installed already" in o
        srv.root = repos["two"]
        rc, o = pkg("UPGRADE")
        checks["upgrade"] = rc == 0 and "upgrading NOTES 1.0 to 1.1" in o and "PKG installed NOTES 1.1" in o
        checks["upgraded"] = fetch("C:\\APPS\\NOTES\\README.TXT") == src("1.1", "README.TXT") and \
            fetch("C:\\APPS\\NOTES\\NEW.TXT") == src("1.1", "NEW.TXT") and \
            agent.ssh("glos config get program:NOTES.EXE title")[1].strip() == "Notes 1.1"
        rc, o = pkg("REMOVE NOTES")
        checks["remove"] = rc == 0 and "PKG removed NOTES" in o
        checks["gone"] = not exists("C:\\APPS\\NOTES\\NUL") and not exists("C:\\APPS\\NUL") and \
            not exists("C:\\TEST\\PKG\\NOTES.LST")
        t0 = time.time()                        # (the kernel writes GLOS.CFG soon after a change)
        after = None
        while time.time() - t0 < 30 and after != before:
            time.sleep(1)
            after = fetch("C:\\TEST\\GLOS.CFG")
        checks["cfg-as-before"] = before is not None and after == before
        if not checks["cfg-as-before"]:
            notes.append("GLOS.CFG after: %r" % after)
        srv.root = repos["tampered"]
        rc, o = pkg("INSTALL NOTES")
        checks["tampered"] = rc == 1 and "isn't the signed index's: refused" in o and not exists("C:\\APPS\\NUL")
        srv.root = repos["badsig"]
        rc, o = pkg("INSTALL NOTES")
        checks["bad-signature"] = rc == 1 and "isn't signed by a key" in o and "no source has NOTES" in o
        if internet:
            rc, o = pkg("LIST", timeout=900)
            checks["freedos-list"] = re.search(r"^PKG CHOICE .*\(FreeDOS\)$", o, re.M) is not None
            rc, o = pkg("INSTALL CHOICE", timeout=900)
            checks["freedos-install"] = rc == 0 and "in C:\\FDTEST" in o and "unsigned" in o and \
                exists("C:\\FDTEST\\BIN\\CHOICE.EXE") and not exists("C:\\FDTEST\\SOURCE\\NUL")
            rc, o = pkg("REMOVE CHOICE")
            checks["freedos-remove"] = rc == 0 and not exists("C:\\FDTEST\\NUL")
        else:
            notes.append("no internet: the FreeDOS source not asked")
        # the window: NOTES chosen (the signed source's rows come first), installed, removed
        srv.root = repos["one"]
        agent.ssh("glos display on 640x480x16")
        t0 = time.time()
        while time.time() - t0 < 60 and " 640x480x16 " not in agent.ssh("glos display")[1]:
            time.sleep(0.5)
        g = GuiDriver(agent, out, 16, False, "C:\\TEST\\")
        win = subprocess.Popen(agent.opts + ["glos@127.0.0.1", "C:\\TEST\\PKG.EXE --resolve test.glos:10.0.2.2"],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        checks["window"] = bool(g.until(lambda d: g.window("Packages", d) is not None, 120))
        time.sleep(20 if internet else 8)       # (the sources read)
        agent.ssh("glos key " + gui_key_list(["down", "home"]))
        agent.ssh("glos key " + gui_key_list(["alt+i"]))
        t0 = time.time()
        while time.time() - t0 < 120 and not exists("C:\\APPS\\NOTES\\README.TXT"):
            time.sleep(2)
        checks["window-install"] = exists("C:\\APPS\\NOTES\\README.TXT")
        agent.ssh("glos key " + gui_key_list(["alt+r"]))
        t0 = time.time()
        while time.time() - t0 < 60 and exists("C:\\APPS\\NUL"):
            time.sleep(2)
        checks["window-remove"] = not exists("C:\\APPS\\NUL")
        agent.ssh("glos key " + gui_key_list(["alt+f4"]))
        try:
            o, _ = win.communicate(timeout=60)
            checks["window-end"] = b"PKG end" in o
            open(os.path.join(out, "window.out"), "wb").write(o)
        except subprocess.TimeoutExpired:
            win.kill()
            checks["window-end"] = False
        checks["sockets"] = "sockets=0" in agent.ssh("glos net")[1]
        checks["exit"] = agent.ssh("glos exit")[0] == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    text = agent.wait()
    srv.close()
    checks["no-panic"] = "GLOS-PANIC" not in text and "GLOS-WARN" not in text
    for n in notes:
        print("    pkg-%s %s" % (tag, n))
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s" % ("pkg-" + tag, "PASS" if not bad else "FAIL",
                             "" if not bad else " failed: " + " ".join(bad)), not bad


VNC_CASES = [("bf6", "g200", "ne2kpci", 16), ("bf6", "g400", "ne2kpci", 32), ("bf6", "vbe", "ne2kpci", 8)]


def vnc_case(profile, vcard, card, bits):
    """VNC and capture (M10 step F; kernel/wm/vnc.c, glos rec, tools/rec.py). The gates: a stock VNC client
    (vncdotool, pinned: tests/vnc) views and drives the desktop through an SSH tunnel (ssh -L to 127.0.0.1:5900),
    and a capture's frames are identical to glos shot. Its screen equal to glos shot's (with a second viewer held
    on, so that the task bar's mark is the same in both); text typed into Notepad through it and saved, read back;
    Notepad's close box pressed through it, the window gone; the screen equal again. At 16 bits, glos rec runs
    meanwhile: each glos shot taken during it is one of its frames, byte for byte."""
    import socket
    import threading
    tag = "%s-%s-%d" % (profile, vcard, bits)
    out = os.path.join(ROOT, "out", "vnc-%s-files" % tag)
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        if f.endswith(".png"):
            os.remove(os.path.join(out, f))
    open(os.path.join(out, "GLOS.CFG"), "w").write("; GLOS.CFG for the vnc suite\r\n[vnc]\r\nenable = on\r\n")
    agent = Agent("vnc-" + tag, profile, card, ["--card", vcard,
        "--file", "%s=/TEST/GLOS.CFG" % os.path.relpath(os.path.join(out, "GLOS.CFG"), ROOT),
        "--file", "build/apps/NOTEPAD.EXE=/TEST/NOTEPAD.EXE",
        "--cmd", "SERSAY HX-START vnc", "--cmd", "C:\\TEST\\GLOS.EXE", "--cmd", "VMODE", "--cmd", "SERSAY HX-DONE 0"],
        timeout=3600)
    checks, notes, procs = {"listen": agent.listening()}, [], []
    hold = "glos-vnc-hold-" + tag
    g = GuiDriver(agent, out, bits, False, "C:\\TEST\\")

    def vncdo(*args, timeout=180):
        r = subprocess.run(["docker", "run", "--rm", "--network", "host", "-v", out + ":/out", "glos-vnc:m10",
                            "timeout", str(timeout), "vncdo", "-s", "127.0.0.1::%d" % port] + list(args),
                           capture_output=True, text=True, timeout=timeout + 60)
        if r.returncode:
            notes.append("vncdo %s: %d %s" % (" ".join(args), r.returncode, (r.stdout + r.stderr)[-200:]))
        return r.returncode == 0

    def shot():
        for _ in range(20):                     # (glos rec's capture may have the screen a moment)
            data = subprocess.run(agent.opts + ["glos@127.0.0.1", "glos shot"], capture_output=True, timeout=180).stdout
            if data[:8] == b"\x89PNG\r\n\x1a\n":
                return data
            time.sleep(0.5)
        return b""

    def same(name):
        from PIL import Image
        for _ in range(3):                      # (a shot either side of the capture: the clock may turn between)
            before = shot()
            if not vncdo("capture", "/out/%s.png" % name):
                return False
            data = shot()
            if data == before:
                break
        open(os.path.join(out, "%s-shot.png" % name), "wb").write(data)
        a = png_rgb(data)
        try:
            v = Image.open(os.path.join(out, "%s.png" % name)).convert("RGB")
        except OSError:
            return False
        if not a or (a[0], a[1]) != v.size:
            notes.append("%s: sizes %s %s" % (name, a and (a[0], a[1]), v.size))
            return False
        vb = v.tobytes()
        diff = sum(1 for i in range(0, len(a[2]), 3) if a[2][i:i + 3] != vb[i:i + 3])
        if diff:
            notes.append("%s: %d pixels differ" % (name, diff))
        return diff == 0

    try:
        subprocess.run(["docker", "build", "-q", "-t", "glos-vnc:m10", os.path.join(ROOT, "tests", "vnc")], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        checks["on"] = agent.ssh("glos display on 640x480x%d" % bits)[0] == 0
        t0 = time.time()
        while time.time() - t0 < 60 and (" 640x480x%d " % bits) not in agent.ssh("glos display")[1]:
            time.sleep(0.5)
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
        while time.time() - t0 < 60 and "GLOS-VNC init" not in agent.log():
            time.sleep(0.5)
        time.sleep(3)
        rec = None
        if bits == 16:                          # glos rec meanwhile, into rec/ (tools/rec.py)
            recout = os.path.join(out, "rec")
            rec = subprocess.Popen("%s | %s %s %s" % (" ".join(agent.opts + ["glos@127.0.0.1", "'glos rec --fps 5 --seconds 120'"]),
                                                    sys.executable, os.path.join(ROOT, "tools", "rec.py"), recout),
                                   shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            time.sleep(3)
        checks["desk-same"] = same("desk")
        procs.append(subprocess.Popen(agent.opts + ["glos@127.0.0.1", "C:\\TEST\\NOTEPAD.EXE C:\\TEST\\VNC.TXT"],
                                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT))
        d = g.until(lambda d: any("Notepad" in w["title"] and w["shown"] for w in d["win"]), 90)
        time.sleep(3)
        checks["notepad-same"] = same("notepad")
        vncdo("type", "hello from vnc")
        vncdo("key", "ctrl-s")
        t0 = time.time()
        got = None
        while time.time() - t0 < 60 and got != b"hello from vnc":
            time.sleep(2)
            r = subprocess.run(["scp", "-P", str(agent.port), "-i", agent.key, "-o", "IdentitiesOnly=yes", "-o",
                                "BatchMode=yes", "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
                                "-o", "LogLevel=ERROR", "glos@127.0.0.1:/C/TEST/VNC.TXT", os.path.join(out, "VNC.TXT")],
                               capture_output=True, timeout=120)
            got = open(os.path.join(out, "VNC.TXT"), "rb").read() if r.returncode == 0 else None
        checks["keys"] = got == b"hello from vnc"
        if not checks["keys"]:
            notes.append("VNC.TXT: %r" % got)
        time.sleep(2)
        checks["typed-same"] = same("typed")
        w = next((w for w in g.desk()["win"] if "Notepad" in w["title"] and w["shown"]), None)
        if w:                                   # the close box: the title bar's left, 10 from the frame's corner
            vncdo("move", str(w["x"] + 9), str(w["y"] - 10), "click", "1")
        checks["button"] = bool(w) and bool(g.until(lambda d: not any("Notepad" in x["title"] and x["shown"]
                                                                       for x in d["win"]), 60))
        time.sleep(3)
        checks["after-same"] = same("after")
        if rec:
            o, _ = rec.communicate(timeout=300)
            frames = {}
            for f in sorted(os.listdir(recout)) if os.path.isdir(recout) else []:
                if f.endswith(".png"):
                    frames[open(os.path.join(recout, f), "rb").read()] = f
            shots = [open(os.path.join(out, n + "-shot.png"), "rb").read() for n in ("desk", "notepad", "typed", "after")]
            found = [frames.get(x) for x in shots]
            checks["rec-frames"] = len(frames) >= 3 and all(found)
            checks["rec-index"] = os.path.exists(os.path.join(recout, "index.txt"))
            notes.append("rec: %d frames; the shots were %s; %s" % (len(frames), found, o.decode("latin-1").strip()[-80:]))
            notes += [l for l in agent.log().splitlines() if l.startswith("GLOS-REC")]    # (captures it retried)
        checks["vnc-log"] = "GLOS-VNC open viewers=2" in agent.log()
        subprocess.run(["docker", "rm", "-f", hold], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        tunnel.terminate()
        checks["exit"] = agent.ssh("glos exit")[0] == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    for p in procs:
        if p.poll() is None:
            p.kill()
    subprocess.run(["docker", "rm", "-f", hold], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    text = agent.wait()
    checks["no-panic"] = "GLOS-PANIC" not in text and "GLOS-WARN" not in text
    for n in notes:
        print("    vnc-%s %s" % (tag, n))
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s" % ("vnc-" + tag, "PASS" if not bad else "FAIL",
                             "" if not bad else " failed: " + " ".join(bad)), not bad


def config_case(profile, vcard, card):
    """GLOS.CFG's settings (M9 step B; include/glos/config.h, kernel/core/config.c): every setting the schema has
    set over ssh (glos config set) to a value other than its default, read back, in the file afterwards with the
    file's comments, blank lines and unknown section where they were, and in effect at once where it can be (the
    desktop's mode, the watchdog, the key repeat, the pointer's speed, the mixer's rate and the mixer); a wrong
    value refused with the rule it broke; a program's env in its next run; and after glos exit and a new start the
    loader and the kernel reading the changed file."""
    tag = "%s-%s" % (profile, vcard)
    out = os.path.join(ROOT, "out", "config-" + tag)
    os.makedirs(out, exist_ok=True)
    cfg = os.path.join(ROOT, "out", "config-%s.cfg" % tag)
    open(cfg, "w").write(CONFIG_FILE.replace("\n", "\r\n"))
    agent = Agent("config-" + tag, profile, card, ["--card", vcard, "--sound", "sb16",
        "--pre", "SET BLASTER=A220 I5 D1 H5 T6", "--file", "%s=/TEST/GLOS.CFG" % os.path.relpath(cfg, ROOT),
        "--cmd", "SERSAY HX-START config", "--cmd", "C:\\TEST\\GLOS.EXE", "--cmd", "SERSAY HX-AGAIN",
        "--cmd", "C:\\TEST\\GLOS.EXE", "--cmd", "VMODE", "--cmd", "SERSAY HX-DONE 0"], timeout=2400)
    checks, notes = {"listen": agent.listening()}, []

    def loaded(limit=60):
        t0 = time.time()
        while time.time() - t0 < limit:
            if agent.ssh("glos config")[1].startswith("config loaded"):
                return True
            time.sleep(0.5)
        return False

    def get(sec, key):
        rc, o, _ = agent.ssh("glos config get %s %s" % (sec, key))
        return o.rstrip("\r\n") if rc == 0 else None

    try:
        checks["loaded"] = loaded()
        lst = agent.ssh("glos config")[1]
        checks["list"] = "[display] accel = on\n" in lst.replace("\r", "") and "[display] mode = none (default)" in lst \
            and "[program COMMAND.COM] env = GLOSCFG=one" in lst
        # A program's env, from the file the loader read and the kernel keeps.
        checks["env-file"] = "GLOSCFG=one" in agent.ssh("ECHO GLOSCFG=%GLOSCFG%")[1]
        for sec, key, val, effect in CONFIG_SETS:
            mark = len(agent.log())
            if effect == "pointer":
                agent.ssh("glos mouse -2000 -2000")
            rc, o, e = agent.ssh("glos config set %s %s %s" % (sec, key, val))
            ok = rc == 0 and o.startswith("ok ") and get(sec, key) == val.replace(";", "\n")
            if ok and effect == "display":
                t0 = time.time()
                while time.time() - t0 < 60 and " 640x480x16 " not in agent.ssh("glos display")[1]:
                    time.sleep(0.5)
                ok = " 640x480x16 " in agent.ssh("glos display")[1]
            elif ok and effect == "pointer":
                agent.ssh("glos mouse 10 5")
                m = re.search(r"pointer=(-?\d+),(-?\d+)", agent.ssh("glos desk")[1])
                ok = bool(m) and (int(m.group(1)), int(m.group(2))) == (20, 10)
                agent.ssh("glos config set input pointer_speed 1")
            elif ok and effect:
                t0 = time.time()
                while time.time() - t0 < 10 and effect not in agent.log()[mark:]:
                    time.sleep(0.3)
                ok = effect in agent.log()[mark:]
            checks["%s.%s" % (sec.split(":")[0], key)] = ok
            if not ok:
                notes.append("set %s %s %s: rc=%d %r %r" % (sec, key, val, rc, o.strip(), e.strip()))
        rc, o, e = agent.ssh("glos config set display mode 800x600x24")
        checks["refused"] = rc == 1 and "WIDTHxHEIGHTxBITS" in e
        rc, o, e = agent.ssh("glos config set display colour red")
        checks["unknown"] = rc == 2
        agent.ssh("glos config set sound mixer on")
        agent.ssh("glos config set program:COMMAND.COM env GLOSCFG=two")
        checks["env-set"] = "GLOSCFG=two" in agent.ssh("ECHO GLOSCFG=%GLOSCFG%")[1]
        # The file as the kernel wrote it: the suite's lines all there, the old ones where they were.
        t0 = time.time()
        while time.time() - t0 < 30 and " pending" in agent.ssh("glos config")[1].split("\n")[0]:
            time.sleep(0.5)
        p = subprocess.run(agent.opts + ["-o", "LogLevel=ERROR"] + ["glos@127.0.0.1", "glos config"],
                           capture_output=True, timeout=60)
        f = subprocess.run(["scp", "-P", str(agent.port), "-i", agent.key, "-o", "IdentitiesOnly=yes", "-o",
                            "BatchMode=yes", "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
                            "-o", "LogLevel=ERROR", "glos@127.0.0.1:/C/TEST/GLOS.CFG", os.path.join(out, "GLOS.CFG")],
                           capture_output=True, timeout=120)
        text = open(os.path.join(out, "GLOS.CFG"), "rb").read().decode("latin-1") if f.returncode == 0 else ""
        lines = text.replace("\r\n", "\n").split("\n")
        first = CONFIG_FILE.split("\n")
        checks["file-kept"] = lines[:2] == first[:2] and "[unknown]" in lines and "colour = blue" in lines \
            and lines[lines.index("[unknown]") + 1] == "colour = blue" and "; no desktop at first" in lines
        checks["file-set"] = all(("%s = %s" % (k, v.split(";")[0])) in lines for _, k, v, _ in CONFIG_SETS
                                 if k not in ("mixer", "pointer_speed"))
        checks["file-crlf"] = text.count("\r\n") == text.count("\n") and text.endswith("\r\n")
        if not (checks["file-kept"] and checks["file-set"]):
            notes.append("the file: %r" % text[:600])
        # Put back what would get in the second start's way, then a new start reads the file.
        for sec, key in (("shell", "comspec"), ("shell", "autoexec"), ("shell", "console"), ("shell", "options"),
                         ("desktop", "start"), ("display", "accel")):
            agent.ssh("glos config unset %s %s" % (sec, key))
        agent.ssh("glos config set display mode 800x600x16")
        t0 = time.time()
        while time.time() - t0 < 30 and " pending" in agent.ssh("glos config")[1].split("\n")[0]:
            time.sleep(0.5)
        checks["exit-1"] = agent.ssh("glos exit")[0] == 0
        checks["listen-2"] = agent.listening(after="HX-AGAIN")
        checks["loaded-2"] = loaded()
        t0 = time.time()
        while time.time() - t0 < 60 and " 800x600x16 " not in agent.ssh("glos display")[1]:
            time.sleep(0.5)
        checks["mode-at-start"] = " 800x600x16 " in agent.ssh("glos display")[1]
        checks["kept-2"] = get("program:FWHEEL.EXE", "memory") == "16384" and get("net", "hostname") == "cfgtest"
        checks["exit-2"] = agent.ssh("glos exit")[0] == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    text = agent.wait()
    checks["no-panic"] = "GLOS-PANIC" not in text and "GLOS-WARN" not in text
    checks["done"] = text.count("GLOS-EXIT code=0") == 2 and "HX-DONE 0" in text
    for n in notes:
        print("    config-%s %s" % (tag, n))
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s" % ("config-" + tag, "PASS" if not bad else "FAIL",
                             "" if not bad else " failed: " + " ".join(bad)), not bad


# ---- programs started by apps, console jobs, the shell's events, stats, watches (M9 step C)

CONSOLE_CASES = [("bf6", "g200", "ne2kpci", 16), ("486dx2", "vbe", "ne2k", 8)]


def console_case(profile, vcard, card, bits):
    """Programs an app starts (GLOS_FN_RUN, GLOS_FN_JOB; tests/dos/runtest.c, conin.c, pmvga.c): a console job's
    output by every way DOS and the BIOS write it, its exit code, keys typed into it three ways (a DOS line, INT
    16h, DOS characters) and Ctrl-C, CLS kept in its window; a mode set, a write to B800h from real mode and from
    protected mode and a touch of the VGA's ports each taking it full-screen, the desktop back after; its folder;
    a batch file; an app running beside it; watched directories told of changes by a job, the agent and SFTP;
    GLOS_FN_STAT's figures; the shell sent the launcher (Ctrl-Esc, the Windows key, GLOS's mark) and Ctrl-Alt-Del,
    with its desktop window at the bottom; an app ended by another."""
    tag = "%s-%s-%d" % (profile, vcard, bits)
    out = os.path.join(ROOT, "out", "console-" + tag)
    os.makedirs(out, exist_ok=True)
    bat = os.path.join(ROOT, "out", "console-%s.bat" % tag)     # (not in the run's own directory: run.py empties it)
    open(bat, "w").write("@ECHO batch %1 %2\r\n@C:\\TEST\\CONIN.EXE code 4\r\n@IF ERRORLEVEL 4 ECHO level 4\r\n")
    agent = Agent("console-" + tag, profile, card, ["--card", vcard,
        "--file", "build/dj/RUNTEST.EXE=/TEST/RUNTEST.EXE", "--file", "build/dj/PMVGA.EXE=/TEST/PMVGA.EXE",
        "--file", "build/ow/dos/CONIN.EXE=/TEST/CONIN.EXE", "--file", "build/ow/dos/ECHOARGS.EXE=/TEST/ECHOARGS.EXE",
        "--file", "%s=/TEST/T.BAT" % os.path.relpath(bat, ROOT),
        "--cmd", "SERSAY HX-START console", "--cmd", "C:\\TEST\\GLOS.EXE", "--cmd", "VMODE", "--cmd", "SERSAY HX-DONE 0"],
        timeout=3600)
    agent.opts += ["-o", "ControlMaster=auto", "-o", "ControlPath=" + os.path.join(agent.tmp, "cm"),
                   "-o", "ControlPersist=600"]
    checks, notes = {"listen": agent.listening()}, []
    rt = "C:\\TEST\\RUNTEST.EXE "

    def run(args, timeout=300):
        rc, o, e = agent.ssh(rt + "run " + args, timeout=timeout)
        o = o.replace("\r", "")
        text = "".join(re.sub(r"\\x([0-9a-f]{2})", lambda m: chr(int(m.group(1), 16)), l[4:])
                       .replace("\\n", "\n").replace("\\r", "").replace("\\\\", "\\")
                       for l in o.split("\n") if l.startswith("OUT "))
        states = [int(x) for x in re.findall(r"^STATE (\d+)$", o, re.M)]
        m = re.search(r"^DONE code=(\d+)$", o, re.M)
        return text, states, int(m.group(1)) if m else None, o

    def desktop_back(limit=60):
        t0 = time.time()
        while time.time() - t0 < limit:
            if agent.ssh("glos display")[1].startswith("display on mode="):
                return True
            time.sleep(0.5)
        return False

    def check(name, ok, why=""):
        checks[name] = bool(ok)
        if not ok:
            notes.append("%s: %s" % (name, why))

    try:
        checks["on"] = agent.ssh("glos display on 640x480x%d" % bits)[0] == 0 and desktop_back()
        text, st, code, o = run("C:\\TEST\\ECHOARGS.EXE one two")
        check("output", "one two" in text and "nine two six int29" in text and code == 7 and 2 not in st,
              repr(o[-400:]))
        text, st, code, o = run('--type "hello\\n" C:\\TEST\\CONIN.EXE line')
        check("keys-line", "LINE hello" in text and "hello" in text.split("LINE")[0], repr(o[-300:]))
        text, st, code, o = run('--type abc C:\\TEST\\CONIN.EXE bios 3')
        check("keys-bios", "BIOS 61 62 63" in text, repr(o[-300:]))
        text, st, code, o = run('--type xy C:\\TEST\\CONIN.EXE char 2')
        check("keys-char", "CHAR xy" in text, repr(o[-300:]))
        text, st, code, o = run("C:\\TEST\\CONIN.EXE code 5")
        check("code", code == 5, repr(o[-200:]))
        text, st, code, o = run('--now --type xyz C:\\TEST\\CONIN.EXE wait 3')  # (typed, never read: the app's again)
        check("type-ahead", re.search(r"^LEFT xyz$", o, re.M) is not None, repr(o[-200:]))
        text, st, code, o = run("C:\\TEST\\CONIN.EXE tty HELLO")
        check("teletype", "HELLO" in text and 2 not in st, repr(o[-200:]))
        text, st, code, o = run("C:\\TEST\\CONIN.EXE cls")
        check("cls", "\f" in text and "CLS done" in text and 2 not in st, repr(o[-200:]))
        text, st, code, o = run('--type "\\c" C:\\TEST\\CONIN.EXE line')
        check("ctrl-c", "^C" in text and code is not None, repr(o[-200:]))
        for what in ("mode", "b800", "port"):
            mark = len(agent.log())
            text, st, code, o = run("C:\\TEST\\CONIN.EXE " + what)
            check("full-" + what, 2 in st and ("%s done" % what.upper()) in text and "GLOS-SESSION full" in agent.log()[mark:]
                  and desktop_back(), repr(o[-300:]))
        text, st, code, o = run("C:\\TEST\\PMVGA.EXE")
        check("full-pm", 2 in st and "PMVGA done" in text and desktop_back(), repr(o[-300:]))
        text, st, code, o = run("--full C:\\TEST\\CONIN.EXE code 1")
        check("full-asked", st[:2] in ([2], [0, 2], [1, 2]) or (2 in st and 1 not in st), repr(st))
        text, st, code, o = run("--dir C:\\TEST CONIN.EXE code 3")
        check("dir", code == 3, repr(o[-200:]))
        text, st, code, o = run("C:\\TEST\\T.BAT one two")
        check("batch", "batch one two" in text and "level 4" in text, repr(o[-300:]))
        # An app beside a console job: it runs on (a session would pause it).
        side = subprocess.Popen(agent.opts + ["glos@127.0.0.1", rt + "beside 6"], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE)
        text, st, code, o = run("C:\\TEST\\CONIN.EXE wait 4")
        so = side.communicate(timeout=120)[0].decode("latin-1")
        m = re.search(r"BESIDE (\d+)", so)
        check("beside", "WAIT done" in text and m and int(m.group(1)) > 0, so[-200:])
        # Watches: a job's file, the agent's, SFTP's.
        w = subprocess.Popen(agent.opts + ["glos@127.0.0.1", rt + "watch C:\\TEST 40"], stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE)
        time.sleep(4)
        n0 = 0
        run("C:\\COMMAND.COM /C ECHO job> C:\\TEST\\W1.TXT")
        time.sleep(2)
        agent.ssh("ECHO agent> C:\\TEST\\W2.TXT")
        time.sleep(2)
        src = os.path.join(out, "W3.TXT")
        open(src, "w").write("sftp\r\n")
        subprocess.run(["scp", "-P", str(agent.port), "-i", agent.key, "-o", "IdentitiesOnly=yes", "-o", "BatchMode=yes",
                        "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null", "-o", "LogLevel=ERROR",
                        src, "glos@127.0.0.1:/C/TEST/W3.TXT"], capture_output=True, timeout=120)
        wo = w.communicate(timeout=120)[0].decode("latin-1")
        n0 = wo.count("FILES ")
        check("watch", n0 >= 3, wo[-300:])
        # Figures.
        so = agent.ssh(rt + "stat")[1]
        m = re.search(r"^SYS .*screen=(\d+)x(\d+)x(\d+) accel=(\d+)", so, re.M)
        check("stat", m and (m.group(1), m.group(2), m.group(3)) == ("640", "480", str(bits))
              and (m.group(4) == "1") == (vcard in MATROX_CARDS) and "name=RUNTEST" in so
              and re.search(r"threads=[1-9]", so) and re.search(r"LOG bytes=[1-9]", so), so[-500:])
        # The shell: the launcher three ways, Ctrl-Alt-Del, its desktop at the bottom.
        sh = subprocess.Popen(agent.opts + ["glos@127.0.0.1", rt + "shell 30"], stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE)
        t0 = time.time()
        while time.time() - t0 < 60 and '"RUNTEST Desktop"' not in agent.ssh("glos desk")[1]:
            time.sleep(0.5)
        desk = agent.ssh("glos desk")[1]
        check("desktop", re.search(r'^window \d+ shown desktop at 0,0 640x460 "RUNTEST Desktop"$', desk.replace("\r", ""),
                                   re.M), desk[-400:])
        agent.ssh("glos key 0x1D:down,0.05:0x01,0.2:0x1D:up")
        time.sleep(1)
        agent.ssh("glos key 0x15B")
        time.sleep(1)
        m = re.search(r"pointer=(-?\d+),(-?\d+)", agent.ssh("glos desk")[1])
        if m:
            agent.ssh("glos mouse %d %d" % (5 - int(m.group(1)), 470 - int(m.group(2))))
            agent.ssh("glos key 0:mouse:0:0:1,0.05:mouse:0:0:0")
        time.sleep(1)
        agent.ssh("glos key 0x1D:down,0.05:0x38:down,0.1:0x153,0.25:0x38:up,0.3:0x1D:up")
        so = sh.communicate(timeout=120)[0].decode("latin-1")
        check("launch", so.count("LAUNCH 0 460") == 3, so[-300:])
        check("syskey", "SYSKEY" in so, so[-300:])
        # One app ending another.
        b = subprocess.Popen(agent.opts + ["glos@127.0.0.1", rt + "beside 60"], stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE)
        time.sleep(4)
        m = re.search(r"^app (\d+) RUNTEST", agent.ssh("glos ps")[1].replace("\r", ""), re.M)
        kill_o = agent.ssh(rt + "kill %s" % (m.group(1) if m else "0"))[1]
        b.communicate(timeout=60)
        check("kill", m and "KILLED" in kill_o and "reason=monitor" in agent.log(), kill_o)
        checks["exit"] = agent.ssh("glos exit")[0] == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    text = agent.wait()
    checks["no-panic"] = "GLOS-PANIC" not in text and "GLOS-WARN" not in text
    checks["done"] = "GLOS-EXIT code=0" in text and "HX-DONE 0" in text
    for n in notes:
        print("    console-%s %s" % (tag, n))
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s" % ("console-" + tag, "PASS" if not bad else "FAIL",
                             "" if not bad else " failed: " + " ".join(bad)), not bad


# The native apps (M9 steps D-G): their scripts (tests/gui, all but GUITEST's) on the desktop, the apps beside
# GLOS.EXE.
NATIVE_APPS = ["NOTEPAD", "CALC", "IMAGES", "TERM", "FILES", "MONITOR", "CONTROL"]
NATIVE_CASES = [("bf6", "vbe", "ne2kpci", 8), ("bf6", "g200", "ne2kpci", 16), ("bf6", "g400", "ne2kpci", 32),
                ("486dx2", "vbe", "ne2k", 8)]


def native_case(profile, vcard, card, bits):
    scripts = sorted(f for f in os.listdir(os.path.join(ROOT, "tests", "gui"))
                     if f.endswith(".txt") and "GUITEST" not in open(os.path.join(ROOT, "tests", "gui", f)).read())
    only = os.environ.get("NATIVE_ONLY")        # e.g. NATIVE_ONLY=notepad,calc
    if only:
        scripts = [f for f in scripts if f[:-4] in only.split(",")]
    return gui_case(profile, vcard, card, bits, scripts=scripts, tag_prefix="native",
                    files=["build/apps/%s.EXE=/TEST/%s.EXE" % (a, a) for a in NATIVE_APPS]
                    + ["build/ow/dos/ECHOARGS.EXE=/TEST/ECHOARGS.EXE", "build/ow/dos/CONIN.EXE=/TEST/CONIN.EXE",
                       "tests/gui/drives/C/GLOS/GLOS.CFG=/TEST/GLOS.CFG"])   # (the Control Panel's, beside GLOS.EXE)


# ---- the Control Panel (M9 step G): every setting the schema has, changed by keys alone

CONTROL_CASES = [("bf6", "g200", "ne2kpci")]
CONTROL_NUMBERS = {"gpu_watchdog": "1000", "repeat_rate": "20", "double_click": "400", "envsize": "2048",
                   "memory": "8192", "interval": "12"}
CONTROL_TEXTS = {"hostname": "cptest", "start": "NOTEPAD.EXE", "console": "C:\\KIOSK.BAT", "options": "/NOVME",
                 "title": "CP Test", "args": "-a -b", "comspec": "C:\\COMMAND.COM",
                 "autoexec": "C:\\AUTOEXEC.BAT", "dir": "C:\\CP", "blaster": "A220 I7 D1 H5",
                 "env": "CPA=1\nPATH=C:\\A;C:\\B", "server": "time.example.org:1230",
                 "zone": "CET-1CEST,M3.5.0,M10.5.0/3"}


def cfg_pages():
    """cfg_sections as sdk/config/cfg.c has them: the Control Panel's pages, in order."""
    src = open(os.path.join(ROOT, "sdk", "config", "cfg.c")).read()
    body = src[src.index("cfg_sections[] = {"):]
    return re.findall(r'"(\w+)"', body[:body.index("};")])


def cfg_schema():
    """The settings' schema as sdk/config/cfg.c has it: a dict a setting (section, key, type, when, min, max,
    choices, default, label)."""
    src = open(os.path.join(ROOT, "sdk", "config", "cfg.c")).read()
    at = src.index("cfg_schema[] = {")
    body = src[at:src.index("\n};", at)]
    keys = []
    for m in re.finditer(r'\{\s*"(\w+)",\s*"([^"]+)",\s*(CFG_\w+),\s*(CFG_\w+),\s*(-?\d+),\s*(-?\d+),\s*'
                         r'("[^"]*"|0),\s*"([^"]*)",\s*"([^"]*)"', body):
        sec, key, typ, when, mn, mx, ch, d, label = m.groups()
        keys.append({"section": sec, "key": key, "type": typ, "when": when, "min": int(mn), "max": int(mx),
                     "choices": None if ch == "0" else ch.strip('"').split("|"), "default": d, "label": label})
    return keys


def control_script(schema):
    """Keys for CONTROL.EXE that change every setting from its default (the file the suite gives it has none),
    a page at a time; the values each should then have: (section, key, value)."""
    lines, want, page = ["run CONTROL.EXE", 'wait "Control Panel"'], [], 0

    def to(p):
        nonlocal page
        if p != page:
            lines.append("quick" + " ctrl+tab" * ((p - page) % len(cfg_pages())))
            page = p

    def value(k):
        letter = k["label"][k["label"].index("&") + 1].lower()
        ch = k["choices"]
        if k["type"] == "CFG_CHOICE" and ch in (["on", "off"], ["0", "1"]):
            lines.append("quick alt+" + letter)  # (its letter presses a check box)
            return ch[1] if k["default"] == ch[0] else ch[0]
        if k["type"] == "CFG_CHOICE":
            i = (ch.index(k["default"]) + 1) % len(ch)
            lines.append("quick alt+%s home%s" % (letter, " down" * i))
            return ch[i]
        v = CONTROL_NUMBERS.get(k["key"]) if k["type"] == "CFG_NUMBER" else CONTROL_TEXTS.get(k["key"], "X")
        if k["type"] == "CFG_ENV":
            lines.append("quick alt+%s ctrl+a" % letter)
        else:
            lines.append("quick alt+%s home shift+end" % letter)
        lines.append("type " + v.replace("\\", "\\\\").replace("\n", "\\n"))  # (the script's escapes)
        return v

    for k in schema:
        if k["section"] in ("open", "program") or k["type"] == "CFG_MODE":
            continue
        to(cfg_pages().index(k["section"]))
        want.append((k["section"], k["key"], value(k)))
    to(cfg_pages().index("open"))             # a kind of file
    lines += ["key alt+a", 'wait "Add"', "quick home shift+end", "type .doc\\n", "pause 1500", 'wait "Add"',
              "type NOTEPAD.EXE\\n", 'gone "Add"']
    want.append(("open", ".DOC", "NOTEPAD.EXE"))
    to(cfg_pages().index("program"))          # a new program's profile
    lines += ["key alt+n", 'wait "New Program"', "type cptest.exe\\n", 'gone "New Program"']
    for k in schema:
        if k["section"] == "program":
            want.append(("program CPTEST.EXE", k["key"], value(k)))
    lines += ["quick alt+p", "pause 3000"]
    for s, k, v in want:
        lines.append("expect CONTROL set [%s] %s = %s" % (s, k, v.replace("\n", "|")))
    to(0)                                       # the screen: a mode kept, and one let go back
    lines += ["quick alt+s home" + " down" * 5, "quick alt+p", 'wait "Screen"', "pause 2000", "key alt+k",
              'gone "Screen"', "pause 2000", "quick alt+s home" + " down" * 2, "quick alt+p", 'wait "Screen"',
              "pause 18000", 'gone "Screen"', "pause 2000", "key alt+f4", 'gone "Control Panel"',
              "expect CONTROL mode kept 800x600x16", "expect CONTROL mode back 800x600x16", "expect CONTROL end"]
    return lines, want


def control_case(profile, vcard, card):
    """Every setting in GLOS.CFG changed from the Control Panel (M9 step G; apps/control): from a file with none
    of them, the suite walks the schema (sdk/config/cfg.c), changes each setting from its default by keys alone
    (a page at a time, Alt and the label's letter, the value typed or chosen), adds a kind of file and a new
    program's profile with every key, and applies; then glos config get and the file agree with each; a screen
    mode kept and one let go back after fifteen seconds."""
    import time
    tag = "%s-%s" % (profile, vcard)
    out = os.path.join(ROOT, "out", "control-" + tag)
    os.makedirs(out, exist_ok=True)
    cfg = os.path.join(ROOT, "out", "control-%s.cfg" % tag)
    open(cfg, "w").write("; GLOS.CFG for the control suite: nothing set, every setting its default.\r\n[display]\r\n")
    lines, want = control_script(cfg_schema())
    script = os.path.join("out", "control-%s.txt" % tag)
    open(os.path.join(ROOT, script), "w").write("\n".join(lines) + "\n")
    agent = Agent("control-" + tag, profile, card, ["--card", vcard, "--sound", "sb16",
        "--pre", "SET BLASTER=A220 I5 D1 H5 T6", "--file", "%s=/TEST/GLOS.CFG" % os.path.relpath(cfg, ROOT),
        "--file", "build/apps/CONTROL.EXE=/TEST/CONTROL.EXE",
        "--cmd", "SERSAY HX-START control", "--cmd", "C:\\TEST\\GLOS.EXE", "--cmd", "VMODE",
        "--cmd", "SERSAY HX-DONE 0"], timeout=2400)
    agent.opts += ["-o", "ControlMaster=auto", "-o", "ControlPath=" + os.path.join(agent.tmp, "cm"),
                   "-o", "ControlPersist=600"]
    checks, notes = {"listen": agent.listening()}, []
    try:
        checks["on"] = agent.ssh("glos display on 640x480x16")[0] == 0
        t0 = time.time()
        while time.time() - t0 < 60 and " 640x480x16 " not in agent.ssh("glos display")[1]:
            time.sleep(0.5)
        g = GuiDriver(agent, out, 16, False, "C:\\TEST\\")
        g.play(script)
        text = g.finish()
        open(os.path.join(out, "control.out"), "w").write(text)
        checks["script"] = not g.bad
        notes += g.notes + (["script: " + " ".join(g.bad)] if g.bad else [])
        bad = []
        for sec, key, val in want:
            rc, o, _ = agent.ssh("glos config get %s %s" % (sec.replace("program ", "program:"), key))
            if rc != 0 or o.replace("\r", "").rstrip("\n") != val:
                bad.append("%s.%s=%r" % (sec, key, o.strip()))
        checks["get"] = not bad
        if bad:
            notes.append("get: " + " ".join(bad))
        rc, o, _ = agent.ssh("glos config get display mode")
        checks["mode-kept"] = o.strip() == "800x600x16" and " 800x600x16 " in agent.ssh("glos display")[1]
        t0 = time.time()
        while time.time() - t0 < 30 and " pending" in agent.ssh("glos config")[1].split("\n")[0]:
            time.sleep(0.5)
        f = subprocess.run(["scp", "-P", str(agent.port), "-i", agent.key, "-o", "IdentitiesOnly=yes", "-o",
                            "BatchMode=yes", "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
                            "-o", "LogLevel=ERROR", "glos@127.0.0.1:/C/TEST/GLOS.CFG", os.path.join(out, "GLOS.CFG")],
                           capture_output=True, timeout=120)
        ftext = open(os.path.join(out, "GLOS.CFG"), "rb").read().decode("latin-1") if f.returncode == 0 else ""
        sec, have = "", set()
        for l in ftext.replace("\r\n", "\n").split("\n"):
            if l.startswith("["):
                sec = l.strip("[]")
            elif " = " in l:
                k, v = l.split(" = ", 1)
                have.add((sec.lower(), k.lower(), v))
        missing = [(s, k, v) for s, k, v in want for one in v.split("\n") if (s.lower(), k.lower(), one) not in have]
        checks["file"] = not missing
        if missing:
            notes.append("the file lacks %r" % missing[:6])
        checks["exit"] = agent.ssh("glos exit")[0] == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    text = agent.wait()
    checks["no-panic"] = "GLOS-PANIC" not in text and "GLOS-WARN" not in text
    checks["done"] = "GLOS-EXIT code=0" in text and "HX-DONE 0" in text
    for n in notes:
        print("    control-%s %s" % (tag, n))
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s" % ("control-" + tag, "PASS" if not bad else "FAIL",
                             "" if not bad else " failed: " + " ".join(bad)), not bad


# (machine, video card, network card, bits a pixel)
GUI_CASES = [("bf6", "vbe", "ne2kpci", 8), ("bf6", "g200", "ne2kpci", 16), ("bf6", "g400", "ne2kpci", 32),
             ("486dx2", "vbe", "ne2k", 8)]


def gui_case(profile, vcard, card, bits, scripts=None, files=None, tag_prefix="gui", appdir="C:\\TEST\\"):
    """The toolkit (M9 step A): GUITEST's gallery, dialogs and clipboard driven by the scripts in tests/gui on a
    640x480 desktop, each shot equal to the host's (make gui-goldens); its output lines as expected; then two
    GUITESTs copying and pasting between them through the clipboard, and glos clip."""
    import time
    tag = "%s-%s-%d" % (profile, vcard, bits)
    files = (files or ["build/dj/GUITEST.EXE=/TEST/GUITEST.EXE"]) + ["sdk/gui/fonts/SMOOTH.DAT=/TEST/SMOOTH.DAT"]
    extra = []
    for f in files:
        extra += ["--file", f]
    for root, _, names in os.walk(os.path.join(ROOT, "tests", "gui", "drives", "C")):
        for nm in names:
            src = os.path.join(root, nm)
            extra += ["--file", "%s=/%s" % (os.path.relpath(src, ROOT), os.path.relpath(src, os.path.join(
                ROOT, "tests", "gui", "drives", "C")).replace(os.sep, "/"))]
    agent = Agent("%s-%s" % (tag_prefix, tag), profile, card, ["--card", vcard] + extra + [
        "--cmd", "SERSAY HX-START gui", "--cmd", "C:\\TEST\\GLOS.EXE", "--cmd", "VMODE", "--cmd", "SERSAY HX-DONE 0"],
        timeout=3600)
    agent.opts += ["-o", "ControlMaster=auto", "-o", "ControlPath=" + os.path.join(agent.tmp, "cm"),
                   "-o", "ControlPersist=600"]
    out = os.path.join(ROOT, "out", "%s-%s" % (tag_prefix, tag))
    os.makedirs(out, exist_ok=True)
    checks, info, notes = {"listen": agent.listening()}, "", []
    try:
        checks["ver"] = agent.ssh("glos ver")[0] == 0
        checks["on"] = agent.ssh("glos display on 640x480x%d" % bits)[0] == 0
        line = ""
        t0 = time.time()
        while time.time() - t0 < 60 and not line.startswith("display on mode="):
            line = agent.ssh("glos display")[1]
            time.sleep(0.5)
        checks["mode"] = (" 640x480x%d " % bits) in line
        if os.environ.get("GUI_SCRIPTS"):       # e.g. GUI_SCRIPTS=dialogs,files
            scripts = [x + ".txt" for x in os.environ["GUI_SCRIPTS"].split(",")]
        for script in scripts or sorted(f for f in os.listdir(os.path.join(ROOT, "tests", "gui"))
                                        if f.endswith(".txt") and open(os.path.join(ROOT, "tests", "gui", f)).readline()
                                        and "GUITEST" in open(os.path.join(ROOT, "tests", "gui", f)).read()):
            g = GuiDriver(agent, out, bits, " dac=6" in line, appdir)
            name = os.path.splitext(os.path.basename(script))[0]
            g.play(os.path.join("tests", "gui", os.path.basename(script)))
            with open(os.path.join(out, name + ".out"), "w") as f:
                f.write(g.finish())
            checks[name] = not g.bad
            if g.bad:
                notes.append("%s: %s" % (name, " ".join(g.bad)))
            notes += ["%s: %s" % (name, x) for x in g.notes]
        if tag_prefix == "gui":
            # The clipboard: one GUITEST's Ctrl+C, another's Ctrl+V, and glos clip between.
            g = GuiDriver(agent, out, bits, " dac=6" in line, appdir)
            a = subprocess.Popen(agent.opts + ["glos@127.0.0.1", appdir + "GUITEST.EXE clip one"],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            g.until(lambda d: g.window("GUITEST Clip", d) is not None, 120)
            agent.ssh("glos key " + gui_key_list(["ctrl+a", "ctrl+c"]))
            time.sleep(1)
            checks["clip-copied"] = agent.ssh("glos clip")[1] == "one"
            agent.ssh("glos key " + gui_key_list(["alt+f4"]))
            b = subprocess.Popen(agent.opts + ["glos@127.0.0.1", appdir + "GUITEST.EXE clip two"],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            time.sleep(1)
            g.until(lambda d: g.window("GUITEST Clip", d) is not None, 120)
            agent.ssh("glos key " + gui_key_list(["end", "ctrl+v"]))
            agent.ssh("glos clip three\\tand\\\\four")
            agent.ssh("glos key " + gui_key_list(["ctrl+v", "alt+f4"]))
            ob = b.communicate(timeout=120)[0].decode("latin-1").replace("\r", "")
            a.communicate(timeout=60)
            checks["clip-pasted"] = "GUI text twoone" in ob.split("\n")
            checks["clip-set"] = "GUI text twoonethree\tand\\four" in ob.split("\n")
            if not checks["clip-set"]:
                notes.append("clip: %r" % ob[-300:])
        checks["exit"] = agent.ssh("glos exit")[0] == 0
    except subprocess.TimeoutExpired:
        checks["timeout"] = False
    text = agent.wait()
    checks["no-panic"] = "GLOS-PANIC" not in text and "GLOS-WARN" not in text
    checks["done"] = "GLOS-EXIT code=0" in text and "HX-DONE 0" in text
    for n in notes:
        print("    %s-%s %s" % (tag_prefix, tag, n))
    bad = [k for k, v in checks.items() if not v]
    return "  %-22s %s%s%s" % ("%s-%s" % (tag_prefix, tag), "PASS" if not bad else "FAIL", info,
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
                                      "djtst", "dpmitools", "hdpmireg", "ecm", "dpmi16", "tpx", "sess", "shot", "input", "gdb", "m5exit", "apps", "display", "desk", "win", "gpu", "audio", "glwin", "sdlwin", "games", "gui", "config", "console", "native", "control", "time", "netapp", "https", "sshc", "pkg", "vnc"])
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
        res = matrix(ssh_case, SSH_CASES, a.jobs)
        if not os.environ.get("SSH_ONE") and not os.environ.get("SSH_ONLY"):
            res += matrix(reboot_case, [("bf6", "ne2kpci"), ("486dx2", "ne2k")], a.jobs)
        return 0 if all(r[1] for r in res) else 1
    if a.suite == "desk":
        cases = [c for c in DESK_CASES if not a.profile or c[0] in a.profile]
        return 0 if all(r[1] for r in matrix(desk_case, cases, a.jobs)) else 1
    if a.suite == "win":
        cases = [(win_case,) + c for c in WIN_CASES if not a.profile or c[0] in a.profile] \
            + [(win_phys_case,) + c for c in WIN_PHYS_CASES if not a.profile or c[0] in a.profile]
        return 0 if all(r[1] for r in matrix(lambda fn, *c: fn(*c), cases, a.jobs)) else 1
    if a.suite == "config":
        cases = [c for c in CONFIG_CASES if not a.profile or c[0] in a.profile]
        return 0 if all(r[1] for r in matrix(config_case, cases, a.jobs)) else 1
    if a.suite == "time":
        cases = [c for c in TIME_CASES if not a.profile or c[0] in a.profile]
        return 0 if all(r[1] for r in matrix(time_case, cases, a.jobs)) else 1
    if a.suite == "vnc":
        cases = [c for c in VNC_CASES if not a.profile or c[0] in a.profile]
        return 0 if all(r[1] for r in matrix(vnc_case, cases, a.jobs)) else 1
    if a.suite == "pkg":
        cases = [c for c in PKG_CASES if not a.profile or c[0] in a.profile]
        return 0 if all(r[1] for r in matrix(pkg_case, cases, a.jobs)) else 1
    if a.suite == "sshc":
        cases = [c for c in SSHC_CASES if not a.profile or c[0] in a.profile]
        return 0 if all(r[1] for r in matrix(sshc_case, cases, a.jobs)) else 1
    if a.suite == "https":
        cases = [c for c in HTTPS_CASES if not a.profile or c[0] in a.profile]
        return 0 if all(r[1] for r in matrix(https_case, cases, a.jobs)) else 1
    if a.suite == "netapp":
        cases = [c for c in NETAPP_CASES if not a.profile or c[0] in a.profile]
        return 0 if all(r[1] for r in matrix(netapp_case, cases, a.jobs)) else 1
    if a.suite == "control":
        cases = [c for c in CONTROL_CASES if not a.profile or c[0] in a.profile]
        return 0 if all(r[1] for r in matrix(control_case, cases, a.jobs)) else 1
    if a.suite == "console":
        cases = [c for c in CONSOLE_CASES if not a.profile or c[0] in a.profile]
        return 0 if all(r[1] for r in matrix(console_case, cases, a.jobs)) else 1
    if a.suite == "native":
        cases = [c for c in NATIVE_CASES if not a.profile or c[0] in a.profile]
        if os.environ.get("GUI_ONLY"):
            cases = [c for c in cases if c[1] in os.environ["GUI_ONLY"].split(",")]
        return 0 if all(r[1] for r in matrix(native_case, cases, a.jobs)) else 1
    if a.suite == "gui":
        cases = [c for c in GUI_CASES if not a.profile or c[0] in a.profile]
        if os.environ.get("GUI_ONLY"):          # e.g. GUI_ONLY=g200
            cases = [c for c in cases if c[1] in os.environ["GUI_ONLY"].split(",")]
        return 0 if all(r[1] for r in matrix(gui_case, cases, a.jobs)) else 1
    if a.suite == "games":
        cases = [c for c in GAMES_CASES if not a.profile or c[0] in a.profile]
        if os.environ.get("GAMES_ONLY"):
            cases = [c for c in cases if c[1] in os.environ["GAMES_ONLY"].split(",")]
        return 0 if all(r[1] for r in matrix(games_case, cases, a.jobs)) else 1
    if a.suite == "sdlwin":
        cases = [c for c in SDLWIN_CASES if not a.profile or c[0] in a.profile]
        if os.environ.get("SDLWIN_ONLY"):
            cases = [c for c in cases if c[1] in os.environ["SDLWIN_ONLY"].split(",")]
        return 0 if all(r[1] for r in matrix(sdlwin_case, cases, a.jobs)) else 1
    if a.suite == "glwin":
        cases = [c for c in GLWIN_CASES if not a.profile or c[0] in a.profile]
        if os.environ.get("GLWIN_ONLY"):        # e.g. GLWIN_ONLY=g200
            cases = [c for c in cases if c[1] in os.environ["GLWIN_ONLY"].split(",")]
        return 0 if all(r[1] for r in matrix(glwin_case, cases, a.jobs)) else 1
    if a.suite == "audio":
        cases = [c for c in AUDIO_CASES if not a.profile or c[0] in a.profile]
        return 0 if all(r[1] for r in matrix(audio_case, cases, a.jobs)) else 1
    if a.suite == "gpu":
        cases = [c for c in GPU_CASES if not a.profile or c[0] in a.profile]
        if os.environ.get("GPU_ONLY"):          # e.g. GPU_ONLY=g200
            cases = [c for c in cases if c[1] in os.environ["GPU_ONLY"].split(",")]
        return 0 if all(r[1] for r in matrix(gpu_case, cases, a.jobs)) else 1
    if a.suite == "display":
        cases = [c for c in DISPLAY_CASES if not a.profile or c[0] in a.profile]
        return 0 if all(r[1] for r in matrix(display_case, cases, a.jobs)) else 1
    if a.suite == "apps":
        cases = [c for c in APPS_CASES if not a.profile or c[0] in a.profile]
        res = matrix(apps_case, cases, a.jobs)
        res += matrix(apps_run, [(p, b) for p in a.profile or PROFILES for b in a.boot or ["default"]], a.jobs)
        return 0 if all(r[1] for r in res) else 1
    if a.suite == "m5exit":
        return 0 if all(r[1] for r in matrix(exit_case, EXIT_CASES, a.jobs)) else 1
    if a.suite == "gdb":
        cases = [c for c in [("bf6", "ne2kpci"), ("486dx2", "ne2k")] if not a.profile or c[0] in a.profile]
        return 0 if all(r[1] for r in matrix(gdb_case, cases, a.jobs)) else 1
    if a.suite == "input":
        return 0 if all(r[1] for r in matrix(input_case, [c for c in INPUT_CASES if not a.profile or c[0] in a.profile],
                                             a.jobs)) else 1
    if a.suite == "shot":
        cases = [c for c in SHOT_CASES if not a.profile or c[0] in a.profile]
        if os.environ.get("SHOT_ONLY"):         # e.g. SHOT_ONLY=SHOTTEST-bf6-vbe
            cases = [c for c in cases if "%s-%s-%s" % (c[3], c[0], c[1]) in os.environ["SHOT_ONLY"].split(",")]
        return 0 if all(r[1] for r in matrix(shot_case, cases, a.jobs)) else 1
    if a.suite == "sess":
        res = matrix(sess, [(p, b, h) for p, b in combos for h in ("base", "run", "shell")], a.jobs)
        res += matrix(sess_prof, combos, a.jobs)
        res += matrix(sess_direct, combos, a.jobs)
        res += matrix(sess_kill, combos, a.jobs)
        res += matrix(sess_agent, [c for c in SESS_AGENT if not a.profile or c[0] in a.profile], a.jobs)
        return 0 if all(r[1] for r in res) else 1
    if a.suite == "dpmi":
        res = matrix(dpmi, combos, a.jobs) + matrix(dpmiconf, combos, a.jobs) + matrix(lazy, combos, a.jobs)
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
