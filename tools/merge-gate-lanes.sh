#!/usr/bin/env bash
# merge-gate-lanes.sh - GLOS's merge gate on THIS machine in lanes: the host tests and the Loop A suites as
# `tools/merge-gate.sh remote` runs them on devserver (tools/merge-gate-there.sh: built and run in the dev
# container, GATE_LANES suites at once), then the four suites that run outside the container, one after another.
#
#   tools/merge-gate-lanes.sh                      the whole gate, of this checkout's HEAD as it is built here
#   ONLY="loopa-m1 loopa-pkg" tools/merge-gate-lanes.sh   those suites alone (and no host suites)
#
# Why: `tools/merge-gate.sh` by itself runs one suite after another (about six hours on a 20-core PC, whatever
# JOBS is: most suites have few cases). In four lanes of two emulators the same gate took 141 minutes plus 21 for
# the last four suites (2026-11-24, the first gate run this way, after the owner's word that gates may run on
# this PC as well as devserver). Other Loop A runs stay off the machine meanwhile; ~/RetroGPU's checkout is read
# as it is (have it at the main the gate should test, and its gl/build/lib built).
# Environment: GATE_LANES (4), JOBS (2: a suite's emulators), LOOPA_MAX_VMS (8), MGA_NICE (10), LOOPA_MIN_FREE_MB.
# Result: out/gate/summary.txt and out/gate/here.txt; the last line of the output is gate=0 or gate=1, and the
# exit status says the same.
set -u
root=$(pwd -P)
rgpu=${RETROGPU:-$HOME/RetroGPU}
HOST_SUITES="host-test gui-goldens"
ALL=$(sed -n '/^LOOPA_SUITES="/,/"$/p' tools/merge-gate.sh | tr -d '"' | sed 's/^LOOPA_SUITES=//')
HERE="loopa-pkg loopa-https loopa-vnc loopa-sshc"
if [ -n "${ONLY:-}" ]; then                    # (ONLY="loopa-m1 loopa-pkg": those alone, and no host suites)
    HOST_SUITES=
    ALL=$(for s in $ALL; do case " $ONLY " in *" $s "*) echo "$s";; esac; done)
    HERE=$(for s in $HERE; do case " $ONLY " in *" $s "*) echo "$s";; esac; done)
fi
there=
for s in $ALL; do
    case " loopa-pkg loopa-https loopa-vnc loopa-sshc " in *" $s "*) ;; *) there="$there $s";; esac
done
rshow() { git -C "$rgpu" show "HEAD:$1"; }
box86_commit=$(rshow tools/setup/versions.mk | sed -n 's/^BOX86_COMMIT *:= *//p')
key=$( { echo "$box86_commit"; rshow tools/86box/series;
         rshow tools/86box/series | grep -v '^#' | while read -r p; do [ -n "$p" ] && rshow "tools/86box/patches/$p"; done; } \
       | sha256sum | cut -c1-16)
[ -x "$HOME/.cache/mga-glide/86box/bin/86Box-$key" ] || { echo "no emulator 86Box-$key"; echo gate=1; exit 1; }
mkdir -p "$HOME/.cache/mga-glide/86box-glosgate/bin"
ln -sfn "../../86box/bin/86Box-$key" "$HOME/.cache/mga-glide/86box-glosgate/bin/86Box"
ln -sfn ../86box/roms "$HOME/.cache/mga-glide/86box-glosgate/roms"
mkdir -p out/gate && find out/gate -type f -delete
echo "gate-lanes-here: $(git log -1 --format='%h %s' | cut -c1-70) with RetroGPU $(git -C "$rgpu" log -1 --format=%h), emulator $key"
MGA_NICE=${MGA_NICE:-10} LOOPA_MAX_VMS=${LOOPA_MAX_VMS:-8} LOOPA_MIN_FREE_MB=${LOOPA_MIN_FREE_MB:-8192} LOOPA_OOM_FIRST=1 \
    GATE_LANES=${GATE_LANES:-4} JOBS=${JOBS:-2} HOST_SUITES="$HOST_SUITES" LOOPA_SUITES="$(echo $there)" \
    bash tools/merge-gate-there.sh
bad=$(cat out/gate/done 2>/dev/null || echo 1)
cat out/gate/summary.txt
# The four that run outside the container, one after another (their drivers start containers of their own).
for s in $HERE; do
    s0=$(date +%s) rc=0
    make -s JOBS=3 "$s" > "out/gate/$s.log" 2>&1 || rc=$?
    k=$(grep -c ' SKIP' "out/gate/$s.log")      # (skips said where there are any)
    l="$s (here) exit=$rc $(( ($(date +%s) - s0) / 60 )) min pass=$(grep -c ' PASS' "out/gate/$s.log") fail=$(grep -c ' FAIL' "out/gate/$s.log")$([ "$k" -gt 0 ] && echo " skip=$k")"
    echo "$l" | tee -a out/gate/here.txt
    if [ $rc -ne 0 ]; then
        bad=1
        grep ' FAIL\|^    ' "out/gate/$s.log" | cut -c1-600 | head -8
    fi
done
echo "gate=$bad"
exit "$bad"
