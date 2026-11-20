#!/usr/bin/env bash
# soak.sh - one Loop A suite's cases of one machine, several copies at once, round after round: for a fault that
# comes once in many runs and under load (the drive that was "not ready" on the 486DX2's profile in the
# twenty-third gate).
#
#   tools/soak.sh SUITE PROFILE COPIES ROUNDS      e.g. tools/soak.sh apps 486dx2 8 5
#
# Each copy is tests/loopa/jobs.py SUITE --profile PROFILE with a name of its own (LOOPA_SUFFIX=-sN: its PC and
# its folder under out/ are apart from the others'). Built first (make all dos-tests). What each round's copies
# said goes to out/soak/ROUND-COPY.log; at the end: how many passed, and every line of the kernel's about a drive
# given up on (GLOS-DOS critical, GLOS-DISK state) in any copy's serial log. Exit status 0 if all passed.
set -u
suite=${1:?the suite (apps, sess, api ...)} profile=${2:?the machine (486dx2, bf6 ...)} copies=${3:-8} rounds=${4:-3}
root=$(cd "$(dirname "$0")/.." && pwd -P)
cd "$root"
mkdir -p out/soak
rm -f out/soak/*.log
pass=0 fail=0
for r in $(seq 1 "$rounds"); do
    pids=()
    for k in $(seq 1 "$copies"); do
        LOOPA_SUFFIX="-s$k" python3 tests/loopa/jobs.py "$suite" --profile "$profile" -j 1 > "out/soak/$r-$k.log" 2>&1 &
        pids+=($!)
    done
    for p in "${pids[@]}"; do wait "$p"; done
    p=$(cat out/soak/$r-*.log | grep -c ' PASS') f=$(cat out/soak/$r-*.log | grep -c ' FAIL\|SETUP-ERROR\|HANG')
    pass=$((pass + p)) fail=$((fail + f))
    echo "soak: round $r of $rounds: $p passed, $f failed ($(date +%H:%M))"
    grep -h ' FAIL\|^    ' out/soak/$r-*.log | cut -c1-300 | head -20
    for k in $(seq 1 "$copies"); do             # (a failed copy's logs kept apart from the next round's)
        if grep -q ' FAIL\|SETUP-ERROR\|HANG' "out/soak/$r-$k.log"; then
            for d in out/"$suite"-*-s"$k"; do [ -d "$d" ] && rm -rf "$d.round$r" && cp -r "$d" "$d.round$r"; done
        fi
    done
    grep -l 'GLOS-DOS critical\|GLOS-DISK state' out/*-s*/serial.log 2>/dev/null | while read -r s; do
        echo "soak: $s:"; grep -a 'GLOS-DOS critical\|GLOS-DISK state' "$s" | cut -c1-300 | head -8
    done
done
echo "soak: $pass passed, $fail failed in $rounds rounds of $copies"
[ "$fail" = 0 ] && [ "$pass" -gt 0 ]
