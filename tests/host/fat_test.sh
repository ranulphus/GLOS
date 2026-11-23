#!/bin/sh
# fat_test.sh BINARY OUTDIR: kernel/fs/fat.c (N1a, reading) against volumes that mkfs.fat and kernel/disk's
# fat_format made and mtools filled; then (N1b, writing) the same kinds of volume written through kernel/fs and
# judged by mtools and fsck.fat, and cut short at sampled points (a power cut) for fsck.fat to judge
# (tests/host/fat_test.c). In the dev container (mkfs.fat, fsck.fat, mtools, python3).
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
# wvol NAME KB HOW LABEL CUTS: a new volume (HOW as vol's) written by `fat_test write`: mtools' copy of it the same as
# the mirror the program kept, fsck.fat content, the reader's check of it, mtools' free space the reader's. Then
# CUTS power cuts spread over its writes, each on a fresh volume: fsck.fat may find only what a write order that
# keeps the volume repairable leaves (lost clusters, a chain longer than its file, the FAT copies apart, long-name
# entries without their short one), never a file longer than its chain, clusters shared or free in a chain.
wvol() {
    name=$1 kb=$2 how=$3 label=$4 cuts=$5
    base=$dir/$name.base img=$dir/$name.img mir=$dir/$name.mirror out=$dir/$name.extract
    rm -rf "$mir" "$out" && mkdir -p "$mir" "$out"
    case "$how" in
        glos*) "$bin" format "$base" $((kb * 2)) "${how#glos }" > "$dir/$name.format" || { echo "FAIL $name: format"; fails=$((fails + 1)); return; } ;;
        *) mkfs.fat $how -C "$base" "$kb" > "$dir/$name.mkfs" 2>&1 || { echo "FAIL $name: mkfs.fat"; fails=$((fails + 1)); return; } ;;
    esac
    cp "$base" "$img"
    if ! "$bin" write "$img" "$mir" > "$dir/$name.out" 2>&1; then
        echo "FAIL $name: writing"; grep -v '^VOLUME\|^WRITES' "$dir/$name.out" | head -20; fails=$((fails + 1)); return
    fi
    writes=$(sed -n 's/^CUTTABLE //p' "$dir/$name.out")
    if ! fsck.fat -n "$img" > "$dir/$name.fsck" 2>&1; then
        echo "FAIL $name: fsck.fat"; head -20 "$dir/$name.fsck"; fails=$((fails + 1))
    fi
    mcopy -s -n -m -i "$img" '::/*' "$out/" 2> "$dir/$name.mcopy" || { echo "FAIL $name: mcopy"; head "$dir/$name.mcopy"; fails=$((fails + 1)); }
    if ! diff -r "$mir" "$out" > "$dir/$name.diff" 2>&1; then
        echo "FAIL $name: mtools' copy differs"; head -20 "$dir/$name.diff"; fails=$((fails + 1))
    fi
    manifest "$mir" "$dir/$name.manifest"
    if ! "$bin" check "$img" "$dir/$name.manifest" 850 "$label" > "$dir/$name.check" 2>&1; then
        echo "FAIL $name: read back:"; grep -v '^FREE\|^VOLUME' "$dir/$name.check" | head -20; fails=$((fails + 1))
    fi
    want=$(mdir -i "$img" :: 2>/dev/null | sed -n 's/ bytes free.*//p' | tr -d ' ')
    got=$(sed -n 's/^FREE //p' "$dir/$name.check")
    [ "$want" = "$got" ] || { echo "FAIL $name: free $got, mtools $want"; fails=$((fails + 1)); }
    # The power cuts.
    bad=0 step=$(( writes / cuts + 1 )) c=1
    while [ "$c" -le "$writes" ]; do
        cp "$base" "$img"
        rc=0
        timeout 60 "$bin" write "$img" "$mir.none" "$c" > /dev/null 2>&1 || rc=$?
        if [ $rc -ge 124 ]; then
            echo "FAIL $name: cut at write $c: fat_test hung or crashed ($rc)"; bad=$((bad + 1)); c=$((c + step)); continue
        fi
        fsck.fat -n "$img" > "$dir/$name.cut" 2>&1 || true
        if grep -v "$repairable" "$dir/$name.cut" > "$dir/$name.left"; then
            [ $bad = 0 ] && { echo "FAIL $name: cut at write $c of $writes, fsck.fat says:"; head -12 "$dir/$name.left"; }
            bad=$((bad + 1))
        fi
        c=$((c + step))
    done
    [ $bad = 0 ] || { echo "FAIL $name: $bad cuts left more than fsck.fat repairs"; fails=$((fails + 1)); }
    echo "$name: written ($(sed -n 's/^VOLUME //p' "$dir/$name.out" | cut -d' ' -f1-3), $writes sector writes before the fill), $cuts cuts repairable"
    rm -f "$base" "$img"
}

# What fsck.fat may say after a cut: lost clusters, a chain longer than its file, the FAT copies apart (the first
# intact), long-name entries without their short one, the dirty bit (the clean bit in FAT[1] that writing clears),
# the free count, a FAT12 entry cut half way past the volume's end, and its own framing.
repairable='^fsck.fat [0-9]\|^Leaving filesystem unchanged\|^ *$\|^/\|files, [0-9]*/[0-9]* clusters$'
repairable="$repairable"'\|Reclaimed [0-9]* unused clusters\?\|cluster chain length is > [0-9]* bytes\|Truncating file to'
repairable="$repairable"'\|FATs differ but appear to be intact\|Using first FAT\|Free cluster summary'
repairable="$repairable"'\|Dirty bit is set\|Automatically removing dirty bit\|^Orphaned long file name part'
repairable="$repairable"'\|^  Auto-correcting\.$\|^  Auto-deleting\.$'
# (a FAT12 entry across two sectors cut half way: past the volume, which fsck ends the chain at; fat.c writes the
# half first that makes it so)
repairable="$repairable"'\|^Cluster [0-9]* out of range ([0-9]* > [0-9]*)\. Setting to EOF\.$'

wvol wfloppy 1440 "-n N1BFLOPPY" N1BFLOPPY 150
wvol wfat16 32768 "-F 16 -n N1BW16" N1BW16 100
wvol wfat32 40960 "-F 32 -s 1" - 100
wvol wglos12 1440 "glos 12" "GLOS N1A" 40
wvol wglos16 65536 "glos 16" "GLOS N1A" 40
wvol wglos32 204800 "glos 32" "GLOS N1A" 30
echo "fat_test.sh: $fails failures"
[ "$fails" = 0 ]
