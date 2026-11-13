#!/bin/sh
# shell_test.sh PROGRAM DIR: each script in tests/shell run by PROGRAM (tests/host/shell_test.c) against a new,
# empty C: under DIR, its transcript compared with the one kept beside it (NAME.out). With SHELL_TEST_KEEP=1
# the transcripts made are written over the kept ones (after a change that is meant: read the difference).
prog=$1 dir=$2 bad=0 n=0
for script in tests/shell/*.txt; do
    name=$(basename "$script" .txt)
    rm -rf "$dir/$name"
    mkdir -p "$dir/$name/C"
    "$prog" "$dir/$name" "$script" > "$dir/$name.out" 2>&1 || { echo "shell_test: $name: the program failed"; bad=$((bad + 1)); }
    if [ "${SHELL_TEST_KEEP:-}" = 1 ]; then
        cp "$dir/$name.out" "tests/shell/$name.out"
    elif ! diff -u "tests/shell/$name.out" "$dir/$name.out" > "$dir/$name.diff" 2>&1; then
        echo "shell_test: $name: the transcript differs:"
        head -40 "$dir/$name.diff"
        bad=$((bad + 1))
    fi
    n=$((n + 1))
done
echo "shell_test: $bad failures ($n scripts)"
[ $bad = 0 ]
