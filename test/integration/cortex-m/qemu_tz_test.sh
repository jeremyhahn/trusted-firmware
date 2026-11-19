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
#   qemu_tz_test.sh --self-test
#
# checks tz_check_report against fixed CRLF and LF reports (make host-test).
set -eu

# tz_check_report <log> <sfar>: the secure handler's SecureFault line in <log>
# reports AUVIOL, and SFAR <sfar> where SFARVALID is set. The handler ends its
# lines with CRLF; the CR is stripped before any field is compared. Prints the
# reason and returns 1 when the report is not that fault.
tz_check_report() {
    line=$(grep '^tz: s: SecureFault SFSR=' "$1" | head -n 1 | tr -d '\r')
    if [ -z "$line" ]; then
        echo "the non-secure read of $2 was not a SecureFault reported by the secure handler"
        return 1
    fi
    sfsr=${line#*SFSR=}
    sfsr=${sfsr%% *}
    got_sfar=${line##*SFAR=}
    # AUVIOL (bit 3): a Non-secure access to an address the SAU makes Secure.
    if [ $((sfsr & 0x8)) -eq 0 ]; then
        echo "SFSR $sfsr has no AUVIOL"
        return 1
    fi
    # SFAR is the address only where SFARVALID (bit 6) says so; QEMU 10 sets
    # AUVIOL alone for a data access, and a part sets both.
    if [ $((sfsr & 0x40)) -ne 0 ] && [ "$got_sfar" != "$2" ]; then
        echo "SFAR $got_sfar is not the secure SRAM read at $2"
        return 1
    fi
}

if [ "${1:-}" = --self-test ]; then
    t=$(mktemp)
    trap 'rm -f "$t"' EXIT
    st() {
        printf 'tz: ns: reading secure SRAM%btz: s: SecureFault SFSR=%s SFAR=%s%b' "$3" "$1" "$2" "$3" > "$t"
        tz_check_report "$t" 0x30000000 > /dev/null
    }
    st 0x00000048 0x30000000 '\r\n' || { echo "qemu_tz_test: FAIL: CRLF report, SFARVALID, matching SFAR"; exit 1; }
    if st 0x00000048 0x30000004 '\r\n'; then echo "qemu_tz_test: FAIL: CRLF report, SFARVALID, mismatching SFAR passed"; exit 1; fi
    st 0x00000048 0x30000000 '\n' || { echo "qemu_tz_test: FAIL: LF report, SFARVALID, matching SFAR"; exit 1; }
    st 0x00000008 0x00000000 '\r\n' || { echo "qemu_tz_test: FAIL: CRLF report, AUVIOL alone"; exit 1; }
    if st 0x00000040 0x30000000 '\r\n'; then echo "qemu_tz_test: FAIL: CRLF report without AUVIOL passed"; exit 1; fi
    echo "qemu_tz_test: self-test green"
    exit 0
fi

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
grep -q '^tz: ns: reading secure SRAM' "$log" || fail "the non-secure image did not reach its secure SRAM read"
why=$(tz_check_report "$log" "$sfar") || fail "$why"
if grep -q '^tz: ns: secure SRAM read returned' "$log"; then
    fail "the non-secure image read secure SRAM"
fi
if grep -q 'fault$' "$log"; then
    fail "an unexpected fault"
fi
echo "qemu_tz_test: green (secure boot, SAU, NSC veneer, SecureFault at $sfar)"
