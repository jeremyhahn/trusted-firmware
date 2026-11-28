#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# TF-M v2.3.1 on QEMU's mps2-an521 (#608 slice 2, ruling 1024 (a)): BL2
# (MCUboot) verifies and boots the signed secure image, TF-M prints its
# "Booting TF-M" banner and enters the non-secure image, and the non-secure
# application prints its first line and then, from the DICE partition
# (slice 3), "tfw-dice: deviceid ok". TF-M is built out of tree by
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

# The DICE partition (#608 slice 3): the NS app's check
# (src/tz/tfm/ns/tfw_dice_ns_check.c) prints one "tfw-dice: " line after the
# NS marker, "tfw-dice: deviceid ok" when the partition answered the DeviceID
# public key. Waited for as the marker was: until it appears or QEMU is gone.
while ! grep -qaF -- 'tfw-dice: ' "$log" 2>/dev/null; do
    kill -0 "$pid" 2>/dev/null || break
    [ $SECONDS -lt $deadline ] || break
    sleep 0.2
done
dice_line=$(grep -aF -- 'tfw-dice: ' "$log" 2>/dev/null | head -n 1 | tr -d '\r')
[ -n "$dice_line" ] || fail "no 'tfw-dice: ' line: the non-secure DICE check did not run"
case $dice_line in
*'tfw-dice: deviceid ok'*) ;;
*) fail "the DICE partition did not answer the DeviceID key: $dice_line" ;;
esac
dice=$(at 'tfw-dice: ')
[ "$dice" -gt "$ns" ] || fail "'tfw-dice: ' came before '$ns_marker'"

# The alias evidence (Codex round 5 on #900): the same check goes on to the
# alias key, the alias certificate over nonce 00..1f and an alias signature
# over digest 20..3f, prints each as hex, and ends with "tfw-dice: alias ok"
# or "tfw-dice: alias status <op> <n>". Waited for the same way; then the hex
# is checked on the host with go-xkms (xkms/cmd/tfm-dice-check).
while ! grep -qaE -- 'tfw-dice: alias (ok|status)' "$log" 2>/dev/null; do
    kill -0 "$pid" 2>/dev/null || break
    [ $SECONDS -lt $deadline ] || break
    sleep 0.2
done
alias_status=$(grep -aF -- 'tfw-dice: alias status' "$log" 2>/dev/null | head -n 1 | tr -d '\r')
[ -z "$alias_status" ] || fail "the DICE partition refused the alias evidence: $alias_status"
grep -qaF -- 'tfw-dice: alias ok' "$log" || fail "no 'tfw-dice: alias ok' line: the alias evidence did not complete"

# The value after "tfw-dice: <label> " on its first line, hex only.
hexof() {
    grep -aF -- "tfw-dice: $1 " "$log" 2>/dev/null | head -n 1 | tr -d '\r' |
        sed -n "s/^.*tfw-dice: $1 \([0-9a-f]*\)\$/\1/p"
}
work=$(mktemp -d)
trap 'kill "$pid" 2>/dev/null || true; rm -rf "$work"' EXIT
deviceid_pub=$(hexof deviceid-pub)
alias_pub=$(hexof alias-pub)
alias_cert=$(hexof alias-cert)
alias_sig=$(hexof alias-sig)
[ ${#deviceid_pub} -eq 128 ] || fail "deviceid-pub is not 64 bytes of hex"
[ ${#alias_pub} -eq 128 ] || fail "alias-pub is not 64 bytes of hex"
[ ${#alias_sig} -eq 128 ] || fail "alias-sig is not 64 bytes of hex"
[ -n "$alias_cert" ] && [ $((${#alias_cert} % 2)) -eq 0 ] || fail "alias-cert is not whole bytes of hex"
if command -v xxd >/dev/null 2>&1; then
    printf '%s' "$alias_cert" | xxd -r -p > "$work/alias.der"
else
    rest=$alias_cert
    : > "$work/alias.der"
    while [ -n "$rest" ]; do
        # shellcheck disable=SC2059
        printf "\\x${rest%"${rest#??}"}" >> "$work/alias.der"
        rest=${rest#??}
    done
fi

repo=$(cd "$(dirname "$0")/../../.." && pwd)
nonce=000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f
digest=202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f
(cd "$repo/xkms" && go run ./cmd/tfm-dice-check \
    -deviceid-pub "$deviceid_pub" -alias-pub "$alias_pub" \
    -cert "$work/alias.der" -nonce "$nonce" -digest "$digest" \
    -sig "$alias_sig") || fail "go-xkms refused the alias evidence (xkms/cmd/tfm-dice-check)"

echo "qemu_tfm_test: green (BL2, TF-M secure image, non-secure '$ns_marker', tfw-dice: deviceid ok, alias evidence checked by go-xkms)"
