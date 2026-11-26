#!/usr/bin/env python3
"""install.py HOST: GLOS put on a PC's hard disk over the network (docs/floppy.md).

The PC has been started from GLOS's setup floppy (make floppy: build/floppy/glos1440.img), so GLOS runs there
with its network, SSH and SFTP, and answers as HOST (glos-setup.local, or its address). From this computer:

  tools/floppy/install.py glos-setup.local            GLOS into C:\\GLOS of the DOS that is on the disk; what to
                                                      add to its AUTOEXEC.BAT is said, not done
  ... --autoexec                                      ... and done: C:\\AUTOEXEC.BAT starts GLOS (the file as it
                                                      was is kept as C:\\AUTOEXEC.PRE)
  ... --new-disk                                      the PC's first hard disk has nothing on it: one partition is
                                                      made, formatted, FreeDOS's kernel put on it from the floppy,
                                                      then GLOS, which is DOS's shell there (FDCONFIG.SYS's SHELL=
                                                      line: no COMMAND.COM of FreeDOS's; GLOS runs AUTOEXEC.BAT)
  ... --new-disk --erase-existing                     the same on a disk that has partitions (any system's):
                                                      EVERYTHING ON THE DISK IS LOST

What goes to C:\\GLOS: GLOS.EXE and GLOSK.BIN, GLOSLDR.SYS (the loader with no DOS, for DISK n INSTALL p's upgrade
to a GLOS that starts the PC by itself), the programs (build/apps), CERTS.DAT, the package key, the
fonts' smoothing data, a GLOS.CFG with the PC's name (--name; one that is there already is kept), and KEYS\\ (the
floppy's host key and AUTHKEYS, so the PC stays who it was and lets the same people in).

With no DOS (--native): the PC was started from tools/ndos/nfloppy.py's floppy (make native-floppy), GLOS alone
with its network. GLOS's DISK command puts GLOS on the first hard disk: its loader, kernel, COMMAND.COM, keys and
its own boot records, on a disk with nothing on it (--new-disk), one erased (--new-disk --erase-existing), or in
the disk's free space beside the systems there (--free: GLOS's start-up menu offers them). The PC is restarted,
from the floppy again, so that the new volume has its drive letter (MS-DOS's: C:, or D: beside another system's
C:), and the programs go there over SFTP.

  tools/floppy/install.py --native glos-setup.local --new-disk
  tools/floppy/install.py --native glos-setup.local --free

Nothing is partitioned or formatted without --new-disk, and never a disk whose partition table has an entry
(DOS's or any other system's: GLOS reads the table itself) without --erase-existing. The disk is laid out,
formatted and made to start by GLOS's own DISK command; FreeDOS's FDISK, FORMAT and SYS are not on the floppy. Run from the repository's root, after `make all apps floppy`.
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
APPS = ["FILES", "TERM", "NOTEPAD", "CALC", "IMAGES", "PAINT", "MONITOR", "CONTROL", "MIXER", "RECORDER", "PLAYER", "STUDIO", "PDF", "GAMES", "CHAT", "WEB", "DISKS", "HELP", "SKI", "HVCRAFT", "PKG", "FETCH"]
SAVERS = ["STARS", "PIPES", "TEXT", "OBJECTS", "MAZE"]                      # (apps/savers: NAME.SCR beside GLOS.EXE)

AUTOEXEC_LINES = [":glosrun", "C:\\GLOS\\GLOS.EXE", "IF ERRORLEVEL 200 GOTO glosrun"]
# A new disk (docs/v1/shell.md, S4d): FreeDOS's kernel with GLOS as its shell, which runs this AUTOEXEC.BAT itself.
NEW_FDCONFIG = ["LASTDRIVE=Z", "BUFFERS=20", "FILES=40", "SHELL=C:\\GLOS\\GLOS.EXE /SHELL"]
NEW_AUTOEXEC = ["@ECHO OFF", "SET PATH=C:\\GLOS;C:\\FREEDOS\\BIN"]


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

    def parts(self):
        """The first hard disk's partitions, as GLOS's DISK command lists them (GLOS reads the table itself):
        a list of (number, type, MB, fat), or None if the disk can't be read."""
        rc, out = self.ssh("glos disk 1", timeout=120)
        if rc != 0 or not re.match(r"Disk 1: \d+ MB", out) or "can't be read" in out:
            return None                         # (no first disk, or the BIOS couldn't read it)
        found = []
        for n, kind, mb in re.findall(r"^  (\d+)  .*? \[(\w\w)\] +(\d+) MB", out, re.M):
            found.append((int(n), kind, int(mb), kind.lower() in ("01", "04", "06", "0b", "0c", "0e")))
        return found

    def disk(self, args, what, timeout=3600):
        """One of DISK's writes there (with /Y: what it destroys was said and agreed to here), its words shown."""
        print("  > DISK " + args)
        rc, out = self.ssh("glos disk " + args, timeout=timeout)
        for line in out.strip().split("\n"):
            if line.strip():
                print("    " + line.rstrip())
        if rc != 0:
            die("%s failed (exit code %d)" % (what, rc))

    def has_c(self):
        """DOS reads a C: there. (Asked only of a disk whose table has a DOS partition. One that isn't formatted
        is a disk error to DOS: GLOS answers it Fail and says so in its log; a GLOS from before 2026-11-12 let
        DOS ask Abort, Retry or Fail at the PC's keyboard, which is answered Abort here.)"""
        errors = lambda: self.ssh("glos log", timeout=60)[1].count("GLOS-DOS critical")
        before = errors()
        rc, out = self.ssh("VOL C:", timeout=60)
        if rc == 255 and "no answer" in out:
            self.ssh("glos key 0x1E", timeout=30)
            self.ssh("glos key 0x1E", timeout=30)
            return False
        return "Volume in drive C" in out and errors() == before

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
    """One partition over the first hard disk, formatted and made to start (GLOS's DISK command), with FreeDOS's
    kernel copied from the floppy (and the few programs of FreeDOS's the floppy has); GLOS is its shell."""
    parts = pc.parts()
    if parts is None:
        die("the PC's first hard disk can't be read (the BIOS has none?)")
    if parts and not erase:
        die("the disk has partitions already (%s). Without --new-disk GLOS goes into C:\\GLOS of the DOS there. To "
            "destroy everything on the disk and start anew: --new-disk --erase-existing"
            % ", ".join("%d: type %s, %d MB" % p[:3] for p in parts))
    print("The first hard disk: one partition over all of it%s" % (" (EVERYTHING ON IT IS LOST)" if parts else ""))
    pc.disk("1 NEW /Y", "making the partition")
    parts = pc.parts()
    if not parts or len(parts) != 1 or not parts[0][3]:
        die("the partition wasn't made as expected: %r" % (parts,))
    print("  partition 1: type %s, %d MB" % parts[0][1:3])
    pc.disk("1 FORMAT 1 /V:GLOS /Y", "formatting the partition")
    pc.disk("1 BOOT 1 /Y", "writing the boot records")
    print("Restarting the PC, so that DOS sees the new volume (from the floppy again)")
    pc.ssh("glos reboot --force", timeout=30)
    time.sleep(20)
    if not pc.up(900):
        die("the PC didn't come back after the restart: is the floppy still in the drive, and the drive first in the "
            "BIOS's boot order?")
    if not pc.has_c():
        die("C: can't be read after the format")
    print("Putting FreeDOS's kernel on C:")
    pc.dos("COPY /Y A:\\KERNEL.SYS C:\\", "copying FreeDOS's kernel")
    pc.dos("MD C:\\FREEDOS", "making C:\\FREEDOS")
    pc.dos("MD C:\\FREEDOS\\BIN", "making C:\\FREEDOS\\BIN")
    pc.dos("COPY /Y A:\\FREEDOS\\BIN\\*.* C:\\FREEDOS\\BIN", "copying FreeDOS's programs",
           ok=(0, 1))                           # (a 720 KB floppy has none)
    pc.put_text("/C/FDCONFIG.SYS", NEW_FDCONFIG)
    pc.put_text("/C/AUTOEXEC.BAT", NEW_AUTOEXEC)


