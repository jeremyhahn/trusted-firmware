# Device capabilities: engineering

> **Status: NORMATIVE** · Scope: arduino-generic · Last verified: 2026-09-23
>
> Conflicts with this document are defects. Fix them here first.

## Exported surface

Every function below is declared in the header named and takes/returns
`ag_status_t` (`include/ag/status.h`) unless noted. `r`/`w` are the reader and
writer over the frame payload (`src/core/reader.h`, `src/core/writer.h`).

| header | handler | does |
|---|---|---|
| `src/caps/gpio.h` | `ag_gpio_cmd_mode`, `ag_gpio_cmd_read`, `ag_gpio_cmd_write` | direction, level in, level out |
| `src/caps/sched.h` | `ag_sched_cmd_set`, `_cancel`, `_query`; `ag_sched_init`, `ag_sched_poll`, `ag_sched_active` | arm/cancel/query a timed pin; `poll` runs from the main loop |
| `src/caps/analog.h` | `ag_analog_cmd_read`, `ag_analog_cmd_set_ref` | raw ADC count, reference select |
| `src/caps/pwm.h` | `ag_pwm_cmd_write`, `ag_tone_cmd_start`, `ag_tone_cmd_stop` | duty cycle; tone lives in its own `.c` for file-selection gating |
| `src/caps/pulse.h` | `ag_pulse_cmd_in`, `ag_pulse_cmd_capture` | one pulse width; edge-interval capture |
| `src/caps/storage.h` | `ag_storage_cmd_read`, `ag_storage_cmd_write` | EEPROM read/write |
| `src/caps/bus.h` | `ag_i2c_cmd_xfer`, `ag_spi_cmd_xfer`, `ag_ow_cmd_reset`, `_write`, `_read` | I2C, SPI, 1-Wire |
| `include/ag/board.h` | `ag_board_pin`, `ag_board_adc_channel`, `ag_board_pwm`, `ag_board_pin_port_bit`, `ag_board_pin_pwm_state`, `ag_board_pin_flags`, `ag_board_pin_count`, `ag_board_name` | resolve a logical pin; consulted by every handler above before touching hardware |
| `include/ag/hal.h` | `ag_hal_gpio_*`, `ag_hal_adc_*`, `ag_hal_pwm_*`, `ag_hal_tone_*`, `ag_hal_pulse_*`, `ag_hal_eeprom_*`, `ag_hal_i2c_xfer`, `ag_hal_spi_xfer`, `ag_hal_ow_*` | one implementation linked per image: `src/hal/avr/*.c` for the device, `test/mock/hal_mock.c` for the host |

Go SDK (`sdk/go/commands.go`) mirrors this 1:1 as `(*Device)` methods —
`SetPinMode`, `DigitalRead`/`Write`, `TimerSet`/`Cancel`/`Query`,
`AnalogRead`, `SetADCReference`, `PWMWrite`, `Tone`/`NoTone`, `PulseIn`,
`PulseCapture`, `EEPROMRead`/`Write`, `I2CTransfer`, `SPITransfer`,
`OneWireReset`/`Write`/`Read`.

## Invariants

- **A pin the firmware is using for itself refuses driving, not reading.**
  `resolve_writable()` in `gpio.c` checks `AG_PIN_RESERVED | AG_PIN_IN_USE`;
  `ag_gpio_cmd_read` does not, because observing the UART line is harmless
  and a blanket refusal would make wiring problems harder to diagnose.
- **The scheduler has one writer.** Only `ag_sched_poll()`, called from the
  main loop, mutates `g_slots`; the tick ISR only increments a counter. No
  `volatile`, no lock, and `test_sched.c` drives the 49.7-day wrap
  deterministically by setting `ag_hal_millis()` rather than waiting for it.
- **Re-arming a pin extends its timer rather than consuming a second slot.**
  A host that reissues `TIMER_SET` — routine for an irrigation controller —
  does not exhaust `AG_SCHED_SLOTS`.
- **A scheduled duration above `AG_SCHED_MAX_MS` (`0x7FFFFFFF`) is refused at
  arm time**, not discovered later as a pin that never reverts.
- **PWM duty zero disconnects the pin from its compare unit**, rather than
  holding it low through the timer, which would silently break
  `DIGITAL_WRITE` on that pin afterwards.
- **`AG_PWM_RSVD` and `AG_PWM_NONE` are different refusals**, both folded to
  `AG_ERR_UNSUPPORTED` by `ag_board_pwm()`; the distinguishing byte lives only
  in the `CAPS` pin page, because a host that cannot tell them apart moves a
  load to a different pin when the correct fix is a different build.
- **`TONE` is refused on every pin except `AG_TONE_PIN`.** One timer, one
  output-compare pin; there is no bit-banged fallback.
- **An EEPROM write must clear the reserved region for its *entire* span,
  not just its start address.** A write beginning at `0x7F` for 8 bytes is
  refused whole, even though `0x7F` alone would pass a start-only check.
- **EEPROM writes are chunked to `AG_EEPROM_CHUNK` (8) bytes**; a byte commit
  is ~3.4 ms, so a 64-byte write would hold the main loop 220 ms — long
  enough to miss a scheduled deadline.
