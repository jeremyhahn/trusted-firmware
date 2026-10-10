# Building and testing

> **Status: NORMATIVE** · Scope: the build system · Last verified: 2026-09-22

Nothing here requires an AVR toolchain on your machine. `make docker-build`
produces an image with all of it, and every other target runs inside it.

## The gate

```sh
make ci          # what CI requires, run locally
make docker-ci   # the same, inside the toolchain image
make xkms-test xkms-coverage   # the xkms adapter gate, a separate job
```

There are two gates. `make ci` covers everything that needs only this checkout
and public modules, so it runs in the toolchain image. The `xkms/` adapter module
depends on the private go-xkms module, which the image has no route to and which
must not be given a credential baked into a layer, so `make xkms-test` and
`make xkms-coverage` are not part of `make ci`. The `xkms` CI job runs them on the
runner, and it is part of the CI Gate: a change is green only when both pass. To
run the whole gate locally, run both commands where go-xkms is reachable.

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
make firmware-matrix             # every board, at every tier its profile claims
make reproducible-check          # two builds in different dirs must be identical
make flash AVRDUDE_PORT=/dev/ttyUSB0
```

Boards: `atmega328p`, `atmega2560`, `atmega32u4`, `attiny85`. The first three
build at `CRYPTO=soft` and `CRYPTO=atecc608a`; the ATtiny85 builds only at
`CRYPTO=none`, because neither of the others is possible on a part with no I2C
and no boot section. `BOARD_TIERS` in each `profile.mk` is where that is
recorded, and `firmware-matrix` reads it rather than assuming a cross product.

The build takes the architecture from the board: every `boards/<board>/profile.mk`
says `ARCH := avr`, and `mk/firmware.mk` and `mk/boot.mk` — which name no
toolchain, flag or HAL path — pick up `mk/arch/$(ARCH).mk` (toolchain, flags,
size, programmer, linker placement) and `src/hal/$(ARCH)/`. A second architecture
is a directory, not an edit to those files. `BOARD=` is unchanged.

The CI workflow's firmware matrix is a hand-written list of board × tier pairs.
`make ci-matrix-check` (part of `make ci`, and a step of the lint job) compares it
with `make firmware-matrix-list`, which prints what `firmware-matrix` builds, and
fails if either has a pair the other lacks.

`make provenance` records `arch`, `compiler` and `toolchain` for each image, and
`make sbom` the same for the toolchain, using whatever the architecture file
names — nothing in `mk/release.mk` says `avr`.

## The simulator

```sh
make tfwsim
./build/host/tfwsim --pty        # prints a pty path on stderr
```

`tfwsim` is the firmware, not a model of it. The Go SDK's cross-implementation
tests run against it, which is what proves the C and Go codecs agree without
hardware.

```sh
make tfwsim-atecc608a            # or: make tfwsim CRYPTO=atecc608a
./build/host/tfwsim-atecc608a --pty --provision-element
```

The element-tier build links the real ATECC608A driver and provider against
`test/model/atecc608a.c`, so ATTEST and ATTEST_CERT produce DICE evidence over
the wire. It starts blank; its identity options and their limits (the model does
real P-256 under a published TEST key, so its evidence verifies and must never be
trusted) are in `docs/host-sdk/cli.md`.

## Adding a board

A board is data, not code. Add `boards/<name>/` with:

- `board_profile.h` — pin count, sizing, feature deviations, port register addresses
- `board.c` — the pin table
- `profile.mk` — `ARCH`, MCU, clock, budgets, programmer, crypto tiers

Then add a test asserting the pin table against the datasheet, as
`test/unit/test_board_328p.c` does. **If adding a board requires touching
`src/`, that is a defect in the design rather than a property of the board** —
unless the part is missing a peripheral the HAL assumes, in which case the cost
is one new file under `src/hal/avr` shared with the next part that needs it.
[`docs/boards/adding-a-board.md`](../boards/adding-a-board.md) is the normative
version of all of this, and [`attiny85.md`](../boards/attiny85.md) is what
adding a part four times smaller than the next smallest actually cost.
