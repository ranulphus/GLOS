#!/usr/bin/env bash
# mkfloppy.sh OUT.img KEYSDIR HOSTNAME [SIZE [CABLE]]: GLOS's setup floppy (docs/floppy.md), a disk that boots FreeDOS's
# kernel with GLOS as its shell, its network and SSH on, so that the rest is put on the PC's hard disk from another computer
# (tools/floppy/install.py). In the dev container (mtools), from the repository's root.
#
# SIZE: 1440 (a 3.5" 1.44 MB disk, the default), 1200 (5.25" 1.2 MB) or 720 (3.5" 720 KB). The smaller two carry
# the kernel packed (build/kernel/GLOSK.PAK, which GLOS.EXE unpacks), and the 720 KB disk no program of FreeDOS's.
#
# On it: FreeDOS 1.4's kernel (KERNEL.SYS) and, on the larger two, a few of its tools (as its own boot floppy has
# them: FreeDOS's programs, unchanged, under their own licences, the GPL for most); GLOS.EXE, GLOSK.BIN and
# GLOS's own COMMAND.COM, a GLOS.CFG with the PC's name, and KEYS\ (HOSTKEY: this floppy's own; AUTHKEYS: who may
# log in). No COMMAND.COM of FreeDOS's (since S4d, 2026-11-16): GLOS is DOS's shell (FDCONFIG.SYS's SHELL= line)
# and runs AUTOEXEC.BAT itself. No FDISK, FORMAT or SYS: a disk is laid out, formatted and made to start by GLOS's
# own DISK command (kernel/disk/diskcmd.c; since 2026-11-13).
set -euo pipefail
out=$1 keys=$2 host=$3 size=${4:-1440} cable=${5:-}
case $size in
    1440) fd=144m kernel=build/kernel/GLOSK.BIN
          keep="ATTRIB.COM XCOPY.EXE MEM.EXE MORE.EXE DELTREE.COM";;
    1200) fd=120m kernel=build/kernel/GLOSK.PAK
          keep="ATTRIB.COM XCOPY.EXE MEM.EXE MORE.EXE DELTREE.COM";;
    720)  fd=720k kernel=build/kernel/GLOSK.PAK
          keep="";;
    *) echo "mkfloppy: SIZE is 1440, 1200 or 720"; exit 1;;
esac
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
cache=${MGA_CACHE:-$HOME/.cache/mga-glide}
for f in "$root/build/ow/GLOS.EXE" "$root/$kernel" "$root/build/glos/COMMAND.COM" "$keys/HOSTKEY" "$keys/AUTHKEYS"; do
    [ -s "$f" ] || { echo "mkfloppy: no $f"; exit 1; }
done
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT
"$root/tools/setup/fetch.sh" "$FREEDOS_FLOPPY_URL" "$FREEDOS_FLOPPY_SHA256" "$cache/dl/$(basename "$FREEDOS_FLOPPY_URL")"
unzip -q -o "$cache/dl/$(basename "$FREEDOS_FLOPPY_URL")" $fd/x86BOOT.img -d "$tmp"
img=$tmp/glos.img
cp "$tmp/$fd/x86BOOT.img" "$img"
# FreeDOS's installer and its COMMAND.COM go: what stays is the kernel and a few tools.
mdir -b -i "$img" ::/FREEDOS/BIN | tr -d '\r' | while read -r f; do
    n=$(basename "$f" | tr a-z A-Z)
    case " $keep " in *" $n "*) ;; *) mdel -i "$img" "::/FREEDOS/BIN/$n" ;; esac
done
mdeltree -i "$img" ::/FREEDOS/NLS ::/FREEDOS/CONFIGS 2>/dev/null || true
mdel -i "$img" ::/FREEDOS/VERSION.FDI ::/FDAUTO.BAT ::/SETUP.BAT ::/FDCONFIG.SYS ::/AUTOEXEC.BAT 2>/dev/null || true
for f in FDCONFIG.SYS AUTOEXEC.BAT GLOS.CFG; do
    sed "s/@HOST@/$host/g" "$here/dos/$f" > "$tmp/$f"
done
# CABLE ("client COM1"): the PC is reached over a serial cable (docs/cable.md), with or without a network card.
[ -z "$cable" ] || printf 'cable = %s\r\n' "$cable" >> "$tmp/GLOS.CFG"
mcopy -o -i "$img" "$tmp/FDCONFIG.SYS" ::/FDCONFIG.SYS
mcopy -o -i "$img" "$tmp/AUTOEXEC.BAT" ::/AUTOEXEC.BAT
mmd -i "$img" ::/GLOS ::/GLOS/KEYS
mcopy -i "$img" "$root/build/ow/GLOS.EXE" ::/GLOS/GLOS.EXE
mcopy -i "$img" "$root/$kernel" ::/GLOS/GLOSK.BIN
mcopy -i "$img" "$root/build/glos/COMMAND.COM" ::/GLOS/COMMAND.COM    # GLOS's own, for programs that shell out (S4c)
mcopy -i "$img" "$tmp/GLOS.CFG" ::/GLOS/GLOS.CFG
mcopy -i "$img" "$keys/HOSTKEY" ::/GLOS/KEYS/HOSTKEY
mcopy -i "$img" "$keys/AUTHKEYS" ::/GLOS/KEYS/AUTHKEYS
mlabel -i "$img" ::GLOSSETUP >/dev/null 2>&1 || true
mkdir -p "$(dirname "$out")"
mv "$img" "$out"
echo "mkfloppy: $out ($(mdir -i "$out" -/ :: | grep 'bytes free' | sed 's/^ *//; s/  */ /g'))"
