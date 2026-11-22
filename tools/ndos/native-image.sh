#!/bin/sh
# tools/ndos/native-image.sh OUT.img [SRC=DOSPATH...]: a disk GLOS starts from with no DOS at all
# (docs/v1/native-dos.md, native boot): 64 MB (130 cylinders of 16 heads by 63), GLOS's MBR, one FAT volume made
# by GLOS's own formatter with GLOS's boot code naming GLOSLDR.SYS (tools/ndos/mkvol.c), \GLOSLDR.SYS
# (build/ow/GLOSLDR.SYS) and \GLOS\ with GLOSK.BIN; then the files given (DOSPATH as \GLOS\GLOS.CFG, \X.EXE).
# For Loop A's --hdd. Run inside the dev container (tools/dev): gcc, mtools.
set -eu
out=${1:?"usage: native-image.sh OUT.img [SRC=DOSPATH...]"}
shift
here=$(cd "$(dirname "$0")/../.." && pwd)
cyls=130
total=$((cyls * 16 * 63))
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
gcc -m32 -O1 -o "$tmp/mkmbr" "$here/tools/ndos/mkmbr.c" "$here/kernel/disk/mbr.c" "$here/kernel/disk/mbrboot.S"
gcc -m32 -O1 -I"$here/kernel/include" -I"$here/include" -o "$tmp/mkvol" "$here/tools/ndos/mkvol.c" \
    "$here/kernel/disk/fatfmt.c" "$here/kernel/disk/mbr.c" "$here/kernel/disk/mbrboot.S" "$here/kernel/disk/fdboot.S" \
    "$here/sdk/config/cp.c" "$here/sdk/config/cp_tables.c"
"$tmp/mkmbr" "$total" 0 > "$tmp/mbr" 2> "$tmp/part"
read -r start sectors < "$tmp/part"
rm -f "$out"
truncate -s $((total * 512)) "$out"
dd if="$tmp/mbr" of="$out" conv=notrunc status=none
"$tmp/mkvol" "$out" "$start" "$sectors" GLOSNATIVE
export MTOOLS_SKIP_CHECK=1
i="$out@@$((start * 512))"
mcopy -i "$i" "$here/build/ow/GLOSLDR.SYS" ::GLOSLDR.SYS
mattrib -i "$i" +s +h +r ::GLOSLDR.SYS
mmd -i "$i" ::GLOS
mcopy -i "$i" "$here/build/kernel/GLOSK.BIN" ::GLOS/GLOSK.BIN
for a in "$@"; do
    src=${a%%=*}
    dst=$(printf '%s' "${a#*=}" | tr '\\' '/')
    mcopy -o -i "$i" "$src" "::$dst"
done
echo "$out: native GLOS, volume at $start ($sectors sectors)"
