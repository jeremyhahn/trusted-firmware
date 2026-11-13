#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# The tfwctl wire suite on a QEMU Cortex-M board (#161, #163), run by
# `make integration-test-cortex-m-wire` (mk/arch/cortex-m-targets.mk).
#
#   qemu_wire_test.sh <dir> <machine> <board>
#
# <dir> holds what cm_image.c wrote for <board> (boot.bin,
# boot-unprovisioned.bin, platform.bin, the signed app.bin and addrs.env), as
# for qemu_test.sh. Each boot is one QEMU that runs the signed image with its UART on a pty and its QMP monitor on a Unix socket;
# qmp_pty.py reads the pty's path from QMP's query-chardev; then `make
# hw-test` runs the suite with PORT set to the pty and HW_RESET to the
# socket, so internal/peer resets the guest with QMP system_reset where it
# pulses DTR on a real board. The suite itself is unchanged.
#
# Two boots, each a QEMU of its own:
#
#   boot.bin, the image cm_image provisions with a UDS, runs the suite's
#   soft-tier rows (e2e_firmware_test.go) and the dice and attestcsr rows
#   this image serves, with DEVICEID from addrs.env. A row whose peer lacks
#   the feature it needs fails, naming it (#7), so the lists below leave out
#   by name every row this image cannot serve: the analog, PWM, EEPROM, bus
#   and pulse rows (CAPS bits 2, 3, 5-9 clear); the alias certificate (bit
#   13), operator session (bit 14) and CSR (bit 15) rows; and the OTA and app
#   rows that need a stager or an application (it is built without
#   OTA_STAGE and has none), which CAPS cannot say. The soft image offers
#   ATTEST (bit 10), so the evidence-frame rows run. The refusal rows run
#   instead of what it lacks: each of those families' commands is refused
#   as unknown (e2e_refusal_test.go, e2e_refusal_board_test.go).
#   docs/cortex-m/README.md tables every row per board.
#
#   boot-unprovisioned.bin, the same boot with its UDS slot left erased,
#   runs TestFirmwareAttestRefusedUnprovisioned alone: the soft root reports
#   NOT_PROVISIONED and the application refuses ATTEST, which the
#   provisioned image cannot show.
#
#   reset runs TestQEMUResetCause (sdk/go/cmd/tfwctl/reset_qemu_test.go) on
#   a boot.bin boot of its own: SYS_INFO reads POWER_ON after QMP's reset,
#   SOFTWARE after RESET mode 0 (AIRCR.SYSRESETREQ), and POWER_ON after the
#   next QMP reset.
#
#   fault runs TestQEMUFaultPanic (sdk/go/cmd/tfwctl/fault_qemu_test.go) on
#   a boot.bin boot of its own: through QEMU's gdbstub it points the guest's
#   PC at 0xE0000000, which QEMU makes execute-never, and SYS_INFO reads
#   PANIC after the HardFault's reset, the fault record that PC, then
#   POWER_ON after the next QMP reset.
#
#   qemu_wire_test.sh <dir> <machine> <board> store
#
# is the board's store row instead (mk/arch/cortex-m-targets.mk
# cortex-m-qemu-store-wire): tfw-boot built with BOOT_STORE=1 and app.bin the
# application with the stager (OTA_STAGE=1). Two boots again:
#
#   staging runs the firmware, OTA and app rows on it. The image stages, so
#   the OTA gate row runs its stager legs, OTA_CONFIRM refused as unknown;
#   the OTA_INFO row (an A/B image's) and the OTA_ACTIVATE and OTA_CONFIRM
#   rows (the operator session's) are left out. The refusal rows run, with
#   the soft tier's OTA_ACTIVATE with no OTA_AUTH, AUTH_REQUIRED.
#
#   cycle runs TestQEMUOTACycle (`make cortex-m-qemu-cycle`) on a boot of its
#   own, so no MARK another row left is in the store: it stages the
#   staged.bin cm_image signed, activates it with activate.sig, resets
#   through QMP and checks, in the boot that follows, that the image running
#   is the one it staged.
#
#   qemu_wire_test.sh <dir> <machine> <board> ab
#
# is the board's A/B row (cortex-m-qemu-ab-wire): tfw-boot built with
# BOOT_STORE=1 BOOT_AB=1 and app.bin the A/B application with the stager.
# addrs.env names the factory copies' addresses (cm_image.c), so QEMU's
# loaders write app.bin and platform.bin there and tfw-boot copies each to its
# home only while the home is blank. A QMP system_reset reloads the factory
# copies and nothing else, so within one QEMU the primary, the secondary and
# the platform block keep what the last boot left: a swap, a trial and a
# confirm all survive the reset. Five boots, each a QEMU of its own, so each
# outcome starts from the factory image:
#
#   ab-Info, ab-Confirm, ab-Rollback, ab-Floor and ab-Watchdog each run one test of
#   sdk/go/cmd/tfwctl/ota_ab_qemu_test.go (`make cortex-m-qemu-cycle
#   CYCLE_TEST=TestQEMUAB<outcome>`): OTA_INFO on both slots; a confirmed
#   update kept across the next resets; an unconfirmed one rolled back;
#   below-floor.bin refused once a confirmed update raised the floor; and a
#   trial that stops petting reset by the watchdog and rolled back.
#
#   qemu_wire_test.sh <dir> <machine> <board> spi
#
# is the board's SPI row (cortex-m-qemu-spi-wire): the base row's tfw-boot and
# the application built with SPI=1. One boot, spi, runs TestQEMUSPIXfer
# (sdk/go/cmd/tfwctl/spi_qemu_test.go, `make cortex-m-qemu-cycle
# CYCLE_TEST=TestQEMUSPIXfer`): SPI_XFER through tfwctl's spi verb on the
# shield 0 PL022, which QEMU models with nothing on its bus, so every byte read
# back is 0x00.
#
#   qemu_wire_test.sh <dir> <machine> <board> i2c
#
# is the board's I2C row (cortex-m-qemu-i2c-wire): the base row's tfw-boot and
# the application built with I2C=1. One boot, i2c, runs TestQEMUI2CXfer
# (sdk/go/cmd/tfwctl/i2c_qemu_test.go): I2C_XFER through tfwctl's i2c verb on
# the shield 0 SBCon, which QEMU models with nothing on its bus, so every
# address is NACKed.
#
#   qemu_wire_test.sh <dir> <machine> <board> pulse
#
# is the board's PULSE row (cortex-m-qemu-pulse-wire): the base row's tfw-boot
# and the application built with PULSE=1. One boot, pulse, runs
# TestQEMUPulseTimeout (sdk/go/cmd/tfwctl/pulse_qemu_test.go): PULSE_IN and
# PULSE_CAPTURE on a pin QEMU does not drive, so each times out.
#
# A failed row leaves its guest's registers at the end of the boot's QEMU log
# (regs below), so a guest that stopped answering says where it stopped.
#
# Nothing here waits on a clock of its own: QEMU daemonizes once its sockets
# are bound, qmp_pty.py's and qmp_regs.py's exchanges are bounded, every wait
# in the suite is bounded, and the trap kills QEMU on every exit.
set -eu

