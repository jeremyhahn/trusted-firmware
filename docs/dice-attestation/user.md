# DICE attestation — user guide

> **Status: NORMATIVE** · Scope: arduino-generic · Last verified: 2026-09-23
> Conflicts with this document are defects. Fix them here first.

Task-oriented. For the design and the wire detail behind each task, see
`architecture.md`, `engineering.md` and `api.md` in this folder.

## Find out what root of trust a device has

Before trusting anything a device signs, ask it what it is:

```
agctl /dev/ttyACM0 caps
```

Read `root of trust` (`AG_CRYPTO_NONE`/`SOFTWARE`/`SECURE_ELEMENT`) and
`algorithms`. A `SOFTWARE`-tier device signs with HMAC-SHA256 only, and the
verifier holds that key — it proves identity to a gateway it already trusts,
never to a stranger. Only `SECURE_ELEMENT` produces a certificate at all.

## Build a firmware image with attestation enabled

`AG_FEATURE_ATTEST` and `AG_FEATURE_ALIAS_CERT` default to **off**. For the
element-backed image:

```
make firmware-attested          # CRYPTO=atecc608a, both features on
```

For the software tier, build with `CRYPTO=soft` (the AVR default) and set
`AG_FEATURE_ATTEST=1` explicitly; `AG_FEATURE_ALIAS_CERT` has nothing to
certify on that tier and stays off.

## Provision a device before its evidence means anything

`ATTEST` and `ATTEST_CERT` both refuse (`AG_ERR_NOT_PROVISIONED`) until the
element holds a DeviceID key and a UDS. That is a separate, one-time,
irreversible procedure — run it on a spare part first:

```
agprov --dry-run  --config board.yaml /dev/ttyACM0   # see the plan, write nothing
agprov --commit --yes-i-mean-it --config board.yaml /dev/ttyACM0
agprov --verify   --config board.yaml /dev/ttyACM0   # confirm afterward
```

Full procedure and failure modes: `docs/device-provisioning/user.md`. There is
the verb is `agctl attest` — see `cli.md`.

## Get freshness-bound evidence for a local verifier

`ATTEST`/`Device.Attest` is for a verifier that already trusts this
product's identity model and wants a compact, signed statement now — e.g. a
gateway checking a device before relaying its data. There is no `agctl`
verb; call the SDK:

```go
nonce := make([]byte, ag.AttestNonceLen) // 16 bytes, freshly random per challenge
evidence, err := dev.Attest(ctx, nonce)
```

`evidence` is the 164-byte frame in `api.md`. Verify the nonce you sent
appears at offset 1 and that the signature (offset 74, algorithm named at
offset 73) validates against the device's known public key — key
distribution and trust decisions are the verifier's, not this repo's.

## Enrol a device with a DICE-aware CA or fleet manager

`ATTEST_CERT`/`Device.AttestCert` mints a full X.509 alias certificate
carrying the challenge inside `tcg-dice-UCCS-evidence`, for a verifier that
wants the spec-shaped artifact rather than the compact frame — e.g. an ACME
`device-attest-01` enrolment:

```go
nonce := sha256(keyAuthorization) // the host hashes; the device never sees keyAuthorization
cert, err := dev.AttestCert(ctx, nonce) // ag.AliasNonceLen == 32 bytes
```

`cert` is DER, ready to hand to whatever verifies it. This repo does not
verify certificates or run enrolment policy — that is go-xkms's and
go-trusted-platform's job; hand the bytes across that boundary unmodified.

## Read the failure a device gives you

| You see | It means |
|---|---|
| `AG_ERR_UNKNOWN_CMD` | this build has `AG_FEATURE_ATTEST`/`AG_FEATURE_ALIAS_CERT` off, or you are talking to a provisioning image |
| `AG_ERR_NOT_PROVISIONED` | the element is present but has no UDS/DeviceID yet — run `agprov --commit` |
| `AG_ERR_UNSUPPORTED` from `ATTEST_CERT` | this device's tier has no public key to certify (software tier), or this board has no room for the certificate builder |
| `AG_ERR_RANGE` while paging | you asked for a page past the document's end, or reused a page index after sending a new nonce |

## Known limitation: the measurement fields are not live yet

`DeviceID` and `FWHash` inside the evidence and the certificate's `ueid`/
`measurements` claims are currently zero-filled — the trusted bootloader
that supplies a real measurement is not wired up (`architecture.md`).
Signing, paging and the wire shapes are exercised and correct; do not build
a verifier today that expects those two fields to carry a real firmware
identity.
