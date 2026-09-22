# Building and testing

> **Status: NORMATIVE** · Scope: the build system · Last verified: 2026-09-22

Nothing here requires an AVR toolchain on your machine. `make docker-build`
produces an image with all of it, and every other target runs inside it.

## The gate

```sh
make ci          # what CI requires, run locally
make docker-ci   # the same, inside the toolchain image
```

`make help` lists every target.

## Host tests

```sh
make test        # the unit suite; under a second
make test-asan   # the same suite under AddressSanitizer and UBSan
make coverage    # with a 90% floor on the host-testable layers
```

The coverage filter deliberately excludes `src/hal/avr/`. Register pokes cannot
execute on x86, and counting code that cannot run would make the number mean
nothing.

`make test-asan` is a separate gate rather than a flag on the first because it
has already found a defect the ordinary build constant-folded away.

## Firmware

```sh
make firmware BOARD=atmega328p   # builds and enforces the size budget
make reproducible-check          # two builds in different dirs must be identical
make flash AVRDUDE_PORT=/dev/ttyUSB0
```

## The simulator

```sh
make agsim
./build/host/agsim --pty        # prints a pty path on stderr
```

`agsim` is the firmware, not a model of it. The Go SDK's cross-implementation
tests run against it, which is what proves the C and Go codecs agree without
hardware.

## Adding a board

A board is data, not code. Add `boards/<name>/` with:

- `board.h` — pin count, sizing, feature deviations, port register addresses
- `board.c` — the pin table
- `profile.mk` — MCU, clock, budgets, programmer

Then add a test asserting the pin table against the datasheet, as
`test/unit/test_board_328p.c` does. **If adding a board requires touching
`src/`, that is a defect in the design rather than a property of the board.**
