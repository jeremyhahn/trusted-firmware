# Device onboarding — architecture

> **Status: DRAFT** · Scope: trusted-firmware (device side, `sdk/go`, the `xkms`
> adapter), seam only for go-xkms, go-quicbaas and go-trusted-platform ·
> Issue: #53 · SPEC row: `device-onboarding` (future) · Last verified: 2026-10-18
>
> This page traces a device from first power-on to enrolled and marks, for every
> step, which repository owns it. It adds no command, call or vector. What it
> finds missing is written as a requirement on the repository that owns it
> ("Requirements on other repositories", below), never as a workaround here.

See also: [`../device-provisioning/`](../device-provisioning/architecture.md)
(the bench steps), [`../ethernet-transport/`](../ethernet-transport/architecture.md)
(the network block, the Ethernet link and the `sdk/go` listener),
[`../xkms-adapter/`](../xkms-adapter/architecture.md) (the backend go-xkms
loads), [`../dice-attestation/`](../dice-attestation/architecture.md) (the
evidence) and its
[possession ADR](../dice-attestation/adr-possession-at-acme-finalize.md) (ACME
finalize).

## Ownership boundary

Onboarding is mostly not this repository's. It supplies the device, the evidence,
the host SDK that speaks to the device, and the adapter that lets go-xkms treat
the device as a key provider. It owns no enrolment, registry, profile or
verification.

| Repository | Owns in onboarding |
|---|---|
| trusted-firmware (this) | the firmware and its wire commands; provisioning tools (`tfwprov`); the network block and its encoding; `sdk/go` (serial, TCP listener, typed calls); the `xkms` adapter module and its registration |
| go-xkms | DICE and EAT/UCCS verification, the device-attest-01 challenge and its nonce, the possession-CSR profile and builder, the ACME client and server, the backend registry hook |
| go-quicbaas | composing go-xkms's verification with this repo's adapter as one hardware implementation; it has no firmware dependency |
| go-trusted-platform | the device-facing endpoint, running the ACME order, the device registry and the resolver go-xkms consults, matching the device to its profile, the enrolment record and fleet policy |
| operator | the bench: fuses, lock bits, the element, the addresses written into the network block |

## The sequence

```
 BENCH (serial, provisioning image)                          owner
 1  fuses                       avrdude                      operator
 2  platform block (serial)     tfwprov --bake-eeprom        trusted-firmware
 3  element: DeviceID key       PROV_* 0x70–0x75, tfwprov    trusted-firmware
 4  provisioning record ──────► device registry entry        go-trusted-platform
 5  lock bits, production image (ETH=1)                      operator / trusted-firmware
 6  network block               EEPROM_WRITE 0x41            trusted-firmware
                                                             (values: operator)
 FIELD (Ethernet, production image)
 7  device dials the endpoint   link_eth.c, socket 0         trusted-firmware
 8  endpoint accepts            tfw.Listener.Accept          go-trusted-platform
                                                             (listener: trusted-firmware)
 9  backend over the connection go-xkms registry → "tfw"     go-quicbaas
                                avr.NewKeyProvider, CAPS 0x00 (adapter: trusted-firmware)
10  ACME order + challenge      device-attest-01             go-trusted-platform runs it,
                                b = DICENonce(keyAuth)       go-xkms owns it
11  evidence                    ATTEST_CERT 0x63 (b)         trusted-firmware
12  verification                registry-anchored DICE       go-xkms (verifier),
                                                             go-trusted-platform (resolver)
13  finalize: possession        ATTEST_CSR 0x64 (same b)     trusted-firmware (signature),
                                CSR rebuilt, verified        go-xkms (CRI, CSR, CA)
14  record, profile match       registry, fleet policy       go-trusted-platform
 RUNTIME
15  re-attestation              ATTEST 0x60 / ATTEST_CERT    trusted-firmware (evidence),
                                                             go-xkms (verification)
```

### Bench: steps 1–6

