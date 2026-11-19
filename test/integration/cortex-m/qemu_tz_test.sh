#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The TZ=1 case (#590, docs/plans/590.md): QEMU boots the secure image, which
# sets up the SAU and enters the non-secure image; the non-secure image calls
# tfw_s_version through its NSC veneer and prints the value, then reads
# Secure SRAM, and the secure SecureFault handler reports the fault
# (SFSR.AUVIOL; SFAR checked where SFARVALID is set). Its own script, not a qemu_test.sh case: that script
# boots tfw-boot and a signed application, neither of which this case has.
#
#   qemu_tz_test.sh <dir> <machine> <s-load> <ns-load> <s-ram>
#
# <dir> holds tz_s.bin and tz_ns.bin (mk/arch/cortex-m-targets.mk
# cortex-m-qemu-tz-run); the addresses are the images' __tz_load symbols and
# the Secure SRAM base, read from the ELF files by the make.
set -eu

dir=$1
machine=$2
s_at=$3
ns_at=$4
s_ram=$5
qemu=${QEMU:-qemu-system-arm}
log=$dir/tz.uart.log
qlog=$dir/tz.qemu.log

fail() {
    echo "qemu_tz_test: FAIL: $*"
    echo "--- uart"
    cat "$log" 2>/dev/null || true
    exit 1
}

for a in "$s_at" "$ns_at" "$s_ram"; do
    case $a in 0x[0-9a-fA-F]*) ;; *) fail "an address is missing: '$s_at' '$ns_at' '$s_ram'";; esac
done

# Ruling 935: the DeviceID seed, the CDI, the UDS and every private key live
# in the secure image only. The non-secure image has the four veneers and no
# symbol of the secrets.
nm=${TZ_NM:-arm-none-eabi-nm}
syms=$("$nm" "$dir/tz_ns.elf") || fail "$nm cannot read tz_ns.elf"
for s in tfw_dice_deviceid_pub tfw_dice_alias_pub tfw_dice_alias_cert tfw_dice_alias_sign; do
    printf '%s\n' "$syms" | grep -q " $s\$" || fail "tz_ns.elf does not link the $s veneer"
done
if printf '%s\n' "$syms" | grep -Ei 'seed|cdi|uds|priv|dice_test|dice_service'; then
    fail "tz_ns.elf carries a DICE secret's symbol (above)"
fi

rm -f "$log" "$qlog" "$dir/tz.pid"
"$qemu" -M "$machine" -display none -monitor none \
    -serial "file:$log" \
    -device "loader,file=$dir/tz_s.bin,addr=$s_at,force-raw=on" \
    -device "loader,file=$dir/tz_ns.bin,addr=$ns_at,force-raw=on" \
    -d guest_errors,unimp -D "$qlog" \
    -daemonize -pidfile "$dir/tz.pid"
read -r pid < "$dir/tz.pid"
trap 'kill "$pid" 2>/dev/null || true' EXIT

# The last line the run prints is the secure handler's; wait for it.
i=0
while [ $i -lt 100 ] && ! grep -q 'SecureFault' "$log" 2>/dev/null; do
    sleep 0.1
    i=$((i + 1))
done
sleep 0.2

sfar=$(printf '0x%08x' "$s_ram")
grep -q '^tz: s: sau on, entering ns' "$log" || fail "the secure image did not reach the non-secure entry"
grep -q '^tz: ns: up' "$log" || fail "the non-secure image did not run"
grep -q '^tz: ns: tfw_s_version=0x00010000' "$log" || fail "tfw_s_version through the veneer did not return 0x00010000"
grep -q '^tz: s: dice ready' "$log" || fail "the secure DICE service did not initialise"
grep -Eq '^tz: ns: dice deviceid sha256=[0-9a-f]{64}' "$log" ||
    fail "tfw_dice_deviceid_pub through its veneer gave no DeviceID public key hash"
if grep -q '^tz: ns: dice deviceid sha256=0\{64\}' "$log"; then
    fail "the DeviceID public key hash is all zeros"
fi
cert=$(grep '^tz: ns: dice alias cert len=0x' "$log" | head -n 1) ||
    fail "tfw_dice_alias_cert through its veneer gave no certificate"
cert=${cert##*len=}
cert=${cert%$'\r'}
[ $((cert)) -gt 0 ] || fail "the alias certificate is empty"
grep -q '^tz: ns: dice alias signature verified' "$log" ||
    fail "the alias signature over the fixed digest did not verify against the alias public key"
# TFW_NS_CHECK: an output buffer in Secure SRAM is TFW_ERR_RANGE, not a write.
grep -q '^tz: ns: dice secure pointer refused' "$log" ||
    fail "the DICE service did not refuse a buffer in Secure SRAM"
if grep -q '^tz: ns: dice .* failed' "$log"; then
    fail "a DICE veneer call failed"
fi
grep -q '^tz: ns: reading secure SRAM' "$log" || fail "the non-secure image did not reach its secure SRAM read"
line=$(grep '^tz: s: SecureFault SFSR=' "$log" | head -n 1) ||
    fail "the non-secure read of $sfar was not a SecureFault reported by the secure handler"
sfsr=${line#*SFSR=}
sfsr=${sfsr%% *}
got_sfar=${line##*SFAR=}
# AUVIOL (bit 3): a Non-secure access to an address the SAU makes Secure.
[ $((sfsr & 0x8)) -ne 0 ] || fail "SFSR $sfsr has no AUVIOL"
# SFAR is the address only where SFARVALID (bit 6) says so; QEMU 10 sets
# AUVIOL alone for a data access, and a part sets both.
if [ $((sfsr & 0x40)) -ne 0 ] && [ "$got_sfar" != "$sfar" ]; then
    fail "SFAR $got_sfar is not the secure SRAM read at $sfar"
fi
if grep -q '^tz: ns: secure SRAM read returned' "$log"; then
    fail "the non-secure image read secure SRAM"
fi
if grep -q 'fault$' "$log"; then
    fail "an unexpected fault"
fi
echo "qemu_tz_test: green (secure boot, SAU, NSC veneers, DICE service, refused secure pointer, SecureFault at $sfar)"
