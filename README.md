# arduino-generic

One firmware, built from one ANSI C codebase, that exposes an AVR's hardware as
generic primitives over a compact binary protocol — small enough for the classic
AVR-8 family, testable without hardware, and carrying a hardware-rooted device
identity so a platform can attest it, enforce policy on it and enroll it into a
fleet.

It replaces a single-purpose Arduino C++ irrigation controller, kept under
`legacy/` for reference.

## What it is

The firmware exposes primitives — GPIO, the pin scheduler, ADC, PWM, EEPROM,
I²C, SPI, 1-Wire — and decodes no sensors. A DHT22 is decoded by the host, from
timings the device captures. That is what makes one image useful on any board:
the device offers capability, the host supplies meaning.

| | |
|---|---|
| Language | ANSI C (`-std=c11`), bare metal, `avr-gcc` + `avr-libc` |
| Not used | The Arduino core, C++, and every Arduino library |
| Transport | A framed binary protocol over UART, CRC-16 checked |
| Host SDK | `sdk/go` — a Layer 0 Go module with **zero in-house dependencies** |
| Tested | 80 C tests, 19 Go tests, no hardware required |

## Quick start

```sh
make docker-build      # the toolchain image: avr-gcc, simavr, Go, the linters
make docker-ci         # everything the gate requires
make firmware          # build for BOARD (default atmega328p)
make flash             # write it to a board on AVRDUDE_PORT
```

Nothing above needs an AVR toolchain on your machine, and nothing but `flash`
needs a board.

```sh
make agsim             # the firmware, compiled for the host
cd sdk/go && go run ./cmd/agctl /dev/ttyUSB0 caps
```

## How it is tested without hardware

`agsim` is the firmware compiled for the host: the same `src/proto`, `src/core`
and `src/caps` the device runs, linked against a mock HAL, speaking the real
protocol on stdio or a pty. The Go SDK's tests talk to it. So the two codecs are
proven to agree before any board is involved — and the thing they are proven
against is the firmware itself rather than a hand-written fake that would drift
the moment either side changed.

Beyond that: the framer takes ten million random bytes under AddressSanitizer
and must never overrun, wedge or report a frame that was not there. The real
ATmega328P pin table is asserted against the datasheet on x86, because a wrong
port bit for pin 13 is otherwise findable only by wiring up a board and noticing
the wrong LED.

## Gates that are unusual, and why

- **`size-check`** fails the build when an image exceeds its board's budget. It
  is a prerequisite of `firmware`, not a report, so flash creep is caught on the
  commit that causes it.
- **`reproducible-check`** builds twice in different directories and fails if
  the images differ. The firmware hash is signed evidence, so a verifier needs a
  golden measurement computable from source; without this, an attestation
  verdict can never be better than "trust whatever hash the device reported".
- **`sdk-verify`** fails if `sdk/go` gains any in-house dependency. It is
  imported by projects above it in the stack, and a dependency pointing upward
  would make it unimportable by the very projects it exists for.
- **`verify-vectors`** holds the C and Go wire vectors to the same bytes. They
  are two files because neither language can include the other's, and two files
  that must agree eventually will not.

## Layout

```
include/ag/      the contracts: status, protocol, config, HAL, board
src/proto/       CRC, framing, transport
src/core/        bounded cursors, command dispatch
src/caps/        capability modules — one per command group
src/hal/avr/     the only code that touches a register
boards/          per-board descriptors: data, not code
sdk/go/          the host SDK and agctl
tools/agsim/     the firmware, built for the host
testdata/        the wire vectors, shared with sdk/go
legacy/          the irrigation firmware this replaces
```

## Licence

AGPL-3.0-or-later, or a commercial licence. See `LICENSE`.