- **The I2C reserved address is refused unconditionally**, fitted or not: a
  build that allowed it when no element was present would allow it the
  moment one was added.
- **An SPI transfer's chip-select is checked against the same
  `AG_PIN_RESERVED | AG_PIN_IN_USE` flags as GPIO**, so a TPM's CS or the
  UART pins refuse the same way a reserved GPIO pin does.
- **Pulse and capture are bounded by `AG_PULSE_MAX_US`** so a single command
  cannot hold the main loop indefinitely; capture is further bounded by
  `AG_CAPTURE_MAX = (AG_MAX_PAYLOAD - 1) / 2`, the most intervals one reply
  frame can carry.

## Error semantics

Codes this layer returns, from `include/ag/status.h`; a handler never
constructs its own arity or CRC error — those come from the dispatcher and
framer respectively.

| status | when |
|---|---|
| `AG_ERR_BAD_PIN` | pin index does not exist on this board |
| `AG_ERR_UNSUPPORTED` | pin exists but lacks the capability (no ADC channel, no compare unit, tone on the wrong pin), or the feature was compiled out |
| `AG_ERR_RESERVED_PIN` | pin is `/RESET` or claimed by this build (UART, soft-UART bit clock) |
| `AG_ERR_PROTECTED_REGION` | target is platform-reserved: EEPROM `<0x080`, the I2C element address, a reserved SPI chip-select |
| `AG_ERR_RANGE` | value outside the permitted interval — mode/level out of range, duration outside `[1, AG_SCHED_MAX_MS]`, EEPROM span outside the part or over `AG_EEPROM_CHUNK` |
| `AG_ERR_NO_SLOT` | scheduler table full, or `TIMER_CANCEL` on a pin with no timer |
| `AG_ERR_BAD_ARGC` | a variable-length payload's declared length does not match what arrived |
| `AG_ERR_TIMEOUT` | `PULSE_IN`/`PULSE_CAPTURE` saw no qualifying edge, or an I2C/1-Wire transaction timed out |
| `AG_ERR_NACK` | I2C address or data NACK |
| `AG_ERR_BUSY` | peripheral already in use |

## Configuration

All defaults live once in `include/ag/config.h`, each wrapped in `#ifndef` so
a board profile states only its deviations; resolution order is
`-D` on the command line, then `boards/<board>/board_profile.h`, then this
file.

| knob | default | governs |
|---|---:|---|
| `AG_SCHED_SLOTS` | 8 | scheduler table depth |
| `AG_SCHED_MAX_MS` | `0x7FFFFFFF` | longest timer the sweep can arm |
| `AG_MAX_PAYLOAD` | 64 | frame buffer size; bounds `AG_CAPTURE_MAX` and the bus transfer ceiling |
| `AG_EEPROM_SIZE` | 1024 | per-board EEPROM extent; overridden per board |
| `AG_I2C_RESERVED_ADDR` | `0x60` | the element's address, refused by `I2C_XFER` |
| `AG_PULSE_MAX_US` | 50000 | ceiling on `PULSE_IN`/`PULSE_CAPTURE` timeout |
| `AG_ADC_COUNT` | 8 | HAL's ADC channel range check, per board |
| `AG_FEATURE_GPIO/SCHED/ANALOG/PWM/TONE/PULSE/EEPROM/I2C/SPI/ONEWIRE` | 1 | file-selection gates; 0 drops the translation unit, not a branch |

## Test strategy

Unit tests (`test/unit/test_gpio.c`, `test_sched.c`, `test_analog.c`,
`test_pwm.c`, `test_pulse.c`, `test_storage.c`, `test_bus.c`) run on the host
against `test/mock/hal_mock.c` and drive commands through `ag_dispatch()`
exactly as the framer would — no serial, no board. Time-dependent behaviour
(the scheduler's 49.7-day wrap) is asserted by setting the mock clock
(`ag_mock_set_millis`, `ag_mock_advance`) rather than waiting for it. Mock
state is inspected through `ag_mock_pin_level`, `ag_mock_pin_is_output`,
`ag_mock_pwm_duty`, `ag_mock_pwm_connected`, `ag_mock_last_i2c_addr`, and the
raw EEPROM array — each a seam that exists so a test can assert the *effect*
on hardware, not just the return status.

The board descriptor is tested separately: `make board-sweep` compiles each
board's own `test_board_<short>.c` against **that board's own profile**,
because compiling one board's test against another's register set fails on
day one. Ordinary `make test` excludes it for that reason.

`make ci`'s cross-implementation suite runs the real firmware, built for the
host as `agsim`, so protocol-level behaviour for every capability here is
exercised with zero hardware. See `../host-sdk/architecture.md`.

## Observability

`CAPS` (no argument) reports the compiled feature bitmap and, with a one-byte
pin index, that pin's flags/port/ADC channel/PWM state — the discovery path
every host uses before calling anything in this folder; full wire layout:
`../wire-protocol/architecture.md`. `SYS_INFO` reports uptime, free RAM
(`ag_hal_free_ram`) and reset cause. `ag_sched_active()` exposes the current
slot count for tests and diagnostics; there is no wire command for it beyond
`TIMER_QUERY` on a specific pin.
