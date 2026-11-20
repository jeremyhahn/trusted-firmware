#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# TF-M v2.3.1 on QEMU's mps2-an521 (#608 slice 2, ruling 1024 (a)): BL2
# (MCUboot) verifies and boots the signed secure image, TF-M prints its
# "Booting TF-M" banner and enters the non-secure image, and the non-secure
# application prints its first line. TF-M is built out of tree by
# scripts/tfm-build.sh (mk/arch/tfm-targets.mk tfm-qemu-test).
#
#   qemu_tfm_test.sh <dir> <ns-marker>
#
# <dir> holds bl2.axf and load.txt, one "<file> <address>" line per signed
# image QEMU loads into flash (tfm-build.sh writes it from TF-M's flash
# layout); <ns-marker> is the non-secure application's first line.
#
# The wait is on the serial log, not a clock: it ends when the non-secure
# line appears or QEMU exits. TFM_QEMU_TIMEOUT (seconds, default 120) only
# bounds a hang, and reaching it is a failure.
set -eu

dir=$1
ns_marker=$2
qemu=${QEMU:-qemu-system-arm}
timeout=${TFM_QEMU_TIMEOUT:-120}
log=$dir/tfm.uart.log
qlog=$dir/tfm.qemu.log

fail() {
    echo "qemu_tfm_test: FAIL: $*"
    echo "--- uart"
    cat "$log" 2>/dev/null || true
    exit 1
}

[ -n "$ns_marker" ] || fail "no non-secure marker given"
[ -f "$dir/bl2.axf" ] || fail "$dir/bl2.axf is missing: run make tfm-image TFM_BOARD=an521"
[ -f "$dir/load.txt" ] || fail "$dir/load.txt is missing: run make tfm-image TFM_BOARD=an521"

# The loader devices, as the positional parameters.
set --
while read -r file addr; do
    [ -n "$file" ] || continue
    case $addr in 0x[0-9a-fA-F]*) ;; *) fail "load.txt: '$file' has no address";; esac
    [ -f "$dir/$file" ] || fail "load.txt names $file, which is not in $dir"
    set -- "$@" -device "loader,file=$dir/$file,addr=$addr"
done < "$dir/load.txt"
[ $# -gt 0 ] || fail "load.txt names no image"

rm -f "$log" "$qlog" "$dir/tfm.pid"
"$qemu" -M mps2-an521 -display none -monitor none \
    -serial "file:$log" \
    -kernel "$dir/bl2.axf" \
    "$@" \
    -d guest_errors,unimp -D "$qlog" \
    -daemonize -pidfile "$dir/tfm.pid"
read -r pid < "$dir/tfm.pid"
trap 'kill "$pid" 2>/dev/null || true' EXIT

# Until the non-secure line, or QEMU is gone; the deadline bounds a hang.
deadline=$((SECONDS + timeout))
while ! grep -qF -- "$ns_marker" "$log" 2>/dev/null; do
    kill -0 "$pid" 2>/dev/null || break
    [ $SECONDS -lt $deadline ] || break
    sleep 0.2
done

# The first matching line of each; the banner carries ANSI colour codes, so
# the match is on its text, not the whole line.
at() { grep -naF -- "$1" "$log" 2>/dev/null | head -n 1 | cut -d: -f1; }
boot=$(at 'Booting TF-M')
ns=$(at "$ns_marker")

[ -n "$boot" ] || fail "no 'Booting TF-M' banner: BL2 did not boot the secure image, or TF-M did not start"
if [ -z "$ns" ]; then
    kill -0 "$pid" 2>/dev/null || fail "QEMU exited before the non-secure application ran"
    fail "the non-secure application did not print '$ns_marker' within ${timeout}s"
fi
[ "$ns" -gt "$boot" ] || fail "'$ns_marker' came before the TF-M banner"
if grep -qa 'FATAL ERROR\|HardFault\|panic' "$log"; then
    fail "a fault on the way to the non-secure image"
fi
echo "qemu_tfm_test: green (BL2, TF-M secure image, non-secure '$ns_marker')"