usage() { echo "usage: qemu_wire_test.sh <dir> <machine> <board> [store|ab|spi|i2c|pulse]" >&2; exit 2; }
case $#:${4:-} in 3: | 4:store | 4:ab | 4:spi | 4:i2c | 4:pulse) ;; *) usage ;; esac
dir=$1
machine=$2
board=$3
row=${4:-base}
qemu=${QEMU:-qemu-system-arm}
make=${MAKE:-make}
here=$(cd "$(dirname "$0")" && pwd)

# BOOT_AT, APP_AT, PLATFORM_AT and DEVICEID, as cm_image.c wrote them.
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

# boot <name> <boot image>: boot <boot image> under QEMU, and name its pty
# and QMP socket in $pty and $qmp.
boot() {
    name=$1
    image=$2
    qmp=$sockdir/$name.sock
    pidfile=$dir/wire-$name.pid
    log=$dir/wire-$name.qemu.log

    rm -f "$pidfile" "$log"
    "$qemu" -M "$machine" -display none -monitor none \
        -serial pty \
        -qmp "unix:$qmp,server=on,wait=off" \
        -device "loader,file=$dir/$image,addr=$BOOT_AT,force-raw=on" \
        -device "loader,file=$dir/app.bin,addr=$APP_AT,force-raw=on" \
        -device "loader,file=$dir/platform.bin,addr=$PLATFORM_AT,force-raw=on" \
        -d guest_errors,unimp -D "$log" \
        -daemonize -pidfile "$pidfile" ||
        { echo "qemu_wire_test: $board $name: $qemu -M $machine did not start (its log: $log)" >&2; exit 1; }
    read -r pid < "$pidfile"

    pty=$(python3 -I "$here/qmp_pty.py" "$qmp" serial0 10) ||
        { echo "qemu_wire_test: $board $name: no pty path from QMP at $qmp" >&2; exit 1; }
    [ -e "$pty" ] || { echo "qemu_wire_test: $board $name: QMP named $pty, which does not exist" >&2; exit 1; }
    echo "qemu_wire_test: $board $name: $image, QEMU $pid, serial0 on $pty, QMP at $qmp"
}

# alive: QEMU outlived the run, which a guest fault would not; then stop it.
alive() {
    kill -0 "$pid" 2>/dev/null ||
        { echo "qemu_wire_test: $board $name: QEMU exited during the run (its log: $log)" >&2; exit 1; }
    stop
}

