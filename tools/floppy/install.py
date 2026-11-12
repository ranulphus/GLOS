#!/usr/bin/env python3
"""install.py HOST: GLOS put on a PC's hard disk over the network (docs/floppy.md).

The PC has been started from GLOS's setup floppy (make floppy: build/floppy/glos144.img), so GLOS runs there
with its network, SSH and SFTP, and answers as HOST (glos-setup.local, or its address). From this computer:

  tools/floppy/install.py glos-setup.local            GLOS into C:\\GLOS of the DOS that is on the disk; what to
                                                      add to its AUTOEXEC.BAT is said, not done
  ... --autoexec                                      ... and done: C:\\AUTOEXEC.BAT starts GLOS (the file as it
                                                      was is kept as C:\\AUTOEXEC.PRE)
  ... --new-disk                                      the PC's first hard disk has nothing on it: one partition is
                                                      made, formatted, FreeDOS put on it from the floppy, then
                                                      GLOS, started by its AUTOEXEC.BAT
  ... --new-disk --erase-existing                     the same on a disk that has a C: already: EVERYTHING ON THE
                                                      DISK IS LOST

What goes to C:\\GLOS: GLOS.EXE and GLOSK.BIN, the programs (build/apps), CERTS.DAT, the package key, the
fonts' smoothing data, a GLOS.CFG with the PC's name (--name; one that is there already is kept), and KEYS\\ (the
floppy's host key and AUTHKEYS, so the PC stays who it was and lets the same people in).

Nothing is partitioned or formatted without --new-disk, and never a disk with a C: on it without
--erase-existing. Run from the repository's root, after `make all apps floppy`.
"""
import argparse
import os
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
APPS = ["FILES", "TERM", "NOTEPAD", "CALC", "IMAGES", "MONITOR", "CONTROL", "PKG", "FETCH"]

AUTOEXEC_LINES = [":glosrun", "C:\\GLOS\\GLOS.EXE", "IF ERRORLEVEL 200 GOTO glosrun"]
NEW_FDCONFIG = ["LASTDRIVE=Z", "BUFFERS=20", "FILES=40", "SHELL=C:\\COMMAND.COM C:\\ /E:2048 /P"]
NEW_AUTOEXEC = ["@ECHO OFF", "SET PATH=C:\\FREEDOS\\BIN;C:\\GLOS", "SET DIRCMD=/OGN"] + AUTOEXEC_LINES


def die(msg):
    print("install: " + msg, file=sys.stderr)
    sys.exit(1)


