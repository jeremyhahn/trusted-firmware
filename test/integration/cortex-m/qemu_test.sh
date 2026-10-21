#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The Cortex-M QEMU suite (#161): tfw-boot and the firmware on QEMU's
# mps2-an505, run by `make integration-test-cortex-m` (mk/arch/cortex-m-targets.mk).
#
#   qemu_test.sh <dir> <machine> <board-id> <tier>
#
# <dir> holds what cm_image.c wrote (boot.bin, platform.bin, the three
# applications and addrs.env) and the wirecheck binary. Each run is its own
# QEMU, every image a raw blob at the address addrs.env names, the UART on a
# Unix socket, and QEMU daemonized: it returns once the socket is bound.
# wirecheck verifies ATTEST under the DeviceID point cm_image derived with the
# firmware's own deviceid_p256.c (DEVICEID in addrs.env).
#
#   genuine    the signed application answers CAPS with <board-id> and <tier>,
#              takes a soft reset, and answers again after tfw-boot has
#              verified it a second time
#   tampered   one bit of the image flipped after signing: tfw-boot refuses
#   otherkey   signed by a key tfw-boot does not hold: tfw-boot refuses
#
# A refusal on this row (BOOT_OTA=0) is recovery_halt.c: tfw-boot stops and
# never answers. wirecheck -silent passes only if no PING is echoed for the
# whole window, a deadline on the socket and nothing slept; that QEMU is still
# running at the end is checked here, so a guest that crashed QEMU is not taken
# for one that refused.
#
# Nothing here waits on a clock of its own: every wait is wirecheck's bounded
# dial, or `timeout` around QEMU's exit.
set -eu

[ $# -eq 4 ] || { echo "usage: qemu_test.sh <dir> <machine> <board-id> <tier>" >&2; exit 2; }
dir=$1
machine=$2
board=$3
tier=$4
qemu=${QEMU:-qemu-system-arm}
silent=${CM_SILENT:-10s}
wait=${CM_WAIT:-60s}

# BOOT_AT, APP_AT and PLATFORM_AT, as cm_image.c wrote them.
# shellcheck source=/dev/null
. "$dir/addrs.env"

# The sockets in a short directory of their own: a Unix socket's path is
# limited to about a hundred bytes, which a CI checkout's build path can pass.
sockdir=$(mktemp -d)
pid=

stop() {
    if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
        kill "$pid"
        timeout 10 tail --pid="$pid" -f /dev/null || true
    fi
    pid=
}
trap 'stop; rm -rf "$sockdir"' EXIT

# start <name> <application>: a QEMU booting tfw-boot with <application> in
# the slot, its UART at $sockdir/<name>.sock, its pid in $pid.
start() {
    sock=$sockdir/$1.sock
    log=$dir/$1.qemu.log
    rm -f "$dir/$1.pid" "$log"
    "$qemu" -M "$machine" -display none -monitor none \
        -serial "unix:$sock,server=on,wait=off" \
        -device "loader,file=$dir/boot.bin,addr=$BOOT_AT,force-raw=on" \
        -device "loader,file=$dir/$2,addr=$APP_AT,force-raw=on" \
        -device "loader,file=$dir/platform.bin,addr=$PLATFORM_AT,force-raw=on" \
        -d guest_errors,unimp -D "$log" \
        -daemonize -pidfile "$dir/$1.pid"
    read -r pid < "$dir/$1.pid"
}

fail() {
    echo "qemu_test: $1: $2"
    if [ -f "$log" ]; then
        echo "qemu_test: the last of $log:"
        tail -n 20 "$log"
    fi
    exit 1
}

alive() {
    kill -0 "$pid" 2>/dev/null || fail "$1" "QEMU exited: the guest crashed it rather than halting"
}

start genuine app.bin
"$dir/wirecheck" -network unix -addr "$sock" -wait "$wait" -board "$board" -tier "$tier" \
    -deviceid "$DEVICEID" -reset ||
    fail genuine "the signed image did not answer, or did not take the reset"
"$dir/wirecheck" -network unix -addr "$sock" -wait "$wait" -board "$board" -tier "$tier" \
    -deviceid "$DEVICEID" ||
    fail genuine "the signed image did not answer after the reset"
alive genuine
stop
echo "qemu_test: genuine: verified, answered, reset, verified and answered again"

for row in tampered otherkey; do
    start "$row" "app-$row.bin"
    "$dir/wirecheck" -network unix -addr "$sock" -silent "$silent" ||
        fail "$row" "tfw-boot booted an image it must refuse"
    alive "$row"
    stop
    echo "qemu_test: $row: refused, silent for $silent, QEMU still up"
done

echo "qemu_test: green"
