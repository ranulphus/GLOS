#!/bin/sh
# tools/ndos/msdos-image.sh OUT.img [SRC[=NAME]...]: a boot disk of MS-DOS 7.1 for 86Box (Loop A's --hdd), made
# from the owner's own MS-DOS files in MSDOS_DIR (PRIVATE: never committed, and so neither is this disk; keep
# OUT outside the repository). GLOS's reference for its own DOS (docs/v1/native-dos.md, NS2: the "to check"
# answers): a 128 MB disk, GLOS's MBR, one FAT32 partition whose boot code is MS-DOS 7.1's own
# (MSDOS_DIR/bootsect-0-2.bin, the BPB mkfs.fat's), IO.SYS, COMMAND.COM, an MSDOS.SYS of ours (no Windows, no
# logo), and the files given in its root (an AUTOEXEC.BAT among them). Run inside the dev container
# (tools/dev): mkfs.fat, mtools, gcc.
set -eu
msdos=${MSDOS_DIR:?"MSDOS_DIR: the owner's MS-DOS 7.1 files (private)"}
out=${1:?"usage: msdos-image.sh OUT.img [SRC[=NAME]...]"}
shift
here=$(cd "$(dirname "$0")/../.." && pwd)
for f in IO.SYS COMMAND.COM bootsect-0-2.bin; do
    [ -s "$msdos/$f" ] || { echo "msdos-image: no $msdos/$f" >&2; exit 1; }
done
case "$(cd "$(dirname "$out")" && pwd)/" in
    "$here"/*) echo "msdos-image: $out is inside the repository: MS-DOS's files are private" >&2; exit 1 ;;
esac
cyls=254                                # (254 x 16 x 63 sectors: 128 MB)
total=$((cyls * 16 * 63))
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
gcc -m32 -O1 -o "$tmp/mkmbr" "$here/tools/ndos/mkmbr.c" "$here/kernel/disk/mbr.c" "$here/kernel/disk/mbrboot.S"
"$tmp/mkmbr" "$total" 0C > "$tmp/mbr" 2> "$tmp/part"
read -r start sectors < "$tmp/part"
rm -f "$out"
truncate -s $((total * 512)) "$out"
dd if="$tmp/mbr" of="$out" conv=notrunc status=none
# The partition: FAT32 by mkfs.fat, then MS-DOS 7.1's boot code over its sectors 0 and 2 (and their backups at
# 6 and 8), mkfs.fat's BPB kept.
truncate -s $((sectors * 512)) "$tmp/p.img"
mkfs.fat -F 32 -s 1 -h "$start" -S 512 -R 32 -b 6 -D 0x80 -n MSDOS71 -i 71071071 "$tmp/p.img" > /dev/null
python3 - "$tmp/p.img" "$msdos/bootsect-0-2.bin" <<'PY'
import sys
p, ms = sys.argv[1], open(sys.argv[2], "rb").read()
f = open(p, "r+b")
mk0 = f.read(512)
s0 = ms[:0x0B] + mk0[0x0B:0x5A] + ms[0x5A:512]
s2 = ms[1024:1536]
assert s0[510:512] == b"\x55\xaa" and len(s2) == 512, "bootsect-0-2.bin: not a FAT32 boot sector's three"
for at, data in ((0, s0), (2, s2), (6, s0), (8, s2)):
    f.seek(at * 512)
    f.write(data)
PY
export MTOOLS_SKIP_CHECK=1
i="$tmp/p.img"
mcopy -i "$i" "$msdos/IO.SYS" ::IO.SYS
mattrib -i "$i" +s +h +r ::IO.SYS
{
    printf '[Paths]\r\nWinDir=C:\\WINDOWS\r\nWinBootDir=C:\\WINDOWS\r\nHostWinBootDrv=C\r\n'
    printf '[Options]\r\nBootGUI=0\r\nLogo=0\r\nBootDelay=0\r\nBootMenu=0\r\nAutoScan=0\r\nDblSpace=0\r\nDrvSpace=0\r\n'
    n=0
    while [ $n -lt 20 ]; do
        printf ';xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\r\n'
        n=$((n + 1))
    done
} > "$tmp/MSDOS.SYS"
mcopy -i "$i" "$tmp/MSDOS.SYS" ::MSDOS.SYS
mattrib -i "$i" +s +h +r ::MSDOS.SYS
mcopy -i "$i" "$msdos/COMMAND.COM" ::COMMAND.COM
printf 'FILES=40\r\nBUFFERS=20\r\n' > "$tmp/CONFIG.SYS"
mcopy -i "$i" "$tmp/CONFIG.SYS" ::CONFIG.SYS
for a in "$@"; do
    src=${a%%=*}
    name=${a#*=}
    [ "$name" = "$a" ] && name=$(basename "$src")
    mcopy -o -i "$i" "$src" "::$name"
done
dd if="$i" of="$out" bs=512 seek="$start" conv=notrunc status=none
echo "$out: MS-DOS 7.1, partition at $start ($sectors sectors)"
