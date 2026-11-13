#!/usr/bin/env bash
# cable.sh - this computer's end of GLOS's cable link (docs/cable.md): IP over a serial cable, by SLIP.
#
#   tools/cable.sh up [DEVICE] [BAUD]    the link up as the server (192.168.250.1), the PC being the client
#                                        (192.168.250.2: [net] cable = client COM1 in its GLOS.CFG, or
#                                        NETWORK CLIENT COM1 there). DEVICE: /dev/ttyS0; BAUD: 115200
#   tools/cable.sh down                  ... and down again: the serial port is free
#   tools/cable.sh status
#
# Needs root for the line discipline and the interface (sudo slattach, sudo ip): nothing else is changed, and
# nothing is left behind after `down`. Then: ssh glos@192.168.250.2, tools/floppy/install.py 192.168.250.2 ...
set -euo pipefail
{
run=${XDG_RUNTIME_DIR:-/tmp}/glos-cable
dev=${2:-/dev/ttyS0} baud=${3:-115200}
case ${1:-} in
up)
    [ -e "$run.pid" ] && sudo kill -0 "$(cat "$run.pid")" 2>/dev/null && { echo "cable: up already (tools/cable.sh down first)"; exit 1; }
    before=$(ls /sys/class/net | grep '^sl' || true)
    sudo modprobe slip
    sudo setsid slattach -L -p slip -s "$baud" "$dev" </dev/null >/dev/null 2>&1 &
    for i in $(seq 1 50); do
        ifc=$(comm -13 <(echo "$before") <(ls /sys/class/net | grep '^sl' || true) | head -1)
        [ -n "$ifc" ] && break
        sleep 0.1
    done
    [ -n "${ifc:-}" ] || { echo "cable: slattach made no interface on $dev"; sudo pkill -x -f "slattach -L -p slip -s $baud $dev" || true; exit 1; }
    pgrep -x -f "slattach -L -p slip -s $baud $dev" | head -1 > "$run.pid"
    echo "$ifc" > "$run.if"
    sudo ip addr add 192.168.250.1 peer 192.168.250.2 dev "$ifc"
    sudo ip link set "$ifc" mtu 576 up
    echo "cable: $ifc on $dev at $baud baud: this end 192.168.250.1, the PC 192.168.250.2"
    ;;
down)
    [ -e "$run.pid" ] || { echo "cable: not up"; exit 0; }
    sudo kill "$(cat "$run.pid")" 2>/dev/null || true
    rm -f "$run.pid" "$run.if"
    echo "cable: down"
    ;;
status)
    if [ -e "$run.if" ] && [ -d "/sys/class/net/$(cat "$run.if")" ]; then
        ip -s link show "$(cat "$run.if")"
    else
        echo "cable: not up"
    fi
    ;;
*)
    sed -n '2,12p' "$0"; exit 2;;
esac
exit
}
