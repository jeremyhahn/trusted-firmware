# Building and testing

> **Status: NORMATIVE** · Scope: the build system · Last verified: 2026-09-22

Nothing here requires an AVR toolchain on your machine. `make docker-build`
produces the devcontainer with all of it, and every other target runs inside it.

## The image

`build/docker/devcontainer/Dockerfile` is the one image the gate runs in
(handbook devcontainer.md §1). Its stages:

| Stage | Contents | Runs |
|---|---|---|
| `tools`, `modules` | build-only: the Go-built analysis tools at their pins; the module cache for `sdk/go`, `test/integration/esp32` and `xkms/`, fetched with the forge credentials as a BuildKit secret | nothing; copied into the stages below |
| `base` | Debian trixie-slim by digest, the AVR and host toolchains (`docker/packages.txt`), Go by checksum, a non-root user, the module cache with `GOPROXY=off` | nothing on its own |
| `toolchain` | `base` + cppcheck, gcovr, clang-format/-tidy, golangci-lint, actionlint, gosec, govulncheck, trivy, shellcheck, git, jq | build, test, security, deps-check |
| `integration` | `base` + the source baked in, no setuid bits, non-root | integration-test; `compose.yaml` beside it runs it read-only, without network or capabilities |
| `esp32` | Espressif's `espressif/idf:v5.5.5` by digest, its QEMU, Go and the module cache | `esp32-build`, `esp32-qemu-test` |

Every version is in `scripts/tool-pins.sh`. The Dockerfile sources it, and
`make deps-check` reads it. `docker/packages.txt` is the apt list: lines tagged
`[toolchain]` go to the toolchain stage only.

```sh
make docker-build MODULE_ENV_FILE=~/.config/gomodauth.env   # every stage
make docker-run CMD='make security' # one command in the toolchain image, tree mounted
make docker-ci                      # make ci in the toolchain image
make integration-test               # stage 3 in the integration image, under compose
```

`MODULE_ENV_FILE` holds the forge credentials (`GOPRIVATE` and the git
`GIT_CONFIG_*` lines) that reach the private go-xkms. They go in as a BuildKit
secret, never a build argument or a layer; without them `xkms/`'s modules do
not resolve and the build fails.

## The gate

The five stages of handbook ci-standard.md §1, each a target in `mk/stages.mk`,
wired as the fleet template wires them:

| Stage | Gates |
|---|---|
| `make build` | `firmware-matrix` (size budgets), `bootloader-check`, `sdk-build`, the three tfwsim builds, `reproducible-check`, `provenance`, `app-firmware-check`, `platform-bake-no-link-check`, `platform-bake-build-check`, `header-deps-check` |
| `make test` | `host-test`, `boot-test`, `board-sweep`, `coverage`, `test-asan`, `verify-vectors`, `xkms-coverage` |
| `make integration-test` | `integration-test-host` in the integration image: `integration-test-simavr`, `integration-test-boot`, `sdk-test` (with the cross-implementation suite), `app-sdk-check`, `xkms-test` |
| `make security` | `lint` (cppcheck, the `#if` and `__DATE__` greps, arch-leak), `lint-arch-leak-selftest`, `verify-vendor`, `releasability`, `releasability-test`, `sdk-lint`, `sdk-verify`, `go-vet`, `go-mod-verify`, `go-gosec`, `go-govulncheck`, `go-golangci` (`.golangci.yml`), `xkms-security`, `secrets-scan` |
| `make deps-check` | `scripts/deps-check.py`: every pin in `scripts/tool-pins.sh` and every Go require, `xkms/` included, against its latest release |

Each stage first checks for its tools and fails if one is missing. Run it in
the image instead. A pin may sit behind its latest release only with its reason
written in place: `HELD_<NAME>` in the pin file, or a `held:` comment on the
require's line in go.mod. deps-check needs the network.

```sh
make ci          # build, test, integration-test-host, security: what CI requires, in place
make docker-ci   # the same, inside the toolchain image
make deps-check  # the fifth stage; needs the network, so not in `make ci`
```

`make ci` runs the third stage as `integration-test-host`, in place, so it needs
no docker daemon; CI's `integration` job runs `make integration-test` in the
image. `xkms/` is in the stages, so `make ci` needs go-xkms: in the image it is
in the module cache, elsewhere it needs the forge credentials.

CI (`.github/workflows/ci.yml`, the fleet template) runs the Go jobs (lint,
security, build, test, modules, cross-arch) on the runner, the `image` job's
build, test, security, deps-check, ESP32 and trivy image steps in the
devcontainer, and `integration` (full tier: push, dispatch or the `ci:full`
label) under compose. `CI Gate` is the one required check.

`make help` lists every target.

## Host tests

```sh
make host-test   # the unit suite; under a second
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

Flashing over an ISP programmer, with the boot-flash EESAVE guard, is in
[`isp-route.md`](isp-route.md).

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

The board gets its IPv4 address from DHCP. For a network with no DHCP server, set
a static address in the board's `boards/<board>/sdkconfig.defaults` (the
`esp32p4-poe` file carries the lines commented out) or in a file of your own,
passed as `ESP_SDKCONFIG_EXTRA`:

```sh
cat > static.defaults <<'EOF'
CONFIG_TFW_ETH_STATIC_IP=y
CONFIG_TFW_ETH_IP_ADDR="192.0.2.10"
CONFIG_TFW_ETH_NETMASK="255.255.255.0"
CONFIG_TFW_ETH_GATEWAY="192.0.2.1"
EOF
make esp32-build BOARD=esp32p4-poe ESP_SDKCONFIG_EXTRA=static.defaults
```

The addresses are RFC 5737 documentation addresses; use your network's.
`CONFIG_TFW_ETH_STATIC_IP` is off by default, and while it is off the DHCP client
runs and the three values are ignored. They are declared in
`mk/arch/esp32/main/Kconfig.projbuild`, and a value that is not a dotted quad
stops the build at configure time. With a static address no DHCP client runs.

Neither target is in `make ci`, and the toolchain image carries no ESP-IDF; CI's
`image` job runs them in the devcontainer's `esp32` stage, which has it. Both refuse any toolchain but the pins in
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
