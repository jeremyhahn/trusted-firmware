# Hardware enrollment — the #7 run

> **Status: INFORMATIVE** · Scope: `make hw-enroll`, `xkms/e2e/hw_enroll_test.go`
> · Last verified: 2026-10-15
>
> Not a gate. CI has no board. It runs the same harness against `tfwsim`.

Issue #7's exit is a recorded run that shows the certificate each board was
issued. `make hw-enroll` makes that run one command. It enrolls a flashed,
provisioned board through device-attest-01, using go-xkms's ACME client and
the same flow as `TestDeviceAttestDICEEnrollsTheRealFirmware`. It then writes
a run record, which the operator commits.

The harness is `TestHardwareEnrollment` in `xkms/e2e`. Under `make xkms-test`,
which the `xkms` CI job runs, it enrolls `tfwsim-atecc608a` over serial and
reads back the record it wrote. So the code a bench run takes is the code CI
takes, and the only untested part is the board itself.

## Which board, which tier

Only the element tier enrolls through ACME. The evidence has to be signed by a
provisioned DeviceID key that the device cannot export.

| Board | Element | Can enrol |
|---|---|---|
| Arduino Mega 2560 | ATECC608C on the MIKROE-6774 Click board (build name `CRYPTO=atecc608a`) | yes, once the preconditions below are met |
| Arduino Nano (ATmega328P) | the same | not yet. Its element-tier image fits (`docs/dice-attestation/adr-possession-at-acme-finalize.md`), but it needs the ISP route below |

The owner's dev-complete criterion (2026-10-15) is both boards enrolling and
attesting **over Ethernet** against the go-trusted-platform test control
plane. Today the harness drives a board over serial only (see Ethernet below).

## Preconditions and their blockers (2026-10-15)

| Precondition | Why | Blocker today |
|---|---|---|
| ATECC608C on a MIKROE-6774, wired to the board | the element tier | the part has not arrived |
| An ISP programmer | `tfw-boot`, which measures the image into the DICE handoff, is written over ISP (`make boot-flash`). The fuses and the platform block are ISP too (`docs/device-provisioning/user.md`). The Nano has no other route | none on the bench |
| A route from the operator's host to VLAN 405 | the test control plane, and the Ethernet boards | none from the lane containers (Infrastructure) |
| An `sdk/go` tag carrying the Ethernet transport (#59) | `xkms` builds only against a tagged `sdk/go` (`docs/xkms-adapter/engineering.md`), and `v0.3.0-alpha` has no `tfw.Listen`. Until it does, the harness has no TCP target, for `tfwsim` or for a board | tag `sdk/go`, then bump `xkms/go.mod` |

## The commands

Build and flash: the Mega, element tier. The ISP programmer and port are
avrdude's `-c` and `-P`. `make flash` takes the same `CRYPTO` and `PROVISION`
as the build. The image path is made from them (`FW_DIR` in
`mk/firmware.mk`), so leaving them off flashes the default image instead.

```sh
make boot-flash BOARD=atmega2560 BOOT_TIER=atecc608a ISP_PROG=<prog> ISP_PORT=<port>   # docs/secure-boot/cli.md
make firmware   BOARD=atmega2560 CRYPTO=atecc608a PROVISION=1
make flash      BOARD=atmega2560 CRYPTO=atecc608a PROVISION=1 AVRDUDE_PORT=/dev/ttyACM0
```

Provision the element and **record the `deviceid` line** that `--commit`
prints. The whole procedure, fuses and lock bits included, is in
`docs/device-provisioning/user.md`.

```sh
tfwprov --commit --config sdk/go/templates/atecc608a-dice.conf /dev/ttyACM0 --yes-i-mean-it
tfwprov --verify --config sdk/go/templates/atecc608a-dice.conf /dev/ttyACM0
```

Reflash the production image:

```sh
make firmware BOARD=atmega2560 CRYPTO=atecc608a
make flash    BOARD=atmega2560 CRYPTO=atecc608a AVRDUDE_PORT=/dev/ttyACM0
```

Enrol, against the in-process go-xkms server. Its registry holds the DeviceID
you recorded:

```sh
make hw-enroll DEVICE=/dev/ttyACM0 DEVICEID=<hex X||Y from tfwprov>
```

Or enrol against the control plane, which holds its own registry. ACME_ROOTS
is optional; without it the system pool is used.

```sh
make hw-enroll DEVICE=/dev/ttyACM0 ACME_DIRECTORY=https://<control plane>/acme/directory ACME_ROOTS=<pem>
```

| Variable | Meaning |
|---|---|
| `DEVICE` | required. The board's serial port. A `host:port` fails and names the missing `sdk/go` tag |
| `DEVICEID` | the in-process server only. The 64-byte X‖Y DeviceID key, in hex |
| `ACME_DIRECTORY` | an external ACME directory URL. Unset, the in-process server |
| `ACME_ROOTS` | a PEM file of the roots the directory's TLS chains to |
| `BOOT_WAIT` | how long to wait for the board's BOOT event after the port opens (Go duration). Raise it for a `BOOT_MEASURE=atecc608a` boot |
| `HW_ENROLL_RECORDS` | where the record goes. Default `docs/development/enrollment-runs` |

Without `DEVICE`, the target fails and says what to set. It never falls back to
the simulator. A missing `DEVICEID`, a port that does not open, a refusal or a
certificate that does not match the attested key or the ueid also fail the
run. None of these is a skip.

Take the control plane's address and the board's addresses from the bench,
never from this tree. `make releasability` refuses them.

## The run record

Each run writes `docs/development/enrollment-runs/<board>-<transport>-<UTC>.md`.
Commit that file. It holds:

- a table with the board (from `CAPS`), transport, firmware version, crypto
  tier and attest level (from the evidence), device identity (the ueid), which
  ACME server, the evidence verdict, and the UTC time;
- the issued chain in PEM, leaf first. Against the in-process server the chain
  includes its issuing CA. Against a control plane it is the leaf alone, because
  go-xkms's `Client.DeviceAttest` returns only the leaf.

It records no address, port or URL.

## Ethernet

The board-over-TCP target and the `tfwsim`-over-TCP target
(`tfwsim.StartElementEthernet`) both go into `enrollTargets` once
`xkms/go.mod` requires an `sdk/go` tag that has `tfw.Listen`. The device dials
the host (`sdk/go/tcp.go`). So over Ethernet, `DEVICE` will be the address the
harness listens on, and the board's network block will name that address as
its endpoint (`docs/ethernet-transport/user.md`).
