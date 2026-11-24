#!/usr/bin/env python3
"""smoke.py: GLOS's smoke tier (the owner's tiers of 2026-11-06: host tests, a short Loop A smoke, the gate).

One boot per profile, the profiles side by side, each running a list of steps over SSH in that same boot: the
in-guest programs the suites already have, with their suites' pass rules. About ten minutes in all, to run after
every change; the merge gate stays what it is, for what one boot can't hold (a kernel made to panic or killed,
other boot configurations, two machines, long GUI scripts, timing that wants a quiet host).

    tests/loopa/smoke.py [--only NAME,...] [--steps STEP,...] [-j N] [--list]          (make loopa-smoke)

A line a step: "smoke-PROFILE STEP PASS|FAIL|SKIP (Ns) [what]". A step that leaves the PC dead (no answer: a
panic, a hang) is failed, the profile's emulator started again, and the next step run in the new boot. Every
profile fails if its serial log has GLOS-PANIC, a GLOS-WARN of the frame allocator (pmm-*), of the kernel's
linear space (mm-kernel-lin) or vif-stuck: the faults that make no other noise. Emulators are taken from
LOOPA_MAX_VMS's slots as every run's are (run.py), and nothing starts while a merge gate runs on this machine
(SMOKE_FORCE=1 starts anyway).

A profile runs in a process of its own (--one NAME), so that native ones (LOOPA_NATIVE=1: jobs.py reads it when
it is imported) and hosted ones go side by side."""
import argparse
import concurrent.futures
import os
import random
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

# name: (machine, network card, display card, desktop bits or 0, native, sound, GLOS.EXE's flags)
PROFILES = {
    "bf6-g400": ("bf6", "ne2kpci", "g400", 32, False, None, ""),
    "bf6-vbe8": ("bf6", "ne2kpci", "vbe", 8, False, None, ""),
    "486-vbe8": ("486dx2", "ne2k", "vbe", 8, False, None, ""),
    "native-bf6": ("bf6", "ne2kpci", "vbe", 0, True, None, ""),
    "native-486": ("486dx2", "ne2k", "vbe", 0, True, None, ""),
    "es1371-share": ("bf6", "ne2kpci", "g200", 16, False, "es1371", " /SHAREIRQ"),
}
GUI_SCRIPT = "calc"                             # (a short one: the Calculator opened, sums typed, shot, closed)
FLOPPY_ROOM = 607232                            # (GLOSK.PAK on the 720 KB setup floppy: loopa-floppy's room)
# APITEST's cases that fail natively for a reason known and owed, shown on the step's line (never hidden): a case
# that starts to pass is said too, so the entry goes.
APITEST_KNOWN_NATIVE = {}               # (fs-watch, until 2026-11-24's kernel/dos/watch.c: long-name calls' paths)
SILENT_FAULTS = re.compile(r"GLOS-PANIC|GLOS-WARN pmm-|GLOS-WARN mm-kernel-lin|vif-stuck")


def gate_running():
    """A merge gate whose emulators run on this machine: the lanes (merge-gate-lanes.sh), the far end of a remote
    one (merge-gate-there.sh), or merge-gate.sh run here. merge-gate.sh remote's own process here only waits for
    the other machine, and doesn't count."""
    p = subprocess.run(["ps", "-eo", "args"], capture_output=True, text=True)
    for line in p.stdout.split("\n"):
        if re.search(r"merge-gate-(lanes|there)\.sh", line):
            return True
        if re.search(r"merge-gate\.sh(\s|$)", line) and not re.search(r"merge-gate\.sh\s+remote", line):
            return True
    return False


# ---- one profile (the child process)

