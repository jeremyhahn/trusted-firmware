# Testing

> **Status: INFORMATIVE** · Scope: `make ci`, `make bench-check` · Last verified: 2026-09-22
> How this firmware is tested, what each tier can and cannot catch, and why the
> tiers are separate.

Everything the gate runs needs **no hardware attached**. A bench Arduino is for
bring-up and for `make hw-test` (below), never for a gate.

## The three tiers

| Tier | Target | Runs | What only it can catch |
|---|---|---|---|
| 1 — unit | `make host-test` | x86-64, mock HAL | logic: framing, dispatch, bounds, the scheduler's rollover |
| 2 — emulated | `make integration-test-simavr` | the real `.elf` under simavr | register-level behaviour: baud divisor, interrupt vectors, peripheral bring-up order, tick rate |
| 3 — integrity | `make lint size-check reproducible-check` | the build itself | drift: flash/SRAM budgets, byte-for-byte reproducibility, out-of-whitelist `#if` |
| 4 — cost (not a gate) | `make bench-check` | the real `.elf` under simavr | drift in TIME: cycles per decoded byte, per scheduler sweep, per tick interrupt, and the round-trip latency |

`make ci` runs tiers 1 to 3 plus the sanitizers and the Go SDK suite. **Tier 4
is run deliberately and gates nothing** — see `performance.md` for why, and for
the numbers.

## The five CI stages

The gate is grouped into the five stages of handbook ci-standard.md §1, and
`make ci` runs them in this order (`mk/stages.mk`; the full list of gates per
stage is in `building.md`):

| Stage | Holds |
|---|---|
| `make build` | tier 3's budgets and reproducibility: every image at every tier, the bootloader, tfwsim, the SDK |
| `make test` | tier 1: `host-test`, `boot-test`, `board-sweep`, `coverage`, `test-asan`, `verify-vectors` |
| `make integration-test` | tier 2 and the cross-implementation suite: `integration-test-simavr`, `integration-test-boot`, `sdk-test`, `app-sdk-check` |
| `make security` | tier 3's `lint`, plus gosec, govulncheck, golangci-lint, go mod verify, the secrets scan, vendored hashes and releasability |
| `make deps-check` | no toolchain pin or Go require behind its latest release without a written reason |

All five run in the devcontainer, `build/docker/devcontainer/Dockerfile`:
integration-test in its `integration` stage, the other four in `toolchain`.
A stage whose tool is missing fails and names it. It never skips.

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

## The suite on a real board — `make hw-test` (not a gate)

```sh
make flash   BOARD=atmega2560 AVRDUDE_PORT=/dev/ttyACM0
make hw-test BOARD=atmega2560 PORT=/dev/ttyACM0
```

`make hw-test` runs the SDK's cross-implementation suite, the
`TestAgainstFirmware*` tests that `make sdk-test` runs against `tfwsim`,
against the image flashed on a real board. It is the same suite, not a copy.
The one difference is the transport, chosen in `sdk/go/internal/peer`:

- With `TFW_HW_PORT` unset, which is every CI run, the peer is `tfwsim`.
- With `TFW_HW_PORT` set (`hw-test` sets it from `PORT`, and `TFW_HW_BOARD`
  from `BOARD`), the peer is the board on that port. The seam first resets the
  board by pulsing DTR, as avrdude does. It then waits up to 5 s for the board
  to transmit, and hands the first bytes, the BOOT event, to the suite
  unconsumed. Each test therefore gets a freshly reset board, as it would get a
  fresh `tfwsim` process.

It does not rely on opening the port to reset the board. A port left with
`-hupcl`, for example by a probe run's `stty`, keeps DTR up across close and
open. The board would then not reset and would send no BOOT event.

`BOARD` selects the pin facts the suite expects (`peerBoards` in
`sdk/go/tfwsim_test.go`), and `CAPS` must report that board. `tfwsim` is the
328P. A board that is not in that table fails the run. So do a missing `PORT`,
a port that does not exist, and a board that does not answer within the
bound. None of these is a skip. Flash the image first: the target tests what
is on the board and flashes nothing.

It is outside `make ci` because CI has no board. The seam itself is
unit-tested on the host (`sdk/go/internal/peer`), and that test is in
`make sdk-test`.

## Enrolling a real board — `make hw-enroll` (not a gate)

```sh
make hw-enroll DEVICE=/dev/ttyACM0 DEVICEID=<hex X||Y from tfwprov>
```

This is issue #7's recorded run. It enrolls a flashed, provisioned
element-tier board through device-attest-01 and writes a run record to commit
under `docs/development/enrollment-runs/`. The harness is
`TestHardwareEnrollment` (`xkms/e2e`), and `make xkms-test` runs it against
`tfwsim`. Without `DEVICE` it fails. The procedure, the preconditions and
their blockers are in
**[docs/development/hardware-enrollment.md](hardware-enrollment.md)**.

## Golden vectors

The protocol codec necessarily exists twice — in C for the device and in Go for
the host — because Go does not run on an ATmega328P. The shared hex vectors are
what hold the two to the same wire, and `make verify-vectors` asserts both
implementations produce them.

They are literals **on purpose**. A failure there is a wire break, not a test to
update.

## Running one thing

```sh
make host-test                 # all unit suites
./build/host/test_frame         # one suite
make integration-test-simavr    # simavr, needs the firmware built
make test                       # stage 2: unit, boot, board sweep, coverage, sanitizers
make security                   # stage 4: cppcheck, gosec, govulncheck, golangci-lint, secrets
make deps-check                 # stage 5: pins and Go requires against their latest release
make bench-check                # the cycle budget, both benchmarked boards
make bench-run BOARD=atmega2560 # one board
make coverage                   # with the 90 % floor enforced
make ci                         # everything the gate requires, bar the xkms job
make platform-bake-build-check  # in `make ci`: the bench bake image links for both boards, under its flash ceiling
make xkms-test xkms-coverage    # the xkms adapter's tests and 90 % floor; the `xkms` CI job
make xkms-security xkms-deps-check  # its security and latest-version checks; the same job
make hw-test BOARD=atmega2560 PORT=/dev/ttyACM0  # the cross-implementation suite on a flashed board
make hw-enroll DEVICE=/dev/ttyACM0 DEVICEID=<hex>  # issue #7's enrollment run on a provisioned board
```

The xkms adapter module needs the private go-xkms module, so its four targets are
outside `make ci` (and so outside `make docker-ci`); see `building.md`.

Tier 2 needs the AVR toolchain and `libsimavr`; `make docker-ci` runs the
whole gate in the devcontainer if you do not have them locally.

## What this deliberately does not do

- **No hardware in CI.** No USB, no serial port, no flashing. A gate that needs
  a device plugged in is a gate that fails for reasons unrelated to the change.
  `make hw-test` is run by hand, on a bench.
- **No mocking of the firmware itself.** `tfwsim` is the real firmware sources
  compiled for x86; the simavr harness is the real image. Neither is a stub, so
  neither can drift from what ships.
- **No timing assertions tighter than the emulator is honest about.** The tick
  rate is checked as ~1 kHz within a tolerance, not to the cycle.
