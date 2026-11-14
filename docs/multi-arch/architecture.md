# Multi-architecture strategy

> **Status: REFERENCE** · Owner: Software Department · Last verified: 2026-10-25

AVR is implemented. The generic Cortex-M port boots on QEMU `mps2-an505`, and the
i.MX 95 M7 and ESP32-P4 ports are in progress (§7). Everything else here is planned
and marked with its issue.
Order of work: [ROADMAP.md](../../ROADMAP.md). The AVR gate ([#7](../../../../issues/7):
every primitive and a proven end-to-end example) is lifted for `esp32-p4` (#48), and the
Cortex-M ADRs (#14, #15) and the generic port on QEMU `mps2-an505` (#161) are ungated
(CA m623); it is not lifted for `optiga-trust-m` or `rp2350` ([SPEC](../SPEC.md)).

## 1. Goal

One repository holds every bare-metal target of the trusted platform: one wire
protocol, one Go SDK, one wire-vector set, one simulator. Per-architecture code lives
only behind the HAL, the boot HAL and the board profiles.

Escape hatch: if one architecture's build (ESP-IDF, say) bloats this repository, a thin
consumer repository uses this one's core and owns no protocol logic.

## 2. Portable today

- `src/core`, `src/caps`, `src/proto`
- Crypto providers: `src/crypto/soft`, `src/crypto/atecc608a`, `src/crypto/none`
- Boot policy under `boot/`: image trailer, verify, measure, CDI tiers, OTA session
- The Go SDK (`sdk/go`) and the literal wire vectors (`testdata`)
- The simulator: the firmware compiled for the host against the mock HAL

## 3. AVR-specific today, and where it moves

Phase 4 ([#21](../../../../issues/21)).

| Today | Moves to | Issue |
|---|---|---|
| ~~Build hardcodes `src/hal/avr`~~ **done:** `ARCH := avr` in each `profile.mk`, `mk/arch/$(ARCH).mk`, `HAL_DIR := src/hal/$(ARCH)` | `ARCH`-selected HAL directory | [#8](../../../../issues/8) |
| ~~`tfw_pinref_t` / `tfw_pwmref_t` hold AVR register shapes~~ **done:** defined with the port and PWM row types in `src/hal/<arch>/hal_types.h` (the host's is `test/mock/hal_types.h`), opaque to core | opaque, HAL-defined refs | [#9](../../../../issues/9) |
| ~~`src/main.c` includes `<avr/interrupt.h>`, calls `sei()`~~ **done:** `tfw_hal_irq_enable()` | HAL interrupt-enable call | [#9](../../../../issues/9) |
| MCUSR reset-cause decoding in core and boot | HAL returns a neutral cause | [#10](../../../../issues/10) |
| Boot section at top of flash, 256-byte OTA page, EEPROM platform block, `F_CPU`-based session polls | boot HAL geometry, NVM and timing | [#11](../../../../issues/11) |
| ~~CAPS pin descriptor: 4-bit port and bit~~ **done:** a whole byte each, 6-byte page | wider, generalised descriptor | [#12](../../../../issues/12) |
| ~~`free_ram` is a u16~~ **done:** u16 kept, exact u32 appended to SYS_INFO | wider field | [#12](../../../../issues/12) |
| Conditional-confinement gate misses `src/crypto`, `src/main.c`, `tools/`, headers | gate covers them; arch-leak gate added | [#13](../../../../issues/13) |

Wire changes extend; ids and status codes never renumber.

## 4. Target layout (planned, #8)

- Each board's `profile.mk` declares `ARCH`. Boards stay flat under `boards/`.
- `mk/arch/<arch>.mk` holds toolchain and flags.
- `src/hal/<arch>/` and `boot/hal/<arch>/` hold the per-architecture code.
- The HAL is substituted at link time, never through a vtable. A vtable costs RAM and
  keeps every entry live, defeating `--gc-sections`. A function added to the HAL
  header must be implemented by the mock, or `make test` fails to link.
- Features are gated by including or excluding whole `.c` files, not `#if`.

## 5. Board-id registry

The board id is one byte (`TFW_BOARD_ID` in `boards/*/board_profile.h`). Ranges:

| Range | Family |
|---|---|
| `0x01`-`0x3F` | AVR |
| `0x40`-`0x7F` | Cortex-M |
| `0x80`-`0xBF` | ESP32 and other |
| `0xC0`-`0xFF` | unallocated |

Current ids: `0x01` ATmega328P, `0x02` ATmega2560, `0x03` ATmega32U4, `0x04` ATtiny85,
`0x40` Arm AN505 (Cortex-M33) as QEMU's `mps2-an505` models it, `0x41` NXP i.MX 95
Cortex-M7 on the FRDM-IMX95-PRO (provisional: [`imx95-design.md`](imx95-design.md)
question 20), `0x80` ESP32-P4 PoE board, `0x81` Espressif QEMU.
Ids extend; they never renumber. The registry is [`../boards/board-id-registry.md`](../boards/board-id-registry.md).

## 6. Roots of trust per architecture

Each choice is an ADR before any code.

- **AVR**: tfw-boot, with a software or ATECC608A tier.
- **Cortex-M**: the reference part and CI emulation target are in
  [`../cortex-m/adr-reference-part-and-emulation.md`](../cortex-m/adr-reference-part-and-emulation.md)
  ([#14](../../../../issues/14)), accepted on 2026-11-14 (owner ruling 632
  item 208 a). Its decision 1 records the SPEC's reference part, the Cortex-M7 of the NXP i.MX 95 on the FRDM-IMX95-PRO
  (SPEC rows `cortex-m` and `imx95`), checked against the two Cortex-M33 boards
  on the bench: the STM32H573I-DK, the second target (SPEC row `stm32h5`), and
  the nRF54L15-DK. The ruling answers the ADR's question 1: the M7 stays the
  reference part. CI emulates the generic port on QEMU
  `mps2-an505`. The hardware order is the SE052F, then the i.MX 95 M7, then the
  STM32H5 (ruling 269), whose port waits for [#79](../../../../issues/79).
  On the i.MX 95 the key tier is the EdgeLock secure enclave and AHAB secure
  boot verifies the M7 image ([`imx95-design.md`](imx95-design.md)); whether
  tfw-boot also runs on the M7 is that page's question 19. Trust model and
  Trusted Firmware-M (Arm's project; "TF-M" for short): [#15](../../../../issues/15),
  accepted on the same ruling in [`../cortex-m/adr-trust-model-vs-tf-m.md`](../cortex-m/adr-trust-model-vs-tf-m.md):
  TF-M Profile Medium with our DICE attestation partition on the M33, MPU-only
  isolation on the M7.
- **ESP32**: secure boot v2, flash encryption and the HMAC peripheral:
  [#16](../../../../issues/16). The P-256 DeviceID, layer 0's ES256 gate, OTA
  on `esp_ota` and the port's slices: [`esp32-p4-design.md`](esp32-p4-design.md).

## 7. Support matrix

| Architecture | Board | Crypto tier | Status |
|---|---|---|---|
| AVR | ATmega328P | ATECC608A only: `CRYPTO=soft` is refused (owner ruling 2026-10-17 (m522); `boards/atmega328p/profile.mk`) | supported |
| AVR | ATmega2560 | none, soft, ATECC608A | supported |
| AVR | ATmega32U4 | none, soft, ATECC608A | supported |
| AVR | ATtiny85 | none only (`boards/attiny85/profile.mk`) | supported |
| Cortex-M33 | Arm AN505 on QEMU `mps2-an505` (`boards/mps2-an505`, id `0x40`) | soft | in progress ([#161](../../../../issues/161)): tfw-boot verifies a P-256 signature in software and boots the firmware; CI runs `make integration-test-cortex-m` |
| Cortex-M7 | NXP i.MX 95 M7 on the FRDM-IMX95-PRO (`boards/frdm-imx95-m7`, provisional id `0x41`) | soft root; the EdgeLock enclave tier is not started | in progress ([#49](../../../../issues/49)): built in CI, not run; [`imx95-design.md`](imx95-design.md) (Row status) |
| Cortex-M33 | STM32H573I-DK; nRF54L15-DK | ADR [#14](../../../../issues/14), accepted | planned ([#22](../../../../issues/22); onboarding ADRs [#126](../../../../issues/126), [#129](../../../../issues/129)); the STM32H5 port waits for [#79](../../../../issues/79) |
| ESP32 | ESP32-P4 PoE board; Espressif QEMU (`esp32c3`, `esp32`) | secure boot v2, HMAC CDI, P-256 DeviceID from the UDS (`hmac-p256`, `boards/esp32p4-poe/profile.mk:13`); `none` | in progress ([#48](../../../../issues/48), ADR [#16](../../../../issues/16)) |
| Other | | | planned ([#17](../../../../issues/17)) |
