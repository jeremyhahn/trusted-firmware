# Cortex-M

> **Status: DRAFT** · Issue: #161 · SPEC row: `cortex-m` (future) · Last verified: 2026-10-22

The Cortex-M architecture ports: Cortex-M7 and Cortex-M33 first. Each is a HAL
and boot-HAL port under `src/hal/cortex-m/` and `boot/hal/cortex-m/`, and boards
are data. The owner ungated the two ADRs on 2026-10-18 (CA m623); the first
slice is the generic HAL and boot-HAL port on QEMU `mps2-an505` (#161, below),
then hardware in the SPEC row's order.

| Document | What it covers |
|---|---|
| [`architecture.md`](architecture.md) | the generic port and the board as data: the three layers, the memory map, placement, the boot flow, what tfw-boot verifies, the trust boundaries |
| [`api.md`](api.md) | the HAL functions the port implements, those it leaves to a later slice, and tfw-boot's hardware layer |
| [`user.md`](user.md) | building for QEMU `mps2-an505` and `mps2-an500`, and running `make integration-test-cortex-m` and `make integration-test-cortex-m-wire` |
| [`engineering.md`](engineering.md) | files, invariants, the toolchain, `lint-arch-leak`, the `progmem.h` far reads, the tests, and the open owner questions |
| [`adr-reference-part-and-emulation.md`](adr-reference-part-and-emulation.md) | **PROPOSED** (#14): the i.MX 95 Cortex-M7 as the reference part, checked against the STM32H573I-DK and the nRF54L15-DK; QEMU `mps2-an505` as the CI target; the HAL mapping, toolchain, budgets and boot chain |
| [`adr-trust-model-vs-tf-m.md`](adr-trust-model-vs-tf-m.md) | **PROPOSED** (#15): the trust model and its relation to TF-M — TF-M Profile Medium with our attestation partition on the M33, MPU-only isolation on the M7, where the DICE root lives on each part, and the owner's open questions |

## What is built (#161, then the M7 on QEMU)

The generic port and its two QEMU boards:

- `boards/mps2-an505`: Arm's AN505 (a Cortex-M33) as QEMU models it, board id
  `0x40`;
- `boards/mps2-an500`: Arm's AN500 (a Cortex-M7, ARMv7-M, no TrustZone) as
  QEMU models it, board id `0x42` ([`mps2-an500.md`](../boards/mps2-an500.md)).

Both ids are allocated in [`board-id-registry.md`](../boards/board-id-registry.md),
and both boards are soft tier only. The architecture's make file is
`mk/arch/cortex-m.mk`; the CPU flags come from each profile's `CPU`
(`cortex-m33` or `cortex-m7`, FPU off). The two MPS2 images share one
SoC directory, because their peripherals are the same CMSDK blocks at
different addresses:

- the linker script `mk/arch/cortex-m/mps2.ld`;
- the drivers in `src/hal/cortex-m/mps2/` and `boot/hal/cortex-m/mps2/`.

Each `board_profile.h` states its block's base addresses and UART interrupt
numbers. The core HAL is in `src/hal/cortex-m/`, and tfw-boot's hardware layer
is in `boot/hal/cortex-m/`. Each board builds the application
(`make firmware BOARD=<board> CRYPTO=soft`) and one tfw-boot row: soft tier
and measurement, P-256 verified in software, no OTA, no store.

**I2C.** `src/hal/cortex-m/mps2/hal_bus.c` is an I2C master bit-banged over
the shield 0 SBCon, a block of two open-collector lines (Arm DUI 0224I, 4.19).
`sbcon.c` is the only file that touches its registers. The base is in the
board profile: `0x4020C000` on mps2-an505 (AN505, DAI 0505B) and `0x40029000`
on mps2-an500 (AN500, DAI 0500B). QEMU's `mps2-tz.c` and `mps2.c` put the
block at the same addresses. A NACK on the address or on a data byte returns
`TFW_ERR_NACK`. SDA held low through nine recovery clocks, or SCL held low
past the stretch limit, returns `TFW_ERR_TIMEOUT`. These are the AVR TWI
driver's codes, but here a data NACK also sends STOP. The files link with
`src/caps/bus.c` only when a bus is on (`ARCH_BUS_SRCS` in
`mk/arch/cortex-m.mk`). `TFW_FEATURE_I2C` stays 0 by default, so the images
below do not report CAPS bit 7. `I2C=1` turns it on (`TFW_CM_I2C` in the
board profile, `I2C` in `BOARD_FEATURES_ON`). mps2-an505's row with the bus
on is `atecc608a:I2C=1` (`make firmware BOARD=mps2-an505 CRYPTO=atecc608a
I2C=1`), and `soft:I2C=1` beside it builds the image the QEMU wire row runs,
so `firmware-matrix` size-checks it too; mps2-an500 has no element, so its
row is `soft:I2C=1` alone. The SBCon has
no select, so neither pin table changes. 1-Wire returns
`TFW_ERR_UNSUPPORTED`. `test/unit/test_mps2_bus.c` runs the master against a
simulated bus and device in `make host-test`. The QEMU I2C wire row (s14)
boots each board's soft image with `I2C=1` and runs `TestQEMUI2CXfer`
(`sdk/go/cmd/tfwctl/i2c_qemu_test.go`): `mps2.c` and `mps2-tz.c` attach no
device to the SBCon's bus, so `hw/i2c/bitbang_i2c.c`'s address byte finds
none in `hw/i2c/core.c`'s `i2c_start_transfer` and leaves SDA high on the
ninth clock. Every transfer at `0x50` returns `TFW_ERR_NACK`, as a write, a
read and at 400 kHz; `0x60` is refused as `TFW_ERR_PROTECTED_REGION` and
`0x80` as `TFW_ERR_RANGE`.

**SPI.** The same `hal_bus.c` is an SPI master on the shield 0 SPI, an Arm
PrimeCell SSP (PL022, Arm DDI 0194H). `pl022.c` is the only file that touches
its registers and the FPGA I/O block's MISC register. The image does not wire
the PL022's own select to the shield: MISC bit 8, `SHIELD0_SPI_nCS`, is the
select (AN505 4.6 and Table 4-5; AN500 3.7 and Table 3-6, MISC at `+0x4C` in
both). The base is in the board profile: `0x4020A000` on mps2-an505 (AN505
Table 3-7) and `0x40026000` on mps2-an500 (AN500 Table 3-1). `SPI_CONFIG`'s
mode is SSPCR0's SPO:SPH. Its divisor, AVR's SPR1:SPR0, becomes a prescale of
2 times 1 + SCR, for 4, 16, 64 or 128 (DDI 0194H, Table 3-2). The PL022 shifts
the most significant bit first only, so least-significant-first reverses each
byte in software. The select is released before init touches the port, as on
AVR. A transfer accepts one select, pin reference port 1 bit 8 (MISC bit 8);
any other is `TFW_ERR_BAD_PIN`. A frame not done in 2000 µs releases the
select and returns `TFW_ERR_TIMEOUT`. `pl022.c` joins `ARCH_BUS_SRCS`, so the
`atecc608a:I2C=1` row's boot-time `tfw_bus_init` now also sets the port up.
Both board tables name the select as pin 2, port 1 bit 8, flagged digital
(it is driven as a digital output); `hal_gpio.c` still refuses port 1, so `PIN_MODE` and `DIGITAL_WRITE` cannot reach
it, and `SPI_XFER` takes an unclaimed select. `TFW_FEATURE_SPI` is 0 by
default and 1 with `SPI=1` (`TFW_CM_SPI` in the board profile, `SPI` in
`BOARD_FEATURES_ON`), which links the bus. Each board has the row
`soft:SPI=1` (`make firmware BOARD=mps2-an505 CRYPTO=soft SPI=1`).
`test/unit/test_mps2_bus.c` runs the master against a modelled PL022 in
`make host-test`; `test/unit/test_mps2_spi_row.c` builds mps2-an505's
`SPI=1` profile and table, checks CAPS reports SPI alone, and sends an
`SPI_XFER` on pin 2 from `src/caps/bus.c` to a modelled device. QEMU's
`mps2-tz.c` puts a PL022 at `0x4020a000` ([ADR #14](adr-reference-part-and-emulation.md),
"Peripherals") and `mps2.c` the AN500's at `0x40026000`, with MISC at
`+0x4c` in `mps2-fpgaio.c` (s13). The QEMU SPI wire row (s13) runs
`TestQEMUSPIXfer` on both boards: every byte reads back `0x00`, and pin 0 is
refused as `TFW_ERR_BAD_PIN`.

Open: the OPTIGA Trust M tier's Cortex-M cost. `CRYPTO=optiga` (row
`optiga-trust-m`) is not on this branch's base yet, so nothing measures it
here. When it is, mps2-an505 adds `optiga:I2C=1` to `BOARD_ROWS` beside
`atecc608a:I2C=1`, as the ATmega2560 carries `optiga:ETH=1` and
`optiga:ETH=0`. `make firmware-matrix` then size-checks the row against
`FLASH_BUDGET` and `RAM_BUDGET`, and its flash and RAM against the soft
image's are the tier's cost on a Cortex-M. The tier stays out of
`BOARD_TIERS`: which boards it fits is open question 5 of
[`adr-optiga-trust-m-onboarding.md`](../multi-arch/adr-optiga-trust-m-onboarding.md).

**The emulation test.** `make integration-test-cortex-m`
(`mk/arch/cortex-m-targets.mk`) runs the same test on each board, in
`qemu-system-arm -M mps2-an505` and then `-M mps2-an500`
(`integration-test-cortex-m-<board>` runs one). The signer
`test/integration/cortex-m/cm_image.c` provisions a fresh key and UDS into
tfw-boot and signs the application. `test/integration/cortex-m/qemu_test.sh`
runs three QEMUs per board:

- the signed image answers CAPS (the board's id, tier 1), takes a soft reset
  and answers again;
- a tampered image stays silent for a bounded window with QEMU still up;
- an image signed with another key does the same.

CI runs it in the image job, in the toolchain image, as the step
`make integration-test-cortex-m (toolchain image)`. The steps before it build
both images for both boards. `firmware-matrix` builds both boards with the
rest. The Arm toolchain and QEMU are in `docker/packages.txt`.

**The wire suite on QEMU.** `make integration-test-cortex-m-wire` (one board:
`integration-test-cortex-m-wire-<board>`) runs the `tfwctl` end-to-end rows
(`TestFirmware*`, which `hw-test` points at a board) on both boards, as the CI
step `make integration-test-cortex-m-wire (toolchain image)`.
`test/integration/cortex-m/qemu_wire_test.sh` boots the same signed image
with `-serial pty -qmp unix:<socket>,server=on,wait=off`; `qmp_pty.py` reads
the pty's path from QMP's `query-chardev`; then `hw-test` runs with `PORT` set
to the pty and `HW_RESET=qmp:<socket>`, so the seam, `sdk/go/internal/peer`,
resets the guest with `system_reset` where it pulses DTR on a real board, and
opens the pty without asking for modem lines, which a pseudo-terminal does not
have. The start wait after the reset is the same. `peerBoards` in
`e2e_firmware_test.go` has a row for each MPS2 board (two LEDs, outputs only,
and the SPI select as pin 2).
A row whose family the board lacks finds it absent from CAPS and checks that
the board refuses the command. That covers ADC, PWM, EEPROM, I2C, SPI, 1-Wire
and pulse, and the alias certificate, operator session and CSR. Every wait is
bounded, and the script's trap kills QEMU on every exit.

A row that fails leaves its guest's state at the end of the boot's
`wire-<name>.qemu.log`, the log its failure line names. Before QEMU is
stopped, `qmp_regs.py` asks QMP's `human-monitor-command` for these, then
the gdbstub for the special registers, with one 5 s bound across them all:

- `info registers`: the PC, and XPSR's handler or thread mode;
- the SCB words: ICSR (whole), VTOR, AIRCR, SCR, CCR, SHPR1-3, SHCSR, CFSR,
  HFSR, DFSR, MMFAR, BFAR and DHCSR;
- the NVIC's enabled, pending and active words, and the priorities of
  interrupts 0 to 47;
- SysTick, and its Non-secure bank through the `0xE002E010` alias (the an500's
  Cortex-M7 has none, and QEMU says it cannot access the memory);
- 32 words at the stack pointer;
- UART0's STATE, CTRL and INTSTATUS (`QEMU_UART` in `profile.mk`; DATA is
  never read, since reading it takes the byte);
- by symbol from the image's ELF (`elf=` and `sym=`): the receive ring
  (`rx_head`, `rx_tail`, `rx_buf`, `rx_dropped`), the transmit ring, `ticks`,
  the framer (`transport`) and the stager (`state`, `total`, `received`);
- the board's RAM (`RAM_BASE`, `RAM_BUDGET`);
- last, PRIMASK, BASEPRI, FAULTMASK and CONTROL (and their `_s`/`_ns`
  banks on the Cortex-M33). `info registers` has none of them and they have no
  address, so HMP `gdbserver` starts QEMU's gdbstub on `<qmp socket>.gdb` and
  the script reads them over the GDB remote protocol, by the regnum the
  stub's `m-system` and `secext` features give each.

A guest that stopped answering then shows where it stopped. A PC in
`tfw_cm_unhandled` in handler mode is the NMI (the watchdog's first expiry),
or SecureFault or an unused vector; the IPSR names which. A HardFault, MemManage,
BusFault or UsageFault no longer parks there: since s16 it resets with the
panic marker (below), so the dump shows a fresh boot and the next SYS_INFO
reads `PANIC`. Since s17 the faulting frame's PC, LR and xPSR survive that
reset in `tfw_cm_fault_frame`, which the dump reads by symbol. A PC
in thread mode in the main loop means the guest was still running and the frame
was lost. An exception VECTPENDING names, not taken in thread mode with no
active exception, means PRIMASK is 1, BASEPRI is at or above its priority, or
FAULTMASK is 1: the special registers line says which. On the an505 the NVIC's
IRQ 42 is UART0's combined interrupt, which the firmware never enables: it is
pending after any UART traffic. Receive and transmit are IRQs 32 and 33 (0 and
1 on the an500).

For a hang with the main loop alive and every mask clear, each value below
decides one hypothesis (ARMv7-M ARM DDI 0403E: B1.5.4 priority and
preemption, B3.2.4 ICSR, B3.2.5 VTOR, B3.2.6 AIRCR, B3.2.12 SHPR3, B3.3.3
SYST_CSR, C1.6.2 DHCSR; ARMv8-M ARM DDI 0553 for STTNS, BFHFNMINS and the
banked SysTick; the CMSDK UART is DDI 0479, 4.3):

| Hypothesis | Decided by |
|---|---|
| A receive ring too small for a frame | `rx_dropped` non-zero; `rx_head - rx_tail` at the ring's size less one |
| An active exception holding the priority | ICSR VECTACTIVE non-zero or RETTOBASE 0; a bit set in IABR |
| SysTick's priority masked | SHPR3 bits 31:24 against the active exception's, from SHPR or IPR |
| A stale vector table | VTOR not the running slot's base (`APP_BASE`) |
| The other security state's SysTick | AIRCR.BFHFNMINS, ICSR.STTNS, and which bank's CSR has ENABLE and COUNTFLAG |
| A debugger's hold | DHCSR C_HALT or C_MASKINTS set |
| A byte the UART lost | STATE RXOVR (bit 3) set, or RXFULL (bit 1) set with RX enabled in CTRL and INTSTATUS RX set |
| A host-side framing or timeout fault | the ring and framer empty and idle, `rx_dropped` 0: the bytes never arrived |

The A/B row's intermittent timeouts on OTA_DATA (0x69) and OTA_AUTH (0x6f)
were the first row. Both requests are 70 bytes, a 60-byte chunk and a 64-byte
signature; QEMU's CMSDK UART offers the next byte as soon as the guest reads
DATA, with no baud rate between them, and the 64-byte receive ring held 63. A
main loop slow to reach the transport let the ISR drop the frame's tail; the
framer then gave up after `TFW_FRAME_IDLE_MS` and the host timed out. The
MPS2 boards' profiles now size the ring at 128, `hal_uart.c` asserts that it
holds `TFW_MAX_FRAME`, and `rx_dropped` counts any byte a full ring still
refuses.

The script boots each board for the suite twice, and twice more for the reset
cause and the fault below. Every boot uses the soft-tier image
(`CRYPTO=soft SOFT_ID=p256`, CAPS tier 1), which is the one tier both boards
build:

1. **`boot.bin`** is provisioned: `cm_image.c` patches a random UDS into it.
   This boot runs every row in the table below except one, including the
   OTA and app rows, and passes
   `DEVICEID`, the soft root's key from `addrs.env`, so the identity rows
   can verify against it.
2. **`boot-unprovisioned.bin`** is the same boot with its UDS slot left
   erased. It runs only `TestFirmwareAttestRefusedUnprovisioned`, which
   needs a device that has not been provisioned: the soft root reports
   NOT_PROVISIONED and the application refuses ATTEST.

A third boot of `boot.bin`, `reset`, runs `TestQEMUResetCause`
(`sdk/go/cmd/tfwctl/reset_qemu_test.go`), the reset-cause path the ADR's §3
names. RESET mode 0 ends in `tfw_cm_system_reset`, which writes the
reset-request marker into SRAM and sets `AIRCR.SYSRESETREQ`; QEMU's armv7m
NVIC (`hw/intc/armv7m_nvic.c`) turns that into a system reset, which leaves
the SRAM as it was (`hw/core/loader.c`'s `rom_reset` reloads only the images
QEMU loaded). tfw-boot's reset handler reads the marker, clears it, decodes it
through `src/hal/cortex-m/hal_reset_decode.h` and hands the cause to the
application. SYS_INFO reports `POWER_ON` (1) after QMP's `system_reset`,
which writes no marker; `SOFTWARE` (5) after RESET; and `POWER_ON` again after
the next QMP reset. Before s15 the request marker decoded to `UNKNOWN` (0).
`test/unit/test_cm_reset_cause.c` pins the table on the host.

A fourth boot of `boot.bin`, `fault`, runs `TestQEMUFaultPanic`
(`sdk/go/cmd/tfwctl/fault_qemu_test.go`), the HardFault path the ADR's §3
names. The HardFault, MemManage, BusFault and UsageFault vectors go to
`tfw_cm_fault`, which writes the panic marker and asks for the same
`AIRCR.SYSRESETREQ` reset; tfw-boot decodes it as `PANIC` (6), the wire's
value for a reset the image's fatal-error handler took. MemManage, BusFault and
UsageFault are not enabled in SHCSR, so each escalates to HardFault (ARMv7-M
ARM DDI 0403E, priority escalation); the NMI still parks in `tfw_cm_unhandled`, so the
watchdog's second expiry resets with its own marker. The row stops the guest
through QEMU's gdbstub (HMP `gdbserver` on `<qmp socket>.fault.gdb`), sets
its PC to `0xE0000000` and detaches. QEMU makes that region execute-never
(`target/arm/ptw.c`, `m_is_system_region`), so the next fetch faults. SYS_INFO
then reads `PANIC`, and `POWER_ON` after the next QMP reset. A trial image that
faults is rolled back at that reset, not at the watchdog's bite.

Before it resets, `tfw_cm_fault` keeps the faulting frame
(`src/hal/cortex-m/hal_fault_frame.h`, since s17). Its entry is a naked stub
that picks MSP or PSP by EXC_RETURN bit 2. The record copies the stacked
return address, LR and xPSR from the basic frame (ARMv7-M ARM DDI 0403E,
B1.5.6; the same frame in ARMv8-M, DDI 0553) into three words at
`RAM_END - 127`. Both images place that section there from the same
`mk/arch/cortex-m.mk` line, and the reset does not clear it. A stack pointer
whose frame would leave the image's SRAM is not read, and all three words
then say `0xFFFFFFFF`. The fault row reads the record through the gdbstub
after the reset and requires the PC to be `0xE0000000`.
`test/unit/test_cm_fault_frame.c` pins the save on the host.

Every suite row on each board. **Runs** means the row exercises the command.
**CAPS lacks bit N** means the row runs, finds bit N clear and checks the
board's refusal. **Refused** means the row finds that the image lacks what
CAPS cannot report, and checks the refusal's status code. The CAPS bits are
the same on both boards, because they boot the same tier.

| Row (`TestFirmware…`) | mps2-an505 (0x40) | mps2-an500 (0x42) |
|---|---|---|
| `Ping`, `PingDeliversTheBootEventWithNoBootWait`, `PingAfterAMalformedBurst`, `Caps`, `Sys`, `Pin`, `Write`, `Timer` | runs | runs |
| `Analog` | CAPS lacks bit 2 (Analog) | CAPS lacks bit 2 (Analog) |
| `PWM` | CAPS lacks bit 3 (PWM) | CAPS lacks bit 3 (PWM) |
| `EEPROM` | CAPS lacks bit 6 (EEPROM) | CAPS lacks bit 6 (EEPROM) |
| `I2C`, `I2CConfig` | CAPS lacks bit 7 (I2C) | CAPS lacks bit 7 (I2C) |
| `SPIConfig` | CAPS lacks bit 8 (SPI) | CAPS lacks bit 8 (SPI) |
| `OneWireAndPulseIn` | CAPS lacks bits 9 (OneWire) and 5 (Pulse) | CAPS lacks bits 9 (OneWire) and 5 (Pulse) |
| `AttestRefusedUnprovisioned` | runs, on `boot-unprovisioned.bin` | runs, on `boot-unprovisioned.bin` |
| `AttestEvidenceFrame`, `EvidenceFrameRejectsTamperingAndOtherKeys` | runs (bit 10, Attest) | runs (bit 10, Attest) |
| `SoftwareTierStillCannotIssueACertificate` | runs | runs |
| `AttestCertIsDICEEvidenceBoundToTheCallersNonce`, `AttestCertWithoutAnApplicationCarriesTheFirmwareMeasurement`, `AttestCertSignatureVerifiesUnderTheDeviceIDKey`, `AttestCertSignatureRejectsTamperingAndOtherKeys`, `AttestCertDiffersWithTheNonce`, `EvidenceAndCertificateShareOneKey`, `ElementReportsItsRootOfTrust`, `BlankElementRefusesToAttest` | CAPS lacks bit 13 (AliasCert) | CAPS lacks bit 13 (AliasCert) |
| `AuthSessionIsAdvertisedByTheElementTier`, `AnOperatorSignatureOpensTheSession`, `ASignatureFromAnotherKeyDoesNotOpenTheSession`, `ASignatureBoundToAnotherDevicesSerialIsRefused`, `EachAuthTakesAFreshChallenge` | CAPS lacks bit 14 (AuthSession) | CAPS lacks bit 14 (AuthSession) |
| `AttestCSRPrintsTheSignatureAsHex`, `AttestCSRSignatureVerifiesOverTheKnownDigest`, `AttestCSRSignatureIsOverTheNonce`, `AttestCSRRefusesBWithNoCertificateMintedForIt`, `CapsNamesAttestCSROnADeviceThatOffersIt` | CAPS lacks bit 15 (AttestCSR) | CAPS lacks bit 15 (AttestCSR) |
| `AttestCSRReportsADeviceThatCannotIssueOne`, `ElementWithoutTheCapsBitIsRefusedAsUnsupported` | runs (tfwctl refuses on CAPS) | runs (tfwctl refuses on CAPS) |
| `OTAAuthLetsTheCommandPastTheGate`, `OTAInfo` | base image: refused, no stager, every OTA command `ERR_UNKNOWN_CMD` | the same |
| `OTAActivate`, `OTAConfirm` | base image: CAPS lacks bit 14 (AuthSession); then refused, no stager, every OTA command `ERR_UNKNOWN_CMD` | the same |
| `AppReportsItsApplication` | refused: no application, APP_INFO `ERR_UNKNOWN_CMD` | refused: same |
| `AppIsAnUnknownCommandWithoutAnApplication` | runs | runs |

**The staging image's OTA rows.** On both boards the wire run boots a
second image: the application built with `OTA_STAGE=1`, under tfw-boot's
store row (`p256-soft:0:1`). That boot runs the firmware, OTA and app rows.
The firmware and app rows match the table above. The OTA rows differ:

| Row (`TestFirmware…`) | mps2-an505 (0x40) | mps2-an500 (0x42) |
|---|---|---|
| `OTAAuthLetsTheCommandPastTheGate` | runs: stages under the CAPS board id; OTA_CONFIRM is `ERR_UNKNOWN_CMD`; OTA_ACTIVATE gets past the gate | the same |
| `OTAInfo` | refused: OTA_INFO and OTA_CONFIRM `ERR_UNKNOWN_CMD` (single slot, no `BOOT_AB`) | the same |
| `OTAActivate` | CAPS lacks bit 14 (AuthSession); soft tier, so OTA_ACTIVATE sent with no OTA_AUTH is checked for `ERR_AUTH_REQUIRED` | the same |
| `OTAConfirm` | CAPS lacks bit 14 (AuthSession); OTA_CONFIRM `ERR_UNKNOWN_CMD` | the same |

**The OTA cycle.** A third boot, on a QEMU of its own, runs
`TestQEMUOTACycle` (`sdk/go/cmd/tfwctl/ota_cycle_qemu_test.go`, build tag
`qemu`, `make cortex-m-qemu-cycle`). `cm_image` signs the base application
as the image to stage: `staged.bin`, whole pages with a version-2 trailer for
the board, and `activate.sig`, the ES256 activate authorization over its
measurement. The row:

1. checks that ATTEST's evidence does not already carry `staged.bin`'s
   measurement;
2. stages `staged.bin`, and checks that OTA_STATUS reports it complete;
3. checks that OTA_INFO is refused as `ERR_UNKNOWN_CMD`;
4. sends OTA_AUTH with `activate.sig` and OTA_ACTIVATE for the next reset;
5. checks that OTA_CONFIRM is refused as `ERR_UNKNOWN_CMD`;
6. resets through QMP. In the boot that follows, before any further reset,
   it checks that ATTEST's evidence carries `staged.bin`'s measurement, and
   that OTA_STATUS is `ERR_UNKNOWN_CMD`, because the installed image has no
   stager.

Every wait is the peer seam's and is bounded. The script's trap kills QEMU.

**Why some OTA rows are refused.** CAPS has no bit for the stager, for
OTA_INFO or OTA_CONFIRM, or for an application. Each OTA row therefore asks
OTA_STATUS first, which reads and changes nothing. When a board answers
`ERR_UNKNOWN_CMD`, the row checks that the board refuses every OTA command
(0x68 to 0x6F) the same way. A stager without OTA_INFO must also refuse
OTA_CONFIRM as unknown. `AppReportsItsApplication` checks APP_INFO's refusal
in the same way. A simulator always stages, has both commands and links its
application, so on tfwsim the rows run in full. The reasons are:

- **Application stager.** Both profiles name a store (`OTA_STORE := mem`,
  [below](#the-image-store)), so `OTA_STAGE=1` links `ota_stage.c`,
  `ota_prev.c` and OTA_ACTIVATE. The base image is built without it.
  OTA_INFO and OTA_CONFIRM (`ota_ctl.c`, ADR decision 8) are linked only
  where tfw-boot runs the trial over two slots (`BOOT_AB`). The single-slot
  build of each MPS2 profile, which these rows run, answers those two
  `ERR_UNKNOWN_CMD`. The two-slot build (`BOOT_AB=1`,
  [below](#two-slots)) links them.
- **The soft tier's gate.** It is open (`src/core/auth_session_none.c`), so
  OTA_ACTIVATE with any OTA_AUTH reaches the stager. The authorization is
  checked by tfw-boot at the next reset. One with the wrong key is refused,
  and tfw-boot forgets the mark (`boot/boot_stage.c`). An activate left by
  `OTAAuthLetsTheCommandPastTheGate` therefore installs nothing.
- **tfw-boot's rows.** `mk/boot.mk` adds `mps2-an505` with OTA in boot
  (`…:1:0`, single slot) and a store row on each board (`…:0:1`, whose
  `boot_prev.c` copies the staged image in and PREV back), and an A/B row
  on each (`…:0:1:ab`, [below](#two-slots)). The soft tier
  verifies P-256 on every row. None of them is measured yet: the lanes that
  added them had no Arm toolchain, so `make bootloader-check` on the CI image
  gives the first bytes-used figure.
- **The reset.** QEMU writes the loaded images back into the code regions on
  every `system_reset`, before the core runs. After a reset the slot holds
  the factory image again. tfw-boot finds the mark, which is still in the
  store, and copies the staged image over the factory image. A successful
  copy leaves the mark in place: tfw-boot forgets it only for a refused
  authorization or a failed install (`boot/boot_stage.c`,
  `boot/boot_main.c`). A later reset therefore starts again from the
  reloaded factory image, and what it installs depends on tfw-boot's checks
  at that point. The cycle row claims only the boot that the image was
  activated into.

**References.** The core follows the Armv8-M Architecture Reference Manual
(DDI 0553) and the AAPCS (IHI 0042), whose 8-byte stack alignment at a public
interface sets the stack top in `mk/arch/cortex-m.mk`. The interrupt numbers
are NVIC lines, the core's IRQ[n] (AN505, DAI 0505, Table 7-1): UART0 on 32
and 33 on mps2-an505, 0 and 1 on mps2-an500 (each `board_profile.h`). They are
listed, and the AAPCS is copied, in
[`../reference/README.md`](../reference/README.md).

## The image store

QEMU models no SPI NOR on either MPS2 machine. What it does model is RAM
that its reset leaves alone: `hw/core/loader.c`'s `rom_reset` copies back
only the blobs it loaded from the ELF or raw images. The code bank is 4 MiB
on both boards (SSRAM-1 and -2 at `0x38000000` on AN505, `hw/arm/mps2-tz.c`;
SSRAM1 at `0` on AN500, `hw/arm/mps2.c`), and the images use 160 KiB of it.
The rest keeps what was written into it across a reset. It is lost only when
QEMU itself is restarted. The platform block already lives in that remainder
(`TFW_CM_PLATFORM_BASE`), and the store does too:

| Board | Base | Size | Layout (offsets) |
|---|---|---|---|
| mps2-an505 | `0x38200000` | 264 KiB | STAGE `0x000000` 128 KiB, MANIFEST `0x020000`, PREV `0x021000` 128 KiB, MARK `0x041000` |
| mps2-an500 | `0x00200000` | 264 KiB | the same |

Two drivers serve the same span. `src/store/mem.c` is the application's side
of `src/core/ota_store.h`. It sits in `src/store/` beside `spi_nor.c` because
`mk/firmware.mk` selects the store as `src/store/$(OTA_STORE).c`.
`boot/hal/cortex-m/boot_ota_store_cortex-m.c` is tfw-boot's side: it answers
the five SPI NOR opcodes that `boot/boot_stage.c` sends (JEDEC ID, status,
write enable, read, page program) and refuses any other. Both keep a NOR's
rules: an erase sets a whole 4 KiB sector to `0xFF`, and a write or a program
only clears bits. A stager that forgot an erase would therefore fail here as
it fails on a real part. `test/unit/boot/test_store_mem.c` runs both drivers
over one host array.

### Two slots

`BOOT_AB=1` builds both MPS2 boards with two slots, swap-move as on the
ATmega2560 (ADR decision 7, `boot/boot_swap.c`): the stager writes the
secondary, tfw-boot swaps it into the primary for a trial, OTA_CONFIRM
settles it, and a trial that is not confirmed swaps back at the next reset.
The application goes to `build/<board>/soft-stage-ab` (`OTA_STAGE=1` and
`BOOT_AB=1` each name the directory), beside the single-slot build's;
`make firmware-dir` prints it.
`mk/boot.mk` carries the row per board as `mps2-an505:soft:soft:p256-soft:0:1:ab`
(and `mps2-an500`'s), P-256 verify as on every row; `:ab` is `BOOT_AB=1`.

Both slots and the platform block must be memory that QEMU does not load,
or its reset would put them back. QEMU therefore loads the application and
the platform block at factory copies, and tfw-boot copies each one to its
home once while that home is blank (QEMU starts every RAM zeroed,
`boot/hal/cortex-m/boot_factory.c`), as a programmer would on a part with
flash. A reset after that puts back only the factory copies, which nothing
reads again.

| mps2-an505 | mps2-an500 | Span | Holds |
|---|---|---|---|
| `0x38000000` | `0x00008000` | 128 KiB | factory application: loaded, put back on reset |
| `0x38040000` | `0x00040000` | 128 KiB + 512 B | primary slot: the image that runs |
| `0x38060200` | `0x00060200` | 128 KiB | secondary slot: where the stager writes |
| `0x38200000` | `0x00200000` | 264 KiB | the image store, as above |
| `0x383FE000` | `0x003FE000` | 4 KiB | factory platform block: loaded, put back on reset |
| `0x383FF000` | `0x003FF000` | 4 KiB | platform block; the swap record at `0x0E0` and OTA_CONFIRM's request at `0x0EA` past it |

A slot moves in 256 units of 512 bytes (`TFW_SWAP_UNIT`), so the swap
record's one-byte step still covers it. `src/hal/cortex-m/hal_eeprom.c` lets
the application write the platform window past the block, which is where
OTA_CONFIRM's request goes. A write into the block itself is refused.

The A/B build has a QEMU wire run of its own. On both boards,
`make integration-test-cortex-m-wire` runs `cortex-m-qemu-ab-wire` after the
store row. CI's image job runs that target. It builds the A/B stager
(`OTA_STAGE=1 BOOT_AB=1`) and the `…:0:1:ab` tfw-boot. `cm_image` signs that
same application three times: as `app.bin` at version 1, and tagged as
`staged.bin` at version 2 and as `below-floor.bin` at version 1. The tag is
16 bytes after the image's last byte, so each staged copy measures apart from
the running one. `qemu_wire_test.sh … ab` then starts one QEMU per test, from
the factory copies. Every reset is QMP's `system_reset`, so the slots and the
block carry over from one boot to the next. Each test lives in
`sdk/go/cmd/tfwctl/ota_ab_qemu_test.go`, under the `qemu` build tag:

| Test | What it checks |
|---|---|
| `TestQEMUABInfo` | OTA_INFO 0 is 2 slots, active 0, GOOD, no flags, floor 0. OTA_INFO 1 is an empty store (IDLE). Slot 2 is refused with `ERR_RANGE`. |
| `TestQEMUABConfirm` | Stage, OTA_AUTH and OTA_ACTIVATE put `staged.bin` at PENDING version 2 in slot 1. The reset runs it on trial (TRIAL, flag `0x04`), and OTA_CONFIRM is accepted. The next reset keeps it at GOOD, floor 2, and so does the reset after that. |
| `TestQEMUABRollback` | The same trial, with no confirm. The reset swaps the factory image back in, at GOOD, flag `0x01` (fell back) and floor 0. Slot 1 is FAILED, and OTA_CONFIRM is `ERR_BAD_STATE`. |
| `TestQEMUABFloor` | The confirm as above, then `below-floor.bin` is staged and activated (PENDING version 1). tfw-boot refuses it at the reset (`tfw_boot_rollback_permit`). The confirmed image runs on at GOOD, no flags, floor 2, and OTA_CONFIRM is `ERR_BAD_STATE`. |
| `TestQEMUABWatchdog` | The same trial, which then stops petting: through the gdbstub, the test points the PC at the running table's NMI vector, `tfw_cm_unhandled`. The CMSDK watchdog's first expiry raises the NMI and the second resets the core, 8 s after the last pet. SYS_INFO reads `WATCHDOG` (4). The factory image runs again at GOOD, flag `0x01`, and slot 1 is FAILED. |

The refusal is tfw-boot's alone, so no status on the wire carries it. What
the host sees is the image that keeps running and a floor that stays at 2.
Each A/B board profile gives the trial a 60 s deadline
(`TFW_OTA_TRIAL_TIMEOUT_S`), as on every board with a trial. The tests reset
long before it runs out. The single-slot builds keep their rows and refusals
above.

The `TestEthernet*` rows need `HW_ETH` and a network block, and neither MPS2
board has one.

QEMU has no i.MX 95, so this proves the core, tfw-boot and the wire on a
32-bit core, never a vendor SoC. The i.MX 95's Cortex-M7 is the board
`frdm-imx95-m7` (SPEC row `imx95`). `firmware-matrix` builds it, but nothing
runs it. Its map and SoC directory are in
[`../multi-arch/imx95-design.md`](../multi-arch/imx95-design.md#9-the-board-as-built-frdm-imx95-m7).

## Slices

The row's slices, from `git log` on its branches (`row/cortex-m-s<n>`, tip
commit in brackets):

- s1 (`91b6ccdc`): ADRs #14 and #15, the i.MX 95 M7 as reference part and the DICE root per part.
- s2 (`3772267f`): `mps2-an500`, a Cortex-M7 on QEMU, as board `0x42`, with its tfw-boot row.
- s3 (`1d653a1b`): why the tfwctl wire suite could not yet reach the QEMU boards.
- s4 (`e9a0feaf`): the tfwctl wire suite runs on both QEMU boards.
- s5 (`76c030cb`): the dice, auth and attestcsr rows join the QEMU wire run.
- s6 (`30fcc691`): the OTA and app rows join the QEMU wire run.
- s7 (`65e1b909`): a memory image store on both boards, and tfw-boot rows that read it.
- s8 (`b7ac733a`): the staged image in the wire run, and the OTA cycle row.
- s9 (`c2a48884`): two slots on the MPS2 boards, and a tfw-boot A/B row per board.
- s10 (`3278072a`): the A/B row's QEMU wire run, the failed-row register dump, and the 70-byte UART receive ring.
- s11 (`6dfa1782`): SPI on the MPS2 boards, a PL022 master on shield 0 with its select in MISC.
- s12 (`24abf89d`): the SPI=1 row on both boards, its select at pin 2 and the row test.
- s13 (`b421ed4c`): a QEMU wire row drives SPI_XFER through tfwctl, and the PL022 and MPS2 sources.
- s14 (`ac10ef66`): the I2C=1 row on mps2-an500, and a QEMU wire row drives I2C_XFER through tfwctl on both boards.
- s15 (`0f53db0a`): `soft:I2C=1` in mps2-an505's `BOARD_ROWS`; a commanded reset reads as `SOFTWARE`, with a QEMU reset-cause wire row on both boards.
- s16 (`c3af4631`): a HardFault resets with the panic marker and reads as `PANIC`, with a QEMU fault wire row on both boards.
- s17: the fault record keeps the faulting PC, LR and xPSR across the panic reset, and an A/B wire row where the trial's watchdog bites and the trial is rolled back.

The I2C master over the shield 0 SBCon (`b7bfec3f`) came between s10 and s11 on
unnumbered branches.

## Bench boards

The Cortex-M boards on the bench have profile pages, with vendor citations and
no port: [`stm32h573i-dk.md`](../boards/stm32h573i-dk.md),
[`rp2350-pico2w.md`](../boards/rp2350-pico2w.md) and
[`nrf54l15-dk.md`](../boards/nrf54l15-dk.md). None has a board id. The vendor
documents the ADRs cite are listed in
[`../reference/README.md`](../reference/README.md), "Cortex-M sources".

The multi-architecture plan
these ports belong to is [`../multi-arch/architecture.md`](../multi-arch/architecture.md).
