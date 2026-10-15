# Device capabilities: engineering

> **Status: NORMATIVE** · Scope: trusted-firmware · Last verified: 2026-09-23
>
> Conflicts with this document are defects. Fix them here first.

## Exported surface

Every function below is declared in the header named and takes/returns
`tfw_status_t` (`include/tfw/status.h`) unless noted. `r`/`w` are the reader and
writer over the frame payload (`src/core/reader.h`, `src/core/writer.h`).

| header | handler | does |
|---|---|---|
| `src/caps/gpio.h` | `tfw_gpio_cmd_mode`, `tfw_gpio_cmd_read`, `tfw_gpio_cmd_write` | direction, level in, level out |
| `src/caps/sched.h` | `tfw_sched_cmd_set`, `_cancel`, `_query`; `tfw_sched_init`, `tfw_sched_poll`, `tfw_sched_active` | arm/cancel/query a timed pin; `poll` runs from the main loop |
| `src/caps/analog.h` | `tfw_analog_cmd_read`, `tfw_analog_cmd_set_ref` | raw ADC count, reference select |
| `src/caps/pwm.h` | `tfw_pwm_cmd_write`, `tfw_tone_cmd_start`, `tfw_tone_cmd_stop` | duty cycle; tone lives in its own `.c` for file-selection gating |
| `src/caps/pulse.h` | `tfw_pulse_cmd_in`, `tfw_pulse_cmd_capture` | one pulse width; edge-interval capture |
| `src/caps/storage.h` | `tfw_storage_cmd_read`, `tfw_storage_cmd_write` | EEPROM read/write |
| `src/caps/bus.h` | `tfw_i2c_cmd_xfer`, `tfw_i2c_cmd_config`, `tfw_spi_cmd_xfer`, `tfw_spi_cmd_config`, `tfw_ow_cmd_reset`, `_write`, `_read`; `tfw_bus_init` | I2C, SPI, 1-Wire; `tfw_bus_init` applies the boot defaults, called once from `main.c` |
| `include/tfw/board.h` | `tfw_board_pin`, `tfw_board_adc_channel`, `tfw_board_pwm`, `tfw_board_pin_page`, `tfw_board_pin_pwm_state`, `tfw_board_pin_flags`, `tfw_board_pin_count`, `tfw_board_name` | resolve a logical pin; consulted by every handler above before touching hardware |
| `include/tfw/hal.h` | `tfw_hal_gpio_*`, `tfw_hal_adc_*`, `tfw_hal_pwm_*`, `tfw_hal_tone_*`, `tfw_hal_pulse_*`, `tfw_hal_eeprom_*`, `tfw_hal_i2c_init`, `tfw_hal_i2c_xfer`, `tfw_hal_spi_init`, `tfw_hal_spi_xfer`, `tfw_hal_ow_*` | one implementation linked per image: `src/hal/avr/*.c` for the device, `test/mock/hal_mock.c` for the host |

Go SDK (`sdk/go/commands.go`) mirrors this 1:1 as `(*Device)` methods —
`SetPinMode`, `DigitalRead`/`Write`, `TimerSet`/`Cancel`/`Query`,
`AnalogRead`, `SetADCReference`, `PWMWrite`, `Tone`/`NoTone`, `PulseIn`,
`PulseCapture`, `EEPROMRead`/`Write`, `I2CTransfer`, `SetI2CClock`,
`SPITransfer`, `ConfigureSPI`, `OneWireReset`/`Write`/`Read`.

## Invariants

- **A pin the firmware is using for itself refuses driving, not reading.**
  `resolve_writable()` in `gpio.c` checks `TFW_PIN_RESERVED | TFW_PIN_IN_USE`;
  `tfw_gpio_cmd_read` does not, because observing the UART line is harmless
  and a blanket refusal would make wiring problems harder to diagnose.
- **The scheduler has one writer.** Only `tfw_sched_poll()`, called from the
  main loop, mutates `g_slots`; the tick ISR only increments a counter. No
  `volatile`, no lock, and `test_sched.c` drives the 49.7-day wrap
  deterministically by setting `tfw_hal_millis()` rather than waiting for it.
