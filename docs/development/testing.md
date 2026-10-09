# Testing

Scope: how this firmware is tested, what each tier can and cannot catch, and
why the tiers are separate. Last verified 2026-09-22 against `make ci`.

Everything here runs with **no hardware attached**. A bench Arduino is for
bring-up, never for a gate.

## The three tiers

| Tier | Target | Runs | What only it can catch |
|---|---|---|---|
| 1 — unit | `make test` | x86-64, mock HAL | logic: framing, dispatch, bounds, the scheduler's rollover |
| 2 — emulated | `make integration-test` | the real `.elf` under simavr | register-level behaviour: baud divisor, interrupt vectors, peripheral bring-up order, tick rate |
| 3 — integrity | `make lint size-check reproducible-check` | the build itself | drift: flash/SRAM budgets, byte-for-byte reproducibility, out-of-whitelist `#if` |
| 4 — cost (not a gate) | `make bench-check` | the real `.elf` under simavr | drift in TIME: cycles per decoded byte, per scheduler sweep, per tick interrupt, and the round-trip latency |

`make ci` runs tiers 1 to 3 plus the sanitizers and the Go SDK suite. **Tier 4
is run deliberately and gates nothing** — see `performance.md` for why, and for
the numbers.

## Tier 1 — host unit tests

`src/proto`, `src/core`, `src/caps` and `boards/*/board.c` compile for x86-64
against `test/mock/hal_mock.c` and link with Unity. One binary per test file, so
a segfault in one suite cannot take the others' results with it.

The mock is what makes timing testable: `tfw_mock_set_millis()` and
`tfw_mock_advance()` turn the scheduler's 49.7-day rollover into a two-line
test, and `tfw_mock_fail_next()` reaches error branches a real peripheral would
only produce intermittently.

Coverage floor is 90 % over those paths. `src/hal/avr/` is **excluded on
purpose** — a number counting code x86 cannot execute would not mean anything.

`make test-asan` reruns the same suites under AddressSanitizer and UBSan. The
codec is pointer-heavy parser code reached from untrusted bytes, so it gets its
own gate rather than riding along; it has already caught a defect the ordinary
build constant-folded away.

## Tier 2 — the real image under simavr

`test/integration/simavr/harness.c` loads `build/<board>/trusted-firmware.elf`
— the same file `make flash` sends to the part — into an emulated ATmega328P
through the `libsimavr` C API, and drives it over the emulated USART by
attaching to `AVR_IOCTL_UART_GETIRQ('0')`.

It is the firmware, not a model of it. The harness shares `src/proto/crc16.c`
with the device so it can build the frames it sends. The per-byte fold itself
is the one thing it does **not** share: the harness links the portable
`src/proto/crc16_update.c` to compute the CRCs it sends and checks, while the
device links the fast `src/hal/avr/hal_crc16.c` (#12) — the golden check value
holding on both is what makes that safe. Everything else it asserts about comes
back from emulated silicon.

What it checks today:

- the firmware transmits at all on boot, at the configured baud
- `PING` round-trips, returns `TFW_OK` and echoes its payload
- `CAPS` answers, reports protocol major 2 and the 328P's 22 pins
- `SYS_INFO` answers and the millisecond tick ISR is **running at ~1 kHz** —
  the CTC divisor is wrong on x86 by construction, so only this tier sees it
- `PIN_MODE` + `DIGITAL_WRITE` on pin 13, read back **from the real PORT
  register** rather than from the reply
- the UART pin is refused
- an unprovisioned device refuses to attest
- a corrupted frame is answered with `TFW_ERR_CRC`

Emulation, not simulation: simavr models the ADC, EEPROM, USART, SPI, TWI,
timers, GPIO with pin-change interrupts and the watchdog. `qemu-system-avr` was
rejected for this — it is limited to the USART and the 16-bit timer, and stubs
the rest.

Every harness carries a hard cycle-count bound, so a firmware that stops
answering fails the job instead of hanging it.

## Tier 3 — build integrity

- **`size-check`** fails the build when an image exceeds its board budget. It is
  a prerequisite of `all`, not a report.
- **`reproducible-check`** builds twice in different directories and fails if
  the `.hex` differs. This is load-bearing: `FWHash` is signed evidence, so a
  verifier has to be able to recompute the measurement from source.
- **`lint`** is cppcheck with the board configuration supplied rather than its
  complaints suppressed — told the configuration it can analyse the code,
  whereas a suppression would also hide the next genuinely unknown macro.
- Grep gates reject `__DATE__`/`__TIME__` anywhere in the image and any `#if`
  outside the four whitelisted places.

## Tier 4 — the cycle budget

`make bench-check` counts what the shipped image spends, on simavr's exact cycle
counter, and fails the build when any of it gets more expensive. It is
`size-check` for time.

Six budgets, one table in `test/bench/targets.h`, each row carrying the project
plan's reasoning. One of the six is met today; the other five are enforced at
what they cost now, print `MISS` on every run, and cannot get worse.

**[docs/development/performance.md](performance.md)** has the numbers, what
dominates each miss, how the tick ISR's span is defined, and a simavr modelling
defect that makes the emulated UART run at half rate unless the harness corrects
for it — which matters to tier 2 as well.

## Golden vectors

The protocol codec necessarily exists twice — in C for the device and in Go for
the host — because Go does not run on an ATmega328P. The shared hex vectors are
what hold the two to the same wire, and `make verify-vectors` asserts both
implementations produce them.

They are literals **on purpose**. A failure there is a wire break, not a test to
update.

## Running one thing

```sh
make test                      # all unit suites
./build/host/test_frame         # one suite
make integration-test           # simavr, needs the firmware built
make bench-check                # the cycle budget, both benchmarked boards
make bench-run BOARD=atmega2560 # one board
make coverage                   # with the 90 % floor enforced
make ci                         # everything the gate requires
```

Tier 2 needs the AVR toolchain and `libsimavr`; `make docker-test` runs the
whole gate in the pinned CI image if you do not have them locally.

## What this deliberately does not do

- **No hardware in CI.** No USB, no serial port, no flashing. A gate that needs
  a device plugged in is a gate that fails for reasons unrelated to the change.
- **No mocking of the firmware itself.** `tfwsim` is the real firmware sources
  compiled for x86; the simavr harness is the real image. Neither is a stub, so
  neither can drift from what ships.
- **No timing assertions tighter than the emulator is honest about.** The tick
  rate is checked as ~1 kHz within a tolerance, not to the cycle.
