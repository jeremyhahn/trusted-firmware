# Multi-architecture strategy

> **Status: REFERENCE** · Owner: Software Department · Last verified: 2026-10-08

Only AVR is implemented. Everything else here is planned and marked with its issue.
Order of work: [ROADMAP.md](../../ROADMAP.md). No other-platform work starts until AVR
has every primitive and a proven end-to-end example (owner ruling 2026-10-08).

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

Phase 4 ([#21](http://git.example.internal/jeremyhahn/trusted-firmware/issues/21)).

| Today | Moves to | Issue |
|---|---|---|
| ~~Build hardcodes `src/hal/avr`~~ **done:** `ARCH := avr` in each `profile.mk`, `mk/arch/$(ARCH).mk`, `HAL_DIR := src/hal/$(ARCH)` | `ARCH`-selected HAL directory | [#8](http://git.example.internal/jeremyhahn/trusted-firmware/issues/8) |
| ~~`tfw_pinref_t` / `tfw_pwmref_t` hold AVR register shapes~~ **done:** defined with the port and PWM row types in `src/hal/<arch>/hal_types.h` (the host's is `test/mock/hal_types.h`), opaque to core | opaque, HAL-defined refs | [#9](http://git.example.internal/jeremyhahn/trusted-firmware/issues/9) |
| ~~`src/main.c` includes `<avr/interrupt.h>`, calls `sei()`~~ **done:** `tfw_hal_irq_enable()` | HAL interrupt-enable call | [#9](http://git.example.internal/jeremyhahn/trusted-firmware/issues/9) |
| MCUSR reset-cause decoding in core and boot | HAL returns a neutral cause | [#10](http://git.example.internal/jeremyhahn/trusted-firmware/issues/10) |
| Boot section at top of flash, 256-byte OTA page, EEPROM platform block, `F_CPU`-based session polls | boot HAL geometry, NVM and timing | [#11](http://git.example.internal/jeremyhahn/trusted-firmware/issues/11) |
| ~~CAPS pin descriptor: 4-bit port and bit~~ **done:** a whole byte each, 6-byte page | wider, generalised descriptor | [#12](http://git.example.internal/jeremyhahn/trusted-firmware/issues/12) |
| ~~`free_ram` is a u16~~ **done:** u16 kept, exact u32 appended to SYS_INFO | wider field | [#12](http://git.example.internal/jeremyhahn/trusted-firmware/issues/12) |
| Conditional-confinement gate misses `src/crypto`, `src/main.c`, `tools/`, headers | gate covers them; arch-leak gate added | [#13](http://git.example.internal/jeremyhahn/trusted-firmware/issues/13) |

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

Current ids: `0x01` ATmega328P, `0x02` ATmega2560, `0x03` ATmega32U4, `0x04` ATtiny85.
Ids extend; they never renumber. The registry is [`../boards/board-id-registry.md`](../boards/board-id-registry.md).

## 6. Roots of trust per architecture

Each choice is an ADR before any code.

- **AVR**: tfw-boot, with a software or ATECC608A tier.
- **Cortex-M33** (LPC55S69, MCXN, STM32U5): the vendor ROM provides secure boot and, on
  LPC55S6x, a DICE CDI. A ROM CDI tier replaces tfw-boot there. Part and CI emulation:
  [#14](http://git.example.internal/jeremyhahn/trusted-firmware/issues/14). Trust model and
  Trusted Firmware-M (Arm's project; "TF-M" for short): [#15](http://git.example.internal/jeremyhahn/trusted-firmware/issues/15).
- **ESP32**: secure boot v2, flash encryption and the DS peripheral:
  [#16](http://git.example.internal/jeremyhahn/trusted-firmware/issues/16).

## 7. Support matrix

| Architecture | Board | Crypto tier | Status |
|---|---|---|---|
| AVR | ATmega328P | none, soft, ATECC608A | supported |
| AVR | ATmega2560 | none, soft, ATECC608A | supported |
| AVR | ATmega32U4 | none, soft, ATECC608A | supported |
| AVR | ATtiny85 | none only (`boards/attiny85/profile.mk`) | supported |
| Cortex-M33 | LPC55S69, MCXN, STM32U5 | ROM CDI | planned ([#22](http://git.example.internal/jeremyhahn/trusted-firmware/issues/22)) |
| ESP32 | to be chosen | secure boot v2, DS | planned ([#23](http://git.example.internal/jeremyhahn/trusted-firmware/issues/23)) |
| Other | | | planned ([#17](http://git.example.internal/jeremyhahn/trusted-firmware/issues/17)) |
