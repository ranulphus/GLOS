#!/usr/bin/env bash
# sdk/sshc against OpenSSH (M10 step D): an sshd of the user's own on a free port of 127.0.0.1 (no root: logins by
# key, for this user only), with a new host key and client key, then build/host/sshc_test against it. Also: the
# key sshc_test writes is read by OpenSSH's ssh-keygen, and gives the same public key.
set -euo pipefail
bin=$1
work=$(mktemp -d)
trap 'kill "$pid" 2>/dev/null || true; rm -rf "$work"' EXIT
ssh-keygen -q -t ed25519 -N '' -f "$work/hostkey"
ssh-keygen -q -t ed25519 -N '' -f "$work/client" -C client
cp "$work/client.pub" "$work/authorized_keys"
port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
sftp=$(for f in /usr/lib/openssh/sftp-server /usr/libexec/openssh/sftp-server /usr/libexec/sftp-server; do
         [ -x "$f" ] && echo "$f" && break; done)
cat > "$work/sshd_config" <<EOF
Port $port
ListenAddress 127.0.0.1
HostKey $work/hostkey
AuthorizedKeysFile $work/authorized_keys
PasswordAuthentication no
KbdInteractiveAuthentication no
UsePAM no
StrictModes no
PidFile $work/sshd.pid
Subsystem sftp $sftp
LogLevel ERROR
EOF
/usr/sbin/sshd -D -e -f "$work/sshd_config" 2> "$work/sshd.log" &
pid=$!
for i in $(seq 50); do (exec 3<>/dev/tcp/127.0.0.1/$port) 2>/dev/null && break; sleep 0.1; done
fp=$(ssh-keygen -l -f "$work/hostkey.pub" | awk '{print $2}')
rc=0
"$bin" "$port" "$(id -un)" "$work/client" "$fp" "$work" || rc=1
chmod 600 "$work/made.key"   # (DOS has no modes; OpenSSH wants them)
if [ "$(ssh-keygen -y -f "$work/made.key" | cut -d' ' -f1-2)" != "$(cut -d' ' -f1-2 "$work/made.PUB")" ]; then
  echo "sshc_test: OpenSSH reads the key written as another key"; rc=1
fi
[ $rc = 0 ] || { echo "sshd's log:"; cat "$work/sshd.log"; }
exit $rc