def glos_files(args):
    """(local path, name there under /C/GLOS) for the whole system; what is missing ends the run."""
    files = [("build/ow/GLOS.EXE", "GLOS.EXE"), ("build/kernel/GLOSK.BIN", "GLOSK.BIN"),
             ("build/glos/COMMAND.COM", "COMMAND.COM"),         # (GLOS's own, for programs that shell out: S4c)
             ("build/ow/GLOSLDR.SYS", "GLOSLDR.SYS")]           # (the loader with no DOS: DISK n INSTALL p's upgrade
                                                                #  of a GLOS that DOS starts takes it from here)
    files += [("build/apps/%s.EXE" % a, a + ".EXE") for a in APPS + ["SSH", "SFTP", "SSHKEY"]]
    files += [("build/apps/%s.SCR" % v, v + ".SCR") for v in SAVERS]   # (the savers' programs: phase D)
    files += [("build/glos/CERTS.DAT", "CERTS.DAT"), ("sdk/gui/fonts/SMOOTH.DAT", "SMOOTH.DAT"),
              ("sdk/gui/fonts/FONTS.DAT", "FONTS.DAT"),         # (the UI's small and large fonts: toolkit 2)
              ("sdk/gui/fonts/ICONS.DAT", "ICONS.DAT"),         # (GLOS's colour icons, by name)
              ("build/apps/PDFFONTS.DAT", "PDFFONTS.DAT"),      # (the PDF Viewer's stand-in fonts: phase F)
              ("build/apps/HELP.DAT", "HELP.DAT"),              # (the user guide's pages: Help)
              ("build/apps/PCIIDS.DAT", "PCIIDS.DAT"),          # (the Control Panel's names for PCI devices: phase F)
              ("build/apps/USBIDS.DAT", "USBIDS.DAT"),          # (... and for USB devices)
              ("build/glos/KEYS/PKG/GLOS.PUB", "KEYS/PKG/GLOS.PUB"),
              (os.path.join(args.keys, "HOSTKEY"), "KEYS/HOSTKEY"), (os.path.join(args.keys, "AUTHKEYS"), "KEYS/AUTHKEYS")]
    missing = [src for src, _ in files if not os.path.exists(os.path.join(ROOT, src))]
    if missing:
        die("not built: %s (make all apps build/apps/FETCH.EXE build/apps/PKG.EXE floppy)" % ", ".join(missing))
    return files


