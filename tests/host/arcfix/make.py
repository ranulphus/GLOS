#!/usr/bin/env python3
"""The LZH and ARJ archives beside this, made with the tools people made such archives with: jlha (LHA's methods
and its three levels of header) and arj 3.10, in a container thrown away after (Ubuntu 24.04's packages
jlha-utils, lhasa and arj). Each is then tested by a second program (lhasa's lha; arj itself) and listed by a
third (7-Zip, on this PC): LIST is 7-Zip's account of what is in each, which tests/host/arc_cases.py holds our
reader to. PACK.LZH and PACK.ARJ are put on the GUI scripts' drive too (tests/gui/drives/C/LZH: files-lzh.txt).
Run once; what it made is in the tree.

    tests/host/arcfix/make.py WORK      (WORK: an empty folder for the files to pack; needs docker and 7z)
"""
import calendar, os, shutil, subprocess, sys, zlib

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
import inputs

work = os.path.abspath(sys.argv[1])
src, out = os.path.join(work, "in"), os.path.join(work, "out")
for d in (src, out):
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
when = calendar.timegm(inputs.STAMP + (0, 0, 0))
names = []
for name, data in inputs.files():
    p = os.path.join(src, name)
    if data is None:
        os.makedirs(p)
    else:
        open(p, "wb").write(data)
    names.append(p)
for p in reversed(names):               # (a folder's time after its files are in it)
    os.utime(p, (when, when))

ALL = "READ.ME TEXT.TXT FAR.BIN SKEW.BIN ZEROS.BIN NOISE.BIN EMPTY DOCS EMPTYDIR"
script = r"""
set -e
export DEBIAN_FRONTEND=noninteractive TZ=UTC
apt-get update -q > /dev/null
apt-get install -y -q arj lhasa jlha-utils > /dev/null 2>&1
cd /w/in
# LHA: header level 0, 1 or 2 (the digit) and method -lh5-, -lh6- or -lh7- (o5, o6, o7)
jlha a0o5 /w/out/L0.LZH %(all)s > /dev/null
jlha a1o6 /w/out/L1.LZH %(all)s > /dev/null
jlha a2o7 /w/out/L2.LZH %(all)s > /dev/null
jlha a2o5 /w/out/PACK.LZH READ.ME DOCS > /dev/null
for f in /w/out/*.LZH; do lha t $f > /dev/null; done
# ARJ: methods 1 to 4 and none (-m0), folders as entries (-r -a1 with '*'), one with a password (-g), one in
# volumes of 10 KB (-v10k: the second file goes on in VOL.A01)
mkdir /part
cp -a READ.ME NOISE.BIN EMPTY DOCS /part
arj a -y -r -a1 -m1 /w/out/M1.ARJ '*' > /dev/null
arj a -y -r -a1 -m4 /w/out/M4.ARJ '*' > /dev/null
(cd /part && arj a -y -r -a1 -m0 /w/out/M0.ARJ '*' > /dev/null)
arj a -y -m2 /w/out/M2.ARJ TEXT.TXT > /dev/null
arj a -y -m3 /w/out/M3.ARJ TEXT.TXT > /dev/null
rm /part/NOISE.BIN /part/EMPTY
(cd /part && arj a -y -r -a1 -m1 /w/out/PACK.ARJ '*' > /dev/null)
arj a -y -gsecret /w/out/LOCKED.ARJ READ.ME > /dev/null
arj a -y -m1 -v10k /w/out/VOL.ARJ TEXT.TXT FAR.BIN > /dev/null
for f in /w/out/M?.ARJ /w/out/PACK.ARJ; do arj t $f > /dev/null; done
ls -l /w/out
chown -R %(uid)d:%(gid)d /w
""" % {"all": ALL, "uid": os.getuid(), "gid": os.getgid()}
open(os.path.join(work, "make.sh"), "w").write(script)
subprocess.run(["docker", "run", "--rm", "-v", work + ":/w", "ubuntu:24.04", "bash", "/w/make.sh"], check=True)

lines = []
for f in sorted(os.listdir(out)):
    path = os.path.join(out, f)
    if f not in ("LOCKED.ARJ", "VOL.ARJ", "VOL.A01"):
        subprocess.run(["7z", "t", path], check=True, stdout=subprocess.DEVNULL)
    listing = subprocess.run(["7z", "l", "-slt", path], check=False, stdout=subprocess.PIPE).stdout.decode("latin-1")
    lines.append("A\t%s" % f)
    entry = None
    for ln in listing.split("\n"):
        ln = ln.rstrip("\r")
        if ln.startswith("Path = ") and "----------" in listing[:listing.find(ln)]:
            entry = {"Path": ln[7:]}
        elif entry is not None and " = " in ln:
            k, v = ln.split(" = ", 1)
            entry[k] = v
        elif entry is not None and not ln.strip():
            lines.append("E\t%s\t%s\t%s" % (entry["Path"].replace("\\", "/"), "d" if entry.get("Folder") == "+" else "f",
                                           entry.get("Size", "0")))
            entry = None
    shutil.copy(path, os.path.join(here, f))
open(os.path.join(here, "LIST"), "w").write("\n".join(lines) + "\n")
gui = os.path.join(here, "..", "..", "gui", "drives", "C", "LZH")    # (the two that tests/gui/files-lzh.txt goes into)
os.makedirs(os.path.join(gui, "OUT"), exist_ok=True)
for f in ("PACK.LZH", "PACK.ARJ"):
    shutil.copy(os.path.join(out, f), os.path.join(gui, f))
sums = ["%s\t%d\t%08x" % (n, len(d), zlib.crc32(d) & 0xFFFFFFFF) for n, d in inputs.files() if d is not None]
open(os.path.join(here, "SUMS"), "w").write("\n".join(sums) + "\n")
print("\n".join(lines))