- **Re-arming a pin extends its timer rather than consuming a second slot.**
  A host that reissues `TIMER_SET` — routine for a periodic controller —
  does not exhaust `TFW_SCHED_SLOTS`.
- **The pin timer stays in firmware.** A pin told to hold a level for ten
  minutes must revert after ten minutes whether or not the host is still there.
  If the host owned the timer, a crashed host, a pulled cable or a rebooted
  gateway would leave the pin latched. The scheduler runs from the millisecond
  tick with no host involvement.
- **A scheduled duration above `TFW_SCHED_MAX_MS` (`0x7FFFFFFF`) is refused at
  arm time**, not discovered later as a pin that never reverts.
- **PWM duty zero disconnects the pin from its compare unit**, rather than
  holding it low through the timer, which would silently break
  `DIGITAL_WRITE` on that pin afterwards.
- **`TFW_PWM_RSVD` and `TFW_PWM_NONE` are different refusals**, both folded to
  `TFW_ERR_UNSUPPORTED` by `tfw_board_pwm()`; the distinguishing byte lives only
  in the `CAPS` pin page, because a host that cannot tell them apart moves a
  load to a different pin when the correct fix is a different build.
- **`TONE` is refused on every pin except `TFW_TONE_PIN`.** One timer, one
  output-compare pin; there is no bit-banged fallback.
- **An EEPROM write must clear the reserved region for its *entire* span,
  not just its start address.** A write beginning at `0x7F` for 8 bytes is
  refused whole, even though `0x7F` alone would pass a start-only check.
- **EEPROM writes are chunked to `TFW_EEPROM_CHUNK` (8) bytes**; a byte commit
  is ~3.4 ms, so a 64-byte write would hold the main loop 220 ms — long
  enough to miss a scheduled deadline.
- **The I2C reserved address is refused unconditionally**, fitted or not: a
  build that allowed it when no element was present would allow it the
  moment one was added.
- **An SPI transfer's chip-select is checked against the same
  `TFW_PIN_RESERVED | TFW_PIN_IN_USE` flags as GPIO**, so a TPM's CS or the
  UART pins refuse the same way a reserved GPIO pin does.
- **Pulse and capture are bounded by `TFW_PULSE_MAX_US`** so a single command
  cannot hold the main loop indefinitely; capture is further bounded by
  `TFW_CAPTURE_MAX = (TFW_MAX_PAYLOAD - 1) / 2`, the most intervals one reply
  frame can carry.
