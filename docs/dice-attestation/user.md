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

## Produce real DICE evidence with no hardware

`agsim-atecc608a` is the firmware with the real ATECC608A driver and provider,
talking to the software model of the part. Started with an identity it answers
`ATTEST` and `ATTEST_CERT` over the wire for a nonce **you** supply, exactly as
a device does:

```sh
make agsim-atecc608a       # same as: make agsim CRYPTO=atecc608a
```

From Go, in a test (`go-xkms` `pkg/backend/avr`, `go-trusted-platform`
`pkg/firmware/avr`, or this repo's own):

```go
import "github.com/jeremyhahn/arduino-generic/sdk/go/agsim"

dev := agsim.StartElement(t) // provisioned with the test identity below
der, err := dev.AttestCert(ctx, nonce32)   // UCCS alias certificate
ev, err := dev.Attest(ctx, nonce16)        // 164-byte evidence frame
```

A binary that is not built fails the test with the command that builds it; it
never skips. Where binaries are looked up, and the full API, is in
`docs/host-sdk/api.md`.

### The test identity

| Source of identity | What the simulator is given | Where it comes from |
|---|---|---|
| Platform EEPROM block (DeviceID / UEID) | serial `agsim-test-id-01` (16 bytes) | `ag.BuildPlatformBlock`, the builder `agprov --bake-eeprom` uses |
| Element | the TEST DeviceID key in slot 0 and a TEST UDS | `--provision-element` seeds the model directly |
| ag-boot handoff (FWHash, CDI) | FWHash = SHA-256 of `agsim-test-firmware-image-v1` | `--image-hash`, built by `boot/boot_handoff.c`'s `ag_boot_handoff_build` |

Everything is off by default: `agsim-atecc608a` with no options is a blank part
and attestation answers `AG_ERR_NOT_PROVISIONED` (`agsim.StartBlankElement`).
To use your own identity, hand it the same artefacts a real device has:

```sh
agprov --bake-eeprom --serial 00112233445566778899aabbccddeeff --out dev.eep
build/host/agsim-atecc608a --pty --eeprom dev.eep --provision-element \
    --image-hash "$(sha256sum app.bin | cut -d' ' -f1)"
```

`--eeprom FILE` reads Intel HEX (what `agprov --bake-eeprom` and avrdude write);
`--eeprom-bin FILE` a raw image. A bad option or an unreadable file stops the
simulator with exit status 2 and a message naming it; nothing is skipped.

### The DeviceID test key, and verifying under it

The model does real P-256 (the vendored micro-ecc v1.1,
`test/third_party/micro-ecc/`), so the evidence **verifies**. `--provision-element`
installs a deterministic key so a test can pin it:

| | |
|---|---|
| Private key | `SHA-256("agsim-atecc608a TEST DeviceID key v1")` (ASCII, no terminator), `0df3d0b553ec7486fc43dd08763416cf0feee11c46a163fdaff28ea75cf9f76d` |
| Public point `X` | `41fbd5657867813d482f3b4519638191109bf4708e3b9cb518e866f3a78dbf09` |
| Public point `Y` | `a3a696b6af162bda303d1bde9030379186a3a5c82ed8439bc433c9305d4daea4` |

In Go, `agsim.TestDeviceIDPublicKey()` returns it. Per
[`architecture.md`](architecture.md) the DeviceID key signs both artefacts and is
the alias certificate's subject key, so one public key checks both:

```go
cert, _ := x509.ParseCertificate(der)
err := cert.CheckSignature(cert.SignatureAlgorithm, cert.RawTBSCertificate, cert.Signature)

digest := sha256.Sum256(ev[:73])           // everything before the algorithm byte
r := new(big.Int).SetBytes(ev[74:106])
s := new(big.Int).SetBytes(ev[106:138])
ok := ecdsa.Verify(agsim.TestDeviceIDPublicKey(), digest[:], r, s)
```

Signing is deterministic (an HMAC-DRBG `k` in the style of RFC 6979, micro-ecc's
variant), so the same nonce gives the same signature; the real part's `k` is
random.

The operator-signature auth session is compiled into this binary, so it can be
exercised too: `agsim.StartElementWithOperator(t, &opKey.PublicKey)` bakes the
operator's public key into the platform block, and `Device.AuthResponse` with
the matching private key opens the session. The element verifies for real, so a
signature from another key, for another serial, or over a stale nonce is refused.

### Limits

**THIS IDENTITY IS A TEST IDENTITY AND MUST NEVER BE TRUSTED.** The evidence
verifies, which is exactly why this matters: the private key above is the hash of
a published string, so anyone can compute it and sign as this device. A passing
signature check proves the bytes are well formed and were signed by that key; it
proves nothing about any device. **Never enrol, allow-list or pin this key
outside a test that supplied it.** The serial `agsim-test-id-01` and the FWHash
of `agsim-test-firmware-image-v1` are likewise fixed and public.

Use it to test paging, parsing, signature verification, nonce binding, the UEID
and measurement claims, the auth session and error handling. Not modelled:
element timing, Verify in Stored mode (the bootloader's image check, answered
from a flag), ECDH or any KDF (the firmware uses neither), a real TRNG
(`Random` is a fixed sequence that differs call to call), and the provisioning
commands (a jig image concern; the element is seeded instead).

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
