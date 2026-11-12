#!/bin/sh
# disk_test.sh BINARY OUTDIR: what kernel/disk makes (tests/host/disk_test.c), read by the tools everyone else's
# disks are read by: fsck.fat finds each volume clean, mtools lists it, puts a file in and reads it back, and the
# volume is still clean; the whole disk's partition is found where its table says. In the dev container.
set -e
bin=$1 dir=$2
rm -rf "$dir" && mkdir -p "$dir"
"$bin" "$dir" > "$dir/list"
grep -q '^disk_test: 0 failures' "$dir/list" || { cat "$dir/list"; exit 1; }
head -c 70000 /dev/urandom > "$dir/data"
fails=0
check() {       # NAME IMAGE[@@OFFSET] FILE-FOR-FSCK FAT
    if ! fsck.fat -n "$3" > "$dir/$1.fsck" 2>&1; then echo "FAIL $1: fsck.fat"; tail -5 "$dir/$1.fsck"; fails=$((fails + 1)); return; fi
    grep -q "FAT$4" "$dir/$1.fsck" || minfo -i "$2" :: 2>/dev/null | grep -qi "fat" || true
    mdir -i "$2" :: > "$dir/$1.dir" 2>&1 || { echo "FAIL $1: mdir"; fails=$((fails + 1)); return; }
    mcopy -i "$2" "$dir/data" ::/DATA.BIN && mmd -i "$2" ::/SUB && mcopy -i "$2" "$dir/data" ::/SUB/MORE.BIN \
        && mcopy -n -i "$2" ::/SUB/MORE.BIN "$dir/back" && cmp -s "$dir/data" "$dir/back" \
        || { echo "FAIL $1: a file in and out"; fails=$((fails + 1)); return; }
    rm -f "$dir/back"
}
n=0
while read -r kind name fat spc clusters; do
    [ "$kind" = VOLUME ] || continue
    n=$((n + 1))
    check "$name" "$dir/$name.img" "$dir/$name.img" "$fat"
    fsck.fat -n "$dir/$name.img" > "$dir/$name.fsck2" 2>&1 || { echo "FAIL $name: fsck.fat after files"; tail -5 "$dir/$name.fsck2"; fails=$((fails + 1)); }
    case "$name" in f*) grep -q 'GLOS TEST' "$dir/$name.dir" || { echo "FAIL $name: label"; fails=$((fails + 1)); };; esac
    rm -f "$dir/$name.img"
done < "$dir/list"
set -- $(grep '^DISK' "$dir/list")
dd if="$dir/disk.img" of="$dir/part.img" bs=512 skip="$2" count="$3" status=none
check disk "$dir/disk.img@@$(($2 * 512))" "$dir/part.img" 16
rm -f "$dir/part.img" "$dir/disk.img" "$dir/data"
echo "disk_test.sh: $fails failures ($n volumes and a disk read by fsck.fat and mtools)"
[ "$fails" = 0 ]
