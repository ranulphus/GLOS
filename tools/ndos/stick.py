#!/usr/bin/env python3
"""stick.py OUT.img: a USB stick's image that starts GLOS with no DOS (docs/v1/native-dos.md, the BIOS's disks).

The whole system as tools/floppy/install.py puts it on a hard disk (GLOS's kernel, its own COMMAND.COM, the
programs, the savers, the data files, the package key), GLOSLDR.SYS in the root, a GLOS.CFG with the PC's name,
and KEYS\\ (a host key of the stick's own; AUTHKEYS: who may log in). One FAT volume in a disk of 255 heads by 63,
as a BIOS shows a USB stick, with GLOS's MBR and a disk signature of its own.

GLOS reads such a disk by the BIOS's calls (it has no USB storage driver yet) and leaves the PC's USB controllers
to the BIOS while it does, so its own USB keyboards, mice and pads are off; a USB keyboard works as far as the
BIOS makes it a PS/2 one. The PC's BIOS must be able to start from a USB disk.

  tools/ndos/stick.py build/usb/glos-usb.img --keys build/floppy/keys --name glos-usb [--mb 128] [--quiet]
  sudo dd if=build/usb/glos-usb.img of=/dev/sdX bs=1M conv=fsync      (EVERYTHING ON THE STICK IS LOST)

In the dev container, from the repository's root, after `make all apps native-loader` (make usb-stick does it)."""
import argparse
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools", "floppy"))
import install                                  # noqa: E402  (its list of the system's files)


def main():
    ap = argparse.ArgumentParser(description="a USB stick's image that starts GLOS with no DOS")
    ap.add_argument("out")
    ap.add_argument("--keys", default="build/floppy/keys", help="the folder with HOSTKEY and AUTHKEYS")
    ap.add_argument("--name", default="glos-usb", help="the PC's name on the network (NAME.local)")
    ap.add_argument("--mb", type=int, default=128, help="the disk's size (the stick may be larger)")
    ap.add_argument("--quiet", action="store_true", help="no /VERBOSE: the kernel's log lines not on the screen")
    ap.add_argument("--flags", default="", help="the loader's command line (GLOS.EXE's flags)")
    a = ap.parse_args()
    files = install.glos_files(a)
    files = [(src, dst) for src, dst in files if dst != "GLOS.EXE"]     # (GLOSLDR.SYS is its loader here)
    with tempfile.TemporaryDirectory() as tmp:
        cfg = os.path.join(tmp, "GLOS.CFG")
        lines = ["; GLOS on a USB stick (tools/ndos/stick.py)"]
        if not a.quiet:
            lines += ["[shell]", "options = /VERBOSE"]
        lines += ["[net]", "hostname = " + a.name, "[desktop]", "tour = on", "tips = on"]
        open(cfg, "w", newline="").write("\r\n".join(lines) + "\r\n")
        args = ["%s=\\GLOS\\%s" % (os.path.join(ROOT, src), dst.replace("/", "\\")) for src, dst in files]
        args.append(cfg + "=\\GLOS\\GLOS.CFG")
        env = dict(os.environ, GLOS_DISK_MB=str(a.mb), GLOS_HEADS="255", GLOS_LABEL="GLOS", GLOS_CMDLINE=a.flags)
        os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
        r = subprocess.run(["sh", os.path.join(ROOT, "tools/ndos/native-image.sh"), a.out] + args, env=env)
        if r.returncode != 0:
            sys.exit("stick: the image couldn't be made")
    with open(a.out, "r+b") as f:               # (the MBR's disk signature: each stick its own)
        f.seek(440)
        f.write(os.urandom(4))
    total = sum(os.path.getsize(os.path.join(ROOT, src)) for src, _ in files)
    print("%s: %d MB, %d files of GLOS (%.1f MB), the PC's name %s" % (a.out, a.mb, len(files), total / 1e6, a.name))
    print("then: sudo dd if=%s of=/dev/sdX bs=1M conv=fsync   (everything on the stick is lost)" % a.out)


if __name__ == "__main__":
    main()
