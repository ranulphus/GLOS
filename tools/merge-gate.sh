#!/usr/bin/env bash
# merge-gate.sh - GLOS's merge gate: the host tests and every Loop A suite.
#
#   tools/merge-gate.sh                   here: one suite after another (the host to itself: other Loop A
#                                         runs beside it slow the guests and fail its timings)
#   tools/merge-gate.sh remote [COMMIT]   on another machine: devserver by default (the owner's choice,
#                                         2026-11-12: at most 8 emulators at once there, niced, in Docker)
#
# Remote: COMMIT (default HEAD; uncommitted changes are not sent) and RetroGPU's HEAD are sent; what the
# suites read is kept in step there (rsync: the toolchains, the emulator and its ROMs, Loop A's and GLOS's
# caches, the games' inputs built here); everything is built in the dev container; the Loop A suites run
# in lanes under that machine's limit on emulators; the logs come back to out/gate-remote/. The run goes on
# there if the connection drops, and this script picks it up again. Exit status: 0 if every suite passed.
# A few suites stay here even then (HERE_SUITES): their test driver starts containers of its own and wants
# Python modules the dev container hasn't got, and the other machine's own system is left alone. They are
# short, and run here one after another while the rest runs there, in a checkout of their own of the same
# commit (GATE_HERE_DIR, ~/wt-glos-gate: a git worktree), so that work goes on in this one meanwhile.
#
# There, GLOS is ~/GLOS-gate and RetroGPU is ~/RetroGPU (never ~/GLOS: RetroGPU's regress-remote.sh keeps
# ~/GLOS/include in step with this machine's, under any checkout there).
#
# Environment: ONLY="loopa-ssh loopa-api" (those suites alone), JOBS (a suite's emulators at once, 3);
# remote: GATE_HOST (retro@devserver), GATE_LANES (suites at once, 4), LOOPA_MAX_VMS (8), MGA_NICE (10).
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd -P)
cd "$root"
rgpu=${RETROGPU:-$HOME/RetroGPU}

# The suites, the long ones first (the lanes fill better). The first two are host tests.
HOST_SUITES="host-test gui-goldens"
LOOPA_SUITES="loopa-sess loopa-api loopa-ssh loopa-desk loopa-win loopa-display loopa-games loopa-glwin
    loopa-audio loopa-m1 loopa-apps loopa-gpu loopa-gui loopa-native loopa-control loopa-config loopa-console
    loopa-input loopa-time loopa-m5exit loopa-thr loopa-vnc loopa-sdlwin loopa-netapp loopa-gdbprog loopa-pkg
    loopa-joy loopa-cache loopa-https loopa-sshc loopa-floppy"
HERE_SUITES="loopa-pkg loopa-https loopa-vnc loopa-sshc"
if [ -n "${ONLY:-}" ]; then
    HOST_SUITES=$(for s in $HOST_SUITES; do case " $ONLY " in *" $s "*) echo "$s";; esac; done)
    LOOPA_SUITES=$(for s in $LOOPA_SUITES; do case " $ONLY " in *" $s "*) echo "$s";; esac; done)
fi
jobs=${JOBS:-3}

line() {    # SUITE RC SECONDS LOG: the summary's line for a suite
    echo "$1 exit=$2 $(( $3 / 60 )) min pass=$(grep -c ' PASS' "$4" || true) fail=$(grep -c ' FAIL' "$4" || true)"
}

# ---- here ---------------------------------------------------------------------
if [ "${1:-}" != remote ]; then
    log=out/gate; mkdir -p $log
    t0=$(date +%s) bad=0
    "$rgpu/tools/dev" make -s all > $log/build.log 2>&1 || { echo "build failed: $log/build.log"; exit 1; }
    for s in $HOST_SUITES $LOOPA_SUITES; do
        s0=$(date +%s) rc=0
        make -s JOBS="$jobs" "$s" > "$log/$s.log" 2>&1 || rc=$?
        line "$s" $rc $(( $(date +%s) - s0 )) "$log/$s.log"
        if [ $rc -ne 0 ]; then
            bad=1
            grep ' FAIL\|^    ' "$log/$s.log" | cut -c1-600 | head -8 || true
        fi
    done
    echo "total $(( ($(date +%s) - t0) / 60 )) min"
    exit $bad
fi

# ---- on another machine ---------------------------------------------------------
commit=$(git rev-parse --verify "${2:-HEAD}^{commit}")
host=${GATE_HOST:-retro@devserver}
lanes=${GATE_LANES:-4} vms=${LOOPA_MAX_VMS:-8} niceness=${MGA_NICE:-10}
[ "$(cd "$rgpu" && pwd -P)" = "$HOME/RetroGPU" ] || { echo "merge-gate: RetroGPU must be ~/RetroGPU for a remote run" >&2; exit 1; }
rcommit=$(git -C "$rgpu" rev-parse HEAD)
if [ -z "${2:-}" ] && [ -n "$(git status --porcelain --untracked-files=no)" ]; then
    echo "merge-gate: uncommitted changes are not sent; running $(git log -1 --format=%h "$commit")" >&2
fi
[ -z "$(git -C "$rgpu" status --porcelain --untracked-files=no)" ] \
    || echo "merge-gate: RetroGPU has uncommitted changes, which are not sent; its $(git -C "$rgpu" log -1 --format=%h)" >&2

there= here=
for s in $LOOPA_SUITES; do
    case " $HERE_SUITES " in *" $s "*) here="$here $s";; *) there="$there $s";; esac
done

