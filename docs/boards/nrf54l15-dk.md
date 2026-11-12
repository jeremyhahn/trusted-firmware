# nRF54L15-DK

> **Status: INFORMATIVE** · SPEC rows: `nrf54l15`, `cortex-m` (future) · Scope:
> `boards/nrf54l15-dk`, `src/hal/cortex-m/nrf54l/`, `boot/hal/cortex-m/nrf54l/` · Last verified: 2026-10-20
>
> A profile of a board on the bench and of the port's first two slices (#17).
> They build; nothing here has been measured on the board, because no board
> was attached to it. Each value is cited to the vendor's document or source. A
> value that could not be read from one is written **not confirmed**. The
> normative contract the port follows is [`adding-a-board.md`](adding-a-board.md);
> the decisions it follows are ADR #14
> ([`../cortex-m/adr-reference-part-and-emulation.md`](../cortex-m/adr-reference-part-and-emulation.md))
> and ADR #15 ([`../cortex-m/adr-trust-model-vs-tf-m.md`](../cortex-m/adr-trust-model-vs-tf-m.md)),
> both PROPOSED.

Documents cited:

- **[UG]** *nRF54L15 DK Hardware*, Nordic Semiconductor user guide (PDF dated
  2025-03-07; document version **not confirmed**). It describes the board
  PCA10156 and mentions DK v0.9.1. Sections cited by title.
- **[PS]** *nRF54L15 | nRF54L10 | nRF54L05 Datasheet*, Nordic Semiconductor
  (docs.nordicsemi.com, `ps_nrf54l15`; version **not confirmed**). Sections
  cited by title.