- **Every peripheral the HAL exports an init for is initialised at boot**,
  from `main.c`, with no `#if` there: `tfw_hal_adc_init()` unconditionally
  (`analog.c`/`hal_adc.c` compile on every board this family builds), and
  `tfw_bus_init()` for I2C/SPI, which `mk/firmware.mk` links as either `bus.c`
  (an init) or `bus_none.c` (a no-op stub) — always exactly one, so the call
  is never a dangling reference. Before this landed, `ADEN` and `SPE` were
  never set: `ANALOG_READ` returned a plausible-looking zero and the first
  `SPI_XFER` hung the device permanently (issues #16, #19).
- **Neither bus's wait for its peripheral to finish is unbounded.** `hal_bus.c`
  polls I2C's `TWINT` and SPI's `SPIF` against a spin limit each and returns
  `TFW_ERR_TIMEOUT` rather than looping forever — a stuck bus costs one
  refused command, not the device's ability to answer any other.
- **`I2C_CONFIG`/`SPI_CONFIG` let a host change what `tfw_bus_init()` fixed at
  boot**, not make the bus usable in the first place: `I2C_XFER`/`SPI_XFER`
  work at the boot defaults before either is ever sent.
- **`I2C_CONFIG`'s range check is layered, not single-source.** The capability
  layer's `TFW_I2C_HZ_MIN..MAX` is a board-independent sanity bound; whether a
  given rate inside it is one THIS board's 8-bit `TWBR` can actually produce
  depends on `F_CPU`, which only `hal_bus.c` knows. `tfw_hal_i2c_init()` checks
  its own divisor and returns `TFW_ERR_RANGE` for anything it cannot represent
  — without it, a rate the generic bound allowed but the register could not
  hold silently ran the bus at a different, wrong speed instead of refusing.
- **The secure element's wake pulse depends on the SAME TWI peripheral
  `I2C_CONFIG` reconfigures.** `tfw_atecc_wake()` (element tier) re-asserts
  `TFW_ATECC_I2C_HZ` (100 kHz) itself before every wake rather than trusting
  the rate `tfw_bus_init()` left at boot: a host calling `I2C_CONFIG` to
  anything faster, for an unrelated sensor, would otherwise leave the wake
  pulse too short and every later element operation failing until reboot.
- **Boot-time SPI init deasserts the hardware `SS` pin before making it an
  output.** `tfw_hal_spi_init()` now sets `PORTB`'s `SS` bit high before
  `DDRB`'s, so a real device wired to that pin as its chip-select is never
  observed selected before a host has issued a transfer naming it.

## Error semantics

Codes this layer returns, from `include/tfw/status.h`; a handler never
constructs its own arity or CRC error — those come from the dispatcher and
framer respectively.

| status | when |
|---|---|
| `TFW_ERR_BAD_PIN` | pin index does not exist on this board |
| `TFW_ERR_UNSUPPORTED` | pin exists but lacks the capability (no ADC channel, no compare unit, tone on the wrong pin), or the feature was compiled out |
| `TFW_ERR_RESERVED_PIN` | pin is `/RESET` or claimed by this build (UART, soft-UART bit clock) |
| `TFW_ERR_PROTECTED_REGION` | target is platform-reserved: EEPROM `<0x080`, the I2C element address, a reserved SPI chip-select |
| `TFW_ERR_RANGE` | value outside the permitted interval — mode/level out of range, duration outside `[1, TFW_SCHED_MAX_MS]`, EEPROM span outside the part or over `TFW_EEPROM_CHUNK` |
| `TFW_ERR_NO_SLOT` | scheduler table full, or `TIMER_CANCEL` on a pin with no timer |
| `TFW_ERR_BAD_ARGC` | a variable-length payload's declared length does not match what arrived |
| `TFW_ERR_TIMEOUT` | `PULSE_IN`/`PULSE_CAPTURE` saw no qualifying edge, or an I2C/SPI/1-Wire transaction timed out |
| `TFW_ERR_NACK` | I2C address or data NACK |
| `TFW_ERR_BUSY` | peripheral already in use |

## Configuration

All defaults live once in `include/tfw/config.h`, each wrapped in `#ifndef` so
a board profile states only its deviations; resolution order is
`-D` on the command line, then `boards/<board>/board_profile.h`, then this
file.

| knob | default | governs |
|---|---:|---|
| `TFW_SCHED_SLOTS` | 8 | scheduler table depth |
| `TFW_SCHED_MAX_MS` | `0x7FFFFFFF` | longest timer the sweep can arm |
| `TFW_MAX_PAYLOAD` | 64 | frame buffer size; bounds `TFW_CAPTURE_MAX` and the bus transfer ceiling |
| `TFW_EEPROM_SIZE` | 1024 | per-board EEPROM extent; overridden per board |
| `TFW_I2C_RESERVED_ADDR` | `0x60` | the address hosts were told is the element's, refused by `I2C_XFER` |
| `TFW_ELEMENT_I2C_ADDR` | `0x60`; `0x36` in the Nano and Mega profiles | the element's address, used by the driver and refused by `I2C_XFER` |
| `TFW_ELEMENT_I2C_HZ` | 100000 | the element's bus speed; the driver asserts it is at most 100 kHz for the wake pulse |
| `TFW_I2C_DEFAULT_HZ` | 100000 | `tfw_bus_init()`'s boot-time I2C clock |
| `TFW_I2C_HZ_MIN` / `_MAX` | 1000 / 400000 | `I2C_CONFIG`'s accepted interval, capped at Fast-mode |
| `TFW_SPI_DEFAULT_MODE` / `_CLKDIV` / `_LSB_FIRST` | 0 / 3 / 0 | `tfw_bus_init()`'s boot-time SPI mode, divider and bit order |
| `TFW_PULSE_MAX_US` | 50000 | ceiling on `PULSE_IN`/`PULSE_CAPTURE` timeout |
| `TFW_ADC_COUNT` | 8 | HAL's ADC channel range check, per board |
| `TFW_FEATURE_GPIO/SCHED/ANALOG/PWM/TONE/PULSE/EEPROM/I2C/SPI/ONEWIRE` | 1 | file-selection gates; 0 drops the translation unit, not a branch |

## Test strategy

Unit tests (`test/unit/test_gpio.c`, `test_sched.c`, `test_analog.c`,
`test_pwm.c`, `test_pulse.c`, `test_storage.c`, `test_bus.c`) run on the host
against `test/mock/hal_mock.c` and drive commands through `tfw_dispatch()`
exactly as the framer would — no serial, no board. Time-dependent behaviour
(the scheduler's 49.7-day wrap) is asserted by setting the mock clock
(`tfw_mock_set_millis`, `tfw_mock_advance`) rather than waiting for it. Mock
state is inspected through `tfw_mock_pin_level`, `tfw_mock_pin_is_output`,
`tfw_mock_pwm_duty`, `tfw_mock_pwm_connected`, `tfw_mock_last_i2c_addr`,
`tfw_mock_last_i2c_hz`, `tfw_mock_last_spi_mode`/`_clkdiv`/`_lsb`, and the raw
EEPROM array — each a seam that exists so a test can assert the *effect* on
hardware, not just the return status. The mock cannot model `ADEN`/`SPE` --
it answers a fixed value regardless of whether a peripheral was ever
initialised -- which is exactly why issues #16 and #19 shipped past it; the
simavr suite below is what actually exercises those bits.

The board descriptor is tested separately: `make board-sweep` compiles each
board's own `test_board_<short>.c` against **that board's own profile**,
because compiling one board's test against another's register set fails on
day one. Ordinary `make test` excludes it for that reason.

`make ci`'s cross-implementation suite runs the real firmware, built for the
host as `tfwsim`, so protocol-level behaviour for every capability here is
exercised with zero hardware. See `../host-sdk/architecture.md`.

`test/integration/simavr/harness.c` runs the real `.elf` on a cycle-accurate
ATmega328P emulator, which models `ADEN` and `SPE` where the mock cannot: it
feeds `ADC_IRQ_OUT_TRIGGER` a voltage so `ANALOG_READ` converts something
real, and answers `SPI_IRQ_OUTPUT` with a fixed byte so `SPI_XFER` has
something to read back. Every exchange is bounded by a fixed cycle count, so
a regression that reintroduces #16's hang FAILS the suite rather than
blocking it.

## Observability

`CAPS` (no argument) reports the compiled feature bitmap and, with a one-byte
pin index, that pin's flags/port/ADC channel/PWM state — the discovery path
every host uses before calling anything in this folder; full wire layout:
`../wire-protocol/architecture.md`. `SYS_INFO` reports uptime, free RAM
(`tfw_hal_free_ram`), reset cause, and the framer's sticky `err_crc`/
`err_format` counters, appended after reset cause so a host built before they
existed keeps working unmodified (#5). `tfw_sched_active()` exposes the current
slot count for tests and diagnostics; there is no wire command for it beyond
`TIMER_QUERY` on a specific pin.

**Reset cause and the stock bootloader (issue #18).** `MCUSR` is cleared by
whatever runs first after reset, and it is not cleared by reset itself — the
flags accumulate otherwise. `src/hal/avr/hal_early.c`'s `.init3` reads and
clears it in BOTH images this project builds, so `tfw-boot` now carries its
own observation forward in the CDI handoff (`include/tfw/boot.h`,
`src/core/handoff.c`), and `tfw_hal_reset_cause()` reports that value when a
valid handoff exists. A device flashed with the **stock Arduino
bootloader** has no such handoff: the stock bootloader reads and clears
`MCUSR` before jumping to the application, so by the time this firmware's
own `.init3` runs the register is already zero. `SYS_INFO` then reports
`TFW_RESET_UNKNOWN` after every reset, including a deliberate external one —
not a broken decoder, but the honest answer for a device whose only record
of the reset cause was consumed by code this project does not own. The
value is unrecoverable in that configuration; there is no workaround short
of flashing `tfw-boot`.