class Pc:
    """The PC over SSH and SFTP, with the floppy's host key as the only one trusted."""

    def __init__(self, host, port, key, hostkey_pub):
        self.host, self.port = host, port
        self.tmp = tempfile.mkdtemp(prefix="glos-install-")
        known = os.path.join(self.tmp, "known")
        kind, blob = open(hostkey_pub).read().split()[:2]
        open(known, "w").write("glos-setup %s %s\n" % (kind, blob))
        self.opts = ["-o", "IdentitiesOnly=yes", "-o", "BatchMode=yes", "-o", "UserKnownHostsFile=" + known,
                     "-o", "HostKeyAlias=glos-setup", "-o", "StrictHostKeyChecking=yes", "-o", "LogLevel=ERROR",
                     "-o", "ConnectTimeout=10", "-i", key]

    def ssh(self, command, timeout=300):
        """(exit code, output) of a command there: a DOS command line, or one of GLOS's own (glos ...)."""
        try:
            p = subprocess.run(["ssh", "-p", str(self.port)] + self.opts + ["glos@" + self.host, command],
                               capture_output=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            return 255, "(no answer in %d seconds)" % timeout
        return p.returncode, (p.stdout + p.stderr).decode("latin-1").replace("\r", "")

    def sftp(self, lines, timeout=1800):
        batch = os.path.join(self.tmp, "batch")
        open(batch, "w").write("\n".join(lines) + "\n")
        p = subprocess.run(["sftp", "-P", str(self.port), "-b", batch] + self.opts + ["glos@" + self.host],
                           capture_output=True, timeout=timeout)
        return p.returncode, (p.stdout + p.stderr).decode("latin-1")

    def up(self, limit=300):
        """Waits until GLOS answers there: its version line, or None."""
        t0 = time.time()
        while time.time() - t0 < limit:
            rc, out = self.ssh("glos ver", timeout=30)
            if rc == 0 and out.strip():
                return out.strip().split("\n")[0]
            time.sleep(3)
        return None

    def dos(self, command, what, ok=(0,), timeout=600):
        """A DOS command there, its output shown; its exit code must be one of `ok`."""
        print("  > " + command)
        rc, out = self.ssh(command, timeout=timeout)
        for line in out.strip().split("\n"):
            if line.strip():
                print("    " + line.rstrip())
        if rc not in ok:
            die("%s failed (exit code %d)" % (what, rc))
        return out

    def has_c(self):
        rc, out = self.ssh("IF EXIST C:\\NUL ECHO GLOS-HAS-C")
        return "GLOS-HAS-C" in out

    def put_text(self, path, lines):
        """A text file there (DOS's line ends), by SFTP: path as /C/NAME."""
        src = os.path.join(self.tmp, "text")
        open(src, "wb").write("\r\n".join(lines).encode("ascii") + b"\r\n")
        rc, out = self.sftp(["put %s %s" % (src, path)])
        if rc != 0:
            die("couldn't write %s: %s" % (path, out.strip()[-200:]))

    def get_text(self, path):
        """A text file from there, its lines; None if there is none."""
        dst = os.path.join(self.tmp, "got")
        if os.path.exists(dst):
            os.unlink(dst)
        rc, out = self.sftp(["get %s %s" % (path, dst)])
        if rc != 0 or not os.path.exists(dst):
            return None
        return open(dst, "rb").read().decode("latin-1").replace("\r", "").rstrip("\x1a\n").split("\n")


def new_disk(pc, erase):
    """One partition over the first hard disk, formatted, with FreeDOS from the floppy."""
    if pc.has_c() and not erase:
        die("the PC has a C: already. Without --new-disk GLOS goes into C:\\GLOS there. To destroy what is on the "
            "disk and start anew: --new-disk --erase-existing")
    print("The first hard disk: one partition (everything on it is lost)")
    pc.dos("FDISK /CLEARALL 1", "clearing the partition table")
    pc.dos("FDISK /PRI:100,100 1", "making the partition")
    pc.dos("FDISK /ACTIVATE:1 1", "making it the one the PC starts from")
    pc.dos("FDISK /INFO 1", "reading the partition table")
    print("Restarting the PC, so that DOS sees the new partition (from the floppy again)")
    pc.ssh("glos reboot --force", timeout=30)
    time.sleep(20)
    if not pc.up(600):
        die("the PC didn't come back after the restart: is the floppy still in the drive, and the drive first in the "
            "BIOS's boot order?")
    print("Formatting C: and putting FreeDOS on it")
    pc.dos("FORMAT C: /Q /V:GLOS /Z:SERIOUSLY", "formatting C:", timeout=1800)
    pc.dos("SYS A: C:", "making C: startable")
    pc.dos("FDISK /MBR 1", "writing the disk's boot record")
    pc.dos("COPY /Y A:\\FREEDOS\\BIN\\COMMAND.COM C:\\COMMAND.COM", "copying COMMAND.COM")
    pc.dos("XCOPY A:\\FREEDOS\\BIN C:\\FREEDOS\\BIN\\ /Y", "copying FreeDOS's programs")
    if not pc.has_c():
        die("C: can't be read after the format")
    pc.put_text("/C/FDCONFIG.SYS", NEW_FDCONFIG)
    pc.put_text("/C/AUTOEXEC.BAT", NEW_AUTOEXEC)


def glos_files(args):
    """(local path, name there under /C/GLOS) for the whole system; what is missing ends the run."""
    files = [("build/ow/GLOS.EXE", "GLOS.EXE"), ("build/kernel/GLOSK.BIN", "GLOSK.BIN")]
    files += [("build/apps/%s.EXE" % a, a + ".EXE") for a in APPS]
    files += [("build/glos/CERTS.DAT", "CERTS.DAT"), ("sdk/gui/fonts/SMOOTH.DAT", "SMOOTH.DAT"),
              ("build/glos/KEYS/PKG/GLOS.PUB", "KEYS/PKG/GLOS.PUB"),
              (os.path.join(args.keys, "HOSTKEY"), "KEYS/HOSTKEY"), (os.path.join(args.keys, "AUTHKEYS"), "KEYS/AUTHKEYS")]
    missing = [src for src, _ in files if not os.path.exists(os.path.join(ROOT, src))]
    if missing:
        die("not built: %s (make all apps build/apps/FETCH.EXE build/apps/PKG.EXE floppy)" % ", ".join(missing))
    return files


def put_glos(pc, args, files):
    total = sum(os.path.getsize(os.path.join(ROOT, src)) for src, _ in files)
    print("GLOS into C:\\GLOS: %d files, %.1f MB" % (len(files), total / 1e6))
    pc.sftp(["-mkdir /C/GLOS", "-mkdir /C/GLOS/KEYS", "-mkdir /C/GLOS/KEYS/PKG"])
    t0 = time.time()
    rc, out = pc.sftp(["put %s /C/GLOS/%s" % (os.path.join(ROOT, src), dst) for src, dst in files])
    if rc != 0:
        die("copying failed: %s" % out.strip()[-300:])
    rc, out = pc.sftp(["ls -l /C/GLOS", "ls -l /C/GLOS/KEYS", "ls -l /C/GLOS/KEYS/PKG"])
    sizes = {}
    for line in out.split("\n"):
        w = line.split()
        if len(w) >= 9 and w[4].isdigit():
            sizes[w[-1].split("/")[-1].upper()] = int(w[4])
    bad = [dst for src, dst in files if sizes.get(dst.split("/")[-1].upper()) != os.path.getsize(os.path.join(ROOT, src))]
    if bad:
        die("these aren't there whole: %s" % ", ".join(bad))
    print("  copied and checked in %d seconds" % (time.time() - t0))
    if pc.get_text("/C/GLOS/GLOS.CFG") is None:
        pc.put_text("/C/GLOS/GLOS.CFG", ["[net]", "hostname = " + args.name])
        print("  C:\\GLOS\\GLOS.CFG: the PC's name is %s (%s.local)" % (args.name, args.name))
    else:
        print("  C:\\GLOS\\GLOS.CFG is there already: kept")


def autoexec(pc, do):
    """C:\\AUTOEXEC.BAT of a DOS that was there: GLOS started by its last lines."""
    lines = pc.get_text("/C/AUTOEXEC.BAT") or pc.get_text("/C/FDAUTO.BAT")
    if lines is not None and any("GLOS.EXE" in l.upper() for l in lines):
        print("C:\\AUTOEXEC.BAT starts GLOS already")
        return
    if not do:
        print("To have the PC start GLOS, add these as the last lines of C:\\AUTOEXEC.BAT (or run this again with "
              "--autoexec):")
        for l in AUTOEXEC_LINES:
            print("    " + l)
        return
    if lines is None:
        lines = ["@ECHO OFF"]
    elif pc.get_text("/C/AUTOEXEC.PRE") is None:
        pc.put_text("/C/AUTOEXEC.PRE", lines)
        print("C:\\AUTOEXEC.BAT as it was is kept as C:\\AUTOEXEC.PRE")
    pc.put_text("/C/AUTOEXEC.BAT", lines + AUTOEXEC_LINES)
    print("C:\\AUTOEXEC.BAT starts GLOS now")


def main():
    ap = argparse.ArgumentParser(description="GLOS put on a PC's hard disk over the network, from its setup floppy")
    ap.add_argument("host", help="the PC as the floppy's GLOS answers: glos-setup.local, or its address")
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--key", help="the private key to log in with (default: the one whose public half the floppy "
                                  "was made with: ~/.ssh/glos_bench, else ~/.ssh/id_ed25519)")
    ap.add_argument("--keys", default="build/floppy/keys", help="the floppy's keys (HOSTKEY, HOSTKEY.pub, AUTHKEYS)")
    ap.add_argument("--name", default="glos", help="the installed PC's name on the network (NAME.local)")
    ap.add_argument("--new-disk", action="store_true", help="partition and format the first hard disk first")
    ap.add_argument("--erase-existing", action="store_true", help="with --new-disk: even if the disk has a C: now")
    ap.add_argument("--autoexec", action="store_true", help="make C:\\AUTOEXEC.BAT of the DOS there start GLOS")
    ap.add_argument("--reboot", action="store_true", help="restart the PC when done")
    args = ap.parse_args()
    os.chdir(ROOT)
    key = args.key or next((k for k in (os.path.expanduser("~/.ssh/glos_bench"), os.path.expanduser("~/.ssh/id_ed25519"))
                            if os.path.exists(k)), None)
    if not key:
        die("no private key: --key FILE")
    if not os.path.exists(os.path.join(args.keys, "HOSTKEY.pub")):
        die("no %s/HOSTKEY.pub: make floppy first (the floppy's own keys)" % args.keys)
    files = glos_files(args)
    pc = Pc(args.host, args.port, key, os.path.join(args.keys, "HOSTKEY.pub"))
    ver = pc.up(60)
    if not ver:
        die("no answer from GLOS at %s port %d (is the PC started from the floppy, and on the network?)"
            % (args.host, args.port))
    print("GLOS there: " + ver)
    if args.new_disk:
        new_disk(pc, args.erase_existing)
    elif not pc.has_c():
        die("the PC has no C: that DOS can read. If its disk is new, or may be erased: --new-disk")
    put_glos(pc, args, files)
    if not args.new_disk:
        autoexec(pc, args.autoexec)
    print("Done. Take the floppy out (or have the BIOS start from the hard disk first), then restart the PC%s."
          % (": doing that now" if args.reboot else " (ssh ... glos reboot, or its reset button)"))
    print("Then it answers as %s.local, to the same keys." % args.name)
    if args.reboot:
        pc.ssh("glos reboot --force", timeout=30)


if __name__ == "__main__":
    main()
