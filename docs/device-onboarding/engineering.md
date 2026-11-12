# Device onboarding — engineering

> **Status: DRAFT** · Scope: trusted-firmware · Issue: #53 · Last verified: 2026-10-19
>
> The tests that pin this repository's side of the onboarding seam, and the gaps
> that remain with their owners.

See [`architecture.md`](architecture.md) for the sequence, [`api.md`](api.md)
for what each test pins.

## The tests that pin the seam

Every row is in `make ci`. The end-to-end rows run the real firmware compiled
for the host (`tfwsim`); the adapter's unit tests answer from a scripted device
and pin the adapter alone.

| Seam | Test | Target | Against |
|---|---|---|---|
| the record joins the baked serial to the element's key (steps 2–4) | `TestE2ERecordJoinsTheBakedSerialToTheElementsPoint`, `sdk/go/cmd/tfwprov/e2e_record_test.go` | `make sdk-test` | `tfwsim-prov`, the element tier linked as a provisioning image |
| the network block points the device at its endpoint (step 6) | `TestE2ENetcfgSetPointsTheDeviceAtTheWrittenEndpoint`, `sdk/go/cmd/tfwctl/e2e_netcfg_test.go` | `make sdk-test` | `tfwsim-atecc608a`, `--eth` |
| the Ethernet link and its allow-list (steps 7, 8, 11, 13) | `TestEthernet*`, `sdk/go/cmd/tfwctl/e2e_ethernet_test.go` | `make sdk-test` | `tfwsim-atecc608a`, serial and TCP side by side |
| the adapter registers and constructs from a `*tfw.Device` (step 9) | `xkms/registration/registration_test.go`, `xkms/backends_avr_test.go`, `xkms/avr/factory_test.go` | `make xkms-test` | a scripted device |
| evidence and possession (steps 11, 13) | `xkms/avr/attestation_test.go`, `xkms/avr/possession_test.go` | `make xkms-test` | a scripted device |
| device-attest-01 and its refusals (steps 10–13) | `TestDeviceAttest*`, `TestPossessionSigner*`, `xkms/avr/device_attest_test.go` | `make xkms-test` | recorded `tfwsim` frames |

What the tfwprov E2E checks, in order:

1. `--bake-eeprom --record` writes the `ueid`, and `tfwsim-prov` boots from the
   same `.eep`.
2. `--commit` onto a record that already holds a point is refused before the
   element is sent anything: both zones stay open and the record is unchanged.
3. `--commit --record` joins a `cnf.jwk` equal to `ProvPubKey(SlotDeviceID)`.
4. After a `RESET`, `ATTEST` evidence names the record's UEID and verifies under
   the record's key: the record and the evidence describe one device.
5. `--verify`, a second `--commit` and a bake of another serial leave the
   committed record unchanged.

`xkms/avr/device_attest_test.go` covers the refusals the verifier must make:
another UEID, another challenge or nonce, a request for another key, another
provisioned key, and an unprovisioned device. Its registry is a stand-in in the
test; the real one is go-trusted-platform's.

`make hw-enroll` runs `TestHardwareEnrollment`, from `xkms/e2e`, which
`make xkms-test` leaves out, against a
board on the bench, over serial or TCP
([hardware enrollment](../development/hardware-enrollment.md)). It is not in
`make ci`: CI has no board.

## What no test here covers

- The registry ingesting the record, the resolver, and the device-facing
  endpoint: go-trusted-platform's (R1, R2), so its tests.
- A backend per accepted connection: go-quicbaas's (R3).
- The verifier and CSR builder themselves: go-xkms's (R5); the E2E runs them
  at the go-xkms version `xkms/go.mod` pins.

## Gaps and their owners

| Gap | Owner | State |
|---|---|---|
| T3 — the ATmega328P cannot enrol: element-tier Ethernet image over budget, `ATTEST_CSR` off | the owner | choice pending ([the 328P budget](../ethernet-transport/adr-wire-protocol-over-w5x00.md#the-328p-budget)) |
| T4 — the ATECC608C slot map for the pre-provisioned part | the owner | ruling pending ([slot-map ADR](../device-provisioning/adr-trustflex-608c-slot-map.md)) |
| T5 — application measurements beside the firmware's in the evidence | go-xkms (question 3); matching the profile from claim 273 is go-trusted-platform's | waits on go-xkms question 3 ([architecture.md](architecture.md#this-repositorys-own-open-items)) |
| R4 — a DICE device seam for go-xkms's ACME client; today `diceElement` test glue in `xkms/e2e` | go-xkms | requirement ([architecture.md](architecture.md#requirements-on-other-repositories)) |
