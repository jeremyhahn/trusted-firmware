# Device onboarding — user guide

> **Status: DRAFT** · Scope: trusted-firmware · Issue: #53 · Last verified: 2026-10-19
>
> The operator's path today, from a blank board to a device go-trusted-platform
> can enrol. Each step links the page that is normative for it.

See [`architecture.md`](architecture.md) for why the steps are in this order and
who owns each, [`api.md`](api.md) for what the device presents.

## What you need

- An ATmega2560 board with the ATECC608C element: today the only part that can
  enrol, because `ATTEST_CSR` is built only on its element tier
  ([which parts can enrol](../dice-attestation/architecture.md)). For Ethernet,
  a W5100 or W5500 shield ([what you need](../ethernet-transport/user.md#what-you-need)).
- An ISP programmer for fuses and lock bits, and a serial connection.
- `tfwprov` and `tfwctl`: `go build ./cmd/<name>` or `go run ./cmd/<name>` from
  `sdk/go` (`make sdk-build` only checks it compiles).

## 1. Bake the platform block and start the record

```sh
tfwprov --bake-eeprom --serial <16-byte hex> --operator-pubkey <64-byte hex> \
        --out platform.eep --record device.json
avrdude -c <prog> -p <part> -D -U eeprom:w:platform.eep:i
```

`device.json` now holds `{"ueid": …}`: the serial as the device's UEID. Keep
the file: the same one goes to step 3. A record of another serial is refused,
never overwritten ([the provisioning record](../device-provisioning/cli.md#the-provisioning-record)).

## 2. Fuses and the provisioning image

Set the fuses with `avrdude`, then flash an image built with `PROVISION=1`
([full sequence](../device-provisioning/cli.md#full-sequence-including-the-parts-outside-tfwprov)).

## 3. Provision the element and join the key to the record

```sh
tfwprov --dry-run --config sdk/go/templates/atecc608a-dice.conf <port>
tfwprov --commit  --config sdk/go/templates/atecc608a-dice.conf --record device.json --yes-i-mean-it <port>
tfwprov --verify  --config sdk/go/templates/atecc608a-dice.conf --record device.json <port>
```

`--commit` is irreversible ([the guard](../device-provisioning/cli.md#the-irreversibility-guard)).
It generates the DeviceID key and adds it to the record as `cnf.jwk`; `--verify`
reads the key back and checks it against the record.

The ATECC608C-TFLXTLS on the MIKROE-6774 board ships locked. It takes the
TrustFLEX template instead, by the (a1) slot map owner ruling 1243 accepted
([slot-map ADR](../device-provisioning/adr-trustflex-608c-slot-map.md#applied)):

```sh
tfwprov --dry-run --config sdk/go/templates/atecc608a-trustflex.conf <port>
tfwprov --commit  --config sdk/go/templates/atecc608a-trustflex.conf --image-key <image-key.pem> --record device.json --yes-i-mean-it <port>
tfwprov --verify  --config sdk/go/templates/atecc608a-trustflex.conf --image-key <image-key.pem> --record device.json <port>
```

`--dry-run` prints the seven steps. `--commit` keeps the shipped slot-0
DeviceID, seeds the UDS into slot 6, writes the `--image-key` public key to
slot 15 and slot-locks both
([the TrustFLEX plan](../device-provisioning/cli.md#the-trustflex-plan)). Until the slot-6
lock CRC is computed in the firmware, `--commit` refuses a TrustFLEX part
before it writes anything; `--verify` reads the part and writes nothing.

## 4. Lock bits and the production image

Only after `--verify` passes: lock bits, then the production image, `make ETH=1`
for Ethernet ([build](../ethernet-transport/user.md#build)).

## 5. Write the network block

Over serial; the Ethernet link refuses `EEPROM_WRITE`:

```sh
tfwctl <port> netcfg set --mac <mac> --ip <a.b.c.d> --mask <a.b.c.d> \
    --gateway <a.b.c.d> --endpoint <a.b.c.d:port>
tfwctl <port> netcfg get
```

The endpoint is go-trusted-platform's device-facing address, taken from its
deployment. Flags and exit codes: [ethernet-transport CLI](../ethernet-transport/cli.md).
`tfwprov --bake-netblock` writes the same block into the step 1 image instead.

## 6. Hand the record to go-trusted-platform

`device.json` is the device's identity: the registry entry is built from it,
from the bench, never from the enrolment path. Ingesting it is
go-trusted-platform's (go-trusted-platform#772); this repo has no tool that
does it. From then on the device dials the endpoint and go-trusted-platform
runs the enrolment ([steps 7–14](architecture.md#field-steps-714)).

To try an enrolment without a control plane, `make hw-enroll` runs go-xkms's
ACME client and server against the board in process
([hardware enrollment](../development/hardware-enrollment.md)).

## Open: the bench

- **T3 — the ATmega328P enrols.** Owner ruling 1243 gives it its 28 672-byte
  application region as its flash budget and its element image carries
  `ATTEST_CSR` ([the 328P budget](../ethernet-transport/adr-wire-protocol-over-w5x00.md#the-328p-budget)).
  Still open: a Nano answering `ATTEST_CSR` on the bench, and the element-tier
  Ethernet image, which fits flash but not RAM.
- **T4 — the ATECC608C slot map.** (a1) is accepted and step 3 above follows
  it. Still open: the ADR's bench questions, read before any lock, and
  `make hw-enroll` on a MIKROE-6774 board
  ([slot-map ADR](../device-provisioning/adr-trustflex-608c-slot-map.md#applied)).
