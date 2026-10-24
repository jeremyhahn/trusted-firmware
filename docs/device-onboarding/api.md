# Device onboarding — API

> **Status: DRAFT** · Scope: trusted-firmware · Issue: #53 · Last verified: 2026-10-19
>
> What a device presents to go-trusted-platform, through go-xkms, when it
> onboards. Every shape here is defined by a standard or by a page it links;
> this page adds no command, field or vector of its own.

See [`architecture.md`](architecture.md) for the sequence and who owns each
step, [`user.md`](user.md) for the operator's path, and
[`engineering.md`](engineering.md) for the tests that pin it.

## Two artefacts, two moments

| Artefact | Leaves the device | Written by | Read by |
|---|---|---|---|
| the provisioning record | once, on the bench (steps 2–4) | `tfwprov --record` | go-trusted-platform's device registry (go-trusted-platform#772) |
| the alias certificate and its UCCS claims | at every enrolment and re-attestation (steps 11, 15) | the firmware, `ATTEST_CERT` (`0x63`) | go-xkms's DICE verifier, which resolves the UEID through go-trusted-platform |
| the possession signature | at ACME finalize (step 13) | the firmware, `ATTEST_CSR` (`0x64`) | go-xkms's CSR builder and CA |

Enrolment is the two meeting: the verifier accepts the alias certificate only
when its UEID and its key are the pair the record registered.

## The provisioning record

A JSON claims set of two registered claims, written by `tfwprov` and specified
in [device-provisioning CLI](../device-provisioning/cli.md#the-provisioning-record),
which is normative for it. In summary:

| Member | Standard | Value |
|---|---|---|
| `ueid` | RFC 9711 §4.2.1 ([`rfc9711.txt`](../reference/rfc9711.txt)), base64url per §7.2.2 | type byte `0x01` (RAND), then the 16-byte serial `--bake-eeprom` wrote into the platform block |
| `cnf.jwk` | RFC 7800 §3.2 ([`rfc7800.txt`](../reference/rfc7800.txt)), RFC 7517 ([`rfc7517.txt`](../reference/rfc7517.txt)) | the DeviceID public key the element generated: `kty` `EC`, `crv` `P-256`, `x`, `y` per RFC 7518 §6.2.1 ([`rfc7518.txt`](../reference/rfc7518.txt)) |

The record carries no member of this repo's own, and no RFC 7638 thumbprint.
Re-encoding it as the registry's hex UEID and DER SPKI is go-trusted-platform's.

## The alias certificate and the UCCS claims

`ATTEST_CERT` returns an X.509 certificate in the shape TCG DICE Attestation
Architecture r23 defines (title and URL in
[`../reference/README.md`](../reference/README.md)), with one critical
`tcg-dice-UCCS-evidence` extension (`2.23.133.5.4.6`). Its subject key is the
DeviceID point, the same point as the record's `cnf.jwk`. The exact certificate,
the 151-byte claims set and the CoSWID are in
[dice-attestation API](../dice-attestation/api.md) (`ATTEST_CERT` — 0x63);
the claims that tie it to the record are:

| Claim | Key | Value | Ties to |
|---|---|---|---|
| `nonce` | 10 | the 32-byte `b`, `SHA-256(keyAuthorization)` | the ACME challenge (go-xkms) |
| `ueid` | 256 | `0x01` ‖ the 16-byte serial | the record's `ueid` |
| `measurements` | 273 | one CoSWID evidence tag: the 32 bytes a running application's `tfw_app_evidence` hook supplies, or the firmware hash `tfw-boot` verified where none does (T5 in [architecture.md](architecture.md#this-repositorys-own-open-items)) | the device profile (go-trusted-platform) |

The chain today is the self-signed alias certificate alone: issuer
`CN=tfw DeviceID`, signed by the DeviceID key. What anchors it is the record,
not an issuer ([trust boundaries](architecture.md#trust-boundaries)).

## The possession signature

`ATTEST_CSR` returns 64 bytes, `r‖s`, P-256 over the SHA-256 of a 365-byte
PKCS#10 `CertificationRequestInfo` the device builds from its own template,
with the UEID as an RFC 4043 ([`rfc4043.txt`](../reference/rfc4043.txt)) `permanentIdentifier` in the subjectAltName and the
same UCCS claims set
([dice-attestation API](../dice-attestation/api.md), `ATTEST_CSR` — 0x64). It exists
only on the ATmega2560's element tier (`CAPS` bit 15).

## How go-xkms reaches them

The device is never addressed by go-xkms directly: the `xkms` adapter module
registers backend type `tfw` with go-xkms's registry and turns those commands
into `types.AttestingKeyProvider` calls (`AttestKey`, `PossessionSigner`). Its
configuration keys and errors are in [xkms-adapter API](../xkms-adapter/api.md).

## Not specified here

| What | Whose |
|---|---|
| DICE certificate profiles, the alias certificate's verification, the CSR profile and its builder, device-attest-01 and its nonce | go-xkms (go-xkms#1441) |
| the device registry entry built from the record, the resolver, the device-facing endpoint, profile match and fleet policy | go-trusted-platform (go-trusted-platform#772, R1 and R2 in [architecture.md](architecture.md#requirements-on-other-repositories)) |
| composing a backend per accepted connection | go-quicbaas (R3) |