# What the suites read, in step (paths relative to both homes).
rshow() { git -C "$rgpu" show "$rcommit:$1"; }
box86_commit=$(rshow tools/setup/versions.mk | sed -n 's/^BOX86_COMMIT *:= *//p')
key=$( { echo "$box86_commit"; rshow tools/86box/series;
         rshow tools/86box/series | grep -v '^#' | while read -r p; do [ -n "$p" ] && rshow "tools/86box/patches/$p"; done; } \
       | sha256sum | cut -c1-16)
box=.cache/mga-glide/86box/bin/86Box-$key
[ -x "$HOME/$box" ] || { echo "merge-gate: no emulator built for RetroGPU $(git -C "$rgpu" log -1 --format=%h) here (make 86box there)" >&2; exit 1; }
optdir() { sed -n "s|^$1 *?= *\$(HOME)/||p" config.mk; }
paths=("$(optdir WATCOM)" "$(optdir DJGPP_PREFIX)" "$box"
       .cache/mga-glide/86box/roms .cache/mga-glide/loopa .cache/mga-glide/dl .cache/mga-glide/games
       .cache/mga-glide/glos FifthWheel/build/dos/FWHEEL.EXE FifthWheel/build/data/WORLD.PAK
       RetroGPU/gl/build/exe RetroGPU/gl/build/doom)
present=()
for p in "${paths[@]}"; do [ -e "$HOME/$p" ] && present+=("$p"); done
echo "merge-gate: syncing ${#present[@]} paths to $host"
ssh -o BatchMode=yes "$host" '
    for r in glos-gate retrogpu-gate; do [ -d $r.git ] || git init -q --bare $r.git; done
    [ -d RetroGPU/.git ] || { mkdir -p RetroGPU && git -C RetroGPU init -q && git -C RetroGPU remote add origin "$HOME/retrogpu-gate.git"; }
    [ -d GLOS-gate/.git ] || { mkdir -p GLOS-gate && git -C GLOS-gate init -q && git -C GLOS-gate remote add origin "$HOME/glos-gate.git"; }'
( cd "$HOME" && rsync -aR --delete --exclude __pycache__ --exclude '/.cache/mga-glide/glos/bin-*' \
      --exclude /.cache/mga-glide/glos/trace "${present[@]}" "$host:" )
git push -q --force "$host:glos-gate.git" "$commit:refs/heads/gate"
git -C "$rgpu" push -q --force "$host:retrogpu-gate.git" "$rcommit:refs/heads/gate"

# The run there: built in the dev container, then the Loop A suites in lanes; out/gate/done holds its result.
echo "merge-gate: $(git log -1 --format='%h %s' "$commit" | cut -c1-70) with RetroGPU $(git -C "$rgpu" log -1 --format=%h)" \
     "on $host (lanes $lanes, emulators $vms, nice $niceness)"
ssh -o BatchMode=yes "$host" "set -e
    git -C RetroGPU fetch -q origin gate && git -C RetroGPU checkout -q -f --detach $rcommit
    cd GLOS-gate
    git fetch -q origin gate && git checkout -q -f --detach $commit && git clean -fdq
    mkdir -p out/gate && find out/gate -type f -delete
    mkdir -p \$HOME/.cache/mga-glide/86box-glosgate/bin
    ln -sfn ../../86box/bin/86Box-$key \$HOME/.cache/mga-glide/86box-glosgate/bin/86Box
    ln -sfn ../86box/roms \$HOME/.cache/mga-glide/86box-glosgate/roms
    MGA_NICE=$niceness LOOPA_MAX_VMS=$vms GATE_LANES=$lanes JOBS=$jobs HOST_SUITES='$(echo $HOST_SUITES)' \
        LOOPA_SUITES='$(echo $there)' \
        setsid nohup bash tools/merge-gate-there.sh > out/gate-run.log 2>&1 < /dev/null &"
dest=$root/out/gate-remote/${host#*@}-$(date +%Y%m%d-%H%M%S)
mkdir -p "$dest"
bad=0
if [ -n "$here" ]; then                         # (meanwhile, here)
    hdir=${GATE_HERE_DIR:-$HOME/wt-glos-gate}
    if [ -e "$hdir/.git" ]; then
        git -C "$hdir" checkout -q -f --detach "$commit" && git -C "$hdir" clean -fdq
    else
        git worktree add -q --detach "$hdir" "$commit"
    fi
    ( cd "$hdir" && "$rgpu/tools/dev" make -s all ) > "$dest/here-build.log" 2>&1 \
        || { echo "merge-gate: the build here failed ($dest/here-build.log)"; bad=1; }
    for s in $here; do
        s0=$(date +%s) rc=0
        ( cd "$hdir" && make -s JOBS="$jobs" "$s" ) > "$dest/$s.log" 2>&1 || rc=$?
        line "$s (here)" $rc $(( $(date +%s) - s0 )) "$dest/$s.log" | tee -a "$dest/here.txt"
        if [ $rc -ne 0 ]; then
            bad=1
            grep ' FAIL\|^    ' "$dest/$s.log" | cut -c1-600 | head -8 | tee -a "$dest/here.txt" || true
        fi
    done
fi
rc=
while [ -z "$rc" ]; do                          # (a dropped connection: the run goes on there, ask again)
    rc=$(ssh -o BatchMode=yes -o ServerAliveInterval=30 "$host" \
             'while [ ! -f GLOS-gate/out/gate/done ]; do sleep 15; done; cat GLOS-gate/out/gate/done' 2>/dev/null) || rc=
    [ -n "$rc" ] || sleep 20
done
rsync -a "$host:GLOS-gate/out/gate/" "$host:GLOS-gate/out/gate-run.log" "$dest/"
cat "$dest/summary.txt"
[ ! -f "$dest/here.txt" ] || cat "$dest/here.txt"
echo "merge-gate: logs in $dest"
[ "$rc" = 0 ] && [ $bad = 0 ]
