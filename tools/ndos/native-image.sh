#!/bin/sh
# tools/ndos/native-image.sh OUT.img [SRC=DOSPATH...]: a disk GLOS starts from with no DOS at all
# (docs/v1/native-dos.md, native boot): 64 MB (130 cylinders of 16 heads by 63), GLOS's MBR, one FAT volume made
# by GLOS's own formatter with GLOS's boot code naming GLOSLDR.SYS (tools/ndos/mkvol.c), \GLOSLDR.SYS
# (build/ow/GLOSLDR.SYS) and \GLOS\ with GLOSK.BIN; then the files given (DOSPATH as \GLOS\GLOS.CFG, \X.EXE).
# GLOS_CMDLINE in the environment: the loader's command line (a folder other than \GLOS for GLOS's files, flags).
# GLOS_DISK_MB: the disk's size (64); GLOS_HEADS: its heads (16; 255 is what a BIOS gives a USB stick);
# GLOS_LABEL: the volume's name (GLOSNATIVE).
# For Loop A's --hdd, and for tools/ndos/stick.py's USB stick. Run inside the dev container (tools/dev): gcc, mtools.
set -eu
out=${1:?"usage: native-image.sh OUT.img [SRC=DOSPATH...]"}
shift
here=$(cd "$(dirname "$0")/../.." && pwd)
heads=${GLOS_HEADS:-16}
if [ -n "${GLOS_DISK_MB:-}" ]; then
    total=$((GLOS_DISK_MB * 2048 / (heads * 63) * heads * 63))
else
    total=$((130 * 16 * 63))
fi
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
gcc -m32 -O1 -o "$tmp/mkmbr" "$here/tools/ndos/mkmbr.c" "$here/kernel/disk/mbr.c" "$here/kernel/disk/mbrboot.S"
gcc -m32 -O1 -I"$here/kernel/include" -I"$here/include" -o "$tmp/mkvol" "$here/tools/ndos/mkvol.c" \
    "$here/kernel/disk/fatfmt.c" "$here/kernel/disk/mbr.c" "$here/kernel/disk/mbrboot.S" "$here/kernel/disk/fdboot.S" \
    "$here/sdk/config/cp.c" "$here/sdk/config/cp_tables.c"
"$tmp/mkmbr" "$total" 0 "$heads" > "$tmp/mbr" 2> "$tmp/part"
read -r start sectors < "$tmp/part"
rm -f "$out"
truncate -s $((total * 512)) "$out"
dd if="$tmp/mbr" of="$out" conv=notrunc status=none
"$tmp/mkvol" "$out" "$start" "$sectors" "${GLOS_LABEL:-GLOSNATIVE}" "$heads"
export MTOOLS_SKIP_CHECK=1
i="$out@@$((start * 512))"
# GLOSLDR.SYS, its command line (GLOS_CMDLINE: a folder for GLOS's files and GLOS.EXE's flags, 93 bytes at most)
# written into its first sector at 01A0h (loader/nprelude.S).
cp "$here/build/ow/GLOSLDR.SYS" "$tmp/GLOSLDR.SYS"
if [ -n "${GLOS_CMDLINE:-}" ]; then
    [ ${#GLOS_CMDLINE} -le 93 ] || { echo "native-image: GLOS_CMDLINE is over 93 bytes" >&2; exit 1; }
    printf '%s\0' "$GLOS_CMDLINE" | dd of="$tmp/GLOSLDR.SYS" bs=1 seek=416 conv=notrunc status=none
fi
mcopy -i "$i" "$tmp/GLOSLDR.SYS" ::GLOSLDR.SYS
mattrib -i "$i" +s +h +r ::GLOSLDR.SYS
mmd -i "$i" ::GLOS
mcopy -i "$i" "$here/build/kernel/GLOSK.BIN" ::GLOS/GLOSK.BIN
made=
for a in "$@"; do
    src=${a%%=*}
    dst=$(printf '%s' "${a#*=}" | tr '\\' '/')
    dir=$(dirname "$dst")
    case " $made " in                           # (its folders, the outer ones first, each made once)
        *" $dir "*) ;;
        *)  d=$dir up=
            while [ "$d" != "/" ] && [ "$d" != "." ]; do up="$d $up"; d=$(dirname "$d"); done
            for d in $up; do mmd -i "$i" "::$d" 2> /dev/null || true; done
            made="$made $dir" ;;
    esac
    mcopy -o -i "$i" "$src" "::$dst"
done
echo "$out: native GLOS, volume at $start ($sectors sectors)"
