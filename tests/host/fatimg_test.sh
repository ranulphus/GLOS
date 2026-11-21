#!/bin/sh
# fatimg_test.sh BINARY OUTDIR: Disk Tools' FAT volumes (apps/shared/fatimg.c; tests/host/fatimg_test.c) held
# against the tools everyone else's disks are read by. What it wrote: fsck.fat finds each volume clean, and
# mtools reads its files to the byte, in the root and two folders deep, and lists the folder that grew. The
# other way round: a volume mtools made, with long names and a folder, is listed and read by fatimg. In the dev
# container.
set -e
bin=$1 dir=$2
rm -rf "$dir" && mkdir -p "$dir"
"$bin" "$dir" > "$dir/list"
grep -q '^fatimg_test: 0 failures' "$dir/list" || { cat "$dir/list"; exit 1; }
fails=0
n=0
for name in $(sed -n 's/^VOLUME \([a-z0-9]*\) .*/\1/p' "$dir/list"); do
    n=$((n + 1))
    img="$dir/$name.img"
    if ! fsck.fat -n "$img" > "$dir/$name.fsck" 2>&1; then echo "FAIL $name: fsck.fat"; tail -5 "$dir/$name.fsck"; fails=$((fails + 1)); continue; fi
    mcopy -n -i "$img" ::/BIG.BIN "$dir/back" 2> "$dir/$name.err" && cmp -s "$dir/$name.big" "$dir/back" \
        || { echo "FAIL $name: BIG.BIN read by mtools"; fails=$((fails + 1)); }
    mcopy -n -i "$img" ::/SUB/DEEP/MORE.BIN "$dir/back2" 2>> "$dir/$name.err" && cmp -s "$dir/$name.more" "$dir/back2" \
        || { echo "FAIL $name: SUB/DEEP/MORE.BIN read by mtools"; fails=$((fails + 1)); }
    mdir -i "$img" ::/SUB > "$dir/$name.dir" 2>&1 && grep -q "F199" "$dir/$name.dir" && grep -q "DEEP" "$dir/$name.dir" \
        || { echo "FAIL $name: the folder that grew, listed by mtools"; fails=$((fails + 1)); }
    mdir -i "$img" :: 2>/dev/null | grep -q "GLOS TEST" || { echo "FAIL $name: its label"; fails=$((fails + 1)); }
    rm -f "$dir/back" "$dir/back2" "$img"
done
# a volume of mtools' making: long names, a folder, a name in small letters
name=f1440
dd if=/dev/zero of="$dir/m.img" bs=512 count=2880 status=none
mformat -i "$dir/m.img" -f 1440 -v MTOOLS ::
mcopy -i "$dir/m.img" "$dir/$name.big" "::/A long name with blanks.bin"
mmd -i "$dir/m.img" "::/Sub Folder"
mcopy -i "$dir/m.img" "$dir/$name.more" "::/Sub Folder/more.bin"
mcopy -i "$dir/m.img" "$dir/$name.more" "::/UPPER.BIN"
"$bin" list "$dir/m.img" "" > "$dir/m.list" || { echo "FAIL: mtools' volume not listed"; fails=$((fails + 1)); }
grep -q '^F 70000 [A-Z0-9~]*\.BIN A long name with blanks\.bin$' "$dir/m.list" && grep -q '^D 0 [A-Z0-9~]* Sub Folder$' "$dir/m.list" \
    && grep -q '^F 5000 UPPER\.BIN UPPER\.BIN$' "$dir/m.list" || { echo "FAIL: mtools' volume's root:"; cat "$dir/m.list"; fails=$((fails + 1)); }
"$bin" list "$dir/m.img" "sub folder" > "$dir/m.sub" && grep -q '^F 5000 MORE\.BIN more\.bin$' "$dir/m.sub" \
    || { echo "FAIL: mtools' folder:"; cat "$dir/m.sub"; fails=$((fails + 1)); }
"$bin" read "$dir/m.img" "A long name with blanks.bin" "$dir/r1" && cmp -s "$dir/$name.big" "$dir/r1" \
    || { echo "FAIL: a file by its long name"; fails=$((fails + 1)); }
"$bin" read "$dir/m.img" "SUB FOLDER/MORE.BIN" "$dir/r2" && cmp -s "$dir/$name.more" "$dir/r2" \
    || { echo "FAIL: a file in a folder by its long name"; fails=$((fails + 1)); }
rm -f "$dir"/*.big "$dir"/*.more "$dir/m.img" "$dir/r1" "$dir/r2"
echo "fatimg_test.sh: $fails failures ($n volumes read by fsck.fat and mtools; one of mtools' read here)"
[ "$fails" = 0 ]
