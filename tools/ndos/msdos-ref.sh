#!/bin/sh
# tools/ndos/msdos-ref.sh [OUT.txt]: MS-DOS 7.1's own answers to DOSDIFF (tests/dos/dosdiff.c), for
# tests/dos/dosdiff.msdos71.txt (OUT.txt: there if not given). One boot in 86Box (Loop A's run.py, about half a
# minute) of the private MS-DOS 7.1 disk that tools/ndos/msdos-image.sh makes from the owner's files (MSDOS_DIR:
# PRIVATE, and so is the disk: both stay in the cache, outside the repository), with a blank 1.44 MB floppy in A:
# for DOSDIFF's FCB probes: MS-DOS 7.1 opens no file through an FCB on a FAT32 volume, and its own disk is one.
# The floppy's boot sector hands the start to the hard disk (run.py's --floppy is tried first by the BIOS).
# The reference disk is ref.img, not c.img (jobs.py's ndualm test's MS-DOS disk, left alone). Run inside the dev
# container (tools/dev) from the repository's root, after `make dos-tests`.
set -eu
here=$(cd "$(dirname "$0")/../.." && pwd)
cache=${MGA_CACHE:-$HOME/.cache/mga-glide}
retro=${RETROGPU:-$HOME/RetroGPU}
dir=$cache/msdos71-img
out=${1:-$here/tests/dos/dosdiff.msdos71.txt}
export MSDOS_DIR=${MSDOS_DIR:-$cache/msdos71}
mkdir -p "$dir"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
printf '@ECHO OFF\r\nC:\\DOSDIFF.EXE A > C:\\DIFF.TXT\r\nC:\\SERSAY.COM HX-DONE 0\r\n' > "$tmp/AUTOEXEC.BAT"
sh "$here/tools/ndos/msdos-image.sh" "$dir/ref.img" "$tmp/AUTOEXEC.BAT" "$here/build/ow/dos/DOSDIFF.EXE" \
    "$retro/build/ow/dos/SERSAY.COM"
rm -f "$dir/ref-a.img"
mkfs.fat -C -n REFDISK "$dir/ref-a.img" 1440 > /dev/null
python3 - "$dir/ref-a.img" <<'PY'
import sys
# The floppy's boot code: itself moved to 0000:0600h, the hard disk's first sector read to 0000:7C00h and
# started with DL 80h, as the BIOS would have started it.
code = bytes([
    0xFA, 0x31, 0xC0, 0x8E, 0xD0, 0xBC, 0x00, 0x7C, 0x8E, 0xD8, 0x8E, 0xC0, 0xFB, 0xFC,   # stack, DS = ES = 0
    0xBE, 0x00, 0x7C, 0xBF, 0x00, 0x06, 0xB9, 0x00, 0x01, 0xF3, 0xA5,                     # 512 bytes to 0600h
    0xEA, 0x5C, 0x06, 0x00, 0x00,                                                         # jmp 0000:065Ch
    0xB8, 0x01, 0x02, 0xBB, 0x00, 0x7C, 0xB9, 0x01, 0x00, 0xBA, 0x80, 0x00, 0xCD, 0x13,   # INT 13h: read sector 1
    0x72, 0xFE,                                                                           # (an error: stop here)
    0xEA, 0x00, 0x7C, 0x00, 0x00,                                                         # jmp 0000:7C00h
])
f = open(sys.argv[1], "r+b")
b = bytearray(f.read(512))
assert b[0] == 0xEB and b[1] == 0x3C, "mkfs.fat's FAT12 boot sector begins EB 3C"
b[0x3E:0x3E + len(code)] = code
assert b[510] == 0x55 and b[511] == 0xAA
f.seek(0)
f.write(b)
PY
rm -rf "$here/out/msdos71-ref"
python3 "$retro/tools/loopa/run.py" --name msdos71-ref --hdd "$dir/ref.img" --floppy "$dir/ref-a.img" --machine bf6 \
    --out "$here/out/msdos71-ref"
MTOOLS_SKIP_CHECK=1 mtype -i "$dir/ref.img@@32256" ::DIFF.TXT | tr -d '\r' > "$tmp/diff.txt"
grep -q '^DOSDIFF end$' "$tmp/diff.txt" || { echo "msdos-ref: DOSDIFF didn't end (out/msdos71-ref)" >&2; exit 1; }
cp "$tmp/diff.txt" "$out"
echo "msdos-ref: $(wc -l < "$out") lines in $out"
