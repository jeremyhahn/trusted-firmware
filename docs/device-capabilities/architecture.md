# Device capabilities: architecture

> **Status: NORMATIVE** · Scope: arduino-generic · Last verified: 2026-09-23
>
> Conflicts with this document are defects. Fix them here first.

## Scope

The MCU primitives: GPIO (`src/caps/gpio.c`), the pin scheduler (`sched.c`),
analog read (`analog.c`), PWM and tone (`pwm.c`, `tone.c`), pulse measurement
and edge capture (`pulse.c`), EEPROM storage (`storage.c`), and the I2C/SPI/
1-Wire bus commands (`bus.c`) — plus the board descriptor
(`include/ag/board.h`, `boards/*`) and the link-time HAL
(`include/ag/hal.h`, `src/hal/avr/*`) that let one source tree serve four
boards. `attest.c`, `alias.c`, `uccs.c` and `provision.c` live in the same
directory but are a different owner; see `../attestation/` and
`../device-provisioning/user.md`.

## Layering

```
L3  DISPATCH   src/core/dispatch.c    reads args, calls a capability, writes a reply
L2  CAPABILITY src/caps/*.c           resolves the board, calls the HAL, enforces refusals
L1b BOARD      src/core/board_lookup.c + boards/*/board.c   pin/PWM/ADC lookup, pure data
L1a HAL        src/hal/avr/*.c        register access, one implementation per image
L0  REGISTERS  <avr/io.h>
```

L3 never touches the HAL and does not know a pin exists — that is what makes
the protocol layer testable on x86 with no mock of the dispatcher. L1b sits
**above** the HAL: it holds addresses as numbers, no register access, so the
real pin table of a real part is unit-tested on the host rather than found
wrong by watching the wrong LED on a bench. Full layer contract:
`../device-capabilities/architecture.md`.

## Data flow, one example

`PIN_MODE` → `ag_dispatch` checks payload length against `commands.def` →
`ag_gpio_cmd_mode` resolves the logical pin via `ag_board_pin` and refuses it
if `AG_PIN_RESERVED | AG_PIN_IN_USE` → `ag_hal_gpio_mode` writes the register.
Every capability in this folder follows that shape: resolve and refuse at L2,
touch hardware at L1a. None of them call `ag_hal_*` before checking board
flags.

## The HAL is substituted at link time, never through a vtable

`include/ag/hal.h` declares plain `extern` functions; the Makefile links
exactly one implementation (`src/hal/avr` or `test/mock`). A function-pointer
vtable would cost 68 bytes of RAM on an ATtiny85 — a fifth of its SRAM — and
every slot would be a live reference, so `--gc-sections` could no longer drop
an unused peripheral's code from an image that never calls it. Link-time
substitution costs nothing, lets `-flto` inline `ag_hal_gpio_write` to a
two-instruction `sbi`, and the contract is enforced by the linker: add a
function to `hal.h`, forget the mock, and `make test` fails to link — that is
the contract working, not a build break to route around.

## There is no `delay()`

`ag_hal_delay_us` exists for bus bit-banging — 1-Wire slots, the USI clock,
the ATECC608A wake pulse — where the delay **is** the protocol. No capability
in this folder may call it from a command handler; the main loop must stay
non-blocking so the scheduler sweep never misses a deadline.

## Preprocessor conditionals are confined to four places

`include/ag/config.h`, `boards/*/board.h`, `src/core/commands.def`, and
`src/hal/avr/` — plus the toolchain shim in `include/ag/progmem.h`. Everywhere
else, including every file in `src/caps/`, a feature is gated by including or
excluding the whole `.c` file from the object list; `make lint` greps for
violations. The cost of `#if` in logic is that no single build is ever the one
that was tested — `AG_FEATURE_TONE 0` on the ATmega32U4 keeps `tone.c` and
`hal_tone.c` out of the image entirely rather than compiling a dead branch.

## The tick and the scheduler

A true **1 ms** CTC tick (`OCR0A = 249` at 16 MHz, clk/64) — not Arduino's
1.024 ms. The ISR does the tick and nothing else; the sweep runs in
`ag_sched_poll()` from the main loop, so `g_slots` in `src/caps/sched.c` has
exactly one writer and needs no locking, no `volatile`, and compiles
unchanged on the host. Rollover uses a signed difference,
`(int32_t)(now - deadline) >= 0`, correct across the 49.7-day millisecond
wrap; the interval is bounded to `AG_SCHED_MAX_MS` (`0x7FFFFFFF`) at arm time
so the failure is a clean `AG_ERR_RANGE` rather than an output that stays on
for seven weeks.

## Trust boundaries this layer enforces

Exposing hardware generically and being a trusted device conflict in exactly
two places here, and both refuse rather than weaken the primitive:

| protected resource | enforced in | refusal |
|---|---|---|
| EEPROM `[0x000, 0x080)` — bootloader pubkey hash, serial, boot counter, OTA state | `src/caps/storage.c` | `AG_ERR_PROTECTED_REGION` |
| I2C address `0x60` (`AG_I2C_RESERVED_ADDR`) — the ATECC608A, refused whether fitted or not | `src/caps/bus.c` | `AG_ERR_PROTECTED_REGION` |
| SPI chip-select flagged `AG_PIN_RESERVED`/`AG_PIN_IN_USE` — a TPM's CS, or the UART's pins | `src/caps/bus.c` | `AG_ERR_PROTECTED_REGION` |
| the boot section | not reachable by any primitive here | n/a — no callable exists |

Lock bits do not help: they stop an external programmer, not application
code, and this firmware **is** the application, so the check has to live
here or it does not exist. Full attestation-boundary detail, including why
reads of the EEPROM region are allowed: `../dice-attestation/architecture.md`.

## The board descriptor and the four boards

`boards/atmega328p`, `atmega2560`, `atmega32u4` and `attiny85` each supply
three tables (`ag_board_ports`, `ag_board_pins`, `ag_board_pwms`) and nothing
else — no functions, no register access. The lookups that read those tables
live once in `src/core/board_lookup.c`, identical on every classic AVR. Adding
a board is data, not code; the full contract, including the two peripheral
gaps (no USART on the ATtiny85, no key store without a boot section) and how
they are resolved by file selection rather than `#if`, is
`../boards/adding-a-board.md`. Per-board measurements: `../boards/attiny85.md`.

## Alternatives rejected

| alternative | why not |
|---|---|
| the Arduino core | `String` and friends cost ~900 bytes of a Nano's 2 KB before Ethernet is counted — the whole reason this project exists |
| a function-pointer HAL vtable | costs RAM and pins every entry as a live reference, defeating `--gc-sections`; see above |
| `#if` feature gating inside logic files | every combination becomes a different program and the test suite only ever exercised one of them |

## What this deliberately does not do

- **No sensor drivers.** `PULSE_CAPTURE` returns intervals; a host turns 26 µs
  and 70 µs into DHT22 bits. A device that decoded sensors would need a new
  build per sensor.
- **No calibration or unit conversion.** `ANALOG_READ` returns the raw
  converter count, never a voltage or a percentage — calibration is a
  property of the sensor and its wiring, both known to the host and not to
  the device.
- **No dynamic allocation.** Every buffer here is static and sized from
  `config.h`.
- **No runtime board detection.** The board is a compile-time fact; the
  descriptor tables are chosen by the Makefile, not probed at boot.
- **No `delay()`**, anywhere in this layer. See above.
