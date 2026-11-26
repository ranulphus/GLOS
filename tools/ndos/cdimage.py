#!/usr/bin/env python3
"""cdimage.py OUT.iso: GLOS's boot CD (docs/v1/native-dos.md, N4's C8; phase G's bootable CD).

The PC's BIOS starts the CD's El Torito image, GLOS's setup floppy with no DOS (tools/ndos/nfloppy.py) by floppy
emulation: GLOS runs from it as A: (the PC's own floppy drive, if it has one, is B: meanwhile), GLOS's own CD-ROM
driver gives the CD a letter after the hard disks', and SETUP (tools/ndos/SETUP.BAT) puts GLOS on a hard disk with
`DISK n INSTALL /FROM:X:\\GLOS`: the whole system from the CD's \\GLOS, as tools/floppy/install.py puts it on a hard
disk (GLOS.EXE and GLOS's kernel, its own COMMAND.COM, the programs, the savers, the data files, the package key,
GLOSLDR.SYS), with a GLOS.CFG naming the PC and KEYS\\ (a host key of the CD's own, which every PC installed from
it shares; AUTHKEYS: who may log in). GLOSCD.ID in the CD's root is how SETUP knows the CD from a hard disk with
a GLOS on it. ISO 9660 level 1 (tools/ndos/mkiso.py), no tool of a CD writer's.

  tools/ndos/cdimage.py build/cd/glos.iso --keys build/cd/keys [--name glos] [--quiet]

In the dev container, from the repository's root, after `make all apps native-loader build/kernel/GLOSK.PAK`
(`make cd-image` does it). Then burn it as it is (an ISO image), or attach it to a virtual PC's CD drive."""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools", "floppy"))
import install                                  # noqa: E402  (its list of the system's files)


def build_id():
    r = subprocess.run(["git", "describe", "--always", "--dirty"], cwd=ROOT, capture_output=True, text=True)
    return r.stdout.strip() or "unknown"


def main():
    ap = argparse.ArgumentParser(description="GLOS's boot CD")
    ap.add_argument("out")
    ap.add_argument("--keys", default="build/cd/keys", help="the folder with HOSTKEY and AUTHKEYS")
    ap.add_argument("--name", default="glos", help="the installed PC's name on the network (SETUP asks)")
    ap.add_argument("--quiet", action="store_true", help="no /VERBOSE: the kernel's log lines not on the screen")
    a = ap.parse_args()
    files = install.glos_files(a)
    out = os.path.abspath(a.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        boot = os.path.join(tmp, "BOOT.IMG")
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools/ndos/nfloppy.py"), boot, "--keys", a.keys,
                            "--name", "glos-setup", "--size", "1440"] + (["--quiet"] if a.quiet else []), cwd=ROOT)
        if r.returncode != 0:
            sys.exit("cdimage: the setup floppy couldn't be made")
        tree = os.path.join(tmp, "cd")
        for src, dst in files:
            d = os.path.join(tree, "GLOS", dst)
            os.makedirs(os.path.dirname(d), exist_ok=True)
            shutil.copy2(os.path.join(ROOT, src), d)
        lines = ["; GLOS installed from its boot CD (tools/ndos/cdimage.py)"]
        if not a.quiet:
            lines += ["[shell]", "options = /VERBOSE"]
        lines += ["[net]", "hostname = " + a.name, "[desktop]", "tour = on", "tips = on"]
        open(os.path.join(tree, "GLOS", "GLOS.CFG"), "w", newline="").write("\r\n".join(lines) + "\r\n")
        open(os.path.join(tree, "GLOSCD.ID"), "w", newline="").write(
            "GLOS's boot CD, build %s (tools/ndos/cdimage.py)\r\n" % build_id())
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools/ndos/mkiso.py"), out, tree, "--label", "GLOS",
                            "--boot-floppy", boot], cwd=ROOT)
        if r.returncode != 0:
            sys.exit("cdimage: the CD's image couldn't be made")
    total = sum(os.path.getsize(os.path.join(ROOT, src)) for src, _ in files)
    print("%s: %d files of GLOS (%.1f MB) in \\GLOS, the setup floppy as its El Torito image" % (a.out, len(files),
                                                                                              total / 1e6))


if __name__ == "__main__":
    main()
