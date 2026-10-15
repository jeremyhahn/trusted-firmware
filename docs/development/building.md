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

## ESP32 (ESP-IDF)

```sh
make esp32-build BOARD=esp32-qemu ESP_TARGET=esp32c3   # or ESP_TARGET=esp32
make esp32-build BOARD=esp32p4-poe
make esp32-qemu-test   # both esp32-qemu machines in Espressif's QEMU
```

Neither target is in `make ci`, and the toolchain image carries no ESP-IDF.
Both refuse any toolchain but the pins in
[`adr-esp-idf-integration.md`](../multi-arch/adr-esp-idf-integration.md#toolchain-pins)
(`mk/arch/esp32.mk`). The install goes outside the clone, under `$HOME`. These
are the steps that worked on Debian 13 without root:

```sh
mkdir -p ~/esp && cd ~/esp
git clone --depth 1 -b v5.5.5 --recursive --shallow-submodules \
    https://github.com/espressif/esp-idf
cd esp-idf
./install.sh esp32,esp32c3,esp32p4
. ./export.sh
python tools/idf_tools.py install cmake ninja          # not installed on Linux by default
python tools/idf_tools.py install qemu-riscv32 qemu-xtensa
python -m pip install esp-secure-cert-tool==2.6.1      # the DS encoder qemu_test.py uses
```

Three host gaps, each fixed without root:

- **No `ensurepip`** (Debian's `python3-venv` is absent): `install.sh` fails to
  create its Python environment. Create it at the path it names with the same
  interpreter, then rerun `install.sh`:
  `uv venv --seed --python /usr/bin/python3 ~/.espressif/python_env/idf5.5_py3.13_env`.
- **QEMU's shared libraries** (`libSDL2-2.0.so.0`, `libslirp.so.0`, and SDL's
  `libXss`, `libdecor-0`, `libsamplerate`): `idf_tools.py` removes a QEMU it
  cannot run. Download its archives with `idf_tools.py download qemu-riscv32
  qemu-xtensa`, unpack each into
  `~/.espressif/tools/qemu-<arch>/esp_develop_9.2.2_20260417/`, and unpack the
  Debian packages `libsdl2-2.0-0 libslirp0 libxss1 libdecor-0-0 libsamplerate0`
  (`apt-get download`, `dpkg-deb -x`) into a directory on `LD_LIBRARY_PATH`.
- **QEMU not on `PATH`**: add both
  `~/.espressif/tools/qemu-<arch>/esp_develop_9.2.2_20260417/qemu/bin`
  directories after `. ./export.sh`.

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
