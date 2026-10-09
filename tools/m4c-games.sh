#!/bin/bash
# M4c's programs under GLOS as the DOS shell (the glosshell boot, so every
# program runs in GLOS's system VM with GLOS as its DPMI host):
#   conform   MGA-Glide's conformance tests on G100, G200, G400 and G450
#   replay    MGA-Glide's GTA and Screamer Rally replays on G200, G400 and G450
#   survey    GTA and Screamer Rally themselves, against runs without GLOS
#   dosbench  DOSBench's Loop A job: BENCHGL (DJGPP), BENCHG (DOS/4GW), DBMENU
# Each part prints "### PART ... PASS|FAIL"; the exit code is 1 if any failed.
#
#   tools/m4c-games.sh [PART ...]          (default: all four)
#
# M4C_MGA is an MGA-Glide tree at deps.mk's pin with its runtime built
# (default $MGA_GLIDE); its out/ takes the runs. The replays and the games
# need the owner's traces and fixtures in ~/.cache/mga-glide (skipped, and
# reported, without them). `make glos-cache` (a prerequisite of make
# loopa-m4c) puts GLOS.EXE and GLOSK.BIN where the runs take them from.
# M4C_DIRECT=1 runs every program in direct mode (IOPL 3, M4e): GLOS.CFG's
# [shell] options = /DIRECT, from the same place. GLOS_BIN: another such
# place (inside the cache, which the dev container mounts).
set -u
GLOS=$(cd "$(dirname "$0")/.." && pwd)
CACHE=${MGA_CACHE:-$HOME/.cache/mga-glide}
MGA_TREE=${M4C_MGA:-${MGA_GLIDE:-$HOME/MGA-Glide}}
DOSBENCH=${DOSBENCH:-$HOME/DOSBench}
BIN=${GLOS_BIN:-$CACHE/glos/bin}       # make glos-cache's (GLOS_CACHE=)
export LOOPA_EXTRA_ARGS="--boot-cfg glosshell --file $BIN/GLOS.EXE=/TEST/GLOS.EXE --file $BIN/GLOSK.BIN=/TEST/GLOSK.BIN"
VARIANT=glos
if [ "${M4C_DIRECT:-0}" = 1 ]; then
    LOOPA_EXTRA_ARGS="$LOOPA_EXTRA_ARGS --file $BIN/direct/GLOS.CFG=/TEST/GLOS.CFG"
    VARIANT=glos-direct
fi
fail=0

result() {      # PART NAME OK DETAIL
    echo "### $1 $2 $([ "$3" = 1 ] && echo PASS || echo FAIL) $4"
    [ "$3" = 1 ] || fail=1
}

# A run under GLOS that went wrong in the host, whatever the program said.
glos_trouble() {
    grep -l "GLOS-PANIC\|GLOS-CRASH\|DPMI-UNIMPL" "$@" 2>/dev/null | head -3 | tr '\n' ' '
}

part_conform() {
    local c log pass bad
    for c in g100 g200 g400 g450; do
        log=$MGA_TREE/out/conform-glos-$c.log
        (cd "$MGA_TREE" && MGA_CARD=$c timeout 3300 tools/dev python3 tools/conform/run.py check > "$log" 2>&1)
        local st=$?
        pass=$(grep -c ' PASS ' "$log")
        bad=$(glos_trouble "$MGA_TREE"/out/conform/t*/serial.log)
        result conform $c "$([ $st = 0 ] && [ "$pass" = 27 ] && [ -z "$bad" ] && echo 1)" "exit=$st pass=$pass $bad"
    done
}

part_replay() {
    local c t log
    for t in "gta gta-voodoo-300.bin gta.ovl 60,150,240,295" "sr sr-389.bin sr.ovl 60,150,250,300,340,380"; do
        set -- $t
        if [ ! -f "$CACHE/traces/$2" ]; then
            result replay "$1" 0 "no trace $CACHE/traces/$2"
            continue
        fi
        for c in g200 g400 g450; do
            log=$MGA_TREE/out/replay-glos-$1-$c.log
            (cd "$MGA_TREE" && MGA_CARD=$c timeout 3000 tools/dev python3 tools/games/replay.py "$CACHE/traces/$2" \
                --ovl-ref "$CACHE/fixtures/ovl/$3" --frames "$4" --name "$1-$c" > "$log" 2>&1)
            local st=$?
            result replay "$1-$c" "$([ $st = 0 ] && tail -1 "$log" | grep -q ': PASS' && echo 1)" "exit=$st $(tail -1 "$log")"
        done
    done
}

# The survey's variants carry GLOS themselves: its base runs must not get it
# from LOOPA_EXTRA_ARGS. Each run must end as the game does (MGAGLIDE's
# exit_after) as well as match the other.
part_survey() {
    local out j bad=""
    env -u LOOPA_EXTRA_ARGS MGA_GLIDE=$MGA_TREE python3 "$GLOS/tools/survey/survey.py" run --suite gta \
        --suite screamer-rally --variant base --variant $VARIANT
    out=$(env -u LOOPA_EXTRA_ARGS MGA_GLIDE=$MGA_TREE python3 "$GLOS/tools/survey/survey.py" diff --suite gta \
        --suite screamer-rally --variant $VARIANT)
    local st=$?
    echo "$out"
    for j in gta/base gta/$VARIANT screamer-rally/base screamer-rally/$VARIANT; do
        grep -q "MGL-EXIT frames=" "$GLOS/out/survey/$j/serial.log" 2>/dev/null || bad="$bad $j:no-exit"
        [ "$(cat "$GLOS/out/survey/$j/status" 2>/dev/null)" = PASS ] || bad="$bad $j:status"
    done
    bad="$bad $(glos_trouble "$GLOS"/out/survey/*/$VARIANT/serial.log)"
    result survey games "$([ $st = 0 ] && [ -z "${bad// }" ] && echo 1)" \
        "$(echo "$out" | tr -s ' ' | tr '\n' ';')$bad"
}

part_dosbench() {
    local st
    (cd "$DOSBENCH" && python3 tools/run.py loopa --card g450)
    st=$?
    local bad
    bad=$(glos_trouble "$DOSBENCH/out/loopa/g450/serial.log")
    result dosbench g450 "$([ $st = 0 ] && [ -z "$bad" ] && echo 1)" "exit=$st $bad"
}

for p in ${@:-conform replay survey dosbench}; do
    "part_$p"
done
exit $fail