def put_glos(pc, args, files, drive="C", new_cfg=False):
    total = sum(os.path.getsize(os.path.join(ROOT, src)) for src, _ in files)
    print("GLOS into %s:\\GLOS: %d files, %.1f MB" % (drive, len(files), total / 1e6))
    pc.sftp(["-mkdir /%s/GLOS" % drive, "-mkdir /%s/GLOS/KEYS" % drive, "-mkdir /%s/GLOS/KEYS/PKG" % drive])
    t0 = time.time()
    rc, out = pc.sftp(["put %s /%s/GLOS/%s" % (os.path.join(ROOT, src), drive, dst) for src, dst in files])
    if rc != 0:
        die("copying failed: %s" % out.strip()[-300:])
    rc, out = pc.sftp(["ls -l /%s/GLOS" % drive, "ls -l /%s/GLOS/KEYS" % drive, "ls -l /%s/GLOS/KEYS/PKG" % drive])
    sizes = {}
    for line in out.split("\n"):
        w = line.split()
        if len(w) >= 9 and w[4].isdigit():
            sizes[w[-1].split("/")[-1].upper()] = int(w[4])
    bad = [dst for src, dst in files if sizes.get(dst.split("/")[-1].upper()) != os.path.getsize(os.path.join(ROOT, src))]
    if bad:
        die("these aren't there whole: %s" % ", ".join(bad))
    print("  copied and checked in %d seconds" % (time.time() - t0))
    if new_cfg or pc.get_text("/%s/GLOS/GLOS.CFG" % drive) is None:
        pc.put_text("/%s/GLOS/GLOS.CFG" % drive, ["[net]", "hostname = " + args.name]
                    + (["cable = " + args.cable] if args.cable else [])
                    + ["[desktop]", "tour = on", "tips = on"]    # (Help's tour the first time the desktop comes on)
                    + (["[sound]", "blaster = " + args.old_blaster] if getattr(args, "old_blaster", None) else []))
        print("  %s:\\GLOS\\GLOS.CFG: the PC's name is %s (%s.local)" % (drive, args.name, args.name))
    else:
        print("  %s:\\GLOS\\GLOS.CFG is there already: kept" % drive)


BLASTER_SET = re.compile(r"(?i)^\s*@?\s*SET\s+BLASTER\s*=\s*(\S.*?)\s*$")


def blaster_of(lines):
    """The value the last SET BLASTER= of a batch file's lines gives, or None."""
    found = None
    for line in lines or []:
        m = BLASTER_SET.match(line)
        if m:
            found = m.group(1)
    return found


