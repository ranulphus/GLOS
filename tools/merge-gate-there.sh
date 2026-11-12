#!/usr/bin/env bash
# merge-gate-there.sh - the far end of `tools/merge-gate.sh remote`: run in the GLOS checkout on the other
# machine, detached, with HOST_SUITES, LOOPA_SUITES, GATE_LANES, JOBS, LOOPA_MAX_VMS and MGA_NICE in the
# environment. Everything is built, and the suites run, in the dev container (nothing is built or run on
# that machine's own system). Logs in out/gate/; out/gate/done holds 0 or 1 at the end.
set -u
cd "$(dirname "$0")/.." || exit 1
export MGA_EXTRA_DIR=$HOME MGA_NICE LOOPA_MAX_VMS
export BOX86_DIR=$HOME/.cache/mga-glide/86box-glosgate
dev=$HOME/RetroGPU/tools/dev
log=out/gate
t0=$(date +%s)
bad=0

line() {
    echo "$1 exit=$2 $(( $3 / 60 )) min pass=$(grep -c ' PASS' "$4") fail=$(grep -c ' FAIL' "$4")"
}

# Built first, one thing at a time: RetroGPU's DOS tools, GLOS, and what each suite wants built (its make
# target with the test driver told to leave at once), so that the lanes build nothing between them. The host
# tests run here for real. (RetroGPU's MOUSETST is the input suite's.)
{ "$dev" make -s -C "$HOME/RetroGPU" dostests build/djgpp/MOUSETST.EXE && "$dev" make -s all; } > $log/build.log 2>&1 \
    || { echo "build failed (build.log)" > $log/summary.txt; echo 1 > $log/done; exit 1; }
for s in $HOST_SUITES; do
    s0=$(date +%s) rc=0
    "$dev" make -s "$s" > "$log/$s.log" 2>&1 || rc=$?
    line "$s" $rc $(( $(date +%s) - s0 )) "$log/$s.log" >> $log/summary.txt
    [ $rc -eq 0 ] || bad=1
done
for s in $LOOPA_SUITES; do
    LOOPA_BUILD_ONLY=1 "$dev" make -s "$s" > "$log/build-$s.log" 2>&1 \
        || { echo "$s: its inputs did not build (build-$s.log)" >> $log/summary.txt; bad=1; }
done

one() {     # a suite, in a lane: its own recipe alone. What it depends on was built above and is not made
            # again here (-o): several of those targets write files the suites share (the toolkit's goldens,
            # DOS-GL's programs copied in), and a suite in the next lane would find one missing or half written.
    local s=$1 s0 rc=0 old
    s0=$(date +%s)
    old=$("$dev" make -s -pn "$s" 2>/dev/null | sed -n "s/^$s: //p" | head -1 | tr ' ' '\n' | sed -n 's/^..*/-o &/p' | tr '\n' ' ')
    "$dev" make -s JOBS="$JOBS" $old "$s" > "$log/$s.log" 2>&1 || rc=$?
    line "$s" $rc $(( $(date +%s) - s0 )) "$log/$s.log" >> $log/summary.txt
    [ $rc -eq 0 ] || { grep ' FAIL\|^    ' "$log/$s.log" | cut -c1-600 | head -8 >> $log/summary.txt; return 1; }
}
export -f one line
export dev log JOBS
printf '%s\n' $LOOPA_SUITES | xargs -P "$GATE_LANES" -I{} bash -c 'one {}' || bad=1
echo "total $(( ($(date +%s) - t0) / 60 )) min" >> $log/summary.txt
echo $bad > $log/done
