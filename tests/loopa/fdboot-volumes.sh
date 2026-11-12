#!/bin/sh
# fdboot-volumes.sh FLOPPY DIR: the volumes for the test of GLOS's FAT boot code (kernel/disk/fdboot.S; the
# floppy suite's fdboot cases), in the dev container (mtools). build/host/fdbootdisk makes a floppy (FAT12)
# and a disk with one partition (FAT16), each formatted by GLOS's own code with GLOS's boot code in it; here
# FreeDOS's KERNEL.SYS and COMMAND.COM (from FLOPPY, GLOS's setup floppy as built for the tests), a program
# that reports on COM1 and an AUTOEXEC.BAT that runs it are put on them. KERNEL.SYS is written in two
# pieces (a file is made before it and one removed), so the boot code follows a chain that is not in order.
# Out: DIR/floppy.img and DIR/disk.img.
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
for kind in floppy disk; do
    build/host/fdbootdisk $kind "$dir/$kind.img"
    image=$dir/$kind.img
    [ $kind = disk ] && image=$image@@32256
    mcopy -i "$image" "$dir/A.TMP" "$dir/B.TMP" ::
    mdel -i "$image" ::A.TMP
    mcopy -i "$image" "$dir/KERNEL.SYS" "$dir/COMMAND.COM" "$dir/AUTOEXEC.BAT" "$dir/HXSAY.COM" ::
done
