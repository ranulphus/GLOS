#!/bin/sh
# fat_test.sh BINARY OUTDIR: kernel/fs/fat.c (N1a, reading) against volumes that mkfs.fat and kernel/disk's
# fat_format made and mtools filled (tests/host/fat_test.c). In the dev container (mkfs.fat, mtools, python3).
#
# Each volume gets a tree of files: long names (spaces, many dots, Unicode in Latin, Greek and Cyrillic), short
# ones in lower case, deep folders, a folder of 300 files (more than a cluster of entries), empty files, files
# of a cluster and a byte either side, files of megabytes, and fragmentation (filler files copied, every other
# deleted, then the big files copied into the holes). The manifest is the host's own copy of the tree.
set -e
bin=$1 dir=$2
rm -rf "$dir" && mkdir -p "$dir"
export LC_ALL=C.UTF-8 MTOOLS_SKIP_CHECK=1
fails=0

# tree DIR SIZE-KB: the tree to copy (fewer and smaller files on a floppy).
tree() {
    python3 - "$1" "$2" <<'EOF'
import os, random, sys
root, kb = sys.argv[1], int(sys.argv[2])
rnd = random.Random(20261120)
def put(path, n):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(rnd.randbytes(n))
put(os.path.join(root, "short.txt"), 100)
put(os.path.join(root, "README"), 0)
put(os.path.join(root, "A Longer Name With Spaces.data"), 5000)
put(os.path.join(root, "many.dots.in.this.name.txt"), 1234)
put(os.path.join(root, ".hidden"), 77)
put(os.path.join(root, "Ünïcödé Ελληνικά кириллица.txt"), 4321)
put(os.path.join(root, "deep", "er", "and", "deeper", "still", "folder", "seven", "eight", "leaf.bin"), 999)
put(os.path.join(root, "deep", "er", "sibling.txt"), 1)
for n in (511, 512, 513, 2047, 2048, 2049, 4095, 4096, 4097, 16383, 16384, 16385):
    put(os.path.join(root, "sizes", "s%05d.bin" % n), n)
many = 300 if kb > 2000 else 120
for i in range(many):
    put(os.path.join(root, "many", "file number %03d.txt" % i), rnd.randrange(0, 600))
big = [kb * 1024 // 8, kb * 1024 // 5]
for i, n in enumerate(big):
    put(os.path.join(root, "big", "big%d.bin" % i), n)
EOF
}

# manifest TREE OUT: "path<TAB>size<TAB>crc32" for each file.
manifest() {
    python3 - "$1" "$2" <<'EOF'
import os, sys, zlib
root, out = sys.argv[1], sys.argv[2]
with open(out, "w") as f:
    for d, _, names in os.walk(root):
        for n in names:
            p = os.path.join(d, n)
            data = open(p, "rb").read()
            f.write("%s\t%d\t%08x\n" % (os.path.relpath(p, root), len(data), zlib.crc32(data)))
EOF
}

# fill IMAGE TREE KB: the fillers, every other deleted (host copy too), then the tree.
fill() {
    mkdir -p "$2/fill"
    i=0
    while [ $i -lt 24 ]; do
        head -c $(( $3 * 1024 / 200 + 1500 )) /dev/urandom > "$2/fill/f$i.bin"
        i=$((i + 1))
    done
    mcopy -s -i "$1" "$2/fill" ::/
    i=1
    while [ $i -lt 24 ]; do
        mdel -i "$1" "::/fill/f$i.bin"
        rm -f "$2/fill/f$i.bin"
        i=$((i + 2))
    done
    for f in "$2"/* "$2"/.[!.]*; do
        [ -e "$f" ] || continue
        [ "$(basename "$f")" = fill ] && continue
        mcopy -s -i "$1" "$f" ::/
    done
}

# vol NAME KB HOW CP LABEL: HOW is a mkfs.fat option string, or "glos FAT" for kernel/disk's fat_format.
vol() {
    name=$1 kb=$2 how=$3 cp=$4 label=$5
    img=$dir/$name.img src=$dir/$name.tree
    rm -rf "$src"
    tree "$src" "$kb"
    case "$how" in
        glos*) "$bin" format "$img" $((kb * 2)) "${how#glos }" > "$dir/$name.format" || { echo "FAIL $name: format"; fails=$((fails + 1)); return; } ;;
        *) mkfs.fat $how -C "$img" "$kb" > "$dir/$name.mkfs" 2>&1 || { echo "FAIL $name: mkfs.fat"; cat "$dir/$name.mkfs"; fails=$((fails + 1)); return; } ;;
    esac
    fill "$img" "$src" "$kb" || { echo "FAIL $name: mtools filling"; fails=$((fails + 1)); return; }
    manifest "$src" "$dir/$name.manifest"
    if ! "$bin" check "$img" "$dir/$name.manifest" "$cp" "$label" > "$dir/$name.out" 2>&1; then
        echo "FAIL $name:"; grep -v '^FREE\|^VOLUME' "$dir/$name.out" | head -20; fails=$((fails + 1)); return
    fi
    # Free space as mtools counts it ("N bytes free", its thousands parted by spaces).
    want=$(mdir -i "$img" :: 2>/dev/null | sed -n 's/ bytes free.*//p' | tr -d ' ')
    got=$(sed -n 's/^FREE //p' "$dir/$name.out")
    [ "$want" = "$got" ] || { echo "FAIL $name: free $got, mtools $want"; fails=$((fails + 1)); }
    echo "$name: $(grep '^VOLUME' "$dir/$name.out" | cut -d' ' -f2-4), $(sed -n 's/^fat_test: \([0-9]* files, [0-9]* folders\)/\1/p' "$dir/$name.out")"
    rm -f "$img"
}

vol floppy 1440 "-n N1AFLOPPY" 850 N1AFLOPPY
vol fat16 32768 "-F 16 -n N1AFAT16" 850 N1AFAT16
vol fat16big 262144 "-F 16 -s 64" 850 -
vol fat32 307200 "-F 32 -n N1AFAT32" 850 N1AFAT32
vol fat32small 40960 "-F 32 -s 1" 850 -
vol glos12 1440 "glos 12" 850 "GLOS N1A"
vol glos16 65536 "glos 16" 850 "GLOS N1A"
vol glos32 204800 "glos 32" 850 "GLOS N1A"
echo "fat_test.sh: $fails failures"
[ "$fails" = 0 ]
