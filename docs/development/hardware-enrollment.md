# Hardware enrollment — the #7 run

> **Status: INFORMATIVE** · Scope: `make hw-enroll`, `xkms/e2e/hw_enroll_test.go`
> · Last verified: 2026-10-16
>
> Not a gate. CI has no board, and it does not run this harness (#163 decision 2).

Issue #7's exit is a recorded run that shows the certificate each board was
issued. `make hw-enroll` makes that run one command. It enrolls a flashed,
provisioned board through device-attest-01, using go-xkms's ACME client and
the same flow as `TestDeviceAttestDICEEnrollsTheRealFirmware`. It then writes
a run record, which the operator commits.

The harness is `TestHardwareEnrollment` in `xkms/e2e`, and `make hw-enroll`
runs it on a board. `make xkms-test` leaves the `xkms/e2e` package out of its
list (#163 decision 2): out of the list, not skipped inside it, and
`make xkms-coverage` leaves it out too. Run directly with `go test ./e2e` and no
`TFW_HW_DEVICE`, the harness enrolls `tfwsim-atecc608a` over serial and over TCP
and reads back the record each run wrote, which is how a change to it is checked
before a bench run:

```sh
make tfwsim-atecc608a
cd xkms && TFWSIM_DIR=$PWD/../build/host go test -tags codec_json ./e2e/
```

## Which board, which tier

Only the element tier enrolls through ACME. The evidence has to be signed by a
provisioned DeviceID key that the device cannot export.

| Board | Element | Can enrol |
|---|---|---|
| Arduino Mega 2560 | ATECC608C on the MIKROE-6774 Click board (build name `CRYPTO=atecc608a`) | yes, once the preconditions below are met |
| Arduino Nano (ATmega328P) | the same | not yet. Its element-tier image fits (`docs/dice-attestation/adr-possession-at-acme-finalize.md`), but it needs the ISP route below |

The owner's dev-complete criterion (2026-10-15) is both boards enrolling and
attesting **over Ethernet** against the go-trusted-platform test control
plane. The harness drives a board over serial or over TCP (see Ethernet
below).

## Preconditions and their blockers (2026-10-15)

| Precondition | Why | Blocker today |
|---|---|---|
| ATECC608C on a MIKROE-6774, wired to the board | the element tier | the part has not arrived |
| An ISP programmer | `tfw-boot`, which measures the image into the DICE handoff, is written over ISP (`make boot-flash`). The fuses and the platform block are ISP too (`docs/device-provisioning/user.md`). The Nano has no other route | none on the bench |
| A route from the operator's host to VLAN 405 | the test control plane, and the Ethernet boards | none from the lane containers (Infrastructure) |
| A signed production image: trailer v1 for the board id, signed with the image key the element holds | `tfw-boot` boots nothing else (`docs/secure-boot/user.md`, "Install the application under tfw-boot") | `make firmware` writes no trailer, and this tree has no signing key or trailer tool. The key is go-xkms's |

## The commands

The order is the one in `docs/secure-boot/user.md`: provision through the
Mega's stock bootloader, then `tfw-boot`, then the signed production image over
ISP. `tfw-boot` has to come after provisioning: until the element holds the
image key it verifies nothing, the provisioning image included. And once it is
on, the stock bootloader is gone, so `make flash` over `/dev/ttyACM0` has
nothing to talk to.

Flash the provisioning image through the stock bootloader. `make flash` takes
the same `CRYPTO` and `PROVISION` as the build. The image path is made from them
(`FW_DIR` in `mk/firmware.mk`), so leaving them off flashes the default image
instead. The EEPROM block and the fuses go first, over ISP
(`docs/device-provisioning/user.md`, steps 1 and 2, with `EESAVE` programmed).

```sh
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

Flash `tfw-boot` over ISP. The variables are the Mega's row with the serial OTA
session. `make boot-flash` refuses any configuration that does not fit, and
`BOOT_TIER=atecc608a` alone does not (`docs/secure-boot/cli.md`). The
programmer and port are avrdude's `-c` and `-P`. It chip-erases the part: the
provisioning image goes, the element keeps its keys and, with `EESAVE`, the
EEPROM keeps its block.

```sh
make boot-flash BOARD=atmega2560 BOOT_TIER=atecc608a BOOT_MEASURE=atecc608a \
     BOOT_SIG=atecc608a BOOT_OTA=1 ISP_PROG=<prog> ISP_PORT=<port>
```

Install the production image over the same ISP, without an erase, so the boot
section stays. It must carry a trailer signed with the image key, or `tfw-boot`
stays in recovery. The build writes no trailer and nothing here signs one, so
this step waits for the signed image (preconditions above). Then set the lock
bits (`docs/device-provisioning/user.md`, step 7).

```sh
make firmware BOARD=atmega2560 CRYPTO=atecc608a
avrdude -c <prog> -P <port> -p m2560 -D -U flash:w:<signed trusted-firmware.hex>:i
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
| `DEVICE` | required. The board. Its form picks the transport: a `host:port` is TCP, the address this host listens on; anything else is a serial port |
| `DEVICEID` | the in-process server only. The 64-byte X‖Y DeviceID key, in hex |
| `ACME_DIRECTORY` | an external ACME directory URL. Unset, the in-process server |
| `ACME_ROOTS` | a PEM file of the roots the directory's TLS chains to |
| `BOOT_WAIT` | serial only. How long to wait for the board's BOOT event after the port opens (Go duration). Raise it for a `BOOT_MEASURE=atecc608a` boot |
| `CONNECT_WAIT` | TCP only. How long to wait for the board to connect (Go duration). Default `1m` |
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

The device dials the host, never the reverse (`sdk/go/tcp.go`). So over
Ethernet `DEVICE` is the address the harness listens on, and the board's
network block names that address as its endpoint
(`docs/ethernet-transport/user.md`). The harness accepts the first device that
connects and enrolls it through the same flow and the same go-xkms ACME client
as over serial. Which device connected is read from its evidence, never from
its source address.

```sh
make hw-enroll DEVICE=192.0.2.1:7000 DEVICEID=<hex X||Y from tfwprov>
make hw-enroll DEVICE=192.0.2.1:7000 ACME_DIRECTORY=https://<control plane>/acme/directory ACME_ROOTS=<pem>
```

`192.0.2.1` stands for the operator host's address on the board's network
(RFC 5737 documentation range); take the real one from the bench. A port that
cannot be bound, or no device connecting within `CONNECT_WAIT`, fails the run.

On a run with no `TFW_HW_DEVICE` the TCP target is `tfwsim.StartElementEthernet`: the
simulator boots with a network block whose endpoint is a loopback listener,
dials it, and is enrolled over that connection. Its record says `tcp`.
