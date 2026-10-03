#!/usr/bin/env bash
# GLOS's SSH layer against OpenSSH's client (milestones-m0-m4.md M3 item 6):
# tests/host/sshd.c on a free port, then ssh with the test client key.
#   tests/host/sshd_test.sh build/host/sshd
set -u
sshd=$1
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'kill $pid 2>/dev/null; rm -rf "$tmp"' EXIT
cp "$root/tests/keys/client" "$tmp/client" && chmod 600 "$tmp/client"
port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
"$sshd" "$port" "$root/tests/keys/hostkey" "$root/tests/keys/AUTHKEYS" > "$tmp/ready" 2> "$tmp/log" &
pid=$!
for _ in $(seq 50); do grep -q ready "$tmp/ready" 2>/dev/null && break; sleep 0.1; done

opts=(-p "$port" -i "$tmp/client" -o IdentitiesOnly=yes -o BatchMode=yes -o StrictHostKeyChecking=yes
      -o UserKnownHostsFile="$tmp/known" -o LogLevel=ERROR)
echo "[127.0.0.1]:$port $(cat "$root/tests/keys/hostkey.pub")" > "$tmp/known"
fails=0
check() {                                       # check NAME CONDITION...
    local name=$1; shift
    if "$@"; then echo "  ok   $name"; else echo "  FAIL $name"; fails=$((fails + 1)); fi
}

out=$(ssh "${opts[@]}" glos@127.0.0.1 echo hello from glos)
check "exec echo" [ "$out" = "hello from glos" ]
ssh "${opts[@]}" glos@127.0.0.1 exit 7; rc=$?
check "exit status 7" [ $rc -eq 7 ]
err=$(ssh "${opts[@]}" glos@127.0.0.1 err oops 2>&1 >/dev/null); rc=$?
check "stderr and status 3" [ "$err" = "oops" -a $rc -eq 3 ]
head -c 300000 /dev/urandom > "$tmp/in"
ssh "${opts[@]}" glos@127.0.0.1 cat < "$tmp/in" > "$tmp/back"
check "stdin echoed (300 KB)" cmp -s "$tmp/in" "$tmp/back"
ssh "${opts[@]}" glos@127.0.0.1 big 3000000 > "$tmp/big"
check "3 MB out, flow control" [ "$(stat -c %s "$tmp/big")" -eq 3000000 ]
ssh "${opts[@]}" -o RekeyLimit=64K glos@127.0.0.1 big 2000000 > "$tmp/rekey"
check "re-keying every 64 KB (2 MB)" [ "$(stat -c %s "$tmp/rekey")" -eq 2000000 ]
grep -q "kex done strict=1" "$tmp/log"
check "strict kex (CVE-2023-48795)" [ $? -eq 0 ]

jobs=()                                         # wait for these, not for the server
for i in 1 2 3 4 5 6; do ssh "${opts[@]}" glos@127.0.0.1 sleep 300 > "$tmp/par$i" & jobs+=($!); done
wait "${jobs[@]}"
check "6 connections at once" [ "$(cat "$tmp"/par* | grep -c slept)" -eq 6 ]
ssh "${opts[@]}" -o ControlMaster=yes -o ControlPath="$tmp/mux" -o ControlPersist=10 -fN glos@127.0.0.1
jobs=()
for i in 1 2 3 4; do ssh "${opts[@]}" -o ControlPath="$tmp/mux" glos@127.0.0.1 sleep 300 > "$tmp/mux$i" & jobs+=($!); done
wait "${jobs[@]}"
check "4 channels on one connection" [ "$(cat "$tmp"/mux? | grep -c slept)" -eq 4 ]
ssh "${opts[@]}" -o ControlPath="$tmp/mux" -O exit glos@127.0.0.1 2>/dev/null

ssh-keygen -q -t ed25519 -N "" -f "$tmp/stranger"
ssh -p "$port" -i "$tmp/stranger" -o IdentitiesOnly=yes -o BatchMode=yes -o StrictHostKeyChecking=yes \
    -o UserKnownHostsFile="$tmp/known" -o LogLevel=ERROR glos@127.0.0.1 echo no 2>/dev/null; rc=$?
check "a stranger's key refused" [ $rc -eq 255 ]
ssh "${opts[@]}" root@127.0.0.1 echo no 2>/dev/null; rc=$?
check "user root refused" [ $rc -eq 255 ]
out=$(ssh "${opts[@]}" glos@127.0.0.1 nosuch 2>&1); rc=$?
check "unknown command: 127" [ $rc -eq 127 -a "$out" = "unknown command" ]
echo "sshd_test: $fails failures"
[ $fails -eq 0 ] || { echo "--- server log"; tail -20 "$tmp/log"; exit 1; }