# regs: a row failed against a guest QEMU still runs. Read the guest's
# registers, its SCB and NVIC words and its stack (qmp_regs.py); where make
# named them, UART0's STATE, CTRL and INTSTATUS (QEMU_UART, its base: DATA is
# passed over, since reading it takes the byte), the firmware's receive ring,
# framer and stager by symbol from its ELF (QEMU_ELF) with the fault record
# (src/hal/cortex-m/hal_fault_frame.h: the last fault's PC, LR, xPSR), and
# the RAM the image's .data and .bss sit in (QEMU_RAM, "<base> <bytes>"); stop
# QEMU; then append what was read to $log, which the failure line names. The append waits for the stop because QEMU
# opened $log for writing, not appending, as its stderr too: its last line,
# written as it is killed, would land over a dump appended before. Bounded at
# 5 s; a dump that fails says why in the log and leaves the run's result as
# it was.
regs() {
    set --
    if [ -n "${QEMU_UART:-}" ]; then
        set -- "x /3wx $(printf '0x%08x' $((QEMU_UART + 4)))"
    fi
    if [ -n "${QEMU_ELF:-}" ]; then
        set -- "$@" "elf=$QEMU_ELF" sym=rx_head sym=rx_tail sym=rx_dropped sym=rx_buf \
            sym=tx_head sym=tx_tail sym=tx_busy sym=ticks sym=transport \
            sym=state sym=total sym=received sym=tfw_cm_fault_frame
    fi
    if [ -n "${QEMU_RAM:-}" ]; then
        set -- "$@" "x /$((${QEMU_RAM#* } / 4))wx ${QEMU_RAM% *}"
    fi
    python3 -I "$here/qmp_regs.py" "$qmp" 5 "$@" > "$sockdir/regs" 2>&1 || true
    stop
    { echo "qemu_wire_test: $board $name: the guest when the row failed"
      cat "$sockdir/regs"; } >> "$log"
}

# wire <name> <boot image> <rows>: boot <boot image> under QEMU, run the rows
# matching <rows> against it, and stop QEMU.
wire() {
    boot "$1" "$2"
    "$make" --no-print-directory hw-test BOARD="$board" PORT="$pty" HW_RESET="qmp:$qmp" \
        DEVICEID="$DEVICEID" HW_TEST_RUN="$3" ||
        { regs; echo "qemu_wire_test: $board $name: the wire suite failed (QEMU's log: $log)" >&2; exit 1; }
    alive
}

# cycle <name> <test>: boot boot.bin under QEMU, run the one OTA test <test>
# against it, and stop QEMU.
cycle() {
    boot "$1" boot.bin
    "$make" --no-print-directory cortex-m-qemu-cycle BOARD="$board" PORT="$pty" HW_RESET="qmp:$qmp" \
        STAGED="$dir" CYCLE_TEST="$2" ||
        { regs; echo "qemu_wire_test: $board $1: $2 failed (QEMU's log: $log)" >&2; exit 1; }
    alive
}

firmware='Ping|PingDeliversTheBootEventWithNoBootWait|PingAfterAMalformedBurst|Caps|Sys|Pin|Write|Timer'
refuse='RefusesAnalogItsCapsLacks|RefusesPWMItsCapsLacks|RefusesPulseItsCapsLacks|RefusesEEPROMItsCapsLacks|RefusesI2CItsCapsLacks|RefusesSPIItsCapsLacks|RefusesOneWireItsCapsLacks|RefusesTheAuthSessionItsCapsLacks|RefusesAttestCSRItsCapsLacks'
dice='AttestEvidenceFrame|EvidenceFrameRejectsTamperingAndOtherKeys|SoftwareTierStillCannotIssueACertificate'
csr='AttestCSRReportsADeviceThatCannotIssueOne|ElementWithoutTheCapsBitIsRefusedAsUnsupported'
ota='OTAAuthLetsTheCommandPastTheGate'
app='AppIsAnUnknownCommandWithoutAnApplication'

if [ "$row" = store ]; then
    wire staging boot.bin "^TestFirmware($firmware|$refuse|$ota|SoftTierOTAActivateWithoutOTAAuthIsAuthRequired|$app)\$"
    cycle cycle TestQEMUOTACycle
elif [ "$row" = ab ]; then
    for outcome in Info Confirm Rollback Floor Watchdog; do
        cycle "ab-$outcome" "TestQEMUAB$outcome"
    done
elif [ "$row" = spi ]; then
    cycle spi TestQEMUSPIXfer
elif [ "$row" = i2c ]; then
    cycle i2c TestQEMUI2CXfer
elif [ "$row" = pulse ]; then
    cycle pulse TestQEMUPulseTimeout
else
    wire provisioned boot.bin "^TestFirmware($firmware|$refuse|$dice|$csr|$app)\$"
    wire unprovisioned boot-unprovisioned.bin '^TestFirmwareAttestRefusedUnprovisioned$'
    cycle reset TestQEMUResetCause
    cycle fault TestQEMUFaultPanic
fi
echo "qemu_wire_test: $board $row row: green"
