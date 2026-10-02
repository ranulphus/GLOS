#!/usr/bin/env bash
# GLOS M1: run GLOS.EXE /ROUNDTRIP /GDB in Loop A with COM2 bridged to TCP,
# attach gdb to the kernel's stub and run tests/loopa/gdb-smoke.gdb. Runs in
# the dev container (make loopa-gdb). Passes when gdb stopped at the
# breakpoint, single-stepped, read a kernel variable and detached, and the
# kernel then finished its round trip. Output: out/m1-gdb/ and gdb.log there.
set -uo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
mga=${MGA_GLIDE:-$HOME/MGA-Glide}
out=$root/out/m1-gdb
cd "$root"
rm -rf "$out"
python3 "$mga/tools/loopa/run.py" --name m1-gdb --out "$out" --com2 \
  --file build/ow/GLOS.EXE=/TEST/GLOS.EXE --file build/kernel/GLOSK.BIN=/TEST/GLOSK.BIN \
  --cmd "SERSAY HX-START gdb" --cmd "C:\\TEST\\GLOS.EXE /ROUNDTRIP /GDB" --cmd "SERSAY HX-DONE 0" \
  --idle 180 --timeout 400 > /dev/null 2>&1 &
job=$!
for _ in $(seq 1 600); do
  [ -s "$out/com2.port" ] && grep -q "GLOS-GDB waiting" "$out/serial.log" 2>/dev/null && break
  sleep 0.5
done
port=$(cat "$out/com2.port" 2>/dev/null)
[ -n "$port" ] || { echo "gdb-loopa: no COM2 bridge"; wait $job; exit 1; }
timeout 300 gdb -q -batch -ex "set architecture i386" -ex "file build/kernel/glosk.elf" \
  -ex "target remote localhost:$port" -x tests/loopa/gdb-smoke.gdb > "$out/gdb.log" 2>&1
wait $job
fail=0
check() { if grep -q "$2" "$3"; then echo "  gdb $1: ok"; else echo "  gdb $1: MISSING ($2)"; fail=1; fi; }
check attach "eip *0x" "$out/gdb.log"
check breakpoint "Breakpoint 1, timer_start" "$out/gdb.log"
check variable '\$1 = 0x' "$out/gdb.log"
check detach "Detaching\|detached" "$out/gdb.log"
check roundtrip "GLOS-RING0 ticks=" "$out/serial.log"
check exit "GLOS-EXIT code=0" "$out/serial.log"
[ $fail = 0 ] && echo "gdb-loopa: PASS" || { echo "gdb-loopa: FAIL (see $out/gdb.log)"; exit 1; }
