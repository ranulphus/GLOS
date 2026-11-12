#!/bin/sh
# fdboot-volumes.sh FLOPPY DIR: the volumes for the test of GLOS's FAT boot code (kernel/disk/fdboot.S; the
# floppy suite's fdboot cases), in the dev container (mtools). build/host/fdbootdisk makes a floppy (FAT12)
# and two disks with one partition (FAT16, FAT32), each formatted by GLOS's own code with GLOS's boot code in it; here
# FreeDOS's KERNEL.SYS and COMMAND.COM (from FLOPPY, GLOS's setup floppy as built for the tests), a program
# that reports on COM1 and an AUTOEXEC.BAT that runs it are put on them. KERNEL.SYS is written in two
# pieces (a file is made before it and one removed), so the boot code follows a chain that is not in order.
# Out: DIR/floppy.img, DIR/disk.img and DIR/disk32.img.
set -e
floppy=$1 dir=$2
export MTOOLS_SKIP_CHECK=1
mkdir -p "$dir"
build/host/fdbootdisk say "$dir/HXSAY.COM"
printf '@ECHO OFF\r\nHXSAY\r\n' > "$dir/AUTOEXEC.BAT"
mcopy -n -o -i "$floppy" ::KERNEL.SYS "$dir/KERNEL.SYS"
mcopy -n -o -i "$floppy" ::FREEDOS/BIN/COMMAND.COM "$dir/COMMAND.COM"
head -c 3000 /dev/zero > "$dir/A.TMP"
cp "$dir/A.TMP" "$dir/B.TMP"
for kind in floppy disk disk32; do
    build/host/fdbootdisk $kind "$dir/$kind.img"
    image=$dir/$kind.img
    [ $kind = floppy ] || image=$image@@32256
    if [ $kind = disk32 ]; then                 # its root folder over several clusters, not next to each other
        for n in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36; do
            mcopy -i "$image" "$dir/AUTOEXEC.BAT" ::N$n.TMP
        done
    fi
    mcopy -i "$image" "$dir/A.TMP" "$dir/B.TMP" ::
    mdel -i "$image" ::A.TMP
    if [ $kind = disk32 ]; then                 # (FAT32 remembers where it last found room: forget it, so the
        printf '\377\377\377\377' |           #  kernel starts in the room A.TMP left)
            dd of="$dir/$kind.img" bs=1 seek=$((32256 + 512 + 492)) conv=notrunc status=none
    fi
    mcopy -i "$image" "$dir/KERNEL.SYS" "$dir/COMMAND.COM" "$dir/AUTOEXEC.BAT" "$dir/HXSAY.COM" ::
done
