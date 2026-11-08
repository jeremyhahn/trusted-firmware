# Testing

> **Status: INFORMATIVE** · Scope: `make ci`, `make bench-check` · Last verified: 2026-10-19
> How this firmware is tested, what each tier can and cannot catch, and why the
> tiers are separate.

Everything the gate runs needs **no hardware attached**. A bench Arduino is for
bring-up and for `make hw-test` (below), never for a gate.

## The three tiers

| Tier | Target | Runs | What only it can catch |
|---|---|---|---|
| 1 — unit | `make host-test` | x86-64, mock HAL | logic: framing, dispatch, bounds, the scheduler's rollover |
| 2 — emulated | `make integration-test-simavr` | the real `.elf` under simavr | register-level behaviour: baud divisor, interrupt vectors, peripheral bring-up order, tick rate |
| 2 — emulated, Cortex-M | `make integration-test-cortex-m`, `make integration-test-cortex-m-wire` | the real tfw-boot and application images under QEMU `mps2-an505` and `mps2-an500` | the Cortex-M port's vector table, reset and handoff; tfw-boot's P-256 verify refusing a tampered image and a foreign key on a 32-bit core (#161) |
| 3 — integrity | `make lint size-check reproducible-check` | the build itself | drift: flash/SRAM budgets, byte-for-byte reproducibility, out-of-whitelist `#if` |
| 4 — cost (not a gate) | `make bench-check` | the real `.elf` under simavr | drift in TIME: cycles per decoded byte, per scheduler sweep, per tick interrupt, and the round-trip latency |

`make ci` runs tiers 1 to 3 plus the sanitizers and the Go SDK suite. **Tier 4
is run deliberately and gates nothing** — see `performance.md` for why, and for
the numbers. The Cortex-M rows of tier 2 are not in `make ci`. CI's image job runs
them as their own steps, `make integration-test-cortex-m (toolchain image)` and
`make integration-test-cortex-m-wire (toolchain image)`, after building each
board's two images (`building.md`, Cortex-M).

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
A stage whose tool is missing fails and names it. It never skips. CI splits
`make test` into three duration-balanced steps of its one Unit job, `make
test-shard-N`, beside the sdk/go race suite, which
together run each of its gates once (`building.md`, The gate).

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

## Waits, not sleeps

A test waits for the thing it needs — a reply arrived, a byte was read — and
the wait has a bound. An expired bound fails the test, and the failure names
the wait and the bound. A fixed delay is too short on a loaded runner and too
long everywhere else, and when it is too short the failure blames the reply,
not the wait.

In the simavr harnesses the form is `run_until(done, budget)`, and
`wait_for(done, budget, what)` where an expired bound fails the row. Each
budget is the step count of the fixed wait it replaced, so no row got slower
(#163). `send()` puts each byte in once the device has read the one before it.
Two fixed paces stay in `boot_harness.c` because they are not waits:

- the junk stream's one byte per character time, because that rate is the
  input under test;
- the host bridge's slice of free running between host bytes. tfwctl, on the
  other end, bounds the exchange, and between bytes there is nothing to wait
  for.

`make lint-test-sleep`, part of `make lint`, fails on any `time.Sleep` in a
`*_test.go` and any `time.sleep(` in a `test_*.py` or `*_test.py`. It prints
`path:line: test-sleep` for each one.

### Exemptions proposed to the owner

In these places the elapsed time is what is under test, so the fixed interval
is the stimulus, not a wait. They are proposed, not granted: #163 decision 4
has no answer yet.

| Site | Interval | Why it is fixed |
|---|---|---|
| `test/integration/simavr/harness.c`, the tick check | 1 s of emulated clock | the millisecond tick ISR's count across exactly one second is the measurement |
| `test/integration/simavr/bench_harness.c`, the scheduler's firing sweep | 10 ms of emulated clock | the firmware's own tick must carry the millisecond counter past the deadline |
| `sdk/go/internal/peer/peer.go`, `ResetPulse` | 100 ms of DTR held off | the pulse length is what resets the board; there is no condition to watch |

## Tier 2, Cortex-M — the real images under QEMU

Two commands run the rows, each on `mps2-an505` and then `mps2-an500`:

```sh
make integration-test-cortex-m
make integration-test-cortex-m-wire
```

The first signs the application three ways with
`test/integration/cortex-m/cm_image.c` and runs
`test/integration/cortex-m/qemu_test.sh`, which boots each image in its own
`qemu-system-arm`, with the UART on a Unix socket, and checks it with
`wirecheck` (`test/integration/esp32/wirecheck`) (#161):

| Row | Image | What must hold |
|---|---|---|
| genuine | signed with the provisioned key | CAPS reports the board's id and tier 1, across a soft reset; QEMU is still up |
| tampered | one bit flipped after signing | tfw-boot refuses it: no PING is answered for the whole window, and QEMU is still up |
| otherkey | signed with another key | the same as tampered |

The second boots the signed image with its UART on a pty and runs the tfwctl
wire suite (`make hw-test`) against it, resetting the guest with QMP
`system_reset` where a board's DTR line would
(`test/integration/cortex-m/qemu_wire_test.sh`), then the store, two-slot, SPI
and I2C rows. `docs/cortex-m/README.md` lists every row per board.

No row sleeps: every wait is a bounded one (`CM_WAIT`, default 60 s, for an
answer; `CM_SILENT`, default 10 s, for a refused image's silence). A missing
`qemu-system-arm` fails the target with its Debian package's name, and a
missing Arm toolchain fails the build of the images; nothing skips.

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

`make hw-test` runs the cross-implementation scenarios, the `tfwctl`
`TestFirmware*` tests that `make sdk-test` runs against `tfwsim`, against the
image flashed on a real board. Which rows those are, and the board each needs,
is tabled under [Which rows run on a board](#which-rows-run-on-a-board). Each drives one `tfwctl` verb and keeps its
refusals. They are the same scenarios, not a copy.
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

`BOARD` selects the pin facts the scenarios expect (`peerBoards` in
`sdk/go/cmd/tfwctl/e2e_firmware_test.go`), and `CAPS` must report that board. `tfwsim` is the
328P. A board that is not in that table fails the run. So do a missing `PORT`,
a port that does not exist, and a board that does not answer within the
bound. None of these is a skip. Flash the image first: the target tests what
is on the board and flashes nothing.

It is outside `make ci` because CI has no board. The seam itself is
unit-tested on the host (`sdk/go/internal/peer`), and that test is in
`make sdk-test`.

### Which rows run on a board

```sh
make hw-test BOARD=<board> PORT=<serial port> [HW_ETH=<ip:port>] \
  DEVICEID=<hex X||Y> CRI_DIGEST=<hex> OPERATOR_KEY=<operator key PEM>
```

That one command runs the rows below that the board's image serves. Each reaches its device through
`sdk/go/cmd/tfwctl/e2e_peer_test.go`, the serial rows' side of the seam. The
rows need different images, so no single board passes them all. Each row checks
the crypto tier its image needs before anything else, and a board flashed with
the other tier fails the row with the tier named. A row about one feature
fails the same way, naming the feature and the board, when the image lacks it:
a CAPS bit that is clear, or the OTA stager, OTA_INFO or an application, which
CAPS has no bit for (#7). It is never skipped, and never ends early as a pass.
So which rows run is chosen by name. On an AVR board the command runs the rows
its default image serves, chosen from the board's entry in `boardImages` and
each row's needs in `firmwareRowNeeds`
(`sdk/go/cmd/tfwctl/e2e_rows_test.go`). A row in no list fails the run, and so
does a board with no list. `HW_TEST_RUN`, when given, is the run instead, for
example `HW_TEST_RUN='^TestFirmwareOTA'`; `test/integration/cortex-m/qemu_wire_test.sh`
names the rows of each QEMU image that way. The refusal rows run where the
image lacks a family. Each asserts that the family's command is refused as
unknown, and that the soft tier answers OTA_ACTIVATE with no OTA_AUTH with
`AUTH_REQUIRED`.

| File | Rows (`TestFirmware*`) | The board they need | Values they take |
|---|---|---|---|
| `e2e_firmware_test.go` | every row | soft tier | `BOARD` |
| `e2e_dice_test.go` | the seven alias-certificate and evidence rows, `AttestCertWithoutAnApplicationCarriesTheFirmwareMeasurement` and `ElementReportsItsRootOfTrust` | element tier, provisioned | `DEVICEID` |
| `e2e_dice_test.go` | `BlankElementRefusesToAttest` | element tier, not yet provisioned | — |
| `e2e_dice_test.go` | `SoftwareTierStillCannotIssueACertificate` | soft tier | — |
| `e2e_auth_test.go` | the five AUTH rows | element tier, provisioned with an operator key | `OPERATOR_KEY` |
| `e2e_attestcsr_test.go` | the four signature and caps rows, `AttestCSRRefusesBWithNoCertificateMintedForIt` and `ElementWithoutTheCapsBitIsRefusedAsUnsupported` | element tier, provisioned | `DEVICEID`, `CRI_DIGEST` |
| `e2e_attestcsr_test.go` | `AttestCSRReportsADeviceThatCannotIssueOne` | soft tier | — |
| `e2e_ota_test.go` | `OTAAuthLetsTheCommandPastTheGate` | soft tier with the OTA stager | — |
| `e2e_ota_test.go` | `OTAInfo`, `OTAActivate`, `OTAConfirm` | element tier with the OTA stager, provisioned with an operator key, OTA state fresh | `OPERATOR_KEY` |
| `e2e_app_test.go` | `AppReportsItsApplication` | the heartbeat application image | — |
| `e2e_app_test.go` | `AppIsAnUnknownCommandWithoutAnApplication` | soft tier, no application | — |
| `e2e_refusal_test.go` | `RefusesTheAuthSessionItsCapsLacks`, `RefusesAttestCSRItsCapsLacks` | CAPS bit 14, or bit 15, clear | — |
| `e2e_refusal_test.go` | `SoftTierOTAActivateWithoutOTAAuthIsAuthRequired` | soft tier with the OTA stager, nothing staged | — |
| `e2e_refusal_board_test.go` (`hwtest` tag, hw-test only) | `Refuses{Analog,PWM,Pulse,EEPROM,I2C,SPI,OneWire}ItsCapsLacks` | that family's CAPS bit clear | — |

On a board, a row checks the identity the board reports over `PORT`: its serial
number and FWHash. It checks the DeviceID key against `DEVICEID` and the CSR
digest against `CRI_DIGEST`, both described in the next section. The operator
key is `OPERATOR_KEY`, the PEM file of the key `tfwprov` provisioned the board
to trust, in the form `tfwctl auth` reads. `hw-test` passes it as
`TFW_HW_OPERATOR_KEY`. A row that needs one of these values fails when it is
unset and names the value.

The OTA rows hand over any 64 bytes as the signature. The application only
holds that signature; tfw-boot verifies it at reset. So the image
`ota-activate` leaves PENDING on a board is refused at the board's next reset.

These rows stay on `tfwsim` and keep their `TestE2E*` names, so `hw-test` does
not run them:

| Row | Why it is simulator-only |
|---|---|
| `TestE2EAttestCertSameNonceIsReproducible` | the element model signs with a deterministic k; the part draws its own |
| `TestE2EAttestRejectsAWrongLengthNonceBeforeTheWire` | it reaches no device |
| `TestE2EAttestCertCarriesTheApplicationsEvidence` | it holds the evidence frame's FWHash to the known `TestFWHash`; a board's FWHash is read from claim 273, which on this image carries the application's bytes |
| `TestE2ETheDefaultElementHasNoOperatorToAuthenticate` | it pins the simulator's default, a zero operator key; a board's is whatever `tfwprov` wrote |
| `TestE2ENetcfgSetPointsTheDeviceAtTheWrittenEndpoint` | it needs an erased network block, an image only the simulator is handed, and on a board `netcfg set` would overwrite the provisioned block |
| `TestE2ESimprocStartRunsTheSimulatorAndCloseEndsIt` | it tests the simulator process itself |
| every row of `e2e_auto_test.go` | they vary whether the element answers, a simulator option; a board is wired one way |
| every row of `e2e_launch_test.go` | they test the simulator's launch options, and a board has none |

### The Ethernet rows on a board

```sh
make hw-test BOARD=<board> PORT=<serial port> HW_ETH=<ip:port> DEVICEID=<hex X||Y> CRI_DIGEST=<hex>
```

`HW_ETH` adds the `TestEthernet*` rows to the run: every row of
`sdk/go/cmd/tfwctl/e2e_ethernet_test.go` (CAPS, ATTEST, the alias certificate,
the CSR signature, the allow-list refusals, the OTA staging ids, AUTH and
OTA_ACTIVATE refused, serial and Ethernet side by side, the network block read
back) and the two staging rows of `e2e_ota_test.go`. `HW_ETH` is the `ip:port` on
the bench host that the board's network block names as its endpoint
(`tfwctl netcfg`); `hw-test` passes it as `TFW_HW_ETH`, and it has no default.
It is not `ETH`, the firmware build's Ethernet switch, which a board's
`profile.mk` may set: the 2560's says `ETH ?= 1`, and while the two shared a
name a plain `make hw-test BOARD=atmega2560 PORT=…` ran the Ethernet rows
against an endpoint of `1`.

The rows take the same seam as the serial ones. Each listens where
`peer.Target.Listen` says: a loopback port the kernel picks for `tfwsim`,
written into the simulator's network block, or `HW_ETH` on a board. Each then
starts the device through `peer.Start`, which resets a board through `PORT`,
and waits for the device to dial, up to 30 s. There is no wall-clock wait. A
board that never dials fails the row, and so do `HW_ETH` without `PORT`, an `HW_ETH`
that is not an `ip:port`, and port 0.

The assertions are the same on both peers. Where a row checks the device's
identity, `tfwsim` is expected to answer as the documented test identity. A
board is expected to answer as itself:

- its crypto tier and serial number are the ones it reports over `PORT`;
- its DeviceID key is `DEVICEID`, the hex X||Y `tfwprov --commit` printed (the
  same value `hw-enroll` takes);
- the CSR row's digest is `CRI_DIGEST`, the SHA-256 of the
  CertificationRequestInfo the board signs for b = 00 01 … 1f. go-xkms builds
  that structure (go-xkms#1441), and `tfwctl` does not.

A row that needs `DEVICEID` or `CRI_DIGEST` fails when it is unset. The rows
expect an element-tier image: CAPS must report the attestation commands, and
the staging rows need the OTA stager linked.

## Enrolling a real board — `make hw-enroll` (not a gate)

```sh
make hw-enroll DEVICE=/dev/ttyACM0 DEVICEID=<hex X||Y from tfwprov>
```

This is issue #7's recorded run. It enrolls a flashed, provisioned
element-tier board through device-attest-01 and writes a run record to commit
under `docs/development/enrollment-runs/`. The harness is
`TestHardwareEnrollment` (`xkms/e2e`); `make hw-enroll` runs it on a board, and
`make xkms-test` leaves the `xkms/e2e` package out (#163 decision 2).
Without `DEVICE` it fails. The procedure, the preconditions and
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
make xkms-test xkms-coverage    # every xkms suite but TestHardwareEnrollment; the unit tests' 90 % floor, no simulator
make xkms-security xkms-deps-check  # its security and latest-version checks; the same job
make hw-test BOARD=atmega2560 PORT=/dev/ttyACM0  # the cross-implementation suite on a flashed board
make hw-test BOARD=atmega2560 PORT=/dev/ttyACM0 HW_ETH=<ip:port> DEVICEID=<hex> CRI_DIGEST=<hex> OPERATOR_KEY=<pem>  # every row a board runs, the Ethernet rows too
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

## Owner test standard (m655)

The owner's standard of 2026-10-18 (#163): integration tests are only the
CLI-driven multi-protocol E2E suites, with none for lower layers. Every package
is unit-tested to 90 % or better, in memory. No test at any tier sleeps. A PR
pipeline finishes within one hour. Where each part is met:

| Item | Where it is met |
|---|---|
| (a) Inventory of the integration tests by layer, and of every sleep | The table on #163, posted before any code changed |
| (b) A lower-layer integration test moves into its package's unit suite, or E2E, or goes | #169 (`b0eab87`): `sdk/go/tfwsim`'s simulator suites (attest CSR, auth, auto, DICE, Ethernet, launch, OTA) became `tfwctl` E2E, `sdk/go/cmd/tfwctl/e2e_*_test.go`, and the package kept in-memory fakes (`fake_test.go`, `operator_test.go`). Every test that starts a simulator carries the `tfwsim` build tag, and only `make sdk-test` compiles it in. #170 (`6928bce`): `internal/simproc`'s start test went to `internal/peer`, and `tfwctl` gained the app and netcfg E2E. The cross-implementation suite (`sdk/go/tfwsim_test.go`) becoming `tfwctl` E2E is in flight (#163 decision 1) |
| (c) Every sleep becomes a bounded polling helper | #169 (`b0eab87`): `awaitFile` in `sdk/go/cmd/tfwctl/ota_stage_simavr_test.go`, `wait_for` in `test/integration/esp32/qemu_test.py`, and fakes driven by the test in `sdk/go`, `internal/peer`, `serialport` and `xkms/avr` |
| (d) 90 % or better unit coverage per package | `make sdk-coverage` and `make xkms-coverage` fail when any one package is under 90 %, not only the module total (`mk/go.mk`, `go-cover-floor`). `sdk-coverage` builds no simulator: the `tfwsim` tag is off. `make coverage` holds the C sources at 90 % (`mk/host.mk`); #166 (`81cdd4c`) raised the lowest of them |
| No sleep at any tier | `make lint-test-sleep`, part of `make lint` (#172, `8b640c9`), fails on `time.Sleep` or `time.sleep(` in any Go or Python test file. Production code sleeps only where the delay is the protocol: `internal/peer`'s reset pulse and `scripts/deps-check.py`'s backoff |
| PR pipeline under one hour | CI minutes: coordinator |

Counts, before (`c33e82d`, the last main commit before #166) and after
(`8b640c9`):

| Measure | Before | After |
|---|---|---|
| Go test files behind an integration build tag (`tfwsim`, `simavr`) | 2 | 16 |
| Simulator-driven test files in the lower-layer packages `sdk/go/tfwsim` and `internal/simproc` | 8 | 0 |
| `tfwctl` E2E test files (`sdk/go/cmd/tfwctl/e2e_*_test.go`) | 0 | 10 |
| Sleep calls in test files | 16, in 8 files | 0 |
| Sleep calls outside test files | 4, in 3 files | 2, in 2 files |
| Lowest Go package unit coverage | not gated per package | 91.2 % (`sdk/go/tfwsim`) |
| Lowest C source line coverage under `make coverage` | — | 95.2 % (`boot/boot_ota.c`) |
| PR pipeline, CI minutes | coordinator | coordinator |