class Profile:
    def __init__(self, name):
        import jobs                             # (here: LOOPA_NATIVE is set for a native one before this)
        self.jobs = jobs
        self.name = name
        (self.machine, self.net, self.vcard, self.bits, self.native, self.sound, self.flags) = PROFILES[name]
        self.out = os.path.join(ROOT, "out", "smoke-" + name)
        os.makedirs(self.out, exist_ok=True)
        self.boots, self.agent, self.logs = 0, None, []

    def files(self):
        f = ["build/ow/dos/ECHOARGS.EXE", "build/ow/dos/SESSTEST.EXE", "build/ow/dos/TIMECHK.EXE",
             "build/dj/NETTEST.EXE", "build/dj/APITEST.EXE"]
        if self.native:
            f += ["build/ow/dos/NDOSTEST.EXE", "build/ow/dos/DOSDIFF.EXE", "build/ow/dos/CONTEST.EXE"]
        if self.bits:
            f += ["build/apps/CALC.EXE", "sdk/gui/fonts/SMOOTH.DAT", "sdk/gui/fonts/FONTS.DAT",
                  "sdk/gui/fonts/ICONS.DAT"]
        if self.sound:
            f += ["build/dj/AUDTEST.EXE", "build/apps/RECORDER.EXE"]
        args = []
        for x in f:
            args += ["--file", "%s=/TEST/%s" % (x, os.path.basename(x))]
        return args

    def boot(self):
        self.boots += 1
        args = ["--card", self.vcard] + self.files()
        if self.sound:
            args += ["--sound", self.sound, "--wav"]
        args += ["--cmd", "SERSAY HX-START smoke", "--cmd", "VECCHK save", "--cmd", "C:\\TEST\\GLOS.EXE" + self.flags,
                 "--cmd", "VECCHK check", "--cmd", "SERSAY HX-DONE 0"]
        self.agent = self.jobs.Agent("smoke-%s-%d" % (self.name, self.boots), self.machine, self.net, args,
                                     timeout=2400, idle=900)
        self.agent.opts += ["-o", "ControlMaster=auto", "-o", "ControlPath=" + os.path.join(self.agent.tmp, "cm"),
                            "-o", "ControlPersist=600"]
        self.logs.append(self.agent)
        return self.agent.listening(limit=420)

    def alive(self):
        if self.agent.proc.poll() is not None:
            return False
        for _ in range(2):
            try:
                if self.agent.ssh("glos ver", timeout=60)[0] == 0:
                    return True
            except subprocess.TimeoutExpired:
                pass
        return False

    def ssh(self, cmd, timeout=300):
        return self.agent.ssh(cmd, timeout=timeout)

    def sftp(self, lines, timeout=600):
        batch = os.path.join(self.out, "sftp.batch")
        open(batch, "w").write("\n".join(lines) + "\n")
        p = subprocess.run(["sftp", "-P", str(self.agent.port)] + self.agent.base[3:]
                           + ["-i", self.agent.key, "-b", batch, "glos@127.0.0.1"], capture_output=True, timeout=timeout)
        return p.returncode, (p.stdout + p.stderr).decode("latin-1")

    # ---- the steps: each (ok, what) or None for a skip

    def s_sanity(self):
        rc1, ver, _ = self.ssh("glos ver")
        rc2, ps, _ = self.ssh("glos ps")
        rc3, mem, _ = self.ssh("glos mem")
        ok = rc1 == 0 and "GLOS" in ver and rc2 == 0 and re.search(r"^thread vm ", ps, re.M) is not None \
            and rc3 == 0 and mem.strip() != ""
        return ok, "" if ok else "ver=%r ps=%r mem=%r" % (ver[:60], ps[:80], mem[:80])

    def s_echoargs(self):
        rc, o, e = self.ssh("C:\\TEST\\ECHOARGS.EXE x y")
        ok = rc == 7 and o == "x y\r\nnine two six int29\r\n"
        return ok, "" if ok else "rc=%d %r" % (rc, (o + e)[-120:])

    def s_session(self):
        """SESSTEST between VECCHK's save and check (sess_agent's own steps): the PIC, the mode, the vectors as
        they were."""
        rcs =[self.ssh(c)[0] for c in self.jobs.SESS_STEPS]
        text = self.agent.log()
        ok = rcs == [0] * len(self.jobs.SESS_STEPS) and text.count("HX-VECCHK ok") >= 1
        return ok, "" if ok else "rcs=%r" % rcs

    def s_net(self):
        """NETTEST against an echo server and a closed port on the host (netapp's), then its datagrams."""
        echo = self.jobs.EchoServer()
        closed = self.jobs.free_port()
        rc, o, e = self.ssh("C:\\TEST\\NETTEST.EXE all 10.0.2.2 %d %d" % (echo.port, closed), timeout=900)
        bad = [m.group(1) for m in re.finditer(r"^NETTEST (\S+) FAIL", o.replace("\r", ""), re.M)]
        ok = rc == 0 and "NETTEST-DONE failures=0" in o and not bad
        udp = self.jobs.UdpEcho()
        rc2, o2, _ = self.ssh("C:\\TEST\\NETTEST.EXE udp 10.0.2.2 %d" % udp.port, timeout=300)
        bad += [m.group(1) for m in re.finditer(r"^NETTEST (\S+) FAIL", o2.replace("\r", ""), re.M)]
        ok = ok and rc2 == 0 and "NETTEST-UDP-DONE failures=0" in o2 and not bad
        return ok, "" if ok else "rc=%d,%d failed=%s out=%r" % (rc, rc2, ",".join(bad) or "?", (o + o2)[-300:])

    def s_files(self):
        """A file of 1 MB by SFTP: written, renamed, read back the same, deleted; the folder empty after."""
        data = random.Random(7).randbytes(1 << 20)
        src, back = os.path.join(self.out, "put.bin"), os.path.join(self.out, "got.bin")
        open(src, "wb").write(data)
        if os.path.exists(back):
            os.remove(back)
        rc, out = self.sftp(["-mkdir /C/SMOKE", "put %s /C/SMOKE/A.BIN" % src, "rename /C/SMOKE/A.BIN /C/SMOKE/B.BIN",
                             "get /C/SMOKE/B.BIN %s" % back, "rm /C/SMOKE/B.BIN"])
        same = os.path.exists(back) and open(back, "rb").read() == data
        rc2, ls, _ = self.ssh("dir C:\\SMOKE")
        empty = re.search(r"^\s*0 file\(s\)", ls.replace("\r", ""), re.M) is not None    # (an empty folder's DIR: exit code 1)
        self.ssh("rd C:\\SMOKE")
        ok = rc == 0 and same and empty
        return ok, "" if ok else "rc=%d same=%s empty=%s dir=%r" % (rc, same, empty, ls[-300:])

    def s_attrib(self):
        """ATTRIB +R, a write refused, -R, a write allowed (the shell's, phase H)."""
        rc, o, e = self.ssh("echo one> C:\\SMOKE.TXT")
        rc, o, e = self.ssh("attrib +r C:\\SMOKE.TXT")
        if rc != 0 and re.search(r"(?i)bad command|not found|unknown", o + e):
            self.ssh("del C:\\SMOKE.TXT")
            return None                         # (no ATTRIB in this GLOS yet)
        refused = self.ssh("echo two> C:\\SMOKE.TXT")[0] != 0
        rc2 = self.ssh("attrib -r C:\\SMOKE.TXT")[0]
        allowed = self.ssh("echo three> C:\\SMOKE.TXT")[0] == 0 and "three" in self.ssh("type C:\\SMOKE.TXT")[1]
        self.ssh("del C:\\SMOKE.TXT")
        ok = rc == 0 and refused and rc2 == 0 and allowed
        return ok, "" if ok else "set=%d refused=%s cleared=%d allowed=%s" % (rc, refused, rc2, allowed)

    def s_apitest(self):
        """API 2.0's conformance (api's APITEST), its cases as an app started over SSH."""
        mark = len(self.agent.log())
        rc, o, e = self.ssh("C:\\TEST\\APITEST.EXE", timeout=900)
        serial = self.agent.log()[mark:]
        cases = re.findall(r"HX-APITEST case=(\S+) ok=(\d)", serial)
        done = re.search(r"HX-APITEST done pass=(\d+) fail=(\d+)", serial)
        bad = [c for c, k in cases if k != "1"]
        known = APITEST_KNOWN_NATIVE if self.native else {}
        unexpected = [c for c in bad if c not in known]
        fixed = [c for c in known if c not in bad and any(c == x for x, _ in cases)]
        ok = bool(done) and len(cases) >= 100 and not unexpected and int(done.group(2)) == len(bad)
        what = "cases=%d" % len(cases) + "".join(" known:%s" % c for c in bad if c in known) \
            + "".join(" now-passes:%s" % c for c in fixed)
        return ok, what + ("" if ok else " failed=%s rc=%d" % (",".join(unexpected[:8]) or "?", rc))

    def s_display(self):
        """The desktop on at the profile's depth, a shot of that size, off again."""
        if not self.bits:
            return None
        rc = self.ssh("glos display on 640x480x%d" % self.bits)[0]
        line, t0 = "", time.time()
        while time.time() - t0 < 60 and not line.startswith("display on mode="):
            line = self.ssh("glos display")[1]
            time.sleep(0.5)
        p = subprocess.run(self.agent.opts + ["glos@127.0.0.1", "glos shot"], capture_output=True, timeout=300)
        pic = self.jobs.png_rgb(p.stdout) if p.returncode == 0 else None
        ok = rc == 0 and (" 640x480x%d " % self.bits) in line and bool(pic) and pic[:2] == (640, 480)
        self.dac6 = " dac=6" in line
        return ok, "" if ok else "rc=%d line=%r shot=%s" % (rc, line[:80], pic[:2] if pic else None)

    def s_gui(self):
        """One short GUI script (tests/gui), its shots against the host's goldens (make gui-goldens)."""
        if not self.bits or self.sound:
            return None
        g = self.jobs.GuiDriver(self.agent, self.out, self.bits, getattr(self, "dac6", False), "C:\\TEST\\")
        g.play(os.path.join("tests", "gui", GUI_SCRIPT + ".txt"))
        open(os.path.join(self.out, GUI_SCRIPT + ".out"), "w").write(g.finish())
        ok = not g.bad
        return ok, "" if ok else " ".join(g.bad + g.notes)[:300]

    def s_dosdiff(self):
        """GLOS's own DOS against MS-DOS 7.1's answers (tests/dos/dosdiff.msdos71.txt), every line."""
        if not self.native:
            return None
        rc, o, e = self.ssh("C:\\TEST\\DOSDIFF.EXE", timeout=300)
        diffs = self.jobs.dosdiff_compare(o)
        ok = rc == 0 and "DOSDIFF end" in o and not diffs
        return ok, "" if ok else "rc=%d diffs=%d %r" % (rc, len(diffs), diffs[:3])

    def s_ndostest(self):
        """NDOSTEST (Open Watcom's C library on GLOS's DOS: files, folders, searches, memory, a spawn)."""
        if not self.native:
            return None
        t0 = time.time()                        # (it checks the date: the clock set by SNTP first, as nboot2 waits)
        while time.time() - t0 < 120 and "GLOS-TIME set by=sntp" not in self.agent.log():
            time.sleep(0.5)
        rc, o, e = self.ssh("C:\\TEST\\NDOSTEST.EXE x two", timeout=600)  # (each SSH command a shell of its own:
                                                # a CD before it is gone)
        ok = rc == 0 and "NDOSTEST: 0 failures" in o and "child: 7 word" in o
        return ok, "" if ok else "rc=%d %r" % (rc, (o + e)[-200:])

    def s_audio(self):
        """The ES1371 on the network card's line (/SHAREIRQ: the D2 chain, the quiet drivers), a tone through the
        mixer, the Sound Recorder (86Box's ES1371 records nothing: it says so), and no watchdog's kick."""
        if not self.sound:
            return None
        lg = self.agent.log()
        m = re.search(r"GLOS-PCI shareirq irq=(\d+) was=\d+", lg)
        share = m is not None and "GLOS-DEV line irq=%s drivers=2" % m.group(1) in lg
        t0 = time.time()
        while time.time() - t0 < 60 and not self.ssh("glos audio")[1].startswith("audio on "):
            time.sleep(0.5)
        on = self.ssh("glos audio")[1].startswith("audio on rate=22050 ")
        rc, o, e = self.ssh("C:\\TEST\\AUDTEST.EXE tone A 440 6", timeout=300)
        m2 = re.search(r"^AUD-OK A periods=(\d+) underruns=(\d+)", o.replace("\r", ""), re.M)
        tone = rc == 41 and bool(m2) and int(m2.group(1)) >= 100 and m2.group(2) == "0"
        g = self.jobs.GuiDriver(self.agent, self.out, 16, False, "C:\\TEST\\")
        rec = subprocess.Popen(self.agent.opts + ["glos@127.0.0.1", "C:\\TEST\\RECORDER.EXE"], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
        win = bool(g.until(lambda d: g.window("New take - Sound Recorder", d) is not None, 120))
        keys = lambda *k: (self.ssh("glos key " + self.jobs.gui_key_list(list(k))), time.sleep(1.5))
        time.sleep(2)
        keys("ctrl+r")
        time.sleep(20)
        keys("f6")
        keys("alt+f4")
        try:
            o_rec = rec.communicate(timeout=60)[0].decode("latin-1")
        except subprocess.TimeoutExpired:
            rec.kill()
            o_rec = ""
        recorded = rec.returncode == 0 and "RECORDER recorded 0 at 22050 Hz" in o_rec and "RECORDER end" in o_rec
        kicks = "GLOS-AUDIO kick" in self.agent.log()
        ok = share and on and tone and win and recorded and not kicks
        return ok, "" if ok else "share=%s on=%s tone=%s window=%s recorded=%s kicks=%s" % (
            share, on, tone, win, recorded, kicks)

    STEPS = ["sanity", "echoargs", "session", "files", "attrib", "net", "apitest", "display", "gui", "dosdiff",
             "ndostest", "audio"]

    def run(self, only=None):
        lines, allok = [], True
        t0 = time.time()
        up = self.boot()
        say = lambda step, res, secs, what="": (lines.append("smoke-%s %s %s (%ds)%s" % (
            self.name, step, res, secs, (" " + what) if what else "")), print(lines[-1], flush=True))
        say("boot", "PASS" if up else "FAIL", time.time() - t0)
        if not up:
            return False
        for step in self.STEPS:
            if only and step not in only:
                continue
            t1 = time.time()
            try:
                r = getattr(self, "s_" + step)()
            except subprocess.TimeoutExpired:
                r = (False, "no answer in time")
            except Exception as x:              # (a step's own fault: said, and on to the next)
                r = (False, "%s: %s" % (type(x).__name__, x))
            if r is None:
                say(step, "SKIP", time.time() - t1)
                continue
            say(step, "PASS" if r[0] else "FAIL", time.time() - t1, r[1])
            allok &= r[0]
            if not r[0] and not self.alive():   # (the PC is gone: a new boot for the rest)
                self.agent.proc.kill()
                t1 = time.time()
                up = self.boot()
                say("reboot-after-" + step, "PASS" if up else "FAIL", time.time() - t1)
                allok = False
                if not up:
                    break
        try:
            self.ssh("glos exit", timeout=60)
        except subprocess.TimeoutExpired:
            pass
        t1 = time.time()
        while time.time() - t1 < 60 and "GLOS-EXIT code=" not in self.agent.log():
            time.sleep(0.5)
        self.agent.proc.kill()
        self.agent.proc.wait()
        faults = []
        for a in self.logs:
            faults += [l for l in a.log().split("\n") if SILENT_FAULTS.search(l)]
        say("silent-faults", "PASS" if not faults else "FAIL", 0, "; ".join(faults[:3]))
        allok &= not faults
        return allok


# ---- the parent

def main():
    ap = argparse.ArgumentParser(description="GLOS's smoke tier")
    ap.add_argument("--only", help="profiles, comma separated (default: all)")
    ap.add_argument("--steps", help="steps, comma separated (default: all)")
    ap.add_argument("-j", "--jobs", type=int, default=0, help="profiles at once (default: all)")
    ap.add_argument("--one", help=argparse.SUPPRESS)
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    os.chdir(ROOT)
    if a.list:
        for n, p in PROFILES.items():
            print("%-14s %s %s %s%s%s" % (n, p[0], p[1], p[2], " native" if p[4] else "",
                                          (" " + p[5] + p[6]) if p[5] else ""))
        print("steps: " + " ".join(Profile.STEPS))
        return 0
    if a.one:
        return 0 if Profile(a.one).run(a.steps.split(",") if a.steps else None) else 1
    names = a.only.split(",") if a.only else list(PROFILES)
    t0 = time.time()
    pak = os.path.join(ROOT, "build", "kernel", "GLOSK.PAK")
    size = os.path.getsize(pak) if os.path.exists(pak) else -1
    room_ok = 0 < size <= FLOPPY_ROOM
    print("smoke-host floppy-room %s GLOSK.PAK %d of %d" % ("PASS" if room_ok else "FAIL", size, FLOPPY_ROOM),
          flush=True)
    while gate_running() and not os.environ.get("SMOKE_FORCE"):
        print("smoke: a merge gate runs on this machine: waiting (SMOKE_FORCE=1 starts anyway)", flush=True)
        time.sleep(60)

    def one(n):
        env = dict(os.environ)
        if PROFILES[n][4]:
            env["LOOPA_NATIVE"] = "1"
        else:
            env.pop("LOOPA_NATIVE", None)
        p = subprocess.run([sys.executable, os.path.abspath(__file__), "--one", n]
                           + (["--steps", a.steps] if a.steps else []), env=env, capture_output=True, text=True)
        return n, p.returncode, p.stdout + p.stderr[-2000:]

    results = []
    with concurrent.futures.ThreadPoolExecutor(a.jobs or len(names)) as ex:
        for n, rc, out in ex.map(one, names):
            results.append((n, rc))
            print(out.rstrip(), flush=True)
    bad = [n for n, rc in results if rc != 0] + ([] if room_ok else ["host"])
    print("smoke: %s in %d min%s" % ("PASS" if not bad else "FAIL", (time.time() - t0) / 60,
                                     "" if not bad else " (" + ", ".join(bad) + ")"), flush=True)
    return 0 if not bad else 1


if __name__ == "__main__":
    sys.exit(main())