def old_blaster(pc):
    """The Sound Blaster's resources as a DOS already on the PC's disks has them (AUTOEXEC.BAT's SET BLASTER, or
    FreeDOS's FDAUTO.BAT's), read before DISK changes anything: GLOS.CFG's [sound] blaster for the native GLOS,
    which has no DOS to set it (a card jumpered away from 220h, or an ESS AudioDrive). (drive, file, value) or
    None. One SFTP batch; a drive or file that isn't there is passed over."""
    tmp = os.path.join(pc.tmp, "old")
    os.makedirs(tmp, exist_ok=True)
    names = [(d, n) for d in "CDEFGHIJ" for n in ("AUTOEXEC.BAT", "FDAUTO.BAT")]
    for d, n in names:
        if os.path.exists(os.path.join(tmp, d + n)):
            os.unlink(os.path.join(tmp, d + n))
    pc.sftp(["-get /%s/%s %s" % (d, n, os.path.join(tmp, d + n)) for d, n in names], timeout=600)
    for d, n in names:
        f = os.path.join(tmp, d + n)
        if os.path.exists(f):
            b = blaster_of(open(f, "rb").read().decode("latin-1").replace("\r", "").split("\n"))
            if b:
                return d, n, b
    return None


def native_install(pc, args, files):
    """--native: GLOS put on the first hard disk by its own DISK command, the PC restarted (from the floppy again)
    so that the new volume has its letter, and the programs copied there. The drive GLOS went to: its letter."""
    parts = pc.parts()
    if parts is None:
        die("the PC's first hard disk can't be read (glos disk 1)")
    old = old_blaster(pc) if parts else None
    if old:
        args.old_blaster = old[2]
        print("  %s:\\%s sets BLASTER=%s: GLOS.CFG's [sound] blaster" % old)
    if args.free:
        how, what = "1 install /free /y", "GLOS put in the disk's free space"
    elif not parts:
        if not args.new_disk:
            die("the first hard disk has nothing on it: --new-disk puts GLOS on it")
        how, what = "1 install /y", "GLOS put on the new disk"
    else:
        if not (args.new_disk and args.erase_existing):
            die("the first hard disk has partitions (%s): --free puts GLOS beside them; --new-disk --erase-existing "
                "erases them" % ", ".join("type %s, %d MB" % p[1:3] for p in parts))
        how, what = "1 install /erase /y", "GLOS put on the erased disk"
    pc.disk(how, what)
    print("Restarting the PC from the floppy again, so that GLOS has the new volume as a drive")
    pc.ssh("glos reboot --force", timeout=30)
    time.sleep(15)
    ver = pc.up(900)
    if not ver:
        die("GLOS didn't come back after the restart (is the floppy still in, and the first to start?)")
    for drive in "CDEFGHIJ":                    # (the first volume with GLOS's kernel that isn't the floppy)
        rc, out = pc.sftp(["ls -l /%s/GLOS/GLOSK.BIN" % drive], timeout=120)
        if rc == 0 and "GLOSK.BIN" in out.upper():
            print("  GLOS's volume is %s:" % drive)
            return drive
    die("GLOS's new volume has no drive after the restart (glos disk 1, and the log, say why)")


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
    ap.add_argument("--cable", metavar="SETTING", help="the installed system's cable link, as GLOS.CFG's [net] cable "
                                                       "has it: \"client COM1\" (docs/cable.md)")
    ap.add_argument("--new-disk", action="store_true", help="partition and format the first hard disk first")
    ap.add_argument("--erase-existing", action="store_true", help="with --new-disk: even if the disk has a C: now")
    ap.add_argument("--autoexec", action="store_true", help="make C:\\AUTOEXEC.BAT of the DOS there start GLOS")
    ap.add_argument("--reboot", action="store_true", help="restart the PC when done")
    ap.add_argument("--native", action="store_true", help="the PC runs GLOS from nfloppy.py's floppy, no DOS: "
                                                          "GLOS's DISK puts GLOS on the disk (see above)")
    ap.add_argument("--free", action="store_true", help="with --native: in the disk's free space, beside the "
                                                        "systems there")
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
    if args.native:
        drive = native_install(pc, args, files)
        put_glos(pc, args, files, drive, new_cfg=True)
        print("Done. Take the floppy out, then restart the PC (ssh ... glos reboot, or its reset button). It starts "
              "GLOS from %s: and answers as %s.local, to the same keys." % (drive, args.name))
        return
    if args.new_disk:
        new_disk(pc, args.erase_existing)
    else:
        parts = pc.parts()
        if not parts or not any(p[3] for p in parts) or not pc.has_c():
            die("the PC has no C: that DOS can read%s. If its disk is new, or may be erased: --new-disk"
                % ("" if not parts else " (its disk has: %s)" % ", ".join("type %s, %d MB" % p[1:3] for p in parts)))
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