- **[MDK]** the nrfx MDK, Nordic's device headers and `SystemInit`
  (<https://github.com/NordicSemiconductor/nrfx>, revision `d1f2c35a4820961f4f7b7b2ece007f8e037842db`),
  files cited by name.
- **[Z]** Zephyr's board files for the DK, `boards/nordic/nrf54l15dk/`, and
  its SoC device tree, `dts/vendor/nordic/`
  (<https://github.com/zephyrproject-rtos/zephyr>, revision
  `0c57145c9c3377ff19f273b6fb6bd7c8f93e97cd`), files cited by name.

Both sources are listed in [`../reference/README.md`](../reference/README.md),
"Board sources".

## The board

| | | Source |
|---|---|---|
| Board id | `0x45`, in [`board-id-registry.md`](board-id-registry.md) and `sdk/go/boardid.go` (`BoardNRF54L15DK`) | |
| MCU | nRF54L15 SoC, QFN48 package, on PCA10156 | [UG], introduction |
| Core | Arm Cortex-M33, run at 128 MHz (`PLL.FREQ` = `CK128M`, which `SystemInit` sets) | [MDK] `system_nrf54l.c` |
| Non-volatile memory | 1524 KB of RRAM at `0x00000000`, not flash. The application reads it as memory and never writes it; tfw-boot writes the platform block through the RRAM controller (below) | [Z] `dts/vendor/nordic/nrf54l15.dtsi` (`cpuapp_rram`); [PS], "NVM — Non-volatile memory" |
| RAM | 256 KB at `0x20000000` | [Z] `dts/vendor/nordic/nrf54l15.dtsi` (`cpuapp_sram`) |
| Debug probe | SEGGER J-Link OB programmer/debugger on the board, over USB, which also powers the board; a 2x5 SWD Debug In connector and a 20-pin trace footprint. The interface MCU's part number is **not confirmed** | [UG], introduction; "Debugger"; "Debug input and trace" |
| Console to the host | two UARTs through the J-Link OB's virtual serial ports. The debugger's UART pins are tri-stated until a terminal opens the port and asserts DTR; RTS/CTS are free for other use when hardware flow control is off; the signals pass through analog switches set from the Board Configurator | [UG], introduction; "Virtual serial ports" |
| Console pins | UARTE20, TXD P1.4 and RXD P1.5 | [Z] `nrf54l15dk_nrf54l_05_10_15-pinctrl.dtsi`, `nrf54l15dk_nrf54l_05_10_15.dtsi` |
| LEDs, buttons | LED0-3 on P2.9, P1.10, P2.7, P1.14; BUTTON0-3 on P1.13, P1.9, P1.8, P0.4, active low | [Z] `nrf54l15dk_common.dtsi` |
| Ethernet | **none**. Radios: Bluetooth Low Energy, 802.15.4 (Thread), 2.4 GHz proprietary, NFC | [UG], introduction |
| Power | nPM1300 PMIC, 1.8 V to 3.3 V user-programmable from USB | [UG], introduction |

## The port (#17)

The board on the generic Cortex-M port, application only, soft tier. The
pin table is the four LEDs then the four buttons. `make firmware
BOARD=nrf54l15-dk CRYPTO=soft` builds it in `firmware-matrix`, and CI builds
it again in the toolchain image.

| | What the slice does | Source |
|---|---|---|
| Memory map | tfw-boot's 32 KB at `0x0`, the application from `0x8000`, the platform block in the last 4 KB below `0x17D000`; RAM from `0x20000000` | `profile.mk`, `board_profile.h` |
| Reset | the vendor's `SystemInit` for a Secure image, less the SAU, approtect and tamper set-up: `PLL.FREQ`, the FICR trim copy, the device configuration word, errata 31, 32 and 40 on the first revision (part `0x1C`, revision `0x01`), the `RESETREAS` pin-reset fold | [MDK] `system_nrf54l.c`, `nrf54l_erratas.h` |
| Clock | the CPU at 128 MHz; the high-frequency crystal started, not waited on | [MDK] `system_nrf54l.c` |
| Console | UARTE20 at the baud rates of the vendor's `BAUDRATE` table (its clock is 16 MHz), 8N1, no flow control, one byte per EasyDMA transfer each way, on IRQ 198 | [MDK] `nrf54l15_types.h`, `nrf54l15_application_peripherals.h` |
| Watchdog | WDT30 on the 32.768 kHz clock, 8 s, with `STOPEN` so a later reset can stop it | [MDK] `nrf54l15_types.h` |
| PWM | PWM20, channel 0 on LED1 (P1.10) and channel 1 on LED3 (P1.14), the DK's LEDs on port 1. Up-counting, `PRESCALER` `DIV_16` (1 MHz), `COUNTERTOP` 1000: a 1 kHz period, the wire's 8-bit duty scaled to 1000 counts. The compare values are a four-half-word sequence in RAM (`DECODER` `LOAD` `Individual`), read by EasyDMA (`DMA.SEQ[0].PTR`, `MAXCNT` in bytes) and played by `TASKS_DMA.SEQ[0].START`; bit 15 of a value set for a pin high first. Duty 0 and 255 disconnect the channel's `PSEL.OUT` and drive the pin as a GPIO; stop disconnects it, and the last channel out triggers `TASKS_STOP`. Configured on first use (`src/hal/cortex-m/nrf54l/hal_pwm.c`, host-tested against a register model) | [MDK] `nrf54l15_types.h` (`NRF_PWM_Type`, `PWM_*`), `nrf54l15_global.h` (`NRF_PWM20_S_BASE`); [Z] `nrf54l15dk_nrf54l_05_10_15-pinctrl.dtsi` (`pwm20_default`), `nrf54l15dk_common.dtsi`, `drivers/pwm/pwm_nrfx.c` (`PWM_NRFX_CH_POLARITY_MASK`) |
| Vectors | the core's 96 entries, then IRQs 80-198 in `.vectors.soc`; the linker script asserts the two meet and the table sits on a 1 KB boundary | `mk/arch/cortex-m/nrf54l.ld` |

## tfw-boot (#17, slice 2)

The core boots from `0x0`, tfw-boot's region. tfw-boot has one row on this
board (`BOOT_SUPPORTED` in `mk/boot.mk`), `nrf54l15-dk:soft:soft:p256-soft:0:0`,
the same as `mps2-an505`'s. It uses the soft tier and soft measurement, and
verifies the image's ECDSA P-256 signature in software on every boot. It has
no OTA and no store. It hands the soft DeviceID seed to the application in the
handoff block. `make bootloader-check` builds it. Nothing has run on the board.

| | What tfw-boot does | Source |
|---|---|---|
| Core half | `boot/hal/cortex-m/boot_hal_cortex-m.c`, shared with `mps2-an505`. It range-checks every write and calls the SoC's `tfw_cm_boot_code_write` (`boot/hal/cortex-m/boot_soc.h`) to make it | |
| Console | UARTE20 on the application's pins and baud table, polled: `INTEN` stays 0. One byte goes each way per EasyDMA transfer. A receive transfer is always armed, and its end event means a byte is waiting. A send waits for `TXDRDY`. Before the jump the receiver is stopped (`RXTO`), and the UARTE and both pins go back to their reset values (`boot/hal/cortex-m/nrf54l/boot_uart.c`) | [MDK] `nrf54l15_types.h` |
| RRAM writes | `boot/hal/cortex-m/nrf54l/boot_rram.c` follows the vendor's sequence. It sets `CONFIG.WEN` with the write buffer off and waits for `READY`. Before each store it waits for `READYNEXT`, storing a word where the address is aligned and a byte otherwise. Then it puts `CONFIG` back and waits for `READY`. The vendor's waits are unbounded; these are bounded, and a write that times out fails. While `WEN` is set every RRAM byte is writable, so the file refuses any span below tfw-boot's region end or past the RRAM (`tfw_nrf54l_rram_span`, host-tested) | [MDK] `haly/nrfy_rramc.h`, `nrf54l15_types.h` |
| What it writes | the platform block, rewritten whole when the rollback floor advances. The OTA swap's journal and confirm byte (`boot/boot_swap.c`) and page writes are not on this row, which has no OTA | `boot/boot_store.c` |

## The reset cause (#17, slice 3)

The reset handler, in tfw-boot and in the application alike, hands its reset
marker to the SoC (`tfw_cm_soc_reset_cause`, `src/hal/cortex-m/hal_early.h`).
`mps2-tz` decodes the marker alone, as before. `nrf54l` reads `RESET.RESETREAS`
(`0x5010E600`), decodes it and clears it by writing all ones: the register is
write-1-to-clear, as `SystemInit`'s pin-reset fold shows
(`src/hal/cortex-m/nrf54l/soc_reset.c`, host-tested against a fake word).

| `RESETREAS` | Cause |
|---|---|
| nothing latched | power-on |
| `RESETPIN` alone | external |
| `DOG0`, `DOG1` or both, alone | watchdog |
| `SREQ` alone, with the firmware's reset marker | software |
| `SREQ` alone without the marker (a debugger can set it), the debugger's three resets, `LOCKUP`, the five wakes from System OFF, `SECTAMPER`, bits 14-31, or two causes at once | unknown |

The MDK defines no brownout bit, so a brownout reads as power-on. Watchdog
means a watchdog bit and nothing else, because the update-request gate honours
it. Bits, offsets and lines are cited in `soc_reset.c` ([MDK]
`nrf54l15_types.h`, `nrf54l15_global.h`, `system_nrf54l.c`).

## Security facilities

As Nordic names them, from [PS] section titles and their summaries. Register
detail is **not confirmed**; none is used by the first slice.

| | |
|---|---|
| TrustZone-M | hardware isolation between secure and non-secure resources as defined by Arm TrustZone, with peripherals assignable to either world ("TrustZone security"; "Peripherals with split security access") |
| KMU | Key management unit: keys in a dedicated RRAM region; the KMU and its key slots in SICR are accessible from secure mode only ("KMU — Key management unit") |
| CRACEN | Cryptographic accelerator engine. With the KMU, keys can be pushed to CRACEN so they stay protected even from secure-world code; it has DPA countermeasures ("CRACEN — Cryptographic accelerator engine"). Which curves it accelerates is **not confirmed** |
| Boot | an immutable boot region ("Immutable boot region"); Nordic does not call it "secure boot" in the section read |
| Tamper | TAMPC tamper controller and voltage glitch detectors ("TAMPC — Tamper controller"; "GLITCHDET — Voltage glitch detectors") |

## Upstream TF-M

As ADR #15 states it ("Platforms upstream at v2.3.1"): there is **no nRF54L15
port in upstream TF-M** at v2.3.1. Nordic carries TF-M for the part in its own
SDK.

## What this page bears on

- **ADR #15 open question 2** (vendor secure worlds): with no upstream port,
  this part's secure world is Nordic's TF-M or none, so the licence check of
  Nordic's SDK is on this board's path.
- **ADR #15 open question 4** (crypto tier values): a KMU key used through
  CRACEN is hidden even from secure code, which is a stronger claim than
  "TrustZone-isolated key".
- **The `nrf54l15` row's enrolment**: there is no Ethernet on this board.

## What is left in the row

In order:

1. The KMU, for the DeviceID's root. The MDK defines the KMU's registers
   (`KEYSLOT`, `SRC`, `METADATA`), not the layout of a slot or its metadata:
   it needs [PS], "KMU — Key management unit".
2. TrustZone: the SAU and SPU set-up `SystemInit` does and these slices leave
   out, approtect and the tamper controller. Locking tfw-boot's region with
   the immutable boot region belongs here. The MDK gives `UICR.BOOTCONF`'s
   and `RRAMC.REGION[n].CONFIG`'s fields, but not the unit of
   `BOOTCONF.SIZE`, where the boot region starts, or which `REGION` is the
   system's (it points to "the instantiation table"): that needs [PS],
   "Immutable boot region" and "NVM — Non-volatile memory". A `UICR` write
   needs the board as well.
3. CRACEN: it needs [PS], "CRACEN — Cryptographic accelerator engine".
4. The bench run on the DK: the first time tfw-boot and the application run
   on it. The writer is wired: SEGGER J-Link Commander (`JLinkExe`, device
   `NRF54L15_M33`) over the J-Link OB. `make board-record BOARD=nrf54l15-dk
   CM_PROBE_SERIAL=<serial>` saves the whole RRAM from `0x00000000`
   (`savebin`) and the UICR, 4 KiB at `0x00FFD000`, an address still to
   confirm against [PS] before the first record; `make flash` and
   `make boot-flash` refuse until that record exists, then `loadbin` and
   `verifybin`; `make board-restore` with
   `BOARD_RESTORE_IMAGE=build/records/<record>.flash.bin` writes it back. No
   UICR, APPROTECT or KMU write, no erase, no recover. Steps:
   [`../cortex-m/README.md`](../cortex-m/README.md).
5. OTA and the store, if the owner wants them on this board: the RRAM write
   path is already in place for both.
6. The SAADC. PWM is built (PWM20 on LED1 and LED3, the table above) and
   host-tested, and has not run on the board.

## What is not verified

CRACEN's algorithms, whether RAM survives a watchdog reset and whether WDT30
survives a soft reset (the port assumes neither), whether RAM survives an
`SREQ` reset (the reset marker needs it; if it does not, the firmware's own
restart reads as unknown), which of `DOG0` and `DOG1` is WDT30, and every value
above on the board itself. For the RRAM writes in particular, none of these is verified:

- whether the RRAMC's `REGION` permissions, as reset leaves them, let the
  Secure core write the platform block;
- what a store with `WEN` clear does (ignored or a fault);
- how long a write takes against the spin bounds.

For the PWM, none of these is verified on the board, and each needs [PS],
"PWM — Pulse width modulation":

- that PWM20 reaches port 1 only (the port drives no port 0 or port 2 pin
  with it, the reversible choice) and that its EasyDMA reads the sequence
  anywhere in RAM;
- that a sequence played once with `LOOP` 0 holds its last values until
  `TASKS_STOP`, and that a new `TASKS_DMA.SEQ[0].START` while it plays loads
  the new values;
- that a pin left set as a GPIO output takes the channel's level while its
  `PSEL.OUT` is connected, and that bit 15 set means high first (Zephyr's
  driver sets it for a channel not inverted);
- that `ENABLE` may stay set after `TASKS_STOP` with every channel
  disconnected (the port never clears it, rather than clearing it before
  `EVENTS_STOPPED`).