Everything before the device reaches a network happens on a wired bench, and
the commands that do it are absent from a production image
([device-provisioning](../device-provisioning/architecture.md#trust-boundaries)).

1. **Fuses.** `avrdude` sets `BOOTRST`, `BOOTSZ`, `BODLEVEL`, and clears `DWEN`.
   Operator.
2. **Platform block.** `tfwprov --bake-eeprom --serial <hex>` writes the 16-byte
   device serial into the platform-reserved EEPROM block. That serial is the
   DeviceID field of the evidence, and the UEID is `0x01 ‖ serial`. This repo.
3. **The element.** `tfwprov --commit` drives `PROV_WRITE_CONFIG` (`0x71`),
   `PROV_LOCK` (`0x72`), `PROV_GENKEY` (`0x73`) and `PROV_SEED_UDS` (`0x75`), and
   `PROV_PUBKEY` (`0x74`, `Device.ProvPubKey`) reads back the DeviceID public
   point. The ATECC608C on the MIKROE-6774 Click board ships with its zones
   locked, and `--commit` refuses it until the owner chooses a slot map
   ([device-provisioning engineering](../device-provisioning/engineering.md#the-atecc608c-mikroe-6774-click-board)).
   This repo.
4. **The provisioning record.** The pair (UEID, DeviceID SPKI) is what the device
   registry anchors enrolment in. Writing that entry, from the bench and never
   from the enrolment path, is go-trusted-platform's (go-trusted-platform#772).
   The pair leaves this repo as `tfwprov --record`'s JSON file: the UEID
   (RFC 9711 §4.2.1) as `ueid` and the DeviceID point as an RFC 7517 JWK in the
   RFC 7800 §3.2 `cnf` claim
   ([device-provisioning CLI](../device-provisioning/cli.md#the-provisioning-record)).
5. **Lock bits and the production image.** Lock bits go on only after `--verify`
   passes. The production image carries no `PROV_*` handler. An Ethernet image is
   `make ETH=1` ([build selection](../ethernet-transport/architecture.md#build-selection)).
6. **The network block.** `Device.WriteNetConfig` encodes `tfw.NetConfig` and
   writes the 32-byte block at EEPROM `0x0C0` with `EEPROM_WRITE` (`0x41`) over
   serial; `tfwctl netcfg set` does it from the command line, and
   `tfwctl netcfg get` reads it back through `Device.ReadNetConfig`. It holds the MAC, the device's address, mask, gateway, and the
   endpoint's address and port. Until a shield's assigned MAC is supplied, the
   host writes `tfw.DerivedMAC(id)`. The encoding is this repo's; the values,
   above all the endpoint, are the operator's, taken from go-trusted-platform's
   deployment ([addressing](../ethernet-transport/adr-wire-protocol-over-w5x00.md#3-addressing)).

### Field: steps 7–14

7. **The device dials.** At each attempt `link_eth.c` reads the block, resets and
   identifies the W5100 or W5500, and opens one TCP connection on socket 0 to the
   endpoint. It retries on a 1 s to 64 s backoff. The device never listens. This
   repo ([data flow](../ethernet-transport/architecture.md#data-flow)).
8. **The endpoint accepts.** go-trusted-platform's device-facing endpoint calls
   `tfw.Listen` (or `tfw.NewListener` over a listener it already owns) and
   `Accept`, which returns a `*tfw.TCPDevice` embedding an ordinary `*tfw.Device`.
   The endpoint is go-trusted-platform's (R1); the listener is this repo's.
   `RemoteAddr` is for logs. **Who the device is comes from its evidence, never
   from its address.**
9. **The backend.** go-quicbaas obtains the `tfw` backend from go-xkms's registry
   with the factory config `{"device": *tfw.Device}`, and optionally
   `{"config": *avr.Config}` for timeouts and trust anchors. The factory
   (`avr.NewKeyProvider`) reads `CAPS` once and caches it: crypto tier, algorithm
   bitmap and feature bits. A part with no key to hold an identity fails here
   (`ErrAVRNoDeviceKey`); one without the `ATTEST_CSR` feature bit is refused
   by `PossessionSigner`, before the challenge is answered rather than at
   finalize.
   Composition is go-quicbaas's (R3); the adapter is this repo's.
10. **The order and the challenge.** The ACME order names the device by a
    `permanent-identifier` whose value is the 34 lowercase hex digits of the
    UEID. The challenge is device-attest-01. The device's challenge `b` is
    `deviceattest01.DICENonce(keyAuthorization)`, `SHA-256` of the key
    authorization, computed in go-xkms and nowhere else. go-trusted-platform runs
    the order with go-xkms's ACME client; go-xkms owns the protocol and the
    nonce.
11. **Evidence.** `Backend.AttestKey(ctx, attrs, b)` sends `ATTEST_CERT` (`0x63`)
    with the 32-byte `b`. The device mints an alias certificate whose
    `tcg-dice-UCCS-evidence` extension carries `nonce` (10) = `b`, `ueid` (256)
    and `measurements` (273). The adapter checks the nonce in constant time
    before it returns anything. The ACME client posts the DER verbatim as `dice`
    evidence. This repo.
12. **Verification.** go-xkms's registry-anchored DICE verifier resolves the
    claimed UEID through go-trusted-platform's resolver. It requires the leaf's
    SPKI to equal the provisioned key and the leaf's signature to verify under
    it (or the chain to anchor in the DICE roots, once a DeviceID certificate
    exists), then checks nonce, UEID and measurement. No record, a lookup error
    or a mismatch makes the challenge invalid. go-xkms (go-xkms#1441) and
    go-trusted-platform (go-trusted-platform#772). This repo verifies nothing.
13. **Finalize.** `Backend.PossessionSigner(att)` returns a `crypto.Signer` for
    the same `b`. go-xkms's `deviceattest01.SignPossessionCSR` rebuilds the
    365-byte CertificationRequestInfo from the alias certificate and `b`, and
    asks the signer for a signature over its digest. The signer sends
    `ATTEST_CSR` (`0x64`) with `b`, receives the device's 64-byte `r‖s`, and
    returns it only if it verifies over that digest under the attested key. The
    device signs a request it builds from its own template; it never signs bytes
    the host sends. go-xkms wraps the CSR and its CA issues
    ([possession ADR](../dice-attestation/adr-possession-at-acme-finalize.md)).
14. **Record and profile.** go-trusted-platform checks that the certified key is
    the provisioned DeviceID key, records the enrolment, matches the device to
    its device profile and applies fleet policy. go-trusted-platform.

### Runtime: step 15

Over the same connection the control plane re-attests with `ATTEST` (`0x60`, a
16-byte nonce, the 164-byte evidence frame) or `ATTEST_CERT`. Freshness is the
verifier's nonce alone; no board in this family has a clock.

## What this repository provides at the seam

### Wire commands

The Ethernet link admits exactly `0x00`, `0x06`, `0x60`, `0x63` and `0x64`, pinned id by
id against literals in `test/unit/test_link_eth.c`. Every other id received on
Ethernet answers `TFW_ERR_UNKNOWN_CMD` and is never dispatched
([allow-list](../ethernet-transport/adr-wire-protocol-over-w5x00.md#2-allow-list)).

| Id | Command | Link | `sdk/go` call | Pinned by |
|---|---|---|---|---|
| `0x00` | `CAPS` | serial, Ethernet | `Device.Caps` | `TFWV_CAPS_PROTO_REPLY`, `TFWV_CAPS_PIN13_REPLY` in `testdata/vectors.h`; Go mirror `sdk/go/internal/vectors` |
| `0x06` | `APP_INFO` | serial, Ethernet; images with an application only | `Device.AppInfo` (the descriptor's `profile` among it) | `TFWV_APPINFO_*` in `testdata/vectors.h`; Go mirror `sdk/go/internal/vectors`; checked from Go against `tfwsim-app-heartbeat` |
| `0x60` | `ATTEST` | serial, Ethernet | `Device.Attest` (16-byte nonce) | the frame layout in `include/tfw/protocol.h`; no literal frame vector |
| `0x63` | `ATTEST_CERT` | serial, Ethernet | `Device.AttestCert` (32-byte nonce) | `TFWV_UCCS`, `TFWV_ALIAS_TBS_DIGEST`, `TFWV_ALIAS_CERT` and its negative variants in `testdata/uccs_vectors.h` |
| `0x64` | `ATTEST_CSR` | serial, Ethernet | `Device.AttestCSR` (32-byte `b`, returns 64 bytes) | `TFWV_CSR_CRI`, `TFWV_CSR_CRI_DIGEST` in `testdata/vectors.h`; checked from Go in `sdk/go/tfwsim/attestcsr_test.go`; go-xkms pins its CRI builder to the same bytes |
| `0x41` | `EEPROM_WRITE` | serial only | `Device.WriteNetConfig` | the network block, a literal in both `test/unit/test_link_eth.c` (`NETCFG`) and `sdk/go/netcfg_test.go` (`netcfgVector`) |
| `0x40` | `EEPROM_READ` | serial only | `Device.ReadNetConfig` | the same literal, decoded by `(*NetConfig).UnmarshalBinary` in `sdk/go/netcfg_test.go` |
| `0x70`–`0x75` | `PROV_*` | serial, provisioning image only | `Device.Prov*` | [device-provisioning api](../device-provisioning/api.md) |

`ATTEST_CSR` is built only where the board's profile carries it: today the
ATmega2560's element tier
([which parts can enrol](../dice-attestation/architecture.md)).

### `sdk/go`

`sdk/go` is Layer 0 with no in-house dependencies; go-trusted-platform imports it
directly. go-quicbaas does not need to (R3).

| Call | Used at step |
|---|---|
| `tfw.Listen`, `tfw.NewListener`, `(*Listener).Accept` → `*TCPDevice` | 8 |
| `tfw.NetConfig`, `(NetConfig).MarshalBinary`, `(*NetConfig).UnmarshalBinary`, `tfw.DerivedMAC`, `(*Device).WriteNetConfig`, `(*Device).ReadNetConfig` | 6 (through `tfwctl netcfg`) |
| `(*Device).Caps`, `Attest`, `AttestCert`, `AttestCSR` | 9, 11, 13, 15 (through the adapter) |
| `(*Device).ProvInfo`, `ProvWriteConfig`, `ProvLock`, `ProvGenKey`, `ProvPubKey`, `ProvSeedUDS` | 3 (through `tfwprov`) |
| `tfwsim.StartElementEthernet` | tests: the real firmware, element tier, on both links |

### The `xkms` adapter

- **Registration.** Importing `github.com/jeremyhahn/trusted-firmware/xkms` runs
  `init()` in `xkms/register.go`, which calls `RegisterAVRBackend`:
  `goxkms.RegisterExternalBackend` with type `tfw` (`AVRBackendType`) and
  factory `avr.NewKeyProvider`. A second registration in one process fails with
  an error that satisfies `errors.Is(err, types.ErrBackendTypeRegistered)`.
- **Factory config.** `AVRConfigKeyDevice` (`"device"`) must hold a
  `*tfw.Device` (a `*TCPDevice`'s embedded `Device`). `AVRConfigKeyConfig`
  (`"config"`) may hold a `*avr.Config`. Anything else is refused.
- **Surface.** The backend is a `types.KeyProvider` and a
  `types.AttestingKeyProvider`. `AttestKey` (step 11), `PossessionSigner`
  (step 13), `AttestLevel` from the crypto tier, and `Algorithms` from the
  device's bitmap. It never owns the link: closing the backend leaves the
  connection to whoever accepted it.
- **End to end.** `xkms/e2e/device_attest_dice_e2e_test.go` enrols the real
  firmware (`tfwsim.StartElement`) through go-xkms's ACME client and server with
  device-attest-01 and a stand-in registry. It also covers the refusals: another
  UEID, another nonce, a CSR for another key or nonce, another provisioned key,
  an unprovisioned device.

## Requirements on other repositories

Each is the owning repository's to accept and implement. Nothing here works
around one.

- **R1 — go-trusted-platform: the device-facing TCP endpoint.** Accepts
  connections the device opens, through `tfw.Listener`. Issues only `CAPS`,
  `ATTEST`, `ATTEST_CERT` and `ATTEST_CSR`. Identifies the device from its
  verified evidence, never its source address. Enrols through device-attest-01.
  The exact row its SPEC needs is in the
  [Ethernet ADR](../ethernet-transport/adr-wire-protocol-over-w5x00.md#6-seam-to-go-trusted-platform).
- **R2 — go-trusted-platform#772: the registry and the resolver.** A registry
  entry from UEID (34 hex digits) to the provisioned DeviceID SPKI, written from
  the provisioning record and never from the enrolment path. The read-only
  resolver go-xkms's verifier consults, saying "no record" distinctly. A refusal
  of a device whose `CAPS` lacks the `ATTEST_CSR` bit. A check after issuance
  that the certified key is the provisioned one.
- **R3 — go-quicbaas: a backend per accepted connection.** The adapter's factory
  takes a live `*tfw.Device`, not a port name. A connection exists only once the
  endpoint accepts it, so no static daemon configuration can describe it (the
  adapter is not in go-xkms's `initializeBackends` for the same reason,
  [xkms-adapter cli](../xkms-adapter/cli.md)). go-quicbaas needs a call that takes
  a backend type and an opaque factory config from go-trusted-platform at accept
  time, constructs the backend through go-xkms's registry, and closes it with
  the connection. The config is a `map[string]interface{}`, so go-quicbaas
  passes `*tfw.Device` through without importing `sdk/go` and keeps no firmware
  dependency.
- **R4 — go-xkms: a DICE device seam for the ACME client.** The ACME client
  reaches a device through `client.TPMModule` (`DeviceIdentity`,
  `CreateAttestedKey`). Turning a DICE backend into one, by encoding the alias
  certificate as `dice` evidence and pairing it with the possession signer, is
  generic. Today it exists only as test glue in this repo (`diceElement` in
  `xkms/e2e`). go-xkms should own that: a generic DICE attested-key source over
  `types.AttestingKeyProvider`, plus an interface for the possession signer it
  declares and the adapter implements. Otherwise go-trusted-platform writes the
  glue, or this repo ships it, and either is a second copy of go-xkms's ACME
  device seam.
- **R5 — go-xkms#1441: the verifier and the CSR builder.** The registry-anchored
  DICE verifier, the possession-CSR profile and its builder, pinned to
  `TFWV_CSR_CRI`. The e2e above runs them at go-xkms v0.53.0-alpha.

## This repository's own open items

- **T1 — a structured provisioning record. Done:** `tfwprov --record <file>`.
  `--bake-eeprom` starts the record with the serial as an RFC 9711 §4.2.1 `ueid`;
  `--commit` or `--verify` joins the DeviceID point as an RFC 7800 §3.2 `cnf`
  `jwk` (RFC 7517; EC P-256, RFC 7518 §6.2.1). A record of another serial or
  another point is refused, never overwritten. It closes follow-up 6 of the
  [possession ADR](../dice-attestation/adr-possession-at-acme-finalize.md);
  go-trusted-platform ingests it for R2
  ([device-provisioning CLI](../device-provisioning/cli.md#the-provisioning-record)).
  Open: the link between the serial and the element is the operator's until an
  authenticated read of the platform block is used.
- **T2 — no command line writes the network block. Done:**
  `tfwctl netcfg set` writes it and `tfwctl netcfg get` reads it back
  ([ethernet user guide](../ethernet-transport/user.md),
  [tfwctl reference](../host-sdk/cli.md)).
- **T3 — the ATmega328P cannot enrol.** Its element-tier Ethernet image is over
  its flash budget, and its profile leaves `ATTEST_CSR` out. The owner's choice
  is pending ([the 328P budget](../ethernet-transport/adr-wire-protocol-over-w5x00.md#the-328p-budget)).
- **T4 — the ATECC608C slot map.** Step 3 waits on the owner's slot map for the
  pre-provisioned part.
- **T5 — application measurements.** The SPEC row has the evidence carry the
  application's measurement beside the firmware's, and the device profile is
  matched from it. The alias certificate still carries one measurement, claim
  273, and it is now the application's: the 32 bytes a running application's
  `tfw_app_evidence` hook supplies, or the firmware hash `tfw-boot` verified
  where none runs or supplies any
  ([dice-attestation/api.md](../dice-attestation/api.md)).
  The firmware's own measurement stays in `ATTEST`'s `FWHash`. Matching the
  profile from 273 is go-trusted-platform's; carrying the firmware's
  measurement beside the application's in one certificate (submodules) is a
  follow-up ([application-sdk](../application-sdk/architecture.md)).

## Open questions for the owner

1. **Which binary links the adapter.** The adapter registers itself when
   imported. go-trusted-platform never imports it (SPEC), and go-quicbaas has no
   firmware dependency. So the deployment's composition root, the binary that
   wires go-quicbaas for go-trusted-platform, must import it. Whose repository
   holds that binary?
2. **R4's home.** Confirm that the DICE device seam for the ACME client is
   go-xkms's. It is generic, but the only implementation today is this repo's
   test glue.
3. **The SPEC row's Detail column** still names `docs/xkms-adapter/`. Should it
   name this folder? Not changed here; the row is `future`.

## Trust boundaries

- **The network is untrusted, and no TLS runs on the device.** The evidence and
  the possession signature are signed by the element and bound to the
  verifier's nonce, so a network attacker can delay or drop them but not forge
  or replay them. What crosses the wire carries no secret
  ([rejected: TLS on the device](../ethernet-transport/adr-wire-protocol-over-w5x00.md#rejected-alternatives)).
- **The network cannot reconfigure or re-provision the device.** `EEPROM_WRITE`
  and `PROV_*` are refused on Ethernet, and `PROV_*` has no handler in a
  production image at all.
- **A self-signed alias certificate vouches for nothing on its own.** Enrolment
  is anchored in the provisioning record, which comes from the bench. A device
  with no registry entry is refused however genuine its evidence.

## What this deliberately does not do

- **No enrolment, registry, profile or verification code.** Those are
  go-trusted-platform's and go-xkms's; this page names the seam.
- **No ACME client on the device, no DHCP yet, no listening port.** The device
  dials one provisioned endpoint and answers five commands, `APP_INFO` only
  in an image with an application.
- **No new command, status, vector or `sdk/go` call.** Every row above cites what
  already ships.
