#!/usr/bin/env python3
"""nfloppy.py OUT.img: GLOS's setup floppy with no DOS on it (docs/floppy.md, "With no DOS"; docs/v1/native-dos.md,
N5). A FAT12 floppy of GLOS's own formatter whose boot sector loads GLOSLDR.SYS; in A:\\GLOS the kernel (packed),
GLOS's own COMMAND.COM, a GLOS.CFG that names the PC, and KEYS\\ (a host key of the floppy's own, kept from one
image to the next; AUTHKEYS: who may log in). Started from it, a PC runs GLOS with its network and SSH, and
`tools/floppy/install.py --native` puts GLOS on its hard disk from another computer: GLOS's DISK command lays
out and formats the disk and copies GLOS there, then the programs go over SFTP.

  tools/ndos/nfloppy.py build/floppy/nglos1440.img --keys build/floppy/keys [--name glos-setup] [--size 1440]

In the dev container, from the repository's root, after `make all native-loader build/kernel/GLOSK.PAK`
(`make native-floppy` does it). Sizes: 1440 (3.5", the default), 720, 1200 (5.25")."""
import argparse
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHAPES = {1440: (2880, 2), 720: (1440, 2), 1200: (2400, 2)}     # (sectors, heads: fatfmt.c knows each shape)


def main():
    ap = argparse.ArgumentParser(description="GLOS's setup floppy with no DOS on it")
    ap.add_argument("out")
    ap.add_argument("--keys", default="build/floppy/keys", help="the folder with HOSTKEY and AUTHKEYS")
    ap.add_argument("--hostkey", help="the host key itself (default: KEYS/HOSTKEY)")
    ap.add_argument("--authkeys", help="who may log in (default: KEYS/AUTHKEYS)")
    ap.add_argument("--name", default="glos-setup", help="the PC's name on the network while it runs the floppy")
    ap.add_argument("--size", type=int, default=1440, choices=sorted(SHAPES), help="the floppy's size in KB")
    ap.add_argument("--quiet", action="store_true", help="no /VERBOSE: the kernel's log lines not on the screen")
    a = ap.parse_args()
    hostkey = a.hostkey or os.path.join(a.keys, "HOSTKEY")
    authkeys = a.authkeys or os.path.join(a.keys, "AUTHKEYS")
    files = [("build/ow/GLOSLDR.SYS", "GLOSLDR.SYS"), ("build/kernel/GLOSK.PAK", "GLOS/GLOSK.BIN"),
             ("build/glos/COMMAND.COM", "GLOS/COMMAND.COM"), ("build/glos/KEYS/PKG/GLOS.PUB", "GLOS/KEYS/PKG/GLOS.PUB"),
             (hostkey, "GLOS/KEYS/HOSTKEY"), (authkeys, "GLOS/KEYS/AUTHKEYS")]
    missing = [src for src, _ in files if not os.path.exists(os.path.join(ROOT, src))]
    if missing:
        sys.exit("nfloppy: not built: %s (make all native-loader build/kernel/GLOSK.PAK)" % ", ".join(missing))
    sectors, heads = SHAPES[a.size]
    out = os.path.abspath(a.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        cfg = os.path.join(tmp, "GLOS.CFG")
        lines = ["; GLOS's setup floppy with no DOS on it (tools/ndos/nfloppy.py)"]
        if not a.quiet:
            lines += ["[shell]", "options = /VERBOSE"]
        lines += ["[net]", "hostname = " + a.name]
        open(cfg, "w", newline="").write("\r\n".join(lines) + "\r\n")
        bat = os.path.join(tmp, "AUTOEXEC.BAT")
        open(bat, "w", newline="").write("\r\n".join([
            "@ECHO OFF", "ECHO.", "ECHO This PC runs GLOS from its setup floppy, with no DOS. From another computer,",
            "ECHO in GLOS's checkout:", "ECHO.", "ECHO   tools/floppy/install.py --native %s.local --new-disk" % a.name,
            "ECHO.", "ECHO (or --free, beside another system on the disk). Then take the floppy out and",
            "ECHO restart the PC.", "ECHO."]) + "\r\n")
        mkvol = os.path.join(tmp, "mkvol")
        steps = [
            ["gcc", "-m32", "-O1", "-Ikernel/include", "-Iinclude", "-o", mkvol, "tools/ndos/mkvol.c",
             "kernel/disk/fatfmt.c", "kernel/disk/mbr.c", "kernel/disk/mbrboot.S", "kernel/disk/fdboot.S",
             "sdk/config/cp.c", "sdk/config/cp_tables.c"],
            ["rm", "-f", out], ["truncate", "-s", str(sectors * 512), out],
            [mkvol, out, "0", str(sectors), "GLOSSETUP", str(heads), "0"],
            ["mmd", "-i", out, "::GLOS", "::GLOS/KEYS", "::GLOS/KEYS/PKG"],
            ["mcopy", "-i", out, cfg, "::GLOS/GLOS.CFG"], ["mcopy", "-i", out, bat, "::AUTOEXEC.BAT"],
        ] + [["mcopy", "-i", out, os.path.join(ROOT, src), "::" + dst] for src, dst in files]
        env = dict(os.environ, MTOOLS_SKIP_CHECK="1")
        for s in steps:
            r = subprocess.run(s, cwd=ROOT, env=env, capture_output=True)
            if r.returncode != 0:
                sys.exit("nfloppy: %s: %s" % (" ".join(s[:3]), (r.stdout + r.stderr).decode("latin-1")[-300:]))
    with open(out, "r+b") as f:                 # (the volume's serial number: each floppy its own; DISK knows the
        f.seek(39)                              # disk GLOS runs from by it)
        f.write(os.urandom(4))
    used = sum(os.path.getsize(os.path.join(ROOT, src)) for src, _ in files)
    print("%s: %d KB, GLOS's %.0f KB of it, the PC's name %s" % (a.out, a.size, used / 1024, a.name))
    print("write it to a floppy as it is (dd if=%s of=/dev/sdX with a USB floppy drive)" % a.out)


if __name__ == "__main__":
    main()
